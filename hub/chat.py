"""The chat itself: who may say what, the last lines, moderation. No sockets here: a peer is any object with `identity`, `send_text(bytes)`, `close(code, reason)` and `strike()`
(hub/server.py has the real one), so the tests drive this with plain fakes. `charge()` is a refusal that is not counted as a mistake: it costs the socket a frame more.

Messages (JSON, "v":1; docs/HUB.md has the table):
  up    {"t":"say","text":"..."}   {"t":"ping"}   {"t":"who"}   {"t":"here",...}      (the last two: hub/presence.py)
        {"t":"pick","pick":"display"|"username"}     which of its two names the account is shown by (only an account that has a display name as well may); stored, then every socket of
                                                     the account is closed (the same call as a logout's: end_logins), and the page connects again and reads the new name in the welcome
  down  {"t":"welcome",...}  {"t":"line",...}  {"t":"del","id":n}  {"t":"online","n":n}  {"t":"who",...}  {"t":"err","code":"login|rate|muted|banned|long|empty|young|bad|pick"}
A name is not unique (Discord display names are not), so nothing that a moderator or a page does with a person goes by the name that is shown: a line and `you` carry `k`, a short key of the
account that is made new with every start, and a moderator's `/mute` takes the username, the one name that is unique. The username of an author is sent only to moderators (`u` in a line,
and only where it is not the name shown); the frame of everybody else never holds it.
An `err` and every message that cannot be read count as a mistake of the socket (peer.strike()): twenty in a minute close it. A line is kept only if the account's own limit, the limit of
all accounts together and the size of the database allow it (`rate` otherwise); a moderator's command is never refused for those.
"""
import collections
import hashlib
import hmac
import json
import os
import re

from . import PROTOCOL, presence as presencemod, text as textrules
from .limits import AccountLimits, RateByAccount, TokenBucket
from .store import PICKS, StoreError

NUMBER = re.compile(r"[0-9]{1,9}\Z")
MAX_CHAT_MUTE_MINUTES = 10080               # a moderator's /mute: a week at the most (the command line has no such bound)
PICK_REFUSED = "pick"                       # the code of the `err` for a `pick` that is not done (a page that reads the codes of its lines must not take it for one)


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
        self.pick_limits = RateByAccount(limits.pick_burst, limits.pick_refill_seconds)               # an account's switches between its two names, across all its tabs
        self.salt = os.urandom(16)                  # the key of an account (key_of) is made with this: it changes with every start, so it names nobody for longer than the hub runs
        self.peers = presencemod.Peers()
        self.presence = presencemod.Presence(self, limits, clock, dumps)       # (who is here: the list of the front page)
        self.on_store_error = None                  # called with the error when the database cannot be used for a message (the hub logs it, not more than once in a while)
        self.end_logins = None                      # end_logins(account=...): the hub's call that closes every socket of an account (a logout's and a forget's too); the hub sets it
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

    def key_of(self, account):
        """A short key of an account for the pages of this run of the hub (a keyed hash: it is made new with every start): a line is its author's by this key and not by its name, which
        is not unique. It says nothing of the account to anybody who does not have the id."""
        return hmac.new(self.salt, account.encode("utf-8"), hashlib.sha256).hexdigest()[:12]

    def you_of(self, who):
        """What a page is told of its own account (the welcome's `you`, and /hub/me): the name that is shown, `mod`, the key of its lines `k`; and, only where the account has a display name as
        well and so can choose, `pick` (the name that is shown: "display" or "username") and `other` (the name that it is not shown by). Nothing else."""
        you = {"name": who.name, "mod": who.account_id in self.mods, "k": self.key_of(who.account_id)}
        if who.other:
            you["pick"], you["other"] = who.pick, who.other
        return you

    def welcome(self, peer):
        you = peer.identity
        lines = self.recent()
        names = self.usernames([l.account for l in lines]) if you is not None and you.account_id in self.mods else None        # (only a moderator's welcome has the usernames)
        welcome = {"t": "welcome", "v": PROTOCOL, "you": self.you_of(you) if you else None, "login": bool(self.login), "presence": True,
                   "online": self.presence.people(), "history": [self.line_of(l, names) for l in lines]}
        if you:
            welcome["me"] = self.presence.row_id(you.account_id)       # (the number of the account's own row in the list: the page leaves that row out of its groups)
        return welcome

    def usernames(self, accounts):
        """account -> username for those of `accounts` whose username is known, for a moderator's eyes: a socket of the account that is open now says it, else the account's profile (an account
        with no profile, a purged one, has none: its lines go without)."""
        here = {p.identity.account_id: p.identity.username for p in self.peers if p.identity is not None and not getattr(p, "closed", False)}
        found = {}
        for account in set(accounts):
            name = here.get(account)
            if name is None:
                try:
                    profile = self.store.profile(account)
                except StoreError:
                    profile = None
                name = profile.username if profile else None
            if name:
                found[account] = name
        return found

    def line_of(self, line, names=None):
        """A line as it goes to a page: `k` is its author's key (the page tells its own lines by it, since names are not unique). `names` (account -> username) is given only for a moderator's
        copy: it adds `u`, the author's username, where that is not the name that the line shows."""
        out = {"id": line.id, "at": line.at, "who": line.name, "text": line.text, "mod": bool(line.mod), "k": self.key_of(line.account)}
        username = names.get(line.account) if names else None
        if username and username != line.name:
            out["u"] = username
        return out

    def broadcast(self, obj):
        data = dumps(obj)
        for peer in list(self.peers):
            peer.send_text(data)

    def online(self):
        self.broadcast({"t": "online", "n": self.presence.people()})

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
            peer.send_text(dumps({"t": "online", "n": self.presence.people()}))      # the answer to a ping is the online count: the page learns that the hub is alive
        elif kind == "say":
            self.on_say(peer, msg.get("text"))
        elif kind == "who":
            self.presence.on_who(peer)
        elif kind == "here":
            self.presence.on_here(peer, msg)
        elif kind == "pick":
            self.on_pick(peer, msg)
        else:
            self.mistake(peer, "bad")

    @staticmethod
    def refuse_constant(name):
        raise ValueError(name)

    def mistake(self, peer, code, count=True):
        """Tell the peer what was wrong and count it; the twentieth within a minute closes the socket. (`count=False` is for a refusal that is not the client's doing: it is not a mistake, but
        it is not free either: it costs the socket a frame more of its budget of frames, so a client that keeps asking runs out of it twice as fast.)"""
        peer.send_text(dumps({"t": "err", "code": code}))
        if count:
            peer.strike()
        else:
            peer.charge()

    def trouble(self, peer, error, code):
        """The database could not be used for this message (another process holds its write lock longer than the hub waits, the disk is full or refuses): the peer is told `code`, which is
        not its doing and no mistake, and the socket stays."""
        if self.on_store_error:
            self.on_store_error(error)
        self.mistake(peer, code, count=False)

    def on_say(self, peer, raw):
        who = peer.identity
        if who is None:
            return self.mistake(peer, "login")
        try:
            if self.store.is_banned(who.account_id):
                return self.mistake(peer, "banned")
            if self.store.muted_until(who.account_id):
                return self.mistake(peer, "muted")
        except StoreError as e:
            return self.trouble(peer, e, "rate")
        if who.young:
            return self.mistake(peer, "young")
        line, why = textrules.clean_line(raw)
        if why:
            return self.mistake(peer, why)
        mod = who.account_id in self.mods
        if mod and line.startswith("/"):
            try:
                return self.command(peer, line)
            except StoreError as e:
                return self.trouble(peer, e, "bad")                # (a moderator is told that the command did not happen, and may type it again)
        now = self.clock()
        try:
            full = self.store.size() >= self.limits.db_max_bytes
        except StoreError as e:
            return self.trouble(peer, e, "rate")
        if not self.global_limit.ready(now) or full:
            return self.mistake(peer, "rate", count=False)         # (the hub is busy or full: a client that tries again is not misbehaving)
        if self.account_limits.check(who.account_id, line, now):
            return self.mistake(peer, "rate")                      # (this check is the last: it is the one that counts the line as said)
        self.global_limit.take(now)
        try:
            entry = self.store.add_line(who.account_id, who.name, line, mod)
        except StoreError as e:
            self.account_limits.refund(who.account_id, line)       # (the line was not kept: it did not cost a token, and the same text may be tried again)
            self.global_limit.give_back()
            return self.trouble(peer, e, "rate")
        self.ring.append(entry)
        self.send_line(entry, who.username)

    def send_line(self, entry, username):
        """A new line to everybody; a moderator gets it with its author's username (`u`) where that is not the name shown, and nobody else ever gets a username."""
        frame = dict({"t": "line"}, **self.line_of(entry))
        everybody = dumps(frame)
        moderators = dumps(dict(frame, u=username)) if username and username != entry.name else everybody
        for peer in list(self.peers):
            who = peer.identity
            peer.send_text(moderators if who is not None and who.account_id in self.mods else everybody)

    def on_pick(self, peer, msg):
        """Which of its two names the account is shown by. Refused (`pick`, a mistake of the socket) for a guest's, for anything but one of the two words, past the account's limit, and for an
        account that has no display name. Done: stored, and every socket of the account is closed with the call that a logout uses, so that the pages connect again and read the new name in the
        welcome (a socket's name is fixed at its handshake, as its identity is); lines that were written keep the name they were written with."""
        who = peer.identity
        if who is None:
            return self.mistake(peer, "login")
        pick = msg.get("pick")
        if pick not in PICKS or not self.pick_limits.take(who.account_id, self.clock()):
            return self.mistake(peer, PICK_REFUSED)
        try:
            done = self.store.set_pick(who.account_id, pick)
        except StoreError as e:
            return self.trouble(peer, e, PICK_REFUSED)
        if not done:
            return self.mistake(peer, PICK_REFUSED)
        if self.end_logins:
            self.end_logins(account=who.account_id)

    def command(self, peer, line):
        """A moderator's /mute username minutes, /del id."""
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

    def account_named(self, username):
        """The account that a moderator means by a USERNAME (never by the name that is shown: those are not unique, and two accounts can show the same one): the one that is here with it; of two
        that are here with it (a rename can make two) none, because it is not known which; when nobody is here with it, the one account whose profile has it, and none when several do."""
        if not textrules.valid_name(username):
            return None
        here = {p.identity.account_id for p in self.peers if p.identity is not None and p.identity.username == username}
        if len(here) > 1:
            return None
        return next(iter(here)) if here else self.store.account_of_username(username)

    def drop_lines(self, ids):
        """Take lines off every screen at once (the lines of an account that forgot itself, the lines that were deleted from the command line). The number of frames is bounded, whatever the number
        of lines: a page keeps 100 lines at most and the id of a line only ever grows, so the lines that can be on a screen are the newest 100 of the deleted ones, and the older ones need no frame.
        (One frame for each of 300,000 lines, to each of 500 sockets, is a loop that holds the hub's one thread for minutes.)"""
        gone = set(ids)
        for line in [l for l in self.ring if l.id in gone]:
            self.ring.remove(line)
        for line_id in sorted(gone)[-self.limits.deleted_announce_max:]:
            self.broadcast({"t": "del", "id": line_id})

    # ---- the clock's work -----------------------------------------------------------------------------------------------------------------

    def sync_deletions(self):
        """Tell everybody about the lines that were deleted since the last look (by a /del here, or by the command line in another process): a bounded number of frames, see drop_lines."""
        rows = self.store.deletions_since(self.deleted_seq)
        if rows:
            self.deleted_seq = rows[-1][0]
            self.drop_lines([line_id for _, line_id in rows])
