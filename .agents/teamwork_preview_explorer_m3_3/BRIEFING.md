# BRIEFING — 2026-09-06T23:43:55Z

## Mission
Design the audio subsystem (32-channel PCM mixer, native AudioToolbox MIDI synth) and headless automated verification test harness for Milestone 3 (ants-app).

## 🔒 My Identity
- Archetype: Teamwork explorer
- Roles: Audio Architecture Designer, Test Harness Architect, Synthesizer
- Working directory: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m3_3
- Original parent: a28dfa55-5a82-453d-a21b-99459a66b340
- Milestone: Milestone 3 (ants-app)

## 🔒 Key Constraints
- Read-only investigation — do NOT implement production code in src/ or tests/ directly until requested (deliver design blueprints in agent folder)
- No third-party audio/MIDI dependencies: use native macOS AudioToolbox / CoreAudio, standard C++17
- Respect existing project conventions and Milestone 1/2 designs
- 32 concurrent PCM audio channels playing 8-bit unsigned 22.05 kHz audio clips loaded from ants.chd
- Spatial panning (stereo left/right attenuation based on entity distance from viewport center)
- AudioToolbox MIDI playback of Original-Ants/INTRO.MID via MusicSequence and MusicPlayer
- Headless automated verification harness in tests/test_app/test_app_integration.cpp

## Current Parent
- Conversation ID: a28dfa55-5a82-453d-a21b-99459a66b340
- Updated: not yet

## Investigation State
- **Explored paths**:
  - `ORIGINAL_REQUEST.md`, `PROJECT.md`, `survey_architecture.md`, `GAME_REVERSE_ENGINEERING.md`
  - `include/ants_assets/asset_archive.hpp`, `chd_parser.hpp`
  - `include/ants_sim/sim_engine.hpp`, `src/ants_sim/sim_engine.cpp`
  - `Original-Ants/INTRO.MID`, `Original-Ants/ants.chd`
  - Clang toolchain test with AudioToolbox frameworks and Homebrew SDL2
- **Key findings**:
  - `ants.chd` contains 91 8-bit unsigned mono PCM clips (11025 and 22050 Hz, silence=128)
  - `Original-Ants/INTRO.MID` is 9,261 bytes, 38 tracks, 96.01 beats duration, loads cleanly via AudioToolbox `MusicSequenceFileLoad`
  - `SimulationEngine::poll_audio_events()` yields `AudioEvent` with `sound_id`, `world_x`, `world_y`, `priority`, and `target_player`
  - Split win/loss audio: Winner player gets `SoundID::VictoryFanfare` (56), losers get `SoundID::PlayerDefeat` (41)
  - Headless offscreen surface (640x480) enables complete pipeline testing without display or audio hardware
- **Unexplored areas**: None (full architectural blueprint formulated)

## Key Decisions Made
- Multi-Channel Audio Mixer: 32 channels with linear interpolation resampling, equal-power spatial panning ($\cos/\sin$ curve), distance attenuation ($MAX\_DIST = 800$ px), priority preemption with past-75% progress tie-breaking, and memory rendering helper `render_frames()`.
- AudioToolbox MIDI: Native macOS `MusicPlayer` + `MusicSequence` with Pimpl `Impl` pattern to prevent Apple headers leaking into public headers; looping via `kSequenceTrackProperty_LoopInfo`; volume fading state machine with headless mock mode.
- Headless Integration Test Harness: 6 suites in `test_app_integration.cpp` testing surface compositing, camera transforms, HUD/radar projection, 32-channel mixer, AudioToolbox MIDI lifecycle, and Scorecard modal with 4-stat columns.
- CMake: static library `ants_app` linking `ants_sim`, `ants_assets`, `SDL2`, and macOS frameworks (`AudioToolbox`, `CoreFoundation`, `CoreAudio`), and test target `test_app_integration`.

## Artifact Index
- `DISPATCH.md` — record of incoming dispatch instructions
- `BRIEFING.md` — persistent working memory
- `progress.md` — liveness heartbeat
- `audio_and_tests_plan.md` — full specifications and drop-in code blueprints for headers, sources, CMake, and test harness
- `handoff.md` — 5-component handoff report
