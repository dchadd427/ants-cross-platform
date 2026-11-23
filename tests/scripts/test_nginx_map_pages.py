#!/usr/bin/env python3
"""The routes of the map pages in docker/nginx.conf (/map/NAME, /map-library, the other names of a map; docs/MAP_PAGES.md): run by ./run_tests.sh --fast and by the CI where docker works;
tests/data/nginx_map_pages_mutants.json breaks one rule at a time, for tools/mutate.py.

The routes cannot be seen by reading the file (what a rewrite, an `if` and a map do with a query, which headers an answer ends with), so a real nginx (the image's own base, as test_nginx_routes.py)
runs the repository's file with the pages of a small library that tools/map_pages.py made (two names of one map, two maps with one address word, an original) and is asked: a page for each address that
names one, with the headers of a page and the query kept; a 301 for another name of a map, for a slash after an address and for the file a page is made of; a 404 with the page of the site for everything else.
What the file says is read where only the text can say it: the folder that the redirects are read from is the one that the Dockerfile fills, and a server with no such folder starts.
"""
import http.client
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.request
import uuid

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import map_pages_rig  # noqa: E402
import test_nginx_routes as routes  # noqa: E402  (docker and the image of the web server)
import test_nginx_stats as stats  # noqa: E402  (the helpers that read the file)

REPO = routes.REPO
CONF = routes.CONF
HEADERS = ("content-security-policy", "x-frame-options", "referrer-policy", "permissions-policy", "cross-origin-opener-policy", "cross-origin-embedder-policy", "x-content-type-options", "strict-transport-security",
           "cache-control")


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def small_library(tmp):
    """(library folder, originals folder): TREASURE.LVL; Alley Ants and AlleyAnts (one file under two names); Fire and Fire! (two maps, one address word); Newbie_Islands and Newbie Islands (one file)."""
    library, originals = os.path.join(tmp, "library"), os.path.join(tmp, "originals")
    write(os.path.join(originals, "TREASURE.LVL"), b"the original")
    maps = []
    for file, data in (("Alley Ants.lvl", b"a"), ("AlleyAnts.lvl", b"a"), ("Fire.lvl", b"f1"), ("Fire!.lvl", b"f2"), ("Newbie_Islands.lvl", b"n"), ("Newbie Islands.lvl", b"n")):
        write(os.path.join(library, file), data)
        maps.append('{"file": "%s", "size": 1, "hash": "0", "width": 31, "height": 31, "minutes": 12, "players": 4, "description": "x"}' % file)
    write(os.path.join(library, "maps.json"), ('{"format": 1, "maps": [%s]}' % ", ".join(maps)).encode())
    return library, originals


class TheFile(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.conf = stats.strip_comments(stats.read("docker", "nginx.conf"))

    def test_the_two_addresses_are_locations_that_rewrite_to_the_file_the_build_made_and_have_no_header_of_their_own(self):
        index = re.findall(r"location = /map-library \{([^{}]*)\}", self.conf)
        page = re.findall(r'location ~ "\^/map/\[a-z0-9\]\+\(\?:-\[a-z0-9\]\+\)\*\\z" \{([^{}]*)\}', self.conf)
        self.assertEqual([b.split() for b in index], [["rewrite", "^", "/map_pages/library/$ants_library_page.html", "last;"]])
        self.assertEqual([b.split() for b in page], [["rewrite", "^/map/([a-z0-9-]+)$", "/map_pages/map/$1.html", "last;"]])
        for header in ("add_header", "proxy_pass", "alias", "root"):
            self.assertNotIn(header, index[0] + page[0])                                        # (a location with an add_header drops the server's, and the file's address says the page's)

    def test_the_folder_that_the_other_names_are_read_from_is_the_one_the_image_fills(self):
        found = re.findall(r"include (/etc/nginx/ants-map-pages/\*\.map);", self.conf)
        self.assertEqual(len(found), 1)                                                         # (a glob: a server with no such folder, a test of this file, starts and moves no name)
        dockerfile = stats.read("Dockerfile")
        self.assertIn("/etc/nginx/ants-map-pages/", dockerfile)
        self.assertIn(os.path.dirname(found[0]) + "/", dockerfile)

    def test_the_other_names_are_a_permanent_redirect_that_keeps_the_query_and_come_before_any_page_is_served(self):
        server = self.conf[self.conf.index("server {"):]
        found = re.findall(r"if \(\$ants_map_redirect\) \{\s*return 301 \$ants_map_redirect\$is_args\$args;\s*\}", server)
        self.assertEqual(len(found), 1)
        self.assertLess(server.index("if ($ants_map_redirect)"), server.index("location = /catalog"))      # (a server-level `if`: it runs before any location)


@unittest.skipUnless(routes.shutil.which("docker"), "docker is not installed: the routes of the map pages were NOT run (tests/scripts/test_nginx_map_pages.py)")
class TheRoutesRun(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.why = routes.docker_ready()
        cls.name = ""
        if cls.why:
            return
        cls.tmp = tempfile.mkdtemp(prefix="ants_mp_routes.")
        html = os.path.join(cls.tmp, "html")
        for name in ("404.html", "50x.html", "matches.html"):
            with open(os.path.join(REPO, "web", name), "rb") as f:
                write(os.path.join(html, name), f.read())
        write(os.path.join(html, "front", "classic.css"), b":root { --clay: #db4b13; }\n")
        library, originals = small_library(cls.tmp)
        cls.moved = os.path.join(cls.tmp, "ants-map-pages")
        map_pages_rig.put_pages(html, cls.moved, library, originals)
        conf = os.path.join(cls.tmp, "default.conf")
        text = routes.read(CONF)
        if not os.path.exists("/proc/net/if_inet6"):
            text = text.replace("listen [::]:80;", "")
        write(conf, text.encode())
        cls.name = "ants-mp-" + uuid.uuid4().hex[:8]
        import socket
        s = socket.socket()
        s.bind(("127.0.0.1", 0))
        cls.port = s.getsockname()[1]
        s.close()
        ran = subprocess.run(["docker", "run", "-d", "--rm", "--name", cls.name, "-p", "127.0.0.1:%d:80" % cls.port, "-v", conf + ":/etc/nginx/conf.d/default.conf:ro", "-v", html + ":/usr/share/nginx/html:ro",
                              "-v", cls.moved + ":/etc/nginx/ants-map-pages:ro", routes.IMAGE], capture_output=True, text=True)
        if ran.returncode != 0:
            cls.why = "the container did not start: " + ran.stderr.strip()[:200]
            return
        for _ in range(100):
            try:
                urllib.request.urlopen("http://127.0.0.1:%d/matches" % cls.port, timeout=2).read()           # (a page that does not depend on the routes under test: a route that is broken must fail a test, not skip them all)
                return
            except Exception:
                time.sleep(0.2)
        cls.why = "nginx did not answer"

    @classmethod
    def tearDownClass(cls):
        if cls.name:
            subprocess.run(["docker", "rm", "-f", cls.name], capture_output=True)
        if getattr(cls, "tmp", None):
            shutil.rmtree(cls.tmp, ignore_errors=True)

    def setUp(self):
        if self.why:
            self.skipTest(self.why)

    def ask(self, path, method="GET", headers=None):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        try:
            conn.request(method, path, headers=headers or {})
            r = conn.getresponse()
            return r.status, {k.lower(): v for k, v in r.getheaders()}, r.read().decode("utf-8", "replace")
        finally:
            conn.close()

    def test_a_page_for_each_address_that_names_one_with_the_query_kept(self):
        for path in ("/map/treasure", "/map/alley-ants", "/map/fire", "/map/newbie-islands", "/map/treasure?utm_source=x&y=%20", "/map-library", "/map-library?page=1", "/map-library?page=1&x=y", "/map-library?page="):
            status, headers, body = self.ask(path)
            self.assertEqual(status, 200, path)
            self.assertTrue(headers["content-type"].startswith("text/html"), path)
            self.assertNotIn("Page not found", body, path)
        self.assertIn("<h1>Alley Ants", self.ask("/map/alley-ants")[2])
        self.assertIn('<link rel="canonical" href="https://site.example/map/alley-ants">', self.ask("/map/alley-ants?x=1")[2])
        self.assertIn("<h1>Maps</h1>", self.ask("/map-library?page=1")[2])

    def test_two_maps_with_one_word_have_two_addresses_and_neither_is_lost(self):
        library = self.ask("/map-library")[2]
        links = re.findall(r'<a href="(/map/fire[^"]*)">(Fire!?)</a>', library)
        self.assertEqual(len(links), 2)
        self.assertEqual(len({address for address, _ in links}), 2)
        for address, _name in links:
            self.assertEqual(self.ask(address)[0], 200, address)

    def test_a_page_has_the_headers_of_a_page_the_same_as_the_matches_page(self):
        reference = self.ask("/matches")[1]
        for path in ("/map/treasure", "/map-library"):
            status, headers, _ = self.ask(path)
            self.assertEqual(status, 200)
            for name in HEADERS:
                self.assertIn(name, headers, "%s %s" % (path, name))
                self.assertEqual(headers[name], reference[name], "%s %s" % (path, name))
            self.assertIn("etag", headers)
            self.assertEqual(self.ask(path, headers={"If-None-Match": headers["etag"]})[0], 304, path)

    def test_another_name_of_a_map_is_a_permanent_redirect_to_its_page_with_the_query_and_the_page_then_answers(self):
        for old, new in (("/map/alleyants", "/map/alley-ants"), ("/map/newbie-islands", None)):
            status, headers, body = self.ask(old + "?x=1&y=%20z")
            if new:
                self.assertEqual((status, headers["location"]), (301, new + "?x=1&y=%20z"), old)
                self.assertNotIn("content-security-policy", headers, old)
                self.assertEqual(self.ask(old, "HEAD")[0], 301)
                self.assertEqual(self.ask(new)[0], 200)
            else:
                self.assertEqual(status, 200, old)                                              # (the page's own word is never redirected, to itself or anywhere)
        with open(os.path.join(self.moved, "moved.map"), encoding="utf-8") as f:
            moved = f.read().split("\n")
        self.assertIn("/map/alleyants /map/alley-ants;", moved)
        self.assertNotIn("/map/newbie-islands /map/newbie-islands;", moved)

    def test_a_slash_after_an_address_and_the_file_a_page_is_made_of_go_to_the_clean_address(self):
        for old, new in (("/map/treasure/", "/map/treasure"), ("/map-library/", "/map-library"), ("/map-library/?page=2", "/map-library?page=2"), ("/map_pages/map/treasure.html", "/map/treasure"),
                         ("/map_pages/map/treasure.html?x=1", "/map/treasure?x=1"), ("/map_pages/library/1.html", "/map-library")):
            status, headers, _ = self.ask(old)
            self.assertEqual((status, headers["location"]), (301, new), old)
        for plain in ("/map_pages/", "/map_pages/map/", "/map_pages/library/", "/map_pages/library/0.html", "/map_pages/map/treasure.htm"):
            self.assertEqual(self.ask(plain)[0], 404, plain)                                    # (the folder is no address)

    def test_an_address_that_names_no_page_is_the_404_of_the_site(self):
        for path in ("/map/nosuchmap", "/map/Treasure", "/map/", "/map/a--b", "/map/-a", "/map/a-", "/map/treasure/x", "/map/" + "a" * 90, "/map/a_b", "/map/tre%20sure", "/map/%2e%2e", "/map/..%2f",
                     "/map-library?page=0", "/map-library?page=02", "/map-library?page=abc", "/map-library?page=-1", "/map-library?page=2.5", "/map-library?page=1000", "/map-library?page=9999", "/map-library/x",
                     "/map-library?page=../../matches", "/map-library?page=../404", "/map-library?page=1/../../matches", "/map-library?page=%2e%2e/%2e%2e/matches"):                      # (a page number is put into a file name: never a path)
            status, headers, body = self.ask(path)
            self.assertEqual(status, 404, path)
            self.assertIn("Page not found", body, path)
            self.assertNotIn("location", headers, path)

    def test_only_get_and_head_are_served(self):
        for path in ("/map/treasure", "/map-library"):
            self.assertEqual(self.ask(path, "HEAD")[0], 200, path)
            for method in ("POST", "PUT", "DELETE"):
                self.assertEqual(self.ask(path, method)[0], 405, "%s %s" % (method, path))

    def test_a_server_with_no_folder_of_other_names_starts_and_the_file_is_accepted(self):
        done = subprocess.run(["docker", "exec", self.name, "nginx", "-t"], capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        bare = tempfile.mkdtemp(prefix="ants_mp_bare.")
        try:
            text = routes.read(CONF)
            write(os.path.join(bare, "default.conf"), text.encode())
            once = subprocess.run(["docker", "run", "--rm", "-v", os.path.join(bare, "default.conf") + ":/etc/nginx/conf.d/default.conf:ro", routes.IMAGE, "nginx", "-t"], capture_output=True, text=True)
            self.assertEqual(once.returncode, 0, once.stdout + once.stderr)                     # (no folder at all: the glob matches nothing, which is no error)
        finally:
            shutil.rmtree(bare, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()
