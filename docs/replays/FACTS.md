# Replays and Watch mode: verified facts and measurements

What `DESIGN.md` rests on. Checked against main `b8d2603` (v0.1.2) and, where it says so, the branches in flight (`start-clock` read at b098d6c and again at cc73c42, `rollback` 847d0ba = R1, `bots-b4-1` 2e6c267). Paths are repository-relative. Nothing in the repository was changed; the tools that made the numbers are in `tools/` and `re/`, the raw streams in `measure/`.

## 1. What the repository has

| Fact | Where | Consequence |
|---|---|---|
| A match is its start data plus the commands of every turn. A turn is ONE tick (50 ms, protocol 8): its commands are applied in order, then the tick runs; a state hash every 20 turns | `ants_net/lockstep.cpp` (`update`, `fast_forward`), `protocol.hpp` (`kTurnMs`, `kHashEveryTurns`) | the file needs nothing else. "Turn t" = the t-th call of `tick()`, 0-based, NOT `current_tick()`: the call that ends a match does not advance it (`arena.hpp`) |
| The start data of a room is `StartMsg` (seed, map name, FNV-1a 64 of the map file, fog, roster, names) and `RoomMsg` (who sits where, `SlotState::Bot`) | `protocol.hpp` | every client already holds all that a header needs |
| `TurnLog` keeps packed turns only when a room holds seats (`if (cfg_.hold_seats) log_.append(turn)`), reconnect is off by default; 2 bytes an empty turn + 4 bytes of index; 16 MiB a room; freed when the match ends | `ants_net/turnlog.hpp`, `session.cpp:444` | it cannot be the replay: not always there, memory only, an index that a file does not need |
| The arena records `(step, tick, command)` and replays them into a fresh engine without a bot, comparing the hash at every 20th tick and at the end | `ants_ai/arena.hpp`, `arena.cpp` `replay_commands` | `step` IS the turn number; an arena log converts one to one. Every recorded match replayed ok in this work |
| Alliance commands work as the very first commands of a match (before any tick, in canonical or written order): 4 of 4 accepted, the alliances stand | `tools/ally_probe.cpp`, run on TREASURE | a 2v2 forms its teams at turn 0 by ordinary commands and the replay holds them |
| `SimulationEngine` copy exists on branch `rollback` (R1): 1 - 22 us native, 1.4 - 41 us best / 170 us worst in wasm; 124 KiB (TINY), 154 (SMALL), 244 (MEDIUM), 343 (GAUNTLET), 271 (TREASURE), 372 (ISLANDS) held per copy at tick 1500 | `rollback` 847d0ba, its `docs/audit/rollback_notes.md` | seek checkpoints. R1 must be on main first (complete: land it alone) |
| Bots: levels Easy 0.4, Medium 1.5, Hard 3 commands a second at most; kinds `idle`, `worker`, `standard` in the registry; there is no "style"; `SetupInfo::allow_all_bots` is "only the arena" today | `ants_ai/bot.hpp` | Watch's Level is `Level`, its Style the bot kind |
| Local game: the HUD's commands go to `sim.apply_command` (`HUD::command_sink_` null), a bot's through `LocalBotSink`, `confirm_quit` applies Quit itself; all between two ticks | `application.cpp`, `hud.hpp:431` | one recording sink catches them all |
| A local game's default name is `USER@host` (OS user and the machine's short name); a network game's `Player`; the browser's `WebPlayer` | `application.cpp:76` | a replay must not store the implicit name |
| The control interface answers any content type up to 1 MiB behind a bearer secret; `--results-dir` gets `<room code>.json` for ended non-demo rooms | `ants_ctl/http.hpp` `kMaxResponseBytes`, `ants_server/main.cpp:395` | the server's file limit is below 1 MiB |
| The viewer's building blocks exist: `is_paused_`, the 20 Hz accumulator in `update_simulation`, `load_match`, `HUD::set_command_sink`, `ScorecardModal::show(result, local)`, `ingest_simulation_events(events, local)` (drops cues aimed at another team), `MatchResult::rows(local)` (ties go to `local`: `rows(255)` = by seat order), the original's own chat-off cover (`chatcovr`), score slots `ScreenLayout::score_slot` (slot 0 top bar, 1 - 3 bottom) | `application.*`, `hud.*`, `scorecard.*`, `screen_layout.hpp`, `audio_mixer.cpp:380`, `match_stats.hpp` | a spectator HUD is a few gates, not a new screen |
| The web build: `ants_ai` is linked (no SDL, sockets or threads); the heap grows to 512 MB (`-sMAXIMUM_MEMORY`); the game's file system is exported (`FS`) | `src/ants_app/CMakeLists.txt:136` | bots, checkpoints and a file picker work in the browser |
| The page turns an address into game arguments through pure whitelisting functions (`ANTS_PAGE.joinArguments`, `antsFillArg`), tested by `tests/scripts/web_fill_check.js` | `web/shell.html` | `?watch=` and `?replay=` follow the pattern |

## 2. What the original has (a recorder and an observer playback of its own)

Checked with Capstone on the local copy of `Ants.exe` (`re/observer_check.py`, read only, nothing of the program is copied) and read in the local decompilation (never committed):

* `-I<file>` (the case at **0x100ca69**) sets `[W+0x4ae4] = 1` (OBSERVER) and stores the argument (at most 0x104 characters) at `[W+0x4ff7]`; `-O<file>` stores the name without the flag.
* The file is opened when `[W+0x4ff7]` is not empty (**0x100abcc - 0x100ac0b**), with the mode word `((observer == 0) + 1) | 0x100` (0x101 for an observer, 0x102 otherwise) through the file object's vtable +0xc; the handle goes to `[W+0x5330]`; a failure shows an error text.
* For an observer a `PLAYBACK` task (constructor **0x1024eba**, called at **0x100acbc** with the handle) is scheduled with the delay **0x3e8 = 1000 ms** (**0x100acc7 - 0x100acd9**).
* From the decompilation only: the message dispatcher `FUN_0100d791` writes one record for every handled message while a file is open and the machine is not an observer (8517 - 8560): a header of start values once, then the milliseconds since the last message and the message; many order paths are guarded by `[W+0x4ae4] != 0`. The scheduler ledger says Space toggles the playback (not verified here).

So the original records the MESSAGES a machine handles in a game that is not lock-step: such a file cannot be rebuilt from commands and the remake does not read it. The remake's file is its own. The original's switch names can be kept as aliases (`-O` = `--save-replay`, `-I` = `--replay`); a file with another magic is refused.

## 3. Measurements

**Streams** (`tools/measure_streams.cpp`: the arena with `record`, 4 bots on TREASURE, the whole 12-minute match = 14,405 turns; native Release, Apple clang, a 10-core Mac under other work):

| Stream | Commands | Ants a command | Play (with bots) | Re-simulate from the commands alone | Replay ok |
|---|---:|---:|---:|---:|---|
| 4 x worker, Medium (main) | 86 | 1.2 | 0.28 s | 0.23 s | yes |
| 4 x worker, Hard (main) | 76 | 1.2 | 0.31 s | 0.23 s | yes |
| 4 x standard, Easy (B4 tree) | 100 | 1.2 | 0.27 s | 0.23 s | yes |
| 4 x standard, Medium (B4) | 241 | 1.0 | 0.34 s | 0.23 s | yes |
| 4 x standard, Hard (B4), delay 0 / 3 | 1,479 / 1,476 | 1.0 | 0.98 / 0.60 s | 0.40 / 0.26 s | yes |

The re-simulation column is the arena's `replay_commands`, which also takes a state hash every 20 ticks (720 hashes of 100 - 150 us: about 0.1 s of it); without the hashes the engine needs about 0.17 s for the whole match (the seek probe below). Re-simulation costs 12 - 28 us a tick; with heavy orders (the rollback notes) 22 - 450 us on average and 2 ms at worst for a tick that runs a path-search slice; wasm is 1.2 - 1.4 times native. Match lengths of the shipped maps: TINY 7,200 ticks (6:00), SMALL 9,600 (8:00), MEDIUM and GAUNTLET 12,000 (10:00), TREASURE and ISLANDS 14,400 (12:00). Four standard bots of the B4 tree at the three levels did not fight in these matches (kills 0 - 1): the results counters in the pictures are samples.

**Sizes** (`tools/size_analysis.py`, `measure/sizes.json`; every compact stream is decoded back and compared with the original commands). "Human-like" rows are synthetic bounds with ids like a drag selection (30 % in click order).

| Stream | Turns | Commands | TurnLog packing | + its index | Wire form + gaps | **Compact** | zlib 9 of compact | xz of compact |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| worker Medium | 14,405 | 86 | 29,906 | 57,620 | 1,196 | **546** | 354 | 416 |
| worker Hard | 14,405 | 76 | 29,786 | 57,620 | 1,061 | **481** | 324 | 376 |
| standard Easy | 14,405 | 100 | 30,098 | 57,620 | 1,406 | **640** | 426 | 492 |
| standard Medium | 14,405 | 241 | 31,746 | 57,620 | 3,194 | **1,472** | 869 | 912 |
| standard Hard | 14,405 | 1,479 | 46,618 | 57,620 | 19,289 | **8,891** | 3,776 | 3,504 |
| 2 people, 2 cmd/s, 6 ants | 14,400 | 2,880 | 119,980 | 57,600 | 94,060 | **30,606** | 22,875 | 22,404 |
| 4 people, 3 cmd/s, 6 ants | 14,400 | 8,640 | 304,388 | 57,600 | 284,228 | **94,995** | 72,614 | 69,608 |
| 4 people, 4 cmd/s, 12 ants | 14,400 | 11,520 | 621,000 | 57,600 | 603,720 | **182,580** | 129,520 | 123,568 |

The four alliance commands of a 2v2 start cost 36 bytes in wire form and 12 compact. NETWORK_PORT.md's own number for busy play, 651 KB for 30 minutes in the TurnLog packing, agrees with the synthetic rows (260 KB for 12 minutes).

**Seeking with the real engine copy** (`tools/seek_probe.cpp` against branch `rollback` R1, 847d0ba; `measure/seek_probe.txt`; native Release on a machine under other work, so two runs differ by 10 - 40 percent): the recorded 12-minute 4-bot TREASURE streams (Easy, Medium, Hard of the B4 tree) were re-simulated in the rollback tree's engine and ended on the arena's own final hashes (45c6f13464d322e, 84131b7b7607fac5, e0708945ef6d30a4: **a match recorded by one tree replays in another**); then 2,000 random seeks (forward and back) per setting (300 for the Easy stream and for "none"), each compared with the straight run's hash after every turn:

| Copy every | Copies | Memory | Turns re-simulated (mean / max) | Seek time mean | p99 | max | Mismatches |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 200 turns | 73 | 14.6 MB | 101 / 199 | 1.1 - 1.5 ms | 3 - 5 ms | 4 - 7 ms | 0 |
| **400 turns** | **37** | **7.4 MB** | 204 / 399 | **2.6 - 3.2 ms** | 8 - 9 ms | 14 - 20 ms | **0** (4,300 seeks in the saved run) |
| 1000 turns | 15 | 3.0 MB | 494 / 999 | 5.4 - 7.6 ms | 13 - 20 ms | 13 - 25 ms | 0 |
| none (from turn 0) | 1 | 0.2 MB | 6,917 / 14,358 | 80 - 114 ms | 153 - 232 ms | 155 - 264 ms | 0 |

A copy holds about 200 KB on TREASURE (198 - 206 KB over a whole match: malloc's bytes in use before and after the copy). The whole match played with a state hash after every turn costs 2.2 - 2.8 s (the hash is 100 - 150 us a call). A whole Hard match skips ahead with its bots in about 1 s native, 1.3 s wasm (68 us a tick including the bots, about 90 us in wasm: 8x speed is 14 ms of work a second).
