#pragma once

#include <arrow/api.h>

#include <memory>
#include <span>
#include <tuple>
#include <vector>

#include "schemas/base.h"

namespace tick_db {

enum class OrderEventType : uint8_t { Add = 'A', Modify = 'M', Cancel = 'C', Execute = 'E' };

struct OrderEvent {
    uint64_t ts_exchange_ns{0};
    uint64_t seq_no{0};
    uint64_t order_id{0};
    OrderEventType event_type{OrderEventType::Add};
    Side side{Side::None};
    int64_t price{0};
    int64_t size{0};

    auto spatial_tuple() const { return std::tie(ts_exchange_ns, seq_no, order_id, event_type, side, price, size); }
};

std::shared_ptr<arrow::Table> to_columns(std::span<const OrderEvent> events);
void from_columns(const arrow::Table& table, std::vector<OrderEvent>& out_events);
inline std::vector<OrderEvent> order_events_from_columns(const arrow::Table& table) {
    std::vector<OrderEvent> res;
    from_columns(table, res);
    return res;
}

}  // namespace tick_db
