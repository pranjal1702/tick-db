#pragma once

#include <arrow/api.h>

#include <memory>
#include <string>
#include <vector>

#include "schemas/base.h"
#include "schemas/book_level.h"
#include "schemas/order_event.h"
#include "schemas/quote.h"
#include "schemas/trade.h"

namespace tick_db {

struct ParquetFileInfo {
    std::string filepath;
    int64_t num_rows{0};
    int num_columns{0};
    int num_row_groups{0};
    std::vector<std::string> column_names;
};

class ParquetReader {
   public:
    ParquetReader() = default;
    ~ParquetReader() = default;

    // Read full Arrow Table directly from Parquet file
    static std::shared_ptr<arrow::Table> read_table(const std::string& filepath);

    // Read general column data from a Parquet file into tick_db::RawColumn
    // structures
    bool read_raw_columns(const std::string& filepath, std::vector<RawColumn>& out_columns,
                          ParquetFileInfo* out_info = nullptr);

    // Read specialized trade tick data from a Parquet file into vector<Trade>
    bool read_trades(const std::string& filepath, std::vector<Trade>& out_trades, ParquetFileInfo* out_info = nullptr);

    // Print summary metadata of a Parquet file
    void print_summary(const std::string& filepath);
};

}  // namespace tick_db
