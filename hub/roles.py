"""Who is a moderator or an administrator (docs/HUB.md, "Roles"): the answer comes from Discord and from nothing a client says. The site's own Discord server has two roles; the hub asks, with
the bot's token, `GET /guilds/<server>/members/<account>` (hub/discord.py `member`) and reads the `roles` of the answer: the administrator's role makes `admin` (also for a person who holds both),
the moderator's role makes `mod`, and a person who is not in the server, or holds neither, is `none`. The account in `ANTS_HUB_OWNER` is always `admin`, without a call.

All of it is memory, for the people who have a socket open and not for long after (`Roles.entries`: one small entry for each, at most `roles_entries_max`); nothing goes to the database. The
lookups are made by one task, one at a time, at about two a second (a token bucket) and by Discord's own rate-limit headers; the event loop is never held, and no call waits for longer than
`discord_seconds`. The order of the queue: a person who has just connected (the welcome says `none` or the last answer, and a live `you` frame follows), (a role holder who is 30 seconds overdue is asked before even that), then people who hold a role or lost one in the
last hour, then everybody else; each is asked again `ANTS_HUB_ROLE_RECHECK_SECONDS` after the last answer, plus or minus a tenth, so that a crowd that arrived together is spread out. The busier
it is, the later the last group is asked; a role holder is never held back for it.

Fail closed. An answer is believed for the interval, a tenth more (the latest that a check is due) and `roles_grace_seconds` more (a missed check is forgiven once); after that the person is `none` until Discord says otherwise, and a `you` frame
tells the person's pages. A Discord that is down, slow, busy or sends garbage is a failed check, never "no role" and never a role. A token, a server or an access that Discord refuses (401, 403,
404 with the code for an unknown server) stops every call for `roles_pause_seconds` with one neutral line in the log and takes everybody's role away but the owner account's; then one call tries again. Chat
itself is never held up: it asks `role_of`, which is a dictionary look and no I/O.

An action of a moderator is another matter (`fresh`, `confirm`): the role of the person who acts and of the person who is acted on must have been answered by Discord within the last
`roles_action_fresh_seconds` (60). If either answer is older, or there is none (the person is not here: a moderator may act on somebody who is offline), the action waits, up to `roles_urgent_seconds`
(3), for an urgent lookup of both (the owner account needs none), which goes before everything else in the queue (still through the same bucket, the same rate-limit holds and the same pause, and from a
share of its own, `roles_urgent_burst` and one more a second, so that a moderator who asks for many cannot use up the lookups that everybody's role holds on to); a lookup that does not give an answer
in time, for any reason, refuses the action (`role`), and no role that is merely believed is ever enough. An urgent lookup is a lookup like another: the Discord id of the person is in the request
and nowhere else.

Nothing in a log line, an error or a frame holds the token, the server's id, a role's id or an account: the log says a kind and a status. The token is read once, from the settings.
"""
import asyncio
import random

from . import config, discord as discordmod
from .limits import TokenBucket

NONE, MOD, ADMIN = "none", "mod", "admin"
MAX_ROLES = 250                                 # a member has at most this many roles in Discord; an answer with more is not Discord's
NOT_A_MEMBER = 10007                            # Discord's code for "Unknown Member": the person is not in the server (an answer, and the answer is `none`)
UNKNOWN_USER = 10013                            # ... and "Unknown User": a Discord account that was deleted; it is in no server either
UNKNOWN_GUILD = 10004
LOST_FOR = 3600.0                               # a person who lost a role is asked again like a role holder for this long (the direction that matters)
OVERDUE = 30.0                                  # a role holder who has been due this long is asked before even a new socket (a crowd that arrives must not run out a role)
MAX_WAIT = 5.0                                  # the longest the worker sleeps without looking at the queue again


class Entries(dict):
    """The entries by account (a dictionary, which a test or a debugger may print): what it prints is how many there are, never the accounts, whose Discord ids are like passwords."""

    def __repr__(self):
        return "<%d entries>" % len(self)

    __str__ = __repr__


class Entry:
    """What is known of one person who is here: the role that Discord last said, when, when to ask again, and the role that the pages were last told."""

    __slots__ = ("role", "ok_at", "due", "fails", "new", "seen", "shown", "lost_at")

    def __repr__(self):
        return "<Entry>"                        # (no field: none of them is for a log, and the dictionary that holds it is keyed by the account)

    def __init__(self, now):
        self.role = NONE
        self.ok_at = None                       # when Discord last said (None: never, or the roles were refused since)
        self.due = now                          # when to ask next
        self.fails = 0                          # checks that failed since the last answer
        self.new = True                         # just connected, or too old to rely on: asked before the rest
        self.seen = now                         # when the person last had a socket
        self.shown = NONE                       # the role that the person's pages were last told
        self.lost_at = None


class Roles:
    def __init__(self, cfg, limits, clock, log, client=None, rng=random.random):
        self.on = cfg.roles_on
        self.owner = cfg.owner
        self.guild, self.admin_id, self.mod_id = cfg.discord_guild, cfg.role_admin, cfg.role_mod
        self._token = cfg.discord_bot_token
        self.limits = limits
        self.clock = clock
        self.log = log
        self.rng = rng
        self.interval = float(cfg.role_recheck_seconds)
        self.grace = self.interval * (1.0 + limits.roles_jitter) + limits.roles_grace_seconds        # (past the latest that a check can be due: a jittered check never comes after the role ran out)
        self.client = client if client is not None else (discordmod.Discord(cfg, limits) if self.on else None)
        self.entries = Entries()                # account -> Entry, for the accounts that have a socket open (and, for one grace, those that had)
        self.connected = lambda: ()             # the accounts that have a socket open (the chat sets it)
        self.changed = lambda account: None     # called when the role that an account's pages should show changed (the chat sets it)
        self.bucket = TokenBucket(limits.roles_burst, limits.roles_refill_seconds, clock())
        self.pause_until = float("-inf")        # no call before this (a refused token, server or access)
        self.hold_until = float("-inf")         # no call before this (Discord said wait)
        self.down = False
        self.next_log = float("-inf")
        self.next_error_log = float("-inf")
        self.wake = None
        self.task = None
        self.urgent = {}                        # account -> futures of the actions that wait for a lookup of it (see `confirm`)
        self.urgent_bucket = TokenBucket(limits.roles_urgent_burst, limits.roles_urgent_refill_seconds, clock())      # what the actions may ask for, all together

    def __repr__(self):
        return "<Roles %s, %d entries>" % ("on" if self.on else "off", len(self.entries))      # (no setting, no account)

    __str__ = __repr__

    # ---- what the chat asks ----------------------------------------------------------------------------------------------------------------

    def role_of(self, account):
        """`admin`, `mod` or `none` now: the account in `ANTS_HUB_OWNER` is always `admin`; anybody else holds what Discord last said, for the interval and the grace, and nothing after that."""
        if self.owner and account == self.owner:
            return ADMIN
        entry = self.entries.get(account)
        if entry is None or entry.ok_at is None or self.clock() - entry.ok_at > self.grace:
            return NONE
        return entry.role

    def fresh(self, accounts):
        """Whether the role of every one of `accounts` was answered by Discord within the last `roles_action_fresh_seconds` (the owner account, which needs no answer, and any account when the roles are
        off, which nobody holds, always are): the condition for an action of a moderator."""
        if not self.on:
            return True
        now = self.clock()
        for account in accounts:
            if self.owner and account == self.owner:
                continue
            entry = self.entries.get(account)
            if entry is None or entry.ok_at is None or now - entry.ok_at > self.limits.roles_action_fresh_seconds:
                return False
        return True

    async def confirm(self, accounts):
        """True when every one of `accounts` has a fresh answer (see `fresh`) now, after an urgent lookup of those that had not: the lookups go to the front of the queue and are one at a time like all of
        them. False (fail closed) when one cannot be had within `roles_urgent_seconds`, whatever the reason (Discord down, slow, busy, refusing: the roles are paused, which is told at once)."""
        accounts = [a for a in dict.fromkeys(accounts)]
        if self.fresh(accounts):
            return True
        if self.task is None or self.clock() < self.pause_until:
            return False                                                   # (no worker, or Discord refuses what the roles ask with: there is no answer to wait for)
        loop = asyncio.get_running_loop()
        stale = [a for a in accounts if not (self.owner and a == self.owner)]      # (one answer that is old is a reason to ask about all of them: the action rests on answers of one moment)
        for account in stale:
            if account not in self.entries:
                if len(self.entries) >= self.limits.roles_entries_max and not self.evict():
                    return False                                           # (nowhere to keep an answer: the action is refused, which is the safe side)
                self.entries[account] = Entry(self.clock())
        if not self.afford(len(stale)):
            return False                                                   # (the actions have asked for more than their share of the lookups: this one is refused)
        waiting = []
        for account in stale:
            future = loop.create_future()
            self.urgent.setdefault(account, []).append(future)
            waiting.append(future)
        if self.wake is not None:
            self.wake.set()
        done, pending = await asyncio.wait(waiting, timeout=self.limits.roles_urgent_seconds)
        for future in pending:
            future.cancel()
        return not pending and all(f.result() for f in done) and self.fresh(accounts)

    def afford(self, count):
        """Whether the actions may ask for `count` lookups now, from their own share (`roles_urgent_burst`, one more every `roles_urgent_refill_seconds`); if so, they are spent."""
        bucket = self.urgent_bucket
        bucket.ready(self.clock())                                         # (brings the tokens up to date)
        if bucket.tokens < count:
            return False
        bucket.tokens -= count
        return True

    def next_urgent(self):
        """The account of an action that waits for a lookup, first come first served (futures that were cancelled, by an action that gave up, are dropped)."""
        for account in list(self.urgent):
            self.urgent[account] = [f for f in self.urgent[account] if not f.done()]
            if self.urgent[account]:
                return account
            del self.urgent[account]
        return None

    def answered(self, account, ok):
        """A lookup of `account` is over (`ok`: it gave an answer): the actions that waited for it are told."""
        for future in self.urgent.pop(account, ()):
            if not future.done():
                future.set_result(ok)

    def want(self, account):
        """A socket of `account` was let in: its role is asked at the front of the queue, unless an answer that is young is already known."""
        if not self.on or (self.owner and account == self.owner):
            return
        now = self.clock()
        entry = self.entries.get(account)
        if entry is None:
            if len(self.entries) >= self.limits.roles_entries_max and not self.evict():
                return                                              # (nowhere to keep it: the person is `none`, which is the safe side)
            entry = self.entries[account] = Entry(now)
        elif (entry.ok_at is None or now - entry.ok_at >= self.interval) and not (entry.fails > 0 and entry.due > now):
            entry.new, entry.due = True, now                        # (a person whose checks fail keeps the delay of the retries: a page that reconnects does not skip it)
        entry.seen = now
        if self.wake is not None:
            self.wake.set()

    def evict(self):
        """Room for one more: the entry of the person who left longest ago goes. False when everybody is here (cannot happen: sockets are capped far below the number of entries)."""
        here = set(self.connected())
        gone = [(e.seen, a) for a, e in self.entries.items() if a not in here]
        if not gone:
            return False
        del self.entries[min(gone)[1]]
        return True

    # ---- the clock's work ------------------------------------------------------------------------------------------------------------------

    def on_tick(self, now):
        """Once a tick: forget those who left (after the grace: a page that reloads keeps its role), and tell the pages of a role that ran out."""
        if self.task is not None and self.task.done():                # (the worker ended: roles run out by themselves meanwhile; say so once and start it again)
            ended, self.task = self.task, None
            try:
                if not ended.cancelled():
                    ended.exception()                                # (read, so that asyncio does not print it, with its text, when the task is dropped: only the class of it goes in the log, below)
            except BaseException:
                pass
            self.trouble(RuntimeError("worker ended"), "worker_ended")
            self.start()
        here = set(self.connected())
        for account, entry in list(self.entries.items()):
            if account in here:
                entry.seen = now
            elif now - entry.seen > self.grace:
                del self.entries[account]
                continue
            self.settle(account, entry, now)

    def settle(self, account, entry, now):
        """If the role that the person holds now is not the one that the pages were told, tell them (a live `you` frame, by the chat)."""
        current = self.role_of(account)
        if current != entry.shown:
            if entry.shown != NONE and current == NONE:
                entry.lost_at = now
            entry.shown = current
            self.changed(account)

    # ---- the worker ------------------------------------------------------------------------------------------------------------------------

    def start(self):
        if self.on and self.task is None:
            self.wake = asyncio.Event()
            self.task = asyncio.ensure_future(self.run())

    def stop(self):
        if self.task is not None:
            self.task.cancel()
            self.task = None

    async def run(self):
        while True:
            try:
                delay = await self.step()
            except asyncio.CancelledError:
                raise
            except Exception as e:                                  # (a bug costs one look and never the worker; the class of the error is all that is logged)
                self.trouble(e)
                delay = 1.0
            if delay > 0:
                self.wake.clear()
                try:
                    await asyncio.wait_for(self.wake.wait(), delay)
                except asyncio.TimeoutError:
                    pass
            else:
                await asyncio.sleep(0)

    def trouble(self, error, kind=None):
        now = self.clock()
        if now >= self.next_error_log:
            self.next_error_log = now + self.limits.roles_log_seconds
            self.log.event("error", kind=kind or type(error).__name__, where="roles")

    async def step(self):
        """One look at the queue: asks about the person who is first, if a call may be made now. The number is the seconds until the next look is worth making (0: look again at once)."""
        now = self.clock()
        for until in (self.pause_until, self.hold_until):
            if now < until:
                return min(until - now, MAX_WAIT)
        account = self.next_urgent()
        urgent = account is not None
        if not urgent:
            account = self.pick(now)
        if account is None:
            return self.until_next(now)
        if not self.bucket.take(now):
            return max(0.01, (1.0 - self.bucket.tokens) * self.bucket.refill)
        ok = await self.check(account)
        if urgent:
            self.answered(account, ok)
        return 0.0

    def pick(self, now):
        """The account to ask about next: role holders who are long overdue, then those that connected first (in the order they came), then role holders, then the rest, each by the time it is due."""
        here = set(self.connected())
        best, best_key = None, None
        for account, entry in self.entries.items():
            if account not in here or entry.due > now:
                continue
            holder = entry.role != NONE or (entry.lost_at is not None and now - entry.lost_at < LOST_FOR)
            key = (0 if holder and now - entry.due > OVERDUE else 1 if entry.new else 2 if holder else 3, entry.due)
            if best_key is None or key < best_key:
                best, best_key = account, key
        return best

    def until_next(self, now):
        here = set(self.connected())
        soonest = min((e.due for a, e in self.entries.items() if a in here), default=now + MAX_WAIT)
        return min(MAX_WAIT, max(0.05, soonest - now))

    def jittered(self, seconds):
        spread = self.limits.roles_jitter
        return seconds * (1.0 + spread * (2.0 * self.rng() - 1.0))

    async def check(self, account):
        """Ask Discord about one account and take the answer in."""
        try:
            status, data, rl = await self.client.member(self.guild, account, self._token)
        except discordmod.DiscordError:
            self.failed(account, "down")
            return False
        except Exception as e:                                       # (anything else is a failed check too, and the class of it is all that is logged)
            self.trouble(e)
            self.failed(account, "down")
            return False
        now = self.clock()
        remaining, reset = rl.get("remaining"), rl.get("reset_after")
        if remaining is not None and remaining < 1 and reset is not None:
            self.hold_until = max(self.hold_until, now + min(reset, self.limits.roles_hold_max_seconds))          # (the bucket is empty: wait for it to fill)
        code = data.get("code")
        if type(code) is not int:
            code = None
        if status == 200:
            role = self.role_in(data, account)
        elif status == 404 and code in (NOT_A_MEMBER, UNKNOWN_USER):
            role = NONE
        elif status == 404 and code == UNKNOWN_GUILD:
            self.refused("guild", status, now)
            return False
        elif status == 401:
            self.refused("token", status, now)
            return False
        elif status == 403:
            self.refused("access", status, now)
            return False
        elif status == 429:
            wait = rl.get("retry_after", 5.0)                       # (an unreadable or huge value is read as the longest wait: see hub/discord.py)
            self.hold_until = max(self.hold_until, now + min(max(wait, 0.5), self.limits.roles_hold_max_seconds))     # (every call goes through this one worker: all of them wait)
            self.unwell("busy", now)
            return False
        else:
            role = None
        if role is None:
            self.failed(account, "down")
            return False
        entry = self.entries.get(account)
        if entry is not None:
            entry.role, entry.ok_at, entry.fails, entry.new = role, now, 0, False
            entry.due = now + self.jittered(self.interval)
            self.settle(account, entry, now)
        if self.down:
            self.down = False
            self.log.event("roles_up")
        return entry is not None

    def role_in(self, data, account):
        """The role that a member object says (`admin`, `mod` or `none`), or None when it is not the shape of a member: roles that are a list of at most 250 ids, and a user, if there is one,
        that is the one asked about."""
        roles = data.get("roles")
        if not isinstance(roles, list) or len(roles) > MAX_ROLES or not all(isinstance(r, str) and config.SNOWFLAKE.match(r) for r in roles):
            return None
        user = data.get("user")
        if user is not None and (not isinstance(user, dict) or user.get("id") != account):
            return None
        if self.admin_id and self.admin_id in roles:
            return ADMIN
        if self.mod_id and self.mod_id in roles:
            return MOD
        return NONE

    def failed(self, account, kind):
        """A check that gave no answer (down, slow, garbage): the person keeps what they have until the grace runs out, and is asked again soon, then at the normal pace."""
        now = self.clock()
        entry = self.entries.get(account)
        if entry is not None:
            entry.fails += 1
            entry.new = False
            if entry.fails <= 3:
                entry.due = now + self.jittered(min(self.limits.roles_retry_seconds * 2 ** (entry.fails - 1), self.limits.roles_retry_max_seconds))
            else:
                entry.due = now + self.jittered(self.interval)
        self.unwell(kind, now)

    def unwell(self, kind, now):
        """Discord is not answering well: one line, not more often than `roles_log_seconds`; `roles_up` says when it is again."""
        self.down = True
        if now >= self.next_log:
            self.next_log = now + self.limits.roles_log_seconds
            self.log.event("roles_down", kind=kind)

    def refused(self, kind, status, now):
        """Discord refuses the token, the access or the server: no call for `roles_pause_seconds`, and nobody holds a role (the owner account's stays); one line."""
        self.pause_until = now + self.limits.roles_pause_seconds
        self.log.event("warning", what="discord_roles_refused", kind=kind, status=status)
        for account, entry in list(self.entries.items()):
            entry.ok_at = None
            entry.role = NONE
            entry.due = self.pause_until
            entry.new = True
            self.settle(account, entry, now)
        for account in list(self.urgent):
            self.answered(account, False)                           # (the actions that wait are refused at once: there is no answer to wait for)
