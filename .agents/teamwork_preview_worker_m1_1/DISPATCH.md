# Task Assignment: Milestone 1 Worker (`libants-assets`)

## Scope & Target
Implement Milestone 1 (`ants-assets`): The Native Binary Asset Decoder for Microsoft Ants remake.

## Inputs
- Verbatim User Request: `/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md`
- Master Project Specification: `/Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md`
- M1 Architecture Blueprint: `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_1/m1_architecture_plan.md`
- CHD Parsing Specification: `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_2/chd_parsing_plan.md`
- Map Parsing & Mirroring Specification: `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_3/lvl_and_mirroring_plan.md`
- Asset Survey: `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_spec_miner_survey_1/survey_assets.md`
- Reference Assets: `/Users/dchadd/Desktop/Ants-Mac/Original-Ants/ants.chd`, `/Users/dchadd/Desktop/Ants-Mac/Original-Ants/Maps/*.LVL`

## File Ownership
You exclusively own and will create:
- Root `CMakeLists.txt`
- `include/ants_assets/mirroring.hpp`
- `include/ants_assets/chd_parser.hpp`
- `include/ants_assets/lvl_parser.hpp`
- `include/ants_assets/asset_archive.hpp`
- `src/ants_assets/CMakeLists.txt`
- `src/ants_assets/mirroring.cpp`
- `src/ants_assets/chd_parser.cpp`
- `src/ants_assets/lvl_parser.cpp`
- `src/ants_assets/asset_archive.cpp`
- `tests/test_assets/CMakeLists.txt`
- `tests/test_assets/test_assets.cpp`

## Acceptance Criteria to Satisfy
- Programmatic test suite parses all 2,794 sprites, 91 sound effects, and 6 map levels from `ants.chd` and `Maps/*.LVL` without crash, truncation, or memory leaks.
- Visual/pixel verification of palette accuracy (color 254 transparent, RGB Little-Endian) and 8-way directional orientation ($dx' = -(dx+W)$, $p'(x, y) = p(W-1-x, y)$).
- Standalone test executable `test_assets` builds cleanly with Apple Clang C++17 and passes 100% of test suites.

## Mandatory Integrity Warning
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_preview_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

## Output
1. Implement all code files listed under File Ownership.
2. Build and run tests using `cmake -B build` and `cmake --build build`.
3. Verify test execution output and ensure zero failures and zero memory errors.
42: 4. Deliver your handoff report to `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_worker_m1_1/handoff.md` and send a message back to your parent.
43: 

## 2026-09-06T22:43:19Z
You are the M1 Worker for the Microsoft Ants remake project.
Your identity: worker_m1_1
Your working directory: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_worker_m1_1
Parent conversation ID: a28dfa55-5a82-453d-a21b-99459a66b340

MANDATORY: Read the original user request before starting work:
/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md

Also read your task dispatch:
/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_worker_m1_1/DISPATCH.md

Master Project Document:
/Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md

Explorer blueprints and specifications:
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_1/m1_architecture_plan.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_2/chd_parsing_plan.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_3/lvl_and_mirroring_plan.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_spec_miner_survey_1/survey_assets.md

Reference files:
- /Users/dchadd/Desktop/Ants-Mac/Original-Ants/ants.chd
- /Users/dchadd/Desktop/Ants-Mac/Original-Ants/Maps/*.LVL

File Ownership:
You exclusively own and will implement:
- /Users/dchadd/Desktop/Ants-Mac/CMakeLists.txt (root CMakeLists.txt)
- /Users/dchadd/Desktop/Ants-Mac/include/ants_assets/mirroring.hpp
- /Users/dchadd/Desktop/Ants-Mac/include/ants_assets/chd_parser.hpp
- /Users/dchadd/Desktop/Ants-Mac/include/ants_assets/lvl_parser.hpp
- /Users/dchadd/Desktop/Ants-Mac/include/ants_assets/asset_archive.hpp
- /Users/dchadd/Desktop/Ants-Mac/src/ants_assets/CMakeLists.txt
- /Users/dchadd/Desktop/Ants-Mac/src/ants_assets/mirroring.cpp
- /Users/dchadd/Desktop/Ants-Mac/src/ants_assets/chd_parser.cpp
- /Users/dchadd/Desktop/Ants-Mac/src/ants_assets/lvl_parser.cpp
- /Users/dchadd/Desktop/Ants-Mac/src/ants_assets/asset_archive.cpp
- /Users/dchadd/Desktop/Ants-Mac/tests/test_assets/CMakeLists.txt
- /Users/dchadd/Desktop/Ants-Mac/tests/test_assets/test_assets.cpp

MANDATORY INTEGRITY WARNING:
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_preview_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

Mission:
1. Implement the complete, genuine, zero-dependency C++17 library libants-assets according to the architectural blueprints and specifications.
2. Implement the comprehensive programmatic test suite in tests/test_assets/test_assets.cpp verifying:
   - Header verification (version 9, palette=1024, offsets).
   - Palette extraction (Little-Endian RGBA, color 254 alpha=0, 4 team color ranges).
   - Table 1: All 2,794 raw paletted sprites parsed with pitch stride padding handling and RGBA conversion.
   - Table 2: All 91 PCM audio clips parsed with standard 44-byte RIFF WAV reconstruction and Sound IDs 0..90 mapping.
   - Table 3: All 4 event tags.
   - Table 4: All 1,344 animations, frame offsets dx, dy, and 365 default_sp sound triggers.
   - Maps: All 6 maps in Maps/*.LVL parsed with Layer 1, Layer 2 (sentinel 0x7FFE), and all 4 trailing blocks with exactly 0 remaining bytes (rem=0).
   - 5-to-8 Directional Mirroring: Horizontal reflection math (dx'=-(dx+W), p'(x,y)=p(W-1-x,y)), lunchbox suite, and pre-computed O(1) in-memory atlas.
3. Build using cmake and verify test_assets runs cleanly with 100% passes and 0 leaks.
4. Deliver your handoff report to:
   /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_worker_m1_1/handoff.md
   and send a completion message to your parent.

