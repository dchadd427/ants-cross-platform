# Status

_Updated 2026-10-02 · current release **v0.0.93** · details: [CHANGELOG](CHANGELOG.md)_

## In progress
- **16:9 by default (priority)**: one screen layout and a 960 x 540 canvas done; the wide match screen (more map, frame stretched from the original art) and the web page in 16:9 being built; classic 4:3 stays selectable
- **v0.0.94 Less lag**: ping and delay next to the FPS counter, 50 ms turns, an adaptive buffer, a lagging player no longer freezes the others · fixing review findings
- **v0.0.95 Community maps**: default ant types (popcorn.lvl hatches combat ants), power-ups by tile, busy ants and the pick-up panel as in the original · ready, ships after v0.0.94
- **v0.0.96 Web**: the game starts at once (no start overlay) and keeps playing in a hidden tab · ready, ships after v0.0.95
- **Desktop start menu**: Single player with bots per seat, Join with a code, Host an online match · in review
- **Reconnect** (release A, the server side, off by default): a lost player's seat is held, the match pauses, vote after 30 s · in review
- **Online rooms**: bots fill the empty seats at START, chat in the waiting room · network side being built

## Next
- The screens for the online bots and the waiting-room chat (host choice, chat box)
- Reconnect release B: the games rejoin by themselves (also after a crash or power cut), "Rejoin" in the menu and on the page; then on by default
- Matches survive a server restart (rooms restored from their turn log)
- Mouse-wheel zoom; later an option to match the monitor's aspect
- Touch: tap fixes, two-finger pan, pinch zoom
- Bots B4a / B4b (power-ups incl. standing on them, raids, defence, fights)
- Dead-code cleanup, CI, Docker hardening, short room codes, replays, match API

## Recently done
- **v0.0.93** the room leader can start the match early; flooding clients are dropped; web files revalidated (304)
- **v0.0.92** pointer fix + fullscreen mouse grab (black bars scroll the map), the widescreen safety net, GCC 12 builds, bots' review fixes, four-corner layout
- **History rewrite**: removed files and local paths gone from every public commit
- **v0.0.91** the original program is out of the repo; table tests keep full strength by pinned digests
- **v0.0.90** Play online: host (map, 2–4 players), join by code, seats on the page / in windows / by link
- **v0.0.89** server makes its own control secret; one Portainer stack; "Play online" link
