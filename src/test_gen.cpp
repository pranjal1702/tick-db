#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/writer.h>

#include <iostream>
#include <memory>

int main() {
    arrow::UInt64Builder ts_exchange_builder;
    arrow::UInt64Builder ts_local_builder;
    arrow::DoubleBuilder price_builder;
    arrow::UInt64Builder size_builder;
    arrow::StringBuilder side_builder;
    arrow::StringBuilder symbol_builder;
    arrow::UInt64Builder seq_no_builder;

    for (int i = 1; i <= 20; ++i) {
        (void)ts_exchange_builder.Append(1700000000000000000ULL + i * 1000);
        (void)ts_local_builder.Append(1700000000000000100ULL + i * 1000);
        (void)price_builder.Append(150.25 + i * 0.05);
        (void)size_builder.Append(100 + i * 10);
        (void)side_builder.Append(i % 2 == 0 ? "B" : "S");
        (void)symbol_builder.Append("AAPL");
        (void)seq_no_builder.Append(i);
    }

    std::shared_ptr<arrow::Array> ts_exchange, ts_local, price, size, side, symbol, seq_no;
    (void)ts_exchange_builder.Finish(&ts_exchange);
    (void)ts_local_builder.Finish(&ts_local);
    (void)price_builder.Finish(&price);
    (void)size_builder.Finish(&size);
    (void)side_builder.Finish(&side);
    (void)symbol_builder.Finish(&symbol);
    (void)seq_no_builder.Finish(&seq_no);

    auto schema = arrow::schema({arrow::field("ts_exchange_ns", arrow::uint64()),
                                 arrow::field("ts_local_ns", arrow::uint64()), arrow::field("price", arrow::float64()),
                                 arrow::field("size", arrow::uint64()), arrow::field("side", arrow::utf8()),
                                 arrow::field("symbol", arrow::utf8()), arrow::field("seq_no", arrow::uint64())});

    auto table = arrow::Table::Make(schema, {ts_exchange, ts_local, price, size, side, symbol, seq_no});

    auto file_result = arrow::io::FileOutputStream::Open("/tmp/test_trades.parquet");
    if (!file_result.ok()) {
        std::cerr << "Failed to open output file: " << file_result.status().ToString() << "\n";
        return 1;
    }
    std::shared_ptr<arrow::io::FileOutputStream> outfile = *file_result;

    auto write_status = parquet::arrow::WriteTable(*table, arrow::default_memory_pool(), outfile, 20);
    if (!write_status.ok()) {
        std::cerr << "Failed to write parquet table: " << write_status.ToString() << "\n";
        return 1;
    }

    std::cout << "Successfully generated /tmp/test_trades.parquet via Arrow C++!\n";
    return 0;
}
