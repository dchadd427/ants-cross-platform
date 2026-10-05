# Changelog

What changed in each release of the Ants remake, newest first, a few lines each: what a player sees and, only when it changed, the rules or the network protocol. The version is the one line of the file `VERSION` (how it moves: [`docs/WORKFLOW.md`](docs/WORKFLOW.md)); every build also has a build id, the short git commit, shown by `ants --version`, `ants_server --version` and the footer of the web page. The long notes of every release up to v0.1.0, with their measured numbers and sources, are the detailed history, [`docs/CHANGELOG_ARCHIVE.md`](docs/CHANGELOG_ARCHIVE.md) (on the site: `/changelog_archive.html`).

An entry is 5 - 15 lines in a fixed template: the heading `## vX.Y.Z - YYYY-MM-DD - title`, then **For players:** (1 - 6 bullets), **Rules / network:** (only if the rules or the network protocol changed: what, and the protocol number), **Fixes:** (optional, one line each) and **Details:** (a link to the commit range). No test counts and no mutation or review lists: those belong in commit messages and documents. Merged work that is not released yet is collected under **Next** (a section headed `## Next`, above the newest release); `tools/release.py` turns it into the next entry when `VERSION` moves.

<!--
Template of an entry (copy it, keep the labels and the order, leave out a paragraph that has nothing to say):

## vX.Y.Z - YYYY-MM-DD - title

**For players:**
- one bullet: what a player or a server owner sees or can do now

**Rules / network:** only when the rules or the protocol changed: what, and "network protocol N: a vA.B.C game cannot join"

**Fixes:**
- one line each (optional)

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/OLD...NEW), [detailed notes](docs/CHANGELOG_ARCHIVE.md)

Work that is not released yet is written in the same template under a heading that says only "## Next" (no version, no date), above the newest release;
`tools/release.py X.Y.Z "title"` turns that heading into "## vX.Y.Z - date - title" and refuses when there is no "## Next" or it is empty.
-->

## Next

**For players:**
- **One card, New match:** the front page's two cards (a game against the computer, and Play online) are one. Pick the map and your colour (**Sit here** moves you), then make every other seat a **Friend** (a person you invite), a bot at **Easy**, **Medium** or **Hard**, or **Nobody**; with three or four players **Teams** pairs two of them. **START!** takes you in: against bots the match begins at once, with Friends it begins when they are in. The card remembers its choices.
- **An invitation for each Friend**, shown in the card with its colour, **Copy link** and, where the browser has it, **Share**. The links carry no name and no key; a friend is asked for a name first, and can go back to the front page from that step.
- **A level for every bot seat, at any colour** (a bot may sit at Green too), and the room's teams: in the desktop game's Host panel as well.
- A game against the computer from the front page is now a room on the game server; an old address with `&players=1` still plays on your computer.
- **Clearer messages in the waiting room:** when a friend's colour was taken the game says which colour you play instead; when the server has no room left for a new match it says so (try again in a few minutes) instead of "no such room"; and a seat that does not play is named by its colour.

**Rules / network:** network protocol 13: a start carries a bot level for each seat and the teams, so a v0.6.x game cannot join (reload the page once after the update). A room's teams are a word of its code (`demo-treasure-4p-t01-k7m2xq`).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/OLD...NEW), [the front page](docs/NETWORK_PORT.md), [bot games](docs/BOTS.md)

## v0.6.0 - 2026-10-05 - An online match waits for you

**For players:**
- **An online match waits for you:** if you reload the page, lose the network or your phone falls asleep, your seat is held and the match pauses for everybody ("Ann (Green) lost the connection, waiting 0:06"); when you come back the match goes on where it was. The page keeps a key for three hours and the front page shows **Rejoin your match (CODE)** while it holds one (the desktop game keeps it in a file and rejoins by itself). After 30 seconds the others may vote to go on without you; the pauses of a match are capped at 30 minutes (10 in the rooms the front page makes).
- **Leaving on purpose is immediate:** Menu or the logo, then Yes, drops your seat at once: nobody waits for you. A closed tab or a reload still holds it.
- **Matches survive a restart of the game server:** a running match is replayed from its record, and its players come back with their keys.

**Rules / network:** the network protocol stays 12. The server holds seats by default now (`--no-reconnect` turns it off, a room's `"reconnect": false` too). Operators: on the native TCP door (port 4001) a seat's key travels in clear; close that port or use `--no-reconnect` if that matters (the browser's door is the WebSocket one).

**Fixes:**
- A reload of the game page no longer asks for a name: it takes your seat back (the address carries your seat).
- An online match that nobody comes back to gives its place up (it held a slot for as long as it was paused).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/09961f8...ec4906c), [the notes](docs/audit/persist_notes.md), [Network Port](docs/NETWORK_PORT.md)

## v0.5.1 - 2026-10-05 - Every page in the front page's look

**For players:**
- **Every page in the front page's look:** the game page, the changelog pages and "Sprites and sounds" now have the front page's clay, thin green frame, teal buttons and black boxes, the "ants!" logo (a link back to the front page; on the game page it asks first while a match runs, as Menu does) and a Play button. The game page's loading screen shows the logo and a teal bar, and on a phone its header is one row (the logo, Menu, Fullscreen and a More button). Nothing about the game, its keys or its addresses changed; the picture is 3 to 7 percent smaller at common window sizes, because of the frame's room.
- **Phones:** "Sprites and sounds" shows its animation details and its sprites table (they were cut off) and keeps its Play button; every page fits from 320 to 1600 px wide with no sideways scroll.
- **A way back from a shared link:** the name step that a shared room link opens has a "Back to the front page" link.

**Fixes:**
- The two changelog pages link to each other (the short page's links to the detailed history opened GitHub's file view).
- A bullet after a blank line in a changelog was written outside its list.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/60dea01...8511283), [the pages](docs/audit/web_home_notes.md)

## v0.5.0 - 2026-10-04 - A new front page in the game's own look, with game statistics

**For players:**
- **A new front page in the game's own look:** the 1998 game's menu style (the clay, the green frame, the teal buttons, the original "ants!" logo and START! button, a picture of the chosen map). Two cards: **Play vs the computer** (each opponent's level is a row of one-click buttons; Teams as before) and **Play online** (host a match or join one by its code); the help is behind "How it works". It fits phones and wide screens with no sideways scrolling, and every address, link and remembered choice works as before.
- **Game statistics on the front page:** a line under the welcome banner shows the matches being played and the players online now, and the games played today and in all: online matches of 30 seconds or more, and single-player games (a game in the browser tells the server once that it began: a count, nothing else).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/56c4c5d...2b28516), [the front page](docs/audit/web_home_notes.md), [the statistics](docs/audit/site_stats_notes.md)

## v0.4.0 - 2026-10-04 - Many zoom levels, a level for each bot, teams before the start

**For players:**
- **Many zoom levels:** the mouse wheel steps through four levels to a doubling (2, 1.68, 1.41, 1.19, 1, 0.84, 0.71, 0.59, 0.5 ...) down to the map's own limit, where the map's width or height just fills the view, so you never see past its edge; matches on the network offer the zoom-out too (it was local games only), and with Fog of War on every level hides exactly what zoom 1 hides. 2, 1 and 0.5 are exact pictures, the levels between are smoothed, scrolling moves whole screen pixels and a click picks the tile under the middle of the pointer. `--zoom` and the settings key `zoom` take any number from 0.05 to 2.
- **A level for each bot and teams before the start.** On the front page each bot (Red, Blue, Black) has its own level (None, Easy, Medium, Hard), and with two or more bots a Teams choice (Free for all, or you and one bot against the others); the desktop start menu has the same Teams choice and `--teams 0+1` does it from the command line. The teams are made with the original's own team-up at the first tick. A bot that declines a team-up says why ("Bots team up only while three or more teams play.", "You already have a teammate.").
- **Your name in single player on the desktop:** the start menu's Single player panel has a name field (the same remembered name as Join and Host), and the game is played under it.
- **Your own orders can show at once in a network match** (off by default: add `?prediction=on` to the game's address, or start the desktop game with `--prediction on`): your ants answer a click as in a game on one computer instead of a round trip later; the corner's `delay` shows what a click feels. It switches itself off for a while on a device that is too slow for it, and is off in a hidden tab, in a pause and behind the "Get ready" dialog.
- **Desktop:** `start_game.sh` / `start_game.bat` open one game with the start menu (Single player, Join, Host); the four-window test match is `--players 4`.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/9e6ed37...6744e04), [the zoom levels](docs/audit/view_fixes_notes.md), [the prediction](docs/audit/rollback_notes.md), [the bots](docs/BOTS.md)

## v0.3.0 - 2026-10-04 - Computer players that fight; the front page is the lobby

**For players:**
- **Computer players gather food, raid and fight back.** At every level a bot answers a blow on one of its ants (with one, two or three ants at Easy, Medium and Hard), sends a hit carrier home, guards its thief hole with fire walls, puts out your fire walls and defuses your bombs, and keeps its last ants out of fights; Medium and Hard also take their side's power-ups at the start, contest the middle of the map and raid the leader with a Thief.
- **Hard bots guide their carriers at the hill's gate by hand** and steal an unguarded Thief power-up for a second thief, and every bot draws one of four styles for the match (aggressive, economic, raider, defensive; `--bot 2:hard:raider` pins one), so bots do not all play alike. A bot that gets stuck (a refused click, a hill that its carriers cannot reach) backs off instead of repeating the same order.
- **The front page is the lobby:** [beta.playants.org](https://beta.playants.org) opens it, with single player in it. Players 1 plays on this computer in this tab, alone or against Easy, Medium or Hard bots; 2 to 4 host a match as before. The game page's "Play online" button is now "Menu", and every old game link still works (`/four.html` goes to `/`).
- **The map keeps scrolling when the mouse goes a little past the game's edge** in a browser window (about an inch, corners included); farther out, over a button of the page or out of the window, it stops.
- For whoever runs a server: a match in a room that holds seats survives a restart of the game server (restart records, [`docs/NETWORK_PORT.md`](docs/NETWORK_PORT.md) "Restart records"); rooms hold seats only when asked, so nothing changes by default.

**Fixes:**
- Behind the "Get ready to play!" dialog the ants are drawn again, standing, as in the original (since v0.2.0 only their hit-point numbers were).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/888e788...e57833b), [the bots](docs/audit/B4_1_notes.md), [the front page](docs/audit/web_home_notes.md), [restart records](docs/audit/persist_notes.md)

## v0.2.0 - 2026-10-03 - The clock waits for the start dialog; every screen in 16:9; fullscreen mouse

**For players:**
- **The match clock waits for the "Get ready to play!" dialog.** Every match still opens with the original's dialog for 5 s, but the clock and the ants only start when it closes, so all 12 minutes (or the map's own time) are playable. On the network the host seals the first turn 5 s after the match began and every machine closes its dialog when its first turn runs. Computer players do not move while the dialog is up either, and their first orders come one by one at the speed of their level: about 0.5 s into the match at Hard, 1.3 s at Medium and 3.1 s at Easy (the machine that runs the bots is ahead of a remote person's screen by the link's delay and the buffer, 20 - 290 ms measured, not seconds).
- **Every screen is composed for 16:9:** the loading screen, the quick help at the start, the results and the desktop start menu use the whole 960 x 540 canvas, built from the original's own art, instead of a 640 x 480 page centred on clay. Classic 4:3 (`--aspect 4:3`, `?aspect=4:3`) is as it was.
- **Fullscreen mouse.** On a screen that is not 16:9 the black bars count as the picture's edge: push the mouse into a bar and the map scrolls. On the web page the browser holds the mouse inside the game in fullscreen (the setting "Fullscreen mouse: Locked / Free" under the game, remembered), so every edge and corner scrolls and a Mac's Dock and menu bar stay away; the desktop game keeps the Dock and the menu bar hidden in a fullscreen Space of the green button too.
- **The window opens in 16:9 from the first frame:** the desktop window and each of the four windows of `start_game.sh` / `start_game.bat` are created at the shape of the picture (a 4:3 window used to flash up first). `start_game.sh` builds the game every time it starts and stops when the build fails; it used to launch an old binary for ever.
- **Your name on beta.playants.org:** one "Your name" field on the Play online page, shared by Host and Join and remembered in the browser, puts your name into the game instead of a random one. A link that somebody sends you asks for your name first.

**Rules / network:** The rules did not change. Network protocol 12: v0.1.x games cannot join (refused with a version message; from this release the web page's message says "Reload the page to update.").

**Fixes:**
- A command that reaches the host before the first turn is sealed is discarded: a modified client could script an opening of up to 64 orders per seat that ran at the first tick, ahead of every person. Honest clients are not affected.
- The loading screen drew the frame's pieces in the wrong order (145 pixels), and the results' numbers ran together ("20", "4", "10" as "204 10").

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/ddf134f...f29c2f9), [network notes](docs/NETWORK_PORT.md), [notes](docs/audit/B3_notes.md)

## v0.1.3 - 2026-10-03 - Deploys wait for an idle server; faster checks

**For players:**
- No change in the game itself. For whoever runs a server: the game server answers `GET /busy` with the number of matches that are loading or running and the people in rooms (two counts, no names, no codes), and the site's proxy routes `/busy` to it, so that an update can wait for a moment when nobody plays.
- Behind the scenes: GitHub can now deploy the site itself after every test has passed, and it waits for an idle server first (up to three hours); the site owner switches this on with one secret (docs/WORKFLOW.md). A staging copy of the site can run next to it (`docker-compose.staging.yml`, its pages say "staging"). The tests run in parallel (the full run in about 2.5 minutes instead of 6 to 8), the Windows builds use a compiler cache, and a release is one command (`tools/release.py`).

**Rules / network:** None: the rules and network protocol 11 are unchanged.

**Fixes:**
- CI's script and python steps report every failure instead of stopping at the first.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/b8d2603...e40591f), [workflow](docs/WORKFLOW.md)

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
