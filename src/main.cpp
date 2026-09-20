#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "backfill/csv_backfiller.h"
#include "catalog/database_config.h"

void print_ingest_usage() {
    std::cout << "Usage: tick_db_ingest --csv <file.csv> --schema <trades|ohlcv> --date <YYYY-MM-DD> --db-path <dir>\n";
}

int main(int argc, char** argv) {
    std::string csv_path = "";
    std::string schema = "";
    std::string date_str = "";
    std::string symbol_str = "UNKNOWN";
    std::string db_path = "./market_data";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--csv" && i + 1 < argc) {
            csv_path = argv[++i];
        } else if (arg == "--schema" && i + 1 < argc) {
            schema = argv[++i];
        } else if (arg == "--date" && i + 1 < argc) {
            date_str = argv[++i];
        } else if (arg == "--db-path" && i + 1 < argc) {
            db_path = argv[++i];
        } else if (arg == "--symbol" && i + 1 < argc) {
            symbol_str = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            print_ingest_usage();
            return 0;
        }
    }

    if (csv_path.empty() || schema.empty() || date_str.empty()) {
        std::cerr << "[tick_db_ingest] Error: Missing required arguments.\n";
        print_ingest_usage();
        return 1;
    }

    tick_db::DatabaseConfig::instance().set_db_root_path(db_path);

    // Compute target path: db_path/date=YYYY-MM-DD/schema/data.parquet
    // Using DatabaseConfig or manual format to match expectations
    std::filesystem::path root(db_path);
    std::filesystem::path target_dir = root / ("date=" + date_str) / schema;
    
    // Ensure the output directory exists
    std::filesystem::create_directories(target_dir);
    
    std::string out_path = (target_dir / "data.parquet").string();

    std::cout << "[tick_db_ingest] Starting CSV Backfill...\n";
    std::cout << "[tick_db_ingest] Input CSV : " << csv_path << "\n";
    std::cout << "[tick_db_ingest] Schema    : " << schema << "\n";
    std::cout << "[tick_db_ingest] Output    : " << out_path << "\n";

    bool success = false;
    if (schema == "ohlcv" || schema == "olhcv") { // support common typo "olhcv"
        success = tick_db::CsvBackfiller::backfill_ohlcv_from_csv(csv_path, out_path, symbol_str);
    } else if (schema == "trades" || schema == "trade") {
        success = tick_db::CsvBackfiller::backfill_trades_from_csv(csv_path, out_path, symbol_str);
    } else {
        std::cerr << "[tick_db_ingest] Error: Unsupported schema: " << schema << "\n";
        return 1;
    }

    if (success) {
        std::cout << "[tick_db_ingest] Successfully ingested and created embedded R-Tree Parquet file!\n";
    } else {
        std::cerr << "[tick_db_ingest] Failed to ingest CSV data.\n";
        return 1;
    }

    return 0;
}
