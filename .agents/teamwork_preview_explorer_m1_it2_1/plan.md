# Milestone 1 Iteration 2 Remediation Plan: LevelData Interface Contract & Robustness

**Author:** `explorer_m1_it2_1`  
**Date:** 2026-09-06T23:00:00Z  
**Target Module:** `libants-assets` (`LevelData`, `LVLParser`, `mirroring`)  
**Parent Conversation ID:** `a28dfa55-5a82-453d-a21b-99459a66b340`  
**Working Directory:** `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_it2_1`

---

## 1. Executive Summary & Root Cause Analysis

### 1.1 Root Cause of Gate Rejection
In Milestone 1, `worker_m1_1` implemented `LevelData` as a plain data struct with public member variables:
- `uint32_t width;`
- `uint32_t height;`
- `std::vector<MapCell> layer1_terrain;`
- `std::vector<MapCell> layer2_interactive;`
- `std::vector<AnthillSpawn> anthill_spawns;`
- `std::vector<FoodSchedule> food_schedules;`

However, `/Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md` (lines 111–118) specifies the mandatory interface contract:
```cpp
ants::assets::LevelData:
  load_lvl(const std::string& path) -> bool
  width() -> uint32_t, height() -> uint32_t
  layer1_terrain(uint32_t x, uint32_t y) -> uint16_t
  layer2_item(uint32_t x, uint32_t y) -> uint16_t
  anthill_spawns() -> std::vector<AnthillSpawn>
  food_schedules() -> std::vector<FoodSchedule>
```
Because C++ member variables shadow member functions of the same name in class scope, calling `level.width()`, `level.height()`, `level.layer1_terrain(x, y)`, `level.anthill_spawns()`, or `level.food_schedules()` resulted in 6 compiler errors. Furthermore, `layer2_interactive` did not provide the `layer2_item(x, y)` method.

### 1.2 Secondary Deficiencies Identified by Reviewers
1. **Uncaught `std::bad_alloc` on Malformed Dimensions**: `LVLParser::load_from_memory` called `layer1_terrain.resize(cell_count)` before checking `r.remaining() >= cell_count * 12`, allowing corrupted dimension headers to crash callers with an unhandled exception rather than returning `false`.
2. **In-place Aliasing Hazard in `mirror_bounding_box`**: `mirror_bounding_box` assigned directly to reference outputs without temporaries, corrupting coordinates when called as `mirror_bounding_box(l, r, l, r)`.
3. **In-place Scanline Hazard in `mirror_pixel_buffer`**: If `src == dst`, scanlines were partially overwritten before being read.

---

## 2. Refactoring Design for `LevelData`

### 2.1 Interface Signatures (Strict `PROJECT.md` Conformance)
```cpp
namespace ants::assets {

class LevelData {
public:
    // 1. Mandatory Contract Methods (PROJECT.md)
    uint32_t width() const noexcept;
    uint32_t height() const noexcept;
    uint16_t layer1_terrain(uint32_t x, uint32_t y) const noexcept;
    uint16_t layer2_item(uint32_t x, uint32_t y) const noexcept;
    const std::vector<AnthillSpawn>& anthill_spawns() const noexcept;
    std::vector<AnthillSpawn>& anthill_spawns() noexcept;
    const std::vector<FoodSchedule>& food_schedules() const noexcept;
    std::vector<FoodSchedule>& food_schedules() noexcept;

    // 2. Full Grid Cell & Container Accessors (Engine & Test Flexibility)
    const MapCell& get_cell_layer1(uint32_t x, uint32_t y) const;
    const MapCell& get_cell_layer2(uint32_t x, uint32_t y) const;
    const std::vector<MapCell>& layer1_cells() const noexcept;
    std::vector<MapCell>& layer1_cells() noexcept;
    const std::vector<MapCell>& layer2_cells() const noexcept;
    std::vector<MapCell>& layer2_cells() noexcept;
    const std::vector<MapCell>& layer2_interactive() const noexcept;
    std::vector<MapCell>& layer2_interactive() noexcept;

    // 3. Metadata & Dictionary
    const std::string& get_tile_name(uint16_t tile_index) const;
    int32_t find_tile_index(const std::string& name) const noexcept;

    // 4. File Loading Convenience
    bool load_from_file(const std::string& filepath);
    bool load_from_memory(const uint8_t* data, size_t size);
    bool load_lvl(const std::string& filepath) { return load_from_file(filepath); }
};

} // namespace ants::assets
```

### 2.2 Member Variable Renaming
Internal variables are renamed with trailing underscores:
- `uint32_t width_{0};`
- `uint32_t height_{0};`
- `std::vector<MapCell> layer1_terrain_;`
- `std::vector<MapCell> layer2_interactive_;`
- `std::vector<AnthillSpawn> anthill_spawns_;`
- `std::vector<FoodSchedule> food_schedules_;`

---

## 3. Exact Code Changes by File

### 3.1 `include/ants_assets/lvl_parser.hpp`
Replace `class LevelData` (lines 80–115) with:

```cpp
/**
 * @brief Complete parsed level structure for a Maps/ *.LVL file.
 */
class LevelData {
public:
    uint32_t version{0};         // Must be 8
    uint32_t game_mode{0};       // 1 = Standard
    uint16_t default_minutes{0}; // Match duration (6, 8, 10, 12)
    std::string description;     // 30-byte ASCII description
    uint16_t tile_type_count{0}; // Tile dictionary count
    std::vector<std::string> tile_dictionary; // 11-byte ASCII tile names

    // Trailing Configuration Blocks
    uint16_t ambient_flag{0};                 // Block 3
    uint16_t ambient_tile_or_sound{LVL_EMPTY_TILE}; // Block 3
    std::vector<Waypoint> waypoints;          // Block 4
    uint16_t boundary_param{0};               // f_last (final parameter before EOF)

    // --- Interface Contract (PROJECT.md) ---
    uint32_t width() const noexcept { return width_; }
    uint32_t height() const noexcept { return height_; }
    uint16_t layer1_terrain(uint32_t x, uint32_t y) const noexcept {
        return get_cell_layer1(x, y).tile_index;
    }
    uint16_t layer2_item(uint32_t x, uint32_t y) const noexcept {
        return get_cell_layer2(x, y).tile_index;
    }
    const std::vector<AnthillSpawn>& anthill_spawns() const noexcept { return anthill_spawns_; }
    std::vector<AnthillSpawn>& anthill_spawns() noexcept { return anthill_spawns_; }
    const std::vector<FoodSchedule>& food_schedules() const noexcept { return food_schedules_; }
    std::vector<FoodSchedule>& food_schedules() noexcept { return food_schedules_; }

    // --- Grid Cell & Container Accessors ---
    const MapCell& get_cell_layer1(uint32_t x, uint32_t y) const;
    const MapCell& get_cell_layer2(uint32_t x, uint32_t y) const;
    const std::vector<MapCell>& layer1_cells() const noexcept { return layer1_terrain_; }
    std::vector<MapCell>& layer1_cells() noexcept { return layer1_terrain_; }
    const std::vector<MapCell>& layer2_cells() const noexcept { return layer2_interactive_; }
    std::vector<MapCell>& layer2_cells() noexcept { return layer2_interactive_; }
    const std::vector<MapCell>& layer2_interactive() const noexcept { return layer2_interactive_; }
    std::vector<MapCell>& layer2_interactive() noexcept { return layer2_interactive_; }

    // --- Dictionary Lookups ---
    const std::string& get_tile_name(uint16_t tile_index) const;
    int32_t find_tile_index(const std::string& name) const noexcept;

    // --- Loading Convenience ---
    bool load_from_file(const std::string& filepath);
    bool load_from_memory(const uint8_t* data, size_t size);
    bool load_lvl(const std::string& filepath) { return load_from_file(filepath); }

    // --- Internal Member Storage ---
    uint32_t width_{0};                      // Grid width (31, 40, 60)
    uint32_t height_{0};                     // Grid height (31, 40, 60)
    std::vector<MapCell> layer1_terrain_;    // Base walkable/obstacle/water grid
    std::vector<MapCell> layer2_interactive_;// Interactive items/food/spawns/bridges
    std::vector<AnthillSpawn> anthill_spawns_;// Block 1
    std::vector<FoodSchedule> food_schedules_;// Block 2
};
```

---

### 3.2 `src/ants_assets/lvl_parser.cpp`
1. Update `get_cell_layer1` and `get_cell_layer2` (lines 68–76):
```cpp
const MapCell& LevelData::get_cell_layer1(uint32_t x, uint32_t y) const {
    if (x >= width_ || y >= height_) return EMPTY_CELL;
    return layer1_terrain_[static_cast<size_t>(y) * width_ + x];
}

const MapCell& LevelData::get_cell_layer2(uint32_t x, uint32_t y) const {
    if (x >= width_ || y >= height_) return EMPTY_CELL;
    return layer2_interactive_[static_cast<size_t>(y) * width_ + x];
}
```

2. Update `LVLParser::load_from_memory` (lines 141–223):
```cpp
    // 2. Tile Dictionary: (tile_type_count + 1) entries of 11 bytes
    size_t dict_count = static_cast<size_t>(out_level.tile_type_count) + 1;
    if (r.remaining() < dict_count * 11) return false;
    out_level.tile_dictionary.resize(dict_count);
    for (size_t i = 0; i < dict_count; ++i) {
        char name_buf[11];
        if (!r.read_bytes(name_buf, 11)) return false;
        size_t nlen = 0;
        while (nlen < 11 && name_buf[nlen] != '\0') {
            ++nlen;
        }
        out_level.tile_dictionary[i].assign(name_buf, nlen);
    }

    // 3. Grid Dimensions (8 bytes)
    if (!r.read_u32(out_level.width_) || !r.read_u32(out_level.height_)) return false;
    if (out_level.width_ == 0 || out_level.height_ == 0) return false;

    size_t cell_count = static_cast<size_t>(out_level.width_) * out_level.height_;

    // Robust allocation defense: each cell requires 6 bytes in Layer 1 and 6 bytes in Layer 2
    // plus trailing blocks require at least 12 bytes
    if (r.remaining() < 12 || cell_count > (r.remaining() - 12) / 12) return false;

    try {
        // 4. Layer 1: Base Terrain Grid (cell_count * 6 bytes)
        out_level.layer1_terrain_.resize(cell_count);
        for (size_t i = 0; i < cell_count; ++i) {
            MapCell& c = out_level.layer1_terrain_[i];
            if (!r.read_u16(c.tile_index) || !r.read_u16(c.flags) || !r.read_u16(c.properties)) {
                return false;
            }
        }

        // 5. Layer 2: Interactive Overlay Grid (cell_count * 6 bytes)
        out_level.layer2_interactive_.resize(cell_count);
        for (size_t i = 0; i < cell_count; ++i) {
            MapCell& c = out_level.layer2_interactive_[i];
            if (!r.read_u16(c.tile_index) || !r.read_u16(c.flags) || !r.read_u16(c.properties)) {
                return false;
            }
        }

        // 6. Trailing Block 1: Anthill Spawns
        uint16_t b1_count = 0;
        if (!r.read_u16(b1_count)) return false;
        if (r.remaining() < static_cast<size_t>(b1_count) * 7) return false;
        out_level.anthill_spawns_.resize(b1_count);
        for (size_t i = 0; i < b1_count; ++i) {
            AnthillSpawn& sp = out_level.anthill_spawns_[i];
            if (!r.read_u16(sp.tile_id) || !r.read_u16(sp.y) || !r.read_u16(sp.x)) {
                return false;
            }

            const std::string& tname = out_level.get_tile_name(sp.tile_id);
            if (tname.find("BSTART") != std::string::npos) {
                sp.team_id = 0; // Black
            } else if (tname.find("USTART") != std::string::npos) {
                sp.team_id = 1; // Blue
            } else if (tname.find("RSTART") != std::string::npos) {
                sp.team_id = 2; // Red
            } else if (tname.find("GSTART") != std::string::npos) {
                sp.team_id = 3; // Green
            } else {
                sp.team_id = 255;
            }
        }

        // 7. Trailing Block 2: Food Respawn Pools
        uint16_t b2_count = 0;
        if (!r.read_u16(b2_count)) return false;
        out_level.food_schedules_.resize(b2_count);
        for (size_t i = 0; i < b2_count; ++i) {
            FoodSchedule& fs = out_level.food_schedules_[i];
            uint16_t item_count = 0;
            if (!r.read_u16(fs.y) || !r.read_u16(fs.x) ||
                !r.read_u16(fs.initial_delay) || !r.read_u16(fs.respawn_interval) ||
                !r.read_u16(item_count)) {
                return false;
            }

            if (r.remaining() < static_cast<size_t>(item_count) * 4) return false;
            fs.variants.resize(item_count);
            for (size_t v = 0; v < item_count; ++v) {
                FoodItemVariant& var = fs.variants[v];
                if (!r.read_u16(var.weight) || !r.read_u16(var.tile_id)) {
                    return false;
                }
            }
        }
    } catch (const std::exception&) {
        return false;
    }
```

---

### 3.3 `include/ants_assets/mirroring.hpp`
Update `mirror_bounding_box` (lines 123–127) with temporaries:
```cpp
constexpr inline void mirror_bounding_box(int32_t left, int32_t right,
                                          int32_t& out_left, int32_t& out_right) noexcept {
    int32_t temp_left  = -right;
    int32_t temp_right = -left;
    out_left  = temp_left;
    out_right = temp_right;
}
```

---

### 3.4 `src/ants_assets/mirroring.cpp`
Update `mirror_pixel_buffer` (lines 46–58) to handle `src == dst`:
```cpp
    for (uint32_t y = 0; y < height; ++y) {
        uint8_t* dst_row = dst + (static_cast<size_t>(y) * pitch);
        if (src == dst) {
            for (uint32_t x = 0; x < width / 2; ++x) {
                std::swap(dst_row[x], dst_row[width - 1 - x]);
            }
        } else {
            const uint8_t* src_row = src + (static_cast<size_t>(y) * pitch);
            for (uint32_t x = 0; x < width; ++x) {
                dst_row[x] = src_row[width - 1 - x];
            }
        }

        // Clear stride padding
        if (pitch > width) {
            std::memset(dst_row + width, 0, pitch - width);
        }
    }
```

---

### 3.5 Test Suite Adjustments

#### `tests/test_assets/test_assets.cpp`
- Replace `map.width` with `map.width()` and `map.height` with `map.height()`.
- Replace `map.layer1_terrain.size()` with `map.layer1_cells().size()`.
- Replace `map.layer2_interactive.size()` with `map.layer2_cells().size()`.
- Replace `map.anthill_spawns.size()` with `map.anthill_spawns().size()`.
- Replace `map.food_schedules.size()` with `map.food_schedules().size()`.
- Replace `for (const auto& cell : map.layer2_interactive)` with `for (const auto& cell : map.layer2_cells())`.
- Add test case verifying all 6 contract accessors on `TINY.LVL`.

#### `tests/test_assets/test_adversarial.cpp`
- In `TEST_CASE("3.5 LevelData Out-of-Bounds Coordinates & Index Lookups")`:
  - Replace `lvl.width` with `lvl.width()` and `lvl.height` with `lvl.height()`.

#### `tests/test_assets/test_challenger_m1_2.cpp`
- In `CHALLENGE_CASE("5. ...")`:
  - Replace `map.width` with `map.width()` and `map.height` with `map.height()`.
  - Replace `map.layer1_terrain.size()` with `map.layer1_cells().size()`.
  - Replace `map.layer2_interactive.size()` with `map.layer2_cells().size()`.
- In `CHALLENGE_CASE("6.5 Corrupted dimensions ...")`:
  - Replace `lvl.layer1_terrain.size()` with `lvl.layer1_cells().size()`.
- In `CHALLENGE_CASE("7.1 ...")`:
  - Add in-place aliasing assertion `mirror_bounding_box(l, r, l, r)`.

---

## 4. Implementation Steps for Worker

1. **Step 1:** Apply changes to `include/ants_assets/lvl_parser.hpp` (add accessors, rename internal members to `_`).
2. **Step 2:** Apply changes to `src/ants_assets/lvl_parser.cpp` (update member references, add defensive remaining-byte checks and `try/catch`).
3. **Step 3:** Apply changes to `include/ants_assets/mirroring.hpp` and `src/ants_assets/mirroring.cpp` (aliasing and in-place mirroring safety).
4. **Step 4:** Update tests (`test_assets.cpp`, `test_adversarial.cpp`, `test_challenger_m1_2.cpp`) to call accessor methods and verify contract.
5. **Step 5:** Build and run tests:
   ```bash
   cmake -B build && cmake --build build
   ./build/tests/test_assets/test_assets
   ./build/tests/test_assets/test_challenger_m1_1
   ./build/tests/test_assets/test_challenger_m1_2
   ./build_e2e/e2e_runner --all
   ```
6. **Step 6:** Run with AddressSanitizer:
   ```bash
   cmake -B build_asan -DENABLE_ASAN=ON && cmake --build build_asan
   ctest --test-dir build_asan --output-on-failure
   ```
