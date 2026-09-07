## 2026-09-06T23:43:48Z
You are M3 Explorer 3 (Audio Mixer, AudioToolbox MIDI & Verification Harness) for Milestone 3 (ants-app).
Your identity: explorer_m3_3
Your working directory: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m3_3
Parent conversation ID: a28dfa55-5a82-453d-a21b-99459a66b340

MANDATORY: Read the original user request before starting work:
/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md

Also read:
- /Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_survey_3/survey_architecture.md
- /Users/dchadd/Desktop/Ants-Mac/GAME_REVERSE_ENGINEERING.md
- /Users/dchadd/Desktop/Ants-Mac/include/ants_assets/asset_archive.hpp
- /Users/dchadd/Desktop/Ants-Mac/include/ants_sim/sim_engine.hpp

Your mission:
1. Design the audio subsystem and automated verification test suite for ants-app:
   - Multi-Channel Audio Mixer: 32 concurrent PCM audio channels playing 8-bit unsigned 22.05 kHz audio clips loaded from ants.chd, spatial panning (stereo left/right attenuation based on entity distance from viewport center), volume control, priority queuing (poll_audio_events from SimulationEngine).
   - AudioToolbox MIDI Synthesizer: Native macOS AudioToolbox playback of Original-Ants/INTRO.MID via MusicSequence and MusicPlayer, looping playback, volume fade, clean initialization and teardown without third-party dependencies.
   - Headless Automated Verification Harness: Design tests/test_app/test_app_integration.cpp that programmatically instantiates the application/renderer/HUD/audio pipeline without requiring a physical display:
     * Validates 640x480 software surface rendering and pixel buffer compositing.
     * Validates camera panning and world-to-screen coordinate transforms.
     * Validates HUD element layout, radar dot placement, and selection card updates.
     * Validates 32-channel audio mixer ingestion of simulation audio events and spatial pan calculation.
     * Validates AudioToolbox MIDI file loading and playback lifecycle.
     * Validates Scorecard modal 4-stat rendering and winner/loser audio routing.
   - CMake configuration for libants_app, ants executable, and test_app_integration.
2. Formulate public C++ headers and implementation blueprints:
   - include/ants_app/audio_mixer.hpp
   - include/ants_app/midi_player.hpp
   - src/ants_app/audio_mixer.cpp
   - src/ants_app/midi_player.cpp
   - tests/test_app/CMakeLists.txt
   - tests/test_app/test_app_integration.cpp
3. Document full specifications and drop-in code blueprints in:
   /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m3_3/audio_and_tests_plan.md
4. Deliver handoff report to:
   /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m3_3/handoff.md
   and send a completion message to your parent.
