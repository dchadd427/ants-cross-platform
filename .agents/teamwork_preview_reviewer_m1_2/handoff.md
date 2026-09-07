# Review Handoff Report: Milestone 1 Native Binary Asset Decoder (`libants-assets`)

**Reviewer:** `reviewer_m1_2` (Reviewer & Adversarial Critic)  
**Date:** 2026-09-06T15:53:00-07:00  
**Target Module:** `libants-assets` (Milestone 1)  
**Parent Conversation ID:** `a28dfa55-5a82-453d-a21b-99459a66b340`  
**Working Directory:** `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_reviewer_m1_2`  

---

## Review Summary

**Verdict**: **REQUEST_CHANGES**

**Summary of Assessment**:  
Milestone 1 implementer `worker_m1_1` delivered a genuine, high-fidelity binary decoder that parses authentic original assets (`Original-Ants/ants.chd` and `Original-Ants/Maps/*.LVL`) without facade logic, shortcuts, or memory leaks under AddressSanitizer. The binary decoding fidelity, palette transparency, Table 1 stride padding handling, Table 2 RIFF WAV synthesis, Table 4 animation and sound trigger extraction, and mathematical directional mirroring are verified authentic and robust.

However, **REQUEST_CHANGES** is issued due to three concrete findings:
1. **[Critical - Contract Violation]**: The public interface of `LevelData` in `include/ants_assets/lvl_parser.hpp` fails to implement the mandatory interface methods specified in `PROJECT.md` (`width()`, `height()`, `layer1_terrain(x, y)`, `layer2_item(x, y)`, `anthill_spawns()`, `food_schedules()`), causing immediate compilation failures for Milestone 2 (`libants-sim`).
2. **[Major - Robustness / Exception Safety]**: In `LVLParser::load_from_memory`, vector allocation (`layer1_terrain.resize(cell_count)`) precedes stream size validation (`cell_count * 12 > r.remaining()`), causing an uncaught `std::bad_alloc` crash upon malformed dimension headers instead of a clean `false` return.
3. **[Minor - In-Place Aliasing Hazard]**: `mirror_bounding_box` in `include/ants_assets/mirroring.hpp` does not use temporary variables before writing to reference outputs, resulting in corrupted output coordinates when input and output variables alias (e.g. `mirror_bounding_box(b_left, b_right, b_left, b_right)`).

---

## 1. Observation

1. **Compilation and Sanitizer Execution:**
   - Command: `cmake -B build_asan -DENABLE_ASAN=ON && cmake --build build_asan`
     - Output: Exited code 0. Zero warnings under `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wnon-virtual-dtor`.
   - Command: `./build_asan/tests/test_assets/test_assets`
     - Output: Exited code 0. All 25 test cases across 8 suites passed (35,840 assertions).
     - AddressSanitizer / UndefinedBehaviorSanitizer: 0 heap-buffer-overflows, 0 stack overflows, 0 unaligned accesses, 0 memory leaks.
   - Command: `./build_e2e/e2e_runner --all`
     - Output: Exited code 0. 506 / 506 tests passed in 2.48 ms across all 4 tiers.
   - Command: `./build_asan/tests/test_assets/test_challenger_m1_1`
     - Output: Exited code 0. 24 / 24 challenge suites passed cleanly (13,204,992 assertions).

2. **Interface Contract in Master Project Specification (`PROJECT.md`):**
   In `/Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md` lines 111–118:
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

3. **Current Definition in `include/ants_assets/lvl_parser.hpp`:**
   Lines 88–101:
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
   ```
   Verbatim compiler output when invoking the contract specified in `PROJECT.md`:
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
   ```

4. **Resource Allocation Preceding Bounds Check in `LVLParser::load_from_memory`:**
   In `/Users/dchadd/Desktop/Ants-Mac/src/ants_assets/lvl_parser.cpp` lines 155–163:
   ```cpp
   if (!r.read_u32(out_level.width) || !r.read_u32(out_level.height)) return false;
   if (out_level.width == 0 || out_level.height == 0) return false;

   size_t cell_count = static_cast<size_t>(out_level.width) * out_level.height;

   // 4. Layer 1: Base Terrain Grid (cell_count * 6 bytes)
   out_level.layer1_terrain.resize(cell_count);
   ```
   When fed corrupted header bytes where `width = 0x80000000`, `cell_count` is large, triggering `std::bad_alloc` in `std::vector::resize()` before verifying `r.remaining() >= cell_count * 12`. This crashed Challenger 2 fuzzing test 6.5 with `Unexpected exception: std::bad_alloc`.

5. **Aliasing in `mirror_bounding_box`:**
   In `/Users/dchadd/Desktop/Ants-Mac/include/ants_assets/mirroring.hpp` lines 123–127:
   ```cpp
   constexpr inline void mirror_bounding_box(int32_t left, int32_t right,
                                             int32_t& out_left, int32_t& out_right) noexcept {
       out_left  = -right;
       out_right = -left;
   }
   ```
   If evaluated with aliased arguments (`mirror_bounding_box(left, right, left, right)`), `out_left = -right` mutates `left`, and `out_right = -left` reads the mutated `left` (`-right`), making `out_right = right` rather than `-original_left`.

6. **Raw Binary Data Verification:**
   Independent raw binary byte stream inspection via Python confirmed:
   - `Original-Ants/ants.chd`:
     - Version = 9, Timestamp = 0x378D661C.
     - Table offsets: 1052, 6835937, 7903773, 7903835.
     - Palette: 1024 bytes; index 254 is RGB(255, 0, 255) (magenta transparency key).
     - Table 1 count = 2,794; Sprite 0 = `dclay48.bmp` (45x47, pitch 48).
     - Table 2 count = 91; formats include 88 mono, 3 stereo (IDs 6, 45, 46).
     - Table 3 count = 4; IDs 3, 4, 5, 10.
     - Table 4 count = 1,344; 365 sound triggers.
   - `Original-Ants/Maps/*.LVL`:
     - `GAUNTLET.LVL`: 60x60, 1330 tiles, 32 spawns, 2 food pools, 20 waypoints, f_last=6, rem=0.
     - `ISLANDS.LVL`: 60x60, 1330 tiles, 40 spawns, 10 food pools, 22 waypoints, f_last=4, rem=0.
     - `MEDIUM.LVL`: 60x60, 1325 tiles, 30 spawns, 4 food pools, 15 waypoints, f_last=6, rem=0.
     - `SMALL.LVL`: 40x40, 1326 tiles, 18 spawns, 5 food pools, 2 waypoints, f_last=2, rem=0.
     - `TINY.LVL`: 31x31, 670 tiles, 12 spawns, 14 food pools, 0 waypoints, f_last=3, rem=0.
     - `TREASURE.LVL`: 60x60, 1335 tiles, 29 spawns, 18 food pools, 19 waypoints, f_last=9, rem=0.
     - All 6 authentic level files parse with strictly zero remaining unparsed bytes (`rem = 0`).

---

## 2. Logic Chain

1. **Integrity Chain:**
   - From Observation 1, 2, and 6: The asset parsing logic reads authentic binary bytes directly from disk streams. There are no hardcoded lookup tables replacing the decoding of `ants.chd` or `Maps/*.LVL`. All sprites, sounds, and map grids correspond to authentic reverse-engineered assets.
   - Deductions: No integrity violations detected. The implementation is genuine.

2. **Interface Conformance Chain:**
   - From Observation 2: `PROJECT.md` establishes a binding contract that `LevelData` provides accessor methods:
     `uint32_t width()`, `uint32_t height()`, `uint16_t layer1_terrain(uint32_t x, uint32_t y)`, `uint16_t layer2_item(uint32_t x, uint32_t y)`, `std::vector<AnthillSpawn> anthill_spawns()`, and `std::vector<FoodSchedule> food_schedules()`.
   - From Observation 3: In `include/ants_assets/lvl_parser.hpp`, `width` and `height` are public member variables; `layer1_terrain` is a member variable of type `std::vector<MapCell>` (which cannot be called with `(x, y)`); `layer2_interactive` has a mismatched name (`layer2_interactive` instead of `layer2_item(x, y)`); and `anthill_spawns` and `food_schedules` are vectors without functional accessor signatures.
   - Deductions: Downstream modules (specifically Milestone 2 `libants-sim`) cannot compile against `libants-assets` using the contract defined in `PROJECT.md`. This represents a Critical contract defect that blocks Milestone 2 integration.

3. **Memory Safety & Robustness Chain:**
   - From Observation 4: If an input buffer contains corrupt or crafted `width` / `height` integers, `out_level.layer1_terrain.resize(cell_count)` will attempt to allocate memory before confirming whether the buffer contains enough remaining bytes (`cell_count * 12`).
   - Deductions: Because `load_from_memory` is a public parsing entry point expected to return `bool`, throwing uncaught `std::bad_alloc` causes unexpected program termination on corrupted maps. Stream length validation (`r.remaining() >= cell_count * 12`) and defensive try-catch handling must be added.

4. **Mathematical Correctness Chain:**
   - From Observation 5: In `mirror_bounding_box`, sequential assignment without temporaries leads to incorrect calculations when caller passes the same variables as inputs and outputs.
   - Deductions: Introducing local temporaries (`int32_t temp_left = -right; int32_t temp_right = -left; out_left = temp_left; out_right = temp_right;`) guarantees invariance under argument aliasing.

---

## 3. Findings

### [Critical] Finding 1: Interface Contract Non-Conformance in `LevelData`
- **What**: `LevelData` does not provide the public accessor methods specified in `PROJECT.md`.
- **Where**: `include/ants_assets/lvl_parser.hpp`, lines 88–115.
- **Why**: Milestone 2 (`libants-sim`) depends on the documented contract:
  - `width() -> uint32_t`
  - `height() -> uint32_t`
  - `layer1_terrain(uint32_t x, uint32_t y) -> uint16_t`
  - `layer2_item(uint32_t x, uint32_t y) -> uint16_t`
  - `anthill_spawns() -> std::vector<AnthillSpawn>` (or `const std::vector<AnthillSpawn>&`)
  - `food_schedules() -> std::vector<FoodSchedule>` (or `const std::vector<FoodSchedule>&`)
  Calling these methods currently causes 6 compilation errors.
- **Suggestion**:
  Refactor `LevelData` in `include/ants_assets/lvl_parser.hpp`:
  1. Rename internal member variables (e.g. `width_`, `height_`, `layer1_terrain_`, `layer2_interactive_`, `anthill_spawns_`, `food_schedules_`).
  2. Add the required public accessor methods:
     ```cpp
     uint32_t width() const noexcept { return width_; }
     uint32_t height() const noexcept { return height_; }
     uint16_t layer1_terrain(uint32_t x, uint32_t y) const noexcept {
         return get_cell_layer1(x, y).tile_index;
     }
     uint16_t layer2_item(uint32_t x, uint32_t y) const noexcept {
         return get_cell_layer2(x, y).tile_index;
     }
     const std::vector<AnthillSpawn>& anthill_spawns() const noexcept { return anthill_spawns_; }
     const std::vector<FoodSchedule>& food_schedules() const noexcept { return food_schedules_; }
     ```
  3. Keep the existing cell and raw container accessors as overloads or companion methods for flexibility.
  4. Update `src/ants_assets/lvl_parser.cpp` and `tests/test_assets/test_assets.cpp` to use the updated member variable names or accessors.

### [Major] Finding 2: Uncaught `std::bad_alloc` on Malformed LVL Dimensions
- **What**: Vector allocation prior to buffer remaining checks triggers uncaught `std::bad_alloc` on corrupted inputs.
- **Where**: `src/ants_assets/lvl_parser.cpp`, line 161.
- **Why**: Malformed or fuzzed `.LVL` files specifying large `width` and `height` cause `std::vector::resize` to exhaust memory or exceed max size before `BinaryReader::read_bytes` fails.
- **Suggestion**:
  In `LVLParser::load_from_memory`:
  ```cpp
  // Ensure the buffer contains at least the required bytes for Layer 1 and Layer 2
  if (r.remaining() < cell_count * 12) return false;
  ```
  Apply similar checks for `dict_count` (`r.remaining() < dict_count * 11`) and wrap the parsing loop in a `try / catch (const std::bad_alloc&)` block to return `false` on allocation failure.

### [Minor] Finding 3: In-Place Aliasing Hazard in `mirror_bounding_box`
- **What**: Calling `mirror_bounding_box` with aliased input and output references produces incorrect bounding coordinates.
- **Where**: `include/ants_assets/mirroring.hpp`, lines 123–127.
- **Why**: `out_left = -right;` overwrites `left` when `out_left` is a reference to `left`, causing `out_right = -left` to use the newly assigned value.
- **Suggestion**:
  ```cpp
  constexpr inline void mirror_bounding_box(int32_t left, int32_t right,
                                            int32_t& out_left, int32_t& out_right) noexcept {
      int32_t temp_left = -right;
      int32_t temp_right = -left;
      out_left  = temp_left;
      out_right = temp_right;
  }
  ```

---

## 4. Caveats

- **No Caveats.** Full investigation was conducted on all source code files, tests, original binary archives (`ants.chd`, all 6 `.LVL` files), and execution under AddressSanitizer and UndefinedBehaviorSanitizer.
- Note on `test_challenger_m1_2.cpp`: Several test failures in `test_challenger_m1_2.cpp` stemmed from erroneous test assumptions about the 1995 game data (e.g. asserting non-existent animation prefixes `agfg`/`acfg`/`aglb`/`atlb`, asserting all food variant weights `> 0` when `TINY.LVL` contains sentinel variants with weight 0, and asserting all Block 1 objects are team anthill spawns when decorative objects also reside in Block 1). Only the mutation fuzzing failure (`std::bad_alloc`) reflects a genuine library robustness defect.

---

## 5. Conclusion

The decoding logic and memory safety of `libants-assets` under normal conditions are exceptional, demonstrating 100% test passes and zero AddressSanitizer violations. However, the interface contract mismatch on `LevelData` and the missing defensive allocation checks on malformed input must be addressed before approving Milestone 1.

**Verdict: REQUEST_CHANGES**

---

## 6. Verification Method

To verify the changes requested:

1. **Verify Interface Contract Conformance:**
   Compile a test snippet asserting that `LevelData` implements the `PROJECT.md` contract:
   ```cpp
   ants::assets::LevelData lvl;
   uint32_t w = lvl.width();
   uint32_t h = lvl.height();
   uint16_t t1 = lvl.layer1_terrain(0, 0);
   uint16_t t2 = lvl.layer2_item(0, 0);
   const std::vector<ants::assets::AnthillSpawn>& spawns = lvl.anthill_spawns();
   const std::vector<ants::assets::FoodSchedule>& food = lvl.food_schedules();
   (void)w; (void)h; (void)t1; (void)t2; (void)spawns; (void)food;
   ```
2. **Verify Memory Allocation Safety under Mutation Fuzzing:**
   Pass a 100-byte buffer with `width = 0x80000000` to `LVLParser::load_from_memory` and verify it returns `false` without throwing an uncaught `std::bad_alloc` exception.
3. **Verify Aliasing Safety in `mirror_bounding_box`:**
   ```cpp
   int32_t l = -10, r = 25;
   ants::assets::mirror_bounding_box(l, r, l, r);
   assert(l == -25 && r == 10);
   ```
4. **Run Full Test Suite with AddressSanitizer:**
   ```bash
   cmake -B build_asan -DENABLE_ASAN=ON && cmake --build build_asan
   ./build_asan/tests/test_assets/test_assets
   ./build_e2e/e2e_runner --all
   ```
