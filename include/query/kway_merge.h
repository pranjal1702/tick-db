#pragma once

#include <queue>
#include <vector>

#include "schemas/trade.h"

namespace tick_db {

class KWayMergeReader {
   public:
    KWayMergeReader() = default;
    ~KWayMergeReader() = default;

    // Merge multiple trade streams in guaranteed deterministic (ts_exchange_ns,
    // seq_no) order
    static std::vector<Trade> merge_trades(const std::vector<std::vector<Trade>>& streams) {
        struct StreamState {
            size_t stream_idx;
            size_t item_idx;
            const Trade* trade;

            bool operator>(const StreamState& other) const {
                if (trade->ts_exchange_ns != other.trade->ts_exchange_ns) {
                    return trade->ts_exchange_ns > other.trade->ts_exchange_ns;
                }
                return trade->seq_no > other.trade->seq_no;
            }
        };

        std::priority_queue<StreamState, std::vector<StreamState>, std::greater<StreamState>> min_heap;

        for (size_t i = 0; i < streams.size(); ++i) {
            if (!streams[i].empty()) {
                min_heap.push({i, 0, &streams[i][0]});
            }
        }

        std::vector<Trade> merged;
        while (!min_heap.empty()) {
            auto top = min_heap.top();
            min_heap.pop();

            merged.push_back(*top.trade);

            if (top.item_idx + 1 < streams[top.stream_idx].size()) {
                size_t next_idx = top.item_idx + 1;
                min_heap.push({top.stream_idx, next_idx, &streams[top.stream_idx][next_idx]});
            }
        }

        return merged;
    }
};

}  // namespace tick_db
