#pragma once

#include <cstdint>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <filesystem>

namespace tick_db {

class SymbolCatalog {
   public:
    SymbolCatalog() = default;

    static SymbolCatalog& instance() {
        static SymbolCatalog cat;
        return cat;
    }

    uint32_t get_or_create_id(const std::string& symbol) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sym_to_id_.find(symbol);
        if (it != sym_to_id_.end()) {
            return it->second;
        }
        uint32_t id = static_cast<uint32_t>(id_to_sym_.size()) + 1;
        sym_to_id_[symbol] = id;
        id_to_sym_.push_back(symbol);
        return id;
    }

    std::string get_symbol(uint32_t id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (id == 0 || id > id_to_sym_.size()) return "";
        return id_to_sym_[id - 1];
    }

    uint32_t get_id(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sym_to_id_.find(symbol);
        if (it != sym_to_id_.end()) return it->second;
        return 0;
    }

    void save(const std::string& db_path) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::filesystem::path path = std::filesystem::path(db_path) / "symbol_catalog.csv";
        std::ofstream out(path);
        if (!out) {
            std::cerr << "[SymbolCatalog] Failed to open " << path << " for writing.\n";
            return;
        }
        for (const auto& sym : id_to_sym_) {
            out << sym << "\n";
        }
    }

    void load(const std::string& db_path) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::filesystem::path path = std::filesystem::path(db_path) / "symbol_catalog.csv";
        std::ifstream in(path);
        if (!in) return; // OK if not exists

        sym_to_id_.clear();
        id_to_sym_.clear();
        std::string line;
        while (std::getline(in, line)) {
            while (!line.empty() && std::isspace(line.back())) line.pop_back();
            if (line.empty()) continue;
            uint32_t id = static_cast<uint32_t>(id_to_sym_.size()) + 1;
            sym_to_id_[line] = id;
            id_to_sym_.push_back(line);
        }
    }

   private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, uint32_t> sym_to_id_;
    std::vector<std::string> id_to_sym_;
};

}  // namespace tick_db
