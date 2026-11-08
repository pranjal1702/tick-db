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
                                          const GenericMBR& query_mbr, const std::vector<std::string>& target_symbols = {},
                                          uint32_t target_symbol_id = 0, Side target_side = Side::None,
                                          QueryMetrics* out_metrics = nullptr) {
        if (target_symbol_id == 0 && target_symbols.size() == 1) {
            target_symbol_id = SymbolCatalog::instance().get_id(target_symbols[0]);
        }

        // Single-I/O open handle: opens file once and keeps handle open for embedded metadata and streaming
        auto reader = ParquetReader::open_file(parquet_path);
        if (!reader) {
            return RecordStream<T>{};
        }

        HierarchicalRTree rtree;
        bool has_index = SidecarIndex::read_index(reader.get(), sidecar_index_path, rtree);

        std::vector<RTreeEntry> candidate_rgs;
        std::vector<PageMBR> candidate_pages;
        size_t total_rgs_in_file = reader->parquet_reader()->metadata()->num_row_groups();

        if (has_index) {
            candidate_rgs = rtree.search(query_mbr, target_symbol_id);
            candidate_pages = rtree.search_pages(query_mbr, target_symbol_id);

            if (candidate_rgs.empty() && rtree.total_leaf_entries() > 0) {
                if (out_metrics) {
                    out_metrics->files_considered = 1;
                    out_metrics->files_pruned = 1;
                    out_metrics->row_groups_considered = rtree.total_leaf_entries();
                    out_metrics->row_groups_selected = 0;
                }
                return RecordStream<T>{};  // File pruned via N-Dimensional R-Tree + Symbol Bitmask!
            }
        }

        std::vector<int> rg_indices;
        if (has_index) {
            rg_indices.reserve(candidate_rgs.size());
            for (const auto& rg : candidate_rgs) {
                rg_indices.push_back(rg.row_group_id);
            }
        } else {
            rg_indices.resize(total_rgs_in_file);
            for (size_t i = 0; i < total_rgs_in_file; ++i) rg_indices[i] = static_cast<int>(i);
        }

        std::vector<SymbolRange> compact_ranges;
        if (has_index && target_symbol_id > 0) {
            compact_ranges = rtree.symbol_directory().get_compact_symbol_ranges(target_symbol_id, 4);
        }

        struct StreamState {
            size_t rg_pos{0};
            size_t records_examined{0};
            size_t records_returned{0};
        };
        auto state = std::make_shared<StreamState>();

        auto check_symbol = [target_symbols, target_symbol_id](const T& rec) -> bool {
            if (!target_symbols.empty()) {
                if constexpr (requires { rec.symbol; }) {
                    return std::find(target_symbols.begin(), target_symbols.end(), rec.symbol) != target_symbols.end();
                }
            }
            if (target_symbol_id > 0) {
                if constexpr (requires { rec.symbol_id; }) {
                    return rec.symbol_id == target_symbol_id;
                }
            }
            return true;
        };

        auto check_side = [target_side](const T& rec) -> bool {
            if (target_side == Side::None) return true;
            if constexpr (requires { rec.side; }) {
                return rec.side == target_side;
            }
            return true;
        };

        auto supplier = [reader, rg_indices, compact_ranges, query_mbr, check_symbol, check_side, state, out_metrics](std::vector<T>& out_batch) -> bool {
            out_batch.clear();
            while (state->rg_pos < rg_indices.size()) {
                int rg_id = rg_indices[state->rg_pos++];
                auto records = ParquetReader::read_records<T>(*reader, {rg_id});
                if (records.empty()) continue;

                // Check for sub-row-group symbol ranges for this row group
                std::vector<SymbolRange> rg_ranges;
                for (const auto& sr : compact_ranges) {
                    if (sr.row_group_id == static_cast<uint32_t>(rg_id)) {
                        rg_ranges.push_back(sr);
                    }
                }

                if (!rg_ranges.empty()) {
                    for (const auto& sr : rg_ranges) {
                        size_t end_idx = std::min<size_t>(sr.start_row + sr.count, records.size());
                        for (size_t i = sr.start_row; i < end_idx; ++i) {
                            state->records_examined++;
                            const auto& rec = records[i];
                            if (!check_symbol(rec)) continue;
                            if (!check_side(rec)) continue;
                            auto rec_mbr = extract_mbr(rec);
                            if (rec_mbr.intersects(query_mbr)) {
                                out_batch.push_back(rec);
                                state->records_returned++;
                            }
                        }
                    }
                } else {
                    for (const auto& rec : records) {
                        state->records_examined++;
                        if (!check_symbol(rec)) continue;
                        if (!check_side(rec)) continue;
                        auto rec_mbr = extract_mbr(rec);
                        if (rec_mbr.intersects(query_mbr)) {
                            out_batch.push_back(rec);
                            state->records_returned++;
                        }
                    }
                }

                if (!out_batch.empty()) {
                    if (out_metrics) {
                        out_metrics->records_examined = state->records_examined;
                        out_metrics->records_returned = state->records_returned;
                    }
                    return true;
                }
            }
            if (out_metrics) {
                out_metrics->records_examined = state->records_examined;
                out_metrics->records_returned = state->records_returned;
            }
            return false;
        };

        if (out_metrics) {
            out_metrics->files_considered = 1;
            out_metrics->files_pruned = 0;
            out_metrics->row_groups_considered = has_index ? rtree.total_leaf_entries() : total_rgs_in_file;
            out_metrics->row_groups_selected = rg_indices.size();
            out_metrics->pages_considered = total_rgs_in_file;
            out_metrics->pages_read = candidate_pages.empty() ? rg_indices.size() : candidate_pages.size();
            if (out_metrics->pages_considered > 0) {
                out_metrics->page_pruning_ratio =
                    1.0 - (static_cast<double>(out_metrics->pages_read) / static_cast<double>(out_metrics->pages_considered));
                if (out_metrics->page_pruning_ratio < 0.0) out_metrics->page_pruning_ratio = 0.0;
            }
        }

        size_t total_matching_count = 0;
        if (!compact_ranges.empty()) {
            for (const auto& sr : compact_ranges) {
                total_matching_count += sr.count;
            }
        } else {
            auto meta = reader->parquet_reader()->metadata();
            for (int rg : rg_indices) {
                if (rg < meta->num_row_groups()) {
                    total_matching_count += meta->RowGroup(rg)->num_rows();
                }
            }
        }

        return RecordStream<T>(total_matching_count, supplier);
    }

    template <SpatialRecord T>
    static RecordStream<T> execute_stream(const std::string& parquet_path, const std::string& sidecar_index_path,
                                          const GenericMBR& query_mbr, uint32_t target_symbol_id,
                                          Side target_side = Side::None,
                                          QueryMetrics* out_metrics = nullptr) {
        return execute_stream<T>(parquet_path, sidecar_index_path, query_mbr, {}, target_symbol_id, target_side, out_metrics);
    }

    template <SpatialRecord T>
    static RecordStream<T> execute_symbol_stream(const std::string& parquet_path, const std::string& sidecar_index_path,
                                                 const std::string& symbol, const GenericMBR& query_mbr,
                                                 Side target_side = Side::None,
                                                 QueryMetrics* out_metrics = nullptr) {
        uint32_t sym_id = SymbolCatalog::instance().get_id(symbol);
        std::vector<std::string> syms = symbol.empty() ? std::vector<std::string>{} : std::vector<std::string>{symbol};
        return execute_stream<T>(parquet_path, sidecar_index_path, query_mbr, syms, sym_id, target_side, out_metrics);
    }

    template <SpatialRecord T>
    static MultiDayRecordStream<T> execute_multi_day_stream(const std::vector<std::string>& parquet_paths,
                                                            const GenericMBR& query_mbr,
                                                            const std::vector<std::string>& target_symbols = {},
                                                            Side target_side = Side::None,
                                                            bool lazy = true) {
        uint32_t sym_id = (target_symbols.size() == 1) ? SymbolCatalog::instance().get_id(target_symbols[0]) : 0;
        
        if (!lazy) {
            std::vector<RecordStream<T>> day_streams;
            for (const auto& path : parquet_paths) {
                auto ds = execute_stream<T>(path, path, query_mbr, target_symbols, sym_id, target_side);
                if (ds.has_next()) {
                    day_streams.push_back(std::move(ds));
                }
            }
            return MultiDayRecordStream<T>(std::move(day_streams));
        }

        // Lazy on-demand stream: opens and scans each partition file ONLY when needed!
        auto generator = [parquet_paths, query_mbr, target_symbols, sym_id, target_side](size_t file_idx) -> RecordStream<T> {
            if (file_idx >= parquet_paths.size()) return RecordStream<T>{};
            const auto& path = parquet_paths[file_idx];
            return execute_stream<T>(path, path, query_mbr, target_symbols, sym_id, target_side);
        };

        return MultiDayRecordStream<T>(parquet_paths.size(), generator);
    }

    // Overload for single symbol string
    template <SpatialRecord T>
    static MultiDayRecordStream<T> execute_multi_day_symbol_stream(const std::vector<std::string>& parquet_paths,
                                                                   const GenericMBR& query_mbr, const std::string& symbol,
                                                                   Side target_side = Side::None,
                                                                   bool lazy = true) {
        std::vector<std::string> syms = symbol.empty() ? std::vector<std::string>{} : std::vector<std::string>{symbol};
        return execute_multi_day_stream<T>(parquet_paths, query_mbr, syms, target_side, lazy);
    }

    template <SpatialRecord T>
    static std::vector<T> execute_query(const std::string& parquet_path, const std::string& sidecar_index_path,
                                        const GenericMBR& query_mbr, QueryMetrics* out_metrics) {
        return execute_query<T>(parquet_path, sidecar_index_path, query_mbr, {}, 0, Side::None, out_metrics);
    }

    template <SpatialRecord T>
    static std::vector<T> execute_query(const std::string& parquet_path, const std::string& sidecar_index_path,
                                        const GenericMBR& query_mbr, uint32_t target_symbol_id,
                                        Side target_side = Side::None,
                                        QueryMetrics* out_metrics = nullptr) {
        return execute_query<T>(parquet_path, sidecar_index_path, query_mbr, {}, target_symbol_id, target_side, out_metrics);
    }

    template <SpatialRecord T>
    static std::vector<T> execute_query(const std::string& parquet_path, const std::string& sidecar_index_path,
                                        const GenericMBR& query_mbr, const std::vector<std::string>& target_symbols = {},
                                        uint32_t target_symbol_id = 0, Side target_side = Side::None,
                                        QueryMetrics* out_metrics = nullptr) {
        QueryMetrics metrics;
        auto stream = execute_stream<T>(parquet_path, sidecar_index_path, query_mbr, target_symbols, target_symbol_id, target_side, &metrics);
        std::vector<T> matching_records;
        while (stream.has_next()) {
            auto batch = stream.fetch_next_batch(4096);
            matching_records.insert(matching_records.end(), batch.begin(), batch.end());
        }
        metrics.records_returned = matching_records.size();
        if (metrics.records_examined == 0) {
            metrics.records_examined = matching_records.size();
        }
        if (metrics.records_returned > 0) {
            metrics.read_amplification = static_cast<double>(metrics.records_examined) / static_cast<double>(metrics.records_returned);
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
        return execute_query<Trade>(parquet_path, sidecar_index_path, query_mbr, {}, 0, Side::None, out_metrics);
    }
};

}  // namespace tick_db
