#!/usr/bin/env python3
"""The chat store prunes old messages to stay inside its size limit (hub/store/sqlite.py `make_room`, hub/server.py, hub/limits.py, hub/chat.py, hub/admin.py; docs/HUB.md "Data", "Limits", "Moderation").

When hub.db and its log file come near `db_max_bytes`, the hub's clock deletes the oldest lines (the moderators' lines last, the newest 1,000 never) until the pages that hold something are down to
80% of the limit. The freed pages are reused, so the file stops growing; mutes, bans, the counter of deletions and the logins are never touched. An account has a budget of lines for an hour and for
a day (`quota`), which keeps the share of one account in the store small. `python -m hub.admin status` shows how full the database is, and `purge` frees space on demand.

The classes below are listed here because docs/TESTING.md has no room for them. The hub is driven without sockets (fake peers, two clocks that the test moves), except where a class says so.
  TheStoreAtItsLimit              the store filled up to its size guard, at a small limit and at the default 64 MB: the guard answers `rate` as documented, then the hub's clock makes room and
                                  the chat takes lines again at the next tick; the newest lines, the mutes, the bans, the deletion counter and the logins stay.
"""
import dataclasses
import io
import json
import os
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
from hub import config as configmod, limits as limitmod, log as logmod, server, session, store as storemod  # noqa: E402
from test_hub_core import Clock, FakePeer  # noqa: E402  (the fake peer and the clock that every chat test of the hub uses)

SITE = "https://chat.example.org"
SMALL = dict(db_max_bytes=1_000_000, purge_chunk=300)           # (a store of 1 MB and a tick that deletes 300 lines at most, so that a test fills it in no time; the proportions are the default's)

FILL = """WITH RECURSIVE n(i) AS (SELECT 0 UNION ALL SELECT i + 1 FROM n WHERE i < ?)
          INSERT INTO lines (at, account, name, text, mod)
          SELECT ? + i / ?, '100' || (i % ?), 'user' || (i % ?), printf('%07d ', ? + i) || ?, 0 FROM n"""


def fill(store, lines, accounts=10, per_second=5, length=150, serial=0):
    """`lines` chat lines of `length` characters from `accounts` accounts in turn (ids 1000, 1001, ...), `per_second` of them for each second of the store's clock, which moves on by that long:
    what a busy chat leaves in the database, made in one statement so that a test can fill a store of 64 MB in a moment."""
    start = int(store.clock())
    with store.transaction():
        store.db.execute(FILL, (lines - 1, start, per_second, accounts, accounts, serial, "x" * length))
    store.clock.now += max(1, lines // per_second)


def fill_to(store, limit, batch=50, **kw):
    """Add lines until the store holds `limit` bytes with its log file cut (that is what the guard sees once the hub has cut it): big steps while it is far from it, steps of `batch` lines near it,
    so that it ends just over it. Returns how many lines were added."""
    serial = 0
    while True:
        n = max(batch, min(20000, (limit - store.size()) // 400))
        fill(store, n, serial=serial, **kw)
        serial += n
        store.checkpoint()
        if store.size() >= limit:
            return serial


class Room:
    """A hub without sockets: the store in a temporary folder, one clock for the lines (`wall`) and one for the limits (`mono`), the hub's clock work in `tick()`, and `person()`s that say things."""

    def __init__(self, mods=("9001",), busy_ms=300, **limits):
        self.folder = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.folder.name, "hub.db")
        self.wall, self.mono = Clock(1_800_000_000.0), Clock(1000.0)
        self.limits = dataclasses.replace(limitmod.Limits(), **limits)
        self.store = storemod.Store(self.path, clock=self.wall, busy_ms=busy_ms)
        if self.limits.db_max_bytes < 16 * 1024 * 1024:
            self.store.db.execute("PRAGMA wal_autocheckpoint=20")           # (the log file is folded into the database every 20 pages, as it is every 1000 at the default size: the proportions of a small store are the default's)
        self.log = io.StringIO()
        self.hub = server.Hub(configmod.load({"ANTS_HUB_PUBLIC_URL": SITE, "ANTS_HUB_MODS": ",".join(mods)}), self.store, session.NoLogin(), logmod.Log(self.log), self.limits, clock=self.mono)
        self.chat = self.hub.chat

    def close(self):
        self.store.close()
        self.folder.cleanup()

    def person(self, n, name=None):
        """An account that has said nothing yet (ids 2001, 2002, ...: not one of the accounts that `fill` makes)."""
        return FakePeer(session.Identity(str(2000 + n), name or "visitor%d" % n, False))

    def say(self, peer, text):
        self.chat.on_text(peer, json.dumps({"t": "say", "text": text}))

    def tick(self, seconds=1.0):
        """One second of the hub's clock work (the lines and the limits see the same time)."""
        self.wall.now += seconds
        self.mono.now += seconds
        self.hub.on_tick(self.mono.now)


class TheStoreAtItsLimit(unittest.TestCase):
    def check_the_store_makes_room(self, room, batch, ticks_to_resume, ticks_to_settle):
        limit = room.limits.db_max_bytes
        fill_to(room.store, limit, batch=batch)
        self.assertGreaterEqual(room.store.size(), limit)
        people = [room.person(n) for n in range(1, 6)]
        for n, person in enumerate(people):
            room.say(person, "hello from %d" % n)
        self.assertEqual([p.codes() for p in people], [["rate"]] * 5)                  # the guard answers rate to each account, and no line is kept
        self.assertIsNone(room.store.account_of_name("visitor1"))
        room.store.mute("1000", 60, "cli", "user0")
        room.store.ban("1001", "cli", "user1")
        room.store.add_session("a" * 64, "1002", "user2", 3600, 10, 100000)
        for line in room.store.lines(300):
            room.store.delete_line(line[0], "cli")                                      # a deletion only marks a line, and no line is 7 days old yet
        room.store.checkpoint()
        self.assertEqual(room.store.purge(), 0)
        self.assertGreaterEqual(room.store.size(), limit)
        newest = [row[0] for row in room.store.lines(100)]
        # the hub's clock makes room
        for second in range(ticks_to_resume):
            room.tick()
        self.assertLess(room.store.size(), limit, "the store is still full after %d seconds" % ticks_to_resume)
        for n, person in enumerate(people):
            room.say(person, "hello again from %d" % n)
        self.assertEqual([p.codes() for p in people], [["rate"]] * 5, "one refusal each, from before: the second try was taken")
        self.assertEqual([l.text for l in room.store.recent_lines(5)], ["hello again from %d" % n for n in range(5)])
        for second in range(ticks_to_settle):
            room.tick()
        self.assertLessEqual(room.store.size(), room.limits.prune_to_bytes)
        self.assertEqual([row[4] for row in room.store.lines(5)], ["hello again from %d" % n for n in (4, 3, 2, 1, 0)])
        self.assertTrue(set(newest[:50]).issubset({row[0] for row in room.store.lines(10 ** 6)}), "the newest lines are all still there")
        self.assertGreater(room.store.muted_until("1000"), 0)                           # moderation and the logins were not touched
        self.assertTrue(room.store.is_banned("1001"))
        self.assertEqual(room.store.session("a" * 64)[0], "1002")
        self.assertEqual(room.store.last_deletion(), 300)

    def test_a_store_full_of_lines_makes_room_by_itself_and_the_chat_goes_on(self):
        room = Room(**SMALL)
        self.addCleanup(room.close)
        self.check_the_store_makes_room(room, batch=50, ticks_to_resume=3, ticks_to_settle=15)

    def test_the_same_at_the_default_size_of_64_mb(self):
        room = Room()
        self.addCleanup(room.close)
        self.assertEqual(room.limits.db_max_bytes, 64 * 1024 * 1024)
        self.check_the_store_makes_room(room, batch=50, ticks_to_resume=3, ticks_to_settle=40)


if __name__ == "__main__":
    unittest.main()
