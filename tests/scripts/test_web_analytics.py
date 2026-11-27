#!/usr/bin/env python3
"""The counter's script and the Analytics tab of the admin page (web/analytics.js, web/admin_analytics.js, web/admin_analytics.css; docs/ANALYTICS.md): run by ./run_tests.sh --fast and by the CI.

  - the code RUNS in node: the counter's script on a fake page (tests/scripts/web_analytics_check.js: what it sends and when, a browser that asked not to be tracked, a frame, the admin page, what it never
    touches) and the tab on a fake page with a fake hub (tests/scripts/web_admin_analytics_check.js: the question and the range, the three views, the charts, the files of the export, text from a visitor's
    browser put in as text and marked as data); the tab inside the whole admin page is web_admin_check.js's;
  - the files, where no run of the code can look: every public page loads the counter's script once, after the update notice, and the admin page never does; the image and the local web build ship the files;
    the scripts are plain ES5, make no markup from a text, touch no cookie and no storage, and the counter's script is within its byte budget; the tab's style is plain CSS with a forced-colors block;
  - tests/data/web_analytics_mutants.json changes one thing at a time in a COPY of the files: the node checks must then fail (the last class of this file; tools/mutate.py runs the same list).
The hub's side (the door, what is kept, the day's secret, the numbers) is test_hub_analytics.py's, nginx's door is test_nginx_hub_analytics.py's.
"""
import gzip
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools"))
import changelog_to_html  # noqa: E402
import size_limits  # noqa: E402

NODE = shutil.which("node")
CLIENT = os.path.join(REPO, "web", "analytics.js")
TAB = os.path.join(REPO, "web", "admin_analytics.js")
CLIENT_CHECK = os.path.join(REPO, "tests", "scripts", "web_analytics_check.js")
TAB_CHECK = os.path.join(REPO, "tests", "scripts", "web_admin_analytics_check.js")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def changelog_page():
    return changelog_to_html.convert(read("CHANGELOG.md"), changelog_to_html.DEFAULT_REPO_BLOB, version="v0.0.0", build_id="0000000")


# the public pages: every address of the site that carries the update notice, except the admin page
PUBLIC = {"lobby": read("web", "lobby.html"), "game": read("web", "shell.html"), "matches": read("web", "matches.html"), "match": read("web", "match.html"), "legal": read("web", "legal.html"),
          "mapmaker": read("web", "mapmaker.html"), "map page": read("web", "map_pages", "page.html"), "changelog": changelog_page()}


class ThePages(unittest.TestCase):
    def test_every_public_page_loads_the_script_once_and_after_the_update_notice(self):
        for name, page in PUBLIC.items():
            tags = re.findall(r'<script src="(/?(?:update_notice|analytics)\.js)"[^>]*></script>', page)
            self.assertEqual([t.lstrip("/") for t in tags], ["update_notice.js", "analytics.js"], name)
            self.assertEqual(len(re.findall(r"analytics\.js", page)), 1, name)

    def test_the_admin_page_does_not_count_itself_and_loads_the_tab_before_the_page(self):
        page = read("web", "admin.html")
        self.assertNotIn("analytics.js\"", page.replace("admin_analytics.js", ""))
        self.assertLess(page.index("admin_analytics.js"), page.index("admin_page.js"))
        self.assertIn('<link rel="stylesheet" href="admin_analytics.css">', page)

    def test_the_game_page_tells_how_long_the_game_took_once_and_only_when_the_counter_is_there(self):
        page = read("web", "shell.html")
        self.assertEqual(page.count("AntsAnalytics.game("), 1)
        self.assertIn("if (window.AntsAnalytics && window.performance && performance.now) window.AntsAnalytics.game(performance.now());", page)

    def test_the_image_and_the_local_build_ship_the_files(self):
        dockerfile, build = read("Dockerfile"), read("build_web.sh")
        for name in ("analytics.js", "admin_analytics.js", "admin_analytics.css"):
            self.assertIn("COPY web/%s /usr/share/nginx/html/%s" % (name, name), dockerfile)
            self.assertIn("web/%s" % name, build)
        self.assertIn("analytics.js", read(".github", "workflows", "ci.yml"), "the CI's list of the image's files")


class TheFiles(unittest.TestCase):
    def test_the_counters_script_is_small_plain_ES5_and_touches_nothing_of_the_visitors_browser(self):
        script = read("web", "analytics.js")
        for bad in ("=>", "let ", "const ", "`", "class ", "async ", "await ", "innerHTML", "document.write", "eval(", "new Function", "XMLHttpRequest", "fetch(", "new Image", "cookie", "localStorage", "sessionStorage",
                    "indexedDB", "openDatabase", "caches", "serviceWorker", "getImageData", "toDataURL", "AudioContext", "WebGL", "userAgent", "plugins", "fonts", "deviceMemory", "hardwareConcurrency"):
            self.assertNotIn(bad, re.sub(r"/\*.*?\*/", "", script, flags=re.S), bad)
        self.assertEqual(re.findall(r"""['"](/[A-Za-z][^'"\s]*)""", script), ["/hub/a"], "the one address that it sends to")
        raw = script.encode("utf-8")
        self.assertLess(len(raw), size_limits.ANALYTICS_BUDGET, "%d bytes" % len(raw))
        self.assertLess(len(gzip.compress(raw, 9)), size_limits.ANALYTICS_GZIP_BUDGET, "%d bytes gzipped" % len(gzip.compress(raw, 9)))

    def test_the_tab_makes_no_markup_from_a_text_and_loads_nothing(self):
        script = re.sub(r"(?m)^\s*//.*|\s//.*", "", read("web", "admin_analytics.js"))                  # (a comment may name them; the code may not)
        for bad in ("innerHTML", "outerHTML", "insertAdjacentHTML", "document.write", "eval(", "new Function", "localStorage", "sessionStorage", "document.cookie", "location.href", "XMLHttpRequest", "=>", "`"):
            self.assertNotIn(bad, script, bad)
        self.assertEqual(re.findall(r"""['"](/(?:hub|admin)[^'"\s]*)""", script), ["/hub/admin/api/analytics"], "the one call that it makes")
        self.assertEqual(re.findall(r"setAttribute\('href', '([^']*)'\)", script), ["/legal"], "the one link that it makes")
        self.assertNotRegex(script.replace("http://www.w3.org/2000/svg", ""), r"https?://")

    def test_the_tab_asks_the_hub_with_its_token_and_the_pages_one_call(self):
        page = read("web", "admin_page.js")
        self.assertIn("hubToken: hubToken", page)
        self.assertEqual(page.count("ANALYTICS.make("), 1)

    def test_the_style_is_plain_css_that_a_forced_colors_window_can_still_read(self):
        css = read("web", "admin_analytics.css")
        self.assertNotIn("@import", css)
        self.assertNotRegex(css, r"url\(")
        self.assertIn("@media (forced-colors: active)", css)
        self.assertNotRegex(css, r"(?m)^\s*--[a-z-]+\s*:")                      # (the page's own tokens: not made again here)
        for selector in (".adm .chip", ".adm .btn"):
            self.assertNotIn(selector + " {", css)                              # (a finger's 44 pixels are admin.css's)


class TheChecksRun(unittest.TestCase):
    @unittest.skipUnless(NODE, "no node on this machine: the counter's script was NOT run (tests/scripts/web_analytics_check.js)")
    def test_the_counters_script_run_on_a_fake_page(self):
        done = subprocess.run([NODE, CLIENT_CHECK, CLIENT], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r"all \d+ checks hold")

    @unittest.skipUnless(NODE, "no node on this machine: the Analytics tab was NOT run (tests/scripts/web_admin_analytics_check.js)")
    def test_the_tab_run_on_a_fake_page_and_a_fake_hub(self):
        done = subprocess.run([NODE, TAB_CHECK, TAB], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r"all \d+ checks hold")


class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/web_analytics_mutants.json (the list that tools/mutate.py runs too, with `--keep-going --jobs 1`) on a COPY of the files, one at a time: each must make a check fail, and the copy
    with nothing changed must pass"""
    FILES = ["web/analytics.js", "web/admin_analytics.js", "tests/scripts/web_analytics_check.js", "tests/scripts/web_admin_analytics_check.js", "tests/scripts/web_mapmaker_fake_dom.js"]

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "web_analytics_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_analytics_mutants.")
        for name in cls.FILES:
            os.makedirs(os.path.dirname(os.path.join(cls.folder, *name.split("/"))), exist_ok=True)
            shutil.copy(os.path.join(REPO, *name.split("/")), os.path.join(cls.folder, *name.split("/")))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.folder, ignore_errors=True)

    def run_command(self, command):
        done = subprocess.run(command, shell=True, cwd=self.folder, capture_output=True, text=True, timeout=600)
        return done.returncode, done.stdout + done.stderr

    @unittest.skipUnless(NODE, "node is not installed: the mutants of the counter and its tab were NOT run")
    def test_the_copy_with_nothing_changed_passes(self):
        for command in sorted({m["test"] for m in self.spec["mutants"]} | {b["test"] for b in self.spec["baseline"]}):
            status, out = self.run_command(command)
            self.assertEqual(status, 0, command + "\n" + out[-3000:])

    @unittest.skipUnless(NODE, "node is not installed: the mutants of the counter and its tab were NOT run")
    def test_every_mutant_is_seen(self):
        ids = [m["id"] for m in self.spec["mutants"]]
        self.assertEqual(len(ids), len(set(ids)), "an id twice")
        missed = []
        for m in self.spec["mutants"]:
            path = os.path.join(self.folder, *m["file"].split("/"))
            with open(path, encoding="utf-8", newline="") as f:
                text = f.read()
            self.assertEqual(text.count(m["old"]), 1, "%s: %r is not in %s exactly once" % (m["id"], m["old"][:60], m["file"]))
            try:
                with open(path, "w", encoding="utf-8", newline="") as f:
                    f.write(text.replace(m["old"], m["new"]))
                status, _ = self.run_command(m["test"])
            finally:
                with open(path, "w", encoding="utf-8", newline="") as f:
                    f.write(text)
            if status == 0:
                missed.append(m["id"])
        self.assertEqual(missed, [], "the checks did not see these changes")


if __name__ == "__main__":
    unittest.main()
