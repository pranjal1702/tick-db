#pragma once

#include <string>
#include <vector>

#include "schemas/trade.h"

namespace tick_db {

enum class ClusteringMethod {
    Chronological,
    ZOrderMorton,
    HilbertCurve
};

class Compactor {
   public:
    Compactor() = default;
    ~Compactor() = default;

    // Compact multiple segment Parquet files into one space-filling curve clustered Parquet
    // file with embedded spatial R-Tree metadata
    static bool compact_trades(const std::vector<std::string>& segment_files, const std::string& output_compacted_file,
                               ClusteringMethod method = ClusteringMethod::HilbertCurve,
                               int64_t row_group_size = 10000);
};

}  // namespace tick_db
