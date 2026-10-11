# The original program and reverse engineering

How this remake is tied to the 1998 game *Ants*: how its rules are found in the original program, what the remake adds that the original does not have, and where your own copy of the original program goes. The original program and its decompilation are not in the repository.

## What this project is

This project is an educational remake and historical preservation effort.

Its method is reverse engineering of the original 1998 program (`Ants.exe`): Capstone disassembly for the exact instructions, and a Ghidra C decompilation for readable logic and constants, always cross-referenced with each other. [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md#20-dual-primary-reference-methodology) section 2.0 ("Dual Primary Reference Methodology") describes it. Rule 4 of [`AGENTS.md`](../AGENTS.md) requires that the game's logic, timings and behaviour are checked against the original this way.

## What comes from the original and what does not

- **Read from the original's data files:** the artwork, animations and sounds of `Original-Ants/ants.chd` and the maps `Original-Ants/Maps/*.LVL` ([the asset catalog](PLAY_IN_BROWSER.md#interactive-asset-catalog) shows what is inside the archive).
- **Re-derived from the original:** the simulation's mechanics, timings and constants, to match the original exactly where that can be checked. This was done one system at a time over many releases, not in one step; [`../CHANGELOG.md`](../CHANGELOG.md) says which release did which (from v0.0.90; the earlier ones are only in git history).
- **This project's own additions:** network play (lock-step turns, state hashes, the dedicated server), computer players, widescreen, zoom and touch control.
- **Network play:** the original's game is not lock-step. The remake keeps its lobby flow, texts and rules, but not its transport, trust model or sync model ([`NETWORK_PORT.md`](NETWORK_PORT.md#what-the-original-does); the evidence is section 5.46 of [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md)).
- **Music:** how the original plays its music, and why the remake plays MP3 renders of it, is described in [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md#524-authentic-gamesound-dispatch-table-0x1002c28) section 5.24e and in [View and HUD](VIEW_AND_HUD.md#sound-and-music).
- **Other differences made on purpose** are listed under [Differences made on purpose](#differences-made-on-purpose) below, so that they are not mistaken for deviations.

## Differences made on purpose

The goal is a one-to-one copy; tweaks on top of it are each recorded here and in the CHANGELOG so that they are not mistaken for deviations:
* v0.0.51: the hit-point numbers above the ants (`Ctrl + L`) are **on by default** (the original starts with them off).
* Treasure is the default map (decided in October 2026: it is the map that is played most, and it is to be the default of everything). The one deviation from an original screen: the **setup screen highlights `TREASURE.LVL` when it starts** (when the Maps folder holds it; else the first entry, as the original does). The original highlights the first entry of its list (`GAUNTLET.LVL` for the six maps): `FUN_01013de7` ends with `FUN_010298fc(list, 0)`, which puts the list's cursor on its head, and the host screen's constructor `FUN_01013b36` ends with `FUN_01013fc9(this, 0)`, which announces the entry at the cursor. Only the highlight differs (`MapSelectScreen::init`, with the comment at the line): the list, its order (`strcmp`), Up / Down, START, the keys and the labels are the original's, and a guest and the leader of a server's room show the room's map. The same decision makes Treasure the default of the remake's own choices of a map, none of them a screen of the original: the start menu's Host panel until a `host_map` is stored, the form of `web/lobby.html` until a choice is remembered, the map of the stack's server for a demo room whose create block names none (`docker-compose.stack.yml`: `ANTS_DEMO_MAP`, default `TREASURE.LVL`), and the map of a run that skips the setup screen without `--map`.
* v0.2.0: **the match clock waits for the "Get ready to play!" dialog** (reported after play: the wait wastes about five or six seconds). The original runs the clock and the ants behind the dialog (`GAME_REVERSE_ENGINEERING.md` section 6.2, item 21: the GO handler `0x1022432` that releases the dialog also starts the clock), so a person loses the 5 s of its task KWFO; the remake keeps the dialog (picture, texts, 5 s, input swallowing) but its simulation does not run while it is up, so the match's whole time is playable. Local games count the dialog in real time; in a match of the network the host seals the first turn 5 s after the match began and a machine closes its dialog when its first turn executes (network protocol 12). A game that starts straight into a match (`--map`) has no dialog. The bots wait for nothing (no tick, no look) and open with one token. The ants are drawn behind the dialog, standing and locked (requested: the ants are shown but cannot move until the game actually starts): the original has none before GO (CreateAnt `0x100ef18`, from the GO handler), the remake's snapshot shows the first frame of the idle clip that its first tick starts (`GAME_REVERSE_ENGINEERING.md` section 21).
* v0.0.99: **the 16:9 picture is the default** (the widescreen work; the desktop game and the web page): a fixed canvas of 960 x 540 with a 762 x 500 map view instead of 442 x 440, so a player sees about twice the area at once. The original's own 640 x 480 picture is unchanged and one option away (`--aspect 4:3`, the settings key `aspect`, the page's selector), and every screen outside a match is composed for the wide canvas from the original's own art. Nothing is drawn that the original does not have (`VIEW_AND_HUD.md`, "16:9 by default").
* v0.1.0, many levels since v0.4.0: **the mouse-wheel zoom, a remake addition** (the original has none): the wheel over the map view steps through the levels 2, 1.68, 1.41, 1.19, 1, 0.84 ... down to the map's limit, four to a doubling, towards the pointer, and the middle button goes back to 1, the original's picture, which is drawn exactly as before. The zoom is the player's own view: it never reaches the simulation, the network, the bots or a state hash, and every kind of match has the same levels (`VIEW_AND_HUD.md`, "Mouse-wheel zoom"; `docs/NETWORK_PORT.md`, "The view's zoom and the network").
* **A declined team-up says why** (reported after play: when a player tries to team up with a bot, the bot just declines): when a computer player rejects the local player's invitation the original's text, "%s rejected teaming up" (string 80, the status line), is unchanged and the game adds ONE line of the chat log after it: "Bots team up only while three or more teams play.", "You already have a teammate.", "<name> already has a teammate." or "This bot never teams up." (the worker bot). The reason is the bot's own accept rule asked again (`ai::team_up_answer`), never a second rule; nothing is added for a person's answer. The engine's news carries the answering seat (`NewsEvent::subject`), which no rule or hash reads (`docs/BOTS.md`, "Alliances").
* **Teams chosen before a local game** (requested: the teams can be pre-selected): `--teams ffa|A+B`, the start menu's Teams row and the web page's Team 1 and Team 2 switches. The game makes the teams at the start of the match with the original's own commands (an invitation, its acceptance), so the original's texts and sounds are the ones of a team made in play; nothing in the match's rules, in the network protocol (12) or in a state hash changes. A team that would be the whole match is refused with the reason: the original ends such a match at once (`checkgo_ends_for`).
* **One level for each bot in single player** (requested: the difficulty can be set for each bot individually): the start menu's Single player panel already had a row for each seat; the web page's Opponents select became one select for each of Red, Blue and Black, and later a group of four level buttons (None, Easy, Medium, Hard) for each. The panel also has a **Your name** field now (the one remembered name of Join and Host; the game is played under it, not under the computer's user name), so the Single player panel is no longer the mock-up's of the 16:9 pages (its numbers in `tests/test_app/test_wide_pages.cpp` are the panel as it is drawn).

### What only the real game can settle

The audit of the remake against the original was made from static evidence (it did not run the original). Screenshots of the original (cnc-ddraw in a local copy of the original game's folder, `Original-Ants/`, saves the game's own 640 x 480 picture with the Print Screen key) would settle: the minimap hit flash, the start view, the setup and results layouts and the start-up timeline, the splash jingle, the score-box edge and minimap frame, the real refresh cadence (hence bubble and task periods), the 180 s life of fire walls and bridges (0.3-2.2 s spread), contact latency of fights, and whether pressed-button clicks play sounds at the press. A stopwatch recording of a fire wall's life would settle the timers.

## The original program is a local reference, not part of the repository

Neither `Ants.exe` nor its Ghidra decompilation (`Ants.exe.c`) is in the repository. Comments and documents cite the decompilation as "Ants.exe.c line NNNN" and the program by address (virtual addresses, image base `0x01000000`).

If you own the original game, put your own copy of `Ants.exe` in `Original-Ants/` (and a decompilation, if you have one, at `docs/legacy/Ants.exe.c`). `.gitignore` keeps both out of every commit, together with the other local files of an installation of the original (cnc-ddraw, its shaders, `chat.txt`). Rule 4 of [`AGENTS.md`](../AGENTS.md) covers these local copies for contributors.

A WebAssembly build made outside Docker (`build_web.sh`) packs the whole folder `Original-Ants/` into its data bundle, so it would carry your copy of `Ants.exe`. The Docker image build leaves the program out (`.dockerignore`).

The data files of the original game that the repository does hold, and the terms that apply to them, are described under [License](../README.md#license) and in [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).

## The tools

The reverse-engineering tools read the program. When it is missing they stop with "put your own copy of the original Ants.exe in Original-Ants/" (exit status 1; `analyze_binary.py` adds "and run this script from the repository root"):

| Tool | Needs | Does |
|---|---|---|
| `tools/analyze_binary.py` | `Original-Ants/Ants.exe` (pefile, capstone) | writes a function map to `docs/ORIGINAL_BINARY_MAP.md` and `docs/binary_analysis.json` for your own use (git ignores both; never commit them; the classification column of the map is a keyword guess) |
| `tools/extract_movement_tables.py` | `Original-Ants/Ants.exe`, `Original-Ants/ants.chd` (pefile) | writes `src/ants_sim/movement_tables_data.inc`, or checks it (`--check`; `--stdout` prints it instead) |
| `tools/movement_reference_model.py` | the same two files (pefile) | prints the golden timings of `tests/test_sim/test_movement_golden.cpp` (`--trace` also prints every pixel move) |

`Original-Ants/ants.chd` is in the repository, so only `Ants.exe` has to come from you; no tool or test reads the decompilation, it is for following the citations. If the Python module `pefile` (all three tools) or `capstone` (`analyze_binary.py`) is not installed, a tool stops earlier, with an error about that module (`pip install pefile capstone`).

`extract_movement_tables.py` checks the machine code that uses each table before it extracts anything, so a copy that is not the supported build of the program stops it instead of giving wrong data. The header of `src/ants_sim/movement_tables_data.inc` records the size and SHA-256 of the two original files that the tables came from.

## Tests and the original program's bytes

The test suites do not need the program. The two that compare the remake with static tables inside it, 1.1 `test_movement_tables` and 2.15 `test_movement_differential` (the suite ids of `./run_tests.sh`, see [`TESTING.md`](TESTING.md)), pin every byte of `Ants.exe` that they read: the address, size and SHA-256 digest of each region are fixed in `tests/common/original_program_bytes.hpp`.

- **With your copy** in `Original-Ants/`, they check its layout and every region against the digests and read the bytes from it. A copy of a different build fails that check (test case 5.0 of `test_movement_tables`, 0.1 of `test_movement_differential`).
- **Without it**, they lay the remake's own generated tables out like the program's, check that those bytes hash to the same digests (computed from the original program, so a match means the bytes are the original's, byte for byte) and read them instead. Each test prints which source it used ("Bytes of the original program: ...").
- **What is skipped without the program** is only what needs the program itself: test case 5.1 of `test_movement_tables` (its PE header) and, in its test case 5.3, the reading of the original's terrain-pair and tile-flag lists (`FUN_0100724c`). The remake keeps the per-tile result of those lists, not the lists, so 5.3 checks that result against pinned digests instead.
- A pinned digest is changed only with the program present: the run with the program is what verifies it.

## Where the findings are

- [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md): the specification, with the disassembly addresses, opcode traces and formulas, system by system. Older text there counts as unverified until it is checked against the program again; a section whose heading says "Capstone-Verified" records such a check.
- [`reverse_engineering/movement/README.md`](reverse_engineering/movement/README.md): the reports behind the movement ground truth (section 5.32 of the specification).
- [`chd_table4_animations.json`](chd_table4_animations.json): the 1,344 animation sequences of `Original-Ants/ants.chd` (Table 4), with timings, sounds and motion. `tools/dump_table4.py` writes it from the archive, so it needs no copy of the program.
