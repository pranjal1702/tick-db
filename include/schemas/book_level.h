#pragma once

#include <arrow/api.h>

#include <memory>
#include <span>
#include <tuple>
#include <vector>

#include "schemas/base.h"

namespace tick_db {

struct BookLevel {
    uint64_t ts_exchange_ns{0};
    uint64_t seq_no{0};
    uint16_t level_index{0};
    Side side{Side::None};
    int64_t price{0};
    int64_t size{0};

    auto spatial_tuple() const { return std::tie(ts_exchange_ns, seq_no, level_index, side, price, size); }
};

std::shared_ptr<arrow::Table> to_columns(std::span<const BookLevel> levels);
void from_columns(const arrow::Table& table, std::vector<BookLevel>& out_levels);
inline std::vector<BookLevel> book_levels_from_columns(const arrow::Table& table) {
    std::vector<BookLevel> res;
    from_columns(table, res);
    return res;
}

}  // namespace tick_db
