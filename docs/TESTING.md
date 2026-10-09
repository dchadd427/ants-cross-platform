# Testing and CI

How the game is tested and what CI runs for every pull request. `./run_tests.sh` runs every suite (decoders, simulation, network, server, bots, the application's screens, view fingerprints, an opaque-box E2E runner and the repository checks) and prints each result and time; GitHub Actions runs the same suites for every pull request, and also builds on Windows and builds the web and server images. Every suite keeps a 100% pass rate ([`AGENTS.md`](../AGENTS.md), rule 2); the counts of tests and assertions are printed by `./run_tests.sh` and are not kept in documents.

Prerequisites and how to build: [`BUILD_AND_RUN.md`](BUILD_AND_RUN.md).

## The three tiers

The tests come in three tiers. [`WORKFLOW.md`](WORKFLOW.md) ("The three test tiers") says when each one runs.

- **Quick**, for every change: `./run_tests.sh --fast` (below).
- **Full**, for every pull request: the full matrix of GitHub Actions, the one full gate ([Continuous Integration](#continuous-integration)). `main` takes a change only through a pull request whose checks pass.
- **Deep**, for changes to the network, the rules or fairness: mutation batteries (`tools/mutate.py`), AddressSanitizer, soak runs and browser checks.

## Quick tier (every change)

```bash
./run_tests.sh --fast
```

It builds what it needs and runs the asset, simulation, network-core, bot and application suites that finish in seconds, plus the repository checks (the version and changelog consistency check and the python tests of `tests/scripts`).

On a 10-core Mac the test time is about 25 seconds in the default parallel run (about 100 seconds one suite after the other); the build comes on top.

It leaves out the E2E runner, the script suites that start the game, the sanitizer and the slow suites (the lock-step core, the control interface, the map sweep, the dedicated server and its way back, the worker bot, the whole-match engine copies, the network application, the start menu application and the way back in the application). The table below marks every suite that only the full run has, and `./run_tests.sh --fast --list` names what the quick tier runs.

## Master Test Suite

To run all test suites:

```bash
./run_tests.sh
```

Independent suites run side by side, up to as many as the machine has cores. Each suite's output is printed in the order of the table, and the result of every suite is the same as in a serial run. [`WORKFLOW.md`](WORKFLOW.md) ("The three test tiers") describes the parallel runner.

It runs the same suites as the Linux and macOS jobs of CI. CI also builds on Windows and builds the web and server images ([Continuous Integration](#continuous-integration)).

On Windows, `run_tests.bat` runs only a part of the suites (1, 1.2 - 1.5, 2, 2.1, 2.2, 3 and 4). The other test programs are built by the same CMake project, and `ctest -C Release --output-on-failure` in the build folder runs them all.

## Running Specific Suites

```bash
./run_tests.sh --all      # Everything (the default)
./run_tests.sh --assets   # Asset decoders, movement-table parity with the original program (from a local copy of Ants.exe when there is one; its bytes are always checked against pinned digests) and the asset challengers (suites 1, 1.1 - 1.5)
./run_tests.sh --sim      # Simulation rules, golden action suites, movement differential, command layer, the network (lock-step, room, TCP, LAN, WebSocket), dedicated server, computer players (suites 2.x)
./run_tests.sh --app      # Application integration, render, HUD, status, input, pointer, options, start menu, zoom, touch and rejoin suites, and the script suites (suites 3.x)
./run_tests.sh --e2e      # Opaque-box E2E test runner (runs against its own model, see below)
./run_tests.sh --tools    # The repository checks: version / changelog consistency and the python tests of tests/scripts (suites 5.x)
./run_tests.sh --fast     # The quick tier (above); combines with the others: --sim --fast runs the quick simulation suites only
./run_tests.sh --list     # Name the suites the other options select (with their tier), and run nothing
./run_tests.sh --jobs 4   # At most 4 suites at a time (the default is the number of cores); also -j 4 and --jobs=4
./run_tests.sh --serial   # One suite at a time, the output live (the same as --jobs 1)
./run_tests.sh --asan     # Build in build_asan and run with AddressSanitizer and UBSan
./run_tests.sh --clean    # Remove the build directories and rebuild first (also --rebuild)
./run_tests.sh -v         # The E2E runner also prints a [PASS] line for every test that passes (also --verbose)
./run_tests.sh -h         # The usage (also --help)
```

A test program that reads `ANTS_TEST_FILTER` runs only the cases whose title contains its text, for work on one case (`ANTS_TEST_FILTER="N3.1 Room" ./build/tests/test_net/test_netgame`); `test_ai_worker` also reads `W_ONLY` and `W_SKIP`, comma-separated case numbers (`W_ONLY=AI3.4,AI3.7`). `./run_tests.sh` removes these variables for its suites, so a filter left in the shell never reaches a master run.

## What the Suites Cover

The numbers are the ones `./run_tests.sh` prints; `./run_tests.sh --list` names every suite with its tier. A suite marked "full tier" is not in `--fast`. Section numbers (for example "section 5.32") are those of [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md), where the original program's rule that a suite checks is written down.

| Suite | What it checks |
|---|---|
| 1 Asset decoders | Decoding of `ants.chd` (header, palette, sprites, audio, event tags, Table 4 animations) and of the `.LVL` maps, directional mirroring, and fuzzing with damaged input. |
| 1.1 Movement tables | The generated locomotion tables and clips equal the static tables of the original program (section 5.32; the action clip tables: section 5.33.10), read from a local copy of `Ants.exe` when there is one and otherwise from the remake's own tables laid out like the program's; in both cases every region is first checked against its pinned SHA-256 digest. The clips also equal Table 4 of `ants.chd`. |
| 1.2 - 1.5 Asset challengers | Adversarial second opinions on asset decoding: every sprite and its mirroring, compass facings, the level loader's interface and in-place mirroring buffers. |
| 2 Simulation rules | The engine's rules: clock, random numbers, unit attributes, combat, placement, bombs, fire, bridges, hills, thieves, alliances, scoring, match end and the result rows. |
| 2.1 - 2.2 Challengers | Adversarial scenarios for combat and hazards, and for the lifecycle, economy and alliances. |
| 2.3 Path planner | The port of the original `PATHMGR` A* path planner (section 5.32). |
| 2.4 Movement golden | Frame-exact walking timings from a reference model, plus blocking, bumping, terrain and solid bits. |
| 2.5 - 2.9 Action suites | Golden cases from the disassembly for hill actions, combat actions, abilities, power-ups and food (sections 5.35 - 5.38 and 5.40). |
| 2.9.1 Level defaults | What a level file says that the simulation must obey as the original does (sections 4.4, 4.5 and 5.62): the level's default ant type, and power-ups and flower droppers recognised by tile id (community maps included). |
| 2.10 Command layer | The command codec (fuzzed), validation, canonical order, rosters, drop-out and quit; engines fed the same commands stay bit-identical; the state hash field by field. |
| 2.11 Lock-step core (full tier) | The lock-step network core without sockets: protocol fuzzing, sequencer, runner, matches over simulated links, desync detection, lag policy, flood control, the turn log, team chat, drop-out and host migration, the hold that keeps a lost seat (presence, the vote, the catch-up, the resume countdown), and a host restored from its predecessor's turns (the attendance of a restart, the hooks that let a server write a turn before it is sent). |
| 2.12 Room | The room: joining, roster, map and fog, seat requests, the start barrier, connection thumbs, room codes, the room's leader and its early START, and flood control. |
| 2.13 TCP | The TCP transport: framing, hostile frames, a match over real sockets, a flood of tiny messages held back by TCP, and many connections arriving at once. |
| 2.13.1 Control interface (full tier) | The strict JSON library and the loopback HTTP server with its bearer secret (right and wrong secrets, limits, timeouts, pipelining, many connections) over real sockets. |
| 2.14 NetGame | The game's network layer over real sockets: room, thumbs, start barrier, matches with commands and chat, leaving guests, host migration, refused joins, LAN announcements, bot fill. |
| 2.15 Movement differential | Two independent models of the original's walk and A*, written from the disassembly (section 5.32), compared with the remake on random maps and random walks. |
| 2.16 LAN discovery | Room announcements on the local network: the datagram codec (with fuzzing), and an announcer and browsers over real UDP sockets. |
| 2.17 WebSocket transport | RFC 6455 for the browser door: hash and base64 vectors, frame codec, fragmentation, protocol errors, fuzzing, the upgrade handshake and real-socket clients. |
| 2.18 Map sweep (full tier) | `map_sweep --selftest` on the six shipped maps: they load, run and are deterministic, the report counts what happened, and an injected fault is found. |
| 2.19 Dedicated server (full tier) | The server: map store, rooms (waiting, start, failures, desync, closing), the door, the control calls over real sockets, the control secret and the demo rooms, and the restart records (the format and its reader fuzzed, a match restored bit-identically, every refusal, the disk's limits, a real server stopped with SIGTERM and with SIGKILL while two real clients rejoin). |
| 2.19.1 Way back of a NetGame (full tier) | The game's own network layer in a match that holds seats: real NetGames over loopback sockets against a real room manager come back by themselves after a cut link and after a restart of the server over its records, join again from nothing with their key, start again from nothing when the server lost turns, end every refusal with its own words and its rule for the key, give up in time, show the missing seats and the vote (checked field by field against a scripted server), say in their Hello what a server needs, and leave in every state; the state of every machine and of the referee agrees at the end; the prediction of one's own orders is off while the way back runs; a room on the local network and a room that holds no seats are what they were. |
| 2.19.2 Replay store | `test_replay_store`: the folder of the matches that the server keeps, on a real folder with a clock, a disk, a delete and a rename that the test sets: the file names (what the store could have made and nothing else), the 30 days, the size limit (the oldest go first), the hour's budget of saves, the free-space reserve, other files and folders left alone, a crashed half-written file, a write or a rename that fails, damaged files (kept, listed to the owner only), files of other rules (listed to everybody with their rules number), same-second matches in order, a file read, listed and deleted by name only, 280 files found by name and purged by half, and a refusal that repeats, told once and then counted. The server's own tests (`test_server`, `test_ctl`) add the room's side: a match that ran 600 turns (30 seconds) is kept however it ended (the rules, a Quit, a close, the time limit) and a shorter one is not, restored rooms and `"record": false` rooms keep nothing, demo rooms (also those made through the control interface) only when asked, a person's seat keeps the name that was typed (`replay_person_name`: the words "Player" and "Player 1" to "Player 4" and an empty name leave the seat to its colour, a name with a quote, a backslash or angle brackets keeps both lists valid JSON), a bot's seat keeps its display name, teams and fog are in its head, the control interface's list and file calls, and the public read-only door (which answers on the loopback address only unless it is opened wider).; a file that somebody else puts in the folder is listed at the next look and one that is taken away is forgotten (RS9.1, RS9.2). |
| 2.19.3 Live board | `test_live_board`: the board of the matches that run now (no network, no room: recorders that the test feeds, a clock of its own): the id of a match (the map and the second it began in, `-2`, `-3` ... in one second, never from a name, a seed or a room, unique among the matches that run and the ones that ended lately, none left after 9999), its shape check, the list (30 seconds, no failed recording, the newest start first, at most 50, the players written as the replay list writes them), the snapshot and its cache (the same bytes for as long as the clock is in one second, a clock set back), the memory of the matches that ended (the file they were kept as, 15 minutes, 256 of them, a clock set back). The live door in front of real rooms is in `test_server` (S3.176 - S3.178: a match that runs is listed after 30 seconds, its snapshot grows and plays on the real map, the id answers with the file that the public list has when the match ends, no room code anywhere, a room that is destroyed takes its match off the board). |
| 2.20 Computer players | The bots' read-only view and map analysis (checked against the engine's path finder), the controller's budget and timing, the idle bot and bot seats in rooms, the arena's match runner (determinism, replay without a bot, limits) and the standard bot (power-up guard, strike back, the team-up rule and the opening, fights, the gate, styles, harassment, sabotage and ambush, cost and robustness, the contest play, the island play, and the order in which the tasks take ants: [`BOTS.md`](BOTS.md) "Testing"). |
| 2.21 Bot arena | `bot_arena --selftest`: command line, baseline table, seat arrangements, determinism, replay without a bot, refusals, threads and the report. `--save-replays`: the match is kept as a replay of the server's kind, reads back, plays back to the arena's own hash, and a match under 30 seconds is not kept. |
| 2.22 Worker bot (full tier) | The worker bot's economy on every shipped map, learning from refused orders, the endgame, the command budget and its pinned baselines. |
| 2.23 Ping, delay and waiting | The ping and delay meters, a host and a guest over a simulated link, and what the player sees when the game waits (stalls, bunched turns, hidden windows). |
| 2.24 Jitter buffer | The adaptive jitter buffer: its rule alone and inside the runner (steady and jittery links, stalls, hitches, hidden and frozen windows, catching up, the last turns of a match). |
| 2.25 Engine copy | The deep copy of `SimulationEngine` (value semantics): a copy equals its source in every observable part, ticks identically, shares nothing (the path searches, the pool, the caches, the queues), an assignment replaces the target's whole state; `--bench` prints the costs. |
| 2.26 Engine copy, whole matches (full tier) | `test_engine_copy --whole-matches`: the six shipped maps to the end of the match with a copy taken every 1000 ticks and the state hash compared at every tick. |
| 2.27 Prediction | The client-side prediction of one's own orders on a rig that plays the server and an oracle engine: exactness with the right lead, rebuilds only when a turn disagrees, the predicted engine equal to its derivation under random jitter, stalls, bursts and orders of every kind, the cues that are played once, the budget and its cool-downs, and the lead that learns the lag of the orders. |
| 2.28 Island matches (full tier) | `test_ai_islands`: whole matches of four standard bots on ISLANDS and SMALL at every level (the expedition that flies a crew to the Swimmers, the bridges, the ferry): nobody lost to a bridge or a flight, every team scores, the island of SMALL is taken, the same state every time. |
| 2.29 Replays | `test_replay`: the `.antsrep` file (written and read back exactly, the same bytes every time, in several chunks for a long match; every damaged, cut, forged or oversize file is refused with a reason and never a crash), the recorder (a game on one machine and the lock-step runner's tap write the same file; a turn that went missing spoils the recording), the player (a recorded match, four computer players' included, plays out to the same hashes and the same final state; a changed order, seed, roster or rules number is caught and the first wrong hash named), the list of orders (time, seat, place and the type of every ant that an order names; the words of the table) and the finding of the map by name and hash. RP7.1 - RP7.3: `sim_rules` (the head field, the table of protocol numbers for files made before it, and the guard that fails when the reference match's final hash moves and the number does not). RP8.1 - RP8.3: the snapshot of a match that still runs (the optional `live` chunk of an incomplete file, every refusal of the reader, `Recorder::snapshot`: it plays on the real map up to its turns, ends in the state the match was in, changes nothing in the recording). |
| 2.30 Replay tool | `replay_tool --selftest`: the command line, `info`, `verify` and `orders` with their filters and the table for a spreadsheet, the exit codes, and files that are damaged, cut, made for other rules or not the match that they claim to be. |
| 3 Application integration | Whole-application behaviour through the HUD, renderer and simulation, including the pointer on windows with black bars, the setup screen's hand-overs and the mouse grab. |
| 3.1 - 3.5 Model suites | The render, HUD layout, status message, input and pointer models against the original's draw rules, coordinates, texts, edge scrolling and cursor table (sections 5.14, 5.33, 5.34, 5.41, 5.43 and 5.44). |
| 3.6 Network application (full tier) | The application over the network: command line, window placement, host and guest of a room, matches, chat, teaming, leaving, host migration and `--bot`. |
| 3.7 Options | The options machinery: the slider, latching button and edit field, the settings store, and the options screen end to end. |
| 3.8 Start script (full tier) | `start_game.sh --dry-run`: one window per player with its colour and seat, the grid cells, random names, which options make a single game, and the setup banner; and the build step, run on a copy of the script with a fake cmake and a fake game: it builds on every start, `--dry-run` builds nothing, a failed configure or build stops the launch without starting the old game. |
| 3.9 - 3.9.5 Server end-to-end (full tier) | The real `ants_server` with headless game clients, in six parts (`tests/scripts/test_ants_server.sh --list-parts`): 3.9 options (the command-line options, the pages' texts), 3.9.1 rooms (a room by code, the leader, bot fill, chat, floods), 3.9.2 secret (the control secret), 3.9.3 demo (demo rooms and the stack's defaults) 3.9.4 reconnect (a held seat and the restart records on disk: mode, SIGTERM and SIGKILL in the middle of real games) and 3.9.5 replays (two real games play a match that the owner then closes: it is kept as a file, listed and given out by the control interface and by the public door with the names that the two games typed and with no room code or address, played out to every hash by `replay_tool verify`, still there after a restart, absent from a server started without the door, and deleted). Also checked: automatic start, a match to its end, clean stop, result file and startup refusals. |
| 3.10 View fingerprint | The classic 640 x 480 picture and pointer pinned as 64-bit hashes: draw calls of the HUD and every screen, what the software renderer paints, and what the pointer does at every pixel. |
| 3.11 Start menu model | The desktop start menu's model: keys and mouse, text fields, single-player seats, join and host panels, server and room-code grammar, settings, layout, and the command lines that skip it. |
| 3.12 Start menu application (full tier) | The start menu inside the application against a real room manager over loopback TCP: single player, bots, join, host, every failure, cancel and the way back to the menu. |
| 3.13 Screen layout | The picture's geometry as numbers (`screen_layout.hpp`) and every consumer of it: HUD, scroll, pointer, frame-rate plate, camera and renderer, classic and 16:9. |
| 3.14 Canvas layout | The 16:9 canvas inside a window: scale and bars, the window that opens, `--aspect` and its settings key (four shapes: 4:3, 16:10, 16:9, 21:9), the quick help's columns against the 4:3 page, the pointer over bars, Alt+Enter. |
| 3.14.1 Aspect switch | `Application::set_aspect`, the live change of the picture's shape: every shape to every shape in a match (canvas, layouts, view, picture, a frame), the middle of the view and the zoom kept, the pages (setup, quick help, results) centred in 16:10 and 21:9 and drawn like the 16:9 page pixel for pixel, the pointer kept on the canvas, the state hash after every tick equal to a match that was never switched (also with computer players' orders in it); `test_hud_layout` has the chat log staying on its newest line when the view gets shorter. |
| 3.15 Wide HUD | The 16:9 match screen: the frame's pieces (the `uishell` art of `ants.chd`) anchored and cut at plain lines, checked against the approved mock-up, the HUD at 960 x 540, dialogs and the picture per screen. |
| 3.16 Wide setup | The 16:9 setup screen with its map preview: every rectangle of the three variants against the mock-ups, the art strips against `ants.chd`, presses at every pixel, the picture per screen. |
| 3.16.1 Wide pages | The 16:9 loading screen, quick help, results and start menu: every rectangle against the mock-ups, the results boxes against the art of `ants.chd` (also the same boxes at the 4:3 page's width, pixel for pixel), the loading screen's frame order, the six menu panels pixel for pixel, the numbers' digits, presses at every pixel. |
| 3.17 Map preview | The map preview as the game's own picture of the map: the area filter against an independent oracle, the fit of non-square maps, the render, the cache and the fallback. |
| 3.18 Zoom model | The wheel zoom as numbers, written out independently of the header: levels offered, anchoring, camera, clamps, edge scroll in screen pixels, start view, wheel accumulation, settings key. |
| 3.19 Zoom view | The zoom in the real renderer, HUD and application: the world pass against the direct pass and, at every level of a map, against the picture that the design specifies; the HUD at a zoom, wheel and middle button, the same levels in local and network matches, settings and `--zoom`. |
| 3.20 Zoom fingerprint | What the game draws and what the pointer does at the exact zooms 0.5 and 2, classic and wide, pinned as 64-bit hashes as suite 3.10 pins zoom 1. |
| 3.21 Prestart view | The ants behind the "Get ready to play!" dialog: every team's ants are drawn (local game and guest of a network match, classic and 16:9), the first tick leaves their pixels as they are, and nothing moves before it. |
| 3.22 Prediction in the application | A real application in a real match over loopback: the frame, the click, the HUD's step and the cursor read the predicted engine, the cues are played once, `--prediction` and its settings key, a hidden page, and the corner's felt delay. |
| 3.23 Rejoin store | The keys of the seats that the game keeps (`rejoin.txt`, the browser's local storage over a map): the file's format, broken lines, the three hours, the eight entries, replace, forget by the exact key, mode 0600 under any umask, the atomic write. |
| 3.24 Way back in the application (full tier) | A real application in a room of a real room manager over loopback, with bare second machines: the overlay of the way back, of the missing seats and of the countdown with every text at both picture shapes, the vote (F2, F3, the mouse), the catch-up screen with its percent and what it blocks, the start of a rejoin without the start's dialog, sound and news, the dialog that gives way to a pause, quit and Esc while the match is held, the keys' file and which key a join uses, the start menu's "Rejoin your match", the BadRequest fallback's second begin. |
| 3.25 Touch model | Every rule of the touch model with an injected clock, no SDL: tap, hold, drag, the minimap, the second and the third finger, pan and pinch (judged once per frame), cancels, the clock, the slop's size, a soak of random sessions, and the feedback's geometry (the ring and the pulse). |
| 3.26 Touch in the application | Synthetic finger events through the real event loop of a headless application: a tap, a drag and a hold against the mouse's click, band and right click (point by point), the hold's timing, the pan and the pinch at three zooms, every place where two fingers do nothing, cancels, a press that waited on a dialog that opened, the other screens, SDL's letterbox, an inset picture, the slop's size, and the ring and the pulse in the picture (nothing else of it changes). |
| 3.27 Replays in the application | `test_replay_app`: a real headless application records a game on this computer (the HUD's order, the computer players' orders and the quit; a clock that runs out; teams made at the start; the direct start) and a match of the network (both machines' orders, the names and seats of the Start), and each file plays out to the state in which the match ended; the names are the ones that were typed; the desktop game keeps the file in the folder `replays` beside its settings and never replaces one; a match that is left before its end keeps its file when an order was given or a minute went by, and leaves nothing otherwise. The replay viewer's controls (RA7.1 - RA7.6): state, jump, speed, restart, the end, a cut-short file and the refusals (older, newer, no map, diverged) that the page shows as cards. |
| 4 E2E (full tier) | Opaque-box scenarios in four tiers, run against the suite's own model of the rules (`tests/e2e/e2e_model.hpp`; it links no engine code and still has the early combat rules, see `tests/TEST_INFRA.md`). |
| 5.1 Version consistency | `tools/check_version_consistency.py`: the file `VERSION`, the top release heading of `CHANGELOG.md`, and the version line of `README.md` name the same release. |
| 5.2 Tool and script tests | The python tests of `tests/scripts` (every `test_*.py`): the pages of the site, the version, release, mutation and deploy tools, the web build without Docker, `run_tests.sh` itself and the CI workflow; the list is below the table. |

`./run_tests.sh` prints every suite's result and its time, and the totals. The CI (see [Continuous Integration](#continuous-integration)) shows pass / fail for every pull request. No per-suite numbers are kept on this page.

### The python tests (suite 5.2)

Suite 5.2 runs every `tests/scripts/test_*.py`:

- **Pages of the site:** the two changelog pages (their structure, in the Classic look), Sprites and sounds (`test_web_catalog.py`), the Classic look of every page of the site (`test_web_pages_classic.py`) and the 16:9 default of the pages (`test_web_aspect_default.py`).
- **The front page:** its art and font (`test_web_front.py`), its rules and words (`test_web_lobby.py`, `test_web_lobby_rules.py`, `test_web_lobby_net.py`), the line of numbers in its footer (`test_web_stats.py`) and the count of single-player games (`test_web_report.py`).
- **The game page:** its look (`test_web_game.py`), the pointer at its edge (`test_web_edge.py`) and touch screens (`test_web_touch.py`).
- **Watching replays on the web:** the shared code of the list page and the player (the address, the day and time, the length, the maps, the players, the chips) run with node (`tests/scripts/web_replay_check.js`), and `test_web_replay.py` reads the markup (the one place of the footer link, the pieces the glue finds by id, text never becomes markup, the files in the image, the CI and the local web build). The list and the player were also checked by eye in a real browser against the owner's pictures (wide, side and phone, fullscreen, every card). Watching a match that is being played has the same two layers (the live words and helpers in `web_replay_check.js`, the live glue in `test_web_replay.py`) and an opt-in check in a real browser that needs no image and no server, `tests/scripts/test_web_live.sh` (the pages against a fake door and a stand-in for the game).
- **The player's name on the web pages:** the rules of a name, the name step of a shared link and the whole front page (the real lobby with its two front scripts and a scripted game server), run with node and a small fake of the browser's DOM (`tests/scripts/web_name_check.js`).
- **The front page's Rejoin button:** its block run by node (`tests/scripts/web_rejoin_block_check.js`) and the pins of `test_web_rejoin.py`. The check in real browsers, `web_rejoin_check.py`, is opt-in.
- **nginx:** the site's block for `/busy` (`test_nginx_conf.py`), and the routes and the `/stats` routing run against a real nginx where docker works (`test_nginx_routes.py`, `test_nginx_stats.py`). The two blocks of the recorded matches (`/replays`, `/replays/<file>`) are pinned in `test_nginx_replays.py` (the names that pass, the methods and queries that never reach the server, what is passed on, the two allowances and the five-second cache of the list, the port the stack and the image agree on and that the stack does not publish) and, where docker works, run against a real nginx with a stand-in for the server. The two blocks of the matches that run now (`/live`, `/live/<id>`) are pinned the same way in `test_nginx_live.py` (the ids that pass, the methods and queries that never reach the server, what is passed on, their own two allowances, the two-second cache with a key for each id, and that an old answer is served only while a new one is fetched) and run against a real nginx where docker works.
- **Version and build:** the version tools (build id, stamp, consistency check), the generated version header on the include path of every target in every configuration, and the compile commands (no folder of the checkout in them: ccache).
- **The web build without Docker:** `tools/web_without_docker.py` (`test_web_without_docker.py`): the replay of the Dockerfile and of `.dockerignore`, the ports and nginx's file. The tool itself is described in [`WORKFLOW.md`](WORKFLOW.md) ("Browser checks without Docker").
- **The runners:** `run_tests.sh` (its tiers, the parallel run, the table's resources) and the python runner itself.
- **Defaults:** the default map (the stack file, the front page and the program name the same one, Treasure).
- **Release, deploy and CI:** the release, mutation and deploy tools (`tools/release.py`, `tools/mutate.py`, `tools/deploy_filter.py`, `tools/deploy_wait.py`, `tools/deploy_webhook.sh`), the staging stack and its site label, and the CI workflow (the required job names, the deploy job's secrets and its wait).

## Standalone E2E Test Runner

The E2E suite exercises the game's features in four tiers. It is a CMake project of its own and runs against its own model of the rules: it links none of the game's code, and its model still has the early combat rules. A pass says that the model agrees with the documents, not that the engine does; the engine's rules are checked by the golden, integration and differential suites above. The status note and the feature list are in [`tests/TEST_INFRA.md`](../tests/TEST_INFRA.md).

```bash
# Build standalone E2E runner
cmake -S tests/e2e -B build_e2e
cmake --build build_e2e

# Run all tests
./build_e2e/e2e_runner --all

# Run specific tiers
./build_e2e/e2e_runner --tier 1   # Tier 1: Feature Coverage
./build_e2e/e2e_runner --tier 2   # Tier 2: Boundary & Corner Cases
./build_e2e/e2e_runner --tier 3   # Tier 3: Cross-Feature Pairwise
./build_e2e/e2e_runner --tier 4   # Tier 4: Real-World Workloads (full matches)
```

`--list` names the registered tests, and `-v` also prints a `[PASS]` line with its time for every test that passes (a test that fails is always printed).

## Continuous Integration

Every pull request, every push to `main` and to `staging`, and every manual run is built and tested by GitHub Actions (`.github/workflows/ci.yml`). A newer push to a branch or pull request cancels the run of the older one.

`main` is protected: it takes a change only through a pull request whose five checks (the five jobs of the table below) pass on a branch that is up to date with `main`, merged with a merge commit ([`WORKFLOW.md`](WORKFLOW.md), "Branches, pull requests and releases"). The merge deploys the beta site through the sixth job once the deploy secret is set ([the deploy job](#the-deploy-job), below).

The five jobs run side by side. A run takes about 10 to 15 minutes: the web job about 4 minutes, the others 8 to 13.

| Job | Runner and compiler | SDL2 | What runs |
|---|---|---|---|
| Linux (GCC) | `ubuntu-latest`, GCC 13, Ninja, Release, `-DANTS_WERROR=ON` | apt: `libsdl2-dev`, `libsdl2-ttf-dev` | build everything; `ctest` (every registered test program: the same ones as `./run_tests.sh`); `map_sweep`, `bot_arena` and `replay_tool` self-tests; the E2E tests; the script suites (`start_game.sh --dry-run`, the six parts of the `ants_server` end-to-end script, five at a time); the repository checks (version / changelog consistency, the python tests of `tests/scripts`, side by side) |
| macOS (Apple clang) | `macos-latest` (Apple silicon), Apple clang, Ninja, Release, `-DANTS_WERROR=ON` | SDL2 2.32.10 and SDL2_ttf 2.24.0 built from the release sources (cached) | the same as Linux |
| Windows (MSVC 2022) | `windows-2022`, MSVC 19.44, Ninja under the Visual C++ environment, Release, `/W4 /WX` (`-DANTS_WERROR=ON`), sccache (its folder cached) | the prebuilt Visual C++ packages that `CMakeLists.txt` downloads | build everything; `ctest`; the `map_sweep`, `bot_arena` and `replay_tool` self-tests; the E2E tests. Runs for pull requests, pushes to `main` and manual runs |
| Windows (MSVC 2026) | `windows-latest`, MSVC 19.5x, the same flags and cache | the same | the same, for every run (also for a push to `staging`) |
| Web (Emscripten, Docker image) | `ubuntu-latest`, `docker build -t ants-beta .` (emsdk 3.1.58) | the Emscripten port | the beta site's image builds; `nginx -t` accepts its configuration; the game's files are in it and the page names the version and the build; the site's routes are right (the front page at `/`, the game page's addresses, the redirect of the old Play online address); the game server's image builds and prints its `--version`; the bot arena's image (stage `arena`) builds and plays one match of a fixed seed into a folder; both stack files render (`docker compose config`); a build with `ANTS_SITE_LABEL=staging` puts "staging" in the title and the footer of both pages and the production pages do not have the word |

The compiler versions come from GitHub's runner images and move with them; the repository does not pin them. The macOS job builds SDL2 itself because Homebrew's `sdl2` is now sdl2-compat (the SDL2 interface on top of SDL3), under which eight test programs failed in the job's first run (pointer, window and canvas checks, pixel fingerprints, one crash).

### The zero-warning standard

The project builds without warnings under `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor`. `cmake -B build -DANTS_WERROR=ON` turns them into errors, and `./run_tests.sh` configures its build folder that way. MSVC uses `/W4 /permissive-` and, with `ANTS_WERROR`, `/WX` (the warnings C4127, C4244, C4100 and C4189 are off). CI builds every pull request that way with GCC 13, Apple clang and MSVC 2022 and 2026.

The rule covers the game, the server and every test program, with three exceptions. The E2E runner, a CMake project of its own, builds with `-Wall -Wextra -Wpedantic`. One test file (`tests/test_ai/test_ai_worker.cpp`) turns off GCC 13's `-Wdangling-reference`, which has a known false positive there. GCC's sanitizer build (`./run_tests.sh --asan`) leaves `-Wsign-conversion` off, because GCC 13 reports `(mask >> n) & 1u` under `-fsanitize=shift` as a sign change when there is none (`CMakeLists.txt`, under `ENABLE_ASAN`); every other build, CI's too, has it on.

### The deploy job

The sixth job, **Deploy (Portainer webhook)**, is not a required check. It runs only for a push to `main` (a merged pull request) or to `staging`, after the build and test jobs of that run passed (five on `main`, four on `staging`, which has no MSVC 2022 build). It deploys only when the push changed something that an image contains, or the stack file (`tools/deploy_filter.py`). `CHANGELOG.md`, `VERSION` and the changelog archive count, because an image copies them. The other documents, `.github/`, `tests/` and the tools that no image runs do not.

A deploy restarts the game server and ends the matches that run, so the job first waits for an idle server. It polls the site's public `/busy` (the repository variable `DEPLOY_BUSY_URL`; the beta site's `/busy` when it is not set) every minute and goes on when no match runs, after `DEPLOY_MAX_WAIT_MINUTES` (a repository variable, 180 by default, 300 at the most) whatever runs, or when the address has not answered for five minutes (`tools/deploy_wait.py`). On `staging` it waits only when the variable `STAGING_BUSY_URL` is set. A newer push cancels a job that waits, and a push that is no longer the tip of its branch after the wait is not deployed.

The job then calls the Portainer webhook that the repository secret `PORTAINER_WEBHOOK_URL` (staging: `PORTAINER_STAGING_WEBHOOK_URL`) holds, never printing it. Without the secret it says "deploy secret not set: skipped" and does nothing, so a merge deploys only once the secret is set. [`WORKFLOW.md`](WORKFLOW.md) ("Deploy from CI and the staging site") has how to switch it on, the wait and the staging stack.

### What CI does not cover

All tests run headless (SDL's dummy video and audio drivers). CI does not run:

- the opt-in browser checks (`tests/scripts/test_web_*.sh`: a real browser against a running page);
- the script suites on Windows (they are bash and python);
- a sanitizer job (AddressSanitizer and UBSan: `./run_tests.sh --asan`, locally);
- the 32-bit and ARM Windows builds.

`ctest -N` in a build folder lists the test programs; `ctest -C Release --output-on-failure` runs them on Windows. A failed run keeps `Testing/Temporary` as an artifact.

Two rules keep a CI run short (its test step takes about as long as all the test programs together, divided by the three or four cores of a runner). `ctest --parallel 4` starts the programs with the highest `COST` first (a number of seconds of a CI run, set with `set_tests_properties` beside the `add_test` of the program; in a fresh build folder one without a `COST` counts as 0 and starts after them, in the order of its number, and a folder that has run the tests before gives it the average time that it took), so a new test program that takes a minute or more gets one. And the pump loops of the network tests (a step of game time, then a moment for the kernel to deliver the loopback bytes) pause with `ants_test::short_pause()` (tests/common/ants_test_pause.hpp), not with `sleep_for(300 us)`: on Windows that lasts a whole timer tick, and one test program then took five minutes instead of fifteen seconds. A step is short in real time, so a wait that the sockets decide ends with `ants_test::real_time_tail` (the same header): a late kernel gets seconds of real time with the game clock standing still.

One rule follows from the machines that CI uses: a pinned pixel fingerprint must mask every TrueType text, because SDL's alpha blit rounds differently on x86-64 and on ARM.
