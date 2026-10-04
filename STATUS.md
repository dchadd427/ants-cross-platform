# Status

_Updated 2026-10-04 02:39 PDT · current release **v0.3.0** · details: [CHANGELOG](CHANGELOG.md)_

## In progress
- Your own orders show the instant you click in online matches (prediction, off by default): the review's fixes; then smoothing the corrections
- Many zoom levels (the zoom-out in online matches too): the last checks
- Matches that survive a server restart, phase 2 (high priority): the games rejoin by themselves (phase 1, the server side, is in v0.3.0)
- Replays and watching bots play (1v1v1v1, 1v1, 2v2): designed, the owner approved the pictures

## On hold (not started; the owner decides when)
- Bots, later steps: the island hop (Islands) and the swimmer ferry (Small), standing on power-ups (Hard), flower drops, the opening trips on a few community maps, tuning and a level ladder
- The original's stunned ant may be invulnerable: to check in the original program (a rules change)
- Server: the server uses all cores; a load test for the VPS; watching other people's matches live
- Touch: tap fixes, two-finger pan, pinch zoom
- Dead-code cleanup, trimming the biggest documents, Docker hardening, short room codes, match API, an option to match the monitor's aspect

## Recently done
- **v0.3.0** computer players that gather food, raid and fight back (Easy, Medium, Hard, four styles; a stuck bot backs off); the front page is the lobby, with single player against bots in the same tab; the map keeps scrolling just past the game's edge in a browser window; the ants behind the start dialog again; restart records on the server (off by default)
- **v0.2.0** the match clock waits for the "Get ready to play!" dialog (network protocol 12: a v0.1.x game cannot join); the loading screen, quick help, results and start menu are composed for 16:9; fullscreen: the black bars count as the picture's edge, the pointer lock on the web page, the macOS Dock; the windows (the start scripts' four too) open in 16:9 and `start_game.sh` always builds first; your name on the Play online page, asked first for a link that somebody sent you
- **v0.1.3** deploys can wait for an idle server (the game server's `/busy`; switched on by the site owner), a staging copy of the site, tests in parallel, a release in one command
- **v0.1.2** Treasure is the default map everywhere (setup screen, Host panel, Play online, online rooms without a map); the web pages open in 16:9 again (an old remembered Classic 4:3 is forgotten once)
- **v0.1.1** the bots wait for the "Get ready to play!" dialog: their first orders come after it closes, one by one at the speed of their level; the build id on the web page and in `--version`; a short changelog (the old one is `docs/CHANGELOG_ARCHIVE.md`); faster checks for every change (`./run_tests.sh --fast`)
