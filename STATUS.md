# Status

_Updated 2026-10-05 18:07 PDT · current release **v0.8.3** · details: [CHANGELOG](CHANGELOG.md)_

## Release schedule (the next releases in order; targets in Pacific time)
| Release | Progress | Target | Now |
|---|---|---|---|
| **v0.9.0** the bots: contested food, fights, safe fire-in, island and swimmer play | ███████░░░ 71% | Mon or Tue | PR #14 is the release pull request (both batches joined, both independent reviews answered, version and changelog done); v0.8.3 is out, so main is merged in and the five checks are running again; it merges when they are green and no match is running |
| **v0.10.0** short room codes (no "demo") and platform / operating system icons (protocol 14) | ░░░░░░░░░░ 0% | Tue or later | planned after the bots (v0.9.0) |

A release has seven steps, each a seventh of its bar: built, tested here, reviewed, review fixes done, checks green on all five platforms, merged, live. After these: the bots' "Can't go there." fix and flower play, the fire-in ring of 8, mines on the enemy's food path, recordings and replays (version numbers may move).

## In progress
- **Quitting during a catch-up** (draft pull request #22): a player who quits while their game is catching up no longer leaves the match paused with their seat held; review and checks running; ships as a patch release after v0.9.0
- Bots: contested food, health-aware fights, the safe fire-in and the island play (bombing a crew over to the swimmers, the ferry on Islands and Small) are joined in PR #14, the v0.9.0 release pull request (both independent code reviews answered and every finding fixed; main with v0.8.3 is merged in and the five checks are running again, then the merge)
- Bots next: the flower play (swimmers on Treasure too), the fire-in ring of 8 with more walls when the enemy has no Fire Ant, mines on the enemy's food path, harassment (the owner has answered playbook questions 1, 2, 4, 7, 11)
- Bots: "Can't go there." is mostly the game's own loop for ants shut in by fire walls, not illegal orders. PR #14 (the v0.9.0 candidate) no longer orders special actions onto an ant (-95% on Small); draft PR #16, stacked on it, adds a counter and the can't-go checks of the gate, the rescue and the raids: the bots' own refused orders fall 68% on Small Hard and 35 to 45% on Treasure, scores unchanged within noise (measured before #14's review fixes; #16 is measured again on the merged bot and moves onto main when #14 merges). The loops themselves stay (the owner's decision); a quieter raid rule costs 5% of the score at Medium and is the owner's to choose
- Short room codes, and the platform and operating system icons: planned after the bots (v0.9.0)
- Recordings of finished online matches, with the players' names, to tune the bots (after the bot fixes)
- Your own orders at once in online matches (prediction): smoothing the other players' ants, then on by default
- Replays and watching bots play (1v1v1v1, 1v1, 2v2): designed, the owner approved the pictures
- **No-Docker browser checks** (pull request #13, tools only, no redeploy): main is in and four of its five checks are green; it waits for the Windows test fix (pull request #20) to reach main, then merges
- Host moves a player's colour in the waiting room (tap a player's ant; network protocol 14, ships with v0.10.0 and the short room codes): being built, draft pull request to follow
- **Prediction-budget tests (the Windows flake, PR #20):** reviewed, no defect; main with v0.8.3 is merged in and the five checks are running again; merges right after v0.9.0.

## On hold (not started; the owner decides when)
- Bots, later steps: the opening trips on a few community maps, tuning and a level ladder, an automatic tuner
- The original's stunned ant may be invulnerable: to check in the original program (a rules change)
- Extinguishing a fire under an ant: does the original allow it for a person? (bots do not; a rules change if it does not)
- Server: the server uses all cores; a load test for the VPS; watching other people's matches live
- Dead-code cleanup, trimming the other big documents (the README is done), Docker hardening, match API, an option to match the monitor's aspect

## Recently done
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
