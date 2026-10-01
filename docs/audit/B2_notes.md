# Bots B2: perception and the arena (what was built, measured, and where it differs from the design)

Update for section 50 of `implementation_plan.md` (that file is not part of the repository; the owner copies this text into it). Milestone B2 of the computer players, as designed in [`docs/BOTS.md`](../BOTS.md): the complete view, the analysis of the map, the headless arena. Status: built, every suite passes. Released as v0.0.87 (version.hpp, the version test, the README and the CHANGELOG entry carry it); a review of its own found 37 points, fixed in the next release (see the CHANGELOG).

## Built

| Item | Where |
|---|---|
| `BotView` complete: the seat's own eggs and incubator, the food piles, `map()`, a borrowed `grid()` / `predict_ack()` / `has_pending_path()`, copies for everything it keeps, a copy of a view has no borrow | `include/ants_ai/bot_view.hpp`, `src/ants_ai/bot_view.cpp` |
| `MapInfo`: hills, one walking-cost field per hill on the engine's own step weights, per pile and team the approach cost and click tile, walker components, power-ups, reachable points, the trip model, `approach_now` | `include/ants_ai/map_info.hpp`, `src/ants_ai/map_info.cpp` |
| The match runner (`play_match`, `replay_commands`): pure, no threads, no clock, no files; the sink latency (a room's turn boundary), recording, the replay without a bot | `include/ants_ai/arena.hpp`, `src/ants_ai/arena.cpp` |
| `bot_arena` (CMake target, `EXCLUDE_FROM_ALL` like `map_sweep`; links `ants_ai ants_sim ants_assets Threads`): the command line, the threads, the JSON report, `--selftest` | `tools/bot_arena.cpp`, root `CMakeLists.txt` |
| The controller owns one `MapInfo` and hands it out (`BotController::map()`, `BotContext::map`, `BotView::map()`) | `bot_controller.*`, `bot.hpp` |
| Tests: AI1.1 - AI1.7 (view), AI1.8 - AI1.20 (map analysis), AI4.1 - AI4.4 (match runner), in the existing suite 2.20 (now 46 tests, 85,125 assertions); `bot_arena --selftest` is the new suite 2.21 (52 checks) | `tests/test_ai/test_ai_view.cpp`, `test_ai_map.cpp`, `test_ai_arena.cpp`, `run_tests.sh` |
| Documents | `docs/BOTS.md` ("B2 done"), README, CHANGELOG (v0.0.87), this file |

## Where this differs from the design, and why

- **The match runner is a library file, not part of the tool.** The design put all of `bot_arena` into `tools/bot_arena.cpp`. The tests (AI4.x) and the tool need the same code, so `play_match` / `replay_commands` live in `ants_ai` (no threads, files or clock: it builds for the web too) and the tool adds the command line, `std::thread`, the clock and the report. `ArenaSpec::factory` lets a test seat a bot that is not in the registry (the selftest's scripted walker, the tests' walkers).
- **`BotView::score()` stays the number of the score box** (a team's score plus its ally's, never below 0): the design wrote "the individual score", but B1's review decided (AI2.16, BOTS.md) that a view shows what the boxes show; nothing in B2 needs the individual score (nothing hatches before B5).
- **`BotView::build` takes a `const MapInfo*`** (default null) instead of a reference: B1's tests build views by hand without a map and must keep working unchanged.
- **A copy of a `BotView` drops the borrow** (empty grid, `predict_ack` 0, `has_pending_path` false). The design only said "grid() borrowed, valid inside think()"; making the copy lose the borrow means a bot that keeps a view for later cannot read a stale engine, and costs nothing.
- **The engine's walking rule is not `TileCell::is_passable`.** The design (and my first version) used it plus the queue-row flag. The conformance test AI1.9 found at once that the engine walks over a tile whose obstacle-overlay flag is set when the LVL file does not mark the tile solid (TREASURE (52, 41)): `MapInfo::walkable` now mirrors `can_enter` R1, R3, R4 and R5 (terrain, the 4 x 4 mound of any hill, `Grid::is_solid_object`, other teams' queue rows, the last two by geometry because `is_corridor_team_locked` is not set by the current grid). The design's "4,790 random pairs" agreement did not include that tile; with the old rule the test fails (mutation `overlay`, below).
- **The approach cost is exact**: the best walkable neighbour of any cell of the pile plus the engine's own step cost onto the cell (the design added a constant 20), and a pile whose cost reaches 8,000 (the path finder's limit) has no approach.
- **Piles with two objects on one anchor**: `PileInfo::cells` is empty for the object whose cells a later object took, `bite_index` says which object loses the units (found and tested in AI1.20, see below); the design only said "keep distinct indices".
- **`approach_now`** (new): the click tile of the start may no longer be food after a stage change; a bot that re-targets a worked pile asks again.
- **Suite numbers**: the view, map and arena tests are new files of the existing `test_ai` executable (suite 2.20), the arena's self-test is the next free number, 2.21.
- **Options the design did not list**: `--no-wall-time` (so that a report is bit-reproducible: wall times are in the JSON by default, as asked), `--maps-dir`, `--quiet`, `--map shipped` (all six), default seats = four standard bots at medium level, default threads 1.
- **The reference numbers of docs/BOTS.md were NOT regenerated**: `worker` and `standard` still run the idle bot, so `bot_arena` can only show zeros. The prototype numbers stay labelled as prototype measurements until B3. What `bot_arena` does reproduce is everything about the maps: lengths, reachable points, trip costs.

## Measured (release build unless stated)

| What | Value |
|---|---|
| Reachable points on foot (AI1.15), same for every hill | TINY 4800 of 4800, SMALL 3000 of 4000, MEDIUM 4900 of 4900, GAUNTLET 1500 of 1800, ISLANDS 0 of 5600, TREASURE 8850 of 10950: **identical to the design's numbers**. Unreachable piles: SMALL #0, GAUNTLET #1, TREASURE #7 - #11 (the corner anchors, with their duplicates), every pile of ISLANDS |
| Flood fill against PATHMGR (AI1.9: 300 pairs per map in the suite; 3,000 per map in a one-off run) | 0 disagreements in 18,000 + 1,800 pairs; the engine finds a path exactly when the tiles are in one component and the best cost is below 8,000; no engine path was ever cheaper than MapInfo's optimum |
| The engine's path finder is not exact (one-off run, 3,000 pairs per map) | Paths that cost more than the best: TINY 78, SMALL 151, MEDIUM 187, GAUNTLET 42, TREASURE 53, ISLANDS 0 (2.6 to 6 percent); the worst was +11.8 percent (TINY). On random terrain (AI1.14) 38 of 300, worst +1.6 percent |
| Components | ISLANDS: 21 components for every team, two of each hill's eight start ants on other islands (8 in all), none of them with a pile on its island; the other five maps keep every ant with its hill. Power-ups on foot from some hill: SMALL 2 of 2, GAUNTLET 8 of 10, TREASURE 20 of 20, ISLANDS 4 of 40 |
| Trip model (AI1.17), nearest pile of every hill (20 cases) | model against measured solo cycle: TINY +9 +16 +10 +13, SMALL +12 +8 +5 +10, MEDIUM +5 +3 +3 +12, GAUNTLET +4 +4 +3 +5, TREASURE +15 +15 +18 +22 percent (19 of 20 within 20 percent: the design's 19 of 20; the design's outlier "TREASURE team 3: model 140 against 263" is 146 against 119 in a clean solo run). Over all 144 reachable (hill, pile) pairs of the five maps: -3 to +22 percent, mean +6.3. With a constant of 45 instead of 70 every pair fits within 8 percent; the design's 70 was kept (it is what B3 was designed with, and with several workers the queue at the hill adds time) |
| The hill gate (all start ants on the nearest pile, deposits 3 to 14) | one deposit every 90 to 117 ticks on every map and hill, GAUNTLET about 200 (6 ants, a trip of 1,100 ticks), so the per-ant cycle is the number of ants times the gap |
| Walking time per unit of cost (AI1.12) | grass, sand and dirt: 0.4 tick per unit within 1 tick over 20 tiles; mud walks 7 percent faster than its weight says (356 ticks for 20 tiles where 384 are predicted) |
| `BotView::build` per look | 0.4 to 0.5 microsecond on a cached world state, 1.6 to 3.0 including the rebuild after a tick (12 to 32 ants, six maps); the test compares 48 against 500 ants (linear: about 10 times) |
| `MapInfo` build, once per match | 0.25 ms (ISLANDS) to 1.5 ms (MEDIUM): four fields, components, piles, power-ups |
| Match length in ticks (AI4.3) | TINY 7200, SMALL 9600, MEDIUM 12000, GAUNTLET 12000, TREASURE 14400, ISLANDS 14400 (the engine declares the match over 4 ticks after the clock reaches 0: 7204, 9604, 12004, 14404) |
| A full four-team idle match | 23 ms (TINY), 48 (SMALL), 128 (MEDIUM, GAUNTLET), 154 (TREASURE), 161 (ISLANDS); with `--replay-check` twice that; four threads divide the wall time |
| Default (unoptimised) build, `test_ai` | about 73 seconds in all; the new tests take about 26 seconds (37 in another run: the machine decides), mostly AI1.9 - AI1.11 (the engine pairs) and AI4.1 / AI4.2 (matches of 1,200 to 7,204 ticks); the same suite takes about 12 seconds in a release build |

## Findings about the engine (no behaviour changed)

- `TileCell::is_passable` and `is_obstacle_overlay` are not what the engine walks by; `can_enter` / `step_cost` are (see above).
- The path finder fails for a goal whose best path costs 8,000 or more (`kFailF`, tested before the goal test), also when the goal is in the same component; and it is not optimal (no decrease-key).
- `SimulationEngine::tick()` returns before `current_tick_++` on the call that ends the match, so after the end `current_tick()` is one less than the number of calls. A replay (and any "play N ticks" loop) must count calls: `RecordedCommand::step`, `ArenaResult::steps`. (The replay of a full match failed at its last call until this was found.)
- Harvest: an ant bites the FIRST object of the table that has the pile's anchor (`start_harvest`), the points of the bite are those of the object found by the clicked cell, and the cells belong to the LAST object of the table (`food_object_at_cell`). AI1.20 runs it with an ant: 4 bites take the 4 units of the first object, each earns the last object's 50 points, the last object keeps its 4 units for ever. On the shipped maps this only touches the two TREASURE corner anchors, which no hill reaches on foot.
- A pile's footprint is the footprint of its current stage tile; `approach_now` finds the cell to click now. On TINY's pile 12 (5 cells at the start) the click tile of the start is no longer food for some hill before the pile is eaten (AI1.18).
- The HUD draws an egg tray of at most nine eggs; the view gives the exact stock (a harmless deviation, noted in the header).
- `kill_unit` removes an ant at once (a test fixture): the pair tests clear the field with it, and a fresh engine per pair costs 15 to 150 microseconds.

## Verified

- Every existing suite passes unchanged (`./run_tests.sh`); the build is clean with `-Wall -Wextra -Werror -Wsign-conversion` on top of the project's `-Wpedantic -Wconversion -Wshadow`.
- A mutation check of the conformance tests: with the obstacle-overlay rule added to `walkable`, with the mound rule removed, with the other-team queue-row rule removed, with the halving of the step cost removed, with the diagonal factor 1.5 instead of 1.4, with the 8,000 limit removed from the approach, with a wrong mud weight, each change is caught by at least one of AI1.8 - AI1.19 (one by AI1.9 alone, one by AI1.19 alone, one by AI1.12 alone).
- AddressSanitizer + UBSan: `test_ai` (46 tests) and `bot_arena --selftest` run clean. ThreadSanitizer: `bot_arena --selftest` (one and four threads) and a 24-match run with `--threads 4 --replay-check` report no race.
- The replay check can fail (a command missing, changed, moved to another step, or added; a wrong hash; a wrong checkpoint: it names the tick), in both `test_ai` and `bot_arena --selftest`.

## Open

- **Emscripten**: `ants_ai` (with `map_info.cpp`, `arena.cpp` and the rest) was compiled with the web image's toolchain (the local `emscripten/emsdk:3.1.58` image, Release, `-Werror -Wsign-conversion`): no warning, no error. It uses no threads, files, clock or sockets (only `<algorithm>`, `<queue>`, `<functional>`, `<utility>`), and the tool is inside `if(NOT EMSCRIPTEN)`. The whole `docker build -t ants-beta .` and the server image were NOT run for this patch; the owner's release routine (both Docker builds, a clean-clone build, new files tracked) still applies. The new files are `include/ants_ai/map_info.hpp`, `arena.hpp`, `src/ants_ai/map_info.cpp`, `arena.cpp`, `tools/bot_arena.cpp`, `tests/test_ai/test_ai_view.cpp`, `test_ai_map.cpp`, `test_ai_arena.cpp`, `docs/audit/B2_notes.md`.
- `MapInfo` has not been run over the 586 community maps (B5): the design measured the flood fill at about 93 percent there.
- The trip model's constant (70) is a design value; a lone worker needs about 45. B3 decides whether the pile ranking wants the gate-aware cycle.
- `run_tests.bat` does not run the bot suites (neither B1's nor B2's).
- The numbers of the prototype worker bot in docs/BOTS.md are still not reproducible from the repository (B3).
