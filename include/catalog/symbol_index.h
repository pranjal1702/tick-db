#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace tick_db {

struct SymbolRange {
    uint32_t row_group_id{0};
    uint32_t start_row{0};
    uint32_t count{0};
};

class SymbolIndexDirectory {
   public:
    SymbolIndexDirectory() = default;

    void add_row_group(uint32_t symbol_id, uint32_t row_group_id) {
        auto& rgs = symbol_to_rgs_[symbol_id];
        if (rgs.empty() || rgs.back() != row_group_id) {
            rgs.push_back(row_group_id);
        }
    }

    void add_symbol_range(uint32_t symbol_id, uint32_t row_group_id, uint32_t start_row, uint32_t count) {
        add_row_group(symbol_id, row_group_id);
        symbol_to_ranges_[symbol_id].push_back({row_group_id, start_row, count});
    }

    const std::vector<uint32_t>* get_row_groups(uint32_t symbol_id) const {
        auto it = symbol_to_rgs_.find(symbol_id);
        if (it != symbol_to_rgs_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    const std::vector<SymbolRange>* get_symbol_ranges(uint32_t symbol_id) const {
        auto it = symbol_to_ranges_.find(symbol_id);
        if (it != symbol_to_ranges_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    // Adaptive Gap-Tolerant Range Merging: Collapses adjacent ranges separated by <= max_gap rows
    std::vector<SymbolRange> get_compact_symbol_ranges(uint32_t symbol_id, uint32_t max_gap = 4) const {
        const auto* raw_ranges = get_symbol_ranges(symbol_id);
        if (!raw_ranges || raw_ranges->empty()) return {};

        std::vector<SymbolRange> compact;
        compact.push_back((*raw_ranges)[0]);

        for (size_t i = 1; i < raw_ranges->size(); ++i) {
            const auto& next = (*raw_ranges)[i];
            auto& prev = compact.back();

            if (next.row_group_id == prev.row_group_id &&
                next.start_row <= (prev.start_row + prev.count + max_gap)) {
                uint32_t new_end = std::max(prev.start_row + prev.count, next.start_row + next.count);
                prev.count = new_end - prev.start_row;
            } else {
                compact.push_back(next);
            }
        }
        return compact;
    }

    const std::unordered_map<uint32_t, std::vector<uint32_t>>& map() const { return symbol_to_rgs_; }
    std::unordered_map<uint32_t, std::vector<uint32_t>>& map() { return symbol_to_rgs_; }

    const std::unordered_map<uint32_t, std::vector<SymbolRange>>& ranges_map() const { return symbol_to_ranges_; }
    std::unordered_map<uint32_t, std::vector<SymbolRange>>& ranges_map() { return symbol_to_ranges_; }

   private:
    std::unordered_map<uint32_t, std::vector<uint32_t>> symbol_to_rgs_;
    std::unordered_map<uint32_t, std::vector<SymbolRange>> symbol_to_ranges_;
};

}  // namespace tick_db
