#pragma once

#include <cmath>
#include <concepts>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "catalog/symbol_catalog.h"
#include "spatial/mbr.h"

namespace tick_db {

template <typename T>
concept SpatialRecord = requires(const T& r) {
    { r.spatial_tuple() };
};

template <SpatialRecord Record>
constexpr size_t record_dimensions_v = std::tuple_size_v<decltype(std::declval<Record>().spatial_tuple())>;

template <SpatialRecord Record>
using RecordMBR = MBR<record_dimensions_v<Record>>;

template <SpatialRecord Record>
RecordMBR<Record> extract_mbr(const Record& record) {
    RecordMBR<Record> mbr;
    mbr.empty = false;
    size_t idx = 0;
    std::apply(
        [&](auto&&... args) {
            (([&](auto&& val) {
                 using FieldType = std::decay_t<decltype(val)>;
                 if constexpr (std::is_integral_v<FieldType> || std::is_enum_v<FieldType>) {
                     mbr.min_bounds[idx] = static_cast<int64_t>(val);
                 } else if constexpr (std::is_floating_point_v<FieldType>) {
                     mbr.min_bounds[idx] = static_cast<int64_t>(val * 100000000.0);
                 } else if constexpr (std::is_same_v<FieldType, std::string>) {
                     uint64_t hash_val = std::hash<std::string>{}(val);
                     mbr.min_bounds[idx] = static_cast<int64_t>(hash_val);
                 }
                 mbr.max_bounds[idx] = mbr.min_bounds[idx];
                 idx++;
             }(args)),
             ...);
        },
        record.spatial_tuple());
    return mbr;
}

template <SpatialRecord Record>
RecordMBR<Record> extract_mbr(std::span<const Record> records) {
    RecordMBR<Record> mbr;
    if (records.empty()) return mbr;
    mbr = extract_mbr(records[0]);
    for (size_t i = 1; i < records.size(); ++i) {
        mbr.expand(extract_mbr(records[i]));
    }
    return mbr;
}

template <SpatialRecord Record>
uint64_t extract_symbol_bitmask(const Record& record) {
    uint64_t mask = 0;
    std::apply(
        [&](auto&&... args) {
            (([&](auto&& val) {
                 using FieldType = std::decay_t<decltype(val)>;
                 if constexpr (std::is_integral_v<FieldType>) {
                     uint64_t bit_pos = static_cast<uint64_t>(val) % 64;
                     mask |= (1ULL << bit_pos);
                 } else if constexpr (std::is_same_v<FieldType, std::string>) {
                     uint64_t hash_val = std::hash<std::string>{}(val);
                     uint64_t bit_pos = hash_val % 64;
                     mask |= (1ULL << bit_pos);
                 }
             }(args)),
             ...);
        },
        record.spatial_tuple());
    return mask;
}

template <SpatialRecord Record>
uint64_t extract_symbol_bitmask(std::span<const Record> records) {
    uint64_t mask = 0;
    for (const auto& r : records) {
        mask |= extract_symbol_bitmask(r);
    }
    return mask;
}

template <SpatialRecord Record>
uint32_t extract_primary_symbol_id(const Record& record) {
    uint32_t sym_id = 0;
    std::apply(
        [&](auto&&... args) {
            (([&](auto&& val) {
                 using FieldType = std::decay_t<decltype(val)>;
                 if constexpr (std::is_same_v<FieldType, std::string>) {
                     if (sym_id == 0) sym_id = SymbolCatalog::instance().get_or_create_id(val);
                 }
             }(args)),
             ...);
        },
        record.spatial_tuple());
    return sym_id;
}

}  // namespace tick_db
