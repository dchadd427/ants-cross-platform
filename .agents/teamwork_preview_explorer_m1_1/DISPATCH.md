## 2026-09-06T22:39:01Z

# Task Assignment: M1 Explorer 1 - Asset Architecture & API Design

## Scope & Target
Milestone 1 (`ants-assets`): Architecture, Public C++ Headers, Memory Management, and CMake Integration.

## Inputs
- Verbatim User Request: `/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md`
- Master Project Document: `/Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md`
- Asset Survey: `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_spec_miner_survey_1/survey_assets.md`
- Architecture Survey: `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_survey_3/survey_architecture.md`

## Objective
1. Design the public C++ API headers under `include/ants_assets/`:
   - `asset_archive.hpp`: Master asset manager loading `ants.chd`, providing access to palettes, sprites, audio clips, animations, and mirrored directional sprites.
   - `chd_parser.hpp`: Internal decoder classes/functions for parsing `ants.chd` header, palette, Table 1, Table 2, Table 3, Table 4.
   - `lvl_parser.hpp`: Level parser loading `Maps/*.LVL` with layer 1 terrain, layer 2 items/spawns, and 4 trailing blocks.
   - `mirroring.hpp`: Pre-generated 8-way directional mirroring data structures and lookup utilities.
2. Formulate memory strategy:
   - Contiguous sprite pixel buffers with stride padding handling.
   - Palette color format: Little-Endian RGBA (color 254 alpha=0).
   - Audio buffer representation: raw PCM bytes + WAV header generation for SDL2/audio mixer consumption.
3. Formulate CMake target `ants_assets` (static library) and standalone test executable `test_assets`.
4. Document full implementation recommendations and file-by-file blueprint in `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m1_1/m1_architecture_plan.md` and handoff report `handoff.md`.
