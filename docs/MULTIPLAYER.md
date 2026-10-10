# Network play and bots

A network match is a lock-step game: every machine runs the whole match, and only the players' commands cross the network. This page says how to play online, how a match runs and where the limits are, and it describes the bots (computer players) that can take the other seats. [`NETWORK_PORT.md`](NETWORK_PORT.md) has the design, the wire format and the measurements, [`BOTS.md`](BOTS.md) has the bots, and [`SERVER.md`](SERVER.md) has the dedicated game server.

## Network Port

The original game runs a full TCP mesh on port 4001. Every machine simulates only its own team and broadcasts the results, and there is no host or join screen: an external lobby starts every machine with its roster on the command line. What the original does is recorded in sections 5.46 to 5.48 of [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md).

The remake runs **one deterministic simulation on every machine** and sends only the players' intent, a lock-step of commands. The host is the sequencer: it stamps every command with the sender's seat (a peer cannot speak for another player), seals a turn every 50 ms with the commands in canonical order and sends it to everybody. The host is a player's machine (a game on the local network) or a dedicated server (`ants_server`) that plays nobody (a room of the game server). This also makes web play possible.

There is no NAT traversal in the game. Over the internet every player connects out to a dedicated server, so players need no forwarded port, and the browser build can join it. A launcher or lobby can still start the game with `--host` or `--join HOST[:PORT]`; the original's roster options (`-P`, `-G`, `-H`) are not read.

### What you can do

- Host a room (`--host`) or join one (`--join`) over TCP, and list the rooms of a local network (`--lan-list`).
- Join and host rooms on the game server from the desktop start menu or the browser, and come back to a match after a lost connection.
- Fill the empty seats of a room with bots, or play against bots on your own computer.
- Chat in the waiting room (the T key on the setup screen) and in the match.
- Team up with another player (three dialogs: the invitation, the waiting dialog and the confirmation before a team is broken). A team line reaches only the sender and its ally.
- Play on when a player leaves. A game on the local network chooses a new host when the host leaves.
- Read `ping` and `delay` in the corner of the screen.
- Show your own orders at once (the prediction, off by default).

## Ways to play online

Every option of the game that is named on this page is listed in [`COMMAND_LINE.md`](COMMAND_LINE.md); the server's options are in [`SERVER.md`](SERVER.md) and the arena's in [`BOTS.md`](BOTS.md#running-bots).

### On a local network

One player hosts and the others join. A local network, a VPN or a forwarded port 4001 will do.

```bash
ants --host --name Alice               # on one machine: opens a room (TCP port 4001)
ants --lan-list                        # on another: lists the rooms that the network announces
ants --join 192.168.1.20 --name Bob    # joins the room of that host
```

- The setup screen becomes the **room**: a row for every player with the name and a thumb for the quality of the connection. A green thumbs up means a round trip under 1.2 s, a yellow sideways hand under 1.8 s, a red thumbs down more than that, and an orange question mark that it is not measured yet (the original's tiers, [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 5.48). [`CONTROLS.md`](CONTROLS.md) "In a network room" describes the screen.
- The host picks the map and the fog and presses START once every guest's thumb has appeared ("Press START when all players' thumbs have appeared."). A match needs two seats at least, and a bot is a seat.
- The host also picks the game mode with `--game-mode highest-score|187` (the default is the original's game; a guest cannot choose it, and `--game-mode` is refused with `--join`, `--join-url` and `--room`): the room tells every guest, the match's Start carries the mode and every machine makes its engine with it before the first tick, so all of them play the same rules. A game on a server has the mode of its room (`"mode"` in [`SERVER.md`](SERVER.md#the-room-specification), network protocol 17), and a guest's game plays whatever the room chose. 187 is described in [`GAMEPLAY.md`](GAMEPLAY.md); there is no screen to choose it yet.
- Everybody loads the same map file (a hash of the file is checked, and a machine whose file differs cancels the start) and the match begins for all at once.
- An open room announces itself on the local network (a UDP broadcast on port 4001, once a second and at once when the room changes). The host's firewall must let TCP port 4001 in (the room's port); the announcement is only sent out. A machine that lists rooms with `--lan-list` must let UDP port 4001 in. `--lan-port N` changes the UDP port of the announcement, and `--no-lan` switches it off.
- There is no in-game LAN screen: `--lan-list` shows what is on offer and `--join HOST[:PORT]` joins it. To try it on one computer, `--host --loopback` accepts only this machine and `--join 127.0.0.1` joins it.

### On the game server

- The desktop start menu has **Join with a code** and **Host an online match** ([`CONTROLS.md`](CONTROLS.md) "Start Menu"). They use the game server's TCP port, `beta.playants.org:4001` unless `--server HOST[:PORT]` (or the settings key `server`) says another. The room's code is shown with a Copy button, every failure and every refusal is told in words, and a network game that ends takes you back to the menu. **Rejoin your match (CODE)** takes your seat in a match that still runs back.
- From the command line the same is `ants --join HOST --room CODE`.
- The first player in a server's room is its leader (`early_start` in the room's specification, on by default). Its setup screen is the host's, with START, and it may start the match with the players who are there: two at least, or one alone with bots in the empty seats. A room whose seats are all taken starts by itself, unless its create block says that a full room waits for its leader (the leader can then arrange the colours first). The leader also moves a player to another colour: a press on the player's row puts that player in the next free colour, or, in a room that waits for its leader's START when it is full, changes places with the next player when every colour is held; the rooms of the front page and of the start menu start as soon as every colour is held ([`CONTROLS.md`](CONTROLS.md)).
- A room is a host that plays nobody. It seals the turns, runs the match on its own engine as the referee and compares every client's state hash with its own. [`SERVER.md`](SERVER.md) describes the server: the TCP and WebSocket doors, the control interface, reconnect and the Docker stack.

### In the browser

The front page [beta.playants.org](https://beta.playants.org) makes rooms and joins them ([`PLAY_IN_BROWSER.md`](PLAY_IN_BROWSER.md)). The game joins the server through a WebSocket (`--join-url`, which the page passes from `?join=`). A browser has no TCP, so the web build cannot host a room or join a LAN room.

Bots can take the empty seats of any room: see [Bots (computer players)](#bots-computer-players).

## How a match runs

Every change a player makes is a `Command` (a group move, special or attack, Stop, Hatch or an alliance command). `SimulationEngine::apply_command` validates it, and it is the only way a player changes the simulation.

### Turns and the jitter buffer

- A turn is 50 ms, one tick, and every machine runs the same turns.
- Every machine runs a turn after an **adaptive jitter buffer** that it sizes itself from how late the turns come: one turn (50 ms) on a steady link, up to four (200 ms) on a rough one. It grows at once when turns come later than it covers, or after a short stall (a late turn that came within 100 ms), and it shrinks by a turn after ten seconds without a stall.
- A machine that waits at a missing turn does not owe the wait. A queue that is longer than the buffer runs down at up to four times normal speed, in a long frame as in a short one.
- The feedback of your click (the ant's voice, the marker, the pedestal flash) comes at once, from a prediction of the order's acknowledgement. The order itself runs a few turns later.
- Every network match opens with the "Get ready to play!" dialog for 5 s. The original runs the clock and the ants behind it ([`AUDIT_ONE_TO_ONE.md`](AUDIT_ONE_TO_ONE.md) section 3b, [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 21), so the remake waits instead: the simulation starts when the dialog closes, and all the match's time is playable ([`GAMEPLAY.md`](GAMEPLAY.md) "The start of a match"). In a network match the host seals the first turn 5 s after the match began (a LAN host and a server's room alike), and every machine closes its dialog when its first turn runs. Nothing is sealed before that, so there is no waiting message, no lag notice and no pause, and no bot moves ([`NETWORK_PORT.md`](NETWORK_PORT.md) "Protocol 12").

### Ping and delay

- The corner of the screen shows `ping` and `delay` next to the frame rate, in the room and in the match (never in a game of one machine). `ping` is this machine's round trip to the host: the mean of the last five answers, a dash when the last answer is older than 3 s. In the browser the time that an answer waited for its frame is taken off, and a native window reads up to a frame too high. `delay` is the real time from sending one of your commands to the tick that applies it: the median of the last five, a dash after 10 s without a command. With the prediction on, `delay` shows what the click feels instead: from the frame that took the order to the frame that shows it.
- Without the prediction a click takes about the round trip plus 25 ms (the wait for the next seal) plus 50 ms (the buffer) plus the frames of the window (17 to 21 ms at 60 frames a second). Measured in a browser window at 60 frames a second: 99.5 ms at a round trip of 3 ms and 162.5 ms at 71 ms ([`NETWORK_PORT.md`](NETWORK_PORT.md) "What the player feels").

### Lag, silence and drop-outs

| Situation | What happens |
|---|---|
| A game on the local network (the host has a seat) | The host lets a peer fall up to 3 s behind, then stops sealing and waits for it ("Waiting for Bob...") and goes on when it is back. A player who sends nothing for 60 s is dropped (the original's drop-out time, [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 5.46). |
| A server's room: a slow player | The room never waits. The others play on and read "Bob is lagging (12 s behind)" from 3 s of lag. The laggard runs the turns it missed at up to four times normal speed ("Catching up..."; "You are lagging (12 s behind)" when its own link is the slow one). A player 60 s behind, or whose game has run nothing for 30 s, is dropped like a player who left, and is told "You were away too long and were dropped from the match." when it is back. |
| A server's room: a lost connection | A connection is lost when its link closes, a send fails or nothing at all arrives for 10 s. The room holds the seat and pauses the match for everybody. After 30 s of absence in all, the others may vote (more than half of those who are there; bots never vote) to go on without the seat. The player comes back with its key (a 128-bit secret that the Welcome hands out) and is given the match again, and after a pause of 3 s or more the match is held for a 10 s countdown before it goes on. The game does this by itself and shows "Connection lost. Reconnecting..." while it tries. The pauses of a match are capped: 30 minutes, and 10 in the demo rooms that the front page makes. |

A window that is slow, or frozen for less than 10 s, is lag and never a pause. A room that does not hold seats (a server started with `--no-reconnect`, or a room made with `"reconnect": false`) drops a player whose connection is lost instead. A server that restarts brings the matches that hold seats back, paused until the players return with their keys. [`SERVER.md`](SERVER.md) "Lag and lost connections", "Reconnect" and "Restart records" have the rules, the numbers and what the player sees.

A player who leaves, is thrown out or is dropped drops out at the same tick on every machine: its ants die, its alliance ends and "%s dropped out of the game!" is written into the chat log. A drop that leaves one team (or one allied pair) decides the match at once, and the quit dialog's Yes is a forfeit that ends the match for everybody when one other side is left (the `Quit` command), otherwise it is a drop-out. These are the original's end-of-match rules ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 5.47, [`GAMEPLAY.md`](GAMEPLAY.md) "End of the Match").

### Host migration

The original has no sequencer: it is a full mesh in which every machine simulates its own team, a peer that is lost is only a drop-out of its team, and there is no host migration ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) sections 5.46 and 5.47). The remake sends every command through one sequencer, so in a game on the local network the role of sealing turns moves to another machine when the host leaves: the host is only that role, because every machine already holds the whole match.

- While the map loads, every guest connects to the guests above its seat (each announces a port in its `Hello`, and the host passes the addresses on with the roster). Every machine keeps the last 30 s of turns.
- When the host's connection closes, or the host is silent for 10 s, the guests elect the lowest seat they still see alive. It fetches the turns it lacks, becomes the host and sends the others the turns they miss. Its first turn drops the old host, and any seat that did not follow, on every machine at the same tick.
- The game shows "The host left. Choosing a new host..." while it happens and "Bob is the host now." afterwards. Orders that were not yet in a sealed turn when the host went are lost and must be given again. A match with one machine left goes on for it alone.
- A server's room has no host to lose and never migrates. [`NETWORK_PORT.md`](NETWORK_PORT.md) "Host migration" has the election and the resync.

### Checks and flood control

- Every 20 ticks (one second) the machines compare a hash of the whole gameplay state (`SimulationEngine::state_hash`), in seven named parts (engine, players, grid, food, ants, paths, droppers). A mismatch names the peer and the subsystem and freezes the match.
- Malformed, flooding or host-only messages are counted, and a peer is thrown out after eight strikes. A connection may send 1000 messages a second, with a burst of 1000. A START request that a host cannot honour costs nothing up to 16 times (a person clicks twice): the 17th and later are offences.
- The TCP inbox of a connection is bounded (4096 messages, 1 MiB), so a client that floods a server is held back, is thrown out after a second's worth at the most and cannot grow or slow the server. [`SERVER.md`](SERVER.md) "Flood control" and [`NETWORK_PORT.md`](NETWORK_PORT.md) "Flood control" have the rest.
- The network protocol number (`kProtocolVersion` in `include/ants_net/protocol.hpp`) changes with every change that the peers of a match must share: the messages and the simulation's rules (any fix that changes a state hash of some play). A game of another number is refused by a LAN host and by a server's room ("This version cannot play with the host's version."; the web page adds "Reload the page to update."), so update the game. The LAN list shows a room of another number as another version.

### Prediction of your own orders

The prediction is **off by default**. Switch it on with `--prediction on`, the settings key `prediction` or `?prediction=on` on the address of a network game (a `?join=...` link; the front page does not pass it on) (`--prediction off` and `--no-prediction` switch it off, and the command line wins). There is no protocol change, and no other machine can tell.

- On, a second engine (`net::Prediction`) shows your own orders at once: the confirmed engine, plus the turns that are in hand, plus your own orders that no turn has carried yet, advanced by a lead (the median lag of your last five orders). The ants answer a click as they do in a game on your own computer. The second engine is rebuilt only when a confirmed turn disagrees with what it assumed.
- The screen, the HUD's picks and your own action sounds read it. The news, the combat sounds, the hashes, the bots and the end of the match stay the confirmed engine's. Group moves, specials, attacks and Stop are predicted. Hatching and the alliance commands wait for their turn.
- It is off by default because the ants of other players hop about a dozen pixels when their orders arrive. It is off in a pause, a catch-up, a host change, a hidden tab and before the first tick (the "Get ready to play!" dialog).
- When its work costs more than 12 ms of the thread's CPU time four times within ten seconds, it switches itself off for 10 s, and each further time for twice as long, up to 160 s.
- [`NETWORK_PORT.md`](NETWORK_PORT.md) "Prediction of one's own orders" and [`audit/rollback_notes.md`](audit/rollback_notes.md) have the design and the measurements.

## Limits

- No late join: a running match takes nobody new (the original has none either). Only a seat that was lost comes back, with its key, in a server's room.
- A game on the local network has no pause, as in the original. A server's room pauses only for a player whose connection is lost.
- A room on a local network is found with `ants --lan-list` and joined with `--join HOST[:PORT]` over TCP (a LAN, a VPN or a forwarded port 4001). There is no in-game LAN screen.
- The start menu joins and hosts on the game server only.
- The browser build joins a game server over a WebSocket (`--join-url`). It cannot host a room or join a LAN room.
- A LAN host that dies in the first second of the match, before the links between guests are made, can split it.
- When a network match ends, Leave on the results screen ends the program, as in the original ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) sections 5.49 and 5.63), unless the game was started from the start menu: then it returns to the menu ([`CONTROLS.md`](CONTROLS.md) "After a network match").
- A player has one pending team offer at a time (the original queues several, section 5.42).
- Names are printable ASCII, up to 32 characters.

## Bots (computer players)

A bot is a **virtual client**: a seat whose commands are produced by a program (the `ants_ai` library) instead of a person. It reads the world through a read-only copy of what a player of its seat can see and sends the same `Command`s that a mouse click makes, through the same door: the simulation of a local game, the sequencer of a room. The simulation validates every command, so a bot has a person's powers and cannot bend a rule. A game without bots runs no bot code, and no state hash changes.

The 1:1 core has no computer players: bots live in `ants_ai` only (project rule 5 in [`AGENTS.md`](../AGENTS.md)). The only computer behaviour inside the simulation is the original's own auto-engage reflex of the Combat Ant ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 5.36).

### The rule

- **Off by default and always visible.** A bot exists only when asked for: `--bot`, the start menu's Single player rows, `--fill-bots` or a room's `"bots"`. A bot seat is named "Bot (Easy)", "Bot (Medium)" or "Bot (Hard)" (the idle and worker bots are "Bot (Idle)" and "Bot (Worker)") on the HUD labels, in the chat log and on the results screen. In a local game `-N` / `--team-name` may rename a seat; a room never does. In a room a bot is a slot of its own kind (`Bot`) that a person cannot take, nor fake with a name that starts with "Bot (".
- **Fair.** A bot has a human pace. It looks at the world every 0.2 s (Hard) to 5 s (Easy) and acts a reaction time later (0.4 s to 3 s, plus or minus 25 percent from the seat's own random numbers). A token bucket limits its commands to 3.0, 1.5 or 0.4 a second (Hard, Medium, Easy), with bursts of 10, 6 and 2. Every bot starts with one token, so its first orders come one by one.
- A bot never sends more than 24 ants in one command (a chosen cap: the HUD lets a person send 32), a special order names one ant, and it never quits, never attacks an ally, never orders an ant that is not its own and never orders the same ant twice within half a second unless urgent.
- With Fog of War a bot would see through the fog, so a bot together with fog is **refused** everywhere (`check_setup`, the room, `NetGame`, the controller).
- [`BOTS.md`](BOTS.md) "The rule", "Fairness in detail" and "Difficulty levels" (the table of the three levels) have the details.

### Where bots come from

- **Local games**, with `--bot SEAT[:KIND][:LEVEL][:STYLE]` (repeatable):

  ```bash
  ants --map Original-Ants/Maps/TINY.LVL --bot 1:medium                           # you at seat 0 against one bot
  ants --player 2 --bot 0:hard --bot 1:easy --bot 3:easy                           # you at seat 2 against three bots
  ants --map Original-Ants/Maps/TREASURE.LVL --bot 1 --bot 2 --bot 3 --teams 0+1   # you and the red bot against the other two
  ```

  SEAT is 0 to 3 (green, red, blue, black), not your own. KIND is `idle`, `worker` or `standard` (the default), LEVEL is `easy`, `medium` (the default) or `hard`, and STYLE is `aggressive`, `economic`, `raider`, `defensive` or `random` (only the standard bot has a style, and a Hard bot plays only `aggressive` or `raider`: `--bot 2:hard:economic` is refused). A game with bots has the seats that are taken, you and the bots: an empty seat has no hill and no ants.
- **The start menu**: Single player offers Empty, Easy bot, Medium bot or Hard bot for each of the three other seats and starts the match with exactly the bots that `--bot SEAT:LEVEL` would give. The panel says "Bots play without fog of war." and remembers the choice in the settings file ([`CONTROLS.md`](CONTROLS.md) "Single player").
- **Teams before the game**: `--teams 0+1` makes seats 0 and 1 a team, and the two other seats too when both play (`--teams ffa`, free for all, is the default). The start menu (Single player and the Host panel) has a Teams row, and each colour of the web front page's lobby a Team 1 and a Team 2 button. A room's teams are chosen before the start too (network protocol 13): the leader's choice, or the room's own teams (set when the room was made, in the create block of its first Hello: network protocol 15), which hold for every start, the automatic start of a full room included. The players of a room with no teams can still team up in the match.
- **A bot that declines your invitation to team up says why.** The original's "%s rejected teaming up" stays (string 80, [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 5.42) and one line of the chat log follows it: "Bots team up only while three or more teams play.", "You already have a teammate.", "<name> already has a teammate." or "This bot never teams up." (the worker bot).
- **Your room on the local network**: `ants --host --bot 2` seats a bot at seat 2. Guests take the first free seat, the room shows "Bot (Medium)" with the good thumb, and START never waits for it. The host's machine runs the bot and a guest never does. Its commands enter the host's sequencer with the bot's seat, so every machine sees the same turns and the same state hash. If the host leaves, its bot leaves with it (the new host drops the seat in its first turn, like any seat that did not follow). `ants --host --fill-bots hard` fills the empty seats at the host's START.
- **A server's room**, two ways. The room's specification names the bots: `"bots": [{"seat": 2, "bot": "medium"}]` in `POST /rooms` (`"hard:raider"` pins a style; at least one seat stays for a person; never with Fog of War; the status JSON lists them, with their `style`, "random" for a bot that draws its own). Or the leader's START carries a level for each seat: `ants --join HOST --room CODE --fill-bots medium` (or four words, `none,none,easy,hard`; the desktop Host panel's rows "Red at START" and so on, and the web front page's card, do the same). The server then seats a "Bot (Medium)" (or the level of that seat) in each seat that is still empty and has a level, up to the room's players, and starts with the people who are there. Only the leader's START adds bots (a room that fills up, or whose time ends, never does), Fog of War refuses them and the leader is told why, and a cancelled start takes them away again. The referee runs them, so every player's game stays identical to the referee's. A bot is never absent and never votes. [`SERVER.md`](SERVER.md) "Bots fill the empty seats, and chat in the waiting room" and "The room specification" have the details.
- **The browser**: the front page is a lobby, a room that exists when it opens. The host gives each colour that no person holds **Open**, **Easy**, **Medium**, **Hard** or **Nobody**, and START seats the bots of that plan in the room (the room holds the plan: protocol 16), once every person's game is in; with nobody else in the room and no computer player (every other colour **Open** or **Nobody**), START plays a game for one on this computer instead, with no room and no colony but yours (`--alone`: [`PLAY_IN_BROWSER.md`](PLAY_IN_BROWSER.md)).
- [`BOTS.md`](BOTS.md) "Running bots" has the rest.

### What a bot sees and how the bots play

A bot reads the world through `BotView` and `MapInfo`, a copy of what a player of its seat can know. Its own ants are exact, and every ant's hit points are in the view (a number from 1 to 10, as the screen shows them); other teams' carried points, eggs and orders are not in the view at all. [`BOTS.md`](BOTS.md) "Fairness in detail" lists the few things that a bot sees and a person would have to count or guess.

- **`idle`** stands still and only reads the world (the test bot).
- **`worker`** only harvests, and never hatches, fights, raids or uses a power-up. It is the fixed yardstick that other bots are measured against.
- **`standard`** is the bot that people meet: the worker's economy plus the tactics of its level and one of four styles (`aggressive`, `economic`, `raider`, `defensive`; a Hard bot plays only `aggressive` or `raider`, and on Easy a style only moves the numbers). Its tactics include the contest play (a race for contested food, fights for the kill, fire walls lit only where they cannot simply be put out, a harder game when it is behind) and, on ISLANDS and SMALL, the island play (a crew flown over the water by bomb flights, Swimmers that carry the food home). A standard bot draws a style for each match from its seat's own random numbers, or you pin it (`--bot 2:hard:raider`). It sends fewer orders that cannot work (the gate, the rescue of a carrier and the carrier aid look at the map as it is now, and a thief raids no hole that an ant holds), so the bot's own refused orders ("Can't go there.") are few, and `bot_arena` counts them ([`BOTS.md`](BOTS.md) "The can't-go loop"). Its name stays "Bot (Level)".

What each level and style does, how every tactic was judged (the win rate and the margin against opponents that fight) and the tables of numbers: [`BOTS.md`](BOTS.md) "Difficulty levels" and "The standard bot", and "Measurements" in [`BOTS_history.md`](history/BOTS_history.md).

### The arena

`bot_arena` plays one or many matches of bots headless with the real engine, bit-reproducibly, and writes a JSON report. It is built on request (`cmake --build build --target bot_arena`) and is not part of the normal build. Its options (`--map`, `--seeds`, `--seat`, `--rotate`, `--replay-check`, `--latency-ticks`, `--out`, `--selftest`, `--write-baselines`) are in [`BOTS.md`](BOTS.md#running-bots).
