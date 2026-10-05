# Status

_Updated 2026-10-05 16:36 PDT · current release **v0.8.1** · details: [CHANGELOG](CHANGELOG.md)_

## Release schedule (the next releases in order; targets in Pacific time)
| Release | Progress | Target | Now |
|---|---|---|---|
| **v0.8.2** rounder, less flat buttons (8 px corners, a bevel on every edge) | ███░░░░░░░ 29% | Mon | built and tested here after the owner's two notes; the browser checks and the review are running again, then the pull request |
| **v0.8.3** a game for one on this computer, Friend as the default seat, a dirtier orange background | █░░░░░░░░░ 14% | Mon or Tue | built here; waits for v0.8.2 to merge, then its own checks and review |
| **v0.9.0** the bots: contested food, fights, safe fire-in, island and swimmer play | ████░░░░░░ 43% | Mon or Tue | PR #14 (both batches joined, main v0.8.0 in) has all five checks green; two independent code reviews are running, then the release steps and the merge |
| **v0.10.0** short room codes (no "demo") and platform / operating system icons (protocol 14) | ░░░░░░░░░░ 0% | Tue or later | planned after the bots (v0.9.0) |

A release has seven steps, each a seventh of its bar: built, tested here, reviewed, review fixes done, checks green on all five platforms, merged, live. After these: the bots' "Can't go there." fix and flower play, the fire-in ring of 8, mines on the enemy's food path, recordings and replays (version numbers may move).

## In progress
- v0.8.2: rounder buttons (8 px corners) with a bevel on every edge and soft top-to-bottom shading, so they look less flat (the owner's asks); a patch release, built, with its review and browser checks running again
- v0.8.3: START with every other seat on Nobody begins a game for one on this computer (the original's single player; a room on the game server needs two people), Friend is the default seat of the New match card, and the orange background gets a little noise and dirt (the owner's asks); a patch release right after v0.8.2, built here
- Bots: contested food, health-aware fights, the safe fire-in and the island play (bombing a crew over to the swimmers, the ferry on Islands and Small) are joined in draft PR #14, the v0.9.0 candidate (all five checks green with main v0.8.0 in; two independent code reviews running, then the release steps)
- Bots next: the flower play (swimmers on Treasure too), the fire-in ring of 8 with more walls when the enemy has no Fire Ant, mines on the enemy's food path, harassment (the owner has answered playbook questions 1, 2, 4, 7, 11)
- Bots: "Can't go there." is mostly the game's own loop for ants shut in by fire walls, not illegal orders. PR #14 (the v0.9.0 candidate) no longer orders special actions onto an ant (-95% on Small); draft PR #16, stacked on it, adds a counter and the can't-go checks of the gate, the rescue and the raids: the bots' own refused orders fall 68% on Small Hard and 35 to 45% on Treasure, scores unchanged within noise. The loops themselves stay (the owner's decision); a quieter raid rule costs 5% of the score at Medium and is the owner's to choose
- Short room codes, and the platform and operating system icons: planned after the bots (v0.9.0)
- Recordings of finished online matches, with the players' names, to tune the bots (after the bot fixes)
- Your own orders at once in online matches (prediction): smoothing the other players' ants, then on by default
- Replays and watching bots play (1v1v1v1, 1v1, 2v2): designed, the owner approved the pictures
- Browser checks without Docker (draft pull request #13): a session that has no Docker can build the web pages and run the touch checks and the other browser checks; it changes no page (no redeploy), has main v0.8.0 in and merges once its five checks are green
- Host moves a player's colour in the waiting room (tap a player's ant; network protocol 14, ships with v0.10.0 and the short room codes): being built, draft pull request to follow
- **Windows prediction-budget test flake** (draft pull request #20): diagnostic first, then the fix; merges once its five checks are green and it has been reviewed.

## On hold (not started; the owner decides when)
- Bots, later steps: the opening trips on a few community maps, tuning and a level ladder, an automatic tuner
- The original's stunned ant may be invulnerable: to check in the original program (a rules change)
- Extinguishing a fire under an ant: does the original allow it for a person? (bots do not; a rules change if it does not)
- Server: the server uses all cores; a load test for the VPS; watching other people's matches live
- Dead-code cleanup, trimming the other big documents (the README is in progress), Docker hardening, match API, an option to match the monitor's aspect

## Recently done
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
