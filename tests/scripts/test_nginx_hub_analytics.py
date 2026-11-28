#!/usr/bin/env python3
"""docker/nginx.conf's door for the counter of the site's visits (POST /hub/a: docs/ANALYTICS.md). Run by ./run_tests.sh --fast and by the CI; the blocks are RUN where the program nginx is installed.

  - the text: one exact location, the checks (method, query, the page's own request) before the allowance and before anything is passed on; the visitor's request goes on as the few headers that the hub
    needs (the browser's own User-Agent and the two headers that ask not to be tracked among them; never a cookie, a referrer or a language); the access log is off; the body is at most the hub's number;
  - the real block in front of a stand-in hub that writes down what arrives: a beacon of the page goes through with exactly those headers and its body; every other method, path and query is refused and the
    hub is not asked; a request that is not of the site's own page is a 403; a body over the limit is a 413; a flood is a 429; a hub that is not there is the JSON 503; nothing is written to the access log.
"""
import json
import os
import re
import shutil
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
import test_nginx_hub_admin_api as api  # noqa: E402
import test_nginx_hub_login as hl  # noqa: E402
import test_nginx_stats as stats  # noqa: E402
import test_nginx_submissions as sub  # noqa: E402
from hub import analyticsdoor, analytics_events, limits as limitmod  # noqa: E402

CONF = stats.strip_comments(stats.read("docker", "nginx.conf"))
HEAD = CONF[:CONF.index("server {")]
DOOR = stats.block_of(CONF, "location = /hub/a ")
BEACON = {"Sec-Fetch-Site": "same-origin", "Origin": "https://site.example", "Content-Type": "text/plain;charset=UTF-8", "User-Agent": "Mozilla/5.0 (test)"}
EVENT = b'{"t":"view","p":"/","w":1300,"l":"en-US"}'


class TheText(unittest.TestCase):
    def test_it_is_one_exact_location_and_no_other_names_the_path(self):
        locations = re.findall(r"^\s*location\s+(.*?)\s*\{", CONF, re.M)
        self.assertEqual([l for l in locations if l.endswith("/hub/a")], ["= /hub/a"])
        self.assertNotRegex(CONF, r"location\s+~\*?\s+[^\n]*hub/a\b")
        for location in locations:
            if location != "= /hub/a":
                self.assertNotIn("/hub/a ", stats.block_of(CONF, "location %s " % location) + " ", location)

    def test_the_checks_come_before_the_allowance_and_before_anything_is_passed_on(self):
        self.assertIn("if ($request_method != POST) { return 405; }", DOOR)
        self.assertIn("if ($is_args) { return 404; }", DOOR)
        self.assertIn('if ($http_sec_fetch_site != "same-origin") { return 403; }', DOOR)
        for check in ("$request_method", "$is_args", "$http_sec_fetch_site"):
            self.assertLess(DOOR.index(check), DOOR.index("limit_req "), check)
        self.assertLess(DOOR.index("limit_req "), DOOR.index("proxy_pass "))
        self.assertIn("limit_req_status 429;", DOOR)

    def test_only_these_headers_go_on_and_there_is_no_cookie_no_referrer_and_no_language(self):
        self.assertEqual(hl.headers_set(DOOR), sorted(["Connection", "Host", "Origin", "Sec-Fetch-Site", "Content-Type", "User-Agent", "DNT", "Sec-GPC", "X-Client-Address"]))
        for line in ("proxy_pass_request_headers off;", "proxy_set_header X-Client-Address $remote_addr;", "proxy_set_header User-Agent $http_user_agent;", "proxy_set_header DNT $http_dnt;",
                     "proxy_set_header Sec-GPC $http_sec_gpc;", 'proxy_set_header Connection "close";', "proxy_redirect off;", "error_page 502 504 = @hub_off;"):
            self.assertIn(line, DOOR, line)
        self.assertNotIn("proxy_pass_request_body off", DOOR)                                    # (a beacon has a body)
        for forbidden in ("$http_cookie", "$http_referer", "$http_accept_language", "Authorization", "X-Forwarded", "$args", "$request_uri", "proxy_cache", "add_header", "rewrite", "4002", "ants-server"):
            self.assertNotIn(forbidden, DOOR, forbidden)

    def test_nothing_is_logged_by_nginx(self):
        self.assertIn("access_log off;", DOOR)
        self.assertIn("error_log /var/log/ants/web.log crit;", DOOR)                         # (the serious errors only: nginx writes the visitor's address into the line of a body that is too big)

    def test_the_body_is_as_big_as_the_hubs_and_not_bigger(self):
        self.assertEqual(re.search(r"client_max_body_size (\d+)k;", DOOR).group(1) + "k", "1k")
        self.assertEqual(analytics_events.MAX_BODY, 1024)
        self.assertEqual(analyticsdoor.MAX_BODY, 1024)

    def test_the_allowance_is_wider_than_the_hubs_own(self):
        self.assertIn("limit_req zone=ants_hub_a burst=60 nodelay;", DOOR)
        found = re.search(r"limit_req_zone \$binary_remote_addr zone=ants_hub_a:1m rate=(\d+)r/s;", HEAD)
        limits = limitmod.Limits()
        self.assertGreaterEqual(int(found.group(1)), 1.0 / limits.analytics_refill_seconds, "the hub's allowance for an address is the narrower one")
        self.assertGreaterEqual(60, limits.analytics_burst)


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: the counter's door was NOT run (tests/scripts/test_nginx_hub_analytics.py)")
class TheBlockRuns(unittest.TestCase):
    relaxed = True

    @classmethod
    def setUpClass(cls):
        cls.stand, cls.server = api.start_stand()
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

    def beacon(self, headers=None, body=EVENT, method="POST", path="/hub/a"):
        return self.rig.ask(method, path, dict(BEACON, **(headers or {})), body)

    def test_a_beacon_goes_through_with_the_headers_that_the_hub_needs_and_its_body(self):
        before = self.count()
        status, headers, answer = self.beacon({"Cookie": "__Host-ants_hub=" + "a" * 32, "Referer": "https://evil.example/", "Accept-Language": "de", "Authorization": "Bearer x", "X-Client-Address": "6.6.6.6",
                                                "X-Forwarded-For": "7.7.7.7", "DNT": "1", "Sec-GPC": "1"})
        self.assertEqual(status, 200)
        method, path, raw, got, version = self.newest()
        self.assertEqual(self.count(), before + 1)
        self.assertEqual((method, path, version, got), ("POST", "/hub/a", "HTTP/1.1", EVENT))
        seen = dict((k.lower(), v) for k, v in raw)
        self.assertEqual(sorted(seen), sorted(["connection", "host", "origin", "sec-fetch-site", "content-type", "content-length", "user-agent", "dnt", "sec-gpc", "x-client-address"]))
        self.assertEqual((seen["origin"], seen["sec-fetch-site"], seen["content-type"], seen["user-agent"], seen["dnt"], seen["sec-gpc"]), ("https://site.example", "same-origin", "text/plain;charset=UTF-8", "Mozilla/5.0 (test)", "1", "1"))
        self.assertEqual(seen["x-client-address"], "127.0.0.1", "the address nginx saw, never one that the visitor wrote")
        self.assertEqual(seen["content-length"], str(len(EVENT)))

    def test_a_header_that_the_browser_did_not_send_is_not_made_up(self):
        self.beacon()
        seen = dict((k.lower(), v) for k, v in self.newest()[2])
        self.assertNotIn("dnt", seen)
        self.assertNotIn("sec-gpc", seen)
        self.assertNotIn("cookie", seen)

    def test_every_other_method_path_and_query_is_refused_and_the_hub_is_not_asked(self):
        before = self.count()
        for method in ("GET", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS"):
            self.assertEqual(self.beacon(method=method)[0], 405, method)
        for path in ("/hub/a?x=1", "/hub/a?canary-query-1f4e"):
            self.assertEqual(self.beacon(path=path)[0], 404, path)
        for path in ("/hub/a/", "/hub/ab", "/hub/A", "/hub/a%2fx"):
            self.assertIn(self.beacon(path=path)[0], (400, 404), path)
        self.assertEqual(self.count(), before)

    def test_a_request_that_the_page_did_not_make_is_a_403_and_the_hub_is_not_asked(self):
        before = self.count()
        for site in ("cross-site", "same-site", "none", "", "same-origin, cross-site", "SAME-ORIGIN"):
            self.assertEqual(self.rig.ask("POST", "/hub/a", dict(BEACON, **{"Sec-Fetch-Site": site}), EVENT)[0], 403, site)
        headers = dict(BEACON)
        del headers["Sec-Fetch-Site"]
        self.assertEqual(self.rig.ask("POST", "/hub/a", headers, EVENT)[0], 403, "no fetch metadata")
        self.assertEqual(self.count(), before)

    def test_a_body_of_1024_bytes_goes_through_and_one_of_1025_does_not(self):
        before = self.count()
        self.assertEqual(self.beacon(body=b"x" * 1024)[0], 200)
        self.assertEqual(len(self.newest()[3]), 1024)
        self.assertEqual(self.beacon(body=b"x" * 1025)[0], 413)
        self.assertEqual(self.count(), before + 1, "the big one was not passed on")

    def test_nothing_is_written_to_the_access_log(self):
        try:
            with open(self.rig.log_file, encoding="utf-8") as f:
                before = len(f.read())
        except OSError:
            before = 0
        self.beacon(body=b'{"t":"view","p":"/canary-page-2c9a"}')
        self.beacon(path="/hub/a?canary-query-9d31")
        self.beacon(headers={"User-Agent": "canary-agent-7e55"})
        with open(self.rig.log_file, encoding="utf-8") as f:
            text = f.read()[before:]
        self.assertNotIn("canary", text, "the door writes no line: not the path, not the query, not the browser (a path that is another location's, such as /hub/ab, is that location's own 404 line)")
        self.assertNotRegex(text, r'"POST /hub/a HTTP')


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: the allowance of the counter's door was NOT run (tests/scripts/test_nginx_hub_analytics.py)")
class TheAllowance(unittest.TestCase):
    def test_ten_a_second_with_a_burst_of_sixty_and_a_429_is_not_passed_on(self):
        stand, server = api.start_stand()
        rig = sub.Rig(hub_port=server.server_address[1], relaxed=False)
        try:
            codes = [rig.ask("POST", "/hub/a", BEACON, EVENT)[0] for _ in range(80)]
            self.assertEqual(codes[:61], [200] * 61)
            self.assertIn(429, codes[61:])
            self.assertEqual(len(stand.seen), codes.count(200))
        finally:
            rig.stop()
            server.shutdown()
            server.server_close()

    def test_a_refused_request_does_not_spend_the_allowance(self):
        stand, server = api.start_stand()
        rig = sub.Rig(hub_port=server.server_address[1], relaxed=False)
        try:
            for _ in range(80):
                self.assertEqual(rig.ask("GET", "/hub/a", BEACON)[0], 405)
                self.assertEqual(rig.ask("POST", "/hub/a", dict(BEACON, **{"Sec-Fetch-Site": "cross-site"}), EVENT)[0], 403)
            self.assertEqual(rig.ask("POST", "/hub/a", BEACON, EVENT)[0], 200)
            self.assertEqual(len(stand.seen), 1)
        finally:
            rig.stop()
            server.shutdown()
            server.server_close()


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: a hub that is not there was NOT run (tests/scripts/test_nginx_hub_analytics.py)")
class TheHubIsNotThere(unittest.TestCase):
    def test_a_hub_that_is_not_there_is_the_json_503_of_the_chat_door(self):
        rig = sub.Rig(hub_port=hl.DEAD_PORT)
        try:
            status, headers, answer = rig.ask("POST", "/hub/a", BEACON, EVENT)
            self.assertEqual(status, 503)
            self.assertEqual(json.loads(answer), {"error": "hub_off", "message": "Chat is not available right now."})
            self.assertEqual(sub.names(headers, "cache-control"), ["no-store"])
        finally:
            rig.stop()


if __name__ == "__main__":
    unittest.main()
