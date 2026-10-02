# Status

_Updated 2026-10-01 · current release **v0.0.91** · details: [CHANGELOG](CHANGELOG.md)_

## In progress
- **v0.0.92** pointer fix (black bars scroll, fullscreen incl. the macOS green button keeps the mouse) + widescreen M0 safety net · final checks
- **Public history rewrite**: purge the removed files from every commit · being prepared
- **Room leader START**: the first player in a server room can start with fewer players (v0.0.93)
- **Reconnect**: pause, rejoin the same seat, vote after 30 s to continue without the player · design
- **Bots**: B2 review fixes being brought up to date; power-up facts (standing on them, walking around them) researched for B4

## Next
- Widescreen: 16:9 default, custom aspect, wheel zoom, stretched HUD
- Touch: tap fixes, two-finger pan, pinch zoom (owner tests on a phone)
- Bots B4–B6: island hops, standard bot, bots in server rooms
- CI (build + test on every push), Docker hardening
- Short room codes, in-game host / join, replays, match API

## Recently done
- **v0.0.91** the original program (Ants.exe, its decompilation, the DirectDraw wrapper) is out of the repo; the table tests keep full strength by pinned digests
- **v0.0.90** Play online: host (map, 2–4 players), join by code, seats on the page / in windows / by link; friends get 10 minutes to join
- **v0.0.89** server makes its own control secret; one Portainer stack; "Play online" link
- **License**: MIT for the code and docs; the original game is not covered
- **v0.0.88** bots B3 (worker bot); engine fix: a carrier keeps its food source
- **v0.0.87** bots B2: bot view, map analysis, arena
