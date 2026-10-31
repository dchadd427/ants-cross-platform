#!/usr/bin/env python3
"""The contract of the hub's data (hub/store/base.py; docs/HUB.md, "Data"): the same checks run against every backend.

A backend is a subclass of `Contract` that says how to make a store (`make_store(clock)`) and how to make writes fail (`refusing_writes(store)`).
SQLite is the default and always runs. A backend that needs a server (the hook at the bottom) is SKIPPED, visibly, when no server is configured: it never passes without one.
"""
import contextlib
import os
import sqlite3
import sys
import tempfile
import unittest
from unittest import mock

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, REPO)
from hub import store as storemod  # noqa: E402
from hub.store import base  # noqa: E402

DAY = 86400


class Clock:
    def __init__(self, now=1_000_000.0):
        self.now = now

    def __call__(self):
        return self.now


class Contract(unittest.TestCase):
    """The checks. They run only in a backend's subclass (the class itself is skipped)."""

    @classmethod
    def setUpClass(cls):
        if cls is Contract:
            raise unittest.SkipTest("the checks themselves: a backend's subclass runs them")

    def make_store(self, clock):
        raise NotImplementedError

    @contextlib.contextmanager
    def refusing_writes(self, store):
        """While active, a write of `store` is refused (locked, full, out of reach) and raises StoreError."""
        raise NotImplementedError

    def setUp(self):
        self.clock = Clock()
        self.store = self.make_store(self.clock)
        self.addCleanup(self.store.close)

    def say(self, n, account="1001", name="alice", mod=False):
        return self.store.add_line(account, name, "line %d" % n, mod)

    # ---- the interface ----

    def test_the_store_is_a_store_and_does_every_operation_of_the_contract(self):
        self.assertIsInstance(self.store, base.StoreBase)
        self.assertGreater(len(base.PUBLIC), 15)
        for name in base.PUBLIC:
            self.assertIsNot(getattr(type(self.store), name), getattr(base.StoreBase, name), name)
        self.assertTrue(issubclass(base.StoreUnavailable, base.StoreError))

    def test_a_refused_write_is_a_store_error_and_leaves_nothing_behind(self):
        self.say(1)
        writes = (lambda: self.say(2), lambda: self.store.delete_line(1, "me"), lambda: self.store.purge(), lambda: self.store.forget("1001"),
                  lambda: self.store.mute("1001", 5, "me"), lambda: self.store.ban("1001", "me"), lambda: self.store.unban("1001"),
                  lambda: self.store.add_session("k", "1001", "alice", 60, 10, 100), lambda: self.store.delete_session("k"))
        with self.refusing_writes(self.store):
            for write in writes:
                with self.assertRaises(base.StoreError):
                    write()
        self.assertEqual([l.text for l in self.store.recent_lines(10)], ["line 1"])
        self.assertFalse(self.store.is_banned("1001"))
        self.assertEqual(self.store.last_deletion(), 0)

    # ---- lines ----

    def test_a_line_is_kept_and_comes_back_oldest_first(self):
        first = self.say(1)
        self.clock.now += 5
        second = self.say(2, "1002", "bob", mod=True)
        self.assertIsInstance(first, base.Line)
        self.assertLess(first.id, second.id)
        self.assertEqual((second.at, second.account, second.name, second.text, bool(second.mod)), (int(self.clock.now), "1002", "bob", "line 2", True))
        self.assertEqual(self.store.recent_lines(10), [first, second])
        self.assertEqual(self.store.recent_lines(1), [second])

    def test_recent_lines_leave_out_the_deleted_and_the_past_their_time(self):
        old = self.say(1)
        self.clock.now += 6 * DAY
        kept, gone = self.say(2), self.say(3)
        self.assertTrue(self.store.delete_line(gone.id, "me"))
        self.assertEqual(self.store.recent_lines(10), [old, kept])
        self.clock.now += 2 * DAY                                           # (the first is 8 days old now)
        self.assertEqual(self.store.recent_lines(10), [kept])

    def test_lines_are_the_newest_first_with_their_state(self):
        a, b = self.say(1), self.say(2)
        self.store.delete_line(a.id, "me")
        rows = [tuple(r) for r in self.store.lines(10)]
        self.assertEqual(rows, [(b.id, b.at, "1001", "alice", "line 2", 0), (a.id, a.at, "1001", "alice", "line 1", 1)])
        self.assertEqual(len(self.store.lines(1)), 1)

    def test_the_account_of_a_name_and_the_name_of_an_account_are_the_last_spoken(self):
        self.assertIsNone(self.store.account_of_name("alice"))
        self.assertEqual(self.store.last_name("1001"), "")
        self.say(1, "1001", "alice")
        self.say(2, "1001", "alicia")
        self.say(3, "1002", "alice")
        self.assertEqual(self.store.account_of_name("alice"), "1002")
        self.assertEqual(self.store.account_of_name("alicia"), "1001")
        self.assertEqual(self.store.last_name("1001"), "alicia")

    def test_text_is_kept_as_it_is(self):
        text = "caf\u00e9 \u4e2d\u6587 'quote' \"double\" % _ ; --"
        line = self.store.add_line("1001", "alice", text, False)
        self.assertEqual(self.store.recent_lines(1)[0].text, text)
        self.assertEqual(line.text, text)

    # ---- deletions ----

    def test_a_deletion_is_numbered_once_and_a_second_one_does_nothing(self):
        a, b = self.say(1), self.say(2)
        self.assertEqual(self.store.last_deletion(), 0)
        self.assertFalse(self.store.delete_line(9999, "me"))
        self.assertTrue(self.store.delete_line(b.id, "me"))
        self.assertTrue(self.store.delete_line(a.id, "me"))
        self.assertFalse(self.store.delete_line(a.id, "me"))
        self.assertEqual(self.store.last_deletion(), 2)
        self.assertEqual([tuple(r) for r in self.store.deletions_since(0)], [(1, b.id), (2, a.id)])
        self.assertEqual([tuple(r) for r in self.store.deletions_since(1)], [(2, a.id)])
        self.assertEqual(self.store.deletions_since(2), [])

    def test_the_deletion_counter_only_goes_up_and_a_purge_never_lowers_it(self):
        a = self.say(1)
        self.store.delete_line(a.id, "me")
        self.assertEqual(self.store.last_deletion(), 1)
        self.clock.now += 8 * DAY
        self.assertEqual(self.store.purge(), 1)                             # (the deleted line is past its time and goes for good)
        self.assertEqual(self.store.last_deletion(), 1)
        b = self.say(2)
        self.store.delete_line(b.id, "me")
        self.assertEqual(self.store.last_deletion(), 2)
        self.assertEqual([tuple(r)[0] for r in self.store.deletions_since(0)], [2])

    # ---- purge and size ----

    def test_purge_deletes_what_is_past_its_time_a_chunk_at_a_time(self):
        for n in range(5):
            self.say(n)
        self.clock.now += 8 * DAY
        fresh = self.say(9)
        self.assertEqual(self.store.purge(2), 2)
        self.assertEqual(self.store.purge(2), 2)
        self.assertEqual(self.store.purge(2), 1)
        self.assertEqual(self.store.purge(2), 0)
        self.assertEqual(self.store.recent_lines(10), [fresh])
        self.assertEqual(len(self.store.lines(10)), 1)

    def test_purge_ends_the_mutes_that_are_over_and_the_logins_that_ran_out(self):
        self.store.mute("1001", 1, "me", "alice")
        self.store.mute("1002", 600, "me", "bob")
        self.store.add_session("k1", "1001", "alice", 60, 10, 100)
        self.store.add_session("k2", "1002", "bob", 3600, 10, 100)
        self.clock.now += 120
        self.store.purge()
        self.assertEqual([m[0] for m in self.store.mutes()], ["1002"])
        self.assertIsNone(self.store.session("k1"))
        self.assertIsNotNone(self.store.session("k2"))

    def test_size_is_a_number_of_bytes_that_grows_with_what_is_kept(self):
        empty = self.store.size()
        self.assertIsInstance(empty, int)
        self.assertGreaterEqual(empty, 0)
        for n in range(300):
            self.say(n)
        self.assertGreater(self.store.size(), empty)

    # ---- mutes ----

    def test_a_mute_ends_by_itself_and_can_be_lifted(self):
        self.assertEqual(self.store.muted_until("1001"), 0)
        until = self.store.mute("1001", 10, "me", "alice")
        self.assertEqual(until, int(self.clock.now) + 600)
        self.assertEqual(self.store.muted_until("1001"), until)
        self.assertEqual(self.store.muted_until("1002"), 0)
        self.assertEqual([tuple(m) for m in self.store.mutes()], [("1001", "alice", until, "me")])
        self.clock.now += 601
        self.assertEqual(self.store.muted_until("1001"), 0)
        self.assertEqual(self.store.mutes(), [])
        self.store.mute("1001", 10, "me", "alice")
        self.assertEqual(self.store.mute("1001", 0, "me"), 0)
        self.assertEqual(self.store.muted_until("1001"), 0)

    def test_a_second_mute_replaces_the_first_and_the_list_is_soonest_first(self):
        self.store.mute("1001", 10, "me", "alice")
        later = self.store.mute("1001", 30, "me", "alice")
        sooner = self.store.mute("1002", 5, "me", "bob")
        self.assertEqual([tuple(m) for m in self.store.mutes()], [("1002", "bob", sooner, "me"), ("1001", "alice", later, "me")])

    # ---- bans ----

    def test_a_ban_stays_until_it_is_lifted(self):
        self.assertFalse(self.store.is_banned("1001"))
        self.assertFalse(self.store.unban("1001"))
        self.store.ban("1001", "me", "alice")
        self.clock.now += 365 * DAY
        self.store.ban("1001", "me", "alice")                               # (again: no second row)
        self.store.ban("1002", "me", "bob")
        self.assertTrue(self.store.is_banned("1001"))
        self.assertFalse(self.store.is_banned("1003"))
        self.assertEqual([tuple(b)[:2] + tuple(b)[3:] for b in self.store.bans()], [("1001", "alice", "me"), ("1002", "bob", "me")])
        self.assertTrue(self.store.unban("1001"))
        self.assertFalse(self.store.is_banned("1001"))
        self.assertFalse(self.store.unban("1001"))

    # ---- logins ----

    def test_a_login_is_found_by_its_key_until_it_runs_out(self):
        self.assertIsNone(self.store.session("k1"))
        self.store.add_session("k1", "1001", "alice", 100, 10, 100)
        self.assertEqual(tuple(self.store.session("k1")), ("1001", "alice", int(self.clock.now) + 100))
        self.clock.now += 99
        self.assertIsNotNone(self.store.session("k1"))
        self.clock.now += 1
        self.assertIsNone(self.store.session("k1"))

    def test_an_account_keeps_a_few_logins_and_all_accounts_together_a_number_the_oldest_go(self):
        for n in range(4):
            self.store.add_session("a%d" % n, "1001", "alice", 1000, 3, 100)
            self.clock.now += 1
        self.assertEqual([self.store.session("a%d" % n) is not None for n in range(4)], [False, True, True, True])
        for n in range(4):
            self.store.add_session("b%d" % n, "1002", "bob", 1000, 3, 5)
            self.clock.now += 1
        alive = [k for k in ("a1", "a2", "a3", "b0", "b1", "b2", "b3") if self.store.session(k) is not None]
        self.assertEqual(alive, ["a2", "a3", "b1", "b2", "b3"])          # (b0 went for the account's three, a1 for the five of all)

    def test_one_login_ends_and_only_that_one(self):
        self.store.add_session("k1", "1001", "alice", 1000, 10, 100)
        self.store.add_session("k2", "1001", "alice", 1000, 10, 100)
        self.assertTrue(self.store.delete_session("k1"))
        self.assertFalse(self.store.delete_session("k1"))
        self.assertIsNone(self.store.session("k1"))
        self.assertIsNotNone(self.store.session("k2"))

    # ---- forget ----

    def test_forget_removes_the_logins_and_the_lines_and_keeps_the_mute_and_the_ban_without_the_name(self):
        mine = [self.say(n) for n in range(3)]
        other = self.say(9, "1002", "bob")
        self.store.add_session("k1", "1001", "alice", 1000, 10, 100)
        self.store.add_session("k2", "1002", "bob", 1000, 10, 100)
        until = self.store.mute("1001", 10, "me", "alice")
        self.store.ban("1001", "me", "alice")
        ids = self.store.forget("1001")
        self.assertEqual(sorted(ids), sorted(l.id for l in mine))
        self.assertEqual(self.store.recent_lines(10), [other])
        self.assertIsNone(self.store.session("k1"))
        self.assertIsNotNone(self.store.session("k2"))
        self.assertEqual(self.store.last_name("1001"), "")
        self.assertTrue(self.store.is_banned("1001"))
        self.assertEqual(self.store.muted_until("1001"), until)
        self.assertEqual([tuple(b)[:2] for b in self.store.bans()], [("1001", "")])
        self.assertEqual([tuple(m)[:2] for m in self.store.mutes()], [("1001", "")])

    def test_forget_names_the_lines_a_screen_may_still_show_and_not_the_ones_announced_deleted(self):
        a, b, c = self.say(1), self.say(2), self.say(3)
        self.store.delete_line(a.id, "me")                                  # (deletion number 1)
        self.store.delete_line(b.id, "me")                                  # (number 2: the hub has not told the screens)
        self.assertEqual(sorted(self.store.forget("1001", since=1)), sorted([b.id, c.id]))
        self.assertEqual(self.store.lines(10), [])


class SqliteContract(Contract):
    """The default backend: one file."""

    def make_store(self, clock):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.path = os.path.join(folder.name, "hub.db")
        return storemod.Store(self.path, clock=clock, busy_ms=50)

    @contextlib.contextmanager
    def refusing_writes(self, store):
        blocker = sqlite3.connect(self.path, isolation_level=None)          # (another process that holds the write lock)
        try:
            blocker.execute("BEGIN IMMEDIATE")
            yield
        finally:
            blocker.close()

    def test_the_operations_that_only_this_backend_has_stay_on_its_class(self):
        for name in ("checkpoint", "unsettled"):
            self.assertTrue(hasattr(self.store, name), name)
            self.assertNotIn(name, base.PUBLIC)
            self.assertFalse(hasattr(base.StoreBase, name), name)
        self.assertEqual(self.store.db.execute("PRAGMA secure_delete").fetchone()[0], 1)

    def test_the_default_store_is_the_sqlite_one(self):
        self.assertIs(storemod.Store, __import__("hub.store.sqlite", fromlist=["Store"]).Store)

    def test_a_file_that_is_not_a_database_cannot_be_opened_and_says_so_as_a_store_error(self):
        bad = os.path.join(os.path.dirname(self.path), "bad.db")
        with open(bad, "wb") as f:
            f.write(b"this is not a database file at all" * 20)
        with self.assertRaises(storemod.StoreError):
            storemod.Store(bad)

    def test_an_error_that_is_not_a_busy_store_is_not_made_a_store_error(self):
        with mock.patch.object(storemod.Store, "transaction", side_effect=sqlite3.IntegrityError("a bug, not a lock")):
            with self.assertRaises(sqlite3.IntegrityError):                 # (the hub closes that socket; it is not "try again later")
                self.store.ban("1001", "me")


# ---- the hook for a backend that needs a server (not there yet) -----------------------------------------------------------------------------
# A SQL backend adds a class like this one. It is skipped, and the skip is shown in the run's output, while no server is configured; it must make
# `make_store` and `refusing_writes` real before it can pass, and with a server configured but nothing wired it fails (it never passes by doing nothing).
SQL_SERVER = os.environ.get("ANTS_HUB_TEST_SQL_URL", "")


@unittest.skipUnless(SQL_SERVER, "no SQL server configured (set ANTS_HUB_TEST_SQL_URL to run the contract against one)")
class SqlContract(Contract):
    def test_the_hook_is_wired_to_a_backend(self):
        self.fail("a SQL server is configured and no SQL backend is wired into this hook yet")

    def make_store(self, clock):
        self.fail("a SQL backend is configured but not wired into this hook yet")

    @contextlib.contextmanager
    def refusing_writes(self, store):
        self.fail("a SQL backend is configured but not wired into this hook yet")
        yield


if __name__ == "__main__":
    unittest.main()
