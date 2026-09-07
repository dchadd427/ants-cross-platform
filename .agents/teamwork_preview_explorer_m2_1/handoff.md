# Handoff Report: Simulation Architecture, Grid & Public Engine APIs (M2)

**Agent Identity:** explorer_m2_1  
**Milestone:** M2 (`ants-sim`)  
**Parent Agent:** orchestrator (`a28dfa55-5a82-453d-a21b-99459a66b340`)  
**Working Directory:** `.agents/teamwork_preview_explorer_m2_1/`  
**Primary Deliverable:** `.agents/teamwork_preview_explorer_m2_1/sim_architecture_plan.md`  
**Date:** 2026-09-06  

---

## 1. Observation

Direct observations from analysis of the repository, reverse engineering specifications, and asset headers:

1. **Interface Contract Specification (`PROJECT.md:120-132`):**
   ```text
   120: ### `ants-sim` ↔ `ants-app`
   121: - `ants::sim::SimulationEngine`:
   122:   - `init(const LevelData& level, uint32_t random_seed)`
   123:   - `tick()` -> discrete advance by 1 tick (50 ms)
   124:   - `issue_order(const AntOrder& order)`
   125:   - `hatch_ant(uint8_t player_id, AntType type) -> bool`
   126:   - `propose_alliance(uint8_t from_player, uint8_t to_player)`
   127:   - `get_world_state() -> const WorldState&`
   128:   - `get_match_time_remaining_ms() -> uint32_t`
   129:   - `is_match_over() -> bool`
   130:   - `get_player_stats(uint8_t player_id) -> PlayerMatchStats` (Score, Friendly Lost, Enemy Killed, Hatched)
   131:   - `poll_audio_events() -> std::vector<AudioEvent>` (sound_id, world_x, world_y, priority)
   132:   - `poll_news_events() -> std::vector<NewsEvent>` (target_player, message_text)
   ```

2. **Discrete 20 Hz Math & PRNG Disassembly (`survey_sim.md:43-88`):**
   - Tile cell dimension is strictly 32×32 pixels:
     `tx = px / 32; ty = py / 32;` (lines 44–45).
   - Linear Congruential Generator at disassembly addresses `0x10345b0` & `0x10345c0`:
     `holdrand = holdrand * 214013 + 2531011;`
     `sim_rand = (holdrand >> 16) & 0x7FFF;` (returns 15-bit integer `[0..32767]`).
   - Seeding from command-line `latseed:<value>` or `timeGetTime()` fallback (line 90).

3. **Tile Validity Flag Bits (`survey_sim.md:193-203`):**
   - Bomb placement check (`0x10071dd`): `(tile_flags & 0x02) != 0` (`CAN_PLACE_BOMB`).
   - Fire placement check (`0x1007202`): `(tile_flags & 0x04) != 0` (`CAN_PLACE_FIRE`).
   - Cardinal-only adjacency rule: Manhattan distance `|dx| + |dy| == 1` strictly enforced; diagonal placement (`dx != 0 && dy != 0`) rejected (lines 175–192).

4. **Dynamic Alliances & Discrete Memory Preservation (`survey_sim.md:349-373`):**
   - Default FFA state: `ally_id = 4`.
   - Scoreboard aggregates combined score: `allied_score = score[player_a] + score[player_b]`.
   - Memory structs strictly track discrete individual stats (`[esi + 0xf2a]`, `[esi + 0x54f0]`), decoupling cleanly on alliance break without data loss.

5. **Match Expiry Freeze & Split Audio (`survey_sim.md:376-381`):**
   - Simulation freeze at `0:00`.
   - Winning player/team hears Sound 56 (`winner.wav`, 22 kHz, 4.67s).
   - Defeated players/teams hear Sound 41 (`playerout.wav`, 11 kHz, 0.94s). Defeated players never hear `winner.wav`.

6. **Level Asset Data Structures (`include/ants_assets/lvl_parser.hpp:18-80`):**
   - `MapCell`: `tile_index` (empty sentinel `0x7FFE`), `flags`, `properties`.
   - `AnthillSpawn`: `tile_id`, `y`, `x`, `team_id` (0=Black, 1=Blue, 2=Red, 3=Green).
   - `FoodSchedule`: `x`, `y`, `initial_delay`, `respawn_interval`, `variants` (`weight`, `tile_id`).
   - `LevelData`: `width`, `height`, `layer1_terrain(x, y)`, `layer2_item(x, y)`, `anthill_spawns()`, `food_schedules()`.

---

## 2. Logic Chain

1. **Requirement:** Guarantee cross-platform bitwise determinism and zero floating-point divergence across macOS, Linux, and Windows.
   - **Inference:** The simulation engine must prohibit `float`/`double` in all state advancement. Spatial positions, velocities, knockback vectors, and timers must be represented using 32-bit and 16-bit signed/unsigned integers.
   - **Design:** `PRNG` implements the MSVC CRT formula verified in `Ants.exe` disassembly (`holdrand * 214013 + 2531011`), providing pure integer range mapping (`rand_range`) and `latseed` parsing.

2. **Requirement:** Satisfy the `SimulationEngine` interface contract in `PROJECT.md:120-132` while decoupling simulation logic from presentation.
   - **Inference:** Client code (`ants-app`) and automated test suites require read-only inspection of the game world without having internal entity pointers exposed.
   - **Design:** Implemented PIMPL pattern in `SimulationEngine` (`impl_`), returning an immutable `WorldState` snapshot containing `AntSnapshot` structs, flattened `TileCell` grids, player statistics, and match result structs.

3. **Requirement:** Support dynamic 4-player FFA and team alliances with split end-game audio and authentic 4-stat scorecard.
   - **Inference:** HUD needs `Score[A] + Score[B]`, but underlying records must not be merged.
   - **Design:** `MatchStatsManager` maintains discrete `PlayerMatchStats` for each player (Score, Friendly Lost, Enemy Killed, Hatched) and computes display scores dynamically. At match clock `0:00`, `evaluate_victory()` classifies winners and losers, routing `winner.wav` (Sound 56) to winners and `playerout.wav` (Sound 41) to losers.

4. **Requirement:** Support original level geometries (31×31, 40×40, 60×60) with dual-layer cells and 180s active timers.
   - **Inference:** Layer 1 determines passability (Ground, Wall, Water); Layer 2 tracks dynamic items, food respawns, bombs, and 180-second countdowns for firewalls and bridges.
   - **Design:** `Grid` provides `TileCoord` and `WorldCoord` with Chebyshev and Manhattan distance functions, encapsulates `TileCell`, and integrates `ActiveFoodSchedule` directly from `LevelData::FoodSchedule`.

5. **Requirement:** Clean CMake integration conforming to the existing M1 build pattern.
   - **Inference:** `ants_sim` must compile as a static C++17 library linking `ants_assets`, with warnings enabled and position-independent code.
   - **Design:** Formulated `src/ants_sim/CMakeLists.txt`, modifications to root `CMakeLists.txt`, and `tests/test_sim/CMakeLists.txt` for `test_sim_rules`.

---

## 3. Caveats

- **PIMPL Implementation Details:** The headers designed (`sim_engine.hpp`, `grid.hpp`, `prng.hpp`, `match_stats.hpp`) define the complete public API and data contracts. The full implementation of `CAntUnit` locomotion, A* pathfinding, and knockback physics will be supplied by peer explorer `explorer_m2_2` and M2 worker agents in `src/ants_sim/`.
- **Map Boundaries:** In original levels, map borders are bordered by obstacle tiles or clamped to grid edges `[0, width-1]` and `[0, height-1]`.
- **Food Schedule Weights:** `LevelData::FoodSchedule` variants specify relative weights. When respawning food, weights must be normalized by summing all variant weights and rolling `PRNG::rand() % total_weight`.

---

## 4. Conclusion

The core simulation architecture, integer tile grid, deterministic MSVC LCG PRNG, 4-stat tracking, and master `SimulationEngine` interface have been completely specified, designed, and documented in:
`/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m2_1/sim_architecture_plan.md`

All designs strictly adhere to:
- Pure Modern C++17 with zero external dependencies.
- Pure integer arithmetic discipline (no floating-point in simulation state).
- 100% compliance with `PROJECT.md` interface contracts.
- Seamless compatibility with `libants-assets` data structures (`LevelData`).

---

## 5. Verification Method

To independently verify the architecture and header specifications:

1. **Inspect Blueprint File:**
   Review `/Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m2_1/sim_architecture_plan.md` to confirm:
   - Complete C++ source code for `include/ants_sim/prng.hpp`.
   - Complete C++ source code for `include/ants_sim/match_stats.hpp`.
   - Complete C++ source code for `include/ants_sim/grid.hpp`.
   - Complete C++ source code for `include/ants_sim/sim_engine.hpp`.
   - Complete CMake files for `src/ants_sim/CMakeLists.txt` and `tests/test_sim/CMakeLists.txt`.

2. **PRNG Output Verification:**
   Instantiate `PRNG(1)` and execute 10 iterations. Verify output matches standard MSVC CRT:
   `rand()` sequence: 41, 18467, 6334, 26500, 19169, 15724, 11478, 29358, 26962, 24464.

3. **Interface Contract Audit:**
   Compare methods in `sim_engine.hpp` against `PROJECT.md:121-132`:
   - `init(const LevelData&, uint32_t)` -> Present
   - `tick()` -> Present
   - `issue_order(const AntOrder&)` -> Present
   - `hatch_ant(uint8_t, AntType) -> bool` -> Present
   - `propose_alliance(uint8_t, uint8_t)` -> Present
   - `get_world_state() -> const WorldState&` -> Present
   - `get_match_time_remaining_ms() -> uint32_t` -> Present
   - `is_match_over() -> bool` -> Present
   - `get_player_stats(uint8_t) -> PlayerMatchStats` -> Present
   - `poll_audio_events() -> std::vector<AudioEvent>` -> Present
   - `poll_news_events() -> std::vector<NewsEvent>` -> Present

4. **Invalidation Conditions:**
   - Any introduction of floating-point types (`float`, `double`) into `TileCoord`, `WorldCoord`, `TileCell`, `PRNG`, or `SimulationEngine::tick()`.
   - Renaming or signature alterations of methods in the `ants::sim::SimulationEngine` interface contract.
