#!/usr/bin/env python3
"""The update notice and the "What's new" window (web/update_notice.js, web/update_notice.css; docs/SITE_NAV.md "The update notice"): run by ./run_tests.sh --fast and by the CI.

  - the code RUNS in node (tests/scripts/web_update_notice_check.js: the real script on a fake page with a clock that the check winds, a fetch that answers what it says and storage that can fail, and the
    game page's own call, the block of web/shell.html between ANTS_UPDATE_BEGIN and ANTS_UPDATE_END): when it asks, what it shows, what it keeps, nothing during a match, and text put in as text;
  - the files, where no run of the code can look: every page that is stamped with the version loads the script and the stylesheet once (the front page, the game page, Matches, Match, Legal, the changelog,
    the Map maker and the admin page), the page that is not stamped (the catalog) loads neither, the game page and the admin page have no window, the Dockerfile and the local web build put both files in the
    image, the CI's list of the image's files names them and the data file, the files are plain ES5 and CSS within their byte budget, and no inline style or script is needed (the admin page allows none);
  - the opt-in browser check (tests/scripts/web_update_notice_browser.py, tests/scripts/test_web_update_notice.sh) takes a bad command line as one;
  - tests/data/update_notice_mutants.json changes one thing at a time in a COPY of the files: the node check must then fail (the last class of this file; tools/mutate.py runs the same list).
The data file and its generator are tests/scripts/test_changelog_to_html.py's; nginx's rule for it is tests/scripts/test_nginx_headers.py's.
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
SCRIPT = os.path.join(REPO, "web", "update_notice.js")
STYLE = os.path.join(REPO, "web", "update_notice.css")
CHECK = os.path.join(REPO, "tests", "scripts", "web_update_notice_check.js")
BROWSER_CHECK = os.path.join(REPO, "tests", "scripts", "web_update_notice_browser.py")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def changelog_page():
    return changelog_to_html.convert(read("CHANGELOG.md"), changelog_to_html.DEFAULT_REPO_BLOB, version="v0.0.0", build_id="0000000")


# name -> (the page, whether it has a window: the game page and the admin page have none). The page of every address that carries the site's version.
STAMPED = {
    "lobby": (read("web", "lobby.html"), True),
    "game": (read("web", "shell.html"), False),
    "matches": (read("web", "matches.html"), True),
    "match": (read("web", "match.html"), True),
    "legal": (read("web", "legal.html"), True),
    "mapmaker": (read("web", "mapmaker.html"), True),
    "admin": (read("web", "admin.html"), False),
    "changelog": (changelog_page(), True),
}


class ThePages(unittest.TestCase):
    def test_every_stamped_page_loads_the_script_and_the_style_once(self):
        for name, (page, window) in STAMPED.items():
            self.assertEqual(len(re.findall(r'<link rel="stylesheet" href="/?update_notice\.css">', page)), 1, name)
            scripts = re.findall(r'<script src="/?update_notice\.js"([^>]*)></script>', page)
            self.assertEqual(len(scripts), 1, name)
            self.assertEqual(scripts[0].strip(), "" if window else 'data-window="no"', name + ": only the game page and the admin page have no window")

    def test_the_stamp_that_the_script_reads_is_on_every_one_of_them(self):
        for name, (page, window) in STAMPED.items():
            stamped = re.search(r'<header class="sb"[^>]*\bdata-version="[^"]+"', page) or re.search(r'id="game-version">[^<]+<', page)
            self.assertIsNotNone(stamped, name)

    def test_the_catalog_has_no_stamp_and_so_no_notice(self):
        page = read("asset_catalog", "index.html")
        self.assertNotIn("update_notice", page)
        self.assertNotRegex(page, r"data-version=|game-version")

    def test_the_script_comes_after_the_bar_it_mounts_under(self):
        for name, (page, window) in STAMPED.items():
            if name == "lobby":
                self.assertIn('class="wrap strips"', page)                                         # (the front page's own strips are where the notice goes)
                self.assertLess(page.index('class="wrap strips"'), page.index("update_notice.js"), name)
            else:
                self.assertLess(page.index('<header class="sb"'), page.index("update_notice.js"), name)

    def test_the_game_page_hands_the_script_its_two_hooks_inside_the_block_that_the_node_check_runs(self):
        page = STAMPED["game"][0]
        block = page[page.index("// ANTS_UPDATE_BEGIN"):page.index("// ANTS_UPDATE_END")]
        self.assertIn("AntsUpdate.page(", block)
        self.assertLess(page.index("update_notice.js"), page.index("// ANTS_UPDATE_BEGIN"))          # (the script is loaded before the page's call)


class TheFiles(unittest.TestCase):
    def test_the_dockerfile_the_local_build_and_the_ci_put_the_files_in_the_image(self):
        docker, local, ci = read("Dockerfile"), read("build_web.sh"), read(".github", "workflows", "ci.yml")
        for name in ("update_notice.css", "update_notice.js"):
            self.assertEqual(docker.count("COPY web/%s /usr/share/nginx/html/%s\n" % (name, name)), 1, name)
            self.assertIn("web/%s" % name, local, name)
            self.assertIn(" " + name + " ", ci, name)
        self.assertIn("whats_new.json", ci)                                                       # (the image check lists the data file too)

    def test_they_are_ascii_and_within_their_budget(self):
        raw = b""
        for path in (SCRIPT, STYLE):
            with open(path, "rb") as f:
                raw += f.read()
        self.assertTrue(all(b < 128 or b in () for b in raw.replace("’".encode(), b"")), "the files are ASCII (the window's title writes its apostrophe as an escape)")
        self.assertLess(len(raw), size_limits.UPDATE_NOTICE_BUDGET, "%d bytes" % len(raw))
        self.assertLess(len(gzip.compress(raw, 9)), size_limits.UPDATE_NOTICE_GZIP_BUDGET, "%d bytes gzipped" % len(gzip.compress(raw, 9)))

    def test_the_script_is_plain_es5_and_never_makes_markup_or_a_style_string(self):
        script = read("web", "update_notice.js")
        for bad in ("=>", "let ", "const ", "`", "class ", "async ", "await ", "innerHTML", "outerHTML", "insertAdjacentHTML", "document.write", "eval(", "new Function", "XMLHttpRequest", "cookie", "setAttribute('style'", ".style."):
            self.assertNotIn(bad, script, bad)

    def test_the_style_is_plain_css_with_the_pages_tokens_as_fallbacks_only(self):
        style = read("web", "update_notice.css")
        self.assertNotIn("@import", style)
        self.assertNotRegex(style, r"url\(")
        self.assertNotRegex(style, r"(?m)^\s*--(?:clay|ink|teal|edge|cream|inset|frame|frame-hi|frame-lo|shadow|gold|muted)\s*:")      # (the pages' own tokens: not made again at the root)
        self.assertIn("@media (forced-colors: active)", style)


class TheCode(unittest.TestCase):
    @unittest.skipUnless(NODE, "no node on this machine: the update notice was NOT run (tests/scripts/web_update_notice_check.js)")
    def test_the_real_script_and_the_game_pages_call_run_on_fakes(self):
        done = subprocess.run([NODE, CHECK, SCRIPT, os.path.join(REPO, "web", "shell.html")], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r"update notice: all \d+ checks passed")


class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/update_notice_mutants.json (the list that tools/mutate.py runs too, with `--keep-going --jobs 1`) on a COPY of the files, one at a time: each must make the check fail, and the copy
    with nothing changed must pass"""
    FILES = ["web/update_notice.js", "web/shell.html", "tests/scripts/web_update_notice_check.js", "tests/scripts/web_mapmaker_fake_dom.js"]

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "update_notice_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_update_notice_mutants.")
        for name in cls.FILES:
            os.makedirs(os.path.dirname(os.path.join(cls.folder, *name.split("/"))), exist_ok=True)
            shutil.copy(os.path.join(REPO, *name.split("/")), os.path.join(cls.folder, *name.split("/")))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.folder, ignore_errors=True)

    def run_command(self, command):
        done = subprocess.run(command, shell=True, cwd=self.folder, capture_output=True, text=True, timeout=600)
        return done.returncode, done.stdout + done.stderr

    @unittest.skipUnless(NODE, "node is not installed: the mutants of the update notice were NOT run")
    def test_the_copy_with_nothing_changed_passes(self):
        for command in sorted({m["test"] for m in self.spec["mutants"]} | {self.spec["baseline"]["test"]}):
            status, out = self.run_command(command)
            self.assertEqual(status, 0, command + "\n" + out[-3000:])

    @unittest.skipUnless(NODE, "node is not installed: the mutants of the update notice were NOT run")
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
        self.assertEqual(missed, [], "the check did not see these changes")


class TheBrowserCheck(unittest.TestCase):
    def test_a_bad_command_line_is_a_bad_command_line(self):
        done = subprocess.run([sys.executable, BROWSER_CHECK, "--no-such-option"], capture_output=True, text=True, timeout=60)
        self.assertEqual(done.returncode, 2, done.stdout + done.stderr)


if __name__ == "__main__":
    unittest.main()
