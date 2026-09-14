#pragma once

#include <concepts>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "spatial/rtree.h"

namespace tick_db {

template <typename T>
concept SpatialRecord = requires(const T& r) {
    { r.spatial_tuple() };
};

template <SpatialRecord Record>
GenericMBR extract_mbr(const Record& record) {
    GenericMBR mbr;
    std::apply(
        [&](auto&&... args) {
            (([&](auto&& val) {
                 using FieldType = std::decay_t<decltype(val)>;
                 if constexpr (std::is_integral_v<FieldType> || std::is_enum_v<FieldType>) {
                     mbr.min_bounds.push_back(static_cast<int64_t>(val));
                 } else if constexpr (std::is_same_v<FieldType, std::string>) {
                     // Hash string fields (e.g., symbol "AAPL") to 64-bit integer dimension
                     uint64_t hash_val = std::hash<std::string>{}(val);
                     mbr.min_bounds.push_back(static_cast<int64_t>(hash_val));
                 }
             }(args)),
             ...);
        },
        record.spatial_tuple());
    mbr.max_bounds = mbr.min_bounds;
    return mbr;
}

template <SpatialRecord Record>
GenericMBR extract_mbr(std::span<const Record> records) {
    if (records.empty()) return GenericMBR();
    GenericMBR mbr = extract_mbr(records[0]);
    for (size_t i = 1; i < records.size(); ++i) {
        mbr.expand(extract_mbr(records[i]));
    }
    return mbr;
}

}  // namespace tick_db
