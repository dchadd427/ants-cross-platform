# Plays and likes of replays

How often a replay was played, and how many people like it: two numbers on every match of [the history](REPLAYS.md#the-match-history), shown on the list of matches and on a match's page, with two sorts, **Most watched** and **Most liked**. This page is the home of what is counted, the door that counts it, the pass that tells the door which person is asking, what is kept (only numbers, and for a like one opaque reference), and how it is tested. What a person sees is in [REPLAYS.md](REPLAYS.md#the-match-pages); the hub's side is in [HUB.md](HUB.md#the-admin-pages-door) (the gate) and [HUB.md](HUB.md#login) (Forget me).

## What is counted

- **A play** is 10 seconds of a replay *playing* in the game page (`web/replay_plays.js`, run by the replay glue of `web/shell.html`): a pause, a jump, the end of the match and a tab that is hidden add nothing, and a gap between two looks adds at most a second. The count is sent once for a replay in a browser in 24 hours (`localStorage` key `ants.plays`: a table of replay id and time, written before the play is sent, at most 200 entries; a browser that refuses `localStorage` counts once for each page view), as `POST /counts/play/<id>` with no body and no cookie; a failed call is never sent again. It is the game page that counts, because it is the page that plays (also inside the frame of a match page); it shows no number. A match that is being played (live), a file of your own and a recording that is not the server's are never counted.
- **A like** is one person's heart on one replay: a logged-in person (the hub's Discord login, [HUB.md](HUB.md#login)) taps it on a card of the list or on a match's page, and taps again to take it back. A person likes a match once; nobody sees who liked (no answer of the server lists people), and the heart that is lit is the one that is yours. Not logged in, the heart shows the count and a tap says "Log in to like a replay." with **Log in with Discord**. At most 1,000 likes stand for one person at a time.
- **A match whose recording is gone, or was made by an older version** keeps its numbers and takes no new play and no new like (its heart is a plain count); a like that was given can always be taken back at the door, whatever became of the recording.
- Both numbers stop at 1,000,000. A replay that was removed from the history by the operator takes its numbers and its likes with it.

## What is kept

| What | Where | What it holds |
|---|---|---|
| plays | `history_matches.plays`, a number on the match's own row | a count and nothing else: no id, cookie, address, name or time of a play |
| the number of likes | `history_matches.likes` | a count; it equals the rows below, kept in step in the transaction that changes either |
| a like | `replay_likes` (match id, reference), one row for each person and match | the hub's reference of the account: 16 hex digits made by the hub from the account (a keyed hash, [HUB.md](HUB.md#data): never the account's id); no time, no address, no name |

That is all. The reference lets a person like a match once, light their own hearts and take every like back when they forget themselves. The game server cannot turn a reference into a person; the hub, which holds the key that makes references, can, as it can for the alerts and the moderators' log that are kept by the same reference. No route lists the people of a match or of anything. The pass of a visitor, the cookie and the address are never stored or logged here; the log says only that a pass was refused, with the kind of the refusal. A deleted account's likes are taken back (Forget me, below), and the counts drop.

## The door

`ants_server --counts-port N` (0: off; the stack files start it at 4032, `ANTS_COUNTS_PORT`; the port is not published) is a door of its own on this machine's network (`ctl::HttpServer::listen_door`: GET and POST, the handler decides), not the public history door, which stays read only. It needs the database (the match history). It answers JSON with no store; a request with a body is refused (the door reads no body); a request that says it is of another site (`Sec-Fetch-Site` other than `same-origin` or `none`) is a 403 whatever it holds.

| Route | Who | What it does |
|---|---|---|
| `POST /counts/play/<id>` | anyone | one play. `204`; `404` for an id that is not in the history, or whose recording is gone or cannot be played here; `429 {"error":"rate"}` when the door's budget is used up (a burst of 100 plays and 10 a second for all visitors together); `503` when the database does not answer |
| `GET /counts/mine?ids=<id>,...` | a person (pass) | the person's own likes among up to 50 matches, and the token of their changes: `{"signed_in":true,"liked":[ids, in the order asked],"csrf":"<32 hex>"}` |
| `POST /counts/like/<id>`, `POST /counts/unlike/<id>` | a person (pass and token in `X-Ants-Csrf`) | the like, once (a second one changes nothing); the take-back. `{"liked":true\|false,"likes":N}`. `404` as for a play (an unlike works for any match of the history); `409 {"error":"limit"}` at 1,000 likes of the person or a million of the match; `429 {"error":"rate"}` beyond a burst of 10 changes and then one every 2 seconds for a person (at most 4,096 people are tracked; one that is full again is forgotten); `403 {"error":"csrf"}` for a missing or wrong token |
| `POST /counts/forget` | the hub (pass and token), not routed by nginx | takes back every like of the pass's person: `{"removed":N}`; it is not held by the budget of changes |

A pass that is missing or bad is a `401 {"error":"not_signed_in"}`, and with no good key at the server `503 {"error":"likes_off"}` (plays still count). The order of the checks is the shape of the request, the site, the pass, the token, the budget, then the match: a request that is refused for its shape costs no budget, and one that is refused for its token costs none either.

`GET /history` and `GET /history/<id>` carry `plays` and `likes` (whole numbers) in every row, and `GET /history?sort=plays` and `sort=likes` sort by them, newest first among equal numbers. The two sorts are carried by an index each, as the other sorts are (`history_matches_plays`, `history_matches_likes`: a page of 25 reads 25 rows, however long the history). The plain list that the site's nginx keeps for five seconds shows a new number with that delay; the page shows the answer of the door at once.

## The pass: who is asking

The game server never sees a cookie. The site's nginx asks the chat hub's **like gate** (`GET /hub/likes/gate`, `auth_request`, as for [the admin page](HUB.md#the-admin-pages-door)) before it passes a like, an unlike or a list of likes on; the hub reads the visitor's login and answers `204` with a signed pass, `401` for nobody, `503` when its key is not good; nginx turns `401` into `{"signed_in":false,"liked":[]}` for the list (a `200`: every visitor of a page of matches asks, and a 401 would be an error in every console), into `401 {"error":"not_signed_in"}` for a like, and any other trouble of the gate into `503 {"error":"likes_off"}`. A visitor's own header of the pass's name never goes on.

- **The pass:** `v1.<payload>.<mac>`; the payload is base64url, no padding, of the JSON `{"exp","nonce","ref"}` (keys sorted, no spaces; `exp` seconds since 1970, 60 seconds after the pass was made; `nonce` 16 hex digits, new each time; `ref` the account's reference); the mac is the hex HMAC-SHA-256 of the text `v1.<payload>` under the pass key. The game server checks the mac in constant time before it reads anything else, then the shape (exactly the three members), `exp` against its own clock (a pass that lasts longer than the hub makes them is refused), and that the nonce was not used (the nonces of the last minute and a half are remembered, 50,000 at the most; a full table accepts nothing new). A pass is for one request.
- **The keys** are made from `ANTS_ADMIN_GATE_SECRET`, the one key that the hub and the game server share ([HUB.md](HUB.md#the-admin-pages-door): 32 to 256 printable characters; the name is the older one): the pass key is the RAW 32-byte HMAC-SHA-256 with the secret as the key and the text `ants-like-pass-v1` as the message, the token key the same with `ants-like-csrf-v1`. They are not the admin page's keys: a like pass is no admin pass and the other way round. The token of a person is the first 32 hex digits of the HMAC-SHA-256, under the token key, of `csrf1`, a zero byte and the reference. A secret that is the published sample of the documents is refused as a key.
- **Known answers.** For the sample secret `canary-gate-key-5d8f0c1e7a9b4263-q`: pass key `8c13d59045b7f259e8ea9cfa18a50178873af160e6ffa04821d9cf4601d55ae9`, token key `4f749ae0b8f8bb03e531bf146ab21d7efd40d07bd58f54de599cbbfc115cdf11` (as hex of the 32 bytes); for the reference `0123456789abcdef` the token is `4a9f9b31add3b8129d21a9147e4247d9` and the pass with `exp` 1800000060 and the nonce `00112233445566ff` is `v1.eyJleHAiOjE4MDAwMDAwNjAsIm5vbmNlIjoiMDAxMTIyMzM0NDU1NjZmZiIsInJlZiI6IjAxMjM0NTY3ODlhYmNkZWYifQ.0e4879c0fd6e4c2daeed36090356d812a6e47f895974279031344a764fff89c3`.
- **The token** (CSRF). The page learns it from the first `GET /counts/mine` of its page and sends it in `X-Ants-Csrf` with a like or an unlike. It is checked for the pass's person at the door; the cookie of the hub is `SameSite=Strict`, nginx turns a request of another site away first, and the door refuses one again, so a page of another site can neither send the cookie nor make the call.

**Why a pass at the edge, and not the hub serving the route.** The numbers and the likes live with the history, because the list sorts, filters and pages by them in one query; a like served by the hub would have to be pushed to the history as a count, and two copies of a number drift. The hub is the one place that knows who is logged in, so it decides that, once, and signs what it decided for the one request (the admin page's design, so one pattern for the whole site, and the same checks: no cookie to the game server, the pass the only word of who). The page keeps one path for the list and for a like (`/counts/...`, same origin), and the hub never learns which match a like is for.

## Forget me

`POST /hub/forget` ([HUB.md](HUB.md#login)) first calls the counts door (`ANTS_HUB_COUNTS_URL`, `http://ants-server:4032` in the stack): `POST /counts/forget` with a pass that the hub makes for the one call and the person's token, an empty body, 3 seconds to connect and 10 for the whole call (nginx waits 12). The answer must be 200 JSON with the number removed. **Forgetting is all or nothing from the person's side:** if the door is on and does not answer, `/hub/forget` answers 503 `busy` and deletes nothing, and the person tries again. With the setting empty the call is skipped; with the setting but no good key the answer is the 503 as well (the likes could not be taken back, so nothing is deleted).

## Settings

| Setting | Where | Meaning |
|---|---|---|
| `ANTS_COUNTS_PORT` (`--counts-port`) | the stack, the game server | the counts door: 4032 in the stack, 0 switches plays and likes off (the pages show the numbers they have, and the hearts are plain counts) |
| `ANTS_HUB_COUNTS_URL` | the stack, the hub | where the hub reaches the door for Forget me; empty: not at all |
| `ANTS_ADMIN_GATE_SECRET` | the stack, the hub and the game server | the one key; without a good one likes are off (`likes_off`) and plays count |

## The history service

The history service ([HISTORY_SERVICE.md](HISTORY_SERVICE.md)) has the same two columns and the table of likes (its file's version 6, the same SQL), lists and sorts by them like the game server, and copies them at its first start (a game database that is older than the numbers is copied with zeros). It does **not** take plays and likes yet: the door and the pass are the game server's until the service takes the writes, so the route of `/history` must not move to the service before that.

## Tests

- `tests/test_server/test_replay_counts.cpp` (suite 2.19.26): the schema, the store (plays, likes, the limits, Forget me, the numbers of a record that is put again, a match taken out), the list and its two sorts with the plans of their indexes, the pass against the known answers of an independent maker and every reason a pass is refused, and every route of the door with every refusal and limit. `tests/test_server/test_history_service.cpp` holds the copy of the likes and of an older database. `tests/scripts/counts_server_check.py` (2.19.27) runs the real program: a recording of the arena, a play, a person's likes with a pass that the test makes from the words above alone, the list and its sorts afterward, Forget me, the refusals, no good key, no port, a port that is taken, and a log with no pass or reference in it. `tests/data/replay_counts_mutants.json` breaks the rules of the door and the store one at a time for `tools/mutate.py`; every mutant must be caught.
- `tests/scripts/test_nginx_replay_counts.py` runs the real `docker/nginx.conf` where nginx is installed (what is passed on, the gate, the allowances, a game server that is not there; the rows of `tests/scripts/test_nginx_headers.py` hold the headers).
- `tests/scripts/test_hub_likes.py` (gate, pass, Forget me; `tests/data/hub_likes_mutants.json`) and the page tests `test_web_plays.py` and `test_web_likes.py` (mutants in `tests/data/replay_plays_mutants.json`, `replay_likes_mutants.json`, `replay_likes_pages_mutants.json`).
