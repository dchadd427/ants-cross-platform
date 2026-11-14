#!/usr/bin/env python3
"""The chat store prunes old messages to stay inside its size limit (hub/store/sqlite.py `make_room`, hub/server.py, hub/limits.py, hub/chat.py, hub/admin.py; docs/HUB.md "Data", "Limits", "Moderation").

When hub.db and its log file come near `db_max_bytes`, the hub's clock deletes the oldest lines (the moderators' lines and the lines that a moderator deleted last, the newest 1,000 never) until the pages that hold something are down to
80% of the limit. The freed pages are reused, so the file stops growing; mutes, bans, the counter of deletions and the logins are never touched. An account has a budget of lines for an hour and for
a day (`quota`), which keeps the share of one account in the store small. `python -m hub.admin status` shows how full the database is, and `purge` frees space on demand.

The classes below are listed here because docs/TESTING.md has no room for them. The hub is driven without sockets (fake peers, two clocks that the test moves), except where a class says so. What every
backend of the store must do (order, chunk, the newest lines, the moderators' and the deleted lines, what is not touched, `usage`) is in tests/scripts/test_hub_store_contract.py. The broken copies that
these tests must notice are tests/data/hub_room_mutants.json (`python3 tools/mutate.py tests/data/hub_room_mutants.json --keep-going --jobs 1`).
  TheStoreAtItsLimit              the store filled up to its size guard, at a small limit and at the default 64 MB: the guard answers `rate` as documented, then the hub's clock makes room and
                                  the chat takes lines again at the next tick; the newest lines, the mutes, the bans, the deletion counter and the logins stay.
  TheRoomInTheFile                what only the SQLite file has: the pages that the oldest lines leave are free and whole, the file stops growing, the target ends a call within a chunk, the bytes
                                  decide and not the number of lines, the text of a deleted line is in neither file (also when a reader holds the log file), a locked database, a store at its target.
  TheReaderThatHoldsTheDatabase   a program that has hub.db open for reading keeps the log file from being cut: while the log file is short the lines go, when it is long they stop and the log file stops
                                  growing, and when the reader lets go they go on; the length that is too long is 16 MB.
  TheBudgetsOfAnAccount           the hour's and the day's budget of an account (fills up steadily, a refused line costs nothing, a refund, the memory that stays small).
  TheChatTellsQuota               the code `quota`: the line is not kept, it is no mistake of the socket and costs it a frame; a moderator's command is not held back; the other codes stay as they were.
  TheBusiestChat                  twelve accounts that say a line every 2 seconds for an hour and twenty quiet ones: the store stays inside its limit, nobody quiet is refused.
  TheGuardAtItsEdges              exactly at the limit and one byte under it, a line of the largest size, lines that are deleted by the bytes.
  TheHubsClock                    the marks (90% and 80%), a chunk a second, a database that refuses (a minute, not every tick), a store where no line may go (said once a minute), a reader that holds
                                  the database (named in the log, asked again in 10 seconds), a log file that is over the mark by itself, the time of a tick (the work, with 5,000 names).
  TheStaffTool                    `hub.admin status` and `purge` (the lower mark, --keep-lines, what a screen may show, a refusal, a pause after each chunk with no lock held, a cut that is refused for a
                                  moment and one that a reader holds, the status line with the log file on its own, the hub going on during and after).
  TheToolAndTheHubWriteAtTheSameTime   the command line's purge in a thread and the hub's clock and chat in this one: the file stays whole, the numbers of the lines go up, what stands is what the hub took.
  TheHubOnRealSockets             `quota` on the wire (the socket stays), and a running hub on a full store takes a line again.
  TheDocumentsSayWhatTheCodeDoes  docs/HUB.md and the page's word agree with the numbers of hub/limits.py.
"""
import collections
import contextlib
import dataclasses
import io
import json
import os
import re
import sqlite3
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
import hub_client as hc  # noqa: E402
import hub_rig  # noqa: E402
from hub import admin, config as configmod, limits as limitmod, log as logmod, server, session, store as storemod  # noqa: E402
from test_hub_core import Clock, FakePeer  # noqa: E402  (the fake peer and the clock that every chat test of the hub uses)

SITE = "https://chat.example.org"
LOOSE = dict(burst=1000, refill_seconds=0.001, frame_burst=1000, frame_refill_seconds=0.001, global_burst=1000, global_refill_seconds=0.001)       # (the pace of the sockets and of the lines out of the way)
CLI = dict(db_max_bytes=2 * 1024 * 1024, purge_chunk=300)       # (the limits that the tool's tests give to the command line: a store of 2 MB, so that they do not fill 64 MB)
SMALL = dict(db_max_bytes=1_000_000, purge_chunk=300)           # (a store of 1 MB and a tick that deletes 300 lines at most, so that a test fills it in no time; the proportions are the default's)

FILL = """WITH RECURSIVE n(i) AS (SELECT 0 UNION ALL SELECT i + 1 FROM n WHERE i < ?)
          INSERT INTO lines (at, account, name, text, mod)
          SELECT ? + i / ?, '100' || (i % ?), 'user' || (i % ?), printf('%07d ', ? + i) || ?, 0 FROM n"""


def fill(store, lines, accounts=10, per_second=5, length=150, serial=0):
    """`lines` chat lines of `length` characters from `accounts` accounts in turn (ids 1000, 1001, ... and the names user0, user1, ...), `per_second` of them for each second of the store's clock, which
    moves on by that long: what a busy chat leaves in the database, made in one statement so that a test can fill a store of 64 MB in a moment. With few accounts the oldest lines of a chunk lie
    together in the index of names; with thousands (5,000 in the tests that time a chunk or count the pages that it touches) they lie all over it, and a chunk costs several times more."""
    start = int(store.clock())
    with store.transaction():
        store.db.execute(FILL, (lines - 1, start, per_second, accounts, accounts, serial, "x" * length))
    store.clock.now += max(1, lines // per_second)


def fill_to(store, limit, batch=50, **kw):
    """Add lines until the store holds `limit` bytes with its log file cut (that is what the guard sees once the hub has cut it): big steps while it is far from it, steps of `batch` lines near it,
    so that it ends just over it. Returns how many lines were added."""
    serial, per_line = 0, kw.get("length", 150) + 100
    while True:
        n = max(batch, min(20000, (limit - store.size()) // (2 * per_line)))
        fill(store, n, serial=serial, **kw)
        serial += n
        store.checkpoint()
        if store.size() >= limit:
            return serial


def reader_holding(test, path):
    """A program that has the database open for reading (a backup tool, a shell that was left open): its read stays open, and the log file cannot be cut while it does. `COMMIT` lets it go."""
    reader = sqlite3.connect(path, isolation_level=None)
    test.addCleanup(reader.close)
    reader.execute("BEGIN")
    reader.execute("SELECT COUNT(*) FROM lines").fetchone()
    return reader


def answer(client):
    """What the hub says to a line: its `line` or its `err` (the online count and the pings are not an answer)."""
    while True:
        message = client.recv_json(5)
        if message["t"] in ("line", "err"):
            return message


class Room:
    """A hub without sockets: the store in a temporary folder, one clock for the lines (`wall`) and one for the limits (`mono`), the hub's clock work in `tick()`, and `person()`s that say things."""

    def __init__(self, mods=("9001",), busy_ms=300, max_lines=10 ** 9, **limits):
        self.folder = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.folder.name, "hub.db")
        self.wall, self.mono = Clock(1_800_000_000.0), Clock(1000.0)
        self.limits = dataclasses.replace(limitmod.Limits(), **limits)
        self.store = storemod.Store(self.path, clock=self.wall, busy_ms=busy_ms, max_lines=max_lines)
        self.store.db.execute("PRAGMA synchronous=OFF")                         # (a test has no use for the disk's promise, and a machine that is busy takes milliseconds for each)
        if self.limits.db_max_bytes < 16 * 1024 * 1024:
            self.store.db.execute("PRAGMA wal_autocheckpoint=4")            # (the log file is folded into the database every 4 pages, as it is every 1000 at the default size: next to a small store it stays as small as the default's is next to 64 MB)
        self.log = io.StringIO()
        self.hub = server.Hub(configmod.load({"ANTS_HUB_PUBLIC_URL": SITE}), self.store, session.NoLogin(), logmod.Log(self.log), self.limits, clock=self.mono, roles=hub_rig.FakeRoles({a: "mod" for a in mods}))
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
        self.assertEqual(room.store.last_name("2001"), "")                              # (and the first of them has no line under any name)
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
        self.assertEqual([p.codes() for p in people], [["rate"]] * 5, "one refusal each, from before the clock made room; the second try was taken")
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


class TheNewest200Lines(unittest.TestCase):
    """The store keeps the newest 200 lines (and none past 7 days): what the hub, the clock and the tool see of it."""

    def test_the_programs_store_keeps_200_and_the_hub_and_the_tool_open_it_so(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        store = storemod.Store(os.path.join(folder.name, "hub.db"))
        self.addCleanup(store.close)
        self.assertEqual(store.max_lines, 200)
        self.assertEqual(store.retention, 7 * 86400)
        with open(os.path.join(REPO, "hub", "server.py"), encoding="utf-8") as f:
            self.assertIn("storemod.Store(cfg.db, busy_ms=300)", f.read(), "the hub opens the store with the defaults: 200 lines, 7 days")

    def test_a_chat_that_goes_on_keeps_200_lines_and_the_page_still_gets_its_last_50(self):
        room = Room(max_lines=200, **LOOSE)
        self.addCleanup(room.close)
        people = [room.person(n) for n in range(1, 4)]
        for n in range(330):
            room.say(people[n % 3], "line %d" % n)
            room.tick(0.01)
        rows = room.store.lines(10 ** 6)
        self.assertEqual(len(rows), 200)
        self.assertEqual([r[4] for r in rows][::-1], ["line %d" % n for n in range(130, 330)])
        self.assertEqual([l.text for l in room.chat.recent()], ["line %d" % n for n in range(280, 330)])
        self.assertEqual([l.text for l in room.store.recent_lines(50)], ["line %d" % n for n in range(280, 330)])

    def test_the_hourly_purge_cuts_a_store_that_holds_more_down_to_200_and_says_how_many(self):
        room = Room(max_lines=10 ** 9, **LOOSE)
        self.addCleanup(room.close)
        fill(room.store, 500)
        room.store.max_lines = 200                      # (a file that an earlier program wrote: more than the newest 200)
        self.assertEqual(room.store.usage().lines, 500)
        room.tick(room.limits.purge_seconds)
        self.assertEqual(room.store.usage().lines, 200)
        self.assertIn("purge lines=300", room.log.getvalue())
        self.assertEqual([r[0] for r in room.store.lines(1000)], list(range(500, 300, -1)))

    def test_an_old_backlog_is_not_deleted_by_the_first_line_and_the_clock_cuts_it_down_a_chunk_at_a_time(self):
        room = Room(max_lines=10 ** 9, purge_chunk=500, **LOOSE)                  # (a file of the earlier program: 3,000 lines)
        self.addCleanup(room.close)
        fill(room.store, 3000)
        room.store.max_lines = 200                                              # (the limit of the program: 200, as the hub opens the store)
        speaker = room.person(1)
        room.say(speaker, "the first line after the upgrade")
        self.assertEqual(speaker.codes(), [])
        self.assertGreater(room.store.usage().lines, 2900, "one chat line took out a few lines, not the backlog")
        counts = []
        for _ in range(40):
            room.tick()
            counts.append(room.store.usage().lines)
            if counts[-1] == 200:
                break
        self.assertEqual(counts[-1], 200)
        self.assertGreater(len(counts), 3, "a chunk of 500 at a time: several ticks")
        self.assertEqual(room.store.lines(1)[0][4], "the first line after the upgrade")

    def test_a_line_that_went_past_the_newest_200_is_in_neither_file_once_the_log_is_cut_and_the_hub_cuts_it_by_itself(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        path = os.path.join(folder.name, "hub.db")
        wall = Clock(1_800_000_000.0)
        store = storemod.Store(path, clock=wall, busy_ms=50, max_lines=3)
        self.addCleanup(store.close)
        store.add_line("1", "ann", "a words-that-must-go-1 line", False)
        for n in range(3):
            store.add_line("1", "ann", "later %d" % n, False)
        self.assertTrue(store.unsettled, "the line that went over is in the log file: the hub's clock is asked to cut it")
        self.assertTrue(store.checkpoint())
        for name in sorted(os.listdir(folder.name)):
            with open(os.path.join(folder.name, name), "rb") as f:
                self.assertNotIn(b"words-that-must-go-1", f.read(), name)

    def test_the_moderators_command_still_deletes_a_line_that_is_on_the_page_and_counts_the_deletion(self):
        room = Room(max_lines=200, **LOOSE)
        self.addCleanup(room.close)
        moderator = FakePeer(session.Identity("9001", "moddy", False))
        for n in range(230):
            room.store.add_line("2001", "visitor1", "line %d" % n, False)
        newest = room.store.lines(1)[0][0]
        room.say(moderator, "/del %d" % newest)
        self.assertEqual(room.store.last_deletion(), 1)
        self.assertEqual(room.store.usage().lines, 200)
        self.assertEqual(moderator.codes(), [])
        self.assertEqual(room.store.deletions_since(0)[0][1], newest)
        room.tick(room.limits.deletions_seconds)
        self.assertEqual(room.store.last_deletion(), 1)


class StoreTest(unittest.TestCase):
    """A store in a temporary folder, with a clock that the test moves."""

    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.path = os.path.join(self.folder.name, "hub.db")
        self.wall = Clock(1_800_000_000.0)
        self.store = storemod.Store(self.path, clock=self.wall, busy_ms=50, max_lines=10 ** 9)
        self.store.db.execute("PRAGMA synchronous=OFF")
        self.addCleanup(self.store.close)

    def in_files(self, text):
        """The files of the database's folder (the database and its log file) that hold `text`."""
        found = []
        for name in sorted(os.listdir(self.folder.name)):
            with open(os.path.join(self.folder.name, name), "rb") as f:
                if text.encode() in f.read():
                    found.append(name)
        return found


class TheRoomInTheFile(StoreTest):
    def test_the_pages_that_the_oldest_lines_leave_are_whole_and_free_to_reuse(self):
        self.store.checkpoint()
        empty = self.store.size()
        fill(self.store, 4000)
        self.store.checkpoint()
        before = self.store.usage()
        self.assertEqual(before.free, 0)
        self.assertEqual(self.store.make_room(0, 2000, 100), 2000)
        after = self.store.usage()
        half = (before.bytes - empty) / 2                                              # (what half of the lines took)
        self.assertGreater(before.bytes - after.bytes, 0.8 * half)                     # almost all of it is free now ...
        self.assertGreater(after.free, 0.8 * half)                                     # ... and is told as room to reuse
        page_size = self.store.db.execute("PRAGMA page_size").fetchone()[0]
        self.assertEqual(after.free, self.store.db.execute("PRAGMA freelist_count").fetchone()[0] * page_size)

    def test_the_file_stops_growing_when_the_oldest_lines_go_as_new_ones_come(self):
        high, low = 540_000, 480_000
        sizes = []
        for cycle in range(100):
            fill(self.store, 100)                                                      # (24 KB a time, 10,000 lines in all)
            self.store.checkpoint()
            while self.store.size() >= high:
                self.assertGreater(self.store.make_room(low, 100, 100), 0)
            sizes.append(os.path.getsize(self.path))
        self.assertLessEqual(max(sizes[40:]), max(sizes[:40]) + 4 * 4096)              # it stays where it had reached
        self.assertLess(max(sizes), 640_000)
        self.assertLess(self.store.usage().lines, 3000)

    def test_the_target_ends_the_deletion_within_a_chunk(self):
        fill(self.store, 3000)
        self.store.checkpoint()
        target = int(self.store.used_bytes() * 0.7)
        calls = 0
        while self.store.make_room(target, 100, 10):
            calls += 1
        self.assertLessEqual(self.store.used_bytes(), target)
        self.assertGreater(self.store.used_bytes(), target - 100 * 400)                # (at most a chunk of lines below it)
        self.assertGreaterEqual(calls, 5)

    def test_the_bytes_decide_and_not_the_number_of_lines(self):
        other = storemod.Store(os.path.join(self.folder.name, "long.db"), clock=self.wall)
        self.addCleanup(other.close)
        fill(self.store, 2000, length=20)                                              # (200 KB)
        fill(other, 2000, length=700)                                                  # (1.7 MB)
        for store in (self.store, other):
            store.checkpoint()
            while store.make_room(400_000, 100, 10):
                pass
        self.assertEqual(self.store.usage().lines, 2000)
        self.assertLessEqual(other.used_bytes(), 400_000)
        self.assertTrue(250 < other.usage().lines < 450, other.usage().lines)

    def test_the_text_of_a_line_that_went_is_in_neither_file(self):
        for cut_first in (False, True):                                                # (the text is in the log file only, or in the database too)
            canary = "canary-line-3c9e41-do-not-keep-%s" % cut_first
            self.store.add_line("1", "ann", canary, False)
            fill(self.store, 300)
            if cut_first:
                self.store.checkpoint()
            self.assertEqual(self.in_files(canary), ["hub.db"] if cut_first else ["hub.db-wal"])
            self.assertGreater(self.store.make_room(0, 1000, 10), 0)                  # (all but the newest 10, the line with the text among them)
            self.assertEqual(self.in_files(canary), [])
            self.assertFalse(self.store.unsettled)

    def test_a_reader_that_holds_the_log_file_keeps_the_text_there_until_it_lets_go(self):
        canary = "canary-line-77ab05-held-by-a-reader"
        self.store.add_line("1", "ann", canary, False)
        fill(self.store, 300)
        self.store.checkpoint()
        reader = sqlite3.connect(self.path, isolation_level=None)                      # (`hub.admin list` or a backup: a read that is open)
        reader.execute("BEGIN")
        self.assertEqual(reader.execute("SELECT COUNT(*) FROM lines").fetchone()[0], 301)
        started = time.monotonic()
        self.assertEqual(self.store.make_room(0, 100, 10), 100)
        self.assertLess(time.monotonic() - started, 1.0)                               # (the hub's one thread does not wait for a reader)
        self.assertTrue(self.store.unsettled)
        reader.execute("COMMIT")
        reader.close()
        self.assertTrue(self.store.checkpoint())
        self.assertEqual(self.in_files(canary), [])

    def test_a_cut_of_the_log_file_that_fails_after_the_lines_went_is_no_error(self):
        fill(self.store, 300)
        real, calls = self.store.checkpoint, []

        def cut():
            calls.append(1)
            if len(calls) > 1:
                raise storemod.StoreError("refused")
            return real()

        self.store.checkpoint = cut
        self.assertEqual(self.store.make_room(0, 50, 10), 50)                          # (the lines are gone, and are told)
        self.assertEqual(len(calls), 2)
        self.assertTrue(self.store.unsettled)

    def test_a_locked_database_refuses_and_deletes_nothing(self):
        fill(self.store, 300)
        self.store.checkpoint()
        blocker = sqlite3.connect(self.path, isolation_level=None)                     # (another process that holds the write lock)
        blocker.execute("BEGIN IMMEDIATE")
        started = time.monotonic()
        with self.assertRaises(storemod.StoreError):
            self.store.make_room(0, 100, 10)
        self.assertLess(time.monotonic() - started, 2.0)
        blocker.execute("ROLLBACK")
        blocker.close()
        self.assertEqual(self.store.usage().lines, 300)
        self.assertEqual(self.store.make_room(0, 100, 10), 100)

    def test_a_log_file_that_cannot_be_cut_does_not_make_the_store_delete_lines_that_are_not_over(self):
        fill(self.store, 300)
        self.store.checkpoint()
        reader = sqlite3.connect(self.path, isolation_level=None)
        reader.execute("BEGIN")
        reader.execute("SELECT COUNT(*) FROM lines").fetchone()
        for n in range(100):
            self.store.add_line("1", "ann", "x" * 150, False)                          # (the log file grows, and a reader holds it: it cannot be cut)
        self.assertGreater(self.store.size(), self.store.used_bytes() + 100_000)
        target = self.store.used_bytes() + 1000
        self.assertEqual(self.store.make_room(target, 100, 10), 0)                     # (the pages are under the target: lines are not what is over)
        self.assertEqual(self.store.usage().lines, 400)
        reader.execute("COMMIT")
        reader.close()

    def test_a_store_that_is_at_the_size_it_is_asked_for_is_not_touched(self):
        fill(self.store, 300)
        log_file = os.path.getsize(self.path + "-wal")
        self.assertGreater(log_file, 0)
        self.assertEqual(self.store.make_room(self.store.size(), 100, 10), 0)
        self.assertEqual(os.path.getsize(self.path + "-wal"), log_file)               # (nothing to do: the log file is not cut either)
        self.assertEqual(self.store.make_room(self.store.size() - 1, 100, 10), 0)       # (a byte over, and it is the log file's: the file is cut, and no line goes)
        self.assertEqual(os.path.getsize(self.path + "-wal"), 0)
        self.assertEqual(self.store.usage().lines, 300)

    def test_a_store_with_no_lines_or_with_the_newest_ones_only_does_nothing_and_takes_no_lock(self):
        blocker = sqlite3.connect(self.path, isolation_level=None)
        blocker.execute("BEGIN IMMEDIATE")
        self.assertEqual(self.store.make_room(0, 100, 10), 0)                          # (no line: nothing to ask the lock for)
        blocker.execute("ROLLBACK")
        blocker.close()
        fill(self.store, 50)
        blocker = sqlite3.connect(self.path, isolation_level=None)
        blocker.execute("BEGIN IMMEDIATE")
        self.assertEqual(self.store.make_room(0, 100, 50), 0)                          # (the 50 lines are the newest 50: none may go)
        blocker.execute("ROLLBACK")
        blocker.close()


class TheReaderThatHoldsTheDatabase(StoreTest):
    """A program that has hub.db open for reading keeps the log file from being cut, and a chunk of deleted lines adds its pages to it (some megabytes for 2,000 lines when the names are many): when the
    log file is long and cannot be cut, no more lines go, and the clock asks again until the reader lets go."""

    def test_the_log_file_does_not_grow_without_end_when_it_cannot_be_cut(self):
        fill(self.store, 30000, accounts=3000)                                         # (3,000 names: a chunk touches the index of names all over)
        self.store.checkpoint()
        reader = reader_holding(self, self.path)
        gone = [self.store.make_room(0, 500, 100)]
        first = self.store.log_bytes()
        self.assertEqual(gone, [500])
        self.assertGreater(first, 100_000)                                             # (a chunk of 500 lines adds this much to the log file, which cannot be cut)
        self.assertTrue(self.store.unsettled)
        self.store.log_max_bytes = int(first * 2.5)                                    # (a long log file, for the test: the program's is 16 MB)
        sizes = [first]
        for call in range(15):
            gone.append(self.store.make_room(0, 500, 100))
            sizes.append(self.store.log_bytes())
        stopped = gone.index(0)
        self.assertGreaterEqual(stopped, 3)                                            # (the lines went while the log file was short)
        self.assertEqual(gone[stopped:], [0] * (len(gone) - stopped))                  # (and when it was long, no more)
        self.assertEqual(len(set(sizes[stopped:])), 1)                                 # (nothing was written then)
        self.assertLessEqual(max(sizes), self.store.log_max_bytes + first)             # (never more than a long log file and a chunk)
        self.assertEqual(self.store.usage().lines, 30000 - 500 * stopped)
        self.assertTrue(self.store.unsettled)
        reader.execute("COMMIT")                                                       # (the reader lets go: the cut works, and the lines go on)
        self.assertEqual(self.store.make_room(0, 500, 100), 500)
        self.assertEqual(self.store.log_bytes(), 0)
        self.assertFalse(self.store.unsettled)

    def test_a_short_log_file_that_cannot_be_cut_does_not_stop_the_lines(self):
        fill(self.store, 3000, accounts=300)
        self.store.checkpoint()
        reader_holding(self, self.path)
        self.assertEqual(self.store.make_room(0, 500, 100), 500)                       # (the log file is short: the lines go, and the cut is tried again later)
        self.assertGreater(self.store.log_bytes(), 0)
        self.assertLess(self.store.log_bytes(), self.store.log_max_bytes)
        self.assertTrue(self.store.unsettled)

    def test_the_log_file_that_is_too_long_is_16_mb_unless_the_store_is_told_otherwise(self):
        self.assertEqual(storemod.Store.log_max_bytes, 16 * 1024 * 1024)
        other = storemod.Store(os.path.join(self.folder.name, "other.db"))
        self.addCleanup(other.close)
        self.assertEqual(other.log_max_bytes, 16 * 1024 * 1024)


class TheBudgetsOfAnAccount(unittest.TestCase):
    def budgets(self, **kw):
        """The limits of one account, with the pace of its lines out of the way (a burst of 1000), and the budgets as asked."""
        return limitmod.AccountLimits(dataclasses.replace(limitmod.Limits(), burst=1000, refill_seconds=0.001, **kw))

    def said(self, al, count, now, account="1001", prefix="line"):
        return [al.check(account, "%s %d at %d" % (prefix, n, now), now) for n in range(count)]

    def test_the_defaults_are_200_lines_an_hour_and_1000_a_day(self):
        defaults = limitmod.Limits()
        self.assertEqual((defaults.account_hour_lines, defaults.account_day_lines), (200, 1000))

    def test_the_budget_of_the_hour_fills_up_again_steadily(self):
        al = self.budgets(account_hour_lines=10, account_day_lines=1000)
        self.assertEqual(self.said(al, 12, 0.0), [None] * 10 + ["quota"] * 2)
        self.assertEqual(al.check("1001", "again", 359.0), "quota")                    # (one more line every 3600 / 10 = 360 seconds)
        self.assertIsNone(al.check("1001", "again", 360.0))
        self.assertEqual(al.check("1001", "and again", 360.0), "quota")
        self.assertIsNone(al.check("1001", "much later", 360.0 + 3600.0))

    def test_the_budget_of_the_day_holds_when_the_hour_has_lines_left(self):
        al = self.budgets(account_hour_lines=10, account_day_lines=25)
        self.assertEqual(self.said(al, 10, 0.0), [None] * 10)                          # (the hour is spent, the day has 15 left)
        self.assertEqual(self.said(al, 10, 3600.0, prefix="hour two"), [None] * 10)    # (the hour is full again; the day has 6 and a bit)
        self.assertEqual(self.said(al, 10, 7200.0, prefix="hour three"), [None] * 7 + ["quota"] * 3)      # (a bit more than 7: 25 / 24 a hour came back)

    def test_each_account_has_its_own_budget(self):
        al = self.budgets(account_hour_lines=3, account_day_lines=100)
        self.assertEqual(self.said(al, 4, 0.0, account="1001"), [None] * 3 + ["quota"])
        self.assertEqual(self.said(al, 4, 0.0, account="1002"), [None] * 3 + ["quota"])

    def test_a_line_over_the_budget_costs_nothing(self):
        al = self.budgets(account_hour_lines=10, account_day_lines=1000)
        self.said(al, 10, 0.0)
        self.assertEqual(self.said(al, 200, 100.0, prefix="over"), ["quota"] * 200)    # (200 tries, none of them is counted)
        self.assertEqual(self.said(al, 3, 360.0, prefix="later"), [None, "quota", "quota"])      # (so the line that is owed at 360 seconds is there)

    def test_the_same_text_twice_is_told_before_the_budget(self):
        al = self.budgets(account_hour_lines=2, account_day_lines=100)
        self.assertIsNone(al.check("1001", "first", 0.0))
        self.assertIsNone(al.check("1001", "second", 0.0))
        self.assertEqual(al.check("1001", "second", 1.0), "dup")
        self.assertEqual(al.check("1001", "third", 1.0), "quota")

    def test_a_refused_line_for_the_pace_costs_no_budget_and_the_budget_is_what_is_said(self):
        al = limitmod.AccountLimits(dataclasses.replace(limitmod.Limits(), burst=3, refill_seconds=2.0, account_hour_lines=5, account_day_lines=100))
        self.assertEqual([al.check("1001", "line %d" % n, 0.0) for n in range(5)], [None, None, None, "rate", "rate"])
        self.assertIsNone(al.check("1001", "line 5", 2.0))
        self.assertIsNone(al.check("1001", "line 6", 4.0))
        self.assertEqual(al.check("1001", "line 7", 6.0), "quota")                     # (five lines are said; the two tries that the pace refused used none of the five)

    def test_a_line_that_the_database_refused_gives_back_the_budget_too(self):
        al = self.budgets(account_hour_lines=3, account_day_lines=3)
        for n in range(20):
            self.assertIsNone(al.check("1001", "try %d" % n, 0.0))
            al.refund("1001", "try %d" % n)
        self.assertEqual(self.said(al, 4, 0.0, prefix="kept"), [None] * 3 + ["quota"])

    def test_a_budget_that_is_spent_is_remembered_and_one_that_is_full_again_is_forgotten(self):
        al = self.budgets(account_hour_lines=10, account_day_lines=1000)
        self.said(al, 10, 0.0)
        self.said(al, 1, 0.0, account="1002")
        al.forget_idle(400.0)
        self.assertEqual(sorted(al.budgets), ["1001"])                                 # (1002 has used one of 1000 and 10: it is full again by now ... 1001 has used all ten)
        self.assertEqual(al.check("1001", "still spent", 400.0), None)                 # (the hour has given 1 line back in 360 seconds)
        self.assertEqual(al.check("1001", "and now", 401.0), "quota")
        al.forget_idle(100000.0)
        self.assertEqual(al.budgets, {})

    def test_the_memory_of_the_budgets_stays_small_when_many_accounts_say_a_line_each(self):
        al = limitmod.AccountLimits(limitmod.Limits())
        biggest = 0
        for second in range(1200):                                                     # (20 minutes, 5 new accounts a second, one line each)
            for n in range(5):
                al.check(str(10 ** 6 + second * 5 + n), "hello", float(second))
            if second % 60 == 59:
                al.forget_idle(float(second))
                biggest = max(biggest, len(al.budgets))
        self.assertLess(biggest, 800)                                                  # (not the 6,000 accounts: the ones that spoke in the last two minutes)
        al.forget_idle(1200.0 + 400.0)
        self.assertEqual((len(al.budgets), len(al.accounts)), (0, 0))


class TheChatTellsQuota(unittest.TestCase):
    def setUp(self):
        self.room = Room(account_hour_lines=5, account_day_lines=100, **{k: v for k, v in LOOSE.items() if k in ("burst", "refill_seconds", "global_burst", "global_refill_seconds")})
        self.addCleanup(self.room.close)

    def test_the_line_over_the_budget_is_not_kept_and_is_no_mistake_of_the_socket(self):
        who = self.room.person(1)
        for n in range(5):
            self.room.say(who, "line %d" % n)
        self.assertEqual(who.codes(), [])
        self.room.say(who, "one too many")
        self.assertEqual(who.codes(), ["quota"])
        self.assertEqual((who.strikes, who.charges), (0, 1))                           # (no mistake; it costs the socket a frame of its budget, as a full database does)
        self.assertEqual(who.sent[-1], {"t": "err", "code": "quota"})
        self.assertEqual([r[4] for r in self.room.store.lines(10)][0], "line 4")
        self.room.mono.now += 3600 / 5 - 1
        self.room.say(who, "too early")
        self.assertEqual(who.codes(), ["quota", "quota"])
        self.room.mono.now += 1
        self.room.say(who, "after a while")
        self.assertEqual(who.codes(), ["quota", "quota"])
        self.assertEqual(self.room.store.lines(1)[0][4], "after a while")

    def test_a_moderators_command_is_not_held_back_by_the_budget_and_the_moderator_is_held_to_it_for_lines(self):
        mod = FakePeer(session.Identity("9001", "moddy", False))
        who = self.room.person(1)
        self.room.store.set_profile("2001", "visitor1", "", 100)                       # (what the login of the account made: a moderator names an account by its username)
        self.room.say(who, "something to mute for")
        for n in range(5):
            self.room.say(mod, "announcement %d" % n)
        self.room.say(mod, "a sixth")
        self.assertEqual(mod.codes(), ["quota"])
        self.room.say(mod, "/mute visitor1 5")
        self.assertEqual(mod.codes(), ["quota"])                                       # (the command is no line: it was done)
        self.assertGreater(self.room.store.muted_until("2001"), 0)

    def test_the_other_refusals_keep_their_codes(self):
        who = self.room.person(1)
        self.room.say(who, "same")
        self.room.say(who, "same")
        self.assertEqual(who.codes(), ["rate"])
        self.assertEqual(who.strikes, 1)                                               # (a repeated line is a mistake, as before)


class TheBusiestChat(unittest.TestCase):
    """Busy accounts say a line every 2 seconds for an hour, and quiet ones a line now and then: the store stays inside its limit, the quiet ones are never refused, the busy ones say what their budget allows."""

    def test_twelve_accounts_that_never_stop_do_not_fill_the_store_or_hold_up_the_others(self):
        room = Room(**SMALL)
        self.addCleanup(room.close)
        accepted = collections.Counter()
        real = room.store.add_line
        room.store.add_line = lambda account, name, text, mod: accepted.update([account]) or real(account, name, text, mod)
        busy = [room.person(n) for n in range(1, 13)]
        quiet = [room.person(100 + n) for n in range(20)]
        biggest = 0
        for step in range(1800):                                                       # (an hour)
            room.tick(2.0)
            for who in busy:
                room.say(who, "line %d from %s %s" % (step, who.identity.name, "and some more words " * 6))
            if step % 300 == 0:
                for who in quiet:
                    room.say(who, "a quiet word at %d" % step)
            biggest = max(biggest, room.store.size())
        self.assertLess(biggest, room.limits.db_max_bytes)                             # (the guard never had to refuse a line)
        self.assertEqual([who.codes() for who in quiet], [[]] * 20)
        for who in busy:
            lines = accepted[who.identity.account_id]
            self.assertTrue(370 <= lines <= 410, lines)                                # (the hour's 200 and a line every 18 seconds for an hour: 400, less what the limit of all accounts took)
            self.assertGreater(who.codes().count("quota"), 1000)
        self.assertEqual(sum(accepted.values()), sum(accepted[w.identity.account_id] for w in busy + quiet))
        self.assertIn("prune lines=", room.log.getvalue())                             # (more than the store holds: the oldest were deleted, not 21,600 lines kept)
        self.assertLess(room.store.usage().lines, sum(accepted.values()))


class TheGuardAtItsEdges(unittest.TestCase):
    def setUp(self):
        self.room = Room(**SMALL)
        self.addCleanup(self.room.close)

    def limit_at(self, bytes_):
        self.room.chat.limits = dataclasses.replace(self.room.limits, db_max_bytes=bytes_)

    def test_exactly_at_the_limit_is_full_and_one_byte_under_it_is_not(self):
        fill(self.room.store, 500)
        self.room.store.checkpoint()
        size = self.room.store.size()
        who = self.room.person(1)
        self.limit_at(size)
        self.room.say(who, "at the limit")
        self.assertEqual(who.codes(), ["rate"])
        self.limit_at(size + 1)
        self.room.say(who, "one byte under")
        self.assertEqual(who.codes(), ["rate"])                                        # (no new refusal)
        self.assertEqual(self.room.store.lines(1)[0][4], "one byte under")

    def test_a_line_of_the_largest_size_may_take_the_store_over_the_limit_and_the_next_waits_for_the_clock(self):
        fill(self.room.store, 500)
        self.room.store.checkpoint()
        who, other = self.room.person(1), self.room.person(2)
        largest = "\U0001F600" * 200                                                  # (200 code points, 800 bytes: what the text rules allow)
        self.assertEqual(len(largest.encode("utf-8")), 800)
        self.limit_at(self.room.store.size() + 1)
        self.room.say(who, largest)
        self.assertEqual(who.codes(), [])
        self.assertEqual(self.room.store.lines(1)[0][4], largest)
        self.assertLess(self.room.store.size() - self.room.chat.limits.db_max_bytes, 12 * 4096)       # (over by a few pages at the most)
        self.room.say(other, "the next one")
        self.assertEqual(other.codes(), ["rate"])
        self.room.chat.limits = self.room.limits
        fill(self.room.store, 2000)
        self.room.store.checkpoint()
        for second in range(20):
            self.room.tick()
        self.room.say(other, "and after the clock")
        self.assertEqual(other.codes(), ["rate"])
        self.assertEqual(self.room.store.lines(1)[0][4], "and after the clock")

    def test_lines_of_the_largest_size_fill_the_store_faster_and_are_deleted_by_the_bytes(self):
        room = Room(keep_lines=100, **SMALL)                                           # (1,000 lines of this size are the whole of a store of 1 MB)
        self.addCleanup(room.close)
        count = fill_to(room.store, room.limits.db_max_bytes, length=700)
        self.assertLess(count, 1500)                                                   # (a store of 1 MB holds about 1,200 lines of 700 characters, 4,500 of 150)
        for second in range(30):
            room.tick()
        self.assertLessEqual(room.store.size(), room.limits.prune_to_bytes)
        left = room.store.usage().lines
        self.assertTrue(700 < left < 1100, left)
        who = room.person(1)
        room.say(who, "hello")
        self.assertEqual(who.codes(), [])


class TheHubsClock(unittest.TestCase):
    def setUp(self):
        self.room = Room(**SMALL)
        self.addCleanup(self.room.close)
        self.calls = []
        real = self.room.store.make_room
        self.room.store.make_room = lambda *args: self.calls.append(args) or real(*args)

    def prune_lines(self):
        return [int(m) for m in re.findall(r"prune lines=(\d+)", self.room.log.getvalue())]

    def test_nothing_is_asked_of_a_store_below_the_mark(self):
        fill(self.room.store, 1000)
        for second in range(200):
            self.room.tick()
        self.assertEqual(self.calls, [])

    def test_from_the_mark_a_chunk_a_second_down_to_the_lower_mark_and_then_nothing_until_the_mark_again(self):
        limits = self.room.limits
        fill_to(self.room.store, limits.prune_from_bytes, batch=20)
        self.assertLess(self.room.store.size(), limits.db_max_bytes)
        self.room.tick()
        self.assertEqual(self.calls, [(limits.prune_to_bytes, 300, 1000)])
        for second in range(30):
            self.room.tick()
        self.assertLessEqual(self.room.store.used_bytes(), limits.prune_to_bytes)
        self.assertTrue(len(self.calls) < 12, len(self.calls))                         # (a chunk a tick, and then it stops)
        done = len(self.calls)
        fill_to(self.room.store, limits.prune_to_bytes + (limits.prune_from_bytes - limits.prune_to_bytes) // 2, batch=20)       # (half way between the marks)
        for second in range(20):
            self.room.tick()
        self.assertEqual(len(self.calls), done)                                        # (no new run: the mark is 90%)
        fill_to(self.room.store, limits.prune_from_bytes, batch=20)
        self.room.tick()
        self.assertEqual(len(self.calls), done + 1)
        self.assertTrue(all(0 < n <= 300 for n in self.prune_lines()), self.prune_lines())

    def test_a_database_that_refuses_is_tried_again_after_a_minute_and_not_at_every_tick(self):
        limits = self.room.limits
        self.room.tick()                                                               # (the first look at the age of the lines is done)
        fill_to(self.room.store, limits.prune_from_bytes, batch=20)
        blocker = sqlite3.connect(self.room.path, isolation_level=None)               # (another process that holds the write lock)
        self.addCleanup(blocker.close)
        blocker.execute("BEGIN IMMEDIATE")
        started = time.monotonic()
        with self.assertRaises(storemod.StoreError):
            self.room.tick()
        self.assertLess(time.monotonic() - started, 2.0)
        self.assertEqual(self.room.hub.next_room, self.room.mono.now + limits.purge_failure_seconds)
        for second in range(59):
            self.room.tick()
        self.assertEqual(len(self.calls), 1)                                           # (no more tries, no more waiting)
        blocker.execute("ROLLBACK")
        self.room.tick()
        self.assertEqual(len(self.calls), 2)
        self.assertEqual(len(self.prune_lines()), 1)

    def test_a_store_that_is_full_and_has_no_line_to_delete_is_said_in_the_log_once_a_minute(self):
        room = Room(keep_lines=10 ** 6, **SMALL)
        self.addCleanup(room.close)
        fill_to(room.store, room.limits.db_max_bytes, batch=20)
        for second in range(130):
            room.tick()
        said = room.log.getvalue().count("warning what=database_full_and_no_line_to_delete")
        self.assertTrue(2 <= said <= 3, said)                                          # (at the first tick and then every minute, not 130 times)
        who = room.person(1)
        room.say(who, "hello")
        self.assertEqual(who.codes(), ["rate"])                                        # (the guard still holds: it is the last resort)

    def test_a_tick_at_the_default_size_holds_the_loop_for_a_short_time(self):
        room = Room()
        self.addCleanup(room.close)
        fill_to(room.store, room.limits.db_max_bytes, batch=50, accounts=5000)         # (5,000 names: a chunk of 2,000 lines touches the index of names all over)
        room.tick()
        walls, works = [], []
        for second in range(40):
            started, work = time.perf_counter(), time.process_time()
            room.tick()
            walls.append(time.perf_counter() - started)
            works.append(time.process_time() - work)
        works.sort()
        self.assertLess(works[len(works) // 2], 0.2)                                   # (the work of a tick is some 10 milliseconds here, without the disk's promise, which adds 30 to 100 on a real volume: a slowdown of 30 times shows)
        self.assertLess(max(walls), 1.0)                                               # (and no tick holds the loop for a second, whatever else the machine is doing)
        self.assertLessEqual(room.store.size(), room.limits.prune_to_bytes)
        self.assertTrue(all(0 < n <= 2000 for n in self.prune_lines()))

    def test_a_log_file_that_is_over_the_mark_by_itself_is_cut_by_the_clock_and_the_chat_goes_on(self):
        room = self.room
        room.tick()                                                                    # (the first look at the age of the lines is done: that one cuts the log file, too)
        room.store.db.execute("PRAGMA wal_autocheckpoint=0")                           # (the log file is not folded into the database by itself)
        n = 0
        while room.store.size() < room.limits.db_max_bytes:
            room.store.add_line("1000", "user0", "line %d %s" % (n, "x" * 100), False)
            n += 1
        self.assertLess(room.store.used_bytes(), room.limits.prune_from_bytes)         # (a few pages of lines)
        self.assertGreater(room.store.log_bytes(), room.limits.prune_from_bytes - 40_000)       # (and a log file that is the whole of the store, but the pages of the tables that a new file has: the secret's, the moderators' log's and the pictures')
        who = room.person(1)
        room.say(who, "hello")
        self.assertEqual(who.codes(), ["rate"])
        room.tick()
        self.assertEqual(room.store.log_bytes(), 0)
        room.say(who, "hello again")
        self.assertEqual(who.codes(), ["rate"])                                        # (the first refusal only)
        self.assertEqual(room.store.lines(1)[0][4], "hello again")

    def test_a_reader_that_holds_the_database_is_named_in_the_log_and_the_log_file_stays_short(self):
        room, limits = self.room, self.room.limits
        room.tick()                                                                    # (the first look at the age of the lines is done)
        fill_to(room.store, limits.db_max_bytes, batch=20, accounts=500)
        reader = reader_holding(self, room.path)
        who = room.person(1)
        room.say(who, "hello")
        self.assertEqual(who.codes(), ["rate"])
        room.tick()                                                                    # (a first chunk goes: the log file is short)
        first = room.store.log_bytes()
        self.assertGreater(first, 0)
        room.store.log_max_bytes = int(first * 2.5)
        before = len(self.calls)
        for second in range(40):
            room.tick()
        asked = len(self.calls) - before
        log = room.log.getvalue()
        self.assertLessEqual(room.store.log_bytes(), room.store.log_max_bytes + first)
        self.assertIn("warning what=database_full_a_reader_holds_the_log_file", log)
        self.assertNotIn("no_line_to_delete", log)
        self.assertLessEqual(log.count("database_full_a_reader_holds_the_log_file"), 5)  # (once in 10 seconds at the most)
        self.assertLessEqual(asked, 8)                                                 # (the chunks that went while the log file was short, and then a try every 10 seconds)
        self.assertGreaterEqual(asked, 3)
        room.say(who, "still full")
        self.assertEqual(who.codes(), ["rate", "rate"])                                # (the guard holds: the log file counts)
        lines = room.store.usage().lines
        reader.execute("COMMIT")                                                       # (the reader lets go)
        for second in range(25):
            room.tick()
        self.assertEqual(room.store.log_bytes(), 0)                                    # (the clock cut the log file within its 10 seconds)
        self.assertFalse(room.store.unsettled)
        room.say(who, "and now")
        self.assertEqual(who.codes(), ["rate", "rate"])
        self.assertEqual(room.store.lines(1)[0][4], "and now")
        self.assertLess(room.store.usage().lines, lines)                               # (and the lines go on)

    def test_a_clock_that_finds_the_log_file_held_asks_again_in_ten_seconds_and_not_in_a_minute(self):
        room, limits = self.room, self.room.limits
        room.tick()
        fill_to(room.store, limits.db_max_bytes, batch=20, accounts=500)
        reader_holding(self, room.path)
        room.tick()
        room.store.log_max_bytes = 1                                                   # (any log file is long)
        gaps = []
        for second in range(30):
            room.tick()
            gaps.append(room.hub.next_room - room.mono.now)
        self.assertEqual(max(gaps), limits.checkpoint_retry_seconds)
        self.assertEqual(limits.checkpoint_retry_seconds, 10.0)
        self.assertLess(limits.checkpoint_retry_seconds, limits.purge_failure_seconds)


class TheStaffTool(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.db = os.path.join(self.folder.name, "hub.db")
        self.wall = Clock(1_800_000_000.0)
        self.store = storemod.Store(self.db, clock=self.wall, max_lines=10 ** 9)
        self.store.db.execute("PRAGMA synchronous=OFF")
        self.addCleanup(self.store.close)

    def run_admin(self, *args):
        out, err = io.StringIO(), io.StringIO()
        with mock.patch.object(admin, "PAUSE_SECONDS", 0.0):                           # (the tests that are not about the pause do not wait for it)
            status = admin.run(["--db", self.db] + list(args), out, err)
        return status, out.getvalue(), err.getvalue()

    def test_status_says_how_full_the_database_is(self):
        status, out, err = self.run_admin("status")
        self.assertEqual((status, err), (0, ""))
        self.assertRegex(out, r"^database: 0\.\d MB of 64\.0 MB used \(0%\), log file 0\.\d MB; 0\.0 MB free to reuse; no lines\npictures: 0 of 2,000 kept, 0\.0 MB of 8\.0 MB; 0 switched off, 0 blocked\n$")
        fill(self.store, 5000)
        self.store.checkpoint()
        status, out, err = self.run_admin("status")
        self.assertEqual((status, err), (0, ""))
        self.assertRegex(out, r"^database: 1\.\d MB of 64\.0 MB used \(1%\), log file 0\.0 MB; 0\.0 MB free to reuse; 5,000 lines, the oldest from 2027-01-15 \d\d:\d\d:\d\dZ\npictures: 0 of 2,000 kept, 0\.0 MB of 8\.0 MB; 0 switched off, 0 blocked\n$")

    def test_purge_frees_pages_down_to_the_hubs_lower_mark_and_says_so(self):
        limits = dataclasses.replace(limitmod.Limits(), **CLI)
        self.store.add_line("9001", "moddy", "an old word of a moderator", True)
        fill_to(self.store, int(limits.db_max_bytes * 0.95), batch=50)
        self.store.checkpoint()
        before = self.store.usage()
        with mock.patch.object(admin.limitmod, "Limits", lambda: limits):
            status, out, err = self.run_admin("purge")
        self.assertEqual((status, err), (0, ""))
        lines = out.splitlines()
        self.assertEqual(len(lines), 3)
        self.assertRegex(lines[0], r"^database: 1\.[89] MB of 2\.0 MB used \(9\d%\), log file 0\.0 MB; 0\.0 MB free to reuse; ")
        self.assertRegex(lines[1], r"^[\d,]+ lines deleted$")
        self.assertRegex(lines[2], r"^database: 1\.[56] MB of 2\.0 MB used \((7\d|80)%\), log file \d\.\d MB; \d+\.\d MB free to reuse; ")
        after = self.store.usage()
        self.assertLessEqual(after.bytes, limits.prune_to_bytes)
        self.assertGreater(after.bytes, limits.prune_to_bytes - 300 * 400)             # (it stops at the lower mark, within a chunk)
        self.assertGreater(after.free, 0.9 * (before.bytes - after.bytes))             # (the pages are free, not only unused)
        self.assertEqual(after.lines, before.lines - int(lines[1].split()[0].replace(",", "")))
        self.assertIn("an old word of a moderator", [r[4] for r in self.store.lines(10 ** 6)])        # (the moderators' lines go last)
        with mock.patch.object(admin.limitmod, "Limits", lambda: limits):
            self.assertEqual(self.run_admin("purge")[::2], (1, "hub.admin: nothing to purge\n"))     # (and a second time it is at the mark already)

    def test_purge_with_keep_lines_leaves_the_newest_lines_only(self):
        self.store.add_line("9001", "moddy", "a moderator", True)
        fill(self.store, 1000)
        for n in range(5):
            self.store.add_line("1001", "alice", "newest %d" % n, False)
        status, out, err = self.run_admin("purge", "--keep-lines", "200")
        self.assertEqual((status, err), (0, ""))
        self.assertIn("806 lines deleted\n", out)
        rows = self.store.lines(10 ** 6)
        self.assertEqual(len(rows), 200)
        self.assertEqual([r[4] for r in rows[:5]], ["newest 4", "newest 3", "newest 2", "newest 1", "newest 0"])
        self.assertNotIn("a moderator", [r[4] for r in rows])                          # (the newest 200 only, whoever said them)
        self.assertEqual(self.run_admin("purge", "--keep-lines", "200")[::2], (1, "hub.admin: nothing to purge\n"))

    def test_purge_says_that_a_reader_holds_the_log_file_and_stops_when_it_is_long(self):
        limits = dataclasses.replace(limitmod.Limits(), **CLI)
        fill_to(self.store, int(limits.db_max_bytes * 0.95), batch=50, accounts=500)
        self.store.checkpoint()
        reader = reader_holding(self, self.db)
        with mock.patch.object(admin.limitmod, "Limits", lambda: limits), mock.patch.object(storemod.Store, "log_max_bytes", 500_000):
            status, out, err = self.run_admin("purge")
            self.assertEqual(status, 1)
            self.assertRegex(err, r"^hub\.admin: a reader holds the log file ")
            deleted = int(re.search(r"^([\d,]+) lines deleted$", out, re.M).group(1).replace(",", ""))
            self.assertTrue(300 <= deleted <= 1200, deleted)                           # (chunks of 300 while the log file was short: not the five or six that reach the lower mark)
            used = self.store.usage()
            self.assertLess(used.log, 500_000 + 400_000)                               # (the log file did not grow without end)
            self.assertLess(used.bytes, limits.db_max_bytes)
            self.assertEqual(out.splitlines()[-1], "database: %s of 2.0 MB used (%d%%), log file %s; %s free to reuse; %s lines, the oldest from %s; at the limit (the log file counts): no new line is kept" % (
                admin.megabytes(used.bytes), used.bytes * 100 // limits.db_max_bytes, admin.megabytes(used.log), admin.megabytes(used.free), format(used.lines, ","), admin.when(used.oldest)))
            status, shown, err = self.run_admin("status")                              # (the status line tells the pages and the log file apart, and that the guard holds)
            self.assertEqual((status, err), (0, ""))
            self.assertEqual(shown.splitlines()[0], out.splitlines()[-1])                   # (the first line of the status; the second is the pictures')
            reader.execute("COMMIT")
            status, out, err = self.run_admin("purge")                                 # (the reader let go: the cut works, and the lines go down to the mark)
            self.assertEqual((status, err), (0, ""))
            self.assertLessEqual(self.store.usage().bytes, limits.prune_to_bytes)
            self.assertEqual(self.store.usage().log, 0)

    def test_a_cut_that_is_refused_for_a_moment_is_tried_again_before_a_reader_is_blamed(self):
        class Stand:
            """A store whose log file is held for a while: its cut is refused as many times as the test says."""

            def __init__(self, refusals):
                self.unsettled, self.refusals, self.cuts = True, refusals, 0

            def usage(self):
                return storemod.Usage(1000, 0, 5, 1_800_000_000, 0)

            def make_room(self, target, chunk, keep):
                return 0

            def checkpoint(self):
                self.cuts += 1
                self.unsettled = self.cuts <= self.refusals
                return not self.unsettled

        limits = limitmod.Limits()
        for refusals, status, said, cuts in ((2, 1, "hub.admin: nothing to purge\n", 3), (99, 1, None, 5)):
            store, waits, out, err = Stand(refusals), [], io.StringIO(), io.StringIO()
            self.assertEqual(admin.purge(store, limits, None, out, err, sleep=waits.append), status)
            self.assertEqual(store.cuts, cuts)
            self.assertEqual(waits, [admin.PAUSE_SECONDS] * cuts if said is None else [admin.PAUSE_SECONDS] * (cuts - 1))
            if said is None:
                self.assertRegex(err.getvalue(), r"^hub\.admin: a reader holds the log file ")
            else:
                self.assertEqual(err.getvalue(), said)

    def test_purge_waits_between_the_chunks_and_holds_no_lock_while_it_waits(self):
        self.assertEqual(admin.PAUSE_SECONDS, 0.1)
        limits = dataclasses.replace(limitmod.Limits(), **CLI)
        fill(self.store, 3000)
        self.store.checkpoint()
        hub = storemod.Store(self.db, clock=self.wall, busy_ms=30, max_lines=10 ** 9)                      # (the hub's own connection: it waits 30 ms at the most for the tool)
        self.addCleanup(hub.close)
        chunks, waits = [], []
        real = self.store.make_room
        self.store.make_room = lambda *args: chunks.append(real(*args)) or chunks[-1]

        def wait(seconds):
            waits.append(seconds)
            if len(waits) <= 5:                                                        # (a line of the hub for the first five pauses; a line for each would make one more line to delete for ever)
                hub.add_line("1", "hub", "a line of the hub between two chunks", False)      # (a StoreError if the tool held the write lock while it waits)

        out, err = io.StringIO(), io.StringIO()
        self.assertEqual(admin.purge(self.store, limits, 100, out, err, sleep=wait), 0, err.getvalue())
        deleted = [c for c in chunks if c]
        self.assertGreaterEqual(len(deleted), 9)
        self.assertEqual(waits, [admin.PAUSE_SECONDS] * len(deleted))                  # (a pause after each chunk, and none for the call that finds nothing more)
        self.assertEqual(self.store.usage().lines, 100)
        self.assertEqual(len([r for r in self.store.lines(10 ** 6) if r[3] == "hub"]), 5)             # (the hub's five lines are among the newest 100: they stay)

    def test_the_help_of_every_command_can_be_printed_on_any_python(self):
        # (a "%" in a help string is a format of argparse's own: Python 3.14 and later look at it when the parser is made, the older ones when the help is printed)
        for argv in (["--help"], ["list", "--help"], ["mute", "--help"], ["status", "--help"], ["purge", "--help"]):
            printed = io.StringIO()
            with contextlib.redirect_stdout(printed):
                status = admin.run(["--db", self.db] + argv, io.StringIO(), io.StringIO())
            self.assertEqual(status, 0, argv)
            self.assertIn("usage: ", printed.getvalue(), argv)
        self.assertIn("100 or more", printed.getvalue())
        self.assertNotIn("%", printed.getvalue())

    def test_purge_never_takes_what_a_screen_may_show(self):
        fill(self.store, 500)
        for keep in ("99", "0", "-5", "x", str(2 ** 62), str(2 ** 63), "99999999999999999999"):         # (a number that is too big for the database is a wrong use, not a traceback)
            status, out, err = self.run_admin("purge", "--keep-lines", keep)
            self.assertEqual(status, 2, keep)
            self.assertEqual(out, "", keep)
        self.assertEqual(self.run_admin("purge", "--keep-lines", "99")[2], "hub.admin: KEEP_LINES is 100 or more\n")
        self.assertEqual(self.store.usage().lines, 500)
        self.assertEqual(self.run_admin("purge", "--keep-lines", "100")[0], 0)
        self.assertEqual(self.store.usage().lines, 100)

    def test_a_database_that_refuses_is_a_message_and_not_a_traceback(self):
        fill(self.store, 500)
        self.store.checkpoint()
        for command in (("purge", "--keep-lines", "100"), ("status",)):
            with mock.patch.object(storemod.Store, "transaction", side_effect=sqlite3.OperationalError("database is locked")), \
                    mock.patch.object(storemod.Store, "usage", side_effect=sqlite3.OperationalError("database is locked")):
                status, out, err = self.run_admin(*command)
            self.assertEqual(status, 1, command)
            self.assertRegex(err, r"^hub\.admin: the database refused it \(\w+\)\n$", command)
        self.assertEqual(self.store.usage().lines, 500)

    def test_the_hub_goes_on_while_the_tool_works_and_after(self):
        room = Room(**CLI)
        self.addCleanup(room.close)
        fill_to(room.store, int(room.limits.db_max_bytes * 0.95), batch=50)
        who = room.person(1)
        before = room.store.lines(1)[0][0]
        out, err = io.StringIO(), io.StringIO()
        with mock.patch.object(admin.limitmod, "Limits", lambda: room.limits), mock.patch.object(admin, "PAUSE_SECONDS", 0.0):
            self.assertEqual(admin.run(["--db", room.path, "purge", "--keep-lines", "300"], out, err), 0, err.getvalue())
        room.say(who, "after the tool")                                                # (the hub's own connection sees the lines gone and takes a new one)
        self.assertEqual(who.codes(), [])
        rows = room.store.lines(10 ** 6)
        self.assertEqual(len(rows), 301)
        self.assertEqual((rows[0][4], rows[1][0]), ("after the tool", before))         # (the newest 300 are the ones of before, and the new line has a number that was never used)
        self.assertGreater(rows[0][0], before)
        self.assertEqual([l.text for l in room.chat.ring][-1:], ["after the tool"])


class TheToolAndTheHubWriteAtTheSameTime(unittest.TestCase):
    """The hub's one thread and a terminal's `purge` are two writers of one file (WAL; the hub waits 300 ms for the other at the most): whatever they do to each other, nothing is lost that should stay."""

    def test_nothing_that_should_stay_is_lost_and_nothing_is_broken(self):
        room = Room(account_hour_lines=10 ** 6, account_day_lines=10 ** 6, **CLI)         # (budgets out of the way: the hub writes a line whenever it can)
        self.addCleanup(room.close)
        fill_to(room.store, int(room.limits.db_max_bytes * 0.95), batch=50)
        stop, results, errors = threading.Event(), [], []

        def tool():
            try:
                while not stop.is_set():
                    out, err = io.StringIO(), io.StringIO()
                    results.append((admin.run(["--db", room.path, "purge", "--keep-lines", "400"], out, err), err.getvalue()))
                    time.sleep(0.005)
            except BaseException as e:                                                 # (a traceback in the tool's thread is a failure of the test)
                errors.append(e)

        said, refused = [], collections.Counter()
        people = [room.person(n) for n in range(1, 6)]
        with mock.patch.object(admin.limitmod, "Limits", lambda: room.limits):
            thread = threading.Thread(target=tool)
            thread.start()
            try:
                for step in range(400):
                    try:
                        room.tick(2.0)
                    except storemod.StoreError:                                        # (the hub's own tick meets the tool's lock: it is tried again, as in the program)
                        refused["tick"] += 1
                    for n, who in enumerate(people):
                        text = "step %d from %d %s" % (step, n, "word " * 20)
                        before = len(who.sent)
                        room.say(who, text)
                        if len(who.sent) == before:                                    # (a line that is kept is told to the others, not to the one who said it)
                            said.append(text.strip())                           # (the text rules take the blank at the end off)
                        else:
                            refused[who.sent[-1]["code"]] += 1
            finally:
                stop.set()
                thread.join(30)
        self.assertEqual(errors, [])
        self.assertTrue(results)
        for code, err in results:                                                      # (the hub is a reader, too, for the moment of each of its statements: the tool may find the log file held, and says so)
            self.assertTrue((code, err) in ((0, ""), (1, "hub.admin: nothing to purge\n"), (1, "hub.admin: the database refused it (StoreError)\n")) or (code == 1 and err.startswith("hub.admin: a reader holds the log file")), (code, err))
        self.assertEqual(room.store.db.execute("PRAGMA integrity_check").fetchone()[0], "ok")
        ids = [r[0] for r in room.store.lines(10 ** 6)][::-1]
        self.assertEqual(ids, sorted(set(ids)))
        newest = [r[4] for r in room.store.lines(5)]
        self.assertTrue(all(text in said for text in newest), newest)                  # (what stands at the end are lines that the hub took)
        self.assertEqual(room.store.lines(1)[0][4], said[-1])                          # (and the last line it took is the newest)
        self.assertLess(room.store.size(), room.limits.db_max_bytes)


class TheHubOnRealSockets(unittest.TestCase):
    def test_a_client_is_told_of_its_budget_with_a_calm_code_and_stays_connected(self):
        with hub_rig.Rig(account_hour_lines=3, account_day_lines=100, **LOOSE) as rig:
            client = hc.Client(rig.port, login="2001:visitor1")
            self.addCleanup(client.close)
            self.assertEqual(client.recv_json()["t"], "welcome")
            for n in range(3):
                client.say("line %d" % n)
                self.assertEqual(client.recv_json()["t"], "line")
            for n in range(25):                                                        # (more tries than the 20 mistakes that close a socket)
                client.say("one too many %d" % n)
            errors = [m for m in client.drain(0.5) if m["t"] == "err"]
            self.assertEqual(errors, [{"t": "err", "code": "quota"}] * 25)
            client.say("is the socket still there")
            self.assertEqual(answer(client), {"t": "err", "code": "quota"})

    def test_a_full_store_is_made_room_in_by_the_running_hub_and_the_chat_takes_a_line_again(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        db = os.path.join(folder.name, "hub.db")
        before = storemod.Store(db, clock=Clock(time.time()))
        fill_to(before, 1_000_000, batch=50)
        before.close()
        with hub_rig.Rig(db=db, db_max_bytes=1_000_000, purge_chunk=300, tick_seconds=0.05) as rig:
            client = hc.Client(rig.port, login="2001:visitor1")
            self.addCleanup(client.close)
            welcome = client.recv_json()
            self.assertEqual((welcome["t"], welcome["you"]["name"]), ("welcome", "visitor1"))
            kept = None
            for n in range(40):                                                        # (a try every 0.2 seconds: the first may be refused, one is kept within 8 seconds)
                client.say("hello, try number %d" % n)
                message = answer(client)
                if message["t"] == "line":
                    kept = message
                    break
                self.assertEqual(message, {"t": "err", "code": "rate"})
                time.sleep(0.2)
            self.assertIsNotNone(kept, "the chat did not take a line")
            self.assertEqual(kept["who"], "visitor1")
            self.assertRegex(rig.log_text.getvalue(), r"prune lines=\d+")
            self.assertLessEqual(rig.call(rig.store.size), 1_000_000)

class TheDocumentsSayWhatTheCodeDoes(unittest.TestCase):
    def setUp(self):
        with open(os.path.join(REPO, "docs", "HUB.md"), encoding="utf-8") as f:
            self.doc = f.read()

    def test_the_numbers_of_the_document_are_the_limits_of_the_code(self):
        d = limitmod.Limits()
        self.assertEqual((d.account_hour_lines, d.account_day_lines, d.prune_from_percent, d.prune_to_percent, d.keep_lines, d.purge_chunk, d.db_max_bytes),
                         (200, 1000, 90, 80, 1000, 2000, 64 * 1024 * 1024))
        self.assertEqual((d.prune_from_bytes, d.prune_to_bytes), (64 * 1024 * 1024 * 9 // 10, 64 * 1024 * 1024 * 8 // 10))
        for needle in ("a budget of 200 lines for the hour and 1,000 for the day", "one line every 18 seconds, one every 86.4", "answered `quota`", "The budgets are kept in memory", "From 90% of 64 MB", "down to 80%",
                       "2,000 lines a second", "the moderators' lines and the lines that a moderator deleted last", "the newest 1,000 never", "`quota` (the account has said the lines of its hour or of its day)", "and a `quota`, are not the client's doing",
                       "a tenth of a second between two chunks", "\"a reader holds the log file\"", "database_full_a_reader_holds_the_log_file", "is over 16 MB the hub deletes no more lines",
                       "python -m hub.admin status", "python -m hub.admin purge [--keep-lines N]", "`--keep-lines N` (100 or more", "\"nothing to purge\"", "database_full_and_no_line_to_delete",
                       "tests/scripts/test_hub_room.py", "tests/data/hub_room_mutants.json"):
            self.assertIn(needle.replace("tests/scripts/", "").replace("tests/data/", ""), self.doc, needle)

    def test_the_chat_sends_the_codes_that_the_document_lists(self):
        with open(os.path.join(REPO, "hub", "chat.py"), encoding="utf-8") as f:
            sent = set(re.findall(r'mistake\(peer, "([a-z]+)"', f.read())) | {"login", "young", "empty", "long"}
        listed = set(re.findall(r"`([a-z]+)`", self.doc[self.doc.index("Error codes:"):self.doc.index("\n", self.doc.index("Error codes:"))]))
        self.assertTrue(sent <= listed, sent - listed)
        self.assertIn("quota", sent)

    def test_the_wording_of_the_page_for_the_code_is_calm_and_the_page_has_it(self):
        with open(os.path.join(REPO, "web", "home", "chat.js"), encoding="utf-8") as f:
            self.assertIn("quota: 'You have chatted a lot, try again later'", f.read())

    def test_the_command_line_says_what_it_does_at_its_top(self):
        self.assertIn("python -m hub.admin status", admin.__doc__)
        self.assertIn("python -m hub.admin purge [--keep-lines N]", admin.__doc__)


if __name__ == "__main__":
    unittest.main()
