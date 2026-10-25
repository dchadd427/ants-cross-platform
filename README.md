# Ants (1998) — Modern Cross-Platform Engine Remake

[![CI](https://github.com/dchadd427/ants-cross-platform/actions/workflows/ci.yml/badge.svg)](https://github.com/dchadd427/ants-cross-platform/actions/workflows/ci.yml)

A faithful, deterministic C++17 engine remake and port of the 1998 real-time strategy game **Ants**, for the desktop (Windows, macOS, Linux) and the browser.

The engine loads the original game's data files (`ants.chd` and `Maps/*.LVL`) directly, without pre-conversion. The game's mechanics are re-derived from the disassembly of the original program, system by system ([the original program](docs/ORIGINAL_PROGRAM.md)); the network play, the computer players, the 16:9 picture, the zoom, the touch controls and the browser build are this project's own. It runs a deterministic 20 Hz simulation, with 32-channel spatial audio, MP3 music (the original's four pieces), TrueType text and an SDL2 2D renderer (hardware-accelerated, with a software fallback).

**Current version: v0.12.134** (shown on screen next to the FPS meter; in the room and the match of a network game the same corner also shows **ping** and **delay**, see [Network play](docs/MULTIPLAYER.md)). The releases since v0.12.83 are in the short **[changelog](CHANGELOG.md)** (also published at **[beta.playants.org/changelog.html](https://beta.playants.org/changelog.html)**); the releases before v0.12.83 are only in git history. How work flows from a commit to a release: [`docs/WORKFLOW.md`](docs/WORKFLOW.md).

## Play

**In a browser, with nothing to install:** **[beta.playants.org](https://beta.playants.org)**; what the site does and the browsers it was tested in are described in [Play in the browser](docs/PLAY_IN_BROWSER.md), and playing on a phone or a tablet in [Touch](docs/TOUCH.md).

**On your computer:** the game needs CMake 3.16 or newer, a C++17 compiler, and SDL2 with SDL2_ttf. From the repository folder, one command builds the game and starts it:

| System | Install | Start |
|---|---|---|
| macOS | `brew install cmake sdl2 sdl2_ttf` (enough for a quick try; for the real SDL2 that the tests want, see [Build and run](docs/BUILD_AND_RUN.md#macos)) | `./start_game.sh` |
| Linux (Debian, Ubuntu) | `sudo apt-get update && sudo apt-get install -y cmake g++ libsdl2-dev libsdl2-ttf-dev` | `./start_game.sh` |
| Windows | Visual Studio 2022 Build Tools (the "Desktop development with C++" workload) and CMake 3.21 or newer (`start_game.bat` also finds the CMake that comes with Visual Studio); CMake downloads SDL2 itself | `start_game.bat` |

The script builds the game each time it starts (quick when nothing changed) and opens one game with the start menu. Its options, the other ways to build, and the web build are in [Build and run](docs/BUILD_AND_RUN.md#quick-launch-desktop).

**Support the project:** if you enjoy the game, you can [buy me a coffee](https://buymeacoffee.com/dchadd).

## Documentation

**Playing**

- [Play in the browser](docs/PLAY_IN_BROWSER.md): the front page, rooms and rejoining, the 16:9 picture and mouse lock in the browser, hidden tabs, sound, the asset catalog (a page of every sprite, sound and animation of `ants.chd`)
- [How the game plays](docs/GAMEPLAY.md): the data files, the 20 Hz simulation, the six kinds of ant and their abilities, movement, food, power-ups, combat, the anthill, the start and end of a match, fog of war
- [View and HUD](docs/VIEW_AND_HUD.md): the 16:9 view, mouse-wheel zoom, edge scrolling and the minimap, the HUD, messages and the chat log, the results screen, sound and music
- [Controls](docs/CONTROLS.md): the start menu, the setup screen, options and settings, the mouse controls, the keyboard
- [The settings menu](docs/GAME_MENU.md): what Esc opens in a match (sound, video, controls, Leave game), the one table of rows, and the browser's panel
- [Touch](docs/TOUCH.md): playing with a finger on a phone or a tablet
- [Network play and bots](docs/MULTIPLAYER.md): LAN and server games, lag and drop-outs, computer players
- [Replays](docs/REPLAYS.md): recording a match in the desktop game, the file, and the tool that lists every order of a recorded match
- [Command-line options](docs/COMMAND_LINE.md): every option of the game and its environment variables

**Building, running and testing**

- [Build and run](docs/BUILD_AND_RUN.md): prerequisites, building, the launch scripts, sanitizers, Docker
- [The dedicated server](docs/SERVER.md): `ants_server`, its options, the control interface, reconnecting, the Docker stack
- [Testing and CI](docs/TESTING.md): the test suites and tiers, the browser checks, continuous integration
- [Workflow](docs/WORKFLOW.md): branches, pull requests, releases and the deploy; the rules for contributors are in [`AGENTS.md`](AGENTS.md)

**How it works**

- [Architecture](docs/ARCHITECTURE.md): the source tree and the libraries
- [The original program](docs/ORIGINAL_PROGRAM.md): reverse engineering, what the remake takes from the original and what it adds, and the differences made on purpose
- [`GAME_REVERSE_ENGINEERING.md`](docs/GAME_REVERSE_ENGINEERING.md): the specification of the original game, system by system
- [`MAP_FORMAT.md`](docs/MAP_FORMAT.md): the map file (`.lvl`): its layout as the remake reads and writes it, the limits, the rules of a map that loads in the original, the round-trip law of the writer, the dictionary and the blank map of a new map, `lvl_tool`, the core in the browser (`lvl_core.wasm`)
- [`MAP_EDITOR.md`](docs/MAP_EDITOR.md): the map maker's page: the two kinds of map (including per-spawn starting ant types on Extended maps and playing Extended maps up to 512 x 512, with limits on long orders), loading any current game map as a starting point, the rules that keep a map from ever being written over, the pull requests of the editor and which are done, how the page is made
- [`MAP_SUBMISSIONS.md`](docs/MAP_SUBMISSIONS.md): a player sends a map for approval and the owner decides: the rules, the API that the editor's Submit button uses (limits, states, error codes), the owner's page at `/admin` and its accounts (a username and a password, Admin and Moderator), the lists the lobby reads (`/maps`, `/maps/all`, including approved Extended maps with an `Extended` badge in the lobby and map maker), how to deploy it and what is off until it is configured
- [`NETWORK_PORT.md`](docs/NETWORK_PORT.md): the network design and the protocol by version
- [`BOTS.md`](docs/BOTS.md): the computer players: the rule, the architecture, the levels and tactics, the known limits (and [`docs/reverse_engineering/bomb_gates.md`](docs/reverse_engineering/bomb_gates.md) for the bomb-gate analysis and universal gate-goal planner)
- [`BOT_GATES.md`](docs/BOT_GATES.md): walled maps: the map's own bombs left to the gate goals, the field breach (food beyond a field of bombs, a fire wall and water: POPcOrN and its family), the measurements, and POPcOrN in a match
- [`BOT_FIGHT.md`](docs/BOT_FIGHT.md): the Hard computer player's quick kill (an enemy beside water, a bomb or a fire wall, struck from the tile that throws it in), fights that a walk reaches, hunting when there is nothing to bank, and the measuring tool `bot_watch`
- [`BOT_187.md`](docs/BOT_187.md): the computer players in the game type 187: the brawl (packs on the enemy ants, the hurt sent home, what the engine allows), the measurements, what was tried and left out
- [`MATCH_COUNTDOWN.md`](docs/MATCH_COUNTDOWN.md): the 30 s countdown with the chat open before an online match: the Begin that tells it, the card, the Fullscreen button
- [`BOT_NAMES.md`](docs/BOT_NAMES.md): the names of the computer players ("Pip (bot, Hard)"): the list, the picker, the label, the names a person may not take, network protocol 20

## Reverse Engineering & Historical Preservation

The method, the tools and the use of your own copy of the original program are described in [The original program and reverse engineering](docs/ORIGINAL_PROGRAM.md), and what the project knows about the original is written down in [`docs/GAME_REVERSE_ENGINEERING.md`](docs/GAME_REVERSE_ENGINEERING.md).

## License

This game is free to play and free to use. It is provided "as is", with no warranty of any kind, express or implied. Its authors are not liable for any damage, data loss or other problem from using it, including online play, the bots, the website and the servers. There is no promise that the online servers will be up: they may be slow, down, restarted or shut down at any time, with no notice. It is an unofficial fan remake of the 1998 game Ants, not affiliated with or endorsed by the authors or owners of the original game. The same notice, in plain words, is the site's **[Legal page](https://beta.playants.org/legal.html)**; it is not legal advice.

The source code and the documents written for this project are released under the **MIT License** ([`LICENSE`](LICENSE)): anybody may use, copy, change and distribute them, including in other projects and commercial ones, as long as the licence text stays with them.

**The original game is not part of that licence.** From the original 1998 game the repository holds its data and pictures made from it: the archive `Original-Ants/ants.chd` (artwork, animations, sounds), the maps `Original-Ants/Maps/*.LVL`, the music (`Original-Ants/*.MID` and the `*.mp3` renders of those pieces that the game plays), the sprites and sounds decoded from the archive (`asset_catalog/`, and the animation table `docs/chd_table4_animations.json`) and the front-page pictures cut out of the artwork (`web/front/`). The web build also packs the folder `Original-Ants/` into its data bundle. They belong to their copyright holders; this project does not own them and cannot license them. The original game's program is not in the repository (see [The original program and reverse engineering](docs/ORIGINAL_PROGRAM.md#the-original-program-is-a-local-reference-not-part-of-the-repository)). The data files are in the repository for now so that the remake can run and so that its tests can compare it with the original; replacing the original artwork with open material is planned as a separate, later project, until the remake needs nothing of the original but a copy that the player already has. If you hold rights to something in this repository and want it removed, open an issue and it will be taken out. Third-party components (SDL2, dr_mp3, Libre Franklin, ...) keep their own licences: see [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

**The players' maps are not part of that licence either.** `Community-Maps/` holds maps that players of the original game made and shared; each belongs to its maker (the makers were not recorded). [`Community-Maps/README.md`](Community-Maps/README.md) says where they come from, which were repaired and which were left out. If you made one and want it removed, open an issue and it will be taken out.
