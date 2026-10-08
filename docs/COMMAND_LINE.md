# Command-line options

Every option of the game program `ants`, and the environment variables that it and its start scripts read. The dedicated server, `ants_server`, has its own options: see [`SERVER.md`](SERVER.md).

- `ants` means the game executable, `./build/src/ants_app/ants` after a build ([`BUILD_AND_RUN.md`](BUILD_AND_RUN.md)). `./start_game.sh` (Windows: `start_game.bat`) builds the game and starts it, and hands it every option that is not its own. The script's own options (`--players`, `--single`, `--dry-run`) are in [`BUILD_AND_RUN.md`](BUILD_AND_RUN.md).
- Run the game in the repository folder. It reads `Original-Ants/ants.chd` and `Original-Ants/Maps` relative to the current folder, and from another folder it stops with "Failed to load CHD archive".
- With no option that chooses a mode, a native game shows the desktop start menu (Single player, Join with a code, Host an online match, Quit, and "Rejoin your match" first while the game holds a fresh key for a match that still runs). Every option that chooses a mode skips it: those that start a match, a room, a test run or a screenshot, and also `--map-select`, `--play`, `--bot`, `--room`, `--token`, `--seat`, `--player` and `-pnum`. The options that only set something up (`--name`, `--seed`, `--settings`, the window options) do not. The menu and the list of options that skip it: [`CONTROLS.md`](CONTROLS.md).
- The web page starts the same game with a command line that it makes from its address: [`PLAY_IN_BROWSER.md`](PLAY_IN_BROWSER.md), "The game page and its addresses".

## How the options are read

- **Refused.** A wrong or missing value for `--aspect`, `--zoom`, `--prediction`, `--window-size`, `--server`, `--fill-bots`, `--bot`, `--teams`, `--room-map`, `--room-seats`, `--room-teams` or `--platform`, a `--bot` that cannot play, and `--start-menu` together with an option that starts a match or a room stop the game at the start. It prints `[Application]` and the reason on stderr, then `Failed to initialize Ants Application`, opens no window and exits with status 1. Only the first problem is reported.
- **Ignored without a word.** An argument that the game does not know (there is no `--help`), an option that needs a value when it is the last word, and a value that does not fit for `--room` (not a valid code), `--seat` (a number that is not 0 to 3; a word counts as 0), `--start-when` (not 1 to 4), `--grid` (not `CxR`), `--window-pos` (not `X,Y`), and a team that is not 0 to 3 in `-N` and `--team-name` (a word counts as 0 for `--team-name`).
- **Not checked.** A word where a whole number is needed (`--seed abc`, and the same for `--frames`, `--select-ant`, `--select-base`, `--player`, `--port`, `--lan-port` and the port of `--join HOST:abc`) gets no message: the program aborts with an uncaught C++ exception (`std::invalid_argument`, exit status 134). A number with too many digits does the same (`std::out_of_range`), also for `--host N`. With `--lan-list` a word for `--lan-port` counts as 0, and the system picks the port.
- **Looked for first.** `--version` and `--lan-list` are found before any other option is read. They need no window and no assets (native builds).

## Mode and map

| Option | Argument | Default | Meaning |
|---|---|---|---|
| `--map` | `PATH` | none | Skip the setup screen and start a match on that `.LVL` file at once, for example `Original-Ants/Maps/SMALL.LVL`. A run that skips the setup screen without `--map` (`--open-options`, `--scorecard`) plays the map that the setup screen highlights: `TREASURE.LVL`, or the first map of the list when the folder has no `TREASURE.LVL`. |
| `--play` | none | off | The game's own START at the first visit of the setup screen. The game goes through the loading screen and the quick help as ever, then starts `--map PATH` (else the map that the setup screen highlights) the way the setup screen's START does, instead of showing the setup screen: the "Get ready" dialog (the original's: [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 6.2, item 21), the start sound, the music and the seats of `--bot`. It is for a game that was chosen elsewhere: the web page's Play button gives it. Only that first visit is skipped, and only in a game of this machine: a room (`--host`, `--join`, `--join-url`) and the start menu are not affected. |
| `--map-select` | none | off | Start on the setup screen, the original's start, **without the start menu** (the menu is the default of a native game that has no mode option). |
| `--start-menu` | none | off | Show the start menu even where an option would skip it: `--headless` and `--screenshot`, for the tests and for screenshots, for example `ants --start-menu --headless --screenshot menu.bmp`. Refused together with `--map`, `--open-options`, `--scorecard`, `--host`, `--join` and `--join-url`. Never in the web build. |
| `--seed` | `N` | `1337` | The random seed of a local game. A room's host draws a random seed at START, so `--seed` does nothing there. |

## Teams, bots and names

| Option | Argument | Default | Meaning |
|---|---|---|---|
| `--player` | `N`, 0 to 3 | `0` | The team you control in a local game: 0 green, 1 red, 2 blue, 3 black. |
| `-pnum:<team>` (also `-pnum=<team>`) | the team is part of the word | none | The original's spelling of `--player`; the original has the colon ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) sections 5.50 and 5.63). `-pnum:2` is `--player 2`. |
| `--name` | `NAME` | see Meaning | Your name in the room, on the HUD label, in the chat, on the results rows and in the simulation's texts. Without `--name` the name that `-N` gives your team is used, else "Player" in a network game (it never sends your user and machine name), else the system user name, `user@machine` (see [Environment variables](#environment-variables)). A game that starts from the start menu uses the name in the menu's field instead: `--name`, else the settings key `name`, else "Player". |
| `-N<team><name>`, `--team-name <team> <name>` | a team, 0 to 3, and a name | no names | The name of a team (0 green, 1 red, 2 blue, 3 black) in a local game. `-N1Bob` and `--team-name 1 Bob` both name the red team Bob. `-N1Bob` is the original's spelling ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) sections 5.46 and 5.63). |
| `--bot` | `SEAT[:SPEC]`, repeatable | no bots | A computer player at seat SEAT (0 to 3, not your own). Repeat the option for more. |
| `--teams` | `ffa` or `A+B` | `ffa` (free for all) | The teams of a game: seats A and B (0 to 3, different) are a team. With `--join`, `--join-url` or `--host` it is this machine's choice for the START that it leads. |

### `--bot` in detail

- SPEC is `easy`, `medium` (the default), `hard`, `idle`, `worker` or `standard`, or `KIND:LEVEL` after the seat (`2:idle:hard`). For the standard bot a style may follow: `LEVEL:STYLE` or `KIND:LEVEL:STYLE` (`2:hard:raider`).
- The styles are `aggressive`, `economic`, `raider`, `defensive` and `random`. Hard plays aggressive or raider only. Without a style the bot draws its own at the start of every match.
- The game then has the seats that are taken (you and the bots). An empty seat has no hill and no ants.
- A bot is called "Bot (Medium)" (its level; the idle and worker bots are "Bot (Idle)" and "Bot (Worker)"), whatever its style, unless `-N` or `--team-name` names it.
- Refused with a message: your own seat, two bots on one seat, `--join` (a guest never runs bots, the host's machine does), and Fog of War on (checked at START: a bot would see through it).
- With `--host` the room shows the bots as players and the host's machine runs them.
- The match clock waits for the "Get ready to play!" dialog, so no bot looks or orders while it is up, and a bot opens with one token, not a burst: [`BOTS.md`](BOTS.md#fairness-in-detail).
- The grammar, the levels and the styles: [`BOTS.md`](BOTS.md#running-bots).

### `--teams` in detail

- Seats A and B are a team, and the two other seats too when both play.
- The first seat invites and the second accepts, with the original's own commands ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 5.11), before the first tick. So "... are a team now!" is in the chat log, no dialog opens, and the standard bot never breaks the team.
- When a seat does not play, or the pair would be the whole match (it would be over as soon as the pair's score is above 0), the game says so and starts without teams.
- With `--join`, `--join-url` or `--host` it is this machine's choice for a room's START that it leads (network protocol 13: every machine and the server make the same team before the first tick; a guest's is ignored). Teams that the seats which play cannot make are told to everybody in the room ("No teams: ..."), and the match starts without.
- A room that has teams of its own (its create block named them: `--room-teams`, below) makes those, for every start (the room that fills up included), and ignores `--teams`.
- The start menu's Teams rows (Single player, and the Host panel for three or four players) and the Teams select of the web page's card choose the same thing.
- More: [`BOTS.md`](BOTS.md#alliances).

## Network

How network play works (a room on the local network, a server's room, bots): [`MULTIPLAYER.md`](MULTIPLAYER.md).

| Option | Argument | Default | Meaning |
|---|---|---|---|
| `--host` | `[PORT]` | off; port `4001` | Open a room on this machine (TCP) and announce it to the local network. |
| `--join` | `HOST[:PORT]` | off; port `4001` | Join the room of a host. |
| `--join-url` | `URL` | none | Join through a server's WebSocket door (`ws://` or `wss://`): the browser build's way (the page passes it from `?join=`). A native game refuses it with a message (exit status 1): it joins with `--join HOST[:PORT]`. |
| `--room` | `CODE` | none | Join: the room of a server (letters, digits, `_` and `-`, up to 32; a code that does not fit is ignored). Without it the host is a LAN or direct host. |
| `--token` | `T` | none | Join: the credential that came with the room code (carried to the server, never interpreted by the game). |
| `--seat` | `N`, 0 to 3 | any free seat | When joining: ask for seat N (0 green, 1 red, 2 blue, 3 black). A seat that is taken gives the first free one. |
| `--room-map` | `treasure`, `tiny`, ..., or `FILE.LVL` | the server's own map | Join: the **create block** of the Hello (network protocol 15): the map of the room that a server with public rooms makes when it has none of the `--room` code. A plain word is the original's map of that name; a name with an extension is a file of the server's maps (the server offers only the maps that it lists, any other gets its own default). Refused when it is no map name. |
| `--room-seats` | `2`, `3` or `4` | `4` | Join: the create block's seats (the room starts by itself when that many people are in). Refused for any other value. |
| `--room-teams` | `ffa` or `A+B` | `ffa` | Join: the create block's teams (the room's own: it makes them for every start). Refused when the pair cannot be made. |
| `--room-leader-start` | none | off | Join: the create block's flag that a full room waits for its leader's START (so that the leader can arrange the colours first) in place of starting by itself. |
| `--platform` | `windows`, `macos`, `linux`, `android`, `ios` or `other`, with `browser-` in front for a game in a web page | this build's own system | What the Hello tells the room about this game (any case; cosmetic: nothing depends on it). The web page passes its own word. Refused for any other value. |
| `--server` | `HOST[:PORT]` | `beta.playants.org:4001` | The game server of the start menu's Join and Host; it overrides the settings key `server`. Refused at the start, with the reason, when it is not a host name, an IPv4 address or an `[IPv6]` address with an optional port (1 to 65535). |
| `--fill-bots` | `none`, `easy`, `medium` or `hard`, or four of them: `none,none,easy,hard` | `none` | The bots that this player's START seats in the empty seats of its room. One word is every seat's level; four words, joined by commas, are the levels of the seats 0 to 3. Any case; anything else, or no value, is refused. |
| `--port` | `N` | `4001` | The TCP port for `--host` and `--join`. |
| `--loopback` | none | off | With `--host`: accept only this machine (two copies on one computer). The room's announcement stays on this machine too. |
| `--lan-port` | `N` | `4001` | The UDP port on which an open room announces itself to the local network. Both machines must use the same one. `--lan-list` listens on it too. |
| `--no-lan` | none | off | Do not announce the room on the local network (guests then need the address). |
| `--start-when` | `N`, 1 to 4 | off | The leader of a server's room presses START itself once N players are in it (also when the room's create block asked for a full room to wait for its leader). The web front page's card gives it to the game of a match, and a headless test client uses it. |
| `--say` | `TEXT` | none | A test hook for headless clients: says the line once in the waiting room. |

### `--fill-bots`, `--start-when` and `--say` in detail

- `--fill-bots` acts on the **leader's** START in a server's room: the request carries the levels (a level for each seat since network protocol 13), the server seats a "Bot (Easy)", "Bot (Medium)" or "Bot (Hard)" in each empty seat that has a level, up to the room's players, and runs them, and one person is then enough. It acts on the **host's** START of a room on the local network too (this machine runs the bots, as for `--bot`). With Fog of War on, START seats none and the room says why. A game that is not a room ignores it.
- One word is every seat's level. Four words are the levels of the seats 0 to 3 (green, red, blue, black); the word of the seat that the leader holds counts for nothing.
- The start menu's Host panel sets it (a row for each seat after yours: "Red at START" and so on), and `--start-menu --fill-bots none,easy,none,hard` starts the panel's rows as it says. A player who joins through the menu fills nothing. The web page's `?fill=` gives it to the game of the room's leader (`web/shell.html`).
- The setup screen's status line says what START will do, for example "Press START: the empty seats get Medium bots."
- More: [`BOTS.md`](BOTS.md#running-bots) and [`NETWORK_PORT.md`](NETWORK_PORT.md#the-screens-and-the-applications-hooks).
- `--start-when`: when this game leads a server's room (the first player who joined) it presses START itself once N players are in the room, and again every second until the match starts. 1 is for a leader with `--fill-bots`, who can start alone. The web front page's card gives it to the game of a match (1 + its Friend rows), and a headless test client uses it. Nobody needs it to play from the menus: the leader clicks START.
- `--say`: the line is said as soon as two players are in the room. Everybody in a room can chat before the match. The lines go to the log on stderr (`Room chat: Name: text`), to the chat box of the 16:9 setup screen and, on the classic 640 x 480 screen, which has no box, to the status line for five seconds.
- To say a line yourself press **T** or click the chat box. **Enter** sends it and **Esc** closes it. A held Enter never starts a room's match (the original acts on a held Enter: [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 5.61), while the setup screen of a local game is the original's own and a held Enter still starts it. The keys, the 400 ms guard after the box closes and the box itself: [`NETWORK_PORT.md`](NETWORK_PORT.md#the-chat-box-of-the-169-setup-screen).

## Display, window and zoom

| Option | Argument | Default | Meaning |
|---|---|---|---|
| `--aspect` | `16:9` or `4:3` | `16:9` | The shape of the picture. `16:9` is a fixed 960 x 540 canvas. `4:3` is the original's fixed 640 x 480 picture, exactly ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 5.43); the 1:1 tests run on it. Anything else is refused with "only 16:9 and 4:3 for now". The settings key `aspect` gives the shape when `--aspect` is not given; a value that is neither is reported and ignored. The web page always passes `--aspect` (from `?aspect=4:3` or its own selector), so in the browser the key never applies. What the picture shows: [`VIEW_AND_HUD.md`](VIEW_AND_HUD.md). The window and the scale: [below](#fullscreen-the-window-and-the-scale). |
| `--zoom` | `0.05` to `2` | the level that the wheel left last, else `1` | The zoom of the map view that a match starts with, in digits and one point (`.5` and `1.0` are fine, `1e0` is not). The match starts at the nearest level that its map offers: the levels and the limit are in [`VIEW_AND_HUD.md`](VIEW_AND_HUD.md), "Mouse-wheel zoom". 1 is the original's picture ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 5.43), and a match of the network has the same levels as a local game. The settings key `zoom`, which the wheel writes, gives the level when `--zoom` is not given. A missing value is refused with "--zoom needs a number from 0.05 to 2", any other bad value with `--zoom "VALUE": a zoom from 0.05 to 2` (the value as typed). |
| `--fullscreen` | none | off | Start in fullscreen (native builds). **Alt+Enter** toggles fullscreen in the running game; the web page has its own button. |
| `--window-size` | `WxH` or `W,H` | see the notes below | The size of the window (native builds): two whole numbers of at least 320 x 240 and at most 100000, with `x`, `X` or a comma between them, for example `1280x720`. Anything else is refused with a message and the game does not start. A `--grid` cell wins over it. |
| `--window-pos` | `X,Y` | centred on the display | The position of the window (native builds): two whole numbers, negative ones allowed (a display left of or above the main one). A value that does not fit is ignored. A `--grid` cell wins over it. |
| `--grid` | `CxR` | no grid | Put the window into a cell of a grid of C columns and R rows (each 1 to 4, `x` or `X`) laid over the usable part of the display; `--cell` says which. The window is the largest client area of the game's shape (16:9, or 4:3 with `--aspect 4:3`) that fits the cell, title bar and frame included. A value that is not `CxR` is ignored. The start scripts lay four games out as a 2 x 2 grid with it. |
| `--cell` | `N` | `0` | The cell of the grid, counted row by row, 0 = top left (in a 2 x 2 grid 1 is top right, 2 bottom left, 3 bottom right). A number past the last cell is the last cell. |
| `--display` | `N` | the display that the window opens on | The display that `--grid` and the default window size are worked out on, counted from 0 as SDL counts them. |
| `--title` | `TEXT` | `Ants` | The window's title. |

### Fullscreen, the window and the scale

- SDL scales the canvas into the window by the largest scale that fits, centred, with black bars where the shapes differ. The scale is a whole number when the window is a multiple of the canvas (1920 x 1080 shows 960 x 540 at 2x, 3840 x 2160 at 4x) and fractional otherwise (2560 x 1440 at 2.667x, 1280 x 720 at 1.333x; a 2880 x 1800 window shows it at 3x with bars of 90 rows above and below).
- A 16:9 window opens at the largest scale in steps of 0.5 of 960 x 540 (1x, 1.5x, 2x, 2.5x ...) that fits the usable part of the display, and at least 1x. 1.5x of a Retina display's points is 3x in pixels, crisp. `--window-size` or `--grid` says otherwise. A 4:3 window opens at 1280 x 960.
- A game that starts in fullscreen has that window to go back to with Alt+Enter.
- Fullscreen shows the same canvas as large as the monitor allows, with bars on a 16:10 or 21:9 monitor. No option fits the picture to the monitor's own shape. The mouse over a bar counts as the picture's nearest edge pixel, so the map scrolls there ([`VIEW_AND_HUD.md`](VIEW_AND_HUD.md)).
- In fullscreen the mouse stays in the window while the game has the focus: SDL's fullscreen grabs the pointer, and so does a macOS fullscreen Space. A Linux window manager's own fullscreen toggle does not, and a window never holds the mouse.
- On macOS the Dock and the menu bar stay away from a fullscreen game. SDL's own fullscreen (`--fullscreen`, Alt+Enter) asks the system to hide them, and the game asks the same for a fullscreen Space that the green button or Cmd+Ctrl+F makes, and gives them back when it leaves.
- The version and the frame rate stay in the bottom right corner of the canvas.

## Audio, prediction and settings

| Option | Argument | Default | Meaning |
|---|---|---|---|
| `--audio-focus` | none | off | A window without the input focus is silent and holds its music (for several games on one machine): the piece goes on where it left off when the window has the focus again. Without this option the original's rule applies: leaving the program closes the music, and coming back starts a new random piece ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 5.24). |
| `--prediction`, `--no-prediction` | `on` or `off` | `off` | Whether a match of the network shows your own orders at once. `--no-prediction` is `--prediction off`. |
| `--settings` | `FILE` | the per-user application folder | Keep the remembered options in this file: see "Settings" in [`CONTROLS.md`](CONTROLS.md). A headless run keeps its settings in memory only, unless this names a file. |

### `--prediction` in detail

- `--prediction` takes `on`, `yes`, `true` or `1`, and `off`, `no`, `false` or `0`, in any case. Anything else, or no value, is refused with "--prediction needs on or off".
- It is off by default. The settings key `prediction` (`on` or `off`) says the same when the command line does not.
- On, an order is also run on a second engine and the screen shows that engine a few ticks ahead of the confirmed match. The confirmed simulation, the network, the hashes and the rules are untouched, and no other machine can tell.
- A game of one machine never predicts (it has no delay). A hidden web page, a pause, a catch-up, a host change and the "Get ready" dialog switch it off by themselves, and so does a prediction whose work takes too long: it stays off for a while and then begins again.
- The web page's `?prediction=on` gives the game `--prediction on` (`?prediction=off` says off).
- How it works: [`NETWORK_PORT.md`](NETWORK_PORT.md#prediction-of-ones-own-orders-predictionhpp-cue_routerhpp-netgame-applicationview_sim).

## Testing, headless and screenshots

These options are for the tests and for screenshots. `--say` (in the Network table) is a test hook too, and so is `--start-when`, which the web front page's card also uses.

| Option | Argument | Default | Meaning |
|---|---|---|---|
| `--headless` | none | off | A hidden window with the dummy video driver and no sound device (used by the tests). A headless run without `--screenshot` ends by itself after 10 frames. |
| `--screenshot` | `FILE` | none | Save a screenshot after `--frames` frames and exit. The file is a BMP of the picture without the bars, at the size that it has in the window (960 x 540 in a headless 16:9 run). A name ending in `.png` is saved as `.bmp` and converted only when `python3` with Pillow is installed; otherwise the `.bmp` stays and nothing is said (`--screenshot menu.png` leaves `menu.bmp`). |
| `--frames` | `N` | `5` | How many frames a `--screenshot` run lasts: the screenshot is taken one frame before the last, then the game exits. With 1 no screenshot is taken. |
| `--select-ant` | `ID` | none | Start with the ant of this id selected (for screenshots). |
| `--select-base` | `TEAM` | none | Start with the hill of this team selected (for screenshots). |
| `--open-options` | none | off | Show the options screen. The match starts at once. |
| `--show-grid` | none | off | Show the tile grid. |
| `--scorecard` | none | off | Show a sample results screen. The match starts at once. |

## Information

These two print their answer and exit. They need no window and no assets (native builds).

| Option | Argument | Default | Meaning |
|---|---|---|---|
| `--version` | none | none | Print the version, the build id and the network protocol, for example `ants v0.8.0 build abc1234 (network protocol 13)`, and exit. Run the executable itself: `./build/src/ants_app/ants --version`. The build id is the short git commit of the build, or `unknown` outside a git checkout; `-DANTS_BUILD_ID=TEXT` sets it by hand when CMake configures the build. The full rule, with the Docker images' own: [`BUILD_AND_RUN.md`](BUILD_AND_RUN.md), "Versioning and the Build Id". `ants_server --version` prints the same line for the server, starting with `ants_server`. |
| `--lan-list` | `[SECONDS]` | `3 s` | Do not start the game: listen for the rooms that the local network announces, print them and exit. The time is in whole seconds. `--lan-port N` picks the UDP port (4001 unless given); no other option is read. A room that has started its match is no longer announced ([`NETWORK_PORT.md`](NETWORK_PORT.md#lan-discovery-lanhpp-native-builds-v0078)). Exit status 0 when a room was heard, 1 when none was (a script can wait for a room), 2 when the port cannot be used. |

`--lan-list` prints a header, one line per room (`address:port  "host"  map  players/seats players  version`, the version being the host's own game) and a count:

```
Games on the local network (UDP port 4001, listening for 3.0 s):
  192.168.1.20:4001  "Alice"  TREASURE.LVL  1/4 players  v0.8.0
1 game found.
```

A room that has no map yet shows `(no map yet)`, and a room of another network protocol ends its line with `[another version of the game: protocol N, this one speaks M]`. With nothing on the network the answer is `No games found.` (exit status 1). With the UDP port in use it is `Cannot listen on UDP port 4001.` (exit status 2).

## Environment variables

The game reads no `ANTS_*` variable. The start scripts read these:

| Variable | Read by | Default | Meaning |
|---|---|---|---|
| `ANTS_PORT` | `start_game.sh`, `start_game.bat` | `4001` | The TCP port of the room of the test rig (`--players N`, 2 to 4): window 0 hosts on it and the others join it. A single game does not read it: give `--host PORT` or `--join HOST:PORT`. |
| `ANTS_NAMES_SEED` | `start_game.sh` only | not set | Makes the random names of the rig's players repeatable: the same value gives the same names. `start_game.bat` has no such variable. |
| `ANTS_CMAKE` | `start_game.sh` only | `cmake` | The cmake command that the script runs to configure and build the game. |

Other variables:

- `USER`, else `USERNAME`: the game reads it for the default name of a local game, `user@machine`. The machine name is cut at its first dot, and the whole name at 32 printable characters. `Player` stands for the user when neither variable is set. See `--name`.
- `ants_server` reads `ANTS_SERVER_SECRET`: [`SERVER.md`](SERVER.md).
- The Docker compose files read variables of their own. `ANTS_PORT` there is the host port of the web page (`19980` by default), not the room's port. See [`BUILD_AND_RUN.md`](BUILD_AND_RUN.md) and [`SERVER.md`](SERVER.md).
- The `ANTS_*` names that CMake knows (`ANTS_BUILD_ID`, `ANTS_WERROR`, `ANTS_USE_CCACHE`, `ANTS_BUILD_APP`) are CMake options, set with `-D` when the build is configured, not environment variables of the game or the start scripts (`ANTS_BUILD_ID` is also read from the environment by `build_web.sh` and the compose files, which pass it to the Docker build as a build argument): [`BUILD_AND_RUN.md`](BUILD_AND_RUN.md).
- The test programs and scripts have variables of their own (for example `ANTS_TEST_FILTER`): [`TESTING.md`](TESTING.md).

## Examples

Try it on one computer: `./start_game.sh --host --loopback --name Alice`, then in a second terminal `./start_game.sh --join 127.0.0.1 --name Bob`.

On a local network: `./start_game.sh --host --name Alice` on one machine. `ants --lan-list` on another prints the room (the sample above), and `./start_game.sh --join 192.168.1.20 --name Bob` joins it. The firewall of the host must let TCP port 4001 in; the machine that runs `ants --lan-list` must let UDP port 4001 in.
