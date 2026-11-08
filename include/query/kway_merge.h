#pragma once

#include <cstdint>
#include <iterator>
#include <memory>
#include <queue>
#include <utility>
#include <variant>
#include <vector>

#include "query/record_stream.h"
#include "schemas/book_level.h"
#include "schemas/ohlcv.h"
#include "schemas/order_event.h"
#include "schemas/quote.h"
#include "schemas/trade.h"

namespace tick_db {

// Generic event key extraction helper for deterministic chronological replay
template <typename R>
inline std::pair<uint64_t, uint64_t> get_event_key(const R& record) {
    uint64_t ts = 0;
    uint64_t seq = 0;
    if constexpr (requires { record.ts_exchange_ns; }) ts = record.ts_exchange_ns;
    if constexpr (requires { record.seq_no; }) seq = record.seq_no;
    return {ts, seq};
}

using MarketRecordVariant = std::variant<Trade, Quote, BookLevel, OrderEvent, OhlcvRecord>;

inline std::pair<uint64_t, uint64_t> get_event_key(const MarketRecordVariant& var) {
    return std::visit([](const auto& r) {
        return get_event_key(r);
    }, var);
}

// True Zero-RAM Streaming K-Way Merge for N concurrent streams
template <typename T>
class StreamingKWayMerge {
   public:
    struct StreamHead {
        size_t stream_idx;
        T record;

        bool operator>(const StreamHead& other) const {
            auto [ts1, seq1] = get_event_key(record);
            auto [ts2, seq2] = get_event_key(other.record);
            if (ts1 != ts2) return ts1 > ts2;
            if (seq1 != seq2) return seq1 > seq2;
            return stream_idx > other.stream_idx;  // Deterministic tie-breaker
        }
    };

    StreamingKWayMerge() = default;

    explicit StreamingKWayMerge(std::vector<RecordStream<T>> streams)
        : streams_(std::move(streams)) {
        init();
    }

    bool has_next() const {
        return !min_heap_.empty();
    }

    T next() {
        if (!has_next()) return T{};
        auto top = min_heap_.top();
        min_heap_.pop();
        T result = std::move(top.record);

        if (streams_[top.stream_idx].has_next()) {
            min_heap_.push({top.stream_idx, streams_[top.stream_idx].next()});
        }
        return result;
    }

    bool next(T& out) {
        if (!has_next()) return false;
        out = next();
        return true;
    }

    std::vector<T> fetch_next_batch(size_t batch_size = 4096) {
        std::vector<T> batch;
        batch.reserve(batch_size);
        while (batch.size() < batch_size && has_next()) {
            batch.push_back(next());
        }
        return batch;
    }

    // Standard C++ Input Iterator for range-based for loops
    class Iterator {
       public:
        using iterator_category = std::input_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        Iterator(StreamingKWayMerge* merger, bool is_end)
            : merger_(merger), is_end_(is_end) {
            if (!is_end_ && merger_) advance();
        }

        reference operator*() const { return current_val_; }
        pointer operator->() const { return &current_val_; }

        Iterator& operator++() {
            advance();
            return *this;
        }

        bool operator==(const Iterator& other) const {
            if (is_end_ && other.is_end_) return true;
            return is_end_ == other.is_end_ && merger_ == other.merger_;
        }

        bool operator!=(const Iterator& other) const {
            return !(*this == other);
        }

       private:
        void advance() {
            if (merger_ && merger_->has_next()) {
                current_val_ = merger_->next();
            } else {
                is_end_ = true;
            }
        }

        StreamingKWayMerge* merger_{nullptr};
        bool is_end_{true};
        T current_val_{};
    };

    Iterator begin() { return Iterator(this, false); }
    Iterator end() { return Iterator(this, true); }

   private:
    void init() {
        for (size_t i = 0; i < streams_.size(); ++i) {
            if (streams_[i].has_next()) {
                min_heap_.push({i, streams_[i].next()});
            }
        }
    }

    std::vector<RecordStream<T>> streams_;
    std::priority_queue<StreamHead, std::vector<StreamHead>, std::greater<StreamHead>> min_heap_;
};

// Unified Multi-Schema Deterministic Replay Engine
class MultiSchemaReplayer {
   public:
    template <typename T>
    static RecordStream<MarketRecordVariant> wrap_stream(RecordStream<T> typed_stream) {
        auto shared_stream = std::make_shared<RecordStream<T>>(std::move(typed_stream));
        auto supplier = [shared_stream](std::vector<MarketRecordVariant>& out) -> bool {
            out.clear();
            if (!shared_stream->has_next()) return false;
            auto batch = shared_stream->fetch_next_batch(4096);
            out.reserve(batch.size());
            for (auto& rec : batch) {
                out.emplace_back(std::move(rec));
            }
            return !out.empty();
        };
        return RecordStream<MarketRecordVariant>(shared_stream->total_matching(), supplier);
    }

    static StreamingKWayMerge<MarketRecordVariant> merge_variants(std::vector<RecordStream<MarketRecordVariant>> streams) {
        return StreamingKWayMerge<MarketRecordVariant>(std::move(streams));
    }
};

class KWayMergeReader {
   public:
    KWayMergeReader() = default;
    ~KWayMergeReader() = default;

    // Merge multiple trade streams in guaranteed deterministic (ts_exchange_ns, seq_no) order
    static std::vector<Trade> merge_trades(const std::vector<std::vector<Trade>>& streams) {
        std::vector<RecordStream<Trade>> record_streams;
        record_streams.reserve(streams.size());
        for (const auto& s : streams) {
            record_streams.emplace_back(s);
        }
        StreamingKWayMerge<Trade> merger(std::move(record_streams));
        std::vector<Trade> merged;
        while (merger.has_next()) {
            merged.push_back(merger.next());
        }
        return merged;
    }
};

}  // namespace tick_db
