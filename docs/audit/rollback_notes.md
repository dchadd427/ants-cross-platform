# Rollback for one's own orders: what was built, measured, and where it differs from the design

## State at handoff (2026-10-04)

**Decisions** (coordinator and owner, after the measurements in R6; unchanged by R7):
- **Lead bias 0** (`Prediction::Config::lead_bias_ticks`): an order lands where the host runs it, so nothing of one's own is corrected while the lag holds and the ants answer a click as in a game on one's own computer (192 ms). A bias of 1 shows the ants 50 ms sooner than a local game but costs a whole-tile hop of an own ant in about one order in six.
- **The prediction is OFF by default (opt-in)** until the corrections are smoothed: other players' ants still hop `lead` ticks of walking (12 px on average, a tile at most) on about two of three foreign orders, and the owner does not want ants jumping about. On with `--prediction on`, the settings key `prediction=on` or `?prediction=on` on the web page's address (`?prediction=off` still works); `NetGame::set_prediction_enabled` for the tests.

**Done and committed** (R1 - R6 below, and R7, the review's fixes): the budget counts the thread's CPU time and four strikes start a cool-down (10 s, doubling to 160 s) instead of ending the prediction for the match; a match that does not ask for the prediction has none (a default match has no `Prediction` object and its cue router idles; switched off at run time it is destroyed and the HUD's special-target query goes back to the confirmed engine at once); the mutants that depend on the default were re-pointed and run again (N17, H1 - H4, the proofs for N3.25, N3.32 and PA4); `./run_tests.sh --fast` passes (the two known container-only failures of suite 5.2 aside: `test_mutate_tool` "a timeout kills what the test command started" and `test_run_tests` "a signal stops the suites that were started"); the prediction's suites and the application's network suite pass under the sanitizer (`--asan --sim` and `--asan --app`: R7 has the numbers).

**LEFT, in this order:**
1. **The web check** `tests/scripts/test_web_prediction.sh`: **run** on the release-0.4.0 tree (380b4e0) on a local-only image (its Emscripten ports built from the pinned release tags, never pushed or deployed) with that tree's `ants_server` and demo rooms, headless Chromium: **23 checks, 0 failed**: on with `?prediction=on` (12 of 12 orders predicted, a click felt in 5 ms against the network's 93.5 ms, the frame function 846 us on average), off by default and with `?prediction=off` (nothing predicted, the corner's delay is the network's), the two windows' state hashes equal in all three matches.
2. One full `./run_tests.sh` on the exact commit of the pull request: CI's (every suite on Linux, macOS and Windows, the web and the server images). The Windows path of the thread clock (`GetThreadTimes`) and the web path (`steady_clock`) compile only there; both were syntax-checked here against stand-in headers.
3. The web numbers in R6 and `docs/NETWORK_PORT.md` were measured with bias 1 (the felt delay does not depend on the lead; re-measure only if wanted); the native table with bias 0 is in R6.
4. The release's changelog line: off by default; try it with `--prediction on`, `prediction=on` in the settings or `?prediction=on`; a prediction whose work costs more than its budget (12 ms of the thread's CPU time) four times within ten seconds switches itself off for ten seconds and comes back (each further time twice as long, up to 160 s); a later release smooths the small hops of other players' ants and then may switch it on.
5. **Known and left for the smoothing work** (the review's LOW findings, also in `docs/NETWORK_PORT.md`, "What it cannot do"): a jitter buffer deeper than the lead makes an order put the picture ahead by the difference and then stand still; a change of the lead moves the display by one tick of walking, once per change.

**The next feature: smooth the corrections.** When a rebuild changes where an ant stands at the display tick (another player's order arrives), draw the ant gliding from where it was shown to where the engine now has it, over about 100 - 150 ms, instead of snapping. It belongs to the RENDERER (`src/ants_app/renderer.cpp`, which already interpolates between ticks in `predict_ant_clip`) with a small per-ant offset kept by the application when `Prediction::stats().rebuilds` changes (`set_measure_corrections` already compares the picture before and after a rebuild); the engines, the state hashes, the derived-state property and every prediction test stay as they are, and clicks still pick where the engine has the ant. Acceptance: `test_prediction --measure` shows glides, not hops (no frame in which an ant is drawn more than a few pixels beyond its walking speed); then the default may be switched on (the `NetGame` and `ApplicationConfig` defaults, README, `docs/NETWORK_PORT.md`, and the pins in N3.25, N3.32, PA4 and the default-match half of PA7).

Update for section 74 of `implementation_plan.md` (that file is not part of the repository; the owner copies this text into it). Client-side prediction with rollback of one's own commands in network matches: the confirmed lock-step simulation, the server's sealing and referee, the network protocol and the rules stay as they are. The steps land one after the other; this file grows with them (R1 the engine copy, R2 the prediction core, R3 the screen, R4 the cues, R5 the switches and the safety, R6 the lead and the measurements, R7 the review's fixes).

## R1: the engine copy (value semantics of `SimulationEngine`)

### Built

| Item | Where |
|---|---|
| `SimulationEngine` is copyable: **`explicit SimulationEngine(const SimulationEngine&)`** and `operator=`. A copy is a second engine in exactly the state of the first and shares nothing with it. The assignment keeps the memory the target already holds where the shapes fit (cells, ants, lists), so a rebuild every frame allocates next to nothing. A copy of a moved-from engine is a fresh, usable engine | `include/ants_sim/sim_engine.hpp`, `src/ants_sim/sim_engine.cpp` |
| `SimulationEngineImpl` is copied **member by member by the compiler's own operations**: a member that is added later cannot be forgotten by a hand-written copy, and one of a type that cannot be copied does not compile. Three members are types of their own that copy themselves as they must: **`AntPtr`** (the table of ants owns its ants through pointers, because an ant keeps its address while the table grows; a copy clones the ant, an assignment copies into the ant that is there; `static_assert`: nothrow move, so that the table moves its ants when it grows), **`WorldStateCache` + `StaleFlag`** (the cached `WorldState` is derived and as big as the map: a copy starts with an empty cache flagged stale, an assignment keeps the target's memory and flags it stale), and the path managers | `src/ants_sim/sim_engine_impl.hpp` |
| `PathManager` deep copy: a new pool and a copy of every queued search, in queue order, attached to the new pool. `PathSearch` holds raw pointers into the pool of its manager (`pool_`, `grid_`): its only copy is the **rebinding constructor** `PathSearch(const PathSearch&, PathGridPool&)` (the plain copy stays deleted), which the manager's copy calls. `PathGridPool` copies the cell blocks of the slots in use and **not** those of the free slots: nothing reads a free slot's content (the search that takes it zeroes it, `PathSearch::init`) | `include/ants_sim/path_planner.hpp`, `src/ants_sim/path_planner.cpp` |
| Suite 2.25 `test_engine_copy` (quick: R1.1 - R1.19 with R1.4b, 20 tests, 4,293 assertions, about 4 s) and suite 2.26 (`--whole-matches`: R1.20, six shipped maps to the end of the match, a copy re-taken every 1000 ticks, the state hash compared at **every** tick: 69,970 assertions, 33 s; under a sanitizer the first 2,500 ticks of each match). `--bench` prints the costs, `--census` what the generated matches contain. Registered with `ctest` (two entries) and in `run_tests.sh` | `tests/test_sim/test_engine_copy.cpp`, `tests/test_sim/CMakeLists.txt`, `run_tests.sh` |

### How the tests compare, and what "identical" is

The state hash is not enough: it does not cover the half-done path searches (only the queue of requests), the cues and news, the effects, the fog or the names, and the `WorldState` does not cover the A* open lists. The tests therefore compare everything that can be observed: the hash (all seven parts) after every tick, the answer of every command, the cues and news of every tick, a fingerprint of the whole `WorldState` (cells, ants, effects, score bubbles, droppers, statistics, fog, the match result), the locomotion trace, names, and the order of the path queue (every pending request: ant, start, goal, has a grid, finished). "Independent" is measured against a **twin**: an engine that was never copied and played the same match from the start (the replay is deterministic). The matches are generated commands (valid and invalid, group moves, attacks, special orders, Stop, Hatch, the alliance commands; targets on enemy ants, food, power-ups, hills, the river) from the state of the engine; a census (`--census`) shows that the whole matches have hatching (8 - 36 ants), fights (up to 16 kills) and food income on most maps (the islands map has no contact, by its design).

### Where this differs from the design, and why

- **Value semantics, not `snapshot()` / `restore()`.** Both were allowed. A copy constructor and an assignment are the idiom the code already knows, they make the tests read naturally (`SimulationEngine c(e)`, `predicted = confirmed`), and the assignment is the "restore" that reuses the target's memory. The constructor is `explicit` so that no engine is copied by accident (by value in a parameter, say): `static_assert`s in R1.1.
- **The cached world state is not copied.** The design said "copies share nothing (caches such as the world-state cache ...)". The cache is derived data and as big as the map; a copy rebuilds it on demand (`get_world_state()` of a copy costs 1 - 3 us on the shipped maps), and R1.16 pins that a copy answers for its own state whether the source's cache was fresh or stale.
- **The ant table keeps removed ants**, as before (a removed ant keeps its entry for its id, and the hash counts it); a copy keeps them too (R1.4b: a mutant that drops them is caught). The table never shrinks, so the target of an assignment reallocates all its ants only when the match has added ants beyond its capacity (each hatch can do it once).
- **No new rule in the simulation.** The engine's behaviour is untouched: `git diff` of `ants_sim` is the copy operations and two wrapper types; every golden hash and every network suite passes unchanged (`./run_tests.sh --fast`: 48 suites). Existing tests changed: none.

### Measured (Release build; `./test_engine_copy --bench`)

Microseconds per operation: the BEST of 300 runs (a busy machine only makes a run longer, so the best is the cost; this Mac was shared with other builds during the measuring), the range over five points of a match (ticks 0, 100, 400, 1500, 4000; matches with the generated orders). "copy-assign" is the rebuild of a prediction (into an engine that already holds the same map).

| Map (cells, ants) | copy-construct native | copy-construct wasm | copy-assign native | copy-assign wasm | state hash native | world state native / wasm |
|---|---|---|---|---|---|---|
| TINY (31 x 31, 12 - 24) | 1.4 - 14 | 2.3 - 31 | 0.7 - 5.9 | 1.3 - 19 | 39 - 64 | 0.9 - 2.5 / 1.0 - 1.3 |
| SMALL (40 x 40, 16 - 24) | 1.6 - 13 | 1.4 - 38 | 1.0 - 5.4 | 1.2 - 24 | 65 - 94 | 1.2 - 3.4 / 1.2 - 1.3 |
| MEDIUM (60 x 60, 24 - 47) | 3.1 - 17.5 | 3.2 - 39 | 2.9 - 8.7 | 2.9 - 24 | 138 - 149 | 2.6 - 2.8 / 2.6 - 3.0 |
| ISLANDS (60 x 60, 32 - 48) | 3.4 - 23.5 | 3.5 - **170** | 2.5 - 16.7 | 3.0 - 103 | 140 - 144 | 2.6 - 3.0 / 2.9 - 3.1 |
| GAUNTLET (60 x 60, 24 - 48) | 2.9 - 17.5 | 3.2 - 41 | 2.2 - 8.2 | 2.8 - 25 | 136 - 148 | 2.5 - 2.8 / 2.6 - 3.2 |
| TREASURE (60 x 60, 24 - 60) | 3.5 - 18.6 | 3.6 - 40 | 2.4 - 8.8 | 3.0 - 25 | 137 - 152 | 2.6 - 3.0 / 2.7 - 3.2 |
| synthetic 256 x 256 (the largest grid the engine hosts), 200 ants walking | 129 | 116 | 119 | 111 | not measured | 40 / 40 |

(wasm: the same program compiled with Emscripten 3.1.58 at `-O3 -DNDEBUG` like the web build, run with node 22 / V8 12.4 on the same machine. The first `get_world_state()` of a new copy costs 1.5 - 3.8 us in both.)

**The target of the design (a copy under 0.2 ms on the shipped maps, native and wasm) is met**: at most 24 us native and 170 us wasm (ISLANDS late in a match, the worst case), 0.13 ms on the largest grid. The wasm cost is dominated by the ALLOCATOR, not by moving bytes: a copy-construct makes 230 - 375 heap allocations (TINY 272, SMALL 234, MEDIUM 316, ISLANDS 253, GAUNTLET 337, TREASURE 375: the ants, the food objects' lists, the queued path searches and their open lists), each of which is about five times slower in wasm; compiling with `-mbulk-memory` changed nothing (165 us against 170 us on ISLANDS). A copy ASSIGNED into an engine of the same shape allocates next to nothing except the queue of path searches, which is rebuilt object by object (`PathManager::operator=`): if the budget of R5 ever needs it, that queue can be assigned in place. Memory of one copy (heap bytes in use with 64 copies held, per copy, at tick 1500): TINY 124 KiB, SMALL 154, MEDIUM 244, ISLANDS 372, GAUNTLET 343, TREASURE 271 (the cells alone are 37 - 140 KiB; the occupancy grid, the fog, the ants and the grids and open lists of the searches in flight make up the rest).

**What the copy does NOT make cheap is the ticks that the second engine runs.** The cost of one tick over 4000 ticks of a match with many orders (a 40 percent chance of an order on every tick, which is far more than a person gives; every order starts an A* search, and a tick that runs a slice of the path manager, up to 1000 expansions, costs a hundred times a quiet one), in microseconds, native (wasm is 1.2 - 1.4 times as much, p99 and max noisier):

| Map | mean | median | p90 | p99 | max |
|---|---|---|---|---|---|
| TINY | 22 - 30 | 9 - 12 | 56 - 82 | 200 - 230 | 380 - 540 |
| SMALL | 48 - 63 | 15 - 19 | 134 - 183 | 305 - 465 | 650 - 1,210 |
| MEDIUM | 181 - 234 | 148 - 178 | 400 - 540 | 650 - 910 | 880 - 1,460 |
| ISLANDS | 32 - 50 | 31 - 49 | 61 - 103 | 89 - 156 | 120 - 385 |
| GAUNTLET | 291 - 447 | 270 - 406 | 614 - 972 | 940 - 1,510 | 1,240 - 2,070 |
| TREASURE | 178 - 267 | 131 - 190 | 413 - 640 | 713 - 1,090 | 951 - 1,600 |
| synthetic 256 x 256, 200 ants | 1,810 - 2,260 (one tick, after three group orders; wasm 2,200) | | | | |

The consequence for the next steps: a prediction that is advanced `lead` ticks (3 - 6 at a round trip of 60 ms) costs the copy (at most 0.02 ms native, 0.17 ms wasm) plus `lead` ticks of the busy kind (about 1 ms each at the heaviest, about 10 - 50 us at a person's pace of orders), and the largest grids may need milliseconds: that is the budget of the fallback of R5, and it is why a prediction that is up to date is advanced by one tick instead of rebuilt (R2).

### Mutants (each is a temporary edit of the source that the quick suite must fail; the table is what the harness printed on the final tests)

A mutant that no test kills would be a hole, so there were three at first (a copy that drops removed ants, one that forgets the audio-owner counter, one that forgets the battle clouds); R1.4b was written for them, and all 35 are killed now. "crash" means that the program died with a segmentation fault in that test (the half-done searches of a mutant that shares or mis-sizes a pool): a failure of the suite all the same. The harness (`run_mutants.py`, not part of the repository) must delete the objects before every build: Apple's `make` compares whole seconds, so a source that is restored within the second of the object that was built from its mutant is not rebuilt, and the stale mutant poisons every run after it (the first complete run reported crashes in R1.4 for harmless mutants; the unmutated tree was then rebuilt from scratch and passed). It also builds and runs the unmutated tree before the first mutant, after every fifth and at the end (all green).

| Mutant | What it does | Result |
|---|---|---|
| `P1_search_shares_pool_and_grid` | a copied search keeps pointing at the source's pool and cell block (no rebinding) | KILLED by R1.4, R1.6, R1.7, R1.8, R1.10, R1.11, R1.14, R1.17, R1.18, R1.19 |
| `P2_search_loses_open_list` | a copied search starts with an empty open list (heap_) | KILLED by R1.4, R1.6, R1.8, R1.11, R1.14, R1.17, R1.18, R1.19 |
| `P3_search_loses_its_slot` | a copied search forgets which grid slot it holds (restarts its search) | KILLED by R1.2, R1.4, R1.5, R1.6, R1.8, R1.11, R1.14, R1.17, R1.18, R1.19 |
| `P4_pool_cells_not_copied` | the pool copy zeroes the cells of the slots in use | KILLED by R1.4, R1.6, R1.8, R1.11, R1.17, R1.18, R1.19 |
| `P5_pool_free_flags_not_copied` | the pool copy marks every slot free | KILLED by R1.4, R1.6, R1.11, R1.17, R1.18, R1.19 |
| `P6_manager_copy_drops_queue` | the manager copy has the pool but no searches | KILLED by R1.2, R1.4, R1.5, R1.8, R1.17, R1.18, R1.19 |
| `P7_manager_copy_reverses_queue` | the manager copy queues the searches in reverse order | KILLED by R1.2, R1.4, R1.8, R1.17, R1.18, R1.19 |
| `P8_manager_assign_keeps_pool_state` | the manager assignment keeps the target's own pool state (slot flags and cells) | KILLED (crash, signal 11, in R1.4) |
| `E1_ant_assign_keeps_old_ant` | assigning an ant pointer over an existing ant leaves the old ant | KILLED by R1.2, R1.11, R1.12, R1.13, R1.14, R1.16 |
| `E2_ant_copy_drops_removed_ants` | a copied ant pointer drops the ants that are removed (their entries stay in the table of the original) | KILLED by R1.4b |
| `E3_stale_flag_not_set_by_assignment` | an assignment into an engine does not mark its cached world state stale | KILLED by R1.13, R1.16 |
| `E4_stale_flag_copied_with_empty_cache` | a copy takes the source's 'fresh' flag but not its cache | KILLED by R1.4b, R1.12, R1.16 |
| `C1_copy_forgets_cosmetic_prng` | hashed: the death-clip generator | KILLED by R1.2, R1.4b, R1.4, R1.5, R1.8, R1.9, R1.12, R1.15 |
| `C2_copy_forgets_effects` | presentation: the visual effects | KILLED by R1.2, R1.4b, R1.4 |
| `C3_copy_forgets_score_bubbles` | presentation: the score bubbles | KILLED by R1.2, R1.4, R1.15 |
| `C4_copy_forgets_audio_queue` | presentation: the queued cues | KILLED by R1.2, R1.4b, R1.5, R1.8, R1.15 |
| `C5_copy_forgets_news_queue` | presentation: the queued news | KILLED by R1.2, R1.4b, R1.5, R1.8, R1.15 |
| `C6_copy_forgets_fog` | presentation: the revealed tiles | KILLED by R1.5 |
| `C7_copy_forgets_viewer` | presentation: the seat that looks (fog) | KILLED by R1.5 |
| `C8_copy_forgets_names` | presentation: the players' names | KILLED by R1.2 |
| `C9_copy_forgets_trace` | diagnostics: the locomotion trace | KILLED by R1.5 |
| `C10_copy_forgets_audio_owner_counter` | presentation: the owner ids of effect sprites | KILLED by R1.4b |
| `C11_copy_forgets_battle_clouds` | presentation: the dust balls | KILLED by R1.4b |
| `C12_copy_forgets_request_serials` | hashed: the path request serials | KILLED by R1.2, R1.4, R1.5, R1.8 |
| `C13_copy_forgets_hatching` | hashed: the eggs in the incubator | KILLED by R1.2, R1.4, R1.5, R1.8, R1.9, R1.12, R1.15 |
| `C14_copy_forgets_occupancy` | hashed: the occupancy grid | KILLED by R1.2, R1.4b, R1.4, R1.5, R1.8, R1.9, R1.12, R1.15 |
| `C15_copy_forgets_audio_tracked` | presentation: which ants own a sound (their stop cues) | KILLED by R1.4 |
| `C16_copy_forgets_trace_switch` | diagnostics: the trace is on | KILLED by R1.5 |
| `C17_copy_forgets_flower_droppers` | hashed: the flower droppers | KILLED by R1.2, R1.4, R1.8, R1.9, R1.12 |
| `C18_copy_forgets_roster` | hashed: the roster mask | KILLED by R1.5 |
| `A1_assign_keeps_effects` | assignment keeps the target's visual effects | KILLED by R1.2, R1.4b, R1.4, R1.14 |
| `A2_assign_keeps_names` | assignment keeps the target's names | KILLED by R1.2, R1.11 |
| `A3_assign_keeps_audio_queue` | assignment keeps the target's queued cues | KILLED by R1.2, R1.4b, R1.6, R1.11, R1.12, R1.13 |
| `A4_assign_keeps_fog` | assignment keeps the target's revealed tiles | KILLED by R1.2, R1.4b, R1.4, R1.6, R1.8, R1.11, R1.14 |
| `A5_assign_keeps_path_serials` | assignment keeps the target's request serials | KILLED by R1.2, R1.4, R1.6, R1.8, R1.11, R1.12, R1.13, R1.14 |

## R2: the prediction core (`net::Prediction`, the lock-step runner's two hooks, the NetGame wiring)

### Built

| Item | Where |
|---|---|
| **`net::Prediction`**: owns the predicted engine (`SimulationEngine pred_`, a copy of the confirmed engine) and keeps it equal to a **derivation**: the confirmed engine, plus the turns that are already in hand (the jitter buffer: sealed by the host, not run yet: they are KNOWN commands of every player and are run as such, nothing about them is guessed), plus the player's own orders that no turn has carried yet, applied at the tick at which the prediction stands, advanced to the **display tick** `D = confirmed tick + lead`. Only the ticks beyond the turns in hand are guesses (and the only guess is that nobody else gives a command). No SDL, no clock, no sockets: three inputs (`submit`, `on_turn`, `on_tick`) and the engine out | `include/ants_net/prediction.hpp`, `src/ants_net/prediction.cpp` |
| **Incremental, not rebuilt every frame.** `on_tick` (one call per tick that the confirmed engine executes) advances the predicted engine by one tick. It keeps a log of the commands that it applied at every tick from the confirmed one to `D`; every live turn that arrives for a tick it has already started is compared with the log. Equal (no command, or the player's own orders at the tick where they were applied): nothing happens. Different (another player's command, an own order sealed at another tick, a kind that is not predicted): the prediction is marked stale and the next time anybody asks for the engine (a frame, an order, the next tick) it is rebuilt: copy of the confirmed engine, replay of the ticks to `D` with the turns as they are by then. That is the whole correction. A rebuild costs a copy (at most 24 us native, 170 us wasm, R1) and `lead` ticks | `prediction.cpp` |
| `LockstepRunner::set_on_turn` (a hook for every LIVE turn the runner queues, not the replayed ones) and `LockstepRunner::queued_turn(n)` (the turn in hand for tick `n`, read only). Nothing else of the runner changed | `include/ants_net/lockstep.hpp`, `src/ants_net/lockstep.cpp` |
| **NetGame wiring**: `submit` sends the order first and then hands it to the prediction, whose engine says what the engine says (the ant that answers, the ants that needed the order); the runner's tick hook runs the prediction's tick BEFORE the application's tick hook (the application reads what is shown); `refresh_prediction()` once per update: off when the user turned it off (`set_prediction_enabled`), the application suspends it (`set_prediction_suspended`), the match is not in its Playing phase, it is out of sync, the client session is not in its Normal mode (a host change, a rejoin) or is catching up, or the runner is held (a pause); `view_engine()` is the engine that the screen shows (the predicted one while `predicting()`, else the confirmed one); the lead follows `expected_command_delay_ms()` (the measured delay of the last orders, else half a seal, the round trip and the jitter buffer); `runner()` is public for the tests | `include/ants_net/netgame.hpp`, `src/ants_net/netgame.cpp` |
| What is predicted: group moves, group special orders, group attacks and Stop. NOT predicted: Hatch (a new ant needs an id that only the confirmed stream gives), the alliance commands, Quit and Drop. A command of another player is never predicted | `Prediction::predicts` |
| Suite 2.27 `test_prediction` (quick: 20 tests, a rig that plays the server and an oracle engine, about 15 s) and N3.25 - N3.29 in `test_netgame` (real sockets, three machines) | `tests/test_net/test_prediction.cpp`, `tests/test_net/test_netgame.cpp`, `run_tests.sh` |

### What the tests hold the prediction to

- **Exactness**: with the lead of the scenario and nobody else giving commands, the predicted engine equals the ORACLE (a second engine that runs every turn the moment the server seals it) at every frame, is never rebuilt, and an order stands at the tick the turn gives it (RP2.1: 3 buffers, 3 lags, 2 uplinks each). With a jitter buffer deeper than the lead the turns in hand are certain input: whatever the other players do (hatching and the alliance commands too) the predicted engine is the oracle with no rebuild and no guess (RP2.2).
- **Corrections cost what they should**: another player's command costs one rebuild when its turn arrives and the picture is right from then on (RP3.1: the ants that move in the correction are the other player's, by a few pixels); an own order sealed earlier than it was applied costs one rebuild, later two (its tick passes without it, then its turn comes), unless it lands where the first miss put it, and the classification of every rebuild (an own order's timing, a turn that passed without it, another player's command) is pinned per shift (RP3.2); an order that the server never seals is dropped after `pending_timeout_ticks`, every turn that passes it costs at most one rebuild until then, and the frame of the drop is already right (RP3.3, with the derivation check at every frame); the first of two orders that is lost does not hold up the second (RP3.4). A lead that is too low is chased turn by turn: one rebuild for each turn that passes the order, exactly `uplink + lag` of them (RP2.4, below).
- **Derived state** (the property that makes it safe): the predicted engine that was advanced tick by tick equals the engine that a rebuild would make now (`derived_hash`), at every frame, under random jitter, stalls, bursts, orders of every kind from every seat and a lead that changes (RP4.1: 24 random matches; RP2.4, RP3.3 and RP5.5 check it at every frame).
- **Convergence**: after a burst of random commands from every seat and some quiet turns, the prediction is the oracle again (RP4.2).
- **No effect on the match**: the same match with and without the prediction has the same confirmed state at every tick, the same turns and the same cues (RP4.3); every network suite and golden hash passes unchanged.
- **States**: suspended the prediction is gone at once, resumed it begins at the next tick (RP5.1); the end of the match (RP5.2); the cues and news of the predicted ticks are kept once, stamped with the tick and a generation (a rebuild's replay is a later one), bounded (RP5.3), and a rebuild does not turn what the confirmed engine has queued into predicted cues (RP5.4); a tick that no hook announced makes the prediction stale (RP5.5).
- **In real NetGames** (N3.25 - N3.33): off by default in every machine of a match (opt-in) and then there is no prediction at all, on in the machines that were asked to (made when the match plays, destroyed when switched off); orders from every seat; every confirmed engine ends identical; an order given through the HUD's sink is in the view engine in the same call and in the confirmed engine at least a turn later; the switch works at run time; a host change switches it off while the guests elect and it begins again under the new host; the application's switch and a held runner (a pause) turn it off and it begins again at the next tick; a machine whose match went out of sync shows the confirmed engine.

### Mutants (each is a temporary edit of the source that the suites must fail)

`Q1 - Q20` edit `prediction.cpp` and are run against `test_prediction`; `N1 - N12` edit `netgame.cpp` and are run against the prediction cases of `test_netgame` (`ANTS_TEST_FILTER=Prediction:`). The harness rebuilds from nothing for every mutant and runs unmutated baselines between them (R1's lesson). The first run had four survivors in the Q list and two in the N list; the tests written for them are RP2.4 (a lead shorter than the buffer: the order must be put behind the turns in hand, chased, never run twice), RP3.2's pinned classification, RP3.3's per-frame derivation check, RP5.4 (events of the confirmed engine's queue), RP5.5 (a tick no hook announced), N3.28 and N3.29. The N list's first two survivors (the election and the pause) were two terms of the gate that the other terms repeat (a client session that elects is not in its Normal mode; a session that is paused holds its runner): the redundant terms are gone and the mode and the runner's `held` are what N3.27 and N3.28 test. The first run also found a bug, not a survivor: RP3.3 showed that a lost order was timed from the tick it had last been put at, and the chase moves that tick on at every miss, so it was never dropped (`Pending::born`, mutant `Q4`).

| Mutant | What it does | Result |
|---|---|---|
| `Q1_no_equality_check` | a turn that arrives for a tick the prediction has started is not compared with what it assumed | KILLED by RP2.3, RP2.4, RP3.1, RP3.2, RP3.3, RP4.1, RP4.2, RP5.3, RP5.4 |
| `Q2_own_timing_not_noticed` | an own order that is sealed at another tick than it was applied is not a mismatch | KILLED by RP3.2 |
| `Q3_pending_never_matched` | the own orders of a turn do not leave the waiting list | KILLED by RP2.1, RP2.3, RP2.4, RP3.1, RP3.2, RP3.4, RP4.1, RP4.2, RP5.5 |
| `Q4_timeout_from_tick` | a lost order is timed from the tick it was last put at (the bug that the first run of the tests found): a chase never ends | KILLED by RP3.3 |
| `Q5_no_reassign_after_miss` | an order that a turn missed stays where it was (a rebuild a turn until it is sealed) | KILLED by RP3.2 |
| `Q6_known_turns_not_run` | the turns in hand are not run as known commands (only the waiting orders are applied) | KILLED by RP2.2, RP3.1, RP3.2, RP3.4, RP4.1, RP4.2 |
| `Q7_rebuild_forgets_orders` | a rebuild does not apply the waiting orders | KILLED by RP2.3, RP2.4, RP3.2, RP4.1 |
| `Q8_lead_ignored` | the display tick is one tick ahead whatever the lead | KILLED by RP1.3, RP2.1, RP2.3, RP3.1, RP3.2 |
| `Q9_submit_not_advancing_to_known_turns` | an order is applied behind a turn that is in hand | KILLED by RP2.4, RP4.1 |
| `Q10_suspend_does_not_stop` | suspending leaves the predicted engine running | KILLED by RP5.1 |
| `Q11_log_not_popped` | the log of assumed commands is not advanced with the confirmed tick | KILLED by RP2.1, RP2.3, RP2.4, RP3.1, RP3.2, RP3.3, RP4.1 |
| `Q12_lead_falls_at_once` | the lead follows the delay down at once | KILLED by RP1.3 |
| `Q13_lead_rises_slowly` | the lead rises only after a delay | KILLED by RP1.3 |
| `Q14_confirmed_events_leak` | a rebuild keeps what the confirmed engine had queued (its cues become the prediction's) | KILLED by RP5.4 |
| `Q15_lost_orders_not_counted_as_stale` | a lost order is dropped without rebuilding | SURVIVED |
| `Q16_unmatched_prefix_kept` | orders before the one that a turn carried (lost ones) are not dropped | KILLED by RP3.4 |
| `Q17_foreign_not_counted` | a command of another player is no mismatch (an empty list is assumed to be right) | KILLED by RP2.3, RP3.1, RP4.1, RP4.2, RP5.3, RP5.4 |
| `Q18_stale_on_tick_not_marked` | a tick that the prediction was not told of does not make it stale | KILLED by RP5.5 |
| `Q19_engine_not_rebuilt_when_stale` | engine() hands out the stale engine | KILLED by RP2.4, RP3.1, RP3.2, RP3.3, RP4.1, RP4.2, RP5.3, RP5.4, RP5.5 |
| `Q20_events_not_captured` | the cues of the predicted ticks are not kept | KILLED by RP5.3, RP5.4 |

| Mutant | What it does | Result |
|---|---|---|
| `N1_submit_not_predicted` | NetGame::submit does not hand the order to the prediction | KILLED by N3.25, N3.26, N3.27 |
| `N2_tick_hook_missing` | the runner's tick is not forwarded to the prediction | KILLED by N3.25, N3.26, N3.27, N3.28, N3.29 |
| `N3_turn_hook_missing` | the runner's turns are not forwarded to the prediction | KILLED by N3.25 (rerun with a compiling form of the same edit) |
| `N4_session_mode_not_a_suspension` | a host change, a rejoin (a session that is not in its Normal mode) does not switch the prediction off | KILLED by N3.27 |
| `N5_view_is_confirmed` | view_engine() always answers with the confirmed engine | KILLED by N3.25, N3.26, N3.27, N3.28 |
| `N6_switch_ignored` | the user's switch does not turn the prediction off | KILLED by N3.25, N3.26 |
| `N7_no_prediction_made` | the prediction is never created | KILLED by N3.25, N3.26, N3.27, N3.28, N3.29 |
| `N8_held_not_a_suspension` | a held match (a pause) keeps predicting | KILLED by N3.28 |
| `N9_desync_not_a_suspension` | a machine that is out of sync keeps predicting | KILLED by N3.29 |
| `N10_app_switch_ignored` | the application's switch does not turn the prediction off | KILLED by N3.28 |
| `N11_wrong_seat` | the prediction is made for seat 0 whoever the player is | KILLED by N3.25, N3.26, N3.27 |
| `N12_catching_up_not_a_suspension` | a machine that is catching up (a dedicated server's lag policy) keeps predicting [reachable only against a server] | SURVIVED |

Two mutants survive, for reasons that are not holes in the tests:

- **Q15** (a lost order is dropped without marking the prediction stale) is equivalent in what can be seen. The order is chased: every turn that passes it without it re-places it at the display tick (a rebuild), so when it is dropped the prediction has just been rebuilt with the order applied at the display tick and no tick run after it (the state hash does not see an order that has not made its first step), and the very next turn compares the log (which still has the order there) with a turn that has not, finds the difference and rebuilds. The picture is wrong for the rest of one step in a way that no frame can show. The code keeps the explicit `stale_ = true`: it is what is true, and it does not wait for the next turn.
- **N12** (a machine that is catching up keeps predicting): `ClientSession::catching_up` is the lag policy of a dedicated server (a backlog of 3 s or more is run down at up to four times the speed); no NetGame test has a server's policy to produce it. The flag is tested in the session suites, the gate is one term of an OR, and the web run against a local server (the last step of this work) is where it can be seen.

### Where this differs from the design, and why

- **Rebuild only when a confirmed turn disagrees; advance tick by tick otherwise** (the coordinator's go message): the predicted engine is not rebuilt every frame. R1 measured that a copy is cheap and the ticks are not.
- **An own order is put at the display tick, but never behind a turn in hand.** Such a turn was sealed before the order existed. When the lead is too low (a delay that grew, or the first orders of a match, before the delay has been measured), the order is put at the first tick whose turn has not arrived and is chased turn by turn until its own turn comes; a rebuild for each turn that passes it, `uplink + lag` of them. The ants of a chased order have started to walk and then hover (their picture is the confirmed one, which is always right) until the lead has caught up. The lead rises at once when the measured delay rises and falls only after 40 ticks (2 s) of a lower one, so the picture does not move back and forth with the phase of the server's seals. A prediction that learns from its own misses (a missed order raises the lead by itself) is possible and is left for the measurements of R6 to ask for.
- **The gate is the sessions' own state, not a list of their flags.** `held` (a pause), `mode != Normal` (a host change, a rejoin), `catching_up` and `desynced`, the user's switch and the application's.

## R3: the match screen reads the predicted engine (`Application::view_sim`)

### Built

| Item | Where |
|---|---|
| **`Application::view_sim()`**: the engine that the match screen shows. In a match of the network it is `NetGame::view_engine()` (the predicted engine while the prediction is on, else the confirmed one); in every game of one machine it is the confirmed engine. One call may rebuild a predicted engine that a turn has shown to be wrong, so a frame asks once. `Application::sim()` stays the confirmed engine | `include/ants_app/application.hpp`, `src/ants_app/application.cpp` |
| What reads the shown engine: **the frame** (`render_world`: the world and the grid; `HUD::render`: the panel, the minimap, the selection markers), **the HUD's step of every tick** (`HUD::update` in `post_tick`: the selection's status text, the alliance flag, the pick-up edge), **the HUD's input** (key, motion, press, release: what a click picks, the group order and its feedback, the rubber band, the minimap), **the cursor** (`evaluate_cursor` and its special-target question). The HUD's own code reads nothing but `grid()`, `get_world_state()`, `get_unit()` and `ant_type()` of the engine it is given and sends the player's commands through the sink (`NetGame::submit`), so that is all there is to it; `HUD::pointer_click` and `pointer_right_click` ask the special-target question of the engine whose world the click picks from | `application.cpp`, `src/ants_app/hud_input.cpp` |
| What stays the CONFIRMED engine's: the news (`poll_sim_events`: told once, on the confirmed stream), the cues (until R4), the scorecard and the end of the match, the hashes (the web page's sync hash), the bots, the names. `HUD::current_cursor()` is new (the cursor that the last evaluation chose) | `application.cpp`, `include/ants_app/hud.hpp` |
| Ant ids are stable: the predicted engine is a copy of the confirmed one, nothing in it hatches (Hatch is not predicted: a new ant appears when its turn is run, with the id that the engine gives it), so a selection by id means the same ant in both | (by construction; PA1 selects by clicking on the shown ant and the id is the confirmed engine's) |
| Suite 3.22 `test_prediction_app` (quick, about 4 s): a headless Application hosts a room, a bare machine joins, SMALL.LVL, a real match over loopback | `tests/test_app/test_prediction_app.cpp`, `tests/test_app/CMakeLists.txt`, `run_tests.sh` |

### What the tests hold the application to

- **PA1**: the application predicts; `view_sim()` is the NetGame's view engine, ahead of the confirmed one by the lead; a right click on open ground with an own ant selected puts the order in the shown engine in the click's own call (and in the confirmed engine a few ticks later); the host then stops sealing and the turns in hand run (no tick can run between two frames: a tick would move an ant in the confirmed engine and prove nothing); **a click at the edge of the shown ant's box, away from where the confirmed engine has the ant, picks it** (the confirmed engine has nothing there); the pointer over it gets the selection cursor; with nothing selected and the pointer away, the frame drawn is the same frame when drawn again, and **the frame drawn after the switch is off (the confirmed engine) differs where the ant stands**; the click that picked the ant now finds nothing and the cursor is the plain one.
- **PA2**: the HUD's step of every tick reads the shown engine: the alliance that a turn in hand makes is in the HUD (`is_on_team`) at every step at which the shown engine has it and the confirmed one has not (a window of several 10 ms steps), and the team's news flash reaches the chat log once, from the confirmed engine (the predicted engine's own queue is empty: the prediction took it).

### Mutants (A1 - A7 edit `application.cpp`; run against `test_prediction_app`)

| Mutant | What it does | Result |
|---|---|---|
| `A1_render_confirmed` | the frame is drawn from the confirmed engine | KILLED by PA1 |
| `A2_mouse_down_confirmed` | a button press is handled against the confirmed engine's world | SURVIVED |
| `A3_mouse_up_confirmed` | a button release (where a click happens) is handled against the confirmed engine's world | KILLED by PA1 |
| `A4_hud_step_confirmed` | the HUD's step of every tick reads the confirmed engine's world | KILLED by PA2 |
| `A5_news_from_view` | the HUD polls the news of the shown (predicted) engine, whose queue the prediction has emptied | KILLED by PA2 |
| `A6_view_always_confirmed` | view_sim() always answers with the confirmed engine | KILLED by PA1, PA2 |
| `A7_cursor_confirmed` | the cursor is evaluated against the confirmed engine's world | KILLED by PA1 |

The first run of the harness had A1 and A5 survive. A1 (the frame drawn from the confirmed engine) survived because the test compared two frames that differed in more than the engine (the selection marker, the pointer, and a tick that `pump_network(0)` could run); the test now freezes the match and compares two frames of one user interface. A5 (the HUD polls the shown engine's news) survived because the line that the test looked for is the HUD's own ("Game started"); it now looks at the news flash of the new team, which only the engine makes. The survivor that remains, **A2**, is equivalent: a press of a button reads the engine's grid for its size (both engines have the same grid) and decides nothing from the ants; the pick happens at the release (A3).

### What this does not do (R4 and R5 follow)

The cues are still the confirmed engine's: a sound is heard when the confirmed engine makes it, `lead` ticks after the picture shows it. The picture of other players' things (a fight, an explosion) is the prediction's, a few ticks ahead, and its sound arrives with the confirmed tick. R4 decides which cues can come from the predicted engine.

## R4: who plays which cue, once (`net::CueRouter`)

### What the engine's sounds are (a finding that shaped this step)

The design spoke of "movement sounds" and "order acknowledgements" from the prediction. **Walking has no sound in this game.** The walk and idle clips of every ant type, on every terrain, loaded or carrying, carry no frame sounds (a probe over `walk_clip`, `idle_clip`, `action_clip`, `swim_clip`, `dive_clip`, `climb_clip`, `cant_go_clip` and `bump_clip` finds sounds only on the action clips: harvest, attack, hit, blown, burn, stun, drown, ignite, extinguish, the bridges, plant, defuse, infiltrate, the power-up pick-up, the dive, the can't-go clip and the bump). The voices of an order ("On my way!", the pedestal's click) are **not the engine's**: the HUD plays them at the click, from the verdict that the predicted engine gives (R3: its `ack_ant` and `needing_order` are the engine's own), so they were instant before this step too. What the engine makes of an own order are the cues of the ant's own ACTION: refused (the can't-go cue), harvests, picks up a power-up, lights or puts out a fire, defuses, shovels a bridge, raids, bumps.

### Built

| Item | Where |
|---|---|
| **`CueRouter`** (no SDL, no clock): two inputs, the cues of the predicted engine (`Prediction::take_audio`) and the cues of the confirmed engine's tick, and what to play out. **The own ants' own actions and their stops** (the list in `is_own_action_sound`: can't go, harvest and grab, the two power-up chimes, fire, bomb, bridge and raid cues, the bump, and the stop of any own ant, whatever it cuts) are played from the predicted engine, when it first runs the tick that makes them, in step with the picture; **everything else is the confirmed engine's, as it was**: combat, hits, blows, stuns, drownings, explosions, scores, the alliance cues, the clock's warnings, the can't-hatch cue, the hatching, every cue of another player's ants. Nothing in the second list is ever a guess: a cue that was played for something that then did not happen can only be one of the first | `include/ants_net/cue_router.hpp`, `src/ants_net/cue_router.cpp` |
| **What a rebuild replays is never played from the predicted engine.** `Prediction::PredictedAudio::replay` (new) is true for the cues that a rebuild's replay made: the ticks that the predicted engine had run before, so a correction cannot make a cue heard twice. Only what the engine makes by running on from its display tick is played, and no earlier run has made a tick beyond it. What the corrected timeline makes later is made again by the confirmed engine and played then, late but never wrong. A tick that is run again by a straight run (the prediction was suspended and began again within its lead) is caught by an exact guard (`repeats_dropped`) | `prediction.hpp`, `prediction.cpp` (`capture_events`), `cue_router.cpp` |
| **The confirmed engine's copy of what was played is dropped, one for each**: the nearest occurrence of the same kind (the same sound, or a stop, of the same ant) within `kMatchWindowTicks` = 12 (the longest lead) of the tick, the same tick when the prediction was right, a few off when an order was sealed later or earlier than assumed. An occurrence that nobody played is played (an own cue is never lost), and one that was played and not met by the time its window has passed is counted a phantom (`Stats`) | `cue_router.cpp` |
| **In the application** (`Application::post_tick`): the predicted engine's cues first, then the confirmed engine's, through the same mixer call that took the confirmed ones; the predicted engine's news are never told (the confirmed engine's are, once); a background step makes no sound from either stream; the router is reset with every match. `Application::cue_router()` is public for the tests | `application.cpp`, `application.hpp` |
| Tests: RP6.1 - RP6.5 in suite 2.27 (`test_prediction`) and PA3 in suite 3.22 (`test_prediction_app`) | `tests/test_net/test_prediction.cpp`, `tests/test_app/test_prediction_app.cpp` |

### What the tests hold the router to

- **RP6.1** the list: every sound of the own-action list, no other (28 sounds that are not: blows, hits, stuns, explosions, scores, alliance, clock, siren, hatching), another player's ants' cues, a cue without an owner and an effect sprite's owner are not the class, the bump (no owner) is, the stops of own ants are.
- **RP6.2** every rule on explicit events: the first appearance plays in order, a rebuild's replay plays nothing (flagged, 6 events), the same tick run again by a straight run plays nothing, the confirmed engine's copies are dropped one for each, an occurrence that nobody played is played (the third harvest that only the replay made), a copy 3 ticks off is met and one beyond the window is another cue, the nearest is the one that is met, a cue that was never met is a phantom exactly when its window has passed, the kinds are kept apart, `reset`.
- **RP6.3** a real match in the rig: an own worker walks to the lunch box and harvests, the bomber defuses, the fire ant bumps: **every cue that the confirmed engine made was heard exactly once, in its order, from the predicted engine, three ticks before the confirmed engine made it** (the lead), none lost, none doubled, no phantom, no replay, no rebuild.
- **RP6.4** twelve random matches (jitter, bursts, orders of every kind from every seat, a lead that is not the delay's, rebuilds): the books balance (every cue of the class that the confirmed engine made was dropped as heard or played now; every cue played from the predicted engine was met or is a phantom; the router's count of the confirmed engine's cues is the test's own count) and rebuilds did replay ticks that had been heard, flagged, and were not heard again.
- **RP6.5** the refusal of an order (the bomber is told to plant a bomb in the river): heard once with the right lead (right after the click: three ticks before the confirmed engine), still once when the lead is too low and the order is chased through three rebuilds (not once for every place it was put at), and once when the lead is too high and the order is sealed before the tick the prediction put it at (the confirmed copy comes five ticks off and is met).
- **PA3** the application: an own worker is sent to food and harvests; the cue is played from the predicted engine (one more sound is playing, the confirmed engine has made nothing yet), and a second later, when the confirmed engine has made the same cues, no second sound is ever playing, the copies were dropped (`duplicates_dropped == played_predicted`), the confirmed engine played no cue of the ant's action itself, and the stop that cuts the sound, from the predicted engine too, was met.

### Mutants (K1 - K13 edit the router and `prediction.cpp`, run against `test_prediction`; B1 - B4 edit `application.cpp`, run against `test_prediction_app`)

| Mutant | What it does | Result |
|---|---|---|
| `K1_every_sound_is_class` | every sound is routed from the predicted engine (combat, hits, explosions too) | KILLED by RP6.1 |
| `K2_foreign_ants_in_class` | the ant's owner is not asked: another player's ants' cues are routed too | KILLED by RP6.1 |
| `K3_replays_played` | what a rebuild's replay makes is played from the predicted engine | KILLED by RP6.2, RP6.4, RP6.5 |
| `K4_no_exact_tick_guard` | a tick that was played before and is run again (a restart within the lead) is played again | KILLED by RP6.2 |
| `K5_window_zero` | a copy that comes a few ticks off is not the same cue | KILLED by RP6.2, RP6.5 |
| `K6_copy_not_consumed` | a played occurrence meets any number of copies | KILLED by RP6.2, RP6.3, RP6.4, RP6.5 |
| `K7_stops_not_in_class` | the stops of own ants are the confirmed engine's | KILLED by RP6.1, RP6.2, RP6.3, RP6.5 |
| `K8_bump_needs_an_owner` | the bump (no owner) is not an own cue | KILLED by RP6.1, RP6.3 |
| `K9_kind_ignores_owner` | an occurrence is met by the same sound of any ant | KILLED by RP6.2 |
| `K10_no_phantom_expiry` | an occurrence that was never met stays in the book for ever | KILLED by RP6.2 |
| `K11_only_later_copies_meet` | a cue that was played AFTER the confirmed engine's tick is met, one played before it is not | KILLED by RP6.2 |
| `K12_unmet_own_cues_dropped` | an own cue that nobody played is dropped (lost) | KILLED by RP6.2, RP6.4 |
| `K13_replay_flag_never_set` | a rebuild's replay is not flagged as one | KILLED by RP6.4, RP6.5 |

| Mutant | What it does | Result |
|---|---|---|
| `B1_no_predicted_cues` | the predicted engine's cues are not played | KILLED by PA3 |
| `B2_confirmed_not_filtered` | the confirmed engine's cues are played as they come (the copies of what was heard are heard again) | KILLED by PA3 |
| `B3_wrong_tick_of_the_confirmed_cues` | the confirmed engine's tick is told wrong (far from the predicted one) | KILLED by PA3 |
| `B4_ownership_never` | no ant is the player's | KILLED by PA3 |

All killed. Three of the first run's verdicts changed when the tests did: K3 (replays are played) was caught only by RP6.2 because the exact-tick guard repeated the replay flag in the rig's matches (the guard now counts apart, `repeats_dropped`, so RP6.4 sees the flag), K5 (a window of zero) was caught only by RP6.2 because no rig case had a cue that the two engines made at different ticks (RP6.5's lead that is too high), and K13 (the replay flag never set) survived for the same reason as K3.

### What the cues do NOT do, and what they cost

- **The picture of other players' things is ahead of their sound.** A fight, an explosion, a hit is shown by the predicted engine `lead` ticks ahead of the confirmed one and heard when the confirmed engine makes it: at a round trip of 60 ms the lead is 2 ticks (3 - 4 while R4 was written, before the lead learned the lag), so the sound of a blow comes 100 ms after its picture (before the prediction both were late together). Playing the cues that the predicted engine makes from turns that are already in hand (certain: the jitter buffer's turns, no guess) would shorten that by the buffer's one or two ticks, and is not built: it is a second class of cues with the same bookkeeping, for a gain of 50 - 100 ms of a skew that the owner can judge first.
- A phantom (a cue of an own ant's action that the confirmed engine never made) is possible only after a correction, is counted, and costs one short sound: a harvest heard for food that another player's ant took first.

## R5: the switches and the safety (`--prediction`, the settings' key, the budget, the hidden page, the felt delay)

### Built

| Item | Where |
|---|---|
| **The switch**: `--prediction on \| off` and `--no-prediction` (the same as `off`), the settings' key `prediction` (`on` / `off`, `yes` / `no`, `true` / `false`, `1` / `0`, in any case: a key that the owner of the settings file writes, nothing in the game does), and `?prediction=on` / `?prediction=off` in the web page's address (the page gives the game `--prediction on` / `off`; nothing else of the address reaches the game through it). **Off by default** (opt-in; see the state at handoff above). The command line beats the settings; a bad value is refused on the command line (the game does not start, with the reason) and reported and ignored in the settings. A game of one machine never predicts (it has no delay): the switch is for matches of the network. The choice goes to every `NetGame` that the application makes (`attach_net`). The settings' key is `prediction` inside the game's own settings (the browser keeps those in its local storage under the game's one key), so it cannot collide with the page's keys (`ants.aspect.v2`, `ants.pointerlock`, `ants.name`) | `parse_switch`, `Application::choose_prediction`, `ApplicationConfig::prediction` / `prediction_given`, `attach_net` (`src/ants_app/application.cpp`, `application_menu.cpp`), `web/shell.html` |
| **The states in which it is off** (`NetGame::refresh_prediction`, once per update; it begins again with the first tick after the cause is gone): the user's switch, the application's (`set_prediction_suspended`), a match that is not in its Playing phase, a desync, a client session that is not in its Normal mode (a host change, a rejoin), a client that is catching up (a server's lag policy), a held runner (a pause), and **the pre-start**: nothing is predicted before the first executed turn, so the "Get ready to play!" dialog of protocol 12 has no predicted engine. **A hidden web page** suspends it too: its wake-ups step the match and no picture needs the predicted engine | `src/ants_net/netgame.cpp`, `Application::pump_network` (`page_hidden_ \|\| background_stepping_`) |
| **The budget** (the automatic fallback): a rebuild, or the ticks that a confirmed tick makes the prediction run, that take longer than `Prediction::Config::budget_ns` (12 ms: three quarters of a frame at 60 Hz; a rebuild is 0.04 ms on the shipped maps) is a strike; 4 strikes within 200 confirmed ticks (10 s) switch the prediction off: the confirmed engine is shown, orders go as they did before the prediction, the console says so (R5 made this permanent for the match, with the wall clock; **R7 below replaced both: the budget counts CPU time and the prediction comes back after a cool-down**). The budget is per machine (one machine cooling down does not touch the others). `NetGame::set_prediction_budget` and `default_prediction_budget_ns` are for the tests: a test process that plays matches on a busy machine stalls now and then, which is not the prediction's cost, so the mains of the suites that play matches raise the default | `Prediction::note_cost`, `src/ants_net/prediction.cpp`, `NetGame` |
| **The felt delay** (`net::FeltDelayMeter`, `include/ants_net/latency.hpp`): what the click feels while the prediction is on, from the frame that took the order to the end of the first frame after the predicted engine's next tick (the median of the last five; the orders are found by the count of predicted commands). The corner's "delay" shows it while predicting (a dash until an order has been felt, and ten seconds after the last one) and the network's delay otherwise (`Application::corner_delay_ms`). The network's delay is not shown while the prediction is on: it is the time that the confirmed engine waits, which the picture no longer does (`NetGame::command_delay_ms()` still measures it, and the prediction's estimate of the lag starts from it) | `include/ants_net/latency.hpp`, `Application::note_orders` (`OrdersNoted` in the mouse and key handlers), `post_tick`, `render_frame` |
| Tests: RP7.1 - RP7.3 (suite 2.27), N3.30 (`test_netgame`), N9.37 - N9.39 (`test_latency`), PA4 - PA6 (suite 3.22) | `tests/test_net/test_prediction.cpp`, `tests/test_net/test_netgame.cpp`, `tests/test_net/test_latency.cpp`, `tests/test_app/test_prediction_app.cpp` |

### What the tests hold the switches to

- **RP7.1 - RP7.3** (rewritten for R7, see there): four strikes switch the prediction off at once, the confirmed engine is shown, an order is left to the caller (`submit` returns false and applies nothing), a suspension does not end it, the match (the confirmed engine, the turns, the hashes) is untouched; the budget runs out in the middle of an order (the rebuild that the order asks for is the strike that starts it: nothing is applied to an engine that was dropped); strikes that are spread out do not add up (a window of nothing forgets each at the next tick).
- **N3.30** a real `NetGame` whose budget nothing meets loses the prediction and nothing else (it shows its confirmed engine, an order goes the old way with `predict_order_ack`'s answer, the match runs on and every machine ends identical); the other machine of the room goes on predicting.
- **N9.37 - N9.39** the meter: the time from the frame that took the order to the end of the first frame after the next predicted tick, orders of one frame share it and each is felt from its own frame, the median of five (one odd order does not show), an order whose effect is never drawn is forgotten after five seconds, the waiting list is bounded.
- **PA4** `parse_switch` (every spelling, anything else refused), the command line (`--prediction on` / `off`, `--no-prediction`, a missing or bad value is a start-up error), the settings' key (read, a bad value ignored and reported), the command line wins; off, the match shows the confirmed engine and nothing is predicted. **PA5** a hidden page: suspended at once, again with the next tick when it is shown. **PA6** the corner: a dash until an order has been felt, then the felt delay (less than a tick and a frame), a dash again when it is stale, the network's delay when the prediction is off.

### Mutants (G1 - G8, G7b, G7c edit `prediction.cpp`, run against `test_prediction`; N13 edits `netgame.cpp`, run against `test_netgame`; H1 - H10 edit `application.cpp` / `application_menu.cpp`, run against `test_prediction_app`)

| Mutant | What it does | Result |
|---|---|---|
| `G1_never_gives_up` | the strikes never end the prediction | KILLED by RP7.1, RP7.2 |
| `G2_one_strike_ends_it` | a single strike ends the prediction | KILLED by RP7.1, RP7.2, RP7.3 |
| `G3_window_not_pruned` | strikes of any age add up | KILLED by RP7.3 |
| `G4_gives_up_but_keeps_running` | the prediction that has given up is not stopped | KILLED by RP7.1, RP7.2 |
| `G5_begins_again_after_giving_up` | a prediction that has given up begins again with the next tick | KILLED by RP7.1, RP7.2 |
| `G6_rebuilds_are_free` | the cost of a rebuild is not counted | KILLED by RP7.1, RP7.2 |
| `G7_submit_uses_a_prediction_that_ended` | an order goes on with a prediction that its own rebuild has ended | SURVIVED (alone; killed with G7b, see below) |
| `G7b_submit_applies_the_order_to_a_prediction_that_ended` | an order is applied to a prediction that its own run of ticks has ended | SURVIVED (alone; killed with G7) |
| `G7c_submit_goes_on_after_the_prediction_ended` | both guards gone | KILLED by RP7.2 |
| `G8_runs_are_free` | the cost of the ticks that are run on is not counted | KILLED by RP7.1, RP7.3 |
| `N13_budget_not_given` | the `NetGame`'s budget is not given to the prediction | KILLED by N3.30 |
| `H1_prediction_wanted_ignored` | the application's choice is not given to the `NetGame` | KILLED by PA4 |
| `H2_no_prediction_option_ignored` | `--no-prediction` does not turn it off | KILLED by PA4 |
| `H3_settings_key_ignored` | the settings' key `prediction` is not read | KILLED by PA4 |
| `H4_settings_beat_the_command_line` | the settings' key wins over the command line | KILLED by PA4 |
| `H5_hidden_page_keeps_predicting` | a hidden page's prediction is not suspended | KILLED by PA5 |
| `H6_felt_ticks_not_told` | the felt delay is not told that a tick of the predicted engine has run | KILLED by PA6 |
| `H7_felt_frames_not_told` | the felt delay is not told that a frame has been drawn | KILLED by PA6 |
| `H8_mouse_orders_not_noted` | an order that a mouse button gives is not looked at | KILLED by PA6 |
| `H9_corner_delay_is_always_the_network` | the corner shows the network's delay while predicting | KILLED by PA6 |
| `H10_corner_delay_never_a_dash` | the corner shows the network's delay while predicting and nothing has been felt (no dash) | KILLED by PA6 |

G7 and G7b are the two guards that an order needs after the budget has ended the prediction (the first after the rebuild that the order asks for, the second after the ticks that it runs): each covers for the other, because either guard alone stops the order from being applied to an engine that was dropped, so no test can tell one from the other. Together (G7c) they are killed (RP7.2). They are redundant by design (a guard at each place that can end the prediction), and each stays.

### Where this differs from the design, and why

- **The fallback was for the rest of the match** (R5; changed by R7: the review found that a stall of the process, timed with the wall clock, ended the prediction of a machine that was never too slow). Now it is a cool-down that doubles, so that a machine that really cannot afford it flaps at most once in 10 s, then 20 s, then 40 s ... and 160 s.
- **The readout shows the felt delay while the prediction is on, and the network's delay only while it is off** ("and the network delay where it helps": it does not help: with the prediction on, the network's delay is the time that the confirmed engine waits, which the screen no longer does, so showing it next to the felt one would suggest that the click still waits that long).
- **The pre-start is not a state that is switched off, it is the absence of the engine**: the prediction begins with the first executed turn (protocol 12's dialog has not run one), so there is nothing to suspend.

## R6: the lead that learns, the measurements, and the chain from the click to the picture

### The lead learns the lag of the player's own orders

R2 gave the lead from the owner's delay estimate (`NetGame::command_delay_ms()`: the median of the last five measured delays, else the ping, half a seal and the jitter buffer), rounded to ticks. On real orders that estimate is **one tick longer than the order's lag** (the corner's delay is measured up to the moment the tick that applies the order has run, the lag stops at its start): a round trip of 0 ms has a delay of 83 ms and a lag of 1, 60 ms 133 ms and 2, 200 ms 283 ms and 5, so the old lead was `lag + 1` by accident. The lead is now measured on the orders themselves and **equals the lag: the bias is 0 by default** (the coordinator's decision of 2026-10-03). A bias of one shows the ants 50 ms sooner than a game of one machine (139 ms against 192) but costs 10 - 16 visible whole-tile hops a minute of the player's own ants at an order a second (the tables below); the feel of a local game, without hops, is the goal.

| Item | Where |
|---|---|
| `Prediction` records the **lag of every own order that comes back in a turn** (`n - born`: the sealed tick minus the confirmed tick at which the order was given; bounded by the longest lead) with the confirmed tick at which it was seen. `learned_lag_ticks()`: the **median of the last five** that are not older than 400 ticks (20 s), the upper one of an even number (a lead that is too low, which makes the order chased, is the worse mistake: R2), 0 when there is none. The lead is that lag **plus `lead_bias_ticks` (0)**, else the owner's estimate plus the bias; it rises at once and falls after 40 ticks, as before, and stays between 1 and 12. One order that a stall held up does not move the lead for the orders after it, a lag that has changed for good does after three orders | `include/ants_net/prediction.hpp` (`learn_lead`, `lag_samples`, `lag_fresh_ticks`, `lead_bias_ticks`), `src/ants_net/prediction.cpp` (`on_turn`, `learned_lag_ticks`, `wanted_lead`, `update_lead`) |
| `NetGame::expected_command_delay_ms()` (the estimate until an order has taught the lag, and after a quiet spell of 20 s) is now the **lag as a delay**: the measured delay, or ping + half a seal + the jitter buffer, less the tick that the delay counts and the lag does not | `src/ants_net/netgame.cpp` |
| **The bias** (`lead_bias_ticks`, default 0): an order is put at the display tick, so with 0 it lands where the host runs it and nothing is corrected while the lag holds. With 1 it is put a tick later than that, one rebuild per order moves the ordered ants a tick on, and the ants react 50 ms sooner than in a local game; the price is a whole-tile hop in about one order in six. The knob stays for the tests and the measurements | measured below |
| Tests: RP8.1 - RP8.6 (suite 2.27) and N3.31 (`test_netgame`); the rig's own configuration keeps learning and bias off, so RP1 - RP7 hold the lead to account as before | `tests/test_net/test_prediction.cpp`, `tests/test_net/test_netgame.cpp` |

What the tests hold: **RP8.1** a lead that starts too low (one tick where the orders need six) costs the first order a chase and is then the lag, with no rebuild for the orders after it (bias 0), exactly one rebuild an order with the bias; **RP8.2** the median of the last five over a sequence with a stall, then a lag that changes for good (the lead rises at once); **RP8.3** a lag that is not fresh is forgotten and the owner's estimate is the lead again, and new orders teach it again; **RP8.4** 16 random matches with learning and a random bias keep the predicted engine equal to its derivation (`derived_hash`) at every third frame, and converge; **RP8.5** the edges (a prediction told nothing starts at two ticks of lag plus the bias and stays inside its bounds, two orders that disagree give the upper one, orders held up for ever count as the longest lead, the bias on a long lag stops at the longest lead); **RP8.6** the product's defaults are the documented numbers (five orders, 400 ticks, bias 0, bounds 1 and 12, fall after 40, lost after 100, budget 12 ms, four strikes in 200); **N3.31** in a real match of two `NetGame`s the lead begins at the lag that the jitter buffer promises (one tick) and, after twelve real orders, is the learned lag, with no order lost and at most three rebuilds (none was made).

### Mutants (L1 - L18 edit `prediction.cpp`, run against `test_prediction`; N14, N16 - N18 edit `netgame.cpp` / `prediction.hpp`, run against `test_netgame`)

| Mutant | What it does | Result |
|---|---|---|
| `L1_learning_off` | the orders that come back teach the lead nothing | KILLED by RP8.1, RP8.2, RP8.3, RP8.5 |
| `L2_lag_is_the_largest` | the lag of the last orders is the largest of them (a stall moves the lead) | KILLED by RP8.2 |
| `L3_lag_is_the_smallest` | the lag of the last orders is the smallest of them | KILLED by RP8.2, RP8.5 |
| `L4_lag_is_the_lower_median` | the lag of an even number of orders is the lower one in the middle | KILLED by RP8.5 |
| `L5_no_bias` | the configured bias is not added to the lead | KILLED by RP8.1, RP8.5 (they name a bias of 1 and 3: the default is 0) |
| `L6_old_lags_never_forgotten` | a lag that is not fresh is counted all the same | KILLED by RP8.3 |
| `L6b_lags_stamped_with_tick_zero` | a lag is stamped with the tick 0 (it is old as soon as the match is) | KILLED by RP8.3 |
| `L7_lag_counted_from_the_display_tick` | the lag is counted from the tick at which the order was put in the predicted engine, not from the confirmed tick that it was given at | KILLED by RP8.1, RP8.2, RP8.3, RP8.5 |
| `L8_window_unbounded` | every order that ever came back counts | KILLED by RP8.2 |
| `L9_window_of_one` | only the last order counts | KILLED by RP8.2 |
| `L10_window_keeps_the_oldest` | the window forgets the newest order, not the oldest | KILLED by RP8.2 |
| `L11_estimate_fixed` | before the first order the lead ignores the owner's estimate | KILLED by RP1.3, RP2.1, RP2.3, RP3.1, RP3.2, RP6.3, RP6.5, RP8.1 |
| `L12_learned_lag_ignored` | the lead is always the owner's estimate | KILLED by RP8.1, RP8.2, RP8.3, RP8.5 |
| `L13_lag_not_bounded` | a lag is not bounded by the longest lead | KILLED by RP8.5 |
| `L14_lead_not_bounded_above` | the lead has no longest value | KILLED by RP1.3, RP8.5 |
| `L15_lead_not_bounded_below` | the lead has no shortest value | KILLED by RP1.3 |
| `L16_constructor_ignores_the_estimate` | a prediction that has been told nothing starts at three ticks, as the first version did | KILLED by RP8.5 |
| `L17_estimate_one_tick_high` | the owner's delay is turned into a lag one tick too long | KILLED by RP1.3, RP2.1, RP2.3, RP3.1, RP3.2, RP6.3, RP6.5, RP8.1, RP8.3 |
| `L18_target_not_refreshed_by_the_tick` | the lead's target follows the owner's delay only, not the orders | KILLED by RP8.1, RP8.2, RP8.3, RP8.5 |
| `N14_estimate_counts_the_turn` | the delay that the owner tells is the delay itself (a tick more than the lag) | KILLED by N3.31 |
| `N16_default_does_not_learn` | the lead of a match does not learn from the orders | KILLED by N3.31 |
| `N17_default_has_a_bias` | the lead of a match has a bias of one (`lead_bias_ticks{0}` to `{1}`: the default is 0; the row said "has no bias" while the default was 1) | KILLED by RP8.6 (the defaults are pinned) and by N3.31 (the lead begins at the lag) |
| `N18_default_window_of_one` | the lead of a match counts the last order only | SURVIVED the first run (every real order of N3.31 has the same lag); killed by RP8.6 (the defaults are pinned) |

(Equivalent mutants that were not run: the `0u` of `n >= born ? n - born : 0u` is unreachable (an order cannot be sealed in a tick before the confirmed tick that it was given at: the turns that were sealed before were already run), the guard is there against an unsigned underflow; the `<=` of the freshness test against `<` differs for an order that is exactly 400 ticks old, one tick of twenty seconds.)

### Measured: the felt delay, native (the application over real sockets, a link of a chosen delay, virtual time)

A scratch program (not in the repository) plays the real application as the guest of a room that a bare machine hosts; a relay between them holds every chunk of the TCP stream for the chosen time in each direction. Time is the program's own (a frame is 1000 / 60 ms of it, the network's clock, the relay's delays and the click moments are numbers, not the machine's clock), because the machine this was measured on ran at a load average of 28 on 10 cores, and what a real machine adds is the time that a frame takes (measured as thread CPU time per frame, which a load does not inflate). A click is a right button release on open ground with the player's ants selected, at a random moment between two frames, handled at the start of the next (as the window delivers input). The answer is the time from the click to the end of the first frame in which the SHOWN engine's ant has its walk pose (or its first step): the ants' first visible reaction. 30 clicks a cell; the same scratch program with a negative delay plays a game of one machine.

| Round trip | the corner's `delay` off (network) | ants stand up, off | ants stand up, **on** (mean / median) | lead (ticks) | rebuilds / orders (0 lost) | for reference: on with a bias of 1 |
|---|---|---|---|---|---|---|
| a game of one machine | - | **192 ms** | - | - | - | - |
| 0 ms | 83 ms | 242 ms | **192 ms** / 193 | 1 | 0 / 30 | 142 ms |
| 20 ms | 133 ms (83 in another run) | 274 ms (243 in another run) | **189 ms** / 193 | 1 | 2 / 30 | 139 ms |
| 60 ms | 134 ms | 294 ms | **192 ms** / 193 | 2 | 0 / 30 | 139 ms |
| 100 ms | 183 ms | 342 ms | **192 ms** / 193 | 3 | 1 / 30 | 157 ms |
| 200 ms | 283 ms | 444 ms | **192 ms** / 193 | 5 | 4 / 30 | 192 ms |

- **With the product's lead (the lag itself, no bias) the ants stand up at 192 ms at every round trip, the same as in a game of one machine** (without the prediction 242 - 444 ms: 294 against 192 at a round trip of 60 ms), and the order is in the shown engine in the click's own frame (9 ms on average: the wait for that frame) instead of 92 - 294 ms later. The lead is the lag (1, 1, 2, 3, 5 ticks); no order was lost, and 0 - 4 of 30 needed a rebuild (a lag that moved by a tick between two orders).
- The last column is the earlier run with `lead_bias_ticks` = 1 (the knob; the tables further down compare the two): the ants stand up 50 ms sooner than in a local game while the round trip is under about 100 ms, because the correction that comes with the order's turn moves the ordered ants a tick on before the picture has shown their reaction (a turn arrives about `lag - 1` ticks after the click, the ants stand up about three ticks after it: at 200 ms the turn comes after, and the gain is gone).
- The 83 / 133 ms and 243 / 274 ms at a round trip of 20 ms are both real: the delay is quantised by the 50 ms turns and the phase between the host's seals and the client's ticks is set by the run's start; the same cell differs by one turn between two runs. The prediction's numbers do not move with it.
- The corner's `delay` with the prediction on reads **33 ms** in this model (the model's click schedule probably has one fixed phase against the ticks; in the browser, where the phases are random, it reads 15 - 18 ms): the wait of a frame or two for the next tick, whatever the round trip.
- The thread CPU time per frame (input, network, ticks and the picture on a software renderer) does not move with the prediction: off and on differ by -1.5 to +1.5 ms and the runs by more (2.4 - 6.6 ms in all, on a machine that was busy with other work). The prediction's own cost: **a rebuild takes 0.01 - 0.05 ms on average (0.06 ms at most)** in this quiet match; the predicted tick runs with the confirmed one (a run of ticks over 12 ms would be a strike; the longest was 5.4 ms, a descheduling).
- **Memory**: the second engine is 124 - 372 KiB on the shipped maps (R1's census); the process's peak resident size over two runs each, off and on, at a round trip of 60 ms: 64.9 and 62.6 MiB off, 64.5 and 63.0 MiB on: the difference is under the noise.

### Measured: the felt delay and the frame cost in the web build (headless Chrome, a native game server behind a relay)

The web image (`docker build -t ants-beta:rb .`) was served by a small script together with a relay of `/ws` to a native `ants_server` with the chosen delay in each direction; a headless Chrome with a throwaway profile opened two windows (both drawn: 60 frames a second) in a demo room for two players, and the first window's player gave twelve orders with the mouse (a rubber band over the hill and a right click on open ground: the game's own HUD, the page's real input path) and then a stream of them for twenty seconds, the second window's player giving some too. What is reported is what the game itself says through its `ants_probe` (the web build's read-only probe: 7 the corner's delay, 8 the network's delay, 9 the prediction's state, 10 and 11 its counts, 12 - 15 the frames' own work), not a guess from outside.

| Round trip | corner's `delay` off (the network's) | corner's `delay` on (what the click feels) | the network's delay with it on | predicted orders / rebuilds | the game's frame function, mean / longest (off, on) |
|---|---|---|---|---|---|
| 0 ms | 84 ms | **15 ms** | 84 ms | 39 / 39 | 0.25 / 0.70 ms, 0.77 / 1.80 ms |
| 60 ms | 149 ms | **18 ms** | 150 ms | 39 / 40 | 0.61 / 1.70 ms, 0.52 / 1.90 ms |
| 200 ms | 282 ms | **16 ms** | 300 ms | 40 / 41 | 0.89 / 1.70 ms, 0.82 / 1.80 ms |

(The medians of twelve orders; the first order or two read 25 - 33 ms before the lead has learned the lag. Measured when the lead still had a tick of bias; the felt delay does not depend on the lead.) **The game's frame function (input, network, ticks, the picture's draw calls; not the browser's own compositing) takes 0.25 - 0.9 ms a frame in wasm with the prediction on or off**: in each run the window with the prediction on (the second window, which plays with the defaults) and the one with it off took the same time within 6 % (0.266 / 0.251, 0.614 / 0.610, 0.886 / 0.890 ms), so the variation between runs is the machine's, not the prediction's; the longest frame function of any run was 1.9 ms, and the budget that ends the prediction is 12 ms. In the three runs with `?prediction=off` the game said so (state 0, nothing predicted), and in the others the prediction was on (state 1).

The repository's own opt-in browser checks were run against the same web build: `tests/scripts/test_web_hidden.sh` (a really hidden tab, prediction on by default in both windows: **11 checks, 0 failed**, the state hashes of the two games equal), `tests/scripts/test_web_edge.sh` (**102 checks, 0 failed**), and the new `tests/scripts/test_web_prediction.sh` (**17 checks, 0 failed**: two matches in two windows, the defaults and `?prediction=off`: the prediction is on and predicts, then off and predicts nothing, the click is felt in 19 ms where the confirmed engine applies it in 169 ms at a round trip of 60 ms, the frame function costs 0.3 ms, and the state hashes of the window that predicts and the one that does not are equal at five ticks of a match). `tests/scripts/test_web_aspect.sh` has **7 failed of 535**: all are taps on the quick help's START button on the 16:9 picture (and the checks of the selector that need a match that was started that way); the same taps on the classic 4:3 picture pass. The check taps at the coordinates of the original's 640 x 480 page centred in the 960 x 540 canvas, and the 16:9 quick help that v0.2.0 brought has its START elsewhere (in the bottom right corner), so these are stale coordinates of that script, not an effect of this branch: **the same seven checks fail against the web build of `origin/main` (888e788)**, built and checked the same way.

### Measured: the correction rate and size (`test_prediction --measure`: ten minutes of match for every line, a rig that plays the server)

Seat 0 is the player whose screen it is. The link is one jitter-buffer turn, a turn that takes one step to arrive, an order that takes one step to be sealed and a step of jitter (a round trip of about 60 - 100 ms); the prediction is the product's (learned lead, no bias). The other seats give random orders (moves, attacks, specials, Stop on two thirds of their ants, 24 ants walking in all) at 0.5 or 2 a second each; the player's own orders come once a second where the line says. A correction is a rebuild after which at least one ant stands elsewhere at the display tick than it did before.

| Own orders / s | Players x foreign orders / s each | Rebuilds: own timing / foreign | Visible corrections a minute | Ants moved per visible correction | An ant's move, mean / largest |
|---|---|---|---|---|---|
| 1 | 1 (nobody else) | 0 / 0 (575 orders) | 0 | - | - |
| 0 | 2 x 0.5 | 0 / 297 (298 orders) | 23.7 | 2.1 | 13 px / 28 px |
| 0 | 2 x 2 | 0 / 1,157 (1,208) | 82.7 | 2.0 | 12 px / 28 px |
| 0 | 4 x 0.5 (three others) | 0 / 917 (967) | 65.2 | 2.0 | 13 px / 32 px |
| 0 | 4 x 2 | 0 / 3,048 (3,673) | 205.8 | 2.2 | 12 px / 32 px |
| 1 | 2 x 0.5 | 0 / 275 (279) | 20.5 | 2.2 | 12 px / 28 px |
| 1 | 2 x 2 | 0 / 1,132 (1,184) | 74.6 | 2.1 | 12 px / 28 px |
| 1 | 3 x 0.5 | 0 / 619 (644) | 49.1 | 2.1 | 12 px / 28 px |
| 1 | 3 x 2 | 0 / 2,088 (2,357) | 140.4 | 2.0 | 12 px / 32 px |
| 1 | 4 x 0.5 | 0 / 824 (861) | 61.2 | 1.8 | 13 px / 28 px |
| 1 | 4 x 2 | 0 / 2,938 (3,559) | 216.0 | 2.2 | 12 px / 28 px |

- **A foreign order costs one rebuild and is seen as a hop of the ants it concerns in about two of three cases** (a hop is `lead` ticks of walking: 4 px a tick, 3 ticks: 12 px on average, a tile (32 px) at most; about two ants a time). At a person's pace (0.5 - 2 orders a second from each other player) that is 21 - 216 visible hops a minute for the whole screen (the more players the more).
- **An own order costs nothing while the lag holds**: no rebuild and no hop (the first line, and the own-order lines of the mixed ones). With a lag that jitters by two ticks about one order in four is chased (the table below).
- Costs, wall time of a rebuild / of a predicted tick in this busy synthetic world (24 ants walking, several players ordering; the machine was lightly loaded: real time equalled CPU time): rebuild 0.30 - 0.53 ms on average (longest 0.9 - 3.1 ms), tick 0.009 - 0.064 ms on average. At the worst line (4 players, 2 orders a second each: about 3,000 foreign rebuilds in ten minutes) the rebuilds are 0.3 % of the time.

**The bias, measured** (own orders only, an order a second, the same orders and link; `lag jitter` is the lag changing by one or two ticks from order to order):

| Bias | Lag jitter | Orders | Rebuilds | Visible corrections a minute | An ant's move |
|---|---|---|---|---|---|
| 0 | none | 599 | 0 | 0 | - |
| 1 | none | 602 | 602 | 10.0 | 32 px |
| 0 | 1 tick | 611 | 0 | 0 | - |
| 1 | 1 tick | 638 | 636 | 11.6 | 32 px |
| 0 | 2 ticks | 585 | 158 (124 own timing, 34 other: a late order, chased) | 3.5 | 32 px |
| 1 | 2 ticks | 575 | 510 | 13.1 | 24 px (36 at most) |
| 0, no Stop orders | none | 635 | 0 | 0 | - |
| 1, no Stop orders | none | 617 | 617 | 10.4 | 32 px |
| 0, no Stop orders | 2 ticks | 620 | 197 | 4.1 | 30 px |
| 1, no Stop orders | 2 ticks | 554 | 500 | 16.2 | 25 px |

Bias 0 (the product's) is exact while the lag holds and, when the lag jitters by two ticks, chases about one order in four (a rebuild for each turn that passes it, and the ants hover); bias 1 pays one rebuild for every order and a visible hop in one of six, and gains the 50 ms of the first table. **The owner chose bias 0**; `Prediction::Config::lead_bias_ticks` stays as the one number (RP8.6 pins its default, N3.31 expects the lag itself, RP8.1 and the rig's configuration name their own).

### The chain from the click to the first changed pixel, and the delays that are avoidable

What happens after a right click, in the native game (the web build has the same order: the page's events are queued and the game takes them at the start of its next frame, `requestAnimationFrame`'s):

| Stage | Time | |
|---|---|---|
| The click marker (the first changed pixel) | in the frame that takes the release | `HUD::pointer_right_click` spawns it at the release, an HUD effect: no engine, no network, the same with the prediction on or off (the original executes the right button at its release, FUN_01027b51) |
| The event waits for the frame that polls it | 0 - 16.7 ms, 8 on average | `Application::run_frame_with_delta`: `handle_events` is the first thing of a frame, before the network is read and the ticks run, so the order is in the predicted engine and in the packet to the host in the same frame, and the tick that is due in that frame runs it before the frame is drawn: **no extra frame of presentation, no input taken late within the frame** |
| The order in the predicted engine | the same call, 0.04 ms | `NetGame::submit` |
| The next tick runs it | 0 - 50 ms, 25 on average (0, 1 or 2 frames of three: the corner's `delay` reads 15 - 33 ms) | the predicted engine's ticks follow the confirmed engine's tick clock |
| The ants' start-up | three ticks, 150 ms | the original's own animation: nothing that the application may change |
| The frame is drawn and shown | up to a frame, and the display | present with vsync |

What was found, and what was done about it:

- **Done**: the lead of R2 was `round(delay / 50)`, one tick longer than the lag by accident; it is now the lag, measured on the orders themselves (median of five), and the estimate until an order has taught the lag counts the lag. Without the prediction the ants stand up at 294 ms at a round trip of 60 ms; with it, at 192 ms, as in a game of one machine.
- **Looked at and not changed, with the reason**:
  - *A tick run at the click* (the predicted engine's next tick, run at once, would save the 25 ms of the wait for the clock): it puts one tick of the whole picture early and the next scheduled tick has nothing to run, so every order makes one tick interval of every ant 50 ms longer (a stutter in the pace of the whole picture per order), and a second order in the same interval would land a tick later than it does now. Not worth 25 ms.
  - *Reading the events between frames* (a native loop that waits for an event instead of `SDL_Delay`, or polls late in the frame): the native loop is `poll, network, ticks, draw, present (vsync), delay`; on a 60 Hz display the present waits out the frame and there is no delay to cut, and on a faster display the `SDL_Delay` holds the game to the 60 frames a second that its limiter is for. Polling as late as possible before the present (a "late latch": sleeping until the vblank minus the frame's cost) would save about half a frame of input wait on average (about 8 ms of 140) at the price of a frame that misses the vblank whenever it costs more than estimated: not done (the existing notes on the input wait reach the same conclusion: "for 8 ms of 100 - 160").
  - *The audio buffer* is 1024 samples at 44.1 kHz (23 ms) between the click's voice and the loudspeaker; it is the sound's latency and not a pixel's, and a smaller buffer risks underruns on busy machines: not changed.
- **Not avoidable**: the 150 ms of the ants' start-up (the original's animation), the wait for the next tick (the lock-step's 50 ms), the scan-out.

### Where this differs from the design, and why (R6)

- **The lead is the lag, with no bias.** The design said "lead = the measured own-command delay in ticks, adaptive"; the delay is one tick longer than the lag, which is what the lead is (the coordinator's decision of 2026-10-03: a bias of one gains 50 ms and costs a whole-tile hop in one order of six; `Config::lead_bias_ticks` keeps the choice).
- **The correction smoothing that the hops invite is not built.** A corrected ant moves at once by `lead` ticks of walking (a tile at most): drawing the correction as a short glide would hide the hop at the price of a drawn position that is not the engine's for a few frames; it belongs to the renderer and is the next candidate if the hops are found to be ugly in play.
- **Ant ids are stable** by construction (nothing in the predicted engine hatches; Hatch waits for its turn) and the tests check it through the state hash, which counts every ant's id and every cell's occupant: every comparison with the oracle at a display tick (RP2.2, with hatching from every seat, and the property tests) would fail on one id that differed.
- **The wasm budget fallback was measured, not forced**: the frame function takes 0.3 - 0.9 ms against a budget of 12 ms, so the fallback is never reached on the shipped maps; RP7.1 - RP7.3 and N3.30 force it natively (a budget of one nanosecond).

### The mutation harness on the final tree

Every mutant of R1 - R6 was run again against the final tree (R2 - R5 had been run before the lead learned and before the v0.2.0 rebase, and the tests that run with the product's defaults, the `NetGame` and application ones, see another lead now; four mutants whose lines had moved were re-pointed first). The harness (a scratch script, not in the repository) applies each mutant as exact-string edits, deletes every object of the libraries and of the test program so that Apple's make (one second timestamps) cannot reuse a stale one, rebuilds, runs the suite, records which tests failed, restores the files and runs the baseline again after every five.

| Suite | What it edits, what it runs against | Mutants | Not killed |
|---|---|---|---|
| R1 | the engine copy (`ants_sim`), `test_engine_copy` | 35 | none |
| R2 | `prediction.cpp`, `test_prediction` | 20 | Q15 (equivalent, above) |
| R2 (netgame) | `netgame.cpp`, `test_netgame` | 12 | N12 (a server's catch-up policy: no `NetGame` test has one) |
| R3 | `application.cpp`, `test_prediction_app` | 7 | A2 (equivalent, above) |
| R4 | the cue router and `prediction.cpp`, `test_prediction` | 13 | none |
| R4 (application) | `application.cpp`, `test_prediction_app` | 4 | none |
| R5 | the budget in `prediction.cpp`, `test_prediction` | 10 | G7, G7b (the two guards that cover for each other; both together, G7c, are killed) |
| R5 (netgame) | `netgame.cpp`, `test_netgame` | 1 | none |
| R5 (application) | `application.cpp`, `application_menu.cpp`, `test_prediction_app` | 10 | none |
| R6 | the learner in `prediction.cpp` and `prediction.hpp`, `test_prediction` | 22 | none |
| R6 (netgame) | `netgame.cpp`, `prediction.hpp`, `test_netgame` | 4 | N18 (killed by RP8.6 as L19, above) |

138 mutants, five that no test kills and that are explained above (Q15, A2 and N12 by what can be seen, G7 and G7b by each other). The tree and the build folder match the sources again after every run (the harness checks).

### Gates on the final tree (commit 1ba4c42; the commits after it only changed this document)

- **Build**: Release with `-DANTS_WERROR=ON` (`-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Werror`) on Apple clang: zero warnings; the AddressSanitizer + UBSan tree (`./run_tests.sh --asan`, also `-Werror`): zero warnings; **GCC 12.2 (Debian 12, the game server image's toolchain)**: every target that needs no SDL (43 targets, the tests of the simulation, the network, the server and the bots among them, `test_prediction`, `test_netgame`, `test_latency` and `test_engine_copy` with them) builds with `-Werror` and **zero warnings** and its `test_prediction`, `test_netgame`, `test_latency` and `test_engine_copy` pass (34, 33, 39 and 20 tests); the first GCC build found one `-Wshadow` error in the cue tests (clang does not flag it), fixed in 1ba4c42, and a rescan of every file of the branch with clang's `-Wshadow-all` found nothing else; the application's targets (they need SDL) were not built with GCC.
- **`./run_tests.sh`** (the full tier, parallel): **66 suites, 0 failed**, 127 s of wall time (the suite times add up to 797 s; the slowest: the lock-step soak 126 s, the server's reconnect part 90 s, the engine copy on whole matches 69 s, the network application 68 s, the server 56 s, the prediction suite 55 s).
- **AddressSanitizer + UBSan**, `./run_tests.sh --asan --sim` and `--app` (57 suites in all; no sanitizer report anywhere): 55 pass. The two that do not are not this branch's: `2.18 map_sweep --selftest` injects a segmentation fault on purpose and expects the tool to report death by SIGSEGV, and ASan's own handler turns it into an exit status (62 checks, 0 failures with `ASAN_OPTIONS=handle_segv=0`); `3.16.1 test_wide_pages` failed one check of 389, "the quick help follows", in the parallel run of unoptimised builds and passed (389 checks, 0 failures) alone. The prediction's own suites under the sanitizers: `test_prediction` (34 tests), `test_netgame`, `test_engine_copy` (both tiers: the whole matches' first 2,500 ticks), `test_prediction_app`, `test_network_app`, `test_start_menu_app`.
- **Web**: the web image builds (`docker build -t ants-beta:rb .`, Emscripten 3.1.58: the probes of the web build compile) and its inputs are identical to the final tree's (`src`, `include`, `web`, `CMakeLists.txt`, `cmake`, `VERSION` and the assets did not change after it); in a headless Chrome against a local server: `test_web_prediction.sh` 17 checks, 0 failed; `test_web_hidden.sh` 11 / 0; `test_web_edge.sh` 102 / 0; `test_web_aspect.sh` 535 checks, 7 failed (the same seven as on `origin/main`).
- **Mutants**: 138, five that no test kills and that are explained (the table above).

## R7: the review's fixes: the budget counts CPU time and ends in a cool-down (HIGH), and a match that does not ask for the prediction has none (the default costs nothing)

### The budget

**Why.** R5 timed a block of work with the wall clock and four strikes ended the prediction for the rest of the match, so a stall of the whole process (a tab in the background, an overloaded machine, a descheduled thread) cost "time" that the prediction never spent and could take it away for good from a machine that was never too slow.

| Item | Where |
|---|---|
| **`thread_cpu_ns()`**: the calling thread's CPU time (POSIX `clock_gettime(CLOCK_THREAD_CPUTIME_ID)`, Windows `GetThreadTimes`, kernel + user; the web build has one thread and keeps the wall clock; a call that fails falls back to the wall clock). **`work_cost_ns(wall, cpu)`**: what the budget counts is the CPU time, never more than the wall time (Windows' thread times count in clock ticks: a block of a few microseconds may be charged a whole tick). The statistics (`rebuild_ns_*`, `advance_ns_*`) stay wall figures | `prediction.hpp`, `prediction.cpp` |
| **The cool-down**: `budget_strikes` (4) strikes within `budget_window_ticks` (200) switch the prediction off for `cooldown_ticks` (200 = 10 s) confirmed ticks (`cooling_down()`, `cooldown_ticks_left()`, `Stats::cooldowns`): the confirmed engine is shown, orders go as before; then it begins again from the confirmed engine as after any suspension, with no strike on record; each further cool-down of the match is twice as long, up to `cooldown_max_ticks` (3200 = 160 s). A suspension does not end it. `gave_up()` is gone; the console says so at each cool-down, and the web probe 9 reads 2 while one lasts | `prediction.cpp`, `NetGame::prediction_cooling_down`, `Application::post_tick` |
| **Test seams**: `Config::work_hook` (runs inside every timed block), `Config::wall_clock` and `cpu_clock` (the clocks that a timed block is measured with, in ns; empty, as the product has them: the real ones), `NetGame::set_prediction_work_hook`, `set_prediction_clocks(wall, cpu)`, `set_prediction_budget(ns, strikes, cooldown_ticks)` | `prediction.hpp`, `netgame.hpp` |

**Tests** (they cost a block what the work hook says by moving the clocks of the budget by hand, `tests/test_net/manual_clock.hpp`: a spin on the real clocks failed now and then on the Windows runners, see "The budget tests on the Windows runners" below). **RP7.1** four strikes start a cool-down at once (the copy that begins the prediction and the three ticks after it), orders are refused, a suspension and a resume do not end it, the confirmed engine equals that of a twin without prediction at every step, and exactly `cooldown_ticks` ticks later it begins again and is the derivation at every frame. **RP7.2** the rebuild that an order asks for starts the cool-down: the order is left to the caller. **RP7.3** strikes that are spread out do not add up. **RP7.4** the cool-downs last 10, 20, 40, 40, 40 ticks (cap 40), between two it is on for one tick (the strikes of one do not count in the next), a first cool-down longer than the cap is the cap. **RP7.5** the budget counts computing and not waiting (30 ms of waiting in a timed block, budget 12 ms: no strike; 15 ms of computing: a strike; a thread clock that reads a whole tick, 15.625 ms, after 2 ms of work: no strike, the block costs its 2 ms), the statistics keep the wall time, the cost rule, and the edge (a block that costs exactly the budget is no strike, one nanosecond more is). **RP7.6** the real clocks, none installed: 15 ms of computing is a strike of a budget of 12 ms on every platform, 30 ms of sleep none where the thread clock is exact (not Windows, not the web build). **N3.30** a real machine, whose prediction has the manual clocks: 30 ms a block is under a budget of 50 (the budget reaches the prediction), three strikes of 60 ms start a cool-down of 40 ticks, it shows the confirmed engine and orders go the old way while the other machine predicts, then it begins again and predicts orders. RP8.6 pins the new defaults (and that a `Config` has no clocks installed).

**Mutants** (27, `tools/mutate.py`, edits of `prediction.cpp`, `prediction.hpp` and `netgame.cpp`; the unmutated tree passed all four baselines):

| Mutant | What it does | Result |
|---|---|---|
| `CD1_no_cooldown_started` | the strikes never start a cool-down | KILLED by RP7.1, RP7.2, RP7.4, RP7.5 |
| `CD2_one_strike_starts_it` | one strike is enough | KILLED by RP7.1 - RP7.5 |
| `CD3_strikes_never_age_out` | strikes of any age add up | KILLED by RP7.3 |
| `CD4_cooldown_never_ends` | the cool-down never ends | KILLED by RP7.1, RP7.4 |
| `CD5_cooldown_ends_at_the_next_tick` | the cool-down is ignored by the next tick | KILLED by RP7.1, RP7.4, RP7.5 |
| `CD6_cooldown_one_tick_short` | one tick too short | KILLED by RP7.1, RP7.4 |
| `CD7_cooldown_not_doubled` | every cool-down is the first one's length | KILLED by RP7.4 |
| `CD8_cooldown_not_capped` | the doubling has no end | KILLED by RP7.4 |
| `CD9_first_cooldown_not_capped` | a first cool-down longer than the cap stands | KILLED by RP7.4 |
| `CD10_strikes_survive_the_cooldown` | the strikes of one cool-down count in the next | KILLED by RP7.4 |
| `CD11_cooldown_not_counted` | `Stats::cooldowns` stays 0 | KILLED by RP7.1, RP7.4, RP7.5 |
| `CD12_cooldown_keeps_predicting` | the prediction is not stopped when the cool-down starts | KILLED by RP7.1, RP7.2, RP7.4, RP7.5 |
| `CD13_resume_ends_the_cooldown` | a resume after a suspension ends the cool-down | KILLED by RP7.1 |
| `CD14_cost_is_the_wall_time` | the budget counts the wall time | KILLED by RP7.5 |
| `CD15_cost_not_bounded_by_wall_at_the_call` | the budget counts the CPU time alone | KILLED by RP7.5 (a thread clock that reads a whole tick after 2 ms of work; on POSIX alone it is equivalent: the CPU time never exceeds the wall time) |
| `CD15b_cost_rule_is_the_cpu_time` | `work_cost_ns` is the CPU time alone | KILLED by RP7.5 |
| `CD16_thread_clock_stands_still` | the thread clock reads 0 | KILLED by RP7.6 (the only test that reads the real thread clock) |
| `CD17_statistics_are_the_charged_time` | the statistics take the charged time, not the wall time | KILLED by RP7.5 |
| `CD18_ticks_left_is_zero` | `cooldown_ticks_left()` says 0 | KILLED by RP7.1 |
| `CD19_default_cooldown_100`, `CD20_default_cooldown_cap_1600` | the defaults | KILLED by RP8.6 |
| `CD21_budget_threshold_doubled` | the budget is twice as large | KILLED by RP7.1, RP7.5 |
| `CD22_strikes_not_counted` | `Stats::over_budget` stays 0 | KILLED by RP7.1, RP7.2, RP7.3, RP7.5 |
| `N19_cooldown_ticks_not_given`, `N20_work_hook_not_given`, `N13_budget_not_given`, `N21_strikes_not_given` | `NetGame` does not give the prediction its cool-down, hook, budget or strike count | KILLED by N3.30 |

(The R5 mutants G1 - G8 and N13 of the table above were run against the permanent give-up; their successors are CD1 - CD22.)

### The prediction is made only where it is wanted

**Why.** The object was made at the start of every match and merely suspended when off: a default match carried an idle second engine and ran the application's cue router on every tick. Now a default match has no `Prediction` at all.

| Item | Where |
|---|---|
| `NetGame::refresh_prediction()` (once per update) **makes** the prediction when it is wanted and missing (`prediction_enabled_` and the match plays) and **destroys** it when the user switches it off (`drop_prediction()`); a suspension (hidden page, pause, host change, desync, cool-down) keeps the object. `begin_match` makes nothing. `set_prediction_budget`, the work hook and the clocks apply to the next one made | `netgame.cpp` |
| **`NetGame::set_on_prediction_dropped`**: told at once whenever the object is destroyed (switched off, the match left, the NetGame destroyed). The application points the HUD's special-target question (`HUD::set_sim_query`, new getter `sim_query()`) back at the confirmed engine then: it could point at the destroyed engine until the next frame (the review's INFO) | `netgame.hpp`, `application_menu.cpp` (`attach_net`) |
| **`CueRouter::idle()`** (no cue waits for its copy): the application runs the router while there is a prediction and, after it is gone, for as long as it is not idle, so that a cue that the predicted engine played is not played twice when the prediction is switched off before the confirmed engine makes its copy; a default match never runs it. The web probes 10 and 11 read 0 for a game without a prediction (the browser check expects 0 where nothing predicts) | `cue_router.hpp`, `application.cpp` |

**Tests.** N3.32 (tightened: no object at all in a default match, and nobody is told of a drop), N3.33 (a machine that was not asked has none; switched off at run time there is none left, switched on again a new one begins with one start; the callback is told once at the switch and once when the machine leaves), PA4 (tightened: no object, router idle), PA7 (a default match never runs the router: an own worker harvests and nothing is routed; switched off at run time while a cue waits for its copy, the copy is dropped, no second sound, idle afterwards), PA8 (after the switch-off, before any frame, the cursor over open ground with an own ant selected is evaluated, which reads the HUD's query, and the query is the confirmed engine: under AddressSanitizer the mutant that leaves it at the destroyed engine is a `heap-use-after-free` right there, the unmutated tree passes).

**Mutants** (16, `tools/mutate.py`; the unmutated tree passed all baselines):

| Mutant | What it does | Result |
|---|---|---|
| `LZ1_made_when_not_wanted` | the prediction is made and runs although it is not asked for | KILLED by N3.25, N3.26, N3.32, N3.33 |
| `LZ2_switched_off_is_only_suspended` | switched off, the object is suspended, not destroyed | KILLED by N3.33 |
| `LZ3_never_made` / `LZ4_made_again_every_update` | it is never made / made again at every update | KILLED by N3.25 - N3.33 |
| `LZ5_made_in_every_phase` | made without asking for the phase | SURVIVED: equivalent where the suites reach (a runner exists only while the match plays or is over, and no NetGame test loses its connection; an object made after the match would be suspended at once) |
| `LZ6_dropped_not_told` / `LZ7_leaving_does_not_tell` | the owner is not told when it is switched off / when the match is left | KILLED by N3.33 |
| `LZ8h_host_makes_it_at_the_start`, `LZ8g_guest_makes_it_at_the_start` | `begin_match` makes it again (it would live until the first update, then be dropped) | KILLED by N3.32 (nobody is told of a drop in a default match; they survived the first run, which only looked at the object a few seconds later) |
| `RT1_router_always_idle` / `RT2_router_never_idle` | `idle()` always / never true | KILLED by PA7 / PA4, PA7 |
| `AP1_router_only_with_a_prediction` | the router stops with the prediction (the copy is played again) | KILLED by PA7 |
| `AP2_router_always_runs` | the router runs in a default match too | KILLED by PA7 |
| `AP3_predicted_cues_without_a_prediction` | the predicted cues are asked of a prediction that is not there | KILLED by PA7 (a segmentation fault) |
| `HP1_hud_not_told` / `HP2_hud_pointed_at_nothing` | the HUD keeps / loses its engine when the prediction is dropped | KILLED by PA8 (HP1 also by AddressSanitizer: `heap-use-after-free`) |

### The mutants that depend on the default, run on the tree where the prediction is off by default

The default flip left the mutants that assumed an on default stale. Re-pointed and run (13, `tools/mutate.py`, all killed; the unmutated tree passed every baseline):

| Mutant | What it does | Result |
|---|---|---|
| `N17a_default_has_a_bias_RP8.6`, `N17b_default_has_a_bias_N3.31` | `lead_bias_ticks{0}` to `{1}` | KILLED by RP8.6 / N3.31 |
| `H1_prediction_wanted_ignored` | `set_prediction_enabled(prediction_wanted_)` to `(true)`: every match predicts | KILLED by PA4 (its default match and its settings cases) |
| `H2_no_prediction_option_ignored` | `--no-prediction` does not turn it off | KILLED by PA4 |
| `H3_settings_key_ignored` / `H4_settings_beat_the_command_line` | the settings' key is not read / wins over the command line | KILLED by PA4 |
| `D1a_netgame_default_on_N3.25`, `D1b_netgame_default_on_N3.32` | `NetGame::prediction_enabled_{false}` to `{true}` | KILLED by N3.25 / N3.32 |
| `D2a_netgame_switch_never_on_N3.25`, `D2b_netgame_switch_never_on_N3.32` | `set_prediction_enabled` never enables | KILLED by N3.25 / N3.32 |
| `N5_view_is_confirmed` | `view_engine()` is always the confirmed engine | KILLED by N3.25 |
| `D3_app_config_default_on` | `ApplicationConfig::prediction{false}` to `{true}` | KILLED by PA4 |
| `D4_app_wants_it_whatever_the_config_says` | `choose_prediction` starts from `true` | KILLED by PA4 |

(`Application::prediction_wanted_{false}`, the member's own initial value, is overwritten by `choose_prediction()` at every `init`: a mutant of it is equivalent.)

### The sanitizers (AddressSanitizer + UBSan, `./run_tests.sh --asan --sim` and `--asan --app`; CI has none)

This container's GCC 13 cannot build the tree with `-Werror` at -O0: it flags a few `(uint8_t mask >> p) & 1u` (-Wsign-conversion) in code that this branch did not touch (`match_stats.hpp`, `bot_controller.cpp`, `session.cpp`, `hud.cpp`, `application.cpp`, three tests; Release builds and clang are clean, so nobody saw it). The sanitizer tree was therefore configured with `-DANTS_WERROR=OFF`; the files that this branch changed compile without a warning there, in the Release tree with `-Werror`, and with clang 18 (`-Werror -Wshadow-all`).

**No sanitizer report anywhere**, in any suite of the two runs and in the direct runs. The prediction's own suites pass under the sanitizers: `test_prediction` 36 of 36 (RP7.x then still on the real clocks), the prediction cases of `test_netgame` 9 of 9, `test_engine_copy` (both tiers), `test_prediction_app` 8 of 8, `test_network_app`, `test_latency`, `test_jitter`, `test_start_menu_app`, the bots' suites. PA8's claim holds: the mutant that leaves the HUD's query at the destroyed engine is reported as a `heap-use-after-free` (HP1, above), the unmutated tree passes.

`--asan --app`: 27 suites, 26 pass. 3.9.1 (server end-to-end, rooms) fails its check "afterwards the server is idle again" in all three runs (1.0 s of server CPU in 3 s against a limit below 1.0, in the ping flood's or the start-request flood's check: the sanitized server, which also plays a match in another room, sits at that figure on this machine; the dedicated server does not run the prediction); the same part passes on the Release build, 53 checks, 0 failures. `--asan --sim`: 30 suites, 28 pass (2.11 `test_lockstep`, which this branch does not change, took 2.3 hours of wall time under the sanitizer on a shared machine). The two that do not pass are not this branch's: 2.18 `map_sweep --selftest` injects a segmentation fault on purpose and expects the tool to report death by SIGSEGV, and ASan's own handler turns it into an exit status (61 of 62 checks; known), and 2.17 `test_ws` W1.18 bounds the time the server needs for 2 million pings (5.2 s against 1.5 s on the loaded machine; the whole suite passes under the sanitizer when run alone, 24 cases, 0 failures, and in the quick tier).

**A lesson from the first sanitizer run of the application's prediction suite**: it failed 7 of 8 tests, because its objects had been compiled while a mutation run had a mutant applied to the shared sources (a build in the same checkout while `tools/mutate.py` runs sees the mutant). A clean rebuild (every changed file touched) passed. Do not build a second tree from a checkout while the mutation harness runs in it.
