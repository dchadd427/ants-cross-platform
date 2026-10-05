# Status

_Updated 2026-10-05 05:08 PDT · current release **v0.6.0** · details: [CHANGELOG](CHANGELOG.md)_

## In progress
- One start card for every game, all online: your colour, a friend or a bot level for each other colour, invite links in the card; with online bot games and teams (network protocol 13): built and checked in a real browser, ships after v0.6.0 with the short room codes
- Short room codes: 8 random letters and numbers instead of the long code, a mistyped code says "no such match", and the word "demo" is gone from the codes, the server settings and the docs (planned: starts when the one card and the reconnect fixes are merged; same release, v0.7.0)
- Bots, stage 1: contested food first, finishing wounded ants (a bot sees every ant's health), fighting when behind, the endgame, the fire play (no lone fire ant, killing the fire ant)
- Bots: getting off the island (Islands: bomb over, swimmers, bridges; Small: the swimmer ferry)
- Bots: the power-up playbook is written (the owner's review is next); the flower play comes first (Small, Medium, Gauntlet; Islands with the island work), then the other tactics (fire a base in, a combat skirmish, fire ant with thief, bombs along the base's edges)
- Bots: the "Can't go there." noise is measured: mostly the game's own loop for ants shut in by fire walls (rings, trapped thieves), not illegal orders; the pointless orders get fixed after the first bot batch, a quieter display is proposed
- Recordings of finished online matches, with the players' names, to tune the bots (after the bot fixes)
- Touch controls on a phone: two-finger pan and pinch zoom, hold for a right click (started; its own release)
- Your own orders at once in online matches (prediction): smoothing the other players' ants, then on by default
- Replays and watching bots play (1v1v1v1, 1v1, 2v2): designed, the owner approved the pictures

## On hold (not started; the owner decides when)
- Bots, later steps: the opening trips on a few community maps, tuning and a level ladder, an automatic tuner
- The original's stunned ant may be invulnerable: to check in the original program (a rules change)
- Extinguishing a fire under an ant: does the original allow it for a person? (bots do not; a rules change if it does not)
- Server: the server uses all cores; a load test for the VPS; watching other people's matches live
- Dead-code cleanup, trimming the biggest documents, Docker hardening, match API, an option to match the monitor's aspect

## Recently done
- **v0.6.0** an online match waits for you: reload, a lost network or a sleeping phone holds your seat and pauses the match, the front page offers "Rejoin your match", leaving on purpose is immediate, and matches survive a server restart (network protocol 12)
- **v0.5.1** every page in the front page's look: the game page, the changelog pages and "Sprites and sounds" (with fixes for phones); a way back to the front page from the page that a shared link opens
- **v0.5.0** a new front page in the original game's style (two cards, one-click bot levels, "How it works"); game statistics on it: matches being played, players online, games played
- **v0.4.0** many zoom levels, down to the whole map, in online matches too; a level for each bot and teams before a game against bots; your name in single player on the desktop; your own orders show at once in online matches (opt-in: `?prediction=on`); the start scripts open one game with the menu
- **v0.3.0** computer players that gather food, raid and fight back (Easy, Medium, Hard, four styles; a stuck bot backs off); the front page is the lobby, with single player against bots in the same tab; the map keeps scrolling just past the game's edge in a browser window; the ants behind the start dialog again; restart records on the server (off by default)
- **v0.2.0** the match clock waits for the "Get ready to play!" dialog (network protocol 12: a v0.1.x game cannot join); the loading screen, quick help, results and start menu are composed for 16:9; fullscreen: the black bars count as the picture's edge, the pointer lock on the web page, the macOS Dock; the windows (the start scripts' four too) open in 16:9 and `start_game.sh` always builds first; your name on the Play online page, asked first for a link that somebody sent you
- **v0.1.3** deploys can wait for an idle server (the game server's `/busy`; switched on by the site owner), a staging copy of the site, tests in parallel, a release in one command
