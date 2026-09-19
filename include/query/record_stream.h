#pragma once

#include <cstddef>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include "schemas/spatial_concept.h"

namespace tick_db {

template <SpatialRecord T>
class RecordStream {
   public:
    RecordStream() = default;

    explicit RecordStream(std::vector<T> records)
        : records_(std::move(records)), current_idx_(0) {}

    bool has_next() const {
        return current_idx_ < records_.size();
    }

    T next() {
        if (!has_next()) {
            return T{};
        }
        return records_[current_idx_++];
    }

    std::vector<T> fetch_next_batch(size_t batch_size = 4096) {
        std::vector<T> batch;
        if (!has_next()) return batch;
        size_t count = std::min(batch_size, records_.size() - current_idx_);
        batch.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            batch.push_back(records_[current_idx_++]);
        }
        return batch;
    }

    size_t total_matching() const {
        return records_.size();
    }

    size_t remaining() const {
        return (current_idx_ < records_.size()) ? (records_.size() - current_idx_) : 0;
    }

    // Standard C++ Input Iterator for Range-Based For Loops
    class Iterator {
       public:
        using iterator_category = std::input_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        Iterator(const RecordStream* stream, size_t pos) : stream_(stream), pos_(pos) {}

        reference operator*() const {
            return stream_->records_[pos_];
        }

        pointer operator->() const {
            return &stream_->records_[pos_];
        }

        Iterator& operator++() {
            ++pos_;
            return *this;
        }

        Iterator operator++(int) {
            Iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        bool operator==(const Iterator& other) const {
            return stream_ == other.stream_ && pos_ == other.pos_;
        }

        bool operator!=(const Iterator& other) const {
            return !(*this == other);
        }

       private:
        const RecordStream* stream_{nullptr};
        size_t pos_{0};
    };

    Iterator begin() const {
        return Iterator(this, current_idx_);
    }

    Iterator end() const {
        return Iterator(this, records_.size());
    }

   private:
    std::vector<T> records_;
    size_t current_idx_{0};
};

template <SpatialRecord T>
class MultiDayRecordStream {
   public:
    MultiDayRecordStream() = default;

    explicit MultiDayRecordStream(std::vector<RecordStream<T>> day_streams)
        : day_streams_(std::move(day_streams)), current_stream_idx_(0) {}

    bool has_next() {
        while (current_stream_idx_ < day_streams_.size()) {
            if (day_streams_[current_stream_idx_].has_next()) {
                return true;
            }
            current_stream_idx_++;
        }
        return false;
    }

    T next() {
        if (!has_next()) return T{};
        return day_streams_[current_stream_idx_].next();
    }

    std::vector<T> fetch_next_batch(size_t batch_size = 4096) {
        std::vector<T> batch;
        while (batch.size() < batch_size && has_next()) {
            auto sub_batch = day_streams_[current_stream_idx_].fetch_next_batch(batch_size - batch.size());
            batch.insert(batch.end(), sub_batch.begin(), sub_batch.end());
        }
        return batch;
    }

    size_t total_matching() const {
        size_t total = 0;
        for (const auto& ds : day_streams_) {
            total += ds.total_matching();
        }
        return total;
    }

   private:
    std::vector<RecordStream<T>> day_streams_;
    size_t current_stream_idx_{0};
};

}  // namespace tick_db
