#!/usr/bin/env python3
"""docker/nginx.conf routes the plays and the likes of replays to the game server's COUNTS door and to nothing else, behind the chat hub's like gate where a person is needed (run by ./run_tests.sh --fast;
the blocks are RUN where the program nginx is installed, and the CI machines have it).

The counts door (ants_server --counts-port 4032, docs/REPLAY_COUNTS.md) is reached by the site's nginx for four paths of a visitor's browser:

  - POST /counts/play/<id>: nobody is asked who; the door is given the path, an empty body and the few headers of the block, never a cookie, an address or a header of the visitor's;
  - GET /counts/mine?ids=<id>,...: the like gate is asked first (a stand-in hub plays it below); a visitor who is not logged in is answered {"signed_in":false} here and the door is not asked; a gate in trouble or with no good
    key is a JSON 503 likes_off; only the gate's pass goes on, never a header of that name from the visitor;
  - POST /counts/like/<id> and POST /counts/unlike/<id>: the same gate, 401 for a visitor who is not logged in, the token of the visitor goes on in X-Ants-Csrf, a request that says it is of another site is turned away first
    (before the allowance and before the gate);
  - everything else under /counts/ (Forget me's /counts/forget included) and the gate's own path are a 404 from outside;
  - an allowance for each address and for each kind (a flood is a 429 here, and neither the gate nor the door is asked), a body of at most a kilobyte, a game server that is not there is a JSON 503 counts_off.
"""
import json
import os
import re
import shutil
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_nginx_submissions as rig_module  # noqa: E402

Rig = rig_module.Rig
names = rig_module.names
HUB_COOKIE = rig_module.HUB_COOKIE
IN = {"Cookie": HUB_COOKIE + rig_module.LIKE_SESSION}
ID = "ants-TINY-20261008-143209Z"
ID2 = "ants-TREASURE-20261008-143210Z-2"


def ids(n):
    return ",".join("ants-TINY-20261008-%06dZ" % (100000 + k) for k in range(n))


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: the counts blocks of docker/nginx.conf were NOT run (tests/scripts/test_nginx_replay_counts.py)")
class TheBlocksRun(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rig = Rig()

    @classmethod
    def tearDownClass(cls):
        cls.rig.stop()

    def last(self, stub):
        with stub.lock:
            return stub.seen[-1]

    def count(self, stub):
        with stub.lock:
            return len(stub.seen)

    def test_a_play_reaches_the_door_with_nothing_of_the_visitor(self):
        rig = self.rig
        before, gates = self.count(rig.counts_door), self.count(rig.like_gate)
        status, headers, body = rig.ask("POST", "/counts/play/" + ID, {"Cookie": "theme=dark; " + HUB_COOKIE + rig_module.LIKE_SESSION, "Authorization": "Bearer x", "X-Client-Address": "9.9.9.9", "X-Forwarded-For": "7.7.7.7",
                                                                        "Referer": "http://evil.example/", "User-Agent": "attacker/1", "X-Ants-Like-Pass": "v1.forged.pass", "X-Ants-Csrf": "token", "Sec-Fetch-Site": "same-origin"})
        self.assertEqual(status, 200)                                                                    # (the stand-in door answers 200 to everything: the answer is passed on as it is)
        method, path, seen, length = self.last(rig.counts_door)
        self.assertEqual((method, path, length, seen.get("content-length")), ("POST", "/counts/play/" + ID, 0, "0"))
        self.assertEqual(set(seen) - {"host"}, {"connection", "content-length"})                         # (nothing else of the request: no cookie, no pass, no token, no address, no site header)
        self.assertEqual(self.count(rig.counts_door), before + 1)
        self.assertEqual(names(headers, "set-cookie"), [])                                               # (a door that sets a cookie: the site never passes one on)
        self.assertEqual(self.count(rig.like_gate), gates)                                               # (a play asks nobody who)

    def test_only_a_post_of_a_good_id_with_no_query_is_let_in(self):
        rig = self.rig
        before = self.count(rig.counts_door)
        for method in ("GET", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS"):
            body = b"" if method in ("PUT", "PATCH") else None
            self.assertEqual(rig.ask(method, "/counts/play/" + ID, None, body)[0], 405, method)
        for target in ("/counts/play/", "/counts/play", "/counts/play/x", "/counts/play/ants-TINY-x", "/counts/play/" + ID + "x", "/counts/play/" + ID + "/", "/counts/play/" + ID + "?x=1",
                       "/counts/play/ants-" + "A" * 30 + "-20261008-143209Z", "/counts/play/ants-TINY-2026100-143209Z", "/counts/play/ants-TINY-20261008-143209Z-12345",
                       "/counts/PLAY/" + ID, "/counts//play/" + ID + "%0a"):
            status = rig.ask("POST", target)[0]
            self.assertIn(status, (404, 405), target)
        self.assertEqual(self.count(rig.counts_door), before)                                                 # (not one of them reached the door)
        for target in ("//counts/play/" + ID, "/./counts/play/" + ID, "/x/../counts/play/" + ID, "/counts/%70lay/" + ID):          # (cleaned by nginx: the door sees the clean path)
            self.assertEqual(rig.ask("POST", target)[0], 200, target)
            self.assertEqual(self.last(rig.counts_door)[1], "/counts/play/" + ID, target)
        self.assertEqual(rig.ask("POST", "/counts/play/" + ID2)[0], 200)
        self.assertEqual(self.last(rig.counts_door)[1], "/counts/play/" + ID2)

    def test_a_request_of_another_site_is_turned_away_before_anything_is_asked(self):
        rig = self.rig
        doors, gates = self.count(rig.counts_door), self.count(rig.like_gate)
        for target, method in (("/counts/play/" + ID, "POST"), ("/counts/like/" + ID, "POST"), ("/counts/unlike/" + ID, "POST"), ("/counts/mine?ids=" + ID, "GET")):
            for site in ("cross-site", "same-site"):
                status, headers, body = rig.ask(method, target, dict(IN, **{"Sec-Fetch-Site": site}))
                self.assertEqual((status, json.loads(body)["error"]), (403, "cross_site"), (target, site))
        self.assertEqual((self.count(rig.counts_door), self.count(rig.like_gate)), (doors, gates))
        for site in ("same-origin", "none"):
            self.assertEqual(rig.ask("POST", "/counts/play/" + ID, {"Sec-Fetch-Site": site})[0], 200, site)

    def test_the_list_of_a_visitor_who_is_not_logged_in_is_answered_here(self):
        rig = self.rig
        doors, gates = self.count(rig.counts_door), self.count(rig.like_gate)
        status, headers, body = rig.ask("GET", "/counts/mine?ids=" + ID)
        self.assertEqual((status, json.loads(body)), (200, {"signed_in": False, "liked": []}))
        self.assertEqual(names(headers, "cache-control"), ["no-store"])
        self.assertEqual(self.count(rig.like_gate), gates + 1)
        self.assertEqual(self.count(rig.counts_door), doors)
        status, headers, body = rig.ask("GET", "/counts/mine?ids=" + ID, {"Cookie": HUB_COOKIE + "0" * 32})              # (a login that ended or never was)
        self.assertEqual(json.loads(body), {"signed_in": False, "liked": []})
        self.assertEqual(self.count(rig.counts_door), doors)

    def test_a_person_gets_the_gates_pass_and_the_token_and_nothing_else_of_the_visitor(self):
        rig = self.rig
        status, headers, body = rig.ask("GET", "/counts/mine?ids=" + ID + "," + ID2, dict(IN, **{"X-Ants-Like-Pass": "v1.forged.pass", "X-Client-Address": "9.9.9.9", "Authorization": "Bearer x", "Sec-Fetch-Site": "same-origin",
                                                                                                 "X-Ants-Csrf": "t"}))
        self.assertEqual(status, 200)
        method, path, seen, length = self.last(rig.counts_door)
        self.assertEqual((method, path, length), ("GET", "/counts/mine?ids=" + ID + "," + ID2, 0))
        with rig.like_gate.lock:
            made = rig.like_gate.passes[-1]
        self.assertEqual(seen.get("x-ants-like-pass"), made)                                                  # (the gate's pass, byte for byte: the visitor's own is gone)
        self.assertEqual(set(seen) - {"host"}, {"connection", "x-ants-like-pass", "sec-fetch-site"})
        gate_method, gate_path, gate_headers, gate_length, version = self.last(rig.like_gate)
        self.assertEqual((gate_method, gate_path, gate_length), ("GET", "/hub/likes/gate", 0))
        sent = dict((k.lower(), v) for k, v in gate_headers)
        self.assertEqual(sent.get("cookie"), HUB_COOKIE + rig_module.LIKE_SESSION)                            # (the login's cookie and no other)
        self.assertEqual(sent.get("x-client-address"), "127.0.0.1")                                          # (the address nginx saw, never the header of the visitor)
        self.assertNotIn("authorization", sent)
        self.assertNotIn("x-ants-like-pass", sent)
        self.assertNotIn("content-length", sent)

    def test_the_list_takes_the_ids_it_may_take_and_no_other_query(self):
        rig = self.rig
        before = self.count(rig.like_gate)
        self.assertEqual(rig.ask("GET", "/counts/mine", IN)[0], 200)                                           # (no ids is the plain list of nothing: the token)
        self.assertEqual(rig.ask("GET", "/counts/mine?ids=" + ids(50), IN)[0], 200)
        for query in ("?ids=" + ids(51), "?ids=", "?ids=" + ID + ",", "?ids=nope", "?ids=" + ID + "&x=1", "?x=" + ID, "?ids=" + ID + "%2c" + ID, "?ids=" + ID + ",,x", "?IDS=" + ID):
            status, headers, body = rig.ask("GET", "/counts/mine" + query, IN)
            self.assertEqual((status, json.loads(body)["error"]), (400, "ids"), query)
        for method in ("POST", "PUT", "DELETE", "HEAD"):
            self.assertEqual(rig.ask(method, "/counts/mine?ids=" + ID, IN, b"" if method in ("POST", "PUT") else None)[0], 405, method)
        self.assertEqual(self.count(rig.like_gate), before + 2)                                              # (the gate is asked only for the two that are well formed)

    def test_a_like_and_an_unlike_pass_the_token_on_and_a_visitor_who_is_not_logged_in_is_a_401(self):
        rig = self.rig
        for verb in ("like", "unlike"):
            status, headers, body = rig.ask("POST", "/counts/%s/%s" % (verb, ID), dict(IN, **{"X-Ants-Csrf": "0123456789abcdef0123456789abcdef", "X-Ants-Like-Pass": "v1.forged.pass", "Sec-Fetch-Site": "same-origin"}))
            self.assertEqual(status, 200, verb)
            method, path, seen, length = self.last(rig.counts_door)
            with rig.like_gate.lock:
                made = rig.like_gate.passes[-1]
            self.assertEqual((method, path, length, seen.get("content-length")), ("POST", "/counts/%s/%s" % (verb, ID), 0, "0"))
            self.assertEqual((seen.get("x-ants-like-pass"), seen.get("x-ants-csrf"), seen.get("sec-fetch-site")), (made, "0123456789abcdef0123456789abcdef", "same-origin"))
            self.assertEqual(set(seen) - {"host"}, {"connection", "content-length", "x-ants-like-pass", "x-ants-csrf", "sec-fetch-site"})
            doors = self.count(rig.counts_door)
            status, headers, body = rig.ask("POST", "/counts/%s/%s" % (verb, ID), {"X-Ants-Csrf": "t"})
            self.assertEqual((status, json.loads(body)["error"]), (401, "not_signed_in"), verb)
            self.assertEqual(names(headers, "cache-control"), ["no-store"])
            self.assertEqual(self.count(rig.counts_door), doors)                                               # (the door is not asked for a visitor whom the gate does not know)
        before = self.count(rig.counts_door)
        for method in ("GET", "PUT", "DELETE"):
            self.assertEqual(rig.ask(method, "/counts/like/" + ID, IN, b"" if method == "PUT" else None)[0], 405, method)
        for target in ("/counts/like/", "/counts/like/x", "/counts/like/" + ID + "?x=1", "/counts/unlike/" + ID + "/x"):
            self.assertIn(rig.ask("POST", target, IN)[0], (404, 405), target)
        self.assertEqual(self.count(rig.counts_door), before)

    def test_a_gate_in_trouble_or_with_no_good_key_is_a_json_503_likes_off_and_never_a_no(self):
        rig = self.rig
        doors = self.count(rig.counts_door)
        for session in (rig_module.LIKE_BROKEN_SESSION, rig_module.LIKE_SHUT_SESSION, rig_module.LIKE_BUSY_SESSION):
            for method, target in (("GET", "/counts/mine?ids=" + ID), ("POST", "/counts/like/" + ID), ("POST", "/counts/unlike/" + ID)):
                status, headers, body = rig.ask(method, target, {"Cookie": HUB_COOKIE + session})
                self.assertEqual((status, json.loads(body)["error"]), (503, "likes_off"), (session, target))
                self.assertEqual(names(headers, "content-type"), ["application/json"])
        self.assertEqual(self.count(rig.counts_door), doors)
        self.assertEqual(rig.ask("POST", "/counts/play/" + ID)[0], 200)                                        # (plays need no gate)

    def test_nothing_else_under_counts_is_passed_and_the_gate_is_not_for_a_browser(self):
        rig = self.rig
        doors, gates = self.count(rig.counts_door), self.count(rig.like_gate)
        for target in ("/counts/forget", "/counts/forget/", "/counts/", "/counts/x", "/counts/mine/", "/counts/play", "/counts/likes", "/counts/like", "/hub/likes/gate", "/hub/likes/", "/hub/likes/x"):
            for method in ("POST", "GET"):
                status, headers, body = rig.ask(method, target, IN, b"" if method == "POST" else None)
                self.assertIn(status, (404, 405), (method, target))
                self.assertNotIn(b"stub", body, target)
        self.assertEqual((self.count(rig.counts_door), self.count(rig.like_gate)), (doors, gates))

    def test_a_body_is_a_kilobyte_at_the_most_and_none_of_it_goes_on(self):
        rig = self.rig
        before = self.count(rig.counts_door)
        self.assertEqual(rig.ask("POST", "/counts/play/" + ID, None, b"x" * 1025)[0], 413)
        self.assertEqual(rig.ask("POST", "/counts/like/" + ID, IN, b"x" * 1025)[0], 413)
        self.assertEqual(self.count(rig.counts_door), before)
        self.assertEqual(rig.ask("POST", "/counts/play/" + ID, None, b"x" * 1000)[0], 200)
        self.assertEqual(self.last(rig.counts_door)[3], 0)                                                     # (what a small body held is not sent: the door's POSTs have none)


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: the allowances of docker/nginx.conf for the counts were NOT run (tests/scripts/test_nginx_replay_counts.py)")
class TheAllowances(unittest.TestCase):
    def test_each_kind_has_a_zone_of_its_own_and_a_flood_is_a_429_that_neither_the_gate_nor_the_door_sees(self):
        rig = Rig(relaxed=False)
        try:
            same = [rig.ask("POST", "/counts/play/" + ID)[0] for _ in range(6)]
            self.assertEqual(set(same[:3]), {200})                                                               # (one replay from one address: 2 a minute with a burst of 2, three in a row pass)
            self.assertEqual(set(same[3:]), {429})
            self.assertEqual(rig.ask("POST", "/counts/play/" + ID2)[0], 200)                                     # (another replay of the same address has its own)
        finally:
            rig.stop()
        rig = Rig(relaxed=False)
        try:
            plays = [rig.ask("POST", "/counts/play/" + ("ants-TINY-20261008-1%05dZ" % k))[0] for k in range(26)]
            self.assertEqual(set(plays[:21]), {200})                                                             # (30 a minute for an address with a burst of 20, whatever the replays: twenty-one in a row pass)
            self.assertEqual(set(plays[21:]), {429})
            with rig.counts_door.lock:
                reached = len(rig.counts_door.seen)
            self.assertEqual(reached, 21)
            likes = [rig.ask("POST", "/counts/like/" + ID, IN)[0] for _ in range(14)]
            self.assertEqual(set(likes[:11]), {200})                                                             # (20 a minute, a burst of 10)
            self.assertEqual(set(likes[11:]), {429})
            gates = len(rig.like_gate.seen)
            self.assertEqual(gates, 11)                                                                         # (the gate was asked for the eleven that passed, not for the flood)
            unlikes = [rig.ask("POST", "/counts/unlike/" + ID, IN)[0] for _ in range(3)]
            self.assertEqual(set(unlikes), {429})                                                                # (a like and an unlike share the zone: it is the same person's changes)
            mine = [rig.ask("GET", "/counts/mine?ids=" + ID, IN)[0] for _ in range(24)]
            self.assertEqual(set(mine[:21]), {200})
            self.assertEqual(set(mine[21:]), {429})
            self.assertEqual(rig.ask("POST", "/counts/play/" + ID, source="127.0.0.2")[0], 200)                  # (another address has its own allowance)
        finally:
            rig.stop()


class TheStackStartsTheDoor(unittest.TestCase):
    """The door is only reached if the program listens on the stack's network: a published port does not reach a program on the container's loopback address, and nginx does not either (the other doors' tests do the same)."""

    def test_the_image_makes_the_door_listen_on_every_interface_and_the_stack_gives_the_port_and_the_hub_its_address(self):
        with open(os.path.join(rig_module.REPO, "Dockerfile.server"), encoding="utf-8") as f:
            dockerfile = f.read()
        entry = re.findall(r"(?m)^ENTRYPOINT \[(.*)\]$", dockerfile)[-1]                                       # (the last stage is the server)
        self.assertIn('"--counts-any-interface"', entry)
        self.assertNotIn("--counts-port", entry)                                                              # (off in the program and in the image: the stack file gives the port)
        stack = open(os.path.join(rig_module.REPO, "docker-compose.stack.yml"), encoding="utf-8").read()
        code = "\n".join(l for l in stack.splitlines() if not l.lstrip().startswith("#"))
        self.assertIn('"--counts-port", "${ANTS_COUNTS_PORT:-4032}"', code)
        self.assertNotIn("4032:4032", code)                                                                   # (not published on the host)
        self.assertIn("ANTS_HUB_COUNTS_URL: ${ANTS_HUB_COUNTS_URL-http://ants-server:4032}", code)            # (the hub's address of the door: the same port)
        self.assertIn("ants-server:4032", rig_module.RAW)                                                      # (nginx's address of the door: the same port)


class TheDoorIsDown(unittest.TestCase):
    @unittest.skipUnless(shutil.which("nginx"), "nginx is not installed")
    def test_a_game_server_without_the_door_is_a_json_503_never_a_page_of_the_site(self):
        rig = Rig()
        try:
            rig.counts_door_server.shutdown()
            rig.counts_door_server.server_close()
            for method, target, headers in (("POST", "/counts/play/" + ID, None), ("POST", "/counts/like/" + ID, IN), ("POST", "/counts/unlike/" + ID, IN), ("GET", "/counts/mine?ids=" + ID, IN)):
                status, response_headers, text = rig.ask(method, target, headers)
                self.assertEqual((status, json.loads(text)["error"]), (503, "counts_off"), target)
                self.assertEqual(names(response_headers, "cache-control"), ["no-store"])
                self.assertEqual(names(response_headers, "content-type"), ["application/json"])
        finally:
            rig.stop()


if __name__ == "__main__":
    unittest.main()
