# Connecting an AI to the Map Maker

A person can let their own AI assistant (Claude Code, Cursor, a custom GPT, any program that speaks MCP or plain HTTP) look at the map they have open in the Map Maker and, if they allow it, change it, while they watch. The person makes a **key** in the Map Maker ("Connect an AI"), puts it into their AI app, and turns on **Let an AI work in this tab**. The AI's calls go from the app to the chat hub, and from the hub to that one tab, which does the work with the Map Maker's own functions: the tab's rules, its undo and its refusals apply, and the person can stop it with one click. The hub stores no map.

The code is the chat hub's (`hub/aikeys.py`, `hub/aidoor.py`, `hub/airelay.py`, `hub/aitools.py`, `hub/aimcp.py`; the hub itself is described in [`HUB.md`](HUB.md)). The page's side is described in [`MAP_EDITOR.md`](MAP_EDITOR.md).

## What it does not do

- It never reaches a stored map, a map of another person, the chat, the account, the library's submissions or any other page. There is no tool to submit, publish, withdraw, delete, rename, download or open a draft; no tool that talks to anyone; no tool that takes a URL, a path or a command; and no generic "call a page function". The hub has an allow-list of tool names (`hub/aitools.py`), and the page registers a handler for those names only.
- It does not work unless the person turned the switch on in an open tab, and the switch turns itself off when the tab is closed or reloaded.
- Test play from the AI is not offered, and a map that an AI helped with carries no mark.
- A key never shows up again: the whole key is shown once, when it is made.

## Settings

`ANTS_HUB_AI_KEYS=on` switches the feature on for every logged-in account; anything else is off, and every door answers `503 ai_off`. The default is off, so that a hub that has this code does nothing until the operator switches it on. It is also the kill switch: off, no key works and no tab can be armed (a key that was made stays in the table and works again when it is switched on). The switch stays after the release that turns it on (default on from then); until that release it is a switch that is off by default, and is retired or kept as the kill switch by the end of the second release after this one.

## Keys

A key is `antsai_<id>_<secret>`: `id` is 8 hex digits (public: the person's list and the logs show it), `secret` is 43 characters of base64url (256 random bits). The prefix lets secret scanners find a leaked key.

| | |
|---|---|
| Kept | `ai_keys` in `hub.db`: id, the account's **reference** (`hub/pseudo.py`, never the Discord id), the person's name for it, scope, the SHA-256 of the secret, made, ends, last used, calls, revoked. The secret is never kept. Additive tables (`ai_keys`, `ai_log`): a hub of an earlier version opens the same file and never looks at them. |
| Scope | `read` (look: `map_info`, `get_region`, `get_picture`, `check_map`, `list_tiles`, `read_guide`, `list_library_maps`, `view_library_map`) or `edit` (also change the map, save a draft). |
| Ends | 7, 30 (the default) or 90 days after it is made. There is no key that never ends. |
| How many | 3 work at a time for an account; 5 may be made in a day. |
| Name | The person's own text: cleaned by the hub's text rules (`hub/text.py`), 1 to 40 characters, shown only to the person and in no log. |
| Judged | On **every** call: the header is `Authorization: Bearer <key>` of exactly that shape, the row exists, the SHA-256 of the secret is the row's (`hmac.compare_digest`, also done for an id that does not exist), the key is not revoked and has not ended. Whatever is wrong the answer is the same (`401 bad_key`), so a key cannot be probed. A call that waited its turn is judged again when the turn comes. Revoking is a flag in the table: it takes effect at once, there is no cache. |
| Gone | A ban and Forget me delete the account's keys and the log rows of their calls in the same step. A key that ended or was revoked stays in the table for 7 days, then the row goes. |

### The key pages

The Map Maker's own calls, through nginx's exact paths (`docker/nginx.conf`):

| Request | Answer |
|---|---|
| `GET /hub/ai/keys` | `{"ok", "keys": [{"id", "name", "scope", "made", "ends", "used", "calls"}], "max": 3, "days": [7, 30, 90], "csrf", "armed"}`. Names are typed text: `untrusted` and `untrusted_fields` say so. |
| `POST /hub/ai/keys` `{"name", "scope", "days"}` | `{"ok", "key", "id", "name", "scope", "made", "ends", "used", "calls"}`: the whole key is in this answer only. `409 live` (three work already), `429 day` (five made today), `400 bad`. |
| `POST /hub/ai/keys/revoke` `{"id"}` | `{"ok"}`; `404 no_key` for a key that is not the person's, does not work or is not there. |

These are the page's own requests and nobody else's: `Sec-Fetch-Site: same-origin` and the site's `Origin` (both for a POST; a same-origin GET may carry no Origin), a JSON body of 512 bytes at the most with exactly the fields above, the person's login (`401 login`), an account that is not banned (`403 banned`) and, for a POST, the token of this login in `X-Ants-Csrf` (an HMAC under a key made from the hub's own secret; it changes with every login and is never in a URL). The answer is `no-store`. Everything that can be decided is decided before a body byte is read.

## The armed tab

The page opens `wss://<site>/hub/ai/ws` when the person turns the switch on; the socket is the armed tab. It is a socket of its own, so the chat's limits and frames are untouched. It needs the person's login (`401 login` before the upgrade), the site's own `Origin` and `Sec-Fetch-Site`, and an account that is not banned.

- **One armed tab for an account.** A second socket takes the first one's place: the first is sent `{"t":"bye","why":"replaced"}` and closed.
- **Disarmed** when the socket closes: the Stop button, Turn off, closing or reloading the tab; after an hour with no call; when the login ends or runs out; on a kick or a ban; when the hub restarts. The call in flight is answered `stopped`, later calls `not_armed`.
- **One call at a time** for an account, with a queue of 3 behind it for 10 seconds (`busy` past that). A call is answered by the tab within 10 seconds, else `timeout`; a late answer is thrown away.
- **Frames.** Hub to tab: `{"t":"ready","v":1,"tools":[...],"idle","budget"}` once, `{"t":"call","id","tool","args","key","client","scope"}` (`key` is the key's name, `client` the AI app's name from MCP's `initialize`, cut and printable, else `an AI app`) and `{"t":"bye","why"}`. Tab to hub: `{"t":"result","id","ok":true,"result":{...},"cells":N}` or `{"t":"result","id","ok":false,"error":"<code>","message":"<sentence>"}`. A frame that is anything else is a mistake of the socket, and so is one over ten a second (a burst of 40); twenty mistakes in a minute close it. A result is checked by the hub: an object, a picture (`result.png`) is base64 within 300 KB, the code of an error is one the tab may use (`refused`, `changed_by_person`, `too_big`, `bad_args`, `no_open_map`, `busy`, `tab_error`), a sentence is cut to one printable line of 300 characters, `cells` is a whole number from 0 to 5,000 and counts only for an edit.
- **Cells.** An AI may change 100,000 cells while one tab stays armed, then `budget`; arming the tab again starts again. One call changes at most 5,000.
- A frame of the tab is at most 1 MiB. The hub keeps nothing of the map: a call and its answer live in memory while they are in flight.

## The Map Maker's side

Three files, loaded the first time **Connect an AI** is pressed (so a person who never connects an AI never downloads them; budgets in `tools/size_limits.py`): `web/front/mapmaker_ai.js` (the sheet, the strip and the socket), `web/front/mapmaker_ai_tools.js` (the handlers for the tools below) and `web/front/mapmaker_ai.css`. The Map Maker's own scripts changed by two small hooks (`history.top()` in `mapmaker_edit.js`; `editor` and `changed` on `mapmaker_tools.js`) and the loader and button in `mapmaker_app.js` and `mapmaker.html`; the page's policy (`docker/nginx.conf`) lets it open its socket to the site's own address.

- **Entry.** A button in the name row (a window with room) and an item of the File menu (a phone).
- **The sheet.** Your keys (name, what it may do, when it ends, when it was last used, Revoke with its question), Make a key (name, Look and edit or Look only, 7, 30 or 90 days), the key shown once with Copy, and how to connect it: one command for Claude Code, a small settings file for apps that take a web address and headers (Cursor, VS Code and others), plain web calls for the rest, and the skills. It says so in words when the site has the feature off, when nobody is logged in (a link back through the login, `/hub/login?next=mapmaker`) and when an account is banned. A key is held in memory while it is shown and is never stored, put in an address or logged.
- **The switch** "Let an AI work in this tab" opens the socket and is off after every load or reload. While it is on, a strip under the top bar says so (and, on a phone, sticks to the top): the name of the AI app and of the key (typed text, put in with `textContent`), the number of changes, **Stop** (closes the socket: the AI is cut off at once) and **Undo AI changes**, the last four things the AI did in plain words, and a dashed frame over the part of the map it just touched. The hub's reasons for turning a tab off (another tab, an hour of quiet, a logout) are said there too.
- **Taking turns.** Every change of the map, by the person or the AI, moves a counter, the `version` of every answer. A call that names an older `base` is refused with `changed_by_person` and the new version; a call waits while the person is drawing a stroke (two seconds, then `busy`); the person keeps working. The first change of a session keeps the person's unsaved map first as a draft "name (before AI)" with the browser's own store (a map that cannot be kept first is not changed). Each call that changes the map is one step of the map's history. The AI's `undo` and `redo` and the button take back only steps that are the AI's and at the top of the history: once the person has worked on the map, the steps below are theirs to undo.
- **What the tab never does for an AI:** write markup, store, evaluate, ask any address but the hub's key pages, the site's skills and (for the guides) the skills' files, or call anything that submits, sends or deletes (`tests/scripts/test_web_mapmaker_ai.py` reads the two scripts for it, because no run can show a call that nobody thought of).

## The tools

One table (`hub/aitools.py`) serves the plain HTTP door, the MCP door, the OpenAPI file and the relay: names, scopes, JSON schemas (a small subset: type, enum, minimum, maximum, length, number of items, required, no other property). The hub checks the arguments before it asks the tab, so a malformed call costs the tab nothing; the tab judges everything about the map itself and answers in its own sentences. Every edit may send `base`, the `version` of the last answer: if the person changed the map meanwhile the call is refused with `changed_by_person` and a fresh look is needed. Each edit is one step of the map's history.

| Tool | Scope | What it does |
|---|---|---|
| `map_info` | read | kind, size, rules, file size, name, description, minutes, eggs, default ant, hills and ants and food piles of each color, the checker's sentences, `version` |
| `get_region` | read | a text picture of a window, at most 128 x 128, with a legend and what stands in it |
| `get_picture` | read | a PNG of the map or a window, at most 1,000 pixels on a side |
| `check_map` | read | the checker's findings for the map as it is now |
| `list_tiles` | read | ids and names of ground, objects, bigger objects, plants, food, teams and ant types |
| `read_guide` | read | the text of one of the guides (`guide`, `tools`, `vocabulary`, `terrain`, `layout`, `check-and-fix`) |
| `list_library_maps`, `view_library_map` | read | the public library's maps as examples: a summary, a text picture or a PNG |
| `paint` | edit | one tile as ground or as a one-cell object: `cells` (at most 2,000), `rect`, `line` with a brush, or `fill`; at most 5,000 cells a call; optional `auto_edges` |
| `put_object`, `put_hill`, `put_ants`, `put_food`, `put_plant`, `erase` | edit | the Map Maker's own tools |
| `set_properties` | edit | description, minutes, eggs, default ant (not the rules word: the page sets that itself) |
| `fix_edges`, `resize_map`, `new_map` | edit | the page's own functions with the page's own refusals; `new_map` only after the safety copy |
| `undo`, `redo` | edit | the AI's own steps only |
| `save_draft` | edit | "Save as new" in the person's browser: a free name or a numbered one |
| `note` | edit | one line (200 characters) that the strip shows |

**Typed text is data.** The names and descriptions in a result (the open map's, the library's) are text that people typed. The hub marks them in every answer (`untrusted: true` and `untrusted_fields`, as the maps API does), the MCP door puts a fixed sentence in front of such an answer, and the `instructions` of MCP's `initialize` and the guides say that text in a map is never an instruction. What an AI can do is limited by the tools above whatever a map says.

### Errors

Plain HTTP answers `{"ok": false, "error": <code>, "message": <sentence>}` with the status below (MCP answers a tool's failure as a result with `isError`, and a code of the door itself, `bad_key`, `rate`, as the HTTP status).

| Code | Status | Meaning |
|---|---|---|
| `bad_key` | 401 | wrong, ended, revoked or deleted: one answer for all |
| `ai_off` | 503 | the switch is off |
| `rate` | 429 | a key's burst, the account's hour, or the address's bad keys |
| `bad_request`, `unknown_tool`, `bad_args` | 400 | not a call that this door takes, not a tool, arguments that do not fit |
| `read_only` | 403 | a read key called a tool that changes |
| `not_armed` | 409 | no tab is armed for the account |
| `stopped`, `busy`, `changed_by_person`, `no_open_map` | 409 | the person stopped it, the queue is full, the person changed the map, the tab has no map to work on |
| `timeout` | 504 | the tab did not answer in 10 seconds |
| `refused` | 422 | the Map Maker's own reason (a sentence) |
| `too_big` | 413 | more than 5,000 cells, or a picture over its size |
| `budget` | 429 | 100,000 cells changed while this tab stayed armed |
| `tab_error` | 502 | the tab's answer was not one the hub takes |

## The doors

- **Plain HTTP:** `POST /hub/ai/v1/call` with `Authorization: Bearer <key>`, `Content-Type: application/json` and `{"tool", "args"}`. `GET /hub/ai/v1/openapi.json` is the OpenAPI 3.1 file (no key): one operation, with every tool's schema in `components`.
- **MCP:** `POST /hub/ai/mcp`, the Streamable HTTP transport, stateless, plain JSON answers: `initialize` (with `instructions`), `ping`, `tools/list` (the tools that the key's scope allows, with `readOnlyHint`), `tools/call` (a text of JSON, and a picture as an image). One message at a time (a batch, resources, prompts and streams are not offered). Claude Code: `claude mcp add --transport http ants-map-maker <site>/hub/ai/mcp --header "Authorization: Bearer <key>"`. Clients that ask for a sign-in (OAuth) are not supported.
- **Order of the checks** at a key door, with nothing read from the body before the key is good: the address's allowance of bad keys, the `Origin` (a page of another site is `403 origin`; an AI app sends none), the type and length of the body, the key, the key's and the account's budgets, the body, the tool and its arguments, the scope, the relay. No cookie is believed and no CORS header is sent, and nginx answers 405 to a preflight.

## Limits

| What | Limit |
|---|---|
| A key | a burst of 20 calls, then 4 a second |
| An account | 600 calls an hour, all keys together |
| An address | 10 keys that are no good a minute, then `429` before any key is looked at; nginx's own zone first (20 a second, burst 40) |
| Keys | 3 working, 5 made a day; the key pages 30 a minute for an address |
| A call's body | 64 KB; a page's body 512 bytes; a frame of the tab 1 MiB; a picture about 300 KB |
| Cells | 5,000 a call (2,000 listed one by one); 100,000 while a tab stays armed |
| Armed tabs | 4 for an address, 100 in all; 8 sockets at once for an address in nginx |
| Time | 10 seconds a call; an hour without a call disarms the tab |

The numbers are in one place, `hub/limits.py` (`ai_*`).

## Logs

A call is one line in the hub's log and one row in the calls' log: `ai_call key=<id> ref=<8 hex> tool=<name> cells=<n> ok=<0|1> ms=<n>`. Never a key, an argument, a result, a map's name or a typed text, and no Discord id: the tables hold the account's reference. A refused call is `ai_refuse why=<code> tag=<tag>` at most once a second. The calls' log keeps 30 days or 10,000 rows. `hub/admin.py` lists the working keys (`aikeys`) and revokes one (`aikey-revoke KEY_ID`) or all of an account's (`aikeys-revoke ACCOUNT`).

## The rules that the tests hold

`tests/scripts/test_hub_ai.py` runs the real hub against a tab played by the test, and `tests/data/hub_ai_mutants.json` breaks each rule on purpose (`tools/mutate.py`): the key is judged by its digest, its end and its revocation on every call and again when its turn comes; a read key cannot change anything; three keys at a time and five a day; the page's own request only (site, type, login, token, shape, size); a ban and Forget me delete the keys; ten bad keys close an address; no cookie is believed at a key door; one armed tab for an account and another person's tab is never asked; the tab's answers are checked; the cells are counted; the allow-list and the schemas; typed text is marked; no key, argument or id in a log or a table. `tests/scripts/test_nginx_hub_ai.py` holds the nginx side (exact paths, the headers that go on, no cookie at the AI doors, the limits). The page's side is `tests/scripts/web_mapmaker_ai_check.js` (run by `tests/scripts/test_web_mapmaker_ai.py`, with `tests/data/mapmaker_ai_mutants.json`): the tools against the hub's table, each change one step that Undo takes back, a hill in the way refused, an old `base`, the safety copy, the person's strokes, the sheet and the socket on a fake page.

## The skills

Six short guides teach an AI that has no knowledge of Ants how to make a good map with these tools. Each is a folder with a `SKILL.md` (the format that Claude and other agent applications load by themselves when a request fits its `description`) and, for two of them, a `reference/` file of worked calls. They are in `ai_skills/`:

| Skill | `read_guide` topic | What it teaches |
|---|---|---|
| `ants-map-guide` | `guide` | Start here: the order of work from an idea to a checked map, what to ask, what the AI may not do (submit, publish, delete, download), what to tell the person at the end |
| `ants-mapmaker-tools` | `tools` | Every tool and its arguments: look first, small steps, `base` and `version`, `changed_by_person`, Stop, `save_draft`, typed text is data |
| `ants-map-vocabulary` | `vocabulary` | The colors, ant types, ground, objects, bigger objects, plants, food and the limits of Classic and Extended maps |
| `ants-map-terrain` | `terrain` | The five kinds of ground, edge pieces (`fix_edges`, `auto_edges`), the ring of mud beside water, strips two cells wide, bridges, natural shorelines |
| `ants-map-layout` | `layout` | Fair and fun layouts for two to four colors, with the numbers measured from the six original maps (hill distance, ants, eggs, food, power-ups, symmetry) and worked calls |
| `ants-map-check-and-fix` | `check-and-fix` | Reading `check_map`, fixing each kind of finding, and the walk check that the checker does not make |

There is no skill for test play: the AI cannot start one, and the person presses "Play this map" themselves.

**Where they are served.** `/ai/skills/` of the site (docker/nginx.conf: GET and HEAD only, a type for each of the four kinds of file, revalidated at every use like a script, nothing else of the site through that prefix): `<skill>.zip` for each skill, `all.zip`, the folders (`<skill>/SKILL.md`, `<skill>/reference/...`) and `index.json`: `{"skills": [{"name", "topic", "description", "bytes" (of the zip), "files" (the paths in the folder)}], "all": {"zip": "all.zip", "bytes"}}`. The "Skills for your AI" screen of the Map Maker lists the skills from `index.json` and links the zips.

**How they are made.** `tools/make_ai_skills.py` (Python standard library only). The words of a skill are written by hand; the **numbers and names** in it are generated, never typed twice: the blocks between `<!-- generated:NAME begin ... -->` and `<!-- generated:NAME end -->` are made from the sources (the tile names of `src/ants_assets/lvl_tile_names.inc`, the palette, teams, food and limits of `web/front/mapmaker_*.js`, the map core's limits in `include/ants_assets/lvl_parser.hpp` and the room's in `include/ants_server/online_limits.hpp`, the checker's table in [`MAP_FORMAT.md`](MAP_FORMAT.md), the tools and errors of `hub/aitools.py`, and the six maps of `Original-Ants/Maps/`). `--check` fails when a block is stale and names it, `--write` makes the blocks again, and `--build OUTDIR` packs the committed files into `OUTDIR/ai/skills/` (reproducible zips: sorted entries, a fixed time, fixed attributes; the marker lines are left out of what is served). The build needs `ai_skills/` and the tool only, so the web image (`Dockerfile`, after the compile layers; `build_web.sh` for a local build) makes the files without the hub's source or the documents; `tests/scripts/test_ai_skills.py` holds the blocks to their sources on every pull request.

**`read_guide`.** The tool answers with the text of the `SKILL.md` of the topic's skill (the Map Maker's tab fetches `/ai/skills/<skill>/SKILL.md`), so an AI that cannot load skills is taught the same: `guide` first. The `reference/` files are named in `files` of the index.

**Installing them.** Claude apps: Settings, Capabilities, Skills, upload one skill's zip at a time. Claude Code: unzip the folders into `~/.claude/skills/` (or take `all.zip`). Any other AI: nothing to install, `read_guide` has the same text.

**What the tests hold.** `tests/scripts/test_ai_skills.py`: six folders, each a `SKILL.md` that names itself and fits in 500 lines; every limit that the prose states is one of the sources' numbers; every name in backticks is a real tool, argument, error or finding, and no skill names a tool that the hub does not have; every call that a skill shows (in `reference/`) is a call that the hub's schemas take, and every tool has an example; the blocks are what the sources say now; the zips are reproducible and hold exactly the folder; the index says what the page needs. `tests/scripts/test_nginx_ai_skills.py` holds the location (text everywhere, the real block where nginx is installed). `tests/data/ai_skills_mutants.json` breaks the tool's rules one at a time (`tools/mutate.py`).
