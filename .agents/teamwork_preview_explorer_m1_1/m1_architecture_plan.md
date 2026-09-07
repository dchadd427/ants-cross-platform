# Milestone 1: Asset Architecture & Public API Design (`ants-assets`)

**Document Version:** 1.0  
**Author:** M1 Explorer 1 (`explorer_m1_1`)  
**Target Module:** `ants-assets` (Milestone 1)  
**Parent Conversation ID:** `a28dfa55-5a82-453d-a21b-99459a66b340`  
**Working Directory:** `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_1`  

---

## 1. Executive Summary

Milestone 1 (`ants-assets`) delivers the zero-dependency, bit-exact native binary asset decoding tier for the Microsoft Ants remake. It directly parses `Original-Ants/ants.chd` (8,411,866 bytes) and all 6 maps in `Original-Ants/Maps/*.LVL` into high-performance, memory-safe C++17 data structures without any pre-conversion, intermediate assets, or external dependencies.

This document establishes:
1. The definitive public C++ API headers under `include/ants_assets/`:
   - `mirroring.hpp`: 5-to-8 directional mirroring math, directional mapping tables, coordinate inversions, and lunchbox mappings.
   - `chd_parser.hpp`: Binary deserialization data structures and routines for `ants.chd` (Header, Palette, Table 1 Sprites, Table 2 Audio, Table 3 Tags, Table 4 Animations).
   - `lvl_parser.hpp`: Complete `.LVL` map parser covering headers, tile dictionaries, 6-byte cell grids (Layer 1 terrain & Layer 2 items), and all 4 trailing configuration blocks with verified 0 remaining bytes.
   - `asset_archive.hpp`: Master asset manager providing $O(1)$ indexed and named lookups, pre-computed 8-directional mirrored sprites, and directional animation queries.
2. The core memory management strategy: contiguous pixel buffers preserving row pitch strides, palette RGBA layout with Color 254 transparency, unsigned 8-bit PCM audio representation with dynamic 44-byte RIFF WAV generation, and RAM atlas pre-computation.
3. The build system configuration: CMake targets for static library `ants_assets` and standalone test harness `test_assets`.
4. The automated verification specification guaranteeing 100% asset coverage with zero memory leaks (ASan clean).

---

## 2. Public C++ Header Blueprints

The public headers are located in `include/ants_assets/` and belong to the `ants::assets` namespace.

### 2.1 `include/ants_assets/mirroring.hpp`

```cpp
#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <string>

namespace ants::assets {

/**
 * @brief Discrete 8-way compass heading angles utilized by the simulation and renderer.
 */
enum class Direction : uint8_t {
    North     = 0, //   0° / 360° (Back to viewer, stored in CHD as 7)
    NorthEast = 1, //  45°        (Stored in CHD as 8)
    East      = 2, //  90°        (Stored in CHD as 9)
    SouthEast = 3, // 135°        (Stored in CHD as 2)
    South     = 4, // 180°        (Front to viewer, stored in CHD as 3)
    SouthWest = 5, // 225°        (Horizontally mirrored from SouthEast / 2)
    West      = 6, // 270°        (Horizontally mirrored from East / 9)
    NorthWest = 7  // 315°        (Horizontally mirrored from NorthEast / 8)
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

/**
 * @brief Converts an angle in degrees [0, 360) to the closest 8-directional heading.
 */
Direction angle_to_direction(float angle_degrees) noexcept;

/**
 * @brief Converts a 2D velocity vector (dx, dy) to the closest 8-directional heading.
 */
Direction vector_to_direction(int32_t dx, int32_t dy) noexcept;

/**
 * @brief Returns the human-readable compass name (e.g. "North-East").
 */
const char* direction_to_string(Direction dir) noexcept;

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
    out_left  = -right;
    out_right = -left;
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

/**
 * @brief Indicates whether the lunchbox sprite must be horizontally mirrored.
 */
constexpr inline bool is_lunchbox_mirrored(Direction dir) noexcept {
    return get_direction_mapping(dir).mirrored;
}

} // namespace ants::assets
```

---

### 2.2 `include/ants_assets/chd_parser.hpp`

```cpp
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <array>
#include "mirroring.hpp"

namespace ants::assets {

// Header Constants
constexpr uint32_t CHD_EXPECTED_VERSION = 9;
constexpr uint32_t CHD_EXPECTED_TIMESTAMP = 0x378D661C;
constexpr uint32_t CHD_PALETTE_BYTE_SIZE = 1024;
constexpr uint32_t CHD_PALETTE_COLOR_COUNT = 256;
constexpr uint8_t  CHD_COLOR_KEY_INDEX = 254; // 0xFE Pure Magenta RGB(255,0,255) -> Alpha = 0

// Team Palette Index Ranges
constexpr uint8_t TEAM_BLACK_START = 237;
constexpr uint8_t TEAM_BLACK_END   = 239;
constexpr uint8_t TEAM_BLUE_START  = 34;
constexpr uint8_t TEAM_BLUE_END    = 39;
constexpr uint8_t TEAM_RED_START   = 178;
constexpr uint8_t TEAM_RED_END     = 181;
constexpr uint8_t TEAM_GREEN_START = 49;
constexpr uint8_t TEAM_GREEN_END   = 52;

/**
 * @brief 32-bit RGBA color representation.
 */
struct ColorRGBA {
    uint8_t r{0};
    uint8_t g{0};
    uint8_t b{0};
    uint8_t a{255};

    constexpr uint32_t to_u32_rgba() const noexcept {
        return (static_cast<uint32_t>(r)) |
               (static_cast<uint32_t>(g) << 8) |
               (static_cast<uint32_t>(b) << 16) |
               (static_cast<uint32_t>(a) << 24);
    }

    constexpr uint32_t to_u32_argb() const noexcept {
        return (static_cast<uint32_t>(a) << 24) |
               (static_cast<uint32_t>(r) << 16) |
               (static_cast<uint32_t>(g) << 8) |
               (static_cast<uint32_t>(b));
    }
};

/**
 * @brief 28-byte binary header of ants.chd.
 */
struct CHDHeader {
    uint32_t version{0};         // Must be 9
    uint32_t timestamp{0};       // 0x378D661C
    uint32_t table1_offset{0};   // 1,052 -> Sprite Bitmaps Table
    uint32_t table2_offset{0};   // 6,835,937 -> Sound Effects Table
    uint32_t table3_offset{0};   // 7,903,773 -> Event Tag Descriptors
    uint32_t table4_offset{0};   // 7,903,835 -> Animation Sequences Table
    uint32_t palette_bytes{0};   // 1,024
};

/**
 * @brief Paletted sprite bitmap from Table 1.
 */
struct Sprite {
    uint32_t id{0};              // 0-indexed position in Table 1
    uint32_t pitch{0};           // Row stride in bytes (width + padding)
    uint32_t width{0};           // Visible width in pixels
    uint32_t height{0};          // Visible height in pixels
    std::string name;            // Asset filename string (e.g. "dclay48.bmp")
    std::vector<uint8_t> pixels; // Size = pitch * height

    // Coordinate Pixel Accessor
    inline uint8_t get_pixel(uint32_t x, uint32_t y) const noexcept {
        if (x >= width || y >= height) return CHD_COLOR_KEY_INDEX;
        return pixels[y * pitch + x];
    }

    // Color Lookup
    inline ColorRGBA get_color(uint32_t x, uint32_t y,
                              const std::array<ColorRGBA, 256>& pal) const noexcept {
        return pal[get_pixel(x, y)];
    }

    // Direct conversion to 32-bit tightly-packed RGBA buffer (width * height * 4)
    std::vector<uint8_t> to_rgba32(const std::array<ColorRGBA, 256>& pal) const;

    // Creates a horizontally mirrored copy of this sprite
    Sprite create_horizontal_flip(uint32_t new_id = 0) const;
};

/**
 * @brief Wave format descriptor matching standard WAVEFORMATEX.
 */
struct WaveFormat {
    uint16_t format_tag{1};      // 1 = WAVE_FORMAT_PCM
    uint16_t channels{1};        // 1 = Mono, 2 = Stereo
    uint32_t samples_per_sec{0}; // 11,025 Hz or 22,050 Hz
    uint32_t avg_bytes_per_sec{0};
    uint16_t block_align{1};     // channels * (bits_per_sample / 8)
    uint16_t bits_per_sample{8}; // 8 bits per sample
    uint16_t extra_size{0};      // 0
};

/**
 * @brief Digitized audio sound clip from Table 2.
 */
struct SoundClip {
    uint32_t id{0};              // Sound ID (0..90)
    std::string name;            // Filename (e.g. "bombexp.wav" or empty if unnamed)
    WaveFormat format;           // Audio format descriptor
    std::vector<uint8_t> pcm_data; // Raw unsigned 8-bit PCM samples (silence = 128)

    // Synthesizes a standard 44-byte RIFF WAV file in memory
    std::vector<uint8_t> build_wav() const;
};

/**
 * @brief Event tag descriptor from Table 3.
 */
struct EventTag {
    uint32_t id{0};              // Tag ID (3 = HITGROUND, 4 = ATTACKHIT, 5 = HEAL, 10 = sentinel)
    std::string name;            // Symbolic string identifier
};

/**
 * @brief Visual render frame specification within an animation subitem.
 */
struct AnimationFrame {
    int32_t  dx{0};              // Horizontal render offset relative to unit anchor
    int32_t  dy{0};              // Vertical render offset relative to unit anchor
    uint32_t sprite_index{0};    // Index into Table 1 (0..2793)
};

/**
 * @brief Animation subitem track.
 */
struct AnimationSubItem {
    uint32_t val1{0};
    uint32_t val2{0};
    uint32_t val3{0};            // Duration / time delta (typically 1000)
    int32_t  box_left{0};        // Interaction bounding box Left
    int32_t  box_top{0};         // Interaction bounding box Top
    int32_t  box_right{0};       // Interaction bounding box Right
    int32_t  box_bottom{0};      // Interaction bounding box Bottom
    uint32_t flags{0};           // v8 layer flag (e.g. 1111111)
    uint32_t default_sp{0xFFFFFFFF}; // Sound trigger: 0xFFFFFFFF = none, 0..90 = Sound ID
    std::vector<AnimationFrame> frames;

    bool has_sound_trigger() const noexcept { return default_sp < 91; }
};

/**
 * @brief Complete animation sequence from Table 4.
 */
struct AnimationSequence {
    uint32_t id{0};              // Animation sequence ID (0..1343)
    std::string name;            // Sequence identifier (e.g. "agwg201", "re_screen")
    uint32_t flag1{0};
    uint32_t flag2{0};
    uint32_t flag3{0};
    std::vector<AnimationSubItem> subitems;
};

/**
 * @brief Low-level binary deserializers for ants.chd data blocks.
 */
class CHDParser {
public:
    static bool parse_header(const uint8_t* data, size_t size, CHDHeader& out_header);
    static bool parse_palette(const uint8_t* data, size_t size, size_t offset,
                              std::array<ColorRGBA, 256>& out_palette);
    static bool parse_table1_sprites(const uint8_t* data, size_t size, size_t offset,
                                     std::vector<Sprite>& out_sprites);
    static bool parse_table2_sounds(const uint8_t* data, size_t size, size_t offset,
                                    std::vector<SoundClip>& out_sounds);
    static bool parse_table3_tags(const uint8_t* data, size_t size, size_t offset,
                                  std::vector<EventTag>& out_tags);
    static bool parse_table4_animations(const uint8_t* data, size_t size, size_t offset,
                                        std::vector<AnimationSequence>& out_anims);
};

} // namespace ants::assets
```

---

### 2.3 `include/ants_assets/lvl_parser.hpp`

```cpp
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <array>

namespace ants::assets {

constexpr uint32_t LVL_EXPECTED_VERSION = 8;
constexpr uint32_t LVL_GAME_MODE_STANDARD = 1;
constexpr uint16_t LVL_EMPTY_TILE = 0x7FFE; // 32,766 = Empty interactive overlay sentinel

/**
 * @brief 6-byte cell representation as stored in Maps/*.LVL files.
 */
struct MapCell {
    uint16_t tile_index{LVL_EMPTY_TILE}; // Index into tile dictionary (or 0x7FFE)
    uint16_t flags{0};                   // Word 2 from file
    uint16_t properties{0};              // Word 3 from file

    constexpr bool is_empty() const noexcept { return tile_index == LVL_EMPTY_TILE; }
};

/**
 * @brief Anthill starting spawn position (Block 1).
 */
struct AnthillSpawn {
    uint16_t tile_id{0}; // Tile dictionary index: BSTART, USTART, RSTART, GSTART
    uint16_t y{0};       // Grid row coordinate
    uint16_t x{0};       // Grid column coordinate
    uint8_t  team_id{0}; // 0 = Black, 1 = Blue, 2 = Red, 3 = Green
};

/**
 * @brief Weighted food/item variant inside a respawn pool.
 */
struct FoodItemVariant {
    uint16_t weight{0};  // Relative probability weight
    uint16_t tile_id{LVL_EMPTY_TILE}; // Tile dictionary index (or 0x7FFE for null)
};

/**
 * @brief Dynamic food/item respawn schedule pool (Block 2).
 */
struct FoodSchedule {
    uint16_t x{0};                // Grid column coordinate
    uint16_t y{0};                // Grid row coordinate
    uint16_t initial_delay{0};    // Delay before initial spawn in seconds
    uint16_t respawn_interval{0}; // Respawn interval after collection in seconds
    std::vector<FoodItemVariant> variants;
};

/**
 * @brief 2D coordinate point for patrol paths.
 */
struct WaypointPoint {
    uint32_t px{0};
    uint32_t py{0};
};

/**
 * @brief Waypoint / patrol trigger (Block 4).
 */
struct Waypoint {
    uint16_t x{0};
    uint16_t y{0};
    uint32_t flag{0};
    uint32_t param{0};
    std::array<WaypointPoint, 5> points{};
};

/**
 * @brief Complete parsed level structure for a Maps/*.LVL file.
 */
class LevelData {
public:
    uint32_t version{0};         // Must be 8
    uint32_t game_mode{0};       // 1 = Standard
    uint16_t default_minutes{0}; // Match duration (6, 8, 10, 12)
    std::string description;     // 30-byte ASCII description
    uint16_t tile_type_count{0}; // Tile dictionary count
    std::vector<std::string> tile_dictionary; // 11-byte ASCII tile names
    uint32_t width{0};           // Grid width (31, 40, 60)
    uint32_t height{0};          // Grid height (31, 40, 60)

    // Grids (size = width * height)
    std::vector<MapCell> layer1_terrain;     // Base walkable/obstacle/water grid
    std::vector<MapCell> layer2_interactive; // Interactive items/food/spawns/bridges

    // Trailing Configuration Blocks
    std::vector<AnthillSpawn> anthill_spawns; // Block 1
    std::vector<FoodSchedule> food_schedules; // Block 2
    uint16_t ambient_flag{0};                 // Block 3
    uint16_t ambient_tile_or_sound{LVL_EMPTY_TILE}; // Block 3
    std::vector<Waypoint> waypoints;          // Block 4
    uint16_t boundary_param{0};               // f_last

    // Grid Cell Accessors
    const MapCell& get_cell_layer1(uint32_t x, uint32_t y) const;
    const MapCell& get_cell_layer2(uint32_t x, uint32_t y) const;

    // Dictionary Lookups
    const std::string& get_tile_name(uint16_t tile_index) const;
    int32_t find_tile_index(const std::string& name) const noexcept;
};

/**
 * @brief Deserializer for Maps/*.LVL files.
 */
class LVLParser {
public:
    static bool load_from_file(const std::string& filepath, LevelData& out_level);
    static bool load_from_memory(const uint8_t* data, size_t size, LevelData& out_level);
};

} // namespace ants::assets
```

---

### 2.4 `include/ants_assets/asset_archive.hpp`

```cpp
#pragma once

#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <memory>
#include "chd_parser.hpp"
#include "mirroring.hpp"

namespace ants::assets {

/**
 * @brief Master asset archive manager for ants.chd.
 * 
 * Provides O(1) indexed and named lookups for all palettes, sprites, audio clips,
 * event tags, and animation sequences. Pre-computes 8-directional mirrored sprites
 * in RAM for zero runtime rendering overhead.
 */
class AssetArchive {
public:
    AssetArchive();
    ~AssetArchive();

    AssetArchive(const AssetArchive&) = delete;
    AssetArchive& operator=(const AssetArchive&) = delete;
    AssetArchive(AssetArchive&&) noexcept;
    AssetArchive& operator=(AssetArchive&&) noexcept;

    // Loading Entry Points
    bool load_from_file(const std::string& chd_path);
    bool load_from_memory(const uint8_t* data, size_t size);

    // State Query
    bool is_loaded() const noexcept { return loaded_; }
    size_t total_memory_bytes() const noexcept;

    // Master Palette (256 Colors)
    const std::array<ColorRGBA, 256>& get_palette() const noexcept { return palette_; }
    ColorRGBA get_palette_color(uint8_t index) const noexcept { return palette_[index]; }

    // Sprites (Table 1: 2,794 Sprites)
    size_t sprite_count() const noexcept { return sprites_.size(); }
    const Sprite& get_sprite(uint32_t index) const;
    const Sprite* find_sprite(const std::string& name) const noexcept;
    int32_t find_sprite_id(const std::string& name) const noexcept;

    // Sound Clips (Table 2: 91 Sound Clips)
    size_t sound_count() const noexcept { return sounds_.size(); }
    const SoundClip& get_sound(uint32_t sound_id) const;
    const SoundClip* find_sound(const std::string& name) const noexcept;
    int32_t find_sound_id(const std::string& name) const noexcept;

    // Event Tags (Table 3: 4 Event Tags)
    size_t tag_count() const noexcept { return tags_.size(); }
    const EventTag& get_tag(uint32_t index) const;

    // Animations (Table 4: 1,344 Animation Sequences)
    size_t animation_count() const noexcept { return animations_.size(); }
    const AnimationSequence& get_animation(uint32_t anim_id) const;
    const AnimationSequence* find_animation(const std::string& name) const noexcept;
    int32_t find_animation_id(const std::string& name) const noexcept;

    // ========================================================================
    // 5-to-8 Directional Pre-computed Mirroring (O(1) Direct Lookup)
    // ========================================================================

    /**
     * @brief Returns the sprite for a given base sprite ID and discrete compass heading.
     * For directions North (0), NorthEast (1), East (2), SouthEast (3), South (4),
     * returns the unmirrored sprite from Table 1.
     * For directions SouthWest (5), West (6), NorthWest (7), returns the pre-computed
     * horizontally mirrored sprite.
     */
    const Sprite& get_directional_sprite(uint32_t base_sprite_id, Direction dir) const;

    /**
     * @brief Returns the pre-computed horizontally mirrored sprite for any sprite ID.
     */
    const Sprite& get_mirrored_sprite(uint32_t base_sprite_id) const;

    /**
     * @brief Resolves a directional animation sequence.
     * Given an animation prefix (e.g. "agwg") and a heading (0..7), returns the sequence
     * with pre-computed mirrored frame render offsets dx' for Western directions.
     */
    const AnimationSequence* get_directional_animation(const std::string& base_prefix,
                                                      Direction dir) const;

private:
    void build_index_tables();
    void precompute_mirrored_sprites();
    void precompute_directional_animations();

    bool loaded_{false};
    CHDHeader header_{};
    std::array<ColorRGBA, 256> palette_{};
    std::vector<Sprite> sprites_;                   // 2,794 original sprites
    std::vector<Sprite> mirrored_sprites_;          // 2,794 pre-mirrored sprites
    std::vector<SoundClip> sounds_;                 // 91 audio clips
    std::vector<EventTag> tags_;                    // Event tags
    std::vector<AnimationSequence> animations_;     // 1,344 original animations
    std::vector<AnimationSequence> dir_animations_; // Pre-computed mirrored directional animations

    // Hash Maps for O(1) String Lookups
    std::unordered_map<std::string, uint32_t> sprite_name_map_;
    std::unordered_map<std::string, uint32_t> sound_name_map_;
    std::unordered_map<std::string, uint32_t> anim_name_map_;
};

} // namespace ants::assets
```

---

## 3. Binary Layout & Decoding Specifications

### 3.1 `ants.chd` Binary Layout
File size: **8,411,866 bytes** (`0x00805B5A`).

```text
+-------------------------------------------------------------------------------+
| Header (28 Bytes)                                                             |
| - version (9), timestamp (0x378D661C), 4 table offsets, palette_bytes (1024)  |
+-------------------------------------------------------------------------------+
| Master Palette (1,024 Bytes, 0x0000001C to 0x0000041C)                        |
| - 256 * [R, G, B, Flags]                                                      |
+-------------------------------------------------------------------------------+
| Table 1: Sprite Bitmaps (0x0000041C, count = 2,794)                           |
| - count (uint32_t), offsets[2794]                                             |
| - Sprite records: pitch, width, height, filename_len, filename, pixels        |
+-------------------------------------------------------------------------------+
| Table 2: Digital Audio Clips (0x00684EE1, count = 91)                         |
| - count (uint32_t), offsets[91]                                               |
| - Sound records: format_len, WAVEFORMATEX, pcm_len, pcm_data, nlen, filename  |
+-------------------------------------------------------------------------------+
| Table 3: Event Tag Descriptors (0x00789A1D, 62 Bytes)                         |
| - max_tag_id (10), records: [tag_id, name_len, name]                          |
| - Tags: 3 (HITGROUND), 4 (ATTACKHIT), 5 (HEAL), 10 (sentinel)                 |
+-------------------------------------------------------------------------------+
| Table 4: Animation Sequences (0x00789A5B, count = 1,344)                      |
| - count (uint32_t), offsets[1344]                                             |
| - Anim records: name_len, name, flag1, flag2, flag3, subitem_count            |
|   - SubItem: val1, val2, val3, box_left, top, right, bottom, v8, default_sp,  |
|              frame_count                                                      |
|   - Frames: [dx, dy, sprite_index]                                            |
+-------------------------------------------------------------------------------+
```

#### Exact Table Parsing Details
1. **Header Validation:**
   - Verify `version == 9`. If `< 9`, raise error `0x2717`.
   - Verify `palette_bytes == 1024`.
   - `table1_offset == 1052`, `table2_offset == 6835937`, `table3_offset == 7903773`, `table4_offset == 7903835`.
2. **Palette Extraction:**
   - 256 entries $\times$ 4 bytes.
   - Byte 0: Red, Byte 1: Green, Byte 2: Blue, Byte 3: Flags.
   - Index 254: Pure Magenta `RGB(255, 0, 255)` with `Alpha = 0`.
   - All other indices 0..253, 255: `Alpha = 255`.
3. **Table 1 Sprites:**
   - 2,794 entries. Each sprite starts with `pitch`, `width`, `height`, `filename_len`, followed by `filename` (length `filename_len`), then `pixels` ($pitch \times height$).
   - Row stride rule: pixels in scanline are $[0 .. width-1]$. Bytes $[width .. pitch-1]$ are padding zeroes.
4. **Table 2 Audio:**
   - 91 entries. Format length is typically 18 bytes (`WAVEFORMATEX`). Unsigned 8-bit PCM.
   - 88 clips are mono (1 channel), 3 clips are stereo (2 channels: IDs 6, 45, 46).
   - Sample rates: 11,025 Hz or 22,050 Hz.
   - Filenames: 15 clips have `filename_len = 1` and `filename = "\0"`. They are identified strictly by numeric Sound ID.
5. **Table 3 Tags:**
   - 62 bytes. Descriptors for 3 (`HITGROUND`), 4 (`ATTACKHIT`), 5 (`HEAL`), 10 (`""`).
6. **Table 4 Animations:**
   - 1,344 entries. Each entry: `name_len`, ASCII `name` (unpadded, exactly `name_len` bytes), 3 `uint32_t` flags, `subitem_count`.
   - Each subitem: 10 `uint32_t` fields (`val1`, `val2`, `val3`, `box_left`, `box_top`, `box_right`, `box_bottom`, `v8`, `default_sp`, `frame_count`), followed by `frame_count` frames of `[int32_t dx, int32_t dy, uint32_t sprite_index]`.
   - `default_sp`: 0xFFFFFFFF means silent; values $< 91$ trigger Sound ID `default_sp`. Exactly 365 subitems feature active sound triggers.

---

### 3.2 `Maps/*.LVL` Binary Layout

Every map parses to **0 remaining bytes** (`rem = 0`):

| Level File | Dims | Dict Count | Spawns (C1) | Food Pools (C2) | Ambient | Waypoints (C4) | `f_last` | File Size |
|---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| `TINY.LVL` | 31×31 | 669 | 12 | 14 | (0, 32766) | 0 | 3 | 19,312 B |
| `SMALL.LVL` | 40×40 | 1,325 | 18 | 5 | (0, 32766) | 2 | 2 | 34,190 B |
| `MEDIUM.LVL` | 60×60 | 1,324 | 30 | 4 | (0, 32766) | 15 | 6 | 58,381 B |
| `GAUNTLET.LVL` | 60×60 | 1,329 | 32 | 2 | (0, 32766) | 20 | 6 | 58,508 B |
| `ISLANDS.LVL` | 60×60 | 1,329 | 40 | 10 | (0, 32766) | 22 | 4 | 58,776 B |
| `TREASURE.LVL` | 60×60 | 1,334 | 29 | 18 | (0, 32766) | 19 | 9 | 58,853 B |

#### Trailing Blocks Parsing Specifications:
- **Block 1 (Spawns):** `uint16_t count`, followed by `count * [uint16_t tile_id, uint16_t y, uint16_t x]`.
  - `BSTART`: Team 0 (Black), `USTART`: Team 1 (Blue), `RSTART`: Team 2 (Red), `GSTART`: Team 3 (Green).
- **Block 2 (Food Pools):** `uint16_t count`, followed by `count` food spawns:
  - `[uint16_t x, y, init_delay, respawn_interval, item_count]`
  - `item_count * [uint16_t weight, uint16_t tile_id]`
- **Block 3 (Ambient):** 2 `uint16_t` values: `ambient_flag`, `ambient_tile_or_sound`.
- **Block 4 (Waypoints):** `uint16_t count`.
  - For each entry: `[uint16_t x, uint16_t y, uint32_t flag]`.
  - If `flag != 0`: followed by `[uint32_t param, 5 * (uint32_t px, uint32_t py)]` (44 additional bytes).
  - If `count == 0` (e.g. `TINY.LVL`), immediately proceeds to `f_last`.
- **Final Field (`f_last`):** 1 `uint16_t` parameter immediately preceding EOF.

---

## 4. Memory Management Strategy

### 4.1 Contiguous Sprite Pixel Buffers & Stride Padding
- **Preservation of Pitch Stride:**
  Each sprite's pixel vector contains $pitch \times height$ raw bytes.
  This maintains byte-exact parity with `ants.chd`, allowing tests to verify bit-exact checksums.
- **Access Discipline:**
  Pixel at $(x, y)$ is accessed as $y \times pitch + x$.
  When uploading to textures or converting to RGBA:
  The parser extracts only $x \in [0, width-1]$, cleanly discarding padding zeroes ($x \ge width$).
- **RGBA32 Conversion:**
  `Sprite::to_rgba32()` allocates a tightly-packed $width \times height \times 4$ byte buffer.
  Every pixel index is resolved against the master palette.

### 4.2 Palette Color Format & Color Key 254 Transparency
- **Format:**
  Stored as 256 `ColorRGBA` structures in a fixed `std::array<ColorRGBA, 256>`.
- **Transparency Rules:**
  - Index 254 (`0xFE`, `RGB(255, 0, 255)` Pure Magenta) $\implies \text{Alpha} = 0$.
  - Index 0 (`RGB(119, 119, 127)` Opaque Slate) $\implies \text{Alpha} = 255$.
  - All other indices 1..253, 255 $\implies \text{Alpha} = 255$.
- **Team Color Swap Support:**
  Helper table to remap team colors across teams 0..3 without modifying sprite buffers.

### 4.3 Audio Buffer Representation & 44-Byte RIFF WAV Reconstruction
- **PCM Format:**
  Unsigned 8-bit PCM samples ($0..255$, DC center / silence = 128).
- **Dynamic WAV Generation:**
  `SoundClip::build_wav()` prepends a 44-byte standard RIFF header:
  1. `0x00`: `"RIFF"`
  2. `0x04`: `36 + pcm_data_len` (uint32_t, little-endian)
  3. `0x08`: `"WAVE"`
  4. `0x0C`: `"fmt "`
  5. `0x10`: `16` (uint32_t, subchunk 1 size)
  6. `0x14`: `1` (uint16_t, PCM format)
  7. `0x16`: `channels` (uint16_t, 1 or 2)
  8. `0x18`: `samples_per_sec` (uint32_t, 11025 or 22050)
  9. `0x1C`: `avg_bytes_per_sec` (uint32_t)
  10. `0x20`: `block_align` (uint16_t)
  11. `0x22`: `bits_per_sample` (uint16_t, 8)
  12. `0x24`: `"data"`
  13. `0x28`: `pcm_data_len` (uint32_t)
  14. `0x2C`: raw `pcm_data` bytes
  This memory buffer is loaded directly by SDL2 via `SDL_RWFromConstMem`.

### 4.4 5-to-8 Directional Pre-computed Mirroring Strategy
- **$O(1)$ Direct Lookup Architecture:**
  `AssetArchive` pre-computes horizontally mirrored versions of all 2,794 sprites into `mirrored_sprites_` at startup.
  Total additional RAM: ~6.8 MB (negligible on modern systems).
  Lookups for Western facings ($SW=5, W=6, NW=7$) directly index `mirrored_sprites_[id]`, granting instantaneous $O(1)$ direct array access with zero branching or shader reflection.
- **Directional Animation Sequences:**
  At archive initialization, `AssetArchive` pre-computes 193 mirrored directional animation sequences for headings $SW, W, NW$.
  Each sequence has its frame render offsets pre-transformed to $dx' = -(dx + W)$ and bounding boxes reflected ($[-right, -left]$).
  Simulation and rendering engines consume these sequences without any runtime coordinate translation.

---

## 5. CMake Build Configuration

### 5.1 Root `CMakeLists.txt` Integration
```cmake
cmake_minimum_required(VERSION 3.16)
project(MicrosoftAntsRemake VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler Warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Wnon-virtual-dtor
    )
endif()

# Sanitizer Option
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Global Include Directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Subdirectories
add_subdirectory(src/ants_assets)

# Testing
option(BUILD_TESTS "Build test executables" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/test_assets)
endif()
```

### 5.2 Library Target: `src/ants_assets/CMakeLists.txt`
```cmake
add_library(ants_assets STATIC
    mirroring.cpp
    chd_parser.cpp
    lvl_parser.cpp
    asset_archive.cpp
)

target_include_directories(ants_assets PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<INSTALL_INTERFACE:include>
)

set_target_properties(ants_assets PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ants_assets"
)
```

### 5.3 Test Target: `tests/test_assets/CMakeLists.txt`
```cmake
add_executable(test_assets
    main.cpp
)

target_link_libraries(test_assets PRIVATE ants_assets)

target_compile_definitions(test_assets PRIVATE
    ORIGINAL_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../Original-Ants"
)

add_test(NAME test_assets COMMAND test_assets)
```

---

## 6. Verification & Automated Test Specification (`test_assets`)

The `test_assets` test harness executes headlessly and validates the following test suites:

### Suite 1: CHD Header & Palette Validation
- Unpack 28-byte header from `ants.chd`.
- Assert `version == 9`.
- Assert `timestamp == 0x378D661C`.
- Assert table offsets: `table1 == 1052`, `table2 == 6835937`, `table3 == 7903773`, `table4 == 7903835`, `palette_bytes == 1024`.
- Validate 256 palette colors:
  - Color 254: $R=255, G=0, B=255, A=0$ (Magenta transparency).
  - Color 0: $R=119, G=119, B=127, A=255$ (Opaque).
  - Team 1 (Blue) indices 34..39 have blue dominance ($B > R$).
  - Team 2 (Red) indices 178..181 have red dominance ($R > B$).

### Suite 2: Table 1 Sprite Bitmaps Validation
- Assert `sprite_count == 2794`.
- For all 2,794 sprites:
  - Assert `width > 0` and `height > 0`.
  - Assert `pitch >= width`.
  - Assert `pixels.size() == pitch * height`.
  - Assert `!name.empty()`.
- Spot check Sprite 0 (`dclay48.bmp`): `width == 45`, `height == 47`, `pitch == 48`.

### Suite 3: Table 2 Digital Audio Validation
- Assert `sound_count == 91`.
- For all 91 sounds:
  - Assert `format.format_tag == 1` (PCM).
  - Assert `format.bits_per_sample == 8`.
  - Assert `format.samples_per_sec == 11025 || format.samples_per_sec == 22050`.
  - Assert `pcm_data.size() > 0`.
  - Assert `build_wav().size() == 44 + pcm_data.size()`.
- Channel check: exactly 3 stereo sounds (IDs 6, 45, 46); exactly 88 mono sounds.
- Spot checks:
  - Sound 56 (`winner.wav`): `samples_per_sec == 22050`, `size == 102860`.
  - Sound 58 (`underattack.wav`): `samples_per_sec == 22050`, `size == 15540`.
  - Sound 71 (`splash.wav`): `samples_per_sec == 11025`, `size == 21203`.
  - Sound 72 (`antdrown.wav`): `samples_per_sec == 11025`, `size == 13899`.

### Suite 4: Table 3 Event Tags Validation
- Assert `tag_count == 4`.
- Verify Tag 3 == `"HITGROUND"`.
- Verify Tag 4 == `"ATTACKHIT"`.
- Verify Tag 5 == `"HEAL"`.
- Verify Tag 10 == `""`.

### Suite 5: Table 4 Animations Validation
- Assert `animation_count == 1344`.
- Total sound triggers across all subitems: assert exactly 365 subitems have `default_sp < 91`.
- Spot check key animations:
  - Animation 0: `d_shell` (subitems = 1, frames = 68).
  - Animation 25: `re_screen` (scorecard modal).
  - Animation 40: `dsplash` (Sound 71 trigger, 5 frames).
  - Animation 1134: `agdr301` (Worker ant drowning, 22 subitems, Sounds 71 & 72).
  - Animation 1095: `atcr501` (Thief base dive, non-mirrored direction 5).

### Suite 6: Map Loading & Trailing Block Validation (`Maps/*.LVL`)
- Parse all 6 maps: `TINY.LVL`, `SMALL.LVL`, `MEDIUM.LVL`, `ISLANDS.LVL`, `GAUNTLET.LVL`, `TREASURE.LVL`.
- For each map:
  - Assert `version == 8` and `game_mode == 1`.
  - Assert `layer1_terrain.size() == width * height`.
  - Assert `layer2_interactive.size() == width * height`.
  - Assert `anthill_spawns.size() > 0`.
  - Assert `food_schedules.size() > 0`.
  - Assert `rem == 0` (0 remaining unparsed bytes).

### Suite 7: 5-to-8 Directional Mirroring Validation
- Direction map check:
  - Dirs 0..4 have `mirrored == false`.
  - Dirs 5..7 have `mirrored == true`.
- Math verification:
  - Given $dx = 10, W = 40 \implies dx' = -(10 + 40) = -50$.
  - Given bounding box $[-10, 20] \implies [-20, 10]$.
- Pixel inversion verification:
  - For mirrored sprite: `mirrored.get_pixel(x, y) == original.get_pixel(width - 1 - x, y)`.
- Lunchbox prefix verification:
  - Dirs 0..7 map to `7lb`, `6lb`, `5lb`, `4lb`, `3lb`, `4lb` (mirrored), `5lb` (mirrored), `6lb` (mirrored).

### Suite 8: Memory Safety & Leak Detection
- Run complete test under AddressSanitizer (`-fsanitize=address,undefined`).
- Assert 0 buffer overflows, 0 memory leaks, and total runtime $< 150$ ms.

---

## 7. Implementation Blueprint for M1 Implementers

To implement `ants_assets`, implementers should follow this step-by-step sequence:

1. **Create Directory Tree:**
   ```bash
   mkdir -p include/ants_assets
   mkdir -p src/ants_assets
   mkdir -p tests/test_assets
   ```
2. **Deploy Public Headers:**
   Write `include/ants_assets/mirroring.hpp`, `include/ants_assets/chd_parser.hpp`, `include/ants_assets/lvl_parser.hpp`, `include/ants_assets/asset_archive.hpp` exactly matching the blueprints in Section 2.
3. **Implement Core Parsers:**
   - `src/ants_assets/mirroring.cpp`: Implement `mirror_pixel_buffer`, angle conversion, vector conversion, lunchbox prefix resolution.
   - `src/ants_assets/chd_parser.cpp`: Implement little-endian buffer readers with bounds-checking, palette decoding, Table 1 sprite unpacking, Table 2 WAV synthesis, Table 3 tag parsing, Table 4 animation sequence deserialization.
   - `src/ants_assets/lvl_parser.cpp`: Implement header parsing, 11-byte tile name dictionary extraction, 6-byte cell unpacking for Layer 1 & Layer 2, and sequential extraction of trailing blocks 1..4.
   - `src/ants_assets/asset_archive.cpp`: Implement `load_from_file`, hash table indexing, sprite mirroring pre-computation, and directional animation synthesis.
4. **Implement Test Suite:**
   - `tests/test_assets/main.cpp`: Implement test suites 1..8 with clear test output.
5. **Configure CMake & Build:**
   Deploy `CMakeLists.txt` files and compile with `cmake -B build -DENABLE_ASAN=ON && cmake --build build`.
6. **Execute Verification:**
   Run `./build/tests/test_assets/test_assets`.
