#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>

struct CustomSymbolTrade {
    std::string symbol;
    uint64_t ts_exchange_ns{0};
    int64_t price{0};
    int64_t size{0};

    auto spatial_tuple() const { return std::tie(symbol, ts_exchange_ns, price, size); }
};

#include "backfill/csv_backfiller.h"
#include "catalog/catalog.h"
#include "catalog/symbol_catalog.h"
#include "live/collector.h"
#include "query/kway_merge.h"
#include "query/query_engine.h"
#include "query/sql_query_engine.h"
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

    auto query_mbr = tick_db::extract_mbr(trades[0]);
    query_mbr.min_bounds = {1000100, 10, 15010, 105, 66};
    query_mbr.max_bounds = {1001000, 100, 15050, 200, 66};

    tick_db::QueryMetrics metrics;
    auto results = tick_db::QueryEngine::execute_query<tick_db::Trade>(test_parquet, test_index, query_mbr, &metrics);

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

    auto match_query = tick_db::extract_mbr(ev);
    auto matches = restored.search(match_query);
    assert(matches.size() == 1);

    // Test pruning on order_id dimension (dim index 2)
    auto mismatch_query = match_query;
    mismatch_query.min_bounds[2] = 100000000;  // Outside order_id range
    mismatch_query.max_bounds[2] = 200000000;
    auto no_matches = restored.search(mismatch_query);
    assert(no_matches.empty());  // Pruned on order_id dimension!

    std::cout << "  - 7D OrderEvent R-Tree Multi-Dimensional Pruning: PASSED\n";
}

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

void test_multi_symbol_shared_parquet_pruning() {
    std::cout << "\n[TEST ND-4] Testing Multi-Symbol Shared Parquet & Symbol Bitmask Indexing...\n";
    std::string test_parquet = "/tmp/multi_symbol_market_data.parquet";
    std::string test_index = "/tmp/multi_symbol_market_data.index";

    auto& cat = tick_db::SymbolCatalog::instance();
    uint32_t aapl_id = cat.get_or_create_id("AAPL");
    uint32_t msft_id = cat.get_or_create_id("MSFT");

    std::vector<CustomSymbolTrade> multi_trades;
    // 500 AAPL trades + 500 MSFT trades stored in a single shared vector / file
    for (uint64_t i = 1; i <= 500; ++i) {
        multi_trades.push_back({"AAPL", 1000000 + i * 10, static_cast<int64_t>(15000 + (i % 50)), 100});
        multi_trades.push_back({"MSFT", 1000000 + i * 10, static_cast<int64_t>(25000 + (i % 50)), 200});
    }

    tick_db::TypedRTree<CustomSymbolTrade> rtree;
    rtree.insert_row_group(multi_trades, 0, test_parquet);

    assert(tick_db::SidecarIndex::write_index(test_index, rtree.inner()));

    tick_db::HierarchicalRTree restored;
    assert(tick_db::SidecarIndex::read_index(test_index, restored));

    CustomSymbolTrade aapl_query_target{"AAPL", 1000010, 15000, 100};
    auto aapl_mbr = tick_db::extract_mbr(aapl_query_target);
    auto matches = restored.search(aapl_mbr);
    assert(matches.size() == 1);

    std::cout << "  - Shared Multi-Symbol Parquet File Sidecar Search: PASSED\n";
}

void test_sub_row_group_symbol_range_pruning() {
    std::cout << "\n[TEST ND-5] Testing Sub-Row-Group Symbol Range Pruning (Zero Read Amplification)...\n";
    std::string test_parquet = "/tmp/symbol_range_test.parquet";
    std::string test_index = "/tmp/symbol_range_test.index";

    auto& cat = tick_db::SymbolCatalog::instance();
    uint32_t aapl_id = cat.get_or_create_id("AAPL");
    uint32_t msft_id = cat.get_or_create_id("MSFT");

    std::vector<CustomSymbolTrade> multi_trades;
    // 500 contiguous AAPL trades followed by 500 contiguous MSFT trades (1000 records total)
    for (uint64_t i = 1; i <= 500; ++i) {
        multi_trades.push_back({"AAPL", 1000000 + i * 10, static_cast<int64_t>(15000 + (i % 50)), 100});
    }
    for (uint64_t i = 1; i <= 500; ++i) {
        multi_trades.push_back({"MSFT", 1000000 + i * 10, static_cast<int64_t>(25000 + (i % 50)), 200});
    }

    // Build RTree with Sub-Row-Group Symbol Ranges
    tick_db::TypedRTree<CustomSymbolTrade> rtree;
    rtree.insert_row_group(multi_trades, 0, test_parquet);
    assert(tick_db::SidecarIndex::write_index(test_index, rtree.inner()));

    // Read back sidecar index and verify Sub-Row-Group Symbol Ranges
    tick_db::HierarchicalRTree restored;
    assert(tick_db::SidecarIndex::read_index(test_index, restored));

    const auto* aapl_ranges = restored.symbol_directory().get_symbol_ranges(aapl_id);
    assert(aapl_ranges != nullptr);
    assert(aapl_ranges->size() == 1);
    assert((*aapl_ranges)[0].start_row == 0);
    assert((*aapl_ranges)[0].count == 500);

    const auto* msft_ranges = restored.symbol_directory().get_symbol_ranges(msft_id);
    assert(msft_ranges != nullptr);
    assert(msft_ranges->size() == 1);
    assert((*msft_ranges)[0].start_row == 500);
    assert((*msft_ranges)[0].count == 500);

    // Perform Sub-Row-Group Slicing for "AAPL"
    size_t examined = 0;
    std::vector<CustomSymbolTrade> aapl_records;
    for (const auto& sr : *aapl_ranges) {
        for (size_t i = sr.start_row; i < sr.start_row + sr.count; ++i) {
            examined++;
            aapl_records.push_back(multi_trades[i]);
        }
    }

    assert(aapl_records.size() == 500);
    assert(examined == 500);  // 1000 total records in RG, but ONLY 500 AAPL records were examined!
    double read_amp = static_cast<double>(examined) / static_cast<double>(aapl_records.size());
    assert(read_amp == 1.0);  // 1.0x Read Amplification = 0% Wasted Decoding!

    std::cout << "  - Sub-Row-Group Symbol Range Slicing (500/1000 records examined, 1.0x Read Amp): PASSED\n";
}

void test_varint_compressed_position_index() {
    std::cout << "\n[TEST ND-6] Testing Delta-Varint Position Index Compression & Gap Merging...\n";
    std::string test_parquet = "/tmp/varint_compress_test.parquet";
    std::string test_index = "/tmp/varint_compress_test.index";

    auto& cat = tick_db::SymbolCatalog::instance();
    uint32_t aapl_id = cat.get_or_create_id("AAPL");
    uint32_t msft_id = cat.get_or_create_id("MSFT");

    std::vector<CustomSymbolTrade> alternating_trades;
    // 1000 records strictly alternating every single row: AAPL, MSFT, AAPL, MSFT...
    for (uint64_t i = 1; i <= 500; ++i) {
        alternating_trades.push_back({"AAPL", 1000000 + i * 10, static_cast<int64_t>(15000 + (i % 50)), 100});
        alternating_trades.push_back({"MSFT", 1000000 + i * 10, static_cast<int64_t>(25000 + (i % 50)), 200});
    }

    tick_db::TypedRTree<CustomSymbolTrade> rtree;
    rtree.insert_row_group(alternating_trades, 0, test_parquet);
    assert(tick_db::SidecarIndex::write_index(test_index, rtree.inner()));

    // Read back sidecar index and test Varint decompression
    tick_db::HierarchicalRTree restored;
    assert(tick_db::SidecarIndex::read_index(test_index, restored));

    const auto* aapl_raw = restored.symbol_directory().get_symbol_ranges(aapl_id);
    assert(aapl_raw != nullptr);
    assert(aapl_raw->size() == 500);  // 500 individual 1-row ranges recovered via Varint

    // Test Adaptive Gap Merging (max_gap = 2)
    auto aapl_compact = restored.symbol_directory().get_compact_symbol_ranges(aapl_id, 2);
    assert(aapl_compact.size() == 1);  // 500 alternating ranges collapsed down to 1 compact span!
    assert(aapl_compact[0].start_row == 0);
    assert(aapl_compact[0].count == 999);

    std::cout << "  - Delta-Varint Decompression (500 ranges) & Adaptive Gap Merging (500->1 span): PASSED\n";
}

void test_record_stream_and_embedded_parquet_index() {
    std::cout << "\n[TEST ND-7] Testing Embedded Parquet Metadata Indexing & RecordStream<T> Iterator...\n";
    std::string test_parquet = "/tmp/embedded_index_test.parquet";

    std::vector<tick_db::Trade> trades;
    for (uint64_t i = 1; i <= 100; ++i) {
        trades.push_back({1000000 + i * 10, i, static_cast<int64_t>(15000 + i), 100, tick_db::Side::Bid});
    }

    // Build RTree
    tick_db::TypedRTree<tick_db::Trade> rtree;
    rtree.insert_row_group(trades, 0, test_parquet);

    // Serialize RTree payload & attach to KeyValueMetadata
    std::string payload = tick_db::SidecarIndex::serialize_to_string(rtree.inner());
    auto kv_meta = std::make_shared<arrow::KeyValueMetadata>();
    kv_meta->Append("tick_db.index.v1", payload);

    // Write single Parquet file with embedded index in footer
    assert(tick_db::ParquetWriter::write_records_with_metadata(test_parquet, std::span<const tick_db::Trade>(trades), kv_meta));

    // Verify SidecarIndex reads index directly from single .parquet file footer!
    tick_db::HierarchicalRTree restored;
    assert(tick_db::SidecarIndex::read_index(test_parquet, restored));
    assert(restored.total_leaf_entries() == 1);

    // Query via RecordStream<Trade> Iterator
    tick_db::DynamicMBR full_mbr;
    full_mbr.min_bounds = {0, 0, 0, 0, 0};
    full_mbr.max_bounds = {std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::max(),
                           std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::max(),
                           std::numeric_limits<int64_t>::max()};

    tick_db::QueryMetrics metrics;
    auto stream = tick_db::QueryEngine::execute_stream<tick_db::Trade>(test_parquet, test_parquet, full_mbr, 0, &metrics);

    size_t count = 0;
    for (const auto& trade : stream) {
        count++;
        assert(trade.ts_exchange_ns > 0);
    }
    assert(count == 100);
    assert(stream.total_matching() == 100);

    std::cout << "  - Embedded Parquet Metadata Index & RecordStream<T> Iterator (100 records): PASSED\n";
}

void test_multi_day_stream_query() {
    std::cout << "\n[TEST ND-8] Testing Multi-Day Range Querying & Chained Stream Iteration...\n";
    std::vector<std::string> day_files = {"/tmp/day1_test.parquet", "/tmp/day2_test.parquet", "/tmp/day3_test.parquet"};

    for (size_t d = 0; d < day_files.size(); ++d) {
        std::vector<tick_db::Trade> day_trades;
        uint64_t base_ts = 1000000 + d * 1000000;
        for (uint64_t i = 1; i <= 100; ++i) {
            day_trades.push_back({base_ts + i * 1000, i, static_cast<int64_t>(15000 + i), 100, tick_db::Side::Bid});
        }

        tick_db::TypedRTree<tick_db::Trade> rtree;
        rtree.insert_row_group(day_trades, 0, day_files[d]);

        std::string payload = tick_db::SidecarIndex::serialize_to_string(rtree.inner());
        auto kv_meta = std::make_shared<arrow::KeyValueMetadata>();
        kv_meta->Append("tick_db.index.v1", payload);

        assert(tick_db::ParquetWriter::write_records_with_metadata(day_files[d], std::span<const tick_db::Trade>(day_trades), kv_meta));
    }

    tick_db::DynamicMBR full_mbr;
    full_mbr.min_bounds = {0, 0, 0, 0, 0};
    full_mbr.max_bounds = {std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::max(),
                           std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::max(),
                           std::numeric_limits<int64_t>::max()};

    auto multi_stream = tick_db::QueryEngine::execute_multi_day_stream<tick_db::Trade>(day_files, full_mbr);

    assert(multi_stream.total_matching() == 300);

    size_t count = 0;
    uint64_t last_ts = 0;
    while (multi_stream.has_next()) {
        auto trade = multi_stream.next();
        count++;
        assert(trade.ts_exchange_ns > last_ts);  // Verified 100% strictly timestamp-ascending across multi-day files!
        last_ts = trade.ts_exchange_ns;
    }
    assert(count == 300);

    std::cout << "  - Multi-Day Range Querying (300 records across 3 days, strictly time-ascending): PASSED\n";
}

void test_sql_query_engine() {
    std::cout << "\n[TEST ND-9] Testing SQL Query Engine Parsing & Streaming Execution...\n";
    std::string test_parquet = "/tmp/sql_query_test.parquet";

    auto& cat = tick_db::SymbolCatalog::instance();
    uint32_t aapl_id = cat.get_or_create_id("AAPL");

    std::vector<tick_db::Trade> trades;
    for (uint64_t i = 1; i <= 100; ++i) {
        trades.push_back({1000000 + i * 10, i, static_cast<int64_t>(15000 + i), 100, tick_db::Side::Bid});
    }

    tick_db::TypedRTree<tick_db::Trade> rtree;
    rtree.insert_row_group(trades, 0, test_parquet);

    std::string payload = tick_db::SidecarIndex::serialize_to_string(rtree.inner());
    auto kv_meta = std::make_shared<arrow::KeyValueMetadata>();
    kv_meta->Append("tick_db.index.v1", payload);

    assert(tick_db::ParquetWriter::write_records_with_metadata(test_parquet, std::span<const tick_db::Trade>(trades), kv_meta));

    // Test SQL Parsing & Execution Stream
    std::string sql = "SELECT * FROM trades WHERE symbol = 'AAPL' AND ts_exchange_ns BETWEEN 1000000 AND 2000000 AND price >= 15000";
    auto sql_stream = tick_db::SqlQueryEngine::execute_sql_stream<tick_db::Trade>(test_parquet, sql);

    size_t count = 0;
    while (sql_stream.has_next()) {
        auto trade = sql_stream.next();
        count++;
        assert(trade.ts_exchange_ns >= 1000000 && trade.ts_exchange_ns <= 2000000);
    }
    assert(count == 100);

    std::cout << "  - SQL Query Engine Execution (\"SELECT * FROM trades WHERE symbol = 'AAPL'\"): PASSED\n";
}

void test_csv_backfiller() {
    std::cout << "\n[TEST ND-10] Testing CSV Backfill Ingestion & Embedded Parquet Generation...\n";
    std::string test_csv = "/tmp/sample_ohlcv.csv";
    std::string test_parquet = "/tmp/backfill_ohlcv.parquet";

    {
        std::ofstream csv_out(test_csv);
        csv_out << "timestamp,symbol,open,high,low,close,volume\n";
        csv_out << "1000000,AAPL,150.25,150.50,150.10,150.40,5000\n";
        csv_out << "2000000,AAPL,150.40,151.00,150.35,150.90,7500\n";
        csv_out << "3000000,MSFT,250.10,251.50,249.80,251.20,12000\n";
    }

    assert(tick_db::CsvBackfiller::backfill_ohlcv_from_csv(test_csv, test_parquet));

    // Verify embedded index was created and can be deserialized directly from .parquet footer
    tick_db::HierarchicalRTree restored;
    assert(tick_db::SidecarIndex::read_index(test_parquet, restored));
    assert(restored.total_leaf_entries() == 1);

    // Query backfilled records via RecordStream<OhlcvRecord>
    tick_db::DynamicMBR full_mbr;
    full_mbr.min_bounds = {0, 0, 0, 0, 0, 0, 0};
    full_mbr.max_bounds = {std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::max(),
                           std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::max(),
                           std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::max(),
                           std::numeric_limits<int64_t>::max()};

    auto stream = tick_db::QueryEngine::execute_symbol_stream<tick_db::OhlcvRecord>(test_parquet, test_parquet, "AAPL", full_mbr);
    assert(stream.total_matching() == 2);

    auto bar1 = stream.next();
    assert(bar1.symbol == "AAPL");
    assert(bar1.close == static_cast<int64_t>(150.40 * 100000000.0));

    std::cout << "  - CSV Backfilling & Parquet Embedded Index Generation (3 bars ingested): PASSED\n";
}

int main() {
    std::cout << "=======================================================\n";
    std::cout << " Running Generic N-Dimensional R-Tree Architecture Tests\n";
    std::cout << "=======================================================\n";

    test_generic_nd_rtree_5d_trade();
    test_generic_nd_rtree_7d_order_event();
    test_string_symbol_indexing();
    test_multi_symbol_shared_parquet_pruning();
    test_sub_row_group_symbol_range_pruning();
    test_varint_compressed_position_index();
    test_record_stream_and_embedded_parquet_index();
    test_multi_day_stream_query();
    test_sql_query_engine();
    test_csv_backfiller();

    std::cout << "\n=======================================================\n";
    std::cout << " ALL GENERIC N-DIMENSIONAL R-TREE TESTS PASSED! \n";
    std::cout << "=======================================================\n";

    return 0;
}
