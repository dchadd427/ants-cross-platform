#!/usr/bin/env python3
"""The hub's rules without a socket (hub/chat.py, limits.py, store.py, config.py, session.py, admin.py; docs/HUB.md): who may say what, the limits, the last 50 lines, bans and mutes,
history that is kept 7 days and then deleted for good, the moderators' commands and the command line.

The chat is driven with plain fake peers and a fake clock, so every limit is tested at its edge in no time. The sockets, the timers and the real process are tests/scripts/test_hub_server.py.
"""
import collections
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, REPO)
from hub import admin, chat as chatmod, config, limits as limitmod, session, store as storemod  # noqa: E402

LIMITS = limitmod.Limits()
ALICE = session.Identity("1001", "alice", False)
BOB = session.Identity("1002", "bob", False)
MOD = session.Identity("9001", "moddy", False)
YOUNG = session.Identity("1003", "newbie", True)


class Clock:
    def __init__(self, now=1000.0):
        self.now = now

    def __call__(self):
        return self.now


class FakePeer:
    def __init__(self, identity=None):
        self.identity = identity
        self.sent = []
        self.strikes = 0
        self.closed = None

    def send_text(self, data):
        self.sent.append(json.loads(data.decode("utf-8")))

    def strike(self):
        self.strikes += 1

    def kinds(self, kind):
        return [m for m in self.sent if m["t"] == kind]

    def codes(self):
        return [m["code"] for m in self.kinds("err")]


class World:
    """A chat with a store in a temporary folder, a clock that the test moves, and peers."""

    def __init__(self, mods=("9001",), login=True, limits=LIMITS, db=None):
        self.folder = None if db else tempfile.TemporaryDirectory()
        self.db = db or os.path.join(self.folder.name, "hub.db")
        self.clock = Clock()
        self.wall = Clock(1_800_000_000.0)
        self.limits = limits
        self.mods = frozenset(mods)
        self.login = login
        self.store = storemod.Store(self.db, clock=self.wall, retention_days=limits.retention_days)
        self.chat = chatmod.Chat(self.store, self.mods, limits, self.clock, login)

    def join(self, identity=None):
        peer = FakePeer(identity)
        self.chat.peers.add(peer)
        return peer

    def say(self, peer, text):
        self.chat.on_text(peer, json.dumps({"t": "say", "text": text}))

    def close(self):
        self.store.close()
        if self.folder:
            self.folder.cleanup()


class WorldTest(unittest.TestCase):
    def setUp(self):
        self.w = World()
        self.addCleanup(self.w.close)
        self.alice = self.w.join(ALICE)
        self.bob = self.w.join(BOB)
        self.reader = self.w.join(None)


class TheWelcome(WorldTest):
    def test_shape_for_a_reader_a_member_and_a_moderator(self):
        reader = self.w.chat.welcome(self.reader)
        self.assertEqual(reader, {"t": "welcome", "v": 1, "you": None, "login": True, "online": 3, "history": []})
        self.assertEqual(self.w.chat.welcome(self.alice)["you"], {"name": "alice", "mod": False})
        mod = self.w.join(MOD)
        self.assertEqual(self.w.chat.welcome(mod)["you"], {"name": "moddy", "mod": True})

    def test_login_false_when_the_hub_has_no_login(self):
        w = World(login=False)
        self.addCleanup(w.close)
        self.assertFalse(w.chat.welcome(w.join(None))["login"])

    def test_history_is_the_last_50_lines_oldest_first(self):
        for n in range(60):
            self.w.clock.now += 3                                   # (the rate limit is not the subject here)
            self.w.say(self.alice, "line %d" % n)
        history = self.w.chat.welcome(self.reader)["history"]
        self.assertEqual(len(history), 50)
        self.assertEqual([h["text"] for h in history], ["line %d" % n for n in range(10, 60)])
        self.assertEqual(set(history[0]), {"id", "at", "who", "text", "mod"})
        self.assertEqual([h["id"] for h in history], sorted(h["id"] for h in history))
        self.assertEqual(history[0]["who"], "alice")
        self.assertIs(history[0]["mod"], False)

    def test_exactly_50_and_exactly_49(self):
        for n in range(49):
            self.w.clock.now += 3
            self.w.say(self.alice, "line %d" % n)
        self.assertEqual(len(self.w.chat.welcome(self.reader)["history"]), 49)
        self.w.clock.now += 3
        self.w.say(self.alice, "line 49")
        self.assertEqual(len(self.w.chat.welcome(self.reader)["history"]), 50)
        self.w.clock.now += 3
        self.w.say(self.alice, "line 50")
        self.assertEqual(len(self.w.chat.welcome(self.reader)["history"]), 50)

    def test_the_history_survives_a_restart_without_what_was_deleted(self):
        for n in range(5):
            self.w.clock.now += 3
            self.w.say(self.alice, "line %d" % n)
        self.w.store.delete_line(3, "test")
        again = chatmod.Chat(self.w.store, self.w.mods, LIMITS, self.w.clock, True)
        self.assertEqual([l.text for l in again.ring], ["line 0", "line 1", "line 3", "line 4"])


class TheRules(WorldTest):
    def test_a_line_goes_to_everybody_with_its_id_and_time(self):
        self.w.say(self.alice, "hello")
        for peer in (self.alice, self.bob, self.reader):
            line = peer.kinds("line")
            self.assertEqual(len(line), 1)
            self.assertEqual(line[0], {"t": "line", "id": 1, "at": 1_800_000_000, "who": "alice", "text": "hello", "mod": False})
        self.assertEqual(self.alice.codes(), [])

    def test_a_reader_cannot_write(self):
        self.w.say(self.reader, "hello")
        self.assertEqual(self.reader.codes(), ["login"])
        self.assertEqual(self.reader.strikes, 1)
        self.assertEqual(self.bob.kinds("line"), [])
        self.assertEqual(self.w.store.recent_lines(50), [])

    def test_banned_muted_and_young(self):
        self.w.store.ban("1001", "test")
        self.w.say(self.alice, "hello")
        self.assertEqual(self.alice.codes(), ["banned"])
        self.w.store.mute("1002", 10, "test")
        self.w.say(self.bob, "hello")
        self.assertEqual(self.bob.codes(), ["muted"])
        young = self.w.join(YOUNG)
        self.w.say(young, "hello")
        self.assertEqual(young.codes(), ["young"])
        self.assertEqual(self.reader.kinds("line"), [])

    def test_a_ban_and_a_mute_count_at_the_next_line_and_end(self):
        self.w.say(self.alice, "one")
        self.w.store.ban("1001", "test")
        self.w.say(self.alice, "two")
        self.assertEqual([m["text"] for m in self.reader.kinds("line")], ["one"])
        self.w.store.unban("1001")
        self.w.clock.now += 5
        self.w.say(self.alice, "three")
        self.assertEqual([m["text"] for m in self.reader.kinds("line")], ["one", "three"])
        self.w.store.mute("1002", 1, "test")
        self.w.say(self.bob, "x")
        self.assertEqual(self.bob.codes(), ["muted"])
        self.w.wall.now += 61                                        # a minute and a second later
        self.w.say(self.bob, "x")
        self.assertEqual(self.bob.codes(), ["muted"])                # (no new refusal: the mute is over and the line goes out)
        self.assertEqual([m["text"] for m in self.reader.kinds("line")][-1], "x")

    def test_a_ban_follows_the_account_not_the_name_or_the_session(self):
        self.w.store.ban("1001", "test")
        renamed = self.w.join(session.Identity("1001", "other.name", False))
        self.w.say(renamed, "hello")
        self.assertEqual(renamed.codes(), ["banned"])
        self.w.say(self.bob, "hello")                                # another account of the same name is not touched
        self.assertEqual(self.bob.codes(), [])

    def test_empty_long_and_markup(self):
        for text, code in (("", "empty"), ("   ", "empty"), ("‮​", "empty"), ("a" * 201, "long"), ("<b>", "bad"), ("a>b", "bad")):
            self.w.say(self.alice, text)
            self.assertEqual(self.alice.codes()[-1], code, repr(text))
        self.assertEqual(self.reader.kinds("line"), [])

    def test_the_text_is_cleaned_before_it_is_stored_and_sent(self):
        self.w.say(self.alice, "  hi‮   there​  ")
        self.assertEqual(self.reader.kinds("line")[0]["text"], "hi there")
        self.assertEqual(self.w.store.recent_lines(50)[0].text, "hi there")

    def test_a_refused_line_costs_no_token(self):
        for _ in range(10):
            self.w.say(self.alice, "<b>")
        for n in range(3):
            self.w.say(self.alice, "ok %d" % n)
        self.assertEqual(self.alice.codes().count("rate"), 0)

    def test_burst_of_3_then_one_every_2_seconds(self):
        for n in range(3):
            self.w.say(self.alice, "line %d" % n)
        self.assertEqual(self.alice.codes(), [])
        self.w.say(self.alice, "line 3")
        self.assertEqual(self.alice.codes(), ["rate"])
        self.w.clock.now += 1.9
        self.w.say(self.alice, "line 4")
        self.assertEqual(self.alice.codes(), ["rate", "rate"])
        self.w.clock.now += 0.2                                      # 2.1 seconds since the burst: one token
        self.w.say(self.alice, "line 5")
        self.assertEqual(self.alice.codes(), ["rate", "rate"])
        self.w.say(self.alice, "line 6")
        self.assertEqual(self.alice.codes(), ["rate", "rate", "rate"])
        self.w.clock.now += 100                                      # a long quiet time refills the bucket to 3, not more
        for n in range(3):
            self.w.say(self.alice, "again %d" % n)
        self.w.say(self.alice, "again 3")
        self.assertEqual(self.alice.codes()[-1], "rate")
        self.assertEqual(len(self.reader.kinds("line")), 3 + 1 + 3)

    def test_the_limit_is_the_account_s_across_tabs(self):
        tab2, tab3 = self.w.join(ALICE), self.w.join(ALICE)
        self.w.say(self.alice, "a")
        self.w.say(tab2, "b")
        self.w.say(tab3, "c")
        self.w.say(self.alice, "d")
        self.assertEqual(self.alice.codes(), ["rate"])
        self.w.say(self.bob, "e")                                    # another account has its own bucket
        self.assertEqual(self.bob.codes(), [])

    def test_the_same_text_twice_in_30_seconds_is_dropped(self):
        self.w.say(self.alice, "same")
        self.w.clock.now += 5
        self.w.say(self.alice, "same")
        self.assertEqual(self.alice.codes(), ["rate"])
        self.w.clock.now += 24.9                                     # 29.9 s after the first
        self.w.say(self.alice, "same")
        self.assertEqual(self.alice.codes(), ["rate", "rate"])
        self.w.clock.now += 0.2                                      # 30.1 s
        self.w.say(self.alice, "same")
        self.assertEqual(self.alice.codes(), ["rate", "rate"])
        self.assertEqual(len(self.reader.kinds("line")), 2)

    def test_the_duplicate_is_of_the_cleaned_text_and_of_the_account(self):
        self.w.say(self.alice, "same text")
        self.w.clock.now += 3
        self.w.say(self.alice, "same​   text")
        self.assertEqual(self.alice.codes(), ["rate"])
        self.w.say(self.bob, "same text")                            # another account may say it
        self.assertEqual(self.bob.codes(), [])
        self.w.clock.now += 3
        self.w.say(self.alice, "other")
        self.w.say(self.alice, "same text")                          # the first one is still within 30 s
        self.assertEqual(self.alice.codes(), ["rate", "rate"])

    def test_a_dropped_duplicate_does_not_use_a_token(self):
        self.w.say(self.alice, "x")
        for _ in range(5):
            self.w.say(self.alice, "x")
        self.w.say(self.alice, "y")
        self.w.say(self.alice, "z")
        self.assertEqual(self.alice.codes(), ["rate"] * 5)           # (the 5 duplicates, and y and z came through on the 2 tokens left)
        self.assertEqual(len(self.reader.kinds("line")), 3)

    def test_idle_accounts_are_forgotten(self):
        self.w.say(self.alice, "x")
        self.w.chat.account_limits.forget_idle(self.w.clock.now + 10)
        self.assertEqual(len(self.w.chat.account_limits.accounts), 1)
        self.w.chat.account_limits.forget_idle(self.w.clock.now + 400)
        self.assertEqual(len(self.w.chat.account_limits.accounts), 0)

    def test_a_ping_is_answered_with_the_online_count(self):
        self.w.chat.on_text(self.reader, '{"t":"ping"}')
        self.assertEqual(self.reader.sent, [{"t": "online", "n": 3}])
        self.assertEqual(self.reader.strikes, 0)

    def test_unreadable_messages_are_mistakes(self):
        deep = "[" * 1500 + "]" * 1500
        for raw in ("", "{", "null", "5", '"say"', "[]", '{"t":5}', '{"t":"nope"}', '{"text":"x"}', "NaN", '{"t":NaN}', '{"t":"say","text":Infinity}', deep, '{"t":"say","text":' + deep + "}",
                    '{"t":"say"}', '{"t":"say","text":5}', '{"t":"say","text":null}', '{"t":"say","text":["a"]}', "\x00", "{'t':'ping'}"):
            peer = self.w.join(ALICE)
            self.w.chat.on_text(peer, raw)
            self.assertEqual(len(peer.sent), 1, repr(raw[:40]))
            self.assertEqual(peer.sent[0]["t"], "err", repr(raw[:40]))
            self.assertEqual(peer.strikes, 1, repr(raw[:40]))
        self.assertEqual(self.w.store.recent_lines(50), [])

    def test_extra_fields_are_ignored(self):
        self.w.chat.on_text(self.alice, '{"t":"say","text":"hi","id":99,"who":"bob","mod":true,"x":[1,2]}')
        line = self.reader.kinds("line")[0]
        self.assertEqual((line["who"], line["text"], line["mod"], line["id"]), ("alice", "hi", False, 1))

    def test_online_count_is_sent_to_everybody(self):
        self.w.chat.online()
        for peer in (self.alice, self.bob, self.reader):
            self.assertEqual(peer.kinds("online"), [{"t": "online", "n": 3}])


class TheModerators(WorldTest):
    def setUp(self):
        super().setUp()
        self.mod = self.w.join(MOD)

    def test_a_blank_run_in_a_command_is_one_blank(self):
        self.w.say(self.alice, "hi")
        self.w.say(self.mod, "/mute   alice    5")
        self.assertEqual(self.mod.codes(), [])
        self.assertEqual(len(self.w.store.mutes()), 1)

    def test_a_moderator_s_line_is_marked(self):
        self.w.say(self.mod, "welcome")
        self.assertIs(self.reader.kinds("line")[0]["mod"], True)

    def test_mute_by_name_of_someone_who_is_here(self):
        self.w.say(self.mod, "/mute bob 5")
        self.assertEqual(self.mod.codes(), [])
        self.assertEqual(self.reader.kinds("line"), [])              # the command is not a chat line
        self.w.say(self.bob, "hello")
        self.assertEqual(self.bob.codes(), ["muted"])
        self.assertEqual(self.w.store.mutes()[0][:2], ("1002", "bob"))

    def test_mute_by_name_of_someone_who_has_left_but_spoke(self):
        self.w.say(self.bob, "hello")
        self.w.chat.peers.discard(self.bob)
        self.w.say(self.mod, "/mute bob 5")
        self.assertEqual(self.w.store.muted_until("1002") > 0, True)

    def test_the_minutes_and_the_arguments_are_checked(self):
        self.w.say(self.alice, "hi")
        for command in ("/mute alice", "/mute alice 0", "/mute alice -5", "/mute alice 10081", "/mute alice x", "/mute alice 5 6", "/mute nobody 5", "/mute", "/mute alice 1.5",
                        "/mute alice 99999999999", "/ban alice", "/del", "/del x", "/del 1 2", "/del 99", "/mute moddy 5", "/"):
            before = len(self.mod.codes())
            self.w.say(self.mod, command)
            self.assertEqual(self.mod.codes()[before:], ["bad"], command)
        self.assertEqual(self.w.store.mutes(), [])
        self.w.say(self.mod, "/mute alice 10080")
        self.assertEqual(len(self.w.store.mutes()), 1)

    def test_a_moderator_cannot_be_muted_by_a_moderator(self):
        other = session.Identity("9002", "other.mod", False)
        w = World(mods=("9001", "9002"))
        self.addCleanup(w.close)
        a, b = w.join(MOD), w.join(other)
        w.say(b, "hi")
        w.say(a, "/mute other.mod 5")
        self.assertEqual(a.codes(), ["bad"])
        self.assertEqual(w.store.mutes(), [])

    def test_del_takes_the_line_off_every_screen_and_out_of_the_history(self):
        self.w.say(self.alice, "bad line")
        self.w.clock.now += 3
        self.w.say(self.alice, "good line")
        self.w.say(self.mod, "/del 1")
        for peer in (self.alice, self.bob, self.reader, self.mod):
            self.assertEqual(peer.kinds("del"), [{"t": "del", "id": 1}])
        self.assertEqual([h["text"] for h in self.w.chat.welcome(self.reader)["history"]], ["good line"])
        self.w.say(self.mod, "/del 1")                                # twice
        self.assertEqual(self.mod.codes(), ["bad"])
        self.assertEqual(len(self.reader.kinds("del")), 1)

    def test_a_line_older_than_the_history_can_still_be_deleted_and_the_screens_hear_of_it(self):
        for n in range(55):
            self.w.clock.now += 3
            self.w.say(self.alice, "line %d" % n)
        self.w.say(self.mod, "/del 1")                               # (id 1 fell out of the 50 long ago)
        self.assertEqual(self.reader.kinds("del"), [{"t": "del", "id": 1}])

    def test_a_deletion_made_by_the_command_line_is_found(self):
        self.w.say(self.alice, "bad line")
        other = storemod.Store(self.w.db, clock=self.w.wall)          # another process, as `hub.admin` is
        self.assertTrue(other.delete_line(1, "command line"))
        other.close()
        self.assertEqual(self.reader.kinds("del"), [])
        self.w.chat.sync_deletions()
        self.assertEqual(self.reader.kinds("del"), [{"t": "del", "id": 1}])
        self.w.chat.sync_deletions()
        self.assertEqual(len(self.reader.kinds("del")), 1)           # told once
        self.assertEqual(self.w.chat.welcome(self.reader)["history"], [])

    def test_the_commands_are_for_moderators_only(self):
        self.w.say(self.alice, "hi")
        self.w.clock.now += 3
        self.w.say(self.bob, "/mute alice 5")                       # for anybody else it is a line of text
        self.w.clock.now += 3
        self.w.say(self.bob, "/del 1")
        self.assertEqual(self.bob.codes(), [])
        self.assertEqual([m["text"] for m in self.reader.kinds("line")], ["hi", "/mute alice 5", "/del 1"])
        self.assertEqual(self.w.store.mutes(), [])
        self.assertEqual(self.reader.kinds("del"), [])

    def test_a_moderator_who_is_not_in_the_list_is_not_one(self):
        w = World(mods=())
        self.addCleanup(w.close)
        peer = w.join(MOD)
        self.assertIs(w.chat.welcome(peer)["you"]["mod"], False)
        w.say(peer, "hi")
        self.assertIs(w.store.recent_lines(1)[0].mod, 0)


class TheStrikes(unittest.TestCase):
    def test_twenty_in_a_minute(self):
        strikes = limitmod.Strikes(20, 60.0)
        for n in range(19):
            self.assertFalse(strikes.add(n), n)
        self.assertTrue(strikes.add(19.5))

    def test_old_ones_do_not_count(self):
        strikes = limitmod.Strikes(20, 60.0)
        for n in range(19):
            strikes.add(n)
        self.assertFalse(strikes.add(100.0))                         # the earlier 19 are more than a minute old
        for n in range(18):
            self.assertFalse(strikes.add(100.0 + n * 0.1))
        self.assertTrue(strikes.add(102.0))

    def test_the_edge_of_the_window(self):
        strikes = limitmod.Strikes(2, 60.0)
        strikes.add(0.0)
        self.assertFalse(strikes.add(60.0))                          # exactly a minute later the first has expired
        strikes = limitmod.Strikes(2, 60.0)
        strikes.add(0.0)
        self.assertTrue(strikes.add(59.9))


class TheCounters(unittest.TestCase):
    def test_token_bucket(self):
        bucket = limitmod.TokenBucket(3, 2.0, 0.0)
        self.assertEqual([bucket.take(0.0) for _ in range(4)], [True, True, True, False])
        self.assertFalse(bucket.take(1.99))
        self.assertTrue(bucket.take(2.0))
        self.assertFalse(bucket.take(2.0))
        self.assertEqual([bucket.take(1000.0) for _ in range(4)], [True, True, True, False])
        self.assertTrue(bucket.take(500.0) is False)                 # (time going backwards gives nothing)

    def test_per_address_counts(self):
        counter = limitmod.PerAddress(8)
        self.assertTrue(all(counter.try_add("a") for _ in range(8)))
        self.assertFalse(counter.try_add("a"))
        self.assertTrue(counter.try_add("b"))
        counter.remove("a")
        self.assertTrue(counter.try_add("a"))
        self.assertFalse(counter.try_add("a"))
        for _ in range(8):
            counter.remove("a")
        counter.remove("a")                                          # (more removes than adds change nothing)
        self.assertNotIn("a", counter.counts)

    def test_address_keys(self):
        key = limitmod.address_key
        self.assertEqual(key("203.0.113.9", "10.0.0.2"), "203.0.113.9")
        self.assertEqual(key(" 203.0.113.9 ", "10.0.0.2"), "203.0.113.9")
        self.assertEqual(key(None, "10.0.0.2"), "10.0.0.2")
        self.assertEqual(key("not an address", "10.0.0.2"), "10.0.0.2")
        self.assertEqual(key("203.0.113.9, 10.0.0.1", "10.0.0.2"), "10.0.0.2")       # (one address and nothing else)
        self.assertEqual(key("2001:db8:1:2:3:4:5:6", ""), "2001:db8:1:2::/64")
        self.assertEqual(key("2001:db8:1:2:ffff::1", ""), "2001:db8:1:2::/64")       # the same /64 is one address
        self.assertNotEqual(key("2001:db8:1:3::1", ""), key("2001:db8:1:2::1", ""))
        self.assertEqual(key("::ffff:203.0.113.9", ""), "203.0.113.9")
        self.assertEqual(key("::1", ""), "::/64")
        self.assertEqual(key("", ""), "unknown")
        self.assertEqual(key("x", None), "unknown")


class TheStore(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.db = os.path.join(self.folder.name, "hub.db")
        self.wall = Clock(1_800_000_000.0)
        self.store = storemod.Store(self.db, clock=self.wall)
        self.addCleanup(self.store.close)

    def test_lines_are_kept_in_order_with_their_ids(self):
        a = self.store.add_line("1", "ann", "first", False)
        b = self.store.add_line("2", "ben", "second", True)
        self.assertEqual((a.id, b.id), (1, 2))
        self.assertEqual([(l.text, l.mod) for l in self.store.recent_lines(10)], [("first", 0), ("second", 1)])
        self.assertEqual([l.text for l in self.store.recent_lines(1)], ["second"])

    def test_seven_days_and_then_the_lines_are_gone_for_good(self):
        canary = "canary-line-9f3a1c7e-do-not-keep"
        self.store.add_line("1", "ann", canary, False)
        self.wall.now += 7 * 86400 - 60
        self.store.add_line("1", "ann", "newer line", False)
        self.assertEqual(self.store.purge(), 0)
        self.assertEqual(len(self.store.recent_lines(10)), 2)
        self.wall.now += 120                                          # the first line is now 7 days and a minute old
        self.assertEqual([l.text for l in self.store.recent_lines(10)], ["newer line"])      # (not shown from the history either)
        self.assertEqual(self.store.purge(), 1)
        self.assertEqual(self.store.account_of_name("ann"), "1")
        for name in os.listdir(self.folder.name):                    # (the database and its log file: purge() itself must have left the text in neither)
            with open(os.path.join(self.folder.name, name), "rb") as f:
                self.assertNotIn(canary.encode(), f.read(), "the text is still in " + name)
        self.assertEqual([row[4] for row in self.store.lines(10)], ["newer line"])

    def test_purge_ends_old_mutes(self):
        self.store.mute("1", 5, "t")
        self.wall.now += 301
        self.assertEqual(self.store.muted_until("1"), 0)
        self.store.purge()
        self.assertEqual(self.store.db.execute("SELECT COUNT(*) FROM mutes").fetchone()[0], 0)

    def test_retention_days_is_a_parameter(self):
        short = storemod.Store(os.path.join(self.folder.name, "short.db"), clock=self.wall, retention_days=1)
        self.addCleanup(short.close)
        short.add_line("1", "ann", "x", False)
        self.wall.now += 86400 + 1
        self.assertEqual(short.purge(), 1)

    def test_mutes(self):
        self.assertEqual(self.store.muted_until("1"), 0)
        until = self.store.mute("1", 10, "mod", "ann")
        self.assertEqual(until, 1_800_000_000 + 600)
        self.assertEqual(self.store.muted_until("1"), until)
        self.wall.now += 599
        self.assertEqual(self.store.muted_until("1"), until)
        self.wall.now += 2
        self.assertEqual(self.store.muted_until("1"), 0)
        self.store.mute("1", 10, "mod")
        self.assertEqual(self.store.mute("1", 0, "mod"), 0)          # 0 minutes lifts it
        self.assertEqual(self.store.muted_until("1"), 0)
        self.assertEqual(self.store.muted_until("2"), 0)

    def test_a_new_mute_replaces_the_old_one(self):
        self.store.mute("1", 100, "a")
        self.store.mute("1", 5, "b")
        self.assertEqual(self.store.muted_until("1"), 1_800_000_000 + 300)
        self.assertEqual(len(self.store.mutes()), 1)

    def test_bans(self):
        self.assertFalse(self.store.is_banned("1"))
        self.store.ban("1", "cli", "ann")
        self.assertTrue(self.store.is_banned("1"))
        self.assertFalse(self.store.is_banned("2"))
        self.wall.now += 365 * 86400
        self.assertTrue(self.store.is_banned("1"))                   # a ban does not run out
        self.assertEqual(self.store.bans(), [("1", "ann", 1_800_000_000, "cli")])
        self.assertTrue(self.store.unban("1"))
        self.assertFalse(self.store.is_banned("1"))
        self.assertFalse(self.store.unban("1"))

    def test_mutes_and_bans_survive_a_restart(self):
        self.store.ban("1", "cli")
        self.store.mute("2", 60, "cli")
        self.store.close()
        again = storemod.Store(self.db, clock=self.wall)
        self.addCleanup(again.close)
        self.assertTrue(again.is_banned("1"))
        self.assertGreater(again.muted_until("2"), 0)
        self.store = again

    def test_names_are_looked_up_by_the_newest_line(self):
        self.store.add_line("1", "ann", "x", False)
        self.store.add_line("2", "ann", "y", False)                  # the name changed hands
        self.assertEqual(self.store.account_of_name("ann"), "2")
        self.assertIsNone(self.store.account_of_name("nobody"))
        self.assertEqual(self.store.last_name("1"), "ann")
        self.assertEqual(self.store.last_name("9"), "")

    def test_deleted_lines(self):
        a = self.store.add_line("1", "ann", "x", False)
        self.assertEqual(self.store.last_deletion(), 0)
        self.assertTrue(self.store.delete_line(a.id, "cli"))
        self.assertFalse(self.store.delete_line(a.id, "cli"))
        self.assertFalse(self.store.delete_line(99, "cli"))
        self.assertEqual(self.store.deletions_since(0), [(1, a.id)])
        self.assertEqual(self.store.deletions_since(1), [])
        self.assertEqual(self.store.recent_lines(10), [])
        self.assertEqual(self.store.lines(10)[0][5], 1)              # (kept, marked, for the moderators until its 7 days are up)

    def test_a_newer_database_is_refused(self):
        self.store.db.execute("PRAGMA user_version = 99")
        with self.assertRaises(RuntimeError):
            storemod.Store(self.db, clock=self.wall)

    def test_two_connections_write_one_after_the_other(self):
        other = storemod.Store(self.db, clock=self.wall)
        self.addCleanup(other.close)
        for n in range(50):
            (self.store if n % 2 else other).add_line("1", "ann", "line %d" % n, False)
        self.assertEqual(len(self.store.recent_lines(100)), 50)

    @unittest.skipIf(os.name == "nt", "file modes")
    def test_the_file_is_private(self):
        self.assertEqual(os.stat(self.db).st_mode & 0o077, 0)


class TheSettings(unittest.TestCase):
    def test_defaults_are_a_hub_nobody_can_reach_and_nobody_can_write_to(self):
        cfg = config.load({})
        self.assertEqual((cfg.origin, cfg.mods, cfg.test_login, cfg.port, cfg.host), ("", frozenset(), False, 4040, "127.0.0.1"))
        self.assertIsInstance(session.make(cfg), session.NoLogin)
        self.assertFalse(session.make(cfg).login)

    def test_the_site_address(self):
        for given, origin in (("https://beta.example.org", "https://beta.example.org"), ("https://Beta.Example.org/", "https://beta.example.org"), ("https://beta.example.org:443", "https://beta.example.org"),
                              ("http://localhost:19980", "http://localhost:19980"), ("http://localhost:80", "http://localhost"), ("https://[::1]:8443", "https://[::1]:8443"), ("", "")):
            self.assertEqual(config.parse_origin(given), origin, given)
        for bad in ("beta.example.org", "ftp://x.org", "https://x.org/path", "https://x.org?q=1", "https://user:pw@x.org", "https://", "javascript:alert(1)", "https://x.org#f"):
            with self.assertRaises(config.ConfigError, msg=bad):
                config.parse_origin(bad)

    def test_moderators(self):
        self.assertEqual(config.parse_mods("1001, 1002 ,1003"), frozenset(("1001", "1002", "1003")))
        self.assertEqual(config.parse_mods(""), frozenset())
        self.assertEqual(config.parse_mods("  "), frozenset())
        for bad in ("abc", "1001,x", "-1", "1" * 21, "1001;1002", "10 01x"):
            with self.assertRaises(config.ConfigError, msg=bad) as caught:
                config.parse_mods(bad)
            self.assertNotIn(bad.replace(" ", ""), str(caught.exception) if len(bad) > 3 else "never")      # (the message names the place, not the value)

    def test_the_port(self):
        self.assertEqual(config.load({"ANTS_HUB_PORT": "0"}).port, 0)
        for bad in ("x", "-1", "65536", ""):
            with self.assertRaises(config.ConfigError, msg=bad):
                config.load({"ANTS_HUB_PORT": bad})

    def test_test_login_is_for_a_loopback_address_only(self):
        for host in ("127.0.0.1", "::1", "localhost"):
            self.assertTrue(config.load({"ANTS_HUB_TEST_LOGIN": "1", "ANTS_HUB_HOST": host}).test_login)
        for host in ("0.0.0.0", "::", "10.0.0.5", "example.org", ""):
            with self.assertRaises(config.ConfigError, msg=host):
                config.load({"ANTS_HUB_TEST_LOGIN": "1", "ANTS_HUB_HOST": host})
        self.assertFalse(config.load({"ANTS_HUB_TEST_LOGIN": "0", "ANTS_HUB_HOST": "0.0.0.0"}).test_login)
        self.assertFalse(config.load({"ANTS_HUB_TEST_LOGIN": "true", "ANTS_HUB_HOST": "0.0.0.0"}).test_login)
        self.assertFalse(config.load({"ANTS_HUB_HOST": "0.0.0.0"}).test_login)

    def test_a_secret_is_never_in_the_summary_or_the_text_of_the_settings(self):
        secret, client, mod = "canary-secret-8c1d5e", "canary-client-id-77", "7777777777"
        cfg = config.load({"ANTS_HUB_DISCORD_CLIENT_SECRET": secret, "ANTS_HUB_DISCORD_CLIENT_ID": client, "ANTS_HUB_MODS": mod, "ANTS_HUB_PUBLIC_URL": "https://x.example.org"})
        for text in (repr(cfg), str(cfg), json.dumps(cfg.summary())):
            for canary in (secret, client, mod):
                self.assertNotIn(canary, text)
        self.assertEqual(cfg.summary()["mods"], 1)
        self.assertEqual(cfg.login_client_secret, secret)             # (it is read: the login layer that comes later needs it)


class TheSessions(unittest.TestCase):
    def test_no_login_knows_nobody(self):
        self.assertIsNone(session.NoLogin().identify({"x-hub-test-login": "1001:alice", "cookie": "a=b"}))

    def test_the_test_login_reads_one_header(self):
        login = session.TestLogin()
        self.assertTrue(login.login)
        self.assertEqual(login.identify({"x-hub-test-login": "1001:alice"}), ALICE)
        self.assertEqual(login.identify({"x-hub-test-login": "1003:newbie:young"}), YOUNG)
        for bad in ("", "1001", "1001:", ":alice", "x:alice", "1001:Alice", "1001:a", "1001:alice:old", "1001:alice:young:x", "1001:ali ce", "1" * 21 + ":alice", "1001:alice\n"):
            self.assertIsNone(login.identify({"x-hub-test-login": bad}), bad)
        self.assertIsNone(login.identify({}))


class TheCommandLine(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.db = os.path.join(self.folder.name, "hub.db")
        self.store = storemod.Store(self.db)
        self.alice = self.store.add_line("1001", "alice", "hello there", False)
        self.digits = self.store.add_line("2002", "12345", "numbers", False)
        self.store.close()

    def run_admin(self, *args):
        out, err = io.StringIO(), io.StringIO()
        status = admin.run(["--db", self.db] + list(args), out, err)
        return status, out.getvalue(), err.getvalue()

    def test_mute_ban_unban_by_id_and_by_name(self):
        status, out, _ = self.run_admin("mute", "alice", "30")
        self.assertEqual(status, 0)
        self.assertIn("1001 muted until", out)
        store = storemod.Store(self.db)
        self.addCleanup(store.close)
        self.assertGreater(store.muted_until("1001"), 0)
        self.assertEqual(self.run_admin("mute", "1001", "0")[0], 0)
        self.assertEqual(store.muted_until("1001"), 0)
        self.assertEqual(self.run_admin("ban", "alice")[0], 0)
        self.assertTrue(store.is_banned("1001"))
        self.assertEqual(self.run_admin("ban", "7777")[0], 0)        # an account that never spoke: by id
        self.assertTrue(store.is_banned("7777"))
        self.assertEqual(self.run_admin("unban", "alice")[0], 0)
        self.assertFalse(store.is_banned("1001"))
        status, _, err = self.run_admin("unban", "alice")
        self.assertEqual(status, 1)
        self.assertIn("was not banned", err)

    def test_a_name_that_is_all_digits_needs_an_at_sign(self):
        self.assertEqual(self.run_admin("ban", "@12345")[0], 0)
        store = storemod.Store(self.db)
        self.addCleanup(store.close)
        self.assertTrue(store.is_banned("2002"))
        self.assertFalse(store.is_banned("12345"))

    def test_unknown_names_and_bad_arguments(self):
        status, _, err = self.run_admin("mute", "nobody", "5")
        self.assertEqual(status, 1)
        self.assertIn("account id", err)
        self.assertEqual(self.run_admin("mute", "Bad Name!", "5")[0], 1)
        self.assertEqual(self.run_admin("mute", "alice", "-1")[0], 2)
        self.assertEqual(self.run_admin("mute", "alice", "525601")[0], 2)
        self.assertEqual(self.run_admin("mute", "alice")[0], 2)
        self.assertEqual(self.run_admin("frobnicate")[0], 2)
        self.assertEqual(self.run_admin()[0], 2)
        self.assertEqual(self.run_admin("del", "x")[0], 2)

    def test_del(self):
        status, out, _ = self.run_admin("del", "1")
        self.assertEqual((status, out), (0, "line 1 deleted\n"))
        self.assertEqual(self.run_admin("del", "1")[0], 1)
        self.assertEqual(self.run_admin("del", "99")[0], 1)
        store = storemod.Store(self.db)
        self.addCleanup(store.close)
        self.assertEqual(store.deletions_since(0)[0][1], 1)

    def test_list_shows_bans_mutes_and_lines(self):
        self.run_admin("ban", "alice")
        self.run_admin("mute", "@12345", "10")
        self.run_admin("del", "2")
        status, out, _ = self.run_admin("list")
        self.assertEqual(status, 0)
        self.assertIn("1001 alice since", out)
        self.assertIn("2002 12345 until", out)
        self.assertIn("1  ", out)
        self.assertIn("2x ", out)                                     # deleted lines are marked
        self.assertIn("alice: hello there", out)
        self.assertEqual(self.run_admin("list", "--lines", "1")[1].count("alice: hello there"), 0)       # (only the newest line)

    def test_no_database_is_not_made_by_accident(self):
        status, _, err = self.run_admin_elsewhere()
        self.assertEqual(status, 1)
        self.assertIn("no database", err)

    def run_admin_elsewhere(self):
        out, err = io.StringIO(), io.StringIO()
        missing = os.path.join(self.folder.name, "nope", "hub.db")
        status = admin.run(["--db", missing, "list"], out, err)
        self.assertFalse(os.path.exists(os.path.dirname(missing)))
        return status, out.getvalue(), err.getvalue()

    def test_the_module_runs_as_a_program_and_reads_its_database_from_the_environment(self):
        env = dict(os.environ, ANTS_HUB_DB=self.db, PYTHONDONTWRITEBYTECODE="1")
        done = subprocess.run([sys.executable, "-m", "hub.admin", "ban", "alice"], cwd=REPO, env=env, capture_output=True, text=True)
        self.assertEqual((done.returncode, done.stdout), (0, "1001 banned\n"), done.stderr)
        done = subprocess.run([sys.executable, "-m", "hub.admin", "list"], cwd=REPO, env=env, capture_output=True, text=True)
        self.assertIn("1001 alice since", done.stdout)


if __name__ == "__main__":
    unittest.main()
