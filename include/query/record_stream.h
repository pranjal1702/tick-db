#pragma once

#include <cstddef>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include "schemas/spatial_concept.h"

namespace tick_db {

template <typename T>
class RecordStream {
   public:
    using BatchSupplier = std::function<bool(std::vector<T>& out_batch)>;

    RecordStream() = default;

    explicit RecordStream(std::vector<T> records)
        : records_(std::move(records)), current_idx_(0), is_lazy_(false) {
        total_count_ = records_.size();
    }

    // Lazy Batch Constructor: fetches records micro-batch by micro-batch from storage
    RecordStream(size_t estimated_total, BatchSupplier supplier)
        : current_idx_(0), is_lazy_(true), total_count_(estimated_total), supplier_(std::move(supplier)) {}

    bool has_next() const {
        if (!is_lazy_) {
            return current_idx_ < records_.size();
        }
        if (current_idx_ < records_.size()) {
            return true;
        }
        // Pull next batch from supplier
        records_.clear();
        current_idx_ = 0;
        if (!supplier_) return false;
        while (supplier_(records_)) {
            if (!records_.empty()) {
                return true;
            }
        }
        return false;
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
        batch.reserve(batch_size);
        while (batch.size() < batch_size && has_next()) {
            batch.push_back(records_[current_idx_++]);
        }
        return batch;
    }

    size_t total_matching() const {
        return total_count_;
    }

    size_t remaining() const {
        if (!is_lazy_) {
            return (current_idx_ < records_.size()) ? (records_.size() - current_idx_) : 0;
        }
        return (current_idx_ < records_.size()) ? (records_.size() - current_idx_) : (total_count_ > 0 ? 1 : 0);
    }

    bool is_lazy() const { return is_lazy_; }

    // Standard C++ Input Iterator for Range-Based For Loops
    class Iterator {
       public:
        using iterator_category = std::input_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        Iterator(RecordStream* stream, bool is_end)
            : stream_(stream), is_end_(is_end) {
            if (!is_end_ && stream_) {
                advance();
            }
        }

        reference operator*() const {
            return current_val_;
        }

        pointer operator->() const {
            return &current_val_;
        }

        Iterator& operator++() {
            advance();
            return *this;
        }

        Iterator operator++(int) {
            Iterator tmp = *this;
            advance();
            return tmp;
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

        RecordStream* stream_{nullptr};
        bool is_end_{true};
        T current_val_{};
    };

    Iterator begin() {
        return Iterator(this, false);
    }

    Iterator end() {
        return Iterator(this, true);
    }

    Iterator begin() const {
        return Iterator(const_cast<RecordStream*>(this), false);
    }

    Iterator end() const {
        return Iterator(const_cast<RecordStream*>(this), true);
    }

   private:
    mutable std::vector<T> records_;
    mutable size_t current_idx_{0};
    bool is_lazy_{false};
    size_t total_count_{0};
    mutable BatchSupplier supplier_{nullptr};
};

template <typename T>
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
