# BRIEFING — 2026-09-06T22:49:00Z

## Mission
Implement Milestone 1 (`libants-assets`), the zero-dependency C++17 asset decoding library for Ants remake, and comprehensive programmatic verification suite.

## 🔒 My Identity
- Archetype: worker
- Roles: implementer, qa, specialist
- Working directory: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_worker_m1_1
- Original parent: a28dfa55-5a82-453d-a21b-99459a66b340
- Milestone: M1 (Assets)

## 🔒 Key Constraints
- Zero external runtime dependencies (standard C++17 only; <cstdint>, <vector>, <string>, <fstream>, etc.).
- Genuine implementations only: no hardcoding, no facades, no cheating.
- Pitch stride padding support for sprites (pitch vs width).
- Little-Endian RGBA palette, index 254 alpha=0, 4 team color ranges.
- Table 1 (2794 sprites), Table 2 (91 sounds with RIFF WAV reconstruction), Table 3 (4 tags), Table 4 (1344 animations, dx/dy, default_sp sound triggers).
- Maps/*.LVL: Layer 1, Layer 2 (0x7FFE sentinel), trailing blocks with rem=0.
- Mirroring: dx' = -(dx + W), p'(x,y) = p(W-1-x,y), 5-to-8 directional mirroring, lunchbox suites, O(1) in-memory atlas.
- Clean build on macOS with Apple Clang C++17, 100% test passes, 0 leaks.

## Current Parent
- Conversation ID: a28dfa55-5a82-453d-a21b-99459a66b340
- Updated: 2026-09-06T22:49:00Z

## Task Summary
- **What to build**: libants-assets (chd_parser, lvl_parser, mirroring, asset_archive) and test_assets test suite.
- **Success criteria**: Clean compilation, 100% test pass on ants.chd and all 6 LVL files, accurate RGBA/WAV/mirroring decoding.
- **Interface contracts**: include/ants_assets/*.hpp
- **Code layout**: Root CMakeLists.txt, src/ants_assets/, include/ants_assets/, tests/test_assets/

## Change Tracker
- **Files modified**:
  - `CMakeLists.txt`: Root CMake configuration for C++17, options, warning flags, ASan support
  - `include/ants_assets/mirroring.hpp`: Direction enums, DirectionMapping, DIRECTION_MAP, math helpers, lunchbox prefixes, ostream operator
  - `include/ants_assets/chd_parser.hpp`: Header constants, ColorRGBA, CHDHeader, Sprite, WaveFormat, SoundClip, EventTag, AnimationSequence, CHDParser
  - `include/ants_assets/lvl_parser.hpp`: LVL constants, MapCell, AnthillSpawn, FoodSchedule, Waypoint, LevelData, LVLParser
  - `include/ants_assets/asset_archive.hpp`: Master AssetArchive class declaration with O(1) direct and mapped lookups
  - `src/ants_assets/CMakeLists.txt`: Static library `ants_assets` target definition
  - `src/ants_assets/mirroring.cpp`: Implementation of angle, vector, string, mirror_pixel_buffer, get_lunchbox_prefix
  - `src/ants_assets/chd_parser.cpp`: Bounds-checked binary parser for ants.chd header, palette, tables 1-4, RGBA conversion, RIFF WAV builder
  - `src/ants_assets/lvl_parser.cpp`: Bounds-checked parser for all 6 Maps/*.LVL files with rem=0 validation
  - `src/ants_assets/asset_archive.cpp`: AssetArchive implementation with hash indices, pre-mirrored sprite atlas and directional animations
  - `tests/test_assets/CMakeLists.txt`: Standalone test executable target for test_assets
  - `tests/test_assets/test_assets.cpp`: 8 comprehensive programmatic test suites verifying 35,840 assertions
- **Build status**: 100% PASS (Clean build, zero warnings with -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
- **Pending issues**: None

## Quality Status
- **Build/test result**: PASS (100% pass across 25 test cases and 35,840 assertions, zero ASan/UBSan errors, zero memory leaks)
- **Lint status**: Clean (0 warnings)
- **Tests added/modified**: 8 test suites in tests/test_assets/test_assets.cpp

## Loaded Skills
- None (Standard C++17 systems engineering)

## Key Decisions Made
- Implemented robust bounds-checked binary reading using `BinaryReader` helper classes to eliminate undefined behavior and unaligned access faults.
- Precomputed all 2,794 mirrored sprites and directional animations at load time in `AssetArchive` for true O(1) runtime lookups without frame-by-frame memory allocations or branching.
- Preserved raw pitch stride padding in Table 1 Sprite pixel buffers while providing tightly packed RGBA output in `to_rgba32()` and `to_rgba()`.

## Artifact Index
- `DISPATCH.md` — Assignment instructions
- `BRIEFING.md` — Persistent working memory
- `progress.md` — Liveness heartbeat and milestone progress
- `handoff.md` — Formal 5-component handoff report
