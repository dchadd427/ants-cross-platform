"""hub.db: the chat lines kept for moderation, and who is muted or banned (docs/HUB.md, "Data").

Everything is keyed by account id, so a mute or a ban survives a restart, a new session and a rename. The chat process and `python -m hub.admin` open the same file (WAL, so they
do not block each other). A line is kept `retention_days` (7) and then deleted for good: the delete zeroes the freed pages (secure_delete) and the log file is cut. A line that a
moderator deletes stays in the file, marked, until its time is up; the hub learns of a deletion that the command line made by reading `del_seq`.
"""
import collections
import contextlib
import os
import sqlite3
import time

Line = collections.namedtuple("Line", "id at account name text mod")
SCHEMA = 2


class Store:
    def __init__(self, path, clock=time.time, retention_days=7):
        self.path = path
        self.clock = clock
        self.retention = retention_days * 86400
        self.unsettled = False                      # True while the log file could not be cut because a reader held it
        new = not os.path.exists(path)
        self.db = sqlite3.connect(path, timeout=5.0, isolation_level=None)
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute("PRAGMA synchronous=NORMAL")
        self.db.execute("PRAGMA secure_delete=ON")
        self.db.execute("PRAGMA busy_timeout=5000")
        if new:
            try:
                os.chmod(path, 0o600)
            except OSError:
                pass
        try:
            self.migrate()
        except BaseException:
            self.db.close()
            raise

    def migrate(self):
        version = self.db.execute("PRAGMA user_version").fetchone()[0]
        if version > SCHEMA:
            raise RuntimeError("hub.db is from a newer hub (schema %d)" % version)
        if version < 1:
            self.db.executescript("""
                BEGIN;
                CREATE TABLE IF NOT EXISTS lines (id INTEGER PRIMARY KEY AUTOINCREMENT, at INTEGER NOT NULL, account TEXT NOT NULL, name TEXT NOT NULL, text TEXT NOT NULL,
                                                  mod INTEGER NOT NULL DEFAULT 0, deleted INTEGER NOT NULL DEFAULT 0, del_seq INTEGER NOT NULL DEFAULT 0, del_by TEXT);
                CREATE INDEX IF NOT EXISTS lines_at ON lines (at);
                CREATE INDEX IF NOT EXISTS lines_name ON lines (name);
                CREATE INDEX IF NOT EXISTS lines_del_seq ON lines (del_seq);
                CREATE TABLE IF NOT EXISTS mutes (account TEXT PRIMARY KEY, until INTEGER NOT NULL, at INTEGER NOT NULL, by TEXT NOT NULL, name TEXT NOT NULL);
                CREATE TABLE IF NOT EXISTS bans (account TEXT PRIMARY KEY, at INTEGER NOT NULL, by TEXT NOT NULL, name TEXT NOT NULL);
                PRAGMA user_version = 1;
                COMMIT;""")
        if version < 2:
            self.db.executescript("""
                BEGIN;
                CREATE TABLE IF NOT EXISTS meta (k TEXT PRIMARY KEY, v INTEGER NOT NULL);
                INSERT OR IGNORE INTO meta (k, v) SELECT 'del_seq', COALESCE(MAX(del_seq), 0) FROM lines;
                PRAGMA user_version = 2;
                COMMIT;""")

    def close(self):
        self.db.close()

    @contextlib.contextmanager
    def transaction(self):
        """One write at a time across processes (BEGIN IMMEDIATE), committed on success and undone on an error."""
        self.db.execute("BEGIN IMMEDIATE")
        try:
            yield
        except BaseException:
            self.db.execute("ROLLBACK")
            raise
        self.db.execute("COMMIT")

    # ---- lines ----------------------------------------------------------------------------------------------------------------------------

    def add_line(self, account, name, text, mod):
        at = int(self.clock())
        cur = self.db.execute("INSERT INTO lines (at, account, name, text, mod) VALUES (?, ?, ?, ?, ?)", (at, account, name, text, 1 if mod else 0))
        return Line(cur.lastrowid, at, account, name, text, 1 if mod else 0)

    def recent_lines(self, count):
        """The last `count` lines that are not deleted and not past their time, oldest first."""
        rows = self.db.execute("SELECT id, at, account, name, text, mod FROM lines WHERE deleted = 0 AND at >= ? ORDER BY id DESC LIMIT ?",
                               (int(self.clock()) - self.retention, count)).fetchall()
        return [Line(*r) for r in reversed(rows)]

    def delete_line(self, line_id, by):
        """Mark a line deleted; False when there is no such line or it was deleted already. Every deletion gets the next number of a counter that nothing lowers (a purge of old
        lines does not), so the hub, which asks for the deletions after the last number it knows, never misses one."""
        with self.transaction():
            if self.db.execute("SELECT 1 FROM lines WHERE id = ? AND deleted = 0", (line_id,)).fetchone() is None:
                return False
            self.db.execute("UPDATE meta SET v = v + 1 WHERE k = 'del_seq'")
            seq = self.last_deletion()
            self.db.execute("UPDATE lines SET deleted = 1, del_seq = ?, del_by = ? WHERE id = ?", (seq, by, line_id))
            return True

    def deletions_since(self, seq):
        """[(del_seq, id)] of the lines deleted after `seq`, in order."""
        return self.db.execute("SELECT del_seq, id FROM lines WHERE del_seq > ? ORDER BY del_seq", (seq,)).fetchall()

    def last_deletion(self):
        return self.db.execute("SELECT v FROM meta WHERE k = 'del_seq'").fetchone()[0]

    def account_of_name(self, name):
        """The account that last spoke under this name (names are unique among live accounts, but a rename frees one), or None."""
        row = self.db.execute("SELECT account FROM lines WHERE name = ? ORDER BY id DESC LIMIT 1", (name,)).fetchone()
        return row[0] if row else None

    def last_name(self, account):
        row = self.db.execute("SELECT name FROM lines WHERE account = ? ORDER BY id DESC LIMIT 1", (account,)).fetchone()
        return row[0] if row else ""

    def lines(self, count):
        """The newest `count` lines with their state, newest first (for the command line)."""
        return self.db.execute("SELECT id, at, account, name, text, deleted FROM lines ORDER BY id DESC LIMIT ?", (count,)).fetchall()

    def purge(self, chunk=2000):
        """Delete up to `chunk` lines that are past their time (call again while it returns `chunk`: a long backlog is not one long wait) and the mutes that are over, then cut the log
        file. Returns how many lines went."""
        now = int(self.clock())
        with self.transaction():
            gone = self.db.execute("DELETE FROM lines WHERE id IN (SELECT id FROM lines WHERE at < ? LIMIT ?)", (now - self.retention, chunk)).rowcount
            self.db.execute("DELETE FROM mutes WHERE until <= ?", (now,))
        self.checkpoint()
        return gone

    def checkpoint(self):
        """Cut the log file, so that what was deleted is in neither file. False when a reader holds it (`unsettled` says so until a later call succeeds)."""
        self.db.execute("PRAGMA busy_timeout=0")                # (the hub's one thread must not wait for a reader here: a later call tries again)
        try:
            busy = self.db.execute("PRAGMA wal_checkpoint(TRUNCATE)").fetchone()[0]
        finally:
            self.db.execute("PRAGMA busy_timeout=5000")
        self.unsettled = bool(busy)
        return not busy

    def size(self):
        """Bytes of hub.db and its log file."""
        total = 0
        for path in (self.path, self.path + "-wal"):
            try:
                total += os.path.getsize(path)
            except OSError:
                pass
        return total

    # ---- mutes and bans -------------------------------------------------------------------------------------------------------------------

    def mute(self, account, minutes, by, name=""):
        """Mute for `minutes` from now (0 lifts the mute). Returns the time it ends, or 0."""
        with self.transaction():
            if minutes <= 0:
                self.db.execute("DELETE FROM mutes WHERE account = ?", (account,))
                return 0
            now = int(self.clock())
            until = now + int(minutes) * 60
            self.db.execute("INSERT OR REPLACE INTO mutes (account, until, at, by, name) VALUES (?, ?, ?, ?, ?)", (account, until, now, by, name))
            return until

    def muted_until(self, account):
        """When the account's mute ends, or 0 when it is not muted now."""
        row = self.db.execute("SELECT until FROM mutes WHERE account = ?", (account,)).fetchone()
        return row[0] if row and row[0] > self.clock() else 0

    def ban(self, account, by, name=""):
        with self.transaction():
            self.db.execute("INSERT OR REPLACE INTO bans (account, at, by, name) VALUES (?, ?, ?, ?)", (account, int(self.clock()), by, name))

    def unban(self, account):
        with self.transaction():
            return self.db.execute("DELETE FROM bans WHERE account = ?", (account,)).rowcount == 1

    def is_banned(self, account):
        return self.db.execute("SELECT 1 FROM bans WHERE account = ?", (account,)).fetchone() is not None

    def mutes(self):
        return self.db.execute("SELECT account, name, until, by FROM mutes WHERE until > ? ORDER BY until", (int(self.clock()),)).fetchall()

    def bans(self):
        return self.db.execute("SELECT account, name, at, by FROM bans ORDER BY at").fetchall()
