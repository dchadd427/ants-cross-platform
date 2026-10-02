# Status

_Updated 2026-10-01 · current release **v0.0.92** · details: [CHANGELOG](CHANGELOG.md)_

## In progress
- **v0.0.93 Room leader START**: the first player in a server room can start with fewer players; review fixes (start-request flood, accidental early start); faster page reloads (browser caching) · final checks
- **Latency**: ping and delay readout, fix for the stuck "waiting" message, less lag (adaptive buffer, 50 ms turns), a lagging player no longer freezes the others
- **Web**: the game starts at once (no start overlay); a network match keeps playing in a hidden or minimized tab
- **Community maps**: default ant types (e.g. combat ants when hatching on popcorn.lvl) and power-ups recognised and drawn by tile, as in the original

## Next
- **Desktop start menu**: Single player (with bots per seat), Join with a code, Host an online match; single-player bots on the web too
- **Reconnect**: a lost connection pauses the match, rejoin the same seat even after a crash or power cut, the others vote after 30 s (design done)
- **Dead-code cleanup**: the 41 items the survey found
- Widescreen: M1 (one screen layout, no visible change), then 16:9 default, custom aspect, wheel zoom, stretched HUD
- Touch: tap fixes, two-finger pan, pinch zoom (owner tests on a phone)
- Bots B4a (island hops, swimmer ferry) and B4b (standard bot: power-ups incl. standing on them, raids, defence, fights, alliances), then bots in online rooms
- CI, Docker hardening, short room codes, replays, match API

## Recently done
- **v0.0.92** pointer fix + fullscreen mouse grab (the black bars of a wide window scroll the map), the widescreen safety net (M0), GCC 12 builds with warnings as errors, the bots' B2 review fixes, the four-corner layout
- **History rewrite**: the removed files and local paths are gone from every commit of the public history
- **v0.0.91** the original program (Ants.exe, its decompilation, the DirectDraw wrapper) is out of the repo; the table tests keep full strength by pinned digests
- **v0.0.90** Play online: host (map, 2–4 players), join by code, seats on the page / in windows / by link; friends get 10 minutes to join
- **v0.0.89** server makes its own control secret; one Portainer stack; "Play online" link
- **License**: MIT for the code and docs; the original game is not covered
