# Changelog

What changed in each release of the Ants remake, newest first, a few lines each: what a player sees and, only when it changed, the rules or the network protocol. The version is the one line of the file `VERSION` (how it moves: [`docs/WORKFLOW.md`](docs/WORKFLOW.md)); every build also has a build id, the short git commit, shown by `ants --version`, `ants_server --version` and the footer of the web page.

An entry is 5 - 15 lines in a fixed template: the heading `## vX.Y.Z - YYYY-MM-DD - title`, then **For players:** (1 - 6 bullets), **Rules / network:** (only if the rules or the network protocol changed: what, and the protocol number), **Fixes:** (optional, one line each) and **Details:** (a link to the commit range). No test counts and no mutation or review lists: those belong in commit messages and documents. A change that goes live writes its entry under **Next** (a section headed `## Next`, above the newest release) in its own pull request, and `tools/release.py` turns it into the entry when `VERSION` moves (each such change bumps the version, by default the patch number).

<!--
Template of an entry (copy it, keep the labels and the order, leave out a paragraph that has nothing to say):

## vX.Y.Z - YYYY-MM-DD - title

**For players:**
- one bullet: what a player or a server owner sees or can do now

**Rules / network:** only when the rules or the protocol changed: what, and "network protocol N: a vA.B.C game cannot join"

**Fixes:**
- one line each (optional)

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/OLD...NEW)

Work that is not released yet is written in the same template under a heading that says only "## Next" (no version, no date), above the newest release;
`tools/release.py X.Y.Z "title"` turns that heading into "## vX.Y.Z - date - title" and refuses when there is no "## Next" or it is empty.
-->

## v0.12.35 - 2026-10-13 - Computer players can read the fire-wall and bomb gates of a map (analysis only)

**For players:**
- Nothing changes on screen or in play. The computer players' map analysis now also finds what stands between a hill and its food and power-ups when fire walls or bombs are in the way (`POPcOrN`, `Bombz Away`), and which ants get past: a Fire ant, a Bomber that defuses, or a Bomber that bombs itself over a wall. No computer player uses it yet ([the tables](docs/reverse_engineering/bomb_gates.md)).

**Rules / network:** none. No rule, no hash and no protocol number changed.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/1e9d659d...bot-gates-analysis)

## v0.12.33 - 2026-10-13 - Tests for the bomb throw, knockback, fire walls and defuse

**For players:**
- Nothing changes on screen or in play. New tests pin down exactly how bombs, hits, fire walls, defuse and power-up pick-up behave, including every hop over the fire wall on `POPcOrN` ([the table](docs/reverse_engineering/bomb_gates.md)), so that the computer players can later use them.

**Rules / network:** none. No rule, no hash and no protocol number changed.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/3c104ac8...1123e8e3)

## v0.12.32 - 2026-10-12 - Map pictures show the food piles

**For players:**
- The map pictures in the map picker and the lobby now show the food piles. The 534 pictures of the players' maps were drawn before the game's world picture showed every food pile of a map file (v0.12.29), so they still looked like the old, pile-less map; all of them are drawn again by the game itself.
- Nothing else in a picture changes: same size and quality, and a map without piles has exactly the same picture as before.

**Rules / network:** none.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/714d5a35...HEAD)

## v0.12.31 - 2026-10-12 - Names of the computer players: the list, the picker and the label (code only)

**For players:**
- Nothing you can see changes. The game now holds the code for the computer players' coming names (a list of 252 short names, a picker that gives a room different ones, and the label `Pip (bot, Hard)`), but no screen or message uses it yet: bots are still called `Bot (Level)` everywhere.

**Rules / network:** none. Names are display only: the rules, the replay format and network protocol 19 are unchanged.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/d1b887f1...HEAD)

## v0.12.30 - 2026-10-12 - Maps without the full four-seat setup taken out

**For players:**
- 13 more maps of the players are gone from the map list, because every map now has to have a hill and starting ants for all four colours: the nine 1 v 1 maps (`!aLkAtRaZ1v1bOmBeR`, `!aLkAtRaZ1v1tHiEf!`, `!aLkAtRaZ_1v1!`, `ANTS SOCCER`, `ANTSSOCCER`, `GRiM-FAiT`, `ShowDown`, `aLkAtRaZ1v1cOmBaNt`, `aLkAtRaZ1v1cOmBaNtn`), `Fighter Dream 2` and `junk yard` (ants for all four colours, but a hill missing for some) and the two arenas without any hill, `DEATH WAR!!!!!!!!!!!!ARENA 3` and `HELLRASIER ARENA`. 534 maps of the players remain; the lobby's list and the map picker are shorter by the same 13.
- A kept replay (30 days) of a match played on one of these maps can no longer be watched.
- A map of the players is only taken in when all four colours have a hill and starting ants, and the check and the test of the map list now fail if one is added that does not.

**Rules / network:** none. The beta lists the smaller set after its next deploy (the images copy the maps; nothing of them persists).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/973fa3cb...d1b887f1)

## v0.12.29 - 2026-10-12 - Food piles that the map file does not hold are drawn

**For players:**
- A food pile that the map's file does not hold in its picture layer is now drawn where it lies. Before, the view showed nothing there although the pile was in the game (bots and ants could see it and carry it, and the minimap showed it): the heap in the middle of `Bombz Away` and its relatives (the BombzAway maps, `WaTcHoUt BOMBZ!!!`, `Bombzzzzz away!!!`, `AGGRIVATION`), and the piles of more than 400 maps of the players whose file leaves the pile's cell empty (`Trouble`, `TowerPlaza`, `candyland` ...).
- A pile that lies on a bomb of the map file replaces that bomb, as in the original; the view now shows the pile and no bomb there.

**Rules / network:** none. Only the picture changed; the simulation, the replay rules and the network protocol are the same.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/f9a38ef1...cdd857a0)

## v0.12.28 - 2026-10-12 - Back 30 seconds on the replay bar

**For players:**
- **Back 30 seconds:** a new button right of Play / Pause on the replay bar (a double triangle with a small 30) jumps the replay 30 seconds back, never before the start. A match that plays keeps playing, a paused one stays paused, and the speed stays. It works with a mouse, a finger, the keys (Enter and Space on the focused button) and the fullscreen bar under a locked mouse. On a phone 420 px wide or narrower (in fullscreen, up to 520 px) the Picture button moves to the camera's row so that the time stays whole. It is not on a match that is being played (live).

**Rules / network:** none.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/353282ed...28d51043)

## v0.12.27 - 2026-10-12 - Broken maps of the players taken out

**For players:**
- 11 maps of the players that could not be played are gone from the map list: `Home Bound`, `IsLaNd FiRe BoOm` and `Thief craker` (no ants for any colour), `K-P`, `Pulse2`, `Pulse3`, `QuItErS`, `firewar`, `hellzone` and `WAR TIME` (a colour with a hill and no ants), and `hell2` (a map of water: every ant drowns in the first three seconds). 547 maps of the players remain.
- A kept replay of a match on one of these maps can no longer be watched.
- A map of the players is only taken in when every colour it claims has ants, and a test now fails if one is added that does not.

**Rules / network:** none. The beta lists the new set after its next deploy.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/1e01e7f0...HEAD)

## v0.12.26 - 2026-10-12 - A game for one on a map of the players

**For players:**
- You can start a game completely alone on any map of the players, as on the six originals: the line under START now reads "A game for one" instead of asking for a computer player, and START opens the game with only your colony on the map. The map's file is fetched from the site while the game loads.
- If the site no longer has the map, the game page now says so ("not on the server, it may have been taken off") and offers the menu instead of Reload; a failed download still offers Reload. A room whose map the game cannot take is refused with a short line instead of starting Treasure.
- Known broken maps, not changed here: Home Bound, Thief craker, IsLaNd FiRe BoOm (no start marker for seat 0: an empty world alone) and hell2.

**Rules / network:** none.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/343a692f...66edfe93)

## v0.12.24 - 2026-10-12 - Pictures of the players' maps in the match lists

**For players:**
- Watch matches: a match on a map of the players now shows that map's picture in "Live now" and "Earlier matches", as the map list does, instead of an empty dark square. The six original maps keep their pictures.

**Rules / network:** none.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/5a0ac68d...69f88ec3)

## v0.12.23 - 2026-10-12 - The map list and the fullscreen replay mouse: follow-ups

**For players:**
- The map list on the front page: after a search that hides the map you had picked, "Choose map" is off instead of choosing a map you cannot see; the page behind the open list (the header and the notices too) no longer takes a key or a tap.
- Fullscreen replays: letting go of a mouse button over the bar, after pressing it on the picture, now reaches the game, so a drag on the map ends.

**Rules / network:** none.

**Fixes:**
- The front page's early download of a map for the game page gives up after 25 seconds and skips a file that announces more than a megabyte, as the game page's own download does.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/593598fb...01e0fa28)

## v0.12.22 - 2026-10-12 - A map list on the front page

**For players:**
- **A map list on the front page:** "Browse all maps" under the picture opens every map of the game and of the players with its picture, how big it is and how often it was played. Search by name or description, sort by plays, name or size, and switch between a Detail list and Thumbnails (your choice is remembered). The six originals come first, then the most played. Arrows, Enter and Esc work; on a phone the list is a sheet at the bottom.
- The arrows and the drop-down still pick the six originals; if the list cannot be loaded they say so and go on working.
- A game for one cannot use a map of the players yet: add a computer player first.

**Rules / network:** none.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/5a7d610d...11ee3bf6)

## v0.12.21 - 2026-10-12 - Fullscreen replays hold the mouse like the game

**For players:**
- **Fullscreen holds the mouse in a replay and in a live match you watch, as it does in a game.** The cursor stays on the picture and the edges and corners scroll the map; leaving fullscreen gives the mouse back (in Chrome and Edge hold Esc: a short press goes to the game, as in a match). The bar stays clickable: move the cursor onto it (a small arrow shows there) and click, or drag the timeline. "Fullscreen mouse: Free" in a game keeps the plain browser fullscreen here too.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/v0.12.20...v0.12.21); how it works: [`docs/REPLAYS.md`](docs/REPLAYS.md#watching-a-replay)

## v0.12.20 - 2026-10-12 - Crisp zoom levels between 1 and 2

**For players:**
- **The zoom levels between 1 and 2 are drawn with the nearest pixel, like 1 and 2.** Before, those levels were smoothed with a linear filter, which blurred the pixel art; now every level from 1 up stays crisp. Below 1 the picture is averaged as before.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/b54ec65...zoom-nearest)

## v0.12.19 - 2026-10-12 - Comments name the test files where the checks now live

**For players:**
- Nothing changes in the game: comments and documents were corrected so that they name the test files where the checks now live.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/v0.12.18...v0.12.19); comment-only change.

## v0.12.18 - 2026-10-12 - Zooming no longer clicks the sound

**For players:**
- **Zooming no longer makes the sound click.** The sounds are placed by distance from the middle of the picture, so every zoom step with the wheel moves the listener, and the sounds that were playing changed their volume and side in one instant. That click is measured. A faint sound that suddenly got loud may have been heard as played again ("repeat"), but that is only a hypothesis about what the owner heard, not confirmed. The volume and side now glide to the new place in 12 ms. Which sounds start, when and where is unchanged, in a replay and in a game.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/2472b39...zoom-audio-glitch)

## v0.12.16 - 2026-10-12 - Auto camera for replays and live matches

**For players:**
- **Auto camera for replays and live matches.** The picture goes to the busiest place (fights, thefts, fires, bombs) and stays there at least 3 seconds; rings on the minimap show where things happen. **Manual camera** leaves the picture where you put it. Moving the picture by hand (the edge of the screen, the minimap, two fingers) switches to Manual; zooming does not. The button is in the bar (a row of its own on a phone, an icon in fullscreen) and what you chose is remembered on the device.
- **Nothing can be picked in a replay:** no click, drag box, corner marks, Move and Stop buttons, order markers, voices or inspect panel. Matches you play are the same.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/v0.12.15...v0.12.16); how it works: [`docs/REPLAYS.md`](docs/REPLAYS.md#watching-a-replay)

## v0.12.15 - 2026-10-11 - Community maps load in the browser

**For players:**
- **A room can play any community map in the browser:** when the leader picks a map of the community, every browser downloads that one file before the match loads (it is checked to be the host's copy) and the match waits for everybody. The same goes for watching a recording of such a match.
- A map that cannot be had (taken off the site, no connection, too slow) stops that start for the room, with the reason on screen; the leader's next START tries again.

**Rules / network:** none: the simulation and the protocol are unchanged.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/9f65f043...9e408a1b)

## v0.12.14 - 2026-10-11 - The server knows the community maps and lists them

**For players:**
- **The server knows all the community maps** and the lobby can choose any of them, not only the six of the original game.
- **The map list is public:** `GET /maps` lists every map with its size, players, minutes, how often it was played and a preview picture; the previews and the map files are served by the site.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/v0.12.13...v0.12.14)

## v0.12.13 - 2026-10-11 - Chat on a phone

**For players:**
- **Tap the chat box to type on a phone or tablet.** The browser's on-screen keyboard opens; what you type is your chat line, Enter sends it and closes the keyboard (so does Esc, or a tap on the map).
- Nothing looks different, and a computer with a mouse and keyboard plays as before. The keyboard may cover the chat box while you type; a line above the keyboard needs a picture first.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/v0.12.12...v0.12.13); how it works: [`docs/TOUCH.md`](docs/TOUCH.md)

## v0.12.12 - 2026-10-11 - The site statistics live in the database

**For players:**
- **The server keeps the front page's game counts in its database**, with the files of older servers read in on the first start, so no count is lost on a redeploy. The numbers shown are the same.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/v0.12.11...v0.12.12)

## v0.12.11 - 2026-10-11 - The server counts how often each map is played

**For players:**
- **The server counts how often each map is played** (a person took part and the match ran 30 seconds), for a later map list.
- The original maps are counted like the community maps.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/v0.12.10...v0.12.11)

## v0.12.10 - 2026-10-11 - The maps load the way the original loads them

**For players:**
- **Community maps load the way the original loads them.** The game now reads what the map file says (the numbers of its tiles and its solid-ground marks) and no longer guesses from the names the map maker gave the tiles.
- **Power-ups are walkable only where the map says so.** On `POPcOrN` the power-ups no longer plug the corridors (94 maps are like that); on the original maps they stay obstacles that you take by clicking on them.
- **Lakes, mud and dirt that a map leaves unnamed are what they are.** Ants no longer walk across the lakes of Wata fight and hell2, and the mud of the Gant2 maps and the dirt of Turtle World slow them (16 maps).
- **Hills and start ants follow the colour of their tile.** Seven maps lost a hill or swapped two colours because of damaged names (CRISS CROSS and Conquer have four teams now).
- Food piles are told by their tile, a food pile whose next stage has no picture in the map's list vanishes as in the original, and stray objects in a map's list of start positions are ignored and no longer drawn.

**Rules / network:** network protocol 19 (a v0.12.9 game cannot join); replay rules 5 and 6, older recordings stay under "Earlier versions".

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/aec2772a...6bc5cf52); how and why: [the report](docs/reverse_engineering/map_loader_fidelity.md)

## v0.12.9 - 2026-10-11 - The bombs of the community maps work

**For players:**
- **The bombs of 208 community maps work.** The original's bombs, laid out in the map file (`Bombz Away` is a floor of them), were invisible rocks. Now they show in their colours and the first ant that steps on one sets it off (2 hit points, a throw of 4 tiles). Each bomb has an owner team named in the file (mostly the first player): that team's ants and its ally walk around it. Other maps play as before.

**Rules / network:** network protocol 18 (a v0.12.8 game cannot join); replay rules 3 and 4, older recordings stay under "Earlier versions".

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/6400ded7...8e93ca3f)

## v0.12.8 - 2026-10-11 - Spectators choose the picture's shape

**For players:**
- Watching a live match or a replay, you can now pick the picture shape (4:3, 16:10, 16:9, 21:9) from the replay bar.
- The replay bar is as wide as the picture in every shape (a short window keeps it full width).

**Rules / network:** none: the simulation and the protocol are untouched.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/f0eaf2de...6400ded7)

## v0.12.7 - 2026-10-11 - The match server keeps a history of every match

**For players:**
- No change to play or to any screen. The match server now keeps what every online match came to (winner, scores or kills, losses, names) for good, beyond the 30 days a recording is kept; the highest score wins, a tie goes to the kills, equal kills is a draw (in 187: the last side standing, then the kills).
- The list is at `/history` and one match at `/history/<id>`; no page uses them yet.

**Rules / network:** none: the simulation and the protocol are untouched.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/46f7b115...f0eaf2de)

## v0.12.6 - 2026-10-11 - The picture shape is chosen next to the map

**For players:**
- The picture shape (4:3, 16:10, 16:9, 21:9) is chosen next to the map, above START!, instead of at the bottom of the page.

**Rules / network:** none: the simulation and the protocol are untouched.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/0a6a7cf...46f7b115)

## v0.12.5 - 2026-10-11 - The match server keeps a database file

**For players:**
- No change to play. The match server now keeps a database file for records (nothing is stored in it yet).

**Rules / network:** none: the simulation and the protocol are untouched.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/9889d7d...0a6a7cf4)

## v0.12.4 - 2026-10-11 - A link to a missing room says so

**For players:**
- A link to a room that does not exist now says so and offers to start a room of your own, instead of putting you alone in a made-up room.

**Rules / network:** none: the simulation and the protocol are untouched.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/893ef3c...9889d7d4)

## v0.12.3 - 2026-10-11 - Housekeeping: documentation of finished work removed

**For players:**
- No change to play; internal documentation of finished work was removed.

**Rules / network:** none: the simulation and the protocol are untouched.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/166ccc6...d9a8fb0)

## v0.12.2 - 2026-10-10 - Housekeeping: the page of old releases is gone

**For players:**
- **The page of old releases is gone.** The detailed history (every release before v0.0.90, and the long notes of v0.0.90 to v0.1.0) was a second changelog page of the site, `/changelog_archive.html`, with a "Detailed history" button on the changelog page. It is no longer on the site or among the repository's files; the git history keeps it. The changelog page is the only one.
- Nothing else changes in the game or on the site. The rest is housekeeping: the notes in the code say what was reported or decided, not who.

**Rules / network:** none: the simulation and the protocol are untouched.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/288c7de...221b7b3)

## v0.12.1 - 2026-10-10 - The front page is the lobby, matches to watch, computer players that fight (network protocol 17)

**For players:**
- **The front page is now the lobby, and the only way to play.** Open it and your room is ready: a code of six letters and numbers (like `k7m 2xq`), a link to send, the four colours and the map. Players who open the link ask for a name first, take the next free colour and show up at once. The host drags a player onto another colour to move them (a taken colour swaps places; on a phone, tap the player's dots and then the colour), gives a free colour to an Easy, Medium or Hard computer player or to nobody, picks the map, makes teams with three or four players, removes a player (it asks first) and presses START for everybody; everybody can rename themselves with the pencil on their colour. A room lasts while anybody is in it: when the host leaves, the player who has been there longest becomes the host and everybody is told; after a match, Leave Game takes you to the front page, where a new room is ready. **Have a code?** joins a room by its code, and a link to a room that is gone, full or already playing starts a room of your own, with a line that says why. A strip at the top says what happens: the host left, the match did not start, the server has no place, the connection is lost (your colour is kept for a minute), the room was opened in another window, a newer version is out. The old one-card page (Friend seats, Sit here, one invitation for each Friend) is gone, and so are the **More ways to play** button and the testing page behind it, with the old addresses that opened it (`?map=`, `?room=` with its choices, `&play=here`), which show the front page like any other address; links that were shared (`?room=<code>`) and the game's own addresses (`?join=...`) work as before. On a phone, the colour cards are about 11% shorter and the host's a little narrower, with a strip beside them to scroll the page from (a touch that starts on a card's dots still picks that player up, not the page).
- **Four picture shapes, changed while you play.** The buttons under the game (Classic 4:3, 16:10, 16:9, 21:9) switch at once: no reload, no "Leave the match?" question, and a joined match keeps its seat and its room; a wider or taller picture shows more of the map. 16:10 (laptops and 16:10 monitors) and 21:9 (ultrawide monitors) are new (`?aspect=16:10` and `?aspect=21:9` in the browser, `--aspect` on the desktop), and a small "fills your screen" tag stands under the shape nearest to your computer's screen (a computer with a mouse). The front page's Screen selector has all four, shows the shape you picked in a game, and START, a link to join and every game link hand it on to the game (before, a 16:10 or 21:9 showed as 16:9 there). In 16:10 and 21:9 the setup screen, the room, the loading screen, the help and the results are for now the 16:9 page, centred with black around it; the match fills the whole picture.
- **Watch matches, also while they are played.** The server keeps the matches its rooms play that ran 30 seconds or more for 30 days (up to a size limit, the oldest go first); the list is public and shows the names the players typed (no address, room code or chat), and the name screens say so. The front page's header has a **Watch matches** button (on a phone, on the logo's row) and the game page has it in its header after Menu (from a 1140 px window; in More on a phone; in between, the footer link is the way, so that the header stays one row), and everywhere the footer link; it opens a new tab, so a match you are playing goes on. The page lists the matches being played ("Live now") above the earlier ones, with map, time, length and players; **Watch** plays one in the game page with the whole map showing: jump anywhere on the timeline, pause, speed it from 1/2x to 8x, go fullscreen, copy the link or download the file; a match that was left early says "Cut short" and plays as far as it goes. A match being played shows about 10 seconds behind the players, with a red LIVE mark, a timeline that grows and "Jump to live" when you are behind, and becomes a replay of the whole match when it ends. The server plays a match of computer players every hour (a free-for-all or, sometimes, two against two with four players, two against one with three), shown as "Bot (Medium)" or "Bot (Hard)", so there is usually something to watch.
- **Computer players fight, stand their ground and cross the water.** At every level they go for each other's ants once they have nothing left to harvest (Easy later and only with the better odds, Hard soonest); Medium and Hard light fire walls round the gate of their best opponent, a Bomber lays bombs round the food an opponent works and in front of his gate, and every level hunts a Fire or Bomber Ant that comes near its hill. A player who is losing fights harder, the further behind the more (Hard soonest, Easy latest and gentlest): earlier, on worse odds, at the leader's ants first, with its Combat Ants off the food, with bombs at the piles the leader works (Hard) and in front of the gate (Medium and Hard) and, at Medium, fire walls from minute 1.5 instead of minute 4.5; a bot hundreds of points behind sends its typed ants (Combat, Fire, Bomber, Thief) at the leader's ants after the first three minutes, and one that is behind starts mining the enemy gate sooner (it used to wait for the last minutes). They bank about as much food as before against players who do not fight, and the order of the levels is the same. A bot whose gate is fired in by an enemy Fire Ant sends its own Fire Ant at it (the blow knocks it off the fire and the others finish it; before, nobody could reach an ant that stands on a fire wall, and the bot only waited); at Hard, an ant without food that blocks the ramp steps aside for a carrier; the Bomber is no longer held to three bombs (a Hard bot keeps up to ten mines at a pile an enemy works and twelve on the enemy's lanes to its own hill, never on its own carriers' way to the food, a Medium bot eight), so a rush has to walk into them and the other side loses its way to the food. Two of a bot's thieves on one hole no longer jam: the waiting thief steps aside, the first gets home and banks, and the second raids again. On ISLANDS they get their Swimmers across sooner and lose fewer ants: the Bomber flies over with the crew when it is needed, hops no longer wait for orders that cannot come, the team on the far side almost always gets all three Swimmers, and a Hard player takes a Swimmer through the Fire power-ups of a corner row with one ant and exactly timed clicks, not five ants one power-up at a time.
- **The desktop game records its matches.** Every match it plays, against computer players or in a room, is saved in a `replays` folder next to its settings (one that you leave early is kept if you gave an order or it ran a minute). A new tool, `replay_tool`, plays a file back with no window: `verify` says whether the match plays out the same, `orders` lists every order with its time, place and the type of ant that got it. The browser game does not record.

**Rules / network:** network protocol 17, so a game of an earlier release cannot join (reload the page once after the update): a match has a game mode, carried by the room's Start and Room messages, the leader's plan, a server room's `"mode"` and the game's `--game-mode`, and a replay of mode 187 has its own rules number; the original's game plays exactly as before (a switched player's state hash after every tick is the one of a match that was never switched). Recordings now say which simulation they need (`sim_rules`: 1 for the original's game, 2 for game mode 187), so a later protocol change that leaves the rules alone no longer strands them.

**Fixes:**
- The front page's four ants are drawn in the game's own team colours: the red ant no longer has purple legs and a bright rim round its eyes.
- A player can take a seat back up to twelve times a minute (it was three), so a few lost connections or reloads in a row keep you in the match.
- The replay player's REPLAY (or LIVE) tag sits just above the scrub bar (about 25 pixels of height), not over the picture, and in fullscreen above the bar's left end, going with the bar; there a finger on the picture, a tap or a drag of the map, no longer brings the bar up (a tap or swipe up along the bottom edge does, and a swipe down on the bar, or a tap on the small tab on its top edge, hides it at once, also while the match is paused), and the first time the bar goes away after a finger was used, a note says how to bring it back; a mouse works as before.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/e10047d...7017ff3), [view and HUD](docs/VIEW_AND_HUD.md), [play in the browser](docs/PLAY_IN_BROWSER.md), [network port](docs/NETWORK_PORT.md)

## v0.12.0 - 2026-10-08 - The server's lobby rooms (network protocol 16; no page uses them yet)

**For players:**
- **Nothing on screen changes in this release.** It teaches the game server to keep a room open before a match starts, which the lobby page that comes next will use: you will land in a room the moment the front page opens, send one link, and the host will set the colours, the computer players, the teams and the map while everybody waits. No page or menu uses it yet.

**Rules / network:** network protocol 16, so a game of an earlier release cannot join (reload the page once after the update); no rule of the game changes. A lobby page can now ask the server for a lobby room, which holds no match yet: the player who has been in it the longest leads (the next player when the leader goes, never a computer player), and the leader's plan (the map, what each colour is, the teams) and every player's name are shown to everybody. The leader can swap two players' colours, take a player out of the room, and press START, which waits (up to 90 seconds) until every player's game has opened. A colour is kept for a minute after a lost connection, and a room that nobody is in closes a minute later. Three messages are new (the plan, a player's own new name, the removal); the full text is [`docs/NETWORK_PORT.md`](docs/NETWORK_PORT.md#protocol-16-the-lobby-room-the-front-page-waits-in-v0120).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/4a536a9...305ccb9)

## v0.11.0 - 2026-10-08 - Six-character room codes and one link that makes the room (the colour swap is in the game, no page uses it yet)

**For players:**
- **A room's code is six characters that can be said aloud:** `k7m 2xq`, in two groups of three, from an alphabet without `i`, `l`, `o`, `0` and `1`. The front page and the desktop menu make it, show it so (a few lines of the desktop menu, such as its Rejoin button, still show it in one piece for now), and take it typed or pasted as it is shown. The code is only a name: it finds a room that somebody has opened already, while the match's link also makes the room when there is none yet or the last one is over.
- **One link makes the room:** every link of a match carries what the room is (its map, its seats and its teams), so the first player who opens it makes the room and the others walk into it. The desktop menu's Host makes its room the same way; a code typed into its Join only finds a room that is there already. A code that no room has makes none, and a link or bookmark of an earlier release (`demo-small-2p-x7k2`) no longer makes a room: it says that there is no such room.
- **Nobody means nobody in a game for one:** with every other seat on **Nobody**, START! plays on this computer with only your colony on the map. Red, Blue and Black no longer have a hill, ants or eggs there (v0.8.3 gave them all three and left them standing still). The game takes a new option for it, `--alone` ([`docs/COMMAND_LINE.md`](docs/COMMAND_LINE.md)).
- **A Team 1 and a Team 2 switch on each colour:** with three or four players the front page's card has no Teams drop-down menu of sentences any more; every colour that plays has two switches instead. Two colours on the same team start the match allied (with four players the other two colours are the other team, with three the third colour plays alone), and a line under the colours says the plan in words.
- **Leave Game takes you back to the front page:** in the browser, Leave Game (on the results and on the setup screen) and the quit dialog's Yes leave the room and go to the site's front page; a match that you left no longer leaves a dead setup screen behind (it used to show the room you had just left, and START did nothing), and a match of an online room that you leave early is no longer counted among the games on this computer. A game in a frame of another page returns to its setup screen, which starts a new game again. The desktop game is unchanged.
- **Computer players give fewer orders that cannot work, and the Hard ones watch the flowers:** a bot no longer sends an ant with food toward a hill that a ring of fire walls has cut off, no longer sends a thief to a hole that one of its own ants holds shut, and waits when its own ant stands on the way into the hill; a Hard bot also no longer sends the next carrier in while an ant without food is still on the ramp. Hard bots on SMALL have about half as many refused orders ("Can't go there.") as before (2.7 to 1.2 a match and seat), on TREASURE about a third fewer; the Hard bots' click at the gate, which the rule about the ramp changes, was refused 0.71 times a match before that rule and is refused 0.58 times now (over 240 matches of four Hard bots); their scores stay where they were. The Hard bots also watch the flower droppers now (a separate change): they know whose drop a landing is and send an ant to take it, which gets them a Fire Ant, a Bomber or a Thief sooner on MEDIUM, SMALL and GAUNTLET.

**Rules / network:** network protocol 15, so a game of an earlier release cannot join (reload the page once after the update). A player's Hello carries the platform of the game (told, never checked) and, for a join that makes a room, the room's create block; the Room message carries every seat's platform, the room's teams and its rules; the leader's SeatMove carries a 32-bit check of the seating that the room compares with its own seats. **The leader's swap of two players' colours is in the game, but no page or menu uses it yet** (the lobby page will): a room made with the game's `--room-leader-start` lets its leader press a player's row to change places with the next player when every colour is held (never with a bot), and the front page and the desktop menu make no such room, since their rooms start as soon as every colour is held, so there is nothing for a player to try yet. A visitor's room takes only a code with no upper-case letter, so a room that you name by hand through the control interface needs a capital in its code while the server has public rooms (`--demo-rooms`); the codes that the server draws have one. No rule of the simulation changes.

**Fixes:**
- For developers (tests and tools only): the checks of a pull request take about 11 minutes instead of about 14 (the median of the 14 green runs after [#30](https://github.com/dchadd427/ants-cross-platform/pull/30) against the median of the runs before it); the test runner no longer reports a passing suite as failed by mistake ([#29](https://github.com/dchadd427/ants-cross-platform/pull/29)); and tests that failed at random no longer do, on Windows because of how a runner counts a thread's CPU time ([#33](https://github.com/dchadd427/ants-cross-platform/pull/33)) and on a Mac because its loopback can hand the server two messages a pass apart ([#35](https://github.com/dchadd427/ants-cross-platform/pull/35)) or two players' Hellos in either order (the two server tests that connect two players at once now seat the first before the second connects).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/486a39d...141cd3c), merged earlier without a release: [#16](https://github.com/dchadd427/ants-cross-platform/pull/16) (the bots), [#27](https://github.com/dchadd427/ants-cross-platform/pull/27) (Nobody and the Team switches), [#32](https://github.com/dchadd427/ants-cross-platform/pull/32) (Leave Game), [#36](https://github.com/dchadd427/ants-cross-platform/pull/36) (the bots' gate), [#38](https://github.com/dchadd427/ants-cross-platform/pull/38) (the bots and the flowers), [detailed notes](docs/NETWORK_PORT.md#protocol-15-one-link-makes-a-room-short-room-codes-and-the-leaders-swap-v0110)

## v0.10.0 - 2026-10-05 - The leader of a room can move a player to another colour

**For players:**
- **The leader of a room can change a player's colour:** press a player's row in the Players' Status box, and that player moves to the next colour that nobody has (Green, Red, Blue, Black, round again). The player is told in the room's chat ("Ann moved you to Red."), nobody is ever moved out of a colour, and the bots of the leader's plan stay where the match needs them. The leader's own row works too. Only the leader's screen has it (the first player who joined), on the classic page and the 16:9 one; when the first friend is in, the status line says how for five seconds.

**Rules / network:** network protocol 14: one new message (SeatMove: the leader puts a player in an empty colour), so a game of an earlier release cannot join (reload the page once after the update). The server's room status shows `seat_moves` and `ignored_seat_moves`.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/71a8199...e93ee44), [detailed notes](docs/NETWORK_PORT.md#protocol-14-the-leader-of-a-room-moves-a-player-to-another-colour)

## v0.9.1 - 2026-10-05 - A player who quits during a catch-up no longer pauses the match

**For players:**
- **A player who quits during a catch-up no longer leaves the match paused:** the server reads the Leave that came before the reset and drops the seat. Quitting while a lot of the server's messages were still unread (the catch-up a rejoining player gets) made the connection reset, the reset lost the Leave, and the server kept the seat, so the others waited for a player who had gone.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/cf4e71f...4b81a2c)

## v0.9.0 - 2026-10-05 - Computer players that race, hunt and cross the water

**For players:**
- **Smarter computer players:** a bot races an enemy for the food that both can reach, hunts a wounded enemy ant until it is dead, lights fire walls at an enemy gate only where the enemy cannot simply put them out, never clicks a special order onto an ant (far fewer "Can't go there." from the bots' own orders), and plays harder when it is behind.
- **Computer players on ISLANDS and the lake of SMALL:** they fly a crew over the water with bomb flights, bring Swimmers across and ferry the food home. On ISLANDS four bots used to score nothing; they now score about 1,000 (Easy) to 1,400 (Hard) points a seat.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/23dc663...57fa5e4), [the bots](docs/BOTS.md)

## v0.8.3 - 2026-10-05 - Friend by default, a game for one and a dirtier background

**For players:**
- **Friend is the default seat:** a first visit to the front page starts with You at Green and a Friend in each of the other three seats (it was a Medium bot), so START! makes a room and the card shows the invitation links; pick Easy, Medium or Hard for a seat to play against the computer. A browser that already saved its choices keeps them.
- **A game for one:** START! is on with nobody else in the match (it was off: "Pick at least one more seat"). With every other seat on **Nobody** it begins a game for one on this computer: no room, no opponent, and you play Green whatever colour the card showed (the line under START! says so).
- **A dirtier background:** the orange clay behind the pages has a little noise and dirt (patches a little deeper and lighter, grain, pale grit and small brown specks), so it is not so clean. The text on it stays easy to read.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/23dc663...af4e0c1)

## v0.8.2 - 2026-10-05 - Rounder buttons with a bevel on every edge

**For players:**
- **Rounder, less flat buttons:** the teal buttons (START! and the seat choices on the front page, the game page's buttons, the buttons and version plates on the changelog pages, the small buttons of Sprites and sounds) have 8 px rounded corners instead of nearly square ones, and a face lit from above. A bevel runs round all four edges: a bright line and a lit facet on the top and the left, a dark facet on the bottom and the right, so a button looks raised on every side, not only at the top. Their sizes, colours and wording are the same, and the text on them is as easy to read as before.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/638c796...73649a1)

## v0.8.1 - 2026-10-05 - A sharp START! button and 48 demo rooms

**For players:**
- **A sharp START! button:** the front page's START! was the original's small picture (98 x 27 pixels) shown three times larger, so its letters and edges were blocky. It is now a real button in the same teal, drawn by your browser at your screen's own resolution, so it is crisp at any zoom, and smaller: 210 x 58 pixels on a computer (it was 294 x 81), 180 x 54 on a phone.
- **Room for more matches at once:** the public game server makes up to 48 demo rooms at a time (it was 12), so a busy evening is less likely to hear "try again in a few minutes". Each room's turn log is capped at 4 MiB (a busy 30-minute match logs 0.65 MB) so that the demo rooms cannot use up the memory that all the logs share. A stack that sets `ANTS_DEMO_ROOMS` itself keeps its number.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/828ec71...99aa55f), [what a room costs](docs/NETWORK_PORT.md)

## v0.8.0 - 2026-10-05 - One card for every game: friends, bots and teams in one match

**For players:**
- **One card, New match:** the front page's two cards (a game against the computer, and Play online) are one. Pick the map and your colour (**Sit here** moves you), then make every other seat a **Friend** (a person you invite), a bot at **Easy**, **Medium** or **Hard**, or **Nobody**; with three or four players **Teams** pairs two of them. **START!** takes you in: against bots the match begins at once, with Friends it begins when they are in. The card remembers its choices.
- **An invitation for each Friend**, shown in the card with its colour, **Copy link** and, where the browser has it, **Share**. The links carry no name and no key; a friend is asked for a name first, and can go back to the front page from that step.
- **A level for every bot seat, at any colour** (a bot may sit at Green too), and the room's teams: in the desktop game's Host panel as well.
- A game against the computer from the front page is now a room on the game server; an old address with `&players=1` still plays on your computer.
- **Clearer messages in the waiting room:** when a friend's colour was taken the game says which colour you play instead; when the server has no room left for a new match it says so (try again in a few minutes) instead of "no such room"; and a seat that does not play is named by its colour.

**Rules / network:** network protocol 13: a start carries a bot level for each seat and the teams, so a v0.6.x game cannot join (reload the page once after the update). A room's teams are a word of its code (`demo-treasure-4p-t01-k7m2xq`).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/8591392...7947eb2), [the front page](docs/NETWORK_PORT.md), [bot games](docs/BOTS.md)

## v0.7.0 - 2026-10-05 - A phone can pan, zoom and right click

**For players:**
- **A phone can pan, zoom and right click:** a tap is a left click and a drag is a selection box, as before. Hold a finger still on the map for about half a second (a ring closes around it, the phone buzzes) and lift it: that is a right click (a move order, a special power, or "send the selected ants there" on the minimap). Two fingers move the map, and pinching zooms towards them.
- **The page stays out of the way:** no double-tap zoom, no pull to reload, no text selection or menu on a long press, and the sound still unlocks on the first touch.
- Taps and drags on the menus, the dialogs and the minimap work as they did. Two fingers do nothing under a dialog. A touch screen on a desktop or a laptop gets the same controls; a mouse, a trackpad and a pen are unchanged.
- Made for Android Chrome first; the guards for iPhone and iPad are in but have not been tried on a real device.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/b3bc868...12fa1ef), [the touch notes](docs/TOUCH.md)

## v0.6.0 - 2026-10-05 - An online match waits for you

**For players:**
- **An online match waits for you:** if you reload the page, lose the network or your phone falls asleep, your seat is held and the match pauses for everybody ("Ann (Green) lost the connection, waiting 0:06"); when you come back the match goes on where it was. The page keeps a key for three hours and the front page shows **Rejoin your match (CODE)** while it holds one (the desktop game keeps it in a file and rejoins by itself). After 30 seconds the others may vote to go on without you; the pauses of a match are capped at 30 minutes (10 in the rooms the front page makes).
- **Leaving on purpose is immediate:** Menu or the logo, then Yes, drops your seat at once: nobody waits for you. A closed tab or a reload still holds it.
- **Matches survive a restart of the game server:** a running match is replayed from its record, and its players come back with their keys.

**Rules / network:** the network protocol stays 12. The server holds seats by default now (`--no-reconnect` turns it off, a room's `"reconnect": false` too). Operators: on the native TCP door (port 4001) a seat's key travels in clear; close that port or use `--no-reconnect` if that matters (the browser's door is the WebSocket one).

**Fixes:**
- A reload of the game page no longer asks for a name: it takes your seat back (the address carries your seat).
- An online match that nobody comes back to gives its place up (it held a slot for as long as it was paused).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/09961f8...ec4906c), [Network Port](docs/NETWORK_PORT.md)

## v0.5.1 - 2026-10-05 - Every page in the front page's look

**For players:**
- **Every page in the front page's look:** the game page, the changelog pages and "Sprites and sounds" now have the front page's clay, thin green frame, teal buttons and black boxes, the "ants!" logo (a link back to the front page; on the game page it asks first while a match runs, as Menu does) and a Play button. The game page's loading screen shows the logo and a teal bar, and on a phone its header is one row (the logo, Menu, Fullscreen and a More button). Nothing about the game, its keys or its addresses changed; the picture is 3 to 7 percent smaller at common window sizes, because of the frame's room.
- **Phones:** "Sprites and sounds" shows its animation details and its sprites table (they were cut off) and keeps its Play button; every page fits from 320 to 1600 px wide with no sideways scroll.
- **A way back from a shared link:** the name step that a shared room link opens has a "Back to the front page" link.

**Fixes:**
- The two changelog pages link to each other (the short page's links to the detailed history opened GitHub's file view).
- A bullet after a blank line in a changelog was written outside its list.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/60dea01...8511283)

## v0.5.0 - 2026-10-04 - A new front page in the game's own look, with game statistics

**For players:**
- **A new front page in the game's own look:** the 1998 game's menu style (the clay, the green frame, the teal buttons, the original "ants!" logo and START! button, a picture of the chosen map). Two cards: **Play vs the computer** (each opponent's level is a row of one-click buttons; Teams as before) and **Play online** (host a match or join one by its code); the help is behind "How it works". It fits phones and wide screens with no sideways scrolling, and every address, link and remembered choice works as before.
- **Game statistics on the front page:** a line under the welcome banner shows the matches being played and the players online now, and the games played today and in all: online matches of 30 seconds or more, and single-player games (a game in the browser tells the server once that it began: a count, nothing else).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/56c4c5d...2b28516)

## v0.4.0 - 2026-10-04 - Many zoom levels, a level for each bot, teams before the start

**For players:**
- **Many zoom levels:** the mouse wheel steps through four levels to a doubling (2, 1.68, 1.41, 1.19, 1, 0.84, 0.71, 0.59, 0.5 ...) down to the map's own limit, where the map's width or height just fills the view, so you never see past its edge; matches on the network offer the zoom-out too (it was local games only), and with Fog of War on every level hides exactly what zoom 1 hides. 2, 1 and 0.5 are exact pictures, the levels between are smoothed, scrolling moves whole screen pixels and a click picks the tile under the middle of the pointer. `--zoom` and the settings key `zoom` take any number from 0.05 to 2.
- **A level for each bot and teams before the start.** On the front page each bot (Red, Blue, Black) has its own level (None, Easy, Medium, Hard), and with two or more bots a Teams choice (Free for all, or you and one bot against the others); the desktop start menu has the same Teams choice and `--teams 0+1` does it from the command line. The teams are made with the original's own team-up at the first tick. A bot that declines a team-up says why ("Bots team up only while three or more teams play.", "You already have a teammate.").
- **Your name in single player on the desktop:** the start menu's Single player panel has a name field (the same remembered name as Join and Host), and the game is played under it.
- **Your own orders can show at once in a network match** (off by default: add `?prediction=on` to the game's address, or start the desktop game with `--prediction on`): your ants answer a click as in a game on one computer instead of a round trip later; the corner's `delay` shows what a click feels. It switches itself off for a while on a device that is too slow for it, and is off in a hidden tab, in a pause and behind the "Get ready" dialog.
- **Desktop:** `start_game.sh` / `start_game.bat` open one game with the start menu (Single player, Join, Host); the four-window test match is `--players 4`.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/9e6ed37...6744e04), [the bots](docs/BOTS.md)

## v0.3.0 - 2026-10-04 - Computer players that fight; the front page is the lobby

**For players:**
- **Computer players gather food, raid and fight back.** At every level a bot answers a blow on one of its ants (with one, two or three ants at Easy, Medium and Hard), sends a hit carrier home, guards its thief hole with fire walls, puts out your fire walls and defuses your bombs, and keeps its last ants out of fights; Medium and Hard also take their side's power-ups at the start, contest the middle of the map and raid the leader with a Thief.
- **Hard bots guide their carriers at the hill's gate by hand** and steal an unguarded Thief power-up for a second thief, and every bot draws one of four styles for the match (aggressive, economic, raider, defensive; `--bot 2:hard:raider` pins one), so bots do not all play alike. A bot that gets stuck (a refused click, a hill that its carriers cannot reach) backs off instead of repeating the same order.
- **The front page is the lobby:** [beta.playants.org](https://beta.playants.org) opens it, with single player in it. Players 1 plays on this computer in this tab, alone or against Easy, Medium or Hard bots; 2 to 4 host a match as before. The game page's "Play online" button is now "Menu", and every old game link still works (`/four.html` goes to `/`).
- **The map keeps scrolling when the mouse goes a little past the game's edge** in a browser window (about an inch, corners included); farther out, over a button of the page or out of the window, it stops.
- For whoever runs a server: a match in a room that holds seats survives a restart of the game server (restart records, [`docs/NETWORK_PORT.md`](docs/NETWORK_PORT.md) "Restart records"); rooms hold seats only when asked, so nothing changes by default.

**Fixes:**
- Behind the "Get ready to play!" dialog the ants are drawn again, standing, as in the original (since v0.2.0 only their hit-point numbers were).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/888e788...e57833b), [the bots](docs/BOTS.md)

## v0.2.0 - 2026-10-03 - The clock waits for the start dialog; every screen in 16:9; fullscreen mouse

**For players:**
- **The match clock waits for the "Get ready to play!" dialog.** Every match still opens with the original's dialog for 5 s, but the clock and the ants only start when it closes, so all 12 minutes (or the map's own time) are playable. On the network the host seals the first turn 5 s after the match began and every machine closes its dialog when its first turn runs. Computer players do not move while the dialog is up either, and their first orders come one by one at the speed of their level: about 0.5 s into the match at Hard, 1.3 s at Medium and 3.1 s at Easy (the machine that runs the bots is ahead of a remote person's screen by the link's delay and the buffer, 20 - 290 ms measured, not seconds).
- **Every screen is composed for 16:9:** the loading screen, the quick help at the start, the results and the desktop start menu use the whole 960 x 540 canvas, built from the original's own art, instead of a 640 x 480 page centred on clay. Classic 4:3 (`--aspect 4:3`, `?aspect=4:3`) is as it was.
- **Fullscreen mouse.** On a screen that is not 16:9 the black bars count as the picture's edge: push the mouse into a bar and the map scrolls. On the web page the browser holds the mouse inside the game in fullscreen (the setting "Fullscreen mouse: Locked / Free" under the game, remembered), so every edge and corner scrolls and a Mac's Dock and menu bar stay away; the desktop game keeps the Dock and the menu bar hidden in a fullscreen Space of the green button too.
- **The window opens in 16:9 from the first frame:** the desktop window and each of the four windows of `start_game.sh` / `start_game.bat` are created at the shape of the picture (a 4:3 window used to flash up first). `start_game.sh` builds the game every time it starts and stops when the build fails; it used to launch an old binary for ever.
- **Your name on beta.playants.org:** one "Your name" field on the Play online page, shared by Host and Join and remembered in the browser, puts your name into the game instead of a random one. A link that somebody sends you asks for your name first.

**Rules / network:** The rules did not change. Network protocol 12: v0.1.x games cannot join (refused with a version message; from this release the web page's message says "Reload the page to update.").

**Fixes:**
- A command that reaches the host before the first turn is sealed is discarded: a modified client could script an opening of up to 64 orders per seat that ran at the first tick, ahead of every person. Honest clients are not affected.
- The loading screen drew the frame's pieces in the wrong order (145 pixels), and the results' numbers ran together ("20", "4", "10" as "204 10").

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/ddf134f...f29c2f9), [network notes](docs/NETWORK_PORT.md)

## Older versions (v0.1.3 and before)

Every release before v0.2.0, and the long notes of v0.1.3, v0.1.2, v0.1.1, v0.1.0 and v0.0.99 - v0.0.90, are only in the git history.
