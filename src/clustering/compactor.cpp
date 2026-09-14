#include "clustering/compactor.h"

#include <algorithm>
#include <iostream>
#include <limits>

#include "clustering/zorder.h"
#include "storage/parquet_reader.h"
#include "storage/parquet_writer.h"

namespace tick_db {

bool Compactor::compact_trades(const std::vector<std::string>& segment_files,
                               const std::string& output_compacted_file) {
    if (segment_files.empty()) {
        std::cerr << "[Compactor] No segment files provided for compaction.\n";
        return false;
    }

    std::vector<Trade> all_trades;
    ParquetReader reader;

    for (const auto& file : segment_files) {
        std::vector<Trade> segment_trades;
        if (reader.read_trades(file, segment_trades)) {
            all_trades.insert(all_trades.end(), segment_trades.begin(), segment_trades.end());
        }
    }

    if (all_trades.empty()) {
        std::cerr << "[Compactor] No trades loaded from segment files.\n";
        return false;
    }

    // Find min and max for timestamp and price
    uint64_t min_ts = std::numeric_limits<uint64_t>::max();
    uint64_t max_ts = std::numeric_limits<uint64_t>::min();
    int64_t min_price = std::numeric_limits<int64_t>::max();
    int64_t max_price = std::numeric_limits<int64_t>::min();

    for (const auto& t : all_trades) {
        min_ts = std::min(min_ts, t.ts_exchange_ns);
        max_ts = std::max(max_ts, t.ts_exchange_ns);
        min_price = std::min(min_price, t.price);
        max_price = std::max(max_price, t.price);
    }

    uint64_t ts_range = (max_ts > min_ts) ? (max_ts - min_ts) : 1;
    uint64_t price_range = (max_price > min_price) ? static_cast<uint64_t>(max_price - min_price) : 1;

    // Associate each trade with its Z-order Morton key
    struct KeyedTrade {
        uint32_t morton_key;
        Trade trade;
    };

    std::vector<KeyedTrade> keyed_trades;
    keyed_trades.reserve(all_trades.size());

    for (const auto& t : all_trades) {
        uint16_t time_bucket = static_cast<uint16_t>(((t.ts_exchange_ns - min_ts) * 65535ULL) / ts_range);
        uint16_t price_bucket =
            static_cast<uint16_t>(((static_cast<uint64_t>(t.price - min_price)) * 65535ULL) / price_range);
        uint32_t key = morton_encode(time_bucket, price_bucket);
        keyed_trades.push_back({key, t});
    }

    // Sort physically by Morton key
    std::sort(keyed_trades.begin(), keyed_trades.end(),
              [](const KeyedTrade& a, const KeyedTrade& b) { return a.morton_key < b.morton_key; });

    std::vector<Trade> sorted_trades;
    sorted_trades.reserve(keyed_trades.size());
    for (const auto& kt : keyed_trades) {
        sorted_trades.push_back(kt.trade);
    }

    // Write sorted table
    auto table = to_columns(std::span<const Trade>(sorted_trades.data(), sorted_trades.size()));
    return ParquetWriter::write_table(output_compacted_file, table);
}

}  // namespace tick_db
