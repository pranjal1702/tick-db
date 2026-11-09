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
#include "clustering/compactor.h"
#include "clustering/hilbert.h"
#include "live/collector.h"
#include "orderbook/order_book.h"
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
        trades.push_back({1000000 + i * 10, i, 1, static_cast<int64_t>(15000 + (i % 200)), static_cast<int64_t>(100 + i),
                          tick_db::Side::Bid});
    }

    assert(tick_db::ParquetWriter::write_trades(test_parquet, trades, 64));

    tick_db::TypedRTree<tick_db::Trade> rtree;
    rtree.insert_row_group(trades, 0, test_parquet);

    assert(tick_db::SidecarIndex::write_index(test_index, rtree.inner()));

    tick_db::HierarchicalRTree restored;
    assert(tick_db::SidecarIndex::read_index(test_index, restored));

    auto query_mbr = tick_db::extract_mbr(trades[0]);
    query_mbr.min_bounds = {1000100, 10, 1, 15010, 105, static_cast<int64_t>(tick_db::Side::Bid)};
    query_mbr.max_bounds = {1001000, 100, 1, 15050, 200, static_cast<int64_t>(tick_db::Side::Bid)};

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
        trades.push_back({1000000 + i * 10, i, 1, static_cast<int64_t>(15000 + i), 100, tick_db::Side::Bid});
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
    auto stream = tick_db::QueryEngine::execute_stream<tick_db::Trade>(test_parquet, test_parquet, full_mbr, 0, tick_db::Side::None, &metrics);

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
            day_trades.push_back({base_ts + i * 1000, i, 1, static_cast<int64_t>(15000 + i), 100, tick_db::Side::Bid});
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

    auto multi_stream = tick_db::QueryEngine::execute_multi_day_stream<tick_db::Trade>(day_files, full_mbr, {}, tick_db::Side::None, false);

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

    // Test Lazy Mode as well
    auto lazy_stream = tick_db::QueryEngine::execute_multi_day_stream<tick_db::Trade>(day_files, full_mbr, {}, tick_db::Side::None, true);
    assert(lazy_stream.is_lazy());
    assert(lazy_stream.total_files() == 3);
    size_t lazy_count = 0;
    last_ts = 0;
    tick_db::Trade t;
    while (lazy_stream.next(t)) {
        lazy_count++;
        assert(t.ts_exchange_ns > last_ts);
        last_ts = t.ts_exchange_ns;
    }
    assert(lazy_count == 300);

    // Test Lazy Batch Fetching across daily partition boundaries
    auto batch_stream = tick_db::QueryEngine::execute_multi_day_stream<tick_db::Trade>(day_files, full_mbr, {}, tick_db::Side::None, true);
    size_t batch_total = 0;
    while (batch_stream.has_next()) {
        auto batch = batch_stream.fetch_next_batch(75);
        assert(!batch.empty());
        assert(batch.size() <= 75);
        batch_total += batch.size();
    }
    assert(batch_total == 300);

    // Test Range-Based For Loop on MultiDayRecordStream
    auto for_stream = tick_db::QueryEngine::execute_multi_day_stream<tick_db::Trade>(day_files, full_mbr, {}, tick_db::Side::None, true);
    size_t for_count = 0;
    for (const auto& record : for_stream) {
        (void)record;
        for_count++;
    }
    assert(for_count == 300);

    std::cout << "  - Multi-Day Range Querying (300 records across 3 days, strictly time-ascending): PASSED\n";
}

void test_sql_query_engine() {
    std::cout << "\n[TEST ND-9] Testing SQL Query Engine Parsing & Streaming Execution...\n";
    std::string test_parquet = "/tmp/sql_query_test.parquet";

    auto& cat = tick_db::SymbolCatalog::instance();
    uint32_t aapl_id = cat.get_or_create_id("AAPL");

    std::vector<tick_db::Trade> trades;
    for (uint64_t i = 1; i <= 100; ++i) {
        trades.push_back({1000000 + i * 10, i, static_cast<uint16_t>(aapl_id), static_cast<int64_t>(15000 + i), 100, tick_db::Side::Bid});
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

void test_streaming_kway_merge_and_multi_schema_replay() {
    std::cout << "\n[TEST ND-11] Testing Deterministic Multi-Schema Streaming K-Way Merge...\n";

    // 1. Create Trade stream
    std::vector<tick_db::Trade> trades = {
        {1000000, 1, 1, 15000, 100, tick_db::Side::Bid},
        {1000020, 3, 1, 15010, 200, tick_db::Side::Ask},
        {1000050, 5, 1, 15020, 300, tick_db::Side::Bid}
    };
    tick_db::RecordStream<tick_db::Trade> trade_stream(trades);

    // 2. Create Quote stream
    std::vector<tick_db::Quote> quotes = {
        {1000010, 2, 14990, 500, 15010, 500},
        {1000030, 4, 15000, 600, 15020, 400}
    };
    tick_db::RecordStream<tick_db::Quote> quote_stream(quotes);

    // 3. Create OrderEvent stream
    std::vector<tick_db::OrderEvent> orders = {
        {1000000, 0, 999111, tick_db::OrderEventType::Add, tick_db::Side::Bid, 14990, 500},
        {1000060, 6, 999222, tick_db::OrderEventType::Add, tick_db::Side::Ask, 15020, 300}
    };
    tick_db::RecordStream<tick_db::OrderEvent> order_stream(orders);

    // Wrap into unified variant streams
    auto v_trades = tick_db::MultiSchemaReplayer::wrap_stream(trade_stream);
    auto v_quotes = tick_db::MultiSchemaReplayer::wrap_stream(quote_stream);
    auto v_orders = tick_db::MultiSchemaReplayer::wrap_stream(order_stream);

    std::vector<tick_db::RecordStream<tick_db::MarketRecordVariant>> streams;
    streams.push_back(std::move(v_trades));
    streams.push_back(std::move(v_quotes));
    streams.push_back(std::move(v_orders));

    auto merged_stream = tick_db::MultiSchemaReplayer::merge_variants(std::move(streams));

    size_t count = 0;
    uint64_t last_ts = 0;
    uint64_t last_seq = 0;

    while (merged_stream.has_next()) {
        auto var = merged_stream.next();
        count++;
        auto [ts, seq] = tick_db::get_event_key(var);
        if (count > 1) {
            assert(ts > last_ts || (ts == last_ts && seq >= last_seq));
        }
        last_ts = ts;
        last_seq = seq;
    }

    assert(count == 7);
    std::cout << "  - Deterministic Multi-Schema Replay (Trades+Quotes+OrderEvents, 7 items strictly ordered): PASSED\n";
}

void test_order_book_l2_reconstruction() {
    std::cout << "\n[TEST ND-12] Testing L2 Limit Order Book (LOB) Reconstruction & Microstructural Analytics...\n";

    tick_db::OrderBook book("AAPL");

    // Feed L2 3 bid levels and 3 ask levels
    std::vector<tick_db::BookLevel> levels = {
        {1000000, 1, 0, tick_db::Side::Bid, 15000, 500},
        {1000000, 2, 1, tick_db::Side::Bid, 14990, 800},
        {1000000, 3, 2, tick_db::Side::Bid, 14980, 1200},
        {1000000, 4, 0, tick_db::Side::Ask, 15010, 300},
        {1000000, 5, 1, tick_db::Side::Ask, 15020, 700},
        {1000000, 6, 2, tick_db::Side::Ask, 15030, 1000}
    };
    book.apply_book_levels(levels);

    assert(book.is_valid_bbo());
    assert(book.best_bid_price() == 15000);
    assert(book.best_bid_size() == 500);
    assert(book.best_ask_price() == 15010);
    assert(book.best_ask_size() == 300);
    assert(book.spread() == 10);
    assert(book.mid_price() == 15005.0);

    // Microprice: (300 * 15000 + 500 * 15010) / (300 + 500) = (4500000 + 7505000) / 800 = 12005000 / 800 = 15006.25
    double micro = book.microprice();
    assert(std::abs(micro - 15006.25) < 0.001);

    // OBI across top 3 levels:
    // Bid vol: 500 + 800 + 1200 = 2500
    // Ask vol: 300 + 700 + 1000 = 2000
    // OBI = (2500 - 2000) / (2500 + 2000) = 500 / 4500 = 0.1111...
    double obi = book.order_book_imbalance(3);
    assert(std::abs(obi - (500.0 / 4500.0)) < 0.001);

    // VWAP to buy 600 shares from asks:
    // 300 @ 15010 = 4,503,000
    // 300 @ 15020 = 4,506,000
    // Total = 9,009,000 / 600 = 15015.0
    double vwap_buy = book.vwap(tick_db::Side::Ask, 600);
    assert(std::abs(vwap_buy - 15015.0) < 0.001);

    // Delete best bid level (size = 0)
    book.apply_book_level({1000010, 7, 0, tick_db::Side::Bid, 15000, 0});
    assert(book.best_bid_price() == 14990);
    assert(book.best_bid_size() == 800);
    assert(book.spread() == 20);

    std::cout << "  - L2 Order Book BBO, Microprice, OBI, and VWAP Execution Slicing: PASSED\n";
}

void test_order_book_l3_event_matching() {
    std::cout << "\n[TEST ND-13] Testing L3 Order Lifecycle Replay (Add, Modify, Cancel, Execute)...\n";

    tick_db::OrderBook book("MSFT");

    // 1. Add two bid orders at same price level
    book.apply_order_event({1000000, 1, 101, tick_db::OrderEventType::Add, tick_db::Side::Bid, 25000, 100});
    book.apply_order_event({1000001, 2, 102, tick_db::OrderEventType::Add, tick_db::Side::Bid, 25000, 200});

    assert(book.best_bid_price() == 25000);
    assert(book.best_bid_size() == 300);
    assert(book.total_active_orders() == 2);

    // 2. Add ask order
    book.apply_order_event({1000002, 3, 201, tick_db::OrderEventType::Add, tick_db::Side::Ask, 25020, 500});
    assert(book.best_ask_price() == 25020);
    assert(book.best_ask_size() == 500);

    // 3. Modify order 101: change price to 25010 and size to 150
    book.apply_order_event({1000003, 4, 101, tick_db::OrderEventType::Modify, tick_db::Side::Bid, 25010, 150});
    assert(book.best_bid_price() == 25010);  // New higher bid!
    assert(book.best_bid_size() == 150);
    assert(book.spread() == 10);

    // 4. Partial execute order 201 for 200 shares
    book.apply_order_event({1000004, 5, 201, tick_db::OrderEventType::Execute, tick_db::Side::Ask, 25020, 200});
    assert(book.best_ask_size() == 300);

    // 5. Cancel remaining 300 shares of order 201
    book.apply_order_event({1000005, 6, 201, tick_db::OrderEventType::Cancel, tick_db::Side::Ask, 25020, 300});
    assert(!book.has_ask());
    assert(book.total_active_orders() == 2);

    std::cout << "  - L3 Order Lifecycle Replay (Add, Modify, Partial Fill, Cancel): PASSED\n";
}

void test_hilbert_curve_clustering() {
    std::cout << "\n[TEST ND-14] Testing 2D Hilbert Space-Filling Curve Compaction & Querying...\n";

    // 1. Verify 2D Hilbert continuity: consecutive points on order 1 Hilbert curve have Manhattan distance 1
    uint32_t h0 = tick_db::hilbert_encode(0, 0);
    uint32_t h1 = tick_db::hilbert_encode(1, 0);
    uint32_t h2 = tick_db::hilbert_encode(1, 1);
    uint32_t h3 = tick_db::hilbert_encode(0, 1);
    assert(h0 == 0);
    assert(h1 == 1);
    assert(h2 == 2);
    assert(h3 == 3);

    // 2. Create segment files and compact with Hilbert curve
    std::string seg1 = "/tmp/seg1_trades.parquet";
    std::string seg2 = "/tmp/seg2_trades.parquet";
    std::string compacted_file = "/tmp/hilbert_compacted_trades.parquet";

    std::vector<tick_db::Trade> seg1_trades;
    std::vector<tick_db::Trade> seg2_trades;

    for (uint64_t i = 1; i <= 50; ++i) {
        seg1_trades.push_back({1000000 + i * 20, i, 1, static_cast<int64_t>(15000 + i * 2), 100, tick_db::Side::Bid});
        seg2_trades.push_back({1000010 + i * 20, i + 50, 1, static_cast<int64_t>(15001 + i * 2), 200, tick_db::Side::Ask});
    }

    assert(tick_db::ParquetWriter::write_trades(seg1, seg1_trades));
    assert(tick_db::ParquetWriter::write_trades(seg2, seg2_trades));

    // Run Hilbert Compaction
    assert(tick_db::Compactor::compact_trades({seg1, seg2}, compacted_file, tick_db::ClusteringMethod::HilbertCurve, 50));

    // Verify compacted file has embedded index and can be queried via QueryEngine
    tick_db::HierarchicalRTree restored;
    assert(tick_db::SidecarIndex::read_index(compacted_file, restored));
    assert(restored.total_leaf_entries() == 2);

    tick_db::DynamicMBR full_mbr;
    full_mbr.min_bounds = {0, 0, 0, 0, 0};
    full_mbr.max_bounds = {std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::max(),
                           std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::max(),
                           std::numeric_limits<int64_t>::max()};

    auto stream = tick_db::QueryEngine::execute_stream<tick_db::Trade>(compacted_file, compacted_file, full_mbr);
    size_t count = 0;
    while (stream.has_next()) {
        stream.next();
        count++;
    }
    assert(count == 100);

    std::cout << "  - 2D Hilbert Space-Filling Compaction & Embedded Single-I/O Querying (100 records): PASSED\n";
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
    test_streaming_kway_merge_and_multi_schema_replay();
    test_order_book_l2_reconstruction();
    test_order_book_l3_event_matching();
    test_hilbert_curve_clustering();

    std::cout << "\n=======================================================\n";
    std::cout << " ALL GENERIC N-DIMENSIONAL R-TREE TESTS PASSED! \n";
    std::cout << "=======================================================\n";

    return 0;
}
