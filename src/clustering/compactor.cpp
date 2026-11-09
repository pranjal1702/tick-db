#include "clustering/compactor.h"

#include <algorithm>
#include <iostream>
#include <limits>

#include "clustering/hilbert.h"
#include "clustering/zorder.h"
#include "spatial/rtree.h"
#include "spatial/sidecar.h"
#include "storage/parquet_reader.h"
#include "storage/parquet_writer.h"

namespace tick_db {

bool Compactor::compact_trades(const std::vector<std::string>& segment_files,
                               const std::string& output_compacted_file,
                               ClusteringMethod method,
                               int64_t row_group_size) {
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

    std::vector<Trade> sorted_trades;

    if (method == ClusteringMethod::Chronological) {
        sorted_trades = std::move(all_trades);
        std::sort(sorted_trades.begin(), sorted_trades.end(), [](const Trade& a, const Trade& b) {
            if (a.ts_exchange_ns != b.ts_exchange_ns) return a.ts_exchange_ns < b.ts_exchange_ns;
            return a.seq_no < b.seq_no;
        });
    } else {
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

        // Associate each trade with its spatial curve key (Hilbert or Morton Z-order)
        struct KeyedTrade {
            uint32_t curve_key;
            Trade trade;
        };

        std::vector<KeyedTrade> keyed_trades;
        keyed_trades.reserve(all_trades.size());

        for (const auto& t : all_trades) {
            uint16_t time_bucket = static_cast<uint16_t>(((t.ts_exchange_ns - min_ts) * 65535ULL) / ts_range);
            uint16_t price_bucket =
                static_cast<uint16_t>(((static_cast<uint64_t>(t.price - min_price)) * 65535ULL) / price_range);

            uint32_t key = (method == ClusteringMethod::HilbertCurve) ? hilbert_encode(time_bucket, price_bucket)
                                                                      : morton_encode(time_bucket, price_bucket);
            keyed_trades.push_back({key, t});
        }

        // Sort physically by space-filling curve key
        std::sort(keyed_trades.begin(), keyed_trades.end(),
                  [](const KeyedTrade& a, const KeyedTrade& b) { return a.curve_key < b.curve_key; });

        sorted_trades.reserve(keyed_trades.size());
        for (const auto& kt : keyed_trades) {
            sorted_trades.push_back(kt.trade);
        }
    }

    // Build R-Tree over compacted row groups
    TypedRTree<Trade> rtree;
    size_t total_rows = sorted_trades.size();
    size_t rg_size = static_cast<size_t>(row_group_size > 0 ? row_group_size : 10000);
    uint32_t rg_id = 0;

    for (size_t offset = 0; offset < total_rows; offset += rg_size) {
        size_t count = std::min(rg_size, total_rows - offset);
        std::span<const Trade> rg_span(&sorted_trades[offset], count);
        rtree.insert_row_group(rg_span, rg_id++, output_compacted_file);
    }

    std::string payload = SidecarIndex::serialize_to_string(rtree.inner());
    auto kv_meta = std::make_shared<arrow::KeyValueMetadata>();
    kv_meta->Append("tick_db.index.v1", payload);

    // Write compacted file with embedded spatial R-Tree index in footer
    return ParquetWriter::write_records_with_metadata(output_compacted_file,
                                                      std::span<const Trade>(sorted_trades.data(), sorted_trades.size()),
                                                      kv_meta, 256, row_group_size);
}

}  // namespace tick_db
