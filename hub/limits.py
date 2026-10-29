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
    retention_days: int = 7             # lines kept in the database for moderation
    burst: int = 3                      # lines an account may send at once ...
    refill_seconds: float = 2.0         # ... then one every this long, across all its tabs
    duplicate_seconds: float = 30.0     # the same text twice within this long is dropped
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
    purge_seconds: float = 3600.0       # how often the lines past their 7 days are deleted
    write_buffer_max: int = 131072      # a socket that has this much unsent is dropped (it cannot keep up); 500 of them stay well inside the container's memory
    close_grace_seconds: float = 5.0    # a socket that was closed with output unsent is cut after this long
    identify_seconds: float = 5.0       # the login layer has this long to say who is at the other end of a handshake
    global_burst: int = 50              # lines that all accounts together may say at once ...
    global_refill_seconds: float = 0.2  # ... then five a second (what the database is asked to keep)
    db_max_bytes: int = 67108864        # no new chat line is kept once hub.db and its log file hold this much (mutes, bans and deletions are never refused for it)
    purge_chunk: int = 2000             # lines deleted at a time when they are past their 7 days
    purge_failure_seconds: float = 60.0         # how soon a purge that failed (a locked or full database) is tried again
    checkpoint_retry_seconds: float = 10.0      # how soon the database's log file is cut again when a reader held it
    crowded_sockets: int = 6            # more sockets than this from one address group is logged (all visitors may share one address, docs/HUB.md)
    crowded_log_seconds: float = 60.0


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


class AccountLimits:
    """What one account may say, however many tabs it has open: a token bucket, and the texts it said in the last `duplicate_seconds`."""

    def __init__(self, limits):
        self.limits = limits
        self.accounts = {}                       # account id -> [bucket, {text: time said}, last use]

    def check(self, account, text, now):
        """None when the line may go out (and it is counted), else 'dup' or 'rate' (and nothing is counted: a refused line costs no token)."""
        entry = self.accounts.get(account)
        if entry is None:
            entry = self.accounts[account] = [TokenBucket(self.limits.burst, self.limits.refill_seconds, now), {}, now]
        bucket, said, _ = entry
        entry[2] = now
        for old in [t for t, at in said.items() if now - at >= self.limits.duplicate_seconds]:
            del said[old]
        if text in said:
            return "dup"
        if not bucket.take(now):
            return "rate"
        said[text] = now
        return None

    def forget_idle(self, now, idle_seconds=300.0):
        """Drop the accounts that have been quiet for a while (their bucket is full again anyway)."""
        for account in [a for a, e in self.accounts.items() if now - e[2] >= idle_seconds]:
            del self.accounts[account]


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
