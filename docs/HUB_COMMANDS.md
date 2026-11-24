# The chat's commands and moderation

This document is the part of [HUB.md](HUB.md) that belongs to the commands: what a person types in the chat, who may type what, the answers, `/bots` and the game server's door that it uses, `/ignore`, and the moderators' powers.

## The commands

A line that starts with a single `/` is a command, and **a command is never a line of the chat**: it is not kept, not shown to anybody, not counted among the account's lines (so it spends none of its ten at once and is no duplicate of a line) and not put in the history. `//` is how a line that starts with a slash is said: one slash is dropped and the rest goes as any line goes, with every limit of a line (`//shrug` says `/shrug`). A command word inside a line is only text. The checks that come before a line come before a command too: a banned account is refused `banned`, a muted one `muted`, a young one `young`, a reader `login`. Everything about the commands is in `hub/commands.py`, the one table (word, rank, usage, description, group, how its line is read, what it does): the list that is sent to a page, the ranks that [Moderation](#moderation) reads (`NEEDS`), the one place that decides that a word is not the account's own, and the answers.

| Who | Commands |
|---|---|
| everybody who is logged in | `/help`, `/invite NAME`, `/ignore NAME`, `/unignore NAME` |
| a moderator (the role comes from Discord: [Roles](HUB.md#roles)), and the above | `/mute NAME MINUTES`, `/unmute NAME`, `/del NUMBER`, `/kick NAME`, `/mutes` |
| an administrator (the role, or the operator's account), and all the above | `/ban NAME [purge]`, `/unban NAME`, `/bans`, `/nopic NAME`, `/pic NAME`, `/noimages NAME`, `/images NAME`, `/bots ...` |
| a reader | none: it cannot write, and it is sent no list |

The moderators' words and `/nopic` and `/pic` are described in [Moderation](#moderation) (`/nopic` is the admin page's block of a person's Discord picture and `/pic` its lift: the same change, the same log row, the rank that `/ban` and `/unban` need); the others are below.

### The `cmds` frame

`{"t":"cmds","rows":[{"w":"/help","a":"","d":"list your commands","g":"Everyone"},...]}`: the commands that this account has, in the order of the table above, at most 24 rows. `w` is the word with its slash, `a` the arguments as typed (text, may be empty), `d` a short description (40 characters at most) and `g` the group (`Everyone`, `Moderators` or `Administrators`). It is sent after the welcome of a socket that has a login, and again, after the `you` frame, to **every** socket of an account whose role changed (a demotion takes the rows away at once). A row above the account's role is never in it, and a reader is never sent one. The texts are written once, in the table; the page draws them as text only. The page never sends `/help` (it opens its own list); if one does, the answer is `reply help list`.

### The answers

`{"t":"reply","c":WORD,"r":CODE,...}` is the private answer to a command of this file, to the socket that asked and to nobody else; `c` is the command's word without its slash, or `unknown`. The page owns the words. The moderators' words keep the answers they had (`ok`, `bans`, `mutes` and the `err` codes of [Moderation](#moderation)).

| `c` | `r` | Fields and meaning |
|---|---|---|
| `unknown` | `unknown` | **a word that is not a command of the account's own**: a stray word, a bare `/`, and a command of a higher role (a moderator's `/bots`, a player's `/mute`). One frame, byte for byte, whichever of them it was |
| any | `rate` | `wait`: the whole seconds (at least 1) until the account may try again; a word that is no command of the account's own says `unknown` in `c` here too |
| `help` | `list` | the page opens its list |
| `invite` | `ok`, `usage`, `nobody`, `ambiguous`, `self`, `noroom`, `full`, `busy`, `person`, `total` | the answers of an invitation to a room, with `to` (the person's shown name), `n` and `of`, `wait`: [HUB_PEOPLE.md](HUB_PEOPLE.md#invitations) says every one |
| `ignore`, `unignore` | `ok`, `usage`, `nobody`, `ambiguous`, `self`, `staff`, `full`, `not` | `ok` has `n`, how many people the account ignores now |
| `bots` | `started` | `asked`, `started`, `queued`, `at_once` (when known), `every_s` (when the batch has a spacing), `held` (`cap`, `places`, `people` or `busy`: why the first start waits, when it does) |
| `bots` | `status` | `at_once`, `running`, `waiting`, `busy` (percent), `queues`: at most five, the oldest first, each `total`, `remaining`, `finished`, `running`, `kept`, `every_s`, `next_in` (seconds, -1 for none), `held`, `by` (the name of who started it, plain text of 40 characters at most) |
| `bots` | `stopped` | `stopped` (matches ended), `dropped` (starts cancelled) |
| `bots` | `usage`, `bad_count`, `bad_every`, `bad_option` | the line is not written as [`/bots`](#bots) says |
| `bots` | `bad_map` | `close`: up to three names of maps (empty when there are none close) |
| `bots` | `no_map`, `map_187`, `queue_full`, `no_replays` | what the game server refused; `queue_full` has `waiting`, `asked` and `max` |
| `bots` | `door` | the game server did not answer, or answered what is not the shape of an answer: **nothing was done** |
| `bots` | `off` | the hub has no address or no key for the game server |
| `bots` | `role` | the role could not be confirmed with Discord just now, or the account no longer holds it (the same meaning as `err role`) |

A refusal that is no mistake of the client's (`unknown`, `rate`) costs the socket a frame of its budget more, as every such refusal does ([Limits](HUB.md#limits)); it is no strike unless it goes on until that budget is gone.

### `/bots`

```text
/bots COUNT [every MINUTES] [rules highest|187] [bots 2|3|4] [levels easy|medium|hard|mixed|any]
      [teams free|12-34|13-24|14-23|1-23|2-13|3-12] [map NAME]
/bots status
/bots stop
```

The same matches as the admin page's Start now ([MAP_SUBMISSIONS.md](MAP_SUBMISSIONS.md#bot-matches-on-the-admin-page), [SERVER_REPLAYS.md](SERVER_REPLAYS.md#matches-of-computer-players)). The line is read strictly and bounded: the command word and the option words are read in any letter case (ASCII only), separated by blanks, each option at most once. `COUNT` is 1 to 10,000, digits only and at most five of them (more is `bad_count` before it is made a number). `every` takes whole minutes or a half, 0 to 120 (`every_s` of the request is that times 60; 0 is no spacing). `rules` is `highest` (highest score) or `187`, `bots` 2, 3 or 4, `levels` one level for all the bots (`mixed` is Medium or Hard by a draw, `any` is a draw of the three), `teams` `free` (free for all) or one of the six splits, which needs `bots 4` for the three of four (`12-34`, `13-24`, `14-23`) and `bots 3` for the three of three (`1-23`, `2-13`, `3-12`), else `bad_option`. What is left out stays open, as on the admin page: the rules, the number of bots and the teams are drawn, the levels are Medium or Hard, the map is the hourly rotation's. `map` goes **last** and takes the rest of the line: 1 to 96 characters, runs of blanks made one, with no control character and no slash or backslash (else `bad_map` with no close names). `status` and `stop` take nothing. `/bots stop` stops everything, the matches that run and the starts that wait, as the admin page's Stop all does. A line that cannot be read is answered `usage`, `bad_count`, `bad_every`, `bad_option` or `bad_map` before anything is asked of Discord or of the game server.

**The request is built, never copied.** The JSON that the game server is sent is made from the numbers and the words of the line (`count`, `every_s`, `map`, `rules`, `bots`, `levels`, `teams`, `split`: every key that the admin page sends, in the same shape), and from nothing else. The one free text of the grammar, the map's name, is looked up in the game server's own list of maps (`GET bot-options`, kept five minutes, at most 2,000 entries, a row that is not of the shape of a file name left out): the file's name or its name without `.lvl`, in any letter case, must be that of exactly one map, and then **the list's own file name** is what is sent. A map that the list has and says cannot be played is `no_map`; a name that matches none, or two files that differ in letter case only, is `bad_map` with up to three close names (the ones that start with the typed text or hold it, playable ones, the shortest first). A name that is typed is data: compared, and never sent. ISLANDS in 187 and every other refusal of the rules are the game server's to say (`map_187`).

### The game door

The setting `ANTS_HUB_GAME_ADMIN_URL` (`http://` and a host of letters, digits, dots and dashes, a colon and a port from 1 to 65535, nothing else: `http://ants-server:4030` in the stack, which needs `ANTS_SUBMIT_PORT=4030`; empty is none, and `/bots` is `off`, as it is when the admin page's key `ANTS_ADMIN_GATE_SECRET` is not good). The call stays on the stack's private network and publishes no port. A call is `GET` or `POST` to `<address>/admin/api/<tail>`, the tail one of `bot-options`, `bot-matches` (the start), `bot-matches/summary` (the status, and the number that runs at once after a start that did not say it) and `bot-matches/stop-all`, never a text of a person. Its headers are the ones that the admin page's calls carry through nginx:

- `X-Ants-Admin-Pass`: a pass made for this one call, `v1.<payload>.<mac>` ([The admin page's door](HUB.md#the-admin-pages-door): the same key, the same label, the same `sign`), whose payload is `exp` (now plus 60 seconds), `name` (the Discord username), `nonce` (16 hex characters, **new for every call**), `ref` (the account's reference) and `role` (`admin`, or `owner` for the operator's account), and nothing else: never an id.
- for a POST also `X-Ants-Csrf` (the game server's own token for that reference: the first 32 hex characters of the HMAC-SHA-256 of `csrf1`, a zero byte and the reference, keyed with the HMAC-SHA-256 of the secret and the label `ants-admin-csrf-server-v1`), `Content-Type: application/json` and `Content-Length`. No cookie and no `Sec-Fetch-Site`.

The client is a small HTTP/1.1 reader of its own (`hub/gamedoor.py`, the way `hub/discord.py` talks to Discord): 3 seconds to connect and 10 in all, the head and the body together read up to 128 KiB and not a byte more, no redirect followed, the status line, the headers and the lengths read strictly, and the answer must be a JSON object. Every failure is the one `door` answer, and none says an address, a header, a pass or the text of a cause. **One call is on its way for an account at a time, and two for the whole hub** (another is `rate` with `wait` 1); a call that waits does not hold up the chat (it is a task of its own, and a hub that stops ends it).

Before any call the hub confirms the sender's role **exactly as it does before a moderator's action** ([The fresh check](#moderation)): an answer of Discord that is a minute old at the most, else one urgent lookup that is waited for 3 seconds, from the share of the commands that wait (a second one while that share is spent is `rate`); the operator's account needs none. A lookup that fails, or an answer that says the role is gone, is `role`, and nothing is sent. The account is looked at again, for a ban or a mute, when the answers are in and just before the start is sent; a socket that went meanwhile has nothing done.

### The log of `/bots`

The moderators' log ([The moderators' log](HUB.md#the-moderators-log)) gets one row for each start and each stop that the game server **did** (never for one that was refused or that `door` covers): `bots_start` with the detail `1000 every 7200s` (the count that was asked and the spacing in seconds) and `bots_stop` with `stopped 3 dropped 940`. The actor is the account's reference and the target is empty. The detail is numbers and those fixed words only: the store refuses anything else, so a name, a map, a path or an id cannot get in. The game server writes its own line with the administrator's username.

### `/ignore` and `/unignore`

`/ignore NAME` hides the lines of a person from the account, in every one of its tabs: they are not sent live and are left out of the history that the account is sent when it connects. NAME is the rest of the line, read in any letter case against the **name that is shown** (never a username) of the people who have a socket open or whose line is in the chat's memory. One person: ignored (`ok`, with `n`); nobody: `nobody`; two or more who show that name: `ambiguous`; the account itself: `self`; a person who holds a role (a moderator, an administrator, the operator's account): `staff`. The list holds 50 people (`full`); asking again for one that is on it changes nothing. `/unignore NAME` takes a person off by the name that was shown when they were put on (`ok`, or `not`). The hub's own lines and the lines of a person who holds a role now, or held one when they wrote the line, are never left out, a deletion still reaches everybody, and **the person who is ignored is told nothing and sees no change**.

The list is **kept in the store**, in the table `ignores` (made by the first `/ignore`): the account's reference, the reference of the person and the name that was shown, never an id. The hub reads every list when it starts, so a restart does not give an ignored person back the line, the invitation and the mention that the account had shut out ([HUB_PEOPLE.md](HUB_PEOPLE.md)). Forget me ([Login](HUB.md#login)) drops the account's list and its place on the lists of the others.

### Limits of the commands

All per account, across all its tabs (the hub's own key of the account, never the id), in `hub/limits.py`:

| What | Limit |
|---|---|
| `/bots COUNT ...` (any line that begins like a start, one that cannot be read too) | a burst of 3, then one every 10 seconds |
| `/bots status`, `/bots stop` | a burst of 5, then one every 2 seconds |
| `/help`, `/ignore`, `/unignore` and every word that is no command of the account's own | a burst of 10, then one every 2 seconds |
| the moderators' words | no limit of their own: the frame budget of the socket, and the share of the commands that wait for a lookup ([Limits](HUB.md#limits)) |
| a call to the game server | 3 seconds to connect, 10 in all, 128 KiB of answer; one for an account at a time and two for the hub; the list of maps is kept 5 minutes (2,000 entries at most) |
| the people that an account ignores | 50 |

Over a limit the answer is `reply rate`. A command does not spend the account's lines, and a line (`//` included) is not helped by a command: the limits of [Limits](HUB.md#limits) hold for it as they did.

### What the commands never do

- **Who may is decided on the hub and nowhere else.** The role is the one that Discord last said (or the operator's account), read by `role_of`; nothing in a frame, a header or a page is read for it, and a `/bots` is confirmed again with Discord first. A page may show only the rows that the hub sent, and the hub answers every other word as one that is no command.
- **No oracle.** A word that is no command and a command of a higher role are one frame and one limit, so nothing in an answer says which commands exist above a role; a list never has a row above its role; `power` is only for a command that the account has, aimed at a person that it may not act on.
- **No broadcast.** A command's line never reaches the history, the database, another socket or the log, and its answer goes to the asking socket only. What a start did is in the moderators' log by reference.
- **Built requests.** What is sent to the game server is made from parsed numbers and words, the address is the setting's alone, and the only text from a person that can appear in a request is the file name of a map that the game server itself listed.
- **No id.** The pass has the reference and the username, the replies and the log rows have numbers and plain words, no frame or log line or error holds an id, a pass or the key, and a name that the game server says (`by`) is cut to plain text and is never told when it is all digits and as long as an id.

## Moderation

**The page.** The front page's chat reads all of these ([HOME_SCREEN.md](HOME_CHAT.md#the-chat-dock)): the role in `you` and the `you` frame, `/help` (never sent: the page opens the list of its `cmds` frame), the private answers in words, for `ok`, `bans`, `mutes`, each of `power`, `role`, `who`, `rate` and `bad`, and the `reply` frames of [The commands](#the-commands), the hub's own line (only `sys: true` makes one), and a plain notice for `bye`. It writes an administrator's name in red and a moderator's in yellow, from `you.role`, from `role` on a line and from `role` on a row of the players list (a hub that sends `mod` only is read as a moderator); a moderator or an administrator is also shown the number of each line (the number that `/del` takes) and the username that the hub sends in `u`.

Mutes, bans and deletions are kept by account id in `hub.db`, so they survive a restart, a new session and a rename. A banned account can still read. Change bans and mutes only with `hub.admin` or the chat commands: a writer that does not raise the counter in `meta` (see Data) may go unseen by a running hub until the next counted change or a restart.

From a terminal on the machine (no network door; run it as the image's own user, which `docker exec` does by default, so that the database files stay the hub's):

```text
docker exec ants-hub python -m hub.admin list [--lines N]      bans, active mutes, the newest lines with their ids
docker exec ants-hub python -m hub.admin mute ACCOUNT MINUTES  (0 lifts a mute; at most a year)
docker exec ants-hub python -m hub.admin ban ACCOUNT
docker exec ants-hub python -m hub.admin unban ACCOUNT
docker exec ants-hub python -m hub.admin del LINE_ID
docker exec ants-hub python -m hub.admin log [--days N] [--lines N]   the moderators' log
docker exec ants-hub python -m hub.admin status                how full the database is
docker exec ants-hub python -m hub.admin purge [--keep-lines N]   free space at once
```

`ACCOUNT` is a **username** (`@name` when the name is all digits): the one name that is unique, found in the account's profile, which its last login made; or the account's **reference** (16 hex characters: [the moderators' log](HUB.md#the-moderators-log)), which `list` and `log` print for every account, so an account that has no profile yet (it has not logged in since profiles were kept) is addressed by it; or, for a script that has it, an account id (digits, no leading zero). **No command ever prints an account id**: a Discord id is like a password, so `list`, `log`, and the answer of every command print the reference (a mute or a ban that a moderator made before the references were used has an id as the one who made it: it is printed as its reference too), and a wrong use is answered without the words that were typed. A name that the chat shows is never taken for a username. `list` shows the name that a line was written under and, in brackets, the account's username where that is another. A mute or a ban counts from the account's next line; a deleted line leaves every screen within two seconds (the hub looks every two seconds); a ban does not close the sockets that a running hub has open (the chat's `/ban` does). What the command line does is written to the moderators' log too, with `terminal` as the one who did it, and it has full power over everybody. Exit status: 0 done, 1 nothing to do, 2 wrong use.

`status` prints one line: what is in use of the 64 MB (the pages that hold something), the size of the log file (it counts for the limit as well, and is told on its own so that a long one is seen), how much of the rest is free to reuse (the pages that deleted lines left), and the lines kept with the time of the oldest. `purge` does by hand what the hub does by itself from 90%: it deletes the oldest lines (the moderators' and the deleted ones last, the newest 1,000 never), a chunk of 2,000 at a time with a tenth of a second between two chunks, so that the hub's own writes go on (SQLite's wait for the write lock is not fair: a tool that took the lock again at once would keep it), until the database is at 80% of its limit, and prints the status line before and after; `--keep-lines N` (100 or more, what a page shows) keeps the newest N lines instead and deletes all the rest. It exits 1 with "nothing to purge" when there is nothing to delete, and with "a reader holds the log file" when another program has `hub.db` open for reading, so that the log file cannot be cut ([Data](HUB.md#data)).

In the chat, an account whose role is `mod` or `admin` ([Roles](HUB.md#roles)) may type these commands, in any case (the text rules fold a run of blanks, a tab or a trailing blank to what the command needs), a username (the one that is unique) where a person is named, and nothing after them:

| Command | Who | What it does |
|---|---|---|
| `/mute username minutes` | mod, admin | the person cannot write for 1 to 10080 minutes; stored |
| `/unmute username` | mod, admin | lifts the mute (`bad` if there is none) |
| `/del id` | mod, admin | takes a line off every screen and out of the history (a line with an image takes its files with it, and the log says `del_img`: [HUB_IMAGES.md](HUB_IMAGES.md#moderation-and-the-log)); a line of one's own always may go (it is no power over a person and no entry of the log); a line of the hub's own may not |
| `/kick username` | mod, admin | closes the person's sockets and ends their login ([Kick](#kick)); the person must be in the chat |
| `/ban username` | admin | the hub's ban ([Ban](#ban)); `bad` if the person is banned already |
| `/unban username` | admin | lifts a ban: by the username, or by the name that the ban was made with when the person's profile is gone (the names that `/bans` shows) |
| `/bans` | admin | the newest 50 bans, in a private `bans` frame |
| `/mutes` | mod, admin | the mutes that are on (up to 50, the one that ends first at the top), in a private `mutes` frame: the username, the end time, and the row number when the person is here now |
| `/ban username purge` | admin | a ban that also deletes every line of that account written in the last 60 minutes, from every screen and from the history (the same deletions as `/del`, so the same bounded number of `del` frames, see `deleted_announce_max`); the same power check as `/ban`, and `bad` (nothing deleted) if the person is banned already |
| `/nopic username` | admin | blocks the person's Discord picture on this site, as the admin page's "hide the picture" does ([HUB_PICTURES.md](HUB_PICTURES.md)): the same change, the same row of the log (`nopic`), the same power check as `/ban`; `bad` if it is blocked already |
| `/pic username` | admin | lifts that block (the picture then stays off until the person turns it on); the same check as `/unban`; `bad` if it is not blocked |
| `/noimages username` | admin | takes away the person's posting of images, whatever their Discord role says ([HUB_IMAGES.md](HUB_IMAGES.md#who-may-post)): the same power check as `/ban`; the row `noimages` of the log; `bad` if it is taken away already. Their images that are in the chat stay until deleted |
| `/images username` | admin | gives it back (the Discord role counts again); the same check as `/unban`; the row `images`; `bad` if it was not taken away |

**Naming the person.** Where a command takes a username it may take instead `#` and the number of one of the person's lines (`/mute #148 10`: the author of line 148) or `row:` and a row number of the Players list (`/kick row:7`: the person with that row now, who must have a socket open). Neither can be a username (a username has no `#` and no `:`), so a command that was typed before means what it meant. The same table of the powers and the same fresh check apply. A line that is deleted or gone is `bad`; a line of the hub's own is `power`; a row that nobody has now (the person left, or the number never existed), and a `/kick` of a person who is not here, are `who`. A mute or a ban made this way is kept under the person's **username** (for a person who is not here and has no profile any more: their reference, the 16 hex characters that the moderators' log uses, which `/unban` and `/unmute` take as a name), never under the `#148` text and never an id, so `/bans`, `/mutes`, `/unban` and `/unmute` show and take a name. If the lines of a `/ban username purge` cannot all be deleted (the database refuses), the ban is still done, the person is removed and the answer is `ok`; the lines can be taken off with `/del`.

A command is not a chat line; it is never shown to anybody, and a good one is answered with `ok` to the one who typed it. A line that starts with one of these words (in any case) from an account that does not have it (a person with no role, one whose role is not known yet, a moderator's `/ban`) is answered like any word that is no command of the account's own, `reply unknown` ([The commands](#the-commands)), and never posted, so that a command never shows for everybody to read; `//` before a word says a line that starts with a slash. A moderator sees the username next to the name that a line shows, where they differ (`u`, in the copy of a line that a moderator is sent, live and in the history; nobody else is ever sent a username that the account chose not to show). The username means the account that is in the chat with it; when nobody is, it is the one account whose profile has it (a profile is kept 35 days after the last login); when two accounts are, or when nobody is and no one profile has it, the command is refused `who`: use the command line with the reference. A command from a banned or a muted account is refused before anything else (`banned`, `muted`).

**Who may act on whom.** There is one table, `allowed` in `hub/chat.py`: a **moderator** acts on people who hold no role; an **administrator** acts on people who hold no role and on moderators; **nobody acts on an administrator** (administrator against administrator is refused: two of them could silence each other and the chat cannot settle who is right; admins are appointed in Discord, so a dispute is settled there, or from the command line, which keeps full power; it also means that one administrator login cannot silence the others), **nor on the account in `ANTS_HUB_OWNER`** (always an administrator) **nor on themselves**. A command that the role has but may not do to that person is refused `power`; a command that the role does not have (a moderator's `/ban`, `/unban` or `/bans`) is not answered `power`, so that nothing says that it exists: it is `reply unknown`. Nothing is read from a client for it: the roles come from Discord ([Roles](HUB.md#roles)).

**The fresh check.** Before any action the hub looks at the last answer of Discord about the role of the one who acts and of the one acted on (a `/del` of one's own line needs only the first). If either answer is older than **60 seconds**, or there is none, it asks Discord about **both** at once, first in the queue of the lookups, and waits for the answers at most 3 seconds; then it decides on those answers and does what was asked, with no wait between the decision and the action. A lookup that fails (Discord down, slow, busy, refusing, or an answer that is not a member's) or is late **refuses the action** (`role`, not counted as a mistake of the socket; nothing is done and nothing is logged), and so does an answer that says that the one who acts holds no role any more: a role that is only believed is never enough for an action (it is for showing the page, for the grace of [Roles](HUB.md#roles)). The target need not be here, and Discord is asked about a person who is not in the chat when a moderator acts on them: a person who is not in the chat is found by the username of their profile, and their role is asked of Discord with the id that the database holds for that profile (the id is in the request and nowhere else: not in a log line, a frame, an error or the log of moderators). A person who is not in the server at all holds no role, and that is an answer. So a moderator can act on a person who is not here only when Discord says, now, that the person holds no role; and during an outage nobody, the owner account included, can act on a person whose role was not answered within the last minute (the command line is the fallback). A command that waits runs as a task of its own: the chat goes on, one command of a socket waits at a time (a second is `rate`), and nothing is written to a socket that closed meanwhile.

#### Kick

`/kick` **closes every chat socket of the person and ends every login of the account** (the cookie of the browser stops working): each socket is sent `{"t":"bye","why":"kicked"}` and closed (1000). There is **no timer**: the person is logged out and has to log in with Discord again, which they may do at once, and may write at once; a kick that is made again is made again. A kick is for a person who is in the chat (`who` otherwise). It is not stored beyond the log: kick is not mute (stays, cannot write for a time, stored) and not ban (stored, until lifted).

#### Ban

`/ban` is the hub's ban, the one that `hub.admin ban` makes: by account id, until `/unban` or `hub.admin unban`, no address (the hub keeps none). It stops writing (`banned`) and the entry in the list of players (the person is listed as a guest); **it also closes the sockets at once**, each sent `{"t":"bye","why":"banned"}`; the person can still log in and read like any visitor, since reading is public anyway, and their logins are not ended. A banned moderator keeps the Discord role but cannot use it (the ban is checked before a command).

#### The named line

When a kick or a ban was **done** (never for a command that was refused, whatever the reason), everybody in the chat is shown a line of the hub's own: `<name> was kicked.` or `<name> was banned.`, as `{"t":"line","who":"system","text":"alice was kicked.","mod":false,"k":"","sys":true,...}`. It names the person by the name that they are shown by (their display name or username, as they picked) and **never the moderator** (who did it is in the moderators' log only, by reference). `system` is a reserved name that no account can have, so the line cannot be mistaken for a person's; `sys` lets a page draw it as the hub's own, and a page that does not know it shows a line from `system`. **It is kept like any other line**: it is a row of `lines` with the empty account and the name `system`, in the history that a newcomer is sent, in the 200 newest lines and the 7 days, and deleted by the same purge; it can be deleted from the command line (not from the chat). A database that refuses it costs the line and never the action, which is done already; the same holds for the moderator's budget of these lines (see [Limits](HUB.md#limits)): over it, the action is done and answered and logged, and only its line is not made. **Forget me does not remove it** (it is the hub's line, not the person's), and it goes with the other lines within 7 days.
