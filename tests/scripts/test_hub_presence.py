#!/usr/bin/env python3
"""The list of the front page's players: who is on Home right now (hub/presence.py; docs/HUB.md, "Presence"; run by ./run_tests.sh --fast and by the CI).

A page that has the front page open says what its visitor is doing (looking, waiting in a room, in a match) and asks for the list; the hub keeps it in memory for as long as the socket is open and sends the
whole list once and then the changes, at most every two seconds. What this file checks:

  - the count: an account is one person however many tabs it has, a guest is one socket, a closed socket is nobody; the chat's "here now" is that number (welcome, ping, the broadcast);
  - the list: the people by their account's name (never an id), moderators marked, the one that does the most of an account's sockets is its row, the order, the cap (200 rows and a count of the rest),
    a guest counted and never named, the numbers of a row stay while the person is here;
  - what a page may say: nothing but looking, waiting and playing, with a map's file name, seats and a room's code only for a room that the page calls public (a private room, a match, a visitor who is looking
    carry none of it, and a code is never relayed for a room that is not announced as public), every field checked for its kind and size, a guest cannot say anything;
  - the sends: nothing to a socket that did not ask, the whole list at the first ask and again only after 10 seconds, the changes in one frame at most every 2 seconds (a flapping person is one change), a
    change of more than 50 rows is the whole list, and a page that asked in the middle of an interval is brought to the same list as the others (a random run against a model of the page);
  - memory only: nothing is added to the database, a socket that is gone leaves the list and the hub's own tables, the log of a real process holds no name, map or code of the list;
  - on real sockets (tests/scripts/hub_rig.py): the list over the wire, a name that is not a username is a guest, a guest's `here` is refused, a flood of `here` frames is no cheaper than a flood of
    anything else (the socket is closed).
tests/data/hub_presence_mutants.json breaks the rules one at a time for tools/mutate.py (every one must be caught).
"""
import json
import os
import random
import signal
import sqlite3
import sys
import tempfile
import time
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
import hub_client as hc  # noqa: E402
import hub_rig  # noqa: E402
import test_hub_server as ths  # noqa: E402  (its helpers start the real process and read its log)
from hub import chat as chatmod, limits as limitmod, presence, session, store as storemod  # noqa: E402

LIMITS = limitmod.Limits()
MODS = ("9001",)


def person(n, name=None, young=False):
    return session.Identity(str(1000 + n), name or "person%d" % n, young)


MOD = session.Identity("9001", "moddy", False)


class FakePeer:
    def __init__(self, identity=None):
        self.identity = identity
        self.sent = []
        self.strikes = 0
        self.charges = 0
        self.closed = False

    def send_text(self, data):
        self.sent.append(json.loads(data.decode("utf-8")))

    def strike(self):
        self.strikes += 1

    def charge(self):
        self.charges += 1

    def kinds(self, kind):
        return [m for m in self.sent if m["t"] == kind]

    def frames(self):
        return self.kinds("who")

    def codes(self):
        return [m["code"] for m in self.kinds("err")]


class Page:
    """What a page does with the frames of the list: the whole list replaces what it has, a change upserts the rows that it names and drops the numbers that are gone."""

    def __init__(self, peer):
        self.peer = peer
        self.seen = 0
        self.rows, self.n, self.g, self.more = {}, None, None, None

    def update(self):
        for frame in self.peer.frames()[self.seen:]:
            if frame["full"]:
                self.rows = {}
            for row in frame["rows"]:
                self.rows[row["i"]] = row
            for ident in frame.get("gone", []):
                self.rows.pop(ident, None)
            self.n, self.g, self.more = frame["n"], frame["g"], frame["more"]
        self.seen = len(self.peer.frames())
        return self


class World:
    """A chat with a store in a temporary folder, a clock that the test moves, and peers."""

    def __init__(self, limits=LIMITS, mods=MODS):
        self.folder = tempfile.TemporaryDirectory()
        self.db = os.path.join(self.folder.name, "hub.db")
        self.now = 1000.0
        self.limits = limits
        self.store = storemod.Store(self.db, clock=lambda: 1_800_000_000.0, retention_days=limits.retention_days)
        self.chat = chatmod.Chat(self.store, frozenset(mods), limits, lambda: self.now, True)
        self.presence = self.chat.presence

    def join(self, identity=None):
        peer = FakePeer(identity)
        self.chat.peers.add(peer)
        return peer

    def leave(self, peer):
        peer.closed = True
        self.chat.peers.discard(peer)

    def send(self, peer, **message):
        self.chat.on_text(peer, json.dumps(message))

    def here(self, peer, at="look", **fields):
        self.send(peer, t="here", at=at, **fields)

    def watch(self, peer):
        self.send(peer, t="who")
        return Page(peer)

    def advance(self, seconds):
        self.now += seconds
        self.presence.flush(self.now)

    def close(self):
        self.store.close()
        self.folder.cleanup()


class WorldTest(unittest.TestCase):
    def setUp(self):
        self.w = World()
        self.addCleanup(self.w.close)


# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
# the limits (docs/HUB.md, "Limits")
# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

class TheLimits(unittest.TestCase):
    def test_they_are_the_documented_ones(self):
        limits = limitmod.Limits()
        self.assertEqual((limits.who_rows_max, limits.who_seconds, limits.who_soon_seconds, limits.who_resend_seconds, limits.who_diff_max), (200, 2.0, 0.25, 10.0, 50))
        self.assertEqual(presence.SEATS, 4)
        self.assertEqual(presence.AT, ("look", "wait", "play"))

    def test_a_frame_from_a_client_is_still_2_kb_so_what_a_page_says_is_never_big(self):
        self.assertEqual(limitmod.Limits().frame_max, 2048)


# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
# the count
# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

class TheCount(WorldTest):
    def test_an_account_is_one_person_however_many_tabs_and_a_guest_is_one_socket(self):
        self.w.join(person(1))
        self.w.join(person(1))
        self.w.join(person(2))
        self.w.join(None)
        self.w.join(None)
        self.assertEqual(self.w.presence.people(), 4)

    def test_a_closed_socket_is_nobody_even_before_the_hub_has_let_go_of_it(self):
        a, g = self.w.join(person(1)), self.w.join(None)
        a.closed = True
        self.assertEqual(self.w.presence.people(), 1)
        g.closed = True
        self.assertEqual(self.w.presence.people(), 0)

    def test_the_chats_here_now_is_that_number_in_the_welcome_the_answer_to_a_ping_and_the_broadcast(self):
        a1, a2, g = self.w.join(person(1)), self.w.join(person(1)), self.w.join(None)
        self.assertEqual(self.w.chat.welcome(g)["online"], 2)
        self.w.send(a1, t="ping")
        self.assertEqual(a1.kinds("online"), [{"t": "online", "n": 2}])
        self.w.chat.online()
        for peer in (a1, a2, g):
            self.assertEqual(peer.kinds("online")[-1], {"t": "online", "n": 2})

    def test_the_welcome_says_that_this_hub_has_the_list(self):
        self.assertIs(self.w.chat.welcome(self.w.join(None))["presence"], True)


# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
# the list
# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

class TheList(WorldTest):
    def names(self, page):
        return [r["name"] for r in page.rows.values()]

    def test_the_first_ask_is_answered_at_once_with_the_whole_list_the_asker_in_it(self):
        a = self.w.join(person(1, "alice"))
        self.w.join(person(2, "bob"))
        self.w.join(None)
        self.w.here(a, "look")
        page = self.w.watch(a).update()
        self.assertEqual(len(a.frames()), 1)
        self.assertIs(a.frames()[0]["full"], True)
        self.assertEqual((page.n, page.g, page.more), (3, 1, 0))
        self.assertEqual(sorted(self.names(page)), ["alice", "bob"])

    def test_a_row_has_the_name_and_where_and_never_an_account_id(self):
        a = self.w.join(person(1, "alice"))
        self.w.join(MOD)
        page = self.w.watch(a).update()
        rows = sorted(page.rows.values(), key=lambda r: r["name"])
        self.assertEqual([{k: v for k, v in r.items() if k != "i"} for r in rows], [{"name": "alice", "at": "look"}, {"name": "moddy", "at": "look", "mod": True}])
        self.assertNotIn("1001", json.dumps(a.frames()))
        self.assertNotIn("9001", json.dumps(a.frames()))

    def test_a_guest_is_counted_and_never_named(self):
        self.w.join(None)
        g = self.w.join(None)
        page = self.w.watch(g).update()
        self.assertEqual((page.n, page.g, page.rows), (2, 2, {}))

    def test_an_account_with_two_sockets_is_one_row_and_the_one_that_does_the_most_is_its_row(self):
        a1, a2 = self.w.join(person(1, "alice")), self.w.join(person(1, "alice"))
        watcher = self.w.watch(self.w.join(None))
        self.w.here(a1, "wait", pub=True, map="TREASURE.LVL", n=2, of=4, code="c1v3rr")
        self.w.here(a2, "look")
        self.w.advance(2.5)
        row = watcher.update().rows
        self.assertEqual([r["at"] for r in row.values()], ["wait"], "a socket that looks does not hide one that waits")
        self.w.here(a2, "play", pub=True, map="ISLANDS.LVL", n=3)
        self.w.advance(2.5)
        self.assertEqual([(r["at"], r["map"]) for r in watcher.update().rows.values()], [("play", "ISLANDS.LVL")], "and a match is more than a room")
        self.w.here(a1, "wait", pub=True, map="TINY.LVL", n=1, of=2, code="t1ny00")
        self.w.advance(2.5)
        self.assertEqual([r["at"] for r in watcher.update().rows.values()], ["play"])

    def test_of_two_sockets_that_do_the_same_the_newer_word_wins(self):
        a1, a2 = self.w.join(person(1, "alice")), self.w.join(person(1, "alice"))
        watcher = self.w.watch(self.w.join(None))
        self.w.here(a1, "wait", pub=True, map="TREASURE.LVL", n=2, of=4, code="aaaaaa")
        self.w.here(a2, "wait", pub=True, map="ISLANDS.LVL", n=1, of=4, code="bbbbbb")
        self.w.advance(2.5)
        self.assertEqual([r["code"] for r in watcher.update().rows.values()], ["bbbbbb"])
        self.w.here(a1, "wait", pub=True, map="TREASURE.LVL", n=3, of=4, code="aaaaaa")
        self.w.advance(2.5)
        self.assertEqual([r["code"] for r in watcher.update().rows.values()], ["aaaaaa"])

    def test_a_socket_that_said_nothing_is_looking_and_a_moderator_is_marked(self):
        self.w.join(MOD)
        watcher = self.w.watch(self.w.join(None)).update()
        (row,) = watcher.rows.values()
        self.assertEqual((row["at"], row["mod"]), ("look", True))

    def test_the_order_is_moderators_then_those_who_wait_then_those_who_play_then_those_who_look_each_by_name(self):
        peers = {}
        for n, name in enumerate(("zed", "amy", "bob", "cat", "dan", "eve"), 1):
            peers[name] = self.w.join(person(n, name))
        mod = self.w.join(MOD)
        self.w.here(peers["zed"], "wait")
        self.w.here(peers["amy"], "look")
        self.w.here(peers["bob"], "play")
        self.w.here(peers["cat"], "wait")
        self.w.here(mod, "play")
        watcher = self.w.watch(self.w.join(None))
        order = [r["name"] for r in watcher.update().peer.frames()[0]["rows"]]
        self.assertEqual(order, ["moddy", "cat", "zed", "bob", "amy", "dan", "eve"])

    def test_more_people_than_rows_leaves_out_the_last_and_counts_them(self):
        w = World(limits=limitmod.Limits(who_rows_max=5))
        self.addCleanup(w.close)
        for n in range(1, 9):
            peer = w.join(person(n, "p%02d" % n))
            if n in (7, 8):
                w.here(peer, "wait")
        w.join(None)
        watcher = w.watch(w.join(MOD)).update()
        self.assertEqual((watcher.n, watcher.g, watcher.more), (10, 1, 4))
        self.assertEqual(sorted(r["name"] for r in watcher.rows.values()), ["moddy", "p01", "p02", "p07", "p08"], "the moderator, the two who wait, then those who look by name")

    def test_the_number_of_a_row_stays_while_the_person_is_here_and_is_new_when_they_come_back(self):
        a = self.w.join(person(1, "alice"))
        watcher = self.w.watch(self.w.join(None))
        first = list(watcher.update().rows)
        self.w.here(a, "wait")
        self.w.advance(2.5)
        self.assertEqual(list(watcher.update().rows), first)
        self.w.leave(a)
        self.w.advance(2.5)
        self.assertEqual(watcher.update().rows, {})
        self.w.join(person(1, "alice"))
        self.w.advance(2.5)
        self.assertNotEqual(list(watcher.update().rows), first)


# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
# what a page may say
# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

class WhatAPageSays(WorldTest):
    def setUp(self):
        super().setUp()
        self.a = self.w.join(person(1, "alice"))
        self.watcher = self.w.watch(self.w.join(None))

    def row(self):
        self.w.advance(2.5)
        return next(iter(self.watcher.update().rows.values()))

    def test_a_public_room_says_its_map_seats_and_code(self):
        self.w.here(self.a, "wait", pub=True, map="TREASURE.LVL", n=2, of=4, code="c1v3rr")
        r = self.row()
        self.assertEqual({k: v for k, v in r.items() if k != "i"}, {"name": "alice", "at": "wait", "pub": True, "map": "TREASURE.LVL", "n": 2, "of": 4, "code": "c1v3rr"})

    def test_a_private_room_is_a_private_room_and_nothing_more_whatever_the_page_sent(self):
        self.w.here(self.a, "wait", pub=False, map="SECRET.LVL", n=2, of=4, code="s3cr3t")
        self.assertEqual({k: v for k, v in self.row().items() if k != "i"}, {"name": "alice", "at": "wait"})
        self.w.here(self.a, "wait", map="SECRET.LVL", n=2, of=4, code="s3cr3t")
        self.assertEqual({k: v for k, v in self.row().items() if k != "i"}, {"name": "alice", "at": "wait"}, "a page that does not say that the room is public has a private one")
        self.assertNotIn("s3cr3t", json.dumps(self.watcher.peer.sent))
        self.assertNotIn("SECRET", json.dumps(self.watcher.peer.sent))

    def test_a_match_has_a_map_and_players_and_never_a_code_and_a_private_one_nothing(self):
        self.w.here(self.a, "play", pub=True, map="ISLANDS.LVL", n=3, of=4, code="p4n8dz")
        self.assertEqual({k: v for k, v in self.row().items() if k != "i"}, {"name": "alice", "at": "play", "pub": True, "map": "ISLANDS.LVL", "n": 3})
        self.w.here(self.a, "play", pub=False, map="ISLANDS.LVL", n=3, of=4, code="p4n8dz")
        self.assertEqual({k: v for k, v in self.row().items() if k != "i"}, {"name": "alice", "at": "play"})

    def test_a_visitor_who_is_looking_has_no_details_even_if_the_page_sent_them(self):
        self.w.here(self.a, "look", pub=True, map="TREASURE.LVL", n=2, of=4, code="c1v3rr")
        self.assertEqual({k: v for k, v in self.row().items() if k != "i"}, {"name": "alice", "at": "look"})

    def test_a_public_room_may_say_less(self):
        self.w.here(self.a, "wait", pub=True)
        self.assertEqual({k: v for k, v in self.row().items() if k != "i"}, {"name": "alice", "at": "wait", "pub": True})

    def test_what_is_not_what_a_page_says_is_a_mistake_and_changes_nothing(self):
        self.w.here(self.a, "wait", pub=True, map="TREASURE.LVL", n=2, of=4, code="c1v3rr")
        bad = [dict(at="away"), dict(at=None), dict(at=["wait"]), dict(at="wait", pub="yes"), dict(at="wait", pub=1),
               dict(at="wait", pub=True, n=True), dict(at="wait", pub=True, n="2"), dict(at="wait", pub=True, n=5), dict(at="wait", pub=True, n=-1), dict(at="wait", pub=True, n=1.5),
               dict(at="wait", pub=True, of=0), dict(at="wait", pub=True, of=5), dict(at="wait", pub=True, n=3, of=2),
               dict(at="wait", pub=True, map=7), dict(at="wait", pub=True, map="TREASURE"), dict(at="wait", pub=True, map="a/b.LVL"), dict(at="wait", pub=True, map="..\\x.LVL"), dict(at="wait", pub=True, map=".hidden.LVL"),
               dict(at="wait", pub=True, map="TREASURE.LVL "), dict(at="wait", pub=True, map="TREA  SURE.LVL"), dict(at="wait", pub=True, map="TREASURE\u202e.LVL"), dict(at="wait", pub=True, map="TREAS\u200bURE.LVL"),
               dict(at="wait", pub=True, map="x" * 70 + ".LVL"), dict(at="wait", pub=True, map="<b>.LVL"), dict(at="wait", pub=True, map="caf\u00e9.LVL"), dict(at="wait", pub=True, map="TREASURE.TXT"),
               dict(at="wait", pub=True, code="ABCDEF"), dict(at="wait", pub=True, code=""), dict(at="wait", pub=True, code="x" * 33), dict(at="wait", pub=True, code="a b"), dict(at="wait", pub=True, code=5),
               dict(at="look", n=9), dict(at="look", map="nope"), dict(at="look", code="UPPER")]
        for fields in bad:
            before = self.a.strikes
            self.w.here(self.a, **fields)
            self.assertEqual(self.a.codes()[-1:], ["bad"], fields)
            self.assertEqual(self.a.strikes, before + 1, fields)
        self.assertEqual({k: v for k, v in self.row().items() if k != "i"}, {"name": "alice", "at": "wait", "pub": True, "map": "TREASURE.LVL", "n": 2, "of": 4, "code": "c1v3rr"})

    def test_a_message_that_is_not_an_object_with_an_at_is_a_mistake_too(self):
        for raw in ('{"t":"here"}', '{"t":"here","at":""}', '{"t":"here","at":"LOOK"}'):
            self.w.chat.on_text(self.a, raw)
        self.assertEqual(self.a.codes(), ["bad", "bad", "bad"])

    def test_a_guest_cannot_say_where_it_is(self):
        g = self.w.join(None)
        self.w.here(g, "wait", pub=True)
        self.assertEqual((g.codes(), g.strikes), (["login"], 1))
        self.w.advance(2.5)
        self.assertEqual(self.watcher.update().rows.keys(), self.watcher.rows.keys())
        self.assertEqual(self.watcher.g, 2, "it is still counted, as a guest")

    def test_unknown_fields_are_not_kept(self):
        self.w.here(self.a, "look", name="mallory", mod=True, text="buy things")
        r = self.row()
        self.assertEqual({k: v for k, v in r.items() if k != "i"}, {"name": "alice", "at": "look"})

    def test_a_valid_message_costs_nothing_and_says_nothing_back(self):
        self.w.here(self.a, "wait", pub=True, map="TREASURE.LVL", n=2, of=4, code="c1v3rr")
        self.assertEqual((self.a.sent, self.a.strikes, self.a.charges), ([], 0, 0))


# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
# what is sent
# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

class WhatIsSent(WorldTest):
    def test_a_socket_that_did_not_ask_is_sent_nothing(self):
        quiet = self.w.join(person(1))
        asker = self.w.watch(self.w.join(person(2)))
        self.w.join(person(3))
        self.w.advance(2.5)
        self.assertEqual(quiet.sent, [])
        self.assertEqual(len(asker.update().rows), 3)

    def test_nothing_is_sent_when_nothing_changed(self):
        asker = self.w.watch(self.w.join(person(1)))
        for _ in range(5):
            self.w.advance(2.5)
        self.assertEqual(len(asker.peer.frames()), 1)

    def test_changes_in_one_interval_are_one_frame_and_a_person_who_flaps_is_one_change(self):
        a = self.w.join(person(1, "alice"))
        asker = self.w.watch(self.w.join(None))
        self.w.advance(2.5)
        for n in range(100):
            self.w.here(a, "wait" if n % 2 else "look")
        self.w.join(person(2, "bob"))
        self.w.join(None)
        self.w.advance(1.0)
        self.assertEqual(len(asker.peer.frames()), 1, "the interval is not over")
        self.w.advance(1.5)
        frames = asker.peer.frames()
        self.assertEqual(len(frames), 2)
        self.assertIs(frames[1]["full"], False)
        self.assertEqual(sorted(r["name"] for r in frames[1]["rows"]), ["alice", "bob"])
        self.assertEqual((frames[1]["n"], frames[1]["g"]), (4, 2))

    def test_a_change_is_the_rows_that_changed_and_the_numbers_of_those_that_went(self):
        a, b = self.w.join(person(1, "alice")), self.w.join(person(2, "bob"))
        asker = self.w.watch(self.w.join(None))
        ids = {r["name"]: r["i"] for r in asker.update().rows.values()}
        self.w.here(a, "wait", pub=True, map="TREASURE.LVL", n=1, of=4, code="c1v3rr")
        self.w.advance(2.5)
        self.assertEqual([r["name"] for r in asker.peer.frames()[-1]["rows"]], ["alice"])
        self.assertEqual(asker.peer.frames()[-1]["gone"], [])
        self.w.leave(b)
        self.w.advance(2.5)
        self.assertEqual((asker.peer.frames()[-1]["rows"], asker.peer.frames()[-1]["gone"]), ([], [ids["bob"]]))
        self.assertEqual(asker.update().n, 2)

    def test_only_the_guests_changing_is_a_frame_with_the_new_numbers_and_no_rows(self):
        asker = self.w.watch(self.w.join(person(1)))
        g = self.w.join(None)
        self.w.advance(2.5)
        last = asker.peer.frames()[-1]
        self.assertEqual((last["full"], last["rows"], last["gone"], last["n"], last["g"]), (False, [], [], 2, 1))
        self.w.leave(g)
        self.w.advance(2.5)
        self.assertEqual((asker.peer.frames()[-1]["n"], asker.peer.frames()[-1]["g"]), (1, 0))

    def test_a_change_of_more_rows_than_the_limit_is_the_whole_list(self):
        asker = self.w.watch(self.w.join(None))
        for n in range(1, 52):
            self.w.join(person(n))
        self.w.advance(2.5)
        last = asker.peer.frames()[-1]
        self.assertIs(last["full"], True)
        self.assertNotIn("gone", last)
        self.assertEqual(len(last["rows"]), 51)
        for n in range(52, 103):
            self.w.join(person(n))
        self.w.advance(2.5)
        self.assertEqual((asker.peer.frames()[-1]["full"], len(asker.peer.frames()[-1]["rows"])), (True, 102))
        for n in range(103, 153):
            self.w.join(person(n))
        self.w.advance(2.5)
        self.assertEqual((asker.peer.frames()[-1]["full"], len(asker.peer.frames()[-1]["rows"])), (False, 50), "fifty rows is still a change, not the list")
        self.w.join(person(200))
        self.w.advance(2.5)
        self.assertEqual((asker.peer.frames()[-1]["full"], len(asker.peer.frames()[-1]["rows"])), (False, 1))

    def test_a_second_ask_within_ten_seconds_is_dropped_and_charged_and_a_later_one_is_answered(self):
        a = self.w.join(person(1))
        self.w.send(a, t="who")
        self.w.advance(5)
        self.w.send(a, t="who")
        self.assertEqual((len(a.frames()), a.charges, a.strikes, a.kinds("err")), (1, 1, 0, []))
        self.w.advance(5.5)
        self.w.send(a, t="who")
        self.assertEqual((len(a.frames()), a.charges), (2, 1))
        self.assertIs(a.frames()[-1]["full"], True)

    def test_a_guest_may_watch(self):
        g = self.w.join(None)
        self.w.join(person(1, "alice"))
        page = self.w.watch(g).update()
        self.assertEqual((page.n, page.g, [r["name"] for r in page.rows.values()]), (2, 1, ["alice"]))

    def test_the_person_who_comes_and_asks_at_once_is_in_the_list_that_it_gets(self):
        self.w.join(person(1, "alice"))
        self.w.advance(5)
        new = self.w.join(person(2, "bob"))
        self.w.here(new, "wait", pub=True, map="TINY.LVL", n=1, of=4, code="t1ny00")
        page = self.w.watch(new).update()
        self.assertEqual((page.n, sorted(r["name"] for r in page.rows.values())), (2, ["alice", "bob"]))

    def test_the_first_ask_may_bring_the_send_forward_but_not_closer_than_a_quarter_of_a_second(self):
        a = self.w.watch(self.w.join(person(1)))
        self.w.join(person(2))
        self.w.advance(0.1)
        b = self.w.watch(self.w.join(person(3)))
        self.assertEqual(len(a.peer.frames()), 1, "asked 0.1 s after the last send: the list that b gets is the one that was last sent, and a is sent the rest at the next send")
        self.assertEqual(len(b.update().rows), 1)
        self.w.advance(2.5)
        self.assertEqual(len(a.update().rows), 3)
        self.assertEqual(len(b.update().rows), 3)

    def test_the_count_in_the_list_is_the_number_of_people_too(self):
        asker = self.w.watch(self.w.join(person(1)))
        self.w.join(person(1))
        self.w.join(None)
        self.w.advance(2.5)
        self.assertEqual((asker.update().n, self.w.presence.people()), (2, 2))


class TheListAgainstAModelOfThePage(WorldTest):
    """Random people come and go, say what they do and ask for the list at any moment; every page that is still here ends up with exactly the list that the hub has."""

    def test_every_watcher_ends_with_the_list_that_the_hub_has(self):
        for seed in range(30):
            w = World(limits=limitmod.Limits(who_rows_max=12, who_diff_max=4))
            rng = random.Random(seed)
            peers, pages = [], {}
            try:
                for _ in range(400):
                    op = rng.random()
                    if op < 0.20 or not peers:
                        peers.append(w.join(person(rng.randint(1, 18)) if rng.random() < 0.75 else None))
                    elif op < 0.30:
                        peer = peers.pop(rng.randrange(len(peers)))
                        w.leave(peer)
                        pages.pop(peer, None)
                    elif op < 0.60:
                        peer = rng.choice(peers)
                        if peer.identity is not None:
                            at = rng.choice(presence.AT)
                            fields = rng.choice([{}, {"pub": True, "map": "TREASURE.LVL", "n": rng.randint(1, 3), "of": 4, "code": "r%d" % rng.randint(1, 9)}, {"pub": False}])
                            w.here(peer, at, **fields)
                    elif op < 0.75:
                        peer = rng.choice(peers)
                        if peer not in pages:
                            pages[peer] = w.watch(peer)
                    else:
                        w.advance(rng.choice([0.05, 0.3, 1.0, 2.1, 3.0]))
                    for page in pages.values():
                        page.update()
                w.advance(2.5)
                for peer, page in pages.items():
                    page.update()
                    view = w.presence.view()
                    self.assertEqual((page.rows, page.n, page.g, page.more), (view["rows"], view["n"], view["g"], view["more"]), "seed %d" % seed)
                    self.assertLessEqual(len(page.rows), 12)
            finally:
                w.close()


# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
# memory only
# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

class MemoryOnly(WorldTest):
    def tables(self):
        con = sqlite3.connect(self.w.db)
        try:
            return sorted((name, con.execute("SELECT COUNT(*) FROM %s" % name).fetchone()[0]) for (name,) in con.execute("SELECT name FROM sqlite_master WHERE type = 'table'"))
        finally:
            con.close()

    def test_nothing_is_written_to_the_database(self):
        before = self.tables()
        a = self.w.join(person(1, "alice"))
        self.w.here(a, "wait", pub=True, map="TREASURE.LVL", n=2, of=4, code="c1v3rr")
        self.w.watch(self.w.join(None))
        self.w.advance(2.5)
        self.w.leave(a)
        self.w.advance(2.5)
        self.assertEqual(self.tables(), before)
        for path in (self.w.db, self.w.db + "-wal"):
            if os.path.exists(path):
                with open(path, "rb") as f:
                    data = f.read()
                for canary in (b"alice", b"TREASURE.LVL", b"c1v3rr"):
                    self.assertNotIn(canary, data, path)

    def test_a_socket_that_is_gone_leaves_the_list_and_every_table_of_the_hub(self):
        a, g = self.w.join(person(1, "alice")), self.w.join(None)
        self.w.here(a, "wait")
        page = self.w.watch(g).update()
        self.w.send(a, t="who")
        self.w.leave(a)
        self.w.leave(g)
        self.w.advance(2.5)
        p = self.w.presence
        self.assertEqual((p.where, p.asked, p.watchers, p.ids), ({}, {}, set(), {}))
        self.assertEqual(p.people(), 0)
        self.assertEqual(page.update().rows.keys(), page.rows.keys())

    def test_a_socket_that_is_closed_but_not_yet_let_go_of_is_out_of_the_list_at_the_next_send(self):
        a = self.w.join(person(1, "alice"))
        page = self.w.watch(self.w.join(None))
        self.assertEqual(len(page.update().rows), 1)
        a.closed = True
        self.w.advance(2.5)
        self.assertEqual((page.update().rows, page.n), ({}, 1))

    def test_a_peer_that_is_closed_is_not_sent_the_change(self):
        a = self.w.watch(self.w.join(person(1)))
        b = self.w.watch(self.w.join(person(2)))
        self.w.advance(2.5)
        b.peer.closed = True
        self.w.join(person(3))
        before = len(b.peer.frames())
        self.w.advance(2.5)
        self.assertEqual(len(b.peer.frames()), before)
        self.assertEqual(sorted(r["name"] for r in a.update().rows.values()), ["person1", "person3"], "the person of the closed socket is not in the list")


# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
# on real sockets
# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

def who_frames(client, timeout=0.5):
    return [m for m in client.drain(timeout) if m["t"] == "who"]


def send_json(client, **message):
    client.send(hc.frame(1, json.dumps(message).encode("utf-8")))


class OnTheWire(ths.RigTest):
    rig_args = dict(tick_seconds=0.05, online_seconds=0.2, who_seconds=0.2, who_soon_seconds=0.05, who_resend_seconds=0.6, mods="1002", frame_burst=1000, frame_refill_seconds=0.001)

    def login(self, login):
        c = hc.Client(self.rig.port, login=login)
        self.assertTrue(c.ok, c.status)
        self.clients.append(c)
        message = c.recv_json()
        self.assertEqual(message["t"], "welcome")
        return c, message

    def test_a_reader_and_a_member_watch_the_list_and_hear_the_changes(self):
        alice, welcome = self.login("1001:alice")
        self.assertIs(welcome["presence"], True)
        send_json(alice, t="here", at="wait", pub=True, map="TREASURE.LVL", n=2, of=4, code="c1v3rr")
        send_json(alice, t="who")
        (first,) = who_frames(alice)
        self.assertEqual((first["full"], first["n"], first["g"], [(r["name"], r["at"], r.get("code")) for r in first["rows"]]), (True, 1, 0, [("alice", "wait", "c1v3rr")]))
        guest = hc.Client(self.rig.port)
        self.clients.append(guest)
        guest.recv_json()
        send_json(guest, t="who")
        (seen,) = who_frames(guest)
        self.assertEqual((seen["n"], seen["g"], [r["name"] for r in seen["rows"]]), (2, 1, ["alice"]))
        (change,) = [m for m in who_frames(alice, 1.0)]
        self.assertEqual((change["full"], change["n"], change["g"], change["rows"], change["gone"]), (False, 2, 1, [], []))
        bob, _ = self.login("1002:bob")
        send_json(bob, t="here", at="look")
        (bob_list,) = who_frames(guest, 1.0) or [None]
        self.assertEqual([(r["name"], r.get("mod")) for r in bob_list["rows"]], [("bob", True)])
        self.assertEqual((bob_list["n"], bob_list["g"]), (3, 1))
        bob.close()
        (gone,) = who_frames(guest, 1.0)
        self.assertEqual((len(gone["gone"]), gone["n"], gone["rows"]), (1, 2, []))

    def test_the_online_count_is_the_number_of_people_two_tabs_of_one_account_are_one(self):
        self.login("1001:alice")
        self.login("1001:alice")
        c = hc.Client(self.rig.port)
        self.clients.append(c)
        self.assertEqual(c.recv_json()["online"], 2)

    def test_the_online_count_that_the_clock_sends_is_the_number_of_people_too(self):
        self.login("1001:alice")
        self.login("1001:alice")
        guest = hc.Client(self.rig.port)
        self.clients.append(guest)
        guest.recv_json()
        counts = [m["n"] for m in guest.drain(0.8) if m["t"] == "online"]
        self.assertEqual(counts[-1:], [2], counts)

    def test_a_name_that_is_not_a_username_is_a_guest_not_a_row(self):
        c = hc.Client(self.rig.port, login="1001:Eve!")
        self.clients.append(c)
        welcome = c.recv_json()
        self.assertIsNone(welcome["you"])
        send_json(c, t="who")
        (frame,) = who_frames(c)
        self.assertEqual((frame["rows"], frame["g"], frame["n"]), ([], 1, 1))

    def test_a_guest_that_says_where_it_is_is_told_to_log_in_and_a_bad_here_is_refused(self):
        guest = hc.Client(self.rig.port)
        self.clients.append(guest)
        guest.recv_json()
        send_json(guest, t="here", at="look")
        self.assertEqual(guest.recv_json(), {"t": "err", "code": "login"})
        alice, _ = self.login("1001:alice")
        send_json(alice, t="here", at="wait", pub=True, map="../../etc/passwd.LVL")
        self.assertEqual(alice.recv_json(), {"t": "err", "code": "bad"})

    def test_the_whole_list_is_sent_again_only_after_the_wait(self):
        alice, _ = self.login("1001:alice")
        send_json(alice, t="who")
        send_json(alice, t="who")
        self.assertEqual(len(who_frames(alice, 0.3)), 1)
        time.sleep(0.5)
        send_json(alice, t="who")
        self.assertEqual(len(who_frames(alice, 0.3)), 1)

    def test_a_socket_that_never_asked_hears_no_list(self):
        alice, _ = self.login("1001:alice")
        self.login("1003:carol")
        self.assertEqual(who_frames(alice, 0.6), [])


class AFloodOfHereFramesIsClosed(ths.RigTest):
    rig_args = dict(tick_seconds=0.05)           # (the limits of the hub's own: a burst of 10 frames, then 5 a second, and 20 mistakes within a minute)

    def test_no_cheaper_than_any_other_flood(self):
        c = hc.Client(self.rig.port, login="1001:alice")
        self.clients.append(c)
        c.recv_json()
        try:
            for _ in range(80):
                send_json(c, t="here", at="look")
        except OSError:
            pass
        self.assertEqual(c.expect_close(5), 1008)


class TheProcessLogHoldsNothingOfTheList(unittest.TestCase):
    def test_no_name_map_or_code_is_in_the_log_of_the_process(self):
        proc, folder, log_path, env = ths.start_process({"ANTS_HUB_TEST_LOGIN": "1", "ANTS_HUB_MODS": "5551234567"})
        clients = []
        try:
            port = ths.wait_for_port(proc, log_path)
            self.assertIsNotNone(port, ths.read_text(log_path))
            alice = hc.Client(port, login="6101:alice.canary")
            clients.append(alice)
            alice.recv_json()
            send_json(alice, t="here", at="wait", pub=True, map="CANARYMAP.LVL", n=2, of=4, code="canary12")
            send_json(alice, t="who")
            (frame,) = who_frames(alice)
            self.assertEqual([(r["name"], r["map"], r["code"]) for r in frame["rows"]], [("alice.canary", "CANARYMAP.LVL", "canary12")])
            moddy = hc.Client(port, login="5551234567:moddy.canary")
            clients.append(moddy)
            moddy.recv_json()
            send_json(moddy, t="who")
            self.assertEqual(len(who_frames(moddy)), 1)
            send_json(alice, t="here", at="wait", pub=True, map="not a map")
            self.assertEqual(alice.drain(0.3)[-1], {"t": "err", "code": "bad"})
            time.sleep(0.2)
            proc.send_signal(signal.SIGTERM)
            proc.wait(10)
            text = ths.read_text(log_path)
            self.assertIn(" open ", text)
            for canary in ("alice", "moddy", "canary", "CANARYMAP", "canary12", "6101", "5551234567", "not a map", "Traceback"):
                self.assertNotIn(canary, text, "the log shows " + canary)
        finally:
            for c in clients:
                c.close()
            if proc.poll() is None:
                proc.kill()
            proc.wait(5)


if __name__ == "__main__":
    unittest.main()
