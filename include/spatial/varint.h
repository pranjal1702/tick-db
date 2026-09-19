#pragma once

#include <cstdint>
#include <vector>

namespace tick_db {

class Varint {
   public:
    static void encode(uint32_t value, std::vector<uint8_t>& out) {
        while (value >= 0x80) {
            out.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
            value >>= 7;
        }
        out.push_back(static_cast<uint8_t>(value & 0x7F));
    }

    static uint32_t decode(const uint8_t*& ptr, const uint8_t* end) {
        uint32_t result = 0;
        uint32_t shift = 0;
        while (ptr < end) {
            uint8_t byte = *ptr++;
            result |= static_cast<uint32_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) {
                return result;
            }
            shift += 7;
            if (shift >= 35) break;
        }
        return result;
    }
};

}  // namespace tick_db
