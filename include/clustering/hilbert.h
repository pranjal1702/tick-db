#pragma once

#include <cstdint>
#include <utility>

namespace tick_db {

// 2D Hilbert Space-Filling Curve Encoder
// Maps 2D coordinates (time_bucket, price_bucket) in [0, 65535]^2 into a continuous 32-bit Hilbert distance.
// Unlike Morton Z-order bit-interleaving, the Hilbert curve guarantees that consecutive points
// along the curve have Manhattan distance 1, eliminating discrete "Z-jumps" and maximizing spatial locality.
inline uint32_t hilbert_encode(uint16_t x_coord, uint16_t y_coord) {
    uint32_t d = 0;
    uint32_t x = x_coord;
    uint32_t y = y_coord;

    for (uint32_t s = (1U << 15); s > 0; s >>= 1) {
        uint32_t rx = (x & s) ? 1 : 0;
        uint32_t ry = (y & s) ? 1 : 0;
        d += s * s * ((3 * rx) ^ ry);

        if (ry == 0) {
            if (rx == 1) {
                x = (s * 2 - 1) - x;
                y = (s * 2 - 1) - y;
            }
            std::swap(x, y);
        }
    }
    return d;
}

}  // namespace tick_db

