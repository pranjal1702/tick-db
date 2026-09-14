#include "catalog/catalog.h"

#include <algorithm>

namespace tick_db {

void Catalog::add_file(const FileEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.push_back(entry);
}

std::vector<std::string> Catalog::query_files(const std::string& symbol, uint64_t min_ts, uint64_t max_ts,
                                              int64_t min_price, int64_t max_price) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> matches;

    for (const auto& e : entries_) {
        if (e.symbol != symbol) continue;

        // Check time range overlap: [e.min_ts, e.max_ts] overlaps [min_ts, max_ts]
        if (e.max_ts < min_ts || e.min_ts > max_ts) continue;

        // Check price range overlap: [e.min_price, e.max_price] overlaps
        // [min_price, max_price]
        if (e.max_price < min_price || e.min_price > max_price) continue;

        matches.push_back(e.filepath);
    }

    return matches;
}

size_t Catalog::total_files() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

void Catalog::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
}

}  // namespace tick_db
