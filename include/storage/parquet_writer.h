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

    // Write trades to a time-ordered Parquet file
    static bool write_trades(const std::string& filepath, std::span<const Trade> trades,
                             int64_t records_per_page = 256);

    // Write generic Arrow Table to Parquet file
    static bool write_table(const std::string& filepath, const std::shared_ptr<arrow::Table>& table,
                            int64_t records_per_page = 256);
};

}  // namespace tick_db
