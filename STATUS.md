# Status

_Updated 2026-10-05 19:14 PDT · current release **v0.9.1** · details: [CHANGELOG](CHANGELOG.md)_

## Release schedule (the next releases in order; targets in Pacific time)
| Release | Progress | Target | Now |
|---|---|---|---|
| **v0.10.0** the leader of a room can move a player to another colour (network protocol 14) | ████░░░░░░ 43% | Mon night or Tue | built and reviewed with every fix done (draft PR #26); the long local checks and the five CI checks are running; it merges in its turn |
| **v0.11.0** short room codes (no "demo") and platform / operating system icons (protocol 15) | ░░░░░░░░░░ 0% | Wed or later | planned, starts when v0.10.0 is on main |

A release has seven steps, each a seventh of its bar: built, tested here, reviewed, review fixes done, checks green on all five platforms, merged, live. After these: the bots' "Can't go there." fix and flower play, the fire-in ring of 8, mines on the enemy's food path, recordings and replays (version numbers may move).

## In progress
- **Source comments cleaned** (pull request #25, comments only): thirteen comments and five test titles in the bot code say what the code does instead of whose notes a rule came from; reviewed, every finding fixed; merges in its turn
- **Documents, scripts and test banners corrected** (pull request #24, no redeploy): 44 statements that no longer matched the game (23 files) say what the code does now; reviewed, every finding fixed; merges in its turn
- Bots: the flower play (the random power-up droppers on Small, Medium and Gauntlet) is in progress: the bot sees the flowers and what falls, takes the drops on its own side and gets its Fire Ant at home (864 whole matches at Hard: more power-ups taken, scores unchanged within noise); the waiting ant, the learning of the drop rhythm and the recall of a wrong kind are being built and measured (draft PR to come)
- Bots next: swimmers on Treasure, the fire-in ring of 8 with more walls when the enemy has no Fire Ant, mines on the enemy's food path, harassment (the owner has answered five design questions)
- Bots: "Can't go there." is mostly the game's own loop for ants shut in by fire walls, not illegal orders. v0.9.0 no longer orders special actions onto an ant (-95% on Small); draft PR #16 (on main now, checks running) adds a counter and the can't-go checks of the gate, the rescue and the raids: the bots' own refused orders fall 58% on Small Hard and 34 to 45% on Treasure, scores unchanged within noise. The loops themselves stay (the owner's decision); a quieter raid rule costs 5% of the score at Medium and is the owner's to choose
- Short room codes, and the platform and operating system icons (v0.11.0, protocol 15): planned after v0.10.0
- Recordings of finished online matches, with the players' names, to tune the bots (after the bot fixes)
- Your own orders at once in online matches (prediction): smoothing the other players' ants, then on by default
- Replays and watching bots play (1v1v1v1, 1v1, 2v2): designed, the owner approved the pictures
- Host colours (v0.10.0, network protocol 14): the leader of a room taps a player's row to move that player to the next free colour; built and reviewed, draft PR #26; the long local checks and the five CI checks are running, and it merges in its turn
- **Nobody means nobody** (patch, number to come, no protocol change; PR #27): a game for one has only your colony, no hill, ants or eggs for the empty seats; built and tested here, draft PR open.
- **Server tests that fail now and then** (tests only, no release; PR #28): two timing checks, S3.100 on Windows and S3.32 on macOS, are being fixed so that they no longer assume a quiet machine; the draft carries extra diagnostics for now, which come out before it merges.

## On hold (not started; the owner decides when)
- Bots, later steps: the opening trips on a few community maps, tuning and a level ladder, an automatic tuner
- The original's stunned ant may be invulnerable: to check in the original program (a rules change)
- Extinguishing a fire under an ant: does the original allow it for a person? (bots do not; a rules change if it does not)
- Server: the server uses all cores; a load test for the VPS; watching other people's matches live
- Dead-code cleanup, trimming the other big documents (the README is done), Docker hardening, match API, an option to match the monitor's aspect

## Recently done
- **v0.9.1** A player who quits during a catch-up no longer leaves the match paused: the server reads the Leave that came before the reset and drops the seat (native and browser connections) (PR #22, merged 2026-10-05 19:10 PDT)
- **Browser checks without Docker** (no release; PR #13, merged 2026-10-05 19:01 PDT): a session that has no Docker can now build the web pages and run the touch checks and the other browser checks that need no game server; nothing changes for players.
- **Windows test flake fixed** (no release; PR #20, merged 2026-10-05 18:52 PDT): the prediction CPU-budget tests no longer read the machine's clocks, so the Windows (MSVC 2022) check no longer fails now and then in test_prediction RP7.1, RP7.3, RP7.4 and test_netgame N3.30; nothing changes for players (the beta redeploys with the same version).
- **v0.9.0** Smarter computer players: a bot races an enemy for the food that both can reach, hunts a wounded ant until it is dead, lights fire walls only where they cannot simply be put out, never clicks a special order onto an ant (far fewer "Can't go there." from the bots' own orders) and plays harder when it is behind; on ISLANDS and the lake of SMALL bots fly a crew over the water with bomb flights, bring Swimmers across and ferry the food home (four bots on ISLANDS used to score nothing, now about 1,000 Easy to 1,400 Hard points a seat) (PR #14, merged 2026-10-05 18:36 PDT, live 18:39 PDT)
- PR #19 merged: the macOS flakes of test_rejoin_app (RA1.3, RA4.1, RA9.1) are fixed; tests only, no redeploy
- **v0.8.3** Friend by default, a game for one and a dirtier background: a first visit to the New match card has a Friend in the three other seats (it was a Medium bot; a browser that saved its choices keeps them), START! is always on and, with every other seat on Nobody, begins a game for one on this computer (no room, no opponent; you play Green), and the orange background has a little noise and dirt (PR #23, merged 2026-10-05 17:58 PDT, live 18:01 PDT; the owner asked for each of the three)
- **v0.8.2** Rounder buttons with a bevel on every edge: 8 px corners, a face lit from above and a bevel on all four edges, on the front page, the game page, the changelog pages and Sprites and sounds (PR #21, merged 2026-10-05 17:06 PDT, live 17:09 PDT; the owner asked for rounder, less flat buttons, then a bevel on the left edge and slightly smaller corners)
- **README split** (no release): the README is a 63-line front page and each topic has its own page under docs/; every statement of the old README was checked against the game as built (PR #17, merged 2026-10-05 16:29 PDT, docs only).
- **Sanitizer tier (PR #15)**: the four sanitizer suites that ended red without a bug now pass on a clean machine; merged 2026-10-05 15:48 PDT, no redeploy (tests and tools only).
- **v0.8.1** a sharp START! button and 48 demo rooms: the front page's START! is a real button that your browser draws at your screen's own resolution (it was the original's small picture blown up three times, so it looked big and blocky), and the public game server makes up to 48 demo rooms at a time instead of 12, each with a turn log of at most 4 MiB (PR #18, merged 2026-10-05 15:30 PDT, live 15:34 PDT)
- **v0.8.0** one card for every game: the front page's two cards are one "New match" card with four seats (you, Friends with an invitation link each, Easy, Medium or Hard bots, or Nobody), teams for three or four players and START! for every game; a game against the computer is now a room on the game server (network protocol 13; PR #11, merged 2026-10-05 13:52 PDT, live 13:56 PDT)
- **The sanitizer test run builds on GCC 13 again** (task 16, PR #12, merged 2026-10-05 12:20 PDT): a false compiler warning is off in that build only; nothing changes for players
- **v0.7.0** a phone can pan, zoom and right click: hold a finger for a right click (a ring closes first), two fingers move the map and pinch zooms, the page keeps the browser out of the way (made for Android Chrome; touch screens on desktops use it too)
- **v0.6.0** an online match waits for you: reload, a lost network or a sleeping phone holds your seat and pauses the match, the front page offers "Rejoin your match", leaving on purpose is immediate, and matches survive a server restart (network protocol 12)
- **v0.5.1** every page in the front page's look: the game page, the changelog pages and "Sprites and sounds" (with fixes for phones); a way back to the front page from the page that a shared link opens
- **v0.5.0** a new front page in the original game's style (two cards, one-click bot levels, "How it works"); game statistics on it: matches being played, players online, games played
- **v0.4.0** many zoom levels, down to the whole map, in online matches too; a level for each bot and teams before a game against bots; your name in single player on the desktop; your own orders show at once in online matches (opt-in: `?prediction=on`); the start scripts open one game with the menu
- **v0.3.0** computer players that gather food, raid and fight back (Easy, Medium, Hard, four styles; a stuck bot backs off); the front page is the lobby, with single player against bots in the same tab; the map keeps scrolling just past the game's edge in a browser window; the ants behind the start dialog again; restart records on the server (off by default)
- **v0.2.0** the match clock waits for the "Get ready to play!" dialog (network protocol 12: a v0.1.x game cannot join); the loading screen, quick help, results and start menu are composed for 16:9; fullscreen: the black bars count as the picture's edge, the pointer lock on the web page, the macOS Dock; the windows (the start scripts' four too) open in 16:9 and `start_game.sh` always builds first; your name on the Play online page, asked first for a link that somebody sent you
- **v0.1.3** deploys can wait for an idle server (the game server's `/busy`; switched on by the site owner), a staging copy of the site, tests in parallel, a release in one command
