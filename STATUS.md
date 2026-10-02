# Status

_Updated 2026-10-01 · current release **v0.0.92** · details: [CHANGELOG](CHANGELOG.md)_

## In progress
- **Public history rewrite**: purge the removed files from every commit · being prepared
- **Room leader START**: the first player in a server room can start with fewer players (v0.0.93)
- **Reconnect**: pause, rejoin the same seat, vote after 30 s to continue without the player · design
- **Bots**: power-up facts (standing on them, walking around them) researched for B4

## Next
- Widescreen M1: one screen layout, the original's numbers, no visible change
- Widescreen: 16:9 default, custom aspect, wheel zoom, stretched HUD
- Touch: tap fixes, two-finger pan, pinch zoom (owner tests on a phone)
- Bots B4–B6: island hops, standard bot, bots in server rooms
- CI (build + test on every push), Docker hardening
- Short room codes, in-game host / join, replays, match API

## Recently done
- **v0.0.92** pointer fix: the black bars of a wide window scroll the map; fullscreen (incl. the macOS green button) keeps the mouse
- **v0.0.92** widescreen M0: the view fingerprint (307 hashes of the classic picture and pointer) as the safety net for the 16:9 work; GCC 12 builds the whole project with warnings as errors
- **v0.0.91** the original program (Ants.exe, its decompilation, the DirectDraw wrapper) is out of the repo; the table tests keep full strength by pinned digests
- **v0.0.90** Play online: host (map, 2–4 players), join by code, seats on the page / in windows / by link; friends get 10 minutes to join
- **v0.0.89** server makes its own control secret; one Portainer stack; "Play online" link
- **License**: MIT for the code and docs; the original game is not covered
