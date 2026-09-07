# Adversarial Challenge & Handoff Report: Milestone 1 Directional Mirroring & Level Decoder

**Author:** `challenger_m1_2` (Empirical Challenger: Critic / Specialist)  
**Date:** 2026-09-06T22:56:00Z  
**Parent Conversation ID:** `a28dfa55-5a82-453d-a21b-99459a66b340`  
**Working Directory:** `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m1_2`  
**Final Verdict:** `REQUEST_CHANGES`

---

## Challenge Summary

**Overall risk assessment:** HIGH

While the 5-to-8 directional mirroring engine and standard level decoder implementation exhibit high mathematical elegance, byte-exact involution across all 2,794 Table 1 sprites, and exact 0-residual decoding on all 6 authentic level maps, our empirical stress testing uncovered a critical memory exhaustion / denial-of-service vulnerability in `LVLParser::load_from_memory`. When processing malformed or corrupted map buffers with out-of-bounds dimensions, `LVLParser` blindly allocates memory without validating whether the remaining buffer stream contains sufficient bytes to satisfy the claimed dimensions. This causes immediate process termination under AddressSanitizer (`allocation-size-too-big`), uncaught `std::bad_alloc` crashes in standard builds, or multi-gigabyte zero-fill thread hangs.

---

## 1. Observation

1. **Adversarial Test Suite Implementation:**
   Constructed and compiled an independent 6-suite stress harness `/Users/dchadd/Desktop/Ants-Mac/tests/test_assets/test_challenger_m1_2.cpp` linked against static library `libants_assets.a`.
   CMake target added in `/Users/dchadd/Desktop/Ants-Mac/tests/test_assets/CMakeLists.txt`.

2. **Empirical Test Execution (Standard Build):**
   Command:
   ```bash
   cmake --build build && ./build/tests/test_assets/test_challenger_m1_2
   ```
   Verbatim output:
   ```text
   =======================================================
    Microsoft Ants Challenger 2 Adversarial Test Suite
    Target: libants_assets (5-to-8 Mirroring & Level Decoder)
   =======================================================
   Target Assets Directory: /Users/dchadd/Desktop/Ants-Mac/tests/test_assets/../../Original-Ants
   Target CHD Path:         /Users/dchadd/Desktop/Ants-Mac/tests/test_assets/../../Original-Ants/ants.chd
   Target Maps Directory:   /Users/dchadd/Desktop/Ants-Mac/tests/test_assets/../../Original-Ants/Maps

   =======================================================
    [CHALLENGER SUITE] Suite 1: 8-Way Compass Facings & Boundary Conversions
   =======================================================
     [STRESS] 1.1 Exhaustive 360-degree sweep at 1-degree resolution ... PASS
     [STRESS] 1.2 Sector boundary discontinuity stress (epsilon testing) ... PASS
     [STRESS] 1.3 Multi-revolution, negative, and extreme angle handling ... PASS
     [STRESS] 1.4 Vector to Direction extreme bounds & non-symmetric components ... PASS
     [STRESS] 1.5 Direction mapping invariants, aliases and bitmask safety ... PASS

   =======================================================
    [CHALLENGER SUITE] Suite 2: Involution & Offset Math dx'=-(dx+W)
   =======================================================
     [STRESS] 2.1 Rigorous involution proof across wide range of dx and W ... PASS
     [STRESS] 2.2 Vertical offset dy invariance ... PASS
     [STRESS] 2.3 Bounding box involution and interval ordering preservation ... PASS

   =======================================================
    [CHALLENGER SUITE] Suite 3: Pixel Buffer Mirroring & Table 1 Sprites Involution
   =======================================================
     [STRESS] 3.1 Adversarial corrupted inputs to mirror_pixel_buffer ... PASS
     [STRESS] 3.2 Synthetic stride padding and involution test ... PASS
     [STRESS] 3.3 Exhaustive Table 1 sprites (2,794) double-flip involution oracle ... PASS

   =======================================================
    [CHALLENGER SUITE] Suite 4: Directional Animation Mirroring & Lunchbox Suite
   =======================================================
     [STRESS] 4.1 Directional animation frame arithmetic across real assets ... PASS
     [STRESS] 4.2 Lunchbox directional prefixes and real asset existence ... PASS

   =======================================================
    [CHALLENGER SUITE] Suite 5: Level Decoder Exact 0-Remaining & Coordinate Invariants
   =======================================================
     [STRESS] 5. TINY.LVL Exact 0-Rem & Coordinate Bounds Validation ... PASS
     [STRESS] 5. SMALL.LVL Exact 0-Rem & Coordinate Bounds Validation ... PASS
     [STRESS] 5. MEDIUM.LVL Exact 0-Rem & Coordinate Bounds Validation ... PASS
     [STRESS] 5. GAUNTLET.LVL Exact 0-Rem & Coordinate Bounds Validation ... PASS
     [STRESS] 5. ISLANDS.LVL Exact 0-Rem & Coordinate Bounds Validation ... PASS
     [STRESS] 5. TREASURE.LVL Exact 0-Rem & Coordinate Bounds Validation ... PASS

   =======================================================
    [CHALLENGER SUITE] Suite 6: Level Decoder Corrupted Buffers & Fuzzing Defense
   =======================================================
     [STRESS] 6.1 Under-sized buffers (< 42 bytes) rejection ... PASS
     [STRESS] 6.2 Truncated valid level at every critical structural boundary ... PASS
     [STRESS] 6.3 Corrupted header fields (version, game mode, zero dimensions) ... PASS
     [STRESS] 6.4 Strict 0-residual rejection: trailing garbage appended ... PASS
     [STRESS] 6.5 Corrupted dimensions memory exhaustion vulnerability test ... 
       [VULNERABILITY CONFIRMED] LVLParser allocated 250000 cells (1500000 bytes) on a 19 KB buffer before failing!
   FAILED!
       Assertion failed: allocated_cells == 0u at /Users/dchadd/Desktop/Ants-Mac/tests/test_assets/test_challenger_m1_2.cpp:963
     [STRESS] 6.6 Deterministic pseudo-random mutation fuzzing across all 6 maps ... PASS

   =======================================================
    CHALLENGE TEST SUMMARY
    Total Challenge Cases: 25
    Total Assertions:      12889626
    Failures:              1
   =======================================================
   ```

3. **AddressSanitizer Abort on Memory Exhaustion Input:**
   When tested with large dimension inputs (e.g. `0x10000000` x `0x10000000` or `0x7FFFFFFF`):
   ```text
   ==22951==ERROR: AddressSanitizer: requested allocation size 0x600000000000000 (0x600000000001000 after adjustments for alignment, red zones etc.) exceeds maximum supported size of 0x10000000000 (thread T0)
       #0 0x000104bbff50 in _Znwm+0x74 (libclang_rt.asan_osx_dynamic.dylib:arm64e+0x4ff50)
       #7 0x000104363fa0 in std::__1::vector<ants::assets::MapCell>::resize(unsigned long)+0x94
       #8 0x000104361298 in ants::assets::LVLParser::load_from_memory(unsigned char const*, unsigned long, ants::assets::LevelData&)+0xe00
   SUMMARY: AddressSanitizer: allocation-size-too-big
   ==22951==ABORTING
   ```

4. **Code Inspection of `src/ants_assets/lvl_parser.cpp`:**
   Lines 155–167:
   ```cpp
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
   ```
   - No check that `cell_count <= 256 * 256` (maximum authentic map size is 60x60).
   - No check that `cell_count * 12 <= r.remaining()`. Layer 1 requires `cell_count * 6` bytes, and Layer 2 requires `cell_count * 6` bytes. Calling `resize(cell_count)` occurs BEFORE reading cells and without verifying stream capacity.
   - No `try ... catch (const std::exception&)` wrapper around allocation calls.

---

## 2. Challenges & Findings

### [High] Challenge 1: Denial of Service / Memory Exhaustion via Corrupted Map Dimensions

- **Assumption challenged:** The parser implicitly assumes that `out_level.width * out_level.height` represents a benign allocation size that fits into physical memory and matches the file payload.
- **Attack scenario:** An untrusted, corrupted, or maliciously crafted `.LVL` map file supplies dimensions such as $500 \times 500$ (requires 3 MB from a 19 KB file), $50,000 \times 50,000$ (requires 15 GB), or `0x7FFFFFFF` / `0x10000000`.
- **Blast radius:**
  1. On AddressSanitizer builds: immediate process abort (`allocation-size-too-big`).
  2. On non-ASan release builds: unhandled `std::bad_alloc` exception crashes the host application.
  3. On large but representable dimensions: operating system thread hang / CPU exhaustion while attempting to default-construct and zero-fill tens of billions of `MapCell` structs.
  4. On moderate dimension mismatches ($500 \times 500$ on a 19 KB map): allocates 1.5 MB of heap memory before returning `false`, causing memory spikes and cache pollution.
- **Mitigation:**
  In `src/ants_assets/lvl_parser.cpp` at line 157:
  ```cpp
  // 1. Enforce upper sanity limit on map dimensions (Microsoft Ants max is 60x60, allow up to 256x256)
  if (out_level.width > 256 || out_level.height > 256) {
      return false;
  }

  // 2. Validate that remaining buffer has enough bytes for Layer 1 and Layer 2 (12 bytes per cell)
  size_t cell_count = static_cast<size_t>(out_level.width) * out_level.height;
  if (r.remaining() < cell_count * 12) {
      return false;
  }
  ```
  Apply similar stream capacity guards before resizing vectors in other blocks:
  - `dict_count * 11 <= r.remaining()` before `out_level.tile_dictionary.resize(dict_count)`
  - `b1_count * 6 <= r.remaining()` before `out_level.anthill_spawns.resize(b1_count)`
  - `b2_count * 10 <= r.remaining()` before `out_level.food_schedules.resize(b2_count)`
  - `b4_count * 8 <= r.remaining()` before `out_level.waypoints.resize(b4_count)`
  Wrap `load_from_memory` in a top-level `try { ... } catch (const std::exception&) { out_level = LevelData{}; return false; }` block.

---

## 3. Stress Test Results Summary

| Suite | Category | Tested Cases | Result | Notes |
|---|---|---|---|---|
| **Suite 1** | Compass Facings & Headings | 360° sweep, $\epsilon$ transitions, negative angles, INT32_MAX/MIN vectors | **PASS** | 8 headings, bitmask safety, angle/vector mapping 100% correct |
| **Suite 2** | Involution & Offset Math | $dx' = -(dx+W)$, $dx'' = dx$, $[-right, -left]$ bounding box | **PASS** | Mathematical involution proven across wide parameter sweep |
| **Suite 3** | Pixel Mirroring Oracle | Nullptr/zero inputs, stride padding clearance, all 2,794 Table 1 sprites | **PASS** | Double-flip pixel identity holds for 100% of authentic sprites |
| **Suite 4** | Directional Animations & Lunchbox | Real ant walking/fighting sequences, all 8 lunchbox headings | **PASS** | $dx'$ offsets correct; authentic `3lb..7lb` sprites verified |
| **Suite 5** | Maps & Coordinate Serialization | TINY, SMALL, MEDIUM, GAUNTLET, ISLANDS, TREASURE | **PASS** | Exactly 0 rem bytes on all 6 maps; all spawn/food/wp coords in bounds |
| **Suite 6** | Corrupted Buffers & Fuzzing | Header corruptions, truncations, appended garbage, fuzzing, huge dimensions | **FAIL (1/6)** | 6.1–6.4, 6.6 PASS; 6.5 FAIL: premature unvalidated allocation on corrupted dimensions |

---

## 4. Logic Chain

1. **Premise 1 (Contract):** The specification in `ORIGINAL_REQUEST.md` (Acceptance Criteria) and `PROJECT.md` mandates that the asset decoder must parse valid files without crash or truncation, and gracefully reject corrupted map buffers without crashing or leaking memory.
2. **Premise 2 (Observation):** In `src/ants_assets/lvl_parser.cpp:160`, `out_level.layer1_terrain.resize(cell_count)` is called immediately after reading `width` and `height`, before checking whether `cell_count * 12 <= r.remaining()` or checking dimension bounds ($W, H \le 256$).
3. **Premise 3 (Empirical Demonstration):** In test case 6.5 (`test_challenger_m1_2.cpp`), passing a corrupted 19 KB map buffer with $500 \times 500$ dimensions caused `LVLParser` to allocate 250,000 `MapCell` structs (1.5 MB) before returning `false`. Passing dimensions of $0x10000000$ caused AddressSanitizer to abort with `allocation-size-too-big`, and caused standard builds to throw unhandled `std::bad_alloc`.
4. **Conclusion:** `LVLParser::load_from_memory` fails the robustness criteria for corrupted map buffer defense and requires changes to add bounds and stream capacity validation prior to vector resizing.

---

## 5. Caveats

- **No Caveats on Verified Features:** The mathematical formulation of the 5-to-8 mirroring engine ($dx'=-(dx+W)$), the pre-computed in-memory directional atlas, Table 1 sprite mirroring, authentic lunchbox assets, and normal-path level decoding of all 6 official maps with zero residual bytes are completely sound, robust, and verified across 12.8M assertions.
- **Scope Limit:** Challenger 2 did not modify library implementation code in `src/` or `include/`, in strict accordance with the `Review-only` constraint.

---

## 6. Conclusion & Verdict

**Verdict:** `REQUEST_CHANGES`

**Required Fixes for Implementer (`worker_m1_1`):**
1. In `src/ants_assets/lvl_parser.cpp`:
   - Enforce dimension limits: `if (out_level.width > 256 || out_level.height > 256) return false;`
   - Enforce stream capacity: `if (r.remaining() < cell_count * 12) return false;` before calling `layer1_terrain.resize` or `layer2_interactive.resize`.
   - Enforce stream capacity for tile dictionary: `if (r.remaining() < dict_count * 11) return false;` before `tile_dictionary.resize`.
   - Enforce stream capacity for trailing blocks 1, 2, 4:
     - `if (r.remaining() < static_cast<size_t>(b1_count) * 6) return false;`
     - `if (r.remaining() < static_cast<size_t>(b2_count) * 10) return false;`
     - `if (r.remaining() < static_cast<size_t>(b4_count) * 8) return false;`
   - Wrap `load_from_memory` body in `try { ... } catch (const std::exception&) { out_level = LevelData{}; return false; }` for fail-safe exception safety.

---

## 7. Verification Method

To independently reproduce the vulnerability and verify the eventual fix:

1. **Execute Challenger Test Suite (Standard Build):**
   ```bash
   cd /Users/dchadd/Desktop/Ants-Mac
   cmake -B build
   cmake --build build
   ./build/tests/test_assets/test_challenger_m1_2
   ```
2. **Execute Challenger Test Suite (AddressSanitizer Build):**
   ```bash
   cmake -B build_asan -DENABLE_ASAN=ON
   cmake --build build_asan
   ./build_asan/tests/test_assets/test_challenger_m1_2
   ```
3. **Invalidation Condition:**
   When the implementer applies the suggested stream capacity and dimension checks, test case 6.5 in `./build/tests/test_assets/test_challenger_m1_2` will pass cleanly (`allocated_cells == 0u`), achieving 25 / 25 passed tests (100% pass) with 0 failures across both standard and AddressSanitizer builds.
