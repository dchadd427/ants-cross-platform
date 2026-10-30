#!/usr/bin/env python3
"""The hub's rules without a socket (hub/chat.py, limits.py, store.py, config.py, session.py, admin.py; docs/HUB.md): who may say what, the limits, the last 50 lines, bans and mutes,
history that is kept 7 days and then deleted for good, the moderators' commands and the command line.

The chat is driven with plain fake peers and a fake clock, so every limit is tested at its edge in no time. The sockets, the timers and the real process are tests/scripts/test_hub_server.py.
"""
import asyncio
import collections
import dataclasses
import io
import json
import os
import sqlite3
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, REPO)
from hub import admin, chat as chatmod, config, limits as limitmod, log as logmod, session, store as storemod  # noqa: E402

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
        self.charges = 0
        self.closed = None

    def send_text(self, data):
        self.sent.append(json.loads(data.decode("utf-8")))

    def strike(self):
        self.strikes += 1

    def charge(self):
        self.charges += 1

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

    def test_lines_past_their_7_days_are_not_in_the_history_of_a_newcomer(self):
        self.w.say(self.alice, "an old line")
        self.w.clock.now += 3
        self.w.say(self.alice, "a newer line")
        self.w.wall.now += 6 * 86400                                       # 6 days: both are shown
        self.assertEqual([h["text"] for h in self.w.chat.welcome(self.reader)["history"]], ["an old line", "a newer line"])
        self.w.say(self.bob, "a fresh line")
        self.w.wall.now += 86400 + 1                                       # past 7 days for the first two, whatever happened to the database
        self.assertEqual([h["text"] for h in self.w.chat.welcome(self.reader)["history"]], ["a fresh line"])
        self.assertEqual([l.text for l in self.w.chat.ring], ["a fresh line"])      # and they are out of the memory too
        self.w.wall.now += 8 * 86400
        self.assertEqual(self.w.chat.welcome(self.reader)["history"], [])

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
        for text, code in (("", "empty"), ("   ", "empty"), ("\u202e\u200b", "empty"), ("a" * 201, "long"), ("<b>", "bad"), ("a>b", "bad")):
            self.w.say(self.alice, text)
            self.assertEqual(self.alice.codes()[-1], code, repr(text))
        self.assertEqual(self.reader.kinds("line"), [])

    def test_the_text_is_cleaned_before_it_is_stored_and_sent(self):
        self.w.say(self.alice, "  hi\u202e   there\u200b  ")
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
        self.w.say(self.alice, "same\u200b   text")
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

    def test_a_refused_line_is_not_kept(self):
        for n in range(3):
            self.w.say(self.alice, "line %d" % n)
        self.w.say(self.alice, "too fast")                            # rate
        self.w.say(self.alice, "line 0")                              # a duplicate
        self.assertEqual(self.alice.codes(), ["rate", "rate"])
        self.assertEqual([l.text for l in self.w.store.recent_lines(50)], ["line 0", "line 1", "line 2"])
        self.assertEqual([l.text for l in self.w.chat.ring], ["line 0", "line 1", "line 2"])
        self.assertEqual(self.w.store.db.execute("SELECT COUNT(*) FROM lines").fetchone()[0], 3)

    def test_two_accounts_of_one_name_have_a_limit_each(self):
        first, second = self.w.join(session.Identity("1010", "sam", False)), self.w.join(session.Identity("1011", "sam", False))
        for n in range(3):
            self.w.say(first, "a%d" % n)
            self.w.say(second, "b%d" % n)
        self.assertEqual((first.codes(), second.codes()), ([], []))
        self.w.say(first, "a3")
        self.w.say(second, "b3")
        self.assertEqual((first.codes(), second.codes()), (["rate"], ["rate"]))

    def test_the_same_text_is_free_again_at_exactly_30_seconds(self):
        self.w.say(self.alice, "same")
        self.w.clock.now += 30.0
        self.w.say(self.alice, "same")
        self.assertEqual(self.alice.codes(), [])
        self.assertEqual(len(self.reader.kinds("line")), 2)

    def test_lines_that_show_nothing_are_empty(self):
        for text in ("\ufe0f", " \u180b ", "\U000e0100", "\U00016fe4", "\u0301\u0301", "\u180d\u180f \ufe0f"):
            self.w.say(self.alice, text)
            self.assertEqual(self.alice.codes()[-1], "empty", repr(text))
        self.w.say(self.alice, "a\ufe0f")
        self.assertEqual(self.alice.codes()[-1], "empty")              # (the code before is still the last error: this line went out)
        self.assertEqual(self.reader.kinds("line")[-1]["text"], "a\ufe0f")

    def test_all_accounts_together_have_a_limit_too(self):
        w = World(limits=dataclasses.replace(LIMITS, global_burst=3, global_refill_seconds=1.0))
        self.addCleanup(w.close)
        people = [w.join(session.Identity(str(2000 + n), "user%02d" % n, False)) for n in range(5)]
        for person in people:
            w.say(person, "hello")
        self.assertEqual([p.codes() for p in people], [[], [], [], ["rate"], ["rate"]])
        self.assertEqual(len(w.store.recent_lines(50)), 3)
        w.clock.now += 1.0
        w.say(people[3], "hello")
        w.say(people[4], "hello")
        self.assertEqual([p.codes() for p in people[3:]], [["rate"], ["rate", "rate"]])      # one more line, for one account
        self.assertEqual(len(w.store.recent_lines(50)), 4)

    def test_a_moderator_s_command_is_not_held_back_by_the_limits_of_lines(self):
        w = World(limits=dataclasses.replace(LIMITS, global_burst=1, global_refill_seconds=1000.0, db_max_bytes=1))
        self.addCleanup(w.close)
        mod, alice = w.join(MOD), w.join(ALICE)
        w.say(alice, "hello")
        self.assertEqual(alice.codes(), ["rate"])
        w.say(mod, "/mute alice 5")
        self.assertEqual(mod.codes(), [])
        self.assertEqual(len(w.store.mutes()), 1)

    def test_no_line_is_kept_once_the_database_is_full_and_moderation_goes_on(self):
        w = World(limits=dataclasses.replace(LIMITS, db_max_bytes=1))
        self.addCleanup(w.close)
        alice, mod = w.join(ALICE), w.join(MOD)
        w.say(alice, "hello")
        self.assertEqual(alice.codes(), ["rate"])
        self.assertEqual(w.store.recent_lines(10), [])
        self.assertTrue(w.store.mute("1001", 5, "cli") > 0)              # a mute, a ban and a deletion are small writes that the guard does not refuse
        w.store.ban("1002", "cli")
        self.assertTrue(w.store.is_banned("1002"))
        size = w.store.size()
        self.assertGreater(size, 1)
        again = World(limits=dataclasses.replace(LIMITS, db_max_bytes=size + 10 ** 6))
        self.addCleanup(again.close)
        again_alice = again.join(ALICE)
        again.say(again_alice, "hello")
        self.assertEqual(again_alice.codes(), [])
        self.assertEqual(len(again.store.recent_lines(10)), 1)

    def test_a_full_database_takes_lines_again_when_the_old_ones_are_purged(self):
        w = World(limits=dataclasses.replace(LIMITS, db_max_bytes=300000))
        self.addCleanup(w.close)
        for n in range(2500):
            w.store.add_line("9", "old.timer", "an old line %d " % n + "y" * 150, False)
        w.store.checkpoint()
        self.assertGreater(w.store.size(), 300000)
        alice = w.join(ALICE)
        w.say(alice, "hello")
        self.assertEqual(alice.codes(), ["rate"])                          # full
        w.wall.now += 8 * 86400
        w.clock.now += 8 * 86400
        self.assertEqual(w.store.purge(10 ** 6), 2500)
        self.assertGreater(os.path.getsize(w.db), 300000)                  # the file is as big as it was
        w.say(alice, "hello again")
        self.assertEqual(alice.codes(), ["rate"])                          # the line went through: no second refusal
        self.assertEqual([l.text for l in w.store.recent_lines(10)], ["hello again"])

    def test_a_refusal_that_is_not_the_clients_doing_is_not_a_mistake_of_its_socket(self):
        w = World(limits=dataclasses.replace(LIMITS, global_burst=1, global_refill_seconds=1000.0))
        self.addCleanup(w.close)
        alice, bob = w.join(ALICE), w.join(BOB)
        w.say(alice, "first")
        for n in range(30):
            w.say(bob, "line %d" % n)                                       # the shared limit is spent: all of these are refused
        self.assertEqual(bob.codes(), ["rate"] * 30)
        self.assertEqual(bob.strikes, 0)                                    # and none of them counts against the socket (twenty would close it)
        full = World(limits=dataclasses.replace(LIMITS, db_max_bytes=1))
        self.addCleanup(full.close)
        carol = full.join(ALICE)
        for n in range(30):
            full.say(carol, "line %d" % n)
        self.assertEqual((carol.codes().count("rate"), carol.strikes), (30, 0))
        # a client's own limit still counts
        dave = self.w.join(session.Identity("1030", "dave", False))
        for n in range(8):
            self.w.say(dave, "line %d" % n)
        self.assertEqual((dave.codes(), dave.strikes), (["rate"] * 5, 5))

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

    def test_a_message_too_deep_for_the_decoder_is_a_mistake_not_a_crash(self):
        # a frame of 2 KB holds up to 2,000 brackets: Pythons before 3.12 give up (RecursionError) well below that, newer ones read them; the handler must live through either
        real_loads, deep = json.loads, "[" * 2000
        for decoder_error in (RecursionError("maximum recursion depth exceeded"), ValueError("too deep")):
            def decoder(raw, **kw):
                if raw == deep:
                    raise decoder_error
                return real_loads(raw, **kw)
            peer = self.w.join(ALICE)
            with mock.patch.object(json, "loads", side_effect=decoder):
                self.w.chat.on_text(peer, deep)
            self.assertEqual(peer.sent, [{"t": "err", "code": "bad"}])
            self.assertEqual(peer.strikes, 1)

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

    def test_a_name_that_two_accounts_here_have_is_not_a_target(self):
        first, second = self.w.join(session.Identity("1010", "sam", False)), self.w.join(session.Identity("1011", "sam", False))
        self.w.say(first, "hi")
        self.w.say(self.mod, "/mute sam 5")
        self.assertEqual(self.mod.codes(), ["bad"])
        self.assertEqual(self.w.store.mutes(), [])
        self.w.chat.peers.discard(second)                               # with one of them gone it is clear who is meant
        self.w.say(self.mod, "/mute sam 5")
        self.assertEqual([m[0] for m in self.w.store.mutes()], ["1010"])

    def test_the_account_that_is_here_wins_over_one_that_spoke_under_the_name_before(self):
        self.w.store.add_line("1020", "sam", "years ago", False)
        here = self.w.join(session.Identity("1021", "sam", False))
        self.w.say(self.mod, "/mute sam 5")
        self.assertEqual([m[0] for m in self.w.store.mutes()], ["1021"])
        self.assertIn(here, self.w.chat.peers)

    def test_a_user_whose_name_is_a_moderators_id_is_not_a_moderator(self):
        impostor = self.w.join(session.Identity("1050", "9001", False))
        self.assertIs(self.w.chat.welcome(impostor)["you"]["mod"], False)
        self.w.say(self.alice, "hi")
        self.w.say(impostor, "/mute alice 5")
        line = self.reader.kinds("line")[-1]
        self.assertEqual((line["text"], line["mod"]), ("/mute alice 5", False))        # text, from someone who is not one
        self.assertEqual(self.w.store.mutes(), [])

    def test_deleting_goes_on_working_after_old_lines_were_purged(self):
        self.w.say(self.alice, "the first line")
        self.w.say(self.mod, "/del 1")
        self.assertEqual(self.reader.kinds("del"), [{"t": "del", "id": 1}])
        self.w.wall.now += 8 * 86400
        self.w.clock.now += 8 * 86400
        self.assertEqual(self.w.store.purge(), 1)                       # the line is gone, and with it the row that held the number of its deletion
        self.w.say(self.alice, "a later line")
        self.w.say(self.mod, "/del 2")
        self.assertEqual(self.mod.codes(), [])
        self.assertEqual(self.reader.kinds("del"), [{"t": "del", "id": 1}, {"t": "del", "id": 2}])
        self.assertEqual(self.w.chat.welcome(self.reader)["history"], [])
        self.assertEqual(self.w.store.last_deletion(), 2)

    def test_a_deletion_by_the_command_line_after_a_purge_is_found_too(self):
        self.w.say(self.alice, "one")
        other = storemod.Store(self.w.db, clock=self.w.wall)
        self.addCleanup(other.close)
        other.delete_line(1, "command line")
        self.w.chat.sync_deletions()
        self.w.wall.now += 8 * 86400
        self.w.clock.now += 8 * 86400
        other.purge()
        self.w.say(self.alice, "two")
        other.delete_line(2, "command line")
        self.w.chat.sync_deletions()
        self.assertEqual(self.reader.kinds("del"), [{"t": "del", "id": 1}, {"t": "del", "id": 2}])

    def test_a_run_of_deletions_from_the_command_line_is_the_newest_hundred_frames_and_every_line_leaves_the_ring(self):
        other = storemod.Store(self.w.db, clock=self.w.wall)
        self.addCleanup(other.close)
        for n in range(30):
            self.w.say(self.alice, "line %d" % n)
            self.w.clock.now += 3.0
        ring_ids = [l.id for l in self.w.chat.ring]
        with other.transaction():
            other.db.executemany("INSERT INTO lines (at, account, name, text, mod) VALUES (?, ?, ?, ?, 0)", [(int(self.w.wall()), "9", "bulk", "bulk %d" % n) for n in range(450)])
        ids = [row[0] for row in other.db.execute("SELECT id FROM lines ORDER BY id")]
        for line_id in ids[:420]:                                                    # (a script that deletes four hundred lines with the command line within one look of the hub)
            other.delete_line(line_id, "command line")
        self.w.chat.sync_deletions()
        self.assertEqual([m["id"] for m in self.reader.kinds("del")], ids[:420][-100:])
        self.assertEqual(len(self.bob.kinds("del")), 100)
        self.assertEqual([l.id for l in self.w.chat.ring if l.id in ids[:420]], [])
        self.assertEqual(self.w.chat.deleted_seq, other.last_deletion())
        self.assertEqual(self.w.chat.sync_deletions(), None)
        self.assertEqual(len(self.reader.kinds("del")), 100)                         # (and the next look finds nothing new)
        self.assertGreater(len(ring_ids), 0)

    def test_dropping_lines_announces_the_newest_hundred_of_any_number_and_clears_the_ring(self):
        for n in range(5):
            self.w.say(self.alice, "line %d" % n)
            self.w.clock.now += 3.0
        ring_ids = [l.id for l in self.w.chat.ring]
        self.assertEqual(len(ring_ids), 5)
        self.w.chat.drop_lines(list(range(1000, 301000)) + ring_ids)
        self.assertEqual([m["id"] for m in self.reader.kinds("del")], list(range(300900, 301000)))
        self.assertEqual(list(self.w.chat.ring), [])                                 # (the ring is cleared of all of them, announced or not)
        self.w.chat.drop_lines([])
        self.assertEqual(len(self.reader.kinds("del")), 100)
        small = self.w.join(None)
        self.w.chat.drop_lines({7, 3, 5})
        self.assertEqual([m["id"] for m in small.kinds("del")], [3, 5, 7])

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

    def test_two_deletions_by_the_command_line_are_both_announced(self):
        for n in range(3):
            self.w.clock.now += 3
            self.w.say(self.alice, "line %d" % n)
        other = storemod.Store(self.w.db, clock=self.w.wall)
        self.addCleanup(other.close)
        other.delete_line(1, "command line")
        self.w.chat.sync_deletions()
        other.delete_line(3, "command line")
        self.w.chat.sync_deletions()
        self.assertEqual(self.reader.kinds("del"), [{"t": "del", "id": 1}, {"t": "del", "id": 3}])
        self.assertEqual([h["text"] for h in self.w.chat.welcome(self.reader)["history"]], ["line 1"])

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


class TheLog(unittest.TestCase):
    def lines(self, **fields):
        out = io.StringIO()
        logmod.Log(out, clock=lambda: 1_800_000_000.0).event("something", **fields)
        return out.getvalue()

    def test_one_line_for_each_event_with_the_time_in_utc(self):
        self.assertEqual(self.lines(port=4040, why="origin"), "2027-01-15T08:00:00Z something port=4040 why=origin\n")

    def test_a_value_from_a_client_cannot_forge_a_line_or_run_long(self):
        out = self.lines(why="x\n2027-01-15T08:00:00Z open tag=forged", other="a b\tc\x00d\u202ee<b>", long="a" * 500)
        self.assertEqual(out.count("\n"), 1)
        self.assertEqual(out, "2027-01-15T08:00:00Z something why=x_2027-01-15T08:00:00Z_open_tag_forged other=a_b_c_d_e_b_ long=" + "a" * 60 + "\n")

    def test_an_address_is_a_tag_that_changes_with_every_start(self):
        a, b = logmod.Log(io.StringIO()), logmod.Log(io.StringIO())
        self.assertEqual(a.tag("203.0.113.9"), a.tag("203.0.113.9"))
        self.assertNotEqual(a.tag("203.0.113.9"), a.tag("203.0.113.10"))
        self.assertNotEqual(a.tag("203.0.113.9"), b.tag("203.0.113.9"))
        self.assertRegex(a.tag("203.0.113.9"), r"^[0-9a-f]{8}$")

    def test_a_stream_that_fails_does_not_stop_the_hub(self):
        class Broken:
            def write(self, text):
                raise OSError("closed")

            def flush(self):
                raise OSError("closed")
        logmod.Log(Broken()).event("something")


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

    def test_a_place_is_given_back_one_at_a_time(self):
        counter = limitmod.PerAddress(8)
        for _ in range(3):
            self.assertTrue(counter.try_add("a"))
        counter.remove("a")
        self.assertEqual(counter.counts, {"a": 2})
        counter.remove("a")
        self.assertEqual(counter.counts, {"a": 1})                       # the last live socket is still counted
        self.assertTrue(all(counter.try_add("a") for _ in range(7)))
        self.assertFalse(counter.try_add("a"))

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

    def test_every_deletion_has_a_number_of_its_own_so_that_none_is_missed(self):
        ids = [self.store.add_line("1", "ann", "line %d" % n, False).id for n in range(3)]
        self.assertTrue(self.store.delete_line(ids[0], "cli"))
        self.assertEqual(self.store.last_deletion(), 1)
        self.assertTrue(self.store.delete_line(ids[2], "cli"))
        self.assertEqual(self.store.last_deletion(), 2)
        self.assertEqual(self.store.deletions_since(0), [(1, ids[0]), (2, ids[2])])
        self.assertEqual(self.store.deletions_since(1), [(2, ids[2])])           # what the hub has not heard of yet, and nothing it has

    def test_a_deletion_number_is_never_used_twice_even_after_a_purge(self):
        first = self.store.add_line("1", "ann", "x", False)
        self.assertTrue(self.store.delete_line(first.id, "cli"))
        self.wall.now += 8 * 86400
        self.assertEqual(self.store.purge(), 1)
        self.assertEqual(self.store.last_deletion(), 1)                  # the number stays when the line that had it is gone
        second = self.store.add_line("1", "ann", "y", False)
        self.assertTrue(self.store.delete_line(second.id, "cli"))
        self.assertEqual(self.store.last_deletion(), 2)
        self.assertEqual(self.store.deletions_since(1), [(2, second.id)])
        self.assertFalse(self.store.delete_line(second.id, "cli"))
        self.assertFalse(self.store.delete_line(999, "cli"))
        self.assertEqual(self.store.last_deletion(), 2)                  # a deletion that did nothing took no number

    def test_a_database_of_the_first_schema_gets_the_counter(self):
        path = os.path.join(self.folder.name, "old.db")
        old = sqlite3.connect(path, isolation_level=None)
        old.executescript("""CREATE TABLE lines (id INTEGER PRIMARY KEY AUTOINCREMENT, at INTEGER NOT NULL, account TEXT NOT NULL, name TEXT NOT NULL, text TEXT NOT NULL,
                             mod INTEGER NOT NULL DEFAULT 0, deleted INTEGER NOT NULL DEFAULT 0, del_seq INTEGER NOT NULL DEFAULT 0, del_by TEXT);
                             CREATE TABLE mutes (account TEXT PRIMARY KEY, until INTEGER NOT NULL, at INTEGER NOT NULL, by TEXT NOT NULL, name TEXT NOT NULL);
                             CREATE TABLE bans (account TEXT PRIMARY KEY, at INTEGER NOT NULL, by TEXT NOT NULL, name TEXT NOT NULL);
                             INSERT INTO lines (at, account, name, text, deleted, del_seq) VALUES (1, '1', 'ann', 'x', 1, 7);
                             PRAGMA user_version = 1;""")
        old.close()
        migrated = storemod.Store(path, clock=self.wall)
        self.addCleanup(migrated.close)
        self.assertEqual(migrated.last_deletion(), 7)
        line = migrated.add_line("1", "ann", "y", False)
        migrated.delete_line(line.id, "cli")
        self.assertEqual(migrated.last_deletion(), 8)

    def test_a_backlog_is_deleted_a_chunk_at_a_time(self):
        for n in range(5):
            self.store.add_line("1", "ann", "line %d" % n, False)
        self.wall.now += 8 * 86400
        self.store.add_line("1", "ann", "fresh", False)
        self.assertEqual([self.store.purge(2) for _ in range(4)], [2, 2, 1, 0])
        self.assertEqual([r[4] for r in self.store.lines(10)], ["fresh"])

    def test_the_log_file_is_cut_when_a_reader_lets_go_and_not_before(self):
        canary = "canary-text-held-by-a-reader-5d7e"
        self.store.add_line("1", "ann", canary, False)
        self.wall.now += 8 * 86400
        reader = sqlite3.connect(self.db, isolation_level=None)               # `hub.admin list` or a backup: a read that is open
        reader.execute("BEGIN")
        self.assertEqual(reader.execute("SELECT COUNT(*) FROM lines").fetchone()[0], 1)
        started = time.monotonic()
        self.assertEqual(self.store.purge(), 1)
        self.assertLess(time.monotonic() - started, 1.0)                     # (the hub's one thread does not wait for a reader)
        self.assertTrue(self.store.unsettled)
        reader.execute("COMMIT")
        reader.close()
        self.assertTrue(self.store.checkpoint())
        self.assertFalse(self.store.unsettled)
        for name in os.listdir(self.folder.name):
            with open(os.path.join(self.folder.name, name), "rb") as f:
                self.assertNotIn(canary.encode(), f.read(), "the text is still in " + name)

    def test_a_purge_with_nothing_to_delete_still_cuts_the_log_file(self):
        self.store.add_line("1", "ann", "fresh", False)
        self.assertGreater(os.path.getsize(self.db + "-wal"), 0)
        self.assertEqual(self.store.purge(), 0)
        self.assertEqual(os.path.getsize(self.db + "-wal"), 0)

    def test_the_size_counts_what_is_in_use_and_the_log_file(self):
        self.store.checkpoint()
        empty = self.store.size()
        for n in range(200):
            self.store.add_line("1", "ann", "line %d " % n + "x" * 150, False)
        grown = self.store.size()
        self.assertGreater(grown, empty)
        self.assertGreaterEqual(grown, os.path.getsize(self.db + "-wal"))                 # (the log file is in it)
        self.store.checkpoint()
        self.assertEqual(os.path.getsize(self.db + "-wal"), 0)
        self.assertGreater(self.store.size(), empty)                                    # (and the pages that hold the lines, once the log file is cut)

    def test_the_size_goes_down_when_lines_are_deleted_though_the_file_does_not(self):
        for n in range(3000):
            self.store.add_line("1", "ann", "line %d " % n + "x" * 150, False)
        self.store.checkpoint()
        full = self.store.size()
        file_size = os.path.getsize(self.db)
        self.assertGreater(full, 400000)
        self.wall.now += 8 * 86400
        self.assertEqual(self.store.purge(10 ** 6), 3000)
        self.assertGreaterEqual(os.path.getsize(self.db), file_size * 0.9)              # sqlite keeps the file as big as it was ...
        self.assertLess(self.store.size(), full / 10)                                   # ... but the freed pages are room again, and room is what the guard counts


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

    def test_a_forget_is_done_and_told_when_the_cut_of_the_log_file_fails_and_the_cut_is_asked_for_again(self):
        self.store.add_line("7", "gone", "a line", False)
        self.store.add_session("a" * 64, "7", "gone", 100, 10, 100)

        def fail(*a, **k):
            raise sqlite3.OperationalError("database is locked")

        self.store.checkpoint = fail
        ids = self.store.forget("7")
        self.assertEqual(ids, [1])
        self.assertIsNone(self.store.session("a" * 64))
        self.assertEqual(self.store.db.execute("SELECT COUNT(*) FROM lines").fetchone()[0], 0)
        self.assertTrue(self.store.unsettled)                                        # (the hub's clock cuts the file at its next look)

    def test_a_forget_gives_back_the_ids_of_the_lines_that_a_moderator_deleted_and_the_screens_were_not_told_of(self):
        a = self.store.add_line("7", "gone", "one", False)
        b = self.store.add_line("7", "gone", "two", False)
        self.store.delete_line(a.id, "command line")
        self.assertEqual(sorted(self.store.forget("7")), [a.id, b.id])

    def test_a_forget_gives_back_the_deletions_after_the_hubs_last_look_and_the_live_lines_and_not_the_old_deletions(self):
        lines = [self.store.add_line("7", "gone", "line %d" % n, False) for n in range(10)]
        for line in lines[:4]:
            self.store.delete_line(line.id, "command line")                           # (long ago: the hub knew them)
        last_look = self.store.last_deletion()
        self.store.delete_line(lines[4].id, "command line")                            # (after the last look: the screens were not told)
        self.assertEqual(sorted(self.store.forget("7", last_look)), sorted([lines[4].id] + [l.id for l in lines[5:]]))
        self.store.add_line("8", "other", "x", False)
        self.assertEqual(self.store.forget("9"), [])

    def test_a_login_that_ends_asks_for_the_cut_of_the_log_file_and_a_cut_that_works_says_it_is_done(self):
        self.store.add_session("b" * 64, "7", "gone", 100, 10, 100)
        self.store.checkpoint()
        self.assertFalse(self.store.unsettled)
        self.assertTrue(self.store.delete_session("b" * 64))
        self.assertTrue(self.store.unsettled)
        self.store.checkpoint()
        self.assertFalse(self.store.unsettled)
        self.assertFalse(self.store.delete_session("c" * 64))                        # (a login that was not there is the same)
        self.assertTrue(self.store.unsettled)



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

    def test_a_wrong_address_of_the_site_is_a_message_and_not_a_traceback(self):
        for bad in ("https://host:abc", "http://[::1", "https://host:99999", "https://host:-1", "https://[::1]:x"):
            with self.assertRaises(config.ConfigError, msg=bad) as caught:
                config.load({"ANTS_HUB_PUBLIC_URL": bad})
            self.assertIn("ANTS_HUB_PUBLIC_URL", str(caught.exception))
            self.assertNotIn(bad, str(caught.exception))

    def test_an_account_id_is_digits_without_a_leading_zero(self):
        for good in ("1", "9", "10", "1001", "123456789012345678", "1" * 20):
            self.assertTrue(config.ACCOUNT_ID.match(good), good)
        for bad in ("", "0", "00012", "01", "1" * 21, "12a", " 12", "12\n"):
            self.assertFalse(config.ACCOUNT_ID.match(bad), repr(bad))
        with self.assertRaises(config.ConfigError):
            config.parse_mods("1001,0123")

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
        secret, client, mod = "canary-secret-8c1d5e", "4242424242420077", "7777777777"
        cfg = config.load({"ANTS_HUB_DISCORD_CLIENT_SECRET": secret, "ANTS_HUB_DISCORD_CLIENT_ID": client, "ANTS_HUB_MODS": mod, "ANTS_HUB_PUBLIC_URL": "https://x.example.org"})
        for text in (repr(cfg), str(cfg), json.dumps(cfg.summary())):
            for canary in (secret, client, mod):
                self.assertNotIn(canary, text)
        self.assertEqual(cfg.summary()["mods"], 1)
        self.assertEqual(cfg.login_client_secret, secret)             # (it is read: the Discord login needs it)


def identify(sessions, headers):
    return asyncio.run(sessions.identify(headers))


class TheSessions(unittest.TestCase):
    def test_no_login_knows_nobody(self):
        self.assertIsNone(identify(session.NoLogin(), {"x-hub-test-login": "1001:alice", "cookie": "a=b"}))

    def test_the_test_login_reads_one_header(self):
        login = session.TestLogin()
        self.assertTrue(login.login)
        self.assertEqual(identify(login, {"x-hub-test-login": "1001:alice"}), ALICE)
        self.assertEqual(identify(login, {"x-hub-test-login": "1003:newbie:young"}), YOUNG)
        for bad in ("", "1001", "1001:", ":alice", "x:alice", "1001:Alice", "1001:a", "1001:alice:old", "1001:alice:young:x", "1001:ali ce", "1" * 21 + ":alice", "1001:alice\n", "0:alice", "01001:alice"):
            self.assertIsNone(identify(login, {"x-hub-test-login": bad}), bad)
        self.assertIsNone(identify(login, {}))

    def test_identify_is_a_coroutine_so_that_a_real_login_can_look_something_up(self):
        import inspect
        for sessions in (session.Sessions(), session.NoLogin(), session.TestLogin()):
            self.assertTrue(inspect.iscoroutinefunction(sessions.identify), type(sessions).__name__)


class TheDatabaseThatCannotBeUsed(WorldTest):
    """A write that fails because another process holds the database's write lock longer than the hub waits (or the disk refuses): the sender is told, the socket stays, nothing is counted against it."""

    def locked(self, name):
        return mock.patch.object(self.w.store, name, side_effect=sqlite3.OperationalError("database is locked"))

    def test_a_line_that_could_not_be_kept_is_answered_rate_and_is_no_mistake_and_costs_no_token(self):
        seen = []
        self.w.chat.on_store_error = seen.append
        with self.locked("add_line"):
            self.w.say(self.alice, "hello")
        self.assertEqual(self.alice.codes(), ["rate"])
        self.assertEqual((self.alice.strikes, self.alice.charges, self.alice.closed), (0, 1, None))     # (not a mistake, but one frame of its budget more: the refusal is not free)
        self.assertEqual(len(seen), 1)
        self.assertIsInstance(seen[0], sqlite3.OperationalError)
        self.assertEqual(self.bob.sent, [])                                                             # (nobody saw a line)
        self.assertEqual(self.w.store.recent_lines(10), [])
        for n in range(3):
            self.w.say(self.alice, "hello")                                                              # (the same text at once, and the full burst of 3 are still there: nothing was spent)
            self.w.clock.now += 0.001
        self.assertEqual([m["text"] for m in self.bob.kinds("line")], ["hello"])
        self.assertEqual(self.alice.codes(), ["rate", "rate", "rate"])                                   # (the second and third are the same text: dup, as always)

    def test_the_global_limit_gets_its_token_back_too(self):
        w = World(limits=dataclasses.replace(LIMITS, global_burst=1, global_refill_seconds=1e9))
        self.addCleanup(w.close)
        alice = w.join(ALICE)
        with mock.patch.object(w.store, "add_line", side_effect=sqlite3.OperationalError("database is locked")):
            w.say(alice, "one")
        w.say(alice, "two")                                                                              # (the one token of all accounts together was not used by the line that was not kept)
        self.assertEqual(alice.codes(), ["rate"])
        self.assertEqual([m["text"] for m in alice.kinds("line")], ["two"])

    def test_a_moderator_s_command_that_could_not_be_done_is_answered_bad_and_the_socket_stays(self):
        mod = self.w.join(MOD)
        self.w.say(self.alice, "an ugly line")
        line_id = self.w.store.recent_lines(1)[0].id
        for name, command in (("mute", "/mute alice 5"), ("delete_line", "/del %d" % line_id)):
            with self.locked(name):
                self.w.say(mod, command)
        self.assertEqual(mod.codes(), ["bad", "bad"])
        self.assertEqual((mod.strikes, mod.charges, mod.closed), (0, 2, None))
        self.assertEqual(self.w.store.muted_until("1001"), 0)                                            # (the command did not happen, and is typed again)
        self.w.say(mod, "/mute alice 5")
        self.assertEqual(mod.codes(), ["bad", "bad"])
        self.assertTrue(self.w.store.muted_until("1001"))

    def test_a_read_that_fails_is_the_same(self):
        for name in ("is_banned", "muted_until", "size"):
            peer = self.w.join(ALICE)
            self.w.clock.now += 100
            with self.locked(name):
                self.w.say(peer, "hello %s" % name)
            self.assertEqual((peer.codes(), peer.strikes, peer.closed), (["rate"], 0, None), name)

    def test_any_other_database_error_is_still_the_hubs_to_deal_with(self):
        with mock.patch.object(self.w.store, "add_line", side_effect=sqlite3.IntegrityError("not a lock")):
            with self.assertRaises(sqlite3.IntegrityError):                                              # (the socket's task ends and the hub closes it with 1011, hub/server.py)
                self.w.say(self.alice, "hello")


class TheFreeRefusalsCostAFrame(unittest.TestCase):
    def test_a_refusal_that_is_not_a_mistake_takes_a_frame_of_the_budget_and_an_empty_budget_is_a_mistake(self):
        from hub import server as serverlib
        clock = Clock(10.0)
        struck = []
        hub = types_namespace(limits=limitmod.Limits(frame_burst=3, frame_refill_seconds=1e9), clock=clock)
        peer = serverlib.Peer.__new__(serverlib.Peer)
        peer.hub = hub
        peer.frames = limitmod.TokenBucket(3, 1e9, clock())
        peer.strike = lambda: struck.append(1)
        for n in range(5):
            peer.charge()
        self.assertEqual(len(struck), 2)                                                                  # (3 frames of budget: 3 refusals are free of mistakes, the 4th and 5th are mistakes)


def types_namespace(**kw):
    import types
    return types.SimpleNamespace(**kw)


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

    def test_a_number_that_cannot_be_a_line_is_a_wrong_use_and_not_a_traceback(self):
        for number in ("99999999999999999999", "0", "-3", str(2 ** 62), str(2 ** 63)):
            status, _, err = self.run_admin("del", number)
            self.assertEqual(status, 2, number)
            self.assertIn("LINE_ID", err)

    def test_an_account_id_with_a_leading_zero_makes_no_phantom_ban(self):
        for command in (("ban", "00012"), ("mute", "007", "5"), ("unban", "0123")):
            self.assertEqual(self.run_admin(*command)[0], 1, command)
        store = storemod.Store(self.db)
        self.addCleanup(store.close)
        self.assertEqual((store.bans(), store.mutes()), ([], []))

    def test_a_database_that_refuses_the_write_is_a_message_and_not_a_traceback(self):
        for command in (("ban", "alice"), ("mute", "alice", "5"), ("del", "1"), ("unban", "1001")):
            with mock.patch.object(storemod.Store, "transaction", side_effect=sqlite3.OperationalError("database or disk is full")):
                status, out, err = self.run_admin(*command)
            self.assertEqual((status, out), (1, ""), command)
            self.assertEqual(err, "hub.admin: the database refused it (OperationalError)\n", command)

    def test_a_file_that_is_not_a_database_is_a_message_too(self):
        path = os.path.join(self.folder.name, "garbage.db")
        with open(path, "wb") as f:
            f.write(b"this is not a database at all" * 10)
        out, err = io.StringIO(), io.StringIO()
        self.assertEqual(admin.run(["--db", path, "list"], out, err), 1)
        self.assertIn("the database cannot be opened", err.getvalue())

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
