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

    bool next(T& out) {
        if (!has_next()) {
            return false;
        }
        out = records_[current_idx_++];
        return true;
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
    using StreamGenerator = std::function<RecordStream<T>(size_t file_idx)>;

    MultiDayRecordStream() = default;

    // Eager constructor for backward compatibility
    explicit MultiDayRecordStream(std::vector<RecordStream<T>> day_streams)
        : day_streams_(std::move(day_streams)),
          total_files_(day_streams_.size()),
          current_stream_idx_(0),
          is_lazy_(false) {}

    // Lazy constructor: takes total files and a generator callback that loads one file at a time
    MultiDayRecordStream(size_t total_files, StreamGenerator generator)
        : total_files_(total_files),
          generator_(std::move(generator)),
          current_stream_idx_(0),
          is_lazy_(true) {}

    bool has_next() {
        if (!is_lazy_) {
            while (current_stream_idx_ < day_streams_.size()) {
                if (day_streams_[current_stream_idx_].has_next()) {
                    return true;
                }
                current_stream_idx_++;
            }
            return false;
        }

        // Lazy mode: process active_stream_ or pull the next partition file
        while (true) {
            if (active_stream_.has_next()) {
                return true;
            }
            if (current_stream_idx_ >= total_files_) {
                return false;
            }
            // Load partition file on-demand!
            active_stream_ = generator_(current_stream_idx_++);
        }
    }

    T next() {
        if (!has_next()) return T{};
        if (!is_lazy_) {
            return day_streams_[current_stream_idx_].next();
        }
        return active_stream_.next();
    }

    bool next(T& out) {
        if (!has_next()) {
            return false;
        }
        out = next();
        return true;
    }

    std::vector<T> fetch_next_batch(size_t batch_size = 4096) {
        std::vector<T> batch;
        while (batch.size() < batch_size && has_next()) {
            if (!is_lazy_) {
                auto sub_batch = day_streams_[current_stream_idx_].fetch_next_batch(batch_size - batch.size());
                batch.insert(batch.end(), sub_batch.begin(), sub_batch.end());
            } else {
                auto sub_batch = active_stream_.fetch_next_batch(batch_size - batch.size());
                batch.insert(batch.end(), sub_batch.begin(), sub_batch.end());
            }
        }
        return batch;
    }

    size_t total_files() const { return total_files_; }
    bool is_lazy() const { return is_lazy_; }

    size_t total_matching() const {
        if (!is_lazy_) {
            size_t total = 0;
            for (const auto& ds : day_streams_) {
                total += ds.total_matching();
            }
            return total;
        }
        // In lazy mode, total matching is streamed progressively
        return total_files_;
    }

    // Standard C++ Input Iterator for MultiDayRecordStream
    class Iterator {
       public:
        using iterator_category = std::input_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        Iterator(MultiDayRecordStream* stream, bool is_end)
            : stream_(stream), is_end_(is_end) {
            if (!is_end_ && stream_) {
                advance();
            }
        }

        reference operator*() const { return current_val_; }
        pointer operator->() const { return &current_val_; }

        Iterator& operator++() {
            advance();
            return *this;
        }

        bool operator==(const Iterator& other) const {
            if (is_end_ && other.is_end_) return true;
            return is_end_ == other.is_end_ && stream_ == other.stream_;
        }

        bool operator!=(const Iterator& other) const {
            return !(*this == other);
        }

       private:
        void advance() {
            if (stream_ && stream_->has_next()) {
                current_val_ = stream_->next();
            } else {
                is_end_ = true;
            }
        }

        MultiDayRecordStream* stream_{nullptr};
        bool is_end_{true};
        T current_val_{};
    };

    Iterator begin() { return Iterator(this, false); }
    Iterator end() { return Iterator(this, true); }

   private:
    std::vector<RecordStream<T>> day_streams_;
    size_t total_files_{0};
    StreamGenerator generator_{nullptr};
    RecordStream<T> active_stream_;
    size_t current_stream_idx_{0};
    bool is_lazy_{false};
};

}  // namespace tick_db
