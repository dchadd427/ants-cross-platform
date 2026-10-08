# Menus, controls and hotkeys

What a player can press and click: the start menu of a native game, the setup screen, the options window and the settings that are remembered, then the mouse, the touch screen and the keyboard. The rules of the game are in [`GAMEPLAY.md`](GAMEPLAY.md), the picture, the zoom and the HUD in [`VIEW_AND_HUD.md`](VIEW_AND_HUD.md), the options of the `ants` command in [`COMMAND_LINE.md`](COMMAND_LINE.md) and network rooms in [`MULTIPLAYER.md`](MULTIPLAYER.md). Where a control is the original's, its section says so and names the part of [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) that records it ("section 5.44" and the like). The start menu, the mouse wheel, the touch screen and the keys of network play are the remake's own.

## Start Menu (a native game started without a mode)

A native game (macOS, Windows, Linux) that is started with **no mode on its command line** shows a start menu after the original's loading screen and **before the quick help**. The quick help and the setup screen follow the choice, unchanged. **The web build never shows it**: its page has its own controls ([`PLAY_IN_BROWSER.md`](PLAY_IN_BROWSER.md)).

The menu is the remake's own: the original has no such screen (its Single / Multi screen is something else, [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 5.63). It uses only what the game has: the orange background and green frame of the original's Single / Multi screen (`sm_screen`), boxes and buttons in the colours of the original's button pictures, the ants of the setup screen and the game's TrueType face. The version and the frame rate stand in the corner as on every screen.

The first panel has these entries, top to bottom (the order of `Tab`):

| Entry | What it does |
|---|---|
| **Rejoin your match (CODE)** | Only while the game holds a fresh key for a seat of a running match: takes that seat again ([below](#rejoin-your-match-code)). |
| **Single player** | A match on this computer, against nobody or against computer players. |
| **Join with a code** | Joins a room of the game server by its code. |
| **Host an online match** | Makes a room on the game server and shows its code. |
| **Quit** | Ends the program. `Esc` on the first panel does the same and asks nothing. On any other panel `Esc` goes back (while connecting it cancels). |

The menu joins and hosts through a game server ([The server](#the-server)). A room on a local network is found with `--lan-list` and joined with `--join` ([`MULTIPLAYER.md`](MULTIPLAYER.md)).

### Single player

- **Your name** is the one remembered name of Join and Host, with their rules. The game is played under it.
- A row for each of the three seats that are not yours (you are green: Red, Blue and Black; `--start-menu --player N` makes another colour yours), each **Empty / Easy bot / Medium bot / Hard bot**. A click, `Enter` or `Right` go on, `Left` goes back.
- **Continue** leads to the original's quick help (while "Show Quick Help at Startup" is on) and the setup screen.
- **All Empty** (the default) is the plain local game: four teams, nobody to play against ([`BOTS.md`](BOTS.md#running-bots)). The panel says "No bots: the original single-player game (the other colours stand still)." The other three colours keep their hills and the ants that the map starts them with (the number depends on the map).
- **With at least one bot** the match starts with exactly the bots `--bot SEAT:LEVEL` would give (the same code path: "Bot (Medium)" on the labels and the results). Only the seats that are taken play; an empty seat has no ants. The panel says "Bots play without fog of war. Empty seats have no ants." How the bots play: [`MULTIPLAYER.md`](MULTIPLAYER.md#bots-computer-players).
- A bot would see through the fog, so a bot together with Fog of War is refused, as it is with `--bot`. The setup screen's fog buttons are the original's and unchanged: with a bot seated, START with fog on is refused with the reason; with fog off it starts.
- With two or more bots a **Teams** row appears: Free for all, or You + one of the bots (the other two are a team too when both play). It is `--teams`.
- The choices are remembered ([Settings](#settings)).

### Join with a code

- **Your name** is remembered ("Player" at first) and has at most 32 printable characters. A name that starts with "Bot (", whatever its blanks and capitals, is refused on the panel with the reason: the server would rename such a player, and a person is never shown as a bot.
- **Room code**: up to 32 letters, digits, `-` and `_`, as the screens show it (`k7m2 xq9p`, in two groups of four: the blanks are dropped). `Ctrl+V` / `Cmd+V` pastes. **The case is never changed**: the server's codes are case sensitive. A code alone makes no room (use Host): one that no room has is told "There is no room with the code k7m2xq9p on beta.playants.org:4001. Check the code (capital letters matter)." (the web page and the command line say "There is no such room on this server.")
- **Join** connects to the game server ([The server](#the-server)), the same way as `--join HOST:PORT --room CODE --name NAME`. It ends in the guest's screen, or in the leader's host screen with START when you are the first in the room. "Connecting to <server>..." has a Cancel button (`Esc`).

### Host an online match

- The **map** is one of the six maps of the original game: **Treasure** until you choose another. The map you chose last is remembered (`host_map`).
- **2 - 4 players**, and a row for each seat after yours, **Red / Blue / Black at START**: Leave empty / Easy bot / Medium bot / Hard bot. This is the bot that your START puts in that seat if nobody has taken it (alone, if you like). The choices are remembered (`host_fill`: four words, or the one word of an earlier version for every seat).
- For three or four players a **Teams** row: Free for all, or Green + Red against Blue + Black and so on (remembered under `host_teams`). Under the rows: "Bots gather food, raid and fight back."
- Your name, and **Host**.
- The menu makes a room code the way `web/lobby.html` does: eight random characters of `abcdefghjkmnpqrstuvwxyz23456789` (`k7m2xq9p`), shown in two groups of four (`k7m2 xq9p`); the code is only a name. The room's map, its seats and its teams travel in the **create block** of the menu's first Hello (network protocol 15): the server makes the room from it, and a Teams choice makes the teams every time, when the room fills up as well as at START. The menu joins the room as the first player (the room's leader) and **shows the code in large letters**.
- The panel also shows what START will do ("Empty seats will be Medium bots." / "At START: Red gets an Easy bot, Black a Hard bot." / "Empty seats stay empty." and "Room teams: Green + Red against Blue + Black."), the number of players in the room, **Copy** (the system clipboard; `Ctrl+C` / `Cmd+C` too) and **Continue to the room** (the leader's screen with START). The match starts by itself when the room is full.
- The window's title is "Ants - room <code>" from the moment you are in a room until you are back at the menu. `Esc` / Back on that panel leaves the room.
- The server needs demo rooms (`--demo-rooms`; the public beta server has them, [`SERVER.md`](SERVER.md)). When it cannot make a room the panel says it is busy.
- **A server whose `--demo-maps` lacks the map you chose** makes the room on its default map, which the panel does not accept: you are put out of that room and told "This server does not offer the Islands map (its room is on TREASURE.LVL). Choose another map."

### Rejoin your match (CODE)

Since v0.6.0 the first panel has a fifth entry, **Rejoin your match (CODE)** (CODE is the room's code), above Single player and first in the `Tab` order. It is there while the game holds a fresh key for a seat of a running match on a server. The key is kept in `rejoin.txt` and is not used when it is older than three hours ([Settings](#settings)).

A click on the entry, or `Up` from Single player (the first panel preselects Single player, not this entry) and `Enter`, takes that seat again. The panel says "Rejoining your match in room CODE...", and the match comes back with no waiting room and no quick help. When the key has been let go of or is too old, the panel says "There is no match to rejoin any more: its key was let go of, or it is too old." A refusal by the server comes back to the first panel with its reason. How a seat is held and given back: [`NETWORK_PORT.md`](NETWORK_PORT.md) "Reconnect (protocol 10): a player whose connection is lost can come back".

### Keys and mouse of the menu

- `Up` / `Down` (or `Tab` / `Shift+Tab`) select. `Enter` (and `Space` on a button) acts. `Left` / `Right` change a row. `Esc` goes back. Typing and `Backspace` edit the field that has the focus.
- Arriving at a field by the keyboard, or at a panel that has a code from an earlier room in it, selects the text, so the next typing replaces it. `Ctrl+A` selects, `Ctrl+Backspace` clears. A letter that is not A-Z, an accent or a CJK character is refused with the line "Only letters A-Z, digits and simple punctuation.", never dropped in silence.
- **What a panel preselects.** A panel that has an input preselects its first input: the first row of Single player, the map of the Host panel, the first empty field of Join, nothing on "Connecting". The first panel (the entry that you used last, else Single player) and the room's panel (Continue to the room) preselect their way on. After a failed or cancelled attempt the Join or Host button is selected again, so that `Enter` tries once more.
- **`Enter` and `Space` do nothing for 300 ms after a panel appears** (and `Esc` does not quit from the first panel then). An `Enter` that is pressed twice in a hurry is one gesture: its second press cannot start the game, make a room or cancel the attempt. A key that is held never acts again.
- **The mouse**: the pointer selects a button. A click is a press and a release on the same button (the original's button class: the click sound at the press, the action at the release, leaving the button cancels it). A click in a field gives it the focus.
- **The rest of a click that changed the screen is not a click on the screen that it opened.** The controls lie on top of each other: the Host button under the "Host an online match" entry, Quit under the Host panel's Back and under the loading screen, Cancel under the Join panel's Back and under the Host button, the quick help's START! under the setup screen's START. So after a click changed the panel or the screen, a second click that SDL counts as part of the sequence, or one that comes within half a second, only moves the hover (this covers the click that leaves the loading screen, the quit dialog's Yes, the Connecting panel's Cancel and the Single player panel's Continue). A click after that is a click.

### The server

- The server is **beta.playants.org, port 4001** (the public server's TCP game port). `--server HOST[:PORT]` (host name, IPv4 or `[IPv6]`) uses another one for this run, and the settings key `server` makes another one the default. The panels show which one they use.
- A `--server` that is not an address stops the game at the start, with the reason.
- The name is looked up on a worker thread, so the window stays alive and `Esc` cancels at any moment.
- The steps of an attempt are in [`NETWORK_PORT.md`](NETWORK_PORT.md) "Joining and hosting from the desktop start menu".

### When something fails

Every failure comes back to the panel with a line of its own: never a crash, a hang or a silent return.

- The server cannot be reached (the line names it).
- **A server that does not answer** says "The server <name> did not answer". It is one line for all three ways of staying silent: a name that is never resolved, a connection that is never made (the attempt's limit is **20 s**), and a server that accepts the connection and never sends its Welcome (**10 s**, the room's limit).
- A server that hangs up: the connection was lost before the room.
- No room with that code (the line names the code and says "capital letters matter"), the room is full, the match has already started, you were removed from the room, the request was not accepted, the server is busy (when hosting).
- Another version: "This game is <version>, but the server runs another version of the game". The version is the running game's own.

### After a network match

After a network match the game is back **at the start menu** instead of ending the program. The same holds when the connection is lost, when the room is closed under you and when you leave from the room's screen or the quit dialog. A lost or closed room says so on the first panel. The menu works again: another room, a single-player game.

**After a single-player match the original's flow is kept: Leave on the results screen ends the program.** A game that was started with a mode (any option in the next list) has no menu and ends as it always did.

### Which command lines show the menu

- None of these starts a match, a room, a test run or a screenshot, so a plain `ants`, `ants --name Bob`, `ants --server play.example.org` and `./start_game.sh` (or `./start_game.sh --single`) show it.
- These options choose a mode and **skip the menu**: `--map`, `--map-select`, `--play`, `--alone`, `--host`, `--join`, `--join-url`, `--room`, `--token`, `--seat`, `--bot`, `--headless`, `--screenshot`, `--player` / `-pnum`, `--select-ant`, `--select-base`, `--open-options`, `--scorecard`.
- **`--start-menu` forces it**, also with `--headless` and `--screenshot` (for the tests and the screenshots). Combined with `--map`, `--open-options`, `--scorecard`, `--host`, `--join` or `--join-url` it is refused: they start a match or a room at once. So it is with `--alone`: the menu's Single player chooses who plays.
- `--map-select` starts on the setup screen, without the menu.
- The four windows of the `./start_game.sh --players 4` test rig each have `--host` or `--join` and never show it.

The options themselves are in [`COMMAND_LINE.md`](COMMAND_LINE.md).

## Setup Screen (map selection)

The original's setup screen ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 5.50). What differs is named below: the highlight at the start, the 16:9 page and the screens of a network room.

- **The map list is searched, not built in**: every `.lvl` file of `Original-Ants/Maps/` is listed, sorted by the bytes of the file names (capitals before small letters). A map that you drop into that folder (one of the community's maps, say) appears on the screen at once. The name is the file's name without `.lvl`; the description and the minutes come from the file's own header.
- **The highlight at the start is Treasure** (`TREASURE.LVL`, when the folder holds it; else the first map). The owner asked for it because Treasure is the map that is played most, so a player who only presses START plays it. This is the one deliberate difference from the original, which highlights the first entry of its list (`GAUNTLET.LVL` for the six maps; [`AUDIT_ONE_TO_ONE.md`](AUDIT_ONE_TO_ONE.md) section 3b). The list, its order, the keys, START and the labels are the original's; the 16:9 page and the rooms add what the next sections describe. A guest and the leader of a server's room show the room's map, never this highlight.
- **Keys**: `Up` / `Down` (or the arrow buttons) select the previous / next map, wrapping round. `Enter`, `KP Enter` or `S` (or the `START` button) start. `Q` or `X` (or the Leave Game button) leave. The original's setup screen knows no other key: `Esc`, digits, `Left` / `Right`, `Space`, `F` and `D` do nothing. In a network room `T` opens the waiting-room chat ([below](#in-a-network-room)).
- **The buttons are the original's button class**: a press captures the button and the action happens at the release; moving off the button before the release cancels it ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 5.45, which also has the hit test).
- The **Fog of War On / Off** pair is silent and starts on Off. `START` locks the screen. START! of the quick help is not at the same place as the setup screen's START: the setup screen's START lies 3 px to the left and 2 px lower (the original's own data says so, section 5.52).
- The labels, the ant portrait and the thumb appear 500 ms after the screen is created (the original's refresh task), in the original's colour.
- **Soundtrack**: `INTRO` starts with the program and plays once; when it ends, random in-game pieces (`ANTS2A`, `ANTS2B`, `ANTSFUN3`) follow one another, on this screen too ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 5.24, item 5.24e).

### The 16:9 setup screen (desktop and web builds, the 960 x 540 picture)

- The screen is composed for the picture instead of standing on a clay margin as a 640 x 480 page: the big title art on the left, a **map preview** in the middle, the Players' Status box and the Fog of War / START / Leave Game buttons at the right edge, and the map list, Map Info and the status box wider along the bottom. It has the same controls, keys, rectangle rules and 500 ms refresh as the 4:3 page.
- Nothing of the original's art is scaled. The clay is the original's tile at its own pitch, the frame its pieces, and every black box the original's own construction at the size wanted. The map list box and the status box are 70 columns wider than on the 4:3 page (the status box is 383 columns), and the chat input box is 96 columns narrower than the original's status line picture (217 of its 313 columns). The wider boxes repeat or drop columns **inside runs of identical columns** only (`test_wide_setup` checks every junction against the art).
- **The map preview** is the game's own picture of the selected map. The map is set up as for a new match (all four start positions), and the game's renderer draws the whole world once into an offscreen image: the terrain, water, grass and plants, the hills in their team colours, food, power-ups, rocks and every other object, with no HUD, fog, cursor or ants. An exact area filter in linear light reduces it to the preview box and keeps the map's proportion (a map that is not square is centred on black). It is made when a map is first shown and kept, with "60 x 60 cells · 4 players" under it.
- When the offscreen render cannot be made, the preview falls back to a picture in the minimap's colours (one flat colour per terrain class, the four hills in their team colours, a dot for every flower or clover) and the reason is logged once. A map that this machine does not have (a guest without the file) or cannot read has a "No preview" box.
- A game of one machine has the bigger preview (300 x 300 pixels inside). A network room (a LAN host, the leader of a server's room, a guest) has a 248 x 248 preview and keeps the column under it for the waiting-room chat (a "Chat" label, a chat box with the room's lines and the typed line, and an input box) and the leader's bot-fill line ("Empty seats at START:" in the Players' Status box).
- The 4:3 picture (`--aspect 4:3`, `?aspect=4:3` in the browser, the page's selector) has the original's page, unchanged.

### In a network room

- The setup screen lists every player with a portrait in the player's colour, the name and a thumb for the link: a green thumbs up (round trip below 1.2 s), a yellow sideways hand (below 1.8 s), a red thumbs down (slower) or an orange question mark (not measured yet).
- **You are always the first row.** The original shows its own machine as the first slot of every screen: a guest sees itself, then the host, then the others. The colour belongs to the player, not to the row.
- **Only the host changes the map and the fog and presses START.** START needs a second player and every thumb, unless the empty seats are filled with bots: then one person can start.
- **In a room of a dedicated server there is no host, but there is a leader**: the first player who joined (when it leaves, the earliest of those who are left). The leader's screen is the original's host screen with its START button (and `Enter`, `S`), in a server-room mode: the map label shows the room's map, Up / Down and the Fog of War buttons change nothing (the map and the fog were chosen when the room was made), and START asks the server to start the match **now with the players who are there**. A room whose seats are all taken starts by itself (unless its create block says that a full room waits for its leader's START: `--room-leader-start`). The server's side is in [`NETWORK_PORT.md`](NETWORK_PORT.md) "Protocol 7: the room's leader starts early".
- With the empty seats left empty the leader's START needs two players at least: with fewer the can't-go cue answers and nothing else happens, as on a host's screen. With a bot fill one person is enough.
- **The leader moves the colours** (network protocol 14; swaps and the guard 15). A press on a player's row of the Players' Status box, and a release on the same row, puts that player in the next colour that nobody holds (Green, Red, Blue, Black, round again); when every colour is held it changes places with the next player, never with a bot or the host; that happens in a room that waits for its leader's START when it is full (`--room-leader-start`), because the rooms of the front page and of the start menu start as soon as every colour is held (the leader's own row works too, and the row lights under the pointer). Each player that was moved, but the leader, is told in the room's chat ("Ann moved you to Red."), and a second press within half a second of the last is held back (a double click is one press). A press acts on the seating that the leader's screen shows: if somebody has come or gone meanwhile the room ignores it, and a release over a row whose player is another than the one that was pressed does nothing, so a tap moves only the player that was tapped (a newcomer with the same name in the same colour looks like the player who left, to the leader as well). The plan's bots follow what the room shows: the bot that was for the colour that the player takes is for the colour that the player leaves (a swap changes no bot). The leader's status line says it once, for five seconds, when a second person is in the room ("Tap a player to change their colour."). A guest's screen has no such rows ([`NETWORK_PORT.md`](NETWORK_PORT.md#protocol-14-the-leader-of-a-room-moves-a-player-to-another-colour), "Protocol 14" and "Protocol 15").
- **The quick help does not press the leader's START** (START! of the quick help lies under it). Of a double click on START! the second click (a click within half a second of the one that closed the quick help), and the repeat of an `Enter` or `S` that is held down, do nothing on the leader's screen. A fresh click or key press starts the match.
- The original's own screens (the local game, a LAN host) are not changed by the click rule: the original has no such protection, and a second click acts there as it does in the original ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 5.61). A held `Enter` still starts a LOCAL game as in the original, but a LAN host's START (a room's screen, with the chat input) needs a press.
- **A guest sees another screen, as in the original** ("Game Set-Up", "WAITING FOR GAME TO START!"): no arrows, no START, no Fog of War buttons, a fixed "Fog of War?" box that shows the host's choice (No or Yes), the host's map name and the description from the guest's own copy of the file ("???" without it). Only `Q` / `X` or the Leave Game button do anything there, besides `T` for the chat.
- The status line has the original's texts ("Press START when all players' thumbs have appeared.", "Waiting for the host to start the game...", "Trying to connect to the host..."). With a bot fill the host's or the leader's line says what START will do instead ("Press START: the empty seats get Medium bots.").
- **`T`** (or a click in the chat box's input box or its lines on the 16:9 screen, on the status line on the 4:3 page) opens a line to say something to the room. `Enter` sends it, `Esc` or a click closes it. While it is open the screen's own keys (`S`, `Q`, `X`, the arrows, and `Enter` as START) do nothing, and for 400 ms after it closes `Enter` and `S` start nothing. [`NETWORK_PORT.md`](NETWORK_PORT.md) has the rest ("The chat box of the 16:9 setup screen").

## Options Screen (`Ctrl + O` or the Options button)

The original's options window ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 5.51). It is a window over the whole screen that takes every key and click while it is open; the game goes on behind it.

- **`Enter` closes it** (whatever has the focus), **`Esc` does nothing**, a click outside the card does nothing, and the **Return to Game** button closes it when released on the button.
- **Sliders** (Sound Volume, Music Volume, Map Scroll Rate): press on the track (in the 4:3 picture 188 - 418 px across and 20 px high), drag, release. The values are whole numbers 0 - 99, and the track end is 99. The thumb follows the pointer; **the value is applied once, at the release**. The Sound Volume sets the volume and plays a test voice, the Music Volume sets the volume and starts a new piece, the Scroll Rate sets the edge scrolling speed. A plain click sets the value of the clicked place.
- **Switches** (Participate In Chat, Show Quick Help at Startup) are two ON / OFF pairs. They act at the release, like every button: a press only captures, and leaving the button cancels it.
- **Quick Chat keys** (F9 - F12): click a field to edit it (the F9 field is active when the window opens). Type up to 100 characters; the caret blinks, Backspace deletes. Every change is kept at once, and `F9` - `F12` in the game send the texts.
- Every change is written at once and read back at the next start with the original's validity rule ([Settings](#settings)).

## Settings

The options are remembered between runs, as the original keeps them in the registry. They are in `settings.ini`, lines of `name=value`, in your per-user application folder: `~/Library/Application Support/Ants/Ants/` on macOS, `~/.local/share/Ants/Ants/` on Linux, `%APPDATA%\Ants\Ants\` on Windows. On the web they are in the browser's local storage (the key `ants.settings`). `--settings FILE` uses another file. A headless run (the tests) keeps its settings in memory only, unless `--settings` names a file.

Every save writes the whole file beside it and then gives it its name (a replace), so a program that dies in the middle of a write, or a second window that reads at that moment, never sees half a file.

**The original's keys** keep the original's names: `Sound Volume`, `Music Volume`, `Scroll Speed` (the slider's label is "Map Scroll Rate"), `Participate In Chat`, `Show Quick Help at Startup` and `Quick Chat F9` ... `Quick Chat F12`. A value is used only when it is valid: a whole number from 0 to 99, and for the two switches the stored 0 (which turns the switch off). Otherwise the default applies: Sound 100, Music 65, Scroll 50, chat and quick help on, the quick chats of the original.

**The remake's own keys** are in lower case:

| Key | What it holds |
|---|---|
| `aspect` | `16:9` (the default of a desktop game) or `4:3` (the original's picture). Read at the start; the command line's `--aspect` wins. A value that is neither is reported and ignored. |
| `zoom` | A number from 0.05 to 2 (written with three digits: `0.841`, `1.41`): the level of the map view's zoom that the wheel changed last. Written at every change, read at the start; the match starts at the nearest level of its map. `--zoom` wins; anything else is reported and ignored. |
| `prediction` | `on` or `off` (`yes` / `no`, `true` / `false` and `1` / `0` do as well; absent: off): whether a match of the network shows the player's own orders at once. Read at the start; `--prediction` and `--no-prediction` win. A value that is none of these is reported and ignored. Nothing in the game writes it. |
| `name` | Your name, from the start menu. Written when you leave the field, change the panel, press Continue, Join or Host, or end the program, not at every key. `--name` beats it for that run. |
| `bots` | The Single player panel's choice: four words by seat, `off,easy,medium,hard`. |
| `teams` | The Single player panel's Teams row: `ffa`, or two seat numbers such as `0+1`, as `--teams`. Anything else is free for all. |
| `host_map` | The Host panel's map: `tiny`, `small`, `medium`, `gauntlet`, `treasure` or `islands`. `treasure` when none is stored. |
| `host_players` | The Host panel's players, 2 - 4 (4 when none is stored). |
| `host_fill` | The Host panel's bots for the seats after yours at START: four words by seat as in `--fill-bots` (the first, your own seat, is always `none`), or the one word of an earlier version (`none`, `easy`, `medium` or `hard`) for every seat. Anything else is `none`. |
| `host_teams` | The Host panel's Teams row: `ffa`, or two seat numbers such as `0+1`, as `--teams`. A choice that the number of players does not offer, and anything else, is free for all. |
| `server` | `host[:port]`: the game server of the start menu. Written by hand: the menu shows it and never changes it. `--server` beats it, and a value that is not an address is the default server. |

Two more files lie in the same places. `rejoin.txt` beside the settings file holds the keys of the seats that the first panel's "Rejoin your match (CODE)" may offer again. `chat.txt` is the transcript of the chat log that the original writes when the program ends after a match; the game writes it into the per-user folder (not the folder of `--settings FILE`). The web build and headless runs write nothing.

## Mouse Controls

| Action | Trigger | Description |
|---|---|---|
| **Select Friendly Ant** | Left Click on Friendly Ant | Selects a single ant unit (Shift adds it to / removes it from a selection of your ants). |
| **Inspect Enemy Ant** | Left Click on Enemy Ant | When no friendly unit is selected (or a hill or another ant is inspected), selects the ant to view its selection brackets (`*ears`, coloured by health). It gives no orders ([`GAMEPLAY.md`](GAMEPLAY.md#enemy-ant-inspection)). |
| **Move Order** | Left Click on Terrain | Issues move order to selected ant(s). Intermediate food tiles are avoided. |
| **Harvest Order** | Left Click on Food Morsel | Instructs ant to harvest food item and return it to base. |
| **Attack Order** | Left Click on Another Player's Ant | With your ants selected: orders them to engage the ant (the original's group order: ants that already attack that ant are skipped, the nearest one answers; the clickable area is the original's sprite box; allies show the attack cursor too, and an attack on an ally's ant or hill asks first whether to break the team). |
| **Marquee Selection** | Left Click & Drag (more than 4 px in either direction) | Selects your ants whose sprite box overlaps the red 1 px band (a shorter drag is a click at the release point; dragging over nothing deselects). |
| **Minimap Navigation** | Hold Left Button on Minimap | The view follows the point under the pointer while the button is held (every 50 ms); a right click on the minimap orders the selected ants there. |
| **Scroll the Chat Log** | Hold Left Button inside the Chat Log and Move | Drags the log like a sheet (earlier lines while the pointer moves down); a pointer held outside scrolls 15 px every 100 ms; releasing the button snaps back to the newest entries. It is the original's drag (section 5.56); [`VIEW_AND_HUD.md`](VIEW_AND_HUD.md#chat-log-window) describes the window. |
| **Special Ability** | Right Click on Field (at the release) | A move for several ants, workers, combat ants and mixed groups, otherwise the ability of the single ant's type (the next table); or latch the ability pedestal and left-click a valid target. |
| **Bomb Hit (Jump)** | Click on a Friendly Bomb with Several Ants Selected | The move click sends the selected ants to the bomb; the first one to step on it sets it off (a single bomber defuses it instead; a single worker or fighter hits it too). |

The abilities, by the type of the single ant (the original's, [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 5.37 for the bomber, the fire ant and the swimmer and section 5.35 for the thief's raid; [`GAMEPLAY.md`](GAMEPLAY.md#special-abilities) says what each one does):

| Ant | Right click on a valid target |
|---|---|
| **Bomber** | Plant a bomb / defuse an existing bomb (any team's). |
| **Fire Ant** | Ignite firewall / extinguish blaze. |
| **Swimmer** | Dig bridge on water / demolish existing bridge. |
| **Thief** | Infiltrate enemy anthill. |

### Pointer & Commands

The cursor mode decides what a click does, exactly as in the original ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 5.44):

- Over an ant, the ant is picked with the original's sprite boxes (a 3 x 3 tile scan, the last box wins, no filters).
- Other players' ants, allies too, give the attack cursor (an ant that stands on a hill tile gives the move cursor instead); food gives the food cursor; your own hill and the fog give the move cursor.
- A valid special target gives the target cursor: a bomb for a bomber, a hill of another colour (an ally's too) for a thief, or, with the ability pedestal latched, plantable ground, a fire wall, water or a bridge.
- A bomber, fire ant or swimmer that is busy (planting, lighting, building) counts as a worker for that test, as the original's type getter does with its flag 1. It gets the plain move cursor, and its right click is a plain move, until the clip ends.

What a click does after that:

- A left drag of at most 4 px is a click at the release point. A bigger one is the rubber band, which selects your ants by positive-area overlap (Shift adds to a selection of your ants).
- The right button gives its order at the release, at the tile of the press point (an attack goes to the ant under the pointer at the release).
- The Move and ability pedestals only latch: a visual state that removes the band or turns valid tiles into targets, and pops up after an accepted order. Stop stops the ants, locks the mouse for 250 ms and then deselects. The hatch pedestal exists only while eggs remain, and the ally pedestal only while more than two players are still in the match and the hill's owner is not your ally.
- An order that at least one selected ant needed makes the pedestal click or pop up even when every ant refuses it. Only the closest ant's acceptance gives the voice, and a special order speaks only when it goes to exactly one ant.
- Shift clicks and drags that add or remove ants post the selection text like any other selection.

## Touch Controls

A **tap** is the left click, a **hold** the right click (a ring closes around the finger, and the phone buzzes where the browser can), a **drag** the rubber band, **two fingers** move the map and a **pinch** zooms it. It is the game's own input: the commands, the selection and the zoom are the mouse's, and the simulation, the network and every state hash never see it.

| Action | Touch | Same as the mouse's |
|---|---|---|
| **Click, select, order** | Tap | Left click, at the point where the finger went down |
| **Right click** | Hold without moving (a ring closes, the phone buzzes where it can); lift to release | Right click (the game acts at the release); on the minimap: send the selected ants there |
| **Rubber band** | Drag (the finger leaves the slop) | Left drag |
| **Minimap** | Touch and move | Hold Left Button on Minimap |
| **Move the map** | Drag with two fingers | The edge scroll (the map follows the fingers) |
| **Zoom** | Pinch with two fingers | The wheel (the same levels, towards the fingers' middle point) |
| **Buttons, dialogs, other screens** | Tap | Left click (a finger there is a mouse until it lifts) |

Two fingers act on the map view of a match only: not under a dialog, the results screen or the catch-up screen of a network match, and not while a press is held on a button, the minimap or the chat log. The first finger's own press on the map (a rubber band or a right-button hold) does not block them: the second finger ends it with no selection and no order. Elsewhere a tap is still a click. A thumb that rests where no control took the touch (the frame, a blank part of the panel) never blocks the other finger. [`TOUCH.md`](TOUCH.md) has the numbers (the times and the slop), the page's guards for Android's Chrome and for iOS Safari, and what only a real phone can prove.

## No extra shortcuts

The keyboard is the original's ([Camera & Hotkeys](#camera--hotkeys)), plus the keys that the remake's own parts need:

- **`F2` / `F3`** vote while another player of a server's room has lost the connection, and **`Esc`** asks the quit question while this machine's own connection is lost (both are in the table below).
- **`T`** opens the chat line in the waiting room of a network match ([In a network room](#in-a-network-room)).
- **`Alt+Enter`** (native builds) belongs to the window and not to the game: it switches fullscreen on and off, as `--fullscreen` starts it ([`COMMAND_LINE.md`](COMMAND_LINE.md)). No screen sees that key (`Enter` alone is still START and the chat's send), and a held key toggles once. The web page has its own button.

There is no team switching: you play the team that `--player N` or `-pnum=N` gives you, or your seat in a network match. In a local game the other three colours stand still, unless you seat computer players with `--bot` or in the start menu's Single player rows. There is no tile grid or music mute key and no screenshot key (`--show-grid` and `--screenshot FILE` are command-line options), and no Space, arrow or letter hotkeys.

## Camera & Hotkeys

The original's keys (`Ants.exe` `FUN_0102609a`, [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 5.45) are F1, F9 - F12, Enter, Esc and Ctrl + A / H / L / N / O / P / Q / S, and nothing else; section 5.58 shows that the letters need Ctrl. The rows for the wheel, the middle button, F2 / F3 and Esc on a lost connection are the remake's, and so are `T` in a network room and Alt+Enter ([No extra shortcuts](#no-extra-shortcuts)). On macOS the Cmd key works as Ctrl.

| Key | Function |
|---|---|
| **Edge Scrolling** | Move the cursor to the edge: the arrow shows within 12 px, the view scrolls within the 5 px inner strip (every 50 ms, the same distance on the screen at every zoom). No arrow or letter key scrolls the view; only `Ctrl + N` / `Ctrl + P` bring an ant into view. The black bars of a wide window count as the edge, and in the browser so does the margin of about an inch around the game. The step sizes and the rest are in [`VIEW_AND_HUD.md`](VIEW_AND_HUD.md#edge-scrolling--minimap) (the original's input task: section 5.43). |
| **Mouse wheel** | Over the map view: up zooms in, down zooms out, one level a notch, towards the pointer. Not in the original: a remake addition, remembered in the settings (`zoom`). The levels, the limit and where the wheel acts are in [`VIEW_AND_HUD.md`](VIEW_AND_HUD.md#mouse-wheel-zoom); devices and direction are below the table. |
| **Middle button** | Over the map view: back to the zoom 1, towards the pointer (the same rules as the wheel). |
| **Typing** | The chat box is always active (while "Participate In Chat" is on): printable keys and Backspace go into it, **`Enter`** sends the text (to your team when you have an ally, else to everybody); the **[All]** / **[Team]** buttons send it to everybody / your team. |
| **`F9` - `F12`** | Send the four quick chat texts (fresh key presses only, chat on). |
| **`F1`** | Quick help (closes with `C`, `X`, `Enter` or `Esc`, or with its Return button at the release; a click elsewhere does nothing). The quick help at the start of the program (when "Show Quick Help at Startup" is on) has the same keys and a START! button. |
| **`Esc`** | Deselect everything (there is no quit dialog on Esc). The one exception is the remake's: while this machine's own connection is lost or the match is being caught up, Esc opens the quit question (a held Esc asks once). |
| **`F2` / `F3`** | The remake's network layer: while a vote is open (another player of a server's room lost the connection) keep waiting / go on without that player; a fresh press of the other key changes the vote. The original has no such keys ([`NETWORK_PORT.md`](NETWORK_PORT.md) "Reconnect (protocol 10): a player whose connection is lost can come back"). |
| **`Ctrl + A`** | Select all your ants (panel 3 for one, 4 for several, the voice of the first). |
| **`Ctrl + H`** | Select your home anthill (no hatching, no scrolling). |
| **`Ctrl + N` / `Ctrl + P`** | Select the next / previous ant (from the lowest selected one) and scroll just far enough to show it. |
| **`Ctrl + S`** | Stop the selected ants (no flash, no lock, no deselect). |
| **`Ctrl + O` / `Ctrl + Q`** | Options / quit dialog (quit dialog: `Y` yes, `N` or `Esc` no). |
| **`Ctrl + L`** | Show / hide every ant's hit points as white numbers. The numbers are **on by default**, a deliberate difference from the original, which starts with them off ([`AUDIT_ONE_TO_ONE.md`](AUDIT_ONE_TO_ONE.md) section 3b). |
| *In a team dialog* | The offer to team up (section 5.42): `A` accepts, `D` / `Esc` declines; while you wait for the answer: `W` / `Esc` withdraws the offer; "Doing this will break your team": `Y` yes, `N` / `Esc` no. A dialog takes every key and click until it is answered ([`VIEW_AND_HUD.md`](VIEW_AND_HUD.md#alliance-texts--chat-log) has the texts). |

The mouse wheel, by device:

- **Devices**: in the browser a trackpad's pinch and two-finger scroll work (the page cancels the browser's own scroll and page zoom over the game). In the desktop game it is the mouse wheel, and a trackpad's two-finger scroll where the system reports it as wheel events (an inertial tail can pass several levels in one swipe; **not tested on real trackpad hardware**).
- **Direction**: on the desktop the physical direction of the wheel decides (rolled away zooms in; the system's natural scrolling is undone). In the browser the system's natural-scrolling setting applies, because the page cannot see the physical direction.

**Buttons.** The top bar's buttons and the chat's [All] / [Team] buttons are the original's button class: a press captures the button and the click sound plays at the press, the action runs when the button is released while the pointer is still on it, and leaving the button cancels it (section 5.45).
