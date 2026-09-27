#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "query/query_engine.h"
#include "query/record_stream.h"
#include "schemas/spatial_concept.h"

namespace tick_db {

struct ParsedSqlQuery {
    std::string table_name;
    std::string symbol;
    uint64_t min_ts{0};
    uint64_t max_ts{static_cast<uint64_t>(std::numeric_limits<int64_t>::max())};
    int64_t min_price{std::numeric_limits<int64_t>::min()};
    int64_t max_price{std::numeric_limits<int64_t>::max()};
    int64_t min_size{std::numeric_limits<int64_t>::min()};
    int64_t max_size{std::numeric_limits<int64_t>::max()};
    Side side{Side::None};
};

class SqlQueryEngine {
   public:
    static ParsedSqlQuery parse(const std::string& sql) {
        ParsedSqlQuery q;
        std::string lower_sql = sql;
        std::transform(lower_sql.begin(), lower_sql.end(), lower_sql.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        // Extract FROM table_name
        size_t from_pos = lower_sql.find("from ");
        if (from_pos != std::string::npos) {
            size_t start = from_pos + 5;
            while (start < sql.size() && std::isspace(sql[start])) start++;
            size_t end = start;
            while (end < sql.size() && !std::isspace(sql[end]) && sql[end] != ';') end++;
            q.table_name = sql.substr(start, end - start);
        }

        // Extract symbol = 'XYZ' or symbol = "XYZ"
        size_t sym_pos = lower_sql.find("symbol");
        if (sym_pos != std::string::npos) {
            size_t quote1 = sql.find_first_of("'\"", sym_pos);
            if (quote1 != std::string::npos) {
                size_t quote2 = sql.find_first_of("'\"", quote1 + 1);
                if (quote2 != std::string::npos) {
                    q.symbol = sql.substr(quote1 + 1, quote2 - quote1 - 1);
                }
            }
        }

        // Helper to parse human-readable timestamp YYYYMMDD:hh:mm:ss or numeric nanoseconds
        auto parse_ts_token = [](const std::string& tok) -> uint64_t {
            if (tok.empty()) return 0;
            // Check if string contains quotes or colon, e.g. '20230102:09:15:00' or 20230102:09:15:00
            std::string clean = tok;
            clean.erase(std::remove(clean.begin(), clean.end(), '\''), clean.end());
            clean.erase(std::remove(clean.begin(), clean.end(), '"'), clean.end());
            clean.erase(std::remove(clean.begin(), clean.end(), ';'), clean.end());

            // Format check: YYYYMMDD:hh:mm:ss (length 17)
            if (clean.size() == 17 && clean[8] == ':' && clean[11] == ':' && clean[14] == ':') {
                int year = std::stoi(clean.substr(0, 4));
                int month = std::stoi(clean.substr(4, 2));
                int day = std::stoi(clean.substr(6, 2));
                int hour = std::stoi(clean.substr(9, 2));
                int min = std::stoi(clean.substr(12, 2));
                int sec = std::stoi(clean.substr(15, 2));

                struct tm t = {};
                t.tm_year = year - 1900;
                t.tm_mon = month - 1;
                t.tm_mday = day;
                t.tm_hour = hour;
                t.tm_min = min;
                t.tm_sec = sec;
                time_t epoch_sec = timegm(&t);
                if (epoch_sec != -1) {
                    return static_cast<uint64_t>(epoch_sec) * 1000000000ULL;
                }
            }
            // Fallback to direct raw integer nanoseconds
            try {
                return std::stoull(clean);
            } catch (...) {
                return 0;
            }
        };

        // Extract BETWEEN min AND max
        size_t between_pos = lower_sql.find("between ");
        if (between_pos != std::string::npos) {
            std::string after_between = sql.substr(between_pos + 8);
            std::istringstream ss(after_between);
            std::string min_tok, and_tok, max_tok;
            if (ss >> min_tok >> and_tok >> max_tok) {
                q.min_ts = parse_ts_token(min_tok);
                q.max_ts = parse_ts_token(max_tok);
            }
        }

        // Extract price >= X or price > X
        size_t price_pos = lower_sql.find("price");
        if (price_pos != std::string::npos) {
            size_t op_pos = sql.find_first_of("><=", price_pos);
            if (op_pos != std::string::npos) {
                std::istringstream ss(sql.substr(op_pos + 1));
                int64_t p_val = 0;
                if (ss >> p_val) {
                    if (sql[op_pos] == '>') q.min_price = p_val;
                    else if (sql[op_pos] == '<') q.max_price = p_val;
                    else if (sql[op_pos] == '=') { q.min_price = p_val; q.max_price = p_val; }
                }
            }
        }

        // Extract side = 'BUY' or side = 'SELL'
        size_t side_pos = lower_sql.find("side");
        if (side_pos != std::string::npos) {
            if (lower_sql.find("buy", side_pos) != std::string::npos) {
                q.side = Side::Bid;
            } else if (lower_sql.find("sell", side_pos) != std::string::npos) {
                q.side = Side::Ask;
            }
        }

        return q;
    }

    template <SpatialRecord T>
    static RecordStream<T> execute_sql_stream(const std::string& parquet_path, const std::string& sql,
                                             QueryMetrics* out_metrics = nullptr) {
        ParsedSqlQuery parsed = parse(sql);

        DynamicMBR query_mbr;
        size_t dims = record_dimensions_v<T>;
        query_mbr.min_bounds.assign(dims, 0);
        query_mbr.max_bounds.assign(dims, std::numeric_limits<int64_t>::max());

        if (dims > 0) {
            query_mbr.min_bounds[0] = static_cast<int64_t>(parsed.min_ts);
            query_mbr.max_bounds[0] = static_cast<int64_t>(parsed.max_ts);
        }
        if (dims >= 3 && parsed.min_price != std::numeric_limits<int64_t>::min()) {
            query_mbr.min_bounds[2] = parsed.min_price;
        }
        if (dims >= 3 && parsed.max_price != std::numeric_limits<int64_t>::max()) {
            query_mbr.max_bounds[2] = parsed.max_price;
        }

        return QueryEngine::execute_symbol_stream<T>(parquet_path, parquet_path, parsed.symbol, query_mbr, parsed.side, out_metrics);
    }

    template <SpatialRecord T>
    static std::vector<T> execute_sql(const std::string& parquet_path, const std::string& sql,
                                     QueryMetrics* out_metrics = nullptr) {
        auto stream = execute_sql_stream<T>(parquet_path, sql, out_metrics);
        std::vector<T> results;
        while (stream.has_next()) {
            results.push_back(stream.next());
        }
        return results;
    }
};

}  // namespace tick_db
