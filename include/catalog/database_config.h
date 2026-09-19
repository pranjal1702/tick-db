#pragma once

#include <filesystem>
#include <mutex>
#include <string>

namespace tick_db {

class DatabaseConfig {
   public:
    static DatabaseConfig& instance() {
        static DatabaseConfig cfg;
        return cfg;
    }

    void set_db_root_path(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        db_root_path_ = path;
    }

    std::string db_root_path() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return db_root_path_;
    }

    std::string get_parquet_path(const std::string& date, const std::string& schema_name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::filesystem::path p(db_root_path_);
        p /= ("date=" + date);
        p /= schema_name;
        p /= "data.parquet";
        return p.string();
    }

   private:
    DatabaseConfig() : db_root_path_("./market_data") {}
    mutable std::mutex mutex_;
    std::string db_root_path_;
};

}  // namespace tick_db
