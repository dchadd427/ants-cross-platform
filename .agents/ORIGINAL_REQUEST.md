# Original User Request

## 2026-09-06T22:30:38Z

A modern, high-performance, deterministic cross-platform engine remake of Ants (1995/1998) that directly loads original raw binary assets (ants.chd and Maps/*.LVL) without pre-conversion, faithfully executing all authentic mechanics, physics, and audiovisual presentation.

Working directory: /Users/dchadd/Desktop/Ants-Mac
Integrity mode: development

## Reference Material
- Detailed reverse-engineered specification: GAME_REVERSE_ENGINEERING.md in the working directory
- Original game binary and data: Original-Ants/Ants.exe, Original-Ants/ants.chd, Original-Ants/Maps/*.LVL

## Requirements

### R1. Native Binary Asset Decoder (ants-assets)
Directly decode and parse raw original assets at runtime:
- Parse ants.chd (28-byte header, 256-color palette, 2,794 raw paletted sprite bitmaps, 91 PCM audio clips, 1,344 animation sequences with frame sound triggers default_sp).
- Parse all map files in Maps/*.LVL (tile dictionaries, 60×60, 40×40, 31×31 grids, Layer 1 terrain, Layer 2 items/spawns).
- Implement 5-to-8 directional sprite mirroring (pre-generating mirrored versions of directions 8, 9, 2 for North-West, West, South-West) to provide O(1) 8-directional lookup.

### R2. Deterministic Simulation Engine & Game Rules (ants-sim)
Implement the tick-based deterministic simulation grid matching 100% of reverse-engineered rules:
- Unit Damage Matrix: Universal 1 HP melee strike standard for Worker, Thief, Fire, Bomber, and Swimmer ants. Combat Ants deal 2 HP per strike + 4–5 tile ballistic knockback.
- Combat Ant Guard AI: Combat Ants are the only unit type with autonomous behavior (3-tile aggro perimeter scan when idle, autonomous intercept punch, automatic return to post).
- Placement Rules: Firewalls and bombs are strictly cardinal-only (North, East, South, West; no diagonal placement).
- Bomb Mechanics: Bomber Ant plants mines (2 HP explosive damage); Bomber Ant disarms enemy mines via body crush squash (abdb animation, sounds 73 + 74).
- Fire & Ricochet Physics: Non-fire ants landing on fire take 1 fire damage and bounce off; multi-fire chains and ant collision deflections create chaotic ricochets. Ants never extinguish fire by landing on it. Fire Ants are immune and can extinguish fires.
- Timers: 180-second exact lifetime for firewalls (burnout) and bridges (collapse causing non-swimmer ants to drown instantly).
- Base Mechanics: Concentric Chebyshev ring queuing around anthill; 17-frame base entry, food deposit, underground 100% heal, and emergence.
- Thief Ant Infiltration: Dives into enemy anthill, triggers Sound 58 (underattack.wav, 2,566 Hz alarm siren) and News Flash on victim's screen, steals min(50, score), triggers Sound 88 (scoredn.wav), drops lunchbox on death with universal pickup.
- Alliances & Teaming: Dynamic FFA-to-alliance flow initiated by clicking an enemy anthill (allypro.wav, allyyes.wav/allyon.wav, allynot.wav, allyoff.wav). Combined scores for HUD/standings with strictly preserved individual stats in memory.
- Game Over Scorecard: Immediate simulation freeze at 0:00; winner audio (winner.wav / Sound 56) vs loser audio (playerout.wav / Sound 41); full-screen results scorecard (re_screen) displaying 4 tracked statistics per player (Score, Friendly Ants Lost, Enemy Ants Killed, New Ants Hatched).

### R3. Interactive Multi-Platform Application & Audio (ants-app)
Provide a playable, responsive application:
- Hardware-accelerated 2D viewport with integer pixel scaling and authentic 4:3 presentation.
- Complete In-Game HUD: selection card, minimap, hatch controls, egg counter, news flash banner, match clock.
- Multi-channel audio mixer routing sound effects and MIDI background music.
- Interactive end-of-game Results Scorecard modal.

## Acceptance Criteria

### Asset Decoder Verification
- [ ] Programmatic test suite parses all 2,794 sprites, 91 sound effects, and 6 map levels from ants.chd and Maps/*.LVL without crash, truncation, or memory leaks.
- [ ] Visual verification of palette accuracy and 8-way directional orientation.

### Simulation Rule Verification
- [ ] Automated headless unit tests verify:
  - Non-combat ants deal exactly 1 HP damage per hit; Combat Ants deal 2 HP and apply 4–5 tile knockback.
  - Cardinal-only placement rejects diagonal bomb and fire orders.
  - Multi-fire bounces chain damage correctly and never extinguish the fire.
  - Firewalls and bridges expire after exactly 180 seconds; bridge collapse drowns non-swimmers.
  - Idle Combat Ants autonomously intercept enemies within 3 tiles and return to post.
  - Thief infiltration dispatches Sound 58 alarm siren and deducts up to 50 points.
  - Allied teams share combined scores while raw individual player stats remain discrete in memory.
  - Match end at 0:00 triggers winner.wav for winning team and playerout.wav for losing teams.

### Playable Application Verification
- [ ] Native build runs on macOS via installed toolchain without external binary dependencies.
- [ ] User can control ants, collect food, use abilities, and experience complete game loop from match start to game over scorecard.

## Clarification — 2026-09-06T22:34:55Z

User clarification regarding bridge mechanics:
1. Universal Traversal: Once a bridge has been dug/built by a swimmer ant on a water tile, ANY ant in the game can walk across it (friendly, allied, or hostile enemy ants alike).
2. Expiration and Drowning: Bridges expire after exactly 180 seconds. If ANY non-swimmer ant (friendly or enemy) is standing on top of the bridge when it collapses, they fall into deep water and drown instantly. Only Swimmer Ants survive.
This has been recorded in Section 5.3 of GAME_REVERSE_ENGINEERING.md. Please ensure the simulation engine and verification tests enforce this.

## Directive — 2026-09-06T22:37:34Z

User directive and reverse-engineered asset specifications for Water Splash & Ant Drowning:
1. Standalone Splash Animation: `dsplash` (Animation 40 in `ants.chd`, sprites 121..125 `9splas04.bmp..9splas09.bmp`). Plays whenever an ant hits deep water. Triggers Sound 71 (`splash.wav`).
2. Ant Drowning Animation: Every non-swimmer ant has a 22-subitem drowning death sequence:
   - Worker Ant: `agdr301` (Anim 1134)
   - Fire Ant: `afdr301` (Anim 755)
   - Bomber Ant: `abdr301` (Anim 804)
   - Combat Ant: `acdr301` (Anim 941)
   - Thief Ant: `atdr301` (Anim 1130)
   - Subitem 0: Plunges into water with splash (Sprite 121), triggers Sound 71 (`splash.wav`).
   - Subitem 1: Flails in water spray (Sprite 122), triggers Sound 72 (`antdrown.wav`).
   - Subitems 2-5: Pulls ant body underwater (`9splas06..09.bmp`).
   - Subitems 6-21: Submerged ant emits rising air bubbles and foam (`9bub1.bmp..9bub3b.bmp`, sprites 1223..1227) before permanent deallocation.
3. Swimmer Ant Exception: Swimmer Ant has NO drowning animation (it is immune), instead triggering `asdi*` (Dive in, Sound 71) and swimming (`assw*`).
This is documented in Section 5.12 of GAME_REVERSE_ENGINEERING.md. Generated preview GIFs `anim_water_splash.gif` and `anim_ant_drowning.gif` are saved in the project root.

## Directive — 2026-09-06T22:52:13Z

User directive regarding repository organization & cleanliness:
1. Do not gum up the project root. Keep the root directory clean. Store all test suites, test documentation (e.g. move TEST_INFRA.md, TEST_READY.md), test artifacts, and test logs inside `tests/` or `.agents/`, not directly in the project root.
2. Provide a simple, clean, single-command runner script in the root directory (e.g. `./run_tests.sh`) so the user can easily run all tests at any time and view clear, formatted results.
Please ensure all current and future milestone agents follow this structure.
