# Private messages

This document is the part of [HUB.md](HUB.md) that belongs to **private messages**: one person writes to another, and only the two can read it. The code is `hub/messages.py` (with the store's `dms` and `reports` tables in `hub/store/`, the commands `/msg` and `/ignore` in `hub/commands.py` and the bell's `msg` row in `hub/people.py`); what a person sees of it is [the Messages tab of the bell and the conversation's panel](HOME_CHAT.md#private-messages-and-follows). **Follows**, the other part of the same slice, are in [HUB_PEOPLE.md](HUB_PEOPLE.md#follows).

## Who may write to whom

A person who is **logged in** may write to any other person that the hub told them of (not one who is banned now, whatever handle they were given before the ban: that is `who`), and the messages are **open to everybody** for now: there is no request step. The sender passes the chat's own rules (not banned, not muted, not too new: `banned`, `muted`, `young`); the text is cleaned as a line of the chat is (`hub/text.py`: no markup, the invisible characters out) and is at most **500** characters. A person who does not want a person's messages **blocks** them (Block is `/ignore`, [HUB_COMMANDS.md](HUB_COMMANDS.md#ignore-and-unignore): one list for their lines, invitations, mentions and messages). The rest of the rules are the numbers below; if abuse becomes a problem the answer is to lock it down later (a request step), not to read the messages.

A page names the other person by a **handle** (`hub/chat.py` `handle`: a keyed mark that is new with every start of the hub), which the hub sent to **that account** in a list, a thread, a reply of `/msg` or a bell row; any other handle is `who`. It never sees a reference or an id.

## The frames

Up (a login is needed, `login` otherwise; a frame that is not exactly one of these shapes is `bad`):

| Frame | Does |
|---|---|
| `{"t":"dm","h":H,"text":...}` | send a message |
| `{"t":"dmopen","h":H}` | open a conversation: the answer is `thread`, and what was unread is read |
| `{"t":"dmread","h":H}` | what was unread is read (the page that has the conversation open says so for a message that came in) |
| `{"t":"dmdel","h":H}` | the conversation is gone for this account; the other person keeps theirs |
| `{"t":"dmreport","h":H,"id":N,"why":"abuse"\|"spam"\|"other"}` | report a message that the person sent |

Down, to the account's own sockets: `msgs` (`{"n","rows":[{"h","name","at","text","me","u","f","b"}]}`: the conversations, newest first, `text` the start of the newest message, `me` 1 when it is the account's own, `u` the messages not read, `f` 1 for a person that the account follows, `b` 1 for one that it blocked; after the welcome when not empty, and again when a conversation is read, deleted or blocked), `msg` (one message, `{"h","name","id","at","text","me","u","f"}`, to every socket of the sender and, unless the receiver blocked the sender, of the receiver), `thread` (`{"h","name","f","b","rows":[{"id","at","text","me"}]}`: the newest 100 messages of a conversation, to the socket that opened it), `dmx` (`{"h","why"}`: a refusal, to the asking socket, one of `empty`, `long`, `bad`, `banned`, `muted`, `young`, `who`, `dup`, `rate`, `quota`, `new`, `full`, `busy`, `gone`) and `dmreported`. A refusal is no mistake of the socket, but costs it a frame more.

`/msg WORD` (WORD as for `/follow`) answers with the private `reply` `open` (`h`, `to`): the handle that the page opens the conversation by. A new message gives the receiver's pages the `msg` frame, a toast and a count (on the Messages tab and on the bell), and the receiver's list **one bell row for each sender** (kind `msg`: `{"from","h"}`, never a word of the message), replaced by the sender's next message and gone when the conversation is read. The row is kept for a receiver who is **not here** as well; the page uses it for the toast and draws none of it in the bell's list (the Messages tab lists the conversations, so a message is counted once); the frames are for the pages that are open.

## What is kept, and for how long

By the references of both people (never an id), with the names as they were shown, in the table `dms` (made by the first message): **7 days** from sending (`limits.dm_days`, deleted by the hub's regular purge and never shown past it, whatever the file still holds), at most **300** to a conversation that the receiver may see (the newest; what is hidden from the receiver is cut by itself, the newest 300 of the sender's, so it never takes the place of what the receiver sees), and the two tables stop growing at **a quarter of the database** (`limits.dm_table_percent` of `db_max_bytes`: a new message is `full`; nobody else's is deleted). Either person can delete a conversation for themselves at once (`dmdel`: a message that nobody can see any more goes from the file); **Forget me deletes the account's messages for the other people of them as well**. They are **not end-to-end encrypted**: whoever runs the server could open the database by hand, and the Legal page says so.

## Limits

| What | Limit |
|---|---|
| the text | 500 characters, after the chat's text rules |
| sending | a burst of 5, then one every 2 seconds, and 300 a day, for an account across all its tabs; the same text to the same person twice in 30 seconds is `dup` |
| new conversations | 10 an hour (a first message to a person that the account has no messages with: `new`) |
| reading and deleting | a burst of 30, then two a second (`rate`) |
| reports | 5 a day (a budget that fills up again steadily); the same message twice is one report, and the repeat costs nothing of the 5 |
| the list and the thread | 50 conversations and 100 messages are sent |

## A blocked sender

A person who blocks another (and the other holds no role) is sent **nothing** of theirs: the message is kept for the sender, in their own copy of the conversation, and is **hidden from the receiver for good** (not shown if the block is lifted later); the sender gets the same `msg` echo as for any message, no `dmx`, no receipt and no mark, and the receiver's pages get no frame and no bell row. A person who holds a role is never blocked (a person who held none when they were blocked may hold one now).

## Reports, and what the staff can see

**No screen and no command of the hub shows a message to a moderator or an administrator**, apart from a report. A report is a copy that the hub makes from its own store, never from the page: the reported message and up to **5** before it in the conversation (the ones that the reporter could see, and what they sent: not a message that was hidden from them, not a conversation that they deleted and not a message past its 7 days), with both names as they were shown and one of three reasons. Only a message that the person **sent to the reporter** and that the reporter may see can be reported (`gone` otherwise). The reported person is told nothing, not that there was a report and not who made it. The operator reads reports on the command line (`python -m hub.admin reports`, `--all` for the closed ones, and `report-close ID`: each prints the number, the time, the reason, both names, the references that `ban` and `mute` take, and the copy) and acts with the existing mute and ban; nothing happens by itself. A look is **logged**: a report that is made, each listing of the reports and each close are rows of [the moderators' log](HUB_ROLES.md#the-moderators-log) (`report`: who reported and about whom, with the report's number; `report_read`: how many were listed; `report_close`: the number), never a word of a message. A report is deleted **30 days after it was closed** (`limits.report_closed_days`), **90 days** after it was made if nobody closes it (`limits.report_open_days`). Forget me takes the reporter's name and reference out of a report that they made and the name of a person out of a report about them; the copy stays, as a ban does, because it is what keeps the others safe: **a report keeps its copy of the messages for up to 90 days even after Forget me** (and after either person deleted the conversation), and the Legal page says so.

## Tests

`tests/scripts/test_hub_messages.py` runs it: who may write and every refusal and limit, where a message goes and where it never does, the bell's one row for each sender, a blocked sender's identical answer, the thread, what is read, deleted and kept (7 days, 300, 100), what a report holds and its limits, that a report, a look and a close are rows of the moderators' log, that the days are the limits, that no frame, command or screen of the staff holds a message that was not reported, Forget me, and real sockets; the store's checks (messages, reports and the follows, and that reading, purging and forgetting make no table) are in `tests/scripts/test_hub_store_contract.py`. `tests/data/hub_messages_mutants.json` breaks the rules one at a time for `tools/mutate.py` (every one must be caught). The page's part is in [the chat](HOME_CHAT.md#private-messages-and-follows).
