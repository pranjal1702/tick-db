#pragma once

#include <arrow/api.h>

#include <memory>
#include <span>
#include <tuple>
#include <vector>

#include "schemas/base.h"

namespace tick_db {

struct Quote {
    uint64_t ts_exchange_ns{0};
    uint64_t seq_no{0};
    int64_t bid_px{0};
    int64_t bid_sz{0};
    int64_t ask_px{0};
    int64_t ask_sz{0};

    auto spatial_tuple() const { return std::tie(ts_exchange_ns, seq_no, bid_px, ask_px, bid_sz, ask_sz); }
};

std::shared_ptr<arrow::Table> to_columns(std::span<const Quote> quotes);
void from_columns(const arrow::Table& table, std::vector<Quote>& out_quotes);
inline std::vector<Quote> quotes_from_columns(const arrow::Table& table) {
    std::vector<Quote> res;
    from_columns(table, res);
    return res;
}

}  // namespace tick_db
