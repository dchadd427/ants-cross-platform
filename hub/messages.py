"""Private messages (docs/HUB_MESSAGES.md): one person writes to another, and only the two can read it. OPEN to everybody who is logged in (there is no request step); a person stops another's messages by blocking them (`/ignore`, hub/commands.py).
The messages are kept by the references of both people (hub/store/, never an id), `limits.dm_days` (7) days, at most `limits.dm_thread_max` (300) to a conversation, and the tables stop at a quarter of the database (a new message is refused then;
nobody else's is deleted). No screen and no command of the hub reads them: a moderator sees only a REPORTED message and the 5 before it (hub/admin.py `reports`), a copy that the hub makes from its own store, never from a page. They are not end-to-end
encrypted: whoever runs the server could open the database by hand, and the Legal page says so.

A page names the other person by a handle (hub/chat.py `handle`) that the hub sent to this account (hub/follows.py `teach`). Up (a login is needed, `login` otherwise; a frame that is not exactly one of these shapes is `bad`):
  {"t":"dm","h":HANDLE,"text":...}                   send a message
  {"t":"dmopen","h":HANDLE}                          open a conversation: the answer is `thread`, and what was unread is read
  {"t":"dmread","h":HANDLE}                          what was unread of the conversation is read (the page that has it open says so for a message that came in)
  {"t":"dmdel","h":HANDLE}                           the conversation is gone for this account (the other person keeps theirs)
  {"t":"dmreport","h":HANDLE,"id":N,"why":"abuse"|"spam"|"other"}    report a message that the person sent (see below)
Down, to the account's own sockets: {"t":"msgs","n":unread,"rows":[{"h","name","at","text","me","u","f","b"}]} (the list of conversations, newest first, `text` the start of the newest message, `me` 1 when it is the account's own,
`u` the messages not read, `f` 1 for a person that the account follows, `b` 1 for one that it blocked; after the welcome when it is not empty, and again when a conversation is read, deleted or blocked), {"t":"msg","h","name","id","at","text","me","u","f","b"}
(one message, to every socket of the sender and, unless the receiver blocked the sender, of the receiver), {"t":"thread","h","name","f","b","rows":[{"id","at","text","me"}]} (the newest 100 messages of a conversation, to the socket that opened it),
{"t":"dmx","h","why"} (a refusal, to the asking socket: `empty`, `long`, `bad`, `banned`, `muted`, `young`, `who`, `dup`, `rate`, `quota`, `new`, `full`, `busy`, `gone`) and {"t":"dmreported","h","id"}. Everything of the same kind that is said to the
receiver's pages is the alert `msg` of the bell (hub/people.py): one row for each person, replaced by their next message, with no words of it, and gone when the conversation is read.

WHO MAY. A person who is logged in, not banned, not muted and not too new (the chat's own rules: `banned`, `muted`, `young`), to a person that the hub told them of (`who` for any other handle, and for themselves). The text is a line of the chat by the same text
rules (no markup, the invisible characters out), at most 500 characters. A BLOCKED sender (the receiver ignores them, and they hold no role) gets the same answer as anybody: the message is kept for them, in their own copy of the conversation, and is
hidden from the receiver for good; nothing is sent to the receiver's pages and nothing tells the sender (no receipt, no mark).
LIMITS. A burst of 5, then one every 2 seconds, and 300 a day for an account across all its tabs; the same text to the same person twice in 30 seconds is `dup`; 10 new conversations an hour (`new`); the calls that only read or delete are a burst of 30, then two a
second (`rate`); 5 reports a day. A report is the message and up to 5 before it in the conversation (the ones that the reporter could see, and what they sent), with both names and one of the three reasons, copied from the store; the reported person is not told who reported,
and a message that was reported once by the same person is the same report. A report is deleted 30 days after it was closed, 90 if nobody closed it.
FORGET ME. The account's messages go, for the other people of them as well, and the pages that are open are sent their lists again.
"""
import re

from .limits import RateByAccount, TokenBucket
from .store import StoreError
from .store.base import REPORT_REASONS
from . import text as textrules

FRAMES = ("dm", "dmopen", "dmread", "dmdel", "dmreport")
SHAPES = {"dm": {"t", "h", "text"}, "dmopen": {"t", "h"}, "dmread": {"t", "h"}, "dmdel": {"t", "h"}, "dmreport": {"t", "h", "id", "why"}}
HANDLE = re.compile(r"[0-9a-f]{12}\Z")
PREVIEW = 80                                     # the code points of the newest message that the list shows
OVERHEAD = 96                                    # the bytes that a stored message is counted at over its text and its names (what the store's own count adds)


def preview(text):
    return text if len(text) <= PREVIEW else text[:PREVIEW - 1].rstrip(" ") + "\u2026"


class Messages:
    def __init__(self, chat, limits, clock, dumps):
        self.chat = chat
        self.limits = limits
        self.clock = clock
        self.dumps = dumps
        self.sends = {}                          # the run's key of an account -> [burst bucket, day bucket, new-conversation bucket, {(the other person's reference, text): when}, last use]
        self.reads = RateByAccount(30, 0.5)      # the calls of an account that only read or delete: a burst of 30, then two a second
        self.reports = RateByAccount(limits.dm_report_day, 86400.0 / max(1, limits.dm_report_day))       # an account's reports (keyed by the run's key of the account)
        self.last = {}                           # account -> the list that its pages were last sent (a frame): a list that did not change is not sent again
        self.used = None                         # the bytes that the tables hold, as the store counted them last and as the hub added since (None: count again before the next message)

    # ---- the limits ----------------------------------------------------------------------------------------------------------------------------------------------------------------------

    def forget_idle(self, now, idle_seconds=300.0):
        self.reads.forget_idle(now)
        self.reports.forget_idle(now)
        for key in [k for k, e in self.sends.items() if now - e[4] >= idle_seconds and all(b.ready(now) and b.tokens >= b.burst for b in e[:3])]:
            del self.sends[key]
        for account in [a for a in self.last if not self.chat.presence.sockets(a)]:
            del self.last[account]

    def check(self, key, partner, text, new, now):
        """None when the message may go (and it is counted), else `dup`, `quota` (the day's budget is spent), `rate` or `new` (the hour's new conversations are spent); a refused message costs nothing."""
        lim = self.limits
        entry = self.sends.get(key)
        if entry is None:
            entry = self.sends[key] = [TokenBucket(lim.dm_burst, lim.dm_refill_seconds, now), TokenBucket(lim.dm_day, 86400.0 / max(1, lim.dm_day), now),
                                       TokenBucket(lim.dm_new_hour, 3600.0 / max(1, lim.dm_new_hour), now), {}, now]
        burst, day, news, said, _ = entry
        entry[4] = now
        for old in [t for t, at in said.items() if now - at >= lim.dm_duplicate_seconds]:
            del said[old]
        if (partner, text) in said:
            return "dup"
        if not day.ready(now):
            return "quota"
        if not burst.ready(now):
            return "rate"
        if new and not news.ready(now):
            return "new"
        burst.take(now)
        day.take(now)
        if new:
            news.take(now)
        said[(partner, text)] = now
        return None

    def refund(self, key, partner, text, new):
        entry = self.sends.get(key)
        if entry is not None:
            entry[0].give_back()
            entry[1].give_back()
            if new:
                entry[2].give_back()
            entry[3].pop((partner, text), None)

    def full(self):
        """Whether the two tables hold a quarter of the database (or more): then a new message is refused, and nobody else's is deleted."""
        if self.used is None:
            self.used = self.chat.store.dm_usage()[2]
        return self.used >= self.limits.db_max_bytes * self.limits.dm_table_percent // 100

    # ---- what a page is sent -------------------------------------------------------------------------------------------------------------------------------------------------------------

    def refuse(self, peer, handle, why):
        peer.send_text(self.dumps({"t": "dmx", "h": handle, "why": why}))
        peer.charge()                            # (a refusal is no mistake of the socket, but it is not free either)

    def flags(self, account, ref):
        """(1 when the account follows the person, 1 when it blocked them), as the fields of a row say them."""
        chat = self.chat
        followed = any(other == ref for other, _ in chat.follows.list_of(account))
        blocked = ref in chat.commands.ignores.get(chat.ref(account), {})
        return followed, blocked

    def name_of(self, ref, last, mine):
        """The name that the account's pages are told for the person: the one they show now when they are here, else the one that their newest message to or from the account carried."""
        there = self.chat.account_of_ref(ref)
        shown = self.chat.shown_name(there) if there is not None else None
        return shown or (last.frm_name if last.frm == ref else last.to_name)

    def list_frame(self, account):
        chat = self.chat
        mine = chat.ref(account)
        rows, total = [], 0
        for other, last, unread in chat.store.dm_conversations(mine, self.limits.dm_list_max):
            name = self.name_of(other, last, mine)
            followed, blocked = self.flags(account, other)
            row = {"h": chat.follows.teach(account, other, name), "name": name, "at": last.at, "text": preview(last.text), "me": 1 if last.frm == mine else 0, "u": unread, "f": 1 if followed else 0}
            if blocked:
                row["b"] = 1
            rows.append(row)
            total += unread
        return self.dumps({"t": "msgs", "n": total, "rows": rows})

    def send_list(self, peer, force=False):
        """The account's conversations, to one socket after the welcome (nothing for a reader, and nothing when there are none unless `force`)."""
        who = peer.identity
        if who is None:
            return
        try:
            frame = self.list_frame(who.account_id)
        except StoreError as e:
            if self.chat.on_store_error:
                self.chat.on_store_error(e)
            return
        self.last[who.account_id] = frame                    # (what the pages of the account hold now: nothing, for an empty list)
        if force or b'"rows":[]' not in frame:
            peer.send_text(frame)

    def send_lists(self, account):
        """The account's conversations, to every socket of the account, when they are not what they were last sent."""
        try:
            frame = self.list_frame(account)
        except StoreError as e:
            if self.chat.on_store_error:
                self.chat.on_store_error(e)
            return
        if self.last.get(account) == frame:
            return
        self.last[account] = frame
        for peer in self.chat.presence.sockets(account):
            peer.send_text(frame)

    def refresh_all(self):
        """Every account that has a socket open is sent its conversations again if they changed (a purge, an account that forgot itself)."""
        for account in self.chat.accounts():
            self.send_lists(account)

    def after_purge(self):
        """The regular purge has run: the messages that were past their days are gone from the file, and the size is counted again."""
        self.used = None
        self.refresh_all()

    # ---- the frames of a page ----------------------------------------------------------------------------------------------------------------------------------------------------------

    def on_frame(self, peer, kind, msg):
        chat, who = self.chat, peer.identity
        if who is None:
            return chat.mistake(peer, "login")
        handle = msg.get("h")
        if set(msg) != SHAPES[kind] or not isinstance(handle, str) or not HANDLE.match(handle):
            return chat.mistake(peer, "bad")
        now, key = self.clock(), chat.key_of(who.account_id)
        try:
            if kind == "dm":
                return self.send(peer, handle, msg.get("text"), now, key)
            if kind == "dmreport":
                return self.report(peer, handle, msg.get("id"), msg.get("why"), now, key)
            if not self.reads.take(key, now):
                return self.refuse(peer, handle, "rate")
            person = chat.follows.by_handle(who.account_id, handle)
            if person is None:
                return self.refuse(peer, handle, "who")
            if kind == "dmopen":
                return self.open(peer, person, handle)
            if kind == "dmread":
                return self.read(who.account_id, person)
            return self.delete(who.account_id, person)
        except StoreError as e:
            if chat.on_store_error:
                chat.on_store_error(e)
            return self.refuse(peer, handle, "busy")

    def send(self, peer, handle, raw, now, key):
        chat, who, limits = self.chat, peer.identity, self.limits
        me = who.account_id
        text, why = textrules.clean_line(raw)
        if why is None and len(text) > limits.dm_chars:
            why = "long"
        if why:
            return self.refuse(peer, handle, why)
        if chat.store.is_banned(me):
            return self.refuse(peer, handle, "banned")
        if chat.store.muted_until(me):
            return self.refuse(peer, handle, "muted")
        if who.young:
            return self.refuse(peer, handle, "young")
        person = chat.follows.by_handle(me, handle)
        mine = chat.ref(me)
        if person is None or person.ref == mine:
            return self.refuse(peer, handle, "who")
        if self.full():
            return self.refuse(peer, handle, "full")
        new = not chat.store.dm_thread(mine, person.ref, 1)
        why = self.check(key, person.ref, text, new, now)
        if why:
            return self.refuse(peer, handle, why)
        blocked = chat.commands.ignores_ref(person.ref, mine, me)
        try:
            kept = chat.store.dm_add(mine, person.ref, who.name, person.name, text, hide=blocked, keep=limits.dm_thread_max)
        except StoreError:
            self.refund(key, person.ref, text, new)
            raise
        self.used = (self.used or 0) + len(text.encode("utf-8")) + len(who.name.encode("utf-8")) + len(person.name.encode("utf-8")) + OVERHEAD
        self.deliver(me, who.name, person, kept, blocked)

    def deliver(self, me, name, person, kept, blocked):
        """The message went into the store: the sender's pages are told (as for any message), and so are the receiver's, unless the receiver blocked the sender (nothing then goes to them, and nothing differs for the sender)."""
        chat = self.chat
        mine = chat.ref(me)
        followed, _ = self.flags(me, person.ref)
        frame = {"t": "msg", "h": chat.follows.teach(me, person.ref, person.name), "name": person.name, "id": kept.id, "at": kept.at, "text": kept.text, "me": 1, "u": 0, "f": 1 if followed else 0}
        data = self.dumps(frame)
        for peer in chat.presence.sockets(me):
            peer.send_text(data)
        self.last.pop(me, None)
        if blocked or person.account is None or not chat.presence.sockets(person.account):
            return
        receiver = person.account
        unread = chat.store.dm_unread(person.ref, mine)
        theirs, _ = self.flags(receiver, mine)
        data = self.dumps({"t": "msg", "h": chat.follows.teach(receiver, mine, name), "name": name, "id": kept.id, "at": kept.at, "text": kept.text, "me": 0, "u": unread, "f": 1 if theirs else 0})
        for peer in chat.presence.sockets(receiver):
            peer.send_text(data)
        self.last.pop(receiver, None)
        alert = chat.people.add(receiver, "msg", {"from": name, "fr": mine}, from_account=me, replace=True)
        if alert is not None:
            chat.people.deliver(receiver, alert)
            chat.people.send_lists(receiver)

    def open(self, peer, person, handle):
        chat, me = self.chat, peer.identity.account_id
        mine = chat.ref(me)
        rows = chat.store.dm_thread(mine, person.ref, self.limits.dm_shown)
        followed, blocked = self.flags(me, person.ref)
        frame = {"t": "thread", "h": handle, "name": person.name, "f": 1 if followed else 0, "rows": [dict({"id": m.id, "at": m.at, "text": m.text}, **({"me": 1} if m.frm == mine else {})) for m in rows]}
        if blocked:
            frame["b"] = 1
        peer.send_text(self.dumps(frame))
        self.read(me, person)

    def read(self, account, person):
        """What the person sent to the account is read: the conversation's row in the bell goes, and every page is sent the list."""
        chat = self.chat
        mine = chat.ref(account)
        changed = chat.store.dm_read(mine, person.ref)
        dropped = chat.store.drop_alerts_from(mine, person.ref, "msg")
        if changed:
            self.last.pop(account, None)
        self.send_lists(account)
        if dropped:
            chat.people.send_lists(account)

    def delete(self, account, person):
        chat = self.chat
        mine = chat.ref(account)
        chat.store.dm_delete(mine, person.ref)
        self.used = None
        dropped = chat.store.drop_alerts_from(mine, person.ref, "msg")
        self.last.pop(account, None)
        self.send_lists(account)
        if dropped:
            chat.people.send_lists(account)

    def report(self, peer, handle, number, why, now, key):
        chat, me = self.chat, peer.identity.account_id
        if type(number) is not int or not 0 < number < 2 ** 62 or why not in REPORT_REASONS:
            return chat.mistake(peer, "bad")
        person = chat.follows.by_handle(me, handle)
        mine = chat.ref(me)
        if person is None or chat.store.dm_get(mine, person.ref, number) is None:
            return self.refuse(peer, handle, "gone" if person is not None else "who")
        if not self.reports.take(key, now):
            return self.refuse(peer, handle, "rate")
        done = chat.store.report_add(mine, person.ref, peer.identity.name, person.name, why, number, self.limits.report_context)
        if done is None:
            return self.refuse(peer, handle, "gone")
        self.used = None
        peer.send_text(self.dumps({"t": "dmreported", "h": handle, "id": number}))

    # ---- the commands, and the account that forgot itself --------------------------------------------------------------------------------------------------------------------------

    def open_command(self, peer, word):
        """`/msg WORD` (hub/commands.py): the answer is the private `reply` `open` {h, name} with the handle that the page opens the conversation by; the same refusals as /follow (`usage`, `nobody`, `ambiguous`, `self`)."""
        chat, me = self.chat, peer.identity.account_id
        say = lambda code, **fields: chat.commands.reply(peer, "msg", code, **fields)
        person, refusal = chat.follows.resolve(me, word)
        if person is None:
            return say(refusal)
        if person.ref == chat.ref(me):
            return say("self")
        say("open", h=chat.follows.teach(me, person.ref, person.name), to=person.name)

    def forget(self, account):
        """The account forgot itself (hub/login.py): its messages are out of the store, for the other people of them as well; the pages that are open are sent their lists again."""
        self.sends.pop(self.chat.key_of(account), None)
        self.last = {}
        self.used = None
        self.refresh_all()
