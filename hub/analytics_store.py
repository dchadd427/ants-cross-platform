"""analytics.db: the counter's own database file (docs/ANALYTICS.md, "What is kept"), apart from hub.db so that the chat's schema, its size limit and its purge are untouched by it.

  events    the raw events of the last 30 days: the hour (never a moment to the second: the web server's own log holds the seconds, and the two must not be joined), the day, the kind, the visitor's
            code of that day (empty for a browser that asked not to be tracked), whether the event starts a visit and how long the visit had run (a bin of seconds, hub/analytics_sums.py), the country, the
            browser, the system and the kind of device, and the checked fields of the event (hub/analytics_events.py). No address, no header, no text of the visitor's.
  days      the sums of every finished day, for good (hub/analytics_sums.py): numbers and short lists, nothing that names a person
  counters  small numbers of a day: events let in, events refused, sign-ups
  salt      the secret of TODAY, which makes the visitors' codes; the row of a day is deleted when the day is over and the deleted bytes are overwritten (secure_delete) and cut out of the log file
  meta      the moment of the first start and of the last event

The file is made 0600. A refusal of the file (locked, full) is an sqlite3.OperationalError that the caller turns into a dropped event: counting never stops the hub.
"""
import json
import os
import sqlite3

from . import analytics_events as events, analytics_sums as sums

SCHEMA = 1
RAW_DAYS = 30                                       # the days that raw events are kept


CUT_RETRY_SECONDS = 10.0                            # how soon the log file is cut again when a reader held it


class Store:
    def __init__(self, path, clock):
        self.path = path
        self.clock = clock
        self.log_dirty = False                      # a cut of the log file that did not happen (a reader held the file): settle() tries again
        self.next_cut = 0.0
        new = not os.path.exists(path)
        self.db = sqlite3.connect(path, timeout=2.0, isolation_level=None)
        try:
            self.db.execute("PRAGMA journal_mode=WAL")
            self.db.execute("PRAGMA synchronous=NORMAL")
            self.db.execute("PRAGMA secure_delete=ON")
            self.db.execute("PRAGMA busy_timeout=300")
            if new:
                try:
                    os.chmod(path, 0o600)
                except OSError:
                    pass
            self.migrate()
        except BaseException:
            self.db.close()
            raise

    def migrate(self):
        version = self.db.execute("PRAGMA user_version").fetchone()[0]
        if version > SCHEMA:
            raise RuntimeError("analytics.db is from a newer hub (schema %d)" % version)
        if version < 1:
            self.db.executescript("""
                BEGIN;
                CREATE TABLE IF NOT EXISTS events (id INTEGER PRIMARY KEY, at INTEGER NOT NULL, day TEXT NOT NULL, kind TEXT NOT NULL, vis TEXT NOT NULL DEFAULT '', ret INTEGER NOT NULL DEFAULT 0,
                                                   vs INTEGER NOT NULL DEFAULT 0, lb INTEGER NOT NULL DEFAULT 0, country TEXT NOT NULL DEFAULT '', browser TEXT NOT NULL DEFAULT '', system TEXT NOT NULL DEFAULT '', device TEXT NOT NULL DEFAULT '',
                                                   wb INTEGER NOT NULL DEFAULT -1, lang TEXT NOT NULL DEFAULT '', path TEXT NOT NULL DEFAULT '', ref TEXT NOT NULL DEFAULT '',
                                                   ms INTEGER NOT NULL DEFAULT 0, name TEXT NOT NULL DEFAULT '', msg TEXT NOT NULL DEFAULT '', src TEXT NOT NULL DEFAULT '', ver TEXT NOT NULL DEFAULT '');
                CREATE INDEX IF NOT EXISTS events_day ON events (day, vis);
                CREATE TABLE IF NOT EXISTS days (day TEXT PRIMARY KEY, sums TEXT NOT NULL);
                CREATE TABLE IF NOT EXISTS counters (day TEXT NOT NULL, name TEXT NOT NULL, n INTEGER NOT NULL, PRIMARY KEY (day, name)) WITHOUT ROWID;
                CREATE TABLE IF NOT EXISTS salt (day TEXT PRIMARY KEY, salt BLOB NOT NULL);
                CREATE TABLE IF NOT EXISTS meta (k TEXT PRIMARY KEY, v TEXT NOT NULL);
                PRAGMA user_version = 1;
                COMMIT;""")
        self.db.execute("INSERT OR IGNORE INTO meta (k, v) VALUES ('since', ?)", (str(int(self.clock())),))

    def close(self):
        self.db.close()

    # ---- the secret of the day -------------------------------------------------------------------------------------------------------------

    def salt_of(self, day):
        """The secret of `day`: the one that is kept, else a new one (32 random bytes). The secrets of every other day are deleted, and what was deleted is cut out of both files (a cut that a reader
        of the file kept from happening is tried again by settle(), which the hub's clock calls)."""
        row = self.db.execute("SELECT salt FROM salt WHERE day = ?", (day,)).fetchone()
        stale = self.db.execute("SELECT COUNT(*) FROM salt WHERE day != ?", (day,)).fetchone()[0]
        if row is not None and not stale:
            return bytes(row[0])
        salt = bytes(row[0]) if row is not None else os.urandom(32)
        self.db.execute("BEGIN IMMEDIATE")
        try:
            self.db.execute("DELETE FROM salt WHERE day != ?", (day,))
            self.db.execute("INSERT OR IGNORE INTO salt (day, salt) VALUES (?, ?)", (day, salt))
        except BaseException:
            self.db.execute("ROLLBACK")
            raise
        self.db.execute("COMMIT")
        self.cut_log()
        return salt

    def latest_salt_day(self):
        """The last day that has a secret (None when there is none): a clock that stepped back is not a new day (hub/analytics.py `today`)."""
        row = self.db.execute("SELECT MAX(day) FROM salt").fetchone()
        return row[0] if row else None

    def cut_log(self):
        """The write-ahead log is cut, so that what was deleted is in neither file. True when it was; False when a reader holds the file (the hub's one thread never waits for it), and then settle() tries
        again, every CUT_RETRY_SECONDS, until it succeeds."""
        try:
            self.db.execute("PRAGMA busy_timeout=0")
            busy = self.db.execute("PRAGMA wal_checkpoint(TRUNCATE)").fetchone()[0]
        except sqlite3.OperationalError:
            busy = 1
        finally:
            try:
                self.db.execute("PRAGMA busy_timeout=300")
            except sqlite3.OperationalError:
                pass
        self.log_dirty = bool(busy)
        return not busy

    def settle(self):
        """The hub's clock calls this every second: a cut of the log file that did not happen is tried again, not sooner than CUT_RETRY_SECONDS after the last try."""
        if self.log_dirty and self.clock() >= self.next_cut:
            self.next_cut = self.clock() + CUT_RETRY_SECONDS
            self.cut_log()

    def used_bytes(self):
        """The pages of the file that hold something (a page that deleted events freed is room for new ones, though the file never shrinks)."""
        pages = self.db.execute("PRAGMA page_count").fetchone()[0]
        free = self.db.execute("PRAGMA freelist_count").fetchone()[0]
        return (pages - free) * self.db.execute("PRAGMA page_size").fetchone()[0]

    def size(self):
        """Bytes in use, as hub.db counts them: the pages that hold something, and the log file."""
        try:
            log = os.path.getsize(self.path + "-wal")
        except OSError:
            log = 0
        return self.used_bytes() + log

    # ---- events and counters ---------------------------------------------------------------------------------------------------------------

    def add_event(self, at, day, event, vis="", ret=0, vs=0, lb=0, country="", browser="", system="", device=""):
        """`at` is stored as the start of its hour. `vs` is 1 for the page view that starts a visit of `vis`, `lb` the bin of the seconds that the visit had run at this event (hub/analytics_sums.py)."""
        self.db.execute("INSERT INTO events (at, day, kind, vis, ret, vs, lb, country, browser, system, device, wb, lang, path, ref, ms, name, msg, src, ver) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                        (events.hour_of(at), day, event["kind"], vis, ret, vs, lb, country, browser, system, device, event["wb"], event["lang"], event["path"], event["ref"], event["ms"], event["name"], event["msg"], event["src"], event["ver"]))

    def bump(self, day, name, n=1):
        self.db.execute("INSERT INTO counters (day, name, n) VALUES (?, ?, ?) ON CONFLICT (day, name) DO UPDATE SET n = n + excluded.n", (day, name, n))

    def counter(self, day, name):
        row = self.db.execute("SELECT n FROM counters WHERE day = ? AND name = ?", (day, name)).fetchone()
        return row[0] if row else 0

    def set_meta(self, key, value):
        self.db.execute("INSERT INTO meta (k, v) VALUES (?, ?) ON CONFLICT (k) DO UPDATE SET v = excluded.v", (key, str(value)))

    def meta(self, key):
        row = self.db.execute("SELECT v FROM meta WHERE k = ?", (key,)).fetchone()
        return row[0] if row else None

    # ---- the sums --------------------------------------------------------------------------------------------------------------------------

    def rows_of(self, day):
        """The events of a day, one visitor after the other and each in the order it came (the index holds them so): read one at a time, so that a day's events are never all in memory."""
        return map(sums.Row._make, self.db.execute("SELECT vis, ret, kind, vs, lb, country, browser, system, device, wb, lang, path, ref, ms, name, msg, src, ver FROM events WHERE day = ? ORDER BY vis, id", (day,)))

    def sums_now(self, day):
        """The sums of a day made from its raw events now (the sign-ups are the counter's)."""
        return sums.sums_of_day(day, self.rows_of(day), self.counter(day, "signups"))

    def sums_of(self, day, today):
        """The sums of a day for a report: today's are made now; a finished day's are the kept ones, made first when they are not yet. {} for a day with nothing."""
        if day >= today:
            return self.sums_now(day)
        row = self.db.execute("SELECT sums FROM days WHERE day = ?", (day,)).fetchone()
        if row is not None:
            return json.loads(row[0])
        if self.db.execute("SELECT 1 FROM events WHERE day = ? LIMIT 1", (day,)).fetchone() is None and not self.counter(day, "signups"):
            return {}
        return self.keep(day)

    def keep(self, day):
        """Make and keep the sums of a finished day, for good."""
        made = self.sums_now(day)
        self.db.execute("INSERT OR REPLACE INTO days (day, sums) VALUES (?, ?)", (day, json.dumps(made, separators=(",", ":"), sort_keys=True)))
        return made

    def maintain(self, today):
        """The hour's work: the sums of every finished day that has raw events and none kept are made, and the raw events older than RAW_DAYS days go (with their pages overwritten and the log cut).
        Returns the number of events deleted."""
        for (day,) in self.db.execute("SELECT day FROM events WHERE day < ? AND day NOT IN (SELECT day FROM days) UNION SELECT day FROM counters WHERE name = 'signups' AND day < ? AND day NOT IN (SELECT day FROM days)", (today, today)).fetchall():
            self.keep(day)
        oldest = events.add_days(today, -RAW_DAYS)
        gone = self.db.execute("DELETE FROM events WHERE day < ?", (oldest,)).rowcount
        self.db.execute("DELETE FROM counters WHERE day < ?", (oldest,))
        if gone:
            self.cut_log()
        return gone

    def make_room(self, today, target):
        """The file holds more than `target` bytes of pages that hold something: the oldest raw day goes (its sums are kept first, so nothing but its events is lost), and the next, while it does, but
        never yesterday's or today's. Returns the number of events deleted."""
        gone = 0
        while self.used_bytes() > target:
            oldest = self.oldest_event_day()
            if oldest is None or oldest >= events.add_days(today, -1):
                break
            if self.db.execute("SELECT 1 FROM days WHERE day = ?", (oldest,)).fetchone() is None:
                self.keep(oldest)
            gone += self.db.execute("DELETE FROM events WHERE day = ?", (oldest,)).rowcount
            self.cut_log()
        return gone

    def oldest_event_day(self):
        row = self.db.execute("SELECT MIN(day) FROM events").fetchone()
        return row[0] if row else None

    def first_day(self):
        row = self.db.execute("SELECT MIN(day) FROM (SELECT day FROM days UNION SELECT day FROM events)").fetchone()
        return row[0] if row else None
