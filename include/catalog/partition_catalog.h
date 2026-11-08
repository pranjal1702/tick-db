#pragma once

#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace tick_db {

class PartitionCatalog {
   public:
    static PartitionCatalog& instance() {
        static PartitionCatalog catalog;
        return catalog;
    }

    void scan_or_refresh(const std::string& db_root_path, bool force = false) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (scanned_ && !force && db_root_path == last_root_) {
            return;
        }

        std::filesystem::path root(db_root_path);
        if (!std::filesystem::exists(root)) {
            return;
        }

        partitions_.clear();
        last_root_ = db_root_path;

        for (auto it = std::filesystem::recursive_directory_iterator(root);
             it != std::filesystem::recursive_directory_iterator(); ++it) {
            if (it->is_regular_file() && it->path().filename() == "data.parquet") {
                std::string full_path = it->path().string();
                size_t date_pos = full_path.find("date=");
                if (date_pos != std::string::npos) {
                    std::string date_str = full_path.substr(date_pos + 5, 10);
                    // Extract schema name: parent folder of data.parquet
                    std::string schema_name = it->path().parent_path().filename().string();
                    partitions_[date_str][schema_name] = full_path;
                }
            }
        }

        scanned_ = true;
    }

    std::vector<std::string> find_files(const std::string& db_root_path, const std::string& schema_name,
                                        const std::string& start_date = "", const std::string& end_date = "") {
        scan_or_refresh(db_root_path);

        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> matches;

        auto it_start = start_date.empty() ? partitions_.begin() : partitions_.lower_bound(start_date);
        auto it_end = end_date.empty() ? partitions_.end() : partitions_.upper_bound(end_date);

        for (auto it = it_start; it != it_end; ++it) {
            const auto& schema_map = it->second;
            auto s_it = schema_map.find(schema_name);
            if (s_it != schema_map.end()) {
                matches.push_back(s_it->second);
            }
        }

        return matches;
    }

    size_t total_partitions() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return partitions_.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        partitions_.clear();
        scanned_ = false;
    }

   private:
    PartitionCatalog() = default;

    mutable std::mutex mutex_;
    std::string last_root_;
    bool scanned_{false};
    // Map of Date (YYYY-MM-DD) -> (Schema -> FilePath)
    std::map<std::string, std::map<std::string, std::string>> partitions_;
};

}  // namespace tick_db

