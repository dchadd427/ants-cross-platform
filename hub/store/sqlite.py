"""hub.db: the chat lines kept for moderation, who is muted or banned, the logins, and who each account is (its profile) (docs/HUB.md, "Data").

Everything is keyed by account id, so a mute or a ban survives a restart, a new session and a rename. The chat process and `python -m hub.admin` open the same file (WAL, so they
do not block each other). A line is kept `retention_days` (7) days, and only while it is among the newest `max_lines` (200) lines (every line counts, a moderator's and a deleted one too), and then deleted for good: whichever limit is smaller deletes first. The delete zeroes the freed pages (secure_delete) and the log file is cut. A line that a
moderator deletes stays in the file, marked, until its time is up or it is no longer among the newest 200; the hub learns of a deletion that the command line made by reading `del_seq`. When the file comes near the size limit the hub makes
room: `make_room` deletes the oldest lines, and the pages they free are reused by the lines that follow, so the file stops growing.
"""
import contextlib
import functools
import os
import sqlite3
import time

from .base import MAX_LINES, PICKS, PICTURE_DAYS, PICTURE_NAME, PROFILE_DAYS, PUBLIC, SHOW_BLOCKED, SHOW_OFF, SHOW_ON, Action, Line, Profile, StoreBase, StoreError, Usage, check_note
from ..pseudo import reference

SCHEMA = 4
PICTURE_MARKS = 100000                              # the rows of a person's `off` that are kept in all (the ones written longest ago go first)
TRIM_ROWS = 8                                       # the most lines that a new line takes out: one is the rule, a few make up for a limit that was lowered


def refusal_as_store_error(method, errors=sqlite3.OperationalError):
    """The contract's error: a locked file or a full disk (sqlite3.OperationalError) reaches the caller as StoreError. Any other error is not the store being busy and passes as it is."""
    @functools.wraps(method)
    def mapped(*args, **kwargs):
        try:
            return method(*args, **kwargs)
        except errors as e:
            raise StoreError("the database refused it") from e
    return mapped


class Store(StoreBase):
    log_max_bytes = 16 * 1024 * 1024                # a reader that holds hub.db (a backup tool, a shell that was left open) keeps the log file from being cut, and every chunk that make_room deletes adds its pages to it: when the log is this long and cannot be cut, no more lines go

    def __init__(self, path, clock=time.time, retention_days=7, busy_ms=5000, max_lines=MAX_LINES):
        self.path = path
        self.busy_ms = busy_ms                      # how long a write waits for another process's write (the hub uses a short time: its one thread must not wait for seconds)
        self.clock = clock
        self.retention = retention_days * 86400
        self.max_lines = max_lines
        self.unsettled = False                      # True while the log file is to be cut: a reader held it, or a login ended (the hub's clock cuts it at its next look)
        new = not os.path.exists(path)
        self.db = sqlite3.connect(path, timeout=5.0, isolation_level=None)
        try:
            self.db.execute("PRAGMA journal_mode=WAL")
            self.db.execute("PRAGMA synchronous=NORMAL")
            self.db.execute("PRAGMA secure_delete=ON")
            self.db.execute("PRAGMA busy_timeout=%d" % self.busy_ms)
            if new:
                try:
                    os.chmod(path, 0o600)
                except OSError:
                    pass
            self.migrate()
        except BaseException:                       # (a file that is not a database, a newer schema: no connection is left open)
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
        if version < 3:
            self.db.executescript("""
                BEGIN;
                CREATE TABLE IF NOT EXISTS sessions (id TEXT PRIMARY KEY, account TEXT NOT NULL, name TEXT NOT NULL, created INTEGER NOT NULL, expires INTEGER NOT NULL);
                CREATE INDEX IF NOT EXISTS sessions_account ON sessions (account);
                CREATE INDEX IF NOT EXISTS sessions_expires ON sessions (expires);
                PRAGMA user_version = 3;
                COMMIT;""")
        if version < 4:
            self.db.executescript("""
                BEGIN;
                CREATE TABLE IF NOT EXISTS secrets (k TEXT PRIMARY KEY, v BLOB NOT NULL);
                CREATE TABLE IF NOT EXISTS modlog (id INTEGER PRIMARY KEY AUTOINCREMENT, at INTEGER NOT NULL, actor TEXT NOT NULL, action TEXT NOT NULL, target TEXT NOT NULL, detail TEXT NOT NULL DEFAULT '');
                CREATE INDEX IF NOT EXISTS modlog_at ON modlog (at);
                PRAGMA user_version = 4;
                COMMIT;""")
        self.db.execute("INSERT OR IGNORE INTO secrets (k, v) VALUES ('account', ?)", (os.urandom(32),))      # (the key of the pseudonymous references: made once, kept as long as the file)
        # (the profiles are an additive table: no version of the schema came with it, so a hub of an earlier version opens the same file and never looks at it; docs/HUB.md, "Data")
        if self.db.execute("SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'profiles'").fetchone() is None:
            self.db.executescript("""
                BEGIN;
                CREATE TABLE IF NOT EXISTS profiles (account TEXT PRIMARY KEY, username TEXT NOT NULL, display TEXT NOT NULL DEFAULT '', pick TEXT NOT NULL, updated INTEGER NOT NULL);
                CREATE INDEX IF NOT EXISTS profiles_username ON profiles (username);
                CREATE INDEX IF NOT EXISTS profiles_updated ON profiles (updated);
                COMMIT;""")

        # (the pictures are an additive table too, the same way: no version of the schema came with it, so an older hub never looks at it; docs/HUB.md, "Data")
        if self.db.execute("SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'pictures'").fetchone() is None:
            self.db.executescript("""
                BEGIN;
                CREATE TABLE IF NOT EXISTS pictures (ref TEXT PRIMARY KEY, show INTEGER NOT NULL DEFAULT 1, name TEXT UNIQUE, png BLOB, until INTEGER NOT NULL DEFAULT 0, at INTEGER NOT NULL);
                CREATE INDEX IF NOT EXISTS pictures_at ON pictures (at);
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
        mark = 2 if mod == 2 else 1 if mod else 0       # (the column `mod` holds the author's role: 0 none, 1 moderator, 2 administrator; an earlier hub reads any of them as true, so a rollback shows the line as a moderator's: no schema step)
        with self.transaction():
            cur = self.db.execute("INSERT INTO lines (at, account, name, text, mod) VALUES (?, ?, ?, ?, ?)", (at, account, name, text, mark))
            over = self.db.execute("DELETE FROM lines WHERE id IN (SELECT id FROM lines WHERE id <= (SELECT id FROM lines ORDER BY id DESC LIMIT 1 OFFSET ?) ORDER BY id LIMIT ?)",
                                   (self.max_lines, TRIM_ROWS)).rowcount      # (a few rows: a file that holds a long backlog is cut down by the purge, a chunk at a time, never by one chat line)
        if over:
            self.unsettled = True                   # (the lines past the newest max_lines are in the log file until it is cut: the hub's clock cuts it at its next look)
        return Line(cur.lastrowid, at, account, name, text, mark)

    def recent_lines(self, count):
        """The last `count` lines that are not deleted, not past their time and among the newest `max_lines` kept, oldest first."""
        rows = self.db.execute("SELECT id, at, account, name, text, mod FROM lines WHERE deleted = 0 AND at >= ? AND id > COALESCE((SELECT id FROM lines ORDER BY id DESC LIMIT 1 OFFSET ?), 0) "
                               "ORDER BY id DESC LIMIT ?", (int(self.clock()) - self.retention, self.max_lines, count)).fetchall()
        return [Line(*r) for r in reversed(rows)]

    def account_of_line(self, line_id):
        row = self.db.execute("SELECT account FROM lines WHERE id = ? AND deleted = 0", (line_id,)).fetchone()
        return row[0] if row else None

    def delete_line(self, line_id, by, note=None):
        """Mark a line deleted; False when there is no such line or it was deleted already. Every deletion gets the next number of a counter that nothing lowers (a purge of old
        lines does not), so the hub, which asks for the deletions after the last number it knows, never misses one."""
        with self.transaction():
            if self.db.execute("SELECT 1 FROM lines WHERE id = ? AND deleted = 0", (line_id,)).fetchone() is None:
                return False
            self.db.execute("UPDATE meta SET v = v + 1 WHERE k = 'del_seq'")
            seq = self.last_deletion()
            self.db.execute("UPDATE lines SET deleted = 1, del_seq = ?, del_by = ? WHERE id = ?", (seq, by, line_id))
            self.write_note(note)
            return True

    def deletions_since(self, seq):
        """[(del_seq, id)] of the lines deleted after `seq`, in order."""
        return self.db.execute("SELECT del_seq, id FROM lines WHERE del_seq > ? ORDER BY del_seq", (seq,)).fetchall()

    def last_deletion(self):
        return self.db.execute("SELECT v FROM meta WHERE k = 'del_seq'").fetchone()[0]

    def last_name(self, account):
        """The name the account last spoke under (the one that was shown then), or ''."""
        row = self.db.execute("SELECT name FROM lines WHERE account = ? ORDER BY id DESC LIMIT 1", (account,)).fetchone()
        return row[0] if row else ""

    def lines(self, count):
        """The newest `count` lines with their state, newest first (for the command line)."""
        return self.db.execute("SELECT id, at, account, name, text, deleted FROM lines ORDER BY id DESC LIMIT ?", (count,)).fetchall()

    def purge(self, chunk=2000):
        """Delete up to `chunk` lines that are past their time or past the newest `max_lines` (call again while it returns `chunk`: a long backlog is not one long wait), the mutes that are over, the logins that
        have run out and the profiles that no login has refreshed for PROFILE_DAYS (a login lasts 30 days, so such a profile belongs to nobody who is logged in), then cut the log file.
        Returns how many lines went."""
        now = int(self.clock())
        with self.transaction():
            gone = self.db.execute("DELETE FROM lines WHERE id IN (SELECT id FROM lines WHERE at < ? LIMIT ?)", (now - self.retention, chunk)).rowcount
            if gone < chunk:                        # (then the lines past the newest max_lines: add_line keeps them away, so these are a file that held more before)
                gone += self.db.execute("DELETE FROM lines WHERE id IN (SELECT id FROM lines WHERE id <= (SELECT id FROM lines ORDER BY id DESC LIMIT 1 OFFSET ?) LIMIT ?)",
                                        (self.max_lines, chunk - gone)).rowcount
            self.db.execute("DELETE FROM mutes WHERE until <= ?", (now,))
            ended = [r[0] for r in self.db.execute("SELECT DISTINCT account FROM sessions WHERE expires <= ?", (now,))]
            self.db.execute("DELETE FROM sessions WHERE expires <= ?", (now,))
            self.drop_pictures_without_login(ended, now)
            self.db.execute("DELETE FROM profiles WHERE updated < ?", (now - PROFILE_DAYS * 86400,))
            gone_pictures = self.db.execute("DELETE FROM pictures WHERE (show = ? AND until <= ?) OR (show = ? AND at < ?)", (SHOW_ON, now, SHOW_OFF, now - PICTURE_DAYS * 86400)).rowcount
            if gone_pictures:
                self.count_picture_change()
            self.db.execute("DELETE FROM modlog WHERE at < ?", (now - int(self.modlog_days * 86400),))
            self.db.execute("DELETE FROM modlog WHERE id IN (SELECT id FROM modlog ORDER BY id DESC LIMIT -1 OFFSET ?)", (self.max_modlog,))
        self.checkpoint()
        return gone

    def checkpoint(self):
        """Cut the log file, so that what was deleted is in neither file. False when a reader holds it (`unsettled` says so until a later call succeeds)."""
        self.db.execute("PRAGMA busy_timeout=0")                # (the hub's one thread must not wait for a reader here: a later call tries again)
        try:
            busy = self.db.execute("PRAGMA wal_checkpoint(TRUNCATE)").fetchone()[0]
        finally:
            self.db.execute("PRAGMA busy_timeout=%d" % self.busy_ms)
        self.unsettled = bool(busy)
        return not busy

    def used_bytes(self):
        """The pages of hub.db that hold something. A page that deleted lines freed is room for new ones, though the file never shrinks, so the file's own size is not what is counted (a database that
        was once full would stay full for ever)."""
        pages = self.db.execute("PRAGMA page_count").fetchone()[0]
        free = self.db.execute("PRAGMA freelist_count").fetchone()[0]
        page_size = self.db.execute("PRAGMA page_size").fetchone()[0]
        return (pages - free) * page_size

    def log_bytes(self):
        """The size of the log file (hub.db-wal)."""
        try:
            return os.path.getsize(self.path + "-wal")
        except OSError:
            return 0

    def size(self):
        """Bytes in use: the pages of hub.db that hold something, and the log file."""
        return self.used_bytes() + self.log_bytes()

    def make_room(self, target, chunk=2000, keep=1000):
        """The oldest lines go first, so that the pages they leave are whole and the lines that follow use them: the lines are in the order of their numbers, and so are their time and their
        pages. The moderators' lines and the lines that a moderator deleted (they are kept for moderation) go last, and the newest `keep` never (the screens and the hub's memory hold fewer). The log file is cut at once, so that the lines are in neither file (a reader
        that holds it up leaves them there until a later look: `unsettled`; while it does and the log file is over `log_max_bytes`, no line is deleted: nothing would cut what a chunk adds to it)."""
        chunk, keep = max(1, int(chunk)), max(0, int(keep))
        if self.size() <= target:
            return 0
        cut = self.checkpoint()                    # (what is left of the log file is not lines to delete: cut it, and look at the pages that hold something)
        if not cut and self.log_bytes() > self.log_max_bytes:
            return 0                               # (a reader holds the log file and it is long: a chunk would add its pages to a file that cannot be cut; `unsettled` says so, and the hub's clock asks again)
        if self.used_bytes() <= target:
            return 0
        edge = self.db.execute("SELECT id FROM lines ORDER BY id DESC LIMIT 1 OFFSET ?", (keep,)).fetchone()
        if edge is None:
            return 0
        gone = 0
        with self.transaction():
            for kept in (0, 1):                      # (first the lines that nobody had a reason to keep, then the moderators' and the ones that a moderator deleted)
                gone += self.db.execute("DELETE FROM lines WHERE id IN (SELECT id FROM lines WHERE id <= ? AND (mod OR deleted) = ? ORDER BY id LIMIT ?)", (edge[0], kept, chunk - gone)).rowcount
                if gone >= chunk:
                    break
        if gone:
            try:
                self.checkpoint()
            except StoreError:            # (the lines are gone and are told: the cut is tried again by the hub's clock)
                self.unsettled = True
        return gone

    def usage(self):
        lines, oldest = self.db.execute("SELECT COUNT(*), COALESCE(MIN(at), 0) FROM lines").fetchone()
        free = self.db.execute("PRAGMA freelist_count").fetchone()[0] * self.db.execute("PRAGMA page_size").fetchone()[0]
        return Usage(self.used_bytes(), free, lines, oldest, self.log_bytes())

    # ---- logins ---------------------------------------------------------------------------------------------------------------------------

    def add_session(self, key, account, name, seconds, keep, total):
        """Keep a login: `key` is the SHA-256 (hex) of the cookie's value, never the value; it runs out after `seconds`. An account keeps at most `keep` of them, and all accounts together
        `total`: the oldest go."""
        now = int(self.clock())
        with self.transaction():
            self.db.execute("INSERT INTO sessions (id, account, name, created, expires) VALUES (?, ?, ?, ?, ?)", (key, account, name, now, now + int(seconds)))
            over = self.db.execute("DELETE FROM sessions WHERE account = ? AND id NOT IN (SELECT id FROM sessions WHERE account = ? ORDER BY created DESC, rowid DESC LIMIT ?)", (account, account, keep)).rowcount
            cut = self.db.execute("SELECT rowid, account FROM sessions ORDER BY created DESC, rowid DESC LIMIT -1 OFFSET ?", (total,)).fetchall()
            self.db.executemany("DELETE FROM sessions WHERE rowid = ?", [(r[0],) for r in cut])
            over += len(cut)
            self.drop_pictures_without_login({r[1] for r in cut}, now)
        if over:
            self.unsettled = True                   # (the logins that went over the caps are in the log file until it is cut)

    def session(self, key):
        """(account, name, expires) of the login whose key this is, or None when there is none or it has run out."""
        row = self.db.execute("SELECT account, name, expires FROM sessions WHERE id = ? AND expires > ?", (key, int(self.clock()))).fetchone()
        return tuple(row) if row else None

    def delete_session(self, key):
        """End one login. What was deleted is still in the log file until it is cut: `unsettled` asks the hub's clock to cut it at its next look (within ten seconds, and many logins ending
        at once cost one cut), so that the hash of the login does not stay in a file for the hour that is the pause between two purges."""
        with self.transaction():
            row = self.db.execute("SELECT account FROM sessions WHERE id = ?", (key,)).fetchone()
            gone = self.db.execute("DELETE FROM sessions WHERE id = ?", (key,)).rowcount == 1
            if row is not None:
                self.drop_pictures_without_login([row[0]], int(self.clock()))          # (the account's last login: its picture goes with it)
        self.unsettled = True
        return gone

    def end_sessions(self, account, note=None):
        """End every login of the account: what was deleted is in the log file until the hub's clock cuts it (`unsettled`), as for one login."""
        with self.transaction():
            gone = self.db.execute("DELETE FROM sessions WHERE account = ?", (account,)).rowcount
            self.drop_picture(self.ref(account))                        # (a picture lives as long as a login: the account's copy goes with its logins)
            self.write_note(note)
        self.unsettled = True
        return gone

    def forget(self, account, since=0):
        """Everything that ties this account's id or name to the site, except what keeps the others safe: all its logins, its profile and all its chat lines go, and the name is wiped from its
        mute and ban (they stay, by id: forgetting must not lift one). Returns the ids of the lines that went and that a screen may still show: the lines that were not deleted, and the ones that a
        moderator deleted from the command line after the deletion numbered `since` (the hub's last look: it did not tell the screens yet). A line that was deleted and announced before is on
        no screen, and must not use up the few frames that a bulk drop is allowed (hub/chat.py drop_lines). The log file is cut at once, so that neither the lines, nor the name, nor the hashes
        of the logins are left in it (a reader that holds the file up leaves them there until a later look: `unsettled`)."""
        with self.transaction():
            ids = [r[0] for r in self.db.execute("SELECT id FROM lines WHERE account = ? AND (deleted = 0 OR del_seq > ?)", (account, int(since)))]
            self.db.execute("DELETE FROM sessions WHERE account = ?", (account,))
            self.db.execute("DELETE FROM profiles WHERE account = ?", (account,))
            if self.db.execute("DELETE FROM pictures WHERE ref = ? AND show != ?", (self.ref(account), SHOW_BLOCKED)).rowcount:      # (a block stays, as a ban does, and holds no picture)
                self.count_picture_change()
            self.db.execute("DELETE FROM lines WHERE account = ?", (account,))
            self.db.execute("UPDATE mutes SET name = '' WHERE account = ?", (account,))
            self.db.execute("UPDATE bans SET name = '' WHERE account = ?", (account,))
        try:
            self.checkpoint()
        except StoreError:            # (the forgetting is done and is told: the cut is tried again by the hub's clock)
            self.unsettled = True
        return ids

    # ---- profiles -------------------------------------------------------------------------------------------------------------------------

    def set_profile(self, account, username, display, total):
        """What a login of the account just said: its username and its display name ('' for none that may be shown). The pick stays; a new profile, and one that had no display name and now
        has one, picks the display name. At most `total` profiles are kept (the ones refreshed longest ago go, as the logins of `add_session` do), and what went stays in the log file only
        until the hub's clock cuts it (`unsettled`)."""
        now = int(self.clock())
        with self.transaction():
            self.db.execute("INSERT INTO profiles (account, username, display, pick, updated) VALUES (?, ?, ?, ?, ?) ON CONFLICT (account) DO UPDATE SET username = excluded.username, "
                            "display = excluded.display, updated = excluded.updated, pick = CASE WHEN profiles.display = '' AND excluded.display != '' THEN 'display' ELSE profiles.pick END",
                            (account, username, display, "display" if display else "username", now))
            over = self.db.execute("DELETE FROM profiles WHERE rowid IN (SELECT rowid FROM profiles ORDER BY updated DESC, rowid DESC LIMIT -1 OFFSET ?)", (total,)).rowcount
        self.unsettled = self.unsettled or over > 0

    def profile(self, account):
        row = self.db.execute("SELECT username, display, pick FROM profiles WHERE account = ?", (account,)).fetchone()
        return Profile(*row) if row else None

    def set_pick(self, account, pick):
        if pick not in PICKS:
            return False
        with self.transaction():
            return self.db.execute("UPDATE profiles SET pick = ? WHERE account = ? AND display != ''", (pick, account)).rowcount == 1

    def usernames_by_reference(self, references):
        wanted = set(references)
        secret = self.secret()
        return {ref: username for ref, username in ((reference(secret, account), username) for account, username in self.db.execute("SELECT account, username FROM profiles")) if ref in wanted}

    def account_of_reference(self, ref):
        secret = self.secret()
        found = {account for (account,) in self.db.execute("SELECT account FROM profiles UNION SELECT account FROM lines UNION SELECT account FROM bans UNION SELECT account FROM mutes "
                                                          "UNION SELECT account FROM sessions") if reference(secret, account) == ref}
        return next(iter(found)) if len(found) == 1 else None

    def account_of_username(self, username):
        rows = self.db.execute("SELECT account FROM profiles WHERE username = ? LIMIT 2", (username,)).fetchall()
        return rows[0][0] if len(rows) == 1 else None

    # ---- pictures -------------------------------------------------------------------------------------------------------------------------

    max_picture_marks = PICTURE_MARKS

    def ref(self, account):
        return reference(self.secret(), account)

    def count_picture_change(self):
        """Inside a transaction that changed a picture, its switch or its name: one more on the counter that last_picture_change() reads (a row of `meta`, as the bans' is)."""
        self.db.execute("INSERT OR IGNORE INTO meta (k, v) VALUES ('pic_seq', 0)")
        self.db.execute("UPDATE meta SET v = v + 1 WHERE k = 'pic_seq'")

    def drop_picture(self, ref):
        """Inside a transaction: the picture and the name of the account with this reference are deleted (the row of a switch stays, the row of a default is deleted). True when there was a picture."""
        row = self.db.execute("SELECT show, png IS NOT NULL FROM pictures WHERE ref = ?", (ref,)).fetchone()
        if row is None:
            return False
        if row[0] == SHOW_ON:
            self.db.execute("DELETE FROM pictures WHERE ref = ?", (ref,))
        else:
            self.db.execute("UPDATE pictures SET name = NULL, png = NULL, until = 0 WHERE ref = ?", (ref,))
        if row[1]:
            self.count_picture_change()
            self.unsettled = True                     # (the bytes that went are in the log file until the hub's clock cuts it)
        return bool(row[1])

    def drop_pictures_without_login(self, accounts, now):
        """Inside a transaction that ended logins: the picture of each of these accounts goes when it has no login left (no login, no picture), and when one is left the picture's time is cut to the
        latest end of the logins that are left (never made later), so that it is never served after the account's last login has run out."""
        for account in accounts:
            live = self.db.execute("SELECT MAX(expires) FROM sessions WHERE account = ? AND expires > ?", (account, now)).fetchone()[0]
            if live is None:
                self.drop_picture(self.ref(account))
            elif self.db.execute("UPDATE pictures SET until = ? WHERE ref = ? AND until > ?", (live, self.ref(account), live)).rowcount:
                self.count_picture_change()

    def set_picture(self, account, png, name, until, rows_max, bytes_max):
        if not (isinstance(png, bytes) and 0 < len(png) <= bytes_max and isinstance(name, str) and PICTURE_NAME.match(name)):
            return False
        ref, now = self.ref(account), int(self.clock())
        with self.transaction():
            row = self.db.execute("SELECT show FROM pictures WHERE ref = ?", (ref,)).fetchone()
            if row is not None and row[0] != SHOW_ON:
                return False
            live = self.db.execute("SELECT MAX(expires) FROM sessions WHERE account = ? AND expires > ?", (account, now)).fetchone()[0]
            if live is None or self.db.execute("SELECT 1 FROM bans WHERE account = ?", (account,)).fetchone() is not None:
                return False                                           # (no login, or a ban: a fetch that ended after Forget me, the last log out, a kick or a ban keeps nothing)
            until = min(int(until), live)                              # (a picture never outlives the last login of its account)
            self.db.execute("INSERT INTO pictures (ref, show, name, png, until, at) VALUES (?, 1, ?, ?, ?, ?) ON CONFLICT (ref) DO UPDATE SET name = excluded.name, png = excluded.png, until = excluded.until, at = excluded.at",
                            (ref, name, png, until, now))
            while True:
                rows, size = self.db.execute("SELECT COUNT(*), COALESCE(SUM(LENGTH(png)), 0) FROM pictures WHERE png IS NOT NULL").fetchone()
                if rows <= rows_max and size <= bytes_max:
                    break
                if not self.db.execute("DELETE FROM pictures WHERE ref IN (SELECT ref FROM pictures WHERE png IS NOT NULL AND ref != ? ORDER BY at, rowid LIMIT 1)", (ref,)).rowcount:
                    break
            self.count_picture_change()
        self.unsettled = True
        return True

    def clear_picture(self, account):
        with self.transaction():
            return self.drop_picture(self.ref(account))

    def set_picture_show(self, account, show, operator=False):
        if show not in (SHOW_ON, SHOW_OFF, SHOW_BLOCKED):
            raise ValueError("not a state of a picture")
        ref, now = self.ref(account), int(self.clock())
        with self.transaction():
            row = self.db.execute("SELECT show FROM pictures WHERE ref = ?", (ref,)).fetchone()
            now_in = row[0] if row else SHOW_ON
            if now_in == SHOW_BLOCKED and show != SHOW_BLOCKED and not operator:
                return SHOW_BLOCKED
            if show == now_in:
                return show
            self.drop_picture(ref)
            if show == SHOW_ON:
                self.db.execute("DELETE FROM pictures WHERE ref = ?", (ref,))
            else:
                self.db.execute("INSERT INTO pictures (ref, show, name, png, until, at) VALUES (?, ?, NULL, NULL, 0, ?) ON CONFLICT (ref) DO UPDATE SET show = excluded.show, at = excluded.at", (ref, show, now))
                self.db.execute("DELETE FROM pictures WHERE rowid IN (SELECT rowid FROM pictures WHERE show = ? ORDER BY at DESC, rowid DESC LIMIT -1 OFFSET ?)", (SHOW_OFF, self.max_picture_marks))
            self.count_picture_change()
            return show

    def picture_state(self, account, touch=False):
        ref = self.ref(account)
        row = self.db.execute("SELECT show FROM pictures WHERE ref = ?", (ref,)).fetchone()
        if row is None:
            return SHOW_ON
        if touch and row[0] == SHOW_OFF:
            with self.transaction():
                self.db.execute("UPDATE pictures SET at = ? WHERE ref = ?", (int(self.clock()), ref))
        return row[0]

    def has_picture(self, account):
        return self.db.execute("SELECT 1 FROM pictures WHERE ref = ? AND png IS NOT NULL", (self.ref(account),)).fetchone() is not None

    def picture(self, name):
        if not isinstance(name, str) or not PICTURE_NAME.match(name):
            return None
        row = self.db.execute("SELECT png FROM pictures WHERE name = ? AND show = ? AND until > ? AND png IS NOT NULL", (name, SHOW_ON, int(self.clock()))).fetchone()
        return bytes(row[0]) if row else None

    def picture_names(self):
        return {ref: name for ref, name in self.db.execute("SELECT ref, name FROM pictures WHERE show = ? AND until > ? AND png IS NOT NULL", (SHOW_ON, int(self.clock())))}

    def last_picture_change(self):
        row = self.db.execute("SELECT v FROM meta WHERE k = 'pic_seq'").fetchone()
        return row[0] if row else 0

    def picture_usage(self):
        return tuple(self.db.execute("SELECT COUNT(*), COALESCE(SUM(LENGTH(png)), 0) FROM pictures WHERE png IS NOT NULL").fetchone())

    def logins(self, account):
        return self.db.execute("SELECT COUNT(*) FROM sessions WHERE account = ? AND expires > ?", (account, int(self.clock()))).fetchone()[0]

    # ---- mutes and bans -------------------------------------------------------------------------------------------------------------------

    def mute(self, account, minutes, by, name="", note=None):
        """Mute for `minutes` from now (0 lifts the mute). Returns the time it ends, or 0."""
        with self.transaction():
            self.count_restriction()
            if minutes <= 0:
                if self.db.execute("DELETE FROM mutes WHERE account = ?", (account,)).rowcount == 1:
                    self.write_note(note)
                return 0
            now = int(self.clock())
            until = now + int(minutes) * 60
            self.db.execute("INSERT OR REPLACE INTO mutes (account, until, at, by, name) VALUES (?, ?, ?, ?, ?)", (account, until, now, by, name))
            self.write_note(note)
            return until

    def muted_until(self, account):
        """When the account's mute ends, or 0 when it is not muted now."""
        row = self.db.execute("SELECT until FROM mutes WHERE account = ?", (account,)).fetchone()
        return row[0] if row and row[0] > self.clock() else 0

    def ban(self, account, by, name="", note=None):
        with self.transaction():
            self.count_restriction()
            self.db.execute("INSERT OR REPLACE INTO bans (account, at, by, name) VALUES (?, ?, ?, ?)", (account, int(self.clock()), by, name))
            self.drop_picture(self.ref(account))                    # (a banned account is a guest in the list: it shows no picture, and none is fetched while the ban stands)
            self.write_note(note)

    def unban(self, account, note=None):
        with self.transaction():
            self.count_restriction()
            done = self.db.execute("DELETE FROM bans WHERE account = ?", (account,)).rowcount == 1
            if done:
                self.write_note(note)
            return done

    def is_banned(self, account):
        return self.db.execute("SELECT 1 FROM bans WHERE account = ?", (account,)).fetchone() is not None

    def mutes(self):
        return self.db.execute("SELECT account, name, until, by FROM mutes WHERE until > ? ORDER BY until", (int(self.clock()),)).fetchall()

    def bans(self):
        return self.db.execute("SELECT account, name, at, by FROM bans ORDER BY at").fetchall()

    # ---- the hub's secret and the moderators' log ----------------------------------------------------------------------------------------

    def secret(self):
        return bytes(self.db.execute("SELECT v FROM secrets WHERE k = 'account'").fetchone()[0])

    def write_note(self, note):
        """Inside the transaction of an action: the row of the moderators' log that says it was done (nothing for no note), and the rows past the newest `max_modlog` out (a few at a time, as a line does)."""
        if note is None:
            return
        actor, action, target, detail = check_note(note)
        self.db.execute("INSERT INTO modlog (at, actor, action, target, detail) VALUES (?, ?, ?, ?, ?)", (int(self.clock()), actor, action, target, detail))
        self.db.execute("DELETE FROM modlog WHERE id IN (SELECT id FROM modlog WHERE id <= (SELECT id FROM modlog ORDER BY id DESC LIMIT 1 OFFSET ?) ORDER BY id LIMIT ?)", (self.max_modlog, TRIM_ROWS))

    def actions(self, since=0, limit=1000):
        rows = self.db.execute("SELECT id, at, actor, action, target, detail FROM modlog WHERE at >= ? ORDER BY id DESC LIMIT ?", (int(since), int(limit))).fetchall()
        return [Action(*r) for r in rows]

    def count_restriction(self):
        """Inside the transaction of a ban, an unban or a mute: one more on the counter that last_restriction() reads (a row of `meta`, as the deletions' is), so that every process that reads
        it knows that the bans or the mutes changed."""
        self.db.execute("INSERT OR IGNORE INTO meta (k, v) VALUES ('restrict_seq', 0)")
        self.db.execute("UPDATE meta SET v = v + 1 WHERE k = 'restrict_seq'")

    def last_restriction(self):
        row = self.db.execute("SELECT v FROM meta WHERE k = 'restrict_seq'").fetchone()
        return row[0] if row else 0


for _name in PUBLIC + ("checkpoint",):          # (every operation of the contract, and the one that only this file has, speaks StoreError, whatever the file does)
    setattr(Store, _name, refusal_as_store_error(getattr(Store, _name)))
Store.__init__ = refusal_as_store_error(Store.__init__, sqlite3.Error)         # (a file that cannot be opened: any error of the library)
