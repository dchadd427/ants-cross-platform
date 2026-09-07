# Task Assignment: Asset Specification Mining

## Target Scope
Survey the authoritative reference specification and binary files for Asset Decoding:
- Specification: `/Users/dchadd/Desktop/Ants-Mac/GAME_REVERSE_ENGINEERING.md`
- Original Assets: `/Users/dchadd/Desktop/Ants-Mac/Original-Ants/` (`ants.chd`, `Maps/*.LVL`, etc.)
- User Request: `/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md`

## Objective
Extract and document the exact, complete specification for:
1. `ants.chd` binary structure: 28-byte header fields, palette structure (256-color RGB/BGR, color 0 transparency, cycling/reserved indices), sprite table format, 2,794 raw paletted sprite bitmaps (dimensions, offsets, compression or raw bytes), 91 PCM audio clips (sample rates, channels, bit depth, data format, naming/sound IDs), 1,344 animation sequences and frame sound triggers (`default_sp`).
2. Map formats (`Maps/*.LVL`): header, tile dictionaries, grid dimensions (60x60, 40x40, 31x31), Layer 1 terrain indices/types, Layer 2 items/spawns/anthills/food/obstacles.
3. 5-to-8 directional sprite mirroring algorithm: original directions (e.g. 1, 3, 4, 7, etc.), mirrored directions (8, 9, 2 for North-West, West, South-West) to provide O(1) 8-directional lookup, flip axes, horizontal flip mechanics.
4. Audio mapping: Sound IDs 0 to 90, specific sounds mentioned in requirements (Sound 58 underattack.wav 2,566 Hz alarm siren, Sound 88 scoredn.wav, Sound 56 winner.wav, Sound 41 playerout.wav, sounds 73 + 74 bomber defuse squash, etc.).

## Output
Write your findings to `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_spec_miner_survey_1/survey_assets.md` and write your completion `handoff.md` with verified evidence and exact offsets/data structures.
