# Task Assignment: Application Architecture & Platform Toolchain Exploration

## Target Scope
Survey the architecture, UI/HUD, audio mixer, technology stack, and platform environment:
- Specification: `/Users/dchadd/Desktop/Ants-Mac/GAME_REVERSE_ENGINEERING.md`
- User Request: `/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md`
- Project root environment: macOS environment, installed compilers, toolchains (e.g. clang, rustc, python, node, etc.)

## Objective
Investigate and evaluate:
1. Interactive Application Requirements:
   - Hardware-accelerated 2D viewport with integer pixel scaling and authentic 4:3 presentation (original 640x480 resolution scaled to current window/display).
   - Complete In-Game HUD: selection card (ant portrait, health, status), minimap (radar representation of map, units, bases), hatch controls (ant selection buttons, eggs, hatch queuing), egg counter, news flash banner, match clock (counting down to 0:00).
   - Multi-channel audio mixer routing sound effects (PCM 91 clips) and MIDI background music.
   - Interactive end-of-game Results Scorecard modal (`re_screen` tracking 4 stats: Score, Friendly Ants Lost, Enemy Ants Killed, New Ants Hatched).
2. Technology Stack & Native Toolchain Evaluation:
   - What native compilers and runtimes are available in this environment (e.g. check clang/c++, rustc/cargo, python3, etc.)?
   - What architecture is best to satisfy:
     * R1 (`ants-assets`): fast direct binary decoding without external dependencies.
     * R2 (`ants-sim`): 100% deterministic headless testable simulation engine.
     * R3 (`ants-app`): native build runs on macOS via installed toolchain without external binary dependencies, providing interactive window, audio, keyboard/mouse input.
     * Cross-module decoupling: modular crates/libraries/packages separating assets, simulation, and frontend application.
3. Acceptance Criteria & Automated Testing Strategy:
   - Headless unit/integration test harness for automated simulation rules verification.
   - Asset decoder test harness parsing all 2,794 sprites, 91 sound effects, 6 maps.
   - Verification of zero leaks/crashes, 8-way directional mirroring, and macOS native app launch.

## Output
Write your findings to `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_survey_3/survey_architecture.md` and write your completion `handoff.md`.

## 2026-09-06T22:32:23Z
Received mission:
Evaluate Interactive Application Requirements (viewport, HUD, audio mixer, scorecard modal), Technology Stack & Native Toolchain, Acceptance Criteria & Automated Testing Strategy.
Deliver: survey_architecture.md and handoff.md.

