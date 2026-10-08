# The dedicated server

`ants_server` is a headless program that hosts many online matches at once. Players only need to reach it: there is no NAT traversal and no port forwarding on their side. This page covers its rooms, options, control interface, public status, flood limits and Docker stack; the network rules, numbers and tests are in [`NETWORK_PORT.md`](NETWORK_PORT.md), and the game's side of network play (LAN, joining, bots) is in [`MULTIPLAYER.md`](MULTIPLAYER.md).

## What the server is

- A **room** is a host that plays nobody: it seals a turn every 50 ms, runs the match on its own engine as the **referee** and compares every client's state hash with its own. A room is `waiting`, `loading`, `running`, `finished` or `failed`.
- Rooms come from a lobby's backend, which makes them with the control interface, or from a public page that has no secret and makes **demo rooms** by itself ("Demo rooms" below).
- The players' game windows start with `--join HOST:PORT --room CODE` ([`COMMAND_LINE.md`](COMMAND_LINE.md)); a browser joins through the site's `/ws`.
- There are two doors: a TCP port for native clients, and a WebSocket port for browsers and Electron behind a reverse proxy that ends TLS. Each holds 64 waiting connections. A connection that does not say Hello within 10 s is closed. A Hello for another protocol, or for a room that does not exist, is rejected.
- A match starts by itself when the expected seats are taken and everybody has loaded the map. It starts earlier when the room's **leader** (the first player who joined) presses START with at least two players there, or alone with bots in the empty seats ("The leader and the early start", below).

## Starting a server

```bash
cmake --build build --target ants_server
./build/src/ants_server/ants_server --maps Original-Ants/Maps --port 4001 --ws-port 4002 --ctl-port 4010 --results-dir results
```

[`BUILD_AND_RUN.md`](BUILD_AND_RUN.md) has the build. Without `ANTS_SERVER_SECRET` in the environment this run makes the control secret itself: it creates `results/control-secret` and prints the secret once in its log (see "The control secret" below). `.gitignore` covers `results/` and `control-secret`, so they are never committed by mistake.

The log goes to the standard error, and the number after `ants_server` is the seconds since the start. On that first start it begins like this (the build id is the short git commit):

```text
[ants_server 0] control secret made now and stored in results/control-secret (owner-only); it is shown here this once: <64 hex digits>
[ants_server 0] ants_server v0.8.0 build abc1234 (network protocol 13), maps in Original-Ants/Maps
[ants_server 0] ...
[ants_server 0] TCP game port 4001 (this machine only)
[ants_server 0] WebSocket port 4002 (this machine only: put a TLS proxy in front)
[ants_server 0] control interface on port 4010 (this machine only, bearer secret)
```

A later start does not print the secret again. With demo rooms, a line "public rooms on: ..." comes before the version line.

### Options

| Option | Default | Meaning |
|---|---|---|
| `--maps DIR` | required | The folder of `.lvl` files that rooms may use. A room whose map is missing, invalid or does not load is refused. A folder that does not exist stops the server with status 2. The Docker image brings the six maps of the original game in `/maps`; a mounted folder replaces them. |
| `--port N` | 4001 | TCP game port of native clients (0: none). This machine only unless `--public`. A server needs a game port: `--port 0` without `--ws-port` stops it with status 2. |
| `--ws-port N` | 0 (none) | WebSocket port of browsers and Electron behind a reverse proxy that ends TLS. This machine only. It also answers the public status `GET /busy` and the site statistics `GET /stats` and `POST /stats/local` (below). |
| `--ctl-port N` | 0 (none) | Control interface: HTTP + JSON on this machine only, `Authorization: Bearer <secret>`. The secret comes from the environment variable `ANTS_SERVER_SECRET`; without it the server makes one ("The control secret"). |
| `--public` | off | The TCP game port accepts other machines. |
| `--ws-any-interface`, `--ctl-any-interface` | off | For containers only (a published port does not reach a program on the container's loopback address): listen on every interface and let the host's port mapping decide who may connect. |
| `--results-dir DIR` | none | An ended room (not a demo room) writes `<code>.json` there. The site statistics (`site-stats.json`), the control secret that the server makes (`control-secret`) and the restart records (the folder `restart`, see `--restart-dir`) are kept there too. The log has one line for every ended room (a demo room that never ran a tick has none). |
| `--secret-file PATH` | `control-secret` in the results folder | Where the server keeps the control secret that it makes when `ANTS_SERVER_SECRET` is not set. An explicit `--secret-file` always wins over that default, also when `--results-dir` is given (the Docker image always gives `--results-dir /results`). A file that exists there is used as it is. |
| `--max-rooms N` | 256 | The most rooms at a time. |
| `--reconnect`, `--no-reconnect` | on | Rooms hold the seat of a player whose connection is lost ("Reconnect"). A room's own `"reconnect"` overrides it, and demo rooms follow it. On by default since v0.6.0. `--no-reconnect` turns it off: a lost player is dropped at once, and no restart record is kept except for a room that asks for `"reconnect": true`. The last of the two options wins. |
| `--hold-vote-seconds N` | 30 | The others may vote on going on without a seat once it has been away N seconds in all (5 - 3600). |
| `--max-pause-seconds N` | 1800 | The cap on a match's total paused time: at the cap every seat that is not present is dropped (60 - 86400). A demo room takes the smaller of this and 10 minutes. |
| `--max-catch-up-seconds N` | 300 | The time that one absence may spend catching up, over all its attempts: then the catch-up fails, the seat is absent (the vote and the cap apply) and its key is refused (10 - 3600). |
| `--resume-countdown-seconds N` | 10 | After a pause of 3 s or more the match is held this long before it goes on; the players are told the seconds (0 - 60, 0 = none). |
| `--log-mb N` | 16 | The limit of one room's turn log, which a returning player is given the match from (1 - 256 MiB). All the rooms' logs together may take 256 MiB. |
| `--restart-dir DIR`, `--no-restart-records` | the folder `restart` in `--results-dir` | Where the restart records of the rooms that hold seats are kept, and the option that keeps none (a running match then ends with the server). Without a results folder the server keeps none and its log says so. A `--restart-dir` that cannot be used stops the server with status 1. The two options exclude each other (status 2). |
| `--restart-vote-seconds N` | 90 | After a restart the others may vote on going on without a seat that has not come back once it has been away N seconds (30 - 3600; never less than the room's own vote time). |
| `--restart-budget-mb N` | 256 | The disk that all the restart records together may take (1 - 4096; one record is at most 48 MiB). |
| `--demo-rooms N`, `--demo-map NAME`, `--demo-maps A.LVL,B.LVL,...` | off | Demo rooms for a public page that has no secret (`web/lobby.html`): see "Demo rooms" below. |
| `--demo-lobbies N` | 200 with `--demo-rooms` and reconnect, else 0 | The most lobby rooms (network protocol 16) that wait at a time, for a page that waits in a room from its first second: see "Lobby rooms" below. 0 switches them off. |
| `--help`, `-h`, `--version` | | `--help` prints the usage and exits. `--version` prints the version, the build id and the network protocol, for example `ants_server v0.8.0 build abc1234 (network protocol 13)`, and exits. The server's log says the same near its start. |

A value outside its range, or one that is no number, stops the server with status 2.

### Exit statuses

- `0`: the server was stopped by `SIGTERM` or `SIGINT` (its log says "stopping"), or `--help` or `--version` was asked.
- `1`: a port cannot be opened (the log says "cannot listen on ... port N"; the control port also refuses an `ANTS_SERVER_SECRET` that holds anything but visible ASCII characters), or an explicit `--restart-dir` cannot be used.
- `2`: a mistake in the options (an unknown option, a missing or out-of-range value, `--maps` missing or not a folder, no game port, `--restart-dir` together with `--no-restart-records`, a demo-room or lobby-room mistake), a secret file that is no usable secret, or a control port with no secret and no place to keep one.

### Demo rooms

`--demo-rooms N --demo-map NAME` let a public page that has no secret (`web/lobby.html`) make rooms by itself: a Hello that brings a **create block** (network protocol 15) for a room that does not exist makes it, at most N at a time. Whoever reaches a game port (the TCP port, or the WebSocket port through the site) can fill these rooms.

- N is 1 up to one less than `--max-rooms` (255 with the default 256), so that rooms made by the control interface keep their places. Leave the option out to switch demo rooms off (the default); 0, or a number that is too large, stops the server at startup.
- `--demo-map` must name a map of the maps folder: it is the map of a room whose block names none that the server lists. `--demo-maps` needs `--demo-rooms`.
- **The create block says what the room is**: the map (a file name of the maps folder), 2 to 4 seats, the room's own teams (a pair of seats, as `0+1`, or none) and whether a full room waits for its leader's START ("The leader and the early start" below). The code is only a name: any code of 1 to 32 letters, digits, `-` and `_` (the pages and the menu make six random characters, `k7m2xq`) that has **no upper-case letter**: the codes of the control interface have capitals (the server draws them so), and an operator who names a room by hand gives it one, so that a visitor's Hello cannot take the name of a room that is yet to be made. The map is the block's when `--demo-maps` lists it (compared in any case), and `--demo-map` when it does not or when the block names none, so a page that offers a map that this server lacks still gets its room, and the room's first message tells which map it has. The first Hello of a code decides: a room that exists ignores the block of a later Hello. A Hello with no block, a Hello that shows a key, a code with an upper-case letter and the code of a room of the control interface that is over never make a room: `NoSuchRoom` (a Hello for a room of the control interface that is still open joins it, and its block is ignored). Details: [`NETWORK_PORT.md`](NETWORK_PORT.md#protocol-15-one-link-makes-a-room-short-room-codes-and-the-leaders-swap-v0110), "Protocol 15".
- `--demo-maps` takes file names separated by commas (blanks around a name are dropped, at most 64 names). Every name must be a map of the maps folder, or the server stops at startup.
- A demo room waits ten minutes for its players (a friend on another computer needs time), and the pauses of its match may last ten minutes in all (see `--max-pause-seconds`). A match that nobody comes back to gives its place up: when a Hello needs a place and none is free, the demo room that nobody has been at for the longest (a minute at least) is ended.
- A Hello that brings a block for the code of a demo room that is over (finished or failed) makes a new room at once (the old one's end is still reported): a late friend, a reload, a rematch with the same link (the links of a room that the front page made carry the block). A Hello without a block, and a Hello that shows a key, is not served this way: that player's match is gone, and it is told so (`NoSuchRoom`). A room made by the control interface that is over answers `NoSuchRoom`.
- A demo room writes no `<code>.json`.

### Lobby rooms

`--demo-lobbies N` (network protocol 16) lets the front page wait in a room from its first second, before anybody has chosen a match. A **lobby page's** Hello (client kind `kClientPage`) with a **lobby create block** (`kCreateLobby`: four seats, the leader starts) for a code that is no room makes a **lobby room**; the same Hello for a room that exists joins it. A page never makes a public room and a game never makes a lobby room (a Hello whose kind and block do not agree is `BadRequest`), and a Hello with a key never makes a room, as for every room. The rules are in [`NETWORK_PORT.md`](NETWORK_PORT.md#protocol-16-the-lobby-room-the-front-page-waits-in-v0120), "Protocol 16":

- **It lives while a person is in it.** The first player to join leads. The leader sets the plan (the map, what each colour is: the next player, an easy, medium or hard bot, or nobody, and the teams) and everybody sees every change; the players who join take the colours that the plan calls open. When the leader goes, the player who has been in the room the longest leads; a bot never does.
- **A seat whose link ended is held for a minute**, with its colour and its key, and a link that has said nothing for 30 seconds is closed (a page whose machine sleeps): a reload or a lost connection comes back to its seat with the key. A player who leaves (the Leave message) goes at once.
- **An empty lobby is forgotten a minute after the last player has gone**, without a line in the log (no match was begun: nobody played) and with no result file. There is no timer while a player, or a seat that is held, is in it. A Hello for its code then makes a new lobby.
- **The leader's START waits for every player's game.** The pages go to the game page and take their seats over with their keys; the room starts the match when every person is a game, and the leader is told whose game did not come when 90 seconds are over. Then the server loads the plan's map (it must be one of `--demo-maps`, or `--demo-map`), seats the plan's bots and takes a place among the public matches. What it cannot do (the map is lost, the colours cannot play the map, no place is free) ends the START with a notice for the leader, and the lobby goes on waiting.
- **A waiting lobby holds no place among the `--demo-rooms`**; its START asks for one (a place that is free, or one that a public match which nobody has been at for a minute gives up), and the match is a public room like any other. A lobby is no match, so the people in lobbies are in the `players` of `GET /busy` but never in its `matches`, and never hold a deploy back (a restart ends every lobby: the page makes its room again).
- **The pool.** At most N lobbies wait at a time. When all are taken, the lobby that has been empty the longest is forgotten to make room for a new one; when every one has a person in it, the new page is told `NoSuchRoom` (the page says that the server is busy).
- **N** is 0 up to `--max-rooms` - `--demo-rooms` - 1, so that rooms made by the control interface keep their places. Without the option a server that has `--demo-rooms` and keeps seats (reconnect, the default) offers 200 (fewer where `--max-rooms` leaves no more room; none where it leaves none, and the log says so); a number above 0 needs `--demo-rooms`, `--demo-map` and reconnect, and a lobby room together with `--no-reconnect` stops the server at startup, as does a number that is too large. `--demo-lobbies 0` switches lobbies off.
- Whoever reaches a game port can fill the pool and hold it with connections that stay: that is the price of a page that needs no secret, as for the public rooms (nothing behind the site's nginx tells one address from another). A waiting lobby is cheap (no thread, no match, one copy of the map), the people in it are not counted among the matches, and a lobby that nobody is in goes after a minute.

## The control secret

- `ANTS_SERVER_SECRET` in the environment is used as it is (any length; a character that is not a visible ASCII one, such as a space, a control character or an accented letter, stops the server with status 1, "cannot listen on control port").
- Without it the server makes a random secret (32 bytes of the operating system's generator, 64 hex digits) the first time it starts, stores it in the secret file and prints it once in its log, **the moment it is made**: after the options are read, and before the ports are opened, the demo-room checks and the restart folder, so a start that fails at those has shown it all the same. A mistyped option stops the server before any secret is made.
- Every later start reads the same file, so a container needs no setup. `docker exec ants-server cat /results/control-secret` shows the secret again; keep `/results` in a volume.
- On POSIX the file is for its owner only (mode 600; a umask can only make that stricter). **On Windows the server sets no permissions: the file takes those of its folder, which must be private.**
- The file appears all at once and complete (the server writes a temporary file next to it and links it to its name), so servers that start at the same moment get the same secret and a crash never leaves half a secret. This needs a file system with hard links; otherwise the server says so: give `ANTS_SERVER_SECRET`, or `--secret-file` a place on another file system.
- **A file that already exists is used as it is, whoever made it:** its owner and its mode are your responsibility (the server neither checks nor changes them), and a symbolic link is followed (a mounted secret is often one; a link to nothing is an error, and nothing is made behind it).
- A file that is no usable secret stops the server with a message and is never overwritten; delete it to get a new secret. Unusable are a directory, a device or a FIFO; an empty file; one shorter than 32 or longer than 256 characters; one with a space, a control character or a byte that is not ASCII in it (a second line is one: only the line end and the blanks at the end of the file are dropped first); and one of more than 1,024 bytes (the server never reads more than 1,025 bytes of a file).
- With neither the variable nor a place for the file (`--results-dir` or `--secret-file`), a control port refuses to start (status 2, with a message).

## The control interface

HTTP + JSON on `--ctl-port`, on this machine only unless `--ctl-any-interface`. A lobby's backend makes, inspects and closes rooms with it. Every call except `GET /healthz` needs `Authorization: Bearer <secret>`; a missing or wrong secret is 401. Another method on a known path is 405, and an unknown path is 404.

| Call | Answer |
|---|---|
| `POST /rooms` with a JSON body | Makes a room. 201 with the room's status, or 400 (a bad body or value; the reason says which), 404 (no such map), 409 (the code is taken), 422 (the map is in the folder but does not load) or 503 (no room for another room: `--max-rooms`). |
| `GET /rooms` | 200 `{"rooms": [status, ...]}`. |
| `GET /rooms/<code>` | 200 with the room's status, or 404. |
| `DELETE /rooms/<code>` | Closes the room: 200 with its status after the close (a room that was not finished is `failed`, with the reason "closed by the owner"), or 404. |
| `GET /stats` | 200 `{"rooms", "pending", "created", "refused", "log_bytes", "log_budget_bytes"}`: the rooms now, the connections that have not said Hello yet, the rooms made so far, the connections that the door refused, the bytes that the rooms' turn logs hold, and the bytes that they may hold together. This is the control interface's own `GET /stats`; the site statistics of the same name are on the WebSocket port (below). |
| `GET /healthz` | 200 `{"ok": true}`. It is the one call that needs no secret. |

### The room specification

The body of `POST /rooms` is a JSON object, and only `map` is required:

```json
{"map": "TINY.LVL", "players": 3, "fog": false, "early_start": true, "bots": [{"seat": 2, "bot": "medium"}],
 "reconnect": true, "hold_vote_seconds": 30, "max_pause_seconds": 1800, "max_catch_up_seconds": 300, "resume_countdown_seconds": 10,
 "code": "ROOM-1", "seed": 1}
```

| Key | Default | Meaning |
|---|---|---|
| `map` | required | The map's file name in the maps folder. |
| `players` | 2 | 2 to 4: the match starts when this many seats are taken, and takes no more. |
| `fog` | false | Fog of War. |
| `early_start` | true | The room's leader may start the match with the players who are there ("The leader and the early start"). `false`: the room has no leader and waits for every seat. |
| `bots` | none | `[{"seat": 2, "bot": "medium"}]`: at least one seat is left for a person, the seats are distinct and there is no fog (each mistake is a 400 that names the key). `"bot"` is what the game's `--bot` takes after the seat ([`BOTS.md`](BOTS.md#running-bots)). |
| `code` | drawn | What a client puts in its Hello: letters, digits, `_` and `-`, up to 32 characters. |
| `seed` | drawn | 0 to 4294967295: the match's random seed. |
| `wait_seconds` | 120 | 1 - 86400: a room that has not started after this long fails. |
| `load_seconds` | 60 | 1 - 600: everybody must have loaded the map this long after the start. |
| `keep_seconds` | 600 | 0 - 86400: a finished or failed room stays visible to the status calls this long. |
| `max_run_seconds` | 7200 | 60 - 86400: a match that is still running this long after it began is ended. The limit counts the time that the match ran, not the time that it waited for a seat that was away. |
| `reconnect` | the server's setting (on unless `--no-reconnect`) | Whether the room holds the seat of a player whose connection is lost ("Reconnect"). |
| `hold_vote_seconds` | 30 | 5 - 3600, as `--hold-vote-seconds`. |
| `max_pause_seconds` | 1800 | 60 - 86400, as `--max-pause-seconds`. |
| `max_catch_up_seconds` | 300 | 10 - 3600, as `--max-catch-up-seconds`. |
| `resume_countdown_seconds` | 10 | 0 - 60, as `--resume-countdown-seconds`. |

When the body leaves a key out, the five reconnect keys take what the server was started with (`--reconnect` or `--no-reconnect`, and the four numbers); the table shows the defaults of a server started without those options. Any other key is left to the table's default.

### The room's status

`GET /rooms/<code>`, the list and the result file `<code>.json` all hold the same JSON. Every key of the reconnect and the restart records is an addition; a lobby that ignores them works as before.

| Key | Meaning |
|---|---|
| `code`, `map`, `fog` | As the room was made. |
| `state` | `waiting`, `loading`, `running`, `finished` or `failed`. `reason` says why for a finished or failed room. |
| `expected`, `joined` | The seats as asked, and as there are (bots count). |
| `players` | `[{"seat", "name", "bot"}]`: the players of the room; a bot says so. |
| `bots` | The room's computer players: `seat`, `bot`, `kind`, `level`, `style` (standard bots only), `name`, `fill`. |
| `early_start`, `leader`, `ignored_start_requests` | `leader` is the seat of the room's leader while the room waits or loads, and null while nobody leads and once the match runs. `ignored_start_requests` counts the START requests that the room did not honour. |
| `lobby`, `starting`, `plan` | Only for a lobby room (network protocol 16; the keys are absent otherwise): `lobby` is true, `starting` says whether the leader's START waits for every person's game, and `plan` is the leader's plan as a letter for each colour (`o` the next player, `e`, `m` or `h` an easy, medium or hard bot, `n` nobody), a blank and the teams (`-` for none, `0+1`): `omne 0+1`. |
| `seat_moves`, `ignored_seat_moves` | The colours that the leader moved a player to (a swap counts once), and the SeatMove requests that the room did not honour (network protocol 14; since protocol 15 also one whose guard is not the room's seating). |
| `ticks`, `turns`, `age_seconds` | How far the match has run, and how old the room is. |
| `state_hash` | A finished room: the referee's state at `ticks`, as 16 hex digits. |
| `result` | A finished room: `quitter` and the rows of the results screen (`names`, `seats`, `score`, `lost`, `killed`, `hatched`, `winner`; an alliance is one row). |
| `reconnect`, `hold_vote_seconds`, `max_pause_seconds`, `max_catch_up_seconds`, `resume_countdown_seconds` | The room's own settings. |
| `paused`, `resume_seconds` | `paused` says that the match is held: a seat is missing, or the countdown after a pause runs. `resume_seconds` is what is left of that countdown. |
| `absent` | The seats that are missing, longest away first: `[{"seat", "name", "state", "away_seconds", "progress"}]`. `state` is `absent` or `catching_up`, and `away_seconds` is the seat's total time away. |
| `vote` | `{"seat", "continue", "voters"}`, or null. |
| `paused_seconds`, `rejoins`, `drops_by_vote`, `drops_by_cap`, `rejoins_refused`, `catch_up_expired`, `streamed_bytes` | What the pauses of the match came to. Never a key. |
| `log` | `{"turns", "bytes", "usable"}`: the turn log that a returning player is given. |
| `record` | `{"kept", "stale", "bytes", "note"}`: whether a restart of the server would bring this match back, and why not when it would not. |
| `restored` | `{"turns", "replay_ms", "state_hash"}`, or null: the room came back from a record. |

A room that a restart could not bring back (another network protocol, a changed map, ...) is a `failed` room whose `reason` says so.

## `/busy` and `/stats`

Besides the upgrade to a WebSocket, the WebSocket port answers `GET /busy`, `GET /stats` and `POST /stats/local`. None of them needs a secret, and the answers hold numbers only.

### The public status, `GET /busy`

- `GET /busy` (no query, no body; any other method is 405) returns `200` with `{"matches": N, "players": M}` and `Cache-Control: no-store`, and closes the connection. There is no name, room code, address or secret in it, and it costs a pass over the rooms.
- `matches` is the number of rooms whose match is loading or running with a person in it. A room that only waits for players is not a match. A room that a restart of the server brought back counts for its first five minutes, whether or not its players are back.
- `players` is the number of people in rooms that wait or load, and who are there in a match. Bots are no people. Somebody who has not yet said Hello is in no room and is not counted. A room that a restart brought back also counts its held seats for its first five minutes.
- `docker/nginx.conf` forwards `https://<site>/busy` to it like `/ws`: an exact location, GET only, no query, one request a second per address with a burst of 5, a 3-second connect timeout and 5-second read and send timeouts.
- The deploy job of CI polls it and restarts the site when no match runs (after three hours at the most by default, `DEPLOY_MAX_WAIT_MINUTES`), so that a deploy does not end games that people are playing: [`WORKFLOW.md`](WORKFLOW.md#deploy-from-ci-and-the-staging-site), "Deploy from CI and the staging site".

### The site statistics, `GET /stats` and `POST /stats/local`

The front page shows them.

- `GET /stats` returns numbers only: `{"now": {"matches", "players"}, "online": {"day", "total"}, "local": {"day", "total"}, "since": "YYYY-MM-DD"}`.
- `now` is `/busy`'s answer. `online` is the matches that ran at least 600 ticks (30 seconds of play) and ended on the server (demo rooms count too). `local` is the single-player games that browsers reported. `day` is the last 24 hours, `total` all of it, and `since` is the date from which the counters count.
- `POST /stats/local` (no body, no query) counts one single-player game and answers `204`. The web game posts it when such a game begins, so that figure is what browsers say, and the server keeps nothing of a report but the count (at most 120 count a minute).
- The counters are kept in `site-stats.json` of `--results-dir`; without a results folder they live in memory only.
- The limits of the site's nginx and the rest: [`NETWORK_PORT.md`](NETWORK_PORT.md#the-dedicated-server-srcants_server-v0083), "The dedicated server".

## The leader and the early start

In a room with `early_start` on (the default; demo rooms have it on) the first player who joined is the room's **leader**, and when it leaves the earliest of those who are left. The leader's game shows the START button of the setup screen.

- START starts the match at once with the players who are there, when the room is waiting and at least two players are in. One player is enough when the START asks for bots (next section). `expected` keeps what the room was made for and `joined` shows who came. A room whose seats are all taken still starts by itself, unless its create block says that a full room waits for its leader (`--room-leader-start`): then only the leader's START starts it, so that the leader can arrange the colours first, and a full room whose leader never presses START ends when the room's ten minutes are over.
- Every other START request is ignored and counted in `ignored_start_requests`: one of a player who is not the leader, of a room with `early_start` off, with one player and no fill level, or when the match is loading already. The first 16 of a connection cost nothing (a double or triple click); each one after the sixteenth is a violation, and eight violations throw the sender out.
- The leader may also **move a player to another colour** (network protocol 14, with swaps and a guard since 15: a press on the player's row of its screen). The room puts the guest, with its key and its place in the order of the Welcomes, in the empty seat, or changes places with the guest that holds the seat, sends every player the new room and tells each player that was moved, but the leader, in its chat. The press carries a number that stands for who sits where (the guard): a room that does not seat its people exactly so any more (somebody came or went) ignores it. A request from a guest that is not the leader, for a seat that a bot or the host holds, from a seat that holds nobody, with another guard, or in a room that has started is ignored and counted in `ignored_seat_moves` (the same 16 free, then violations, and it costs no part of the budget); the moves that are made have a budget of 6, then 4 a second. Details: [`NETWORK_PORT.md`](NETWORK_PORT.md#protocol-14-the-leader-of-a-room-moves-a-player-to-another-colour), "Protocol 14" and "Protocol 15".

Details: [`NETWORK_PORT.md`](NETWORK_PORT.md#protocol-7-the-rooms-leader-starts-early-the-first-player-of-a-servers-room), "Protocol 7: the room's leader starts early".

## Bots fill the empty seats, and chat in the waiting room

- The leader's START carries a **fill level** for each seat (none, easy, medium or hard: the game's `--fill-bots`, [`COMMAND_LINE.md`](COMMAND_LINE.md); one level for every seat before network protocol 13). With one, the server seats a bot of that level, named "Bot (Easy)", "Bot (Medium)" or "Bot (Hard)", in each seat that is still empty and has a level, up to the room's players, and starts the match, so **one person can start alone**. Only the leader's request adds bots.
- **The room's teams are in its create block** (a pair such as `0+1`, see "Demo rooms") and hold for every start, whether the room fills up by itself or the leader presses START. Details: [`NETWORK_PORT.md`](NETWORK_PORT.md#protocol-13-a-bot-level-for-each-seat-of-the-leaders-fill-and-teams-chosen-before-the-start), "Protocol 13".
- **Bots and Fog of War never mix**: with fog the room seats none and tells the leader why (a notice, sent as a chat line from the room). A cancelled start takes the bots away again.
- The server runs the bots as virtual clients of the referee (`ants_ai`, [`BOTS.md`](BOTS.md)). A bot seat is never absent and never votes. A room's specification takes bots too, and the status lists them.
- **Chat works in the waiting room and while the map loads.** A guest's line goes to everybody in the room, with the sender's seat and name (at most 100 printable characters; the last 200 lines are kept and start the match's chat log). In the match a line for the team reaches only the sender and its ally, and a line for all reaches everybody.

Details, the screens and what the bots cost the server: [`NETWORK_PORT.md`](NETWORK_PORT.md#protocol-11-bots-fill-the-empty-seats-at-the-leaders-start-and-chat-in-the-waiting-room), "Protocol 11: bots fill the empty seats at the leader's START, and chat in the waiting room".

## Lag and lost connections

**A room never waits for a player who lags.** The server keeps sealing a turn every 50 ms whatever lag one machine has. (A game on the local network keeps its own rule: its host has a seat and waits for a friend's slow machine, "Waiting for Bob...".)

- The others play on at full speed and are told once a second from 3 s of lag, "Bob is lagging (12 s behind)", until he is within 1 s. Bob's commands apply when they arrive; he runs the turns he missed at up to four times normal speed, and his screen says "Catching up...".
- A player 60 s behind, or whose machine has run no turn for 30 s although it is connected, is dropped like a player who left (the others see "Bob dropped out of the game!").
- **Lag is not a loss.** A slow link or machine, or a window frozen for less than 10 s, is lag. A client that says nothing for 10 s, or whose connection closes (a sleeping laptop, a stopped process), has lost its connection, and a room that holds seats pauses for that, and for nothing else ("Reconnect"). A hidden tab of a desktop browser keeps playing, and nobody is told that it lags; a phone browser that leaves the foreground, or a tab that the browser freezes, loses its connection.
- The room's leader is a player like the others once the match runs: if its window stops after the early START, the room does not wait for it. A finished room goes on answering until every player that is still connected has run the last turn (15 s at least, 30 s at most).

Details: [`NETWORK_PORT.md`](NETWORK_PORT.md#what-the-player-feels-ping-delay-the-jitter-buffer-and-the-laggard-v0094-protocol-8), "What the player feels: ping, delay, the jitter buffer and the laggard" (the lag policy), and ["The browser as a client: hidden tabs, and sound"](NETWORK_PORT.md#the-browser-as-a-client-hidden-tabs-and-sound-v0096).

## Reconnect

A room that holds seats gives each player a **key** in its Welcome and holds the seat of a player whose connection is lost. Every room of a server does since v0.6.0, unless the server is started with `--no-reconnect` or the room's specification says `"reconnect": false`; demo rooms follow the server.

- The match is **paused for everybody**: nothing is sealed, chat is relayed, and commands are discarded without a violation. The room's status says so (`paused`, and `absent`: who, and how long away in all, `away_seconds`).
- The player comes back with its key. A Hello for a running match that shows a key goes to the room, instead of being refused (`MatchRunning`, "The match has already started."). The player is given the turns it lacks from the server's log, runs them at once without drawing them and says its state hash; the referee compares it with its own, and the match resumes.
- The others may **vote** after the seat has been away 30 s in all (each loss counts at least 5 s): more than half of the people who are there (1 of 1, 2 of 2, 2 of 3; bots never vote) drops it. A seat that is lost three times in a minute is put to the vote at once.
- There is no automatic drop for a seat's own time, but the match's total pause is capped (`--max-pause-seconds`, 30 minutes, 10 in a demo room; every pause counts at least 5 s). At the cap every seat that is not present is dropped, one that is catching up too, and a room whose players all lost their connection ends "everybody left".
- A key is bounded, so that nobody can hold a room or the server's bandwidth with one: the catch-up time of an absence (`--max-catch-up-seconds`), three accepted Hellos a minute from one seat, and the turn log that is streamed to a seat (three times the log's size in ten minutes, at least 1 MiB). Beyond a bound the answer is `RejoinFailed`, and the seat stays held (the vote and the cap apply). The log itself has a limit (`--log-mb`; all the rooms together may hold 256 MiB): a room whose log cannot grow any more drops a lost seat at once, as a room that holds no seats does.
- **The key is a secret**: it is in no status, result file or log line. On the native TCP door it travels in clear (like the room's token: whoever can read a native player's connection can take the seat; `--no-reconnect` has no keys (except in a room that asks for `"reconnect": true`), and the native port can be left closed, the web page needs none). On the WebSocket door it travels inside TLS.

### What a player sees when the connection is lost

The game's own clients, the desktop game and the web page, come back by themselves (since v0.6.0).

- When its own connection is lost, the match stays on the screen with a line at the top, "Connection lost. Reconnecting... 0:12" (with " (attempt 2)", " (attempt 3)" and so on from the second attempt), and under it "Esc leaves the match" (Esc asks the quit question; its Yes leaves for good). When the server gives the game the match again, the loading screen shows "Catching up 45%", and then the match is back with the selection, the view and the chat as they were.
- Another player's loss is a line after one second: "Bob (Red) lost the connection, waiting 0:42". When the vote opens, two buttons stand under it, "F2 Keep waiting" and "F3 Continue without Bob" (the keys or the mouse; never a dialog, so chat and the map work). A player who comes back is announced by "Bob is back: the match goes on in 7".
- **The key of the seat** is kept in the file `rejoin.txt` next to the settings file (mode 600 on Linux and macOS; the per-user folder on Windows): a line a key, tab-separated: server, room, seat, the key in hex, the time written; at most eight, none older than three hours. The web page keeps it in the browser's local storage. A new player's key is kept from the moment the match starts: a waiting room that is only visited leaves none.
- A game that is started again with `--join ADDR --room CODE` finds the key and takes its seat back. A reloaded web page does the same with its join arguments, and is not asked for a name again. The web front page ([`PLAY_IN_BROWSER.md`](PLAY_IN_BROWSER.md)) and the desktop start menu's first panel offer **"Rejoin your match (CODE)"** while a fresh key is kept.
- Closing the game on purpose, leaving through the web page's Menu button or link (the game is told first: the seat is dropped at once and nobody waits for it), a match that ends and being dropped let go of the key. A crash, a closed browser tab or window and a reload keep it. Closing the desktop game's window counts as closing on purpose.

Details, flows and limits: [`NETWORK_PORT.md`](NETWORK_PORT.md#reconnect-protocol-10-a-player-whose-connection-is-lost-can-come-back), "Reconnect (protocol 10)", and its section ["What the clients do in release B"](NETWORK_PORT.md#what-the-clients-do-in-release-b) (release B is v0.6.0, when the game's own clients began to come back by themselves).

## Restart records

**A running match survives a restart of the server.** A room that holds seats keeps a record of its running match in the folder `restart` of the results folder (`--restart-dir` chooses another one, `--no-restart-records` keeps none; a server with neither `--results-dir` nor `--restart-dir` keeps none and says so in its log). Only rooms that hold seats keep a record, so a server started with `--no-reconnect` keeps records only for the rooms that ask for `"reconnect": true`.

- There is one file per room, made when the room's start is accepted and deleted when the room is over. It holds the start message, the keys of the seats and every sealed turn, so **it is for its owner only** (mode 600, in a folder of mode 700 when the server makes it; no key is in any log line, status or result file).
- **Each turn is written to it before the turn is sent to anybody**, so a crash, `kill -9` or the container's death loses no turn that any player has seen. The file is flushed to the disk once a second (a room on a slow disk asks less often: every 10 s at the most) and when the server is told to stop (`SIGTERM`), so only the death of the whole machine can take the last second of turns (ten seconds on a slow disk).
- A server that starts again checks the records, newest first, and replays them in slices of 5 ms while it serves: the engine is rebuilt from the start message, every sealed turn is replayed, the state hashes that the old server stored are checked, and the bots sit down again. A Hello for a room that is not back yet waits for it.
- After a restart **every person's seat is held, absent**: the match is paused until the players come back with their keys, and the room keeps its code. The others may vote on a seat after 90 s (`--restart-vote-seconds`), the cap of the pauses applies, and a room whose players never come back ends "everybody left". The game's own clients rejoin by themselves.
- A record of **another network protocol is not restored** (a rules change would play the match out differently; the game version does not decide), and neither is one whose last write is more than an hour old. Its room is a `failed` room with the reason in the status and the log, and the record is kept for a day in `restart/refused/`, for the owner who moves it back. A deploy that wants to keep the running matches must not change the network protocol (the changelog says when a release does).
- A record is at most 48 MiB, and the records of the running rooms together 256 MiB (`--restart-budget-mb`). A disk that refuses a write ends that room's record: the match goes on without one, and its status says why (`record`).
- **One server to a records folder**: the store locks it for its life. A second server cannot take the folder: with an explicit `--restart-dir` it stops with status 1, and with the default folder it runs without records and says so.
- **Keep the volume** (`ants-server-results` in the stack): a stack that is removed with its volumes loses the records.

The format, the limits, the measurements and the tests: [`NETWORK_PORT.md`](NETWORK_PORT.md#restart-records-a-running-match-survives-a-restart-of-the-server), "Restart records".

## Flood control

A client that is no game can send valid messages as fast as its line allows, so every connection is limited.

- The TCP connection holds at most 4096 messages (1 MiB) for the game and leaves the rest in the kernel, where TCP slows the sender (the WebSocket connection always did the same). A lobby takes at most 64 messages of a guest per update, and a match 256 of a client.
- Every connection has a budget of **1000 messages a second with a burst of 1000**. A real client sends a few dozen: with turns of 50 ms its busiest second held 27 messages in steady play, 48 for a person who gives 20 orders a second and 89 while a window catches up at 4x. A message beyond the budget is not handled and is a violation, and the sender is out after eight of them: a flood at line rate in well under a second, a sender at 1500 a second after about two.
- Chat has a budget of its own, in the waiting room and in the match: a burst of 5 lines, then one a second. A line beyond it is dropped. A talker who keeps on above two lines a second uses up an allowance of 20 dropped lines, and after that every line is a violation; two lines a second are tolerated for ever. It is a protection of the remake.

Details and the tests: [`NETWORK_PORT.md`](NETWORK_PORT.md#flood-control-v0093), "Flood control".

## The protocol version keeps two builds from playing each other

A lock-step match is only as good as the agreement on the rules. The network protocol number changes with every change that the peers of a match must share: the messages and the simulation's rules (any fix that changes a state hash of some play, even one that shows only on some maps or in rare sequences of orders). A game of another number is refused at the Hello by a LAN host and by a server's room, and is listed as another version in the LAN list.

- The game says "This version cannot play with the host's version." (the web page adds "Reload the page to update."; the desktop start menu says "This game is vX.Y.Z, but the server runs another version of the game. Update the game, or wait until the server is updated.").
- So update the game, the server and the page together. The current number is `kProtocolVersion` in `include/ants_net/protocol.hpp`, which also holds the rule and what each number changed; `ants_server --version` prints it.

## The stack (Docker)

### The server image and the example service

`Dockerfile.server` builds the program alone (no SDL, no game assets except the six maps: `-DANTS_BUILD_APP=OFF`) and runs it as an unprivileged user with a healthcheck (the game port accepts a connection). The image carries the six maps of the original game in `/maps`, and the server makes its control secret when none is given. Its entry point already passes `--maps /maps --public --ws-any-interface --ctl-any-interface --results-dir /results`, and its default command is `--port 4001 --ws-port 4002 --ctl-port 4010`.

`docker-compose.server.yml` is an example service. A secret and a maps folder may come from a `.env` file next to it (`.gitignore` keeps that file out of the repository). Only the game port is public; the WebSocket and control ports are published on the host's loopback address only.

```bash
docker compose -f docker-compose.server.yml up -d --build
```

The service joins the external network `proxy-network`, which must exist: for a server alone, create it first with `docker network create proxy-network`. Other containers on that network can reach the control port (it is behind the secret), so put no container there that you do not trust.

### One stack for the site and the server (Portainer)

`docker-compose.stack.yml` holds both services, `ants-beta` (the web game) and `ants-server`, on the network `proxy-network`, which must already exist on the host (it is declared `external`). `docker-compose.staging.yml` is a second copy of it that runs next to it for trying finished work ([`WORKFLOW.md`](WORKFLOW.md#deploy-from-ci-and-the-staging-site)).

In Portainer: Stacks, Add stack, Repository, this repository, reference `refs/heads/main`, compose path `docker-compose.stack.yml`. Let the stack's webhook do the updates and leave the stack's own Git polling off, or it deploys by itself: the deploy job of CI calls the webhook after the tests pass and the server is idle ([`WORKFLOW.md`](WORKFLOW.md#deploy-from-ci-and-the-staging-site), "Deploy from CI and the staging site"). The first deployment builds both images and takes several minutes.

Nothing else has to be set. The server uses the maps of the image (a volume `ants-maps`, filled the first time), makes its control secret, and allows the demo rooms of `web/lobby.html`: 48 at a time, each waiting up to ten minutes for its players and each with a turn log of at most 4 MiB (`--log-mb 4`, so that the rooms cannot use up the 256 MiB that all the logs share). The page chooses the map among the six of the original, and every match that its card starts takes a demo room, a game against bots only too (since v0.8.0). Whoever reaches a game port, the site or TCP port 4001, can fill them.

Optional environment variables, set in the stack's "Environment variables" (they are not part of the repository):

| Variable | Default | Meaning |
|---|---|---|
| `ANTS_SERVER_SECRET` | the server makes one | The control secret ("The control secret"). |
| `ANTS_MAPS_DIR` | the volume `ants-maps` | A host folder instead of the maps of the image. It must hold the demo map and every map of `ANTS_DEMO_MAPS`, or the server stops at startup. To refresh the volume after an update that adds maps, remove it; to add maps, copy them into it. |
| `ANTS_DEMO_ROOMS` | 48 | How many demo rooms the page may make at a time: 1 to 255. **0, or 256 and more, make the server exit at startup, and with `restart: unless-stopped` that is a restart loop.** Keep the number times the stack's `--log-mb` (4) under 256, the MiB that all the turn logs share. No variable switches the demo rooms off: delete the three demo options from the `command` in your copy of `docker-compose.stack.yml`. |
| `ANTS_DEMO_MAP` | `TREASURE.LVL` | The map of a room whose create block names none that the server lists. |
| `ANTS_DEMO_MAPS` | the six maps of the original | The maps a create block may choose: file names separated by commas, blanks around a name are dropped. The page offers the six maps of the original, so list them all: a block with a map the server does not allow gets the default map, with the seats the page asked for. |
| `ANTS_PORT` | 19980 | The host port of the web page. |
| `ANTS_SERVER_PORT` | 4001 | The host port of the game port: TCP only, and only native clients use it; the browser pages reach the server through `/ws` of the site. |
| `ANTS_SERVER_WS_PORT`, `ANTS_SERVER_CTL_PORT` | 4002, 4010 | The host ports of the WebSocket port and the control interface, both on the host's loopback address only. |
| `ANTS_BUILD_ID` | the commit of the clone's git files, else the UTC build time | The build id that the page's footer, the changelog pages and `ants_server --version` show. |

The server's container is given **15 s to stop** (`stop_grace_period`, in the staging stack and the example service too; docker's default is 10 s). On `SIGTERM` it makes the restart records of its running rooms durable and exits within a moment. A redeploy recreates the container, and the server that starts again brings the matches back. **Keep the volume `ants-server-results`**, which holds the restart records and the control secret: a stack that is removed with its volumes loses them.

The container is read-only, with no capabilities and no new privileges, and it may run at most 64 processes: it needs nothing but its sockets and the results folder. The WebSocket port is reached by the site's nginx over `proxy-network` (`/ws`, `/busy`, `/stats` and `/stats/local`).
