# Project: Microsoft Ants Remake

## Architecture
A modern, high-performance, deterministic cross-platform engine remake of Microsoft Ants (1995/1998) loading original binary assets (`ants.chd`, `Maps/*.LVL`).
The project is architected into 3 decoupled tiers using Modern C++ (C++17) with CMake, running on macOS via Apple Clang without external package managers or binary dependencies:

```
+-------------------------------------------------------------------------+
|                                ants-app                                 |
|  Interactive macOS Desktop Application (SDL2 + macOS AudioToolbox)     |
|  - 640x480 Integer Pixel Scaler with Authentic 4:3 Letterboxing         |
|  - Authentic HUD: Viewport, Minimap, Selection Card, Hatch, News, Clock |
|  - 32-Channel Audio Mixer (91 PCM Sound Effects)                        |
|  - Native macOS AudioToolbox MIDI Synthesizer (INTRO.MID)               |
|  - Results Scorecard Modal (re_screen)                                  |
+-------------------------------------------------------------------------+
                                    |
            +-----------------------+-----------------------+
            |                                               |
            v                                               v
+------------------------------------+   +------------------------------------+
|            libants-sim             |   |          libants-assets            |
|  100% Deterministic Headless       |   |  Zero-Dependency Binary Decoder    |
|  Simulation Engine (Zero External  |   |  - ants.chd (Header, Palette,      |
|  Dependencies)                     |   |    2794 Sprites, 91 Sounds,        |
|  - 20 Hz Discrete Tick Engine      |   |    1344 Anims, Sound Triggers)     |
|  - MSVC LCG PRNG Determinism       |   |  - Maps/*.LVL (Terrain & Items,    |
|  - Damage Matrix & Knockback       |   |    Trailing Blocks, 0 Rem Bytes)   |
|  - Combat Ant Autonomous Guard AI  |   |  - 5-to-8 Directional Mirroring    |
|  - Fire, Bomb, Bridge Physics      |   |    (O(1) Direct Indexed RAM Atlas) |
|  - Anthill Queuing & Thief Steal   |   +------------------------------------+
|  - Alliances & 4-Stat Scorecard    |
+------------------------------------+
```

## Feature Inventory
Every feature from the specification survey is assigned to a milestone:

| # | Feature | Description | Milestone | Source |
|---|---------|-------------|-----------|--------|
| 1 | `ants.chd` Header & Palette | 28-byte header, 256-color palette (RGB), color key 254 transparent | M1 | survey_assets.md |
| 2 | Table 1 Paletted Sprites | 2,794 raw paletted bitmaps, row stride padding preservation | M1 | survey_assets.md |
| 3 | Table 2 PCM Audio Clips | 91 PCM audio clips, WAVEFORMATEX, Sound IDs 0..90 | M1 | survey_assets.md |
| 4 | Table 3 & 4 Animations | 1,344 animation sequences, frame offsets, 365 default_sp sound triggers | M1 | survey_assets.md |
| 5 | `Maps/*.LVL` Level Decoder | 6 maps (31x31..60x60), 6-byte cells, Layer 1 & 2, trailing blocks (rem=0) | M1 | survey_assets.md |
| 6 | 5-to-8 Directional Mirroring | Horizontal reflection of facings 8, 9, 2 with dx'=-(dx+W) for O(1) lookup | M1 | survey_assets.md |
| 7 | Discrete 20 Hz Tick Engine | 20 Hz (50 ms) fixed tick, 32x32 integer tile grid, pure integer math | M2 | survey_sim.md |
| 8 | MSVC LCG PRNG Engine | holdrand * 214013 + 2531011, seedable via latseed or clock | M2 | survey_sim.md |
| 9 | Universal Unit Attributes | 10 HP max metric, 1 HP melee strike standard for standard units | M2 | survey_sim.md |
| 10 | Combat Ant Heavy Punch | 2 HP damage + 4-5 tile ballistic knockback, obstacle bounce, 12-tick stun | M2 | survey_sim.md |
| 11 | Combat Ant Guard AI | Autonomous 3-tile Chebyshev aggro scan, intercept punch, return to post | M2 | survey_sim.md |
| 12 | Cardinal-Only Placement | N, E, S, W adjacency check (|dx|+|dy|==1); tile validity bits 0x02/0x04 | M2 | survey_sim.md |
| 13 | Bomb Planting Mechanics | Bomber Ant planting (absb301, Sound 90), 2 HP detonation + 2-3 tile knockback | M2 | survey_sim.md |
| 14 | Bomber Squash Defusal | Bomber Ant exclusive body crush (abdb301, Sounds 73+74), 0 damage defuse | M2 | survey_sim.md |
| 15 | Fire Ignition & Obstruction | Magnifying glass ignition (afsf301, Sounds 67/68), A* path obstruction | M2 | survey_sim.md |
| 16 | Fire & Ricochet Physics | +1 fire damage on contact, deflection bounce, ants never extinguish by landing | M2 | survey_sim.md |
| 17 | Fire Ant Immunity & Extinguish | Immune to fire, extinguishes fire (afxf301, Sound 69, sputter Anim 135) | M2 | survey_sim.md |
| 18 | 180-Second Timers | Exact 180s (180,000 ms) lifetime for firewalls and bridges | M2 | survey_sim.md |
| 19 | Universal Bridge Traversal | Any ant (friendly, allied, or hostile enemy) can walk across built bridge | M2 | Sentinel Clarification |
| 20 | Bridge Collapse Drowning | 180s collapse drowns all non-swimmers instantly (death_status=0xF); Swimmers live | M2 | Sentinel Clarification |
| 21 | Anthill Queuing & 17-Frame Entry | Concentric Chebyshev queuing rings, 17-frame entry animation (hgen301) | M2 | survey_sim.md |
| 22 | Anthill Food Deposit | Frame 4 deposit, Sound 87 (scoreup.wav), food score increment | M2 | survey_sim.md |
| 23 | Underground 100% Heal | Frame 8 submerged in base, 100% full heal to 10 HP (powerupc.wav) | M2 | survey_sim.md |
| 24 | Ant Hatching & Economy | 200 points per egg deducted from team score, egg counter decrement | M2 | survey_sim.md |
| 25 | Thief Infiltration Dive | 33-frame stealth dive (atcr501, Sounds 84-86) into enemy anthill | M2 | survey_sim.md |
| 26 | Thief Alarm Siren & News Flash | Sound 58 (underattack.wav 2,566 Hz) & News Flash (String 53) to victim | M2 | survey_sim.md |
| 27 | Food Theft & Carry Visuals | Steals min(50, score), Sound 88 (scoredn.wav), lunchbox carrying visual suite | M2 | survey_sim.md |
| 28 | Lunchbox Physical Drop | Carrier death drops physical lunchbox (Anim 356, Sprite 513) with universal pickup | M2 | survey_sim.md |
| 29 | Dynamic FFA-to-Alliance Flow | Propose via enemy anthill, Sounds 49-53, accept, deny, break alliance | M2 | survey_sim.md |
| 30 | Allied Standings & Structs | Combined score on scoreboard while individual player records remain discrete | M2 | survey_sim.md |
| 31 | Match Timer & Simulation Freeze | Match clock countdown, immediate simulation freeze at 0:00 | M2 | survey_sim.md |
| 32 | Split Game Over Audio | Winner Sound 56 (winner.wav) vs Loser Sound 41 (playerout.wav) | M2 | survey_sim.md |
| 33 | 4-Stat Scorecard Tracking | Tracks Score, Friendly Ants Lost, Enemy Ants Killed, New Ants Hatched | M2 | survey_sim.md |
| 34 | Native macOS C++17 Application | CMake + Clang + SDL2 + AudioToolbox build pipeline | M3 | survey_architecture.md |
| 35 | 4:3 Integer Pixel Scaler | 640x480 native integer pixel scaling with pillarbox/letterbox | M3 | survey_architecture.md |
| 36 | HUD Top Frame & Clock | Top border (x0y0.bmp), digits (dig0..9.bmp, digc.bmp) | M3 | survey_architecture.md |
| 37 | HUD Borders & News Flash | Left border (x0y22.bmp), bottom banner (x17y461.bmp) | M3 | survey_architecture.md |
| 38 | HUD Minimap / Radar | Radar (x599y35.bmp), terrain, bases, unit dots, camera rect, click navigation | M3 | survey_architecture.md |
| 39 | HUD Selection Card | Selection card (x480y126.bmp), portrait, HP bar, wtype, wstatus, lunchbox icon | M3 | survey_architecture.md |
| 40 | HUD Hatch Controls & Egg Pile | Hatch button (labhatch, buthatup), egg pile (eggs, egg), incubation animation | M3 | survey_architecture.md |
| 41 | HUD Action Order Buttons | Buttons: Move, Attack, Bomb, Fire, Bridge, Thief, Cancel | M3 | survey_architecture.md |
| 42 | Multi-Channel Audio Mixer | 32 PCM channels for 91 sounds in ants.chd | M3 | survey_architecture.md |
| 43 | AudioToolbox MIDI Synthesizer | Native macOS AudioToolbox playback of INTRO.MID | M3 | survey_architecture.md |
| 44 | Results Scorecard Modal | Full-screen re_screen (Anim 25, 4 player columns) with audio routing | M3 | survey_architecture.md |
| 45 | Complete Interactive Game Loop | Playable loop: map selection, unit commands, abilities, win/loss scorecard | M3 | survey_architecture.md |
| 46 | Opaque-Box E2E Test Suite | Tiers 1-4 tests (Feature, Boundary, Combinations, Scenarios), TEST_READY.md | E2E | ORIGINAL_REQUEST.md |
| 47 | Full Acceptance Criteria Verification | Programmatic verification across all acceptance criteria | M4 | ORIGINAL_REQUEST.md |
| 48 | Adversarial Coverage Hardening | Tier 5 white-box challenger coverage audit and gap closure | M4 | Project Pattern |
| 49 | Water Splash & Ant Drowning Sequences | Standalone dsplash (Anim 40, Sound 71), 22-subitem drowning death (Anim 1134, 755, 804, 941, 1130; Sounds 71/72, rising air bubbles), Swimmer immunity | M2 | Sentinel Directive |

## Milestones

| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| M1 | M1: Native Binary Asset Decoder (`libants-assets`) | `ants.chd` header/palette/sprites/audio/tags/anims, `Maps/*.LVL` 6 maps, 5-to-8 directional mirroring, WAV reconstruction | none | **DONE** |
| M2 | Deterministic Simulation Engine & Rules (`ants-sim`) | Headless deterministic 20 Hz simulation engine: grid, MSVC LCG PRNG, damage matrix, Combat Ant Guard AI, cardinal placement, bombs/defuse, fire/ricochets, 180s timers, universal bridge traversal & collapse drowning, anthill queuing/entry/heal, thief infiltration, alliances, 4-stat tracking, automated test suite `test_sim_rules` | M1 (for map & animation data structures) | **DONE** |
| M3 | Interactive Application & Audio (`ants-app`) | Native macOS client: SDL2 640x480 integer pixel scaler (authentic 4:3), complete in-game HUD (viewport, minimap radar, selection card, hatch controls, news flash, match clock), 32-channel audio mixer, AudioToolbox MIDI player, results scorecard modal (`re_screen`), playable game loop | M1, M2 | PLANNED |
| M4 | Final Milestone: E2E Test Pass & Adversarial Hardening | Phase 1: Pass 100% of E2E test suite (Tiers 1-4) produced by E2E Testing Track. Phase 2: Adversarial Coverage Hardening (Tier 5) with Challengers | M1, M2, M3, E2E | PLANNED |
| E2E | E2E Testing Track (`ants-test`) | Independent opaque-box test suite: Test harness, Tier 1 (Feature >=5/feat), Tier 2 (Boundary >=5/feat), Tier 3 (Pairwise interactions), Tier 4 (Real-world scenarios), publishes `TEST_READY.md` | Interface Contracts | **DONE** |

## Interface Contracts

### `ants-assets` ↔ `ants-sim`
- `ants::assets::AssetArchive`:
  - `load_chd(const std::string& path) -> bool`
  - `get_palette() -> const std::array<ColorRGBA, 256>&`
  - `get_sprite(uint32_t index) -> const Sprite&`
  - `get_sound(uint32_t sound_id) -> const SoundClip&`
  - `get_animation(uint32_t anim_id) -> const Animation&`
  - `get_mirrored_sprite(uint32_t sprite_id, Direction dir) -> const Sprite&`
- `ants::assets::LevelData`:
  - `load_lvl(const std::string& path) -> bool`
  - `width() -> uint32_t`, `height() -> uint32_t`
  - `layer1_terrain(uint32_t x, uint32_t y) -> uint16_t`
  - `layer2_item(uint32_t x, uint32_t y) -> uint16_t`
  - `anthill_spawns() -> std::vector<AnthillSpawn>`
  - `food_schedules() -> std::vector<FoodSchedule>`

### `ants-sim` ↔ `ants-app`
- `ants::sim::SimulationEngine`:
  - `init(const LevelData& level, uint32_t random_seed)`
  - `tick()` -> discrete advance by 1 tick (50 ms)
  - `issue_order(const AntOrder& order)`
  - `hatch_ant(uint8_t player_id, AntType type) -> bool`
  - `propose_alliance(uint8_t from_player, uint8_t to_player)`
  - `get_world_state() -> const WorldState&`
  - `get_match_time_remaining_ms() -> uint32_t`
  - `is_match_over() -> bool`
  - `get_player_stats(uint8_t player_id) -> PlayerMatchStats` (Score, Friendly Lost, Enemy Killed, Hatched)
  - `poll_audio_events() -> std::vector<AudioEvent>` (sound_id, world_x, world_y, priority)
  - `poll_news_events() -> std::vector<NewsEvent>` (target_player, message_text)

## Code Layout
```
/Users/dchadd/Desktop/Ants-Mac/
├── CMakeLists.txt              # Root CMake build configuration
├── cmake/                      # FindSDL2 and compiler definitions
├── include/
│   ├── ants_assets/            # Public headers for libants-assets
│   │   ├── asset_archive.hpp
│   │   ├── chd_parser.hpp
│   │   ├── lvl_parser.hpp
│   │   └── mirroring.hpp
│   ├── ants_sim/               # Public headers for libants-sim
│   │   ├── sim_engine.hpp
│   │   ├── grid.hpp
│   │   ├── ant_unit.hpp
│   │   ├── combat_ai.hpp
│   │   ├── physics.hpp
│   │   ├── prng.hpp
│   │   └── match_stats.hpp
│   └── ants_app/               # Application headers
│       ├── application.hpp
│       ├── renderer.hpp
│       ├── hud.hpp
│       ├── audio_mixer.hpp
│       ├── midi_player.hpp
│       └── scorecard.hpp
├── src/
│   ├── ants_assets/            # Implementation of libants-assets
│   ├── ants_sim/               # Implementation of libants-sim
│   └── ants_app/               # Implementation of ants-app
├── tests/
│   ├── test_assets/            # Headless asset verification test suite
│   ├── test_sim/               # Headless simulation rules test suite
│   └── e2e/                    # Opaque-box E2E test suite (Tiers 1-4)
└── tools/                      # Diagnostic and asset extraction tools
```
