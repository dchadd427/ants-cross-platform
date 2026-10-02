# Status

_Updated 2026-10-01 · current release **v0.0.89** · details: [CHANGELOG](CHANGELOG.md)_

## In progress
- **v0.0.90 Play online**: host (map, 2–4 players), join by code, seats on the page, in windows or by link; desktop and web players in one match verified · in review
- **Pointer fix**: the black bars of fullscreen scroll the map; fullscreen (incl. the macOS green button) keeps the mouse · fixing 9 review findings
- **Original program out of the repo** (Ants.exe, its decompilation, the DirectDraw wrapper); tests keep full strength
- **Widescreen M0**: drawing safety net before the 16:9 work

## Next
- Public history rewrite: purge the removed files from every commit
- Widescreen: 16:9 default, custom aspect, wheel zoom, stretched HUD
- Touch: tap fixes, two-finger pan, pinch zoom (owner tests on a phone)
- Bots B4–B6: island hops, standard bot, bots in server rooms
- CI (build + test on every push), Docker hardening
- Short room codes, in-game host / join, replays, match API
- Artwork phase-out: replace the original art with open material

## Recently done
- **v0.0.89** server makes its own control secret; one Portainer stack; "Play online" link
- **License**: MIT for the code and docs; the original game is not covered
- **v0.0.88** bots B3 (worker bot); engine fix: a carrier keeps its food source
- **v0.0.87** bots B2: bot view, map analysis, arena
- **v0.0.86** map loader matches the original; repo cleanup
