#pragma once

#include <string>
#include <vector>

#include "schemas/trade.h"

namespace tick_db {

class Compactor {
   public:
    Compactor() = default;
    ~Compactor() = default;

    // Compact multiple segment Parquet files into one Z-order clustered Parquet
    // file
    static bool compact_trades(const std::vector<std::string>& segment_files, const std::string& output_compacted_file);
};

}  // namespace tick_db
