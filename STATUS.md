# Status

_Updated 2026-10-03 16:37 PDT · current release **v0.1.3** · details: [CHANGELOG](CHANGELOG.md)_

## In progress
- v0.2.0 (finishing): the match clock waits behind the "Get ready to play!" dialog (network protocol 12), every screen in 16:9 (loading, quick help, results, start menu), fullscreen panning into the black bars and a locked mouse (no Dock), your own name on the web page
- Smarter bots (v0.3.0): power-ups first (Fire, then Bomber, then Thief), thief-hole walls at every level, contested food, teaming up with you, raids, guiding at the hill (Hard), personalities, Hard really aggressive; tuned on Treasure
- Your own orders show the instant you click in online matches (prediction of your own orders)
- Matches that survive a server restart (high priority): the server saves and restores running matches, then the games rejoin by themselves
- Replays and watching bots play (1v1v1v1, 1v1, 2v2): designed, the owner approved the pictures
- Next small release: in a browser window the map keeps scrolling when the mouse is just past the game's edge

## On hold (not started; the owner decides when)
- Bots, later steps: the island hop (Islands) and the swimmer ferry (Small), standing on power-ups (Hard), flower drops, tuning and a level ladder, bots in the web single-player game
- Server: the server uses all cores; a load test for the VPS; watching other people's matches live
- Touch: tap fixes, two-finger pan, pinch zoom
- Dead-code cleanup, trimming the biggest documents, Docker hardening, short room codes, match API, an option to match the monitor's aspect

## Recently done
- **v0.1.3** deploys can wait for an idle server (the game server's `/busy`; switched on by the site owner), a staging copy of the site, tests in parallel, a release in one command
- **v0.1.2** Treasure is the default map everywhere (setup screen, Host panel, Play online, online rooms without a map); the web pages open in 16:9 again (an old remembered Classic 4:3 is forgotten once)
- **v0.1.1** the bots wait for the "Get ready to play!" dialog: their first orders come after it closes, one by one at the speed of their level; the build id on the web page and in `--version`; a short changelog (the old one is `docs/CHANGELOG_ARCHIVE.md`); faster checks for every change (`./run_tests.sh --fast`)
- **v0.1.0** online rooms with bots and chat, team chat only to allies, mouse-wheel zoom (0.5 / 1 / 2; no zoom-out in a network match), the web game 16:9 on every device; automatic builds and tests on Windows, macOS and Linux for every push (GitHub Actions); network protocol 11 (a v0.0.99 game cannot join)
- **v0.0.99** the game is 16:9 by default: more map, a frame grown from the original's art, the web page in 16:9 (exact pointer, load failures shown), a wide setup screen with a map preview; classic 4:3 stays selectable (`--aspect 4:3`, `?aspect=4:3`)
