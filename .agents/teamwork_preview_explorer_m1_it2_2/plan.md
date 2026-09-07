# Milestone 1 Remediation Plan: Input-Bounded Allocations & CWE-789 Mitigation

**Author:** `explorer_m1_it2_2` (Explorer Archetype: Investigation & Synthesis)  
**Date:** 2026-09-06T22:58:00Z  
**Target Components:** `src/ants_assets/lvl_parser.cpp`, `src/ants_assets/chd_parser.cpp`  
**Working Directory:** `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_it2_2`  
**Related Agents:** `worker_m1_1` (Implementer), `explorer_m1_it2_1` (Interface Contracts), `explorer_m1_it2_3` (Cleanliness & Test Runner)

---

## 1. Executive Summary & Root Cause Analysis

During Milestone 1 adversarial stress testing, Challengers `challenger_m1_1` and `challenger_m1_2` confirmed a severe **CWE-789 (Uncontrolled Memory Allocation)** vulnerability affecting both the level deserializer (`LVLParser`) and archive deserializer (`CHDParser`):

1. **Unbounded Map Dimensions in `LVLParser`:**
   In `src/ants_assets/lvl_parser.cpp` (lines 155–163), `out_level.width` and `out_level.height` were read as arbitrary 32-bit integers. Immediately afterward, `out_level.layer1_terrain.resize(cell_count)` was called with `cell_count = width * height` without checking:
   - Whether dimensions satisfy authentic engine bounds (Microsoft Ants maps are 31×31, 40×40, 60×60).
   - Whether the remaining stream contains sufficient payload bytes (`cell_count * 12` bytes for Layer 1 + Layer 2).
   - Whether memory allocation throws `std::bad_alloc` or triggers AddressSanitizer `allocation-size-too-big`.
   **Empirical Consequence:** In `test_challenger_m1_2.cpp` (Test 6.5), feeding a 19 KB map with dimensions $500 \times 500$ allocated 250,000 cells (1.5 MB) before failing. Fuzzed inputs with dimensions `0x7FFFFFFF` triggered AddressSanitizer abort or release build hangs attempting to allocate ~825 GB of RAM.

2. **Unbounded Allocations in `CHDParser`:**
   In `src/ants_assets/chd_parser.cpp`:
   - **Table 1 Sprites (lines 273–275):** `sp.pixels.resize(pixel_bytes)` where `pixel_bytes = pitch * height` was executed before verifying `pixel_bytes <= r.remaining()`.
   - **Table 2 Sounds (lines 328–331):** `snd.pcm_data.resize(pcm_len)` was executed before verifying `pcm_len <= r.remaining()`.
   - **Table 4 Animations (lines 423–438):** `anim.subitems.resize(subitem_count)` and `sub.frames.resize(frame_count)` were executed without checking stream capacity against subitem and frame byte footprints.
   - **Header Size Guard (lines 185, 207):** Verify `size >= 28` header size guard and Table 4 offset bound.

---

## 2. Concrete Remediation Specifications

### 2.1 Remediation 1: `LVLParser` (`src/ants_assets/lvl_parser.cpp`)

#### 1. Dimension Sanity Bounds
- **Invariant:** Microsoft Ants authentic maps have dimensions 31×31, 40×40, or 60×60.
- **Rule:** Reject any map with `width == 0 || height == 0 || width > 256 || height > 256`.
- **Rationale:** 256×256 allows ample headroom for community/custom maps while strictly bounding `cell_count <= 65,536` cells, eliminating 32-bit/64-bit integer overflow.

#### 2. Stream Capacity Pre-Validation Before Vector Allocation
- **Invariant:** Each cell occupies 6 bytes in Layer 1 (terrain: `tile_index`, `flags`, `properties`) and 6 bytes in Layer 2 (interactive overlay: `tile_index`, `flags`, `properties`). Total byte requirement for terrain grids is `cell_count * 12` bytes.
- **Rule:** Validate `r.remaining() >= cell_count * 12` BEFORE calling `out_level.layer1_terrain.resize(cell_count)`.
- **Apply to Trailing Blocks:**
  * Tile dictionary: `if (r.remaining() < dict_count * 11) return false;` before `tile_dictionary.resize(dict_count)`.
  * Anthill spawns (Block 1): `if (r.remaining() < static_cast<size_t>(b1_count) * 6) return false;` before `anthill_spawns.resize(b1_count)`.
  * Food schedules (Block 2): `if (r.remaining() < static_cast<size_t>(b2_count) * 10) return false;` before `food_schedules.resize(b2_count)`.
  * Food variants: `if (r.remaining() < static_cast<size_t>(item_count) * 4) return false;` before `fs.variants.resize(item_count)`.
  * Waypoints (Block 4): `if (r.remaining() < static_cast<size_t>(b4_count) * 8) return false;` before `waypoints.resize(b4_count)`.

#### 3. Exception Safety & Fail-Safe Clean State
- **Rule:** Wrap `load_from_file` and `load_from_memory` in `try { ... } catch (...) { out_level = LevelData{}; return false; }`.
- **Rationale:** Guarantees that any memory allocation failure (`std::bad_alloc` or system limits) is gracefully converted to `return false`, leaving `out_level` in an empty, initialized state with 0 allocated cells.

#### 4. Defensive Vector Bounds in Accessors
- **Rule:** In `LevelData::get_cell_layer1` and `LevelData::get_cell_layer2`, verify `idx < layer_vector.size()` before indexing:
  ```cpp
  size_t idx = static_cast<size_t>(y) * width + x;
  if (idx >= layer1_terrain.size()) return EMPTY_CELL;
  return layer1_terrain[idx];
  ```

---

### 2.2 Remediation 2: `CHDParser` (`src/ants_assets/chd_parser.cpp`)

#### 1. Header Size & Offset Guards
- **Rule:** Verify `size >= 28` in `parse_header` (line 185: `if (!data || size < 28) return false;`).
- **Rule:** Enforce `if (size > 28 && out_header.table4_offset >= size) return false;` (line 207). When `size == 28`, header parsing alone is permitted (as validated in `test_assets.cpp:169`); when `size > 28`, Table 4 offset must be strictly within stream bounds.

#### 2. Sprite Pixels Capacity Check (`parse_table1_sprites`)
- **Rule:** Check `uint64_t pixel_bytes = static_cast<uint64_t>(sp.pitch) * sp.height;`.
- **Rule:** `if (pixel_bytes > r.remaining()) return false;` BEFORE invoking `sp.pixels.resize(...)`.
- **Filename check:** `if (fn_len > r.remaining()) return false;`.
- **Offset table check:** `if (r.remaining() < static_cast<size_t>(count) * 4) return false;`.

#### 3. PCM Audio Buffer Capacity Check (`parse_table2_sounds`)
- **Rule:** After reading `pcm_len`, verify `if (pcm_len > r.remaining()) return false;` BEFORE invoking `snd.pcm_data.resize(pcm_len)`.
- **Filename check:** `if (fn_len > 256 || fn_len > r.remaining()) return false;`.
- **Offset table check:** `if (r.remaining() < static_cast<size_t>(count) * 4) return false;`.

#### 4. Event Tag Name Capacity Check (`parse_table3_tags`)
- **Rule:** `if (name_len > 256 || name_len > r.remaining()) return false;`.

#### 5. Animation Sequences Capacity Check (`parse_table4_animations`)
- **Rule:** Offset table check: `if (r.remaining() < static_cast<size_t>(count) * 4) return false;`.
- **Rule:** Subitem count check: Each subitem requires at least 40 bytes for fixed fields (10 × 32-bit fields).
  `if (static_cast<uint64_t>(subitem_count) * 40 > r.remaining()) return false;` BEFORE `anim.subitems.resize(subitem_count)`.
- **Rule:** Frame count check: Each frame requires at least 12 bytes (`dx`, `dy`, `sprite_index`).
  `if (static_cast<uint64_t>(frame_count) * 12 > r.remaining()) return false;` BEFORE `sub.frames.resize(frame_count)`.

#### 6. Exception Safety
- **Rule:** Wrap `parse_table1_sprites`, `parse_table2_sounds`, `parse_table3_tags`, and `parse_table4_animations` in `try { ... } catch (...) { out_vector.clear(); return false; }`.

---

## 3. Code Change Snippets (Before vs. After)

### Snippet 1: `src/ants_assets/lvl_parser.cpp` (Dimension & Stream Validation)

**Before (Lines 154–177):**
```cpp
    // 3. Grid Dimensions (8 bytes)
    if (!r.read_u32(out_level.width) || !r.read_u32(out_level.height)) return false;
    if (out_level.width == 0 || out_level.height == 0) return false;

    size_t cell_count = static_cast<size_t>(out_level.width) * out_level.height;

    // 4. Layer 1: Base Terrain Grid (cell_count * 6 bytes)
    out_level.layer1_terrain.resize(cell_count);
    for (size_t i = 0; i < cell_count; ++i) {
        MapCell& c = out_level.layer1_terrain[i];
        if (!r.read_u16(c.tile_index) || !r.read_u16(c.flags) || !r.read_u16(c.properties)) {
            return false;
        }
    }

    // 5. Layer 2: Interactive Overlay Grid (cell_count * 6 bytes)
    out_level.layer2_interactive.resize(cell_count);
    for (size_t i = 0; i < cell_count; ++i) {
        MapCell& c = out_level.layer2_interactive[i];
        if (!r.read_u16(c.tile_index) || !r.read_u16(c.flags) || !r.read_u16(c.properties)) {
            return false;
        }
    }
```

**After:**
```cpp
    // 3. Grid Dimensions (8 bytes)
    if (!r.read_u32(out_level.width) || !r.read_u32(out_level.height)) return false;
    // Enforce upper sanity limit on map dimensions (Microsoft Ants max is 60x60, allow up to 256x256)
    if (out_level.width == 0 || out_level.height == 0 ||
        out_level.width > 256 || out_level.height > 256) {
        return false;
    }

    size_t cell_count = static_cast<size_t>(out_level.width) * out_level.height;

    // Stream capacity check BEFORE allocating terrain layers (Layer 1 + Layer 2 = 12 bytes/cell)
    if (r.remaining() < cell_count * 12) {
        return false;
    }

    // 4. Layer 1: Base Terrain Grid (cell_count * 6 bytes)
    out_level.layer1_terrain.resize(cell_count);
    for (size_t i = 0; i < cell_count; ++i) {
        MapCell& c = out_level.layer1_terrain[i];
        if (!r.read_u16(c.tile_index) || !r.read_u16(c.flags) || !r.read_u16(c.properties)) {
            return false;
        }
    }

    // 5. Layer 2: Interactive Overlay Grid (cell_count * 6 bytes)
    out_level.layer2_interactive.resize(cell_count);
    for (size_t i = 0; i < cell_count; ++i) {
        MapCell& c = out_level.layer2_interactive[i];
        if (!r.read_u16(c.tile_index) || !r.read_u16(c.flags) || !r.read_u16(c.properties)) {
            return false;
        }
    }
```

---

### Snippet 2: `src/ants_assets/chd_parser.cpp` (Table 1 Sprite Pixels Bounded Allocation)

**Before (Lines 272–276):**
```cpp
        size_t pixel_bytes = static_cast<size_t>(sp.pitch) * sp.height;
        sp.pixels.resize(pixel_bytes);
        if (!r.read_bytes(sp.pixels.data(), pixel_bytes)) return false;
```

**After:**
```cpp
        uint64_t pixel_bytes = static_cast<uint64_t>(sp.pitch) * sp.height;
        // Stream capacity check BEFORE allocating sprite pixel buffer
        if (pixel_bytes > r.remaining()) {
            return false;
        }

        sp.pixels.resize(static_cast<size_t>(pixel_bytes));
        if (!r.read_bytes(sp.pixels.data(), static_cast<size_t>(pixel_bytes))) return false;
```

---

### Snippet 3: `src/ants_assets/chd_parser.cpp` (Table 2 PCM Audio Bounded Allocation)

**Before (Lines 326–332):**
```cpp
        uint32_t pcm_len = 0;
        if (!r.read_u32(pcm_len)) return false;
        snd.pcm_data.resize(pcm_len);
        if (pcm_len > 0) {
            if (!r.read_bytes(snd.pcm_data.data(), pcm_len)) return false;
        }
```

**After:**
```cpp
        uint32_t pcm_len = 0;
        if (!r.read_u32(pcm_len)) return false;

        // Stream capacity check BEFORE allocating PCM sound buffer
        if (pcm_len > r.remaining()) {
            return false;
        }

        snd.pcm_data.resize(pcm_len);
        if (pcm_len > 0) {
            if (!r.read_bytes(snd.pcm_data.data(), pcm_len)) return false;
        }
```

---

### Snippet 4: `src/ants_assets/chd_parser.cpp` (Table 4 Animations & Subitems Bounded Allocation)

**Before (Lines 417–444):**
```cpp
        uint32_t subitem_count = 0;
        if (!r.read_u32(anim.flag1) || !r.read_u32(anim.flag2) ||
            !r.read_u32(anim.flag3) || !r.read_u32(subitem_count)) {
            return false;
        }

        anim.subitems.resize(subitem_count);
        for (uint32_t s = 0; s < subitem_count; ++s) {
            AnimationSubItem& sub = anim.subitems[s];
            uint32_t frame_count = 0;

            if (!r.read_u32(sub.val1) || !r.read_u32(sub.val2) || !r.read_u32(sub.val3) ||
                !r.read_i32(sub.box_left) || !r.read_i32(sub.box_top) ||
                !r.read_i32(sub.box_right) || !r.read_i32(sub.box_bottom) ||
                !r.read_u32(sub.flags) || !r.read_u32(sub.default_sp) ||
                !r.read_u32(frame_count)) {
                return false;
            }
            sub.v8 = sub.flags;

            sub.frames.resize(frame_count);
            for (uint32_t f = 0; f < frame_count; ++f) {
                AnimationFrame& fr = sub.frames[f];
                if (!r.read_i32(fr.dx) || !r.read_i32(fr.dy) || !r.read_u32(fr.sprite_index)) {
                    return false;
                }
            }
        }
```

**After:**
```cpp
        uint32_t subitem_count = 0;
        if (!r.read_u32(anim.flag1) || !r.read_u32(anim.flag2) ||
            !r.read_u32(anim.flag3) || !r.read_u32(subitem_count)) {
            return false;
        }

        // Each subitem requires at least 40 bytes for fixed fields (10 x uint32_t/int32_t)
        if (static_cast<uint64_t>(subitem_count) * 40 > r.remaining()) {
            return false;
        }

        anim.subitems.resize(subitem_count);
        for (uint32_t s = 0; s < subitem_count; ++s) {
            AnimationSubItem& sub = anim.subitems[s];
            uint32_t frame_count = 0;

            if (!r.read_u32(sub.val1) || !r.read_u32(sub.val2) || !r.read_u32(sub.val3) ||
                !r.read_i32(sub.box_left) || !r.read_i32(sub.box_top) ||
                !r.read_i32(sub.box_right) || !r.read_i32(sub.box_bottom) ||
                !r.read_u32(sub.flags) || !r.read_u32(sub.default_sp) ||
                !r.read_u32(frame_count)) {
                return false;
            }
            sub.v8 = sub.flags;

            // Each frame requires at least 12 bytes (dx, dy, sprite_index)
            if (static_cast<uint64_t>(frame_count) * 12 > r.remaining()) {
                return false;
            }

            sub.frames.resize(frame_count);
            for (uint32_t f = 0; f < frame_count; ++f) {
                AnimationFrame& fr = sub.frames[f];
                if (!r.read_i32(fr.dx) || !r.read_i32(fr.dy) || !r.read_u32(fr.sprite_index)) {
                    return false;
                }
            }
        }
```

---

## 4. Artifact & Patch Inventory

All proposed changes have been pre-compiled and verified with Clang (`clang++ -std=c++17 -fsyntax-only`):

1. **`lvl_parser.patch`:**  
   Path: `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_it2_2/lvl_parser.patch`  
   Applies cleanly to `src/ants_assets/lvl_parser.cpp` with `patch --dry-run`.
2. **`chd_parser.patch`:**  
   Path: `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_it2_2/chd_parser.patch`  
   Applies cleanly to `src/ants_assets/chd_parser.cpp` with `patch --dry-run`.
3. **`proposed_lvl_parser.cpp`:**  
   Path: `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_it2_2/proposed_lvl_parser.cpp`  
   Complete drop-in replacement file.
4. **`proposed_chd_parser.cpp`:**  
   Path: `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_it2_2/proposed_chd_parser.cpp`  
   Complete drop-in replacement file.

---

## 5. Implementer Instructions for `worker_m1_1`

To apply the fixes during Milestone 1 Iteration 2 execution:

```bash
cd /Users/dchadd/Desktop/Ants-Mac

# Option A: Apply unified diff patches directly
patch -p0 < .agents/teamwork_preview_explorer_m1_it2_2/lvl_parser.patch
patch -p0 < .agents/teamwork_preview_explorer_m1_it2_2/chd_parser.patch

# Option B: Copy proposed replacement files
cp .agents/teamwork_preview_explorer_m1_it2_2/proposed_lvl_parser.cpp src/ants_assets/lvl_parser.cpp
cp .agents/teamwork_preview_explorer_m1_it2_2/proposed_chd_parser.cpp src/ants_assets/chd_parser.cpp
```

After applying, compile and execute the test suites:
```bash
cmake -B build
cmake --build build
./build/tests/test_assets/test_challenger_m1_2
```
Expected result: Test 6.5 passes with `allocated_cells == 0u`, achieving 25/25 passed tests with 0 failures.
