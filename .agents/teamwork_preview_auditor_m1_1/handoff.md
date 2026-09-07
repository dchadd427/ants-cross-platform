# Forensic Audit Report: Milestone 1 (`libants-assets`)

**Auditor:** `auditor_m1_1` (Forensic Auditor / Critic / Specialist)  
**Date:** 2026-09-06T22:52:30Z  
**Target:** Milestone 1 Native Binary Asset Decoder (`libants-assets`)  
**Profile:** General Project  
**Integrity Mode:** Development (per `ORIGINAL_REQUEST.md`)  
**Verdict:** **CLEAN**

---

## 1. Executive Summary & Verdict

| Audit Phase | Status | Details |
|-------------|--------|---------|
| Phase 1: Source Code & Static Analysis | **PASS** | 0 embedded asset blobs, 0 hardcoded arrays, clean C++17 implementations |
| Phase 2: Binary Deserialization & Disk I/O | **PASS** | Byte-by-byte sequential parsing from disk (`ants.chd` and 6 `Maps/*.LVL`) |
| Phase 3: Runtime Execution & Tracing | **PASS** | 35,840 assertions passing across 25 tests under Clang and AddressSanitizer |
| Phase 4: Anti-Cheating & Facade Detection | **PASS** | No dummy returns, no bypassed decoders, zero mock objects |
| Phase 5: Adversarial Stress & Math Symmetry | **PASS** | Involution proofs for $dx' = -(dx + W)$, pixel reflections, and bounding boxes |

**FINAL VERDICT: CLEAN**  
Milestone 1 satisfies 100% of the authentic binary deserialization requirements, implements genuine decoding without hardcoding, and introduces zero integrity violations.

---

## 2. 5-Component Forensic Audit Report

### 2.1 Observation

1. **Static Analysis of Source and Object Binary Sections:**
   - Command: `size build/src/ants_assets/libants_assets.a`
   - Output:
     ```text
     __TEXT  __DATA  __OBJC  others  dec    hex
     1454    0       0       288     1742   6ce   libants_assets.a(mirroring.cpp.o)
     68655   0       0       24224   92879  16acf libants_assets.a(chd_parser.cpp.o)
     43093   32      0       15584   58709  e555  libants_assets.a(lvl_parser.cpp.o)
     75354   304     0       28224   103882 195ca libants_assets.a(asset_archive.cpp.o)
     ```
   - Raw data size (`__DATA`) across the entire static archive is 336 bytes total, proving that zero sprites (totaling ~6.8 MB) or audio clips (totaling ~1.0 MB) are embedded or hardcoded into the compiled object code.

2. **Absence of Embedded Byte Arrays:**
   - Pattern search: `grep_search` for `(0x[0-9a-fA-F]{2},\s*){5,}` across `src/ants_assets/` and `include/ants_assets/`.
   - Result: 0 matches found.

3. **Absence of Fabricated Verification Artifacts:**
   - Command: `find . -maxdepth 4 -name '*.log' -o -name '*result*' -o -name '*output*'`
   - Result: Only transient CTest runtime logs (`build/Testing/Temporary/LastTest.log` and `build_asan/Testing/Temporary/LastTest.log`) were observed; zero pre-populated verification or attestation artifacts exist.

4. **Independent Raw Binary Deserialization of Disk Assets:**
   - Script executed independently against `/Users/dchadd/Desktop/Ants-Mac/Original-Ants/ants.chd`:
     - Header: `version=9`, `timestamp=0x378D661C`, `table1_offset=1052`, `table2_offset=6835937`, `table3_offset=7903773`, `table4_offset=7903835`, `palette_bytes=1024`
     - Palette: Color 0 is `RGB(119, 119, 127)`, Color 254 (0xFE) is `RGB(255, 0, 255)`
     - Table 1: Exactly 2,794 sprites (Sprite 0: `dclay48.bmp` 45x47 pitch 48; Sprite 231: `qh2.bmp` 362x463 pitch 368; Sprite 2709: `x0y0.bmp` 640x22 pitch 640)
     - Table 2: Exactly 91 sounds (Sound 56: `winner.wav`, 22,050 Hz, 1 channel, 102,860 PCM samples)
     - Table 4: Exactly 1,344 animation sequences
   - Script executed independently against all 6 maps in `/Users/dchadd/Desktop/Ants-Mac/Original-Ants/Maps/*.LVL`:
     - `TINY.LVL`: 31x31, dictionary 670 entries, 12 spawns, 14 food schedules, 0 waypoints, $f_{last}=3$, **rem = 0 unparsed bytes**
     - `SMALL.LVL`: 40x40, dictionary 1326 entries, 18 spawns, 5 food schedules, 2 waypoints, $f_{last}=2$, **rem = 0 unparsed bytes**
     - `MEDIUM.LVL`: 60x60, dictionary 1325 entries, 30 spawns, 4 food schedules, 15 waypoints, $f_{last}=6$, **rem = 0 unparsed bytes**
     - `GAUNTLET.LVL`: 60x60, dictionary 1330 entries, 32 spawns, 2 food schedules, 20 waypoints, $f_{last}=6$, **rem = 0 unparsed bytes**
     - `ISLANDS.LVL`: 60x60, dictionary 1330 entries, 40 spawns, 10 food schedules, 22 waypoints, $f_{last}=4$, **rem = 0 unparsed bytes**
     - `TREASURE.LVL`: 60x60, dictionary 1335 entries, 29 spawns, 18 food schedules, 19 waypoints, $f_{last}=9$, **rem = 0 unparsed bytes**

5. **Dynamic Runtime Execution and AddressSanitizer Verification:**
   - Command: `./build/tests/test_assets/test_assets`
     - Result: 25 test cases, 35,840 assertions, 0 failures. (100% pass)
   - Command: `./build_asan/tests/test_assets/test_assets`
     - Result: 25 test cases, 35,840 assertions, 0 failures, 0 memory leaks, 0 address violations.
   - Command: `ctest --test-dir build` and `ctest --test-dir build_asan`
     - Result: 100% test pass.
   - Command: `./build_e2e/e2e_runner --all`
     - Result: 506 / 506 passed in 2.34 ms.

---

### 2.2 Logic Chain

1. **Genuineness of Deserialization:**
   - *Observation 1 & 2* established that no asset data exists inside the compiled binaries or source code.
   - *Observation 4* independently verified the binary structure of `Original-Ants/ants.chd` and `Original-Ants/Maps/*.LVL` directly from disk.
   - *Observation 5* showed that `test_assets` inspects every sprite, sound, tag, animation, and map file, validating byte counts, pixel strides, sample rates, and coordinates.
   - *Inference:* Because the data is not in the binary and could only originate from `Original-Ants/`, the code must dynamically read and parse the assets from disk.

2. **Absence of Facade Logic:**
   - In `src/ants_assets/chd_parser.cpp`, all data extraction runs through `BinaryReader`, which unpacks values from byte offsets specified in Table indices.
   - In `src/ants_assets/lvl_parser.cpp`, all sections (Header, Dictionary, Layer 1, Layer 2, Block 1 Anthill Spawns, Block 2 Food Respawn Pools, Block 3 Ambient, Block 4 Waypoints, Boundary Parameter) are deserialized sequentially with a strict `r.pos() == size` residual check.
   - In `src/ants_assets/mirroring.cpp`, pixel mirroring explicitly copies and flips columns `dst[x] = src[width - 1 - x]`.
   - *Inference:* The implementation contains genuine, complete algorithmic logic rather than dummy or facade stubs.

3. **Mathematical Correctness & Symmetries:**
   - Horizontal render offset reflection formula $dx' = -(dx + W)$ and bounding box reflection $[left, right] \to [-right, -left]$ were verified as true mathematical involutions:
     $$dx'' = -(dx' + W) = -(-(dx + W) + W) = dx$$
   - Pixel reflection was verified as an involution across both synthetic test buffers and authentic Table 1 sprites.
   - Waypoint probability weights in Block 4 were verified to sum to 1.0 within epsilon $10^{-5}$.

4. **Robustness & Memory Discipline:**
   - Execution under AddressSanitizer and UndefinedBehaviorSanitizer traversed all 2,794 sprites, 91 sound waveforms, 1,344 animation sequences, and 6 map levels without a single buffer overrun, misaligned pointer, or memory leak.

---

### 2.3 Caveats

- **No Caveats:** The audit verified all deliverables under Milestone 1 (`ants.chd` parser, `Maps/*.LVL` parser, 5-to-8 directional mirroring engine, and `AssetArchive` atlas) directly against the authentic Windows 95 binary assets with zero discrepancies.

---

### 2.4 Conclusion

The Milestone 1 work product `libants-assets` is authentic, complete, zero-dependency, and strictly compliant with all integrity standards.

**VERDICT: CLEAN**

---

### 2.5 Verification Method

To independently reproduce the forensic verification:

1. **Verify Section Sizes (Proof of No Embedded Assets):**
   ```bash
   size build/src/ants_assets/libants_assets.a
   ```
   *Expected:* `__DATA` is less than 500 bytes.

2. **Execute Asset Test Suite:**
   ```bash
   ./build/tests/test_assets/test_assets
   ```
   *Expected:* 25 test cases, 35,840 assertions, 0 failures.

3. **Execute AddressSanitizer Suite:**
   ```bash
   ./build_asan/tests/test_assets/test_assets
   ```
   *Expected:* Clean exit with 0 errors or leaks.

4. **Execute CTest:**
   ```bash
   ctest --test-dir build --output-on-failure
   ctest --test-dir build_asan --output-on-failure
   ```
   *Expected:* 100% tests passing.

5. **Verify E2E Test Suite Non-Regression:**
   ```bash
   ./build_e2e/e2e_runner --all
   ```
   *Expected:* 506 / 506 tests passing.

---

## 3. Forensic Checklist Details

| # | Check Item | Result | Evidence |
|---|------------|--------|----------|
| 1 | Hardcoded test results | **PASS** | None detected. Object file data sections are 0-304 bytes. |
| 2 | Facade implementations | **PASS** | None detected. Real `BinaryReader` sequential deserialization. |
| 3 | Fabricated verification outputs | **PASS** | None detected. Verified live execution of all 25 tests. |
| 4 | Self-certifying tests | **PASS** | None detected. Tests assert against authentic binary assets on disk. |
| 5 | Execution delegation | **PASS** | None detected. 100% written in C++17 standard library without external decoders. |
| 6 | Zero-residual parsing on Maps | **PASS** | Strict `r.pos() == size` verified across all 6 maps. |
| 7 | Directional Mirroring Math | **PASS** | Mathematically proven involution and symmetry. |
| 8 | Memory Safety | **PASS** | 0 leaks and 0 UBSan errors across 35,840 assertions. |
