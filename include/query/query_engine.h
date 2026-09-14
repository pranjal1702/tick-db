#pragma once

#include <memory>
#include <string>
#include <vector>

#include "schemas/book_level.h"
#include "schemas/order_event.h"
#include "schemas/quote.h"
#include "schemas/trade.h"
#include "spatial/rtree.h"
#include "spatial/sidecar.h"

namespace tick_db {

struct QueryMetrics {
    size_t files_considered{0};
    size_t files_pruned{0};
    size_t row_groups_considered{0};
    size_t row_groups_selected{0};
    size_t pages_considered{0};
    size_t pages_read{0};
    size_t records_examined{0};
    size_t records_returned{0};
    double page_pruning_ratio{0.0};
    double read_amplification{0.0};
};

class QueryEngine {
   public:
    QueryEngine() = default;
    ~QueryEngine() = default;

    // Execute 4-stage query pipeline over N-Dimensional GenericMBR hyper-rectangle
    static std::vector<Trade> execute_trade_query(const std::string& parquet_path,
                                                  const std::string& sidecar_index_path, const GenericMBR& query_mbr,
                                                  QueryMetrics* out_metrics = nullptr);
};

}  // namespace tick_db
