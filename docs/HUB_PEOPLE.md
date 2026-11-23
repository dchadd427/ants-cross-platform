# Alerts, invitations and mentions

This document is the part of [HUB.md](HUB.md) that belongs to the people of the front page: the list of **alerts** that an account's bell shows, **invitations** to a room (`/invite`, and "Invite to my room" in the page's menu) and **mentions** in the chat (`@name`). The code is `hub/people.py` (with the store's `alerts` table in `hub/store/`, the room that a page says in `hub/presence.py`, the command in `hub/commands.py` and the marks of a line in `hub/chat.py`); what a person sees of it is [Home's alerts](HOME_SCREEN.md#alerts), [the chat](HOME_CHAT.md) and [its commands](HUB_COMMANDS.md).

## Alerts: the account's list

An alert is one row of the account's own list: the newest **30** of the last **7 days**, kept by the account's pseudonymous reference ([HUB.md](HUB.md#data): never the account's id), so it is the same on every device of the account. A visitor who is not logged in has no list on the hub (the page keeps a few alerts of its own in the browser). There are three kinds:

| Kind | What happened | Fields of its row |
|---|---|---|
| `invite` | somebody asked the account into their room | `from`, `map` (a file name, may be absent), `n` and `of` (the seats taken and the seats of the room), `o` and `code` while the room is open |
| `mention` | somebody wrote the account's name after an `@` | `from`, `text` (the start of the line, 80 characters at most) |
| `join` | a person joined the account's room (the lobby's own news, which the account's page tells: see `note`) | `name`, `seat` (`Green`, `Red`, `Blue` or `Black`) |

Every row is `{"id", "at", "k": kind, "u": 1 while it is unread, ...}`. Down: `{"t":"alerts","rows":[...]}` is the whole list, newest first, sent after the welcome of a login (not when the list is empty), and again to **every** socket of the account when it changed (read, cleared, an invitation ended, a line taken off); `{"t":"alert","row":{...}}` is one new alert, at once, to every socket of the account. Up (a login is needed, `login` otherwise; anything not of these shapes is `bad`):

| Frame | Does |
|---|---|
| `{"t":"seen","upto":id}` | everything of the account's list up to that number is read (the page sends it when the bell's list is closed); nothing is sent back when nothing changed |
| `{"t":"clearalerts"}` | the list is emptied |
| `{"t":"note","k":"join","name":...,"seat":...}` | the account's page says that a person joined its room; it becomes a `join` alert of the account's own list (a burst of 10, then one every 6 seconds; `rate` beyond) |

An account that forgets itself ([HUB.md](HUB.md#login)) loses its list and every alert of the others that is from it (an invitation or a mention carries the sender's name). A line that a moderator deletes takes its mention alerts out of every list. The file keeps no alert for longer than 30 days whatever the lists ask.

## Invitations

`/invite NAME` (a shown name, in any letter case), `/invite row:N` (a row of the list of the front page) and `/invite #N` (the author of a line) invite a person to **the room that the inviter's page says it waits in**. The page says it in its `here` frame ([HUB.md](HUB.md#presence)): `inv` is `{"code","map","n","of"}`, the room's code (also of an invite-only room), its map's file name and its seats, only for a page whose `at` is `wait`. It is kept in memory while the socket is open and the page says it, is never in a row or a frame of the list, and an `inv` that is not exactly this (a code that is not the game server's shape, seats that do not fit) is a refused `here`. The page sends a fresh `here` just before it asks for an invitation, so the room is as it is now.

The answer is the private `reply` of the command `invite` ([HUB_COMMANDS.md](HUB_COMMANDS.md#the-answers)): `ok` (`to`), `usage`, `nobody`, `ambiguous`, `self`, `noroom`, `full` (`n`, `of`), `inroom` (`to`), `busy` (`to`), `person` (`to`, `wait`) and `total` (`wait`). The person is sent an `alert` of kind `invite` (and their list, on every socket). Who may be invited: a person who is logged in and has a socket open (a visitor has no name, an account that is banned from the chat is nobody here, and a person who is only in the chat's memory because they wrote lately is not here). Not the inviter, not a person who is playing a match, not a person already in the inviter's room, and not into a room that is full.

- **Limits.** One invitation to the same person in a minute (`person`), five in a minute in all for an account across all its tabs (`total`: a burst of 5, then one every 12 seconds). A refusal costs nothing. An account that cannot chat (muted, too new) cannot use a command at all.
- **A person who ignored the inviter** ([HUB_COMMANDS.md](HUB_COMMANDS.md#ignore-and-unignore)) is sent nothing, and the inviter is told `ok`: the limits count it all the same, so the two cases cannot be told apart. A person who now holds a role is never hidden by an old ignore.
- **How long it is open.** The invitation carries the room's code while the inviter's page keeps saying that it is in that room with a free seat. When it has not for **15 seconds** (the room closed, filled or was left; a page that connects again must not end it) or after an hour at the most, the invitation ends: its `code` goes from the stored row, so nothing of an invite-only room stays in the database, and the list is sent again without `o` and `code`. A hub that starts again closes every invitation it finds open, the first time a list is made.

## Mentions

A line that holds `@` at the start of a word and after it the shown name of **exactly one person** marks that name: the line's frame carries `m`, a list of `[start, length, key]` in UTF-16 units of `text` (the page's own measure) and the person's key (`k`, the same short key that marks a line's author), so every screen draws the same marks and none guesses from the words. A name is looked for in any letter case; the longest known name wins (so a name with a space works); it must end at a character that is not a letter, a digit or `_`. A person who is here or who wrote lately (a line of theirs is in the chat's memory) can be named; a visitor has no name; an account that is banned from the chat is nobody; `@here` and `@everyone` are nobody's (`here` and `everyone` are reserved names, [HUB.md](HUB.md#names)). A name that two people show is not marked. At most 10 names are marked in a line. The marks are kept in memory with the line's place in the chat's history, so a page that connects later has them too.

The first **3** people a line names (never its writer) get a `mention` alert, unless they ignored the writer, the writer alerted them in the last **2 minutes**, or they had **10** mention alerts in the last hour from everybody (a budget that fills up again steadily); later lines are still marked, they just do not alert again. A person who is muted is still alerted (they can read); a person who turned the alert off on their page still has the entry in the list, quietly (the switch is the page's).

## Tests

`tests/scripts/test_hub_people.py` runs it: every refusal and limit of an invitation, the room that a page says, the code that is gone when the room is, every rule of a name and of who is alerted, the list and its frames, and a real hub with real sockets (the code and the lines of a person are in no log); the store's checks are in `tests/scripts/test_hub_store_contract.py`. `tests/data/hub_people_mutants.json` breaks the rules one at a time for `tools/mutate.py` (every one must be caught).
