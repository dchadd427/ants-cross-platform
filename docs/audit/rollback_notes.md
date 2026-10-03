# Rollback for one's own orders: what was built, measured, and where it differs from the design

Update for section 74 of `implementation_plan.md` (that file is not part of the repository; the owner copies this text into it). Client-side prediction with rollback of one's own commands in network matches: the confirmed lock-step simulation, the server's sealing and referee, the network protocol and the rules stay as they are. The steps land one after the other; this file grows with them (R1 the engine copy, R2 the prediction core, R3 the screen, then the cues, the switches and the measurements).

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
| Suite 2.27 `test_prediction` (quick: 20 tests, a rig that plays the server and an oracle engine, about 15 s) and N3.24 - N3.28 in `test_netgame` (real sockets, three machines) | `tests/test_net/test_prediction.cpp`, `tests/test_net/test_netgame.cpp`, `run_tests.sh` |

### What the tests hold the prediction to

- **Exactness**: with the lead of the scenario and nobody else giving commands, the predicted engine equals the ORACLE (a second engine that runs every turn the moment the server seals it) at every frame, is never rebuilt, and an order stands at the tick the turn gives it (RP2.1: 3 buffers, 3 lags, 2 uplinks each). With a jitter buffer deeper than the lead the turns in hand are certain input: whatever the other players do (hatching and the alliance commands too) the predicted engine is the oracle with no rebuild and no guess (RP2.2).
- **Corrections cost what they should**: another player's command costs one rebuild when its turn arrives and the picture is right from then on (RP3.1: the ants that move in the correction are the other player's, by a few pixels); an own order sealed earlier than it was applied costs one rebuild, later two (its tick passes without it, then its turn comes), unless it lands where the first miss put it, and the classification of every rebuild (an own order's timing, a turn that passed without it, another player's command) is pinned per shift (RP3.2); an order that the server never seals is dropped after `pending_timeout_ticks`, every turn that passes it costs at most one rebuild until then, and the frame of the drop is already right (RP3.3, with the derivation check at every frame); the first of two orders that is lost does not hold up the second (RP3.4). A lead that is too low is chased turn by turn: one rebuild for each turn that passes the order, exactly `uplink + lag` of them (RP2.4, below).
- **Derived state** (the property that makes it safe): the predicted engine that was advanced tick by tick equals the engine that a rebuild would make now (`derived_hash`), at every frame, under random jitter, stalls, bursts, orders of every kind from every seat and a lead that changes (RP4.1: 24 random matches; RP2.4, RP3.3 and RP5.5 check it at every frame).
- **Convergence**: after a burst of random commands from every seat and some quiet turns, the prediction is the oracle again (RP4.2).
- **No effect on the match**: the same match with and without the prediction has the same confirmed state at every tick, the same turns and the same cues (RP4.3); every network suite and golden hash passes unchanged.
- **States**: suspended the prediction is gone at once, resumed it begins at the next tick (RP5.1); the end of the match (RP5.2); the cues and news of the predicted ticks are kept once, stamped with the tick and a generation (a rebuild's replay is a later one), bounded (RP5.3), and a rebuild does not turn what the confirmed engine has queued into predicted cues (RP5.4); a tick that no hook announced makes the prediction stale (RP5.5).
- **In real NetGames** (N3.24 - N3.28): on by default in every machine of a match, off where the user turned it off; orders from every seat; every confirmed engine ends identical; an order given through the HUD's sink is in the view engine in the same call and in the confirmed engine at least a turn later; the switch works at run time; a host change switches it off while the guests elect and it begins again under the new host; the application's switch and a held runner (a pause) turn it off and it begins again at the next tick; a machine whose match went out of sync shows the confirmed engine.

### Mutants (each is a temporary edit of the source that the suites must fail)

`Q1 - Q20` edit `prediction.cpp` and are run against `test_prediction`; `N1 - N12` edit `netgame.cpp` and are run against the prediction cases of `test_netgame` (`ANTS_TEST_FILTER=Prediction:`). The harness rebuilds from nothing for every mutant and runs unmutated baselines between them (R1's lesson). The first run had four survivors in the Q list and two in the N list; the tests written for them are RP2.4 (a lead shorter than the buffer: the order must be put behind the turns in hand, chased, never run twice), RP3.2's pinned classification, RP3.3's per-frame derivation check, RP5.4 (events of the confirmed engine's queue), RP5.5 (a tick no hook announced), N3.27 and N3.28. The N list's first two survivors (the election and the pause) were two terms of the gate that the other terms repeat (a client session that elects is not in its Normal mode; a session that is paused holds its runner): the redundant terms are gone and the mode and the runner's `held` are what N3.26 and N3.27 test. The first run also found a bug, not a survivor: RP3.3 showed that a lost order was timed from the tick it had last been put at, and the chase moves that tick on at every miss, so it was never dropped (`Pending::born`, mutant `Q4`).

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
| `N1_submit_not_predicted` | NetGame::submit does not hand the order to the prediction | KILLED by N3.24, N3.25, N3.26 |
| `N2_tick_hook_missing` | the runner's tick is not forwarded to the prediction | KILLED by N3.24, N3.25, N3.26, N3.27, N3.28 |
| `N3_turn_hook_missing` | the runner's turns are not forwarded to the prediction | KILLED by N3.24 (rerun with a compiling form of the same edit) |
| `N4_session_mode_not_a_suspension` | a host change, a rejoin (a session that is not in its Normal mode) does not switch the prediction off | KILLED by N3.26 |
| `N5_view_is_confirmed` | view_engine() always answers with the confirmed engine | KILLED by N3.24, N3.25, N3.26, N3.27 |
| `N6_switch_ignored` | the user's switch does not turn the prediction off | KILLED by N3.24, N3.25 |
| `N7_no_prediction_made` | the prediction is never created | KILLED by N3.24, N3.25, N3.26, N3.27, N3.28 |
| `N8_held_not_a_suspension` | a held match (a pause) keeps predicting | KILLED by N3.27 |
| `N9_desync_not_a_suspension` | a machine that is out of sync keeps predicting | KILLED by N3.28 |
| `N10_app_switch_ignored` | the application's switch does not turn the prediction off | KILLED by N3.27 |
| `N11_wrong_seat` | the prediction is made for seat 0 whoever the player is | KILLED by N3.24, N3.25, N3.26 |
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
| Suite 3.21 `test_prediction_app` (quick, about 4 s): a headless Application hosts a room, a bare machine joins, SMALL.LVL, a real match over loopback | `tests/test_app/test_prediction_app.cpp`, `tests/test_app/CMakeLists.txt`, `run_tests.sh` |

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
