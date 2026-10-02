# Status

_Updated 2026-10-01 · current release **v0.0.92** · details: [CHANGELOG](CHANGELOG.md)_

## In progress
- **Room leader START**: the first player in a server room can start with fewer players (v0.0.93)
- **Latency**: a latency readout, a fix for the "waiting" message that stays stuck, and less lag
- **Reconnect**: pause, rejoin the same seat, vote after 30 s to continue without the player · design
- **Bots**: power-up facts (standing on them, walking around them) researched for B4
- **Dead-code survey**: find the code and files that nothing uses any more

## Next
- Widescreen: M1 first (one screen layout, the original's numbers, no visible change), then 16:9 default, custom aspect, wheel zoom, stretched HUD
- Touch: tap fixes, two-finger pan, pinch zoom (owner tests on a phone)
- Bots B4a (island hops, the swimmer ferry) and B4b (the standard bot: power-ups, raids, defence, fights, alliances)
- CI (build + test on every push)
- Docker hardening
- Short room codes, in-game host / join, replays, match API

## Recently done
- **v0.0.92** pointer fix + fullscreen mouse grab (the black bars of a wide window scroll the map), the widescreen safety net (M0: 307 hashes of the classic picture and pointer; GCC 12 builds with warnings as errors), the bots' B2 review fixes, the four-corner layout (the four games lie by colour on the page and in the start scripts)
- **v0.0.91** the original program (Ants.exe, its decompilation, the DirectDraw wrapper) is out of the repo; the table tests keep full strength by pinned digests
- **v0.0.90** Play online: host (map, 2–4 players), join by code, seats on the page / in windows / by link; friends get 10 minutes to join
- **v0.0.89** server makes its own control secret; one Portainer stack; "Play online" link
- **License**: MIT for the code and docs; the original game is not covered
