# Alerts, invitations and mentions

This document is the part of [HUB.md](HUB.md) that belongs to the people of the front page: the list of **alerts** that an account's bell shows, **invitations** to a room (`/invite`, and "Invite to my room" in the page's menu), **mentions** in the chat (`@name`) and the account's **own match** that is over. The code is `hub/people.py` (with the store's `alerts` table in `hub/store/`, the room that a page says in `hub/presence.py`, the command in `hub/commands.py` and the marks of a line in `hub/chat.py`); what a person sees of it is [Home's alerts](HOME_SCREEN.md#alerts), [the chat](HOME_CHAT.md) and [its commands](HUB_COMMANDS.md).

## Alerts: the account's list

An alert is one row of the account's own list: the newest **30** of the last **7 days**, kept by the account's pseudonymous reference ([HUB.md](HUB_LOGIN.md#data): never the account's id), so it is the same on every device of the account. A visitor who is not logged in has no list on the hub (the page keeps a few alerts of its own in the browser). There are four kinds:

| Kind | What happened | Fields of its row |
|---|---|---|
| `invite` | somebody asked the account into their room | `from`, `map` (a file name, may be absent), `n` and `of` (the seats taken and the seats of the room), `o` and `code` while the room is open |
| `mention` | somebody wrote the account's name after an `@` | `from`, `text` (the start of the line, 80 characters at most) |
| `join` | a person joined the account's room (the lobby's own news, which the account's page tells: see `note`) | `name`, `seat` (`Green`, `Red`, `Blue` or `Black`) |
| `over` | the account's own match is over (the page that came back from it tells: see `note`); kept **read**, the page showed it already | `map` (a file name), `place` (1 to 4, may be absent), `match` (the match's id, which is its replay's file name without `.antsrep`: the page links its stats; may be absent) |

Every row is `{"id", "at", "k": kind, "u": 1 while it is unread, ...}`. Down: `{"t":"alerts","rows":[...]}` is the whole list, newest first, sent after the welcome of a login (not when the list is empty), and again to **every** socket of the account when it changed (read, cleared, an invitation ended, a line taken off); `{"t":"alert","row":{...}}` is one new alert, at once, to every socket of the account. Up (a login is needed, `login` otherwise; anything not of these shapes is `bad`):

| Frame | Does |
|---|---|
| `{"t":"seen","upto":id}` | everything of the account's list up to that number is read (the page sends it when the bell's list is closed); nothing is sent back when nothing changed |
| `{"t":"clearalerts"}` | the list is emptied |
| `{"t":"note","k":"join","name":...,"seat":...}` | the account's page says that a person joined its room; it becomes a `join` alert of the account's own list (a burst of 10, then one every 6 seconds; `rate` beyond). A name that cannot be used is answered `bad` and is not a mistake of the socket: it is the lobby's, not the visitor's |
| `{"t":"note","k":"over","map":...[,"place":n][,"match":id]}` | the account's page says that the match it came back from is over; it becomes an `over` alert, read (a burst of 3, then one a minute; `rate` beyond; a match that is in the list already is not a second alert; a map, a place or a match that is not the right shape is `bad`) |

An account that forgets itself ([HUB.md](HUB_LOGIN.md#login)) loses its list and every alert of the others that is from it (an invitation or a mention carries the sender's name). A line that a moderator deletes takes its mention alerts out of every list. **How long it is kept.** The hub's regular purge cuts every alert that is older than the 7 days (`limits.alerts_days`) from the file, whether or not its account gets another one, so "kept on your account for 7 days" is true of the database as well as of the list. The mention of a chat line goes with the line when the purge or the store's making of room deletes it (the quote must not outlive the line), and the open pages are sent their lists again when that took an alert out. The tables of the alerts and of the ignored people are made by the first alert and the first `/ignore`: a hub that never has one holds nothing of them.

## Invitations

`/invite NAME` (a shown name, in any letter case), `/invite row:N` (a row of the list of the front page) and `/invite #N` (the author of a line **that is in the history the pages are sent**: the hub's memory of the lines, not the whole database; the name in the answer is the line's own, never the account's name now) invite a person to **the room that the inviter's page says it waits in**. The page says it in its `here` frame ([HUB.md](HUB.md#presence)): `inv` is `{"code","map","n","of"}`, the room's code (also of an invite-only room), its map's file name and its seats, only for a page whose `at` is `wait`. It is kept in memory while the socket is open and the page says it, is never in a row or a frame of the list, and an `inv` that is not exactly this (a code that is not the game server's shape, seats that do not fit) is a refused `here`. The page sends a fresh `here` just before it asks for an invitation, so the room is as it is now.

The answer is the private `reply` of the command `invite` ([HUB_COMMANDS.md](HUB_COMMANDS.md#the-answers)): `ok` (`to`), `usage`, `nobody`, `ambiguous`, `self`, `noroom`, `full` (`n`, `of`), `busy` (`to`), `person` (`to`, `wait`) and `total` (`wait`). The room is looked at **first** (`noroom`, `full`): a person with no room, or a full one, learns nothing about anybody. The person is sent an `alert` of kind `invite` (and their list, on every socket). Who may be invited: a person who is logged in and has a socket open (a visitor has no name, an account that is banned from the chat is nobody here, and a person who is only in the chat's memory because they wrote lately is not here). Not the inviter, not a person who is playing a match (`busy`), and not into a room that is full. A person whose own page says that it is in the inviter's room already is sent nothing and the inviter is told `ok`, as for a person who ignored the inviter: no answer says where a person is.

- **Limits.** One invitation to the same person in a minute (`person`), five in a minute in all for an account across all its tabs (`total`: a burst of 5, then one every 12 seconds). An answer that tells that a person is here, or is not (`nobody`, `ambiguous`, `busy`), costs one of the five as well, so that nobody can look through the names or the lines for who is online; what says nothing of anybody (`usage`, `self`, `noroom`, `full`, `person`) costs nothing. An account that cannot chat (muted, too new) cannot use a command at all.
- **A person who ignored the inviter** ([HUB_COMMANDS.md](HUB_COMMANDS.md#ignore-and-unignore): the list is kept in the store and holds after a restart of the hub) is sent nothing, and the inviter is told `ok`: the limits count it all the same, so the two cases cannot be told apart. A person who now holds a role is never hidden by an old ignore.
- **What the page says of its room is trusted as sent.** The hub cannot check that an account is in the room whose code its page says (the game server knows names, not accounts), so a page can claim any well-formed code with its own seats and invite people into somebody else's room. That is accepted: the codes are random, a public room is listed anyway, and the only thing that the claim buys is an invitation that the limits above bound. It buys no answer about where a person is.
- **How long it is open.** The invitation carries the room's code while the inviter's page keeps saying that it is in that room with a free seat. When it has not for **15 seconds** (the room closed, filled or was left; a page that connects again must not end it) or after an hour at the most, the invitation ends: its `code` goes from the stored row, so nothing of an invite-only room stays in the database, and the list is sent again without `o` and `code`. A hub that starts again closes every invitation it finds open, the first time a list is made.

## Mentions

A line that holds `@` at the start of a word and after it the shown name of **exactly one person** marks that name: the line's frame carries `m`, a list of `[start, length, key]` in UTF-16 units of `text` (the page's own measure) and the person's key (`k`, the same short key that marks a line's author), so every screen draws the same marks and none guesses from the words. A name is looked for in any letter case; the longest known name wins (so a name with a space works); it must end at a character that is not a letter, a digit or `_`. A person who is here or who wrote lately (a line of theirs is in the chat's memory) can be named; a visitor has no name; an account that is banned from the chat is nobody; `@here` and `@everyone` are nobody's (`here` and `everyone` are reserved names, [HUB.md](HUB.md#names)). A name that two people show is not marked. At most 10 names are marked in a line. The marks are kept in memory with the line's place in the chat's history, so a page that connects later has them too.

The first **3** people a line names (never its writer) get a `mention` alert, unless they ignored the writer, the writer alerted them in the last **2 minutes**, or they had **10** mention alerts in the last hour from everybody (a budget that fills up again steadily); later lines are still marked, they just do not alert again. A person who is muted is still alerted (they can read); a person who turned the alert off on their page still has the entry in the list, quietly (the switch is the page's).

## Limits

| What | Limit |
|---|---|
| an account's list | the newest 30 alerts of the last 7 days (`limits.alerts_keep`, `alerts_days`), cut from the file by the regular purge |
| invitations | 1 to the same person in a minute; 5 in a minute in all for an account across its tabs (a burst of 5, then one every 12 seconds); an answer that tells who is here costs one of the 5 |
| a live invitation | ends 15 seconds after its room was last seen open with a free seat, and in an hour at the most |
| mentions | a line marks 10 names and alerts 3 people; 1 alert from the same person to the same person in 2 minutes; 10 mention alerts an hour for a person, from everybody |
| the notes of a page | `join`: a burst of 10, then one every 6 seconds; `over`: a burst of 3, then one a minute (both per account, across its tabs) |
| bodies | an alert's body is 600 bytes at the most; the quote of a mention is 80 characters |
| the people that an account ignores | 50 ([HUB_COMMANDS.md](HUB_COMMANDS.md#ignore-and-unignore)) |

## Tests

`tests/scripts/test_hub_people.py` runs it: every refusal and limit of an invitation, the room that a page says, the code that is gone when the room is, every rule of a name and of who is alerted, the list and its frames, the notes of a page, what the hub's clock does to the alerts, what a hub that starts again still knows, and a real hub with real sockets (the code and the lines of a person are in no log); the store's checks are in `tests/scripts/test_hub_store_contract.py`. `tests/data/hub_people_mutants.json` breaks the rules one at a time for `tools/mutate.py` (every one must be caught).
