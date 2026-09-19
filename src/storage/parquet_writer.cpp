#include "storage/parquet_writer.h"

#include <arrow/io/api.h>
#include <parquet/arrow/writer.h>

#include <algorithm>
#include <iostream>

namespace tick_db {

bool ParquetWriter::write_trades(const std::string& filepath, std::span<const Trade> trades, int64_t records_per_page) {
    if (trades.empty()) {
        std::cerr << "[ParquetWriter] Cannot write empty trades to " << filepath << "\n";
        return false;
    }

    // 1. Physically sort records strictly time-ascending (ts_exchange_ns)
    std::vector<Trade> sorted_trades(trades.begin(), trades.end());
    std::sort(sorted_trades.begin(), sorted_trades.end(), [](const Trade& a, const Trade& b) {
        if (a.ts_exchange_ns != b.ts_exchange_ns) {
            return a.ts_exchange_ns < b.ts_exchange_ns;
        }
        return a.seq_no < b.seq_no;
    });

    // 2. Convert to Arrow Table via stateless to_columns transform
    auto table = to_columns(std::span<const Trade>(sorted_trades.data(), sorted_trades.size()));
    return write_table(filepath, table, records_per_page);
}

bool ParquetWriter::write_table(const std::string& filepath, const std::shared_ptr<arrow::Table>& table,
                                int64_t records_per_page) {
    return write_table_with_metadata(filepath, table, nullptr, records_per_page);
}

bool ParquetWriter::write_table_with_metadata(const std::string& filepath, const std::shared_ptr<arrow::Table>& table,
                                         const std::shared_ptr<arrow::KeyValueMetadata>& kv_metadata,
                                         int64_t records_per_page) {
    if (!table || table->num_rows() == 0) {
        std::cerr << "[ParquetWriter] Cannot write null or empty table to " << filepath << "\n";
        return false;
    }

    auto out_result = arrow::io::FileOutputStream::Open(filepath);
    if (!out_result.ok()) {
        std::cerr << "[ParquetWriter] Failed to open output file: " << filepath << " - "
                  << out_result.status().ToString() << "\n";
        return false;
    }
    std::shared_ptr<arrow::io::FileOutputStream> outfile = *out_result;

    std::shared_ptr<arrow::Table> write_tbl = table;
    if (kv_metadata) {
        write_tbl = table->ReplaceSchemaMetadata(kv_metadata);
    }

    auto writer_props = parquet::WriterProperties::Builder()
                            .compression(parquet::Compression::SNAPPY)
                            ->enable_write_page_index()
                            ->data_pagesize(records_per_page * 64)
                            ->build();

    auto arrow_props = parquet::ArrowWriterProperties::Builder().store_schema()->build();

    auto status =
        parquet::arrow::WriteTable(*write_tbl, arrow::default_memory_pool(), outfile, 65536, writer_props, arrow_props);
    if (!status.ok()) {
        std::cerr << "[ParquetWriter] WriteTable failed: " << status.ToString() << "\n";
        return false;
    }

    return true;
}

}  // namespace tick_db
