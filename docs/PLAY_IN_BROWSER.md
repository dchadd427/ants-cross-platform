# Play in the browser

The game runs in a modern web browser, as WebAssembly, with nothing to install: **[Play Ants Online at beta.playants.org](https://beta.playants.org)**. This page says what that site does. To build and serve it yourself, see [`BUILD_AND_RUN.md`](BUILD_AND_RUN.md). The game is developed and tested in a Chromium-based browser (the engine of Chrome and Edge); Firefox and Safari have not been tried. A phone can play it too: [`TOUCH.md`](TOUCH.md).

## The front page: one card for every game

**[beta.playants.org](https://beta.playants.org)** is the front page, the lobby (`web/lobby.html`, served at `/`). It used to be a separate Play online page; that page's address, `/four.html`, now redirects here with its query. The page is built like the game's own menus: the "ants!" logo, the black boxes and teal buttons of the setup screen and the original's help sheets. The pictures are in `web/front/`, made from the game's assets by `tools/front_page_art/`. **START!** is not a picture but a real button in the same teal, drawn by the browser so that it stays sharp at any size; the font is Libre Franklin, under the SIL OFL.

From top to bottom it shows:

- A line of numbers under the welcome banner, for example "3 matches being played · 7 players online · 1,284 games played (21 today)". "Today" means the last 24 hours. It counts what the game server knows: the page reads its `/stats`, else `/busy` for the live part, else it shows no line ([`SERVER.md`](SERVER.md#busy-and-stats)). It looks when the page opens and every 30 seconds while the page is visible.
- **Rejoin your match (CODE)**, only while there is a match to go back to ([below](#rejoin-your-match-code)).
- **Your name** and **Picture** (16:9 or Classic 4:3), then **one card for every game, New match**, with **Have a code?** under it.
- **How it works**, closed at the bottom: it tells the card in a few lines beside the original's two help sheets.

### The card: New match

There is no separate game against the computer and no separate host and join: a match is a room on the game server, and the page makes the room (the one exception is a game for one: with every other seat on **Nobody**, **START!** plays on this computer, with no room and no server). The rooms need a server with demo rooms (`--demo-rooms N --demo-map TREASURE.LVL --demo-maps ...`, as `docker-compose.stack.yml` starts it: [`SERVER.md`](SERVER.md#demo-rooms)); the rooms that the front page makes are demo rooms.

- **Map**: the six maps of the original, each with a picture. **Treasure** is preselected until you choose another.
- **Seats**: Green, Red, Blue and Black, each with its ant. Exactly one is **You** (your name under it); **Sit here** on another colour moves you there, which is how you change colour. Every other seat is one group of buttons that the arrow keys and Tab drive: **Friend** (a seat for a person you invite), **Easy**, **Medium** or **Hard** (a bot of that level) or **Nobody** (the seat stays out). A first visit is Treasure, You at Green and a Friend in the other three seats. The page remembers the choices.
- **Players**: You, the Friends and the bots are 1 to 4 players. **START!** is always on, and the line under it says what it will do. With every other seat on **Nobody** it begins **a game for one on this computer** (you play Green; a room on the server needs two people, so this game uses none), with no opponent.
- **Teams**, with three or four players: free for all, or a pair of the seats that play (with three the third plays alone; with four the other two are a team too). The pair is a word of the room's code (`demo-treasure-4p-t01-k7m2xq`, [`SERVER.md`](SERVER.md#demo-rooms)), so it holds when the room fills up by itself too.
- **Invitations**: each **Friend** seat shows its colour, its link, **Copy link** and, where the browser has it, **Share**. The links carry no name and no key; a note says so when a choice changes the links after one was copied. The room's code is kept by the tab, so a reload does not change the links that you sent.
- **START!** takes this tab to the game page of the room. The game of the room's leader (the first player to connect: you, or a friend who was quicker) presses START by itself once You and the Friends are in (the game's `--start-when`, [`COMMAND_LINE.md`](COMMAND_LINE.md)), which seats the bots of the plan and leaves the **Nobody** seats empty. With no Friend that is at once, against the bots, after the game's loading screen and quick help, with the "Get ready" dialog. With Friends the match waits for them, and the leader can start early with START in the waiting room (a Friend seat that is still empty then stays empty). With every other seat on **Nobody**, START! takes this tab to a game for one on this computer instead (`/play.html?map=<key>&name=<name>&aspect=<shape>`: no server, no room, no bots, no teams).
- **Have a code?**, under the card: type the **room code** of a match that somebody else made (up to 32 letters, digits, `-` and `_`) and press **Join**. Your name goes with it, and you get the first free seat, in this tab.

### The room and the start of a match

- The room's code is `demo-<map>-4p-[t<a><b>-]<random>` for a card (a room of four seats, any colour can be taken); the server makes the room when the first player connects and waits up to ten minutes for the others.
- The match starts by itself when every seat is taken, or earlier: **the first player in the room can start early with START once at least 2 players are in**. That player's setup screen is the original's host screen with the START button ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 5.50; the minimum of 2 is the remake's own rule); everybody else's setup screen waits. A running match takes nobody new (only a player who comes back to their own seat). A code whose demo room is over (finished or failed) makes a new room: a friend who comes late, a rematch with the same link.
- **The plan**, a bot's level for each seat or none: every link of a room carries it as `&fill=<plan>` (one level word for every seat, or four words for the seats 0 to 3, `none,easy,none,hard`, in any case), and only the leader's START uses it. START seats a bot of that level in each seat that is still empty and starts the match, **even when the leader is alone**. Not with Fog of War: the room seats nobody and tells the leader "Bots cannot play with Fog of War." With no Friend, the line under START says that the match starts at once and what the bots do: they gather food, raid and fight back. Under the players the leader's setup screen shows what START will do, worded shorter when there are both: the bots ("Empty seats at START: Medium bots", or a level for each seat) and the teams ("Teams at START", or "Room teams" when the code names them).
- In the waiting room **T** (or a click in the chat box's input box or its lines on the 16:9 setup screen, or on the status line on the classic one) opens a line to say something to everybody. The room's lines are in the chat box above it.
- Every game that this page shows or opened reports its state hash, and the page says whether they stay equal ("In step" or "OUT OF STEP"); games in separate windows report over the browser's `BroadcastChannel`.

### The room panel

The addresses of the earlier pages are not offered any more and work as before: `/?room=<code>` and `/?map=<key>&players=<n>` show the panel of a room. It has a link for anybody (they get the first free seat), **Play in this tab** (your own seat, the first free one, in this tab, as **Join** under **Have a code?** does) and, for each seat, "Play here", "Open a window" or "Copy link" (for a friend on another computer). **All seats on this page** and **All seats in separate windows** play every seat yourself, in frames of this page or in windows of their own. The address bar keeps the code (`?room=...`, with `&fill=<plan>` when bots were chosen; a Teams choice is a word of the code itself, and `&teams=A%2BB` is for a room whose code names none), so a reload or a copied address comes back to the room. `/play.html?map=<key>&bots=<levels>` and `/?map=<key>&players=1` play a game on this computer instead, with no room ([below](#the-game-page-and-its-addresses)).

### Your name

One field at the top of the page, shared by **START!**, **Have a code?** and the name step, and remembered in this browser (`ants.name`). The "Bot (" rule is the desktop start menu's, in the same words: a name that starts with "Bot (", whatever its blanks and capitals, is refused with the reason. A name has at most 32 printable ASCII characters; this page refuses one that has more, or another character, and says why (the desktop menu drops such characters and cuts the name), and it accepts an empty field (the desktop menu asks for a name). A bad name starts nothing and says why under the field.

- The seat that you play from this page plays under it (when you play several seats yourself, the first seat does and the others keep random names). An empty field gives a random name when you start a match with **START!** (a game for one included) and "Player" when you join with a code.
- **A link that somebody sends you asks for your name first, every time, except on a reload of a game page whose seat this browser holds** (the reload takes the seat back and asks nobody). This covers the game page's `?join=...&room=...` (an invitation of the card is such a link), the room links of this page and the links that host a match on a map. A small card shows your remembered name with a Join button (Host on a link that hosts a match on a map; or press Enter), and the game does not connect before you have chosen.
- The page's own frames and windows already carry the name chosen on the page and ask nobody, nor does `&play=here`.

### Rejoin your match (CODE)

While this browser holds a fresh key of a seat on this site's game server, one button stands above everything else on the front page, with the line "Your match in room CODE is still running: go back to your seat." under it. Without such a key nothing of it is shown.

- The game page keeps a key for every seat that it plays, for three hours, and lets go of it when the match ends, the seat is dropped or the player leaves.
- The button takes this tab to the game page of that seat (the newest, when there are several). The game page finds the key itself: it is in no address, text or log. The match goes on where it was.
- The exceptions: the others voted to go on without you (they may after 30 seconds), or the pauses of the match reached their cap, 30 minutes (10 in a demo room). Then the game says so. The rules of a held seat are in [`MULTIPLAYER.md`](MULTIPLAYER.md), "Lag, silence and drop-outs", and [`SERVER.md`](SERVER.md#reconnect).

### The game page and its addresses

**Menu**, the game page's header button (and its footer link), goes back to the front page in the same tab; so does the small "ants!" logo at the left of the header. Both ask first ("Leave the game and go back to the menu?") while a match runs or a room is joined, and Yes drops your seat at once, where a closed tab or a reload holds it. Nothing opens a new tab by default: the links to the sprites, the changelog, GitHub and the feedback do, and "All seats in separate windows" is an explicit choice.

nginx tells the two pages apart by the query: at `/`, `?join=<something>` and `?embed=1` open the game page and anything else is the front page, so the addresses of a game that was shared (`/?join=...&room=...`) or framed (`/?embed=1`) keep working.

| Address | Opens |
|---|---|
| `/` | the front page |
| `/?room=<code>` or `/?map=<key>&players=<n>` | the front page, after the name step, with the panel of that room (for a map, a new room on it; `players=1` plays a game on this computer instead). `&fill=...`, `&teams=...`, `&play=here` and `&aspect=...` are understood |
| `/four.html?...` | a permanent redirect (301) to `/?...` with the same query, so that old links and bookmarks still work |
| `/play.html?map=<key>&bots=<levels>&teams=<0+N>&name=<name>&aspect=<shape>` | the game page, a game on this computer with no server and no room (the address of the earlier front page's game against the computer; **START!** uses it, with no `bots` and no `teams`, when every other seat is Nobody) |
| `/?join=/ws&room=<code>`, `/?embed=1`, `/index.html` | the game page: a room link (with `&seat=<n>`, `&name=...`, `&fill=...`, `&teams=A%2BB`, `&start=<people>`; **START!** and the card's invitations make such links), only the game (a frame of the front page), the game page itself |

## Every page in one look

The changelog pages (`/changelog.html` and `/changelog_archive.html`) and Sprites and sounds (`/asset_catalog/`) are in the Classic look of the front page, with the logo (a link back to the front page) and a **Play** button in the header. They link one stylesheet, `web/front/classic.css` (served at `/front/classic.css`: the colours, the green frame, the teal buttons, the black boxes, the footer bar; the font and the logo are beside it in `web/front/`), and add only their own rules. The front page and the game page keep inline copies of their colours. `tests/scripts/test_web_pages_classic.py` finds every page that the site serves and fails, naming the page, when one is not in the look.

**The game page** (`web/shell.html`) has the front page's look too: the clay, the thin green frame, the teal buttons, the black boxes, the game's font and the logo from `web/front/`. The loading screen shows the logo and a teal bar, and the picture sits in the same frame as the front page's pictures. The two pairs of buttons under it (Picture and Fullscreen mouse) and the footer are the front page's. Up to 700 px the header is one row: the logo, **Menu**, **Fullscreen** and a **More** that opens the other links. A frame of the front page, `?embed=1`, shows only the game.

## Starts at once, and the sound follows your first click or key

- There is no start button. The game runs as soon as its data has downloaded (a loading card shows the download and goes away by itself); the original's own loading screen and quick help follow as always.
- Browsers let a page make sound only after you have done something on it, so the music and the sound effects begin with your first click, key press or touch on the page. The page keeps trying until the browser lets the sound run (Safari is the strictest: [`NETWORK_PORT.md`](NETWORK_PORT.md#the-browser-as-a-client-hidden-tabs-and-sound-v0096), "Sound and the autoplay rules").
- The page downloads the game's data (14.7 MB) itself and tries again, up to three times, when a download fails (several games on the front page ask for it at once, and a browser's cache can refuse one of them). If every try fails, if a file of the game (`index.js`, `index.wasm`) cannot be loaded, if the game never starts, or if the download is a web page instead of the data (a captive portal), a card with a **Reload** button replaces the loading screen.

## A network match keeps playing in a hidden or minimised tab

- A browser stops drawing a page that you cannot see and slows its timers. Every message of the game server still wakes the game, which plays its turns without drawing anything and without sound or music. So the match goes on at the normal speed, nobody is told that you lag (`<your name> is lagging`), and you find it up to date when you come back (the music of a match that begins or ends meanwhile follows then). A single game (no network) simply waits while the page is hidden, as the desktop game does when minimised.
- A server that stops answering is noticed after 10 seconds of real time, a hidden page included (the page's timer is a Worker, which the browser does not slow): a seat hidden for minutes noticed a paused server at +10 s, as a shown one did.
- **Limits**: a computer that sleeps, a phone that locks its screen or leaves the browser, and a tab that the browser freezes stop the page (a frozen tab loses its connection when it wakes up). Since v0.6.0 the server holds such a seat and pauses the match; the game reconnects with the seat's key, or **Rejoin your match** takes the seat back. A seat whose page stays connected but runs no turns is still called lagging after 3 seconds and dropped after 30 ([`MULTIPLAYER.md`](MULTIPLAYER.md), "Lag, silence and drop-outs").
- No automated test covers this code, which exists only in the web build. The manual check and the opt-in script `tests/scripts/test_web_hidden.sh` are in [`NETWORK_PORT.md`](NETWORK_PORT.md#the-browser-as-a-client-hidden-tabs-and-sound-v0096), "The browser as a client: hidden tabs, and sound".

## Your own orders show at once in a network match

- The game can run a second engine next to the match's own (the confirmed one, which the host's turns, the server's referee and every hash keep exactly as they were). An order is run on it in the same call and the screen shows it, a few ticks ahead, so the ants answer a click as they do in a game of one machine instead of one round trip, one seal and one jitter buffer later. The corner's `delay` shows what the click feels.
- **It is off by default**: other players' ants still hop about a dozen pixels, a tile at most, when their orders arrive. `?prediction=on` on the address of a network game (a `?join=...` link; the front page does not pass it on) turns it on, as the game's `--prediction on`; a game on one computer never predicts. Once on, it switches itself off for a while (ten seconds, twice as long each time) when its work takes too long on a slow device, and it is off in a hidden tab, in a pause and while the "Get ready" dialog is up.
- More: [`MULTIPLAYER.md`](MULTIPLAYER.md), "Prediction of your own orders"; the design and the measurements: [`NETWORK_PORT.md`](NETWORK_PORT.md#prediction-of-ones-own-orders-predictionhpp-cue_routerhpp-netgame-applicationview_sim).

## Authentic 1998 Asset Pipeline

The page downloads one data package, `index.data` (14.7 MB: the whole `Original-Ants/` folder), and packs it into the browser's in-memory file system, where the game reads it as it is. It holds the original `ants.chd` (8.4 MB: the sprites, sounds and animations), the six maps, the four pieces of music as MP3 and MIDI files, and the bundled font (Libre Franklin and its licence).

## A 16:9 picture that fills the page

- **The picture**: the game draws a fixed 960 x 540 canvas (`?aspect=16:9` on the page's address; this is the default), shown in a game box that is 16:9 and as large as fits: its width is `min(100%, 1280 px, (window height - the page's header and bar) x 16 / 9)`, so the whole picture is on the screen without scrolling.
- **Exact pixels**: the box takes the screen's own resolution (CSS size x `devicePixelRatio`, in whole steps of 16 x 9 device pixels), so the canvas has exactly the picture's shape and there is no black bar inside the box. The picture and the pointer stay right when the window is resized, the page is zoomed or fullscreen is toggled. On a display of about 100 Hz or more the frame rate is held to 60 a second.
- **Classic 4:3** (the original's 640 x 480 picture) is one click away: the selector "16:9 / Classic 4:3" under the game (the browser remembers the choice and the page reloads with the parameter), or `?aspect=4:3` on the address. The selector asks "Leave the match to change the picture?" while a match is being played or a room is joined.
- **Which shape**: only `16:9` and `4:3` are accepted, anything else is ignored, and the address beats the remembered choice. Every device starts in 16:9, a phone held upright included (a portrait phone gets a tip to turn it sideways for a bigger picture). The choice is remembered under the browser's key `ants.aspect.v2`, which only the two selectors write, the game page's and the front page's.
- **Fullscreen** (the header's button) shows the picture as large as the screen allows, with bars on a screen of another shape; a browser with no Fullscreen API for an element (Safari on an iPhone) gets the page's own fullscreen with a button to leave it. The bars count as the picture's edge: a pointer over a bar scrolls the map like the edge, corners included.
- **The mouse in fullscreen**: the page holds the mouse for the game (the browser's pointer lock, taken by the click on the Fullscreen button). The game's cursor cannot leave the picture, so every edge scrolls and a Mac's Dock and menu bar have no screen edge to come up at. The control "Fullscreen mouse: Locked / Free" under the game turns the lock off (remembered under `ants.pointerlock`).
- **Esc**: with **Fullscreen mouse: Locked** (the default), a browser that has the Keyboard Lock API (Chrome and Edge) gives Esc to the game, and holding it leaves fullscreen; in other browsers Esc gives the mouse back first. With **Free** the fullscreen is the browser's plain one.
- **In a window** the map keeps scrolling while the pointer is just past the edge of the game, up to 96 CSS pixels (about an inch) beyond the box and corners included; it stops when the pointer goes farther, onto a button or link of the page, or out of the window (clicks, the wheel and the menu there are the page's).
- The page gives the game `--aspect 16:9` or `--aspect 4:3` (the desktop game's own option, which wins over the settings key `aspect`).
- **On the front page** the frames of the games are 16:9 too (`?aspect=4:3` there gives the classic frames, and each game gets the shape). In a window that has room for two frames side by side at the picture's own size, 2030 px for 960 x 540 and 1390 px for the classic 640 x 480, each frame is exactly that size and sharp.
- What the wider picture shows (the map view, the frame, the HUD) is in [`VIEW_AND_HUD.md`](VIEW_AND_HUD.md#169-by-default-a-wider-view-with-more-of-the-map-desktop-and-web-builds). The setup screen in the browser is the wide one with its map preview ([`CONTROLS.md`](CONTROLS.md#the-169-setup-screen-desktop-and-web-builds-the-960-x-540-picture)).
- The opt-in `tests/scripts/test_web_aspect.sh` and `tests/scripts/test_web_edge.sh` check the picture and the pointer in a real browser (they need a running page and a Chromium-based browser).
- The design and the measurements are in the bullets "The page's picture", "The pointer in fullscreen" and "The margin of a windowed page" of [`NETWORK_PORT.md`](NETWORK_PORT.md#the-dedicated-server-srcants_server-v0083).

## 32-Channel Spatial Sound & Soundtrack

The game mixes up to 32 sounds in its own mixer, with volume and left / right pan by distance; in the browser the sound goes through SDL's Web Audio. The sound effects are the 91 sounds of `ants.chd`: the units' voice cues, the explosions and the rest. The soundtrack is the original's four MIDI pieces, `INTRO`, `ANTS2A`, `ANTS2B` and `ANTSFUN3`, played from MP3 renderings of them that the game decodes itself and streams from memory. The sound law and the rest: [`VIEW_AND_HUD.md`](VIEW_AND_HUD.md#sound-and-music), "Sound and music".

## Interactive Asset Catalog

Browse all 2,794 sprites, 91 sound effects and 1,344 animation sequences at **[beta.playants.org/asset_catalog/](https://beta.playants.org/asset_catalog/)**; what it shows and how to open it from a checkout: [`ASSET_CATALOG.md`](ASSET_CATALOG.md).

## See also

[`MULTIPLAYER.md`](MULTIPLAYER.md) (network play), [`SERVER.md`](SERVER.md) (the dedicated server), [`CONTROLS.md`](CONTROLS.md) (menus, mouse, keyboard), [`TOUCH.md`](TOUCH.md) (touch), [`VIEW_AND_HUD.md`](VIEW_AND_HUD.md) (the picture and the audio), [`ASSET_CATALOG.md`](ASSET_CATALOG.md), [`BUILD_AND_RUN.md`](BUILD_AND_RUN.md), [`NETWORK_PORT.md`](NETWORK_PORT.md) (design and measurements), and the short front page of the repository, [`../README.md`](../README.md).
