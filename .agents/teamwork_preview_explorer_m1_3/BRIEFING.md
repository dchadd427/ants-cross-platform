# BRIEFING — 2026-09-06T22:42:50Z

## Mission
Provide exact, verified C++ implementation logic for parsing all 6 maps in Maps/*.LVL (rem = 0) and the 5-to-8 directional sprite mirroring engine with O(1) runtime lookup, lunchbox mapping, and unit test specifications.

## 🔒 My Identity
- Archetype: Teamwork explorer
- Roles: Binary Asset Analysis, Map Format Specification, Sprite Mirroring Math & Engine, Unit Test Architecture
- Working directory: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_3
- Original parent: a28dfa55-5a82-453d-a21b-99459a66b340
- Milestone: Milestone 1 (ants-assets)

## 🔒 Key Constraints
- Read-only investigation — do NOT modify original project source code or assets
- Write only to my folder: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_3/
- Provide exact verified C++ structures, parsing logic, and verification routines
- Guarantee 0 remaining bytes across all 6 maps (TINY, SMALL, MEDIUM, ISLANDS, GAUNTLET, TREASURE)
- Full mathematical rigor for pixel and offset mirroring: p'(x,y) = p(W-1-x, y), dx' = -(dx + W), dy' = dy

## Current Parent
- Conversation ID: a28dfa55-5a82-453d-a21b-99459a66b340
- Updated: not yet

## Investigation State
- **Explored paths**:
  - `Original-Ants/Maps/*.LVL` (all 6 files analyzed and verified with C++ and Python)
  - `Original-Ants/ants.chd` (Tables 1 and 4, directional animations, lunchbox sprites)
  - `survey_assets.md` (Sections 2, 3)
  - `PROJECT.md` & `ORIGINAL_REQUEST.md`
- **Key findings**:
  - Tile dictionary size rule: strictly $(N + 1) \times 11$ bytes where $N = \text{tile\_type\_count}$
  - All 6 maps parse to exactly 0 remaining bytes (`rem = 0`) across header, dictionary, Layer 1, Layer 2, Blocks 1-4, and `f_last`
  - Coordinate convention across Blocks 1, 2, and 4 is `(y, x)` (row, col)
  - Block 4 contains 5 IEEE-754 64-bit double precision probability floats summing to 1.0 when `flag != 0`
  - Horizontal pixel reflection formula $p'(x, y) = p(W - 1 - x, y)$ with stride padding preservation
  - Frame offset formula $dx' = -(dx + W), dy' = dy$, collision box $box\_left' = -box\_right, box\_right' = -box\_left$
  - Complete lunchbox carrying suite mapping verified against `ants.chd` (7lb, 6lb, 5lb, 4lb, 3lb)
  - In-memory 8-direction pre-computation enables pure $O(1)$ constant time lookup
- **Unexplored areas**: None. All mission objectives 100% verified.

## Key Decisions Made
- Compiled and verified standalone C++ test programs `test_map_parser` and `test_mirroring`, proving binary correctness under Clang++ (C++17).
- Authored production-ready header and source specifications in `lvl_and_mirroring_plan.md`.

## Artifact Index
- `.agents/teamwork_preview_explorer_m1_3/BRIEFING.md` — persistent memory & briefing
- `.agents/teamwork_preview_explorer_m1_3/progress.md` — liveness heartbeat
- `.agents/teamwork_preview_explorer_m1_3/lvl_and_mirroring_plan.md` — comprehensive specification and C++ implementation plan
- `.agents/teamwork_preview_explorer_m1_3/handoff.md` — 5-component handoff report
- `.agents/teamwork_preview_explorer_m1_3/test_map_parser.cpp` — verified C++ map parser
- `.agents/teamwork_preview_explorer_m1_3/test_mirroring.cpp` — verified C++ mirroring engine
- `.agents/teamwork_preview_explorer_m1_3/test_chd_mirroring_real.cpp` — real asset verification against ants.chd
