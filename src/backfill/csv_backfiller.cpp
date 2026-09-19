#include "backfill/csv_backfiller.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

#include "spatial/rtree.h"
#include "spatial/sidecar.h"
#include "storage/parquet_writer.h"

namespace tick_db {

static uint64_t parse_timestamp_ns(const std::string& ts_str) {
    try {
        if (ts_str.find_first_not_of("0123456789") == std::string::npos) {
            return std::stoull(ts_str);
        }
    } catch (...) {}
    return 1700000000000000000ULL;  // Fallback timestamp
}

bool CsvBackfiller::backfill_ohlcv_from_csv(const std::string& csv_filepath, const std::string& output_parquet_path) {
    std::ifstream in(csv_filepath);
    if (!in) {
        std::cerr << "[CsvBackfiller] Failed to open CSV file: " << csv_filepath << "\n";
        return false;
    }

    std::string line;
    std::vector<OhlcvRecord> records;

    // Check header
    if (std::getline(in, line)) {
        if (line.find("timestamp") == std::string::npos && line.find("symbol") == std::string::npos) {
            // Re-seek if no header
            in.seekg(0);
        }
    }

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string ts_str, sym, open_str, high_str, low_str, close_str, vol_str;

        if (std::getline(ss, ts_str, ',') && std::getline(ss, sym, ',') &&
            std::getline(ss, open_str, ',') && std::getline(ss, high_str, ',') &&
            std::getline(ss, low_str, ',') && std::getline(ss, close_str, ',') &&
            std::getline(ss, vol_str, ',')) {

            OhlcvRecord r;
            r.ts_exchange_ns = parse_timestamp_ns(ts_str);
            r.symbol = sym;
            r.open = static_cast<int64_t>(std::stod(open_str) * 100000000.0);
            r.high = static_cast<int64_t>(std::stod(high_str) * 100000000.0);
            r.low = static_cast<int64_t>(std::stod(low_str) * 100000000.0);
            r.close = static_cast<int64_t>(std::stod(close_str) * 100000000.0);
            r.volume = std::stoll(vol_str);
            records.push_back(r);
        }
    }

    if (records.empty()) {
        std::cerr << "[CsvBackfiller] No valid records parsed from " << csv_filepath << "\n";
        return false;
    }

    // 1. Sort records strictly time-ascending (ts_exchange_ns)
    std::sort(records.begin(), records.end(), [](const OhlcvRecord& a, const OhlcvRecord& b) {
        return a.ts_exchange_ns < b.ts_exchange_ns;
    });

    // 2. Build RTree spatial index & Roaring symbol bitmaps
    TypedRTree<OhlcvRecord> rtree;
    rtree.insert_row_group(records, 0, output_parquet_path);

    // 3. Serialize spatial index payload
    std::string payload = SidecarIndex::serialize_to_string(rtree.inner());
    auto kv_meta = std::make_shared<arrow::KeyValueMetadata>();
    kv_meta->Append("tick_db.index.v1", payload);

    // 4. Write final Parquet file with embedded spatial index
    return ParquetWriter::write_records_with_metadata(output_parquet_path, std::span<const OhlcvRecord>(records), kv_meta);
}

bool CsvBackfiller::backfill_trades_from_csv(const std::string& csv_filepath, const std::string& output_parquet_path) {
    std::ifstream in(csv_filepath);
    if (!in) {
        std::cerr << "[CsvBackfiller] Failed to open CSV file: " << csv_filepath << "\n";
        return false;
    }

    std::string line;
    std::vector<Trade> trades;

    if (std::getline(in, line)) {
        if (line.find("timestamp") == std::string::npos) {
            in.seekg(0);
        }
    }

    uint64_t seq = 1;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string ts_str, sym, px_str, sz_str, side_str;

        if (std::getline(ss, ts_str, ',') && std::getline(ss, sym, ',') &&
            std::getline(ss, px_str, ',') && std::getline(ss, sz_str, ',')) {

            Trade t;
            t.ts_exchange_ns = parse_timestamp_ns(ts_str);
            t.seq_no = seq++;
            t.price = static_cast<int64_t>(std::stod(px_str) * 100000000.0);
            t.size = std::stoll(sz_str);
            t.side = Side::Bid;
            trades.push_back(t);
        }
    }

    if (trades.empty()) return false;

    std::sort(trades.begin(), trades.end(), [](const Trade& a, const Trade& b) {
        return a.ts_exchange_ns < b.ts_exchange_ns;
    });

    TypedRTree<Trade> rtree;
    rtree.insert_row_group(trades, 0, output_parquet_path);

    std::string payload = SidecarIndex::serialize_to_string(rtree.inner());
    auto kv_meta = std::make_shared<arrow::KeyValueMetadata>();
    kv_meta->Append("tick_db.index.v1", payload);

    return ParquetWriter::write_records_with_metadata(output_parquet_path, std::span<const Trade>(trades), kv_meta);
}

}  // namespace tick_db
