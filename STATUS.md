# Status

_Updated 2026-10-03 13:32 PDT · current release **v0.1.2** · details: [CHANGELOG](CHANGELOG.md)_

## In progress
- Smarter bots (v0.3.0): the standard bot opens with power-ups (Fire, then Bomber, then Thief), walls up its thief hole, undoes walls and bombs, takes contested food first by value (Treasure's centre first), teams up with you and helps its teammate, raids the leader, hatches for fights; built and tuned on Treasure first
- The match clock waits behind the "Get ready to play!" dialog: all 12 minutes are playable; network protocol 12 (v0.2.0)
- Your own orders show the instant you click in online matches (prediction of your own orders; after the clock change)
- 16:9 for the screens that are still the original's 640 x 480 pages (loading, quick help, results, start menu): mock-ups for the owner first
- Faster development: GitHub as the one full check, deploys from GitHub after the tests, a staging site, parallel tests, a release tool

## On hold (not started; the owner decides when)
- Bots, later steps: the island hop (Islands) and the swimmer ferry (Small), standing on power-ups (Hard), flower drops, tuning and a level ladder, bots in the web single-player game
- Reconnect part B: the games rejoin by themselves (also after a crash or power cut), Rejoin buttons, the resume countdown on screen; then on by default
- Server: matches survive a restart; the server uses all cores; a load test for the VPS
- Touch: tap fixes, two-finger pan, pinch zoom
- Dead-code cleanup, Docker hardening, short room codes, replays, match API, an option to match the monitor's aspect

## Recently done
- **v0.1.2** Treasure is the default map everywhere (setup screen, Host panel, Play online, online rooms without a map); the web pages open in 16:9 again (an old remembered Classic 4:3 is forgotten once)
- **v0.1.1** the bots wait for the "Get ready to play!" dialog: their first orders come after it closes, one by one at the speed of their level; the build id on the web page and in `--version`; a short changelog (the old one is `docs/CHANGELOG_ARCHIVE.md`); faster checks for every change (`./run_tests.sh --fast`)
- **v0.1.0** online rooms with bots and chat, team chat only to allies, mouse-wheel zoom (0.5 / 1 / 2; no zoom-out in a network match), the web game 16:9 on every device; automatic builds and tests on Windows, macOS and Linux for every push (GitHub Actions); network protocol 11 (a v0.0.99 game cannot join)
- **v0.0.99** the game is 16:9 by default: more map, a frame grown from the original's art, the web page in 16:9 (exact pointer, load failures shown), a wide setup screen with a map preview; classic 4:3 stays selectable (`--aspect 4:3`, `?aspect=4:3`)
- **v0.0.98** reconnect part A: a server can hold a lost player's seat (match paused, vote after 30 s, 10 s countdown before it goes on); off by default, the games do not rejoin yet; network protocol 10 (a v0.0.97 game cannot join)
