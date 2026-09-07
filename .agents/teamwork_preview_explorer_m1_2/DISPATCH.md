# Task Assignment: M1 Explorer 2 - ants.chd Parsing Engine Details

## Scope & Target
Milestone 1 (`ants-assets`): Complete byte-accurate parsing pipeline for `Original-Ants/ants.chd`.

## Inputs
- Verbatim User Request: `/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md`
- Master Project Document: `/Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md`
- Asset Survey: `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_spec_miner_survey_1/survey_assets.md`

## Objective
1. Specify exact parsing logic for `ants.chd`:
   - 28-byte header verification (version 9, offsets for Tables 1..4).
   - Palette extraction (1024 bytes -> 256 RGBA colors, Color 254 transparent Alpha=0, Team color indices).
   - Table 1 (2,794 Sprites): offset table, pitch stride padding `uint8_t pixels[pitch * height]`, filename parsing, bounding box validation.
   - Table 2 (91 Audio Clips): offset table, WAVEFORMATEX, 8-bit unsigned PCM buffer, WAV header reconstruction, Sound IDs 0..90 mapping.
   - Table 3 (Event Tags): Descriptors for Tags 3, 4, 5, 10.
   - Table 4 (1,344 Animations): offset table, sequence names, 4-byte padding formula `((nlen + 4) & ~3)`, subitem records, frame delta offsets (`dx, dy`), sprite IDs, and frame sound triggers (`default_sp`).
2. Provide concrete C++ code patterns for fast, zero-copy or minimal-copy binary deserialization with robust bounds checking.
3. Detail unit test cases for Table 1, Table 2, and Table 4 verification without leaks or segfaults.
4. Document findings in `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_2/chd_parsing_plan.md` and handoff report `handoff.md`.

## 2026-09-06T22:39:01Z
You are M1 Explorer 2 (ants.chd Parsing Engine) for Milestone 1 (ants-assets).
Your identity: explorer_m1_2
Your working directory: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_2
Parent conversation ID: a28dfa55-5a82-453d-a21b-99459a66b340

MANDATORY: Read the original user request before starting work:
/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md

Also read:
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_2/DISPATCH.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_spec_miner_survey_1/survey_assets.md
- /Users/dchadd/Desktop/Ants-Mac/Original-Ants/ants.chd

Your mission:
1. Provide exact, verified C++ implementation logic for parsing ants.chd:
   - 28-byte header verification (version 9, offsets for Tables 1..4).
   - 256-color palette extraction (1024 bytes -> Little-Endian RGBA, color 254 transparent Alpha=0, Team color indices).
   - Table 1 (2,794 Sprites): offset table, pitch stride padding handling (uint8_t pixels[pitch * height]), filename parsing, bounding box validation.
   - Table 2 (91 Audio Clips): offset table, WAVEFORMATEX, 8-bit unsigned PCM buffer, standard 44-byte RIFF WAV header reconstruction, Sound IDs 0..90 mapping.
   - Table 3 (Event Tags): Descriptors for Tags 3, 4, 5, 10.
   - Table 4 (1,344 Animations): offset table, sequence names, 4-byte padding formula ((nlen + 4) & ~3), subitem records, frame delta offsets (dx, dy), sprite IDs, and frame sound triggers (default_sp).
2. Provide concrete C++ deserializer routines with robust bounds checking and ASan clean safety.
3. Document comprehensive unit test specifications for Table 1, Table 2, Table 4 in:
   /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_2/chd_parsing_plan.md
4. Write your handoff report to /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_2/handoff.md and send a completion message to your parent.
