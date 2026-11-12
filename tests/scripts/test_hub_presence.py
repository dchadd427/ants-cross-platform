#!/usr/bin/env python3
"""The list of the front page's players: who is on Home right now (hub/presence.py; docs/HUB.md, "Presence"; run by ./run_tests.sh --fast and by the CI).

A page that has the front page open says what its visitor is doing (looking, waiting in a room, in a match) and asks for the list; the hub keeps it in memory for as long as the socket is open and sends the
whole list once and then the changes, at most every two seconds. What this file checks:

  - the count: an account is one person however many tabs it has, a guest is one socket, a closed socket is nobody; the chat's "here now" is that number (welcome, ping, the broadcast), counted again only
    when the sockets change (a ping is answered from the last count);
  - the list: the people by their account's name (never an id), moderators marked, the one that does the most of an account's sockets is its row, the order, the cap (200 rows and a count of the rest),
    a guest counted and never named, the numbers of a row stay while the person is here, and the welcome says which row is the account's own (`me`);
  - who may chat: a banned account is a guest in the list (counted, never named), a muted or too new account is named and has none of the room's details, whatever its page said;
  - the bans and the mutes that the list keeps: read again only when the store's number of its last change of them is another or the soonest mute has run out (a look that finds nothing changed is
    one small read), and the answers are those of reading both at every look, on random data (a second connection to the file as the command line, mutes that run out, a store that refuses);
  - what a page may say: nothing but looking, waiting and playing, with a map's file name (exactly what the game takes: every map of the repository, and the game's own list of good and bad names), seats and a
    room's code only for a room that the page calls public (a private room, a match, a visitor who is looking carry none of it, and a code is never relayed for a room that is not announced as public), every
    field checked for its kind and size, a guest cannot say anything, a `here` that is refused has a code of its own;
  - the sends: nothing to a socket that did not ask, the whole list at the first ask and again only after 10 seconds, the changes in one frame to every watcher at once and never sooner than 2 seconds after
    the last send whatever happens (a flapping person and a crowd that comes and asks are one change; an ask brings nothing forward), a change of more than 50 rows is the whole list, the first page to ask
    when nobody keeps the list current gets it as it is now (a random run against a model of the page);
  - memory only: nothing is added to the database, a socket that is gone leaves the list and the hub's own tables, the log of a real process holds no name, map or code of the list;
  - the clock: the list's own work and the count of people cannot stop the retention, a failure that goes on is a line in the log now and then and not one for every tick, and the log says sockets for the
    number of open sockets;
  - on real sockets (tests/scripts/hub_rig.py): the list over the wire, a name that is not a username is a guest, a guest's `here` is refused, a flood of `here` frames is no cheaper than a flood of
    anything else (the socket is closed).
tests/data/hub_presence_mutants.json breaks the rules one at a time for tools/mutate.py (every one must be caught).
"""
import json
import os
import random
import signal
import socket
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


def shown(n, name="Maple Leaf"):
    """An account that is shown by its display name: its username is its own, and the name that the list shows is not unique."""
    return session.Identity(str(1100 + n), name, False, None, 0, "user.%d" % n, "display", "user.%d" % n)


class FakePeer:
    def __init__(self, identity=None):
        self.identity = identity
        self.sent = []
        self.sizes = []
        self.strikes = 0
        self.charges = 0
        self.closed = False

    def send_text(self, data):
        self.sizes.append(len(data))
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


class FakeWriter:
    """The part of an asyncio StreamWriter that hub.server.Peer uses."""

    class transport:
        @staticmethod
        def get_write_buffer_size():
            return 0

        @staticmethod
        def abort():
            pass

    def write(self, data):
        pass

    def is_closing(self):
        return False

    def close(self):
        pass


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

    def __init__(self, limits=LIMITS, mods=MODS, store_clock=lambda: 1_800_000_000.0):
        self.folder = tempfile.TemporaryDirectory()
        self.db = os.path.join(self.folder.name, "hub.db")
        self.now = 1000.0
        self.limits = limits
        self.store = storemod.Store(self.db, clock=store_clock, retention_days=limits.retention_days)
        self.chat = chatmod.Chat(self.store, hub_rig.FakeRoles({a: "mod" for a in mods}), limits, lambda: self.now, True)
        self.presence = self.chat.presence

    def join(self, identity=None):
        peer = FakePeer(identity)
        self.chat.peers.add(peer)
        return peer

    def leave(self, peer):
        peer.closed = True
        self.chat.peers.discard(peer)

    def shut(self, peer):
        """A socket that closes and that the hub has not let go of yet (hub.server.Peer.closed tells the set of sockets that it changed)."""
        peer.closed = True
        self.chat.peers.version += 1

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
        self.assertEqual((limits.who_rows_max, limits.who_seconds, limits.who_resend_seconds, limits.who_diff_max), (200, 2.0, 10.0, 50))
        self.assertFalse(hasattr(limits, "who_soon_seconds"), "an ask brings no send forward")
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
        self.assertEqual(self.w.presence.people(), 2)
        self.w.shut(a)
        self.assertEqual(self.w.presence.people(), 1)
        self.w.shut(g)
        self.assertEqual(self.w.presence.people(), 0)

    def test_the_number_is_counted_again_only_when_the_sockets_change(self):
        looked = []

        class Counting(FakePeer):
            @property
            def identity(self):
                looked.append(1)
                return self._identity

            @identity.setter
            def identity(self, value):
                self._identity = value

        for n in range(1, 6):
            self.w.chat.peers.add(Counting(person(n)))
        self.assertEqual(self.w.presence.people(), 5)
        counted = len(looked)
        for _ in range(100):
            self.assertEqual(self.w.presence.people(), 5)
        self.assertEqual(len(looked), counted, "a hundred pings are answered from the last count")
        self.w.chat.peers.add(Counting(person(6)))
        self.assertEqual(self.w.presence.people(), 6)
        self.assertGreater(len(looked), counted)

    def test_every_way_to_change_the_set_of_sockets_tells_the_count(self):
        readers = {"copy", "difference", "intersection", "isdisjoint", "issubset", "issuperset", "symmetric_difference", "union"}
        mutators = {name for name in dir(set) if not name.startswith("_") and name not in readers} | {"__ior__", "__iand__", "__isub__", "__ixor__"}
        self.assertEqual(sorted(name for name in mutators if getattr(presence.Peers, name) is getattr(set, name)), [], "a way to change the set that the count is not told of")
        peers, a, b = presence.Peers(), FakePeer(person(1)), FakePeer(person(2))
        version = [peers.version]

        def changed(label):
            self.assertGreater(peers.version, version[0], label)
            version[0] = peers.version

        peers.add(a); changed("add")
        peers.update([b]); changed("update")
        peers.discard(b); changed("discard")
        peers.remove(a); changed("remove")
        peers.add(a); changed("add again")
        peers.pop(); changed("pop")
        peers.update([a, b]); changed("update again")
        peers.difference_update([b]); changed("difference_update")
        peers.intersection_update([a]); changed("intersection_update")
        peers.symmetric_difference_update([b]); changed("symmetric_difference_update")
        peers |= {a}; changed("|=")
        peers &= {a}; changed("&=")
        peers -= {b}; changed("-=")
        peers ^= {b}; changed("^=")
        peers.clear(); changed("clear")
        self.assertIsInstance(peers, set)
        self.assertIsInstance(self.w.chat.peers, presence.Peers, "the chat's own sockets are counted this way")

    def test_a_real_socket_that_closes_is_nobody_at_once(self):
        from hub import config as configmod, log as logmod, server
        import io
        hub = server.Hub(configmod.Config(db=self.w.db), self.w.store, session.NoLogin(), logmod.Log(io.StringIO()), limitmod.Limits(), clock=lambda: 100.0)
        peer = server.Peer(hub, FakeWriter(), "k", "tag", person(1), 0.0)
        hub.chat.peers.add(peer)
        self.assertEqual(hub.chat.presence.people(), 1)
        peer.close(1000, "bye")
        self.assertEqual(hub.chat.presence.people(), 0, "closed, and the hub has not let go of it yet")
        other = server.Peer(hub, FakeWriter(), "k", "tag", None, 0.0)
        hub.chat.peers.add(other)
        self.assertEqual(hub.chat.presence.people(), 1)
        other.closed = True
        self.assertEqual(hub.chat.presence.people(), 0, "whatever closes a socket")

    def test_a_count_that_missed_a_change_is_counted_again_at_the_next_tick(self):
        a = self.w.join(person(1))
        self.assertEqual(self.w.presence.people(), 1)
        a.closed = True                                                # (a socket that closes without a word to the count)
        self.assertEqual(self.w.presence.people(), 1)
        self.w.advance(1.0)
        self.assertEqual(self.w.presence.people(), 0)

    def test_the_chats_here_now_is_that_number_in_the_welcome_the_answer_to_a_ping_and_the_broadcast(self):
        a1, a2, g = self.w.join(person(1)), self.w.join(person(1)), self.w.join(None)
        self.assertEqual(self.w.chat.welcome(g)["online"], 2)
        self.w.send(a1, t="ping")
        self.assertEqual(a1.kinds("online"), [{"t": "online", "n": 2}])
        self.w.chat.online()
        for peer in (a1, a2, g):
            self.assertEqual(peer.kinds("online")[-1], {"t": "online", "n": 2})

    def test_the_hubs_clock_sends_the_online_count_when_the_people_change_not_when_a_second_tab_opens(self):
        from hub import config as configmod, log as logmod, server
        import io

        class Transport:
            def abort(self):
                pass

        class Writer:
            transport = Transport()

        class TickPeer(FakePeer):
            def __init__(self, identity=None):
                super().__init__(identity)
                self.writer, self.last_rx, self.last_ping, self.closed_at = Writer(), float("inf"), float("inf"), None          # (never silent, never due a ping)

            def write(self, frame):
                pass

        now = [100.0]
        hub = server.Hub(configmod.Config(db=self.w.db), self.w.store, session.NoLogin(), logmod.Log(io.StringIO()), limitmod.Limits(), clock=lambda: now[0])
        watcher, first = TickPeer(None), TickPeer(person(1))
        hub.chat.peers.update([watcher, first])
        hub.on_tick(now[0])
        self.assertEqual(watcher.kinds("online"), [{"t": "online", "n": 2}])
        now[0] += 10
        hub.on_tick(now[0])
        self.assertEqual(len(watcher.kinds("online")), 1, "nothing changed")
        hub.chat.peers.add(TickPeer(person(1)))                        # (a second tab of the same account: three sockets, still two people)
        now[0] += 10
        hub.on_tick(now[0])
        self.assertEqual(len(watcher.kinds("online")), 1, "a second tab is not another person: the clock has nothing to say")
        hub.chat.peers.add(TickPeer(None))
        now[0] += 10
        hub.on_tick(now[0])
        self.assertEqual(watcher.kinds("online")[-1], {"t": "online", "n": 3})
        self.assertEqual(len(watcher.kinds("online")), 2)

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

    def test_the_welcome_of_a_login_says_which_row_is_its_own_and_a_reader_has_none(self):
        a1, a2, b, g = self.w.join(person(1, "alice")), self.w.join(person(1, "alice")), self.w.join(person(2, "bob")), self.w.join(None)
        mine = [self.w.chat.welcome(p)["me"] for p in (a1, a2, b)]
        self.assertEqual((mine[0], len(set(mine))), (mine[1], 2), "an account has one row, however many tabs; two accounts two")
        self.assertNotIn("me", self.w.chat.welcome(g))
        page = self.w.watch(g).update()
        self.assertEqual(sorted(page.rows), sorted(mine[1:]), "the number is the row's own, and the same in the list")
        self.assertEqual(page.rows[mine[0]]["name"], "alice")
        a3 = self.w.join(person(1, "alice"))
        self.assertEqual(self.w.chat.welcome(a3)["me"], mine[0], "and it stays while the account has a socket")
        self.w.leave(a1)
        self.w.leave(a2)
        self.w.advance(2.5)
        self.assertEqual(sorted(r["name"] for r in page.update().rows.values()), ["alice", "bob"], "a third tab keeps the row")
        self.w.leave(a3)
        self.w.advance(2.5)
        self.assertEqual([r["name"] for r in page.update().rows.values()], ["bob"])
        self.assertNotEqual(self.w.chat.welcome(self.w.join(person(1, "alice")))["me"], mine[0], "an account that came back is a new row")

    def test_a_row_has_the_name_and_where_and_never_an_account_id(self):
        a = self.w.join(person(1, "alice"))
        self.w.join(MOD)
        page = self.w.watch(a).update()
        rows = sorted(page.rows.values(), key=lambda r: r["name"])
        self.assertEqual([{k: v for k, v in r.items() if k != "i"} for r in rows], [{"name": "alice", "at": "look"}, {"name": "moddy", "at": "look", "mod": True, "role": "mod"}])
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

    def test_the_biggest_frame_is_the_size_that_the_docs_say(self):
        for n in range(1, 201):
            peer = self.w.join(person(n, "n" * 29 + "%03d" % n))
            self.w.here(peer, "wait", pub=True, map="M" * 60 + ".lvl", n=3, of=4, code="c" * 32)
        watcher = self.w.watch(self.w.join(None))
        self.assertEqual(len(watcher.update().rows), 200)
        self.assertTrue(40000 < max(watcher.peer.sizes) < 42000, "200 rows with the longest username, map and code that a row can have are about 41 KB: %d" % max(watcher.peer.sizes))

    def test_the_biggest_frame_of_names_that_are_shown_is_the_size_that_the_docs_say(self):
        for n in range(1, 201):
            peer = self.w.join(shown(n, "\U00020000" * 32))                                           # (a shown name is up to 32 code points of up to four bytes each, and names are not unique)
            self.w.here(peer, "wait", pub=True, map="M" * 60 + ".lvl", n=3, of=4, code="c" * 32)
        watcher = self.w.watch(self.w.join(None))
        self.assertEqual(len(watcher.update().rows), 200)
        self.assertTrue(59000 < max(watcher.peer.sizes) < 61500, "200 rows with the longest shown name, map and code that a row can have are about 60 KB: %d" % max(watcher.peer.sizes))
        self.assertLess(max(watcher.peer.sizes), self.w.limits.write_buffer_max)

    def test_two_accounts_that_show_the_same_name_are_two_rows_and_each_is_told_its_own_by_its_number(self):
        a, b = self.w.join(shown(1)), self.w.join(shown(2))
        watcher = self.w.watch(self.w.join(None)).update()
        mine = (self.w.chat.welcome(a)["me"], self.w.chat.welcome(b)["me"])
        self.assertEqual((sorted(self.names(watcher)), len(set(mine)), set(watcher.rows)), (["Maple Leaf", "Maple Leaf"], 2, set(mine)))
        self.assertEqual(self.w.chat.welcome(a)["you"]["name"], self.w.chat.welcome(b)["you"]["name"])
        self.assertNotEqual(self.w.chat.welcome(a)["me"], self.w.chat.welcome(b)["me"], "a name is not how the page knows its own row")
        self.w.here(a, "wait")
        self.w.advance(2.5)
        self.assertEqual({r["i"]: r["at"] for r in watcher.update().rows.values()}, {mine[0]: "wait", mine[1]: "look"})

    def test_the_rows_of_people_who_show_the_same_name_are_in_the_order_of_their_accounts_and_every_send_is_the_same_whoever_asks(self):
        for n in (3, 1, 2):
            self.w.join(shown(n))
        first = self.w.watch(self.w.join(None))
        second = self.w.watch(self.w.join(MOD))
        self.assertEqual([r["i"] for r in first.update().peer.frames()[0]["rows"]][:3], sorted(r["i"] for r in first.rows.values() if r["name"] == "Maple Leaf"))
        self.assertEqual(first.peer.frames()[0]["rows"], [r for r in second.update().peer.frames()[0]["rows"]])

    def test_a_row_holds_the_name_that_is_shown_and_never_a_username_not_even_in_the_list_that_a_moderator_asks_for(self):
        self.w.join(shown(1))
        self.w.join(shown(2, "bbb Two"))
        for asker in (self.w.join(None), self.w.join(person(9, "carol")), self.w.join(MOD)):
            page = self.w.watch(asker).update()
            wire = json.dumps(asker.frames())
            self.assertNotIn("user.1", wire)
            self.assertNotIn("user.2", wire)
            self.assertEqual(sorted(self.names(page))[:2], ["Maple Leaf", "bbb Two"] if asker.identity is None else sorted(self.names(page))[:2])

    def test_a_switch_of_the_name_is_a_new_name_in_the_row_when_the_sockets_come_back_and_the_row_keeps_its_number(self):
        a = self.w.join(shown(1))
        watcher = self.w.watch(self.w.join(None))
        before = dict(watcher.update().rows)
        self.assertEqual([r["name"] for r in before.values()], ["Maple Leaf"])
        self.w.leave(a)
        self.w.join(session.Identity("1101", "user.1", False, None, 0, "user.1", "username", "Maple Leaf"))          # (the page connects again after a switch: the welcome of the new socket)
        self.w.advance(2.5)
        after = watcher.update().rows
        self.assertEqual([r["name"] for r in after.values()], ["user.1"])
        self.assertEqual(list(after), list(before), "the same person: the same number")

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
               dict(at="wait", pub=True, map="TREASURE.LVL "), dict(at="wait", pub=True, map="TREASURE.Lvl"), dict(at="wait", pub=True, map="TREASURE.LVL\n"), dict(at="wait", pub=True, map="TREASURE\u202e.LVL"), dict(at="wait", pub=True, map="TREAS\u200bURE.LVL"),
               dict(at="wait", pub=True, map="x" * 70 + ".LVL"), dict(at="wait", pub=True, map="<b>.LVL"), dict(at="wait", pub=True, map="caf\u00e9.LVL"), dict(at="wait", pub=True, map="TREASURE.TXT"),
               dict(at="wait", pub=True, code="ABCDEF"), dict(at="wait", pub=True, code="abcdef\n"), dict(at="wait", pub=True, code=""), dict(at="wait", pub=True, code="x" * 33), dict(at="wait", pub=True, code="a b"), dict(at="wait", pub=True, code=5),
               dict(at="look", n=9), dict(at="look", map="nope"), dict(at="look", code="UPPER")]
        for fields in bad:
            before = self.a.strikes
            self.w.here(self.a, **fields)
            self.assertEqual(self.a.codes()[-1:], ["here"], fields)
            self.assertEqual(self.a.strikes, before + 1, fields)
        self.assertEqual({k: v for k, v in self.row().items() if k != "i"}, {"name": "alice", "at": "wait", "pub": True, "map": "TREASURE.LVL", "n": 2, "of": 4, "code": "c1v3rr"})

    def test_a_map_with_two_spaces_in_its_name_is_a_map(self):
        for name in ("Bomber Match  by PRo.lvl", "FKD  Deathmatch.lvl", "PoP iT Up  PoPcOrN.lvl", "OHH MY GOD..  TRAP!!.lvl", "a.LVL", " lead.lvl", "trail .lvl", "x" * 60 + ".LVL"):
            self.w.here(self.a, "wait", pub=True, map=name, n=1, of=4, code="c1v3rr")
            self.assertEqual((self.a.codes(), self.a.strikes), ([], 0), name)
            self.assertEqual(self.row()["map"], name)

    def test_the_hub_takes_exactly_the_map_names_that_the_game_takes(self):
        def game_takes(name):                                   # net::valid_map_name (src/ants_net/protocol.cpp)
            if len(name) < 5 or len(name) > 64:
                return False
            if any(ord(c) < 0x20 or ord(c) > 0x7E or c in '/\\:*?"<>|' for c in name):
                return False
            return name[0] != "." and name[-4:] in (".LVL", ".lvl")
        unit_test = {                                           # the names in tests/test_net/test_lobby_basics.cpp
            True: ["TREASURE.LVL", "my-map_2.lvl", "!!!! My Map ~v2~ (final).lvl", "ANTS WORLD....lvl", "Beach Day !...lvl", "OHH MY GOD..  TRAP!!.lvl", "Lero .lvl", "#1 Map $5 & It's.lvl", "a.LVL", "x y.lvl",
                   "M" * 60 + ".lvl"],
            False: ["../secret.LVL", "a/b.LVL", "a\\b.LVL", ".hidden.LVL", "x.txt", "LVL", "a:b.LVL", "a*b.lvl", "a?b.lvl", 'a"b.lvl', "a<b.lvl", "a>b.lvl", "a|b.lvl", "tab\t.lvl", "del\x7f.lvl", "caf\u00e9.lvl",
                    "nul\0x.lvl", "x.lv", "x.lvlx", "..lvl", "M" * 61 + ".lvl", "", "x.Lvl", "x.lVL", "name.lvl ", "\ufeffname.lvl"],
        }
        for expected, names in unit_test.items():
            for name in names:
                self.assertEqual((presence.map_file(name) is not None, game_takes(name)), (expected, expected), repr(name))
        rng = random.Random(7)
        alphabet = "abcXYZ019 .!~#$&'()-_+=,;[]{}@%^`" + '/\\:*?"<>|' + "\t\n\r\x00\x1f\x7f\u00e9\u202e\u200b"
        for _ in range(30000):
            name = "".join(rng.choice(alphabet) for _ in range(rng.randint(0, 70)))
            name = rng.choice(["", ".", " ", "..", "x"]) + name + rng.choice([".lvl", ".LVL", ".Lvl", ".lvL", ".lv", "lvl", "", ".lvl\n", ".lvl "])
            self.assertEqual(presence.map_file(name) is not None, game_takes(name), repr(name))
        for value in (None, 5, b"x.lvl", ["x.lvl"]):
            self.assertIsNone(presence.map_file(value))

    def test_it_takes_every_map_of_the_repository_and_keeps_it_as_it_is(self):
        names = []
        for folder, ending in (("Community-Maps", ".lvl"), (os.path.join("Original-Ants", "Maps"), ".lvl")):
            names += [n for n in sorted(os.listdir(os.path.join(REPO, folder))) if n.lower().endswith(ending)]
        self.assertGreaterEqual(len(names), 500, "the folders of the maps were not found")
        refused = [n for n in names if presence.map_file(n) != n]
        self.assertEqual(refused, [], "maps that the game takes and the hub does not")
        for name in [n for n in names if "  " in n] + names[::50]:
            self.w.here(self.a, "wait", pub=True, map=name, n=1, of=4, code="c1v3rr")
            self.assertEqual(self.a.codes(), [], name)
        self.assertGreaterEqual(len([n for n in names if "  " in n]), 1, "the maps with two spaces are in the folder, and in this test")

    def test_a_message_that_is_not_an_object_with_an_at_is_a_mistake_too(self):
        for raw in ('{"t":"here"}', '{"t":"here","at":""}', '{"t":"here","at":"LOOK"}'):
            self.w.chat.on_text(self.a, raw)
        self.assertEqual(self.a.codes(), ["here", "here", "here"])

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
# who may chat (a banned, a muted and a too new account may not: what they say about a room is not shown)
# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

class WhoMayChat(WorldTest):
    ROOM = dict(pub=True, map="TREASURE.LVL", n=2, of=4, code="c1v3rr")

    def setUp(self):
        super().setUp()
        self.watcher = self.w.watch(self.w.join(None))

    def rows(self):
        self.w.advance(2.5)
        return {r["name"]: {k: v for k, v in r.items() if k != "i"} for r in self.watcher.update().rows.values()}

    def test_a_muted_account_is_named_and_has_none_of_the_rooms_details_and_they_are_back_when_it_may_speak(self):
        a, b = self.w.join(person(1, "alice")), self.w.join(person(2, "bob"))
        self.w.here(a, "wait", **self.ROOM)
        self.w.here(b, "wait", **self.ROOM)
        self.w.store.mute("1001", 10, "9001", "alice")
        rows = self.rows()
        self.assertEqual(rows["alice"], {"name": "alice", "at": "wait"})
        self.assertEqual(rows["bob"], {"name": "bob", "at": "wait", **self.ROOM})
        self.assertEqual((a.codes(), a.strikes), ([], 0), "what a muted page says is taken, and shown to nobody")
        self.w.store.mute("1001", 0, "9001")
        self.assertEqual(self.rows()["alice"], {"name": "alice", "at": "wait", **self.ROOM})

    def test_a_too_new_account_is_named_and_has_none_of_the_rooms_details(self):
        a = self.w.join(person(1, "alice", young=True))
        self.w.here(a, "play", **self.ROOM)
        self.assertEqual(self.rows()["alice"], {"name": "alice", "at": "play"})
        self.assertEqual(a.codes(), [])

    def test_a_banned_account_is_a_guest_counted_and_never_named_and_is_named_again_when_it_may_speak(self):
        a, b = self.w.join(person(1, "alice")), self.w.join(person(2, "bob"))
        mine = self.w.chat.welcome(a)["me"]
        self.w.here(a, "wait", **self.ROOM)
        self.w.store.ban("1001", "9001", "alice")
        self.w.advance(2.5)
        page = self.watcher.update()
        self.assertEqual((page.n, page.g, sorted(r["name"] for r in page.rows.values())), (3, 2, ["bob"]), "the watcher and the banned account are guests, the count is the same")
        self.assertEqual(self.w.presence.people(), 3, "and so is the chat's")
        for text in ("alice", "c1v3rr", "TREASURE"):
            self.assertNotIn(text, json.dumps(self.watcher.peer.sent))
        self.assertEqual(self.w.chat.welcome(a)["me"], mine, "its number is kept, so that it is the same row when it is named again")
        self.w.store.unban("1001")
        self.w.advance(2.5)
        self.assertEqual(self.watcher.update().rows[mine]["name"], "alice")
        self.assertEqual((self.watcher.n, self.watcher.g), (3, 1))

    def test_a_banned_moderator_is_a_guest_too(self):
        self.w.join(MOD)
        self.w.store.ban("9001", "9002", "moddy")
        self.assertEqual(self.rows(), {})
        self.assertEqual((self.watcher.n, self.watcher.g), (2, 2))

    def test_a_store_that_does_not_answer_leaves_what_it_said_last(self):
        a = self.w.join(person(1, "alice"))
        self.w.here(a, "wait", **self.ROOM)
        self.w.store.mute("1001", 10, "9001", "alice")
        self.w.join(person(2, "bob"))
        self.w.store.ban("1002", "9001", "bob")
        self.assertEqual(self.rows(), {"alice": {"name": "alice", "at": "wait"}})

        def broken(*args):
            raise storemod.StoreError("locked")
        self.w.store.bans = self.w.store.mutes = broken
        self.w.join(person(3, "carol"))
        self.w.store.ban("1003", "9001", "carol")                      # (a change that the list has to read, and cannot)
        self.assertEqual(sorted(self.rows()), ["alice", "carol"], "bob is still a guest, alice still has none of the details, and carol, banned meanwhile, is not known to be")
        self.assertNotIn("code", self.rows()["alice"])
        del self.w.store.bans, self.w.store.mutes                      # (the store answers again)
        self.assertEqual(sorted(self.rows()), ["alice"], "carol is a guest from the first look that the store answers")
        self.w.store.last_restriction = broken                         # (a store that refuses refuses all of it: the number too)
        self.w.store.unban("1003")
        self.assertEqual(sorted(self.rows()), ["alice"], "the change is not known yet")
        del self.w.store.last_restriction
        self.assertEqual(sorted(self.rows()), ["alice", "carol"])

    def test_the_accounts_that_may_speak_are_as_they_were(self):
        a = self.w.join(person(1, "alice"))
        self.w.here(a, "wait", **self.ROOM)
        self.w.store.mute("1002", 10, "9001", "bob")
        self.w.store.ban("1003", "9001", "carol")
        self.assertEqual(self.rows()["alice"], {"name": "alice", "at": "wait", **self.ROOM})


# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
# the bans and the mutes that the list keeps (hub/presence.py restricted; hub/store: last_restriction)
# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

def read_at_every_look(store, last):
    """What Presence.restricted() answered before it kept the two lists, as a model: both read at every look, and a store that does not answer leaves what it said last."""
    try:
        return frozenset(row[0] for row in store.bans()), frozenset(row[0] for row in store.mutes())
    except storemod.StoreError:
        return last


class TheBansAndTheMutesThatTheListKeeps(unittest.TestCase):
    def setUp(self):
        self.stamp = [1_800_000_000.0]
        self.w = World(store_clock=lambda: self.stamp[0])
        self.addCleanup(self.w.close)
        self.store = self.w.store
        self.other = storemod.Store(self.w.db, clock=lambda: self.stamp[0])      # (the command line: another process that opens the same file)
        self.addCleanup(self.other.close)
        self.reads, self.failing = [], False
        for name in ("bans", "mutes", "last_restriction"):
            def method(name=name, real=getattr(storemod.Store, name)):
                if self.failing:
                    raise storemod.StoreError("locked")
                if name != "last_restriction":
                    self.reads.append(name)
                return real(self.store)
            setattr(self.store, name, method)

    def look(self):
        return self.w.presence.restricted()

    def test_a_look_that_finds_nothing_changed_is_one_small_read_whatever_the_number_of_bans_and_mutes(self):
        for n in range(300):
            self.store.ban("%d" % (5000 + n), "me", "n%d" % n)
            self.store.mute("%d" % (6000 + n), 600, "me", "m%d" % n)
        self.reads.clear()
        banned, muted = self.look()
        self.assertEqual((len(banned), len(muted)), (300, 300))
        self.assertEqual(self.reads, ["bans", "mutes"], "the first look reads both lists")
        for _ in range(200):
            self.assertEqual(self.look(), (banned, muted))
        self.assertEqual(self.reads, ["bans", "mutes"], "two hundred looks that find nothing changed read nothing more")

    def test_what_makes_the_list_read_again_is_a_change_of_the_bans_or_the_mutes_and_nothing_else(self):
        self.look()
        reads = [len(self.reads)]

        def reads_again(what):
            self.look()
            reads.append(len(self.reads))
            self.assertEqual(reads[-1] - reads[-2], 2, what + ": both lists are read again, once")
            self.look()
            self.assertEqual(len(self.reads), reads[-1], what + ": and not again at the look after")

        def reads_nothing(what):
            self.look()
            self.assertEqual(len(self.reads), reads[-1], what)
        self.store.ban("1001", "me", "alice")
        reads_again("a ban made here")
        self.assertEqual(self.look()[0], {"1001"})
        self.other.mute("1002", 10, "command line", "bob")
        reads_again("a mute made by another process")
        self.assertEqual(self.look()[1], {"1002"})
        self.other.unban("1001")
        reads_again("a ban lifted by another process")
        self.store.mute("1002", 0, "me")
        reads_again("a mute lifted here")
        self.assertEqual(self.look(), (frozenset(), frozenset()))
        self.store.add_line("1001", "alice", "a line", False)
        self.store.delete_line(1, "me")
        self.store.forget("1001")
        self.store.purge()
        self.store.is_banned("1001")
        self.store.muted_until("1001")
        reads_nothing("a line, a deletion, forget, a purge and the reads of one account")

    def test_a_mute_that_runs_out_is_read_again_at_the_second_it_ends_and_the_next_one_after_it(self):
        self.store.mute("1001", 1, "me", "alice")                      # (60 seconds)
        self.store.mute("1002", 2, "me", "bob")                        # (120 seconds)
        start = self.stamp[0]
        self.reads.clear()
        self.assertEqual(self.look()[1], {"1001", "1002"})
        self.stamp[0] = start + 59.9
        self.assertEqual(self.look()[1], {"1001", "1002"})
        self.assertEqual(self.reads, ["bans", "mutes"], "nothing has run out yet: no read")
        self.stamp[0] = start + 60.0
        self.assertEqual(self.look()[1], {"1002"})
        self.assertEqual(self.reads, ["bans", "mutes"] * 2, "the first mute ends at exactly its end, and the lists are read again")
        self.stamp[0] = start + 119.5
        self.assertEqual(self.look()[1], {"1002"})
        self.assertEqual(len(self.reads), 4)
        self.stamp[0] = start + 120.0
        self.assertEqual(self.look()[1], frozenset())
        self.assertEqual(len(self.reads), 6)
        self.stamp[0] = start + 10 ** 6
        self.look()
        self.assertEqual(len(self.reads), 6, "no mute at all: no reason to read again")

    def test_a_change_that_lands_while_the_lists_are_read_is_read_at_the_next_look(self):
        """The number is read before the lists, so that a ban that another process makes between the two is not kept under the number that it did not change."""
        real = self.store.bans

        def while_reading():
            rows = real()
            self.other.ban("1004", "command line", "dave")             # (it lands after the list was read)
            return rows
        self.store.bans = while_reading
        self.assertEqual(self.look()[0], frozenset(), "this look has the list as it was read")
        self.store.bans = real
        self.assertEqual(self.look()[0], {"1004"}, "the next look reads again: the number that the list was kept under is not the number now")

    def test_the_answers_are_those_of_reading_both_lists_at_every_look_on_random_data(self):
        accounts = ["%d" % (2000 + n) for n in range(7)]
        ops = ("ban", "unban", "mute", "mute", "unmute", "other_ban", "other_unban", "other_mute", "other_unmute", "advance", "advance", "advance", "line", "forget", "purge", "fail", "fail", "recover", "look", "look")
        compared = changed = 0
        for seed in range(30):
            self.stamp[0] = 1_800_000_000.0 + seed
            self.failing = False
            rng = random.Random(seed)
            for account in accounts:                                    # (the tables are as the last seed left them: the lists are read again at the first look)
                self.other.mute(account, 0, "me")
                self.other.unban(account)
            model = read_at_every_look(self.store, None)
            self.assertEqual(self.look(), model, "each seed starts from what the store says, with nothing banned or muted")
            for step in range(120):
                op, account = rng.choice(ops), rng.choice(accounts)
                if op == "ban":
                    self.store.ban(account, "me", "n" + account)
                elif op == "unban":
                    self.store.unban(account)
                elif op == "mute":
                    self.store.mute(account, rng.choice((1, 2, 5, 30)), "me", "n" + account)
                elif op == "unmute":
                    self.store.mute(account, 0, "me")
                elif op == "other_ban":
                    self.other.ban(account, "command line", "n" + account)
                elif op == "other_unban":
                    self.other.unban(account)
                elif op == "other_mute":
                    self.other.mute(account, rng.choice((1, 2, 5, 30)), "command line", "n" + account)
                elif op == "other_unmute":
                    self.other.mute(account, 0, "command line")
                elif op == "advance":
                    self.stamp[0] += rng.choice((0.5, 1, 30, 59, 60, 61, 119, 120, 600, 4000))
                elif op == "line":
                    self.store.add_line(account, "n" + account, "line %d" % step, False)
                elif op == "forget":
                    self.store.forget(account)
                elif op == "purge":
                    self.store.purge()
                elif op == "fail":
                    self.failing = True                                 # (a store that refuses refuses all of it)
                elif op == "recover":
                    self.failing = False
                for _ in range(rng.choice((1, 1, 2))):
                    before = model
                    model = read_at_every_look(self.store, model)
                    answer = self.look()
                    compared += 1
                    changed += model != before
                    self.assertEqual(answer, model, "seed %d, step %d (%s %s), the store %s" % (seed, step, op, account, "refuses" if self.failing else "answers"))
        self.assertGreater(changed, 300, "the run changed the lists often enough to mean something (%d of %d looks)" % (changed, compared))


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

    def test_changes_that_come_together_are_one_frame_and_a_person_who_flaps_is_one_change(self):
        a = self.w.join(person(1, "alice"))
        asker = self.w.watch(self.w.join(None))
        self.w.advance(2.5)
        for n in range(100):
            self.w.here(a, "wait" if n % 2 else "look")
        self.w.join(person(2, "bob"))
        self.w.join(None)
        self.w.advance(0.5)
        frames = asker.peer.frames()
        self.assertEqual(len(frames), 2, "a change after a quiet time goes out at the next look: one frame for all of it")
        self.assertIs(frames[1]["full"], False)
        self.assertEqual(sorted(r["name"] for r in frames[1]["rows"]), ["alice", "bob"])
        self.assertEqual((frames[1]["n"], frames[1]["g"]), (4, 2))

    def test_a_change_that_comes_while_the_last_send_is_young_waits_for_it_to_be_two_seconds_old(self):
        self.w.join(person(1, "alice"))
        asker = self.w.watch(self.w.join(None))
        self.w.advance(2.5)
        self.w.join(person(2, "bob"))
        self.w.advance(0.5)
        self.assertEqual(len(asker.peer.frames()), 2)
        self.w.join(person(3, "carol"))
        self.w.advance(1.0)
        self.assertEqual(len(asker.peer.frames()), 2, "the last send is a second old")
        self.w.advance(0.9)
        self.assertEqual(len(asker.peer.frames()), 2, "and 1.9 seconds old")
        self.w.advance(0.2)
        self.assertEqual([r["name"] for r in asker.peer.frames()[2]["rows"]], ["carol"])
        self.assertEqual(len(asker.peer.frames()), 3)

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

    def test_an_ask_brings_nothing_forward_the_page_gets_the_list_as_it_was_last_sent_and_the_change_comes_with_the_next_send_to_everybody(self):
        a = self.w.watch(self.w.join(person(1, "alice")))
        self.w.join(person(2, "bob"))
        self.w.advance(0.5)
        late = self.w.watch(self.w.join(person(3, "carol")))
        self.assertEqual(len(a.peer.frames()), 1, "nobody is sent anything because a page asked")
        self.assertEqual([r["name"] for r in late.update().rows.values()], ["alice"], "the list as it was last sent")
        self.w.advance(1.4)
        self.assertEqual((len(a.peer.frames()), len(late.peer.frames())), (1, 1), "1.9 seconds after the last send")
        self.w.advance(0.2)
        for page in (a, late):
            self.assertEqual(sorted(r["name"] for r in page.update().rows.values()), ["alice", "bob", "carol"], "the same frame for both")
        self.assertEqual((len(a.peer.frames()), len(late.peer.frames())), (2, 2))

    def test_whatever_happens_a_watcher_gets_one_send_in_two_seconds_at_the_most(self):
        flapper = self.w.join(person(1, "flap"))
        watcher = self.w.watch(self.w.join(None))
        crowd = [self.w.watch(self.w.join(person(n))) for n in range(2, 6)]
        times, seen, n = [], len(watcher.peer.frames()), 100
        for step in range(400):                                  # (two minutes: a new socket that asks every 0.3 seconds, and a person who changes at every step)
            self.w.here(flapper, "wait" if step % 2 else "look")
            n += 1
            self.w.watch(self.w.join(person(n) if n % 2 else None))
            self.w.advance(0.3)
            for page in [watcher] + crowd:
                page.update()
            if len(watcher.peer.frames()) > seen:
                times.append(self.w.now)
                seen = len(watcher.peer.frames())
        self.assertGreater(len(times), 30, "it does hear the changes")
        gaps = [b - a for a, b in zip(times, times[1:])]
        self.assertGreaterEqual(min(gaps), 2.0 - 1e-6, "no two sends are closer than who_seconds")
        self.assertLessEqual(len(times), 61)
        self.assertEqual({len(p.peer.frames()) for p in crowd}, {len(watcher.peer.frames())}, "and everybody gets the same frames")

    def test_the_first_page_to_ask_when_nobody_keeps_the_list_current_gets_it_as_it_is_now(self):
        a = self.w.join(person(1, "alice"))
        first = self.w.watch(self.w.join(None))
        self.w.leave(first.peer)
        self.w.advance(3.0)
        self.w.leave(a)
        for n in range(2, 5):
            self.w.join(person(n, "p%d" % n))
        for _ in range(50):
            self.w.advance(3600.0)                               # (hours pass: nobody watches, so nobody looks)
        late = self.w.watch(self.w.join(None)).update()
        self.assertEqual((sorted(r["name"] for r in late.rows.values()), late.n, late.g), (["p2", "p3", "p4"], 4, 1), "not the list of hours ago")

    def test_the_clock_does_not_look_at_the_list_when_nobody_watches(self):
        self.w.join(person(1))
        looked = []
        view = self.w.presence.view
        self.w.presence.view = lambda: (looked.append(1), view())[1]
        for _ in range(100):
            self.w.advance(1.0)
        self.assertEqual(looked, [], "nobody asked: the list is not made")
        watcher = self.w.watch(self.w.join(None))
        self.assertEqual(len(looked), 1, "the first page to ask gets it made, once")
        self.w.advance(2.5)
        self.assertGreater(len(looked), 1, "and while somebody watches the clock looks")
        self.assertEqual(len(watcher.update().rows), 1)

    def test_a_watcher_that_closed_is_not_kept_while_nothing_changes(self):
        a = self.w.watch(self.w.join(person(1)))
        b = self.w.watch(self.w.join(person(2)))
        self.w.shut(a.peer)
        self.w.advance(1.0)
        self.assertEqual(self.w.presence.watchers, {b.peer}, "a socket that is closed is not a watcher after the next tick, whether or not the list changed")
        self.w.leave(b.peer)
        self.w.advance(1.0)
        self.assertEqual(self.w.presence.watchers, set())

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
                    elif op < 0.28:
                        peer = peers.pop(rng.randrange(len(peers)))
                        w.leave(peer)
                        pages.pop(peer, None)
                    elif op < 0.31:
                        peer = peers.pop(rng.randrange(len(peers)))      # (closed, and the hub has not let go of it)
                        w.shut(peer)
                        pages.pop(peer, None)
                    elif op < 0.55:
                        peer = rng.choice(peers)
                        if peer.identity is not None:
                            at = rng.choice(presence.AT)
                            fields = rng.choice([{}, {"pub": True, "map": "TREASURE.LVL", "n": rng.randint(1, 3), "of": 4, "code": "r%d" % rng.randint(1, 9)}, {"pub": False}])
                            w.here(peer, at, **fields)
                    elif op < 0.62:
                        account = str(1000 + rng.randint(1, 18))          # (a ban, a lift, a mute: who may chat changes while the pages watch)
                        rng.choice([lambda: w.store.ban(account, "9001"), lambda: w.store.unban(account), lambda: w.store.mute(account, 5, "9001"), lambda: w.store.mute(account, 0, "9001")])()
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
                    self.assertEqual(page.n, w.presence.people(), "seed %d: the count of the list is the chat's" % seed)
            finally:
                w.close()


# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
# the clock (hub/server.py on_tick)
# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

class TheClockAndTheList(WorldTest):
    def hub(self):
        from hub import config as configmod, log as logmod, server
        import io
        self.out = io.StringIO()
        return server.Hub(configmod.Config(db=self.w.db), self.w.store, session.NoLogin(), logmod.Log(self.out), limitmod.Limits(), clock=lambda: 100.0)

    def test_a_list_that_fails_is_logged_by_its_kind_and_never_stops_the_work_of_the_clock(self):
        hub = self.hub()
        done = []

        def broken(now):
            raise RuntimeError("alice is in this message")
        hub.chat.presence.flush = broken
        hub.chat.sync_deletions = lambda: done.append("deletions")
        hub.on_tick(100.0)
        self.assertEqual(done, ["deletions"], "the retention is done in that tick")
        self.assertGreater(hub.next_purge, 100.0)
        line = self.errors()
        self.assertEqual(len(line), 1)
        self.assertIn("kind=RuntimeError", line[0])
        self.assertIn("where=presence", line[0])
        self.assertNotIn("alice", self.out.getvalue())

    def errors(self):
        return [l for l in self.out.getvalue().splitlines() if " error " in l]

    def test_a_count_of_people_that_fails_is_logged_like_a_list_that_fails_and_the_clock_does_the_rest(self):
        hub = self.hub()
        done = []

        def broken():
            raise RuntimeError("alice is in this message")
        hub.chat.presence.people = broken
        hub.chat.presence.flush = lambda now: done.append("flush")
        hub.chat.sync_deletions = lambda: done.append("deletions")
        hub.on_tick(100.0)
        self.assertEqual(done, ["deletions"], "the retention is done in that tick, and it is the count that failed, not the retention")
        self.assertGreater(hub.next_purge, 100.0)
        line = self.errors()
        self.assertEqual(len(line), 1, "one line, and the tick did not raise")
        self.assertIn("kind=RuntimeError", line[0])
        self.assertIn("where=presence", line[0])
        self.assertNotIn("alice", self.out.getvalue())

    def test_a_failure_that_goes_on_is_a_line_now_and_then_and_not_one_for_every_tick(self):
        hub = self.hub()
        hub.chat.sync_deletions = lambda: None

        def broken(now):
            raise RuntimeError("it goes on")
        hub.chat.presence.flush = broken
        for tick in range(100, 110):
            hub.on_tick(float(tick))
        hub.on_tick(109.9)
        self.assertEqual(len(self.errors()), 1, "ten ticks and one more just before the window is over: one line")
        hub.on_tick(110.0)
        self.assertEqual(len(self.errors()), 2, "the window is over at exactly its end")
        for tick in range(111, 120):
            hub.on_tick(float(tick))
        self.assertEqual(len(self.errors()), 2)
        hub.chat.presence.flush = lambda now: (_ for _ in ()).throw(ValueError("another kind"))
        hub.on_tick(120.0)
        lines = self.errors()
        self.assertEqual(len(lines), 3)
        self.assertEqual([("kind=RuntimeError" in l, "kind=ValueError" in l) for l in lines], [(True, False), (True, False), (False, True)], "each line has the kind of the failure that it is for")

    def test_the_count_and_the_list_share_the_limit_of_the_line(self):
        hub = self.hub()
        hub.chat.sync_deletions = lambda: None

        def broken_count():
            raise RuntimeError("the count")

        def broken_flush(now):
            raise RuntimeError("the list")
        hub.chat.presence.people = broken_count
        hub.on_tick(100.0)
        hub.chat.presence.people = lambda: 0
        hub.chat.presence.flush = broken_flush
        hub.on_tick(105.0)
        self.assertEqual(len(self.errors()), 1, "one line for what went wrong in the work of the list in a while, whichever part it was")
        hub.on_tick(110.0)
        self.assertEqual(len(self.errors()), 2)


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

def frames_within(client, seconds):
    """Every text message that arrives within `seconds` of this call: a bound of time, so that a hub that never stops talking (a mutant) cannot hold a test for ever (Client.drain waits for silence)."""
    end = time.monotonic() + seconds
    out = []
    while time.monotonic() < end:
        try:
            out.append(client.recv_json(max(0.02, end - time.monotonic())))
        except (socket.timeout, BlockingIOError, EOFError):
            break
    return out


class Model:
    """What a page does with the frames that came over the wire (as Page does with those of a fake peer)."""

    def __init__(self):
        self.rows, self.n, self.g, self.more = {}, None, None, None

    def take(self, frames):
        for frame in frames:
            if frame["full"]:
                self.rows = {}
            for row in frame["rows"]:
                self.rows[row["i"]] = row
            for ident in frame.get("gone", []):
                self.rows.pop(ident, None)
            self.n, self.g, self.more = frame["n"], frame["g"], frame["more"]


def who_frames(client, timeout=0.5):
    return [m for m in frames_within(client, timeout) if m["t"] == "who"]


def send_json(client, **message):
    client.send(hc.frame(1, json.dumps(message).encode("utf-8")))


class OnTheWire(ths.RigTest):
    rig_args = dict(tick_seconds=0.05, online_seconds=0.2, who_seconds=0.2, who_resend_seconds=1.0, mods="1002", frame_burst=1000, frame_refill_seconds=0.001)

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
        self.assertIsInstance(welcome["me"], int)
        send_json(alice, t="here", at="wait", pub=True, map="TREASURE.LVL", n=2, of=4, code="c1v3rr")
        send_json(alice, t="who")
        alice_sees = Model()
        alice_sees.take(who_frames(alice))
        self.assertEqual((alice_sees.n, alice_sees.g, [(r["name"], r["at"], r.get("code")) for r in alice_sees.rows.values()]), (1, 0, [("alice", "wait", "c1v3rr")]))
        self.assertEqual(list(alice_sees.rows), [welcome["me"]], "the row that is hers is the one that the welcome named")
        guest = hc.Client(self.rig.port)
        self.clients.append(guest)
        guest.recv_json()
        send_json(guest, t="who")
        guest_sees = Model()
        guest_sees.take(who_frames(guest, 1.0))
        self.assertEqual((guest_sees.n, guest_sees.g, [r["name"] for r in guest_sees.rows.values()]), (2, 1, ["alice"]), "the list that the guest has, and what was sent after it")
        alice_sees.take(who_frames(alice, 0.5))
        self.assertEqual((alice_sees.n, alice_sees.g), (2, 1), "the member hears that a guest came")
        bob, _ = self.login("1002:bob")
        send_json(bob, t="here", at="look")
        guest_sees.take(who_frames(guest, 1.0))
        self.assertEqual((guest_sees.n, guest_sees.g, sorted((r["name"], r.get("mod")) for r in guest_sees.rows.values())), (3, 1, [("alice", None), ("bob", True)]), "bob is a moderator of this rig")
        bob.close()
        guest_sees.take(who_frames(guest, 1.0))
        self.assertEqual((guest_sees.n, sorted(r["name"] for r in guest_sees.rows.values())), (2, ["alice"]), "a person who goes is out of the list")

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
        self.assertEqual(alice.recv_json(), {"t": "err", "code": "here"}, "a refused here has a code of its own: the chat's `bad` is for its lines")

    def test_the_whole_list_is_sent_again_only_after_the_wait(self):
        alice, _ = self.login("1001:alice")
        send_json(alice, t="who")
        send_json(alice, t="who")
        self.assertEqual(len(who_frames(alice, 0.3)), 1)
        time.sleep(1.0)
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
        proc, folder, log_path, env = ths.start_process({"ANTS_HUB_TEST_LOGIN": "1", "ANTS_HUB_OWNER": "5551234567"})
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
            self.assertEqual(frames_within(alice, 0.5)[-1], {"t": "err", "code": "here"})
            time.sleep(0.2)
            proc.send_signal(signal.SIGTERM)
            proc.wait(10)
            text = ths.read_text(log_path)
            self.assertIn(" open ", text)
            self.assertIn("sockets=", text, "the log counts sockets, and says so")
            self.assertNotIn("online=", text)
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
