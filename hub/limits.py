"""Every number the hub enforces, in one place (docs/HUB.md, "Limits"), and the small counters that enforce them. No I/O: the clock is given."""
import collections
import ipaddress
from dataclasses import dataclass


@dataclass(frozen=True)
class Limits:
    frame_max: int = 2048               # bytes of a frame's payload that a client may send
    line_code_points: int = 200         # a chat line, after the text rules (NFC)
    line_bytes: int = 800               # ... and in UTF-8
    history_lines: int = 50             # lines sent on connect, kept in memory
    deleted_announce_max: int = 100     # `del` frames for one bulk deletion (a forget, a run of deletions from the command line): the pages keep 100 lines, ids only grow, so only the newest 100 can be on a screen
    retention_days: int = 7             # lines kept in the database for moderation
    burst: int = 3                      # lines an account may send at once ...
    refill_seconds: float = 2.0         # ... then one every this long, across all its tabs
    duplicate_seconds: float = 30.0     # the same text twice within this long is dropped
    account_hour_lines: int = 200       # lines an account may say in an hour: a budget of 200 that fills up again steadily (one every 18 seconds) ...
    account_day_lines: int = 1000       # ... and in a day: 1,000, one more every 86.4 seconds. What is over is `quota`. (The store is everybody's: an account's share of it stays small)
    violations: int = 20                # this many mistakes within a minute close the socket (1008)
    violation_window: float = 60.0
    frame_burst: int = 10               # frames of any kind a socket may send at once ...
    frame_refill_seconds: float = 0.2   # ... then five a second
    sockets_per_address: int = 8
    sockets_total: int = 500
    pending_total: int = 64             # connections that have not finished their handshake
    ping_seconds: float = 25.0
    silence_seconds: float = 60.0
    handshake_seconds: float = 10.0
    online_seconds: float = 5.0         # the online count is sent when it changed, at most this often
    tick_seconds: float = 1.0
    deletions_seconds: float = 2.0      # how often the lines that the command line deleted are looked for
    purge_seconds: float = 3600.0       # how often the lines past their 7 days, or past the newest 200, are deleted
    write_buffer_max: int = 131072      # a socket that has this much unsent is dropped (it cannot keep up); 500 of them stay well inside the container's memory
    close_grace_seconds: float = 5.0    # a socket that was closed with output unsent is cut after this long
    identify_seconds: float = 5.0       # the login layer has this long to say who is at the other end of a handshake
    global_burst: int = 50              # lines that all accounts together may say at once ...
    global_refill_seconds: float = 0.2  # ... then five a second (what the database is asked to keep)
    db_max_bytes: int = 67108864        # no new chat line is kept once hub.db and its log file hold this much (mutes, bans and deletions are never refused for it)
    prune_from_percent: int = 90        # the hub starts to delete the oldest lines when hub.db and its log file hold this much of db_max_bytes ...
    prune_to_percent: int = 80          # ... and goes on, purge_chunk lines a second, until the pages that hold something are down to this much (the freed pages are reused)
    keep_lines: int = 1000              # the newest lines, which making room never deletes (a page shows 100 at most)
    purge_chunk: int = 2000             # lines deleted at a time when they are past their 7 days
    purge_failure_seconds: float = 60.0         # how soon a purge that failed (a locked or full database) is tried again
    checkpoint_retry_seconds: float = 10.0      # how soon the database's log file is cut again when a reader held it
    crowded_sockets: int = 6            # more sockets than this from one address group is logged (docs/HUB.md)
    crowded_log_seconds: float = 60.0
    who_rows_max: int = 200             # named people that the list of the front page carries (the rest are counted: `more`); moderators first, then the people who wait, who play, who look
    who_seconds: float = 2.0            # the list is sent to the pages that watch it when it changed, not sooner than this after the last send, whatever happens (changes in between are one change)
    who_resend_seconds: float = 10.0    # a socket may ask for the whole list again after this long (it gets it once, whatever it asks in between)
    who_diff_max: int = 50              # a change of more rows than this is sent as the whole list: a frame is never bigger than the list's own
    login_burst: int = 10               # /hub/login and /hub/callback: for each client address a burst of 10, then one every 6 seconds (10 a minute; nginx has the same numbers in front)
    login_refill_seconds: float = 6.0
    session_burst: int = 20             # /hub/me, /hub/logout and /hub/forget: a burst of 20, then one every 3 seconds
    session_refill_seconds: float = 3.0
    login_inflight: int = 6             # logins that wait for Discord at one time (the 7th gets the page "Login unavailable" at once, and the sockets never wait for it)
    login_states_max: int = 10000       # logins that were started and not finished (each is about 200 bytes of memory); the oldest go first
    state_seconds: float = 600.0        # how long a login may take, from /hub/login to /hub/callback
    session_seconds: float = 30 * 86400.0       # how long a login lasts (30 days)
    sessions_per_account: int = 10      # logins that one account keeps at a time (browsers, phones); the oldest go first
    sessions_total: int = 100000        # logins that the hub keeps in all (a crowd of throwaway accounts cannot make the table grow without end): the oldest go first
    profiles_total: int = 100000        # profiles (the username and the display name of an account, from its last login) that the hub keeps in all: the ones refreshed longest ago go first
    pick_burst: int = 3                 # switches between the display name and the username that an account may make at once (across all its tabs: a switch makes every page of the account connect again) ...
    pick_refill_seconds: float = 30.0   # ... then one in this long
    discord_connect_seconds: float = 3.0        # to Discord: the connection ...
    discord_seconds: float = 6.0                # ... a whole call, from the first byte sent to the last byte read ...
    discord_total_seconds: float = 10.0         # ... and the two calls of a login together (nginx waits 15 seconds for the hub's answer)
    discord_body_max: int = 32768       # what is read of an answer of Discord (the head and the body together); more is an answer that is not Discord's

    @property
    def prune_from_bytes(self):
        return self.db_max_bytes * self.prune_from_percent // 100

    @property
    def prune_to_bytes(self):
        return self.db_max_bytes * self.prune_to_percent // 100


class TokenBucket:
    """`burst` tokens, one more every `refill_seconds`; take() spends one."""

    __slots__ = ("burst", "refill", "tokens", "at")

    def __init__(self, burst, refill_seconds, now):
        self.burst = float(burst)
        self.refill = float(refill_seconds)
        self.tokens = float(burst)
        self.at = now

    def ready(self, now):
        """Whether take() would succeed now (nothing is spent)."""
        self.tokens = min(self.burst, self.tokens + max(0.0, now - self.at) / self.refill)
        self.at = now
        return self.tokens >= 1.0

    def take(self, now):
        if self.ready(now):
            self.tokens -= 1.0
            return True
        return False

    def give_back(self):
        self.tokens = min(self.burst, self.tokens + 1.0)


class AccountLimits:
    """What one account may say, however many tabs it has open: a token bucket, the texts it said in the last `duplicate_seconds`, and its budgets for the hour and for the day."""

    def __init__(self, limits):
        self.limits = limits
        self.accounts = {}                       # account id -> [bucket, {text: time said}, last use]
        self.budgets = {}                        # account id -> [lines left for the hour, lines left for the day]: kept while they are not full again

    def budget(self, account, now):
        budget = self.budgets.get(account)
        if budget is None:
            hour, day = max(1, self.limits.account_hour_lines), max(1, self.limits.account_day_lines)
            budget = self.budgets[account] = [TokenBucket(hour, 3600.0 / hour, now), TokenBucket(day, 86400.0 / day, now)]
        return budget

    def check(self, account, text, now):
        """None when the line may go out (and it is counted), else 'dup', 'quota' (the hour's or the day's budget is spent) or 'rate' (and nothing is counted: a refused line costs no token)."""
        entry = self.accounts.get(account)
        if entry is None:
            entry = self.accounts[account] = [TokenBucket(self.limits.burst, self.limits.refill_seconds, now), {}, now]
        bucket, said, _ = entry
        entry[2] = now
        for old in [t for t, at in said.items() if now - at >= self.limits.duplicate_seconds]:
            del said[old]
        if text in said:
            return "dup"
        hour, day = self.budget(account, now)
        if not (hour.ready(now) and day.ready(now)):
            return "quota"
        if not bucket.take(now):
            return "rate"
        hour.take(now)
        day.take(now)
        said[text] = now
        return None

    def refund(self, account, text):
        """Undo what check() counted for a line that was not kept after all (the database refused it): the tokens come back, and the text may be tried again at once."""
        entry = self.accounts.get(account)
        if entry is not None:
            entry[0].give_back()
            entry[1].pop(text, None)
        for bucket in self.budgets.get(account, ()):
            bucket.give_back()

    def forget_idle(self, now, idle_seconds=300.0):
        """Drop the accounts that have been quiet for a while (their bucket is full again anyway), and the budgets that are full again (an account that has not used up its hour or its day is remembered)."""
        for account in [a for a, e in self.accounts.items() if now - e[2] >= idle_seconds]:
            del self.accounts[account]
        for account in [a for a, budget in self.budgets.items() if all(b.ready(now) and b.tokens >= b.burst for b in budget)]:
            del self.budgets[account]


class RateByAddress:
    """A token bucket for each client address (the key of address_key): take() says whether this address may go on. A bucket that is full again is forgotten by forget_idle()."""

    def __init__(self, burst, refill_seconds, keys_max=20000):
        self.burst = burst
        self.refill = refill_seconds
        self.keys_max = keys_max                # (a crowd of addresses, an IPv6 range, cannot make the table grow without end: a new address is refused while the table is full)
        self.buckets = {}

    def take(self, key, now):
        bucket = self.buckets.get(key)
        if bucket is None:
            if len(self.buckets) >= self.keys_max:
                self.forget_idle(now)
                if len(self.buckets) >= self.keys_max:
                    return False
            bucket = self.buckets[key] = TokenBucket(self.burst, self.refill, now)
        return bucket.take(now)

    def forget_idle(self, now):
        for key in [k for k, b in self.buckets.items() if b.ready(now) and b.tokens >= self.burst]:
            del self.buckets[key]


class RateByAccount(RateByAddress):
    """The same table of token buckets, one for each account id (the key is the id, not an address): what an account may do however many tabs it has open. A bucket that is full again is
    forgotten by forget_idle()."""


class Strikes:
    """The mistakes of one socket within the last `window` seconds."""

    def __init__(self, limit, window):
        self.limit = limit
        self.window = window
        self.times = collections.deque()

    def add(self, now):
        """Count one mistake; True when that was the last one this socket is allowed."""
        self.times.append(now)
        while self.times and now - self.times[0] >= self.window:
            self.times.popleft()
        return len(self.times) >= self.limit


def address_key(value, peer):
    """The key that counts sockets per address: the X-Client-Address that the site's nginx sets (an IP address, else the peer's own); an IPv6 address counts as its /64; an IPv4-in-IPv6 as the IPv4."""
    for candidate in (value, peer):
        try:
            ip = ipaddress.ip_address((candidate or "").strip())
        except ValueError:
            continue
        if ip.version == 6 and ip.ipv4_mapped is not None:
            ip = ip.ipv4_mapped
        if ip.version == 6:
            return str(ipaddress.IPv6Network((int(ip) >> 64 << 64, 64)))
        return str(ip)
    return "unknown"


class PerAddress:
    def __init__(self, limit):
        self.limit = limit
        self.counts = {}

    def try_add(self, key):
        if self.counts.get(key, 0) >= self.limit:
            return False
        self.counts[key] = self.counts.get(key, 0) + 1
        return True

    def remove(self, key):
        left = self.counts.get(key, 0) - 1
        if left > 0:
            self.counts[key] = left
        else:
            self.counts.pop(key, None)
