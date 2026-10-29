#!/usr/bin/env python3
"""The hub on real sockets (hub/server.py; docs/HUB.md): the handshake and every refusal, the limits of the sockets, the chat between clients, the timers, the broken streams (fuzzed on
the wire), and the real process (`python -m hub`) with its log read for secrets.

Most classes run the hub in a thread of this process (tests/scripts/hub_rig.py) with the timers shortened; the last one starts it as the image does and talks to it with the command line.
The client is tests/scripts/hub_client.py, which shares no code with the hub. Reproducible (fixed seeds); ANTS_HUB_FUZZ_SCALE=10 runs ten times more.
"""
import asyncio
import io
import json
import os
import random
import re
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
import hub_client as hc  # noqa: E402
import hub_rig  # noqa: E402
import test_hub_ws  # noqa: E402  (its random streams are the fuzz here too)
from hub import config as configmod, limits as limitmod, log as logmod, server, session as sessionmod, store as storemod, ws  # noqa: E402

SCALE = int(os.environ.get("ANTS_HUB_FUZZ_SCALE", "1"))
FAST = dict(tick_seconds=0.05, deletions_seconds=0.2, online_seconds=0.2)
LOOSE = dict(burst=1000, refill_seconds=0.001, frame_burst=1000, frame_refill_seconds=0.001, global_burst=1000, global_refill_seconds=0.001)       # (limits out of the way, for tests of something else)


def status_of(port, **kw):
    """The HTTP status code (as text) that the hub answers to a handshake."""
    client = hc.Client(port, **kw)
    client.close()
    return client.status.split()[1]


def welcome_history(test):
    """The history that a new visitor is given."""
    client = hc.Client(test.rig.port)
    test.clients.append(client)
    return client.recv_json()["history"]


def welcome(client):
    message = client.recv_json()
    assert message["t"] == "welcome", message
    return message


def member(rig, login="1001:alice"):
    client = hc.Client(rig.port, login=login)
    assert client.ok, client.status
    welcome(client)
    return client


class RigTest(unittest.TestCase):
    rig_args = {}

    def setUp(self):
        self.rig = hub_rig.Rig(**self.rig_args)
        self.rig.__enter__()
        self.clients = []
        self.addCleanup(self.finish)

    def finish(self):
        for c in self.clients:
            c.close()
        self.rig.__exit__(None, None, None)

    def connect(self, login=None, **kw):
        client = hc.Client(self.rig.port, login=login, **kw)
        self.clients.append(client)
        if client.ok:
            self.assertEqual(welcome(client)["t"], "welcome")
        return client

    def settle(self, what, seconds=3.0):
        """Wait until what() is true (the hub finishes its work for a closed socket a moment after the client sees the end)."""
        end = time.monotonic() + seconds
        while not what() and time.monotonic() < end:
            time.sleep(0.02)
        return what()

    def log_events(self):
        return [line.split(" ")[1] for line in self.rig.log_text.getvalue().splitlines() if " " in line]


class TheHandshake(RigTest):
    def test_a_good_handshake_gets_the_welcome(self):
        client = hc.Client(self.rig.port, login="1001:alice")
        self.clients.append(client)
        self.assertEqual(client.status, "HTTP/1.1 101 Switching Protocols")
        self.assertEqual(client.reply_headers["upgrade"], "websocket")
        self.assertEqual(client.reply_headers["connection"].lower(), "upgrade")
        self.assertNotIn("sec-websocket-extensions", client.reply_headers)
        self.assertNotIn("sec-websocket-protocol", client.reply_headers)
        message = client.recv_json()
        self.assertEqual((message["t"], message["v"], message["login"], message["you"]), ("welcome", 1, True, {"name": "alice", "mod": False}))

    def test_the_origin_must_be_the_site_exactly(self):
        for origin in (None, "null", "", "https://evil.example", "http://chat.example.org", "https://chat.example.org:8443", "https://chat.example.org.evil.example", "https://evil.example/https://chat.example.org",
                       "https://chat.example.org/", "HTTPS://CHAT.EXAMPLE.ORG", "https://sub.chat.example.org"):
            client = hc.Client(self.rig.port, origin=origin)
            self.clients.append(client)
            self.assertEqual(client.status.split()[1], "403", repr(origin))
            self.assertFalse(client.ok)
        self.assertEqual(status_of(self.rig.port), "101")

    def test_a_hub_that_was_told_no_site_lets_nobody_in(self):
        with hub_rig.Rig(origin="") as rig:
            for origin in (hc.SITE, "null", "", None):
                self.assertEqual(status_of(rig.port, origin=origin), "403", origin)

    def test_the_refusals_are_json_that_closes_the_connection(self):
        client = hc.Client(self.rig.port, origin="https://evil.example")
        self.clients.append(client)
        self.assertEqual(client.reply_headers["content-type"], "application/json")
        self.assertEqual(client.reply_headers["connection"], "close")
        self.assertEqual(client.reply_headers["cache-control"], "no-store")
        self.assertEqual(client.reply_headers["x-content-type-options"], "nosniff")
        length = int(client.reply_headers["content-length"])
        self.assertEqual(json.loads(client.recv_exact(length, 2)), {"error": "origin"})
        with self.assertRaises(EOFError):
            client.recv_exact(1, 2)                                      # and the connection is closed

    def test_other_paths_and_methods(self):
        self.assertEqual(hc.http_get(self.rig.port, "/hub/healthz"), (200, b'{"ok":true}'))
        for path in ("/", "/hub", "/hub/", "/hub/ws/", "/hub/ws?x=1", "/hub/healthz?x", "/hub/healthz/", "/HUB/WS", "/hub/ws%20", "/hub/login", "/hub/me", "/%2e%2e/hub/ws", "/hub/../hub/ws"):
            status, body = hc.http_get(self.rig.port, path)
            self.assertEqual((status, json.loads(body)), (404, {"error": "not_found"}), path)
        request, _ = hc.handshake_request(method="POST")
        with socket.create_connection(("127.0.0.1", self.rig.port), timeout=3) as s:
            s.sendall(request)
            self.assertEqual(hc.read_head(s)[0].split()[1], "405")
        with socket.create_connection(("127.0.0.1", self.rig.port), timeout=3) as s:
            s.sendall(b"HEAD /hub/healthz HTTP/1.1\r\nHost: x\r\n\r\n")
            self.assertEqual(hc.read_head(s)[0].split()[1], "404")

    def test_the_headers_of_the_upgrade_are_checked(self):
        self.assertEqual(status_of(self.rig.port, drop=["Upgrade"]), "426")
        self.assertEqual(status_of(self.rig.port, drop=["Connection"]), "426")
        self.assertEqual(status_of(self.rig.port, drop=["Sec-WebSocket-Key"]), "400")
        c = hc.Client(self.rig.port, drop=["Sec-WebSocket-Version"])
        self.clients.append(c)
        self.assertEqual((c.status.split()[1], c.reply_headers.get("sec-websocket-version")), ("426", "13"))
        self.assertEqual(status_of(self.rig.port, key="not-a-key"), "400")
        self.assertEqual(status_of(self.rig.port, key="A" * 24), "400")
        self.assertEqual(status_of(self.rig.port, headers={"Connection": "keep-alive, Upgrade"}, drop=["Connection"]), "101")
        self.assertEqual(status_of(self.rig.port, headers={"Content-Length": "5"}), "400")
        self.assertEqual(status_of(self.rig.port, headers={"Transfer-Encoding": "chunked"}), "400")
        self.assertEqual(status_of(self.rig.port, headers={"Origin": hc.SITE}), "400")      # a second Origin
        self.assertEqual(status_of(self.rig.port, headers={"X-Client-Address": "1.1.1.1"}, login=None), "101")

    def test_the_version_must_be_13(self):
        for version in ("12", "14", "", "13 ", "013", "13, 8"):
            request, _ = hc.handshake_request()
            request = request.replace(b"Sec-WebSocket-Version: 13", b"Sec-WebSocket-Version: " + version.encode())
            with socket.create_connection(("127.0.0.1", self.rig.port), timeout=3) as s:
                s.sendall(request)
                head = hc.read_head(s)
                self.assertEqual(head[0].split()[1], "426" if version.strip() != "13" else "101", version)
                if version.strip() != "13":
                    self.assertEqual(head[1].get("sec-websocket-version"), "13")

    def test_extensions_and_subprotocols_are_not_negotiated(self):
        client = hc.Client(self.rig.port, headers={"Sec-WebSocket-Extensions": "permessage-deflate; client_max_window_bits", "Sec-WebSocket-Protocol": "chat, superchat"})
        self.clients.append(client)
        self.assertTrue(client.ok)
        self.assertNotIn("sec-websocket-extensions", client.reply_headers)
        self.assertNotIn("sec-websocket-protocol", client.reply_headers)

    def test_frames_sent_with_the_handshake_are_not_lost(self):
        client = hc.Client(self.rig.port, login="1001:alice", trailer=hc.say("early bird"))
        self.clients.append(client)
        self.assertEqual(client.recv_json()["t"], "welcome")
        self.assertEqual(client.recv_json()["text"], "early bird")

    def test_a_head_that_never_ends_and_a_client_that_never_speaks(self):
        with socket.create_connection(("127.0.0.1", self.rig.port), timeout=3) as s:
            s.sendall(b"GET /hub/ws HTTP/1.1\r\n" + b"X: " + b"a" * 9000)
            self.assertEqual(hc.read_head(s)[0].split()[1], "431")
        self.assertTrue(self.settle(lambda: self.rig.hub.pending == 0), self.rig.hub.pending)
        self.assertNotIn("error", self.log_events())


class TheSlowHandshake(RigTest):
    rig_args = dict(handshake_seconds=0.3)

    def test_silence_before_the_handshake_is_answered_408_and_closed(self):
        with socket.create_connection(("127.0.0.1", self.rig.port), timeout=3) as s:
            started = time.monotonic()
            self.assertEqual(hc.read_head(s)[0].split()[1], "408")
            self.assertLess(time.monotonic() - started, 2.0)
        self.assertTrue(self.settle(lambda: self.rig.hub.pending == 0), self.rig.hub.pending)


class ThePendingCap(RigTest):
    rig_args = dict(pending_total=2, handshake_seconds=1.0)

    def test_connections_that_have_not_spoken_are_limited(self):
        idle = [socket.create_connection(("127.0.0.1", self.rig.port), timeout=3) for _ in range(2)]
        self.addCleanup(lambda: [s.close() for s in idle])
        time.sleep(0.2)
        third = socket.create_connection(("127.0.0.1", self.rig.port), timeout=3)
        self.addCleanup(third.close)
        third.settimeout(2)
        self.assertEqual(third.recv(100), b"")                         # closed without an answer
        time.sleep(1.3)                                                  # the two idle ones time out, and the door is open again
        self.assertEqual(status_of(self.rig.port), "101")


class TheSocketLimits(RigTest):
    rig_args = dict(sockets_per_address=3, sockets_total=5)

    def test_a_fourth_socket_of_one_address_is_429_and_a_closed_one_frees_its_place(self):
        mine = [self.connect(headers={"X-Client-Address": "203.0.113.9"}) for _ in range(3)]
        refused = hc.Client(self.rig.port, headers={"X-Client-Address": "203.0.113.9"})
        self.clients.append(refused)
        self.assertEqual(refused.status.split()[1], "429")
        self.assertEqual(refused.reply_headers["retry-after"], "10")
        self.assertTrue(self.connect(headers={"X-Client-Address": "203.0.113.10"}).ok)       # another address is fine
        mine[0].close()
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            again = hc.Client(self.rig.port, headers={"X-Client-Address": "203.0.113.9"})
            self.clients.append(again)
            if again.ok:
                break
            time.sleep(0.05)
        self.assertTrue(again.ok)

    def test_the_address_is_the_one_nginx_names_and_an_ipv6_address_counts_as_its_64(self):
        for n in range(3):
            self.connect(headers={"X-Client-Address": "2001:db8:1:2::%x" % (n + 1)})
        refused = hc.Client(self.rig.port, headers={"X-Client-Address": "2001:db8:1:2:aaaa::9"})
        self.clients.append(refused)
        self.assertEqual(refused.status.split()[1], "429")

    def test_a_garbage_address_falls_back_to_the_peer(self):
        for _ in range(3):
            self.connect(headers={"X-Client-Address": "not an address"})
        refused = hc.Client(self.rig.port)                                 # (no header: the peer is 127.0.0.1 too)
        self.clients.append(refused)
        self.assertEqual(refused.status.split()[1], "429")

    def test_the_sixth_socket_is_503(self):
        for n in range(5):
            self.connect(headers={"X-Client-Address": "198.51.100.%d" % n})
        refused = hc.Client(self.rig.port, headers={"X-Client-Address": "198.51.100.77"})
        self.clients.append(refused)
        self.assertEqual(refused.status.split()[1], "503")
        self.assertEqual(refused.reply_headers["retry-after"], "30")

    def test_places_are_given_back_however_the_socket_ends(self):
        for n in range(12):
            c = hc.Client(self.rig.port, headers={"X-Client-Address": "203.0.113.50"})
            self.assertTrue(c.ok, n)
            welcome(c)
            if n % 3 == 0:
                c.send(hc.frame(8, b"\x03\xe8"))
            elif n % 3 == 1:
                c.send(hc.frame(1, b"{}", masked=False))                  # a broken frame
            c.close()
            time.sleep(0.05)
        self.assertTrue(self.settle(lambda: self.rig.hub.per_address.counts == {} and len(self.rig.hub.chat.peers) == 0), self.rig.hub.per_address.counts)


class TheChatOnTheWire(RigTest):
    def test_a_line_reaches_everybody_and_the_next_visitor_reads_it(self):
        alice, bob, reader = self.connect("1001:alice"), self.connect("1002:bob"), self.connect()
        alice.say("hello world")
        for c in (alice, bob, reader):
            line = next(m for m in c.drain(0.4) if m["t"] == "line")
            self.assertEqual((line["who"], line["text"], line["mod"]), ("alice", "hello world", False))
        late = hc.Client(self.rig.port)
        self.clients.append(late)
        history = late.recv_json()["history"]
        self.assertEqual([h["text"] for h in history], ["hello world"])

    def test_a_reader_cannot_write(self):
        reader = self.connect()
        reader.say("hi")
        self.assertEqual(reader.recv_json(), {"t": "err", "code": "login"})

    def test_the_text_rules_hold_on_the_wire(self):
        alice, reader = self.connect("1001:alice"), self.connect()
        for text, code in (("<b>", "bad"), ("", "empty"), ("\u202e", "empty"), ("x" * 201, "long")):
            alice.say(text)
            self.assertEqual(alice.recv_json(), {"t": "err", "code": code}, repr(text))
        alice.say("ad\u202emin\u200b  now")
        self.assertEqual(next(m for m in reader.drain(0.4) if m["t"] == "line")["text"], "admin now")

    def test_the_limit_is_the_account_s_across_sockets(self):
        a1, a2 = self.connect("1001:alice"), self.connect("1001:alice")
        got = []
        for n, c in enumerate((a1, a2, a1, a2)):
            c.say("line %d" % n)
        for c in (a1, a2):
            got += [m for m in c.drain(0.5) if m["t"] == "err"]
        self.assertEqual([m["code"] for m in got], ["rate"])

    def test_a_ping_is_answered_with_the_count(self):
        c = self.connect()
        c.send(hc.frame(1, b'{"t":"ping"}'))
        self.assertEqual(c.recv_json()["t"], "online")

    def test_a_control_ping_is_answered_with_a_pong_of_the_same_bytes(self):
        c = self.connect()
        c.send(hc.frame(9, b"are you there"))
        self.assertEqual(c.recv_frame(), (10, b"are you there"))

    def test_the_close_handshake(self):
        c = self.connect()
        c.send(hc.frame(8, struct.pack("!H", 1001) + b"leaving"))
        self.assertEqual(c.expect_close(), 1001)
        with self.assertRaises(EOFError):
            c.recv_frame(2)

    def test_a_broken_frame_closes_with_its_code_and_a_good_frame_before_it_is_kept(self):
        alice, reader = self.connect("1001:alice"), self.connect()
        alice.send(hc.say("last words") + hc.frame(2, b"binary"))
        self.assertEqual(alice.expect_close(), 1003)
        self.assertEqual(next(m for m in reader.drain(0.4) if m["t"] == "line")["text"], "last words")

    def test_each_kind_of_broken_frame(self):
        cases = ((hc.frame(1, b"{}", masked=False), 1002), (hc.frame(1, b"{}", rsv=1), 1002), (hc.frame(3, b""), 1002), (hc.frame(2, b"x"), 1003), (hc.frame(1, b"{", fin=False), 1003),
                 (hc.frame(0, b"}"), 1003), (hc.frame(1, b"\xff\xfe"), 1007), (hc.frame(1, b"a" * 2049), 1009), (hc.frame(9, b"x" * 126), 1002), (hc.frame(8, b"\x03"), 1002),
                 (hc.frame(1, b"{}", length_form=16), 1002), (struct.pack("!BBQ", 0x81, 0xFF, 1 << 62), 1009))
        for data, code in cases:
            c = self.connect()
            c.send(data)
            self.assertEqual(c.expect_close(), code, data[:12])
        self.assertNotIn("error", self.log_events())

    def test_a_frame_over_2_kb_is_refused_before_its_payload_comes(self):
        c = self.connect()
        c.send(struct.pack("!BBH", 0x81, 0x80 | 126, 2049) + b"\x00\x00\x00\x00")       # the header and the mask, not a byte of the payload
        self.assertEqual(c.expect_close(), 1009)


class TheViolations(RigTest):
    rig_args = dict(frame_burst=1000, frame_refill_seconds=0.001)

    def test_the_twentieth_mistake_in_a_minute_closes_with_1008(self):
        c = self.connect()
        for _ in range(19):
            c.send(hc.frame(1, b"not json"))
        replies = [c.recv_json() for _ in range(19)]
        self.assertEqual({r["code"] for r in replies}, {"bad"})
        c.send(hc.frame(1, b'{"t":"ping"}'))
        self.assertEqual(c.recv_json()["t"], "online")                   # still open after 19
        c.send(hc.frame(1, b"not json"))
        self.assertEqual(c.recv_json(), {"t": "err", "code": "bad"})
        self.assertEqual(c.expect_close(), 1008)

    def test_errors_of_the_rules_count_too(self):
        c = self.connect()
        for _ in range(20):
            c.say("hi")                                                    # a reader: login
        self.assertEqual(c.expect_close(), 1008)


class TheFrameBudget(RigTest):
    def test_a_flood_of_frames_of_any_kind_closes_with_1008(self):
        c = self.connect()
        c.send(b"".join(hc.frame(9, b"") for _ in range(60)))
        self.assertEqual(c.expect_close(), 1008)

    def test_ten_frames_at_once_are_fine(self):
        c = self.connect()
        c.send(b"".join(hc.frame(1, b'{"t":"ping"}') for _ in range(10)))
        self.assertEqual([c.recv_json()["t"] for _ in range(10)], ["online"] * 10)


class TheTimers(RigTest):
    rig_args = dict(tick_seconds=0.05, ping_seconds=0.3, silence_seconds=1.0)

    def test_the_hub_pings_and_a_client_that_answers_stays(self):
        c = self.connect()
        end = time.monotonic() + 2.5
        pings = 0
        while time.monotonic() < end:
            try:
                opcode, payload = c.recv_frame(0.5)
            except socket.timeout:
                continue
            if opcode == 9:
                pings += 1
                c.send(hc.frame(10, payload))
        self.assertGreaterEqual(pings, 4)
        c.say("still here")
        self.assertEqual(c.recv_json()["code"], "login")                 # open and answering

    def test_a_client_that_says_nothing_is_dropped_with_1001_after_the_silence(self):
        c = self.connect()
        started = time.monotonic()
        self.assertEqual(c.expect_close(3), 1001)
        self.assertGreaterEqual(time.monotonic() - started, 0.9)
        self.assertLess(time.monotonic() - started, 2.5)

    def test_the_json_ping_keeps_a_socket_too(self):
        c = self.connect()
        end = time.monotonic() + 2.2
        while time.monotonic() < end:
            c.send(hc.frame(1, b'{"t":"ping"}'))
            time.sleep(0.4)
            c.drain(0.01)
        c.send(hc.frame(1, b'{"t":"ping"}'))
        self.assertEqual(c.recv_json()["t"], "online")


class TheOnlineCount(RigTest):
    rig_args = dict(tick_seconds=0.05, online_seconds=0.2)

    def test_everybody_hears_when_it_changes(self):
        first = self.connect()
        first.drain(0.4)
        second = self.connect()
        self.assertEqual([m["n"] for m in first.drain(0.5) if m["t"] == "online"][-1], 2)
        second.close()
        self.assertEqual([m["n"] for m in first.drain(0.8) if m["t"] == "online"][-1], 1)

    def test_it_is_not_sent_when_it_did_not_change(self):
        first = self.connect()
        first.drain(0.5)
        self.assertEqual(first.drain(0.5), [])

    def test_the_welcome_counts_the_new_socket(self):
        self.connect()
        c = hc.Client(self.rig.port)
        self.clients.append(c)
        self.assertEqual(c.recv_json()["online"], 2)


class TheHistoryOnTheWire(RigTest):
    rig_args = dict(LOOSE)

    def test_sixty_lines_and_the_next_visitor_gets_the_last_fifty(self):
        alice = self.connect("1001:alice")
        for n in range(60):
            alice.say("line %d" % n)
        got = [m for m in alice.drain(0.6) if m["t"] == "line"]
        self.assertEqual(len(got), 60)
        late = hc.Client(self.rig.port)
        self.clients.append(late)
        history = late.recv_json()["history"]
        self.assertEqual([h["text"] for h in history], ["line %d" % n for n in range(10, 60)])


class TheModeratorsOnTheWire(RigTest):
    rig_args = dict(LOOSE, mods="9001", **FAST)

    def test_mute_and_del_in_chat(self):
        mod, alice, reader = self.connect("9001:moddy"), self.connect("1001:alice"), self.connect()
        alice.say("an ugly line")
        line = next(m for m in reader.drain(0.4) if m["t"] == "line")
        mod.say("/del %d" % line["id"])
        self.assertIn({"t": "del", "id": line["id"]}, reader.drain(0.5))
        mod.say("/mute alice 5")
        time.sleep(0.2)
        alice.drain(0.2)
        alice.say("more")
        self.assertEqual(alice.recv_json(), {"t": "err", "code": "muted"})
        self.assertEqual([m["text"] for m in welcome_history(self)], [])

    def test_a_deletion_by_the_command_line_reaches_the_screens(self):
        alice, reader = self.connect("1001:alice"), self.connect()
        alice.say("to be deleted")
        line = next(m for m in reader.drain(0.4) if m["t"] == "line")
        from hub import admin
        status = admin.run(["--db", self.rig.db, "del", str(line["id"])], io.StringIO(), io.StringIO())
        self.assertEqual(status, 0)
        self.assertIn({"t": "del", "id": line["id"]}, reader.drain(1.5))
        self.assertEqual(welcome_history(self), [])

    def test_a_ban_by_the_command_line_counts_from_the_next_line(self):
        alice = self.connect("1001:alice")
        from hub import admin
        self.assertEqual(admin.run(["--db", self.rig.db, "ban", "1001"], io.StringIO(), io.StringIO()), 0)
        alice.say("hello")
        self.assertEqual(alice.recv_json(), {"t": "err", "code": "banned"})
        reborn = self.connect("1001:alice.again")
        reborn.say("hello")
        self.assertEqual(reborn.recv_json(), {"t": "err", "code": "banned"})


class TheYoungAndTheOthers(RigTest):
    def test_a_young_account_reads_and_cannot_write(self):
        c = self.connect("1003:newbie:young")
        c.say("hi")
        self.assertEqual(c.recv_json(), {"t": "err", "code": "young"})


class TheDefaultHasNoLogin(RigTest):
    rig_args = dict(test_login=False)

    def test_login_is_false_and_the_test_header_does_nothing(self):
        c = hc.Client(self.rig.port, login="1001:alice")
        self.clients.append(c)
        message = c.recv_json()
        self.assertEqual((message["login"], message["you"]), (False, None))
        c.say("hello")
        self.assertEqual(c.recv_json(), {"t": "err", "code": "login"})
        self.assertEqual(self.rig.call(self.rig.store.recent_lines, 10), [])


class TheBadIdentity(RigTest):
    def test_a_login_layer_that_hands_out_rubbish_is_not_believed(self):
        from hub import session

        class Rubbish:
            login = True

            def __init__(self, who):
                self.who = who

            async def identify(self, headers):
                if self.who == "raise":
                    raise RuntimeError("boom with a secret")
                return self.who

        for who in (session.Identity("12ab", "alice", False), session.Identity("1001", "Alice Smith", False), session.Identity("1001", "<b>", False), "raise", session.Identity("", "alice", False)):
            self.rig.hub.sessions = Rubbish(who)
            c = hc.Client(self.rig.port)
            self.clients.append(c)
            self.assertIsNone(c.recv_json()["you"], repr(who))
        self.assertNotIn("secret", self.rig.log_text.getvalue())
        self.assertIn(" error kind=RuntimeError", self.rig.log_text.getvalue())
        self.rig.hub.sessions = Rubbish(session.Identity("1001", "alice", False))
        c = hc.Client(self.rig.port)
        self.clients.append(c)
        self.assertEqual(c.recv_json()["you"], {"name": "alice", "mod": False})


class TheLoginLayerIsAskedAndMayBeSlow(RigTest):
    rig_args = dict(identify_seconds=0.3)

    def test_a_login_that_is_late_is_a_reader_and_the_hub_goes_on(self):
        class Slow:
            login = True

            async def identify(self, headers):
                await asyncio.sleep(2.0)
                return sessionmod.Identity("1001", "alice", False)

        self.rig.hub.sessions = Slow()
        started = time.monotonic()
        c = hc.Client(self.rig.port)
        self.clients.append(c)
        self.assertLess(time.monotonic() - started, 1.5)
        self.assertIsNone(c.recv_json()["you"])
        self.assertIn(" error kind=TimeoutError where=session", self.rig.log_text.getvalue())
        self.rig.hub.sessions = sessionmod.TestLogin()
        self.assertEqual(self.connect("1002:bob").status, "HTTP/1.1 101 Switching Protocols")

    def test_the_hub_serves_others_while_one_login_is_being_asked(self):
        gate = []

        class Waiting:
            login = True

            async def identify(self, headers):
                if headers.get("x-hub-test-login") == "1001:slow":
                    await asyncio.sleep(0.25)
                return None

        self.rig.hub.sessions = Waiting()
        slow = threading.Thread(target=lambda: gate.append(hc.Client(self.rig.port, login="1001:slow")))
        slow.start()
        time.sleep(0.05)
        started = time.monotonic()
        quick = hc.Client(self.rig.port)                               # a handshake that arrives meanwhile is not held up behind it
        self.clients.append(quick)
        self.assertLess(time.monotonic() - started, 0.2)
        slow.join()
        self.clients.extend(gate)

    def test_a_place_is_given_back_when_the_handshake_fails_after_the_address_was_counted(self):
        def broken_welcome(peer):
            raise RuntimeError("the welcome could not be made")

        self.rig.hub.chat.welcome = broken_welcome
        for _ in range(3):
            c = hc.Client(self.rig.port)
            self.clients.append(c)
        self.assertTrue(self.settle(lambda: self.rig.hub.per_address.counts == {} and len(self.rig.hub.chat.peers) == 0, 3), (self.rig.hub.per_address.counts, len(self.rig.hub.chat.peers)))
        self.assertTrue(self.settle(lambda: self.rig.hub.pending == 0))
        self.assertIn(" error kind=RuntimeError", self.rig.log_text.getvalue())


class TheLoginRace(RigTest):
    rig_args = dict(sockets_total=1, identify_seconds=2.0)

    def test_the_last_place_is_given_once_when_two_handshakes_wait_for_the_login_layer(self):
        class Waiting:
            login = True

            async def identify(self, headers):
                await asyncio.sleep(0.3)
                return None

        self.rig.hub.sessions = Waiting()
        results = []
        threads = [threading.Thread(target=lambda: results.append(hc.Client(self.rig.port, headers={"X-Client-Address": "203.0.113.%d" % (n + 1)}))) for n in range(2)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.clients.extend(results)
        self.assertEqual(sorted(c.status.split()[1] for c in results), ["101", "503"])
        self.assertEqual(len(self.rig.hub.chat.peers), 1)


class TheHubTidiesTheDatabase(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.store = storemod.Store(os.path.join(self.folder.name, "hub.db"))
        self.addCleanup(self.store.close)
        self.hub = server.Hub(configmod.load({"ANTS_HUB_PUBLIC_URL": hc.SITE}), self.store, sessionmod.NoLogin(), logmod.Log(io.StringIO()),
                              limitmod.Limits(purge_chunk=10, purge_seconds=3600.0, tick_seconds=1.0, checkpoint_retry_seconds=10.0), clock=lambda: 0.0)
        self.purges, self.checkpoints = [], []
        self.hub.store.purge = lambda chunk: self.purges.append(chunk) or self.purged
        self.hub.store.checkpoint = lambda: self.checkpoints.append(1) or True
        self.purged = 0
        self.hub.next_purge = 0.0                                     # the first purge has been done

    def test_a_full_chunk_is_followed_by_another_at_the_next_tick(self):
        self.hub.next_purge = float("-inf")
        self.purged = 10
        self.hub.on_tick(100.0)
        self.assertEqual(self.purges, [10])
        self.assertEqual(self.hub.next_purge, 101.0)
        self.purged = 3
        self.hub.on_tick(101.0)
        self.assertEqual(self.hub.next_purge, 101.0 + 3600.0)

    def test_a_log_file_that_a_reader_held_is_cut_again_every_ten_seconds(self):
        self.hub.next_purge = 10 ** 9
        self.hub.next_checkpoint = float("-inf")
        self.store.unsettled = False
        self.hub.on_tick(100.0)
        self.assertEqual(self.checkpoints, [])                          # nothing to cut again
        self.store.unsettled = True
        self.hub.on_tick(101.0)
        self.assertEqual(self.checkpoints, [1])
        self.hub.on_tick(105.0)
        self.assertEqual(self.checkpoints, [1])                         # not before ten seconds have passed
        self.hub.on_tick(111.0)
        self.assertEqual(self.checkpoints, [1, 1])


class FakeTransport:
    def __init__(self, size):
        self.size = size
        self.aborted = False

    def get_write_buffer_size(self):
        return self.size

    def abort(self):
        self.aborted = True

    def is_closing(self):
        return self.aborted


class FakeWriter:
    def __init__(self, size):
        self.transport = FakeTransport(size)
        self.written = []

    def write(self, data):
        self.written.append(data)

    def is_closing(self):
        return self.transport.is_closing()

    def close(self):
        pass


class TheSlowReader(unittest.TestCase):
    def peer(self, size):
        class Hub:
            limits = limitmod.Limits(write_buffer_max=1000)
            clock = staticmethod(lambda: 0.0)

            class log:
                events = []

                @staticmethod
                def event(*a, **k):
                    Hub.log.events.append(a)
        writer = FakeWriter(size)
        return server.Peer(Hub, writer, "k", "tag", None, 0.0), writer, Hub

    def test_a_socket_that_cannot_keep_up_is_dropped(self):
        peer, writer, hub = self.peer(1001)
        peer.send_text(b"{}")
        self.assertTrue(writer.transport.aborted)
        self.assertEqual(writer.written, [])
        self.assertTrue(peer.closed)
        peer.send_text(b"{}")
        self.assertEqual(writer.written, [])

    def test_a_socket_with_room_gets_its_frame(self):
        peer, writer, _ = self.peer(1000)
        peer.send_text(b"{}")
        self.assertFalse(writer.transport.aborted)
        self.assertEqual(writer.written, [b"\x81\x02{}"])


class TheClosedSocketsAreCut(unittest.TestCase):
    """A socket that was closed with output the other side never takes would stay for ever: the hub cuts it after a while."""

    def hub(self, clock):
        cfg = configmod.load({"ANTS_HUB_PUBLIC_URL": hc.SITE})
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        store = storemod.Store(os.path.join(self.folder.name, "hub.db"))
        self.addCleanup(store.close)
        return server.Hub(cfg, store, sessionmod.NoLogin(), logmod.Log(io.StringIO()), limitmod.Limits(close_grace_seconds=5.0), clock=clock)

    def test_the_socket_is_aborted_five_seconds_after_the_close(self):
        now = [100.0]
        hub = self.hub(lambda: now[0])
        writer = FakeWriter(50000)
        peer = server.Peer(hub, writer, "k", "tag", None, now[0])
        hub.chat.peers.add(peer)
        peer.close(1001, "going")
        self.assertTrue(peer.closed)
        hub.on_tick(now[0] + 4.9)
        self.assertFalse(writer.transport.aborted)
        hub.on_tick(now[0] + 5.0)
        self.assertTrue(writer.transport.aborted)

    def test_an_open_socket_is_not_cut(self):
        now = [100.0]
        hub = self.hub(lambda: now[0])
        writer = FakeWriter(0)
        peer = server.Peer(hub, writer, "k", "tag", None, now[0])
        hub.chat.peers.add(peer)
        hub.on_tick(now[0] + 30.0)
        self.assertFalse(writer.transport.aborted)
        self.assertFalse(peer.closed)


class TheStuckReader(RigTest):
    rig_args = dict(LOOSE, burst=10 ** 6, global_burst=10 ** 6, frame_burst=10 ** 6, write_buffer_max=10 ** 9, close_grace_seconds=0.5, tick_seconds=0.05, online_seconds=1000.0)

    def test_a_reader_that_never_reads_does_not_keep_its_place_after_it_was_closed(self):
        stuck = socket.socket()
        stuck.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
        stuck.connect(("127.0.0.1", self.rig.port))
        self.addCleanup(stuck.close)
        request, _ = hc.handshake_request(headers={"X-Client-Address": "198.51.100.77"})
        stuck.sendall(request)                                         # and never another call to recv
        speaker = self.connect("1001:alice")
        self.assertTrue(self.settle(lambda: len(self.rig.hub.chat.peers) == 2))
        text = "x" * 190
        sent = 0
        while sent < 60000:                                            # the lines pile up in front of the stuck socket until the kernel's buffers are full and the hub's own begins to fill
            for _ in range(100):
                speaker.say("%d %s" % (sent, text))
                sent += 1
            speaker.drain(0.0)
            peers = self.rig.call(lambda: [p for p in self.rig.hub.chat.peers if p.identity is None])
            if peers and self.rig.call(lambda: peers[0].writer.transport.get_write_buffer_size()) > 0:
                break
        else:
            self.skipTest("this machine's sockets took 60,000 lines without filling")
        victim = peers[0]
        self.rig.call(victim.close, 1001, "test")
        self.assertTrue(self.settle(lambda: victim not in self.rig.hub.chat.peers, 5), "the closed socket is still among the peers")
        self.assertTrue(self.settle(lambda: "198.51.100.77" not in self.rig.hub.per_address.counts, 5))


class TheCrowdIsLogged(RigTest):
    rig_args = dict(crowded_sockets=2, crowded_log_seconds=0.2, tick_seconds=0.05)

    def test_many_sockets_of_one_address_group_make_a_line_without_the_address(self):
        for n in range(4):
            self.connect(headers={"X-Client-Address": "203.0.113.9"})
        self.connect(headers={"X-Client-Address": "203.0.113.50"})
        self.assertTrue(self.settle(lambda: " crowded sockets=4 groups=2" in self.rig.log_text.getvalue()), self.rig.log_text.getvalue())
        self.assertNotIn("203.0.113", self.rig.log_text.getvalue())

    def test_a_few_sockets_make_no_line(self):
        self.connect(headers={"X-Client-Address": "203.0.113.9"})
        self.connect(headers={"X-Client-Address": "203.0.113.9"})
        time.sleep(0.6)
        self.assertNotIn(" crowded ", self.rig.log_text.getvalue())


class TheBacklogIsPurgedInChunks(RigTest):
    rig_args = dict(purge_chunk=100, purge_seconds=3600.0, tick_seconds=0.05, deletions_seconds=100.0)

    def test_old_lines_go_a_chunk_at_a_time_while_the_hub_serves(self):
        old = storemod.Store(self.rig.db, clock=lambda: time.time() - 8 * 86400)
        for n in range(350):
            old.add_line("1", "ann", "old line %d" % n, False)
        old.close()
        fresh = storemod.Store(self.rig.db)
        fresh.add_line("1", "ann", "fresh line", False)
        fresh.close()
        c = self.connect()
        self.assertTrue(self.settle(lambda: self.log_events().count("purge") >= 4, 10), self.rig.log_text.getvalue())
        self.assertEqual(sorted(set(re.findall(r" purge lines=(\d+)", self.rig.log_text.getvalue()))), ["100", "50"])
        left = self.rig.call(lambda: self.rig.store.db.execute("SELECT COUNT(*) FROM lines").fetchone()[0])
        self.assertEqual(left, 1)
        c.say("hello")                                                 # (and the hub answers meanwhile)
        self.assertIn({"t": "err", "code": "login"}, c.drain(0.5))


class TheFuzzOnTheWire(RigTest):
    rig_args = dict(LOOSE, handshake_seconds=0.5)

    def test_streams_of_noise_after_a_good_handshake(self):
        rnd = random.Random(1999)
        for n in range(150 * SCALE):
            c = hc.Client(self.rig.port, login="1001:alice", headers={"X-Client-Address": "203.0.113.%d" % (n % 250)})
            if not c.ok:
                c.close()
                continue
            c.send(test_hub_ws.random_stream(rnd))
            c.close()
        self.alive()

    def test_heads_of_noise(self):
        rnd = random.Random(2000)
        for n in range(150 * SCALE):
            head = bytearray(test_hub_ws.GOOD_HEAD)
            for _ in range(rnd.randint(1, 5)):
                where = rnd.randrange(len(head))
                head[where] = rnd.getrandbits(8) if rnd.random() < 0.5 else head[where] ^ (1 << rnd.randrange(8))
            data = bytes(head) if rnd.random() < 0.7 else bytes(rnd.getrandbits(8) for _ in range(rnd.randint(1, 600)))
            with socket.create_connection(("127.0.0.1", self.rig.port), timeout=3) as s:
                s.sendall(data)
                s.shutdown(socket.SHUT_WR)
                s.settimeout(1.0)
                try:
                    while s.recv(4096):
                        pass
                except (socket.timeout, ConnectionError):
                    pass
        self.alive()

    def test_a_flood_of_valid_looking_messages(self):
        rnd = random.Random(2001)
        messages = ['{"t":"say","text":"%s"}', '{"t":"ping"}', '{"t":"say","text":%s}', '{"t":%s}', "[%s]", '{"text":"%s"}']
        junk = ['"x"', "5", "null", "[]", "{}", "1e999", '"\\ud800"', '"\\u202e"', "-0", "true", '"' + "a" * 300 + '"', "[[[[[[[[[[]]]]]]]]]]"]
        c = hc.Client(self.rig.port, login="1001:alice")
        welcome(c)
        sent = 0
        for _ in range(400 * SCALE):
            message = rnd.choice(messages)
            body = message.replace("%s", rnd.choice(junk)).encode("utf-8", "replace")
            if len(body) > 2048:
                continue
            try:
                c.send(hc.frame(1, body))
            except OSError:
                break
            sent += 1
            c.drain(0)
            if sent % 15 == 0:
                c.close()
                c = hc.Client(self.rig.port, login="1001:alice")
                welcome(c)
        c.close()
        self.alive()

    def alive(self):
        time.sleep(0.2)
        self.assertNotIn("error", self.log_events(), "an unexpected error was logged:\n" + self.rig.log_text.getvalue()[-500:])
        c = hc.Client(self.rig.port, login="2002:survivor")
        self.clients.append(c)
        self.assertTrue(c.ok)
        welcome(c)
        c.say("still alive")
        self.assertEqual(next(m for m in c.drain(0.5) if m["t"] == "line")["text"], "still alive")
        self.assertTrue(self.settle(lambda: self.rig.hub.pending == 0), self.rig.hub.pending)


def start_process(extra_env=None, db=None, drop=()):
    folder = tempfile.mkdtemp(prefix="hub-e2e-")
    env = {k: v for k, v in os.environ.items() if not k.startswith("ANTS_HUB_")}
    env.update({"ANTS_HUB_PORT": "0", "ANTS_HUB_HOST": "127.0.0.1", "ANTS_HUB_DB": db or os.path.join(folder, "hub.db"), "ANTS_HUB_PUBLIC_URL": hc.SITE, "PYTHONDONTWRITEBYTECODE": "1",
                "PYTHONUNBUFFERED": "1"})
    env.update(extra_env or {})
    for key in drop:
        env.pop(key, None)
    log_path = os.path.join(folder, "hub.log")
    with open(log_path, "wb") as log:
        proc = subprocess.Popen([sys.executable, "-m", "hub"], cwd=REPO, env=env, stdout=log, stderr=subprocess.STDOUT)
    return proc, folder, log_path, env


def read_text(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def wait_for_port(proc, log_path, timeout=10):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        m = re.search(r" listening port=(\d+)", read_text(log_path))
        if m:
            return int(m.group(1))
        if proc.poll() is not None:
            return None
        time.sleep(0.05)
    return None


class TheProcess(unittest.TestCase):
    """`python -m hub` as the image runs it, and `python -m hub.admin` against the same database."""

    SECRET = "canary-secret-3f9a7c1e5d"
    CLIENT_ID = "canary-client-8812"
    MODS = "5551234567,5559876543"
    COOKIE = "ants_hub=canary-cookie-6d2b"

    def setUp(self):
        self.procs = []
        self.clients = []
        self.addCleanup(self.stop_all)

    def client(self, port, **kw):
        c = hc.Client(port, **kw)
        self.clients.append(c)
        return c

    def stop_all(self):
        for c in self.clients:
            c.close()
        for proc in self.procs:
            if proc.poll() is None:
                proc.kill()
            proc.wait(5)

    def run_hub(self, extra_env=None, db=None, drop=()):
        proc, folder, log_path, env = start_process(extra_env, db, drop)
        self.procs.append(proc)
        port = wait_for_port(proc, log_path)
        self.assertIsNotNone(port, "the hub did not start:\n" + read_text(log_path))
        return proc, port, folder, log_path, env

    def test_the_default_is_a_hub_that_everybody_can_read_and_nobody_can_write_to(self):
        proc, port, folder, log_path, env = self.run_hub()
        c = self.client(port, login="1001:alice")
        message = c.recv_json()
        self.assertEqual((message["login"], message["you"], message["history"]), (False, None, []))
        c.say("hello")
        self.assertEqual(c.recv_json(), {"t": "err", "code": "login"})
        c.close()

    def test_everything_end_to_end_and_the_log_holds_no_secret(self):
        extra = {"ANTS_HUB_TEST_LOGIN": "1", "ANTS_HUB_MODS": self.MODS, "ANTS_HUB_DISCORD_CLIENT_SECRET": self.SECRET, "ANTS_HUB_DISCORD_CLIENT_ID": self.CLIENT_ID}
        proc, port, folder, log_path, env = self.run_hub(extra)
        db = env["ANTS_HUB_DB"]
        headers = {"X-Client-Address": "203.0.113.77", "Cookie": self.COOKIE}
        alice = self.client(port, login="6101:alice", headers=headers)
        self.assertEqual(alice.recv_json()["you"], {"name": "alice", "mod": False})
        mod = self.client(port, login="5551234567:moddy", headers=headers)
        self.assertEqual(mod.recv_json()["you"], {"name": "moddy", "mod": True})
        chat_text = "canary chat text 41b7"
        alice.say(chat_text)
        line = next(m for m in mod.drain(0.5) if m["t"] == "line")
        self.assertEqual(line["text"], chat_text)
        # the command line bans alice: from her next line she is refused, and a restart does not change that
        admin = lambda *a: subprocess.run([sys.executable, "-m", "hub.admin", *a], cwd=REPO, env=env, capture_output=True, text=True)
        self.assertEqual(admin("ban", "alice").returncode, 0)
        alice.drain(0.1)
        alice.say("after the ban")
        self.assertEqual(alice.recv_json(), {"t": "err", "code": "banned"})
        # a deletion by the command line reaches the screens within a few seconds
        self.assertEqual(admin("del", str(line["id"])).returncode, 0)
        self.assertIn({"t": "del", "id": line["id"]}, mod.drain(3.5))
        # the health check of the image
        self.assertEqual(subprocess.run([sys.executable, "-m", "hub.health"], cwd=REPO, env=dict(env, ANTS_HUB_PORT=str(port))).returncode, 0)
        # a stop with SIGTERM: the sockets are told (1001), the process ends by itself, quickly, with status 0
        proc.send_signal(signal.SIGTERM)
        self.assertEqual(mod.expect_close(5), 1001)
        self.assertEqual(proc.wait(10), 0)
        self.assertNotEqual(subprocess.run([sys.executable, "-m", "hub.health"], cwd=REPO, env=dict(env, ANTS_HUB_PORT=str(port))).returncode, 0)
        # the same database in a new process: the history is back (without the deleted line) and the ban stands
        proc2, port2, _, log2, _ = self.run_hub({"ANTS_HUB_TEST_LOGIN": "1"}, db=db)
        again = self.client(port2, login="6101:alice")
        self.assertEqual(again.recv_json()["history"], [])
        again.say("hello again")
        self.assertEqual(again.recv_json(), {"t": "err", "code": "banned"})
        admin("unban", "alice")
        again.say("hello again")
        self.assertEqual(next(m for m in again.drain(0.5) if m["t"] == "line")["text"], "hello again")
        proc2.send_signal(signal.SIGTERM)
        proc2.wait(10)
        for path in (log_path, log2):
            text = read_text(path)
            self.assertIn(" note what=sockets_are_counted_for_each_X-Client-Address", text)          # (the dependence on the proxy setting is said at every start)
            self.assertIn(" listening port=", text)
            self.assertIn(" open ", text)
            for canary in (self.SECRET, self.CLIENT_ID, "5551234567", "5559876543", self.COOKIE, "canary-cookie", chat_text, "hello again", "203.0.113.77", "127.0.0.1", "6101", "alice", "moddy",
                           "X-Hub-Test-Login", "Traceback", folder):
                self.assertNotIn(canary, text, "the log shows " + canary)
            for line in text.splitlines():
                self.assertRegex(line, r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ [a-z_]+( [a-z_]+=[A-Za-z0-9_.:/-]*)*$", line)

    def test_a_deployment_that_enables_the_test_login_for_the_network_does_not_start(self):
        proc, folder, log_path, env = start_process({"ANTS_HUB_TEST_LOGIN": "1", "ANTS_HUB_HOST": "0.0.0.0"})
        self.procs.append(proc)
        self.assertEqual(proc.wait(10), 2)
        text = read_text(log_path)
        self.assertIn("loopback", text)
        self.assertNotIn("listening", text)

    def test_wrong_settings_stop_the_start_without_echoing_the_value(self):
        for key, value in (("ANTS_HUB_MODS", "not-a-number-canary"), ("ANTS_HUB_PUBLIC_URL", "https://x.example.org/canary-path"), ("ANTS_HUB_PORT", "canary")):
            proc, folder, log_path, env = start_process({key: value})
            self.procs.append(proc)
            self.assertEqual(proc.wait(10), 2, key)
            self.assertNotIn("canary", read_text(log_path), key)

    def test_the_site_must_be_named_or_no_socket_is_let_in(self):
        proc, port, folder, log_path, env = self.run_hub(drop=("ANTS_HUB_PUBLIC_URL",))
        self.assertEqual(status_of(port), "403")


if __name__ == "__main__":
    unittest.main()
