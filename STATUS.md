# Status

_Updated 2026-10-02 13:09 PDT · current release **v0.0.98** · details: [CHANGELOG](CHANGELOG.md)_

## In progress
- **16:9 by default (priority)**: the wide match screen is built (more map, frame grown from the original art) · in review; the web page in 16:9 being built; score boxes spread evenly; classic 4:3 stays selectable
- **Online rooms**: bots fill the empty seats at START, chat in the waiting room, team chat only to allies · network side done, screens next

## Next
- The screens for the online bots and the waiting-room chat (host choice, chat box)
- Reconnect part A (server side) done, off by default; part B (the games rejoin, Rejoin buttons) next
- Matches survive a server restart (rooms restored from their turn log)
- The server uses all cores (worker threads per room) and a load test to run on the VPS
- A wider setup screen in 16:9 (map preview, waiting-room chat) · mock-ups being made
- Mouse-wheel zoom; later an option to match the monitor's aspect
- Touch: tap fixes, two-finger pan, pinch zoom
- Bots B4a / B4b (power-ups incl. standing on them, raids, defence, fights)
- Dead-code cleanup, CI, Docker hardening, short room codes, replays, match API

## Recently done
- **v0.0.98** reconnect part A: a server can hold a lost player's seat (match paused, vote after 30 s, 10 s countdown before it goes on); off by default, the games do not rejoin yet; network protocol 10 (a v0.0.97 game cannot join)
- **v0.0.97** the desktop start menu: Single player with bots per seat, Join with a code, Host an online match (the native game only; the web game and every original screen unchanged)
- **v0.0.96** the web game starts at once (no start overlay) and a network match keeps playing in a hidden or minimised tab (no lag notice, no drop, no sound there)
- **v0.0.95** community maps play as in the original: default ant types (combat ants hatch on popcorn.lvl), power-ups by tile, busy ants get the move cursor, a pick-up rebuilds the panel; network protocol 9 (a v0.0.94 game cannot join)
- **v0.0.94** less lag: ping and delay next to the FPS counter, 50 ms turns, an adaptive buffer; a lagging player no longer freezes the others (and is told); the stuck "waiting" message and the last turns of a match fixed
- **v0.0.93** the room leader can start the match early; flooding clients are dropped; web files revalidated (304)
