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

bool CsvBackfiller::backfill_ohlcv_from_csv(const std::string& csv_filepath, const std::string& output_parquet_path, const std::string& default_symbol) {
    std::ifstream in(csv_filepath);
    if (!in) {
        std::cerr << "[CsvBackfiller] Failed to open CSV file: " << csv_filepath << "\n";
        return false;
    }

    std::string line;
    std::vector<OhlcvRecord> records;

    if (std::getline(in, line)) {
        if (line.find("timestamp") == std::string::npos && line.find("symbol") == std::string::npos) {
            in.seekg(0);
        }
    }

    // State for aggregating trades into OHLCV on the fly
    bool is_aggregating = false;
    OhlcvRecord current_candle;
    uint64_t current_bucket = 0;
    const uint64_t INTERVAL_NS = 60000000000ULL; // 1-minute bucket

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        size_t p[7];
        p[0] = line.find(',');
        if (p[0] == std::string::npos) continue;
        int comma_count = 1;
        for (int i = 1; i < 7; ++i) {
            p[i] = line.find(',', p[i-1] + 1);
            if (p[i] != std::string::npos) comma_count++;
            else break;
        }

        try {
            if (comma_count >= 6) {
                // Binance Public Trades format: aggregate on the fly!
                is_aggregating = true;
                std::string px_str = line.substr(p[0] + 1, p[1] - p[0] - 1);
                std::string sz_str = line.substr(p[1] + 1, p[2] - p[1] - 1);
                std::string ts_str = line.substr(p[3] + 1, p[4] - p[3] - 1);
                
                uint64_t ts_ns = parse_timestamp_ns(ts_str);
                if (ts_ns < 10000000000000ULL) ts_ns *= 1000000ULL;
                
                int64_t price = static_cast<int64_t>(std::stod(px_str) * 100000000.0);
                int64_t size = static_cast<int64_t>(std::stod(sz_str) * 1000000.0);
                uint64_t bucket = (ts_ns / INTERVAL_NS) * INTERVAL_NS;

                if (bucket != current_bucket) {
                    if (current_bucket != 0) {
                        records.push_back(current_candle);
                    }
                    current_bucket = bucket;
                    current_candle.ts_exchange_ns = bucket;
                    current_candle.symbol = default_symbol;
                    current_candle.open = price;
                    current_candle.high = price;
                    current_candle.low = price;
                    current_candle.close = price;
                    current_candle.volume = size;
                } else {
                    current_candle.high = std::max(current_candle.high, price);
                    current_candle.low = std::min(current_candle.low, price);
                    current_candle.close = price;
                    current_candle.volume += size;
                }
            } else if (comma_count >= 5) {
                // Generic OHLCV format
                std::string ts_str = line.substr(0, p[0]);
                std::string sym = line.substr(p[0] + 1, p[1] - p[0] - 1);
                std::string open_str = line.substr(p[1] + 1, p[2] - p[1] - 1);
                std::string high_str = line.substr(p[2] + 1, p[3] - p[2] - 1);
                std::string low_str = line.substr(p[3] + 1, p[4] - p[3] - 1);
                std::string close_str = line.substr(p[4] + 1, p[5] - p[4] - 1);
                size_t p6 = line.find(',', p[5] + 1);
                std::string vol_str = line.substr(p[5] + 1, (p6 == std::string::npos ? std::string::npos : p6 - p[5] - 1));

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
        } catch (const std::exception& e) {
            std::cerr << "[CsvBackfiller] Warning: Skipping malformed line: " << line << "\n";
        }
    }

    if (is_aggregating && current_bucket != 0) {
        records.push_back(current_candle);
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

bool CsvBackfiller::backfill_trades_from_csv(const std::string& csv_filepath, const std::string& output_parquet_path, const std::string& default_symbol) {
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
        size_t p[7];
        p[0] = line.find(',');
        if (p[0] == std::string::npos) continue;
        int comma_count = 1;
        for (int i = 1; i < 7; ++i) {
            p[i] = line.find(',', p[i-1] + 1);
            if (p[i] != std::string::npos) comma_count++;
            else break;
        }

        std::string ts_str, sym, px_str, sz_str;

        if (comma_count >= 6) {
            // Binance Public Trades format: id, price, qty, quoteQty, time, isBuyerMaker, isBestMatch
            px_str = line.substr(p[0] + 1, p[1] - p[0] - 1);
            sz_str = line.substr(p[1] + 1, p[2] - p[1] - 1);
            ts_str = line.substr(p[3] + 1, p[4] - p[3] - 1);
            sym = default_symbol;
        } else if (comma_count >= 3) {
            // Generic format: timestamp, symbol, price, size
            ts_str = line.substr(0, p[0]);
            sym = line.substr(p[0] + 1, p[1] - p[0] - 1);
            px_str = line.substr(p[1] + 1, p[2] - p[1] - 1);
            sz_str = line.substr(p[2] + 1, (p[3] == std::string::npos ? std::string::npos : p[3] - p[2] - 1));
        } else {
            continue;
        }

        try {
            Trade t;
            t.ts_exchange_ns = parse_timestamp_ns(ts_str);
            if (t.ts_exchange_ns < 10000000000000ULL) {
                t.ts_exchange_ns *= 1000000ULL; // Binance uses ms, convert to ns
            }
            t.seq_no = seq++;
            t.price = static_cast<int64_t>(std::stod(px_str) * 100000000.0);
            t.size = static_cast<int64_t>(std::stod(sz_str) * 1000000.0); // often floats in Binance
            t.side = Side::Bid;
            trades.push_back(t);
        } catch (const std::exception& e) {
            std::cerr << "[CsvBackfiller] Warning: Skipping malformed Trade line: " << line << "\n";
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
