#include "query/query_engine.h"

#include <iostream>

#include "storage/parquet_reader.h"

namespace tick_db {

std::vector<Trade> QueryEngine::execute_trade_query(const std::string& parquet_path,
                                                    const std::string& sidecar_index_path, const GenericMBR& query_mbr,
                                                    QueryMetrics* out_metrics) {
    std::vector<Trade> matching_trades;
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
            return matching_trades;  // File pruned via N-Dimensional R-Tree sidecar!
        }
    }

    // Stage 3 & 4: Parquet Page Reading & Exact Filtering
    auto table = ParquetReader::read_table(parquet_path);
    if (!table) {
        if (out_metrics) *out_metrics = metrics;
        return matching_trades;
    }

    std::vector<Trade> all_trades = trades_from_columns(*table);
    metrics.records_examined = all_trades.size();
    metrics.pages_considered = (all_trades.size() + 63) / 64;

    for (const auto& t : all_trades) {
        // Evaluate dimensions: [0]=ts_exchange_ns, [1]=seq_no, [2]=price, [3]=size, [4]=side
        bool match = true;
        if (query_mbr.dimensions() > 0 &&
            (t.ts_exchange_ns < query_mbr.min_bounds[0] || t.ts_exchange_ns > query_mbr.max_bounds[0]))
            match = false;
        if (query_mbr.dimensions() > 1 && (t.seq_no < query_mbr.min_bounds[1] || t.seq_no > query_mbr.max_bounds[1]))
            match = false;
        if (query_mbr.dimensions() > 2 && (t.price < query_mbr.min_bounds[2] || t.price > query_mbr.max_bounds[2]))
            match = false;
        if (query_mbr.dimensions() > 3 && (t.size < query_mbr.min_bounds[3] || t.size > query_mbr.max_bounds[3]))
            match = false;

        if (match) {
            matching_trades.push_back(t);
        }
    }

    metrics.records_returned = matching_trades.size();
    metrics.pages_read = candidate_pages.empty()
                             ? ((matching_trades.empty()) ? 0 : ((matching_trades.size() + 63) / 64))
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
    return matching_trades;
}

}  // namespace tick_db
