#include "schemas/order_event.h"

namespace tick_db {

std::shared_ptr<arrow::Table> to_columns(std::span<const OrderEvent> events) {
    arrow::UInt64Builder ts_builder;
    arrow::UInt64Builder seq_builder;
    arrow::UInt64Builder order_id_builder;
    arrow::UInt8Builder evt_builder;
    arrow::UInt8Builder side_builder;
    arrow::Int64Builder price_builder;
    arrow::Int64Builder size_builder;

    for (const auto& e : events) {
        (void)ts_builder.Append(e.ts_exchange_ns);
        (void)seq_builder.Append(e.seq_no);
        (void)order_id_builder.Append(e.order_id);
        (void)evt_builder.Append(static_cast<uint8_t>(e.event_type));
        (void)side_builder.Append(static_cast<uint8_t>(e.side));
        (void)price_builder.Append(e.price);
        (void)size_builder.Append(e.size);
    }

    std::shared_ptr<arrow::Array> ts_arr, seq_arr, order_id_arr, evt_arr, side_arr, price_arr, size_arr;
    (void)ts_builder.Finish(&ts_arr);
    (void)seq_builder.Finish(&seq_arr);
    (void)order_id_builder.Finish(&order_id_arr);
    (void)evt_builder.Finish(&evt_arr);
    (void)side_builder.Finish(&side_arr);
    (void)price_builder.Finish(&price_arr);
    (void)size_builder.Finish(&size_arr);

    auto schema = arrow::schema({arrow::field("ts_exchange_ns", arrow::uint64()),
                                 arrow::field("seq_no", arrow::uint64()), arrow::field("order_id", arrow::uint64()),
                                 arrow::field("event_type", arrow::uint8()), arrow::field("side", arrow::uint8()),
                                 arrow::field("price", arrow::int64()), arrow::field("size", arrow::int64())});

    return arrow::Table::Make(schema, {ts_arr, seq_arr, order_id_arr, evt_arr, side_arr, price_arr, size_arr});
}

void from_columns(const arrow::Table& table, std::vector<OrderEvent>& out_events) {
    out_events.clear();
    int64_t num_rows = table.num_rows();
    if (num_rows == 0) return;

    out_events.resize(num_rows);

    auto ts_col = table.GetColumnByName("ts_exchange_ns");
    auto seq_col = table.GetColumnByName("seq_no");
    auto order_id_col = table.GetColumnByName("order_id");
    auto evt_col = table.GetColumnByName("event_type");
    auto side_col = table.GetColumnByName("side");
    auto price_col = table.GetColumnByName("price");
    auto size_col = table.GetColumnByName("size");

    auto extract_uint64 = [&](std::shared_ptr<arrow::ChunkedArray> col, auto setter) {
        if (!col) return;
        int64_t offset = 0;
        for (int c = 0; c < col->num_chunks(); ++c) {
            auto arr = std::static_pointer_cast<arrow::UInt64Array>(col->chunk(c));
            for (int64_t i = 0; i < arr->length(); ++i) {
                setter(out_events[offset + i], arr->Value(i));
            }
            offset += arr->length();
        }
    };

    auto extract_uint8 = [&](std::shared_ptr<arrow::ChunkedArray> col, auto setter) {
        if (!col) return;
        int64_t offset = 0;
        for (int c = 0; c < col->num_chunks(); ++c) {
            auto arr = std::static_pointer_cast<arrow::UInt8Array>(col->chunk(c));
            for (int64_t i = 0; i < arr->length(); ++i) {
                setter(out_events[offset + i], arr->Value(i));
            }
            offset += arr->length();
        }
    };

    auto extract_int64 = [&](std::shared_ptr<arrow::ChunkedArray> col, auto setter) {
        if (!col) return;
        int64_t offset = 0;
        for (int c = 0; c < col->num_chunks(); ++c) {
            auto arr = std::static_pointer_cast<arrow::Int64Array>(col->chunk(c));
            for (int64_t i = 0; i < arr->length(); ++i) {
                setter(out_events[offset + i], arr->Value(i));
            }
            offset += arr->length();
        }
    };

    extract_uint64(ts_col, [](OrderEvent& e, uint64_t v) { e.ts_exchange_ns = v; });
    extract_uint64(seq_col, [](OrderEvent& e, uint64_t v) { e.seq_no = v; });
    extract_uint64(order_id_col, [](OrderEvent& e, uint64_t v) { e.order_id = v; });
    extract_uint8(evt_col, [](OrderEvent& e, uint8_t v) { e.event_type = static_cast<OrderEventType>(v); });
    extract_uint8(side_col, [](OrderEvent& e, uint8_t v) { e.side = static_cast<Side>(v); });
    extract_int64(price_col, [](OrderEvent& e, int64_t v) { e.price = v; });
    extract_int64(size_col, [](OrderEvent& e, int64_t v) { e.size = v; });
}

}  // namespace tick_db
