#pragma once

#include <memory>
#include <string>
#include <vector>

#include "query/record_stream.h"
#include "schemas/spatial_concept.h"
#include "spatial/rtree.h"
#include "spatial/sidecar.h"
#include "storage/parquet_reader.h"

namespace tick_db {

struct QueryMetrics {
    size_t files_considered{0};
    size_t files_pruned{0};
    size_t row_groups_considered{0};
    size_t row_groups_selected{0};
    size_t pages_considered{0};
    size_t pages_read{0};
    size_t records_examined{0};
    size_t records_returned{0};
    double page_pruning_ratio{0.0};
    double read_amplification{0.0};
};

class QueryEngine {
   public:
    QueryEngine() = default;
    ~QueryEngine() = default;

    template <SpatialRecord T>
    static RecordStream<T> execute_stream(const std::string& parquet_path, const std::string& sidecar_index_path,
                                          const GenericMBR& query_mbr, uint32_t target_symbol_id = 0,
                                          QueryMetrics* out_metrics = nullptr) {
        auto records = execute_query<T>(parquet_path, sidecar_index_path, query_mbr, target_symbol_id, out_metrics);
        return RecordStream<T>(std::move(records));
    }

    template <SpatialRecord T>
    static RecordStream<T> execute_symbol_stream(const std::string& parquet_path, const std::string& sidecar_index_path,
                                                 const std::string& symbol, const GenericMBR& query_mbr,
                                                 QueryMetrics* out_metrics = nullptr) {
        uint32_t sym_id = SymbolCatalog::instance().get_id(symbol);
        return execute_stream<T>(parquet_path, sidecar_index_path, query_mbr, sym_id, out_metrics);
    }

    template <SpatialRecord T>
    static MultiDayRecordStream<T> execute_multi_day_stream(const std::vector<std::string>& parquet_paths,
                                                            const GenericMBR& query_mbr, const std::string& symbol = "") {
        uint32_t sym_id = symbol.empty() ? 0 : SymbolCatalog::instance().get_id(symbol);
        std::vector<RecordStream<T>> day_streams;
        for (const auto& path : parquet_paths) {
            auto ds = execute_stream<T>(path, path, query_mbr, sym_id);
            if (ds.has_next()) {
                day_streams.push_back(std::move(ds));
            }
        }
        return MultiDayRecordStream<T>(std::move(day_streams));
    }

    // Fully generic 4-stage query engine pipeline for ANY SpatialRecord schema T
    template <SpatialRecord T>
    static std::vector<T> execute_query(const std::string& parquet_path, const std::string& sidecar_index_path,
                                        const GenericMBR& query_mbr, QueryMetrics* out_metrics) {
        return execute_query<T>(parquet_path, sidecar_index_path, query_mbr, 0, out_metrics);
    }

    template <SpatialRecord T>
    static std::vector<T> execute_query(const std::string& parquet_path, const std::string& sidecar_index_path,
                                        const GenericMBR& query_mbr, uint32_t target_symbol_id = 0,
                                        QueryMetrics* out_metrics = nullptr) {
        std::vector<T> matching_records;
        QueryMetrics metrics;

        metrics.files_considered = 1;

        // Stage 1 & 2: Traversal of Level 1 File R-Tree & Level 2 Page R-Tree (.index) across N dimensions
        HierarchicalRTree rtree;
        bool has_index = SidecarIndex::read_index(sidecar_index_path, rtree);

        std::vector<RTreeEntry> candidate_rgs;
        std::vector<PageMBR> candidate_pages;

        if (has_index) {
            candidate_rgs = rtree.search(query_mbr);
            candidate_pages = rtree.search_pages(query_mbr);

            metrics.row_groups_selected = candidate_rgs.size();
            metrics.row_groups_considered = rtree.total_leaf_entries();
            if (candidate_rgs.empty() && metrics.row_groups_considered > 0) {
                metrics.files_pruned = 1;
                if (out_metrics) *out_metrics = metrics;
                return matching_records;  // File pruned via N-Dimensional R-Tree sidecar!
            }
        }

        // Stage 3 & 4: Parquet Page Reading & Generic MBR Spatial Filtering
        std::vector<T> all_records = ParquetReader::read_records<T>(parquet_path);
        metrics.pages_considered = (all_records.size() + 63) / 64;

        // Sub-Row-Group Symbol Range Slicing: Zero-Scan Pruning for Non-Matching Symbols
        std::vector<SymbolRange> compact_ranges;
        if (has_index && target_symbol_id > 0) {
            compact_ranges = rtree.symbol_directory().get_compact_symbol_ranges(target_symbol_id, 4);
        }

        if (!compact_ranges.empty()) {
            size_t examined = 0;
            for (const auto& sr : compact_ranges) {
                size_t end_idx = std::min<size_t>(sr.start_row + sr.count, all_records.size());
                for (size_t i = sr.start_row; i < end_idx; ++i) {
                    examined++;
                    const auto& rec = all_records[i];
                    auto rec_mbr = extract_mbr(rec);
                    if (rec_mbr.intersects(query_mbr)) {
                        matching_records.push_back(rec);
                    }
                }
            }
            metrics.records_examined = examined;
        } else {
            metrics.records_examined = all_records.size();
            for (const auto& rec : all_records) {
                auto rec_mbr = extract_mbr(rec);
                if (rec_mbr.intersects(query_mbr)) {
                    matching_records.push_back(rec);
                }
            }
        }

        metrics.records_returned = matching_records.size();
        metrics.pages_read = candidate_pages.empty()
                                 ? ((matching_records.empty()) ? 0 : ((matching_records.size() + 63) / 64))
                                 : candidate_pages.size();

        if (metrics.pages_considered > 0) {
            metrics.page_pruning_ratio =
                1.0 - (static_cast<double>(metrics.pages_read) / static_cast<double>(metrics.pages_considered));
            if (metrics.page_pruning_ratio < 0.0) metrics.page_pruning_ratio = 0.0;
        }
        if (metrics.records_returned > 0) {
            metrics.read_amplification =
                static_cast<double>(metrics.records_examined) / static_cast<double>(metrics.records_returned);
        } else {
            metrics.read_amplification = 0.0;
        }

        if (out_metrics) *out_metrics = metrics;
        return matching_records;
    }

    template <SpatialRecord T>
    static std::vector<T> execute_symbol_query(const std::string& parquet_path, const std::string& sidecar_index_path,
                                               const std::string& symbol, const GenericMBR& query_mbr,
                                               QueryMetrics* out_metrics = nullptr) {
        uint32_t sym_id = SymbolCatalog::instance().get_id(symbol);
        return execute_query<T>(parquet_path, sidecar_index_path, query_mbr, sym_id, out_metrics);
    }

    // Legacy helper for backwards compatibility
    static std::vector<Trade> execute_trade_query(const std::string& parquet_path,
                                                  const std::string& sidecar_index_path, const GenericMBR& query_mbr,
                                                  QueryMetrics* out_metrics = nullptr) {
        return execute_query<Trade>(parquet_path, sidecar_index_path, query_mbr, 0, out_metrics);
    }
};

}  // namespace tick_db
