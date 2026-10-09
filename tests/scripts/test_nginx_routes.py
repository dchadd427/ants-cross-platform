#!/usr/bin/env python3
"""The routes of docker/nginx.conf, run for real (run by ./run_tests.sh --fast and by the CI where docker works).

The front page and the game page share the address "/" (the owner: the Play online page is the front page, which is the lobby now; every address that the game
page ever had keeps opening it): "/" is the lobby unless the query has join=... or embed=1, /four.html is a permanent redirect to "/" with the same query, /play.html is the game page. The
rules cannot be seen by reading the file (what a `rewrite` or an `if` does with a query string, which headers an answer ends with), so this starts the real nginx of the image's own base
(nginx:alpine) with the repository's docker/nginx.conf and two stand-in pages that carry the markers of the real ones, and asks it, with tests/scripts/web_routes_check.py (which the CI also
runs against the real web image; the stand-in docroot also has the Classic look's stylesheet and font and the three other pages of the site). Skipped, with the reason, where docker or the nginx
image is not available. Static checks of the file's text run everywhere.
"""
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

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CONF = os.path.join(REPO, "docker", "nginx.conf")
CHECK = os.path.join(REPO, "tests", "scripts", "web_routes_check.py")
IMAGE = "nginx:alpine"

LOBBY_PAGE = '<!DOCTYPE html><html><head><title>Ants (1998)</title></head><body><main id="lobby"></main><p>the stand-in lobby</p></body></html>\n'
CLASSIC_PAGE = '<!DOCTYPE html><html><head><title>a stand-in page</title><link rel="stylesheet" href="/front/classic.css"></head><body><p>a stand-in page of the site</p></body></html>\n'
WATCH_PAGE = '<!DOCTYPE html><html><head><title>Watch matches - stand-in</title></head><body><div id="list-body"></div></body></html>\n'
GAME_PAGE = '<!DOCTYPE html><html><head><title>Ants (1998) - game</title></head><body><div id="game-stage" data-aspect="16:9"></div><p>the stand-in game page</p></body></html>\n'


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def docker_ready():
    if not shutil.which("docker"):
        return "docker is not installed"
    done = subprocess.run(["docker", "info", "--format", "{{.ServerVersion}}"], capture_output=True, text=True)
    if done.returncode != 0:
        return "the docker daemon does not answer"
    have = subprocess.run(["docker", "image", "inspect", IMAGE], capture_output=True, text=True)
    if have.returncode != 0:
        pulled = subprocess.run(["docker", "pull", "--quiet", IMAGE], capture_output=True, text=True)
        if pulled.returncode != 0:
            return "the image %s is not here and cannot be pulled" % IMAGE
    return ""


class TheFile(unittest.TestCase):
    """What the file says (the routes' own behaviour is run below)."""

    @classmethod
    def setUpClass(cls):
        cls.conf = "\n".join(line.split("#", 1)[0] for line in read(CONF).splitlines())

    def test_the_front_page_is_the_lobby_unless_the_address_is_a_game_s(self):
        self.assertRegex(self.conf, r"location = / \{")
        self.assertIn("set $ants_front /lobby.html;", self.conf)
        self.assertIn('if ($arg_join != "") { set $ants_front /index.html; }', self.conf)
        self.assertIn('if ($arg_embed = "1") { set $ants_front /index.html; }', self.conf)
        self.assertIn("rewrite ^ $ants_front last;", self.conf)

    def test_the_old_play_online_address_redirects_for_good_with_its_query(self):
        self.assertRegex(self.conf, r"location = /four\.html \{\s*return 301 /\$is_args\$args;\s*\}")

    def test_the_game_page_has_no_route_of_its_own_it_is_a_file_play_html(self):
        self.assertNotIn("location = /play.html", self.conf)                               # (a file of the image: index.html copied, see the Dockerfile)
        dockerfile = read(os.path.join(REPO, "Dockerfile"))
        self.assertIn("play.html", dockerfile)

    def test_the_numbers_of_the_front_page_have_their_two_exact_locations(self):
        locations = re.findall(r"^\s*location\s+(.*?)\s*\{", self.conf, re.M)
        self.assertEqual([l for l in locations if "stats" in l], ["= /stats", "= /stats/local"])          # (their blocks are checked in test_nginx_stats.py, run against a stand-in server there)

    def test_no_other_location_could_catch_the_front_page_or_the_old_address(self):
        locations = re.findall(r"^\s*location\s+(.*?)\s*\{", self.conf, re.M)
        self.assertEqual([l for l in locations if l in ("= /", "= /four.html")], ["= /", "= /four.html"])
        self.assertEqual([l for l in locations if "four" in l], ["= /four.html"])


@unittest.skipUnless(shutil.which("docker"), "docker is not installed: the routes of docker/nginx.conf were NOT run (tests/scripts/web_routes_check.py)")
class TheRoutesRun(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.why = docker_ready()
        cls.name = ""
        if cls.why:
            return
        cls.tmp = tempfile.mkdtemp(prefix="ants_routes.")
        html = os.path.join(cls.tmp, "html")
        os.makedirs(html)
        os.makedirs(os.path.join(html, "front"))
        os.makedirs(os.path.join(html, "asset_catalog"))
        for name, text in (("lobby.html", LOBBY_PAGE), ("watch.html", WATCH_PAGE), ("replay_page.js", "window.AntsReplay = {};\n"), ("index.html", GAME_PAGE), ("play.html", GAME_PAGE), ("changelog.html", CLASSIC_PAGE), ("changelog_archive.html", CLASSIC_PAGE),
                           (os.path.join("asset_catalog", "index.html"), CLASSIC_PAGE), (os.path.join("front", "classic.css"), ":root { --clay: #db4b13; }\n")):
            with open(os.path.join(html, name), "w", encoding="utf-8") as f:
                f.write(text)
        with open(os.path.join(html, "front", "LibreFranklin-Medium.ttf"), "wb") as f:
            f.write(b"\x00\x01\x00\x00 a stand-in for the font")
        conf = os.path.join(cls.tmp, "default.conf")
        text = read(CONF)
        if not os.path.exists("/proc/net/if_inet6"):                                       # a kernel without IPv6 cannot listen on [::]: the rest of the file is the one under test
            text = text.replace("listen [::]:80;", "")
        with open(conf, "w", encoding="utf-8") as f:
            f.write(text)
        cls.name = "ants-routes-" + uuid.uuid4().hex[:8]
        import socket
        s = socket.socket()
        s.bind(("127.0.0.1", 0))
        cls.port = s.getsockname()[1]
        s.close()
        ran = subprocess.run(["docker", "run", "-d", "--rm", "--name", cls.name, "-p", "127.0.0.1:%d:80" % cls.port, "-v", conf + ":/etc/nginx/conf.d/default.conf:ro",
                              "-v", html + ":/usr/share/nginx/html:ro", IMAGE], capture_output=True, text=True)
        if ran.returncode != 0:
            cls.why = "the container did not start: " + ran.stderr.strip()[:200]
            return
        for _ in range(100):
            try:
                urllib.request.urlopen("http://127.0.0.1:%d/lobby.html" % cls.port, timeout=2).read()
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

    def test_the_configuration_is_accepted_by_nginx(self):
        done = subprocess.run(["docker", "exec", self.name, "nginx", "-t"], capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)

    def test_every_route_and_its_headers(self):
        done = subprocess.run([sys.executable, CHECK, "--web", "http://127.0.0.1:%d/" % self.port], capture_output=True, text=True, timeout=300)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn(" 0 failed", done.stdout)


if __name__ == "__main__":
    unittest.main()
