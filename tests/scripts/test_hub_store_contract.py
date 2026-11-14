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
        self.store.max_lines = 10 ** 9                  # (out of the way, so that the checks that need many lines have them: the checks of the limit lower it)
        self.addCleanup(self.store.close)

    def say(self, n, account="1001", name="alice", mod=False):
        return self.store.add_line(account, name, "line %d" % n, mod)

    # ---- the interface ----

    def test_the_store_is_a_store_and_does_every_operation_of_the_contract(self):
        self.assertIsInstance(self.store, base.StoreBase)
        self.assertGreater(len(base.PUBLIC), 18)
        for name in base.PUBLIC:
            self.assertIsNot(getattr(type(self.store), name), getattr(base.StoreBase, name), name)
        self.assertTrue(issubclass(base.StoreUnavailable, base.StoreError))

    def test_a_refused_write_is_a_store_error_and_leaves_nothing_behind(self):
        self.say(1)
        writes = (lambda: self.say(2), lambda: self.store.delete_line(1, "me"), lambda: self.store.purge(), lambda: self.store.forget("1001"),
                  lambda: self.store.mute("1001", 5, "me"), lambda: self.store.ban("1001", "me"), lambda: self.store.unban("1001"),
                  lambda: self.store.add_session("k", "1001", "alice", 60, 10, 100), lambda: self.store.delete_session("k"), lambda: self.store.make_room(0, 10, 0),
                  lambda: self.store.set_profile("1001", "alice", "Alice A", 100), lambda: self.store.set_pick("1001", "username"))
        with self.refusing_writes(self.store):
            for write in writes:
                with self.assertRaises(base.StoreError):
                    write()
        self.assertEqual([l.text for l in self.store.recent_lines(10)], ["line 1"])
        self.assertFalse(self.store.is_banned("1001"))
        self.assertEqual(self.store.last_deletion(), 0)
        self.assertIsNone(self.store.profile("1001"))

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

    def test_the_name_of_an_account_is_the_one_it_last_spoke_under_and_a_name_is_not_a_way_to_an_account(self):
        self.assertEqual(self.store.last_name("1001"), "")
        self.say(1, "1001", "alice")
        self.say(2, "1001", "alicia")
        self.say(3, "1002", "alice")
        self.assertEqual(self.store.last_name("1001"), "alicia")
        self.assertEqual(self.store.last_name("1002"), "alice")
        self.assertFalse(hasattr(self.store, "account_of_name"), "names are not unique: an account is found by its username (account_of_username), never by a name that was shown")

    def test_text_is_kept_as_it_is(self):
        text = "caf\u00e9 \u4e2d\u6587 'quote' \"double\" % _ ; --"
        line = self.store.add_line("1001", "alice", text, False)
        self.assertEqual(self.store.recent_lines(1)[0].text, text)
        self.assertEqual(line.text, text)

    # ---- deletions ----

    def test_the_account_of_a_line_is_known_until_the_line_is_deleted(self):
        a, b = self.say(1, "1001"), self.say(2, "1002", "bob")
        self.assertEqual((self.store.account_of_line(a.id), self.store.account_of_line(b.id)), ("1001", "1002"))
        self.assertIsNone(self.store.account_of_line(9999))
        self.store.delete_line(a.id, "me")
        self.assertIsNone(self.store.account_of_line(a.id))

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

    # ---- the newest max_lines ----

    def test_the_default_is_the_newest_200_lines(self):
        self.assertEqual(base.MAX_LINES, 200)
        self.assertEqual(base.StoreBase.max_lines, 200)

    def test_only_the_newest_max_lines_are_kept_and_the_store_never_holds_more(self):
        self.store.max_lines = 5
        ids = []
        for n in range(8):
            ids.append(self.say(n).id)
            self.assertLessEqual(self.store.usage().lines, 5, "the line that goes over takes the oldest out in the same step")
        self.assertEqual([r[0] for r in self.store.lines(100)][::-1], ids[3:])
        self.assertEqual([l.id for l in self.store.recent_lines(100)], ids[3:])
        self.assertEqual(self.store.usage().lines, 5)
        self.assertEqual(self.store.usage().oldest, self.store.lines(100)[-1][1])

    def test_every_line_counts_the_moderators_and_the_deleted_ones_too_and_they_go_in_their_turn(self):
        self.store.max_lines = 6
        ids = [self.say(n, mod=n in (0, 1)).id for n in range(6)]
        self.store.delete_line(ids[2], "me")
        self.store.delete_line(ids[5], "me")
        more = [self.say(n).id for n in range(10, 14)]
        left = [r[0] for r in self.store.lines(100)][::-1]
        self.assertEqual(left, ids[4:] + more, "the two lines of a moderator and the deleted one that is old went out with the rest; the deleted one among the newest stays")
        self.assertEqual(self.store.last_deletion(), 2, "the counter of deletions keeps its number")
        self.assertEqual([tuple(r) for r in self.store.deletions_since(0)], [(2, ids[5])], "a deletion whose line is gone is not told again")
        self.assertEqual([l.id for l in self.store.recent_lines(100)], [ids[4]] + more)

    def test_recent_lines_never_reach_past_the_newest_max_lines_to_find_lines_that_are_not_deleted(self):
        ids = [self.say(n).id for n in range(8)]
        for line_id in ids[4:]:
            self.store.delete_line(line_id, "me")
        self.store.max_lines = 5
        self.assertEqual([l.id for l in self.store.recent_lines(10)], [ids[3]], "the newest 5 are lines 3 to 7: one of them is alive")

    def test_purge_deletes_the_lines_past_the_newest_max_lines_a_chunk_at_a_time(self):
        ids = [self.say(n).id for n in range(10)]
        self.store.max_lines = 4                                            # (a store that held more: the limit is lowered, as a file of an earlier program is opened)
        self.assertEqual(self.store.purge(2), 2)
        self.assertEqual(self.store.purge(2), 2)
        self.assertEqual(self.store.purge(2), 2)
        self.assertEqual(self.store.purge(2), 0)
        self.assertEqual([r[0] for r in self.store.lines(100)][::-1], ids[6:])

    def test_a_chunk_of_the_purge_is_for_the_lines_of_both_limits_together(self):
        for n in range(3):
            self.say(n)
        self.clock.now += 8 * DAY
        fresh = [self.say(n).id for n in range(10, 16)]
        self.store.max_lines = 4
        self.assertEqual(self.store.purge(4), 4, "the 3 that are past their time and one that is past the newest 4: a chunk is 4 lines, not 4 and 4")
        self.assertEqual(len(self.store.lines(100)), 5)
        self.assertEqual(self.store.purge(4), 1)
        self.assertEqual([r[0] for r in self.store.lines(100)][::-1], fresh[2:])

    def test_a_purge_leaves_exactly_the_newest_lines_whatever_their_flags(self):
        ids = [self.say(n, mod=n % 3 == 0).id for n in range(10)]
        for line_id in (ids[1], ids[4], ids[8], ids[9]):
            self.store.delete_line(line_id, "me")
        self.store.max_lines = 4
        self.assertEqual(self.store.purge(), 6)
        self.assertEqual([r[0] for r in self.store.lines(100)][::-1], ids[6:], "the newest 4 stay: a moderator's line and a deleted line count as any other")

    def test_a_line_takes_out_at_most_a_few_of_a_long_backlog_and_the_purge_takes_the_rest_by_chunks(self):
        for n in range(300):
            self.say(n)
        self.store.max_lines = 200
        self.say(300)
        left = self.store.usage().lines
        self.assertGreater(left, 280, "one chat line is not a long delete")
        self.assertLess(left, 301)
        self.assertEqual(self.store.purge(50), 50)
        while self.store.purge(50):
            pass
        self.assertEqual(self.store.usage().lines, 200)

    def test_the_age_limit_stays_whichever_limit_is_smaller_deletes_first(self):
        self.store.max_lines = 6
        for n in range(3):
            self.say(n)
        self.clock.now += 8 * DAY
        fresh = [self.say(n).id for n in range(10, 13)]
        self.assertEqual([l.id for l in self.store.recent_lines(100)], fresh)
        self.assertEqual(len(self.store.lines(100)), 6, "the 3 old lines are among the newest 6, and are past their 7 days: they are not shown")
        self.assertEqual(self.store.purge(), 3)
        self.assertEqual([r[0] for r in self.store.lines(100)][::-1], fresh)
        self.clock.now += 8 * DAY
        self.assertEqual(self.store.purge(), 3)
        self.assertEqual(self.store.lines(100), [])

    def test_the_limit_on_lines_leaves_the_mutes_the_bans_the_logins_and_the_counters(self):
        self.store.max_lines = 3
        self.store.mute("1001", 600, "me", "alice")
        self.store.ban("1002", "me", "bob")
        self.store.add_session("k", "1001", "alice", 3600, 10, 100)
        first = self.say(1).id
        self.store.delete_line(first, "me")
        before = (self.store.last_deletion(), self.store.last_restriction())
        for n in range(10):
            self.say(n)
        self.store.purge()
        self.assertEqual((self.store.last_deletion(), self.store.last_restriction()), before)
        self.assertGreater(self.store.muted_until("1001"), 0)
        self.assertTrue(self.store.is_banned("1002"))
        self.assertIsNotNone(self.store.session("k"))
        self.assertGreater(self.say(99).id, first, "a number is never used twice")

    def test_a_deleted_line_is_not_kept_longer_than_the_others_by_the_limit(self):
        self.store.max_lines = 3
        a = self.say(1)
        self.store.delete_line(a.id, "me")
        for n in range(3):
            self.say(n)
        self.assertNotIn(a.id, [r[0] for r in self.store.lines(100)])
        self.assertFalse(self.store.delete_line(a.id, "me"))

    def test_size_is_a_number_of_bytes_that_grows_with_what_is_kept(self):
        empty = self.store.size()
        self.assertIsInstance(empty, int)
        self.assertGreaterEqual(empty, 0)
        for n in range(300):
            self.say(n)
        self.assertGreater(self.store.size(), empty)

    def test_make_room_deletes_the_oldest_lines_first_a_chunk_at_a_time_and_never_the_newest(self):
        ids = [self.say(n).id for n in range(100)]
        self.assertEqual(self.store.make_room(0, 30, 10), 30)
        self.assertEqual([r[0] for r in self.store.lines(1000)][::-1], ids[30:])
        self.assertEqual(self.store.make_room(0, 1000, 10), 60)             # (all but the newest 10)
        self.assertEqual([r[0] for r in self.store.lines(1000)][::-1], ids[90:])
        self.assertEqual(self.store.make_room(0, 1000, 10), 0)              # (no line may go)
        self.assertEqual(self.store.make_room(0, 1000, 11), 0)
        self.assertEqual(len(self.store.lines(1000)), 10)
        self.assertEqual(self.store.make_room(0, 1000, 0), 10)              # (keep=0: the newest may go too)
        self.assertEqual(self.store.lines(10), [])

    def test_make_room_does_nothing_for_a_store_that_is_at_or_under_the_size_it_is_asked_for(self):
        for n in range(200):
            self.say(n)
        size = self.store.size()
        self.assertEqual(self.store.make_room(size, 1000, 0), 0)
        self.assertEqual(self.store.make_room(size * 2, 1000, 0), 0)
        self.assertEqual(len(self.store.lines(1000)), 200)
        self.assertGreater(self.store.make_room(1, 1000, 0), 0)                # (a size that a store with lines cannot be at: the lines go)
        self.assertLess(self.store.size(), size)

    def test_make_room_takes_the_moderators_lines_last_and_only_when_no_other_line_may_go(self):
        ids = [self.say(n, mod=n in (4, 9)).id for n in range(50)]          # (lines number 4 and 9 are a moderator's)
        self.assertEqual(self.store.make_room(0, 45, 3), 45)                # (the 47 oldest, but not the two of the moderator)
        left = [r[0] for r in self.store.lines(1000)][::-1]
        self.assertEqual(left, [ids[4], ids[9]] + ids[47:])
        self.assertEqual(self.store.make_room(0, 10, 3), 2)                 # (and now they go)
        self.assertEqual([r[0] for r in self.store.lines(1000)][::-1], ids[47:])
        again = [self.say(n, mod=n == 1).id for n in range(10)]
        self.assertEqual(self.store.make_room(0, 5, 0), 5)                  # (a chunk that is smaller than the lines that may go: the moderator's stays)
        self.assertIn(again[1], [r[0] for r in self.store.lines(1000)])

    def test_a_line_holds_the_role_of_its_author_0_none_1_moderator_2_administrator_and_a_bool_is_0_or_1(self):
        for mod in (0, 1, 2, False, True):
            self.say(0, mod=mod)
        self.assertEqual([l.mod for l in self.store.recent_lines(10)], [0, 1, 2, 0, 1])

    def test_make_room_takes_the_lines_of_a_moderator_and_of_an_administrator_both_after_the_others(self):
        ids = [self.say(n, mod=2 if n == 4 else 1 if n == 9 else 0).id for n in range(50)]
        self.assertEqual(self.store.make_room(0, 45, 3), 45)                # (the 47 oldest lines that hold no role)
        self.assertEqual([r[0] for r in self.store.lines(1000)][::-1], [ids[4], ids[9]] + ids[47:])
        self.assertEqual(self.store.make_room(0, 10, 3), 2)                 # (and now both go: the role of the author is no other reason to keep a line)
        self.assertEqual([r[0] for r in self.store.lines(1000)][::-1], ids[47:])

    def test_make_room_takes_the_lines_that_a_moderator_deleted_after_the_others_too(self):
        ids = [self.say(n).id for n in range(30)]
        self.store.delete_line(ids[2], "me")
        self.store.delete_line(ids[7], "me")
        self.assertEqual(self.store.make_room(0, 23, 5), 23)                        # (all but the newest 5 and the two that were deleted: they are kept for moderation)
        self.assertEqual([r[0] for r in self.store.lines(1000)][::-1], [ids[2], ids[7]] + ids[25:])
        self.assertEqual(self.store.make_room(0, 100, 5), 2)                        # (and now they go)
        self.assertEqual([r[0] for r in self.store.lines(1000)][::-1], ids[25:])
        self.assertEqual(self.store.last_deletion(), 2)                             # (the counter of deletions stays)

    def test_make_rooms_chunk_is_for_all_the_lines_of_the_call_whoever_said_them(self):
        ids = [self.say(n, mod=n in (1, 2, 3, 4, 5)).id for n in range(8)]          # (three lines of anybody may go, and five of a moderator)
        self.assertEqual(self.store.make_room(0, 5, 0), 5)                          # (the three, and the two oldest of the moderator's: not the three and the five)
        self.assertEqual([r[0] for r in self.store.lines(1000)][::-1], ids[3:6])

    def test_make_room_leaves_the_mutes_the_bans_the_deletion_counter_and_the_logins(self):
        old = self.say(1)
        for n in range(30):
            self.say(n)
        self.store.delete_line(old.id, "me")
        self.store.mute("1001", 600, "me", "alice")
        self.store.ban("1002", "me", "bob")
        self.store.add_session("k1", "1001", "alice", 3600, 10, 100)
        before = (self.store.mutes(), self.store.bans(), self.store.last_deletion(), self.store.session("k1"))
        self.assertGreater(self.store.make_room(0, 1000, 5), 0)
        self.assertEqual((self.store.mutes(), self.store.bans(), self.store.last_deletion(), self.store.session("k1")), before)
        self.assertEqual(self.store.last_deletion(), 1)
        self.assertEqual(len(self.store.lines(1000)), 5)

    def test_a_line_that_is_said_after_every_line_went_has_a_number_that_was_never_used(self):
        last = max(self.say(n).id for n in range(5))
        self.assertEqual(self.store.make_room(0, 100, 0), 5)
        self.assertGreater(self.say(9).id, last)                            # (the screens and the hub's memory go by the numbers: one is never used twice)

    def test_usage_is_the_bytes_in_use_what_is_free_to_reuse_the_lines_the_oldest_and_the_log(self):
        empty = self.store.usage()
        self.assertEqual((empty.lines, empty.oldest), (0, 0))
        self.assertEqual(empty.bytes + empty.log, self.store.size())
        first = self.say(1)
        self.clock.now += 100
        for n in range(300):
            self.say(n)
        used = self.store.usage()
        self.assertEqual((used.lines, used.oldest), (301, first.at))
        self.assertGreater(used.bytes, empty.bytes)
        self.assertEqual(used.bytes + used.log, self.store.size())
        self.store.make_room(0, 1000, 10)
        after = self.store.usage()
        self.assertEqual(after.lines, 10)
        self.assertLess(after.bytes, used.bytes)
        self.assertGreaterEqual(after.free, 0)

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


    # ---- profiles ----

    def test_a_profile_is_what_the_last_login_said_and_a_new_one_picks_the_display_name_where_there_is_one(self):
        self.assertIsNone(self.store.profile("1001"))
        self.store.set_profile("1001", "alice", "Alice A", 100)
        self.assertIsInstance(self.store.profile("1001"), base.Profile)
        self.assertEqual(tuple(self.store.profile("1001")), ("alice", "Alice A", "display"))
        self.store.set_profile("1002", "bob", "", 100)
        self.assertEqual(tuple(self.store.profile("1002")), ("bob", "", "username"))
        self.assertIsNone(self.store.profile("1003"))

    def test_a_login_refreshes_both_names_and_keeps_the_pick(self):
        self.store.set_profile("1001", "alice", "Alice A", 100)
        self.assertTrue(self.store.set_pick("1001", "username"))
        self.store.set_profile("1001", "alicia", "Alice B", 100)
        self.assertEqual(tuple(self.store.profile("1001")), ("alicia", "Alice B", "username"))
        self.assertTrue(self.store.set_pick("1001", "display"))
        self.store.set_profile("1001", "alicia", "", 100)                       # (the display name is gone: the pick stays, and there is nothing to choose between)
        self.assertEqual(tuple(self.store.profile("1001")), ("alicia", "", "display"))
        self.store.set_profile("1001", "alicia", "Alice C", 100)
        self.assertEqual(tuple(self.store.profile("1001")), ("alicia", "Alice C", "display"))

    def test_an_account_that_had_no_display_name_and_now_has_one_is_shown_by_it_and_a_choice_that_was_made_stays(self):
        self.store.set_profile("1001", "alice", "", 100)
        self.assertEqual(self.store.profile("1001").pick, "username")
        self.store.set_profile("1001", "alice", "Alice A", 100)
        self.assertEqual(self.store.profile("1001").pick, "display")
        self.assertTrue(self.store.set_pick("1001", "username"))
        self.store.set_profile("1001", "alice", "Alice B", 100)
        self.assertEqual(self.store.profile("1001").pick, "username")

    def test_a_pick_needs_a_profile_with_a_display_name_and_one_of_the_two_words(self):
        self.assertFalse(self.store.set_pick("1001", "display"))               # (no profile)
        self.store.set_profile("1001", "alice", "", 100)
        self.assertFalse(self.store.set_pick("1001", "display"))
        self.assertFalse(self.store.set_pick("1001", "username"))
        self.store.set_profile("1001", "alice", "Alice A", 100)
        for word in ("other", "", None, 5, "Display", ["display"]):
            self.assertFalse(self.store.set_pick("1001", word), repr(word))
        self.assertEqual(self.store.profile("1001").pick, "display")
        self.assertTrue(self.store.set_pick("1001", "username"))
        self.assertTrue(self.store.set_pick("1001", "username"))
        self.assertEqual(self.store.profile("1001").pick, "username")
        self.assertIsNone(self.store.profile("1002"))

    def test_a_pick_is_no_login_and_does_not_make_a_profile_newer(self):
        self.store.set_profile("1001", "alice", "Alice A", 100)
        self.clock.now += 20 * DAY
        self.assertTrue(self.store.set_pick("1001", "username"))
        self.clock.now += base.PROFILE_DAYS * DAY - 20 * DAY + 1
        self.store.purge()
        self.assertIsNone(self.store.profile("1001"))                           # (only a login keeps a profile: it is as old as the last login that refreshed it)

    def test_an_account_is_found_by_its_username_when_exactly_one_has_it(self):
        self.assertIsNone(self.store.account_of_username("alice"))
        self.store.set_profile("1001", "alice", "Shown Name", 100)
        self.assertEqual(self.store.account_of_username("alice"), "1001")
        self.assertIsNone(self.store.account_of_username("Shown Name"))          # (a name that is shown is not a username)
        self.assertIsNone(self.store.account_of_username("nobody"))
        self.store.set_profile("1002", "alice", "", 100)                         # (a username that was given up and taken: two profiles have it, each as old as its last login)
        self.assertIsNone(self.store.account_of_username("alice"))
        self.store.set_profile("1001", "alicia", "Shown Name", 100)              # (the first account logs in again, under its new username)
        self.assertEqual(self.store.account_of_username("alice"), "1002")
        self.assertEqual(self.store.account_of_username("alicia"), "1001")
        self.say(1, "1003", "carol")
        self.assertIsNone(self.store.account_of_username("carol"))              # (a line is no profile: a name that was spoken is not looked up)

    def test_two_accounts_may_show_one_name_and_each_is_found_by_its_own_username(self):
        self.store.set_profile("1001", "aaa.one", "Maple", 100)
        self.store.set_profile("1002", "bbb.two", "Maple", 100)
        self.assertEqual((self.store.account_of_username("aaa.one"), self.store.account_of_username("bbb.two")), ("1001", "1002"))
        self.assertIsNone(self.store.account_of_username("Maple"))

    def test_the_profiles_that_are_kept_are_the_newest_and_the_ones_refreshed_longest_ago_go(self):
        for n in range(4):
            self.store.set_profile("100%d" % n, "user%d" % n, "", 3)
            self.clock.now += 1
        self.assertEqual([self.store.profile("100%d" % n) is not None for n in range(4)], [False, True, True, True])
        self.store.set_profile("1001", "user1", "", 3)                           # (a login refreshes: this one is the newest now)
        self.clock.now += 1
        self.store.set_profile("1004", "user4", "", 3)
        self.assertEqual([self.store.profile("100%d" % n) is not None for n in range(5)], [False, True, False, True, True])

    def test_forget_removes_the_profile_with_the_rest_of_the_account_and_no_other(self):
        self.store.set_profile("1001", "alice", "Alice A", 100)
        self.store.set_profile("1002", "bob", "Bobby", 100)
        self.say(1, "1001", "Alice A")
        self.store.forget("1001")
        self.assertIsNone(self.store.profile("1001"))
        self.assertIsNone(self.store.account_of_username("alice"))
        self.assertEqual(tuple(self.store.profile("1002")), ("bob", "Bobby", "display"))
        self.assertFalse(self.store.set_pick("1001", "username"))

    def test_a_profile_lives_as_long_as_a_login_can_and_a_little_longer_and_a_login_makes_it_live_on(self):
        self.store.set_profile("1001", "alice", "Alice A", 100)
        self.store.set_profile("1002", "bob", "", 100)
        self.clock.now += 20 * DAY
        self.store.set_profile("1002", "bob", "", 100)                           # (bob logs in again)
        self.clock.now += base.PROFILE_DAYS * DAY - 20 * DAY
        self.store.purge()
        self.assertIsNotNone(self.store.profile("1001"), "35 days to the second: kept")
        self.clock.now += 1
        self.store.purge()
        self.assertIsNone(self.store.profile("1001"))
        self.assertIsNotNone(self.store.profile("1002"))
        self.assertEqual(base.PROFILE_DAYS, 35)

    def test_the_size_counts_the_profiles_that_are_kept(self):
        empty = self.store.size()
        for n in range(300):
            self.store.set_profile(str(2000 + n), "user%d" % n, "A Display Name Number %d" % n, 1000)
        full = self.store.size()
        self.assertGreater(full, empty)
        self.clock.now += (base.PROFILE_DAYS + 1) * DAY
        self.store.purge()
        self.assertLess(self.store.size(), full)

    def test_a_display_name_is_kept_as_it_is(self):
        name = "Zo\u00eb \u4e2d\u6587 \u0645\u0631\u064a\u0645 'quote' - _ ."
        self.store.set_profile("1001", "zoe", name, 100)
        self.assertEqual(self.store.profile("1001").display, name)

    # ---- the number of the last change of the bans and the mutes ----

    def test_the_last_restriction_goes_up_at_every_ban_unban_and_mute_and_at_nothing_else(self):
        numbers = [self.store.last_restriction()]

        def goes_up(what):
            numbers.append(self.store.last_restriction())
            self.assertGreater(numbers[-1], numbers[-2], what)

        def stays(what):
            self.assertEqual(self.store.last_restriction(), numbers[-1], what)
        self.store.ban("1001", "me", "alice")
        goes_up("a ban")
        self.store.ban("1001", "me", "alice")
        goes_up("a ban made again")
        self.assertTrue(self.store.unban("1001"))
        goes_up("a ban lifted")
        self.store.mute("1002", 10, "me", "bob")
        goes_up("a mute")
        self.store.mute("1002", 30, "me", "bob")
        goes_up("a mute made again")
        self.store.mute("1002", 0, "me")
        goes_up("a mute lifted")
        self.store.mute("1003", 1, "me", "carol")
        goes_up("a mute that is to run out")
        before = self.store.last_restriction()
        a = self.say(1)
        self.store.delete_line(a.id, "me")
        self.store.add_session("k1", "1001", "alice", 1000, 10, 100)
        self.store.delete_session("k1")
        self.store.forget("1003")
        for read in (self.store.bans, self.store.mutes, lambda: self.store.is_banned("1003"), lambda: self.store.muted_until("1003"), lambda: self.store.recent_lines(10), self.store.size):
            read()
        stays("lines, a deletion, a login, forget and every read: the bans and the mutes are as they were")
        self.clock.now += 3 * DAY
        self.assertEqual(self.store.mutes(), [], "the mute has run out")
        self.store.purge()
        self.assertEqual(self.store.last_restriction(), before, "a mute that runs out, and the purge that deletes it, change no number: its end is in mutes()")

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


    # ---- the hub's secret and the moderators' log ----

    REF_A, REF_B = "0123456789abcdef", "fedcba9876543210"

    def test_the_secret_is_32_bytes_the_same_every_time_and_not_the_same_in_two_stores(self):
        self.assertEqual((type(self.store.secret()), len(self.store.secret())), (bytes, 32))
        self.assertEqual(self.store.secret(), self.store.secret())
        other = self.make_store(self.clock)
        self.addCleanup(other.close)
        self.assertNotEqual(other.secret(), self.store.secret())

    def test_an_action_with_a_note_leaves_a_row_in_the_same_step_and_one_without_leaves_none(self):
        A, B = self.REF_A, self.REF_B
        self.say(1)
        self.store.mute("1002", 5, "me", "bob")
        self.store.ban("1002", "me", "bob")
        self.assertEqual(self.store.actions(), [])
        self.store.mute("1002", 5, A, "bob", note=(A, "mute", B, "5"))
        self.clock.now += 1
        self.store.mute("1002", 0, A, "", note=(A, "unmute", B, ""))
        self.clock.now += 1
        self.store.ban("1002", A, "bob", note=(A, "ban", B, ""))
        self.clock.now += 1
        self.assertTrue(self.store.unban("1002", note=(A, "unban", B, "")))
        self.clock.now += 1
        self.assertTrue(self.store.delete_line(1, A, note=(A, "del", B, "1")))
        self.clock.now += 1
        self.assertEqual(self.store.end_sessions("1002", note=(A, "kick", B, "")), 0)
        rows = self.store.actions()
        self.assertEqual([(r.action, r.actor, r.target, r.detail) for r in rows], [("kick", A, B, ""), ("del", A, B, "1"), ("unban", A, B, ""), ("ban", A, B, ""), ("unmute", A, B, ""), ("mute", A, B, "5")])
        self.assertEqual([r.at for r in rows], sorted((r.at for r in rows), reverse=True))
        self.assertTrue(all(isinstance(r.id, int) for r in rows))
        self.assertEqual(self.store.actions(since=rows[3].at)[-1].action, "ban")            # (from a time on)
        self.assertEqual(len(self.store.actions(limit=2)), 2)

    def test_a_note_is_written_only_for_an_action_that_was_done(self):
        A, B = self.REF_A, self.REF_B
        self.assertFalse(self.store.unban("1002", note=(A, "unban", B, "")))                # (nobody was banned)
        self.assertEqual(self.store.mute("1002", 0, A, "", note=(A, "unmute", B, "")), 0)   # (nobody was muted)
        self.assertFalse(self.store.delete_line(99, A, note=(A, "del", B, "99")))           # (no such line)
        self.assertEqual(self.store.actions(), [])

    def test_a_note_that_is_not_a_note_is_refused_and_the_action_is_undone(self):
        A, B = self.REF_A, self.REF_B
        for note in ((A, "mute", "1002", "5"), ("1001", "mute", B, "5"), (A, "frobnicate", B, ""), (A, "mute", B, "5 minutes please"), (A, "mute", "73110000000000000099", "5"), ("1" * 19, "ban", B, ""),
                     (A, "mute", B, "x" * 13), (A, "mute", "A" * 16, "5")):
            with self.assertRaises(ValueError, msg=note):
                self.store.ban("1002", A, "bob", note=note)
            self.assertFalse(self.store.is_banned("1002"), note)                             # (the ban was in the same step: nothing is left of it)
        self.assertEqual((self.store.actions(), self.store.last_restriction()), ([], 0))
        self.store.ban("1002", A, "bob", note=("terminal", "ban", "", ""))                   # (the command line is an actor; an action may have no target)
        self.assertEqual(self.store.actions()[0].actor, "terminal")

    def test_a_refused_write_leaves_no_row_and_no_action(self):
        A, B = self.REF_A, self.REF_B
        self.store.ban("1002", A, "bob", note=(A, "ban", B, ""))
        with self.refusing_writes(self.store):
            for write in (lambda: self.store.mute("1002", 5, A, "bob", note=(A, "mute", B, "5")), lambda: self.store.unban("1002", note=(A, "unban", B, "")),
                          lambda: self.store.end_sessions("1002", note=(A, "kick", B, "")), lambda: self.store.ban("1003", A, "carol", note=(A, "ban", B, ""))):
                with self.assertRaises(base.StoreError):
                    write()
        self.assertEqual([r.action for r in self.store.actions()], ["ban"])
        self.assertTrue(self.store.is_banned("1002"))
        self.assertFalse(self.store.is_banned("1003"))

    def test_the_log_keeps_30_days_and_10000_rows_and_the_purge_does_both(self):
        A, B = self.REF_A, self.REF_B
        self.store.max_modlog = 10 ** 9
        for n in range(5):
            self.store.mute("1002", 5, A, "bob", note=(A, "mute", B, str(n)))
            self.clock.now += 10 * DAY
        self.assertEqual(len(self.store.actions()), 5)
        self.store.purge()
        self.assertEqual([r.detail for r in self.store.actions()], ["4", "3", "2"])         # (the two that are older than 30 days are gone: 50 and 40 days)
        self.store.max_modlog = 2
        self.store.purge()
        self.assertEqual([r.detail for r in self.store.actions()], ["4", "3"])               # (and the rows past the newest 2)
        self.assertEqual(base.MODLOG_DAYS, 30)
        self.assertEqual(base.MODLOG_ROWS, 10000)
        self.assertEqual((self.store.modlog_days, self.store.max_modlog), (30, 2))

    def test_the_log_never_holds_more_than_its_cap_even_between_two_purges(self):
        A, B = self.REF_A, self.REF_B
        self.store.max_modlog = 20
        for n in range(60):
            self.store.mute("1002", 5, A, "bob", note=(A, "mute", B, str(n)))
        self.assertLessEqual(len(self.store.actions()), 20)
        self.assertEqual(self.store.actions()[0].detail, "59")

    def test_forget_and_a_purge_of_profiles_do_not_take_the_log_away(self):
        A, B = self.REF_A, self.REF_B
        self.store.set_profile("1002", "bob", "", 100)
        self.store.ban("1002", A, "bob", note=(A, "ban", B, ""))
        self.store.forget("1002")
        self.assertEqual([r.action for r in self.store.actions()], ["ban"])

    def test_references_are_found_by_their_profile_and_by_the_accounts_that_the_data_knows(self):
        from hub import pseudo
        secret = self.store.secret()
        self.store.set_profile("1001", "alice", "", 100)
        self.say(1, account="1003", name="carol")
        self.store.ban("1004", "me", "dave")
        refs = {a: pseudo.reference(secret, a) for a in ("1001", "1003", "1004", "1009")}
        self.assertEqual(self.store.usernames_by_reference(list(refs.values())), {refs["1001"]: "alice"})
        self.assertEqual(self.store.usernames_by_reference([]), {})
        self.assertEqual([self.store.account_of_reference(refs[a]) for a in ("1001", "1003", "1004", "1009")], ["1001", "1003", "1004", None])
        self.assertIsNone(self.store.account_of_reference("0" * 16))

    def test_end_sessions_ends_every_login_of_the_account_and_no_other(self):
        self.store.add_session("k1", "1001", "alice", 60, 10, 100)
        self.store.add_session("k2", "1001", "alice", 60, 10, 100)
        self.store.add_session("k3", "1002", "bob", 60, 10, 100)
        self.assertEqual(self.store.end_sessions("1001"), 2)
        self.assertEqual((self.store.session("k1"), self.store.session("k2")), (None, None))
        self.assertIsNotNone(self.store.session("k3"))
        self.assertEqual(self.store.end_sessions("1001"), 0)

    def test_no_representation_of_a_line_shows_the_account(self):
        line = self.say(1, account="73110000000000009901")
        for text in (repr(line), str(line), "%r" % (line,), repr([line])):
            self.assertNotIn("7311", text)
            self.assertNotIn("9901", text)
        self.assertEqual(line.account, "73110000000000009901")

    # ---- pictures ----

    def login(self, account="1001", seconds=30 * DAY):
        """A login of the account, if it has none: a picture is kept only for an account that has one."""
        if self.store.logins(account) == 0:
            self.store.add_session("k-" + account, account, "alice", seconds, 10, 1000)

    def keep(self, account="1001", name=None, png=b"\x89PNG-one", until=None, rows=100, size=10 ** 6):
        self.login(account)
        name = name or ("%032x" % (abs(hash((account, png))) % 16 ** 32))
        return name, self.store.set_picture(account, png, name, self.clock.now + DAY if until is None else until, rows, size)

    def test_a_picture_is_kept_under_its_name_and_a_new_one_takes_the_place_of_the_old(self):
        first, kept = self.keep(name="a" * 32, png=b"one")
        self.assertTrue(kept)
        self.assertEqual((self.store.picture(first), self.store.has_picture("1001"), self.store.picture_state("1001")), (b"one", True, base.SHOW_ON))
        second, kept = self.keep(name="b" * 32, png=b"two")
        self.assertTrue(kept)
        self.assertEqual((self.store.picture(first), self.store.picture(second), self.store.picture_usage()), (None, b"two", (1, 3)))
        self.assertEqual(list(self.store.picture_names().values()), [second])

    def test_a_name_that_is_not_the_shape_of_one_finds_nothing_and_a_picture_with_a_bad_name_is_not_kept(self):
        name, _ = self.keep(name="c" * 32)
        for odd in ("c" * 31, "c" * 33, "C" * 32, "c" * 31 + "%", "c" * 32 + "\n", "", None, 5, b"c" * 32, "../" + "c" * 29):
            self.assertIsNone(self.store.picture(odd), repr(odd))
        for odd in ("d" * 31, "D" * 32, "", None, "x" * 32):
            self.assertFalse(self.store.set_picture("1002", b"x", odd, self.clock.now + DAY, 100, 1000), repr(odd))
        self.assertFalse(self.store.has_picture("1002"))
        self.assertEqual(self.store.picture(name), b"\x89PNG-one")

    def test_the_picture_of_an_account_is_one_row_with_no_account_id_in_it(self):
        if not hasattr(self.store, "db"):
            self.skipTest("a backend that is not a file")
        self.keep(account="73110000000000009901", name="e" * 32)
        rows = self.store.db.execute("SELECT * FROM pictures").fetchall()
        self.assertEqual(len(rows), 1)
        self.assertNotIn("7311", " ".join(str(c) for c in rows[0] if not isinstance(c, bytes)))
        from hub.pseudo import reference
        self.assertEqual(rows[0][0], reference(self.store.secret(), "73110000000000009901"))
        self.assertEqual(self.store.picture_names(), {rows[0][0]: "e" * 32})

    def test_a_picture_is_not_given_past_its_time_and_the_purge_deletes_it(self):
        name, _ = self.keep(until=self.clock.now + 100)
        self.assertEqual(self.store.picture(name), b"\x89PNG-one")
        self.clock.now += 100
        self.assertIsNone(self.store.picture(name))
        self.assertEqual(self.store.picture_names(), {})
        self.assertEqual(self.store.picture_usage(), (1, 8))
        self.store.purge()
        self.assertEqual(self.store.picture_usage(), (0, 0))
        self.assertFalse(self.store.has_picture("1001"))

    def test_switching_it_off_deletes_the_copy_and_it_is_not_kept_while_it_is_off(self):
        name, _ = self.keep()
        self.assertEqual(self.store.set_picture_show("1001", base.SHOW_OFF), base.SHOW_OFF)
        self.assertEqual((self.store.picture(name), self.store.has_picture("1001"), self.store.picture_usage(), self.store.picture_state("1001")), (None, False, (0, 0), base.SHOW_OFF))
        self.assertFalse(self.keep(name="f" * 32)[1])
        self.assertEqual(self.store.picture_usage(), (0, 0))
        self.assertEqual(self.store.set_picture_show("1001", base.SHOW_ON), base.SHOW_ON)
        self.assertEqual(self.store.picture_state("1001"), base.SHOW_ON)
        self.assertTrue(self.keep(name="f" * 32)[1])

    def test_a_block_is_lifted_by_the_operator_only_and_it_deletes_the_copy(self):
        name, _ = self.keep()
        self.assertEqual(self.store.set_picture_show("1001", base.SHOW_BLOCKED, operator=True), base.SHOW_BLOCKED)
        self.assertIsNone(self.store.picture(name))
        for wanted in (base.SHOW_ON, base.SHOW_OFF):
            self.assertEqual(self.store.set_picture_show("1001", wanted), base.SHOW_BLOCKED)
        self.assertEqual(self.store.picture_state("1001"), base.SHOW_BLOCKED)
        self.assertFalse(self.keep(name="a1" * 16)[1])
        self.assertEqual(self.store.set_picture_show("1001", base.SHOW_ON, operator=True), base.SHOW_ON)
        self.assertEqual(self.store.picture_state("1001"), base.SHOW_ON)
        with self.assertRaises(ValueError):
            self.store.set_picture_show("1001", 3)

    def test_clearing_deletes_the_bytes_and_the_name_and_keeps_a_switch(self):
        name, _ = self.keep()
        self.assertTrue(self.store.clear_picture("1001"))
        self.assertFalse(self.store.clear_picture("1001"))
        self.assertIsNone(self.store.picture(name))
        self.store.set_picture_show("1002", base.SHOW_OFF)
        self.assertFalse(self.store.clear_picture("1002"))
        self.assertEqual(self.store.picture_state("1002"), base.SHOW_OFF)

    def test_forget_me_deletes_the_picture_and_a_block_stays(self):
        name, _ = self.keep()
        self.store.set_picture_show("1002", base.SHOW_BLOCKED, operator=True)
        self.store.set_picture_show("1003", base.SHOW_OFF)
        self.store.forget("1001")
        self.store.forget("1002")
        self.store.forget("1003")
        self.assertEqual((self.store.picture(name), self.store.picture_usage()), (None, (0, 0)))
        self.assertEqual((self.store.picture_state("1001"), self.store.picture_state("1002"), self.store.picture_state("1003")), (base.SHOW_ON, base.SHOW_BLOCKED, base.SHOW_ON))

    def test_a_kick_deletes_the_picture_with_the_logins_and_another_accounts_is_left(self):
        mine, _ = self.keep()
        other, _ = self.keep(account="1002", name="9" * 32, png=b"other")
        self.store.add_session("k1", "1001", "alice", 60, 10, 100)
        self.store.end_sessions("1001")
        self.assertEqual((self.store.picture(mine), self.store.picture(other)), (None, b"other"))

    def test_the_caps_take_the_pictures_fetched_longest_ago_first(self):
        for n in range(5):
            self.clock.now += 1
            self.keep(account="10%02d" % n, name="%032x" % (n + 1), png=b"p" * 10, rows=4, size=10 ** 6)
        self.assertEqual(self.store.picture_usage(), (4, 40))
        self.assertEqual(sorted(self.store.picture_names().values()), ["%032x" % n for n in (2, 3, 4, 5)])
        for n in range(5, 8):
            self.clock.now += 1
            self.keep(account="10%02d" % n, name="%032x" % (n + 1), png=b"p" * 10, rows=100, size=40)
        self.assertEqual(self.store.picture_usage(), (4, 40))
        self.assertEqual(sorted(self.store.picture_names().values()), ["%032x" % n for n in (5, 6, 7, 8)])
        self.assertFalse(self.keep(account="1099", name="e" * 32, png=b"q" * 41, size=40)[1])               # (a picture over the byte cap is not kept at all)
        self.assertFalse(self.keep(account="1099", name="e" * 32, png=b"", size=40)[1])
        self.assertEqual(self.store.picture_usage(), (4, 40))

    def test_a_picture_of_a_new_login_that_does_not_fit_never_takes_its_own_room_away(self):
        self.keep(account="1001", name="a" * 32, png=b"x" * 30, size=40)
        self.clock.now += 1
        name, kept = self.keep(account="1002", name="b" * 32, png=b"y" * 30, size=40)
        self.assertTrue(kept)
        self.assertEqual((self.store.picture_usage(), list(self.store.picture_names().values())), ((1, 30), [name]))

    def test_the_marks_of_a_switch_are_capped_and_the_oldest_go_and_an_off_is_remembered_for_four_hundred_days(self):
        self.store.max_picture_marks = 3
        for n in range(5):
            self.clock.now += 1
            self.store.set_picture_show("20%02d" % n, base.SHOW_OFF)
        self.assertEqual([self.store.picture_state("20%02d" % n) for n in range(5)], [base.SHOW_ON, base.SHOW_ON, base.SHOW_OFF, base.SHOW_OFF, base.SHOW_OFF])
        self.store.max_picture_marks = 100000
        self.store.set_picture_show("2900", base.SHOW_BLOCKED, operator=True)
        self.clock.now += base.PICTURE_DAYS * DAY - 10
        self.assertEqual(self.store.picture_state("2002", touch=True), base.SHOW_OFF)              # (a login of an account that is off: it is remembered from now)
        self.clock.now += 20
        self.store.purge()
        self.assertEqual([self.store.picture_state("20%02d" % n) for n in (2, 3, 4)], [base.SHOW_OFF, base.SHOW_ON, base.SHOW_ON])
        self.assertEqual(self.store.picture_state("2900"), base.SHOW_BLOCKED)                      # (a block stays: like a ban)

    def test_the_number_of_the_last_change_goes_up_with_every_change_and_with_none_other(self):
        seen = [self.store.last_picture_change()]

        def moved():
            seen.append(self.store.last_picture_change())
            return seen[-1] > seen[-2]
        self.keep(name="a" * 32)
        self.assertTrue(moved())
        self.store.picture("a" * 32)
        self.store.picture_names()
        self.store.picture_state("1001")
        self.assertFalse(moved())
        self.store.set_picture_show("1001", base.SHOW_OFF)
        self.assertTrue(moved())
        self.store.clear_picture("1001")
        self.assertFalse(moved())                                                                  # (nothing was there)
        self.keep(account="1002", name="b" * 32)
        self.store.clear_picture("1002")
        self.assertEqual([moved(), moved()], [True, False])

    def test_the_logins_of_an_account_are_counted_and_the_ones_that_ran_out_are_not(self):
        self.assertEqual(self.store.logins("1001"), 0)
        self.store.add_session("k1", "1001", "alice", 60, 10, 100)
        self.store.add_session("k2", "1001", "alice", 120, 10, 100)
        self.store.add_session("k3", "1002", "bob", 60, 10, 100)
        self.assertEqual((self.store.logins("1001"), self.store.logins("1002")), (2, 1))
        self.clock.now += 90
        self.assertEqual(self.store.logins("1001"), 1)
        self.store.delete_session("k2")
        self.assertEqual(self.store.logins("1001"), 0)

    def test_a_picture_is_not_kept_for_an_account_with_no_login_or_with_a_ban_and_never_outlives_the_last_login(self):
        self.assertFalse(self.store.set_picture("1001", b"x", "a" * 32, self.clock.now + DAY, 10, 1000))             # (no login: a fetch that ended after Forget me or the last log out)
        self.store.add_session("k1", "1001", "alice", 100, 10, 100)
        self.clock.now += 100                                                                                    # (a login that ran out)
        self.assertFalse(self.store.set_picture("1001", b"x", "a" * 32, self.clock.now + DAY, 10, 1000))
        self.store.add_session("k2", "1001", "alice", 500, 10, 100)
        self.store.ban("1001", "me", "alice")
        self.assertFalse(self.store.set_picture("1001", b"x", "a" * 32, self.clock.now + DAY, 10, 1000))
        self.assertEqual((self.store.picture_usage(), self.store.has_picture("1001")), ((0, 0), False))
        self.assertTrue(self.store.unban("1001"))
        self.assertTrue(self.store.set_picture("1001", b"x", "a" * 32, self.clock.now + 99 * DAY, 10, 1000))
        self.clock.now += 499
        self.assertEqual(self.store.picture("a" * 32), b"x")
        self.clock.now += 1
        self.assertIsNone(self.store.picture("a" * 32))

    def test_the_last_login_to_go_takes_the_picture_by_any_way_it_goes(self):
        name, _ = self.keep(name="a" * 32)
        self.store.add_session("k2", "1001", "alice", 60, 10, 100)
        self.store.delete_session("k-1001")
        self.assertEqual(self.store.picture(name), b"\x89PNG-one")                                              # (another login is left)
        self.store.delete_session("k2")
        self.assertIsNone(self.store.picture(name))                                                              # (the last one went)
        self.assertEqual(self.store.picture_usage(), (0, 0))

    def test_the_purge_takes_the_picture_of_an_account_whose_last_login_ran_out_and_leaves_the_others(self):
        self.login("1001", 100)
        self.store.add_session("k3", "1001", "alice", 10 * DAY, 10, 100)        # (the account's newer login: the picture, fetched for it, runs as long as it does)
        mine, _ = self.keep(name="a" * 32)
        other, _ = self.keep(account="1002", name="b" * 32, png=b"other")
        self.store.delete_session("k3")                                         # (the newer one is logged out: the older is left, and the picture with it)
        self.assertEqual(self.store.picture(mine), b"\x89PNG-one")
        self.clock.now += 100                                                   # (the older one runs out: the picture's own time is far off, and it must go all the same)
        self.store.purge()
        self.assertEqual((self.store.picture(mine), self.store.picture(other)), (None, b"other"))

    def test_a_login_pushed_out_by_the_cap_of_all_logins_takes_its_accounts_picture_when_it_was_the_last(self):
        self.store.add_session("k1", "1001", "alice", 60, 10, 2)
        name, _ = self.keep(name="a" * 32)
        self.clock.now += 1
        self.store.add_session("k2", "1002", "bob", 60, 10, 2)
        self.assertEqual(self.store.picture(name), b"\x89PNG-one")
        self.clock.now += 1
        self.store.add_session("k3", "1003", "carol", 60, 10, 2)                 # (the oldest login of all goes)
        self.assertIsNone(self.store.picture(name))
        self.assertEqual(self.store.picture_usage(), (0, 0))

    def test_a_picture_is_not_served_after_the_last_login_has_run_out_whatever_logins_ended_before_it_and_with_no_purge(self):
        self.store.add_session("old", "1001", "alice", 100, 10, 100)
        self.store.add_session("new", "1001", "alice", 1000, 10, 100)
        name, kept = self.keep(name="a" * 32, until=self.clock.now + 5000)
        self.assertTrue(kept)
        self.clock.now += 50
        self.store.delete_session("new")                                          # (the newer login is logged out: the older one is the last, and ends at 100)
        self.clock.now += 49
        self.assertEqual((self.store.picture(name), list(self.store.picture_names().values())), (b"\x89PNG-one", [name]))
        self.clock.now += 1                                                       # (the older one has run out: no purge has run)
        self.assertEqual((self.store.logins("1001"), self.store.picture(name), self.store.picture_names()), (0, None, {}))

    def test_a_picture_lasts_as_long_as_the_latest_login_and_no_longer_than_it_was(self):
        self.store.add_session("a", "1001", "alice", 100, 10, 100)
        self.store.add_session("b", "1001", "alice", 700, 10, 100)
        self.store.add_session("c", "1001", "alice", 300, 10, 100)
        name, _ = self.keep(name="a" * 32, until=self.clock.now + 5000)
        self.store.delete_session("c")                                            # (a login ends and two are left: the picture's time is the latest of them)
        self.clock.now += 650
        self.assertEqual(self.store.picture(name), b"\x89PNG-one")                  # (the time was cut to the latest login's end, 700, not to the earliest or the newest)
        self.clock.now += 49
        self.assertEqual(self.store.picture(name), b"\x89PNG-one")
        self.clock.now += 1
        self.assertIsNone(self.store.picture(name))
        name2, _ = self.keep(account="1002", name="b" * 32, png=b"two", until=self.clock.now + 50)        # (an end that is nearer than the logins' is kept)
        self.store.add_session("x", "1002", "bob", 1000, 10, 100)
        self.store.add_session("y", "1002", "bob", 1000, 10, 100)
        self.store.delete_session("y")
        self.clock.now += 49
        self.assertEqual(self.store.picture(name2), b"two")
        self.clock.now += 1
        self.assertIsNone(self.store.picture(name2))

    def test_a_login_that_ran_out_and_is_not_purged_yet_is_not_a_login_left(self):
        self.store.add_session("short", "1001", "alice", 60, 10, 100)
        self.store.add_session("long", "1001", "alice", 1000, 10, 100)
        name, _ = self.keep(name="a" * 32)
        self.clock.now += 61                                                      # ("short" has run out and is still in the table)
        self.store.delete_session("long")
        self.assertIsNone(self.store.picture(name))
        self.assertEqual(self.store.picture_usage(), (0, 0))

    def test_the_purge_and_the_cap_leave_the_picture_of_an_account_that_still_has_a_login(self):
        self.store.add_session("short", "1001", "alice", 60, 10, 3)
        self.store.add_session("long", "1001", "alice", 1000, 10, 3)
        name, _ = self.keep(name="a" * 32)
        self.clock.now += 61
        self.store.purge()                                                        # ("short" runs out and goes: "long" is left)
        self.assertEqual(self.store.picture(name), b"\x89PNG-one")
        self.store.add_session("k2", "1002", "bob", 1000, 10, 3)
        self.store.add_session("k3", "1003", "carol", 1000, 10, 3)
        self.store.add_session("k4", "1004", "dan", 1000, 10, 3)                   # (the cap of 3 pushes the oldest, "long", out: the account has no login now)
        self.assertIsNone(self.store.picture(name))

    def test_the_cap_of_all_logins_leaves_the_picture_of_an_account_whose_other_login_stays(self):
        self.store.add_session("a1", "1001", "alice", 1000, 10, 3)
        self.clock.now += 1
        self.store.add_session("a2", "1001", "alice", 1000, 10, 3)
        name, _ = self.keep(name="a" * 32)
        self.clock.now += 1
        self.store.add_session("k2", "1002", "bob", 1000, 10, 3)
        self.clock.now += 1
        self.store.add_session("k3", "1003", "carol", 1000, 10, 3)                 # (the cap of 3 pushes "a1" out: "a2" is left)
        self.assertEqual((self.store.session("a1"), self.store.picture(name)), (None, b"\x89PNG-one"))

    def test_a_ban_of_one_account_stops_no_other_accounts_picture(self):
        self.store.ban("1009", "me", "mallory")
        self.assertTrue(self.keep(account="1001", name="a" * 32)[1])

    def test_a_ban_takes_the_picture_and_a_switch_or_block_that_was_made_stays(self):
        name, _ = self.keep(name="a" * 32)
        self.store.set_picture_show("1002", base.SHOW_OFF)
        self.store.ban("1001", "me", "alice")
        self.store.ban("1002", "me", "bob")
        self.assertEqual((self.store.picture(name), self.store.picture_usage()), (None, (0, 0)))
        self.assertEqual(self.store.picture_state("1002"), base.SHOW_OFF)

    def test_the_marks_are_the_accounts_that_are_off_or_blocked_and_nobody_else(self):
        self.assertEqual(self.store.picture_marks(), {})
        self.keep(account="1001", name="a" * 32)
        self.store.set_picture_show("1002", base.SHOW_OFF)
        self.store.set_picture_show("1003", base.SHOW_BLOCKED, True)
        self.store.set_picture_show("1004", base.SHOW_OFF)
        self.store.set_picture_show("1004", base.SHOW_ON)
        marks = self.store.picture_marks()
        self.assertEqual(sorted(marks.values()), [base.SHOW_OFF, base.SHOW_BLOCKED])
        self.assertEqual(len(marks), 2)

    def test_the_end_is_the_soonest_end_of_a_picture_that_may_be_shown_and_none_for_none(self):
        self.assertIsNone(self.store.picture_end())
        self.store.add_session("a", "1001", "alice", 1000, 10, 100)
        self.store.add_session("b", "1002", "bob", 500, 10, 100)
        self.store.add_session("c", "1003", "carol", 100, 10, 100)
        self.keep(account="1001", name="a" * 32, until=self.clock.now + 900)
        self.keep(account="1002", name="b" * 32, png=b"two", until=self.clock.now + 400)
        self.keep(account="1003", name="c" * 32, png=b"three", until=self.clock.now + 50)
        self.assertEqual(self.store.picture_end(), self.clock.now + 50)
        self.store.set_picture_show("1003", base.SHOW_OFF)                              # (a picture that is not shown has no end that matters)
        self.assertEqual(self.store.picture_end(), self.clock.now + 400)
        self.clock.now += 400                                                             # (nor has one that has run out)
        self.assertEqual(self.store.picture_end(), self.clock.now + 500)

    def test_a_look_is_the_counter_the_names_the_marks_and_the_end_together(self):
        name, _ = self.keep(name="a" * 32)
        self.store.set_picture_show("1002", base.SHOW_OFF)
        number, names, marks, end = self.store.picture_look()
        self.assertEqual((number, names, marks, end), (self.store.last_picture_change(), self.store.picture_names(), self.store.picture_marks(), self.store.picture_end()))
        self.assertEqual(len(names), 1)

    def test_the_note_of_a_switch_goes_into_the_log_only_when_the_state_changed(self):
        ref = "0123456789abcdef"
        note = (base.TERMINAL, "nopic", ref, "")
        self.store.set_picture_show("1001", base.SHOW_BLOCKED, True, note=note)
        self.store.set_picture_show("1001", base.SHOW_BLOCKED, True, note=note)         # (the same state: nothing changed, nothing written)
        self.store.set_picture_show("1001", base.SHOW_ON, True, note=(base.TERMINAL, "pic", ref, ""))
        self.assertEqual([(a.actor, a.action, a.target) for a in self.store.actions()], [(base.TERMINAL, "pic", ref), (base.TERMINAL, "nopic", ref)])
        with self.assertRaises(ValueError):
            self.store.set_picture_show("1001", base.SHOW_BLOCKED, True, note=(base.TERMINAL, "nopic", "1001", ""))      # (an id is no reference: refused)

    def test_a_block_is_found_and_changed_by_its_reference_when_the_account_has_no_other_row(self):
        from hub.pseudo import reference
        ref = reference(self.store.secret(), "1001")
        self.assertIsNone(self.store.picture_state_by_reference(ref))
        self.assertIsNone(self.store.set_picture_show_by_reference(ref, base.SHOW_ON, True))
        self.store.set_picture_show("1001", base.SHOW_BLOCKED, True)
        self.store.forget("1001")
        self.assertEqual(self.store.picture_state_by_reference(ref), base.SHOW_BLOCKED)
        self.assertEqual(self.store.set_picture_show_by_reference(ref, base.SHOW_ON, False), base.SHOW_BLOCKED)    # (not the person's to lift)
        self.assertEqual(self.store.set_picture_show_by_reference(ref, base.SHOW_ON, True, note=(base.TERMINAL, "pic", ref, "")), base.SHOW_ON)
        self.assertEqual((self.store.picture_state("1001"), self.store.picture_state_by_reference(ref)), (base.SHOW_ON, None))
        self.assertEqual([a.action for a in self.store.actions()], ["pic"])

    def test_the_number_of_the_last_change_goes_up_when_a_pictures_time_is_cut_and_when_the_purge_takes_a_mark(self):
        self.store.add_session("old", "1001", "alice", 100, 10, 100)
        self.store.add_session("new", "1001", "alice", 1000, 10, 100)
        self.keep(name="a" * 32, until=self.clock.now + 5000)
        before = self.store.last_picture_change()
        self.store.delete_session("new")                                                  # (the picture's time is cut to the older login's end)
        self.assertGreater(self.store.last_picture_change(), before)

    def test_the_number_of_the_last_change_goes_up_when_the_purge_takes_an_old_mark_and_nothing_else_goes(self):
        self.store.set_picture_show("1002", base.SHOW_OFF)
        before = self.store.last_picture_change()
        self.clock.now += base.PICTURE_DAYS * DAY + 10                                    # (no login, no picture: the mark is all that the purge finds)
        self.store.purge()
        self.assertGreater(self.store.last_picture_change(), before)
        self.assertEqual(self.store.picture_marks(), {})

    def test_a_refused_write_of_a_picture_is_a_store_error_and_leaves_the_picture_as_it_was(self):
        name, _ = self.keep()
        writes = (lambda: self.keep(name="b" * 32, png=b"new"), lambda: self.store.clear_picture("1001"), lambda: self.store.set_picture_show("1001", base.SHOW_OFF),
                  lambda: self.store.set_picture_show("1002", base.SHOW_OFF))
        with self.refusing_writes(self.store):
            for write in writes:
                with self.assertRaises(base.StoreError):
                    write()
        self.assertEqual(self.store.picture(name), b"\x89PNG-one")
        self.assertEqual(self.store.picture_state("1002"), base.SHOW_ON)


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

    def test_another_connection_to_the_file_sees_the_number_and_the_bans_and_mutes_that_changed_it(self):
        other = storemod.Store(self.path, clock=self.clock, busy_ms=50)           # (the command line: a process of its own that opens the same file)
        self.addCleanup(other.close)
        start = self.store.last_restriction()
        other.ban("1001", "command line", "alice")
        first = self.store.last_restriction()
        self.assertGreater(first, start)
        self.assertTrue(self.store.is_banned("1001"))
        self.store.mute("1002", 10, "me", "bob")
        second = other.last_restriction()
        self.assertGreater(second, first)
        other.mute("1002", 0, "command line")
        self.assertGreater(self.store.last_restriction(), second)
        self.assertEqual(self.store.last_restriction(), other.last_restriction())

    def test_the_pictures_are_an_additive_table_the_schema_stays_and_a_file_without_the_table_gets_it(self):
        self.assertEqual(self.store.db.execute("PRAGMA user_version").fetchone()[0], 4)
        self.assertEqual(storemod.SCHEMA, 4)
        self.login()
        self.store.set_picture("1001", b"png-bytes", "a" * 32, self.clock.now + DAY, 10, 1000)
        self.store.close()
        old = sqlite3.connect(self.path, isolation_level=None)                      # (the file as a hub of the version before the pictures left it: no such table)
        old.execute("DROP TABLE pictures")
        old.execute("DELETE FROM meta WHERE k = 'pic_seq'")
        self.assertEqual(old.execute("PRAGMA user_version").fetchone()[0], 4)
        old.close()
        self.store = storemod.Store(self.path, clock=self.clock, busy_ms=50)
        self.assertEqual(self.store.db.execute("PRAGMA user_version").fetchone()[0], 4)
        self.assertEqual((self.store.picture_usage(), self.store.picture_state("1001"), self.store.last_picture_change()), ((0, 0), base.SHOW_ON, 0))
        self.assertTrue(self.store.set_picture("1001", b"png-bytes", "a" * 32, self.clock.now + DAY, 10, 1000))          # (the login is in the file: the older hub's data is the same)
        self.assertEqual(self.store.picture("a" * 32), b"png-bytes")

    def test_a_row_that_holds_bytes_with_the_picture_off_or_blocked_is_not_given_and_not_listed(self):
        # (the store never writes such a row; this is the check that a file that was edited by hand, or a bug, cannot make a hidden picture visible)
        self.login()
        self.store.set_picture("1001", b"png-bytes", "a" * 32, self.clock.now + DAY, 10, 1000)
        for show in (base.SHOW_OFF, base.SHOW_BLOCKED):
            self.store.db.execute("UPDATE pictures SET show = ?", (show,))
            self.assertEqual((self.store.picture("a" * 32), self.store.picture_names()), (None, {}), show)
        self.store.db.execute("UPDATE pictures SET show = 1")
        self.assertEqual(self.store.picture("a" * 32), b"png-bytes")

    def test_a_deleted_picture_is_in_neither_file_once_the_log_file_is_cut(self):
        marker = b"canary-picture-bytes-" + b"7f3a" * 30
        self.login()
        self.store.set_picture("1001", marker, "a" * 32, self.clock.now + DAY, 10, 10 ** 6)
        self.store.checkpoint()
        with open(self.path, "rb") as f:
            self.assertIn(marker, f.read())                                           # (the check below can find it: it is in the file)
        self.store.clear_picture("1001")
        self.assertTrue(self.store.unsettled)                                         # (the hub's clock cuts the log file at its next look)
        self.assertTrue(self.store.checkpoint())
        for path in (self.path, self.path + "-wal"):
            if os.path.exists(path):
                with open(path, "rb") as f:
                    self.assertNotIn(marker, f.read(), path)

    def test_another_connection_sees_the_number_of_the_last_change_and_the_picture(self):
        other = storemod.Store(self.path, clock=self.clock, busy_ms=50)               # (the command line)
        self.addCleanup(other.close)
        start = self.store.last_picture_change()
        other.set_picture_show("1001", base.SHOW_BLOCKED, operator=True)
        self.assertGreater(self.store.last_picture_change(), start)
        self.assertEqual(self.store.picture_state("1001"), base.SHOW_BLOCKED)
        self.assertEqual(self.store.last_picture_change(), other.last_picture_change())

    def test_a_file_without_the_counter_reads_as_nothing_changed_yet_and_makes_it_at_the_first_change(self):
        self.store.db.execute("DELETE FROM meta WHERE k = 'restrict_seq'")        # (a file that an older hub made: its `meta` has the deletions' counter only)
        self.assertEqual(self.store.last_restriction(), 0)
        self.store.ban("1001", "me", "alice")
        self.assertEqual(self.store.last_restriction(), 1)
        self.assertEqual(self.store.last_deletion(), 0, "the deletions' counter is a row of its own")


    def test_the_profiles_are_an_additive_table_that_no_version_of_the_schema_came_with(self):
        self.assertEqual(storemod.SCHEMA, 4)                           # (version 4 came with the hub's secret and the moderators' log; the profiles were made before it, without a version)
        self.assertEqual(self.store.db.execute("PRAGMA user_version").fetchone()[0], 4)
        self.assertEqual(self.store.db.execute("SELECT name FROM sqlite_master WHERE type = 'table' AND name = 'profiles'").fetchone(), ("profiles",))
        self.assertEqual([r[1] for r in self.store.db.execute("PRAGMA table_info(profiles)")], ["account", "username", "display", "pick", "updated"])
        self.store.set_profile("1001", "alice", "Alice A", 100)
        self.say(1)
        self.assertEqual(self.store.db.execute("PRAGMA user_version").fetchone()[0], 4)       # (nothing that the hub does with profiles moves it)

    def test_a_file_that_an_earlier_hub_made_gets_the_table_and_loses_nothing_and_opening_again_changes_nothing(self):
        path = os.path.join(os.path.dirname(self.path), "earlier.db")
        db = sqlite3.connect(path, isolation_level=None)               # (the three versions of the schema as an earlier hub makes them, and a little of everything in them)
        db.executescript("""
            CREATE TABLE lines (id INTEGER PRIMARY KEY AUTOINCREMENT, at INTEGER NOT NULL, account TEXT NOT NULL, name TEXT NOT NULL, text TEXT NOT NULL,
                                mod INTEGER NOT NULL DEFAULT 0, deleted INTEGER NOT NULL DEFAULT 0, del_seq INTEGER NOT NULL DEFAULT 0, del_by TEXT);
            CREATE INDEX lines_at ON lines (at);
            CREATE INDEX lines_name ON lines (name);
            CREATE INDEX lines_del_seq ON lines (del_seq);
            CREATE TABLE mutes (account TEXT PRIMARY KEY, until INTEGER NOT NULL, at INTEGER NOT NULL, by TEXT NOT NULL, name TEXT NOT NULL);
            CREATE TABLE bans (account TEXT PRIMARY KEY, at INTEGER NOT NULL, by TEXT NOT NULL, name TEXT NOT NULL);
            CREATE TABLE meta (k TEXT PRIMARY KEY, v INTEGER NOT NULL);
            CREATE TABLE sessions (id TEXT PRIMARY KEY, account TEXT NOT NULL, name TEXT NOT NULL, created INTEGER NOT NULL, expires INTEGER NOT NULL);
            INSERT INTO meta (k, v) VALUES ('del_seq', 0);
            PRAGMA user_version = 3;
            INSERT INTO lines (at, account, name, text) VALUES (%d, '1001', 'alice', 'a line from before');
            INSERT INTO sessions (id, account, name, created, expires) VALUES ('k1', '1001', 'alice', %d, %d);
            INSERT INTO bans (account, at, by, name) VALUES ('1002', 1, 'cli', 'bob');""" % (int(self.clock.now), int(self.clock.now), int(self.clock.now) + 1000))
        db.close()
        secrets = []
        for opening in range(3):
            store = storemod.Store(path, clock=self.clock, busy_ms=50)
            self.assertEqual(store.db.execute("PRAGMA user_version").fetchone()[0], 4)         # (the file of version 3 is migrated: the secret and the log are made, and nothing else is touched)
            secrets.append(store.secret())
            self.assertEqual(store.actions(), [])
            self.assertEqual([l.text for l in store.recent_lines(10)], ["a line from before"])
            self.assertEqual(tuple(store.session("k1"))[:2], ("1001", "alice"))
            self.assertTrue(store.is_banned("1002"))
            if opening == 0:
                self.assertIsNone(store.profile("1001"))                               # (an account that logged in before profiles were kept has none: it is not made up)
                store.set_profile("1001", "alice", "Alice A", 100)
            else:
                self.assertEqual(tuple(store.profile("1001")), ("alice", "Alice A", "display"))      # (opening again keeps what is in the table)
            store.close()
        self.assertEqual((len(secrets[0]), len(set(secrets))), (32, 1))                                # (the secret is made once and is the same at every opening)

    def test_a_file_that_is_from_a_newer_hub_is_still_refused_and_the_table_does_not_make_the_file_look_newer(self):
        self.store.set_profile("1001", "alice", "Alice A", 100)
        self.store.close()
        db = sqlite3.connect(self.path, isolation_level=None)
        try:
            self.assertEqual(db.execute("PRAGMA user_version").fetchone()[0], storemod.SCHEMA)       # (what an earlier hub compares with its own number: the same)
            db.execute("PRAGMA user_version = %d" % (storemod.SCHEMA + 1))
        finally:
            db.close()
        with self.assertRaises(RuntimeError):
            storemod.Store(self.path, clock=self.clock)

    def test_a_forget_by_an_earlier_hub_would_leave_the_profile_which_the_next_purge_of_this_one_deletes_in_time(self):
        self.store.set_profile("1001", "alice", "Alice A", 100)
        self.say(1)
        self.store.db.execute("DELETE FROM sessions WHERE account = '1001'")              # (what an earlier hub's forget does: logins, lines, names of the mute and the ban; it does not know the table)
        self.store.db.execute("DELETE FROM lines WHERE account = '1001'")
        self.assertIsNotNone(self.store.profile("1001"))
        self.clock.now += (base.PROFILE_DAYS + 1) * DAY
        self.store.purge()
        self.assertIsNone(self.store.profile("1001"))

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
