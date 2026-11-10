# Player names (network protocol 24)

The names in a server's rooms are checked. This page says where a name comes from, what the server does with it at the door and inside a room, and what changes when the check cannot be made. Nothing on the wire changed: the Hello, the Welcome, the Room message and the NameMsg have the layouts of protocol 23 (see [`NETWORK_PORT.md`](NETWORK_PORT.md)).

## Where a name comes from

- **A person who is logged in** plays under the name that the hub fits from the name the account is shown by, else from its other Discord name (the rules are the page's `player_name.js` and the hub's `hub/ticket.py`, which read one table of cases, `tests/data/game_name_fit.txt`). No logged-in name can look like a guest name.
- **Everybody else is a guest.** A guest's name is `Guest` and a six-digit number (`Guest 123456`), kept in the browser until its data is cleared.
- The native client and any program that sends a Hello without a pass are guests on a server that checks names. A game on the local network, or on a host started by hand, is not affected: it never checks names.

## The pass

A pass is a short text that the hub gives to a logged-in page and the page gives to the game server in its Hello. It says that an account may sit under one name until a time.

- **Layout:** `n1.` and the base64url (no padding) of 36 bytes, 51 printable characters in all. The 36 bytes are the account number (8 bytes, little endian), the end time (4 bytes, Unix seconds, little endian), 8 random bytes (the pass's own number) and the first 16 bytes of HMAC-SHA256 over `ants-name-ticket-1`, a zero byte, the first 20 of the 36 bytes and the Hello's name, with the secret as the key. The name is not in the pass: the Hello carries it, and the pass fits only that name.
- **Life:** 600 seconds. The server does not believe a pass that ends more than 630 seconds from now.
- **Single use:** the server remembers the numbers of the passes it has used until they run out (at most 32,768 at a time). A pass that is used again counts as no pass. A pass is spent by any Hello that carried it, even when the room then refuses that Hello (the room is full, a match is running): the page asks the hub for a new one before its next try.
- **Where it travels:** the Hello's `token` field (at most 64 printable characters; protocol 6 made it, and nothing read it before protocol 24). It never goes into an address and is never stored: the page keeps it in memory only.

## What the server does at the door

Only a server started with `--verified-names` checks names. It is off by default.

1. A Hello that takes a new seat (it has no seat key, or a key that fits no seat of the room) has its pass looked at. A Hello that takes a seat back with its key keeps that seat's name and its pass is not looked at.
2. The pass is good when it has the right shape, its MAC fits the Hello's name, it has not run out, it was not used before, the window of used passes has room, and the name is one a pass may be for (printable ASCII, 2 to 32 characters, no blank at either end, not a computer player's name, not like a guest's name). The Hello keeps its name.
3. In every other case the person is a guest. A Hello whose name is exactly `Guest` and six digits (the first not 0) keeps it; any other name is replaced by `Guest` and a number that the server draws, every number as likely as any other.
4. A pass can only make a name better. Nobody is refused or disconnected because of a pass.

## Names inside a room

- No two people of a room go by the same name (capital and small letters are the same). A second person with a name that is taken gets ` (2)`, then ` (3)` and so on after it, the name cut so that the whole stays within 32 characters; a second guest with a guest name that is taken gets a new number instead. A suffix never makes a computer player's name (`Bot` twice gives `Bot` and `Bot 2`).
- Two tabs of one account are two seats, and the second one gets ` (2)`.
- A name stays what the seat was given for as long as the seat lives: a person who comes back with the seat key, a rejoin after a restart and a rematch keep it.
- Computer players keep their own names and never meet the check.

## Fixed names

In a room of a server that checks names a NameMsg is not acted on: it is counted as one that was not heard, as a NameMsg is in a room that has started. No page sends one today, so nobody is sent away for it.

## When the hub is away or the setting is missing

Everybody plays as a guest, and no room refuses anybody for it:

- the hub is down, or it has no secret: it hands out no pass;
- the server has no secret: every pass is no pass, and the server says so once when it starts;
- the two secrets differ, or the two clocks are far apart: every pass fails its check, and the server's log line says so (a count, once a minute at most, never a name, a pass or the secret).

## Chat limits

(Written with the chat limits for guests.)

## Settings and logs

(Written with the settings and the log lines.)

## The protocol number

Protocol 24 changes no layout. The number changed because the server's handling of a Hello's name and of the NameMsg is a rule that the page and the server share. A Hello of 23 is refused with `VersionMismatch` by a server's room and by a host on the local network; a page that gets it shows the banner that asks for a reload. The simulation, every state hash and the replay rules numbers are as they were, and a replay made under 23 plays by its own rules number.

## The tests

- `test_lockstep` and `test_lobby` pin the number and read `tests/data/lobby_messages.txt`, the bytes of the lobby page's messages as the C++ encoders write them; `tests/scripts/web_lobby_net_check.js` reads the same file with the page's codec.
- `test_server` checks the pass and the door (`tests/test_server/test_name_ticket.cpp`) and the refusal of a Hello of another number (S3.73, which lists 23); `test_lobby` checks the unique names and the fixed names (`tests/test_net/test_lobby_names.cpp`) and the refusal of 23 by a room; `test_netgame` refuses it for a host on the local network. `tests/data/name_ticket_mutants.json` lists the changes that these tests must catch.
- `tests/scripts/web_lobby_client_check.js` checks the Hello that the page sends and that a refusal for the version is final (no new try). The banner is checked in `tests/scripts/web_name_check.js`: the page's own first socket refused for its version shows the strip that says a newer version is out, with its Reload button, opens no second socket and makes no room of its own; the same file and `web_home_games_room_check.js` check the strip after a refused Join.
