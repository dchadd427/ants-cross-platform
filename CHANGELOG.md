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

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/ed3f1214...b2150865)

Work that is not released yet is written in the same template under a heading that says only "## Next" (no version, no date), above the newest release;
`tools/release.py X.Y.Z "title"` turns that heading into "## vX.Y.Z - date - title" and refuses when there is no "## Next" or it is empty.
-->

## v0.13.0 - 2026-10-30 - Home, public rooms, the games list and the chat

**For players:**
- **A new front page: Home and the Room.** Home is a top bar over a picture of the Yard with **Quick host**, **Quick join**, **My room**, **Games** and the Menu; the Room is the room page under a slim bar with **Home** and **Leave room** (it asks first when other people are in the room). Going between the two is instant and keeps your seat, and a plain visit makes no room until you press My room. [Home and the Room](docs/HOME_SCREEN.md)
- **Public rooms.** The host switches the Room **Private** (link only) or **Public** (listed in Open games). Every new room is private; it is private again when another person leads, and a public room that nobody used for 30 minutes goes private by itself (the host gets a one-tap **Make it public**). **Quick host** makes your room public in one press, and makes the room first if you have none.
- **The Games list** shows the open games, best first: host, map, seats, bots and **Join**, with **Join by code** at the bottom. A room is on it only while a newcomer could join. **Quick join** takes the best open game for you, trying up to three, with a **Cancel**; a refused Join costs you nothing, your own room is kept until the new one welcomes you.
- **A global chat** in the lower-left corner of Home (a strip along the bottom on a phone). Everybody can read it; to write you log in. It runs in its own container (`ants-hub`), so the site and the games work while it is off, and it keeps lines for 7 days. [The chat hub](docs/HUB.md)
- **Log in with Discord** to write in the chat. It asks for your Discord id and username only and keeps nothing else; **Log out** and **Forget me** (deletes your lines) are in the chat. A Discord account under 7 days old cannot write.
- **Everybody needs the new version.** A game or page of v0.12 cannot join a v0.13 server: reload the page.

**Rules / network:** network protocol 22 (was 21): one new message, Visibility (type 35: the host makes a lobby room public or private), and one new Room flag, public; a v0.12 game cannot play with a v0.13 one ([public rooms](docs/PUBLIC_ROOMS.md)). One rule fix: the layer-2 id 38 is no bridge any more, as in the original (a water tile that holds it stays water: only a swimmer can enter it, and anyone else put there drowns). No shipped or community map holds it and every state hash of the library is unchanged, so recordings play as before (rules number 5); only a player's own map that holds id 38 plays differently from v0.12. **If you run a server:** the public door (`--replay-port`) answers `GET /lobbies` and the status of a lobby room has `"public"`; the site's nginx passes `/lobbies`, `/hub/ws` and the login's pages; the stack has a new container `ants-hub` and its volume, with the `ANTS_HUB_*` settings, and the chat and the login must not go live behind a reverse proxy before `docker/trusted-proxies.conf` names it ([the chat hub](docs/HUB.md#behind-a-reverse-proxy-not-ready-for-the-public-until-this-is-set)). The hub keeps its data behind one internal interface (no change to see).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/354be26a...559e49df)

## v0.12.152 - 2026-10-29 - Map Maker: a Bridge chip, and a hint for a bridge that is not on water

**For players:**
- **The Map Maker has a Bridge to put on a map.** In Objects, after the Fire wall and before the four bombs, there is a new chip with the game's own picture of a finished bridge. Tap to put one piece, drag for a line of pieces; the name shows under the chip when it is chosen, and Pick up calls it "Bridge".
- **A plain hint when a bridge is not on water.** A bridge needs water under it. While the Bridge chip is chosen, the line under the palette says how many pieces of the map stand on ground that is not water: "Bridge · 2 pieces are on dry ground. A bridge needs water under it." It counts again after every step, Undo and Redo, and when you open a map. It is only a hint: the map still saves, downloads and passes the check. A phone has no such line.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged (the Map Maker page only). Map Maker v1.7.0.

**Fixes:**
- On a desktop window under 741 pixels high (1280 x 720 with scrollbars that take room) the Objects column was too tall for its 31 chips and cut off the name line: there the chips are 2 pixels shorter and the rows and tool buttons 1 pixel closer. Taller windows and phones look as before.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/354be26a...bfbcbda8)

## v0.12.151 - 2026-10-28 - Tests: the map submission checks no longer fail on a slow machine

**For players:**
- **No change for players or server owners.** The game, the server and the map files are as before.
- **A test of the map submissions no longer fails on a slow, busy test machine.** It checks that a player can send a map, find it again and withdraw it. The server limits the time it spends checking maps, and on a loaded machine that time ran out in the middle of the test, so the server answered "busy" where the test expected "accepted". The tests now give that limit all the time they need, and a new test keeps the real "busy" answer covered: a send that takes longer than the limit closes the window, and the next window starts clean.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/308ce544...c7027eb3)

## v0.12.150 - 2026-10-28 - Online rooms: at most 8,000 starting ants

**For players:**
- **Online rooms play at most 8,000 starting ants (all four colors added up).** Before, nothing limited the ants: a map with 40,000 ants passed the checker, and starting its match froze the whole server for about 11 seconds, with every room, the lobby and the health check, because setting the ants up takes time that grows with the square of their number. Now a map over 8,000 is refused before any ant is set up. A map that starts 8,000 ants or fewer, including the largest ones of the players (6,000 and 2,772 ants), plays online as before; none of the library's 540 maps starts more than 248.
- **How you see the refusal.** The lobby tells the leader "This map starts too many ants to play online (8,000 at most): choose another map." and the room keeps its map. The map list marks the map "Too many ants to play online". In the Map Maker, **Play this map > With other players** says so and its button is off, and **Submit for approval** and test rooms get a sentence from the server. The map can still be opened, edited and saved, and played on your own computer or against bots.
- **A match with many ants takes more of the server's places.** A match takes one place for every 10,000 cells of its map or every 400 starting ants, whichever is more: 2,400 ants take 6 places, 6,000 take 15 and 8,000 take 20 of the server's 48. A shipped map still takes one. So a 6,000-ant map now takes 15 of the 48 places (only 3 such matches at once; it took 3 before). Matches on a map above the cap are left out of the match history counts (only a recording made before the cap can be one). When a map needs more places than are free, the START waits as it does on any full server ("The server has no place for another match right now: try again in a minute.").

**Rules / network:** none for the simulation and the network: network protocol 21, the simulation rules, the recordings and the map files are unchanged. One rule of online play is new, and a map that was accepted before can now be refused online: a room no longer plays a map that starts more than 8,000 ants.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/dd307dfd...bb58eaa2)
## v0.12.149 - 2026-10-28 - On a phone the health numbers are bigger and outlined

**For players:**
- **On a phone the health numbers are bigger and outlined.** In the browser, "Show health as: Number" had digits 2 to 3 CSS pixels tall on a phone, too small to read. On a small screen they now grow (about 8 CSS pixels tall or more: three times as big on a phone held upright, twice on its side) and get a thin black outline about 1 CSS pixel wide. The desktop game, and the browser in a normal desktop window, are unchanged: the original's white digits, pixel for pixel.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/ec8fa2fa...9ee59ee3)

## v0.12.148 - 2026-10-28 - Map Maker: the editor starts with less code

**For players:**
- **No change for players.** The Map Maker looks and works as before, and a map you save or download is byte for byte what it was.
- **The editor starts with less code.** Copy, Save as a new map, Start an Extended map / Make a Classic map and Crop to fit now load the first time you use one of them (two small scripts fetched at once, about 29 KB, 9 KB gzipped), so the Map Maker opens with about 7% less script. On a slow network that first dialog may take a moment (an estimate, not measured). If the scripts cannot load (no network), the page says so in plain words and asks again the next time.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged (the Map Maker page only).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/2e45f112...67c5b473)

## v0.12.147 - 2026-10-27 - Map Maker: each start ant faces its own way

**For players:**
- **Map Maker: each start ant now faces its own way.** Before, every starting ant on the map stood facing south. In the game each one starts facing one of seven directions picked at random when a match begins (never north), so the Map Maker now shows each ant facing one of those seven, for every ant type (worker, combat, thief, bomber, swimmer, fire) and team color.
- **It is an example, not what the match will do.** The game picks the facings again for every match, so the map has none; the editor makes one up from each ant's cell (the same every time, so nothing jumps when you put other ants or pan), and the Ants box says so in one line. Nothing is saved in the map: the file is byte for byte what it was.
- The page fetches 36 KB more of pictures when it opens (the extra standing ants), cached after the first visit. Map Maker v1.6.2.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged (the Map Maker page only).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/10bc34ae...6bd2e893)

## v0.12.146 - 2026-10-27 - Map Maker: the game's pictures at every zoom

**For players:**
- **The Map Maker now draws the game's pictures at every zoom.** Before, a map under 10 pixels a cell (a whole 60 x 60 map on a short window or a phone, every 81 x 100 map, any big Extended map zoomed out) turned into flat colored squares. Now the grass, rocks, toys, food and hills stay pictures at any size, up to a whole 512 x 512 map.
- Under 4 pixels a cell, or when more than 12,000 cells are in view, the ground itself is its smooth average color (a tile's texture in 2 or 3 pixels is only a lattice, and a picture for every cell costs time); the objects are still pictures on it. Start ants are small team-colored dots under 12 pixels a cell, a little smaller under 5.
- Speed, measured in a slow test browser with no graphics card (no real phone was tried): a whole 81 x 100 map takes about 100 ms to draw (4 ms before) and a whole 480 x 480 map about 250 to 300 ms (30 before); panning stays cheap. Maps of 40 x 40 or smaller never went under 10 pixels, so they look as before.
- Only the picture on the screen changes: no map, file or game rule is touched. Map Maker v1.6.1.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged (the Map Maker page only).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/305c793f...4f724792)

## v0.12.145 - 2026-10-27 - Map Maker: clearer hills on the small map, and a small map you can hide, move and lock

**For players:**
- **The hills on the small map are easy to see.** Each team's hill is a square with a 1 pixel edge (black, or white for the black team) and a bright fill for green, red and blue (the black team keeps a dark fill), at least 10 pixels wide on any map, so it no longer disappears on dirt, mud or water or on a 480 x 480 map. The starting ants are small squares, left out beside their own hill.
- **Hide it.** A Hide button on the small map takes it away and leaves a small Show map button in its place, so the corner of the map can be reached. A phone's small map is smaller too (108 pixels, no caption).
- **Move it.** Drag the grip (the dots at the top left of the small map) anywhere inside the map, with a mouse or a finger; the arrow keys move it 16 pixels from the grip, Home or a double tap puts it back in the corner. A smaller window brings it back inside.
- **Lock it.** The lock button next to Hide keeps the small map where it is: it cannot be dragged or moved by the keys until it is unlocked. Hide and Show still work. The place, the lock and the hiding are remembered in this browser. Map Maker v1.6.0.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged (the web page only). No map of the library changes.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/bbf956a1...3b60fa2f)

## v0.12.144 - 2026-10-27 - Big selections: the ants nearest the click are ordered first

**For players:**
- **A move or an attack on a big selection orders the ants nearest the clicked tile first.** A selection of more than 32 ants goes out as several commands, and until now only the ants inside each command of 32 were ordered nearest first, so ants standing at the cursor could be ordered after ants from far away that had already taken the free tiles around it, and then find none and stop. The whole selection is now sorted by distance to the clicked tile (as the original's group order does) before it is cut, and ants at the same distance keep their selection order.
- **A click of 32 ants or fewer, a special order (steal, bomb, fire, bridge) and Stop send exactly what they sent before.**
- **The 1,536 ants per click limit stays, and what it leaves out changes:** a move or an attack beyond it now leaves out the farthest ants from the clicked tile (it was the last selected ones); "Only 1536 ants ordered." says it as before.
- Still true: an ant that finds no free tile within four tiles of the clicked one stops, as in the original, and 1,500 ants still get their paths one every 50 ms (the original's rule), so the last of a very big crowd starts later.

**Rules / network:** none; the rules and network protocol 21 are unchanged, and a recording made before plays as before (the change is only in the order of the ants the screen puts into its commands).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/c119406e...3d620f1c)

## v0.12.143 - 2026-10-27 - Big-map warning moves from 300 to 2,400 starting ants

**For players:**
- **The big-map warning now starts at 2,400 starting ants, not 300.** The amber "Many ants: may run slowly" chip on a map in the map picker, and the one warning line in the Map Maker's Play this map dialog (both tabs), now show only when a map starts more than 2,400 ants (the start markers of the four colors added up). A map with more than 2,400 ants still warns, and no map is refused for its ants.
- No map of the library changes: the most that any of the 540 starts is 248, so none was marked before and none is now. Only maps made in the Map Maker (or uploaded) with 301 to 2,400 ants lose the warning.
- Why: in a real headless Chromium (software rendering, no real GPU) a 2,772-ant map ran with a worst tick of 9 to 11 ms (the budget is 50), and the 6,000-ant map now has a worst tick of 14 to 19 ms after v0.12.141 and v0.12.142.
- What that did not cover: no phone and no real graphics card, no match between people (lockstep), and not what a match costs the server. The runs were 60 seconds of passive play plus one mass order.

**Rules / network:** none; the rules and network protocol 21 are unchanged.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/f011f5c1...3970dcf6)

## v0.12.142 - 2026-10-27 - Faster: team-mates' claims on a tile found at once

**For players:**
- **Faster ticks on a huge map.** When many combat ants end a fight in the same instant, each looks for a free goal tile, and each used to look at every ant of the map to see whether a team-mate had already claimed that tile. The game now keeps a list of the ants that claim each tile. Measured here, worst tick in milliseconds before and after, on the real 6,000-ant map (about 1.4 s into the match): 31 and 4.2 on the desktop, 54 and 8.7 in the web build run in Node (not a browser); 4 x 1,500 combat ants on 120 x 120: 11.8 and 5.8. The average tick of that map goes from 3.3 to 2.1 on the desktop.
- Not changed: an order given by command, outside the tick, still looks at every ant (in bot play about half of the questions come that way), and a mass order to thousands of ants still takes seconds in the command phase (probably the path planner, not scans).
- In the web build run in Node (not a browser), no tick of the first 100 seconds of that map went over 7 ms in two runs (the budget is 50).
- Nothing about a match changes: the state is the same as before at every tick that was compared (big worlds and dozens of maps), and the state of all 540 maps of the library is the same at ticks 0, 200 and 2,400; the web build gives the same state as the desktop on the 6,000-ant map and big combat worlds.

**Rules / network:** none; the rules and network protocol 21 are unchanged.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/ea2c4cc5...26810844)

## v0.12.141 - 2026-10-26 - Faster: orders after a fight no longer look at every ant

**For players:**
- **Faster: the one slow moment of a huge map is about half as long.** When many combat ants end their fight in the same instant, each looks for its goal tile again, and each of them used to look at every ant of the map to do it. Measured here, slowest tick in milliseconds before and after, on the real 6,000-ant map (about 1.4 s into the match): 67 and 36 on the desktop, 94 and 56 in the web build (run in Node here, not in a browser); 4 x 1,500 combat ants packed on 120 x 120: 31 and 11.5. The average tick of that map goes from 2.2 to 1.8.
- That tick is still over the web build's 50 ms budget; the next step (a team-mate's claim on a tile is still found by looking at every ant) is a separate change.
- Nothing about a match changes: every tick has the same state as before (all 540 maps of the library play identically on the desktop build, and the web build gives the same state as the desktop on the 6,000-ant map and two big combat worlds).

**Rules / network:** none; the rules and network protocol 21 are unchanged.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/aa8c3591...2e3cea5f)

## v0.12.140 - 2026-10-26 - Map Maker: the bar under the map stays one row, and the tools fit

**For players:**
- **Map Maker: the map no longer jumps when the pointer first touches it, and a whole 60 x 60 map keeps its pictures.** The bar under the map wrapped to a second row as soon as it had words to show, which made the map 40 pixels lower and a whole map under 10 pixels a cell, where the page draws flat colors. The position ("Row 30, column 30", "Whole map") is now a small label in the top left corner of the map, so the bar stays one row.
- On a desktop window under 861 pixels high (a 1280 x 800 screen, a laptop's 1366 x 768 or 1280 x 720) the tools column, with Ground, Objects, Hills or Food chosen, and the side panel with a long description are no longer cut off: the tool buttons are a little shorter and show the short note, the side panel leaves out the kind tags (the top bar shows them) and shows two lines of the description (all of it is in Edit).
- On a phone held sideways the tile button (Gravel and the like) sits beside Pan and shows the picture and the arrow, so it is inside the tools card; the tile's name is in the sheet that it opens. A phone held upright looks as before. Map Maker v1.5.10.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged (the web page only).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/aa8c3591...79aab92d)

## v0.12.139 - 2026-10-26 - Show health as: Number is the original's number

**For players:**
- **Show health as: Number is now the original's number:** white digits in the original's own small fixed font, at the lower right of the ant, as its Ctrl+L prints them. The colored, outlined numbers of v0.12.134 are gone; the numbers are as they were before it, at every zoom and one size (small on a phone, as in the original).
- **Ant health still says which ants** (Hurt ants, All ants, Off; Ctrl+L flips it), and a dying ant shows its 0 as it does in the original. The bar and its three colors are as they were.

**Rules / network:** none: drawing only; the rules, replays and network protocol 21 are unchanged.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/1e36d1f6...89f64530)

## v0.12.138 - 2026-10-25 - Faster: the next ant to move is found at once

**For players:**
- **Faster: a big map plays quicker because the game finds the next ant to move at once.** It used to look at every ant of the map for every step, and that was most of the time of a crowded tick. Measured here, milliseconds a tick before and after: a real map of 6,000 ants fighting 114 and 6.6 (walking 110 and 6.0), a real map of 2,772 ants 13.5 and 1.2, 4 x 1,500 ants on 150 x 150 91 and 0.7 standing, 87 and 2.7 crossing the map spread out, 94 and 2.8 packed, 91 and 2.6 piled up. In the web build the first 10 seconds of the 6,000-ant map cost 8.5 ms a tick (before: 130; the budget is 50), and 99 ticks in 100 take under 22 ms.
- One tick about 1.4 s into that match still takes 100 ms (170 ms in the web build): many combat ants end their reflex fight at the same moment and each looks for its goal tile again. That is the next thing to make faster.
- Games of a few hundred ants are as fast as before or faster (4 x 100 ants crossing the map: 1.9 ms and 1.6 ms a tick).
- Nothing about a match changes: every tick has the same state as before (all 540 maps of the library and worlds of up to 6,000 ants play identically, in the desktop and the web build).

**Rules / network:** none; the rules and network protocol 21 are unchanged.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/437c6f9b...b5001338)

## v0.12.137 - 2026-10-25 - Cleanup: unused code removed

**For players:**
- Cleanup: nothing you can see changes. Unused code is gone and the server's map-submission options are documented.

**Rules / network:** none: the rules, replays and network protocol 21 are unchanged.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/32ad1087...f3847677)

## v0.12.136 - 2026-10-25 - Health numbers at every zoom, and the settings menu scrolls in fullscreen

**For players:**
- **Health numbers now show at every zoom down to 0.5** (the four wheel steps out from 1), where they used to vanish at the third step out and left no mark at all with Show health as: Number. Below 0.5 a ten would be wider than the tile of its ant, so they stay left out there; the bars are as they were.
- **The settings menu scrolls with the mouse wheel in fullscreen,** so the lower rows (Map scroll speed in Video, the end of the Controls list) can be reached there; in a window and with a finger it always did.

**Fixes:**
- The wheel over the settings panel was cancelled by the fullscreen page's guard over the black bars; only the bars (and a Ctrl + wheel) are cancelled now.
- The health numbers' lowest zoom was the bars' second size edge by accident (0.6); the two are separate now.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/cddbaf07...51169738)

## v0.12.135 - 2026-10-25 - Faster: crowds that pile up

**For players:**
- **Faster: a crowd that piles up plays quicker.** The game now keeps a list of the ants on each tile, so a tile with several ants (a pile-up, a fire wall, a bomb) no longer makes it look at every ant of the map. Measured here, milliseconds a tick before and after: 4 x 693 ants stacked on shared tiles and sent across a 90 x 90 map 19.6 and 12.5, 4 x 1,500 on 150 x 150 103 and 81, a real map of 2,772 ants fighting 16.3 and 12.9 (walking 17.4 and 12.5; its worst ticks 108 ms and 24 ms), a map of 6,000 ants fighting 122 and 111.
- Ants that are spread out, one to a tile, are as fast as before (4 x 1,500 across 150 x 150: 84 ms either way). 6,000 ants fighting still cost more than 100 ms a tick (the budget is 50): the biggest cost left is how the game picks the next ant to move, and the next release removes it.
- Nothing about a match changes: every tick has the same state as before (all 540 maps of the library and crowds of 6,000 ants play identically).

**Rules / network:** none; the rules and network protocol 21 are unchanged.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/cddbaf07...a3811168)

## v0.12.134 - 2026-10-25 - Health bars over the ants

**For players:**
- **New: health bars over the ants.** A bar over every hurt ant shows its hit points out of 10: green at 9 or 10, amber at 3 to 8, red at 1 or 2. Full ants have none. On for everybody, for every ant you can see, yours and the enemy's.
- **Choose:** Settings (Esc), Video: **Ant health** (Hurt ants, All ants, Off) and **Show health as** (Bar, Number). A number has the same colors, an outline, and is left out below zoom 0.6. Ctrl+L turns it off and on again. Bars are thicker on a phone held upright.
- **The white numbers on every ant are gone.** To get numbers: All ants and Number. If you had turned the old numbers off, the marks start off too.

**Rules / network:** none: only what is drawn changes; the rules, replays and network protocol 21 are unchanged.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/fe195f16...2abf0410)

## v0.12.133 - 2026-10-25 - Map Maker: the footer line is no longer cut off by the screen's frame

**For players:**
- **The line at the foot of the Map Maker page (the version, the Lobby, Match history, Changelog and Legal links) is whole again.** It sat on the red edge of the page's frame, which hid the lower half of the words; it now has its own room above the edge, on a desktop window of any size and on a phone. The map is as large as before. Map Maker v1.5.9.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged (the web page only).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/736c9486...7f6588ed)

## v0.12.132 - 2026-10-25 - Faster: big maps with thousands of ants

**For players:**
- **Faster: a map with thousands of ants plays quicker.** Two questions that looked at every ant (which ant has this number, and does an ant wait for a path) are now answered at once. A tick with 4 x 693 ants walking across a 90 x 90 map takes 11 ms instead of 26 ms (2,772 ants packed in the corners: 10 ms instead of 38 ms), and 4 x 1,500 ants on 150 x 150 take 47 ms instead of 112 ms (packed: 46 instead of 119); the browser build gains the same (27 to 12 ms and 115 to 49 ms).
- A crowd that only stands and fights is not faster yet (6,000 ants in a brawl cost about 47 ms a tick: still too much for a slow computer); the next releases remove the other scans.
- Nothing about a match changes: every tick has the same state as before (all 540 maps of the library and crowds of 6,000 ants play identically).

**Rules / network:** none; the rules and network protocol 21 are unchanged.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/c03ec4c0...fbb14974)

## v0.12.131 - 2026-10-25 - Big maps now warn: "may run slowly"

**For players:**
- **No map is refused for its ants.** A map that starts more than 300 ants (all colors together: 75 a color on four) only gets a warning, because a game on it may run slowly, most on phones and older computers. The number 300 is where the warning starts; it changes nothing else.
- **The map picker** shows an amber chip, "Many ants: may run slowly", on the row of such a map. The map can be chosen as any other, for a room too.
- **Map Maker, Play this map:** both *Against bots* and *With other players* say "This map starts with N ants, so a game may run slowly, especially on phones and older computers." right under the first sentence of the dialog. The buttons stay on.
- We scanned the 540 maps of the library: none starts more than 300 ants (0 of 540; the most starts 248), so no map in the picker is marked today.

**Rules / network:** none: the rules and network protocol 21 are unchanged.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/99261f5d...85c01ad8), [the warning](docs/MAP_SUBMISSIONS.md#older-games-and-big-maps)

## v0.12.130 - 2026-10-25 - Map Maker: hold Shift or Ctrl to paint a straight line

**For players:**
- **Map Maker: hold Shift or Ctrl while you paint and the stroke stays on one straight line.** It works for Ground (with Auto edges), Objects (rocks and power-ups) and Erase, with the mouse. The line goes straight across or straight down through the square where you pressed, whichever way your first move away from that square goes farther (exactly diagonal goes across), and it stays that way until you let go of the mouse button. Until you leave the first square only that square is painted, and a line that runs off the map ends at the edge.
- **Let go of the key in the middle of a stroke and you paint freely again** from where the line ended; press it again and the square you are on starts a new line. Press the key before the mouse button and the line starts where you press. On a Mac use Shift, because Ctrl-click is a right click there.
- Painting with no key is exactly as before, a finger on a touch screen is unchanged, and tools that put a whole thing with a tap, Fill, Pan, Pick up and the Ants box do not change. Map Maker v1.5.8.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged (the web page only).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/99261f5d...24364af2)

## v0.12.129 - 2026-10-25 - Big selections follow the original's rules

**For players:**
- **A big selection is ordered with the original's rules for every ant.** Ctrl+A or a drag over any number of ants, then a right click: every selected ant gets the move, attack or Stop order (one click orders up to 1,536 ants; "Only 1536 ants ordered." beyond that). The limit of v0.12.126 for a move is gone. Ants that find no free spot near the clicked tile stop where they stand, as in the original; a move onto an enemy hill makes thieves raid and stops the other ants.
- **Special orders stay one ant at a time:** steal, bomb, fire and bridge orders go to the first 32 selected ants at the most, as before v0.12.126, so a crowd cannot steal from one ant or drop 80 bombs on one square. Order those ants one by one.
- A move of a whole army makes the game work hard for a moment while the ants look for a spot. One player's click on a big army costs a few to tens of milliseconds; it is only noticeable when four players' big clicks land together (about 1,500 ants each costs more than a second on a desktop, and a phone is several times slower).

**Rules / network:** none; network protocol 21 is unchanged.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/0ba63ac2...961523be)

## v0.12.128 - 2026-10-25 - The Picture dropdown opens in fullscreen

**For players:**
- **The Picture dropdown now opens in fullscreen.** Watching a replay or a live match in fullscreen with the mouse held (the default, "Locked"), a click on the dropdown in the Picture window did nothing: the browser's own list cannot be opened by the game's held mouse. The control is now the page's own button and list, so a click or a tap opens it and a click or a tap on a shape chooses it, in fullscreen, in the settings menu (Esc), under the game and in the Picture window, on every browser. It looks as before.
- **Keys:** Enter, Space, ArrowDown or ArrowUp open the list; the arrows, Home, End, a letter or a number ("4" for Classic 4:3) move the mark; Enter or Space chooses; Esc closes the list and only the list (the Picture window or the settings menu stays open). On a closed dropdown the arrow keys no longer change the picture (they did on Windows and Linux); they open the list, the same on every browser. The front page's Picture row is unchanged.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged (the web page only).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/0ba63ac2...025710cf)

## v0.12.127 - 2026-10-24 - Map Maker: the Pick up bar no longer covers the map

**For players:**
- Map Maker, Pick up: the bar with Move, Copy here, Delete and Cancel no longer covers the bottom of the map. On a desktop it has a row of its own under the map, so the map ends above it and can be panned until its last row is above the bar; a thing picked near the bottom is brought into view, and the map keeps its top row and its zoom when the bar comes and goes. A phone's bar stays at the foot of the screen, and the page can be scrolled until its last line is above it. Map Maker v1.5.7.

**Fixes:**
- On a short screen the Hide button sits on the line of the words, so the bar is lower; a map under 300 pixels high shows the smaller map of the whole map, without its caption, so that it never reaches into the bar.
- While a thing is on its way the bar keeps the height it has had, so the map no longer grows and shrinks under the mouse near the foot; a phone that is turned with the bar open leaves room for all of it below the page, and a window that is resized while a thing is on its way logs no "ResizeObserver loop" message.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/50b7ae4b...67a05f86)

## v0.12.126 - 2026-10-24 - Orders for big selections

**For players:**
- **Ctrl+A or a drag over more than 32 ants no longer orders only the first 32.** A click or Stop on a bigger selection is sent as several orders of 32 ants each, in the order you selected them, together in that click. A click on 32 ants or fewer works exactly as before.
- **A move now names up to 81 ants:** the game gives a free tile within four tiles of the clicked spot to 81 ants at the most (fewer near walls or water), so the first 81 of a bigger selection walk there; the rest are left alone and keep what they were doing, and the status line says "Up to 81 walk here." Thieves sent to an enemy hill are not limited. A wider search is planned for the next rules release.
- Attack, special and Stop orders reach up to 1,536 ants in one click ("Only 1536 ants ordered." beyond that). A held Stop key (S) now stops once, not twenty times a second.
- A big click adds to a recording (a move about 350 bytes, a Stop of 1,500 ants about 6 KB), so a very long match on a big map can pass the size a recording may have and then keeps none.

**Rules / network:** none in the rules; network protocol 21 is unchanged (several ordinary commands). A server holds up to 1,024 waiting commands of a player (it was 256) so that a burst of big clicks is carried, not refused.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/ac6454fd...50b7ae4b)

## v0.12.125 - 2026-10-24 - Replays can carry their map

**For players:**
- Under the hood: a replay file can now hold the map it was played on (an optional part, up to 1 MiB, checked against the map's hash), so that a match on a map that the server keeps only for a while can be watched later; `replay_tool map FILE OUT` takes the map out. Nothing records one yet. The game, the tool and the replay page read it already, and every replay that exists plays as before.

**Rules / network:** none: network protocol 21, the simulation rules and the replay format (1) are unchanged.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/2d157bf3...dcda43d2)

## v0.12.124 - 2026-10-24 - Three more picture shapes, and one dropdown to pick them

**For players:**
- **Three more picture shapes:** 3:2 (960 x 640, tablets and Surface-style laptops), 20:9 (1200 x 540, phones held sideways) and 32:9 (1920 x 540, super-ultrawide monitors). The list is now Classic 4:3, 3:2, 16:10, 16:9, 20:9, 21:9 and 32:9. A wider picture shows more of the map at the first zoom (32:9 shows about 2.3 times what 16:9 does) and less when zoomed all the way out; everybody picks their own shape and zooming out is free, so no match changes.
- **One dropdown instead of the buttons:** the Picture control under the game, in the front page's Picture row, in the settings menu (Esc: on Windows and Linux the arrow keys change the picture of the running game at once and the menu stays open; on macOS the arrows open the list and Enter picks; Esc with the list open is the browser's own and has not been checked in each browser yet) and in a recording's bar is a dropdown that lists each shape with its size. A quiet line under it, "16:10 fills your screen", names the shape closest to your screen. On an upright phone the dropdown has a line of its own and the page no longer scrolls sideways.
- **The first visit** counts the new shapes too: a 3:2 tablet starts in 3:2, a 3840 x 1080 monitor in 32:9, a phone held sideways in 20:9. Anything you picked, and `?aspect=` in the address (which also takes `3:2`, `20:9` and `32:9`), still wins.
- On a wide monitor the 32:9 picture is shown at its real size: the page's game area is no longer cut at 1280 pixels for it.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged (what a player sees, not what the match is: nobody's shape is sent to the server or to the other players).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/b9a0ab04...c66cf43e)

## v0.12.123 - 2026-10-24 - The Map Maker draws each tile the way the game does

**For players:**
- **The Map Maker now draws each tile the way the game does.** A map keeps a list of names for its tiles, and the game draws the picture that the name says; the editor used to draw the picture of the tile's number. So a pile of jacks showed as a pink eraser and a purple ring as a yellow one on EatClanMap, and the same shift is in 121 library maps. They now look as they do in the game.
- **A tile whose name is "." is not drawn, as in the game:** the ground there is black, and the flowers, grass and objects of such tiles are not in the picture (about 6,100 cells of 187 library maps). Only the picture changes: the maps, their files and the game are as they were.
- The see-through picture of a tile you are about to put or a thing you pick up, and the before and after pictures of Fix edges, follow the same rule. Map Maker v1.5.6.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged (the Map Maker page only).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/ac6454fd...b9a0ab04)

## v0.12.122 - 2026-10-24 - The picture starts in the shape nearest your screen

**For players:**
- **The picture now starts in the shape nearest your screen.** The first time you open the game page or the front page, and until you pick a shape yourself, it starts in Classic 4:3, 16:10, 16:9 or 21:9, whichever is closest to your screen: a 1440 x 900 laptop gets 16:10, a 3440 x 1440 ultrawide gets 21:9, a 4:3 or 5:4 screen gets Classic 4:3. Before, everyone started in 16:9.
- A phone held upright still starts in 16:9 and is still told to turn it sideways; a phone held sideways picks by the shape of its screen, also when it opened the front page upright (the front page passes the game only a shape you picked).
- Nothing you chose changes: a shape you pressed (16:9 too) is remembered as before, and `?aspect=` in the address still wins. The starting shape is not remembered, so it follows your screen until you press a shape.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged (the pages only).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/36df6960...8bee7cc2)

## v0.12.121 - 2026-10-24 - A Discord button in the header

**For players:**
- **A Discord button at the top of the front page**, after Matches: the Discord mark and the word (the mark alone on a phone). It opens the project's Discord invite in a new tab, so a room you are in stays open.
- The same button ends the header of the Matches, match and Live now pages. The game page's header is unchanged.
- It is a plain link: nothing is loaded from Discord and nothing is sent to it until you click. The Legal page says so, and that the project is not affiliated with Discord.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/bc44d6e2...47f4907c)

## v0.12.120 - 2026-10-23 - A Buy me a coffee button at the bottom of the front page

**For players:**
- **A "Buy me a coffee" button at the bottom of the front page.** If you enjoy the game you can support its owner: the yellow button, centered in the footer, opens the owner's Buy Me a Coffee page in a new tab. The repository has a Sponsor button for the same page.
- The button is a picture that your browser fetches from Buy Me a Coffee's own server, so that server sees your address as any website does; the Legal page says so. Nothing else on the front page is loaded from another company.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged (the front page only).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/e95da98a...1158f600)

## v0.12.119 - 2026-10-23 - One table for the picture's shapes

**For players:**
- **Nothing changes on screen.** The four shapes (Classic 4:3, 16:10, 16:9 and 21:9), their sizes, the default (16:9), `?aspect=` in the address, the choice the browser remembers and the "fills your screen" tag work exactly as before.
- Under the hood, the shapes were listed in six places (the game, the game page, the front page, the settings menu, the page's style and its checks). The game now has one table and the web pages have one table, and a test fails when the two differ, so a new shape is one row in each instead of a hunt through the code.

**Rules / network:** none: network protocol 21, the simulation rules and every recording are unchanged.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/d01c8ec8...7812b621)

## v0.12.118 - 2026-10-23 - No Settings button over the picture in fullscreen

**For players:**
- **Nothing is laid over the game's picture in fullscreen any more.** The Settings button that sat at the top of the picture is gone, on computers and phones alike. On a computer press **Esc** (with nothing selected and no dialog open) to open the settings menu, as before.
- **A phone in fullscreen has no way to open the menu** (it has no Esc): leave fullscreen first, then use the **Settings** button under the picture or in the header. In Safari and Firefox on a computer, Esc belongs to the browser in fullscreen, so leave fullscreen first there too.

**Rules / network:** none: the page only; network protocol 21 and the simulation rules are unchanged.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/c93ee3f5...26f6d531)

## v0.12.117 - 2026-10-23 - Test rooms work on every server

**For players:**
- **Play this map > With other players now works on every server.** The Map Maker's test room used to need the map submissions to be switched on; where they were off (the beta) it showed a dimmed, empty lobby. It now sends your draft map to a small door of its own that is always on, so a room on your draft opens whether or not submissions are open.
- **A test map is as private as before:** it is checked by the same checker as a submitted map, it is on no list, no match on it is recorded, and it is deleted after 6 hours or when the server restarts. A map too big for a room (over 65,536 cells) is still turned away with the same words.
- **The page says why a test room cannot open** in plain words: test rooms switched off on the server, or a map too big to send.
- **For server owners:** the door is port 4031 (`ANTS_TEST_MAP_PORT`, 0 turns test rooms off), on by default; it has no database and no accounts and takes nothing but test maps. The old `/submissions?test=1` form is gone.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/9aa3e544...02eabd8e)

## v0.12.116 - 2026-10-23 - Map Maker: red drops go back, things may stand on water, flowers have stems

**For players:**
- **Map Maker (v1.5.5), Pick up: let go on a red place and the thing goes back.** Drag a hill, an ant, a pile or a plant, let go of the mouse, your finger or the pen where the copy is red, and it jumps back to where you picked it up. Nothing is written and nothing is left to Undo, and the bar says why, and the line stays until your next move ("Put back: the mushroom is in the way"). A lift on a free place still puts it down in one step, a plain click still only places the copy, and Done on a red place keeps the thing in your hand with the reason.
- **Map Maker: hills, starting ants and food piles may be put on water.** Pick up used to refuse them, although many maps of the library have them there. The copy stays green and the bar adds one line about what to expect: an ant that cannot swim moves to land beside its marker, or drowns when there is none; ants that cannot swim reach a pile in the water only from the shore, while Swimmers swim to it or build a bridge; a hill whose way out is water may keep its ants in. The other tools never refused water.
- **Map Maker: flowers are drawn whole, as in the game.** The flower with sunglasses and the other flowers showed only their heads; now the stems, the leaves and the shadow are there too (the same pixels as the game's).

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged (the Map Maker page only).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/56e02b48...81bd4d9d)

## v0.12.115 - 2026-10-23 - Path finder: a wide search for very big maps (switched off for now)

**For players:**
- **Nothing changes in play yet.** The game now contains the groundwork for very long orders on very big maps (today an order of about 400 tiles or more, 286 on a diagonal, is refused with "Can't go there", and a group of ants sent far gets its paths slowly). It is the original's own search, with the same routes and tie-breaks, only without the width limits, and it is switched off: no map can ask for it yet, and every map of the library plays exactly as before (all 540 maps hash byte for byte the same, also when the new search is forced on for all of them).
- Whatever the original can play, the new search plays the same, at the same speed: it keeps the original's 1,000 search steps every 50 ms until an order is past the original's limit, and only then goes faster on a big map.
- The next release lets a new big map ask for it.

**Rules / network:** none. Network protocol 21 and the replay rules numbers are unchanged. A new map rules word 2 exists inside the simulation only; the map readers and writers still refuse it, and the simulation itself plays a word of 2 as 1 outside the tests, so no map and no recording can use it.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/86afa1dc...1a1f55e3)

## v0.12.114 - 2026-10-23 - A settings menu on Esc and a Settings button

**For players:**
- **A settings menu, on the Esc key or a Settings button.** In the browser, press Esc (with nothing selected and no dialog open) or click **Settings** to open a panel over your game with Sound, Video, Controls and Game, and **Resume** and **Leave game** at the bottom. **The game keeps running** while it is open, so settle your settings quickly in an online match. Esc still deselects first, as the original does; the next Esc opens the menu. A click on the dimmed area does not close it.
- **Where the button is:** in the page's header next to Menu, under the picture on a phone or a narrow window, over the picture (beside the cross that leaves fullscreen) while the page is fullscreen, and in the bar of a replay. Safari and Firefox keep Esc for themselves in fullscreen, and a phone has no Esc, so the button is the way in there.
- **New: Master volume, Mute and remembered hit point numbers.** Master volume scales every sound and the music; Mute silences everything and keeps your sliders where they are; the white hit point numbers (Ctrl+L) now stay on or off from one game to the next. The menu also has the sound sliders, the picture shape, Fullscreen, the fullscreen mouse, the map scroll speed, chat, quick help and a list of the mouse, touch and keys. The original's Options window is unchanged.
- **Leave game says what will happen** before it does it: with one opponent left it is a forfeit that ends the match for everybody and counts as a loss; with more it drops you out and the others play on; at home it ends the game. There is no separate Surrender button.
- **Replays and live watching** have the same panel without the Game section ("Back to the matches" leaves, asking nothing); the frame of a match page has none.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged; the panel is the page and the game's own sound, picture and screens, and Leave game is the Quit that the game always had. The desktop app has no panel yet; its Esc is what it was.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/14bbbfdb...feb90e21)

## v0.12.113 - 2026-10-23 - Pick up: a drag let go off the map goes back

**For players:**
- **Pick up: drag a thing off the map and let go, and it goes back.** If you let go of the mouse or lift your finger outside the map (off its edge, over the side panel or over the bar), nothing changes and the thing is back where it was. While your pointer is off the map the see-through copy goes away and the bar says "Let go here and it goes back where it was."
- **Enter or Done while you are still dragging no longer throws the pick away.** They now wait: you lift, and the thing is put down where the copy is.
- A drag that ends on the map still puts the thing down in one step (or stays in your hand, red, with the reason, when the place is blocked); Escape and a click work as before.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged. Map Maker v1.5.4.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/37aeace1...1291d49e)

## v0.12.112 - 2026-10-23 - Hard computer players take the quick kill and stop sending ants where no walk goes

**For players:**
- **The Hard computer players shove more enemies into water, bombs and fire.** An enemy ant next to water, a bomb or a fire wall dies of one blow from the side away from it. A free Hard ant now walks round to that side and strikes; before, it only did so when it happened to stand in line. Over many maps they get a seventh to a quarter more kills.
- **They waste about 40 percent fewer orders.** They no longer send ants at enemies across a lake, which the game used to answer with "Can't go there": about 31 of every 100 orders before and 19 now over the map library (23 and 13 in an independent check).
- **On maps with no food they can reach (AntsTournament, Killing Spree), Hard players now go and fight** instead of waiting at home. Where the food lies over water or behind a gate, the ants keep that work.
- **The ring of fire round an enemy gate is lit only when that team has food to bank.** Hard builds and puts out its other fire walls as often as before.
- **This is not a stronger player.** Against the old Hard player it wins as often (about 50 of 100, with four players or with two), and against Medium as often as before. An enemy beside the water within reach is still left alone about 7 times in 10 (a Hard ant only goes for one that has stood still for a second), ants stand idle a little more because they no longer run pointless trips, and on one map, Acid Blood, the new kill made it play worse until the last ants were told to stay at their work. Easy and Medium are unchanged.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged (computer players run on the server or the host and only send commands, so old recordings play as before).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/91a09a4b...a3899325), [the quick kill](docs/BOT_FIGHT.md)

## v0.12.111 - 2026-10-23 - Map Maker: Start from a game map opens the six original maps

**For players:**
- **Map Maker (v1.5.3): "Start from a game map" now opens the six maps that came with the game.** Tiny, Small, Medium, Gauntlet, Treasure and Islands always said "The map file could not be downloaded. Check your connection and try again.", because the site had no copy of those six files to hand out (only the players' maps). It has them now. Pick one and the editor opens a copy of it under a free name, so you can edit and save it right away; the original is never changed.
- **The message now tells the truth.** A map the site does not have says "This map is not available from the site right now." A site that answers badly says "The site could not send this map. Try again in a moment." "Check your connection" stays for a connection that really failed.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged (the site and the Map Maker page only).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/dc809cd5...588c0067)

## v0.12.110 - 2026-10-23 - Pick up: a drag is put down when you let go of the mouse

**For players:**
- **Pick up: a drag now ends when you let go.** Drag a hill, object, food pile, plant or ant (or a whole group or area) in the Map Maker and let go of the mouse or lift your finger: it is put down where the see-through copy is, in one step that one Undo takes back. Before, the copy kept following the mouse after you let go until you pressed Enter or Done.
- **A red place keeps it in your hand.** If you let go where it cannot go (on a mushroom, over the edge, in the water), nothing is changed, the copy stays red and says why, and you can drag it on to a free place.
- **Escape takes it back.** Escape while you drag, a touch that is cancelled, or leaving the window, puts the thing back where it was. A plain click or tap still only places the copy, and the arrow buttons, Enter and Done work as before.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged. Map Maker v1.5.2.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/ea4ec07f...4c8c2434)

## v0.12.109 - 2026-10-22 - A put-out fire leaves the screen on maps with fire walls

**For players:**
- **A fire that a Fire ant puts out now leaves the screen on maps that came with fire walls.** On a map made with the Map Maker's permanent Fire wall (and on five maps of the library), the Fire ant did its put-out move and the game agreed the fire was gone (the message "Fire put out." came, ants could walk there), but the picture of the fire stayed on the map. Now it disappears, as it does for a fire that was lit in the match.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged; only the picture changed. Fire walls that the Map Maker puts on a map still burn until a Fire ant puts them out.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/d7401fec...1ac880b2)

## Older versions (v0.12.108 and before)

Every release before v0.12.109, and the long notes of v0.12.108, v0.12.107, v0.12.106, v0.12.105, v0.12.104, v0.12.103, v0.12.102, v0.12.101, v0.12.100, v0.12.99, v0.12.98, v0.12.97, v0.12.96, v0.12.95, v0.12.94, v0.12.93, v0.12.92, v0.12.91, v0.12.90, v0.12.89, v0.12.88, v0.12.87, v0.12.86, v0.12.85, v0.12.84, v0.12.83, v0.12.82, v0.12.81, v0.12.80, v0.12.79, v0.12.78, v0.12.77, v0.12.76, v0.12.75, v0.12.74, v0.12.73, v0.12.72, v0.12.71, v0.12.70, v0.12.69, v0.12.59, v0.12.58, v0.12.57, v0.12.56, v0.12.55, v0.12.53, v0.12.52, v0.12.51, v0.12.50, v0.12.49, v0.12.48, v0.12.46, v0.12.45, v0.12.44, v0.12.43, v0.12.42, v0.12.41, v0.12.40, v0.12.39, v0.12.38, v0.12.37, v0.12.36, v0.12.35, v0.12.33, v0.12.32, v0.12.31, v0.12.30, v0.12.29, v0.12.28, v0.12.27, v0.12.26, v0.12.24, v0.12.23, v0.12.22, v0.12.21, v0.12.20, v0.12.19, v0.12.18, v0.12.16, v0.12.15, v0.12.14, v0.12.13, v0.12.12, v0.12.11, v0.12.10, v0.12.9, v0.12.8, v0.12.7, v0.12.6, v0.12.5, v0.12.4, v0.12.3, v0.12.2, v0.12.1, v0.12.0, v0.11.0, v0.10.0, v0.9.1, v0.9.0, v0.8.3, v0.8.2, v0.8.1, v0.8.0, v0.7.0, v0.6.0, v0.5.1, v0.5.0, v0.4.0, v0.3.0, v0.2.0, v0.1.3, v0.1.2, v0.1.1, v0.1.0 and v0.0.99 - v0.0.90, are only in the git history.
