"""The chat itself: who may say what, the last lines, moderation. No sockets here: a peer is any object with `identity`, `send_text(bytes)`, `close(code, reason)` and `strike()`
(hub/server.py has the real one), so the tests drive this with plain fakes. `charge()` is a refusal that is not counted as a mistake: it costs the socket a frame more.

Messages (JSON, "v":1; docs/HUB.md has the table):
  up    {"t":"say","text":"..."}   {"t":"ping"}   {"t":"who"}   {"t":"here",...}      (the last two: hub/presence.py)
        {"t":"pics","on":true|false}                 the switch of the account's Discord picture (a login is needed; a hub without the Discord login does not know it); switching off deletes the copy at once,
                                                     switching on shows nothing until the next login; the answer is the whole `you` again (the account's every socket; for a request that changes nothing, the asking socket alone), or `err pics` (not a boolean, past the limit, blocked)
        {"t":"pick","pick":"display"|"username"}     which of its two names the account is shown by (only an account that has a display name as well may); stored, then every socket of
                                                     the account is closed (the same call as a logout's: end_logins), and the page connects again and reads the new name in the welcome
  down  {"t":"welcome",...}  {"t":"line",...}  {"t":"del","id":n}  {"t":"online","n":n}  {"t":"who",...}  {"t":"err","code":"login|rate|quota|muted|banned|long|empty|young|bad|here|pick|pics|power|role|who"}
        {"t":"you","you":{...}}                      the whole `you` again, to every socket of an account whose role changed (hub/roles.py: who is a moderator or an administrator) or whose picture
                                                     arrived, went, was switched or blocked (hub/pictures.py `sync`)
        {"t":"ok","c":"mute|unmute|del|kick|ban|unban"}   the private answer to a moderator's command that was done
        {"t":"bans","rows":[{"u","at"}]}             the private answer to /bans (a username, never an id; the newest 50)
        {"t":"bye","why":"kicked|banned"}            to every socket of a person who was kicked or banned, just before the hub closes it
A moderator's commands (docs/HUB.md, "Moderation") are /mute username minutes, /unmute username, /del id, /kick username, /ban username, /unban username and /bans, in any case. Who may do what to whom is
one table (`allowed`); before an action the role of the one who acts and of the one acted on must have been answered by Discord within the last minute (hub/roles.py `fresh`, `confirm`), else an
urgent lookup is waited for, and one that fails refuses the action (`err role`). Every action that is done is written to the moderators' log by a pseudonymous reference of the account (hub/pseudo.py),
never the id; a kick or a ban is told to everybody as a line of the chat that names the person ("<name> was kicked.": a line of the account "" and the name `system`, kept like any other line).
A name is not unique (Discord display names are not), so nothing that a moderator or a page does with a person goes by the name that is shown: a line and `you` carry `k`, a short key of the
account that is made new with every start, and a moderator's `/mute` takes the username, the one name that is unique. The username of an author is sent only to moderators (`u` in a line,
and only where it is not the name shown); the frame of everybody else never holds it.
An `err` and every message that cannot be read count as a mistake of the socket (peer.strike()): twenty in a minute close it. A line is kept only if the account's own limit, the limit of
all accounts together and the size of the database allow it (`rate` otherwise; `quota` when the account has said its lines for the hour or for the day); a moderator's command is never refused for those.
"""
import asyncio
import collections
import hashlib
import hmac
import json
import os
import re

from . import PROTOCOL, presence as presencemod, pseudo, text as textrules
from .limits import AccountLimits, RateByAccount, TokenBucket
from .store import PICKS, StoreError
from .store.base import SHOW_OFF, SHOW_ON

NUMBER = re.compile(r"[0-9]{1,9}\Z")
MAX_CHAT_MUTE_MINUTES = 10080               # a moderator's /mute: a week at the most (the command line has no such bound)
COMMANDS = frozenset(("/mute", "/unmute", "/del", "/kick", "/ban", "/unban", "/bans"))     # the words of a moderator's commands: from an account with no role they are refused and never become a line of chat
RANK = {"none": 0, "mod": 1, "admin": 2}
NEEDS = {"mute": 1, "unmute": 1, "del": 1, "kick": 1, "ban": 2, "unban": 2, "bans": 2}       # the rank that a command needs
SYSTEM = "system"                           # who a line of the hub's own is from: a reserved name (hub/text.py), which no account can have
BANS_SHOWN = 50                             # the newest bans that /bans lists
PICS_REFUSED = "pics"                       # the code of the `err` for a `pics` that is not done
PICK_REFUSED = "pick"                       # the code of the `err` for a `pick` that is not done (a page that reads the codes of its lines must not take it for one)


def dumps(obj):
    return json.dumps(obj, ensure_ascii=False, separators=(",", ":")).encode("utf-8")


def allowed(command, actor, target, same):
    """THE table of the powers: may somebody of role `actor` (`none`, `mod`, `admin`) do `command` to somebody of role `target` (None: the command has no target), `same` when that is themselves? The
    rank of the actor must be the command's; nobody acts on themselves (but anybody with a role may delete a line of their own: that is no power over a person); and the target's rank must be below
    the actor's, so a moderator has no power over a moderator or an administrator, an administrator none over an administrator (and so none over the owner account, which is always one)."""
    if RANK[actor] < NEEDS[command]:
        return False
    if target is None:
        return True
    if same:
        return command == "del"
    return RANK[target] < RANK[actor]


class Chat:
    def __init__(self, store, roles, limits, clock, login):
        self.store = store
        self.roles = roles                          # who is a moderator or an administrator (hub/roles.py): role_of(account) is `none`, `mod` or `admin`
        roles.connected = self.accounts
        roles.changed = self.role_changed
        self.limits = limits
        self.clock = clock
        self.login = login
        self.account_limits = AccountLimits(limits)
        self.global_limit = TokenBucket(limits.global_burst, limits.global_refill_seconds, clock())      # all accounts together: what the database is asked to keep
        self.wait_limits = RateByAccount(limits.roles_urgent_actor_burst, limits.roles_urgent_actor_refill_seconds)      # the commands of a moderator that wait for lookups, across all its sockets
        self.named_limits = RateByAccount(limits.named_lines_burst, limits.named_lines_refill_seconds)               # the kicks and the bans of a moderator: each one is a line for everybody
        self.pick_limits = RateByAccount(limits.pick_burst, limits.pick_refill_seconds)               # an account's switches between its two names, across all its tabs
        self.pics_limits = RateByAccount(limits.pics_burst, limits.pics_refill_seconds)               # an account's switches of its picture, across all its tabs (keyed by the run's key of the account)
        self.pictures = None                        # hub/pictures.py Pictures on a hub that has the Discord login (the login sets it): none, and no picture field anywhere, on a hub without it
        self.salt = os.urandom(16)                  # the key of an account (key_of) is made with this: it changes with every start, so it names nobody for longer than the hub runs
        self.peers = presencemod.Peers()
        self.presence = presencemod.Presence(self, limits, clock, dumps)       # (who is here: the list of the front page)
        self.on_store_error = None                  # called with the error when the database cannot be used for a message (the hub logs it, not more than once in a while)
        self.end_logins = None                      # end_logins(account=...): the hub's call that closes every socket of an account (a logout's and a forget's too); the hub sets it
        self.on_command_error = None                # called with the error when a command that waited for a lookup failed for a bug (the hub logs its class)
        self.busy = {}                              # peer -> the task of its command that waits for a lookup of roles (one at a time for a socket)
        self.secret = None                          # the key of the pseudonymous references (store.secret(), read at the first use)
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

    def is_mod(self, account):
        """Whether the account may moderate: it holds the moderator's or the administrator's role (hub/roles.py)."""
        return self.roles.role_of(account) != "none"

    def accounts(self):
        """The accounts that have a socket open (the roles ask only about these)."""
        return {p.identity.account_id for p in self.peers if p.identity is not None and not getattr(p, "closed", False)}

    def role_changed(self, account):
        """The role of an account is not what its pages were told: every socket of it gets the whole `you` again."""
        for peer in list(self.peers):
            who = peer.identity
            if who is not None and who.account_id == account and not getattr(peer, "closed", False):
                peer.send_text(dumps({"t": "you", "you": self.you_of(who)}))

    def picture_changed(self, refs):
        """The pictures of the accounts with these references arrived, went or were switched (hub/pictures.py `sync`): every socket of each gets the whole `you` again."""
        for peer in list(self.peers):
            who = peer.identity
            if who is not None and not getattr(peer, "closed", False) and self.ref(who.account_id) in refs:
                peer.send_text(dumps({"t": "you", "you": self.you_of(who)}))

    def sync_pictures(self):
        """The pictures are read again after a kick or a ban; a database that refuses leaves it to the clock's look a tick later: the act is done, and its public line and its answer must not depend on a look."""
        if self.pictures is not None:
            try:
                self.pictures.sync()
            except StoreError as e:
                if self.on_store_error:
                    self.on_store_error(e)

    def you_of(self, who):
        """What a page is told of its own account (the welcome's `you`, and /hub/me): the name that is shown, `role` (`none`, `mod` or `admin`; from Discord, never from a page), `mod` (true for
        both roles, for pages that read only that), the key of its lines `k`; and, only where the account has a display name as well and so can choose, `pick` (the name that is shown: "display"
        or "username") and `other` (the name that it is not shown by). Nothing else."""
        role = self.roles.role_of(who.account_id)
        you = {"name": who.name, "mod": role != "none", "role": role, "k": self.key_of(who.account_id)}
        if who.other:
            you["pick"], you["other"] = who.pick, who.other
        if self.pictures is not None:
            ref = self.ref(who.account_id)
            you["pics"] = self.pictures.state_of(ref)
            name = self.pictures.name_of(ref)
            if name:
                you["pic"] = name
        return you

    def welcome(self, peer):
        you = peer.identity
        if you is not None:
            self.roles.want(you.account_id)                       # (its role is asked at the front of the queue; the welcome says the last answer, and a `you` frame follows a new one)
        lines = self.recent()
        names = self.usernames([l.account for l in lines]) if you is not None and self.is_mod(you.account_id) else None        # (only a moderator's welcome has the usernames)
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
        out = {"id": line.id, "at": line.at, "who": line.name, "text": line.text, "mod": bool(line.mod), "k": self.key_of(line.account) if line.account else ""}
        if line.mod:
            out["role"] = "admin" if line.mod >= RANK["admin"] else "mod"      # (what the author was when it wrote the line: a mark for the pages and never a reason to allow anything)
        if not line.account:
            out["sys"] = True                      # (a line of the hub's own: nobody's, so it has no key)
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
        elif kind == "pics" and self.pictures is not None:
            self.on_pics(peer, msg)
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

    def ref(self, account):
        """The pseudonymous reference of an account (hub/pseudo.py): what is written about a person where the Discord id must not be."""
        if self.secret is None:
            self.secret = self.store.secret()
        return pseudo.reference(self.secret, account)

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
        mod = self.is_mod(who.account_id)
        role = self.roles.role_of(who.account_id)
        if mod and line.startswith("/"):
            try:
                return self.command(peer, line)
            except StoreError as e:
                return self.trouble(peer, e, "bad")                # (a moderator is told that the command did not happen, and may type it again)
        if line.split(" ")[0].lower() in COMMANDS:
            return self.mistake(peer, "bad")                       # (a command from an account that holds no role, or whose role is not known yet, is not posted for everybody to read)
        now = self.clock()
        try:
            full = self.store.size() >= self.limits.db_max_bytes
        except StoreError as e:
            return self.trouble(peer, e, "rate")
        if not self.global_limit.ready(now) or full:
            return self.mistake(peer, "rate", count=False)         # (the hub is busy or full: a client that tries again is not misbehaving)
        why = self.account_limits.check(who.account_id, line, now)         # (this check is the last: it is the one that counts the line as said)
        if why == "quota":
            return self.mistake(peer, "quota", count=False)        # (the hour's or the day's lines are said: not a mistake, and the page tells it calmly)
        if why:
            return self.mistake(peer, "rate")
        self.global_limit.take(now)
        try:
            entry = self.store.add_line(who.account_id, who.name, line, max(RANK[role], 1 if mod else 0))       # (the role that the author has now: what the line will say of it)
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
            peer.send_text(moderators if who is not None and self.is_mod(who.account_id) else everybody)

    def on_pick(self, peer, msg):
        """Which of its two names the account is shown by. Refused (a mistake of the socket) with `login` for a guest and with `pick` for anything but one of the two words, past the account's
        limit, and for an account that has no display name. Done: stored, and every socket of the account is closed with the call that a logout uses, so that the pages connect again and read the new name in the
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

    def on_pics(self, peer, msg):
        """The switch of the account's Discord picture. Refused (a mistake of the socket) with `login` for a guest and with `pics` for anything but a true or a false, past the account's limit, and for an account
        whose picture the operator blocked (a block is not the person's to lift). Done: the copy is deleted first (off) or the switch is remembered (on: nothing is shown until the next login), then every socket of the account gets
        the whole `you` again; when the switch was already in the asked state nothing changed, and the asking socket alone gets the whole `you`. A look that fails ends the call with `err pics` and no `you`. Nothing else of the message is read."""
        who = peer.identity
        if who is None:
            return self.mistake(peer, "login")
        on = msg.get("on")
        if not isinstance(on, bool) or not self.pics_limits.take(self.key_of(who.account_id), self.clock()):
            return self.mistake(peer, PICS_REFUSED)
        try:
            before = self.store.picture_state(who.account_id)
            state = self.store.set_picture_show(who.account_id, SHOW_ON if on else SHOW_OFF)
        except StoreError as e:
            return self.trouble(peer, e, PICS_REFUSED)
        if state not in (SHOW_ON, SHOW_OFF):
            return self.mistake(peer, PICS_REFUSED)
        try:
            self.pictures.sync()
        except StoreError as e:
            return self.trouble(peer, e, PICS_REFUSED)
        if state == before:                                                     # (nothing changed: no frame goes to the account, so the asking socket is answered all the same, and a page's box never waits for nothing)
            peer.send_text(dumps({"t": "you", "you": self.you_of(who)}))

    @staticmethod
    def parse(line):
        """(command, who or line id, minutes) of a moderator's command, or None when it is not one that is written right: single spaces, a username (or a line's number), and for /mute minutes 1 to 10080."""
        words = line.split(" ")
        name = words[0].lower()[1:]
        if name == "mute" and len(words) == 3 and NUMBER.match(words[2]) and 1 <= int(words[2]) <= MAX_CHAT_MUTE_MINUTES:
            return name, words[1], int(words[2])
        if name in ("unmute", "kick", "ban", "unban") and len(words) == 2:
            return name, words[1], 0
        if name == "del" and len(words) == 2 and NUMBER.match(words[1]):
            return name, int(words[1]), 0
        if name == "bans" and len(words) == 1:
            return name, None, 0
        return None

    def command(self, peer, line):
        """A moderator's command (see the top of this file). Done at once when the roles of the accounts that it involves were all answered by Discord within the last minute; otherwise it waits (a task
        of its own, one at a time for a socket; a second command meanwhile is `rate`) for an urgent lookup of them, and is refused `role` if that does not give them."""
        job = self.parse(line)
        if job is None:
            return self.mistake(peer, "bad")
        if peer in self.busy:
            return self.mistake(peer, "rate", count=False)
        accounts, _, refusal = self.aim(peer.identity, job)
        if refusal:
            return self.mistake(peer, refusal)
        if self.roles.fresh(accounts):
            return self.carry_out(peer, job)
        if not self.wait_limits.take(self.key_of(peer.identity.account_id), self.clock()):
            return self.mistake(peer, "rate")                       # (a moderator who asks for too many lookups at once: refused here, before the hub's share is touched, and counted as a mistake; the buckets are keyed by the run's key of the account, not the id)
        task = asyncio.ensure_future(self.wait_and_carry_out(peer, job, accounts))
        self.busy[peer] = task
        task.add_done_callback(lambda done, peer=peer: self.finished(peer, done))

    def finished(self, peer, task):
        """The task of a command that waited is over: the socket may send another; a bug in it is reported by its class (and the task's error is read, so that nothing prints its text)."""
        self.busy.pop(peer, None)
        if not task.cancelled() and task.exception() is not None and self.on_command_error:
            self.on_command_error(task.exception())

    async def wait_and_carry_out(self, peer, job, accounts):
        try:
            await self.roles.confirm(accounts)                      # (what it says is read again, in carry_out, from the answers themselves: they are fresh, or the command is refused `role`)
            if getattr(peer, "closed", None):
                return                                              # (the socket went while it waited: nothing is written to it, nothing is done)
            who = peer.identity.account_id
            if self.store.is_banned(who):
                return self.mistake(peer, "banned")
            if self.store.muted_until(who):
                return self.mistake(peer, "muted")
            self.carry_out(peer, job)
        except StoreError as e:
            self.trouble(peer, e, "bad")

    def aim(self, me, job):
        """(the accounts whose roles the command needs, the account it is done to or None, the code that refuses it or None): who is meant is found again every time, so that what is done is what was
        looked up. `who` for a username that is nobody's here (or two people's), and for a /kick of a person who is not here; `bad` for a line that is not there; /del of one's own line needs nobody's role
        but the one who acts."""
        name, arg, _ = job
        if name == "bans":
            return [me.account_id], None, None
        if name == "del":
            target = self.store.account_of_line(arg)
            if target is None:
                return None, None, "bad"
            if target == "":
                return None, None, "power"                          # (a line of the hub's own is nobody's to delete from the chat)
        else:
            target = self.account_named(arg) or (self.ban_named(arg) if name == "unban" else None)
            if target is None or (name == "kick" and not self.sockets_of(target)):
                return None, None, "who"
        return ([me.account_id] if target == me.account_id and name == "del" else [me.account_id, target]), target, None

    def carry_out(self, peer, job):
        """Do the command, now: the accounts' roles are fresh (else `role`), the table of the powers says yes (else `power`), and what is asked is true of the person (else `bad`: not muted, not banned, already
        banned). Nothing here waits, so what was checked is what is done."""
        me = peer.identity
        name, arg, minutes = job
        accounts, target, refusal = self.aim(me, job)
        if refusal:
            return self.mistake(peer, refusal)
        if not self.roles.fresh(accounts):
            return self.mistake(peer, "role", count=False)
        actor = self.roles.role_of(me.account_id)
        if actor == "none":
            return self.mistake(peer, "role", count=False)         # (no longer a moderator: the answer is fresh, and it says none)
        if not allowed(name, actor, self.roles.role_of(target) if target is not None else None, target == me.account_id):
            return self.mistake(peer, "power")
        by, whom = self.ref(me.account_id), self.ref(target) if target is not None else ""
        if name == "mute":
            self.store.mute(target, minutes, by, arg, note=(by, "mute", whom, str(minutes)))
        elif name == "unmute":
            if not self.store.muted_until(target):
                return self.mistake(peer, "bad")
            self.store.mute(target, 0, by, "", note=(by, "unmute", whom, ""))
        elif name == "del":
            if not self.store.delete_line(arg, by, note=None if target == me.account_id else (by, "del", whom, str(arg))):
                return self.mistake(peer, "bad")
            self.sync_deletions()
        elif name == "bans":
            rows = [{"u": who, "at": at} for _, who, at, _ in reversed(self.store.bans())][:BANS_SHOWN]
            return peer.send_text(dumps({"t": "bans", "rows": rows}))
        elif name == "unban":
            if not self.store.unban(target, note=(by, "unban", whom, "")):
                return self.mistake(peer, "bad")
        elif name == "kick":
            sockets = self.sockets_of(target)
            shown = sockets[0].identity.name
            self.store.end_sessions(target, note=(by, "kick", whom, ""))
            self.remove(sockets, "kicked")
            self.sync_pictures()
            self.announce("%s was kicked." % shown, me)
        else:
            if self.store.is_banned(target):
                return self.mistake(peer, "bad")
            shown = self.shown_name(target) or arg
            self.store.ban(target, by, arg, note=(by, "ban", whom, ""))
            self.remove(self.sockets_of(target), "banned")
            self.sync_pictures()
            self.announce("%s was banned." % shown, me)
        peer.send_text(dumps({"t": "ok", "c": name}))

    def sockets_of(self, account):
        return [p for p in self.peers if p.identity is not None and p.identity.account_id == account and not getattr(p, "closed", False)]

    def remove(self, sockets, why):
        """Tell every one of the sockets of a person who was kicked or banned (`bye`), and close them (a clean close, with the word as its reason)."""
        for peer in sockets:
            peer.send_text(dumps({"t": "bye", "why": why}))
            peer.close(1000, why)

    def shown_name(self, account):
        """The name that the account is shown by: its open socket's, else its profile's (the display name where that is the pick), else None."""
        here = self.sockets_of(account)
        if here:
            return here[0].identity.name
        profile = self.store.profile(account)
        if profile is None:
            return None
        return profile.display if profile.pick == "display" and profile.display else profile.username

    def announce(self, text, by):
        """A line of the hub's own to everybody (and into the history, like any line: the same time to live, the same count among the newest 200): the account is "" and the name `system`, a name that no
        account can have. A database that refuses it costs the line, not the action, which was done already; so does a moderator `by` whom too many were made (a burst of `named_lines_burst`, then
        one every `named_lines_refill_seconds`): the kick or the ban is done, its `ok` and its row of the log are there, and the line is not made, so that the history is not pushed out."""
        if not self.named_limits.take(self.key_of(by.account_id), self.clock()):
            return
        try:
            entry = self.store.add_line("", SYSTEM, text, False)
        except StoreError as e:
            if self.on_store_error:
                self.on_store_error(e)
            return
        self.ring.append(entry)
        self.broadcast(dict({"t": "line"}, **self.line_of(entry)))

    def ban_named(self, username):
        """The account that is banned under this username (the name that the ban was made with), when exactly one is: a ban outlives the profile that a login made, so a ban can be lifted by the name that /bans shows."""
        if not textrules.valid_name(username):
            return None
        found = [account for account, name, _, _ in self.store.bans() if name == username]
        return found[0] if len(found) == 1 else None

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
