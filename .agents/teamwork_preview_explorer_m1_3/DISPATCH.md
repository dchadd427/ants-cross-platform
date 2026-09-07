# Task Assignment: M1 Explorer 3 - Map Parsing & 5-to-8 Directional Mirroring

## Scope & Target
Milestone 1 (`ants-assets`): Complete parsing of all 6 `Maps/*.LVL` files and the 5-to-8 directional sprite mirroring engine.

## Inputs
- Verbatim User Request: `/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md`
- Master Project Document: `/Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md`
- Asset Survey: `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_spec_miner_survey_1/survey_assets.md`
- Reference files: `Original-Ants/Maps/*.LVL`

## Objective
1. Specify exact parsing logic for `Maps/*.LVL` (all 6 levels: `TINY.LVL`, `SMALL.LVL`, `MEDIUM.LVL`, `ISLANDS.LVL`, `GAUNTLET.LVL`, `TREASURE.LVL`):
   - Header (version, game mode, dimensions, title string).
   - Tile dictionary (count, tile records).
   - Layer 1 terrain: width * height * 6 bytes.
   - Layer 2 interactive overlay: width * height * 6 bytes (empty sentinel 0x7FFE).
   - Block 1: Anthill base spawns (`BSTART`, `USTART`, `GSTART`, `RSTART`).
   - Block 2: Food respawn schedules and pools.
   - Block 3: Ambient flags.
   - Block 4: Waypoints and patrol coordinates.
   - Trailing verification: exactly 0 remaining bytes (`rem = 0`) across all 6 maps.
2. Specify 5-to-8 Directional Sprite Mirroring Engine:
   - Base stored facings: 7 (North), 8 (North-East), 9 (East), 2 (South-East), 3 (South).
   - Mirrored facings: North-West (from 8), West (from 9), South-West (from 2).
   - Horizontal reflection math:
     * Pixel flipping: $p'(x, y) = p(W - 1 - x, y)$
     * Frame offset flipping: $dx' = -(dx + W), \quad dy' = dy$
     * Lunchbox carrying sprite mapping (`3lb`, `4lb`, `5lb`, `6lb`, `7lb`).
   - O(1) runtime lookup table pre-computation: atlas storing all 8 directions in memory.
3. Detail unit test cases for map validation and mirroring correctness.
4. Document findings in `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_3/lvl_and_mirroring_plan.md` and handoff report `handoff.md`.

## 2026-09-06T22:39:01Z
You are M1 Explorer 3 (Maps & 5-to-8 Directional Mirroring) for Milestone 1 (ants-assets).
Your identity: explorer_m1_3
Your working directory: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_3
Parent conversation ID: a28dfa55-5a82-453d-a21b-99459a66b340

MANDATORY: Read the original user request before starting work:
/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md

Also read:
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_3/DISPATCH.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_spec_miner_survey_1/survey_assets.md
- /Users/dchadd/Desktop/Ants-Mac/Original-Ants/Maps/

Your mission:
1. Provide exact, verified C++ implementation logic for parsing all 6 maps in Maps/*.LVL:
   - Header, tile dictionary, 6-byte cell structure for Layer 1 & Layer 2, sentinel 0x7FFE, 4 trailing blocks (anthill spawns, food schedules, ambient flags, waypoints).
   - Guarantee 0 remaining bytes (rem = 0) across all 6 maps (TINY, SMALL, MEDIUM, ISLANDS, GAUNTLET, TREASURE).
2. Provide exact C++ 5-to-8 directional sprite mirroring algorithm:
   - Base facings: 7 (N), 8 (NE), 9 (E), 2 (SE), 3 (S).
   - Mirrored facings: NW (from 8), W (from 9), SW (from 2).
   - Pixel reflection: p'(x, y) = p(W - 1 - x, y).
   - Frame offset reflection: dx' = -(dx + W), dy' = dy.
   - Lunchbox directional sprites mapping (3lb, 4lb, 5lb, 6lb, 7lb).
   - Pre-computation into an in-memory 8-direction atlas for O(1) runtime lookup.
3. Document comprehensive unit test specifications for map decoding and mirroring correctness in:
   /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_3/lvl_and_mirroring_plan.md
4. Write your handoff report to /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_3/handoff.md and send a completion message to your parent.
