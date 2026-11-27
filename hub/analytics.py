"""The counter (docs/ANALYTICS.md): what the site's own pages tell the hub about a visit, counted without a cookie, without keeping an address and without a third party.

THE VISITOR is a code, `HMAC-SHA-256(secret of the day, address, 0, browser's header)`, cut to 16 hex digits. The secret is made fresh every day (the day is a Pacific day, as the admin page's), is kept
only until the day is over, and is deleted then and cut out of the file (hub/analytics_store.py `salt_of`): from the next day on nothing can say which address a code came from, or that two codes of two
days are one browser. The address and the header are in this process for the length of one request: the country is looked up (hub/geodata.py), the browser, the system and the device are read from the
header (hub/useragent.py), and then both are dropped. Neither is stored, logged or kept in a table.

A BROWSER THAT ASKED NOT TO BE TRACKED (Do Not Track or Global Privacy Control, said by its header or by its page) is one visit and a country: no code, no device, no page, no timing, no error. To count a
visit once and not every page of it, the hub remembers a short code of the address and the header IN MEMORY ONLY for 30 minutes (never written anywhere, never in an event).

RETURNING needs a link between days, and the only one is in memory too: a second code, made with a key that exists only in this process, is kept for the last 7 days, so that a visitor can be told as
"seen on an earlier day of the last 7". It is never written, a restart forgets it (the visitors that follow count as new until they come back), and what is stored is one flag on the events.

The counter never stops the hub: a database that refuses, or a bug, drops the event and costs one line in the log a minute (the class of the error, nothing else).
"""
import collections
import hashlib
import hmac
import os
import sqlite3
import time

from . import analytics_events as events, analytics_store as storemod, analytics_sums as sumsmod, limits as limitmod, useragent

NT_SECONDS = 1800.0                    # a visit of a browser that asked not to be tracked is counted once in this long
NT_MAX = 100000                        # such visits that are remembered at one time (memory only); more are not remembered, and are counted again
WEEK_MAX = 300000                      # the codes of the last 7 days that are remembered (memory only); more are not added (those visitors count as new)
RETURN_DAYS = 6                        # a visitor is returning when seen on one of the 6 days before
DAY_MAX = 100000                       # events kept in a day, for all visitors together: a flood that gets past the allowances cannot fill the disk (a site with a hundred visitors a day sends under a thousand); the rest are dropped and counted
MAINTAIN_SECONDS = 3600.0
FLUSH_SECONDS = 1.0
RANGE_MAX_DAYS = 400
GEO_NAME = "DB-IP Lite"


class Collector:
    def __init__(self, store, geo, origin, clock=time.time, log=None, global_burst=300, global_refill=0.01):
        self.store = store
        self.geo = geo
        self.origin = origin
        self.clock = clock
        self.log = log
        self.key = os.urandom(32)                                  # (the memory-only key of the two short-lived codes below: never written)
        self.day = None
        self.salt = b""
        self.visitors = {}                                         # visitor's code -> whether it is returning (today; memory only)
        self.week = collections.defaultdict(set)                   # day number -> the memory-only codes of the visitors of that day (the last 7 days)
        self.week_size = 0
        self.nt_seen = {}                                          # the short code of an untracked visit -> when it may be counted again (memory only)
        self.pending = collections.Counter()
        self.bucket = limitmod.TokenBucket(global_burst, global_refill, 0.0)         # (all events together: a flood is dropped before it reaches the file)
        self.next_flush = self.next_maintain = self.next_trouble = 0.0
        self.last_at = int(store.meta("last_at") or 0)
        self.kept_today = 0                                        # the events kept today (the file's own count when a day begins)
        self.today()

    # ---- the day and its secret ------------------------------------------------------------------------------------------------------------

    def today(self):
        """The Pacific day of now. When it is a new day: yesterday's counters are written, the secret of yesterday is deleted and a new one is made, and the memory of the last 7 days moves on."""
        now = self.clock()
        day = events.pacific_day(now)
        if day != self.day:
            self.flush(force=True)
            self.day = day
            self.salt = self.store.salt_of(day)
            self.visitors = {}
            self.kept_today = self.store.counter(day, "accepted")
            number = events.day_number(day)
            for old in [d for d in self.week if d < number - RETURN_DAYS]:
                self.week_size -= len(self.week.pop(old))
            self.next_maintain = 0.0
        return day

    def code(self, ip, ua):
        """The visitor's code of today: the same address and header give the same code all day, and nothing gives it afterwards."""
        return hmac.new(self.salt, ip.encode("ascii", "replace") + b"\0" + ua.encode("utf-8", "replace"), hashlib.sha256).hexdigest()[:16]

    def short(self, ip, ua):
        return hmac.new(self.key, ip.encode("ascii", "replace") + b"\0" + ua.encode("utf-8", "replace"), hashlib.sha256).digest()[:12]

    def returning(self, ip, ua, now):
        """Whether the browser was seen on one of the 6 days before today, and from now on that it was seen today (memory only)."""
        mark, number = self.short(ip, ua), events.day_number(self.day)
        seen = any(mark in self.week[d] for d in range(number - RETURN_DAYS, number) if d in self.week)
        if mark not in self.week[number] and self.week_size < WEEK_MAX:
            self.week[number].add(mark)
            self.week_size += 1
        return seen

    # ---- an event --------------------------------------------------------------------------------------------------------------------------

    def refuse(self):
        self.pending["refused"] += 1

    def admit(self):
        """Whether the hub may look at one more event now (all addresses together)."""
        return self.bucket.take(self.clock())

    def accept(self, ip, ua, untracked, body):
        """One event of a page: "ok" (kept), "again" (a visit of an untracked browser that was counted a moment ago), "crawler" (not counted), "full" (the day's limit is reached), "shape" (not an event: refused)
        or "busy" (the file refused it). `ip` and `ua` are texts that this call forgets."""
        try:
            return self.take(ip, ua, untracked, body)
        except (sqlite3.Error, OSError) as e:
            self.trouble(e)
            return "busy"

    def take(self, ip, ua, untracked, body):
        now = self.clock()
        day = self.today()
        names = useragent.parse(ua)
        if names is None:
            self.pending["crawler"] += 1
            return "crawler"
        event = events.check(body, self.origin)
        if event is None:
            self.refuse()
            return "shape"
        if self.kept_today >= DAY_MAX:
            self.pending["dropped"] += 1
            return "full"
        country = self.geo.country(ip)
        if untracked or event["kind"] == "visit":
            mark = self.short(ip, ua)
            if self.nt_seen.get(mark, 0.0) > now:
                return "again"
            if len(self.nt_seen) >= NT_MAX:
                self.sweep(now)
            if len(self.nt_seen) < NT_MAX:
                self.nt_seen[mark] = now + NT_SECONDS
            self.store.add_event(int(now), day, dict(events.EMPTY, kind="visit"), country=country)
        else:
            code = self.code(ip, ua)
            if code not in self.visitors:
                self.visitors[code] = 1 if self.returning(ip, ua, now) else 0
            self.store.add_event(int(now), day, event, vis=code, ret=self.visitors[code], country=country, browser=names[0], system=names[1], device=names[2])
        self.pending["accepted"] += 1
        self.kept_today += 1
        self.last_at = int(now)
        return "ok"

    def signup(self):
        """A person logged in who had no account here before (the Discord login's hook): one more for today. A count, nothing else."""
        try:
            self.store.bump(self.today(), "signups")
        except (sqlite3.Error, OSError) as e:
            self.trouble(e)

    # ---- the clock -------------------------------------------------------------------------------------------------------------------------

    def sweep(self, now):
        for mark in [m for m, until in self.nt_seen.items() if until <= now]:
            del self.nt_seen[mark]

    def flush(self, force=False):
        """The counters that wait in memory go to the file (once a second at the most), and the moment of the last event."""
        if not force and self.clock() < self.next_flush:
            return
        self.next_flush = self.clock() + FLUSH_SECONDS
        if not self.pending and not self.last_at:
            return
        try:
            for name, n in list(self.pending.items()):
                self.store.bump(self.day, name, n)
                del self.pending[name]
            if self.last_at:
                self.store.set_meta("last_at", self.last_at)
        except (sqlite3.Error, OSError) as e:
            self.trouble(e)

    def on_tick(self):
        """The hub's clock calls this once a second: a new day, the counters, the memory that has run out, the hour's work."""
        now = self.clock()
        self.today()
        self.flush()
        if now >= self.next_maintain:
            self.next_maintain = now + MAINTAIN_SECONDS
            self.sweep(now)
            try:
                self.store.maintain(self.day)
            except (sqlite3.Error, OSError) as e:
                self.trouble(e)

    def trouble(self, error):
        now = self.clock()
        if self.log is not None and now >= self.next_trouble:
            self.next_trouble = now + 60.0
            self.log.event("error", kind=type(error).__name__, where="analytics")

    def stop(self):
        self.flush(force=True)
        self.store.close()

    # ---- the report the admin page asks for ------------------------------------------------------------------------------------------------

    def report(self, first, last):
        """What the Analytics tab shows for the days `first` to `last` (YYYY-MM-DD, both counted): None for a range that is not a range of at most 400 days that ends today or before. The sums of the days
        are read, and the earlier days of the same length are totalled for the changes."""
        today = self.today()
        first, last = events.valid_day(first), events.valid_day(last)
        if first is None or last is None or first > last or last > today:
            return None
        n = events.day_number(last) - events.day_number(first) + 1
        if n > RANGE_MAX_DAYS:
            return None
        days = [events.add_days(first, i) for i in range(n)]
        sums = [self.store.sums_of(day, today) for day in days]
        before = [self.store.sums_of(events.add_days(first, -n + i), today) for i in range(n)]
        per_day = [{"day": day, "visitors": s.get("visitors", 0), "visits": s.get("visits", 0), "nt": s.get("nt", 0), "signups": s.get("signups", 0),
                    "errors": sum(e[3] for e in s.get("errors", ())) + s.get("errors_other", 0), "perf": s.get("perf", {}), "vers": s.get("vers", {})} for day, s in zip(days, sums)]
        lists = sumsmod.merged(sums)
        firsts = {}
        for day, s in zip(days, sums):
            for kind, message, source, *_ in s.get("errors", ()):
                firsts.setdefault((kind, message, source), day)
        errors = [{"kind": k, "msg": m, "src": s, "n": c, "visitors": v, "first": firsts[(k, m, s)], "last": at} for k, m, s, c, v, at in lists.pop("errors")]
        earlier = {"visitors": 0, "visits": 0, "nt": 0, "signups": 0, "errors": 0}
        for s in before:
            earlier["visitors"] += s.get("visitors", 0)
            earlier["visits"] += s.get("visits", 0)
            earlier["nt"] += s.get("nt", 0)
            earlier["signups"] += s.get("signups", 0)
            earlier["errors"] += sum(e[3] for e in s.get("errors", ())) + s.get("errors_other", 0)
        return {"from": first, "to": last, "today": today, "days": per_day, "before": earlier, "lists": lists, "errors": errors, "counter": self.counter_state(today)}

    def counter_state(self, today):
        return {"now": int(self.clock()), "since": int(self.store.meta("since") or 0), "first_day": self.store.first_day(), "last_at": self.last_at or None,
                "accepted_today": self.store.counter(today, "accepted") + self.pending["accepted"], "refused_today": self.store.counter(today, "refused") + self.pending["refused"],
                "crawlers_today": self.store.counter(today, "crawler") + self.pending["crawler"], "dropped_today": self.store.counter(today, "dropped") + self.pending["dropped"], "raw_days": storemod.RAW_DAYS, "oldest_raw": self.store.oldest_event_day(),
                "geo": {"name": GEO_NAME, "month": self.geo.month} if self.geo.month else None}
