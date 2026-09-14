#pragma once

#include <cstdint>

namespace tick_db {

// Interleave 16 bits of a coordinate into 32 bits (bits 0, 2, 4, 6...)
inline uint32_t spread_bits(uint16_t v) {
    uint32_t x = v;
    x = (x | (x << 8)) & 0x00FF00FF;
    x = (x | (x << 4)) & 0x0F0F0F0F;
    x = (x | (x << 2)) & 0x33333333;
    x = (x | (x << 1)) & 0x55555555;
    return x;
}

// 2D Morton Z-order curve bit-interleaving key calculation
inline uint32_t morton_encode(uint16_t time_bucket, uint16_t price_bucket) {
    return spread_bits(time_bucket) | (spread_bits(price_bucket) << 1);
}

}  // namespace tick_db
