#include "schemas/trade.h"

namespace tick_db {

std::shared_ptr<arrow::Table> to_columns(std::span<const Trade> trades) {
    arrow::UInt64Builder ts_builder;
    arrow::UInt64Builder seq_builder;
    arrow::Int64Builder price_builder;
    arrow::Int64Builder size_builder;
    arrow::UInt8Builder side_builder;

    for (const auto& t : trades) {
        (void)ts_builder.Append(t.ts_exchange_ns);
        (void)seq_builder.Append(t.seq_no);
        (void)price_builder.Append(t.price);
        (void)size_builder.Append(t.size);
        (void)side_builder.Append(static_cast<uint8_t>(t.side));
    }

    std::shared_ptr<arrow::Array> ts_arr, seq_arr, price_arr, size_arr, side_arr;
    (void)ts_builder.Finish(&ts_arr);
    (void)seq_builder.Finish(&seq_arr);
    (void)price_builder.Finish(&price_arr);
    (void)size_builder.Finish(&size_arr);
    (void)side_builder.Finish(&side_arr);

    auto schema = arrow::schema({arrow::field("ts_exchange_ns", arrow::uint64()),
                                 arrow::field("seq_no", arrow::uint64()), arrow::field("price", arrow::int64()),
                                 arrow::field("size", arrow::int64()), arrow::field("side", arrow::uint8())});

    return arrow::Table::Make(schema, {ts_arr, seq_arr, price_arr, size_arr, side_arr});
}

void from_columns(const arrow::Table& table, std::vector<Trade>& out_trades) {
    out_trades.clear();
    int64_t num_rows = table.num_rows();
    if (num_rows == 0) return;

    out_trades.resize(num_rows);

    auto ts_col = table.GetColumnByName("ts_exchange_ns");
    auto seq_col = table.GetColumnByName("seq_no");
    auto price_col = table.GetColumnByName("price");
    auto size_col = table.GetColumnByName("size");
    auto side_col = table.GetColumnByName("side");

    auto extract_uint64 = [&](std::shared_ptr<arrow::ChunkedArray> col, auto setter) {
        if (!col) return;
        int64_t offset = 0;
        for (int c = 0; c < col->num_chunks(); ++c) {
            auto arr = std::static_pointer_cast<arrow::UInt64Array>(col->chunk(c));
            for (int64_t i = 0; i < arr->length(); ++i) {
                setter(out_trades[offset + i], arr->Value(i));
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
                setter(out_trades[offset + i], arr->Value(i));
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
                setter(out_trades[offset + i], arr->Value(i));
            }
            offset += arr->length();
        }
    };

    extract_uint64(ts_col, [](Trade& t, uint64_t v) { t.ts_exchange_ns = v; });
    extract_uint64(seq_col, [](Trade& t, uint64_t v) { t.seq_no = v; });
    extract_int64(price_col, [](Trade& t, int64_t v) { t.price = v; });
    extract_int64(size_col, [](Trade& t, int64_t v) { t.size = v; });
    extract_uint8(side_col, [](Trade& t, uint8_t v) { t.side = static_cast<Side>(v); });
}

}  // namespace tick_db
