# The chat hub

`ants-hub` is the site's global chat: a small service of its own, next to the game server and never in its thread. Python 3, standard library only, about twelve hundred lines (`hub/`). It speaks JSON over one WebSocket, `wss://<site>/hub/ws`, keeps the last 50 lines in memory and seven days of lines in SQLite for moderation, and enforces every limit below itself. Reading is open to everybody. Writing needs a login, which this version of the hub does not have yet: it tells every client `login:false` and is read-only for all (the login is one interface, [Login](#login), that a later change fills in).

The pages never need the hub: the site, the rooms and the games list work whether or not it runs, and the chat shows "offline" and retries.

## The protocol

Text frames, each a JSON object with a `t`. Version 1 (`v` in the welcome).

| Direction | Message | Meaning |
|---|---|---|
| up | `{"t":"say","text":"..."}` | a line of chat (needs a login) |
| up | `{"t":"ping"}` | keep-alive; the answer is `online` |
| down | `{"t":"welcome","v":1,"you":{"name","mod"}\|null,"login":bool,"online":n,"history":[line,...]}` | first message: who you are (`null` for a reader), whether this hub has a login at all, how many sockets are open, the last 50 lines oldest first |
| down | `{"t":"line","id","at","who","text","mod"}` | a new line (`at` is seconds since 1970, `who` the account's name, `mod` true for a moderator) |
| down | `{"t":"del","id":n}` | take line `n` off the screen |
| down | `{"t":"online","n":n}` | the number of open sockets, sent when it changes (at most every 5 seconds) and as the answer to `ping` |
| down | `{"t":"err","code":c}` | the last message was refused; the page owns the words |

Error codes: `login` (a reader tried to write), `rate`, `muted`, `banned`, `long`, `empty`, `young` (reserved for the login layer: an account too new to write), `bad` (the message cannot be read, the line holds `<` or `>`, or a moderator's command is wrong). A reader or a member gets no other reply to a good `say` than the `line` that everybody gets.

## Limits

All in `hub/limits.py` (`Limits`), all enforced by the hub.

| What | Limit |
|---|---|
| frame from a client | 2 KB of payload; text only, in one piece (a fragment, a binary frame, a frame over 2 KB closes the socket: 1003, 1003, 1009) |
| a chat line | 200 code points after the text rules (NFC) and 800 bytes |
| writing | a burst of 3, then one line every 2 seconds, for an account across all its tabs; and 5 lines a second (a burst of 50) for all accounts together |
| the same text twice | within 30 seconds the second is dropped (`rate`); a dropped line costs no token |
| mistakes | an `err` or an unreadable message is a mistake; 20 within a minute close the socket with 1008 |
| frames of any kind | a burst of 10, then 5 a second for a socket; the rest are mistakes |
| sockets | 8 for each client address (an IPv6 address counts as its /64), 500 in all; the ninth gets 429, the 501st 503, before the upgrade |
| keep-alive | the hub pings every 25 seconds; a socket that sends nothing for 60 seconds is closed (1001) |
| handshake | 10 seconds to send the request head, 8 KB at most, 64 connections that have not finished it (the login layer has 5 seconds to answer; a late or failing login is a reader) |
| a slow reader | a socket with 128 KB unsent is dropped; a socket that was closed with output still unsent is cut after 5 seconds |
| history | the last 50 lines, sent on connect; 7 days in the database, and no new line is kept once `hub.db` and its log file hold 64 MB (`rate`; a mute, a ban and a deletion are never refused for it) |
| new sockets | nginx lets 5 a second through for each client address (a burst of 10) and 16 open at a time; beyond that is 503 and 429 before the hub is asked |

## The handshake

nginx (see [the site's nginx](#the-sites-nginx)) passes only `GET /hub/ws`. The hub checks, in this order, and answers with JSON and a closed connection (`{"error":"..."}`): the method (405), the **Origin**, which must be exactly the site's (`ANTS_HUB_PUBLIC_URL`; 403 for anything else and for none, and a hub that was not told its site lets nobody in), no body (400), `Upgrade: websocket` with `Connection: Upgrade` (426), version 13 (426), a valid key (400), the 500 socket total (503) and the 8 sockets of the address (429). The request head is read strictly: CRLF only, no folded lines, no control characters, at most 60 headers, and the headers the hub believes at most once. Extensions and subprotocols are never negotiated.

The client address is the `X-Client-Address` that nginx sets (an IP address; anything else falls back to the connection's own). `GET /hub/healthz` answers `{"ok":true}` for the image's health check; nginx does not route it. The 64 connections that have not finished their handshake are a shared pool: through nginx a connection reaches the hub only with a complete head, but any container on the stack's network can open 64 silent ones and hold the door for 10 seconds at a time, and can set `X-Client-Address` to anything; the network is trusted (the game server's ports are in the same position), so keep no container there that you do not trust.

## Frames

`hub/ws.py` is the whole WebSocket (RFC 6455, the server side of text frames, about 150 lines). A client frame must be masked, have no reserved bit, use a known opcode and the shortest length form; control frames are at most 125 bytes and whole; text must be UTF-8. A broken stream gets a close frame with the RFC's code (1002 broken, 1003 not taken, 1007 not UTF-8, 1009 too big), and the frames that came whole before the break are still handled. A frame over the cap is refused from its header, before its payload is read. The parser never raises (`Parser.feed` returns frames and sets `error`) and keeps at most one header and one frame.

## Text rules

What a visitor types is data (`hub/text.py`). In order: NFC; every blank (space, tab, line end, other space characters, line and paragraph separators) becomes one plain space and runs collapse; control characters, format characters (the bidirectional controls, zero-width characters, joiners, the byte order mark, tag characters), surrogates, private use and unassigned code points are removed, and so are the letters that draw as blanks (the Hangul fillers, the braille blank, the combining grapheme joiner, the Khmer inherent vowels); more than 4 combining marks in a row are cut; NFC again. Then: nothing left, or nothing but combining marks (a variation selector or the Khitan filler shows nothing by itself), is `empty`; over the limits is `long`; a `<` or `>` is `bad` (checked on the result, so nothing can hide one). There is no markup, no link detection and no mention: the page shows the text with `textContent` only and puts a `bdi` or an isolating `dir` around names and lines, because right-to-left letters still reorder what is next to them.

Names are what a Discord username is: lower-case letters, digits, `_` and `.`, 2 to 32 characters. The hub refuses an identity with any other name.

## Moderation

Mutes, bans and deletions are kept by account id in `hub.db`, so they survive a restart, a new session and a rename. A banned account can still read.

From a terminal on the machine (no network door; run it as the image's own user, which `docker exec` does by default, so that the database files stay the hub's):

```text
docker exec ants-hub python -m hub.admin list [--lines N]      bans, active mutes, the newest lines with their ids
docker exec ants-hub python -m hub.admin mute ACCOUNT MINUTES  (0 lifts a mute; at most a year)
docker exec ants-hub python -m hub.admin ban ACCOUNT
docker exec ants-hub python -m hub.admin unban ACCOUNT
docker exec ants-hub python -m hub.admin del LINE_ID
```

`ACCOUNT` is an account id (digits, no leading zero), or a name that has said something in the last 7 days (`@name` when the name is all digits). A mute or a ban counts from the account's next line; a deleted line leaves every screen within two seconds (the hub looks every two seconds). Exit status: 0 done, 1 nothing to do, 2 wrong use.

In the chat, the accounts listed in `ANTS_HUB_MODS` (digits, comma separated) may type `/mute name minutes` (1 to 10080, never a moderator) and `/del id`. A name means the account that is in the chat with it; when two accounts are, or when nobody is and no line of that name is kept, the command is refused (`bad`): use the command line with the account id. The command is not a chat line; a wrong one is answered `bad`. For everybody else a line that starts with `/` is text.

## Data

`/data/hub.db` (SQLite, WAL, file mode 600; the volume `ants-hub-data`): `lines` (id, time, account id, name, text, a moderator flag, a deletion mark), `mutes`, `bans`. A line is kept 7 days and then deleted for good: the hub does it every hour, 2,000 lines at a time (a long backlog goes over several seconds, not in one wait), the freed pages are zeroed (`secure_delete`) and the log file is cut; when a reader (for example `hub.admin list`) holds the file at that moment, the cut is tried again every 10 seconds, and once at every start. The size guard keeps the database from growing past the 64 MB for lines, so the hub does not fill the volume by itself; when the disk refuses a write for another reason, the socket that caused it is closed (1011) and an `error` line is logged, and the command line says "the database refused it" in one line. Deletions are numbered by a counter in `meta` that a purge cannot lower, so a mod's `/del` still reaches every screen after old lines were purged. A line that a moderator deleted stays, marked, until its 7 days are up. The last 50 lines that are not deleted and not past their time are the history after a restart. Nothing else about a person is stored.

## Login

The hub core asks a `Sessions` object (`hub/session.py`) who is on the other end of a handshake: `identify(headers)` returns an `Identity(account_id, name, young)` or `None`. An account id is digits (1 to 20: a Discord id), `young` says the account is too new to write. `identify` is a coroutine (a real login looks something up); the hub waits 5 seconds for it. `NoLogin` is the default: nobody is identified, the welcome says `login:false`, and the hub is read-only for everyone. The login of a later change plugs in here; the core and its tests do not change.

`TestLogin` exists for the hub's own tests: it believes the header `X-Hub-Test-Login: <id>:<name>[:young]`. It is on only with `ANTS_HUB_TEST_LOGIN=1`, the hub refuses to start with it unless it listens on a loopback address (the image listens on every interface, so it can never run there), no stack file or image sets it, and nginx passes no such header. A test (`tests/scripts/test_hub_stack.py`) checks all four.

When the real login is added it should: set its cookie `Secure`, `HttpOnly`, `SameSite` and with the `__Host-` prefix; check `Sec-Fetch-Site` as well as `Origin`; fix the identity at the handshake (a socket never changes who it is); keep reserved names such as `mod` and `admin` from being taken; and keep the secrets out of the log as `Config` does.

## Settings

Environment variables only (a command line shows in the process list).

| Variable | Meaning |
|---|---|
| `ANTS_HUB_PUBLIC_URL` | the site's address, `https://host`: the one Origin a socket may come from (no default in the program; the production stack file defaults to the beta site) |
| `ANTS_HUB_MODS` | account ids that may moderate in chat |
| `ANTS_HUB_DB` | the database, `/data/hub.db` in the image |
| `ANTS_HUB_PORT`, `ANTS_HUB_HOST` | `4040` and `127.0.0.1` for the program; the image sets `0.0.0.0` (the port is not published) |
| `ANTS_HUB_DISCORD_CLIENT_ID`, `ANTS_HUB_DISCORD_CLIENT_SECRET` | read for the login of a later change; never printed |
| `ANTS_HUB_TEST_LOGIN` | tests only, see above |

A wrong value stops the start with a message that names the variable and never its value (exit status 2).

## Logs

One line for each event on standard output: `<time> <event> key=value ...`. A line never holds a secret, a cookie, a chat line, a name, an account id or a whole address: values are cut to plain characters, and a connection is told apart by a tag, a keyed hash of its address that changes with every start. The start line lists the settings that are safe (the port, the origin, the number of moderators), and a `note` line says that sockets are counted for each address that nginx names. nginx writes no access log for `/hub/ws` (its default format would write the address, the referrer and the browser of every handshake). The container's log is rotated by docker (`json-file`, 3 files of 10 MB). A `crowded` line (sockets and groups, no address) is written every minute while one address group holds more than 6 sockets. `tests/scripts/test_hub_server.py` starts the real process with canary values for every secret and reads its log.

## The stack and the site's nginx

`ants-hub` is the third service of `docker-compose.stack.yml` and `docker-compose.staging.yml` (own volume, own container name in staging): built from `Dockerfile.hub` (slim Python, a non-root user, bytecode made at build time), `read_only`, `cap_drop: [ALL]`, `no-new-privileges`, `pids_limit: 32`, `mem_limit: 192m`, a tmpfs at `/tmp`, a health check (`python -m hub.health`), no published port. It has no `ANTS_BUILD_ID` argument: an image depends on `hub/` only, so a deploy that changes nothing there builds the same image and the chat is not restarted. `tools/deploy_filter.py` counts `hub/` and `Dockerfile.hub`.

### The site's nginx

`docker/nginx.conf` has one door, `location = /hub/ws`: a GET without a query, the name `ants-hub:4040` resolved per request through Docker's resolver (so the site starts, and works, without the hub), 5 new sockets a second for each address (a burst of 10; 503 beyond) and 16 open at a time (429 beyond), 120 seconds of read timeout (the hub pings every 25), no buffering, no access log. The hub gets only the headers of the upgrade, `Origin`, `Cookie`, `Sec-Fetch-Site` and the one `X-Client-Address` that nginx sets from the connection; nothing else of the visitor's request, and no header of that name from a visitor. A hub that is not there is a JSON 503 `{"error":"hub_off"}`, never a page of the site. nginx counts an IPv6 client by its whole address and the hub by its /64, so for a visitor who has a /64 the hub's 8 are the limit that matters.

### Behind a reverse proxy: not ready for the public until this is set

The hub counts sockets for each address that nginx puts in `X-Client-Address`, and nginx knows the address of the connection it sees. Behind a reverse proxy that is the proxy's address, for every visitor, until `docker/trusted-proxies.conf` is filled in ([MAP_SUBMISSIONS.md](MAP_SUBMISSIONS.md), "Deploying"). Until then **all visitors are one address**: the hub's 8 sockets for each address are 8 for the whole site (one visitor with 8 tabs locks everybody else out), and nginx's 5 new sockets a second are 5 for the whole site. **The chat must not be switched on in a public page until that setting is filled in.** The hub says it at every start (a `note` line) and writes a `crowded` line when one address group holds more than 6 sockets.

Even then an address group is a /64 for IPv6, and a visitor who owns a /48 has 65,536 of them: such a visitor can still open the hub's 500 sockets from many groups (8 each) and keep them with ping answers. The limits that bound it are the 500 in all, nginx's 5 new sockets a second for each address, and the 60 seconds of silence; the answer to a real attack is at the proxy in front, which is outside this service.

### Deploys

A push that changes `hub/`, `Dockerfile.hub`, `docker/nginx.conf` or a stack file counts for the deploy job (`tools/deploy_filter.py`), like any file an image copies: the job waits for a moment with no match running on the game server (`/busy`), then calls the stack's webhook. The webhook rebuilds the stack from the clone, and every image of the page and of the game server carries the build id of the commit (`docker/resolve_build_id.sh`: the commit named by the clone's `.git`), so **a hub-only change also rebuilds and recreates the page and the game server**, after the wait for an idle server (the game server brings its matches back from the restart records; the page is away for a moment). The hub has no build id: its container is recreated only when `hub/` or `Dockerfile.hub` changed, so a deploy for something else does not restart the chat. There is no way to deploy one service of the stack from the webhook; a change that must not touch the game server waits for a deploy that would happen anyway (documents, tests and the other tools are never deployed).

CI builds the image in the Web job, starts it with the stack's flags, waits for `healthy`, runs the command line against its volume and checks that a socket from another site gets 403.

## Tests

`tests/scripts/test_hub_*.py` and `test_nginx_hub.py` (run by `./run_tests.sh --fast` and CI): the text rules and their laws (`test_hub_text.py`); the framing, the request reader and their fuzz (`test_hub_ws.py`: every first byte against a model, random and mutated streams cut at random places); the chat, limits, store, settings and command line without sockets (`test_hub_core.py`); the hub on real sockets, noise on the wire, and the real process with its log read for secrets (`test_hub_server.py`); the stack, the image and the wiring (`test_hub_stack.py`); the nginx block, run for real where nginx is installed (`test_nginx_hub.py`). `ANTS_HUB_FUZZ_SCALE=10` runs the fuzz ten times longer. The client in `tests/scripts/hub_client.py` shares no code with the hub.

`tests/data/hub_mutants.json` breaks the hub on purpose (the bidirectional strip, the rate limit, anonymous writing, the Origin check, the history size, the ban, the mute, a secret in the log, the frame cap, and more): `python3 tools/mutate.py tests/data/hub_mutants.json --keep-going --jobs 1`; every mutant must be caught.
