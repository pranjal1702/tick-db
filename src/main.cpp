#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "storage/parquet_reader.h"

int main(int argc, char** argv) {
    std::string filepath = "/tmp/sample.parquet";
    if (argc > 1) {
        filepath = argv[1];
    }

    std::cout << "[tick-db] Starting Parquet file ingestion tool...\n";
    std::cout << "[tick-db] Target Parquet file: " << filepath << "\n\n";

    tick_db::ParquetReader reader;

    // 1. Print summary metadata
    reader.print_summary(filepath);

    // 2. Read into RawColumn generic schema
    std::vector<tick_db::RawColumn> raw_cols;
    tick_db::ParquetFileInfo info;
    if (reader.read_raw_columns(filepath, raw_cols, &info)) {
        std::cout << "\n[tick-db] Successfully ingested " << raw_cols.size() << " raw columns for " << info.num_rows
                  << " rows.\n";
        for (const auto& col : raw_cols) {
            std::cout << "  - Column '" << col.name << "' loaded with data variant index: " << col.data.index() << "\n";
        }
    } else {
        std::cerr << "[tick-db] Failed to read raw columns from " << filepath << "\n";
    }

    // 3. Read into Trade tick schema
    std::vector<tick_db::Trade> trades;
    if (reader.read_trades(filepath, trades, &info)) {
        std::cout << "\n[tick-db] Successfully ingested " << trades.size() << " trade ticks.\n";
        std::cout << "-------------------------------------------------------------"
                     "-----------------------------\n";
        std::cout << std::left << std::setw(8) << "Index" << std::setw(16) << "Price (fixed)" << std::setw(12) << "Size"
                  << std::setw(8) << "Side" << std::setw(22) << "ts_exchange_ns" << std::setw(12) << "seq_no" << "\n";
        std::cout << "-------------------------------------------------------------"
                     "-----------------------------\n";

        size_t display_count = std::min<size_t>(trades.size(), 10);
        for (size_t i = 0; i < display_count; ++i) {
            const auto& t = trades[i];
            char side_char = (t.side == tick_db::Side::Bid) ? 'B' : ((t.side == tick_db::Side::Ask) ? 'S' : '-');
            std::cout << std::left << std::setw(8) << i << std::setw(16) << t.price << std::setw(12) << t.size
                      << std::setw(8) << side_char << std::setw(22) << t.ts_exchange_ns << std::setw(12) << t.seq_no
                      << "\n";
        }

        if (trades.size() > display_count) {
            std::cout << "... (" << (trades.size() - display_count) << " more trades omitted)\n";
        }
        std::cout << "-------------------------------------------------------------"
                     "-----------------------------\n";
    } else {
        std::cerr << "[tick-db] Failed to ingest trade ticks from " << filepath << "\n";
        return 1;
    }

    std::cout << "\n[tick-db] Ingestion completed successfully.\n";
    return 0;
}
