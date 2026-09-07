# Handoff Report: M1 Asset Architecture & API Design (`ants-assets`)

**From:** M1 Explorer 1 (`explorer_m1_1`)  
**To:** M1 Implementers & Orchestrator (`orchestrator_1` / `a28dfa55-5a82-453d-a21b-99459a66b340`)  
**Target Milestone:** M1 (`ants-assets`)  
**Date:** 2026-09-06T22:42:00Z  

---

## 1. Observation

1. **File Footprint & Byte Layout:**
   - Container `Original-Ants/ants.chd` has a file size of 8,411,866 bytes (`0x00805B5A`).
   - Header inspection via `python3` command unpacked 7 little-endian `uint32` values:
     `v=9, ts=0x378d661c, t1=1052, t2=6835937, t3=7903773, t4=7903835, pal_sz=1024`.
   - Table 1 verified: exactly 2,794 paletted sprites parsed. Sprite 0 (`dclay48.bmp`) has `pitch = 48`, `width = 45`, `height = 47`, demonstrating row stride padding ($pitch > width$) with trailing bytes filled with $0x00$.
   - Table 2 verified: exactly 91 digital audio clips parsed. Format is unsigned 8-bit PCM. Exactly 3 clips are stereo (`channels = 2`: Sound 6 Ants logo, Sound 45, Sound 46); 88 clips are mono (`channels = 1`). Sample rates are either 11,025 Hz or 22,050 Hz.
   - Table 3 verified: 62 bytes containing event tags 3 (`HITGROUND`), 4 (`ATTACKHIT`), 5 (`HEAL`), and 10 (null terminator).
   - Table 4 verified: exactly 1,344 animation sequences parsed. String lengths `name_len` are byte-exact and not padded to 4 bytes. Exactly 365 subitems feature active sound triggers (`default_sp < 91`).
2. **Map Binary Consistency (`Maps/*.LVL`):**
   - Executed parsing against all 6 map files in `Original-Ants/Maps/`:
     - `GAUNTLET.LVL`: 60×60, 1329 tile dictionary entries, 32 spawns, 2 food pools, 20 waypoints, consumed 58,508 / 58,508 bytes (`rem = 0`).
     - `ISLANDS.LVL`: 60×60, 1329 tile dictionary entries, 40 spawns, 10 food pools, 22 waypoints, consumed 58,776 / 58,776 bytes (`rem = 0`).
     - `MEDIUM.LVL`: 60×60, 1324 tile dictionary entries, 30 spawns, 4 food pools, 15 waypoints, consumed 58,381 / 58,381 bytes (`rem = 0`).
     - `SMALL.LVL`: 40×40, 1325 tile dictionary entries, 18 spawns, 5 food pools, 2 waypoints, consumed 34,190 / 34,190 bytes (`rem = 0`).
     - `TINY.LVL`: 31×31, 669 tile dictionary entries, 12 spawns, 14 food pools, 0 waypoints, consumed 19,312 / 19,312 bytes (`rem = 0`).
     - `TREASURE.LVL`: 60×60, 1334 tile dictionary entries, 29 spawns, 18 food pools, 19 waypoints, consumed 58,853 / 58,853 bytes (`rem = 0`).
   - Every single map consumed 100% of its byte buffer with zero trailing leftover bytes.
3. **5-to-8 Directional Distribution:**
   - Table 4 animation direction code analysis of unit sequences (`ag`, `at`, `af`, `ab`, `ac`, `as`, `hg`):
     Base stored direction digits: `'7'` (North, 67 anims), `'8'` (North-East, 63 anims), `'9'` (East, 67 anims), `'2'` (South-East, 63 anims), `'3'` (South, 99 anims), plus `'5'` (1 anim: non-directional underground crawl `atcr501`).
   - Table 1 lunchbox sprites confirmed prefixes: `3lb` (18 sprites), `4lb` (15 sprites), `5lb` (15 sprites), `6lb` (15 sprites), `7lb` (15 sprites).
4. **Host Platform Toolchain:**
   - Apple Clang 21.0.0 (`clang++`) with full C++17 support verified.
   - CMake 4.3.2 verified.
   - SDL2 2.32.10 verified (`/opt/homebrew/opt/sdl2`).

---

## 2. Logic Chain

1. **Header Decoupling:**
   - From Observation 1 & 4, `ants_assets` can be implemented as a pure standard C++17 static library with zero external dependencies (no SDL2, no OS headers).
   - Therefore, public headers `include/ants_assets/mirroring.hpp`, `include/ants_assets/chd_parser.hpp`, `include/ants_assets/lvl_parser.hpp`, and `include/ants_assets/asset_archive.hpp` require only `<cstdint>`, `<vector>`, `<string>`, `<array>`, `<unordered_map>`, `<memory>`.
2. **Stride Padding Handling:**
   - From Observation 1, Sprite 0 has `width = 45`, `pitch = 48`. Row padding bytes ($x \ge width$) are $0x00$.
   - Storing $pitch \times height$ contiguous bytes in `Sprite::pixels` guarantees bit-exact matching with original archive bytes.
   - Accessor `get_pixel(x, y)` evaluates $y \times pitch + x$ for $0 \le x < width$, preventing out-of-bounds stride reads and ensuring padding bytes are never drawn.
   - `Sprite::to_rgba32()` repacks into a tightly-packed $width \times height \times 4$ byte buffer, perfectly suited for GPU texture uploading.
3. **Color Key 254 Transparency & Channel Ordering:**
   - Palette stores 256 entries $\times$ 4 bytes `[R, G, B, Flags]`.
   - Index 254 (`0xFE`, `RGB(255, 0, 255)`) is the DirectDraw magenta key and maps to `Alpha = 0`.
   - Index 0 (`RGB(119, 119, 127)`) is opaque slate with `Alpha = 255`.
   - Reading channels as `[R, G, B]` preserves team colors (Black 237..239, Blue 34..39, Red 178..181, Green 49..52).
4. **Audio Representation & WAV Synthesis:**
   - From Observation 1, audio clips are raw unsigned 8-bit PCM.
   - Prepending a 44-byte standard RIFF header (`RIFF`, `WAVE`, `fmt `, `data`) allows zero-copy consumption by SDL2 (`SDL_RWFromConstMem`) without disk I/O.
5. **Pre-computed 8-Directional Mirroring:**
   - From Observation 3, Western directions ($SW=5, W=6, NW=7$) are reflections of $SE=2, E=9, NE=8$.
   - Inversion math: horizontal offset $dx' = -(dx + W)$, collision box $[-box\_right, -box\_left]$, pixel column $x' = W - 1 - x$.
   - Pre-generating mirrored copies of all 2,794 sprites consumes only ~6.8 MB of RAM. This provides true $O(1)$ direct array lookup without runtime branching.
   - Pre-generating 193 mirrored directional animation sequences provides $O(1)$ animation retrieval with pre-computed $dx'$ and reflected bounding boxes.

---

## 3. Caveats

1. **Audio Clips Without Filenames:**
   - 15 out of 91 sound clips in Table 2 have `filename_len = 1` and `filename = "\0"`. They must be referenced by numeric Sound ID (0..90).
2. **Animation 1095 (`atcr501`):**
   - Uses direction digit `5` but represents an omnidirectional underground base dive, not an 8-way directional heading. It must not be mirrored.
3. **No External Modifications:**
   - In accordance with the Explorer persona, no source code was committed outside of the agent's working directory. The complete implementation blueprints and file designs are documented in `m1_architecture_plan.md`.

---

## 4. Conclusion

1. The architecture for Milestone 1 (`ants-assets`) is completely designed, mathematically validated against the raw binary assets, and documented in detail in `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_1/m1_architecture_plan.md`.
2. Public C++ API headers under `include/ants_assets/`:
   - `mirroring.hpp`: 5-to-8 directional math, direction mapping, lunchbox mapping.
   - `chd_parser.hpp`: Binary deserializers for header, palette, Table 1 sprites, Table 2 audio, Table 3 tags, Table 4 animations.
   - `lvl_parser.hpp`: Level parser for all 6 maps with Layer 1, Layer 2, and trailing blocks 1..4.
   - `asset_archive.hpp`: Master asset manager with $O(1)$ lookups and pre-computed 8-directional mirrored sprites.
3. Memory management and CMake configurations are fully formulated and ready for immediate implementation by M1 developers.

---

## 5. Verification Method

1. **Inspect Architecture Plan:**
   - View `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_1/m1_architecture_plan.md` to verify all class definitions, memory formulas, and CMake targets.
2. **Validate Binary Deserialization Against Original Assets:**
   - Run Python verification script on host:
     ```bash
     python3 -c '
     with open("Original-Ants/ants.chd", "rb") as f:
         assert len(f.read()) == 8411866
     print("ants.chd byte verification PASSED")
     '
     ```
3. **Verify Host Toolchain Compatibility:**
   - Verify compiler: `clang++ --version` (Clang 21.0.0).
   - Verify build tool: `cmake --version` (CMake 4.3.2).
4. **Invalidation Conditions:**
   - Any assertion failure where Table 1 sprite count $\ne 2,794$, Table 2 sound count $\ne 91$, Table 4 animation count $\ne 1,344$, or any of the 6 `.LVL` maps leaves remaining unparsed bytes ($\text{rem} > 0$).
