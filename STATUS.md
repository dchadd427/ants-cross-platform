# Status

_Updated 2026-10-03 12:29 PDT · current release **v0.1.1** · details: [CHANGELOG](CHANGELOG.md)_

## In progress
- Smarter bots, step 1 (v0.2.0): the standard bot fights back, walls up its thief hole with three fire walls (every level), secures the power-ups on its side, undoes walls and bombs, takes contested food first, accepts your alliance and helps its teammate, and raids (Medium, Hard); built and tuned on Treasure first
- Treasure becomes the default map everywhere (the next small release)

## On hold (not started; the owner decides when)
- Bots, later steps: the island hop (Islands) and the swimmer ferry (Small), standing on power-ups (Hard), flower drops, tuning and a level ladder, bots in the web single-player game
- Reconnect part B: the games rejoin by themselves (also after a crash or power cut), Rejoin buttons, the resume countdown on screen; then on by default
- Rollback for your own orders: your ants move the instant you click
- Server: matches survive a restart; the server uses all cores; a load test for the VPS
- Touch: tap fixes, two-finger pan, pinch zoom
- Dead-code cleanup, Docker hardening, short room codes, replays, match API, an option to match the monitor's aspect

## Recently done
- **v0.1.1** the bots wait for the "Get ready to play!" dialog: their first orders come after it closes, one by one at the speed of their level; the build id on the web page and in `--version`; a short changelog (the old one is `docs/CHANGELOG_ARCHIVE.md`); faster checks for every change (`./run_tests.sh --fast`)
- **v0.1.0** online rooms with bots and chat, team chat only to allies, mouse-wheel zoom (0.5 / 1 / 2; no zoom-out in a network match), the web game 16:9 on every device; automatic builds and tests on Windows, macOS and Linux for every push (GitHub Actions); network protocol 11 (a v0.0.99 game cannot join)
- **v0.0.99** the game is 16:9 by default: more map, a frame grown from the original's art, the web page in 16:9 (exact pointer, load failures shown), a wide setup screen with a map preview; classic 4:3 stays selectable (`--aspect 4:3`, `?aspect=4:3`)
- **v0.0.98** reconnect part A: a server can hold a lost player's seat (match paused, vote after 30 s, 10 s countdown before it goes on); off by default, the games do not rejoin yet; network protocol 10 (a v0.0.97 game cannot join)
- **v0.0.97** the desktop start menu: Single player with bots per seat, Join with a code, Host an online match (the native game only; the web game and every original screen unchanged)
