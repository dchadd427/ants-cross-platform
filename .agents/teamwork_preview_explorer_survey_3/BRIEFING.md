# BRIEFING — 2026-09-06T22:37:00Z

## Mission
Survey the architecture, UI/HUD, audio mixer, technology stack, native toolchains, and automated testing strategy for the Microsoft Ants remake.

## 🔒 My Identity
- Archetype: explorer
- Roles: Architecture Explorer
- Working directory: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_survey_3
- Original parent: a28dfa55-5a82-453d-a21b-99459a66b340
- Milestone: Exploration & Architecture Evaluation

## 🔒 Key Constraints
- Read-only investigation — do NOT implement
- Produce comprehensive survey in `survey_architecture.md`
- Produce completion report in `handoff.md`
- Communicate via `send_message` to parent `a28dfa55-5a82-453d-a21b-99459a66b340`

## Current Parent
- Conversation ID: a28dfa55-5a82-453d-a21b-99459a66b340
- Updated: 2026-09-06T22:37:00Z

## Investigation State
- **Explored paths**: `ORIGINAL_REQUEST.md`, `GAME_REVERSE_ENGINEERING.md`, `Original-Ants/ants.chd`, `Original-Ants/Maps/*.LVL`, `Original-Ants/INTRO.MID`, macOS system toolchains (`clang++`, `cmake`, `brew`, `sdl2`, `AudioToolbox`).
- **Key findings**:
  * Toolchains: Clang 21.0.0 (Apple Silicon arm64), CMake 4.3.2, SDL2 2.32.10, AudioToolbox are pre-installed and verified. `rustc`/`cargo` are NOT installed. Modern C++ (C++17) is the authoritative stack.
  * Modular Decoupling: Strict 3-tier structure (`libants-assets`, `libants-sim`, `ants-app`). Core asset and sim engines have zero external dependencies.
  * Viewport & HUD: Exact 640x480 resolution, integer nearest-neighbor scaling (4:3 aspect with black letterboxing), playfield at X: 17-458, Y: 22-461 (441x439 px). Right HUD panel (X: 480-640) with minimap radar, selection card (ant portrait, HP, status), hatch controls, egg counter, order buttons, news flash banner (`x17y461.bmp`), match clock (`dig0..9.bmp`), and results scorecard (`re_screen`, 4 stats, winner/loser audio).
  * Audio: 32-channel PCM mixer for 91 sound clips + native macOS `AudioToolbox` General MIDI synthesizer for `INTRO.MID`.
  * Automated Testing: Headless test suites `test_assets` and `test_sim_rules` verify all acceptance criteria and 8 game rules in RAM without GUI context.
- **Unexplored areas**: None for this milestone. Full survey completed.

## Key Decisions Made
- Architecture: Modern C++ (C++17) with CMake build system.
- Decoupling: Pure C++ static libraries for `libants-assets` and `libants-sim` (zero dependencies), frontend `ants-app` using SDL2 and `AudioToolbox`.
- Presentation: SDL2 logical size 640x480 with nearest-neighbor integer scaling.
- Audio: SDL2 PCM mixer (32 channels) + macOS `AudioToolbox` for MIDI.
- Testing: Headless test suites `test_assets` and `test_sim_rules`.

## Artifact Index
- `.agents/teamwork_preview_explorer_survey_3/DISPATCH.md` — Incoming task specifications
- `.agents/teamwork_preview_explorer_survey_3/BRIEFING.md` — Persistent working memory
- `.agents/teamwork_preview_explorer_survey_3/progress.md` — Liveness heartbeat
- `.agents/teamwork_preview_explorer_survey_3/survey_architecture.md` — Comprehensive architecture survey
- `.agents/teamwork_preview_explorer_survey_3/handoff.md` — Handoff report
