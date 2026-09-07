## 2026-09-06T23:48:31Z
You are the M3 Worker for the Ants remake project.
Your identity: worker_m3_1
Your working directory: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_worker_m3_1
Parent conversation ID: a28dfa55-5a82-453d-a21b-99459a66b340

MANDATORY: Read the original user request before starting work:
/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md

Also read:
- Master Project Document:
  /Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md
- Authoritative Explorer Blueprints:
  1. /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m3_1/graphics_and_renderer_plan.md
  2. /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m3_2/hud_and_ui_plan.md
  3. /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m3_3/audio_and_tests_plan.md

File Ownership:
You exclusively own and will create/modify:
- include/ants_app/application.hpp
- include/ants_app/renderer.hpp
- include/ants_app/hud.hpp
- include/ants_app/scorecard.hpp
- include/ants_app/audio_mixer.hpp
- include/ants_app/midi_player.hpp
- src/ants_app/CMakeLists.txt
- src/ants_app/application.cpp
- src/ants_app/renderer.cpp
- src/ants_app/hud.cpp
- src/ants_app/scorecard.cpp
- src/ants_app/audio_mixer.cpp
- src/ants_app/midi_player.cpp
- src/ants_app/main.cpp
- tests/test_app/CMakeLists.txt
- tests/test_app/test_app_integration.cpp
- CMakeLists.txt
- run_tests.sh

MANDATORY INTEGRITY WARNING:
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_preview_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

Mission:
Implement the complete, authentic, modern C++17 library libants_app, interactive desktop client executable `ants`, and automated verification test suite `test_app_integration` according to the blueprints:
1. Graphics, Windowing & Viewport:
   - SDL2 window, 640x480 virtual canvas with integer scaling / 4:3 letterboxing via SDL_RenderSetLogicalSize.
   - Viewport clipping at (17, 22), 441x439, camera scrolling (arrow keys/WASD, mouse drag, edge pan, minimap click), bounds clamping.
   - Layer 1 terrain + Layer 2 interactive objects/structures compositing from LevelData and Grid.
   - Animated ant sprite rendering: 6 unit classes, 8 facings with 5-to-8 directional mirroring (get_mirrored_sprite), state animations, 36px parabolic knockback elevation and shadow rendering (shadow.bmp), Y-depth sorting.
2. HUD, UI Controls & Scorecard Modal:
   - 32x32 radar minimap widget with live team ant dots, anthills, food markers, camera frustum box, click-to-navigate.
   - Ant selection card (portrait, type name, HP bar, holding food/points indicator, order status).
   - Hatch controls & egg pile with incubation counter.
   - Action order buttons (Move, Attack, Bomb, Fire, Bridge, Thief, Cancel) with hotkeys and active highlighting.
   - News flash banner queue, match countdown clock (mm:ss to 0:00), dynamic alliance scoreboard.
   - Scorecard modal: full-screen re_screen (Anim 25) upon GameOver freeze, 4 player columns with Score, Friendly Lost, Enemy Killed, Hatched; winner Sound 56 vs loser Sound 41 routing.
3. Audio Subsystem:
   - 32-channel software PCM mixer resampling 8-bit unsigned mono PCM (11.025/22.05 kHz) to 44.1 kHz stereo with linear interpolation, equal-power spatial panning and distance attenuation from viewport center, priority preemption, targeted player filtering, and headless render_frames() helper.
   - Native macOS AudioToolbox MIDI player playing Original-Ants/INTRO.MID via MusicSequence/MusicPlayer, Apple DLS General MIDI synth, infinite looping, volume control, fading state machine, and headless mock fallback.
4. Automated Test Suite & Runner Integration:
   - Implement tests/test_app/test_app_integration.cpp covering 6 test suites (surface compositing, camera bounds, HUD/radar projections, 32-channel mixer allocation/spatial audio, AudioToolbox MIDI lifecycle, Scorecard modal rendering & win/loss audio).
   - Update CMakeLists.txt to add src/ants_app and tests/test_app.
   - Update run_tests.sh to support ./run_tests.sh --app and include test_app_integration in ./run_tests.sh --all.
5. Build and Verify:
   - Build using CMake.
   - Execute ./run_tests.sh --app
   - Execute ./run_tests.sh --all
   - Execute ./run_tests.sh --clean --asan
   - Verify 100% tests pass cleanly with 0 memory leaks and 0 undefined behavior.
6. Deliver handoff report to:
   /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_worker_m3_1/handoff.md
   and send a completion message to your parent.
