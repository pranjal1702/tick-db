#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

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

   private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, uint32_t> sym_to_id_;
    std::vector<std::string> id_to_sym_;
};

}  // namespace tick_db
