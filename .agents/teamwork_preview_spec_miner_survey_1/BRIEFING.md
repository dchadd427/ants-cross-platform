# BRIEFING — 2026-09-06T15:37:00Z

## Mission
Extract and document the exact, complete specification for ants.chd binary structure, map formats (.LVL), 5-to-8 directional sprite mirroring algorithm, and audio mapping.

## 🔒 My Identity
- Archetype: specification_miner
- Roles: Teamwork specialist, Asset Specification Miner
- Working directory: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_spec_miner_survey_1
- Original parent: a28dfa55-5a82-453d-a21b-99459a66b340
- Milestone: specification_mining

## 🔒 Key Constraints
- Sole job is to discover and document features by probing authoritative specifications (GAME_REVERSE_ENGINEERING.md, ants.chd, Maps/*.LVL, Ants.exe).
- Do NOT implement anything — read-only specification extraction.
- Prioritize authoritative sources over LLM prior knowledge.
- Must document:
  1. ants.chd binary structure (28-byte header, palette structure, sprite table format, 2,794 raw paletted sprite bitmaps, 91 PCM audio clips, 1,344 animation sequences & frame sound triggers).
  2. Map formats (Maps/*.LVL: header, tile dictionaries, grid dimensions 60x60, 40x40, 31x31, Layer 1 terrain, Layer 2 items/spawns/anthills/food/obstacles).
  3. 5-to-8 directional sprite mirroring algorithm (original directions 1, 3, 4, 7, etc., mirrored directions 8, 9, 2 for NW, W, SW; O(1) 8-directional lookup, flip axes, horizontal flip mechanics).
  4. Audio mapping: Sound IDs 0 to 90, specific sounds mentioned in requirements (Sound 58 underattack.wav, Sound 88 scoredn.wav, Sound 56 winner.wav, Sound 41 playerout.wav, Sounds 73+74 bomber defuse squash, etc.).
- Output to /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_spec_miner_survey_1/survey_assets.md and handoff.md.
- Send completion message to parent via send_message.

## Current Parent
- Conversation ID: a28dfa55-5a82-453d-a21b-99459a66b340
- Updated: 2026-09-06T15:37:00Z

## Task Summary
- **What to build**: Comprehensive asset survey specification document covering ants.chd, Maps/*.LVL, 5-to-8 mirroring algorithm, and sound mappings.
- **Success criteria**: Complete specification with exact byte offsets, structs, field formats, tables of all sound effects (0..90), sprite parameters, map layers, edge cases, and verification evidence.
- **Interface contracts**: /Users/dchadd/Desktop/Ants-Mac/GAME_REVERSE_ENGINEERING.md, ORIGINAL_REQUEST.md
- **Code layout**: .agents/teamwork_preview_spec_miner_survey_1/survey_assets.md, handoff.md

## Key Decisions Made
- Wrote and executed Python probe scripts verifying exact binary structures directly against ants.chd and all 6 .LVL map files in Original-Ants.
- Verified 100% of bytes consumed across all 6 map files (0 remaining bytes).
- Catalogued and documented all 91 digital sound effects including 15 unnamed clips.
- Formulated exact mathematical equations for horizontal sprite and offset reflection ($p'_x = W - 1 - p_x, dx' = -(dx + W)$).
- Produced comprehensive `survey_assets.md` and complete `handoff.md`.

## Artifact Index
- survey_assets.md — Comprehensive asset reverse-engineering survey
- handoff.md — 5-component handoff report
- progress.md — Liveness heartbeat and step tracking
