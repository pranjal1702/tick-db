#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "schemas/base.h"

namespace tick_db {

struct FileEntry {
    std::string symbol;
    SchemaType schema_type{SchemaType::Trade};
    uint64_t min_ts{0};
    uint64_t max_ts{0};
    int64_t min_price{0};
    int64_t max_price{0};
    int64_t num_rows{0};
    std::string filepath;
};

class Catalog {
   public:
    Catalog() = default;
    ~Catalog() = default;

    // Register a file segment in the catalog
    void add_file(const FileEntry& entry);

    // Query candidate files matching symbol, time range, and price range
    std::vector<std::string> query_files(const std::string& symbol, uint64_t min_ts, uint64_t max_ts,
                                         int64_t min_price = std::numeric_limits<int64_t>::min(),
                                         int64_t max_price = std::numeric_limits<int64_t>::max()) const;

    size_t total_files() const;
    void clear();

   private:
    std::vector<FileEntry> entries_;
    mutable std::mutex mutex_;
};

}  // namespace tick_db
