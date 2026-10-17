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

## v0.12.53 - 2026-10-15 - Fewer lost points on walled-in maps

**For players:**
- **Ghetto and ANTS_PARTY:** a computer player whose worker starts beside the food harvests at once instead of waiting two minutes for a plan it does not need. Ghetto, one against one: Hard 2,291 -> 2,741, Medium 1,593 -> 2,240 (both seat pairs). ANTS_PARTY: Hard 26,952 -> 28,831, Medium about 6 percent less.
- **!-Midz-!:** a Hard bot gives up a trip to a power-up only when an enemy really gets there first: 343 -> 645. Medium keeps its old rule; without it, it lost a quarter of its score on Ghetto.
- **A bomb of the bot's own color that closes the way is now defused** instead of being ordered again for minutes.
- **Dustbin and FKD Deathmatch** score a little less than before v0.12.49 (docs/BOTS.md); POPcOrN against one Hard opponent loses its rare 50 points.

**Rules / network:** none.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/f21844cc...8cc8d538)

## v0.12.52 - 2026-10-15 - Lobby: far fewer renames, map changes and color moves lost to a slow moment

**For players:**
- **A rename, a new map or a color move in the lobby is far less likely to be lost to a slow moment.** The page asked for these at exactly the pace the server allows, and the server drops a request it handles a few milliseconds early without a word, so a change could vanish from the screen (a fourth rename in a few seconds, or holding the next-map arrow). The page now keeps a little under the limit, as the chat does: two renames or removals at once and then one every 1.25 seconds, five map changes or color moves at once and then about three a second. A hiccup of up to about a third of a second for a map change or a color move, and of up to one and a half seconds for a rename or a removal, no longer loses one; a longer hiccup still can.

**Rules / network:** none. Network protocol 21 and the server are as before; only the pace of the page's requests changed.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/60fb1ebc...44e5f759), [the page's pace](docs/PLAY_IN_BROWSER.md#chat-in-the-lobby)

## v0.12.51 - 2026-10-15 - Match stats: over time and activity

**For players:**
- **A match page tells the story of the match, not only its totals.** A band of highlights sits under the result (first blood, who led and when, the comeback, the busiest player). **Over time** charts the score and the kills of every seat (a point every 15 seconds), with a strip of who had the most points (a side of two is named), first blood marked, and the facts under them: the last kill, the changes of the lead, the best run of kills, a seat that ran out of ants. **Activity** shows every seat's orders in every minute as a strip of squares, the orders by kind and the busiest seat and minute.
- The server counts these stats from the recording at the same time as the totals, and it counts them in the background for the matches of the last 30 days that were counted without them, one at a time. A match recorded by an older version, one whose recording was removed before it was counted, and a match that has just ended show their totals and one line that says why there is no more.
- Who killed whom, damage, raids, power-ups, food and bombs are counted and kept for every match too; their sections of the page come next. The note of a recording from an older version now says that the **totals** are complete.

**Rules / network:** none. The simulation, the hashes and network protocol 21 are as before; the stats are read from the recording while it plays, and the engine is not touched.

**Fixes:**
- A match on a player's map (a community map) had no record, as the server looked for its map in the official folder only; it is counted now.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/1bf52364...198d2243), [the match pages and the replay pass](docs/REPLAYS.md)

## v0.12.50 - 2026-10-15 - Chat in the lobby

**For players:**
- **The front page has a chat now.** A **Chat** box under the colors lets the people in a room talk before START: type a line (up to 100 letters, digits and signs) and press Enter or Send, and everybody in the room reads it in the color of the sender's seat. It is the chat of the game's own waiting room, so a player of the desktop game in the same room reads it too.
- A small line tells who comes and goes ("Sam joined as Red."). A line that arrives while the box is out of view (below the window, or under START on a phone) also shows as a short message at the bottom of the page, at most one every 4 s (the box has every line).
- The box keeps a little under the room's pace (four lines at once, then one every 1.25 s) and asks for a little slower when you are faster (your line stays in the field). If the room does not say a line back within 3 s, or the connection drops first, the box says "Your last line may not have arrived" and puts the text back in the field. What the game cannot show, an accent or a smiley, is left out as you type. On a phone the field is big enough to tap and the keyboard does not hide it behind START.
- Nothing is saved: a reload starts an empty box, and when START takes everybody to the game the lines stay behind (they are not in the countdown's chat; that is a later change).

**Rules / network:** none. The server already passed a page's chat line on; network protocol 21 is as before.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/6cb8abd3...97aea54a), [the notes](docs/PLAY_IN_BROWSER.md#chat-in-the-lobby)

## v0.12.49 - 2026-10-15 - Computer players play maps whose food is walled in

**For players:**
- **Medium and Hard computer players now play maps whose food is walled in.** On HELLRAISER (rock walls round a vault of food) and POPcOrN (fire walls, a lake, a field of bombs) they used to stand still with a score of 0. A bot that finds no food it can walk to now puts a fire wall out with a Fire Ant, defuses a bomb that plugs a tunnel, or has a Bomber hop over a wall with its own bombs, takes the food and carries it home.
- **Only where no food is in reach, and not Easy.** The bot decides once, at the start of a match. Every original map, ISLANDS and SMALL (with their ferry play), Bombz Away and the 456 community maps with food in reach play exactly as before. On 71 of the 84 community maps with a walled-in seat the computer players now play differently: over Hard 1v1, seeds 1 to 4, the scores add up to 102,535 where they were 85,988; 9 maps score more, 5 less (ANTS_PARTY 30,294 -> 27,825, Ghetto 3,706 -> 2,475, FKD Deathmatch 3,369 -> 2,400, !-Midz-! 675 -> 375, Dustbin 324 -> 45).
- **What to watch on HELLRAISER:** the Bomber plants a bomb behind itself, hops over the wall into the vault, bites the food, hops out with it and banks it. Against another computer player the food is a fight, and a thief or a Combat Ant can take it back.
- **POPcOrN works only in part:** a lone Hard bot banks food there in some games, two bots seldom do.

**Rules / network:** none. No rule, no hash and no protocol number (protocol 21) changed; the computer players are outside the simulation.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/2d94b705...21c1ecd8)

## v0.12.48 - 2026-10-15 - Legal notice

**For players:**
- A new **Legal** page (`/legal.html`) says in plain words that the game is free to play and use, comes with no warranty, and that its authors are not liable for problems from using it (online play, the computer players, the website, the servers). It says the online servers may be down or restarted at any time, that this is an unofficial fan remake of the 1998 game and not endorsed by the original authors or owners, and it links the MIT License of the source code and the third-party credits.
- Every footer now ends with a **Legal** link (the front page, the game page, the match pages and the changelog); it opens in a new tab, so a match that runs is not left. The README's License section opens with the same notice.

**Rules / network:** none. Only the new page, the footers' last link and the README changed; network protocol 21 is as before.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/b843839c...b704515e)

## v0.12.46 - 2026-10-15 - Every match has a page: stats, replay and live games

**For players:**
- **Every match has a page now.** **Matches** (a link in the footer of every page, a button at the top of the front page and of the game page) lists every match played here, newest first: the map, who played (computer players as "Pip (bot, Hard)"), who won and the scores, the length and when. Search for a map or a player, show only the matches with people or only the computers' matches, and narrow it by format, result, color, mode and whether the replay can still be watched. The list comes in pages of 25 and the address of a view can be sent to somebody.
- **Open a match for all its stats and its replay.** Every seat's score, ants killed, lost and hatched, bombs set and defused and fires lit, a bar for each against the best seat, the share of the score, the total of a team, and who left early. **Show replay** opens the game on that page and plays the match; nothing of the game is downloaded until you press it. A match recorded by an older version, or whose recording is more than 30 days old, still shows its stats ("Older version", "Replay removed"), with the file to download where it is still kept.
- **Matches that are being played have their own page,** **Live now**, and the Matches page shows a button "N live now" while one is on. The old addresses `/watch.html` and `/matches` lead to the new list.
- **Stats on the replay page:** a **Stats** button (a row under the controls on a phone) shows the final numbers of the match you are watching. It is closed until you open it, so it does not tell you who won.
- **What is kept, said plainly** on the front page and in the name card: recordings are kept for 30 days; results and names are kept for good and are public.

**Rules / network:** none. No rule, no hash and no protocol number (protocol 20) changed; the pages read what the server already keeps (`GET /history`).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/2d94b705...ec086294)

## v0.12.45 - 2026-10-15 - The bot arena container is gone from the stack

**For players:**
- Nothing changes in the game or on the site. The list of matches still always has one to watch: the game server itself starts a match of computer players every hour (`ANTS_BOT_MATCH_EVERY_MIN`, 60 by default), and anybody can watch it live.
- **For whoever runs a stack:** one container less. The extra bot arena container (`ants-arena`, which started only when the `arena` profile of `docker-compose.stack.yml` was asked for) is gone, with its image stage in `Dockerfile.server`, its loop script and its setting `ANTS_ARENA_EVERY_MIN`; the stack is the web page and the game server again. If you had started it, remove the old container once with `docker rm -f ants-arena`.
- The `bot_arena` command-line tool stays as it was: it plays computer players against each other on your own computer, and `--save-replays` still puts its matches in the server's replays folder.

**Rules / network:** none. No rule, no state hash and no protocol number changed.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/2d94b705...a366cb21), [the bot tool](docs/BOTS.md#running-bots)

## v0.12.44 - 2026-10-15 - A 30 second countdown with the chat open before an online match

**For players:**
- A match of an online room with two or more people now opens with a **30 second countdown** instead of the 5 second "Get ready to play!" dialog. It starts when everybody's game has loaded. The card shows the seconds left, who plays which color (computer players are marked) and the chat, which is open: type and press Enter (on a phone, tap the chat box) to say hello and arrange before the ants move. The status line above the chat says "Match starts in N s".
- The card has a **Fullscreen** button, so you can go fullscreen before the match starts (in the browser the page's own button, on the desktop a button on the card). The Quit button and Ctrl+Q still leave the room; clicks on the map and the keys of the match stay out of reach until the match begins.
- Rooms of one person (with computer players), games on the local network and games on this computer keep the 5 second dialog.

**Rules / network:** the simulation, its rules and every state hash are unchanged (the countdown is not a turn, so recordings and replays are as before). Network protocol 21 (was 20): the Begin message carries the seconds that the host holds the first turn, so a game of an older version cannot join a room of this one (a browser page asks for a reload). The server's `--start-countdown-seconds` (default 30, 0 = none) and a room's `countdown_seconds` set the length.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/2d94b705...58b8f468), [the countdown](docs/MATCH_COUNTDOWN.md)

## v0.12.43 - 2026-10-14 - Auto camera locks on the biggest fight and follows it

**For players:**
- The Auto camera of a replay, and of a match that you watch live, now locks on the biggest action and follows it: kills, fights of several ants, thefts and chains of bombs or fires draw it (food carried home counts a little), a stray hit or blow does not, and the picture goes along with a fight that moves across the map.
- It stays on a place at least 7 seconds and moves on only to a place that is clearly busier (1.5 times as busy; a fight that is cooling is not taken from by a place that has only just begun) or when the action has died out, and then the picture stays where it is. It glides there smoothly, with no snap, and keeps your zoom. Taking the camera by hand still wins, and the **Auto camera** button comes back to the biggest action.

**Rules / network:** none. Only the camera of the viewer changed; no rule, no hash and no protocol number (protocol 20) changed.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/a3b5692e...e3bf5a1a)

## v0.12.42 - 2026-10-14 - The changelog in pages named by version

**For players:**
- The changelog page now shows the releases ten at a time (five on a phone) in pages named by their versions, for example "v0.12.29 to v0.12.19", with Newer and Older buttons at the bottom; the long row of every version is folded behind "Jump to a version".
- A link to one release (`#v0-12-30` or `#v0.12.30`) opens the page that holds it and frames the release in gold, and the Back and Forward buttons move between the pages you looked at. Without JavaScript the page is the whole list, as before.

**Rules / network:** none. Only the changelog page changed; network protocol 20 is as before.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/079b8428...b4100330)

## v0.12.41 - 2026-10-14 - Universal gate planner, windowed web mouse lock and server hardening

**For players:**
- In a browser window, clicking the game during a match now locks the mouse inside the picture (when *Fullscreen mouse: Locked* is on) so you can pan freely at every edge without the cursor leaving the window; hold **Ctrl+Alt** (or press **Esc**) to release the mouse.
- When gate goals are switched on for a Hard computer player, it plans multi-hop power-up, fire-wall and bomb-barrier chains from any reachable land component where its ants stand, and unseals power-up doorways and shore approaches without map-specific rules.
- Hardened the web and match server stack with `/ws` connection rate limiting, read-only container and capability restrictions, parameterized database backup bindings and pinned build dependencies.

**Rules / network:** none. No simulation rule, golden hash or network protocol number changed (protocol 20).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/2b102759...fb846d3a)

## v0.12.40 - 2026-10-14 - Build date and time in the web footer

**For players:**
- The footer of the front page, the game page, the replay list and the changelog page now shows the Pacific date and time of the build in parentheses after the build id, for example `Version v0.12.40 - build b0f2a5c (2026-10-14 09:32 PT)`.

**Rules / network:** none. Only the pages' footer text changed; network protocol 20 is as before.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/b0f2a5c3...2b102759)

## v0.12.39 - 2026-10-14 - Opportunistic water pushes, vicinity kills, smart threat counters and bomb-tunnel defusal

**For players:**
- Computer players now knock enemies standing near water into the water (one punch on the shore or four tiles out with a Combat Ant, or two to four blows in succession), finish a flanked 3-hit-point enemy with three workers, ignore harmless lone fire walls and stray bombs left in open ground after the enemy leaves, and defend their gate ring against a second enemy Fire Ant even while hunting a raider near the thief hole.
- On maps with a food pile in a lake and no water channel between land islands, computer players now activate their island play to fetch a Swimmer, ferry food and dig bridges; when gate goals are switched on for a Hard computer player, it also defuses bomb-plugged tunnels with a Bomber and routes around map bombs on power-up trips.

**Rules / network:** none. No rule, no hash and no protocol number changed; the bots stay outside the simulation.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/07ce107a...0b9a5071)

## v0.12.38 - 2026-10-13 - Bot names on the host's card and in the START line

**For players:**
- In the front page's room, the host's card for a computer player now shows the bot's name (for example `Pip (bot, Hard)`) in small text under the level switch, and the line under START tells the players by name: "You, Sam, Pip (bot, Hard) and Wren (bot, Medium) play on Treasure." Other players' cards already showed the names. A room whose bots have no name yet (a game on the local network, a local game) looks as before.

**Rules / network:** none. Only the page's text changed; network protocol 20 is as before.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/f50b7a1b...fe3cd495)

## v0.12.37 - 2026-10-13 - Computer players can plan around fire walls and bombs (off)

**For players:**
- Nothing changes on screen or in play: the new play is off in every level. A Hard computer player (when it is switched on for a test) whose food is walled off by fire walls or bombs, as on `POPcOrN`, now gets the right ant first: a Fire ant to put a wall out, or a Bomber that bombs itself over it to a power-up or to the food, with the real rules of the bomb throw. It gives a step up after 90 seconds, an attempt after 4 minutes, tries two more times and then plays on ([the plan](docs/reverse_engineering/bomb_gates.md)).

**Rules / network:** none. No rule, no hash and no protocol number changed; the bots stay outside the simulation.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/8ab3344c...a4d4eecf)
## v0.12.36 - 2026-10-13 - Names of the computer players in the server's rooms (protocol 20)

**For players:**
- Computer players now have names. In a room on the game server a bot is called, for example, `Pip (bot, Hard)` (a random short name, then `bot` and its level) instead of `Bot (Hard)`: in the waiting room, on the results, in the replay list and in the match history. The front page's room shows the name of a bot as soon as the host picks it for a color, before START, and the bot keeps that name in the match. The test bots are `Pip (bot, idle)` and `Pip (bot, worker)`. Matches kept before keep their old `Bot (Hard)`.
- A name that looks like a computer player's is not for people: a name that ends with `(bot)` or `(bot, ...)` is refused like the names that start with `Bot (` (start menu, front page, game page, rooms).
- A game on the local network and a game on your own computer still say `Bot (Level)`; the in-game label is unchanged for now.

**Rules / network:** network protocol 20: a v0.12.35 game cannot join a v0.12.36 server or room, and the other way round. Nothing in the rules or the replay format changed.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/b09bc246...26b4a5de)

## v0.12.35 - 2026-10-13 - Computer players can read the fire-wall and bomb gates of a map (analysis only)

**For players:**
- Nothing changes on screen or in play. The computer players' map analysis now also finds what stands between a hill and its food and power-ups when fire walls or bombs are in the way (`POPcOrN`, `Bombz Away`), and which ants get past: a Fire ant, a Bomber that defuses, or a Bomber that bombs itself over a wall. No computer player uses it yet ([the tables](docs/reverse_engineering/bomb_gates.md)).

**Rules / network:** none. No rule, no hash and no protocol number changed.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/1e9d659d...6a085827)

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
- 13 more maps of the players are gone from the map list, because every map now has to have a hill and starting ants for all four colors: the nine 1 v 1 maps (`!aLkAtRaZ1v1bOmBeR`, `!aLkAtRaZ1v1tHiEf!`, `!aLkAtRaZ_1v1!`, `ANTS SOCCER`, `ANTSSOCCER`, `GRiM-FAiT`, `ShowDown`, `aLkAtRaZ1v1cOmBaNt`, `aLkAtRaZ1v1cOmBaNtn`), `Fighter Dream 2` and `junk yard` (ants for all four colors, but a hill missing for some) and the two arenas without any hill, `DEATH WAR!!!!!!!!!!!!ARENA 3` and `HELLRASIER ARENA`. 534 maps of the players remain; the lobby's list and the map picker are shorter by the same 13.
- A kept replay (30 days) of a match played on one of these maps can no longer be watched.
- A map of the players is only taken in when all four colors have a hill and starting ants, and the check and the test of the map list now fail if one is added that does not.

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
- 11 maps of the players that could not be played are gone from the map list: `Home Bound`, `IsLaNd FiRe BoOm` and `Thief craker` (no ants for any color), `K-P`, `Pulse2`, `Pulse3`, `QuItErS`, `firewar`, `hellzone` and `WAR TIME` (a color with a hill and no ants), and `hell2` (a map of water: every ant drowns in the first three seconds). 547 maps of the players remain.
- A kept replay of a match on one of these maps can no longer be watched.
- A map of the players is only taken in when every color it claims has ants, and a test now fails if one is added that does not.

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
- **Hills and start ants follow the color of their tile.** Seven maps lost a hill or swapped two colors because of damaged names (CRISS CROSS and Conquer have four teams now).
- Food piles are told by their tile, a food pile whose next stage has no picture in the map's list vanishes as in the original, and stray objects in a map's list of start positions are ignored and no longer drawn.

**Rules / network:** network protocol 19 (a v0.12.9 game cannot join); replay rules 5 and 6, older recordings stay under "Earlier versions".

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/aec2772a...6bc5cf52); how and why: [the report](docs/reverse_engineering/map_loader_fidelity.md)

## v0.12.9 - 2026-10-11 - The bombs of the community maps work

**For players:**
- **The bombs of 208 community maps work.** The original's bombs, laid out in the map file (`Bombz Away` is a floor of them), were invisible rocks. Now they show in their colors and the first ant that steps on one sets it off (2 hit points, a throw of 4 tiles). Each bomb has an owner team named in the file (mostly the first player): that team's ants and its ally walk around it. Other maps play as before.

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
- **The front page is now the lobby, and the only way to play.** Open it and your room is ready: a code of six letters and numbers (like `k7m 2xq`), a link to send, the four colors and the map. Players who open the link ask for a name first, take the next free color and show up at once. The host drags a player onto another color to move them (a taken color swaps places; on a phone, tap the player's dots and then the color), gives a free color to an Easy, Medium or Hard computer player or to nobody, picks the map, makes teams with three or four players, removes a player (it asks first) and presses START for everybody; everybody can rename themselves with the pencil. A room lasts while anybody is in it: when the host leaves, the player who has been there longest becomes the host; after a match, Leave Game takes you to the front page, where a new room is ready. **Have a code?** joins a room by its code, and a link to a room that is gone, full or already playing starts a room of your own, with a line that says why. A strip at the top says what happens (the host left, the match did not start, the connection is lost and your color is kept for a minute, a newer version is out). The old one-card page, the **More ways to play** button and the testing page behind it are gone; shared links (`?room=<code>`) and the game's own addresses (`?join=...`) work as before, and the old addresses show the front page.
- **Four picture shapes, changed while you play.** The buttons under the game (Classic 4:3, 16:10, 16:9, 21:9) switch at once: no reload, no "Leave the match?" question, and a joined match keeps its seat; a wider or taller picture shows more of the map. 16:10 and 21:9 are new (`?aspect=16:10` and `?aspect=21:9` in the browser, `--aspect` on the desktop), and a small "fills your screen" tag stands under the shape nearest to your screen. The front page's Screen selector has all four and hands the shape on to the game. In 16:10 and 21:9 the setup screen, the room, the loading screen, the help and the results are for now the 16:9 page, centred with black around it; the match fills the whole picture.
- **Watch matches, also while they are played.** The server keeps the matches that ran 30 seconds or more for 30 days (up to a size limit); the list is public and shows the names the players typed (no address, room code or chat), and the name screens say so. The front page's header has a **Watch matches** button and the game page has it after Menu (from a 1140 px window; in More on a phone), and everywhere the footer link; it opens a new tab, so a match you are playing goes on. The page lists the matches being played ("Live now") above the earlier ones; **Watch** plays one in the game page with the whole map showing: jump anywhere on the timeline, pause, speed it from 1/2x to 8x, go fullscreen, copy the link or download the file. A live match shows about 10 seconds behind the players, with a red LIVE mark and "Jump to live", and becomes a replay of the whole match when it ends. The server plays a match of computer players every hour ("Bot (Medium)" or "Bot (Hard)"), so there is usually something to watch.
- **Computer players fight, stand their ground and cross the water.** At every level they go for each other's ants once they have nothing left to harvest (Easy later and only with the better odds, Hard soonest); Medium and Hard light fire walls round the gate of their best opponent, a Bomber lays bombs round the food an opponent works and in front of his gate (a Hard bot keeps up to ten mines at a pile an enemy works and twelve on the enemy's lanes, a Medium bot eight, never on its own carriers' way), and every level hunts a Fire or Bomber Ant that comes near its hill. A player who is losing fights harder, the further behind the more: earlier, on worse odds, at the leader's ants first, with its Combat Ants off the food, with bombs at the leader's piles and in front of his gate, and (Medium) fire walls from minute 1.5 instead of 4.5; a bot hundreds of points behind sends its typed ants at the leader's ants after three minutes, and one that is behind starts mining the enemy gate sooner. They bank about as much food as before against players who do not fight. A bot whose gate is fired in by an enemy Fire Ant sends its own Fire Ant at it; at Hard, an ant without food that blocks the ramp steps aside for a carrier; two thieves on one hole no longer jam. On ISLANDS they get their Swimmers across sooner and lose fewer ants: the Bomber flies over with the crew, the team on the far side almost always gets all three Swimmers, and a Hard player takes a Swimmer through the Fire power-ups of a corner row with one ant and exactly timed clicks.
- **The desktop game records its matches.** Every match it plays, against computer players or in a room, is saved in a `replays` folder next to its settings (one that you leave early is kept if you gave an order or it ran a minute). A new tool, `replay_tool`, plays a file back with no window: `verify` says whether the match plays out the same, `orders` lists every order with its time, place and the type of ant that got it. The browser game does not record.

**Rules / network:** network protocol 17, so a game of an earlier release cannot join (reload the page once after the update): a match has a game mode, carried by the room's Start and Room messages, the leader's plan, a server room's `"mode"` and the game's `--game-mode`, and a replay of mode 187 has its own rules number; the original's game plays exactly as before (a switched player's state hash after every tick is the one of a match that was never switched). Recordings now say which simulation they need (`sim_rules`: 1 for the original's game, 2 for game mode 187), so a later protocol change that leaves the rules alone no longer strands them.

**Fixes:**
- The front page's four ants are drawn in the game's own team colors: the red ant no longer has purple legs and a bright rim round its eyes.
- A player can take a seat back up to twelve times a minute (it was three), so a few lost connections or reloads in a row keep you in the match.
- The replay player's REPLAY (or LIVE) tag sits just above the scrub bar (about 25 pixels of height), not over the picture, and in fullscreen above the bar's left end, going with the bar; there a finger on the picture, a tap or a drag of the map, no longer brings the bar up (a tap or swipe up along the bottom edge does, and a swipe down on the bar, or a tap on the small tab on its top edge, hides it at once, also while the match is paused), and the first time the bar goes away after a finger was used, a note says how to bring it back; a mouse works as before.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/e10047d...7017ff3), [view and HUD](docs/VIEW_AND_HUD.md), [play in the browser](docs/PLAY_IN_BROWSER.md), [network port](docs/NETWORK_PORT.md)

## v0.12.0 - 2026-10-08 - The server's lobby rooms (network protocol 16; no page uses them yet)

**For players:**
- **Nothing on screen changes in this release.** It teaches the game server to keep a room open before a match starts, which the lobby page that comes next will use: you will land in a room the moment the front page opens, send one link, and the host will set the colors, the computer players, the teams and the map while everybody waits. No page or menu uses it yet.

**Rules / network:** network protocol 16, so a game of an earlier release cannot join (reload the page once after the update); no rule of the game changes. A lobby page can now ask the server for a lobby room, which holds no match yet: the player who has been in it the longest leads (the next player when the leader goes, never a computer player), and the leader's plan (the map, what each color is, the teams) and every player's name are shown to everybody. The leader can swap two players' colors, take a player out of the room, and press START, which waits (up to 90 seconds) until every player's game has opened. A color is kept for a minute after a lost connection, and a room that nobody is in closes a minute later. Three messages are new (the plan, a player's own new name, the removal); the full text is [`docs/NETWORK_PORT.md`](docs/NETWORK_PORT.md#protocol-16-the-lobby-room-the-front-page-waits-in-v0120).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/4a536a9...305ccb9)

## Older versions (v0.11.0 and before)

Every release before v0.12.0, and the long notes of v0.11.0, v0.10.0, v0.9.1, v0.9.0, v0.8.3, v0.8.2, v0.8.1, v0.8.0, v0.7.0, v0.6.0, v0.5.1, v0.5.0, v0.4.0, v0.3.0, v0.2.0, v0.1.3, v0.1.2, v0.1.1, v0.1.0 and v0.0.99 - v0.0.90, are only in the git history.
