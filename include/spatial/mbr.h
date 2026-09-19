#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace tick_db {

struct DynamicMBR {
    std::vector<int64_t> min_bounds;
    std::vector<int64_t> max_bounds;

    bool is_empty() const { return min_bounds.empty(); }
    size_t dimensions() const { return min_bounds.size(); }

    bool intersects(const DynamicMBR& other) const {
        if (is_empty() || other.is_empty()) return false;
        size_t dims = std::min(min_bounds.size(), other.min_bounds.size());
        for (size_t i = 0; i < dims; ++i) {
            if (max_bounds[i] < other.min_bounds[i] || min_bounds[i] > other.max_bounds[i]) {
                return false;
            }
        }
        return true;
    }

    void expand(const DynamicMBR& other) {
        if (other.is_empty()) return;
        if (is_empty()) {
            min_bounds = other.min_bounds;
            max_bounds = other.max_bounds;
            return;
        }
        size_t dims = std::min(min_bounds.size(), other.min_bounds.size());
        for (size_t i = 0; i < dims; ++i) {
            min_bounds[i] = std::min(min_bounds[i], other.min_bounds[i]);
            max_bounds[i] = std::max(max_bounds[i], other.max_bounds[i]);
        }
    }
};

template <size_t N>
struct MBR {
    std::array<int64_t, N> min_bounds{};
    std::array<int64_t, N> max_bounds{};
    bool empty{true};

    constexpr MBR() {
        min_bounds.fill(std::numeric_limits<int64_t>::max());
        max_bounds.fill(std::numeric_limits<int64_t>::min());
    }

    operator DynamicMBR() const {
        DynamicMBR dyn;
        if (!empty) {
            dyn.min_bounds.assign(min_bounds.begin(), min_bounds.end());
            dyn.max_bounds.assign(max_bounds.begin(), max_bounds.end());
        }
        return dyn;
    }

    constexpr bool is_empty() const { return empty; }
    constexpr static size_t dimensions() { return N; }

    constexpr bool intersects(const MBR<N>& other) const {
        if (empty || other.empty) return false;
        for (size_t i = 0; i < N; ++i) {
            if (max_bounds[i] < other.min_bounds[i] || min_bounds[i] > other.max_bounds[i]) {
                return false;  // Subtree pruned on dimension i!
            }
        }
        return true;
    }

    constexpr bool intersects(const DynamicMBR& other) const {
        if (empty || other.is_empty()) return false;
        size_t dims = std::min(N, other.dimensions());
        for (size_t i = 0; i < dims; ++i) {
            if (max_bounds[i] < other.min_bounds[i] || min_bounds[i] > other.max_bounds[i]) {
                return false;  // Subtree pruned on dimension i!
            }
        }
        return true;
    }

    constexpr void expand(const MBR<N>& other) {
        if (other.empty) return;
        if (empty) {
            min_bounds = other.min_bounds;
            max_bounds = other.max_bounds;
            empty = false;
            return;
        }
        for (size_t i = 0; i < N; ++i) {
            min_bounds[i] = std::min(min_bounds[i], other.min_bounds[i]);
            max_bounds[i] = std::max(max_bounds[i], other.max_bounds[i]);
        }
    }
};

}  // namespace tick_db
