#pragma once

#include <string>
#include <vector>

#include "schemas/ohlcv.h"
#include "schemas/trade.h"

namespace tick_db {

class CsvBackfiller {
   public:
    CsvBackfiller() = default;

    // Backfill historical OHLCV data from CSV to time-sorted Parquet file with embedded spatial index
    static bool backfill_ohlcv_from_csv(const std::string& csv_filepath, const std::string& output_parquet_path);

    // Backfill historical Trade data from CSV to time-sorted Parquet file with embedded spatial index
    static bool backfill_trades_from_csv(const std::string& csv_filepath, const std::string& output_parquet_path);
};

}  // namespace tick_db
