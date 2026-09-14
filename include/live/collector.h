#pragma once

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "storage/parquet_writer.h"

namespace tick_db {

template <typename T>
class Collector {
   public:
    Collector(std::string symbol, std::string output_dir, size_t flush_threshold = 1000)
        : symbol_(std::move(symbol)), output_dir_(std::move(output_dir)), flush_threshold_(flush_threshold) {
        std::filesystem::create_directories(output_dir_);
    }

    ~Collector() { flush(); }

    // Push a tick record into the memtable buffer
    void push(const T& record) {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.push_back(record);
        if (buffer_.size() >= flush_threshold_) {
            flush_unlocked();
        }
    }

    // Explicitly flush the memtable buffer to a new Parquet segment file
    void flush() {
        std::lock_guard<std::mutex> lock(mutex_);
        flush_unlocked();
    }

    size_t pending_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffer_.size();
    }

    size_t total_flushed_files() const { return flush_count_; }

   private:
    void flush_unlocked() {
        if (buffer_.empty()) return;

        ++flush_count_;
        char filename_buf[64];
        snprintf(filename_buf, sizeof(filename_buf), "seg_%03zu", flush_count_);

        std::filesystem::path final_path =
            std::filesystem::path(output_dir_) / (std::string(filename_buf) + ".parquet");
        std::filesystem::path temp_path = std::filesystem::path(output_dir_) / (std::string(filename_buf) + ".tmp");

        // Convert buffer to Arrow Table via stateless to_columns() transform
        auto table = to_columns(std::span<const T>(buffer_.data(), buffer_.size()));

        // 1. Write to temporary file
        if (!ParquetWriter::write_table(temp_path.string(), table)) {
            std::cerr << "[Collector] Failed to flush temporary file: " << temp_path << "\n";
            return;
        }

        // 2. Fsync and atomically rename temp -> final segment file (crash safety)
        std::filesystem::rename(temp_path, final_path);

        buffer_.clear();
    }

    std::string symbol_;
    std::string output_dir_;
    size_t flush_threshold_{1000};
    size_t flush_count_{0};
    std::vector<T> buffer_;
    mutable std::mutex mutex_;
};

}  // namespace tick_db