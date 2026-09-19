#pragma once

#include <arrow/api.h>
#include <arrow/builder.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <tuple>
#include <vector>

#include "catalog/symbol_catalog.h"

namespace tick_db {

struct OhlcvRecord {
    uint64_t ts_exchange_ns{0};
    std::string symbol;
    int64_t open{0};
    int64_t high{0};
    int64_t low{0};
    int64_t close{0};
    int64_t volume{0};

    auto spatial_tuple() const { return std::tie(symbol, ts_exchange_ns, open, high, low, close, volume); }
};

inline std::shared_ptr<arrow::Table> to_columns(std::span<const OhlcvRecord> records) {
    arrow::UInt64Builder ts_builder;
    arrow::StringBuilder sym_builder;
    arrow::Int64Builder open_builder, high_builder, low_builder, close_builder, vol_builder;

    for (const auto& r : records) {
        (void)ts_builder.Append(r.ts_exchange_ns);
        (void)sym_builder.Append(r.symbol);
        (void)open_builder.Append(r.open);
        (void)high_builder.Append(r.high);
        (void)low_builder.Append(r.low);
        (void)close_builder.Append(r.close);
        (void)vol_builder.Append(r.volume);
    }

    std::shared_ptr<arrow::Array> ts_arr, sym_arr, open_arr, high_arr, low_arr, close_arr, vol_arr;
    (void)ts_builder.Finish(&ts_arr);
    (void)sym_builder.Finish(&sym_arr);
    (void)open_builder.Finish(&open_arr);
    (void)high_builder.Finish(&high_arr);
    (void)low_builder.Finish(&low_arr);
    (void)close_builder.Finish(&close_arr);
    (void)vol_builder.Finish(&vol_arr);

    auto schema = arrow::schema({
        arrow::field("ts_exchange_ns", arrow::uint64()),
        arrow::field("symbol", arrow::utf8()),
        arrow::field("open", arrow::int64()),
        arrow::field("high", arrow::int64()),
        arrow::field("low", arrow::int64()),
        arrow::field("close", arrow::int64()),
        arrow::field("volume", arrow::int64())
    });

    return arrow::Table::Make(schema, {ts_arr, sym_arr, open_arr, high_arr, low_arr, close_arr, vol_arr});
}

inline void from_columns(const arrow::Table& table, std::vector<OhlcvRecord>& out_records) {
    out_records.clear();
    int64_t num_rows = table.num_rows();
    out_records.reserve(num_rows);

    auto ts_col = table.GetColumnByName("ts_exchange_ns");
    auto sym_col = table.GetColumnByName("symbol");
    auto open_col = table.GetColumnByName("open");
    auto high_col = table.GetColumnByName("high");
    auto low_col = table.GetColumnByName("low");
    auto close_col = table.GetColumnByName("close");
    auto vol_col = table.GetColumnByName("volume");

    if (!ts_col || !open_col || !close_col) return;

    for (int64_t i = 0; i < num_rows; ++i) {
        OhlcvRecord r;
        if (ts_col) r.ts_exchange_ns = std::static_pointer_cast<arrow::UInt64Array>(ts_col->chunk(0))->Value(i);
        if (sym_col) r.symbol = std::static_pointer_cast<arrow::StringArray>(sym_col->chunk(0))->GetString(i);
        if (open_col) r.open = std::static_pointer_cast<arrow::Int64Array>(open_col->chunk(0))->Value(i);
        if (high_col) r.high = std::static_pointer_cast<arrow::Int64Array>(high_col->chunk(0))->Value(i);
        if (low_col) r.low = std::static_pointer_cast<arrow::Int64Array>(low_col->chunk(0))->Value(i);
        if (close_col) r.close = std::static_pointer_cast<arrow::Int64Array>(close_col->chunk(0))->Value(i);
        if (vol_col) r.volume = std::static_pointer_cast<arrow::Int64Array>(vol_col->chunk(0))->Value(i);
        out_records.push_back(r);
    }
}

}  // namespace tick_db
