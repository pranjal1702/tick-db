#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace tick_db {

enum class Side : uint8_t { None = 0, Bid = 'B', Ask = 'S' };

enum class SchemaType : uint8_t { Trade = 1, Quote = 2, BookLevel = 3, OrderEvent = 4 };

using RawColumnData = std::variant<std::vector<uint64_t>, std::vector<int64_t>, std::vector<double>,
                                   std::vector<uint8_t>, std::vector<std::string> >;

struct RawColumn {
    std::string name;
    RawColumnData data;
};

}  // namespace tick_db