# Nicknames, tags and guest names

Part of [The chat hub](HUB.md). A person who is logged in has **one nickname** (their own: nobody else may use it) and, if they want, a **tag** ("[ANTS] Nickname"). A guest may type a **name** for the games, which everybody sees as "Name (Guest)". One sheet, "Your name", sets them (the page's side is in [HOME_CHAT.md](HOME_CHAT.md#your-name)); the hub keeps and checks them; the game server takes a nickname only with the hub's signed **pass**; and the game, the chat, the lists, the results and the match history show them. The change of the network protocol (number 29, UTF-8 names) is in [NETWORK_PORT.md](NETWORK_PORT.md#protocol-29-utf-8-names-and-the-hubs-pass). The pictures were approved by the maintainer before anything was built.

## Where a name shows

| Who | Shown as | Where the name comes from |
|---|---|---|
| Logged in, nickname and tag | `[TAG] Nickname` | the hub: `nicknames` row |
| Logged in, nickname | `Nickname` | the hub |
| Logged in, nothing chosen | the name of the login (the Discord display name or the username, as the chat has always shown it) | Discord, [HUB_LOGIN.md](HUB_LOGIN.md) |
| Guest who typed a name | `Wren (Guest)` | this browser (`localStorage`) |
| Guest who typed none | `Guest 482913` | six digits that this browser made once |

The chat and the players list show the nickname and, before it and dimmer, the tag; a guest name is shown with "(Guest)" dimmer than the name. The game has one plain string for each name (`[TAG] Nickname`, `Wren (Guest)`) and draws it in one color. A finished match keeps the names that were played with (the history pages, the replay's head), so a later change of nickname never rewrites an old match; matches from the day this ships on show the new names, earlier ones keep what they had.

The top bar's chip shows the nickname only, and its round badge takes the first letter of the nickname. Every round badge does (the chip, the Players list, a conversation, the moderation card), also for a name that comes as "[TAG] Nickname": never the bracket. A guest's chip has a dashed ring.

## The rules

All in `hub/nickrules.py` (pure functions of text) and `hub/name_letters.py` (the letters). A refusal is one **word**; the page owns the sentences (and never names another person: a lookalike says only that the name is too close to another one).

* **Text first.** NFC; the curly apostrophes and the dashes of a phone's keyboard become `'` and `-`; every run of blanks is one space; the ends are trimmed. Nothing else is removed or changed: what is not allowed is refused, never silently cut.
* **A nickname** (and a guest's name) is 3 to 16 characters: letters of **one** alphabet (Latin, with the Vietnamese letters, or Cyrillic; [The letters](#the-letters)), the digits 0 to 9, spaces and `. _ - '`. It starts and ends with a letter or a digit, has a letter in it and never has two signs in a row. Words: `empty`, `short`, `long`, `chars` (a character or a sign that may not stand there), `script` (a letter of another script: the game's font cannot draw it yet), `alphabets` (two alphabets in one name, the usual way to write a name that looks like another).
* **A tag** is 2 to 5 letters and digits of one alphabet; it is kept and shown with the capitals that the person typed (`[Ant] Zoe`, `[ANT] Zoe` and `[aNt] Zoe` are three different tags), and every check that compares it with a word (reserved, staff badge, rude, bot label) reads it in capitals, so a tag cannot get past them by its case. A tag from before capitals were kept stays as it is (in capitals) until the person changes it. A tag is not unique: anyone may use any tag, except a word that could pass for a staff badge (`staff`) or a reserved word.
* **Reserved** (`reserved`): a name that could pass for the hub's own voice, a role, a placeholder ("guest 123456", "player", "owner", "bot", ...), judged the way [HUB.md](HUB.md#names) judges every shown name (`text.squash`: dots, accents, a digit for the letter that it looks like, a Cyrillic letter drawn like a Latin one).
* **Rude** (`rude`): a word of `hub/rude_words.txt`, read the way a person reads it (the same squash, so `f.u.c.k` and a lookalike alphabet do not help). The list is small and the maintainers' to extend; it is a net, not a promise.
* **Bot-like** (`bot`, for tags): a name that the game's rooms would read as the label of a computer player (`Pip (bot, Hard)`, [BOT_NAMES.md](BOT_NAMES.md)); the room refuses such a name too.
* **Taken** (`taken`): the same name in other letters or case as somebody's nickname; **lookalike** (`lookalike`): a name that reads like somebody's nickname (or one that is held, below): accents, dots, spaces and a digit are ignored or read as the letter, a letter of another alphabet is the Latin letter it is drawn like, the `l`, the capital `I` and the `1` are one letter (`AIice` is `Alice`), and a letter with a bar through it is the letter (`Đ`, `Ø`, `Ł`, `Ħ`, `Ŧ`, the Cyrillic soft sign is a `b`); a test walks every letter of the table and fails for one that has neither a place in `text.squash` nor a place on the short list of letters that no Latin letter is drawn like. Guests may not copy a logged-in person's nickname either.
* **How often.** The first nickname is free. A nickname changes once in **30 days** (`soon`, with `until`, the time it may change again); the old one is **held** for its person for 30 days so that nobody takes it to pass for them, and a person who changes back has it again. A tag changes once a day (`soon`); a change of capitals alone is a change, so "ANT" to "Ant" waits a day like "ANT" to "PET" (the sheet says so under Rules), and saving the same text again is no change. `rate`: too many checks or saves in a moment. `busy`: the store could not answer. `nick`: a tag with no nickname behind it.

## What the hub keeps

SQLite, additive (an older database gets the tables and the column when the hub first opens it; a hub that is rolled back ignores them):

* `nicknames` (account id, nickname, `folded` (lower-case), `key` (the squash, unique: the lookalike test), the time of the nickname, the tag, the time of the tag);
* `nick_hold` (the squash of an old nickname, its folded form, the account that holds it, until when);
* `login_claim` (one row for an account that has no nickname: the squash of the login name that it was passed for in the game, until when: 30 days after the last pass; a nickname does not give way to it, it gives way to a nickname);
* `lines.tag`: the tag that the author had when it wrote a chat line (so that an old line keeps the tag it was written with).

**Forget me** ([HUB_LOGIN.md](HUB_LOGIN.md#login)) deletes the account's `nicknames`, `nick_hold` and `login_claim` rows. The hub never logs a name, a pass or an account id of this feature; a nickname is public by design (it is shown to everybody), a pass is a two-minute signature of one name and holds no account.

## The frames

* **Up**, from any socket: `{"t":"name","r":n,"nick":"Zoë","tag":"ÄNT","save":false}` checks both fields (and saves them, both or none, when `save` is true: a nickname that is refused leaves the tag as it was); `{"t":"name","r":n,"guest":true,"nick":"Wren"}` checks a guest's name (a reader, a socket with no login, may only check guest names). The checks are rate limited per socket (a burst of 12, then four a second) and the saves per account across all its tabs (3, then one every 20 seconds).
* **Down**, to the asking socket: `{"t":"name","r":n,"ok":bool,"nick":{"ok":bool,"why":word,"until":seconds}|null,"tag":{...}|null,"shown":"[ÄNT] Zoë","saved":bool}`. A field that equals what the account has now is fine and changes nothing.
* **`you`** (the welcome and every change; to every socket of the account): `nick`, `tag`, `nickat` (when the nickname was set), `nickfree` and `tagfree` (when each may change again, 0 when now). A saved name reaches the account's other tabs as a whole `you` frame, and the chat's lines, the players list and the rooms list use the new name from the next row on (lines already written keep the name and tag they were written with).
* **Presence rows and chat lines** carry `tag` next to the name.
* **`GET /hub/me`** answers `play: {"name": ..., "pass": ...}` next to `you` ([The pass](#the-pass)).

## The pass

The game server is not the hub, so the hub **signs** the name that a logged-in person plays under, and the server takes a nickname only with that signature. `hub/namepass.py` makes it, `src/ants_server/name_pass.cpp` checks it; the contract is the same words in both.

* **The key** is the one setting that the hub and the game server already share, `ANTS_ADMIN_GATE_SECRET` ([HUB_ROLES.md](HUB_ROLES.md#the-admin-pages-door)), with its own label: `key = HMAC-SHA-256(secret, "ants-name-pass-v1")`. A pass of the admin page's door is never good for a name.
* **A pass** is `<exp>.<mac>`: `exp` is seconds since 1970 (now + 120), `mac` the first 32 hex digits of `HMAC-SHA-256(key, "name1" NUL exp NUL name-as-UTF-8)`. It holds no account, no id and no reference.
* **What the server does.** With the key (`Config::name_gate`): a Hello or a NameMsg whose pass is good **for exactly that name** keeps the name; every other name becomes a guest's name ([Guest names in a room](#guest-names-in-a-room)). A pass that has ended, one that claims to last longer than the hub makes them (plus 30 seconds), one for another name and one of the wrong shape are all "no pass". A pass is not used up: a stolen pass gives the thief that one name for two minutes, and nothing else. The page asks for a fresh one every 90 seconds while a room is open (`/hub/me`) and tells it with each rename. A rejoin ignores the name: the seat keeps the one it had.
* **Without the key** (a LAN host, a direct host, a server that was not given the key): nothing is signed, the names are only checked by the wire's rules, as before.

## Guest names in a room

The server makes a guest name by itself: the letters, digits, spaces and `. _ - '` of what the client said, cut to 16 characters, then ` (Guest)`. The one exception is the placeholder that a page makes for a visitor who typed nothing, `Guest 482913`, which stays as it is. A name with nothing left is `Player 3 (Guest)` (the seat's number). So a person who is not logged in, or whose pass is missing or bad, can never go by "[ANTS] Anyone" or by somebody's nickname in a room; the worst they can do is to type a name that fits the rules above.

## The wire (protocol 29)

A name on the wire is **UTF-8**: printable ASCII and the letters of the table, at most 32 characters and 96 bytes (`ants_net/name_rules.hpp`; `web/front/name_letters.js` is its twin, and the two are held to the same cases). The NameMsg and the Hello carry the pass after the name; the Room, Start and Chat messages carry names in UTF-8 (a room's own notices, `kRoomSender`, may hold name letters; a person's chat line is printable ASCII still). The replay's head keeps the names as UTF-8. The layout, the refusals and the old files are in [NETWORK_PORT.md](NETWORK_PORT.md#protocol-29-utf-8-names-and-the-hubs-pass).

## The letters

What may stand in a name is what the game's font draws: the Latin letters (A-Z, the Latin-1 letters, Latin Extended-A, the Vietnamese letters of Latin Extended Additional, `Ơ ơ Ư ư`, `Ș ș Ț ț`) and the modern Cyrillic ones (U+0400-U+045F, `Ґ ґ`). `hub/name_letters.py` is the table's home; `tools/gen_name_letters.py` writes the C++ include (`src/ants_net/name_letters_data.inc`) and the page's block (`web/front/name_letters.js`) from it, and a test fails when either differs. Greek, Hebrew, Arabic, CJK, emoji and every combining mark are refused with `script` (or `chars`), the name is not cut. A name is compared in NFC.

## The page

* **Logged in.** The chat menu's name chip opens the sheet "Your name": a live preview ("How others see you"), the nickname field and the tag field, each with its counter, the hub's answer under the field, the rules, and Save. The preview is `[TAG] Nickname`. Save is allowed when the hub said both fields are fine.
* **Guest.** A chip with a dashed ring in the top bar (under 375 px it takes the Discord mark's place) and, on the first visit with no name set, a welcome card that offers the Discord login or this sheet ([HOME_CHAT.md](HOME_CHAT.md), "Your name"). The same sheet without the tag. The name is kept in this browser (`ants.guestname`) and nowhere else; another browser starts over.
* **Rooms.** The room's seats follow the current room layout: rows on the wide page and tiles on a phone, with the tag and "(Guest)" dimmer than the name, four players included.

## In the game

The results' alliance rows take two lines when the names are long (the winners' well, "A &" over "B"; the others' rows when "A & B" does not fit one line), the countdown card and the desktop game's room page cut a long name to whole characters and three dots (`fit_name`), the text layout is UTF-8 safe (a letter of two or three bytes is never cut in half), and a sentence of a window that has a letter which the windows' face (Libre Baskerville, Latin only) lacks, such as a Cyrillic name in "Victory!", is drawn in the game's own face. The game takes the hub's pass on the command line with `--name-pass` (the web page does it for the wasm build).

## Limits, and what is not here

* A moderator takes a nickname and a tag away with `/rename NAME` ([HUB_COMMANDS.md](HUB_COMMANDS.md)); the person may choose again at once. There is no other way to make a name "rude" than the word list: a name that gets past it is reset by a moderator.
* The rude-word list is short and English-leaning; a name that gets past it is reported and handled like any other name.
* A browser's font may draw a rare letter of the table differently from the game's font.
* The table is the coverage of the bundled font (Libre Franklin Medium, the face of the web game too). A Windows machine that has Franklin Gothic Medium in its fonts folder uses that face first ([GAME_REVERSE_ENGINEERING.md](GAME_REVERSE_ENGINEERING.md)); its coverage of the Vietnamese letters was not measured, and a letter it lacks is drawn as the font's missing-letter box, never a crash.

## The tests

`tests/scripts/test_hub_names.py` (the rules, the limits, the holds, the frames, the pass, forget, `/rename`, `/hub/me`), `tests/scripts/test_name_letters.py` and `tests/scripts/web_name_letters_check.js` (the table and its twins), `tests/test_net/test_lobby_names.cpp` (the wire, the pass, the guest names, the notices), `tests/scripts/web_lobby_net_check.js` (the page's client), `tests/test_app/test_scorecard_new_look.cpp` and `tests/test_app/test_hud_layout_screens.cpp` (the game's lines), and mutants of each rule (`tools/mutate.py`): `tests/data/hub_names_mutants.json` (the rules, the limits, the pass, `/rename`, Forget me), `tests/data/name_pass_mutants.json` and `tests/data/names_wire_mutants.json` (the server's pass, the wire's names, the guest names and the room that has the hub's key), `tests/data/names_game_mutants.json` (the game's long names: an alliance on two lines, a cut at a whole letter, dots), `tests/data/home_names_mutants.json` (the sheet and the chip) and `tests/data/player_name_mutants.json` (the name that a page plays under, the pass in its Hello).
