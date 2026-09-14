#pragma once

#include <arrow/api.h>

#include <memory>
#include <span>
#include <tuple>
#include <vector>

#include "schemas/base.h"

namespace tick_db {

struct Trade {
    uint64_t ts_exchange_ns{0};
    uint64_t seq_no{0};
    int64_t price{0};  // fixed-point, scaled
    int64_t size{0};
    Side side{Side::None};

    auto spatial_tuple() const { return std::tie(ts_exchange_ns, seq_no, price, size, side); }
};

std::shared_ptr<arrow::Table> to_columns(std::span<const Trade> trades);
void from_columns(const arrow::Table& table, std::vector<Trade>& out_trades);
inline std::vector<Trade> trades_from_columns(const arrow::Table& table) {
    std::vector<Trade> res;
    from_columns(table, res);
    return res;
}

}  // namespace tick_db
