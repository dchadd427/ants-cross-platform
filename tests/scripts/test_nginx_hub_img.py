#!/usr/bin/env python3
"""docker/nginx.conf routes the chat's images, POST /hub/img/up and GET /hub/img/<name>.<type> (run by ./run_tests.sh --fast and by the CI; docs/HUB_IMAGES.md, "How it is served").

The text of the file is checked where no run can see the defect (a regular expression looser than the names' shape, a static-image location that comes before it and takes a .png first, a policy header that
is missing, a page policy that grew a host), and where an nginx program is installed the two blocks are RUN, the real text in front of a stand-in hub that records what arrives and in front of a folder of files:

  THE FILES  a file goes through whole with the right type and each header once (nosniff, a policy that lets it run nothing, same-origin only, no referrer, an hour in a browser, and the server's own);
             only a GET or HEAD of the exact shape without a query is served (anything else is 405, 404 or not passed on at all); another site is 403; a file that is deleted is a 404 at once (there is no cache
             of ours that could hold it); a burst of 240 and then twenty a second for an address; no log holds a name; the hub is never asked
  THE UPLOAD only a POST without a query from the same site is passed on; 413 from the length alone, before the hub is asked; nothing of the visitor's goes with it but the login's cookie (picked out of the whole
             header), Origin, Sec-Fetch-Site, the page's header, the type and the length of the body, and the address; the body goes through whole; a burst and then a pace for an address; a hub that is not
             there is the JSON 503 of the chat's door
"""
import http.server
import os
import re
import shutil
import signal
import socket
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
import quick_http  # noqa: E402
import test_nginx_stats as stats  # noqa: E402  (the readers of the file)

CONF = stats.strip_comments(stats.read("docker", "nginx.conf"))
HEAD = CONF[:CONF.index("server {")]
SERVER = CONF[CONF.index("server {"):]
FILES_AT = SERVER.index('location ~ "^/hub/img/')
UP_AT = SERVER.index("location = /hub/img/up {")


def block_after(start):
    """(the header up to its brace, the text between the braces) of the block that starts at `start`."""
    open_at = SERVER.index("{", start)
    if SERVER[start:open_at].count('"') % 2:                           # (a brace inside the quoted regular expression)
        open_at = SERVER.index("{", SERVER.index('" {', start))
    depth = 0
    for i in range(open_at, len(SERVER)):
        depth += {"{": 1, "}": -1}.get(SERVER[i], 0)
        if depth == 0:
            return SERVER[start:open_at], SERVER[open_at + 1:i]
    raise AssertionError("an unclosed block")


FILES_HEADER, FILES = block_after(FILES_AT)
UP_HEADER, UP = block_after(UP_AT)
SECURITY = ("Cross-Origin-Opener-Policy", "Cross-Origin-Embedder-Policy", "X-Content-Type-Options", "Strict-Transport-Security")
NAME = "0123456789abcdef0123456789abcdef"
JPEG = b"\xff\xd8\xff\xe0made-up jpeg\xff\xd9"
PNG = b"\x89PNG\r\n\x1a\nmade-up png IEND\xaeB`\x82"
GIF = b"GIF89amade-up gif;"


class TheText(unittest.TestCase):
    def test_the_files_door_is_one_regular_expression_of_the_exact_shape_after_the_avatars_and_before_the_static_images(self):
        here = 'location ~ "^/hub/img/(?<ants_img>[0-9a-f]+(?:\\.t)?\\.(?:jpg|png|gif))\\z" {'
        self.assertEqual(SERVER.count(here), 1)
        self.assertEqual(SERVER.count("/hub/img/"), 2, "the two doors and nothing else name the folder")
        self.assertLess(SERVER.index('location ~ "^/hub/pic/'), SERVER.index(here), "the avatars' door stays the first regular expression of the file")
        self.assertLess(SERVER.index(here), SERVER.index("location ~* \\.(png"))
        self.assertEqual(SERVER.index("location ~"), SERVER.index('location ~ "^/hub/pic/'))

    def test_the_files_are_read_from_the_volume_only_by_the_shape_and_nothing_is_passed_to_the_hub(self):
        self.assertIn("alias /srv/hub-img/$ants_img;", FILES)
        self.assertNotIn("proxy_pass", FILES)
        self.assertNotIn("proxy_cache", FILES)
        self.assertNotIn("autoindex on", FILES)
        for check in ('if ($request_method !~ "^(GET|HEAD)\\z") { return 405; }', 'if ($is_args) { return 404; }', 'if ($ants_img !~ "^[0-9a-f]{32}(?:\\.t)?\\.(?:jpg|png|gif)\\z") { return 404; }', 'if ($http_sec_fetch_site ~ "^(cross-site|same-site)$") { return 403; }', "access_log off;",
                      "limit_req zone=ants_hub_img burst=240 nodelay;", "limit_req_status 429;"):
            self.assertIn(check, FILES)
            self.assertLess(FILES.index(check), FILES.index("alias "), check)

    def test_the_type_is_from_the_extension_and_the_headers_are_the_ones_that_make_a_file_run_nothing(self):
        self.assertIn("types { image/jpeg jpg; image/png png; image/gif gif; }", FILES)
        for header in ('Content-Security-Policy "default-src \'none\'; sandbox" always', 'Cross-Origin-Resource-Policy "same-origin" always', 'Referrer-Policy "no-referrer" always',
                       'X-Content-Type-Options "nosniff" always', 'Cache-Control "private, max-age=3600"'):
            self.assertIn("add_header %s;" % header, FILES, header)
        self.assertNotIn("public", FILES)
        for name in SECURITY:                                            # (a location that sets a header does not inherit the server's: its four are repeated)
            self.assertEqual(len(re.findall(r"add_header %s " % name, FILES)), 1, name)
        for bad in ("image/svg", "text/html", "text/xml", "application/xhtml", "default_type text", "image/webp"):
            self.assertNotIn(bad, FILES)

    def test_the_allowances_are_twenty_a_second_for_the_files_and_six_a_minute_for_the_upload(self):
        files = re.search(r"limit_req_zone \$binary_remote_addr zone=ants_hub_img:(\d+)m rate=(\d+)r/([sm]);", HEAD)
        up = re.search(r"limit_req_zone \$binary_remote_addr zone=ants_hub_imgup:(\d+)m rate=(\d+)r/([sm]);", HEAD)
        self.assertEqual((int(files.group(1)), int(files.group(2)), files.group(3)), (1, 20, "s"))
        self.assertEqual((int(up.group(1)), int(up.group(2)), up.group(3)), (1, 6, "m"))
        self.assertIn("limit_conn_zone $binary_remote_addr zone=ants_hub_imgconn:1m;", HEAD)
        self.assertIn("limit_req zone=ants_hub_imgup burst=3 nodelay;", UP)
        self.assertIn("limit_conn ants_hub_imgconn 2;", UP)

    def test_the_upload_checks_come_first_and_only_the_named_headers_are_passed_on(self):
        for check in ('if ($request_method != POST) { return 405; }', 'if ($is_args) { return 404; }', 'if ($http_sec_fetch_site != "same-origin") { return 403; }', "limit_req ", "client_max_body_size 4m;"):
            self.assertIn(check, UP)
            self.assertLess(UP.index(check), UP.index("proxy_pass "), check)
        self.assertIn("access_log off;", UP)
        self.assertIn("proxy_pass_request_headers off;", UP)
        self.assertIn("proxy_request_buffering off;", UP)
        self.assertNotIn("proxy_pass_request_body off", UP)
        self.assertEqual(sorted(re.findall(r"proxy_set_header ([A-Za-z-]+) ", UP)),
                         ["Connection", "Content-Length", "Content-Type", "Cookie", "Host", "Origin", "Sec-Fetch-Site", "X-Ants-Image", "X-Client-Address"])
        self.assertIn("proxy_set_header Cookie $hub_cookie_session;", UP)
        self.assertNotIn("http_cookie", UP)
        self.assertNotIn("http_authorization", UP)
        self.assertIn("proxy_set_header X-Client-Address $remote_addr;", UP)
        self.assertIn("set $ants_hub ants-hub:4040;", UP)
        self.assertIn("error_page 502 504 = @hub_off;", UP)
        self.assertIn("proxy_hide_header X-Content-Type-Options;", UP)

    def test_the_admin_page_may_ask_the_hub_for_the_list_and_for_the_two_changes_and_for_nothing_new_else(self):
        self.assertIn("imagesoff", HEAD)
        self.assertRegex(HEAD, r"\^GET /hub/admin/api/\(\?:me\|bans\|mutes\|hidden\|imagesoff\|modlog\|status\)")
        self.assertRegex(HEAD, r"\^POST /hub/admin/api/\(\?:ban\|unban\|mute\|unmute\|nopic\|pic\|noimages\|images\|del\)")

    def test_the_page_policy_still_lets_a_same_origin_image_in_and_names_no_other_host_for_it(self):
        self.assertIn("img-src 'self' data: blob: https://cdn.buymeacoffee.com;", HEAD)


def nginx_binary():
    return shutil.which("nginx") or ("/usr/sbin/nginx" if os.path.exists("/usr/sbin/nginx") else "")


free_port = quick_http.free_port


class Stand(quick_http.QuickThreadingHTTPServer):
    """A hub for the upload's tests: it records every request that reaches it (the method, the path, the headers and the whole body) and answers 200."""
    daemon_threads = True

    def __init__(self):
        self.seen = []
        stand = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_ANY(self):
                length = int(self.headers.get("Content-Length", "0") or 0)
                body = self.rfile.read(length) if length else b""
                stand.seen.append((self.command, self.path, dict((k.lower(), v) for k, v in self.headers.items()), body))
                answer = b'{"id":"made-up"}'
                self.send_response(200)
                for k, v in (("Content-Type", "application/json"), ("Cache-Control", "no-store"), ("X-Content-Type-Options", "nosniff"), ("Content-Length", str(len(answer)))):
                    self.send_header(k, v)
                self.end_headers()
                self.wfile.write(answer)

            do_GET = do_POST = do_ANY

            def log_message(self, *args):
                pass

        super().__init__(("127.0.0.1", 0), Handler)
        threading.Thread(target=self.serve_forever, daemon=True).start()


def mini_conf(tmp, port, hub_port, images):
    """The real text of the zones, the cookie's map, the two blocks and the server's own security headers in a small server; the hub's name is this machine's port, the volume is a folder."""
    zones = "\n  ".join(m.group(0) for m in re.finditer(r"(?:limit_req_zone [^;]*zone=ants_hub_img(?:up)?:[^;]+|limit_conn_zone [^;]*zone=ants_hub_imgconn:[^;]+);", HEAD))
    cookie = stats.block_of(HEAD, "map $http_cookie $hub_cookie_session ")
    headers = "\n    ".join(m.group(0) for m in re.finditer(r"^\s*add_header (?:%s) [^;]+;" % "|".join(SECURITY), SERVER[:SERVER.index("location")], re.M))
    files = FILES.replace("/srv/hub-img/", images + "/")
    up = UP.replace("resolver 127.0.0.11 valid=10s ipv6=off;", "").replace("set $ants_hub ants-hub:4040;", "set $ants_hub 127.0.0.1:%d;" % hub_port)
    off = "location @hub_off {%s}" % stats.block_of(CONF, "location @hub_off ")
    return ("daemon off;\nmaster_process off;\npid %(t)s/n%(p)d.pid;\nerror_log %(t)s/e%(p)d.log error;\nevents { worker_connections 1024; }\nhttp {\n  access_log %(t)s/a%(p)d.log;\n"
            "  client_body_temp_path %(t)s/b%(p)d;\n  proxy_temp_path %(t)s/p%(p)d;\n  %(zones)s\n  map $http_cookie $hub_cookie_session {%(cookie)s}\n  server {\n    listen 127.0.0.1:%(p)d;\n    %(headers)s\n"
            "    %(fh)s{%(files)s}\n    %(uh)s{%(up)s}\n    %(off)s\n    location / { return 404 'not passed on'; }\n  }\n}\n"
            % {"t": tmp, "p": port, "zones": zones, "cookie": cookie, "headers": headers, "fh": FILES_HEADER, "files": files, "uh": UP_HEADER, "up": up, "off": off})


class Nginx:
    def __init__(self, tmp, hub_port, images):
        self.port, self.tmp = free_port(), tmp
        conf = os.path.join(tmp, "nginx-%d.conf" % self.port)
        with open(conf, "w", encoding="utf-8") as f:
            f.write(mini_conf(tmp, self.port, hub_port, images))
        self.proc = subprocess.Popen([nginx_binary(), "-c", conf, "-p", tmp], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        for _ in range(100):
            try:
                socket.create_connection(("127.0.0.1", self.port), timeout=0.5).close()
                return
            except OSError:
                if self.proc.poll() is not None:
                    err = self.proc.stderr.read().decode(errors="replace")[:400]
                    self.proc.stderr.close()
                    raise unittest.SkipTest("nginx would not start here: " + err)
                time.sleep(0.05)
        self.stop()
        raise unittest.SkipTest("nginx did not come up")

    def stop(self):
        self.proc.send_signal(signal.SIGTERM)
        try:
            self.proc.wait(5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
        self.proc.stderr.close()

    def ask(self, method, path, headers=(), body=b""):
        return hc.request(self.port, method, path, list(headers), body=body)


def names(response, name):
    return [v for k, v in response.headers if k == name]


@unittest.skipUnless(nginx_binary(), "no nginx program on this machine")
class TheFilesRun(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="ants_hub_img_nginx.")
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        self.images = os.path.join(self.tmp, "images")
        os.makedirs(self.images)
        for name, data in ((NAME + ".jpg", JPEG), (NAME + ".t.jpg", JPEG), ("1" * 32 + ".png", PNG), ("2" * 32 + ".gif", GIF)):
            with open(os.path.join(self.images, name), "wb") as f:
                f.write(data)
        self.stand = Stand()
        self.addCleanup(self.stand.server_close)
        self.addCleanup(self.stand.shutdown)
        self.nginx = Nginx(self.tmp, self.stand.server_address[1], self.images)
        self.addCleanup(self.nginx.stop)

    def test_a_file_goes_through_whole_with_its_type_and_each_header_once(self):
        for path, body, kind in (("/hub/img/%s.jpg" % NAME, JPEG, "image/jpeg"), ("/hub/img/%s.t.jpg" % NAME, JPEG, "image/jpeg"), ("/hub/img/%s.png" % ("1" * 32), PNG, "image/png"),
                                 ("/hub/img/%s.gif" % ("2" * 32), GIF, "image/gif")):
            r = self.nginx.ask("GET", path)
            self.assertEqual((r.status, r.body), (200, body), path)
            wanted = {"content-type": kind, "x-content-type-options": "nosniff", "content-security-policy": "default-src 'none'; sandbox", "cross-origin-resource-policy": "same-origin",
                      "referrer-policy": "no-referrer", "cache-control": "private, max-age=3600", "cross-origin-opener-policy": "same-origin", "cross-origin-embedder-policy": "require-corp",
                      "strict-transport-security": "max-age=604800"}
            for name, value in wanted.items():
                self.assertEqual(names(r, name), [value], "%s %s" % (path, name))
            self.assertEqual(names(r, "set-cookie") + names(r, "access-control-allow-origin"), [])
        self.assertEqual(self.stand.seen, [], "the hub is never asked")

    def test_a_head_is_answered_and_nothing_else_than_a_get_or_a_head_is(self):
        path = "/hub/img/%s.jpg" % NAME
        self.assertEqual(self.nginx.ask("HEAD", path).status, 200)
        for method in ("POST", "PUT", "DELETE", "PATCH"):
            self.assertEqual(self.nginx.ask(method, path).status, 405, method)

    def test_only_the_exact_shape_without_a_query_is_a_file_and_everything_else_is_not_passed_on(self):
        self.assertEqual(self.nginx.ask("GET", "/hub/img/%s.jpg?x=1" % NAME).status, 404)
        for path in ("/hub/img/%s.jpg" % NAME[:31], "/hub/img/%s0.jpg" % NAME, "/hub/img/%s.JPG" % NAME, "/hub/img/%s" % NAME, "/hub/img/%s.jpg/" % NAME, "/hub/img/%s.webp" % NAME,
                     "/hub/img/%s.svg" % NAME, "/hub/img/%s.html" % NAME, "/hub/img/%s.t.png" % NAME, "/hub/img/%s.tt.jpg" % NAME, "/hub/img/x/%s.jpg" % NAME, "/hub/img/%%2e%s.jpg" % NAME[:31],
                     "/hub/img/../%s.jpg" % NAME, "/hub/img/", "/hub/img", "/hub/img/%s.jpg%%00" % NAME, "/hub/img/up/%s.jpg" % NAME):
            r = self.nginx.ask("GET", path)
            self.assertIn(r.status, (400, 404, 405), path)
            self.assertNotEqual(r.body, JPEG, path)
        self.assertEqual(self.stand.seen, [])

    def test_a_name_that_is_not_there_is_a_404_and_a_deleted_file_is_a_404_at_once(self):
        path = "/hub/img/%s.jpg" % NAME
        self.assertEqual(self.nginx.ask("GET", path).status, 200)
        os.unlink(os.path.join(self.images, NAME + ".jpg"))
        self.assertEqual(self.nginx.ask("GET", path).status, 404)
        self.assertEqual(self.nginx.ask("GET", "/hub/img/%s.jpg" % ("f" * 32)).status, 404)
        self.assertEqual(names(self.nginx.ask("GET", "/hub/img/%s.jpg" % ("f" * 32)), "cache-control"), [])

    def test_a_request_of_another_site_is_refused_before_the_disk_is_asked(self):
        path = "/hub/img/%s.jpg" % NAME
        for site in ("cross-site", "same-site"):
            self.assertEqual(self.nginx.ask("GET", path, [("Sec-Fetch-Site", site)]).status, 403, site)
        for site in ("same-origin", "none"):
            self.assertEqual(self.nginx.ask("GET", path, [("Sec-Fetch-Site", site)]).status, 200, site)

    def test_an_address_has_a_burst_and_then_a_pace(self):
        statuses = [self.nginx.ask("GET", "/hub/img/%s.jpg" % ("%032x" % n)).status for n in range(300)]
        self.assertEqual(statuses[:240], [404] * 240)
        self.assertIn(429, statuses[240:])

    def test_no_log_of_nginx_has_a_name(self):
        self.nginx.ask("GET", "/hub/img/%s.jpg" % NAME)
        time.sleep(0.2)
        for name in ("a%d.log", "e%d.log"):
            try:
                with open(os.path.join(self.tmp, name % self.nginx.port), encoding="utf-8") as f:
                    text = f.read()
            except OSError:
                text = ""
            self.assertNotIn(NAME, text)


@unittest.skipUnless(nginx_binary(), "no nginx program on this machine")
class TheUploadRuns(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="ants_hub_imgup_nginx.")
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        os.makedirs(os.path.join(self.tmp, "images"))
        self.stand = Stand()
        self.addCleanup(self.stand.server_close)
        self.addCleanup(self.stand.shutdown)
        self.nginx = Nginx(self.tmp, self.stand.server_address[1], os.path.join(self.tmp, "images"))
        self.addCleanup(self.nginx.stop)
        self.good = [("Sec-Fetch-Site", "same-origin"), ("Origin", "https://chat.example.org"), ("Content-Type", "application/octet-stream"), ("X-Ants-Image", "1"),
                     ("Cookie", "__Host-ants_hub=%s; ants_other=secret-1; other=2" % ("a" * 32))]

    def test_a_good_upload_goes_through_whole_with_only_the_named_headers(self):
        body = PNG * 100
        r = self.nginx.ask("POST", "/hub/img/up", self.good + [("Authorization", "Bearer canary-token"), ("Referer", "https://elsewhere.example/p"), ("X-Forwarded-For", "203.0.113.98"),
                                                                 ("X-Client-Address", "203.0.113.99"), ("User-Agent", "canary-agent"), ("Accept-Language", "de")], body=body)
        self.assertEqual(r.status, 200)
        (method, path, headers, got), = self.stand.seen
        self.assertEqual((method, path, got), ("POST", "/hub/img/up", body))
        self.assertEqual(sorted(headers), ["connection", "content-length", "content-type", "cookie", "host", "origin", "sec-fetch-site", "x-ants-image", "x-client-address"])
        self.assertEqual(headers["cookie"], "__Host-ants_hub=" + "a" * 32, "the one cookie of the login, picked out of the whole header")
        self.assertEqual(headers["x-client-address"], "127.0.0.1", "nginx's own view of the peer")
        self.assertEqual((headers["content-type"], headers["x-ants-image"], headers["origin"], headers["sec-fetch-site"]), ("application/octet-stream", "1", "https://chat.example.org", "same-origin"))
        self.assertNotIn("canary", str(headers))
        self.assertNotIn("secret-1", str(headers))

    def test_only_a_post_without_a_query_from_the_same_site_is_passed_on(self):
        for method in ("GET", "PUT", "DELETE", "HEAD"):
            self.assertEqual(self.nginx.ask(method, "/hub/img/up", self.good).status, 405, method)
        self.assertEqual(self.nginx.ask("POST", "/hub/img/up?x=1", self.good, body=PNG).status, 404)
        for site in ("cross-site", "same-site", "none"):
            self.assertEqual(self.nginx.ask("POST", "/hub/img/up", [h for h in self.good if h[0] != "Sec-Fetch-Site"] + [("Sec-Fetch-Site", site)], body=PNG).status, 403, site)
        self.assertEqual(self.nginx.ask("POST", "/hub/img/up", [h for h in self.good if h[0] != "Sec-Fetch-Site"], body=PNG).status, 403, "no header at all")
        self.assertEqual(self.stand.seen, [])

    def test_a_body_over_4_mb_is_413_from_its_length_and_the_hub_is_not_asked(self):
        r = self.nginx.ask("POST", "/hub/img/up", self.good, body=b"x" * (4 * 1024 * 1024 + 1))
        self.assertEqual(r.status, 413)
        self.assertEqual(self.stand.seen, [])
        r = self.nginx.ask("POST", "/hub/img/up", self.good, body=b"\x89PNG" + b"x" * (4 * 1024 * 1024 - 4))
        self.assertEqual(r.status, 200, "4 MiB exactly passes")

    def test_an_address_has_a_burst_of_four_and_then_a_pace(self):
        statuses = [self.nginx.ask("POST", "/hub/img/up", self.good, body=PNG).status for _ in range(8)]
        self.assertEqual(statuses[:4], [200] * 4)
        self.assertIn(429, statuses[4:])
        self.assertEqual(len(self.stand.seen), statuses.count(200))

    def test_a_hub_that_is_not_there_is_the_json_503_of_the_chats_door(self):
        dead = Nginx(self.tmp, 1, os.path.join(self.tmp, "images"))
        self.addCleanup(dead.stop)
        r = dead.ask("POST", "/hub/img/up", self.good, body=PNG)
        self.assertEqual((r.status, r.json()["error"]), (503, "hub_off"))


if __name__ == "__main__":
    unittest.main()
