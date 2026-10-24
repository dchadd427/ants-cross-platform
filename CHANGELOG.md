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

## v0.12.118 - 2026-10-23 - No Settings button over the picture in fullscreen

**For players:**
- **Nothing is laid over the game's picture in fullscreen any more.** The Settings button that sat at the top of the picture is gone, on computers and phones alike. On a computer press **Esc** (with nothing selected and no dialog open) to open the settings menu, as before.
- **A phone in fullscreen has no way to open the menu** (it has no Esc): leave fullscreen first, then use the **Settings** button under the picture or in the header. In Safari and Firefox on a computer, Esc belongs to the browser in fullscreen, so leave fullscreen first there too.

**Rules / network:** none: the page only; network protocol 21 and the simulation rules are unchanged.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/9aa3e544...26f6d531)

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

## v0.12.108 - 2026-10-22 - Auto edges draw a strip of water one cell wide in mud, in any painting order

**For players:**
- **Map Maker (v1.5.1), Auto edges: water painted one cell wide inside mud now gets its soft edges.** A narrow stream used to keep hard square sides, with a single rounded piece where it bent. The mud on both sides of it now takes the edge pieces toward the water, the way the game's own maps draw a narrow lake. It comes out the same whether you paint the water first and the mud round it, or the mud first and the water inside.
- **The same for every other pair of ground that has edge pieces:** a strip one cell wide of gravel in mud or dirt, of slate in dirt or mud, of dirt in mud, and a one-cell inlet or a single cell of one kind inside another (the square notch in a blob of mud is rounded too). **Fix edges...** does the same on a map you have already drawn. It still never takes a piece away, and what you placed by hand stays.
- **Water beside gravel, dirt or slate keeps a hard edge:** the game has no edge piece for those pairs, so none is put there.
- **Painting in several strokes always leaves what Fix edges would make.** Auto edges now looks a little further round each stroke, so a piece from an earlier step is never left out of date.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged (the Map Maker page only).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/47ee66fc...be7df044)

## v0.12.107 - 2026-10-22 - Map Maker: the Pick up tool moves, copies and deletes what is on a map

**For players:**
- **The Map Maker has a new tool, Pick up (key P).** Tap a hill, object, food pile, plant or starting ant and a gold ring shows everything it covers (a hill's 4 x 4 square and its door, a pile's footprint). A bar then offers **Move**, **Copy here**, **Delete**, **Select an area...** and **Cancel**.
- **Move shows a see-through copy of the thing where it would land.** It is green when it fits and red when it does not, with the reason in plain words ("The red hill cannot go here. It needs a free 4 x 4 square, and the mushroom is in the way."). On a desktop it follows the mouse. Use the arrow buttons (or the arrow keys, Shift for 8 cells) to nudge it, or drag it, then press **Done**. Enter does the same, Escape cancels, Delete deletes.
- **Ants: "this ant or its group".** One tap picks one ant; Move, Copy here and Delete each ask whether to take only that ant or the whole group of that team's ants that stand together.
- **Select an area** picks every object that lies whole inside a box you draw, so a whole stretch of the map can be moved or copied at once.
- **One Undo undoes a whole move, copy or delete**, including an area. A short help sheet, "What can be picked up?", is opened from the tool's note, and on a short screen (a phone held sideways) the bar is smaller and can be folded away.
- **The old Move tool is now called Pan** (the M key still chooses it). It only looks around and never changes the map. Maps in the library are never changed by any tool.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged. Map Maker v1.5.0.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/146ef42b...dc352756)

## v0.12.106 - 2026-10-22 - Where ants died: the map of deaths on the match page

**For players:**
- **A new card, "Where ants died", on the page of every match played after this update**, between "Over time" and "Activity": the match's own map with a soft glow where many ants died (dark red for one, gold and cream for many), or one mark on the tile of each ant. Pick one team's ants with its chip, hide the ants that drowned with one switch, mark the first blood and the last kill, zoom with + and - (or the mouse wheel, or two fingers) and tap a mark to see its tile, the time, the kind of ant and who killed it.
- **"All matches on this map"** is a switch on the card: it adds up every match on that map that has a map of deaths, to show where ants really die there.
- **The Stats panel of the replay page** has a smaller version of the card under its table.
- On a very big map, or in a match with more deaths than fit one by one, the card shows colored squares with the number of ants that died in each.
- A match played before this update has no map of deaths; its page says so in one line.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged (the server only watches while it replays a finished match). **Server owners:** the database moves from schema version 6 to 7 at its first start (copy `ants.db` first: an older program refuses version 7).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/4f281d02...52079baf)

## v0.12.105 - 2026-10-22 - Map tools never touch the library, fixes after the review of 0.12.103

**For players:**
- **A START that cannot get a place no longer ends other people's matches.** When a big map needs several of the server's places and the server cannot give them, matches that nobody has come back to are left alone; they are ended only when that really makes room.
- **A saved match on a map over 65,536 cells is not brought back after a server restart.** It ends with the reason "too big to play online" instead of coming back into an online room.
- **Big maps are sent faster.** The server reads a map file in one piece (about 0.5 ms instead of 11 ms for a 3 MB map).
- **The owner's page asks before it resets a password**, the way it asks before it removes a user, and the red note about the last Admin goes away once a user has been added.
- **The Tab key stays inside the boxes of the owner's page** (it had been leaving them in real browsers).
- **The map tools never touch the map library.** `lvl_tool`, the map sweep, the bot arena and the scripts that build, draw or test maps refuse, with a plain message, to write into `Original-Ants/` or `Community-Maps/` or over the file they read. `lvl_tool rewrite` no longer takes the same file as input and output.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/4bf5f60a...dbd35b23)

## v0.12.104 - 2026-10-22 - Big maps keep their fingerprints; the limits of big maps are written down

**For players:**
- **Games on big maps in the original rules keep their old fingerprints.** Since v0.12.97, a map of the original rules with more than 90 rows and more than 120 columns gave a different game fingerprint for as long as an ant held the order tile column 120, row 90, so an older and a newer game (or a recording from before) could disagree about that map. It checks out as before again. Library maps, Classic maps and every other play were never affected.
- **What a big map can do is written down.** Maps up to 512 x 512 load and play, but on an open 512 x 512 map a diagonal order of 286 tiles or more, or a straight one of 400 tiles or more, is refused ("Can't go there"), and a group of 30 ants sent 200 tiles on a 512 x 512 map can take two and a half minutes before the last ant starts walking. Nothing about this changed; the map-format document now says it.

**Rules / network:** none: network protocol 21 and the simulation rules number are unchanged. For the record, since v0.12.97 an order to exactly column 120, row 90 on a map of the extended rules (rules 1) is a real target where it used to read as "no order"; the number was not raised for it.

**Fixes:**
- Two wrong comments in the code about the order tile were corrected.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/1aad1500...2132ed99)

## v0.12.103 - 2026-10-22 - Big maps load, online size limit, old games named, test-room limits

**For players:**
- **Big approved maps load again.** The site could not send a map file bigger than 1 MB, so approved Extended maps over about 290 x 290 (such as 400 x 400 and 512 x 512) were in the lists but never loaded in the Map Maker, a replay or a room. They load now.
- **Online rooms play maps up to 65,536 cells (256 x 256) for now.** A bigger approved map, or a bigger draft in the Map Maker's **With other players**, stays in the lists, opens in the Map Maker and plays on your own computer or against bots in the test game, but an online room says "This map is too big to play online" and keeps its map. Big maps also take more of the server's room (a 256 x 256 match counts as seven ordinary ones) until the load is measured.
- **A game that is too old for a map is named.** When a start is cancelled because one player's game cannot load the map, the room says whose game it is ("Bob's game is too old for this map: refresh the page or update the game.") and tells the leader to press **Remove** on that color to start without them. Before, it only said that the match could not start.
- **Test rooms from the Map Maker have limits of their own.** A test map is kept in the standard form, no longer uses up the 20-an-hour budget of real submissions, and a flood of test maps cannot push out the map of a room that has not started.
- **Matches on approved maps count** in the map's plays and stats.

**Rules / network:** none: network protocol 21 and the simulation rules are unchanged.

**Fixes:**
- The bot arena plays Extended maps.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/e7d33a22...6dd7e251)

## v0.12.102 - 2026-10-22 - Map maker: a friends room that cannot silently become Treasure

**For players:**
- **"Play this map" with other players no longer turns into a Treasure room.** If the map cannot be sent to the server (sending maps is switched off on that server, too many test maps were sent just now, or the editor tab was closed), no room is made at all. The strip at the top of the page says why in plain words and tells you to go back to the editor, instead of quietly showing an ordinary room with the Treasure map.
- **A room made again keeps your map.** When the page has to make your room again (the room was closed, the server restarted, or the connection was lost for a long time), the test map is sent to the new room again, so it does not fall back to Treasure.
- **The line at the foot of the page is true in a test room.** In a room that plays a test map from the Map Maker, it now says that the match is not recorded and that no result or name is kept.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/5a780efc...8de56d9f)

## v0.12.101 - 2026-10-21 - Auto edges switch in Map Maker

**For players:**
- **Auto edges: On / Off switch in the Map Maker (`v1.4.5`).** A new **Auto edges: Off / On** switch sits next to **Fix edges...** (on a phone held upright, under the tool buttons while **Ground** is the chosen tool, and in the phone's **Menu** under **Settings for this browser**). It starts **Off** for a new visitor and is remembered in the browser; when **On**, painting plain ground automatically sets the edge and corner pieces around the stroke as part of the same stroke (one **Undo** step), while leaving hand-placed custom tiles next to the stroke alone until painted over directly or until **Fix edges...** is run.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/c5930395...02374c29)

## v0.12.100 - 2026-10-21 - Play draft maps with other players from the Map Maker

**For players:**
- **Play draft maps with other players from the Map Maker (`v1.4.4`).** The **Play this map** box in the Map Maker now has two tabs: **Against bots** (single-player test run) and **With other players** (opens an online lobby room with the draft map). The room code and link work like any other room, the draft map is downloaded and verified automatically for everyone who joins, and the room and its match are never listed in `/maps`, `/replays`, `/live` or `/history`.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/5f3d19d9...a3ed04cb)

## v0.12.99 - 2026-10-21 - Pre-placed neutral bombs and fire walls in Map Maker

**For players:**
- **Pre-placed neutral bombs and fire walls in the Map Maker (`v1.4.3`).** The **Objects** palette in the Map Maker now includes a permanent **Fire wall** (`134`, `wallup04`) and the four colored **Bombs** (`129..132`: `Black bomb`, `Blue bomb`, `Green bomb`, `Red bomb`), matching `POPcOrN.lvl`. Pre-placed bombs are written and kept with a neutral owner word (`0x0101`) in both Classic (`v8`) and Extended (`v9`) maps so every team can step on them and no team receives kill credit or auto-avoids them.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/077a66c7...3a39dc50)

## v0.12.98 - 2026-10-21 - Serve and play approved Extended maps on the server, web lobby, and Map Maker

**For players:**
- **Approved Extended maps in the web lobby, match history, and Map Maker (`v1.4.2`).** Approved Extended maps (`version == 9`, up to `512 x 512` / `262,144` cells / 4 MiB) now appear with an **Extended** badge and preview thumbnail in the web lobby's **Choose a map** picker, the match history and live pages, and the Map Maker's **Open a map** library dialog (`GET /maps/all`, `format: 2`), while `GET /maps` (`format: 1`) remains Classic-only for older cached pages.
- **Online rooms and downloads for approved Extended maps.** The dedicated game server (`ants_server`) serves `.lvl` files (`/maps/files/<name>.lvl`) and PNG preview pictures (`/maps/previews/<name>.png`) for approved Extended maps and loads and validates Extended maps when starting online rooms.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/2336f325...a6f4a2d4)

## v0.12.97 - 2026-10-20 - Play Extended maps up to 512 x 512 and out-of-grid order-tile marker

**For players:**
- **Playable Extended maps up to `512 x 512`.** Extended maps (`version == 9`) up to `512 x 512` (`262,144` cells, up to `1,024` a side, 4 MiB) now load and play in the game, map selector previews, replay player, `bot_arena`, and the Map Maker's **Play this map** (`v1.4.1`).
- **Ordering to tile `(120, 90)` on wide and tall Extended maps.** Replaced the `(120, 90)` "no order tile" sentinel in `AntUnit::orig_order_tile` with an out-of-grid marker `(-1, -1)` (`kNoOrderTileX`, `kNoOrderTileY`), so ordering an ant directly to tile `(120, 90)` on an Extended map wider than 120 columns and taller than 90 rows works like any other tile while preserving all Classic and `rules == 1` state hashes on the 540-map library.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/30b8fe77...04a4e570)

## v0.12.96 - 2026-10-20 - Play Extended maps up to 256 x 256 and open game maps in Map Maker v1.4.0

**For players:**
- **Playable Extended maps (`256 x 256`).** Extended maps (`version == 9`) up to `256 x 256` (`65,536` cells) now load and play in the game, map selector previews, replay player, `bot_arena`, and the Map Maker's **Play this map**, and resizing an Extended map past 90 rows or 256 columns in the Map Maker upgrades its rules word so ants can cross row 90.
- **Map Maker v1.4.0 — Open a map from the game's library.** Clicking **Open** in the Map Maker now opens the **Open a map** dialog, where you can either open a `.lvl` file from your device or search, sort (`Originals, then plays`, `Most played`, `Name`, `Size`), and pick any existing game map (the 6 Originals and 530+ Community maps, with preview thumbnails and metadata) and click **Use as starting point** to open it under a free numbered copy name (`"Treasure 2"`, etc.).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/ca107726...320c9e0b); how it works: [`docs/MAP_FORMAT.md`](docs/MAP_FORMAT.md), [`docs/MAP_EDITOR.md`](docs/MAP_EDITOR.md)

## v0.12.95 - 2026-10-20 - Extended maps: per-ant starting ant types and Map Maker v1.3.0

**For players:**
- **Extended maps: per-ant starting ant types.** In Extended maps (`version == 9`), each starting ant can now be placed as any specialist type (Default / Worker, Combat, Thief, Bomber, Swimmer, or Fire), and spawns with that type when the match starts. Classic maps (`version == 8`) continue to use a single map-wide Default Ant Type.
- **Map Maker v1.3.0:** The **Starting ants** panel adds a 2 x 3 **Ant type** selector with sprite thumbnails on Extended maps (for **One at a time**, **Clump**, and **Mix**), a one-click button to switch a Classic map to an Extended map for per-ant types, and per-ant specialist sprite rendering in each team's color on the map canvas.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/7a583081...9266739e); how it works: [`docs/MAP_FORMAT.md`](docs/MAP_FORMAT.md), [`docs/MAP_EDITOR.md`](docs/MAP_EDITOR.md)

## v0.12.94 - 2026-10-20 - Adaptive minimap, crisp zoom-out, 5-digit results, and Map Maker v1.2.0

**For players:**
- **Game minimap and zoom-out:** Non-square maps now keep their true proportions in the HUD radar slot (top-aligned when wide, centered when tall) with an adapted recessed bevel and team-colored panel surface fill, and zooming out on hardware windows stays crisp at native window resolution.
- **Game Results screen:** The `960 x 540` results table widens the Lost, Killed and Hatched columns so 5-digit counters fit cleanly without overlapping.
- **Map Maker v1.2.0:** Added all 32 original food items with Bites (uses) and Points per bite steppers, 14 canopy and power-up dropper plants, a Move tool, a brush slider of 1 to 9 cells, Map Resizing (with an interactive crop frame when shrinking), 24 x 24 thumbnails on ground look-alike tile buttons, selected Default Ant Type sprites in Map properties and in team colors on the map canvas, outer scrim click-to-close on dialogs and sheets, and a bottom version footer bar.

**Fixes:**
- Deleting or moving a hill or object in the Map Maker now clears its Layer-1 solid footprint so starting ants do not avoid the area.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/46824e4c...128cce73); how it works: [`docs/MAP_EDITOR.md`](docs/MAP_EDITOR.md)

## v0.12.93 - 2026-10-19 - Owner page: sign in with a username and a password (replaces Discord)

**For players:**
- Owner page: you sign in with a username and a password instead of Discord. There are two roles: an Admin does everything, including adding users, resetting or removing them and reading the list of sign-ins and actions; a Moderator approves, declines, takes down and puts back maps. The lists show who decided each map.
- A new account gets a random starting password that is shown once, and its owner must choose their own at the first sign-in; anyone can change their password from the account menu. Five wrong tries lock a name at an address for 10 minutes.
- The first account is made on the server with one command (`ants_server --admin-create <name>`); a forgotten password is `--admin-reset <name>`. Setup changes: no Discord application, no `ants-admin-login` container and no `ANTS_ADMIN_DISCORD_ID`, `ANTS_DISCORD_*` or `ANTS_PUBLIC_URL`; `ANTS_ADMIN_SESSION_SECRET` stays.

**Rules / network:** none (protocol 21). The server's database moves from schema version 5 to 6 at its first start (copy `ants.db` first: an older program refuses version 6).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/b9c3e784...6948051a)

## v0.12.92 - 2026-10-19 - Map maker: place ants one at a time, and mix teams

**For players:**
- **Map maker: place ants one at a time.** Choose Ants, tap a cell and one ant of the picked team stands there; tap an ant (any team) and it is taken away. "Take one away" makes every tap a removal. Each team shows how many ants it has, with plain warnings (a team with none, too many for the file, ants packed too tight).
- **Group** keeps the clump of before and adds **Mix**: pick the teams and how many ants for each, choose every cell or every other cell, drag a box and the teams are mixed cell by cell, with a preview and one Undo. Good for a "mosh pit" start.
- **Clear (team) ants...** takes one team's ants away after asking, and says how many.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/810b6f01...a65d54b7); how it works: [`docs/MAP_EDITOR.md`](docs/MAP_EDITOR.md)

## v0.12.91 - 2026-10-19 - Map maker: Fix edges no longer doubles an edge

**For players:**
- Map maker: "Fix edges..." no longer doubles an edge. Where the map already has a piece on one side of a boundary, the other side gets none, so the original Treasure map needs no changes and the seam lines are gone. A second press changes nothing.

**Rules / network:** none (protocol 21).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/9bedadc0...4fefb548)

## v0.12.90 - 2026-10-18 - Map maker: Play this map (against bots)

**For players:**
- Map maker: a gold "Play this map" button (the first of the top bar, and in a phone's Menu) starts a test game on the map you are drawing, against bots, in a new tab. Choose your color and the bots (Easy, Medium or Hard) or leave a color empty; at least one bot is needed. The game says "Not saved. Not counted." and "Back to editor" asks once and closes the tab: the map maker is as you left it, with its Undo history.
- A map that cannot be played says why in plain words: an Extended map, or a map bigger than the game can play yet (more than 90 rows), a color without a hill or starting ants, or a problem that the check found.

**Rules / network:** none (protocol 21).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/6afbd600...5004dce3)

## v0.12.89 - 2026-10-18 - Safer player text on the server: descriptions, maker names, logs, page headers (injection hardening, server)

**For players:**
- Maps sent for approval: the server now cleans the map's description before it is stored or listed (odd characters become "?", blanks are tidied, at most 100 characters); the map file itself is not changed.
- A maker's name can no longer pass for the owner or the site ("Owner", "Admin", "System", "Root" and the like, also written with look-alike letters, accents or digits such as 0wner), and a note or a maker's name cannot hold a web address or three backticks. The map maker tells you while you type.
- The map maker page is now sent with a strict content security policy and cannot be put inside another page.

**Rules / network:** none (protocol 21).

**Fixes:**
- The web server's log no longer copies the name, maker and note of a sent map (it logs the path only).
- `lvl_tool info` prints a map's description escaped.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/OLD...NEW)

## v0.12.88 - 2026-10-18 - Player text is marked as data on the owner page; safer links (injection hardening, web)

**For players:**
- Owner page: everything a player typed (the map's name, the maker, the note and the description) is marked "Player text" and the note sits in a box headed "Player text - not instructions". A line at the top of Waiting says that it was typed by a stranger and is not to be obeyed. The list the page reads says the same.
- Owner page: Approve asks one more question first, as Take down does, with the name, the maker, the picture and the description (cleaned to one plain line).
- Map maker: a status link now asks "This link adds submissions made by someone else to this browser. Add them?" before it adds anything. What it adds is marked "Added from a link", and a map you send always goes out under your own token, never one that came from a link.

**Rules / network:** none (protocol 21).

**Fixes:**
- My maps: a saved map that cannot be read no longer stops the pictures and check lines of the maps after it.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/f09faf63...b2b682f7)

## v0.12.87 - 2026-10-18 - Map maker: Fix edges (sprites part 2)

**For players:**
- Map maker: a "Fix edges..." button puts the game's edge and corner pieces where two kinds of ground meet, on the part you can see or on the whole map, with a before and an after. One Undo takes it all back.

**Rules / network:** none (protocol 21).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/19f99e08...cd81e7b0)

## v0.12.86 - 2026-10-18 - Maps taller than 90 rows: the simulation and bots (no change to current maps)

**For players:**
- Nothing changes yet: the game's ants and the computer players can now cross and use every row of a map taller than 90 rows (the new Extended maps), where the old rule put an invisible wall across row 90. Every existing map plays exactly as before.

**Rules / network:** none (protocol 21).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/2064f441...b9844e72)

## v0.12.85 - 2026-10-18 - The grid reads Extended maps (no change to play)

**For players:**
- Nothing changes yet: the game's grid can now read the new Extended maps (taller than 90 rows), ready for the next steps. Every existing map plays exactly as before.

**Rules / network:** none (protocol 21).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/d2bba8f1...3d6d432f)

## v0.12.84 - 2026-10-18 - Map maker: the game's own sprites (part 1)

**For players:**
- The map maker draws the game's own pictures, with five kinds of ground (Gravel, Dirt, Slate, Mud, Water) and a sheet of every ground tile.

**Rules / network:** none (protocol 21).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/124874d2...7af2dc38)

## v0.12.83 - 2026-10-18 - Map maker: send a map for approval (S2)

**For players:**
- Map maker: "Submit for approval..." sends a map to the owner, and My submissions shows the answers. Nothing is public until approved. Off until switched on.

**Rules / network:** none (protocol 21).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/c418fafb...123bf2ea)

## v0.12.82 - 2026-10-18 - Map submissions: server and owner review page (S1)

**For players:**
- Nothing changes yet: this is the server half of sending a map for approval (the Submit button comes next). A sent map waits for the owner. Off until switched on.

**Rules / network:** none (protocol 21). The database goes to schema version 5.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/ac0ad9d3...a4d61167)

## v0.12.81 - 2026-10-18 - Lobby: Map maker link

**For players:**
- The lobby has a small Map maker button next to Matches, at the top right.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/73e722fb...be12b42c)

## v0.12.80 - 2026-10-18 - Map maker: My maps (E6)

**For players:**
- The map maker page (/mapmaker.html, not linked yet) has My maps: the maps you saved in this browser, newest first. Open one, copy it, or delete it after a question.
- The top bar wraps up to 1,150 pixels so all its buttons stay in view.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/c30c9a4a...f6d1f011)

## v0.12.79 - 2026-10-18 - Map maker: save as a new map and drafts (E5)

**For players:**
- The map maker page (not linked from the site yet: /mapmaker.html) can save a map now. Save as new... (in the top bar, or in the Menu on a phone) keeps the map in your browser as a new map under a free name, and shows it, so the next Save makes one more new map. A map is never written over: if the name is already used by the map you opened, one of your saved maps or one of the game's maps, it says so and offers the next free name in one click ("Use Sunny Picnic 3").
- Copy and Start an Extended map use the same name check, so a copy can no longer take the name of a game map or a saved map.
- What you draw is kept in the browser while you work. If you close the page without saving, the next visit offers to pick the work up or throw it away, and the question before you leave a map offers Save as a new map first. A browser that cannot keep files says so, offers Download, and asks before the page is closed as before.
- Saved maps cannot be opened again from the page yet: the list of your maps comes next.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/d09bb5c9...421ebb6b)

## v0.12.78 - 2026-10-17 - Map maker: convert, crop and copy (E4)

**For players:**
- The map maker page (not linked from the site yet: /mapmaker.html) can copy and convert a map now. Copy... (in the top bar, or in the Menu on a phone) makes a new map of the same kind or of the other kind. Under the size numbers of the side panel, "Start an Extended map from this one..." makes an Extended map from any Classic map, and "Make a Classic map from this one..." brings an Extended map back when it fits. Every way makes a new map under a name that is free; the map you opened is never changed or replaced.
- An Extended map that does not fit a Classic map shows the reasons in the converter's own words, and two ways out. Crop it to fit... opens a screen with a movable frame (never bigger than a new Classic map): drag it with the mouse or a finger, pull a corner or an edge, and a wider frame gets shorter by itself. Shorten the description... opens the map properties on the copy.
- A part that is cropped out keeps every object whole or leaves it out (never half a hill), moves the starting ants and food piles inside it, and says what it left out.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/e47cec0b...3ab82b75); the map core (interface version 4) has a new call, `lvl_crop`; the page needs the new module, so reload it once.

## v0.12.77 - 2026-10-17 - Map maker: map properties (E3c)

**For players:**
- The map maker page (not linked from the site yet: /mapmaker.html) can set the properties of a map now: press Properties in the top bar (or Menu, then Properties... on a phone), or the Edit link beside the short summary of the map. You can change the description, the length of a match (1 to 120 minutes), the eggs that every team starts with (0 to 255) and the default ant type (none, combat, thief, bomber, swimmer or fire).
- The description counter counts bytes, because the map file does: a Classic map has room for 29 and an Extended map for 255, and a letter such as the e with an accent counts double. A text that is too long gets the same sentence as the check of the map and a button that cuts it to fit.
- Apply makes all the changes one step (Undo takes them all back), and Cancel changes nothing. The map's other bytes are not touched, so a map that you open and apply without a change is the same file.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/4256634d...05168d98)

## v0.12.76 - 2026-10-17 - Map maker: bigger objects (E3b)

**For players:**
- The map maker page (not linked from the site yet: /mapmaker.html) has a new tool, Bigger (or press K), for the objects that cover several cells: grass, leaves, rocks, toys, cans and glasses, 29 of them, named as the game names them (grass4, grassmed1, bearbod ...), each drawn as its shape. They are the ones that the game's own maps use most. Tap or click and the whole object appears, the same shape that the game's maps have; the eraser takes it away whole; undo and redo work for it.
- Rocks and power-ups put with the Objects tool now block ants and work in the game, as the ones in the game's maps do (before, the map file did not say that they are solid).
- A hill, a team's starting ants or a food pile are never taken away by an object, big or small: the page says what is in the way and puts nothing. The list shows the shape of each object.
- Putting objects does not change what the check says about a map.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/3906df6f...54418041)

## v0.12.75 - 2026-10-17 - Map maker: hills, ants and food (E3a)

**For players:**
- The map maker page (not linked from the site yet: /mapmaker.html) can put hills, starting ants and food now. Choose Hills, Ants or Food (or press H, A or F), choose a team (Green, Red, Blue or Black), and tap or click the map: a whole hill appears, a team's ants stand together where you tapped (you choose how many), or a food pile (you choose how many stages). Put one again and it moves; the eraser takes a hill, a team's ants or a pile away; undo and redo work for them like for drawing.
- The check says what is still missing, for example "Blue has no hill", and says nothing of it once the hill is there.
- On a phone the team, the number of ants and the stages are on one button that opens a list.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/402c19c5...23feff4e)

## v0.12.74 - 2026-10-17 - Replays: the scrub bar should jump to where you tap

**For players:**
- The replay bar: on a phone in fullscreen, tapping it should now jump to that spot instead of the start (a phone is no longer given the mouse lock).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/fa324b31...a1dbecfd)

## v0.12.73 - 2026-10-17 - Map maker: draw and undo (E2)

**For players:**
- The map maker page (not linked from the site yet: /mapmaker.html) can draw now: paint ground and put rocks and power-ups with a click, a tap or a drag, take things away with the eraser, fill a field, choose a brush of 1, 3, 5 or 9 cells, and undo and redo as often as you like (Control or Command with Z; a letter chooses a tool).
- On a phone the tiles are one button that opens a list with their names, the buttons fit in both directions, and an upright phone scrolls when the screen is short.
- The position of the mouse or finger shows under the map, and the check of the map runs again by itself after a change. Hills, starting ants and food come next.
- A map that you drew on is handed over as a new, valid file when you press Download, and the page asks before it lets you leave a map that you have not downloaded.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/83133ebe...89e37e86)

## v0.12.72 - 2026-10-17 - Map maker: the editor starts (E1)

**For players:**
- A first map maker page (not linked from the site yet: /mapmaker.html): make a Classic or Extended map, open a map file, look around, check it, download it. Drawing comes next.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/b39a8c8b...4e1327d9)

## v0.12.71 - 2026-10-17 - Tests: faster and steadier (round 2)

**For players:**
- Under the hood: the tests are faster and steadier on slow or busy machines (the flood-control checks follow the time that really went by, the match history's timing test counts work instead of milliseconds, and the test servers no longer look up host names); nothing changes in the game or on the site.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/edf7086a...e86c5501), [the tests' timing rules](docs/TESTING.md)

## v0.12.70 - 2026-10-17 - Computer players fight in 187 mode

**For players:**
- **The computer players fight in 187.** They go for your ants, a few on each, send hurt ants home to heal and come back, and wait at your hill when none can be reached.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/38a4311d...1d896268), [the 187 bots](docs/BOT_187.md)

## v0.12.69 - 2026-10-17 - Extended maps: the browser module does conversions and the per-kind check

**For players:**
- Under the hood: the map code in the browser module can now make a blank Extended map, turn a Classic map into an Extended one and back (with plain reasons and notes, each with a stable code), check either kind and tell a page the limits of each; a page can refuse an older module. No page uses it yet; nothing changes in the game or on the site.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/15c56b6b...0372a2e2), [the map core in the browser](docs/MAP_FORMAT.md#in-the-browser)

## v0.12.68 - 2026-10-17 - Extended maps: the checker per kind and the 90-row limit for new Classic maps

**For players:**
- Under the hood: the map checker now answers for each of the two kinds of map (a Classic map, and an Extended map that only the new version loads), and a new Classic map is limited to 90 rows, because the original game's ants cannot enter or cross the 91st row of a taller map; a taller map is an Extended map. No map of the library is taller than 81 rows. Nothing changes in the game or on the site; the map maker's limits and checks are prepared.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/28773bfd...366d3ab8), [the checker per kind](docs/MAP_FORMAT.md#the-checker-per-kind)

## v0.12.67 - 2026-10-16 - 187 mode: the out-of-ants line, the start line and the results page on screen

**For players:**
- **187 on screen.** In a 187 game (food-free, kills decide; the room leader picks it under Game type) the chat log begins with "Game started! Most kills wins." and says "<Name> (<Color>) is out of ants!" once, to every player, when a team has no ant, egg or hatch left. The results page has its own look: the headings Kills, Ants lost and Ants left, and a line at the top that says how the match ended ("<Name> is the last colony standing!", "Time is up. The most kills wins!" or "Nobody is left. The most kills wins!"), also on the 4:3 page, where it takes two lines. Games of the original's rules look as before.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/1a5da9ba...348e8741), [187 in the rules](docs/GAMEPLAY.md)

## v0.12.66 - 2026-10-16 - Lobby: the leader can pick the game type (187)

**For players:**
- **The lobby has a Game type control.** The leader picks the game type from a list under the map (a drop-down on a computer, a bottom sheet on a phone): Highest score, the original game, or 187, a fight with no food where the most kills wins. The room shows a 187 tag by its name, and the other players see a read-only "Game type" line that changes as soon as the leader picks. A new room starts with the leader's last choice in that browser.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/a011f5d6...cb6892ad), [Game type in the lobby](docs/PLAY_IN_BROWSER.md#game-type)

## v0.12.65 - 2026-10-16 - Extended maps: the map file version 9 in the core

**For players:**
- Under the hood: the map code and the map tool now know a second kind of map, the Extended map (a new map file version for bigger and taller maps), and can convert a map between the two kinds; nothing changes in the game or on the site, and the game still reads the classic maps only.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/c7c46030...895da29b), [the Extended map](docs/MAP_FORMAT.md#version-9-the-extended-map)

## v0.12.64 - 2026-10-16 - Spelling follow-up: the history address and record say color

**For players:**
- Under the hood: the match history's address and records now say color; addresses, bookmarks and pages keep working as before, and nothing changes in the game or on the site.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/a539116a...84e7e736), [the older spelling](docs/REPLAYS.md#the-match-history)

## v0.12.63 - 2026-10-16 - Spelling: color, not the British spelling

**For players:**
- **Spelling: the site and the game now write "color".** The lobby, the team and color words, the map pages, the match list filter, the chat help and the game's own screens all use the American spelling. Nothing else changed: your saved settings, recordings and bookmarked addresses keep working, and the game plays exactly as before.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/06ea886a...f58c0990)

## v0.12.62 - 2026-10-16 - Map maker: the map core runs in the browser

**For players:**
- Under the hood: the map code (reading, checking and writing a map) now also runs in the browser; nothing changes in the game or on the site.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/0731389a...0d6c0ff6), [the map core in the browser](docs/MAP_FORMAT.md#in-the-browser)

## v0.12.61 - 2026-10-16 - Replays: the full screen button fills the screen with the game

**For players:**
- **On the page of a match, the Fullscreen button of the replay now makes the game fill the screen:** before, it made the whole replay box fullscreen, so the picture stayed small at the top with the controls under it. Now the game itself fills the screen, and the controls lie over it as in a replay opened on its own.
- **On a phone, a swipe up from the bottom edge brings the controls back at once** in that fullscreen replay (the first swipe was lost before).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/2fae1de9...86c50d1b)

## v0.12.60 - 2026-10-16 - Walled maps: the bots open a pile behind a bomb field

**For players:**
- **On walled maps the computer players now get food out of piles that lie behind a bomb field:** a Medium or Hard bot on POPcOrN and maps like it sends one ant through the bombs, the fire wall and the water to open the pile, and then carries the food home. Alone on POPcOrN a Hard bot scores about 2,300 points a game instead of 580, and it now scores in matches there too (about 1,140 points a seat one against one, 570 with four bots; before, nothing). Other maps of this kind score more too; maps without such a pile play as before ([why and how](docs/BOT_GATES.md)).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/4fccf668...b44f54aa), [walled maps](docs/BOT_GATES.md)

## v0.12.59 - 2026-10-16 - Walled maps: the bots leave the map's own bombs to their plan

**For players:**
- **On walled maps the computer players no longer waste their ants on the map's own bombs:** a Medium or Hard bot working its way to walled-off food (POPcOrN) leaves the map's bombs to its plan instead of sending ants to set them off or to defuse them one by one. Alone on POPcOrN a Hard bot now scores about 620 points a game instead of 415 (Medium 444 instead of 273); in a match on POPcOrN the bots still score nothing ([why](docs/BOT_GATES.md)).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/39cbcd11...21cd9a95), [walled maps](docs/BOT_GATES.md)

## v0.12.58 - 2026-10-16 - Bots go for the contested food first

**For players:**
- **Medium and Hard computer players eat the food that other teams also reach before the food at their own base**, because what they leave there the others eat. With four bots on TREASURE they now start with the centre and keep most of the Cheerios next to their base for later. Food far across the map does not pull them away from their own, and an enemy at their own food makes them hurry to eat it. Easy is as before.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/4ac5df8d...ecc3f933), [the food order](docs/BOT_FOOD.md)

## v0.12.57 - 2026-10-16 - Under the hood: a map can be checked for the original game

**For players:**
- Under the hood: a map can now be checked for whether it works in the original game; nothing changes in the game.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/3c5e7824...a92c2a3e)

## v0.12.56 - 2026-10-16 - Rematch: back to the lobby with one button

**For players:**
- **Back to the lobby:** when a match started on the front page is over, a button under the results takes you back to the same room: same link, host, colors, bots, teams and map.
- **Your seat waits** while the room is open (three hours at most). START waits for whoever is not back, or the host removes them; if nobody from the match is in the room for a minute, the room goes on without the waiting seats. Desktop and phone-app players join by the code.

**Rules / network:** none.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/bac7e3b0...05109ff0)

## v0.12.55 - 2026-10-16 - Under the hood: the map files can be written

**For players:**
- Under the hood: the map files can now be written, not only read; nothing changes in the game.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/469bd142...ee0d2604)

## Older versions (v0.12.53 and before)

Every release before v0.12.54, and the long notes of v0.12.53, v0.12.52, v0.12.51, v0.12.50, v0.12.49, v0.12.48, v0.12.46, v0.12.45, v0.12.44, v0.12.43, v0.12.42, v0.12.41, v0.12.40, v0.12.39, v0.12.38, v0.12.37, v0.12.36, v0.12.35, v0.12.33, v0.12.32, v0.12.31, v0.12.30, v0.12.29, v0.12.28, v0.12.27, v0.12.26, v0.12.24, v0.12.23, v0.12.22, v0.12.21, v0.12.20, v0.12.19, v0.12.18, v0.12.16, v0.12.15, v0.12.14, v0.12.13, v0.12.12, v0.12.11, v0.12.10, v0.12.9, v0.12.8, v0.12.7, v0.12.6, v0.12.5, v0.12.4, v0.12.3, v0.12.2, v0.12.1, v0.12.0, v0.11.0, v0.10.0, v0.9.1, v0.9.0, v0.8.3, v0.8.2, v0.8.1, v0.8.0, v0.7.0, v0.6.0, v0.5.1, v0.5.0, v0.4.0, v0.3.0, v0.2.0, v0.1.3, v0.1.2, v0.1.1, v0.1.0 and v0.0.99 - v0.0.90, are only in the git history.
