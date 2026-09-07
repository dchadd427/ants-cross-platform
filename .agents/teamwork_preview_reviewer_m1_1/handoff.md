# Review Handoff Report: Milestone 1 Native Binary Asset Decoder (`libants-assets`)

**Reviewer:** `reviewer_m1_1` (Reviewer & Adversarial Critic)  
**Date:** 2026-09-06T22:53:00Z  
**Target Module:** `libants-assets` (Milestone 1)  
**Parent Conversation ID:** `a28dfa55-5a82-453d-a21b-99459a66b340`  
**Working Directory:** `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_reviewer_m1_1`  

---

## Review Summary

**Verdict**: **REQUEST_CHANGES**

**Summary of Assessment**:  
While `worker_m1_1` has delivered an impressive, genuine, high-performance binary asset decoder that decodes authentic assets (`ants.chd` and all 6 `.LVL` files) with 100% test passes and zero memory leaks under AddressSanitizer, **the public interface of `LevelData` directly breaks the mandatory interface contract defined in `PROJECT.md`**. Any downstream caller in Milestone 2 (`libants-sim`) or application code following the project specification cannot compile against `LevelData`. Furthermore, adversarial stress testing revealed an unhandled `std::bad_alloc` crash on malformed `.LVL` dimensions and an argument aliasing flaw in `mirror_bounding_box`.

---

## 1. Observation

1. **Compilation and Test Execution:**
   - Command: `cmake -B build && cmake --build build`
     - Result: Exited code 0. Zero warnings or compiler errors under `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wnon-virtual-dtor`.
   - Command: `./build/tests/test_assets/test_assets`
     - Result: Exited code 0. 25 test cases across 8 suites passed (35,840 assertions).
   - Command: `./build_e2e/e2e_runner --all`
     - Result: Exited code 0. 506 / 506 tests passed in 3.12 ms.
   - Command: `cmake -B build_asan -DENABLE_ASAN=ON && cmake --build build_asan && ./build_asan/tests/test_assets/test_assets`
     - Result: Exited code 0. 0 heap buffer overflows, 0 leaks, 0 sanitizer errors.

2. **Interface Contract Specifications in `PROJECT.md`:**
   In `/Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md` lines 103–118:
   ```markdown
   ### `ants-assets` ↔ `ants-sim`
   - `ants::assets::AssetArchive`:
     - `load_chd(const std::string& path) -> bool`
     - `get_palette() -> const std::array<ColorRGBA, 256>&`
     - `get_sprite(uint32_t index) -> const Sprite&`
     - `get_sound(uint32_t sound_id) -> const SoundClip&`
     - `get_animation(uint32_t anim_id) -> const Animation&`
     - `get_mirrored_sprite(uint32_t sprite_id, Direction dir) -> const Sprite&`
   - `ants::assets::LevelData`:
     - `load_lvl(const std::string& path) -> bool`
     - `width() -> uint32_t`, `height() -> uint32_t`
     - `layer1_terrain(uint32_t x, uint32_t y) -> uint16_t`
     - `layer2_item(uint32_t x, uint32_t y) -> uint16_t`
     - `anthill_spawns() -> std::vector<AnthillSpawn>`
     - `food_schedules() -> std::vector<FoodSchedule>`
   ```

3. **Observed Interface in `include/ants_assets/lvl_parser.hpp`:**
   Lines 88–115:
   ```cpp
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
   uint16_t boundary_param{0};               // f_last (final parameter before EOF)

   // Grid Cell Accessors
   const MapCell& get_cell_layer1(uint32_t x, uint32_t y) const;
   const MapCell& get_cell_layer2(uint32_t x, uint32_t y) const;
   ```

4. **Interface Contract Verification Test:**
   Compiling consumer code executing `PROJECT.md` contracts against `include/ants_assets/lvl_parser.hpp`:
   ```cpp
   LevelData level;
   level.load_lvl("Original-Ants/Maps/TINY.LVL");
   uint32_t w = level.width();
   uint32_t h = level.height();
   uint16_t t1 = level.layer1_terrain(0, 0);
   uint16_t t2 = level.layer2_item(0, 0);
   auto spawns = level.anthill_spawns();
   auto food = level.food_schedules();
   ```
   Verbatim Clang compiler diagnostics:
   ```text
   error: called object type 'uint32_t' (aka 'unsigned int') is not a function or function pointer
       uint32_t w = level.width();
                    ~~~~~~~~~~~^
   error: called object type 'uint32_t' (aka 'unsigned int') is not a function or function pointer
       uint32_t h = level.height();
                    ~~~~~~~~~~~~^
   error: type 'std::vector<MapCell>' does not provide a call operator
       uint16_t t1 = level.layer1_terrain(0, 0);
                     ^~~~~~~~~~~~~~~~~~~~
   error: no member named 'layer2_item' in 'ants::assets::LevelData'
       uint16_t t2 = level.layer2_item(0, 0);
                     ~~~~~ ^
   error: type 'std::vector<AnthillSpawn>' does not provide a call operator
       auto spawns = level.anthill_spawns();
                     ^~~~~~~~~~~~~~~~~~~~
   error: type 'std::vector<FoodSchedule>' does not provide a call operator
       auto food = level.food_schedules();
                   ^~~~~~~~~~~~~~~~~~~~
   6 errors generated.
   ```

5. **Adversarial Robustness Stress Test on Malformed LVL Dimensions:**
   In `src/ants_assets/lvl_parser.cpp:155–163`:
   ```cpp
   if (!r.read_u32(out_level.width) || !r.read_u32(out_level.height)) return false;
   if (out_level.width == 0 || out_level.height == 0) return false;

   size_t cell_count = static_cast<size_t>(out_level.width) * out_level.height;

   // 4. Layer 1: Base Terrain Grid (cell_count * 6 bytes)
   out_level.layer1_terrain.resize(cell_count);
   ```
   When supplied with an adversarial 100-byte buffer with `width = 0x7FFFFFFF` and `height = 0x7FFFFFFF`:
   - Observed outcome: Process terminated with uncaught `std::bad_alloc` (`std::length_error`) instead of returning `false`.

6. **Aliasing in `mirror_bounding_box`:**
   In `include/ants_assets/mirroring.hpp:123–127`:
   ```cpp
   constexpr inline void mirror_bounding_box(int32_t left, int32_t right,
                                             int32_t& out_left, int32_t& out_right) noexcept {
       out_left  = -right;
       out_right = -left;
   }
   ```
   If called in-place as `mirror_bounding_box(b_left, b_right, b_left, b_right)`:
   - `out_left = -right` modifies `b_left` to `-right`.
   - `out_right = -left` reads the newly modified `b_left` (`-right`), making `out_right = -(-right) = right` instead of `-original_left`.

---

## 2. Logic Chain

1. **Integrity Assessment:**
   - Observations 1 and the implementation files confirm that the decoder is genuine. There are no mock facades, no hardcoded test outputs, no external CLI delegations, and no fabricated logs. Both `test_assets` and `e2e_runner` execute genuine binary parsing logic against `ants.chd` and `Maps/*.LVL`.
   - Result: **Integrity check passes.**

2. **Interface Conformance Assessment:**
   - Observation 2 establishes the mandatory interface contract between `libants-assets` and `libants-sim` in `PROJECT.md`.
   - Observation 3 shows that `LevelData` exposes public fields `uint32_t width;`, `uint32_t height;`, `std::vector<MapCell> layer1_terrain;`, `std::vector<MapCell> layer2_interactive;`, `std::vector<AnthillSpawn> anthill_spawns;`, and `std::vector<FoodSchedule> food_schedules;`.
   - Because C++ member variables reside in the same class scope as member functions, any downstream consumer attempting to call `level.width()`, `level.height()`, `level.layer1_terrain(x, y)`, `level.layer2_item(x, y)`, `level.anthill_spawns()`, or `level.food_schedules()` encounters fatal compilation errors (Observation 4).
   - This prevents Milestone 2 (`libants-sim`) from compiling against the documented contract.
   - Result: **Critical Interface Contract Non-Conformance.**

3. **Adversarial & Robustness Assessment:**
   - Observation 5 reveals that `LVLParser::load_from_memory` does not verify `r.remaining() >= cell_count * 12` before executing `resize(cell_count)`. On adversarial or corrupt inputs, an uncaught standard library exception crashes the host application rather than gracefully returning `false`.
   - Observation 6 reveals that `mirror_bounding_box` is unsafe against in-place argument aliasing.

---

## 3. Findings

### [Critical] Finding 1: Interface Contract Non-Conformance in `LevelData`
- **What**: `LevelData` does not implement the public member methods required by `PROJECT.md`.
- **Where**: `include/ants_assets/lvl_parser.hpp`, lines 88–115.
- **Why**: `PROJECT.md` explicitly specifies the contract:
  - `width() -> uint32_t`, `height() -> uint32_t`
  - `layer1_terrain(uint32_t x, uint32_t y) -> uint16_t`
  - `layer2_item(uint32_t x, uint32_t y) -> uint16_t`
  - `anthill_spawns() -> std::vector<AnthillSpawn>`
  - `food_schedules() -> std::vector<FoodSchedule>`
  Currently, `LevelData` has member variables of the same or conflicting names, causing 6 compilation errors when used according to specification.
- **Suggestion**:
  Update `LevelData` so that it provides these exact methods. For example:
  1. Rename internal member variables with trailing underscores (e.g. `width_`, `height_`, `layer1_terrain_`, `layer2_interactive_`, `anthill_spawns_`, `food_schedules_`).
  2. Implement the accessor methods:
     ```cpp
     uint32_t width() const noexcept { return width_; }
     uint32_t height() const noexcept { return height_; }
     uint16_t layer1_terrain(uint32_t x, uint32_t y) const noexcept { return get_cell_layer1(x, y).tile_index; }
     uint16_t layer2_item(uint32_t x, uint32_t y) const noexcept { return get_cell_layer2(x, y).tile_index; }
     const std::vector<AnthillSpawn>& anthill_spawns() const noexcept { return anthill_spawns_; }
     const std::vector<FoodSchedule>& food_schedules() const noexcept { return food_schedules_; }
     ```
  3. Update `test_assets.cpp` and `lvl_parser.cpp` to use the updated member names/accessors.

### [Major] Finding 2: Uncaught `std::bad_alloc` on Malformed LVL Dimensions
- **What**: Malformed `.LVL` inputs with large dimensions cause an uncaught `std::bad_alloc` crash instead of returning `false`.
- **Where**: `src/ants_assets/lvl_parser.cpp`, line 161 (`out_level.layer1_terrain.resize(cell_count)`).
- **Why**: When reading `width` and `height`, the code does not verify that the stream contains at least `cell_count * 12` remaining bytes before resizing vectors.
- **Suggestion**:
  Add validation before resizing:
  ```cpp
  if (r.remaining() < cell_count * 12) return false;
  ```
  Additionally, wrap vector resizing in a `try / catch (const std::exception&)` block to guarantee `load_from_memory` always returns `false` on malformed allocations.

### [Minor] Finding 3: Argument Aliasing Hazard in `mirror_bounding_box`
- **What**: Calling `mirror_bounding_box` with aliased input and output references produces incorrect bounding coordinates.
- **Where**: `include/ants_assets/mirroring.hpp`, lines 123–127.
- **Why**: `out_left = -right;` overwrites `left` before `out_right = -left;` is evaluated when `out_left` and `left` are the same variable.
- **Suggestion**:
  Store results in local temporary variables before assigning to reference outputs:
  ```cpp
  int32_t temp_left = -right;
  int32_t temp_right = -left;
  out_left = temp_left;
  out_right = temp_right;
  ```

### [Minor] Finding 4: In-Place Mirroring Hazard in `mirror_pixel_buffer`
- **What**: `mirror_pixel_buffer` assumes `src != dst`. If called in-place, pixels on the right half of scanlines are overwritten before being read.
- **Where**: `src/ants_assets/mirroring.cpp`, lines 40–59.
- **Suggestion**:
  Add a branch for `src == dst` to perform `std::swap(row[x], row[width - 1 - x])` across `x < width / 2`.

---

## 4. Verified Claims

- **CHD Header & Palette**: Verified 28-byte header, 256-color palette, color key 254 transparency (`alpha = 0`), and 4 team color ranges $\implies$ **PASS**.
- **Table 1 Sprites**: Verified 2,794 paletted sprites parsed with pitch stride preservation and 32-bit tightly packed RGBA output $\implies$ **PASS**.
- **Table 2 Audio**: Verified 91 PCM audio clips parsed with valid 44-byte RIFF WAV reconstruction, 88 mono, 3 stereo $\implies$ **PASS**.
- **Table 3 Tags**: Verified 4 event tags $\implies$ **PASS**.
- **Table 4 Animations**: Verified 1,344 animation sequences, 8,010 subitems, 12,210 frames, and 365 sound triggers $\implies$ **PASS**.
- **Map Parsing**: Verified all 6 `.LVL` map files parsed with strictly 0 remaining unparsed bytes (`rem = 0`) $\implies$ **PASS**.
- **5-to-8 Mirroring**: Verified reflection mathematics ($dx' = -(dx+W)$), pixel symmetry, lunchbox directional mapping, and $O(1)$ RAM atlas $\implies$ **PASS**.
- **Memory Safety**: Verified AddressSanitizer and UndefinedBehaviorSanitizer clean execution on normal inputs $\implies$ **PASS**.
- **AssetArchive Contract**: Conforms to all 6 methods in `PROJECT.md` $\implies$ **PASS**.

---

## 5. Caveats

- **No Caveats:** All original asset binary files (`ants.chd`, 6 map files in `Maps/`) were tested with real binary data. Independent verification programs were compiled and executed against the public headers.

---

## 6. Conclusion

Milestone 1 (`ants-assets`) contains a high-quality, genuine, and mathematically sound core implementation, but **must not be approved in its current state due to the critical interface contract breakage in `LevelData`**. Because `libants-assets` serves as the foundational data tier for Milestone 2 (`libants-sim`), the interface contracts defined in `PROJECT.md` must be adhered to strictly so that downstream modules can compile and integrate cleanly without interface churn.

**Verdict:** **REQUEST_CHANGES**

---

## 7. Verification Method

To independently reproduce and verify this review:

1. **Verify Interface Contract Failure:**
   Create `/tmp/contract_test.cpp`:
   ```cpp
   #include "ants_assets/lvl_parser.hpp"
   using namespace ants::assets;
   int main() {
       LevelData level;
       uint32_t w = level.width();
       uint32_t h = level.height();
       uint16_t t1 = level.layer1_terrain(0, 0);
       uint16_t t2 = level.layer2_item(0, 0);
       auto spawns = level.anthill_spawns();
       auto food = level.food_schedules();
       (void)w; (void)h; (void)t1; (void)t2; (void)spawns; (void)food;
       return 0;
   }
   ```
   Compile with:
   ```bash
   clang++ -std=c++17 -Iinclude -c /tmp/contract_test.cpp
   ```
   *Expected result*: Fails with 6 compilation errors.

2. **Verify Malformed Input Exception Crash:**
   Execute a test feeding a 100-byte buffer with `width = 0x7FFFFFFF` into `LevelData::load_from_memory`.
   *Expected result*: Throws uncaught `std::bad_alloc` / `std::length_error`.

3. **Verify Existing Tests:**
   ```bash
   cmake -B build && cmake --build build
   ./build/tests/test_assets/test_assets
   ./build_e2e/e2e_runner --all
   ```

**Invalidation Conditions:**
- When `LevelData` provides `width()`, `height()`, `layer1_terrain(x, y)`, `layer2_item(x, y)`, `anthill_spawns()`, and `food_schedules()` matching `PROJECT.md`, and all tests compile and pass.
