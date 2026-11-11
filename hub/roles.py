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


class Entry:
    """What is known of one person who is here: the role that Discord last said, when, when to ask again, and the role that the pages were last told."""

    __slots__ = ("role", "ok_at", "due", "fails", "new", "seen", "shown", "lost_at")

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
        self.entries = {}                       # account -> Entry, for the accounts that have a socket open (and, for one grace, those that had)
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

    # ---- what the chat asks ----------------------------------------------------------------------------------------------------------------

    def role_of(self, account):
        """`admin`, `mod` or `none` now: the account in `ANTS_HUB_OWNER` is always `admin`; anybody else holds what Discord last said, for the interval and the grace, and nothing after that."""
        if self.owner and account == self.owner:
            return ADMIN
        entry = self.entries.get(account)
        if entry is None or entry.ok_at is None or self.clock() - entry.ok_at > self.grace:
            return NONE
        return entry.role

    def known_none(self, account):
        """Whether the account is POSITIVELY known to hold no role now: an answer of Discord that is young says `none`. An account that is not here, has not been asked yet, or whose answer ran
        out is not known (a moderator's command is never done to a person whose role is not known); the owner account is never none. Without the roles' settings nobody but the owner has one."""
        if self.owner and account == self.owner:
            return False
        if not self.on:
            return True
        entry = self.entries.get(account)
        return entry is not None and entry.ok_at is not None and self.clock() - entry.ok_at <= self.grace and entry.role == NONE

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
            self.task = None
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
        account = self.pick(now)
        if account is None:
            return self.until_next(now)
        if not self.bucket.take(now):
            return max(0.01, (1.0 - self.bucket.tokens) * self.bucket.refill)
        await self.check(account)
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
            return self.failed(account, "down")
        except Exception as e:                                       # (anything else is a failed check too, and the class of it is all that is logged)
            self.trouble(e)
            return self.failed(account, "down")
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
            return self.refused("guild", status, now)
        elif status == 401:
            return self.refused("token", status, now)
        elif status == 403:
            return self.refused("access", status, now)
        elif status == 429:
            wait = rl.get("retry_after", 5.0)                       # (an unreadable or huge value is read as the longest wait: see hub/discord.py)
            self.hold_until = max(self.hold_until, now + min(max(wait, 0.5), self.limits.roles_hold_max_seconds))     # (every call goes through this one worker: all of them wait)
            return self.unwell("busy", now)
        else:
            role = None
        if role is None:
            return self.failed(account, "down")
        entry = self.entries.get(account)
        if entry is not None:
            entry.role, entry.ok_at, entry.fails, entry.new = role, now, 0, False
            entry.due = now + self.jittered(self.interval)
            self.settle(account, entry, now)
        if self.down:
            self.down = False
            self.log.event("roles_up")

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
