"""The chat itself: who may say what, the last lines, moderation. No sockets here: a peer is any object with `identity`, `send_text(bytes)`, `close(code, reason)` and `strike()`
(hub/server.py has the real one), so the tests drive this with plain fakes.

Messages (JSON, "v":1; docs/HUB.md has the table):
  up    {"t":"say","text":"..."}   {"t":"ping"}
  down  {"t":"welcome",...}  {"t":"line",...}  {"t":"del","id":n}  {"t":"online","n":n}  {"t":"err","code":"login|rate|muted|banned|long|empty|young|bad"}
An `err` and every message that cannot be read count as a mistake of the socket (peer.strike()): twenty in a minute close it. A line is kept only if the account's own limit, the limit of
all accounts together and the size of the database allow it (`rate` otherwise); a moderator's command is never refused for those.
"""
import collections
import json
import re

from . import PROTOCOL, text as textrules
from .limits import AccountLimits, TokenBucket

NUMBER = re.compile(r"[0-9]{1,9}\Z")
MAX_CHAT_MUTE_MINUTES = 10080               # a moderator's /mute: a week at the most (the command line has no such bound)


def dumps(obj):
    return json.dumps(obj, ensure_ascii=False, separators=(",", ":")).encode("utf-8")


class Chat:
    def __init__(self, store, mods, limits, clock, login):
        self.store = store
        self.mods = mods
        self.limits = limits
        self.clock = clock
        self.login = login
        self.account_limits = AccountLimits(limits)
        self.global_limit = TokenBucket(limits.global_burst, limits.global_refill_seconds, clock())      # all accounts together: what the database is asked to keep
        self.peers = set()
        self.ring = collections.deque(maxlen=limits.history_lines)
        for line in store.recent_lines(limits.history_lines):
            self.ring.append(line)
        self.deleted_seq = store.last_deletion()

    # ---- peers ----------------------------------------------------------------------------------------------------------------------------

    def recent(self):
        """The last lines, oldest first, without those that are past their time (they are deleted from the database; the screens of newcomers must not show them either)."""
        cutoff = int(self.store.clock()) - self.store.retention
        for line in [l for l in self.ring if l.at < cutoff]:
            self.ring.remove(line)
        return list(self.ring)

    def welcome(self, peer):
        you = peer.identity
        return {"t": "welcome", "v": PROTOCOL, "you": {"name": you.name, "mod": you.account_id in self.mods} if you else None, "login": bool(self.login), "online": len(self.peers),
                "history": [self.line_of(l) for l in self.recent()]}

    @staticmethod
    def line_of(line):
        return {"id": line.id, "at": line.at, "who": line.name, "text": line.text, "mod": bool(line.mod)}

    def broadcast(self, obj):
        data = dumps(obj)
        for peer in list(self.peers):
            peer.send_text(data)

    def online(self):
        self.broadcast({"t": "online", "n": len(self.peers)})

    # ---- messages from a peer -------------------------------------------------------------------------------------------------------------

    def on_text(self, peer, raw):
        """One text frame from a peer: JSON, an object with a "t"."""
        try:
            msg = json.loads(raw, parse_constant=self.refuse_constant)
        except (ValueError, RecursionError):
            return self.mistake(peer, "bad")
        if not isinstance(msg, dict):
            return self.mistake(peer, "bad")
        kind = msg.get("t")
        if kind == "ping":
            peer.send_text(dumps({"t": "online", "n": len(self.peers)}))      # the answer to a ping is the online count: the page learns that the hub is alive
        elif kind == "say":
            self.on_say(peer, msg.get("text"))
        else:
            self.mistake(peer, "bad")

    @staticmethod
    def refuse_constant(name):
        raise ValueError(name)

    def mistake(self, peer, code, count=True):
        """Tell the peer what was wrong and count it; the twentieth within a minute closes the socket. (`count=False` is for a refusal that is not the client's doing.)"""
        peer.send_text(dumps({"t": "err", "code": code}))
        if count:
            peer.strike()

    def on_say(self, peer, raw):
        who = peer.identity
        if who is None:
            return self.mistake(peer, "login")
        if self.store.is_banned(who.account_id):
            return self.mistake(peer, "banned")
        if self.store.muted_until(who.account_id):
            return self.mistake(peer, "muted")
        if who.young:
            return self.mistake(peer, "young")
        line, why = textrules.clean_line(raw)
        if why:
            return self.mistake(peer, why)
        mod = who.account_id in self.mods
        if mod and line.startswith("/"):
            return self.command(peer, line)
        now = self.clock()
        if not self.global_limit.ready(now) or self.store.size() >= self.limits.db_max_bytes:
            return self.mistake(peer, "rate", count=False)         # (the hub is busy or full: a client that tries again is not misbehaving)
        if self.account_limits.check(who.account_id, line, now):
            return self.mistake(peer, "rate")                      # (this check is the last: it is the one that counts the line as said)
        self.global_limit.take(now)
        entry = self.store.add_line(who.account_id, who.name, line, mod)
        self.ring.append(entry)
        self.broadcast(dict({"t": "line"}, **self.line_of(entry)))

    def command(self, peer, line):
        """A moderator's /mute name minutes, /del id."""
        words = line.split(" ")
        me = peer.identity
        if words[0] == "/mute" and len(words) == 3 and NUMBER.match(words[2]) and 1 <= int(words[2]) <= MAX_CHAT_MUTE_MINUTES:
            target = self.account_named(words[1])
            if target is None or target in self.mods:
                return self.mistake(peer, "bad")
            self.store.mute(target, int(words[2]), me.account_id, words[1])
        elif words[0] == "/del" and len(words) == 2 and NUMBER.match(words[1]):
            if not self.store.delete_line(int(words[1]), me.account_id):
                return self.mistake(peer, "bad")
            self.sync_deletions()
        else:
            self.mistake(peer, "bad")

    def account_named(self, name):
        """The account that a moderator means by a name: the one that is here with it; of two that are here with it (a name is unique among Discord's accounts, but a test login or a rename can
        make two) none, because it is not known which; when nobody is here with it, the account that last spoke under it."""
        here = {p.identity.account_id for p in self.peers if p.identity is not None and p.identity.name == name}
        if len(here) > 1:
            return None
        return next(iter(here)) if here else self.store.account_of_name(name)

    # ---- the clock's work -----------------------------------------------------------------------------------------------------------------

    def sync_deletions(self):
        """Tell everybody about the lines that were deleted since the last look (by a /del here, or by the command line in another process)."""
        for seq, line_id in self.store.deletions_since(self.deleted_seq):
            self.deleted_seq = seq
            for line in [l for l in self.ring if l.id == line_id]:
                self.ring.remove(line)
            self.broadcast({"t": "del", "id": line_id})
