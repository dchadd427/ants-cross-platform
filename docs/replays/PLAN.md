# Replays and Watch mode: build plan

Companion of `DESIGN.md` (the decisions) and `mockups/` (the pictures). Estimates are lines of C++ / JS unless said otherwise, counted from the size of the neighbouring code (the wide-pages build is the yardstick: about 450 production lines for four screens). Nothing here is started: the owner approves the pictures first (AGENTS.md rule 7), and the build starts when `start-clock`, `edge-pan` and `wide-pages` have landed. The steps are written to be given one at a time to an agent on its own branch, each ending in `./run_tests.sh --fast` and a pushed branch for CI.

## 1. Preconditions and what waits for what

| Needs | Why |
|---|---|
| **R1 of branch `rollback`** (847d0ba: the engine copy) on main, alone | seek checkpoints. It is complete, tested and mutation-proven; it changes only `ants_sim` (value semantics of `SimulationEngine`, `PathManager`) and adds suite 2.25 / 2.26. R2 - R6 (prediction) are not needed |
| the owner's approval of `mockups/overview.png` | his rule for anything visible |
| `start-clock` on main (protocol 12) | the header's `engine_rules` is then 12; the match starts when the dialog closes: `Application::enter_match` / `update_simulation`, `NetGame` start, `Room::begin_match` change, and so does what "turn 0" means for the recorders; the bots' hold ends when the simulation starts (the watch asks nothing special of it) |
| `edge-pan` on main | `Application`'s pointer code (`handle_mouse_*`, the pointer-outside rule, fullscreen) and `web/shell.html`: the viewer's controls are clicked and its camera scrolls at the edges and over the bars |
| `wide-pages` on main | the start menu's three groups (`page_layout.hpp`, `wide_page.hpp`) that the two new panels are laid out with, and the wide results page (`results_layout.hpp`) that the viewer's results page is a variant of |
| `bots-b4-1` (v0.3.0) for ONE step (A5, the arena's flag) | `tools/bot_arena.cpp` is edited there |

**State of the three branches when this was written:** `start-clock` at cc73c42 (five commits: protocol 12, `HostSession::Config::start_delay_ms` and `kMatchStartDelayMs` make the first turn follow the match's start by the dialog's 5 s; the turn numbers still begin at 0 and nothing in `lockstep.*` changed, so the recorders' tap and the header's `engine_rules` = 12 stand), `wide-pages` (ae0f577 .. 1e16cde) and `edge-pan` (503fd06 .. 61d09fe) are stacked on it; `rollback` has moved past R1 (1ca75d1; R1 is the part this plan needs).

What can start before all that: **phase A** (a new library: new files, one `add_subdirectory`, one suite line in `run_tests.sh`; the player's checkpoints need R1). Everything in phases B and C edits `application.cpp`, `hud.cpp`, `lockstep.*`, `netgame.*`, `room.cpp` or `shell.html`, which the three branches (and later rollback R2+) also edit: it waits.

## 2. Phase A: the file, the recorder, the player (library `ants_replay`, no SDL, no sockets, builds for the web)

New: `include/ants_replay/`, `src/ants_replay/CMakeLists.txt`, `tests/test_replay/`. Depends on `ants_sim`, `ants_assets` and `ants_ai` (BotSpec, BotController for the live watch). Native, web and server builds all take it.

| Step | Files | Production | Tests | What it is |
|---|---|---:|---:|---|
| **A1 format** | `replay_file.hpp/.cpp` | 450 | 700 | `ReplayHeader`, `ReplaySeat`, `ReplayResults`, `Replay`; the chunked container, CRC32, varints, the compact command coding, `encode`, `decode` (bounded, strict), `read_header`, `read_tail`; the refusal texts of the version rules; the golden files `tests/data/replays/*.antsrep` (a 10 s scripted match, a 12-minute 4-bot match, a 2v2 with alliances: 1 - 10 KB each) |
| **A2 recorder** | `recorder.hpp/.cpp` | 230 | 400 | `ReplayRecorder`: `begin(header)`, `on_turn(turn, commands)` (network) or `RecordingSink` (a pure `CommandSink` that applies to an engine and records at the recorder's own turn counter: it counts the calls of `tick()`, because `current_tick()` does not advance on the call that ends a match, which is what the arena's `step` is for), hash every 100 turns, the 900 KiB cap and "incomplete", segments, `finish(engine)`; the name rules (`USER@host` -> `Player`, printable ASCII, 32), bot kind / level from a name "Bot (Hard)" |
| **A3 player** | `player.hpp/.cpp` | 350 | 500 | `ReplayPlayer`: `start(Replay, LevelData)` (the same `init` call as a match: roster, `for_roster`, seed), `step`, `seek(turn)` with a checkpoint every 400 turns (at most 64, thinning), slices, the hash comparison and its message, the news log (tick + text) that the HUD's chat is rebuilt from; without R1 it seeks from turn 0 |
| **A3b watch session** | `watch.hpp/.cpp` | 500 | 450 | `WatchSpec` (format, map, sides, seats with kind / level / random, seed), `resolve()` (roster, `BotSpec`s, the four alliance commands of a 2v2, `check_setup` with `allow_all_bots`), `WatchSession`: the live engine, the `BotController`, a `RecordingSink`, the frontier, "rewound" and "skip ahead in slices", the `Replay` it records |
| **A4 folder** | `folder.hpp/.cpp` | 220 | 300 | `ReplayFolder` over `std::filesystem`: list (head + tail only), atomic save (`.part` then rename), `.part` recovery, prune by count / MB / days (never the file just written), delete, the name scheme, UTC times |
| **A5 tools** | `tools/replay_tool.cpp`, `tools/bot_arena.cpp`, CMake, `run_tests.sh` | 450 | 250 | `replay_tool info / verify / list / anonymize / trim`; `bot_arena --save-replays DIR [--save-which first\|all\|check-failed\|seeds=LIST] [--save-max N]` (`turn = step`, the hashes every 5th of the arena's), the new suite registered (quick) |
| | | **~2,200** | **~2,600** | |

Tests of phase A (deep tier: each is shown to fail without the code it tests; the mutants are listed in the commit message):

* **RP1 format.** Round trip of header, commands, hashes, results for random streams: every command type, 0 - 32 ants, tiles at the `int16` limits, issuers 0 - 255 (the raw form), 100,000 commands; lossless against `sim::encode`/`decode`. Refusals, each with its message: every prefix of a golden file, a bit flipped in every chunk (CRC), a wrong magic, an unknown critical chunk, an unknown skippable chunk (skipped), a length beyond the file, a count beyond its limit, 33 ants, a trailing byte after `ENDS`, `format_version` above, `engine_rules` unequal, a map hash unequal; a 100,000-mutation fuzz under ASan and UBSan (no crash, no hang, every result is "refused" or "plays and hashes").
* **RP2 reproduction.** For every shipped map: 4 bots (worker, Medium, B4's Hard when it lands) play with `record`, the file is written and read back, the player reproduces every 100-turn hash and the final hash and the results rows (equal to the arena's `replay_commands`); the recorder leaves every state hash of the match unchanged at every turn; the 900 KiB cap sets "incomplete" at the right command and the file still reads; a command applied AFTER the call that ends the match (a late Quit or Drop) is recorded at the right turn and the replay ends on the same hash.
* **RP3 seeking.** `seek(t)` equals running to `t` (the state hash, the whole `WorldState` fingerprint of the rollback tests) for every t in 0 - 500 and 300 random targets, forward and backward, before and after a checkpoint, repeated; the copy count never passes 64 and the memory is reported (bytes a checkpoint, seek times: `--bench`; the prototype measured 200 KB a copy, 37 copies, 3 ms on average and 20 ms at most on a 12-minute TREASURE match; no timing is asserted: CI machines vary); the news log gives the same chat lines as a straight run at that tick.
* **RP4 folder.** Retention by count, by MB and by days on a virtual clock; the file being written is never removed; a crash between `.part` and rename leaves a recoverable `.part` and no half file with a real name; name collisions; a folder that cannot be written is reported, never a crash; 1,000 files list in under 50 ms.
* **RP5 watch.** Each format starts as it says: 1v1v1v1 roster 0x0F and no alliance; 1v1 two seats with hills and the others none (`validate(roster)`); 2v2: the first four commands of the file are the alliance commands at turn 0 and `get_ally_id` agrees at tick 1; each format's replay reproduces; "Random" levels are fixed by the seed (same seed same match, the file names the concrete levels); a map that the roster cannot play and Fog of War are refused with the reason; a live watch that rewinds and goes on ends with the same final hash as one that never rewound; skipping ahead equals playing.
* **RP6 tool.** `info`, `verify` (exit codes for ok / differs / damaged), `anonymize` (names gone, `meta` gone, hashes still verify), `trim`.

## 3. Phase B: the application (after `start-clock`, `edge-pan`, `wide-pages`)

| Step | Files | Production | Tests | What it is |
|---|---|---:|---:|---|
| **B1 recording** | `lockstep.hpp/.cpp`, `netgame.*`, `session.*` (tap), `application.hpp/.cpp`, `include/ants_app/replay_recording.hpp` | 400 | 500 | `LockstepRunner::set_on_turn_tap(turn, commands, engine)`, called in `update()` and `fast_forward()` (it travels with the runner through a host migration); `ants_replay`'s `RecordingSink` wired in as the HUD's sink, `LocalBotSink` and `confirm_quit`'s apply, the application counting its `tick()` calls into the recorder; the recorder's life in `enter_match` .. match end / leave; the desktop saves to `ReplayFolder` (`replays/` of the per-user folder; the newest 100 / 64 MB), the web keeps it in memory; the settings `replays` (on / off) and `replays_keep`; `--no-replay`, `--save-replay FILE` |
| **B2 viewer** | `hud.hpp/.cpp`, `hud_input.cpp`, new `include/ants_app/replay_controls.hpp` + `src/ants_app/replay_controls.cpp`, `scorecard.hpp/.cpp`, `results_layout.*` (wide-pages), `application.hpp/.cpp` | 1,150 | 900 | `HUD::set_spectator` (view seat, no pedestals, no selection, the chat cover, null sink, hints in the status line); the controls (model, layout from `mockups/spectator.py`'s numbers, drawing with the menu's plates, hit zones, keys); the application's modes (`Watch`, `Replay`): the tick loop at `dt x speed` (at most 16 a frame), pause, seek in slices, the news log, sounds by speed, `ingest_simulation_events(.., 255)`, `open_replay`, `start_watch`, Quit leaves without a command; the spectator results page (info box, Watch again, New match, `rows(255)`, the fanfare) |
| **B3 command line** | `application.cpp` (`parse_arguments`), `docs` | 150 | 250 | `--watch`, `--replay`, `--verify-replay`, `--speed`, `--at`, `--sides`, `-I` / `-O`; these are "modes" (no start menu); `--screenshot` works with them; refusal messages |
| **B4 start menu** | `start_menu.hpp/.cpp`, `start_menu_view.cpp`, `application_menu.cpp`, `config_store` keys | 700 | 600 | the first panel's two buttons (pitch 60), the panels Watch bots and Replays (model: keys, mouse, cyclers, refusals, scroll; view: the controls of `mockups/watch_menu.py`), `MenuRequest::Type::Watch / Replay`, the `watch_*` settings, the delete question, Open folder (`SDL_OpenURL`) |
| | | **~2,400** | **~2,250** | |

Tests of phase B:

* **RB1 (deep: the recorders agree).** A local game with bots played through the `Application` (headless) writes a file equal to the arena's for the same match; a LAN loopback match (host + 2 guests) writes three files with the same commands and hashes; the same with a rejoin (the runner's `fast_forward` feeds the tap) and a host migration (the tap survives); a match with `Drop` and `Quit` commands; the recording does not change a single hash.
* **RB2 (deep: the spectator cannot act).** 2,000 frames of random clicks and keys in `Watch` and `Replay` mode: zero commands recorded, the final hash equals a plain replay's; `confirm_quit` sends no command; the null sink answers "ignored"; speeds 1 / 2 / 4 / 8 run the same ticks as straight play (hash at the end), sounds play at 1x - 2x only (the mixer's queue), pause stops the accumulator, a seek leaves no cue behind.
* **RB3 (quick: pictures and zones).** The spectator HUD, the viewer's results page and the six start-menu pictures are compared with the mock-up PNGs pixel for pixel outside the text (as `test_wide_setup` does: the same compositor numbers, regenerated with the suite's own procedure and listed with their reasons), the hit zones of every control, keys, the first panel's new selection order, the Replays list over a folder of 0, 1, 9 and 120 files (scroll, delete, the question), the classic 4:3 picture of these screens (the menu panels must fit 640 x 480).
* **RB4 (quick: command line).** A table of arguments: valid, invalid, the combinations that are refused (`--watch` with `--host`, with fog, with `--bot` on a seat that the format does not play), `--replay` of damaged, newer and other-rules files with their messages, `--verify-replay` exit codes.

## 4. Phase C: server and web (after `start-clock`, `edge-pan`)

| Step | Files | Production | Tests | What it is |
|---|---|---:|---:|---|
| **C1 server** | `room.hpp/.cpp`, `room_manager.*`, `src/ants_server/main.cpp`, `control.hpp/.cpp` | 570 | 600 | the recorder on the referee's runner tap (not `TurnLog`); the file at the end of a non-demo room and for running rooms on SIGTERM ("incomplete"); `--no-replays`, `--replays-keep`, `--replays-max-mb`, `--replays-days`, `--replay-demo`, `"record": false` in the room specification; retention at start and after each write; `GET /replays`, `GET /replays/<file>`, `DELETE /replays/<file>`, `"replay"` in the room status and the result JSON; the file-name rule |
| **C2 web** | `web/shell.html`, `application.cpp` (web glue), `tests/scripts/` | 300 | 300 | `ANTS_PAGE.watchArguments` (whitelist, like `?fill=`), `?replay=` (same-origin path, fetched, at most 1 MiB, written to the game's file system), the Watch bots and Replay bar, the file picker, the Download button (Blob), exported `ants_open_replay` / the ready callback; the "Leave the match to watch the replay?" question |
| | | **~870** | **~900** | |

Tests of phase C:

* **RS1 (deep: the server's files).** Retention by count / MB / days with a virtual clock (oldest first, never the file just written), atomic write (no half file with a real name after a kill), a name collision gets `-2`, demo rooms record nothing, `--no-replays` and `"record": false` record nothing, a room cut by SIGTERM writes an incomplete file that plays to its last segment, the 900 KiB cap, the file equals the guests' recordings of the same match (commands and hashes), and the room status names it.
* **RS2 (deep: the control interface).** Without the secret: 401 for every new route, nothing else learned; with it: list order and fields, GET returns the bytes of the file, DELETE removes it and a second DELETE is 404; path attacks (`../`, `%2e%2e`, `\`, absolute paths, a NUL, a name that does not match the rule, a 200-character name) never reach the file system (404 / 400); a file above 1 MiB cannot exist; nothing in a response or a file holds an address, a token, a key or a room code beyond the file name.
* **RW1 (quick, node).** A table of addresses for `watchArguments`: every whitelisted word, every other word dropped, a replay path with `..`, `//`, a scheme, a query, 201 characters; the output is identical for `levels=HARD` and `hard`; nothing outside the whitelist reaches the game's arguments.
* **RW2 (full, opt-in browser check).** A headless Chrome against the web image: `?watch=2v2` shows the spectator HUD, the match runs, Download gives a file that `replay_tool verify` accepts; `?replay=` and the file picker play it; speeds and seek work; a damaged file is refused with its message and the game stays usable; the privacy rule (no name but the ones typed).

**Totals:** about **5,500 lines of production code** (A 2,200, B 2,400, C 870) and **5,700 lines of tests** (A 2,600, B 2,250, C 900), plus about 400 lines of documents (`docs/REPLAYS.md`, the replay sections of NETWORK_PORT.md and BOTS.md, README, CHANGELOG, the original's observer facts in GAME_REVERSE_ENGINEERING.md). The pictures give the numbers of every control (`mockups/rects/`), so the screens need no design work.

## 5. Release split

| Release | Contents | Version | Gates |
|---|---|---|---|
| **1a** (a batch, no player-visible change) | R1 on main, phase A | PATCH or none | full suite once; the new suite quick; ASan + UBSan on RP1 / RP3; mutation list in the commit |
| **1b** | B1, B2, B3: every local, LAN and online match is recorded to the replays folder; `ants --watch`, `--replay`, `--verify-replay` work; the spectator HUD and results page | MINOR (player-visible) | deep tier for B1 / B2 (RB1, RB2 mutants), the picture tests, a real run of a 12-minute 1v1v1v1 watch in a window and a screenshot comparison with the pictures |
| **1c** | B4: the two panels and the first panel's buttons; README, CHANGELOG, STATUS | the same MINOR (or the next) | RB3, the classic 4:3 check, one combined quick review of the screens |
| **2** | phase C: server recording, the control interface, the web bar and addresses | MINOR | deep tier for C1 / C2 (RS1, RS2), the web image and the server image built, RW2 on the web image, a real room recorded and fetched through the control interface by `curl`, the retention run on a real folder |
| **Later** (not planned) | hill-jump keys, a view with one team's fog, `mapd` (a map inside the file), a persistent web list (IDBFS), a signature for server files, chat in replays (private: only with consent), live spectating of a server room (a protocol change) | | |

No release changes the network protocol, the simulation's rules or a golden hash: the tap is a read of what the runner already executes; the recorder is observation only, and a test pins that.

## 6. Risks and how the plan meets them

| Risk | Answer |
|---|---|
| A recorder that changes the match (a hook that allocates in the runner, a different order) | the tap runs after the tick and reads only; RB1 pins the hash at every turn with and without a recorder; mutants: tap before the tick, a command recorded twice |
| A viewer that sends a command by mistake | three layers and RB2 (null sink, HUD gates, `confirm_quit`) |
| `application.cpp` is edited by five branches | phases B and C wait for them; the application changes are kept to the match entry (`enter_match`, `update_simulation`), the mode switch and the glue, the rest lives in new files (`replay_controls`, `replay_recording`, `ants_replay`) |
| Memory of checkpoints on the web | at most 64 copies of about 200 KB (TREASURE; the rollback notes give 372 KiB for the biggest shipped map: 24 MB worst case), 7.4 MB for a 12-minute match, the heap grows to 512 MB; `--bench` prints bytes a copy on each map; thinning keeps the cap for long matches |
| A long skip freezes the page | slices of at most 8 ms a frame; "Seeking" shown; a Hard match skips in about 1 s native, 1.3 s wasm |
| Names leaving the machine | the rules of DESIGN section 5, one test per rule (implicit `USER@host`, file names, `anonymize`, the server never writes an address / token / key / chat) |
| Old files after a rules change | the refusal message names the game; golden files make the next bump a test failure instead of a silent drift; `kReplayCompatibleRules` is a verified list, never a guess |
| The 2v2 alliances breaking (a bot withdraws) | the bots of this tree never break; the file shows it if one does; RP5 asserts the alliances at the end of the whole-match runs of today's bots |
| Windows | `std::filesystem` with UTF-8 paths; the per-user folder from `SDL_GetPrefPath`; file names are ASCII and UTC; no `:` in a name |

## 7. Acceptance, in the owner's words

1. Start menu, Watch bots, 1v1v1v1 on Treasure, Hard / Medium / Random / Easy: the picture of C appears, at 0.5 zoom, with all four scores; speeds and the bar work; at 12:00 the results page of D appears.
2. The match is in Replays (B); Watch plays it again, hash for hash, and `ants --verify-replay` on the file says so.
3. 1v1 and 2v2 start as A2 and A3 show; in 2v2 the chat log says that the teams formed.
4. Play a match against a bot yourself, then find it under Replays; open the web page, `?watch=1v1`, and Download this match gives a file that the desktop opens.
5. On the server (release 2) a finished room has a file; the lobby lists and fetches it with its secret; nothing is public.
