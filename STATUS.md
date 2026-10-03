# Status

_Updated 2026-10-03 00:30 PDT · current release **v0.1.0** · details: [CHANGELOG](CHANGELOG.md)_

## In progress
- Workflow streamlining (one version source, short changelog, quick tests): next, approved

## On hold (not started; the owner decides when)
- Reconnect part B: the games rejoin by themselves (also after a crash or power cut), Rejoin buttons, the resume countdown on screen; then on by default
- Rollback for your own orders: your ants move the instant you click
- Server: matches survive a restart; the server uses all cores; a load test for the VPS
- Touch: tap fixes, two-finger pan, pinch zoom
- Bots B4a / B4b (power-ups incl. standing on them, raids, defence, fights)
- Dead-code cleanup, Docker hardening, short room codes, replays, match API, an option to match the monitor's aspect

## Recently done
- **v0.1.0** online rooms with bots and chat, team chat only to allies, mouse-wheel zoom (0.5 / 1 / 2; no zoom-out in a network match), the web game 16:9 on every device; automatic builds and tests on Windows, macOS and Linux for every push (GitHub Actions); network protocol 11 (a v0.0.99 game cannot join)
- **v0.0.99** the game is 16:9 by default: more map, a frame grown from the original's art, the web page in 16:9 (exact pointer, load failures shown), a wide setup screen with a map preview; classic 4:3 stays selectable (`--aspect 4:3`, `?aspect=4:3`)
- **v0.0.98** reconnect part A: a server can hold a lost player's seat (match paused, vote after 30 s, 10 s countdown before it goes on); off by default, the games do not rejoin yet; network protocol 10 (a v0.0.97 game cannot join)
- **v0.0.97** the desktop start menu: Single player with bots per seat, Join with a code, Host an online match (the native game only; the web game and every original screen unchanged)
- **v0.0.96** the web game starts at once (no start overlay) and a network match keeps playing in a hidden or minimised tab (no lag notice, no drop, no sound there)
- **v0.0.95** community maps play as in the original: default ant types (combat ants hatch on popcorn.lvl), power-ups by tile, busy ants get the move cursor, a pick-up rebuilds the panel; network protocol 9 (a v0.0.94 game cannot join)
