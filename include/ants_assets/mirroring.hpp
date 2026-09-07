#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <string>
#include <ostream>

namespace ants::assets {

/**
 * @brief Discrete 8-way compass heading angles utilized by simulation and rendering.
 */
enum class Direction : uint8_t {
    North     = 0, //   0° (Heading back to viewer; stored in CHD as 7)
    NorthEast = 1, //  45° (Stored in CHD as 8)
    East      = 2, //  90° (Stored in CHD as 9)
    SouthEast = 3, // 135° (Stored in CHD as 2)
    South     = 4, // 180° (Heading front to viewer; stored in CHD as 3)
    SouthWest = 5, // 225° (Horizontally mirrored from SouthEast / 2)
    West      = 6, // 270° (Horizontally mirrored from East / 9)
    NorthWest = 7  // 315° (Horizontally mirrored from NorthEast / 8)
};

// Compatibility enum alias
enum Direction8 : uint8_t {
    DIR_N  = 0,
    DIR_NE = 1,
    DIR_E  = 2,
    DIR_SE = 3,
    DIR_S  = 4,
    DIR_SW = 5,
    DIR_W  = 6,
    DIR_NW = 7
};

/**
 * @brief Direction mapping entry translating runtime direction to CHD asset representation.
 */
struct DirectionMapping {
    uint8_t   chd_code;   // Base digit in CHD animation name: 7, 8, 9, 2, 3
    bool      mirrored;   // True if horizontal reflection is required (SW, W, NW)
    uint8_t   base_index; // 0..4 index into base stored directions
};

/**
 * @brief Pre-computed lookup table mapping runtime Direction (0..7) to CHD source direction.
 */
constexpr std::array<DirectionMapping, 8> DIRECTION_MAP = {{
    /* [0] North     */ { 7, false, 0 },
    /* [1] NorthEast */ { 8, false, 1 },
    /* [2] East      */ { 9, false, 2 },
    /* [3] SouthEast */ { 2, false, 3 },
    /* [4] South     */ { 3, false, 4 },
    /* [5] SouthWest */ { 2, true,  3 }, // Flipped SouthEast (2)
    /* [6] West      */ { 9, true,  2 }, // Flipped East (9)
    /* [7] NorthWest */ { 8, true,  1 }  // Flipped NorthEast (8)
}};

/**
 * @brief Gets the direction mapping entry for a given heading.
 */
constexpr inline DirectionMapping get_direction_mapping(Direction dir) noexcept {
    return DIRECTION_MAP[static_cast<size_t>(dir) & 7];
}

constexpr inline DirectionMapping get_direction_mapping(Direction8 dir) noexcept {
    return DIRECTION_MAP[static_cast<size_t>(dir) & 7];
}

/**
 * @brief Converts an angle in degrees [0, 360) to the closest 8-directional heading.
 */
Direction angle_to_direction(float angle_degrees) noexcept;

/**
 * @brief Converts a 2D velocity vector (dx, dy) to the closest 8-directional heading.
 * Assumes 2D screen coordinate system where +X is East and +Y is South.
 */
Direction vector_to_direction(int32_t dx, int32_t dy) noexcept;

/**
 * @brief Returns the human-readable compass name (e.g. "North-East").
 */
const char* direction_to_string(Direction dir) noexcept;

inline std::ostream& operator<<(std::ostream& os, Direction dir) {
    return os << direction_to_string(dir);
}

// ============================================================================
// Horizontal Reflection Transformation Mathematics
// ============================================================================

/**
 * @brief Computes the mirrored horizontal render offset dx' for a sprite.
 * 
 * In ants.chd Table 4, a frame specifies top-left render offset dx relative to the
 * ant's tile anchor. Its native horizontal span is [dx, dx + W]. Under horizontal
 * reflection across the anchor (x -> -x), the span transforms to [-(dx + W), -dx].
 * Thus, the new top-left render offset is dx' = -(dx + W).
 *
 * @param dx Original horizontal frame offset
 * @param width Visible bounding width of the sprite in pixels
 * @return int32_t Transformed horizontal offset dx'
 */
constexpr inline int32_t mirror_dx(int32_t dx, uint32_t width) noexcept {
    return -(dx + static_cast<int32_t>(width));
}

/**
 * @brief Computes the vertical render offset dy' under horizontal reflection.
 * Invariant under horizontal reflection: dy' = dy.
 */
constexpr inline int32_t mirror_dy(int32_t dy) noexcept {
    return dy;
}

/**
 * @brief Mirrors an interaction / collision bounding box horizontally across the center.
 * [left, right] -> [-right, -left].
 */
constexpr inline void mirror_bounding_box(int32_t left, int32_t right,
                                          int32_t& out_left, int32_t& out_right) noexcept {
    const int32_t temp_left  = -right;
    const int32_t temp_right = -left;
    out_left  = temp_left;
    out_right = temp_right;
}

/**
 * @brief Horizontally flips an 8-bit paletted pixel buffer.
 * 
 * Pixel column x maps to (width - 1 - x). Row stride padding bytes (pitch > width)
 * are cleared to 0x00.
 *
 * @param src Pointer to source paletted pixel buffer
 * @param dst Pointer to destination buffer (must be at least pitch * height bytes)
 * @param width Visible sprite width in pixels
 * @param height Visible sprite height in pixels
 * @param pitch Row stride in bytes
 */
void mirror_pixel_buffer(const uint8_t* src, uint8_t* dst,
                         uint32_t width, uint32_t height, uint32_t pitch) noexcept;

// ============================================================================
// Lunchbox Directional Mapping
// ============================================================================

/**
 * @brief Returns the 3-character lunchbox sprite prefix for a given ant heading.
 * Base prefixes in ants.chd:
 * - North (0):     "7lb"
 * - NorthEast (1): "6lb"
 * - East (2):      "5lb"
 * - SouthEast (3): "4lb"
 * - South (4):     "3lb"
 * - SouthWest (5): "4lb" (mirrored)
 * - West (6):      "5lb" (mirrored)
 * - NorthWest (7): "6lb" (mirrored)
 */
const char* get_lunchbox_prefix(Direction dir) noexcept;
const char* get_lunchbox_prefix(Direction8 dir) noexcept;

/**
 * @brief Indicates whether the lunchbox sprite must be horizontally mirrored.
 */
constexpr inline bool is_lunchbox_mirrored(Direction dir) noexcept {
    return get_direction_mapping(dir).mirrored;
}

constexpr inline bool is_lunchbox_mirrored(Direction8 dir) noexcept {
    return get_direction_mapping(dir).mirrored;
}

} // namespace ants::assets
