#include "ants_assets/mirroring.hpp"
#include <cmath>
#include <cstring>
#include <utility>

namespace ants::assets {

Direction angle_to_direction(float angle_degrees) noexcept {
    float norm = std::fmod(angle_degrees, 360.0f);
    if (norm < 0.0f) {
        norm += 360.0f;
    }
    int sector = static_cast<int>(std::floor((norm + 22.5f) / 45.0f)) % 8;
    return static_cast<Direction>(sector);
}

Direction vector_to_direction(int32_t dx, int32_t dy) noexcept {
    if (dx == 0 && dy == 0) {
        return Direction::South;
    }
    // +X = East, -Y = North, +Y = South, -X = West
    constexpr float RAD_TO_DEG = 180.0f / 3.14159265358979323846f;
    float angle = std::atan2(static_cast<float>(dx), -static_cast<float>(dy)) * RAD_TO_DEG;
    return angle_to_direction(angle);
}

const char* direction_to_string(Direction dir) noexcept {
    switch (static_cast<size_t>(dir) & 7) {
        case 0: return "North";
        case 1: return "NorthEast";
        case 2: return "East";
        case 3: return "SouthEast";
        case 4: return "South";
        case 5: return "SouthWest";
        case 6: return "West";
        case 7: return "NorthWest";
        default: return "Unknown";
    }
}

void mirror_pixel_buffer(const uint8_t* src, uint8_t* dst,
                         uint32_t width, uint32_t height, uint32_t pitch) noexcept {
    if (!src || !dst || width == 0 || height == 0 || pitch < width) {
        return;
    }

    // In-place mirroring branch (swapping pixels along scanline)
    if (src == dst) {
        const uint32_t half_w = width / 2;
        for (uint32_t y = 0; y < height; ++y) {
            uint8_t* row = dst + (static_cast<size_t>(y) * pitch);
            for (uint32_t x = 0; x < half_w; ++x) {
                std::swap(row[x], row[width - 1 - x]);
            }
            if (pitch > width) {
                std::memset(row + width, 0, pitch - width);
            }
        }
        return;
    }

    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* src_row = src + (static_cast<size_t>(y) * pitch);
        uint8_t* dst_row = dst + (static_cast<size_t>(y) * pitch);

        for (uint32_t x = 0; x < width; ++x) {
            dst_row[x] = src_row[width - 1 - x];
        }

        // Clear stride padding
        if (pitch > width) {
            std::memset(dst_row + width, 0, pitch - width);
        }
    }
}

const char* get_lunchbox_prefix(Direction dir) noexcept {
    switch (static_cast<size_t>(dir) & 7) {
        case 0: return "7lb"; // North
        case 1: return "6lb"; // NorthEast
        case 2: return "5lb"; // East
        case 3: return "4lb"; // SouthEast
        case 4: return "3lb"; // South
        case 5: return "4lb"; // SouthWest (mirrored 4lb)
        case 6: return "5lb"; // West (mirrored 5lb)
        case 7: return "6lb"; // NorthWest (mirrored 6lb)
        default: return "7lb";
    }
}

const char* get_lunchbox_prefix(Direction8 dir) noexcept {
    return get_lunchbox_prefix(static_cast<Direction>(dir));
}

} // namespace ants::assets
