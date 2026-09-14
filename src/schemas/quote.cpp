#include "schemas/quote.h"

namespace tick_db {

std::shared_ptr<arrow::Table> to_columns(std::span<const Quote> quotes) {
    arrow::UInt64Builder ts_builder;
    arrow::UInt64Builder seq_builder;
    arrow::Int64Builder bid_px_builder;
    arrow::Int64Builder bid_sz_builder;
    arrow::Int64Builder ask_px_builder;
    arrow::Int64Builder ask_sz_builder;

    for (const auto& q : quotes) {
        (void)ts_builder.Append(q.ts_exchange_ns);
        (void)seq_builder.Append(q.seq_no);
        (void)bid_px_builder.Append(q.bid_px);
        (void)bid_sz_builder.Append(q.bid_sz);
        (void)ask_px_builder.Append(q.ask_px);
        (void)ask_sz_builder.Append(q.ask_sz);
    }

    std::shared_ptr<arrow::Array> ts_arr, seq_arr, bid_px_arr, bid_sz_arr, ask_px_arr, ask_sz_arr;
    (void)ts_builder.Finish(&ts_arr);
    (void)seq_builder.Finish(&seq_arr);
    (void)bid_px_builder.Finish(&bid_px_arr);
    (void)bid_sz_builder.Finish(&bid_sz_arr);
    (void)ask_px_builder.Finish(&ask_px_arr);
    (void)ask_sz_builder.Finish(&ask_sz_arr);

    auto schema =
        arrow::schema({arrow::field("ts_exchange_ns", arrow::uint64()), arrow::field("seq_no", arrow::uint64()),
                       arrow::field("bid_px", arrow::int64()), arrow::field("bid_sz", arrow::int64()),
                       arrow::field("ask_px", arrow::int64()), arrow::field("ask_sz", arrow::int64())});

    return arrow::Table::Make(schema, {ts_arr, seq_arr, bid_px_arr, bid_sz_arr, ask_px_arr, ask_sz_arr});
}

void from_columns(const arrow::Table& table, std::vector<Quote>& out_quotes) {
    out_quotes.clear();
    int64_t num_rows = table.num_rows();
    if (num_rows == 0) return;

    out_quotes.resize(num_rows);

    auto ts_col = table.GetColumnByName("ts_exchange_ns");
    auto seq_col = table.GetColumnByName("seq_no");
    auto bid_px_col = table.GetColumnByName("bid_px");
    auto bid_sz_col = table.GetColumnByName("bid_sz");
    auto ask_px_col = table.GetColumnByName("ask_px");
    auto ask_sz_col = table.GetColumnByName("ask_sz");

    auto extract_uint64 = [&](std::shared_ptr<arrow::ChunkedArray> col, auto setter) {
        if (!col) return;
        int64_t offset = 0;
        for (int c = 0; c < col->num_chunks(); ++c) {
            auto arr = std::static_pointer_cast<arrow::UInt64Array>(col->chunk(c));
            for (int64_t i = 0; i < arr->length(); ++i) {
                setter(out_quotes[offset + i], arr->Value(i));
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
                setter(out_quotes[offset + i], arr->Value(i));
            }
            offset += arr->length();
        }
    };

    extract_uint64(ts_col, [](Quote& q, uint64_t v) { q.ts_exchange_ns = v; });
    extract_uint64(seq_col, [](Quote& q, uint64_t v) { q.seq_no = v; });
    extract_int64(bid_px_col, [](Quote& q, int64_t v) { q.bid_px = v; });
    extract_int64(bid_sz_col, [](Quote& q, int64_t v) { q.bid_sz = v; });
    extract_int64(ask_px_col, [](Quote& q, int64_t v) { q.ask_px = v; });
    extract_int64(ask_sz_col, [](Quote& q, int64_t v) { q.ask_sz = v; });
}

}  // namespace tick_db
