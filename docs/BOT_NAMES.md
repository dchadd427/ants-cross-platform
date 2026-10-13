# Names of the computer players

How a computer player is named, shown and kept apart from a person. The code is `include/ants_ai/bot_names.hpp` (C++) and `web/front/bot_names.js` (the same in JavaScript); the list is `tools/bot_names.txt`.

## What a player sees

- A bot has a random name and is shown as `Pip (bot, Hard)`: the level is inside the parentheses. The two test kinds of `--bot` say what they are in small letters: `Pip (bot, idle)`, `Pip (bot, worker)`. The same string is shown in the lobby (also for a bot that the plan asks for and that is not seated yet, from the moment the leader sets the plan), the match, the results, the replay list and head, and the match history.
- A bot may have the name of a person; the ending `(bot, ...)` tells them apart. Two bots of one room never share a name.
- Matches that were stored before keep what they stored (`Bot (Hard)`); the legacy forms stay readable and reserved.
- **Where it is built (v0.12.36): a dedicated server's rooms** (the plan's bots, the leader's fill, the room's own `bots`, the server's matches of computer players). A game on the local network and a local game still say `Bot (Level)` for now; the in-game HUD label is not changed yet (below). The local game's own `--name` and `-N` are not checked against the reserved names yet (they name the local player and the teams of a game on one's own machine; the check, and what to do with a bad name there, belong to the local-game follow-up).

## The list and the picker

252 names of 3 to 8 ASCII letters (first names and ant-ish words), one name per line in `tools/bot_names.txt`; `tools/gen_bot_names.py` writes the C++ table and the JavaScript table (a test fails when a committed table differs from the list). None equals the twelve names the front page offers a visitor, none holds a word of `tests/data/bot_names_blocklist.txt`. The order is part of the picker: new names go at the end.

`pick_bot_names(key, count)` gives `count` different names: draw i is `mix64(mix64(key ^ seed) + i * golden) % N`, a repeat is skipped. `bot_name_key(text)` makes the key from a text (FNV-1a finished with `mix64`, never 0); `bot_seat_name(key, seat)` is entry `seat` of the four names of the key, so a bot keeps its name whatever the other seats are. A room's key is `bot_name_key(its code)`: fixed when the room is made, kept by a restart (the code is), so a restored room names its bots as before. Key 0 means no key: the legacy name.

## The label

`bot_label(name, tag)` makes `Pip (bot, Hard)`; `parse_bot_label` reads it back (and the short form `Pip (bot)` and the five legacy labels); `bot_short_label` makes `Pip (bot)`; a name that cannot be in a label (empty, a parenthesis, over 16 characters) gives the legacy label, so a bot is never shown blank.

## Who may be called what

`is_reserved_bot_name(text)`: a name that starts with `Bot (` or ends with `(bot)` or `(bot, ...)`, blanks, tabs and case ignored, is a bot's. A person cannot take it, in every place a person sets a name: the start menu (`check_player_name`), the room's Hello (renamed `Player N`) and rename (the person keeps the old name), the NameMsg decoder (refused), and the checks of the web pages (`nameCheck` in `web/front/lobby_rules.js`, and the same block in `web/lobby.html` and `web/shell.html`). **The guard must run on the same cleaned string that is stored** (trimmed, tabs gone), never on the raw input: `Pip (bot, Hard)<tab>` must not pass the check and then be stored.

## Network protocol 20

No layout changed. The Room message of a lobby room shows, in the empty seat of a colour whose plan is a bot, that bot's label (the room's own copy keeps the seat empty; `seating_hash` ignores the name of an empty seat). The decoder refuses a name on an empty seat that is not a planned bot's label, and the NameMsg decoder refuses a reserved name. The server seats the plan's bots under the same strings at START, so the Start message and the replay head carry them. A client of 19 is refused (`VersionMismatch`).

## The HUD (not changed yet)

The HUD cuts a label at 15 characters and `Pip (bot, Medium)` has 17. Proposal, to be shown as a picture first: the HUD shows `bot_short_label`, `Pip (bot)` (15 characters at most), with the level in the lobby, the results and the replays.

## Tests

`tests/scripts/test_bot_names.py` (generator, list rules, the JavaScript twin and its mutants), `tests/test_ai/test_ai_names.cpp` (the C++ twin against the same golden file and case table), `tests/test_net/test_lobby_rooms.cpp` N4.40 (the lobby), `tests/test_server` (the rooms). The C++ mutants are `tests/data/bot_names_mutants.json` (`tools/mutate.py`).
