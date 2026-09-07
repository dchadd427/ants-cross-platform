# Handoff Report: Milestone 1 Iteration 2 Challenger 1 (`libants-assets` Stress Testing)

**Agent:** `challenger_m1_it2_1` (Critic, Specialist)  
**Parent Conversation ID:** `a28dfa55-5a82-453d-a21b-99459a66b340`  
**Working Directory:** `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m1_it2_1`  
**Date:** 2026-09-06T23:12:00Z  
**Verdict:** **APPROVE**  

---

## 1. Observation

1. **Defense Mechanisms in `src/ants_assets/lvl_parser.cpp` (lines 167–178):**
   ```cpp
   // Enforce upper sanity limit on map dimensions (Ants max is 60x60, allow up to 256x256)
   if (out_level.width == 0 || out_level.height == 0 ||
       out_level.width > 256 || out_level.height > 256) {
       return false;
   }

   size_t cell_count = static_cast<size_t>(out_level.width) * out_level.height;

   // Stream capacity check BEFORE allocating terrain layers (Layer 1 + Layer 2 = 12 bytes/cell)
   if (r.remaining() < cell_count * 12) {
       return false;
   }
   ```
   Furthermore, all trailing blocks (Block 1 spawns: `r.remaining() < b1_count * 6`, Block 2 food pools: `r.remaining() < b2_count * 10`, item variants: `r.remaining() < item_count * 4`, Block 4 waypoints: `r.remaining() < b4_count * 8`) enforce stream capacity validation prior to vector resizes, and line 281 strictly enforces 0 unparsed residual bytes (`r.pos() == size`).

2. **Defense Mechanisms in `src/ants_assets/chd_parser.cpp` (lines 184–211, 277–283, 344–348, 459–463, 479–483):**
   - Header validation strictly checks `size >= 28`, `version >= CHD_EXPECTED_VERSION`, `palette_bytes == 1024`, `table1_offset == 28 + palette_bytes` (1052), strict monotonicity `table1_offset < table2_offset < table3_offset < table4_offset`, and `table4_offset < size`.
   - In Table 1: `pixel_bytes > r.remaining()` check is performed before `sp.pixels.resize(pixel_bytes)`.
   - In Table 2: `pcm_len > r.remaining()` check is performed before `snd.pcm_data.resize(pcm_len)`.
   - In Table 4: `subitem_count * 40 > r.remaining()` and `frame_count * 12 > r.remaining()` checks are performed before vector resizes.

3. **Master Test Runner Execution (`./run_tests.sh --all`):**
   ```
   ======================================================================
   >>> 1. RUNNING ASSET DECODER SUITES (libants-assets)...               
   ======================================================================
   TEST SUMMARY: 26 Cases, 69809 Assertions, 0 Failures (100% PASS)
   >>> 2. RUNNING E2E OPAQUE-BOX VERIFICATION SUITES...                  
   TOTAL: 506 / 506 passed in 2.44 ms
   RESULT: ALL EXECUTED TEST SUITES PASSED CLEANLY! (Exit code 0)
   ```

4. **AddressSanitizer Test Runner Execution (`./run_tests.sh --asan`):**
   ```
   ======================================================================
   >>> 1. RUNNING ASSET DECODER SUITES (libants-assets)...               
   ======================================================================
   TEST SUMMARY: 26 Cases, 69809 Assertions, 0 Failures (100% PASS)
   >>> 2. RUNNING E2E OPAQUE-BOX VERIFICATION SUITES...                  
   TOTAL: 506 / 506 passed in 2.18 ms
   RESULT: ALL EXECUTED TEST SUITES PASSED CLEANLY! (Exit code 0)
   ```

5. **Challenger 1 Iteration 1 Adversarial Suite under ASan (`./build_asan/tests/test_assets/test_challenger_m1_1`):**
   ```
   ADVERSARIAL TEST SUMMARY: 24 Test Cases, 13,204,992 Assertions, 0 Failures (100% PASS)
   AddressSanitizer: 0 heap buffer overflows, 0 memory leaks, exit code 0.
   ```

6. **Challenger 2 Iteration 1 Adversarial Suite under ASan (`./build_asan/tests/test_assets/test_challenger_m1_2`):**
   ```
   CHALLENGE TEST SUMMARY: 25 Challenge Cases, 12,889,626 Assertions, 0 Failures (100% PASS)
   AddressSanitizer: 0 heap buffer overflows, 0 memory leaks, exit code 0.
   ```

7. **Dedicated Challenger 1 Iteration 2 Empirical Stress Test Suite (`tests/test_assets/test_challenger_m1_it2.cpp`):**
   Authored and executed under AddressSanitizer and UndefinedBehaviorSanitizer:
   - **Section 1: Malformed and Oversized Map Dimensions to LVLParser:**
     - Tested `width = 0x7FFFFFFF, height = 1` -> Clean `false` return, `layer1_cells().empty() == true`, no crash, no `std::bad_alloc`.
     - Tested `width = 1, height = 0x7FFFFFFF` -> Clean `false` return, `layer1_cells().empty() == true`.
     - Tested `width = 0xFFFFFFFF, height = 0xFFFFFFFF` (arithmetic overflow test) -> Clean `false` return.
     - Tested `width = 1000, height = 1000` -> Clean `false` return.
     - Tested `width = 257, height = 257` (boundary just above maximum allowable 256) -> Clean `false` return.
     - Tested `width = 256, height = 256` with buffer smaller than `256 * 256 * 12` bytes -> Clean `false` return before vector resize.
     - Tested `width = 0` and `height = 0` -> Clean `false` return.
     - Tested 9 structural truncation cutoffs -> All returned `false`.
     - Tested corrupted trailing block counts (claiming 50,000 anthills) -> Clean `false` return.
   - **Section 2: Corrupted CHD Headers and Tables to CHDParser:**
     - Tested `size = 20` specifically -> Clean `false` return in `parse_header` and `AssetArchive::load_from_memory`.
     - Tested truncated sizes (0, 1, 2, 10, 15, 20, 24, 27 bytes) -> Clean `false` return.
     - Tested invalid version numbers (0, 1, 5, 8) -> Clean `false` return.
     - Tested invalid palette sizes (0, 512, 1023, 1025, 2048, 0xFFFFFFFF) -> Clean `false` return.
     - Tested invalid `table1_offset` (0, 28, 1000, 1051, 1053, 0xFFFFFFFF) -> Clean `false` return.
     - Tested non-monotonic table offsets (`table2 <= table1`, `table3 <= table2`, `table4 <= table3`, `table4 >= size`) -> Clean `false` return.
     - Tested Table 1 sprite input bounding (claiming 10,000 x 10,000 pixel dimensions on small buffer) -> Clean `false` return before `sp.pixels.resize()`, `sp.pixels.empty() == true`.
     - Tested Table 2 sound input bounding (claiming 1,000,000 PCM bytes on small buffer) -> Clean `false` return before `snd.pcm_data.resize()`, `snd.pcm_data.empty() == true`.
   - **Section 3: Interface Contract & Dual-Syntax Verification:**
     - Verified `lvl.width() == 31`, `lvl.height() == 31`, `lvl.layer1_terrain(0, 0)`, `lvl.layer2_item(0, 0)`, `lvl.anthill_spawns().size() == 12`, `lvl.food_schedules().size() == 14`.
     - Verified dual-syntax field access `lvl.width == 31`, `lvl.height == 31`, `lvl.layer1_terrain.size() == 961`.
   - **Section 4: Multi-Map Adversarial Mutation & Repeated Allocation Fuzz under ASan:**
     - Swept bad dimensions (`257`, `500`, `1000`, `65536`, `0x7FFFFFFF`, `0x80000000`, `0xFFFFFFFF`) across all 6 authentic maps (`TINY.LVL`, `SMALL.LVL`, `MEDIUM.LVL`, `GAUNTLET.LVL`, `ISLANDS.LVL`, `TREASURE.LVL`) -> All 84 permutations returned `false` cleanly.
     - Tested 10 progressive truncation points across all 6 authentic maps (60 permutations) -> All returned `false` cleanly.
     - Executed 500 repeated consecutive parses of malformed/corrupted memory buffers under AddressSanitizer -> Zero memory leaks, zero sanitizer violations.
   - **Results:** 316 / 316 assertions evaluated, 0 failures, exit code 0.

8. **Full CTest Suite Verification:**
   - In `build/`: 5 / 5 tests passed (`test_assets`, `test_challenger_m1_1`, `test_challenger_m1_2`, `test_challenger_m1_it2`, `test_challenger_m1_it2_2`).
   - In `build_asan/`: 5 / 5 tests passed under AddressSanitizer and UndefinedBehaviorSanitizer.

---

## 2. Logic Chain

1. **Input-Bounded Allocation Defense Validation (CWE-789):**
   - *Premise:* An adversarial level file with extreme width/height values (e.g. `0x7FFFFFFF` or `1000`) or corrupted trailing block counts could cause unbounded memory allocation leading to `std::bad_alloc`, denial of service, or heap corruption.
   - *Observation:* In `src/ants_assets/lvl_parser.cpp:168`, dimensions exceeding 256 are immediately rejected. In `src/ants_assets/lvl_parser.cpp:176`, `r.remaining() < cell_count * 12` ensures that even valid dimensions (e.g. 256x256) will not allocate memory unless the input stream actually contains 786,432 bytes. In `src/ants_assets/chd_parser.cpp:278`, `pixel_bytes > r.remaining()` ensures sprites cannot allocate more bytes than remain in the buffer.
   - *Empirical Proof:* Test cases in `test_challenger_m1_it2.cpp` Sections 1, 2, and 4 fed these exact oversized inputs (`0x7FFFFFFF`, `1000`, `257`, `256` truncated, `0xFFFFFFFF`, `0x80000000`) across all 6 maps. All 144 adversarial permutations returned `false` cleanly with `layer1_cells().empty() == true`, zero crashes, zero `std::bad_alloc` exceptions, and zero ASan aborts.

2. **Corrupted CHD Header and Table Offsets Rejection:**
   - *Premise:* Malformed headers (e.g. truncated size = 20, version < 9, non-monotonic table offsets, or inverted pointers) could trigger out-of-bounds reads or invalid pointer arithmetic.
   - *Observation:* In `src/ants_assets/chd_parser.cpp:185–211`, `CHDParser::parse_header` enforces strict pre-condition bounds checking before attempting to parse any archive tables.
   - *Empirical Proof:* In `test_challenger_m1_it2.cpp` Section 2, buffers with `size = 20`, truncated sizes (0..27), version < 9, invalid palette byte counts, invalid Table 1 offsets, non-monotonic offsets, and offsets exceeding buffer sizes were tested. All returned `false` cleanly without exceptions or memory violations.

3. **Memory Leak and Sanitizer Resilience:**
   - *Premise:* Error handling paths upon rejection of malformed inputs might leak memory allocated during partial parsing.
   - *Observation:* `LevelData` and `AssetArchive` use RAII containers (`std::vector`, `std::string`) and wrap error exits in `try / catch` resetting containers.
   - *Empirical Proof:* In Section 4, 500 consecutive cycles of feeding garbage and malformed inputs to `LVLParser`, `CHDParser`, and `AssetArchive` were run under AddressSanitizer. The leak sanitizer reported 0 bytes leaked across all allocations.

4. **Interface Contract and Backward Compatibility Invariance:**
   - *Premise:* Adding required method accessors (`width()`, `height()`, `layer1_terrain(x, y)`, `layer2_item(x, y)`, `anthill_spawns()`, `food_schedules()`) could break existing test code that relies on direct member field access (`lvl.width`, `lvl.layer1_terrain.size()`).
   - *Observation:* `LevelData` implements dual-syntax proxy properties supporting both functor calls and implicit conversion to underlying values and container references.
   - *Empirical Proof:* Section 3 validated both functor calls and direct member access on authentic map data, and all existing test suites (`test_assets`, `test_challenger_m1_1`, `test_challenger_m1_2`) compiled and passed without a single warning or error.

---

## 3. Caveats

- **No Caveats:** Every defense specified in the mission, dispatch, and prompt was empirically verified with targeted test code running under AddressSanitizer and UndefinedBehaviorSanitizer on Apple Clang. All tests passed with 100% success.

---

## 4. Conclusion

**Verdict: APPROVE**

The input-bounded allocation defenses and malformed header rejections in `libants-assets` are complete, robust, and empirically sound:
1. Oversized dimensions (`width = 0x7FFFFFFF`, `width = 1000`, `width = 257`, `width = 0xFFFFFFFF`) and truncated buffers are rejected cleanly with `false` and zero heap allocation.
2. Malformed CHD headers (`size = 20`, truncated table offsets, non-monotonic offsets) are rejected cleanly with `false`.
3. All 26 test cases in `test_assets`, all 24 cases in `test_challenger_m1_1`, all 25 cases in `test_challenger_m1_2`, all 316 assertions in `test_challenger_m1_it2`, and all 506 cases in `e2e_runner` pass cleanly under AddressSanitizer with zero memory leaks and zero sanitizer violations.
4. Repository layout and cleanliness directives are fully preserved.

---

## 5. Verification Method

To independently reproduce the empirical findings:

1. **Run Master Test Runner (Standard Build):**
   ```bash
   ./run_tests.sh --all
   ```
   *Expected:* 26 asset test cases (69,809 assertions) and 506 E2E test cases pass cleanly with exit code 0.

2. **Run Master Test Runner under AddressSanitizer:**
   ```bash
   ./run_tests.sh --asan
   ```
   *Expected:* All test suites pass cleanly under AddressSanitizer with exit code 0 and 0 memory leaks.

3. **Run CTest Suite (All 5 Target Executables):**
   ```bash
   ctest --test-dir build --output-on-failure
   ctest --test-dir build_asan --output-on-failure
   ```
   *Expected:* 5/5 tests pass in both `build` and `build_asan` (`test_assets`, `test_challenger_m1_1`, `test_challenger_m1_2`, `test_challenger_m1_it2`, `test_challenger_m1_it2_2`).

4. **Direct Execution of Dedicated Empirical Stress Harness:**
   ```bash
   ./build_asan/tests/test_assets/test_challenger_m1_it2
   ```
   *Expected output:*
   ```
   =======================================================
    ALL EMPIRICAL STRESS TESTS PASSED SUCCESSFULLY!
    Total Assertions Evaluated: 316/316
   =======================================================
   ```
