#pragma once

#include <arrow/api.h>
#include <parquet/arrow/writer.h>

#include <memory>
#include <span>
#include <string>
#include <vector>

#include "schemas/trade.h"

namespace tick_db {

class ParquetWriter {
   public:
    ParquetWriter() = default;
    ~ParquetWriter() = default;

    // Generic templated writer for ANY schema record T
    template <typename T>
    static bool write_records(const std::string& filepath, std::span<const T> records, int64_t records_per_page = 256) {
        auto table = to_columns(records);
        return write_table(filepath, table, records_per_page);
    }

    template <typename T>
    static bool write_records_with_metadata(const std::string& filepath, std::span<const T> records,
                                            const std::shared_ptr<arrow::KeyValueMetadata>& kv_metadata,
                                            int64_t records_per_page = 256) {
        auto table = to_columns(records);
        return write_table_with_metadata(filepath, table, kv_metadata, records_per_page);
    }

    // Write trades to a time-ordered Parquet file
    static bool write_trades(const std::string& filepath, std::span<const Trade> trades,
                             int64_t records_per_page = 256);

    // Write generic Arrow Table to Parquet file
    static bool write_table(const std::string& filepath, const std::shared_ptr<arrow::Table>& table,
                            int64_t records_per_page = 256);

    static bool write_table_with_metadata(const std::string& filepath, const std::shared_ptr<arrow::Table>& table,
                                         const std::shared_ptr<arrow::KeyValueMetadata>& kv_metadata,
                                         int64_t records_per_page = 256);
};

}  // namespace tick_db
