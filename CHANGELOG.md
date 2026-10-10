# Changelog

What changed in each release of the Ants remake, newest first, a few lines each: what a player sees and, only when it changed, the rules or the network protocol. The version is the one line of the file `VERSION` (how it moves: [`docs/WORKFLOW.md`](docs/WORKFLOW.md)); every build also has a build id, the short git commit, shown by `ants --version`, `ants_server --version` and the footer of the web page. The long notes of every release up to v0.1.0, with their measured numbers and sources, are the detailed history, [`docs/CHANGELOG_ARCHIVE.md`](docs/CHANGELOG_ARCHIVE.md) (on the site: `/changelog_archive.html`).

An entry is 5 - 15 lines in a fixed template: the heading `## vX.Y.Z - YYYY-MM-DD - title`, then **For players:** (1 - 6 bullets), **Rules / network:** (only if the rules or the network protocol changed: what, and the protocol number), **Fixes:** (optional, one line each) and **Details:** (a link to the commit range). No test counts and no mutation or review lists: those belong in commit messages and documents. Work that is merged without a release number is written up in the "Changelog entry" section of its pull request's description; the pull request of the next release collects those sections, and its own, under **Next** (a section headed `## Next`, above the newest release), and `tools/release.py` turns it into the entry when `VERSION` moves.

<!--
Template of an entry (copy it, keep the labels and the order, leave out a paragraph that has nothing to say):

## vX.Y.Z - YYYY-MM-DD - title

**For players:**
- one bullet: what a player or a server owner sees or can do now

**Rules / network:** only when the rules or the protocol changed: what, and "network protocol N: a vA.B.C game cannot join"

**Fixes:**
- one line each (optional)

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/OLD...NEW), [detailed notes](docs/CHANGELOG_ARCHIVE.md)

Work that is not released yet is written in the same template under a heading that says only "## Next" (no version, no date), above the newest release;
`tools/release.py X.Y.Z "title"` turns that heading into "## vX.Y.Z - date - title" and refuses when there is no "## Next" or it is empty.
-->

## Next

**For players:**
- **The front page is now the lobby.** Open it and your room is ready: a code of six letters and numbers (like `k7m 2xq`), a link to send, the four colours and the map. Players who open the link ask for a name first, take the next free colour and show up at once. The host drags a player onto another colour to move them (a taken colour swaps places; on a phone, tap the player's dots and then the colour), gives a free colour to an Easy, Medium or Hard computer player or to nobody, picks the map, makes teams with three or four players, removes a player (it asks first) and presses START for everybody. Everybody can rename themselves with the pencil on their colour.
- A room lasts while anybody is in it: when the host leaves, the player who has been there longest becomes the host and everybody is told; after a match, Leave Game takes you to the front page, where a new room is ready. **Have a code?** joins a room by its code, and a link to a room that is gone, full or already playing starts a room of your own, with a line that says why.
- The page tells you what happens in a strip at the top: the host left, the match did not start, the server has no place, the connection is lost (your colour is kept for a minute), the room was opened in another window, a newer version is out.
- The old one-card page (Friend seats, Sit here, one invitation for each Friend) is gone.
- **The map selection screen is the only way to play.** The **More ways to play** button and the testing page behind it (every colour played by yourself, in frames of the page or in windows of their own) are removed, and so are the old addresses that opened it (`?map=`, `?room=` with its choices, `&play=here`): they show the front page like any other address. Links that were shared (`?room=<code>`) and the game's own addresses (`?join=...`) work as before.
- **Change the shape of the picture while you play.** The buttons under the game (Classic 4:3, 16:10, 16:9, 21:9) switch at once: no reload, no "Leave the match?" question, and a joined match keeps its seat and its room. A wider or taller picture shows more of the map.
- **Two new shapes:** 16:10 for laptops and 16:10 monitors, 21:9 for ultrawide monitors (`?aspect=16:10` and `?aspect=21:9` in the browser, `--aspect` on the desktop). A small "fills your screen" tag stands under the shape nearest to your computer's screen.
- In 16:10 and 21:9 the setup screen, the room, the loading screen, the help and the results are for now the 16:9 page, centred with black around it; the match fills the whole picture.
- **The front page's Screen selector has the four shapes too:** Classic 4:3, 16:10, 16:9 (the default) and 21:9, with the same "fills your screen" tag under the shape nearest to your computer's screen (a computer with a mouse). A shape you picked in a game is shown there, and START, a link to join and every game link hand the shape on to the game; before, a 16:10 or 21:9 showed as 16:9 on the front page.
- **One link to the matches: Watch matches.** The front page's two footer links, Watch live and Watch replays, are one, and the front page's header now has a **Watch matches** button at the right (on a phone, on the logo's row), the same page that lists the matches being played above the earlier ones. The game page has the button in its header after Menu (from a 1140 px window; in More on a phone; between the two the footer link is the way, so that the header stays one row) and the footer link; they open a new tab, so a match you are playing goes on. The list's tab is titled Watch matches.
- **Computer players fight.** At every level they now go for each other's ants once they have nothing left to harvest (Easy later and only with the better odds, Hard soonest). Medium and Hard light fire walls round the gate of their best opponent, and a Bomber lays bombs round the food an opponent works and in front of his gate; every level hunts a Fire or Bomber Ant that comes near its hill. A player who is losing fights harder, the further behind the more (Hard soonest, Easy latest and gentlest): earlier, on worse odds, at the leader's ants first, with its Combat Ants taken off the food, with bombs at the piles the leader works (Hard) and in front of the gate (Medium and Hard) and, at Medium, the fire walls from minute 1.5 instead of minute 4.5. They bank about as much food as before against players who do not fight, and the order of the levels is the same.
- **The replay player: the tag out of the picture, and a fullscreen bar that stays away.** The REPLAY (or LIVE) tag no longer lies over the game: it sits just above the scrub bar (about 25 pixels of height for it), and in fullscreen above the bar's left end, going with the bar. In fullscreen a finger on the picture, a tap or a drag of the map, no longer brings the bar up; a tap or a swipe up along the bottom edge does, and a swipe down on the bar, or a tap on the small tab on its top edge, hides it at once, also while the match is paused. The first time the bar goes away after a finger was used, a note says how to bring it back. A mouse works as before.

**Rules / network:** none: the simulation and the protocol are untouched (a switched player's state hash after every tick is the one of a match that was never switched).

**Details:** [view and HUD](docs/VIEW_AND_HUD.md), [play in the browser](docs/PLAY_IN_BROWSER.md), [network port](docs/NETWORK_PORT.md)

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

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/09961f8...ec4906c), [the notes](docs/audit/persist_notes.md), [Network Port](docs/NETWORK_PORT.md)

## v0.5.1 - 2026-10-05 - Every page in the front page's look

**For players:**
- **Every page in the front page's look:** the game page, the changelog pages and "Sprites and sounds" now have the front page's clay, thin green frame, teal buttons and black boxes, the "ants!" logo (a link back to the front page; on the game page it asks first while a match runs, as Menu does) and a Play button. The game page's loading screen shows the logo and a teal bar, and on a phone its header is one row (the logo, Menu, Fullscreen and a More button). Nothing about the game, its keys or its addresses changed; the picture is 3 to 7 percent smaller at common window sizes, because of the frame's room.
- **Phones:** "Sprites and sounds" shows its animation details and its sprites table (they were cut off) and keeps its Play button; every page fits from 320 to 1600 px wide with no sideways scroll.
- **A way back from a shared link:** the name step that a shared room link opens has a "Back to the front page" link.

**Fixes:**
- The two changelog pages link to each other (the short page's links to the detailed history opened GitHub's file view).
- A bullet after a blank line in a changelog was written outside its list.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/60dea01...8511283), [the pages](docs/audit/web_home_notes.md)

## v0.5.0 - 2026-10-04 - A new front page in the game's own look, with game statistics

**For players:**
- **A new front page in the game's own look:** the 1998 game's menu style (the clay, the green frame, the teal buttons, the original "ants!" logo and START! button, a picture of the chosen map). Two cards: **Play vs the computer** (each opponent's level is a row of one-click buttons; Teams as before) and **Play online** (host a match or join one by its code); the help is behind "How it works". It fits phones and wide screens with no sideways scrolling, and every address, link and remembered choice works as before.
- **Game statistics on the front page:** a line under the welcome banner shows the matches being played and the players online now, and the games played today and in all: online matches of 30 seconds or more, and single-player games (a game in the browser tells the server once that it began: a count, nothing else).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/56c4c5d...2b28516), [the front page](docs/audit/web_home_notes.md), [the statistics](docs/audit/site_stats_notes.md)

## v0.4.0 - 2026-10-04 - Many zoom levels, a level for each bot, teams before the start

**For players:**
- **Many zoom levels:** the mouse wheel steps through four levels to a doubling (2, 1.68, 1.41, 1.19, 1, 0.84, 0.71, 0.59, 0.5 ...) down to the map's own limit, where the map's width or height just fills the view, so you never see past its edge; matches on the network offer the zoom-out too (it was local games only), and with Fog of War on every level hides exactly what zoom 1 hides. 2, 1 and 0.5 are exact pictures, the levels between are smoothed, scrolling moves whole screen pixels and a click picks the tile under the middle of the pointer. `--zoom` and the settings key `zoom` take any number from 0.05 to 2.
- **A level for each bot and teams before the start.** On the front page each bot (Red, Blue, Black) has its own level (None, Easy, Medium, Hard), and with two or more bots a Teams choice (Free for all, or you and one bot against the others); the desktop start menu has the same Teams choice and `--teams 0+1` does it from the command line. The teams are made with the original's own team-up at the first tick. A bot that declines a team-up says why ("Bots team up only while three or more teams play.", "You already have a teammate.").
- **Your name in single player on the desktop:** the start menu's Single player panel has a name field (the same remembered name as Join and Host), and the game is played under it.
- **Your own orders can show at once in a network match** (off by default: add `?prediction=on` to the game's address, or start the desktop game with `--prediction on`): your ants answer a click as in a game on one computer instead of a round trip later; the corner's `delay` shows what a click feels. It switches itself off for a while on a device that is too slow for it, and is off in a hidden tab, in a pause and behind the "Get ready" dialog.
- **Desktop:** `start_game.sh` / `start_game.bat` open one game with the start menu (Single player, Join, Host); the four-window test match is `--players 4`.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/9e6ed37...6744e04), [the zoom levels](docs/audit/view_fixes_notes.md), [the prediction](docs/audit/rollback_notes.md), [the bots](docs/BOTS.md)

## v0.3.0 - 2026-10-04 - Computer players that fight; the front page is the lobby

**For players:**
- **Computer players gather food, raid and fight back.** At every level a bot answers a blow on one of its ants (with one, two or three ants at Easy, Medium and Hard), sends a hit carrier home, guards its thief hole with fire walls, puts out your fire walls and defuses your bombs, and keeps its last ants out of fights; Medium and Hard also take their side's power-ups at the start, contest the middle of the map and raid the leader with a Thief.
- **Hard bots guide their carriers at the hill's gate by hand** and steal an unguarded Thief power-up for a second thief, and every bot draws one of four styles for the match (aggressive, economic, raider, defensive; `--bot 2:hard:raider` pins one), so bots do not all play alike. A bot that gets stuck (a refused click, a hill that its carriers cannot reach) backs off instead of repeating the same order.
- **The front page is the lobby:** [beta.playants.org](https://beta.playants.org) opens it, with single player in it. Players 1 plays on this computer in this tab, alone or against Easy, Medium or Hard bots; 2 to 4 host a match as before. The game page's "Play online" button is now "Menu", and every old game link still works (`/four.html` goes to `/`).
- **The map keeps scrolling when the mouse goes a little past the game's edge** in a browser window (about an inch, corners included); farther out, over a button of the page or out of the window, it stops.
- For whoever runs a server: a match in a room that holds seats survives a restart of the game server (restart records, [`docs/NETWORK_PORT.md`](docs/NETWORK_PORT.md) "Restart records"); rooms hold seats only when asked, so nothing changes by default.

**Fixes:**
- Behind the "Get ready to play!" dialog the ants are drawn again, standing, as in the original (since v0.2.0 only their hit-point numbers were).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/888e788...e57833b), [the bots](docs/audit/B4_1_notes.md), [the front page](docs/audit/web_home_notes.md), [restart records](docs/audit/persist_notes.md)

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

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/ddf134f...f29c2f9), [network notes](docs/NETWORK_PORT.md), [notes](docs/audit/B3_notes.md)

## v0.1.3 - 2026-10-03 - Deploys wait for an idle server; faster checks

**For players:**
- No change in the game itself. For whoever runs a server: the game server answers `GET /busy` with the number of matches that are loading or running and the people in rooms (two counts, no names, no codes), and the site's proxy routes `/busy` to it, so that an update can wait for a moment when nobody plays.
- Behind the scenes: GitHub can now deploy the site itself after every test has passed, and it waits for an idle server first (up to three hours); the site owner switches this on with one secret (docs/WORKFLOW.md). A staging copy of the site can run next to it (`docker-compose.staging.yml`, its pages say "staging"). The tests run in parallel (the full run in about 2.5 minutes instead of 6 to 8), the Windows builds use a compiler cache, and a release is one command (`tools/release.py`).

**Rules / network:** None: the rules and network protocol 11 are unchanged.

**Fixes:**
- CI's script and python steps report every failure instead of stopping at the first.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/b8d2603...e40591f), [workflow](docs/WORKFLOW.md)

## v0.1.2 - 2026-10-03 - Treasure is the default map; the web page opens in 16:9

**For players:**
- Treasure, the map that is played most, is the default everywhere a map is chosen for you: the setup screen highlights it (the list and its order are the original's), the desktop start menu's Host panel opens on it, the web page's Play online form preselects it, and an online room whose code names no map is a Treasure room. A choice you made and saved still wins.
- The web pages open in 16:9 again for everyone: a "Classic 4:3" choice that a browser remembered earlier is forgotten once. Pick Classic 4:3 again under the game to keep it (`?aspect=4:3` in the address still works).

**Rules / network:** None: the rules and network protocol 11 are unchanged (v0.1.0 to v0.1.2 games play together).

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/ff52c2d...74c8085), [the setup screen's deviation](docs/AUDIT_ONE_TO_ONE.md)

## v0.1.1 - 2026-10-03 - Bots wait for the start dialog

**For players:**
- Computer players no longer move during the "Get ready to play!" dialog at the start of a match. Their first orders come after it closes, when you can give yours, and they come one by one at the speed of their level: about 5.4 s into the match at Hard, 6.2 s at Medium and 8 s at Easy. This holds in a game against `--bot` or the start menu's bots, in a room on the local network and on the online server.
- The footer of the web page and `ants --version` / `ants_server --version` now name the build (the short git commit) next to the version, for example "Version v0.1.1 - build abc1234". The version in the corner plate is unchanged.
- This changelog is short: one entry per release in a fixed template. The detailed history of every release up to v0.1.0 (the old 556 KB file, unchanged) is in `docs/CHANGELOG_ARCHIVE.md` and on the site at `/changelog_archive.html`, linked from the changelog page.
- Internal: one version source (the file `VERSION`), a build id at every build, `./run_tests.sh --fast` (about a minute) with the time of every suite, `docs/WORKFLOW.md` (the three test tiers, branches and batch pushes, the version policy) with the rules of `AGENTS.md` that say the same, ccache in CMake when it is installed, and a README without per-suite counts.

**Rules / network:** The rules did not change. Network protocol 11: v0.1.0 and v0.1.1 games play together (the bots run on one machine and their commands travel as data).

**Fixes:**
- The bots could move before the players could: a bot looked on the first tick and started with a full budget, so a Hard bot could send ten orders in one tick while the dialog still took your clicks. They now wait for the same 100 ticks as the dialog and start with one order's worth of budget.
- Test 12.108 no longer pins the value of the version. It checks the format; the value lives in the file `VERSION`, and a check run by `./run_tests.sh --fast` and by CI compares the top release heading of this file, "current release" in `STATUS.md` and the version line of the README with it.
- The reverse-engineering notes said that the dialog lasts 6.0 seconds; the program keeps it up at least 5.0 s, which is what the game does, and the notes now say so.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/3e5bfb5...8a29f14), [workflow](docs/WORKFLOW.md), [notes](docs/audit/B3_notes.md)

## v0.1.0 - 2026-10-02 - Online rooms: bots fill the empty seats at START, chat in the waiting room, team chat only to allies, and mouse-wheel zoom

**For players:**
- The leader of an online room (or the host of a LAN game) can press START with empty seats and have bots fill them: "Bot (Easy)", "Bot (Medium)" or "Bot (Hard)". One person alone is enough. Choose the level in the desktop Host panel ("Empty seats at START"), on the Play online page, or with `--fill-bots none|easy|medium|hard`. Bots gather food but do not fight yet, and with Fog of War on no bots are seated.
- Everybody in the waiting room can chat (press T or click the chat box). The 16:9 setup screen has a chat box with the room's lines, and the leader sees "Empty seats at START: Medium bots". The waiting room's lines start the match's chat log.
- Team chat now reaches only you and your ally. Chat for all still reaches everybody.
- The mouse wheel over the map zooms between 0.5, 1 and 2 toward the pointer, and the middle button goes back to 1. The level is remembered, and `--zoom 0.5|1|2` overrides it. In a network match only 1 and 2 are offered, because zooming out would show more map than the other players see. On the web page the wheel and a trackpad pinch over the game zoom it.
- The web game now starts in 16:9 on every device, phones held upright included. The switch under the game still offers the classic 4:3 picture and remembers it.

**Rules / network:** Network protocol 11: a v0.0.99 game cannot join a v0.1.0 server or LAN game, and the other way round. Update the game, the server and the page together. The simulation rules did not change.

**Fixes:**
- On Windows, the server retries reading its secret file for up to half a second when another program holds it.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/b3b0adb...3e5bfb5), [detailed notes](docs/CHANGELOG_ARCHIVE.md)

## v0.0.99 - 2026-10-02 - The game is 16:9 by default: more map, a frame grown from the original's art, the web page in 16:9, a new setup screen with a map preview

**For players:**
- The desktop game opens in a 16:9 picture with a map view about twice as large (762 x 500 pixels instead of 442 x 440). The frame is the original's own art, made wider by repeating single plain lines, and the score boxes are spread along the bottom strip. `--aspect 4:3` (or `aspect=4:3` in the settings file) gives the original's picture exactly as before.
- The window opens at the largest 0.5 step of 960 x 540 that fits your display. Alt+Enter toggles fullscreen. A map smaller than the view is centred with black around it.
- The web game is 16:9 by default, in a box that fits the window, with a fullscreen button. The selector "16:9 / Classic 4:3" under the game, or `?aspect=4:3`, gives the classic picture. A phone held upright gets the classic picture in this release. The four games of the Play online page are 16:9 too.
- The setup screen of the 16:9 picture is new, with a map preview that is the game's own picture of the selected map. The classic 4:3 screens are unchanged.

**Fixes:**
- The web page downloads the game's data again when a download fails (with several games on one page it could stay at "Downloading data (0%)" for ever), and shows a card with a Reload button when the game's files cannot be loaded.
- The web guide and footer lost some spaces ("Goal:get the most points"); fixed.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/11b8561...b3b0adb), [detailed notes](docs/CHANGELOG_ARCHIVE.md)

## v0.0.98 - 2026-10-02 - Reconnect, part A: the server can hold a lost player's seat (off by default)

**For players:**
- Nothing changes for a player until a server operator switches it on, and the games do not come back by themselves yet (that is part B, next).
- A server started with `--reconnect` (or a room made with `"reconnect": true`) keeps the seat of a player whose connection is lost and pauses the match for everybody. After 30 seconds away the others can vote to go on without that seat. A player who comes back with its key is given the match again and plays on once the server has checked its game.
- After a pause of 3 seconds or more the match is held for a 10 second countdown before it goes on. New server options: `--hold-vote-seconds`, `--max-pause-seconds` (cap on the total pause, default 30 minutes), `--max-catch-up-seconds`, `--resume-countdown-seconds` and `--log-mb`. The room's status shows who is absent, the vote and the paused time, never a key.

**Rules / network:** Network protocol 10 (the Hello and the Welcome carry a key, and five messages are new): a v0.0.97 game cannot join a v0.0.98 server's room or LAN game, and the other way round. The simulation rules did not change.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/621f579...11b8561), [detailed notes](docs/CHANGELOG_ARCHIVE.md)

## v0.0.97 - 2026-10-02 - The desktop start menu: Single player with bots, Join with a code, Host an online match

**For players:**
- A desktop game started with no mode on its command line now opens with a start menu after the loading screen: Single player, Join with a code, Host an online match, Quit. The menu never shows in the web build.
- Single player: pick Empty, Easy, Medium or Hard bot for each of the other three colours. All Empty is the original's single-player game exactly. The choice is remembered.
- Join with a code: type your name and the room code (paste with Ctrl+V or Cmd+V) and Join. Every failure (server not reachable, no such room, room full, match started, another version, and so on) comes back to the panel with a clear line, and Esc cancels at once.
- Host an online match: pick the map, 2 to 4 players and your name. The menu makes the room, shows its code in large letters with a Copy button, and puts you in as the leader. After an online game the program goes back to the menu.
- The original's own screens are unchanged. `--map`, `--host`, `--join` and similar options skip the menu, `--start-menu` forces it, and `--server HOST[:PORT]` names the game server.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/2d048ef...621f579), [detailed notes](docs/CHANGELOG_ARCHIVE.md)

## v0.0.96 - 2026-10-02 - The web game starts at once and keeps playing in a hidden tab

**For players:**
- The web page no longer hides the game behind a "Click anywhere to play!" card. The game starts as soon as its data has downloaded, with the original's loading screen and quick help, and sound and music begin at the first click, tap or key press on the page.
- A network match keeps playing when its tab is hidden or its window is minimised. Before, a hidden seat was called lagging after 3 seconds and dropped after 30. Now it keeps up and the other players are not told it lags.
- A hidden page plays no sound or music, and a local game stands still while its page is hidden. A sleeping computer or a phone with a locked screen still stops the page.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/be5cc38...2d048ef), [detailed notes](docs/CHANGELOG_ARCHIVE.md)

## v0.0.95 - 2026-10-02 - Community maps play as in the original: their default ant types and power-ups by tile

**For players:**
- A map that names a default ant type has every ant of every team of that type, the starting ants and every hatch. For example, `popcorn.lvl` hatches combat ants.
- Power-ups and flower droppers are recognised by their tile and look the same on every map, whatever the map file calls them. A power-up named "." is no longer turned into a rock.
- A Bomber, Fire ant or Swimmer that is busy (planting, lighting, bridging, picking up a power-up and so on) gets the plain move cursor, and a right click is a plain move, as in the original.
- A pick-up rebuilds the panel of a selected ant every time: the text of its type and both command pedestals pop up, also when the ant takes the power-up of its own type again.

**Rules / network:** The end of every attack now resumes the saved auto-engage of any ant that still has it, as in the original. An ant that took another power-up during a Combat Ant's auto-engage walks back to where the engage began. This changes some plays of the shipped maps. Network protocol 9: a v0.0.94 game cannot join.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/f3c3d47...be5cc38), [detailed notes](docs/CHANGELOG_ARCHIVE.md)

## v0.0.94 - 2026-10-02 - Less lag: ping and delay next to the FPS counter, 50 ms turns, an adaptive buffer, and a lagging player no longer freezes the others

**For players:**
- "ping NN ms" and "delay NN ms" now show next to the frame rate in a room and in a network match, on the web too (never in a game of one machine).
- Commands reach the game about 100 ms sooner on a steady link: turns are 50 ms instead of 100 ms, and the buffer adapts from one to four turns to the link's jitter.
- A server's room no longer waits for a lagging player. The others see "Bob is lagging (12 s behind)" from 3 seconds, and the laggard sees "You are lagging" and "Catching up..." while its game runs the backlog at up to 4x. A player 60 seconds behind, or silent for 30, is dropped and told "You were away too long and were dropped from the match." A LAN host with a seat still waits up to 3 seconds for a friend.

**Rules / network:** Network protocol 8 (a new Lag message): a v0.0.93 game cannot join. The simulation rules did not change.

**Fixes:**
- "Waiting for the other players..." no longer stays on screen after a stall, and turns that piled up (a hidden window, a frozen link) are run down instead of leaving a game half a second behind for good.
- A burst of orders after a stuck connection is queued, not counted as violations that could throw the player out.
- The frame-rate counter shows the real rate below 10 frames a second (it read "10 FPS").

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/ce61aaa...f3c3d47), [detailed notes](docs/CHANGELOG_ARCHIVE.md)

## v0.0.93 - 2026-10-01 - The room leader can start early

**For players:**
- The first player to join a server's room is its leader and can press START to begin the match with the players who are there (two at least). With fewer than two the can't-go cue plays and nothing is sent. When the leader leaves, the next earliest player leads. A LAN game has no leader: its host starts as before.
- The leader's setup screen is the original's host screen: START works, and the map and Fog of War buttons show the room's choice and change nothing. The Play online page says that the first player can start early.
- Server operators: `POST /rooms` takes `"early_start": true|false` (true when left out, on for demo rooms), and the room status shows `early_start`, `leader` and `ignored_start_requests`.

**Rules / network:** Network protocol 7 (the Room message names the leader, and StartRequest is new): a v0.0.92 game cannot join. The simulation rules did not change.

**Fixes:**
- One connection could flood the server with valid messages (pings, acknowledgements, chat and so on) and use a whole core and over a gigabyte of memory without being dropped. Inboxes are bounded, every connection has a message budget, and a flooder is dropped within a second.
- Reloading the web game no longer downloads about 9 MB again: the files are revalidated and an unchanged build answers 304.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/4344c0e...ce61aaa), [detailed notes](docs/CHANGELOG_ARCHIVE.md)

## v0.0.92 - 2026-10-01 - The black bars of a wide window scroll the map, fullscreen keeps the mouse, four games in the corners of their hills, bot fixes

**For players:**
- A wide window (fullscreen on a wide monitor) no longer ignores the pointer over its black bars: pushing the mouse into a bar scrolls the map like the edge does.
- Fullscreen (`--fullscreen` or a macOS fullscreen Space) keeps the mouse inside the window while the game has focus. A windowed game never holds the mouse.
- The Play online page and the four-window start script place the games by colour: Black top left, Green top right, Red bottom left, Blue bottom right.
- Internal: a safety net for the planned widescreen work, a GCC 12 build with warnings as errors, and review fixes of the computer-player code. The classic picture and the simulation did not change.

**Fixes:**
- A mouse button released outside the window, or a finger lifted from a touch screen, no longer leaves the map scrolling on its own.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/2185be0...4344c0e), [detailed notes](docs/CHANGELOG_ARCHIVE.md)

## v0.0.91 - 2026-10-01 - The original program leaves the repository

**For players:**
- Internal: the original 1998 program (Ants.exe), its decompilation and the local tools of an installation of the original are no longer in the repository, which keeps only the data archive, the maps and the music. Nothing in the game, the server or the network protocol changed but the version number. Pulling this change into an existing checkout deletes those files from the working tree, so copy them aside first.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/64654b3...2185be0), [detailed notes](docs/CHANGELOG_ARCHIVE.md)

## v0.0.90 - 2026-10-01 - Play online: host a match, join it with a code, any map, 2 to 4 players

**For players:**
- The Play online page (linked from the game page) now hosts matches: choose one of the six maps of the original and 2, 3 or 4 players, then "Create the match". The page makes a room code, shows a link for anybody, and offers "Play here", "Open a window" and "Copy link" for each seat, plus "All seats on this page" and "All seats in separate windows".
- Join: type the code and your name, and the game opens with the first free seat. A link that names a room (`four.html?room=...`) opens its panel at once, and a reload or a bookmark comes back to it.
- The match starts by itself when every seat is taken. A room waits up to ten minutes for the others. A running match takes nobody new, and a code whose room is over makes a new room when it is used again. Rooms of this page have no computer players yet.
- Every game on the page reports its tick and state hash every 100 ticks, and the page says whether they agree ("In step").
- Server operators: the room code chooses the map and the players (`demo-[<map>-][<n>p-]<anything>`), `--demo-maps A.LVL,B.LVL,...` lists the maps a code may choose, and a demo room now waits ten minutes (it waited one minute). The stack starts with the six original maps and 12 demo rooms.

**Details:** [commits](https://github.com/dchadd427/ants-cross-platform/compare/ea3bc89...64654b3), [detailed notes](docs/CHANGELOG_ARCHIVE.md)

## Older versions (v0.0.89 and before)

Every release before v0.0.90, and the long notes of v0.1.0 and v0.0.99 - v0.0.90, are in the detailed history: [`docs/CHANGELOG_ARCHIVE.md`](docs/CHANGELOG_ARCHIVE.md).
