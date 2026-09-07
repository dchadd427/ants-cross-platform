# BRIEFING — 2026-09-06T22:39:01Z

## Mission
Provide exact, verified C++ implementation logic, data structures, and unit tests for decoding ants.chd (Tables 1-4, palette, audio, animations).

## 🔒 My Identity
- Archetype: explorer
- Roles: explorer_m1_2 (ants.chd Parsing Engine)
- Working directory: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_2
- Original parent: a28dfa55-5a82-453d-a21b-99459a66b340
- Milestone: M1 (ants-assets)

## 🔒 Key Constraints
- Read-only investigation — do NOT implement source code files in include/ or src/ directly (propose concrete C++ implementations in chd_parsing_plan.md and handoff.md)
- .agents/ holds only agent metadata
- ASan-clean, bounds-checked, robust binary parsing
- Exact byte offsets and table structures for ants.chd

## Current Parent
- Conversation ID: a28dfa55-5a82-453d-a21b-99459a66b340
- Updated: not yet

## Investigation State
- **Explored paths**: ORIGINAL_REQUEST.md, PROJECT.md, survey_assets.md, GAME_REVERSE_ENGINEERING.md, Original-Ants/ants.chd, Original-Ants/Ants.exe
- **Key findings**: Complete empirical validation of ants.chd: Header (28 bytes, version 9), Palette (1024 bytes, 256 colors, key 254 transparent, team color ramps), Table 1 (2,794 sprites, row stride pitch padding, max dimensions), Table 2 (91 sounds, WAVEFORMATEX, 88 mono, 3 stereo, 15 unnamed, 44-byte RIFF WAV reconstruction), Table 3 (event tags 3, 4, 5, 10), Table 4 (1,344 animations, 8010 subitems, 12210 frames, 365 sound triggers, signed bounding box coordinates, signed dx/dy deltas, memory padding formula `((nlen + 4) & ~3)`). Full C++ parser tested and passed under Clang C++17 with ASan/UBSan with 0 errors.
- **Unexplored areas**: None within ants.chd scope. Level parser is handled by Explorer 1 (`teamwork_preview_explorer_m1_1`), Mirroring atlas by Explorer 3 (`teamwork_preview_explorer_m1_3`).

## Key Decisions Made
- Confirmed Table 4 on-disk sequence names are not null-terminated in file stream, while `((nlen + 4) & ~3)` represents the 4-byte aligned memory allocation size for dword-aligned C-strings with null terminator.
- Confirmed Table 4 subitem bounding boxes and frame dx/dy deltas are signed 32-bit integers (`int32_t`).
- Formulated production-ready, leak-free C++ classes (`ColorRGBA`, `Sprite`, `SoundClip`, `Animation`, `CHDArchive`) with built-in pitch-stride removal (`to_rgba`) and RIFF WAV header synthesis (`create_riff_wav`).
- Compiled and executed full verification test harness under Clang C++17 with `-fsanitize=address,undefined`, validating all 2,794 sprites, 91 sounds, 4 tags, and 1,344 animations with 0 memory errors or leaks.

## Artifact Index
- DISPATCH.md — Assignment instructions
- progress.md — Liveness heartbeat and step tracking
- chd_parsing_plan.md — Detailed parsing architecture, C++ implementation routines, and unit tests
- handoff.md — 5-component handoff report

