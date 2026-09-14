#include "schemas/book_level.h"

namespace tick_db {

std::shared_ptr<arrow::Table> to_columns(std::span<const BookLevel> levels) {
    arrow::UInt64Builder ts_builder;
    arrow::UInt64Builder seq_builder;
    arrow::UInt16Builder lvl_builder;
    arrow::UInt8Builder side_builder;
    arrow::Int64Builder price_builder;
    arrow::Int64Builder size_builder;

    for (const auto& l : levels) {
        (void)ts_builder.Append(l.ts_exchange_ns);
        (void)seq_builder.Append(l.seq_no);
        (void)lvl_builder.Append(l.level_index);
        (void)side_builder.Append(static_cast<uint8_t>(l.side));
        (void)price_builder.Append(l.price);
        (void)size_builder.Append(l.size);
    }

    std::shared_ptr<arrow::Array> ts_arr, seq_arr, lvl_arr, side_arr, price_arr, size_arr;
    (void)ts_builder.Finish(&ts_arr);
    (void)seq_builder.Finish(&seq_arr);
    (void)lvl_builder.Finish(&lvl_arr);
    (void)side_builder.Finish(&side_arr);
    (void)price_builder.Finish(&price_arr);
    (void)size_builder.Finish(&size_arr);

    auto schema =
        arrow::schema({arrow::field("ts_exchange_ns", arrow::uint64()), arrow::field("seq_no", arrow::uint64()),
                       arrow::field("level_index", arrow::uint16()), arrow::field("side", arrow::uint8()),
                       arrow::field("price", arrow::int64()), arrow::field("size", arrow::int64())});

    return arrow::Table::Make(schema, {ts_arr, seq_arr, lvl_arr, side_arr, price_arr, size_arr});
}

void from_columns(const arrow::Table& table, std::vector<BookLevel>& out_levels) {
    out_levels.clear();
    int64_t num_rows = table.num_rows();
    if (num_rows == 0) return;

    out_levels.resize(num_rows);

    auto ts_col = table.GetColumnByName("ts_exchange_ns");
    auto seq_col = table.GetColumnByName("seq_no");
    auto lvl_col = table.GetColumnByName("level_index");
    auto side_col = table.GetColumnByName("side");
    auto price_col = table.GetColumnByName("price");
    auto size_col = table.GetColumnByName("size");

    auto extract_uint64 = [&](std::shared_ptr<arrow::ChunkedArray> col, auto setter) {
        if (!col) return;
        int64_t offset = 0;
        for (int c = 0; c < col->num_chunks(); ++c) {
            auto arr = std::static_pointer_cast<arrow::UInt64Array>(col->chunk(c));
            for (int64_t i = 0; i < arr->length(); ++i) {
                setter(out_levels[offset + i], arr->Value(i));
            }
            offset += arr->length();
        }
    };

    auto extract_uint16 = [&](std::shared_ptr<arrow::ChunkedArray> col, auto setter) {
        if (!col) return;
        int64_t offset = 0;
        for (int c = 0; c < col->num_chunks(); ++c) {
            auto arr = std::static_pointer_cast<arrow::UInt16Array>(col->chunk(c));
            for (int64_t i = 0; i < arr->length(); ++i) {
                setter(out_levels[offset + i], arr->Value(i));
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
                setter(out_levels[offset + i], arr->Value(i));
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
                setter(out_levels[offset + i], arr->Value(i));
            }
            offset += arr->length();
        }
    };

    extract_uint64(ts_col, [](BookLevel& l, uint64_t v) { l.ts_exchange_ns = v; });
    extract_uint64(seq_col, [](BookLevel& l, uint64_t v) { l.seq_no = v; });
    extract_uint16(lvl_col, [](BookLevel& l, uint16_t v) { l.level_index = v; });
    extract_uint8(side_col, [](BookLevel& l, uint8_t v) { l.side = static_cast<Side>(v); });
    extract_int64(price_col, [](BookLevel& l, int64_t v) { l.price = v; });
    extract_int64(size_col, [](BookLevel& l, int64_t v) { l.size = v; });
}

}  // namespace tick_db
