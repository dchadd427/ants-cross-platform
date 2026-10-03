# Status

_Updated 2026-10-03 16:46 PDT · current release **v0.2.0** · details: [CHANGELOG](CHANGELOG.md)_

## In progress
- Smarter bots (v0.3.0): the standard bot opens with power-ups (Fire, then Bomber, then Thief), walls up its thief hole, undoes walls and bombs, takes contested food first by value (Treasure's centre first), teams up with you and helps its teammate, raids the leader, hatches for fights; built and tuned on Treasure first
- Your own orders show the instant you click in online matches (prediction of your own orders)
- Matches that survive a server restart (high priority)
- Replays and watching bots (design)
- Faster development: GitHub as the one full check, deploys from GitHub after the tests, a staging site, parallel tests, a release tool

## On hold (not started; the owner decides when)
- Bots, later steps: the island hop (Islands) and the swimmer ferry (Small), standing on power-ups (Hard), flower drops, tuning and a level ladder, bots in the web single-player game
- Reconnect part B: the games rejoin by themselves (also after a crash or power cut), Rejoin buttons, the resume countdown on screen; then on by default
- Server: the server uses all cores; a load test for the VPS
- Touch: tap fixes, two-finger pan, pinch zoom
- Dead-code cleanup, Docker hardening, short room codes, match API, an option to match the monitor's aspect

## Recently done
- **v0.2.0** the match clock waits for the "Get ready to play!" dialog (network protocol 12: a v0.1.x game cannot join); the loading screen, quick help, results and start menu are composed for 16:9; fullscreen: the black bars count as the picture's edge, the pointer lock on the web page, the macOS Dock; the windows (the start scripts' four too) open in 16:9 and `start_game.sh` always builds first; your name on the Play online page, asked first for a link that somebody sent you
- **v0.1.2** Treasure is the default map everywhere (setup screen, Host panel, Play online, online rooms without a map); the web pages open in 16:9 again (an old remembered Classic 4:3 is forgotten once)
- **v0.1.1** the bots wait for the "Get ready to play!" dialog: their first orders come after it closes, one by one at the speed of their level; the build id on the web page and in `--version`; a short changelog (the old one is `docs/CHANGELOG_ARCHIVE.md`); faster checks for every change (`./run_tests.sh --fast`)
- **v0.1.0** online rooms with bots and chat, team chat only to allies, mouse-wheel zoom (0.5 / 1 / 2; no zoom-out in a network match), the web game 16:9 on every device; automatic builds and tests on Windows, macOS and Linux for every push (GitHub Actions); network protocol 11 (a v0.0.99 game cannot join)
- **v0.0.99** the game is 16:9 by default: more map, a frame grown from the original's art, the web page in 16:9 (exact pointer, load failures shown), a wide setup screen with a map preview; classic 4:3 stays selectable (`--aspect 4:3`, `?aspect=4:3`)
