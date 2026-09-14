#include <cassert>
#include <filesystem>
#include <iostream>
#include <vector>

#include "catalog/catalog.h"
#include "live/collector.h"
#include "query/kway_merge.h"
#include "query/query_engine.h"
#include "schemas/base.h"
#include "schemas/book_level.h"
#include "schemas/order_event.h"
#include "schemas/quote.h"
#include "schemas/spatial_concept.h"
#include "schemas/trade.h"
#include "spatial/rtree.h"
#include "spatial/sidecar.h"
#include "storage/parquet_reader.h"
#include "storage/parquet_writer.h"

void test_generic_nd_rtree_5d_trade() {
    std::cout << "[TEST ND-1] Testing 5-Dimensional Schema-Driven Trade R-Tree Indexing...\n";
    std::string test_parquet = "/tmp/trade_5d.parquet";
    std::string test_index = "/tmp/trade_5d.index";

    std::vector<tick_db::Trade> trades;
    for (uint64_t i = 1; i <= 2000; ++i) {
        trades.push_back({1000000 + i * 10, i, static_cast<int64_t>(15000 + (i % 200)), static_cast<int64_t>(100 + i),
                          tick_db::Side::Bid});
    }

    assert(tick_db::ParquetWriter::write_trades(test_parquet, trades, 64));

    tick_db::TypedRTree<tick_db::Trade> rtree;
    rtree.insert_row_group(trades, 0, test_parquet);

    assert(tick_db::SidecarIndex::write_index(test_index, rtree.inner()));

    tick_db::HierarchicalRTree restored;
    assert(tick_db::SidecarIndex::read_index(test_index, restored));

    tick_db::GenericMBR query_mbr = tick_db::extract_mbr(trades[0]);
    query_mbr.min_bounds = {1000100, 10, 15010, 105, 66};
    query_mbr.max_bounds = {1001000, 100, 15050, 200, 66};

    tick_db::QueryMetrics metrics;
    auto results = tick_db::QueryEngine::execute_trade_query(test_parquet, test_index, query_mbr, &metrics);

    assert(!results.empty());
    std::cout << "  - 5D Trade MBR Query Returned: " << results.size() << " records: PASSED\n";
}

void test_generic_nd_rtree_7d_order_event() {
    std::cout << "\n[TEST ND-2] Testing 7-Dimensional Schema-Driven OrderEvent L3 R-Tree Indexing...\n";
    std::string test_index = "/tmp/order_event_7d.index";

    tick_db::OrderEvent ev{1000000, 1, 999000000, tick_db::OrderEventType::Add, tick_db::Side::Bid, 15000, 100};
    tick_db::TypedRTree<tick_db::OrderEvent> rtree;
    rtree.insert_row_group(std::span<const tick_db::OrderEvent>(&ev, 1), 0, "/data/l3_order_events.parquet");

    assert(tick_db::SidecarIndex::write_index(test_index, rtree.inner()));

    tick_db::HierarchicalRTree restored;
    assert(tick_db::SidecarIndex::read_index(test_index, restored));
    assert(restored.total_leaf_entries() == 1);

    tick_db::GenericMBR match_query = tick_db::extract_mbr(ev);
    auto matches = restored.search(match_query);
    assert(matches.size() == 1);

    // Test pruning on order_id dimension (dim index 2)
    tick_db::GenericMBR mismatch_query = match_query;
    mismatch_query.min_bounds[2] = 100000000;  // Outside order_id range
    mismatch_query.max_bounds[2] = 200000000;
    auto no_matches = restored.search(mismatch_query);
    assert(no_matches.empty());  // Pruned on order_id dimension!

    std::cout << "  - 7D OrderEvent R-Tree Multi-Dimensional Pruning: PASSED\n";
}

struct CustomSymbolTrade {
    std::string symbol;
    uint64_t ts_exchange_ns{0};
    int64_t price{0};
    int64_t size{0};

    auto spatial_tuple() const { return std::tie(symbol, ts_exchange_ns, price, size); }
};

void test_string_symbol_indexing() {
    std::cout << "\n[TEST ND-3] Testing String Symbol Auto-Hash R-Tree Indexing...\n";
    CustomSymbolTrade t1{"AAPL", 1000000, 15000, 100};
    CustomSymbolTrade t2{"MSFT", 1000000, 25000, 200};

    tick_db::TypedRTree<CustomSymbolTrade> rtree;
    rtree.insert_row_group(std::span<const CustomSymbolTrade>(&t1, 1), 0, "/data/aapl.parquet");
    rtree.insert_row_group(std::span<const CustomSymbolTrade>(&t2, 1), 1, "/data/msft.parquet");

    auto aapl_query = tick_db::extract_mbr(t1);
    auto matches = rtree.search(aapl_query);
    assert(matches.size() == 1);
    assert(matches[0].filepath == "/data/aapl.parquet");

    std::cout << "  - String Symbol (\"AAPL\" vs \"MSFT\") Spatial Pruning: PASSED\n";
}

int main() {
    std::cout << "=======================================================\n";
    std::cout << " Running Generic N-Dimensional R-Tree Architecture Tests\n";
    std::cout << "=======================================================\n";

    test_generic_nd_rtree_5d_trade();
    test_generic_nd_rtree_7d_order_event();
    test_string_symbol_indexing();

    std::cout << "\n=======================================================\n";
    std::cout << " ALL GENERIC N-DIMENSIONAL R-TREE TESTS PASSED! \n";
    std::cout << "=======================================================\n";

    return 0;
}
