# Ants (1998) — Modern Cross-Platform Engine Remake

[![CI](https://github.com/dchadd427/ants-cross-platform/actions/workflows/ci.yml/badge.svg)](https://github.com/dchadd427/ants-cross-platform/actions/workflows/ci.yml)

A faithful, deterministic C++17 engine remake and port of the 1998 real-time strategy game **Ants**, for the desktop (Windows, macOS, Linux) and the browser.

The engine loads the original game's data files (`ants.chd` and `Maps/*.LVL`) directly, without pre-conversion. The game's mechanics are re-derived from the disassembly of the original program, system by system ([the original program](docs/ORIGINAL_PROGRAM.md)); the network play, the computer players, the 16:9 picture, the zoom, the touch controls and the browser build are this project's own. It runs a deterministic 20 Hz simulation, with 32-channel spatial audio, MP3 music (the original's four pieces), TrueType text and an SDL2 2D renderer (hardware-accelerated, with a software fallback).

**Current version: v0.21.2** (shown on screen next to the FPS meter; in the room and the match of a network game the same corner also shows **ping** and **delay**, see [Network play](docs/MULTIPLAYER.md)). The releases since v0.15.1 are in the short **[What’s new](CHANGELOG.md)** (also published at **[beta.playants.org/changelog](https://beta.playants.org/changelog)**); the releases before v0.15.1 are only in git history. How work flows from a commit to a release: [`docs/WORKFLOW.md`](docs/WORKFLOW.md).

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

- [Play in the browser](docs/PLAY_IN_BROWSER.md): the front page, rooms and rejoining, the game page and its addresses, the Legal page, hidden tabs and the screen lock
- [Setting up a match in the browser](docs/PLAY_MATCH_SETUP.md): players' maps, choosing a map, Game type and Fog of War in a room
- [The browser's picture and sound](docs/PLAY_PICTURE.md): the data package, the 16:9 picture and mouse lock, sound, the asset catalog (a page of every sprite, sound and animation of `ants.chd`)
- [Home and the Room](docs/HOME_SCREEN.md): the front page's two screens, the top bar, My room, the Menu, the addresses and the START hand-off
- [Home's chat](docs/HOME_CHAT.md): the chat dock, its roles and moderation sheet, emoji and voice, and the login
- [Home's games](docs/HOME_GAMES.md): the Games list, Quick join, Quick host and the public Room
- [How the game plays](docs/GAMEPLAY.md): the data files, the 20 Hz simulation, the six kinds of ant and their abilities, movement, food, power-ups, combat, the anthill, the start and end of a match, fog of war
- [View and HUD](docs/VIEW_AND_HUD.md): the 16:9 view, mouse-wheel zoom, edge scrolling and the minimap, the HUD, messages and the chat log, the results screen, sound and music
- [Controls](docs/CONTROLS.md): the start menu, the setup screen, options and settings, the mouse controls, the keyboard
- [The settings menu](docs/GAME_MENU.md): what Esc opens in a match (sound, video, controls, Leave game), the one table of rows, and the browser's panel
- [Touch](docs/TOUCH.md): playing with a finger on a phone or a tablet
- [Network play and bots](docs/MULTIPLAYER.md): LAN and server games, lag and drop-outs, computer players
- [Replays](docs/REPLAYS.md): recording a match in the desktop game, the file, and the tool that lists every order of a recorded match
- [Heatmaps](docs/HEATMAPS.md): where ants died and where the action was in a match: the map of deaths and the activity curve of the replay page
- [Command-line options](docs/COMMAND_LINE.md): every option of the game and its environment variables

**Building, running and testing**

- [Build and run](docs/BUILD_AND_RUN.md): prerequisites, building, the launch scripts, sanitizers, Docker
- [The dedicated server](docs/SERVER.md): `ants_server`, its options, the control interface, reconnecting, the Docker stack
- [Kept matches on the server](docs/SERVER_REPLAYS.md): the replay store, its limits and matches of computer players
- [Public rooms and the list of games](docs/PUBLIC_ROOMS.md): the leader's choice, the idle rule and `GET /lobbies` of the game server (network protocol 22)
- [The site statistics](docs/SITE_STATS.md): `GET /stats` and `POST /stats/local`, the counters and the nginx blocks
- [The chat hub](docs/HUB.md): `ants-hub`, the global chat service next to the game server: its protocol and limits, the text rules, names and presence, the settings, the stack and nginx wiring
- [Roles, the moderators' log and the admin page's door](docs/HUB_ROLES.md): who is a moderator or an administrator (the Discord roles), what is written down about their actions, who may use the admin page
- [The hub's data and the login](docs/HUB_LOGIN.md): what the hub keeps in its database, and the login with Discord
- [Alerts, invitations, mentions and follows](docs/HUB_PEOPLE.md): the account's list behind the bell, `/invite`, `@name` and `/follow`: the frames, the limits and who is never told
- [Private messages](docs/HUB_MESSAGES.md): who may write to whom, what is kept (7 days), Block, reports and what the staff can see
- [The chat commands](docs/HUB_COMMANDS.md): the slash commands of the chat, who may use which, the helper, `/bots` and the hub's door to the game server
- [Images in the chat](docs/HUB_IMAGES.md): who may post images, the converter that re-encodes them, the limits, moderation and `/noimages`, how they are stored and served
- [Testing and CI](docs/TESTING.md): the tiers, how to run a suite, the rules for tests and mutants, the E2E runner, continuous integration
- [What the test suites cover](docs/TESTING_SUITES.md): the table of the C++ suites by number and what each one checks
- [The python tests](docs/TESTING_PYTHON.md): suite 5.2: what the scripts of `tests/scripts` cover
- [Workflow](docs/WORKFLOW.md): branches, pull requests, releases and the deploy; the rules for contributors are in [`AGENTS.md`](AGENTS.md)

**How it works**

- [Architecture](docs/ARCHITECTURE.md): the source tree and the libraries
- [The original program](docs/ORIGINAL_PROGRAM.md): reverse engineering, what the remake takes from the original and what it adds, and the differences made on purpose
- [`GAME_REVERSE_ENGINEERING.md`](docs/GAME_REVERSE_ENGINEERING.md): the specification of the original game, system by system
- [`MAP_FORMAT.md`](docs/MAP_FORMAT.md): the map file (`.lvl`): its layout as the remake reads and writes it, the limits, the rules of a map that loads in the original, the round-trip law of the writer, the dictionary and the blank map of a new map, `lvl_tool`, the core in the browser (`lvl_core.wasm`)
- [`MAP_EDITOR.md`](docs/MAP_EDITOR.md): the map maker's page: the two kinds of map (including per-spawn starting ant types on Extended maps and playing Extended maps up to 512 x 512, with limits on long orders), loading any current game map as a starting point, the rules that keep a map from ever being written over, the pull requests of the editor and which are done, how the page is made
- [`MAP_EDITOR_TESTS.md`](docs/MAP_EDITOR_TESTS.md): the map maker's tests: the node checks, the page's wiring and the opt-in browser checks
- [`MAP_PAGES.md`](docs/MAP_PAGES.md): the public page of every library map (`/map/NAME`) and the index (`/map-library`): the addresses and their ledger, what a page holds and where each fact comes from, the maker rule, the index's search, how `tools/map_pages.py` makes them at the image's build, the nginx routes and the tests
- [`MAP_AI.md`](docs/MAP_AI.md): a person's own AI in the open Map Maker tab: the keys (made on the Connect an AI sheet, look or edit, 7, 30 or 90 days), the hub's HTTP and MCP doors and the relay to the one tab that is armed, the tools the AI may call and what they never reach, the six skills served under `/ai/skills/`, the limits, the switch (`ANTS_HUB_AI_KEYS`) and what is never logged
- [`MAP_SUBMISSIONS.md`](docs/MAP_SUBMISSIONS.md): a player sends a map for approval and the maintainer decides: the rules, the API that the editor's Submit button uses (limits, states, error codes), the admin page at `/admin` and its door (no accounts of its own: the chat hub's Discord login decides who may use it, and the server checks the hub's signed pass), the lists the lobby reads (`/maps`, `/maps/all`, including approved Extended maps with an `Extended` badge in the lobby and map maker), how to deploy it and what is off until it is configured
- [`NETWORK_PORT.md`](docs/NETWORK_PORT.md): the network design and the protocol by version
- [`BOTS.md`](docs/BOTS.md): the computer players: the rule, fairness, the architecture and interfaces, the levels, the standard bot, running bots (and [`docs/reverse_engineering/bomb_gates.md`](docs/reverse_engineering/bomb_gates.md) for the bomb-gate analysis and universal gate-goal planner)
- [`BOT_TACTICS.md`](docs/BOT_TACTICS.md): the standard bot's tactics: task ranks, strategy, power-ups and the flowers, defence, raids and thieves, aggression and fights, the war and stand batches
- [`BOT_PLAY.md`](docs/BOT_PLAY.md): how the standard bot plays: styles, the gate, the stall detector, the can't-go loop, bombs on the way, alliances, walled maps and the game type 187
- [`BOT_ISLANDS.md`](docs/BOT_ISLANDS.md): the computer players on water maps (ISLANDS and SMALL): the expedition, the ferry, the swarm and the bridges
- [`BOT_LIMITS.md`](docs/BOT_LIMITS.md): the known limits of the computer players
- [`BOT_GATES.md`](docs/BOT_GATES.md): walled maps: the map's own bombs left to the gate goals, the field breach (food beyond a field of bombs, a fire wall and water: POPcOrN and its family), the measurements, and POPcOrN in a match
- [`BOT_FIGHT.md`](docs/BOT_FIGHT.md): the Hard computer player's quick kill (an enemy beside water, a bomb or a fire wall, struck from the tile that throws it in), fights that a walk reaches, hunting when there is nothing to bank, and the measuring tool `bot_watch`
- [`BOT_187.md`](docs/BOT_187.md): the computer players in the game type 187: the brawl (packs on the enemy ants, no ant sent home because the hill is closed, what the engine allows), the measurements, what was tried and left out
- [`P2P_HINTS.md`](docs/P2P_HINTS.md): hints and direct channels between players (network protocol 26): another player's orders, and a computer player's, reach the prediction ahead of their sealed turns; the relay as the default and the switch for a direct connection
- [`MATCH_COUNTDOWN.md`](docs/MATCH_COUNTDOWN.md): the 30 s countdown with the chat open before an online match: the Begin that tells it, the card, the Fullscreen button
- [`BOT_NAMES.md`](docs/BOT_NAMES.md): the names of the computer players ("Pip (bot, Hard)"): the list, the picker, the label, the names a person may not take, network protocol 20
- [`ANT_3D_MODELS.md`](docs/ANT_3D_MODELS.md): procedural 3D ant models (`.glb`), the 88 animation clips, the `/test_3d/` 1998 comparison viewer and 3D studio, and the separate Docker build stage

## Reverse Engineering & Historical Preservation

The method, the tools and the use of your own copy of the original program are described in [The original program and reverse engineering](docs/ORIGINAL_PROGRAM.md), and what the project knows about the original is written down in [`docs/GAME_REVERSE_ENGINEERING.md`](docs/GAME_REVERSE_ENGINEERING.md).

## License

This game is free to play and free to use. It is provided "as is", with no warranty of any kind, express or implied. Its authors are not liable for any damage, data loss or other problem from using it, including online play, the bots, the website and the servers. There is no promise that the online servers will be up: they may be slow, down, restarted or shut down at any time, with no notice. It is an unofficial fan remake of the 1998 game Ants, not affiliated with or endorsed by the authors or owners of the original game. The same notice, in plain words, is the site's **[Legal page](https://beta.playants.org/legal.html)**; it is not legal advice.

The source code and the documents written for this project are released under the **MIT License** ([`LICENSE`](LICENSE)): anybody may use, copy, change and distribute them, including in other projects and commercial ones, as long as the licence text stays with them.

**The original game is not part of that licence.** From the original 1998 game the repository holds its data and pictures made from it: the archive `Original-Ants/ants.chd` (artwork, animations, sounds), the maps `Original-Ants/Maps/*.LVL`, the music (`Original-Ants/*.MID` and the `*.mp3` renders of those pieces that the game plays), the sprites and sounds decoded from the archive (`asset_catalog/`, and the animation table `docs/chd_table4_animations.json`) and the front-page pictures cut out of the artwork (`web/front/`). The web build also packs the folder `Original-Ants/` into its data bundle. They belong to their copyright holders; this project does not own them and cannot license them. The original game's program is not in the repository (see [The original program and reverse engineering](docs/ORIGINAL_PROGRAM.md#the-original-program-is-a-local-reference-not-part-of-the-repository)). The data files are in the repository for now so that the remake can run and so that its tests can compare it with the original; replacing the original artwork with open material is planned as a separate, later project, until the remake needs nothing of the original but a copy that the player already has. If you hold rights to something in this repository and want it removed, open an issue and it will be taken out. Third-party components (SDL2, dr_mp3, Libre Franklin, ...) keep their own licences: see [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

**The players' maps are not part of that licence either.** `Community-Maps/` holds maps that players of the original game made and shared; each belongs to its maker (the makers were not recorded). [`Community-Maps/README.md`](Community-Maps/README.md) says where they come from, which were repaired and which were left out. If you made one and want it removed, open an issue and it will be taken out.
