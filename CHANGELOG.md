# Changelog

What changed in each release of the Ants remake, newest first, a few lines each: what a player sees and, only when it changed, the rules or the network protocol. The version is the one line of the file `VERSION` (how it moves: [`docs/WORKFLOW.md`](docs/WORKFLOW.md)); every build also has a build id, the short git commit, shown by `ants --version`, `ants_server --version` and the footer of the web page. The long notes of every release up to v0.1.0, with their measured numbers and sources, are the detailed history, [`docs/CHANGELOG_ARCHIVE.md`](docs/CHANGELOG_ARCHIVE.md) (on the site: `/changelog_archive.html`).

An entry is 5 - 15 lines in a fixed template: the heading `## vX.Y.Z - YYYY-MM-DD - title`, then **For players:** (1 - 6 bullets), **Rules / network:** (only if the rules or the network protocol changed: what, and the protocol number), **Fixes:** (optional, one line each) and **Details:** (a link to the commit range). No test counts and no mutation or review lists: those belong in commit messages and documents. Merged work that is not released yet is collected under **Unreleased** and becomes the next entry when `VERSION` moves.

<!--
Template of an entry (copy it, keep the labels and the order, leave out a paragraph that has nothing to say):

## vX.Y.Z - YYYY-MM-DD - title

**For players:**
- one bullet: what a player or a server owner sees or can do now

**Rules / network:** only when the rules or the protocol changed: what, and "network protocol N: a vA.B.C game cannot join"

**Fixes:**
- one line each (optional)

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/OLD...NEW), [detailed notes](docs/CHANGELOG_ARCHIVE.md)
-->

## v0.1.2 - 2026-10-03 - Treasure is the default map; the web page opens in 16:9

**For players:**
- Treasure, the map that is played most, is the default everywhere a map is chosen for you: the setup screen highlights it (the list and its order are the original's), the desktop start menu's Host panel opens on it, the web page's Play online form preselects it, and an online room whose code names no map is a Treasure room. A choice you made and saved still wins.
- The web pages open in 16:9 again for everyone: a "Classic 4:3" choice that a browser remembered earlier is forgotten once. Pick Classic 4:3 again under the game to keep it (`?aspect=4:3` in the address still works).

**Rules / network:** None: the rules and network protocol 11 are unchanged (v0.1.0 to v0.1.2 games play together).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/ff52c2d...74c8085), [the setup screen's deviation](docs/AUDIT_ONE_TO_ONE.md)

## v0.1.1 - 2026-10-03 - Bots wait for the start dialog

**For players:**
- Computer players no longer move during the "Get ready to play!" dialog at the start of a match. Their first orders come after it closes, when you can give yours, and they come one by one at the speed of their level: about 5.4 s into the match at Hard, 6.2 s at Medium and 8 s at Easy. This holds in a game against `--bot` or the start menu's bots, in a room on the local network and on the online server.
- The footer of the web page and `ants --version` / `ants_server --version` now name the build (the short git commit) next to the version, for example "Version v0.1.1 - build abc1234". The version in the corner plate is unchanged.
- This changelog is short: one entry per release in a fixed template. The detailed history of every release up to v0.1.0 (the old 556 KB file, unchanged) is in `docs/CHANGELOG_ARCHIVE.md` and on the site at `/changelog_archive.html`, linked from the changelog page.
- Internal: one version source (the file `VERSION`), a build id at every build, `./run_tests.sh --fast` (about a minute) with the time of every suite, `docs/WORKFLOW.md` (the three test tiers, branches and batch pushes, the version policy) with the rules of `AGENTS.md` that say the same, ccache in CMake when it is installed, and a README without per-suite counts.

**Rules / network:** The rules did not change. Network protocol 11: v0.1.0 and v0.1.1 games play together (the bots run on one machine and their commands travel as data).

**Fixes:**
- The bots could move before the players could: a bot looked on the first tick and started with a full budget, so a Hard bot could send ten orders in one tick while the dialog still took your clicks. They now wait for the same 100 ticks as the dialog and start with one order's worth of budget.
- Test 12.108 no longer pins the value of the version. It checks the format; the value lives in the file `VERSION`, and a check run by `./run_tests.sh --fast` and by CI compares the top release heading of this file, "current release" in `STATUS.md` and the version line of the README with it.
- The reverse-engineering notes said that the dialog lasts 6.0 seconds; the program keeps it up at least 5.0 s, which is what the game does, and the notes now say so.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/3e5bfb5...8a29f14), [workflow](docs/WORKFLOW.md), [notes](docs/audit/B3_notes.md)

## v0.1.0 - 2026-10-02 - Online rooms: bots fill the empty seats at START, chat in the waiting room, team chat only to allies, and mouse-wheel zoom

**For players:**
- The leader of an online room (or the host of a LAN game) can press START with empty seats and have bots fill them: "Bot (Easy)", "Bot (Medium)" or "Bot (Hard)". One person alone is enough. Choose the level in the desktop Host panel ("Empty seats at START"), on the Play online page, or with `--fill-bots none|easy|medium|hard`. Bots gather food but do not fight yet, and with Fog of War on no bots are seated.
- Everybody in the waiting room can chat (press T or click the chat box). The 16:9 setup screen has a chat box with the room's lines, and the leader sees "Empty seats at START: Medium bots". The waiting room's lines start the match's chat log.
- Team chat now reaches only you and your ally. Chat for all still reaches everybody.
- The mouse wheel over the map zooms between 0.5, 1 and 2 toward the pointer, and the middle button goes back to 1. The level is remembered, and `--zoom 0.5|1|2` overrides it. In a network match only 1 and 2 are offered, because zooming out would show more map than the other players see. On the web page the wheel and a trackpad pinch over the game zoom it.
- The web game now starts in 16:9 on every device, phones held upright included. The switch under the game still offers the classic 4:3 picture and remembers it.

**Rules / network:** Network protocol 11: a v0.0.99 game cannot join a v0.1.0 server or LAN game, and the other way round. Update the game, the server and the page together. The simulation rules did not change.

**Fixes:**
- On Windows, the server retries reading its secret file for up to half a second when another program holds it.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/b3b0adb...3e5bfb5), [detailed notes](docs/CHANGELOG_ARCHIVE.md)

## v0.0.99 - 2026-10-02 - The game is 16:9 by default: more map, a frame grown from the original's art, the web page in 16:9, a new setup screen with a map preview

**For players:**
- The desktop game opens in a 16:9 picture with a map view about twice as large (762 x 500 pixels instead of 442 x 440). The frame is the original's own art, made wider by repeating single plain lines, and the score boxes are spread along the bottom strip. `--aspect 4:3` (or `aspect=4:3` in the settings file) gives the original's picture exactly as before.
- The window opens at the largest 0.5 step of 960 x 540 that fits your display. Alt+Enter toggles fullscreen. A map smaller than the view is centred with black around it.
- The web game is 16:9 by default, in a box that fits the window, with a fullscreen button. The selector "16:9 / Classic 4:3" under the game, or `?aspect=4:3`, gives the classic picture. A phone held upright gets the classic picture in this release. The four games of the Play online page are 16:9 too.
- The setup screen of the 16:9 picture is new, with a map preview that is the game's own picture of the selected map. The classic 4:3 screens are unchanged.

**Fixes:**
- The web page downloads the game's data again when a download fails (with several games on one page it could stay at "Downloading data (0%)" for ever), and shows a card with a Reload button when the game's files cannot be loaded.
- The web guide and footer lost some spaces ("Goal:get the most points"); fixed.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/11b8561...b3b0adb), [detailed notes](docs/CHANGELOG_ARCHIVE.md)

## v0.0.98 - 2026-10-02 - Reconnect, part A: the server can hold a lost player's seat (off by default)

**For players:**
- Nothing changes for a player until a server operator switches it on, and the games do not come back by themselves yet (that is part B, next).
- A server started with `--reconnect` (or a room made with `"reconnect": true`) keeps the seat of a player whose connection is lost and pauses the match for everybody. After 30 seconds away the others can vote to go on without that seat. A player who comes back with its key is given the match again and plays on once the server has checked its game.
- After a pause of 3 seconds or more the match is held for a 10 second countdown before it goes on. New server options: `--hold-vote-seconds`, `--max-pause-seconds` (cap on the total pause, default 30 minutes), `--max-catch-up-seconds`, `--resume-countdown-seconds` and `--log-mb`. The room's status shows who is absent, the vote and the paused time, never a key.

**Rules / network:** Network protocol 10 (the Hello and the Welcome carry a key, and five messages are new): a v0.0.97 game cannot join a v0.0.98 server's room or LAN game, and the other way round. The simulation rules did not change.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/621f579...11b8561), [detailed notes](docs/CHANGELOG_ARCHIVE.md)

## v0.0.97 - 2026-10-02 - The desktop start menu: Single player with bots, Join with a code, Host an online match

**For players:**
- A desktop game started with no mode on its command line now opens with a start menu after the loading screen: Single player, Join with a code, Host an online match, Quit. The menu never shows in the web build.
- Single player: pick Empty, Easy, Medium or Hard bot for each of the other three colours. All Empty is the original's single-player game exactly. The choice is remembered.
- Join with a code: type your name and the room code (paste with Ctrl+V or Cmd+V) and Join. Every failure (server not reachable, no such room, room full, match started, another version, and so on) comes back to the panel with a clear line, and Esc cancels at once.
- Host an online match: pick the map, 2 to 4 players and your name. The menu makes the room, shows its code in large letters with a Copy button, and puts you in as the leader. After an online game the program goes back to the menu.
- The original's own screens are unchanged. `--map`, `--host`, `--join` and similar options skip the menu, `--start-menu` forces it, and `--server HOST[:PORT]` names the game server.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/2d048ef...621f579), [detailed notes](docs/CHANGELOG_ARCHIVE.md)

## v0.0.96 - 2026-10-02 - The web game starts at once and keeps playing in a hidden tab

**For players:**
- The web page no longer hides the game behind a "Click anywhere to play!" card. The game starts as soon as its data has downloaded, with the original's loading screen and quick help, and sound and music begin at the first click, tap or key press on the page.
- A network match keeps playing when its tab is hidden or its window is minimised. Before, a hidden seat was called lagging after 3 seconds and dropped after 30. Now it keeps up and the other players are not told it lags.
- A hidden page plays no sound or music, and a local game stands still while its page is hidden. A sleeping computer or a phone with a locked screen still stops the page.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/be5cc38...2d048ef), [detailed notes](docs/CHANGELOG_ARCHIVE.md)

## v0.0.95 - 2026-10-02 - Community maps play as in the original: their default ant types and power-ups by tile

**For players:**
- A map that names a default ant type has every ant of every team of that type, the starting ants and every hatch. For example, `popcorn.lvl` hatches combat ants.
- Power-ups and flower droppers are recognised by their tile and look the same on every map, whatever the map file calls them. A power-up named "." is no longer turned into a rock.
- A Bomber, Fire ant or Swimmer that is busy (planting, lighting, bridging, picking up a power-up and so on) gets the plain move cursor, and a right click is a plain move, as in the original.
- A pick-up rebuilds the panel of a selected ant every time: the text of its type and both command pedestals pop up, also when the ant takes the power-up of its own type again.

**Rules / network:** The end of every attack now resumes the saved auto-engage of any ant that still has it, as in the original. An ant that took another power-up during a Combat Ant's auto-engage walks back to where the engage began. This changes some plays of the shipped maps. Network protocol 9: a v0.0.94 game cannot join.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/f3c3d47...be5cc38), [detailed notes](docs/CHANGELOG_ARCHIVE.md)

## v0.0.94 - 2026-10-02 - Less lag: ping and delay next to the FPS counter, 50 ms turns, an adaptive buffer, and a lagging player no longer freezes the others

**For players:**
- "ping NN ms" and "delay NN ms" now show next to the frame rate in a room and in a network match, on the web too (never in a game of one machine).
- Commands reach the game about 100 ms sooner on a steady link: turns are 50 ms instead of 100 ms, and the buffer adapts from one to four turns to the link's jitter.
- A server's room no longer waits for a lagging player. The others see "Bob is lagging (12 s behind)" from 3 seconds, and the laggard sees "You are lagging" and "Catching up..." while its game runs the backlog at up to 4x. A player 60 seconds behind, or silent for 30, is dropped and told "You were away too long and were dropped from the match." A LAN host with a seat still waits up to 3 seconds for a friend.

**Rules / network:** Network protocol 8 (a new Lag message): a v0.0.93 game cannot join. The simulation rules did not change.

**Fixes:**
- "Waiting for the other players..." no longer stays on screen after a stall, and turns that piled up (a hidden window, a frozen link) are run down instead of leaving a game half a second behind for good.
- A burst of orders after a stuck connection is queued, not counted as violations that could throw the player out.
- The frame-rate counter shows the real rate below 10 frames a second (it read "10 FPS").

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/ce61aaa...f3c3d47), [detailed notes](docs/CHANGELOG_ARCHIVE.md)

## v0.0.93 - 2026-10-01 - The room leader can start early

**For players:**
- The first player to join a server's room is its leader and can press START to begin the match with the players who are there (two at least). With fewer than two the can't-go cue plays and nothing is sent. When the leader leaves, the next earliest player leads. A LAN game has no leader: its host starts as before.
- The leader's setup screen is the original's host screen: START works, and the map and Fog of War buttons show the room's choice and change nothing. The Play online page says that the first player can start early.
- Server operators: `POST /rooms` takes `"early_start": true|false` (true when left out, on for demo rooms), and the room status shows `early_start`, `leader` and `ignored_start_requests`.

**Rules / network:** Network protocol 7 (the Room message names the leader, and StartRequest is new): a v0.0.92 game cannot join. The simulation rules did not change.

**Fixes:**
- One connection could flood the server with valid messages (pings, acknowledgements, chat and so on) and use a whole core and over a gigabyte of memory without being dropped. Inboxes are bounded, every connection has a message budget, and a flooder is dropped within a second.
- Reloading the web game no longer downloads about 9 MB again: the files are revalidated and an unchanged build answers 304.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/4344c0e...ce61aaa), [detailed notes](docs/CHANGELOG_ARCHIVE.md)

## v0.0.92 - 2026-10-01 - The black bars of a wide window scroll the map, fullscreen keeps the mouse, four games in the corners of their hills, bot fixes

**For players:**
- A wide window (fullscreen on a wide monitor) no longer ignores the pointer over its black bars: pushing the mouse into a bar scrolls the map like the edge does.
- Fullscreen (`--fullscreen` or a macOS fullscreen Space) keeps the mouse inside the window while the game has focus. A windowed game never holds the mouse.
- The Play online page and the four-window start script place the games by colour: Black top left, Green top right, Red bottom left, Blue bottom right.
- Internal: a safety net for the planned widescreen work, a GCC 12 build with warnings as errors, and review fixes of the computer-player code. The classic picture and the simulation did not change.

**Fixes:**
- A mouse button released outside the window, or a finger lifted from a touch screen, no longer leaves the map scrolling on its own.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/2185be0...4344c0e), [detailed notes](docs/CHANGELOG_ARCHIVE.md)

## v0.0.91 - 2026-10-01 - The original program leaves the repository

**For players:**
- Internal: the original 1998 program (Ants.exe), its decompilation and the local tools of an installation of the original are no longer in the repository, which keeps only the data archive, the maps and the music. Nothing in the game, the server or the network protocol changed but the version number. Pulling this change into an existing checkout deletes those files from the working tree, so copy them aside first.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/64654b3...2185be0), [detailed notes](docs/CHANGELOG_ARCHIVE.md)

## v0.0.90 - 2026-10-01 - Play online: host a match, join it with a code, any map, 2 to 4 players

**For players:**
- The Play online page (linked from the game page) now hosts matches: choose one of the six maps of the original and 2, 3 or 4 players, then "Create the match". The page makes a room code, shows a link for anybody, and offers "Play here", "Open a window" and "Copy link" for each seat, plus "All seats on this page" and "All seats in separate windows".
- Join: type the code and your name, and the game opens with the first free seat. A link that names a room (`four.html?room=...`) opens its panel at once, and a reload or a bookmark comes back to it.
- The match starts by itself when every seat is taken. A room waits up to ten minutes for the others. A running match takes nobody new, and a code whose room is over makes a new room when it is used again. Rooms of this page have no computer players yet.
- Every game on the page reports its tick and state hash every 100 ticks, and the page says whether they agree ("In step").
- Server operators: the room code chooses the map and the players (`demo-[<map>-][<n>p-]<anything>`), `--demo-maps A.LVL,B.LVL,...` lists the maps a code may choose, and a demo room now waits ten minutes (it waited one minute). The stack starts with the six original maps and 12 demo rooms.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/ea3bc89...64654b3), [detailed notes](docs/CHANGELOG_ARCHIVE.md)

## Older versions (v0.0.89 and before)

Every release before v0.0.90, and the long notes of v0.1.0 and v0.0.99 - v0.0.90, are in the detailed history: [`docs/CHANGELOG_ARCHIVE.md`](docs/CHANGELOG_ARCHIVE.md).
