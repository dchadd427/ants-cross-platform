#!/usr/bin/env python3
"""docker/nginx.conf's door for the admin page's calls to the chat hub itself (/hub/admin/api/*: the lists, the status and the changes of the Chat, Log and Status sections; docs/HUB.md "The admin page's door").
Run by ./run_tests.sh --fast and by the CI; the blocks are RUN where the program nginx is installed.

  - the text: one prefix location, and a map that lets a call through only when its method and its name are ones that the hub has (the lists are read from hub/admindoor.py, so a call that the hub gains
    is not forgotten here); the checks (the name, the query, the page's own request) come before the allowance and before anything is passed on; the visitor's request is passed on as nothing but the
    few headers that the hub needs; the allowance is the zone of the admin API, and nginx's number is below the hub's bucket for an address;
  - the real blocks in front of a stand-in hub that writes down what arrives: each of the thirteen calls goes through with its headers and body; every other name, method, path and query is a 404; a request
    that the page did not make (no Sec-Fetch-Site, cross-site, same-site) is a 403 and the hub is not asked; a body of 513 bytes is refused; a flood is a 429 and the hub is not asked; a visitor's other
    cookies, address headers, Authorization and a pass of his own never reach the hub; a hub that is not there is the JSON 503 of the chat door; nothing is written to the access log;
  - the whole way, in front of the real hub, a fake Discord and the real Discord login: the owner's account reads the six lists through nginx, a change carries the page's token and the site's Origin (without
    the token, or with another login's, it is refused by the hub), and a person who is not an administrator is refused.
"""
import json
import os
import re
import shutil
import sys
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
import fake_discord as fd  # noqa: E402
import hub_client as hc  # noqa: E402
import hub_rig  # noqa: E402
import quick_http  # noqa: E402
import test_nginx_hub_login as hl  # noqa: E402
import test_nginx_stats as stats  # noqa: E402
import test_nginx_submissions as sub  # noqa: E402
from login_flow import Visitor  # noqa: E402
from hub import admindoor, limits as limitmod  # noqa: E402

CONF = stats.strip_comments(stats.read("docker", "nginx.conf"))
HEAD = CONF[:CONF.index("server {")]
DOOR = stats.block_of(CONF, "location ^~ /hub/admin/api/ ")
MAP = stats.block_of(HEAD, 'map "$request_method $uri" $hub_admin_call ')
COOKIE = "__Host-ants_hub=" + "a" * 32
READS, WRITES = sorted(admindoor.READS), sorted(admindoor.WRITES)
SECRET = "testing-gate-key-0b8e2d71a4c6f395-z"
OWNER, PLAIN, TARGET = fd.snowflake(910), fd.snowflake(911), fd.snowflake(912)               # (made-up ids: what a login by Discord has)
MINI_CONF = hl.mini_conf


class TheText(unittest.TestCase):
    def test_it_is_one_prefix_location_and_the_only_one_for_these_paths(self):
        locations = re.findall(r"^\s*location\s+(.*?)\s*\{", CONF, re.M)
        self.assertEqual(sorted(l for l in locations if "/hub/admin/api" in l), ["= /hub/admin/api", "^~ /hub/admin/api/"])
        self.assertEqual(stats.block_of(CONF, "location = /hub/admin/api ").strip(), "return 404;")         # (no redirect to the door from the address without its last slash)
        self.assertTrue(DOOR)
        self.assertNotRegex(CONF, r"location\s+~\*?\s+[^\n]*hub/admin")                        # (a regular-expression location would win over it)

    def test_the_map_lets_through_the_calls_that_the_hub_has_and_no_other(self):
        reads = re.search(r'"~\^GET /hub/admin/api/\(\?:([a-z|]+)\)\\z" read;', MAP)
        writes = re.search(r'"~\^POST /hub/admin/api/\(\?:([a-z|]+)\)\\z" change;', MAP)
        self.assertEqual(sorted(reads.group(1).split("|")), READS)
        self.assertEqual(sorted(writes.group(1).split("|")), WRITES)
        self.assertIn('default "";', MAP)
        self.assertEqual(len(re.findall(r'"~\^', MAP)), 2)
        self.assertEqual((len(READS), len(WRITES)), (7, 9))

    def test_the_checks_come_before_the_allowance_and_before_anything_is_passed_on(self):
        self.assertIn('if ($hub_admin_call = "") { return 404; }', DOOR)
        self.assertIn("if ($is_args) { return 404; }", DOOR)
        self.assertIn('if ($http_sec_fetch_site != "same-origin") { return 403; }', DOOR)
        for check in ('$hub_admin_call = ""', "$is_args", "$http_sec_fetch_site"):
            self.assertLess(DOOR.index(check), DOOR.index("limit_req "), check)
        self.assertLess(DOOR.index("limit_req "), DOOR.index("proxy_pass "))
        self.assertIn("limit_req_status 429;", DOOR)

    def test_only_these_headers_go_on_the_cookie_is_the_logins_and_the_body_is_small(self):
        self.assertEqual(hl.headers_set(DOOR), sorted(["Connection", "Host", "Origin", "Cookie", "Sec-Fetch-Site", "Content-Type", "X-Ants-Csrf", "X-Client-Address"]))
        for line in ("proxy_pass_request_headers off;", "proxy_set_header Cookie $hub_cookie_session;", "proxy_set_header X-Client-Address $remote_addr;", 'proxy_set_header Connection "close";',
                     "proxy_next_upstream off;", "client_max_body_size 512;", "proxy_redirect off;", "access_log off;", "error_page 502 504 = @hub_off;", "proxy_pass http://$ants_hub$uri;"):
            self.assertIn(line, DOOR, line)
        self.assertNotIn("proxy_pass_request_body off", DOOR)                                    # (a change has a body)
        for forbidden in ("proxy_cache", "add_header", "rewrite", "alias", "root", "try_files", "sub_filter", "X-Forwarded", "Authorization", "4002", "4030", "ants-server", "$http_cookie", "$args", "$request_uri", "X-Ants-Admin-Pass"):
            self.assertNotIn(forbidden, DOOR, forbidden)
        self.assertEqual(int(re.search(r"client_max_body_size (\d+);", DOOR).group(1)), admindoor.MAX_BODY)

    def test_the_allowance_is_the_zone_of_the_admin_api_and_is_below_the_hubs_bucket_for_an_address(self):
        self.assertIn("limit_req zone=ants_admin_api burst=50 nodelay;", DOOR)
        self.assertIn("limit_req zone=ants_admin_api burst=50 nodelay;", stats.block_of(CONF, "location ^~ /admin/api/ "))          # (the same zone: both spend the one share of the hub's bucket)
        m = re.search(r"limit_req_zone \$binary_remote_addr zone=ants_admin_api:1m rate=(\d+)r/s;", HEAD)
        limits = limitmod.Limits()
        self.assertLessEqual(int(m.group(1)), 1.0 / limits.admin_refill_seconds)
        self.assertLessEqual(50, limits.admin_burst)

    def test_no_other_door_names_these_paths(self):
        for location in re.findall(r"^\s*location\s+(.*?)\s*\{", CONF, re.M):
            if "/hub/admin/api" not in location:
                self.assertNotIn("hub/admin/api", stats.block_of(CONF, "location %s " % location), location)


class HubStand(BaseHTTPRequestHandler):
    """A stand-in hub: writes down every request (the method, the path, the headers as they came, the body) and answers {"stand":"hub"} with the headers that the real hub sends."""
    seen = []
    lock = threading.Lock()

    def handle_any(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length) if length else b""
        with self.lock:
            self.seen.append((self.command, self.path, [(k, v) for k, v in self.headers.items()], body, self.request_version))
        data = b'{"stand":"hub"}'
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    do_GET = do_POST = do_PUT = do_DELETE = do_PATCH = handle_any

    def log_message(self, *args):
        pass


def start_stand():
    cls = type("Stand_hub", (HubStand,), {"seen": [], "lock": threading.Lock()})
    server = quick_http.QuickThreadingHTTPServer(("127.0.0.1", 0), cls)
    threading.Thread(target=lambda: server.serve_forever(poll_interval=0.05), daemon=True).start()
    return cls, server


PAGE = {"Sec-Fetch-Site": "same-origin"}


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: the door for the admin page's calls to the hub was NOT run (tests/scripts/test_nginx_hub_admin_api.py)")
class TheBlocksRun(unittest.TestCase):
    relaxed = True

    @classmethod
    def setUpClass(cls):
        cls.stand, cls.server = start_stand()
        cls.rig = sub.Rig(hub_port=cls.server.server_address[1], relaxed=cls.relaxed, log=True)

    @classmethod
    def tearDownClass(cls):
        cls.rig.stop()
        cls.server.shutdown()
        cls.server.server_close()

    def count(self):
        with self.stand.lock:
            return len(self.stand.seen)

    def newest(self):
        with self.stand.lock:
            return self.stand.seen[-1]

    def ask(self, method, path, headers=None, body=None):
        return self.rig.ask(method, path, headers, body)

    def test_each_call_goes_through_with_its_method_and_the_headers_that_the_hub_needs_and_its_body(self):
        for name in READS + WRITES:
            write = name in WRITES
            before = self.count()
            visitor = dict(PAGE, **{"Cookie": "theme=dark; " + COOKIE + "; track=1", "Origin": "https://site.example", "X-Ants-Csrf": "t" * 64, "Content-Type": "application/json",
                                    "Authorization": "Bearer x", "X-Forwarded-For": "7.7.7.7", "X-Real-IP": "8.8.8.8", "X-Client-Address": "6.6.6.6", "X-Ants-Admin-Pass": "v1.forged.forged",
                                    "Referer": "https://evil.example/", "User-Agent": "agent/1"})
            body = b'{"account":"someone"}' if write else None
            status, headers, answer = self.ask("POST" if write else "GET", "/hub/admin/api/" + name, visitor, body)
            self.assertEqual(status, 200, name)
            self.assertEqual(json.loads(answer), {"stand": "hub"})
            self.assertEqual(self.count(), before + 1, name)
            method, path, raw, got, version = self.newest()
            self.assertEqual((method, path, version), ("POST" if write else "GET", "/hub/admin/api/" + name, "HTTP/1.1"), name)
            self.assertEqual(got, body or b"", name)
            seen = dict((k.lower(), v) for k, v in raw)
            self.assertEqual(sorted(seen), sorted(["connection", "host", "origin", "cookie", "sec-fetch-site", "x-ants-csrf", "x-client-address", "content-type"] + (["content-length"] if write else [])), name)
            self.assertEqual(seen["cookie"], COOKIE, "the login's cookie and no other")
            self.assertEqual(seen["x-client-address"], "127.0.0.1", "the address nginx saw, never one that the visitor wrote")
            self.assertEqual((seen["origin"], seen["sec-fetch-site"], seen["x-ants-csrf"], seen["content-type"]), ("https://site.example", "same-origin", "t" * 64, "application/json"))
            self.assertEqual(seen["connection"], "close")
            if write:
                self.assertEqual(seen["content-length"], str(len(body)))
            self.assertEqual([v for k, v in headers if k == "cache-control"], ["no-store"], "the hub's own header goes through")
            self.assertEqual([v for k, v in headers if k == "x-content-type-options"], ["nosniff"], "once, not twice")

    def test_the_path_that_goes_on_is_the_decoded_one_and_nothing_else_can_be_made_of_it(self):
        for spelled in ("/hub/admin/api/m%65", "/hub/admin/api//me", "/hub/admin/api/a/../me", "/hub/admin/api/../api/me", "/hub/admin/api/%2e%2e/api/me", "/hub/admin/api/me?"):          # (an empty query is no query: it is not passed on)
            before = self.count()
            self.assertEqual(self.ask("GET", spelled, dict(PAGE, Cookie=COOKIE))[0], 200, spelled)          # (the same call, spelled another way: the hub is asked for it spelled plainly, and only so)
            self.assertEqual((self.count(), self.newest()[1]), (before + 1, "/hub/admin/api/me"), spelled)

    def test_a_header_that_is_not_there_is_not_made_up(self):
        self.ask("GET", "/hub/admin/api/me", PAGE)
        seen = dict((k.lower(), v) for k, v in self.newest()[2])
        self.assertEqual(sorted(seen), ["connection", "host", "sec-fetch-site", "x-client-address"], "no empty Origin, no cookie, no token")

    def test_a_cookie_of_another_shape_is_not_passed_on(self):
        for cookie in ("__Host-ants_hub=" + "A" * 32, "__Host-ants_hub=" + "a" * 31, "__Host-ants_hub=" + "a" * 33, "x__Host-ants_hub=" + "a" * 32, "ants_hub=" + "a" * 32, "__Host-ants_oauth=" + "a" * 32):
            self.ask("GET", "/hub/admin/api/me", dict(PAGE, Cookie=cookie))
            self.assertNotIn("cookie", [k.lower() for k, v in self.newest()[2]], cookie)

    def test_every_other_name_method_path_and_query_is_a_404_and_the_hub_is_not_asked(self):
        before = self.count()
        cases = [("GET", "/hub/admin/api/" + w) for w in WRITES] + [("POST", "/hub/admin/api/" + r) for r in READS]
        cases += [(m, "/hub/admin/api/" + n) for m in ("PUT", "DELETE", "PATCH", "HEAD", "OPTIONS") for n in ("me", "ban")]
        cases += [("GET", p) for p in ("/hub/admin/api", "/hub/admin/api/", "/hub/admin/api/me/", "/hub/admin/api/ME", "/hub/admin/api/Me", "/hub/admin/api/me.json", "/hub/admin/api/me%20",
                                       "/hub/admin/api/x", "/hub/admin/api/me%00", "/hub/admin/api/ban/x", "/hub/admin/api/me;x=1",
                                       "/hub/admin/api/me?x=1", "/hub/admin/api/status?x")]
        cases += [("POST", p) for p in ("/hub/admin/api/mute?minutes=5", "/hub/admin/api/ban/", "/hub/admin/api/ban%2fx")]
        for method, path in cases:
            status = self.ask(method, path, dict(PAGE, Cookie=COOKIE, **{"Content-Type": "application/json", "X-Ants-Csrf": "t" * 64}), b"{}" if method in ("POST", "PUT", "PATCH", "DELETE") else None)[0]
            self.assertIn(status, (400, 404), (method, path))                                   # (400: nginx's own answer to a NUL in the address)
        self.assertEqual(self.count(), before, "none of them reached the hub")

    def test_a_request_that_the_page_did_not_make_is_a_403_and_the_hub_is_not_asked(self):
        before = self.count()
        for site in (None, "cross-site", "same-site", "none", "", "same-origin, cross-site", "SAME-ORIGIN"):
            headers = {"Cookie": COOKIE, "Content-Type": "application/json", "X-Ants-Csrf": "t" * 64}
            if site is not None:
                headers["Sec-Fetch-Site"] = site
            for method, name in (("GET", "me"), ("GET", "bans"), ("POST", "ban"), ("POST", "mute")):
                status = self.ask(method, "/hub/admin/api/" + name, headers, b"{}" if method == "POST" else None)[0]
                self.assertEqual(status, 403, (site, method, name))
        self.assertEqual(self.count(), before)

    def test_a_body_of_512_bytes_goes_through_and_one_of_513_does_not(self):
        headers = dict(PAGE, Cookie=COOKIE, **{"Content-Type": "application/json", "X-Ants-Csrf": "t" * 64, "Origin": "https://site.example"})
        before = self.count()
        self.assertEqual(self.ask("POST", "/hub/admin/api/ban", headers, b"x" * 512)[0], 200)
        self.assertEqual(len(self.newest()[3]), 512)
        self.assertEqual(self.ask("POST", "/hub/admin/api/ban", headers, b"x" * 513)[0], 413)
        self.assertEqual(self.count(), before + 1, "the big one was not passed on")

    def log_text(self):
        try:
            with open(self.rig.log_file, encoding="utf-8") as f:
                return f.read()
        except OSError:
            return ""

    def test_nothing_is_written_to_the_access_log(self):
        before = len(self.log_text())
        for name in ("me", "bans", "status"):
            self.ask("GET", "/hub/admin/api/" + name, dict(PAGE, Cookie=COOKIE))
        self.ask("POST", "/hub/admin/api/ban", dict(PAGE, Cookie=COOKIE, **{"Content-Type": "application/json"}), b'{"account":"canary-name-3f7a"}')
        self.ask("GET", "/hub/admin/api/nothing?canary-query-5b81e3", dict(PAGE, Cookie=COOKIE))
        text = self.log_text()[before:]
        self.assertNotIn("/hub/admin/api", text)
        self.assertNotIn("canary", text)

    def test_the_gate_stays_internal(self):
        before = self.count()
        self.assertEqual(self.ask("GET", "/hub/admin/gate", dict(PAGE, Cookie=COOKIE))[0], 404)
        self.assertEqual(self.count(), before)


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: the allowance of the door for the admin page's calls to the hub was NOT run (tests/scripts/test_nginx_hub_admin_api.py)")
class TheAllowance(unittest.TestCase):
    def test_one_a_second_with_a_burst_of_fifty_shared_with_the_admin_api_and_a_429_is_not_passed_on(self):
        stand, server = start_stand()
        rig = sub.Rig(hub_port=server.server_address[1], relaxed=False)
        try:
            codes = [rig.ask("GET", "/hub/admin/api/me", dict(PAGE, Cookie=COOKIE))[0] for _ in range(60)]
            self.assertEqual(codes[:51], [200] * 51)
            self.assertIn(429, codes[51:])
            self.assertEqual(len(stand.seen), codes.count(200))
            again = [rig.ask("GET", "/admin/api/me", dict(sub.ADMIN))[0] for _ in range(5)]
            self.assertIn(429, again, "the admin API's own calls spend the same share")
        finally:
            rig.stop()
            server.shutdown()
            server.server_close()

    def test_a_refused_request_does_not_spend_the_allowance(self):
        stand, server = start_stand()
        rig = sub.Rig(hub_port=server.server_address[1], relaxed=False)
        try:
            for _ in range(70):
                self.assertEqual(rig.ask("GET", "/hub/admin/api/me", {"Cookie": COOKIE})[0], 403)                # (not a request of the page)
                self.assertEqual(rig.ask("GET", "/hub/admin/api/nothing", dict(PAGE, Cookie=COOKIE))[0], 404)
            self.assertEqual(rig.ask("GET", "/hub/admin/api/me", dict(PAGE, Cookie=COOKIE))[0], 200)
            self.assertEqual(len(stand.seen), 1)
        finally:
            rig.stop()
            server.shutdown()
            server.server_close()


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: a hub that is not there was NOT run (tests/scripts/test_nginx_hub_admin_api.py)")
class TheHubIsNotThere(unittest.TestCase):
    def test_a_hub_that_is_not_there_is_the_json_503_of_the_chat_door_and_nothing_else(self):
        rig = sub.Rig(hub_port=hl.DEAD_PORT)
        try:
            for method, name, body in (("GET", "me", None), ("GET", "status", None), ("POST", "ban", b"{}")):
                status, headers, answer = rig.ask(method, "/hub/admin/api/" + name, dict(PAGE, Cookie=COOKIE, **{"Content-Type": "application/json"}), body)
                self.assertEqual(status, 503, name)
                self.assertEqual(json.loads(answer), {"error": "hub_off", "message": "Chat is not available right now."})
                self.assertEqual(sub.names(headers, "content-type"), ["application/json"])
                self.assertEqual(sub.names(headers, "cache-control"), ["no-store"])
        finally:
            rig.stop()


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: a real hub whose door is shut was NOT run (tests/scripts/test_nginx_hub_admin_api.py)")
class TheDoorThatIsShut(unittest.TestCase):
    """The real hub with no key (its door is shut: docs/HUB.md "The admin page's door") behind the real blocks of /admin/api/ (its gate) and /hub/admin/api/ (the page's calls to the hub): every answer says that
    the door is off, for a visitor with a login and for one with none, and none of them says "not an admin"; the game server is not asked."""

    def test_a_hub_with_no_key_is_door_off_for_everybody_and_never_not_allowed(self):
        with hub_rig.Rig(env={}) as hub:
            rig = sub.Rig(hub_port=hub.port)
            try:
                for cookie in (None, COOKIE, sub.ADMIN["Cookie"]):
                    headers = dict(PAGE, **({"Cookie": cookie} if cookie else {}))
                    for method, body in (("GET", None), ("POST", b"{}")):
                        status, answered, text = rig.ask(method, "/admin/api/me", dict(headers, **{"Content-Type": "application/json"}), body)
                        self.assertEqual((status, json.loads(text)["error"]), (503, "door_off"), (cookie, method))
                    status, answered, text = rig.ask("GET", "/hub/admin/api/me", headers)
                    self.assertEqual((status, json.loads(text)), (503, {"error": "door_off"}), cookie)
                    self.assertNotIn(b"csrf", text)
                    self.assertEqual(sub.names(answered, "cache-control"), ["no-store"])
                self.assertEqual(len(rig.door.seen), 0, "the game server is not asked")
            finally:
                rig.stop()

    def test_a_hub_with_a_key_still_says_who_is_not_logged_in(self):
        with hub_rig.Rig(env={"ANTS_ADMIN_GATE_SECRET": SECRET}) as hub:
            rig = sub.Rig(hub_port=hub.port)
            try:
                status, answered, text = rig.ask("GET", "/admin/api/me", {})
                self.assertEqual((status, json.loads(text)["error"]), (401, "not_signed_in"))
                status, answered, text = rig.ask("GET", "/hub/admin/api/me", dict(PAGE, Cookie=COOKIE))
                self.assertEqual((status, json.loads(text)), (401, {"error": "login"}))
            finally:
                rig.stop()


def door_conf(tmp, port, hub_port, stand_port, dead_port):
    """The small server of tests/scripts/test_nginx_hub_login.py with the real text of the map and of this door added to it (and the zone that the door spends)."""
    text = MINI_CONF(tmp, port, hub_port, stand_port, dead_port)
    zone = re.search(r"limit_req_zone \$binary_remote_addr zone=ants_admin_api:[^;]+;", HEAD).group(0)
    door = DOOR.replace("resolver 127.0.0.11 valid=10s ipv6=off;", "").replace("set $ants_hub ants-hub:4040;", "set $ants_hub 127.0.0.1:%d;" % hub_port)
    text = text.replace("  server {", "  %s\n  map \"$request_method $uri\" $hub_admin_call {%s}\n  server {" % (zone, MAP), 1)
    text = text.replace("    location / { return 200 'the page'; }", "    location ^~ /hub/admin/api/ {%s}\n    location / { return 200 'the page'; }" % door, 1)
    return text


@unittest.skipUnless(hl.nginx_binary(), "no nginx program on this machine")
class TheWholeWay(unittest.TestCase):
    """The real hub and the real login behind the door: the page's calls, as a browser sends them."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="ants_hub_admin_nginx.")
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        self.stand = hl.Stand()
        self.addCleanup(self.stand.server_close)
        self.addCleanup(self.stand.shutdown)
        site_port = hl.free_port()
        self.site = "http://127.0.0.1:%d" % site_port
        self.fake = fd.FakeDiscord(self.site + "/hub/callback")
        self.fake.consent = False
        self.fake.members = {OWNER: [], PLAIN: [], TARGET: []}
        self.addCleanup(self.fake.close)
        self.rig = hub_rig.Rig(discord=self.fake, member_fake=self.fake, origin=self.site, owner=OWNER, env={"ANTS_ADMIN_GATE_SECRET": SECRET}, recheck=1, roles_grace_seconds=0.3, roles_refill_seconds=0.01,
                               roles_jitter=0.0, tick_seconds=0.05, sockets_per_address=100, frame_burst=1000, frame_refill_seconds=0.001, discord_seconds=1.0, discord_connect_seconds=1.0,
                               admin_lookups_burst=50, admin_lookups_refill_seconds=0.01)
        self.rig.__enter__()
        self.addCleanup(self.rig.__exit__, None, None, None)
        with mock.patch.object(hl, "mini_conf", door_conf):
            self.nginx = hl.Nginx(self.tmp, site_port, self.rig.port, self.stand.server_address[1], hl.DEAD_PORT)
        self.addCleanup(self.nginx.stop)

    def visitor(self, user):
        self.fake.user = user
        v = Visitor(self.nginx.port, self.fake)
        v.origin = self.site
        self.assertEqual(v.login("admin").status, 302)
        return v

    def page(self, v, method, name, body=None, token=None):
        """A call of the page: its login's cookie, Sec-Fetch-Site and, for a change, the Origin, the JSON type and the token."""
        pairs = [("Cookie", v.jar.header()), ("Sec-Fetch-Site", "same-origin")]
        if method == "POST":
            pairs += [("Origin", self.site), ("Content-Type", "application/json"), ("X-Ants-Csrf", token)]
        return hc.request(self.nginx.port, method, "/hub/admin/api/" + name, pairs, body=json.dumps(body).encode() if body is not None else b"")

    def test_the_owner_reads_the_six_lists_and_changes_through_nginx_with_the_pages_token(self):
        owner = self.visitor((OWNER, "olive.owner"))
        target = self.visitor((TARGET, "terry.target"))
        me = self.page(owner, "GET", "me")
        self.assertEqual((me.status, me.json()["name"], me.json()["role"]), (200, "olive.owner", "owner"))
        token = me.json()["csrf"]
        for name in ("bans", "mutes", "hidden", "modlog", "status"):
            r = self.page(owner, "GET", name)
            self.assertEqual(r.status, 200, name)
            self.assertIn("untrusted", r.json()) if name != "status" else self.assertEqual(sorted(r.json()), ["health", "images", "last_picture", "store", "version"])
        self.assertEqual(self.page(owner, "POST", "mute", {"account": "terry.target", "minutes": 5}, token="0" * 64).json(), {"error": "csrf"}, "a token that is not this login's")
        r = self.page(owner, "POST", "mute", {"account": "terry.target", "minutes": 5}, token=token)
        self.assertEqual((r.status, sorted(r.json())), (200, ["ok", "until"]))
        self.assertEqual([m["name"] for m in self.page(owner, "GET", "mutes").json()["mutes"]], ["terry.target"])
        self.assertEqual(self.page(owner, "POST", "unmute", {"account": "terry.target"}, token=token).status, 200)
        self.assertEqual(self.page(owner, "POST", "ban", {"account": "terry.target"}, token=token).status, 200)
        self.assertEqual([b["name"] for b in self.page(owner, "GET", "bans").json()["bans"]], ["terry.target"])
        self.assertEqual(self.page(owner, "POST", "unban", {"account": "terry.target"}, token=token).status, 200)
        self.assertEqual(self.page(owner, "POST", "ban", {"account": "nobody.at.all"}, token=token).json(), {"error": "who"})
        acts = [r["action"] for r in self.page(owner, "GET", "modlog").json()["modlog"]]
        self.assertEqual(acts, ["unban", "ban", "unmute", "mute"])
        self.assertIsNotNone(target)

    def test_a_person_who_is_not_an_administrator_and_a_visitor_who_is_not_logged_in_are_refused_by_the_hub(self):
        plain = self.visitor((PLAIN, "pat.plain"))
        for name in READS:
            self.assertEqual(self.page(plain, "GET", name).status, 403, name)
        nobody = Visitor(self.nginx.port, self.fake)
        nobody.origin = self.site
        for name in READS:
            self.assertEqual(self.page(nobody, "GET", name).status, 401, name)

    def test_the_hub_still_checks_the_origin_and_the_type_that_nginx_passes_on(self):
        owner = self.visitor((OWNER, "olive.owner"))
        token = self.page(owner, "GET", "me").json()["csrf"]
        headers = [("Cookie", owner.jar.header()), ("Sec-Fetch-Site", "same-origin"), ("X-Ants-Csrf", token)]
        body = json.dumps({"account": "terry.target", "minutes": 5}).encode()
        self.assertEqual(hc.request(self.nginx.port, "POST", "/hub/admin/api/mute", headers + [("Origin", "https://evil.example"), ("Content-Type", "application/json")], body=body).status, 403)
        self.assertEqual(hc.request(self.nginx.port, "POST", "/hub/admin/api/mute", headers + [("Origin", self.site), ("Content-Type", "text/plain")], body=body).status, 415)
        self.assertEqual(hc.request(self.nginx.port, "POST", "/hub/admin/api/mute", headers + [("Content-Type", "application/json")], body=body).status, 403, "no Origin")


if __name__ == "__main__":
    unittest.main()
