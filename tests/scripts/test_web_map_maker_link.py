#!/usr/bin/env python3
"""The "Map maker" link in the lobby's header (web/front/map_maker_link.js; run by ./run_tests.sh --fast with the other python tests of tests/scripts, and by the CI).
web/lobby.html is at the line limit of tools/check_hygiene.py, so the page only loads the script, on the line of its first script; the script adds the link before the Matches button.

  - the page: the script is loaded once, first of the page's scripts (on the line of the first), and the page did not grow;
  - the script: the link goes to /mapmaker.html, looks like the Matches button (class "btn sm"), sits right before it, is made once (a second run adds nothing), has a visible keyboard focus,
    reaches 44 px for a finger, and the phone rule gives it its own column on the logo's row;
  - a small fake of the DOM runs the script with node (skipped without node): one link in front of Matches; none without a Matches button; never twice;
  - tests/data/map_maker_link_mutants.json changes one thing at a time in a COPY of the script and of the page (the address, the place, the class, the guard against a second link, the focus rule, the
    touch layer, the phone column, the script tag): the checks must then fail (the last class of this file; tools/mutate.py runs the same list).
The pictures that the owner approved are the browser check's: tests/scripts/web_home_check.py (the header at 320 to 1600 px).
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCRIPT = os.path.join(REPO, "web", "front", "map_maker_link.js")
LOBBY = os.path.join(REPO, "web", "lobby.html")
NODE = shutil.which("node")

FAKE = r"""
var made = [], head = {kids: [], querySelector: function (s) {
    if (s === '#watch-top') return this.kids.filter(function (k) { return k.id === 'watch-top'; })[0] || null;
    if (s === '#mapmaker-top') return this.kids.filter(function (k) { return k.id === 'mapmaker-top'; })[0] || null;
    return null; },
  insertBefore: function (n, ref) { this.kids.splice(this.kids.indexOf(ref), 0, n); } };
var doc = {querySelector: function (s) { return s === 'header.mast' ? (global.NOHEAD ? null : head) : null; },
  createElement: function (t) { var e = {tag: t}; made.push(e); return e; }, head: {appendChild: function (n) { made.push(n); }}};
if (!global.NOMATCHES) head.kids.push({id: 'watch-top'});
global.document = doc;
var code = require('fs').readFileSync(process.argv[2], 'utf8');
function run() { (new Function(code))(); }
run(); run();
console.log(JSON.stringify(head.kids.map(function (k) { return [k.id, k.tag, k.href, k.className, k.textContent]; })));
"""


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def problems(page, js):
    """What is wrong with this copy of the page and of the script (an empty list: nothing)."""
    bad = []
    if page.count('<script src="front/map_maker_link.js"></script>') != 1:
        bad.append("the page loads the script once")
    if not re.search(r'\n<script src="front/map_maker_link\.js"></script><script src="front/lobby_rules\.js"></script>\n', page):
        bad.append("the script is loaded first of the page's scripts (not before them in the markup: the tests read the markup up to the first script)")
    if len(page.split("\n")) > 1500:
        bad.append("the page is over its line limit")
    if "link.href = '/mapmaker.html';" not in js:
        bad.append("the link goes to /mapmaker.html")
    if "link.className = 'btn sm';" not in js:
        bad.append("the link has the look of the Matches button")
    if "head.insertBefore(link, matches);" not in js:
        bad.append("the link sits before Matches")
    if "link.id = 'mapmaker-top';" not in js or "head.querySelector('#mapmaker-top')) return;" not in js:
        bad.append("the link is made once")
    if "link.textContent = 'Map maker';" not in js:
        bad.append("the link says Map maker")
    if ".mast #mapmaker-top:focus-visible { outline: 3px solid" not in js:
        bad.append("keyboard focus is visible")
    if "top: -3px; bottom: -3px;" not in js or "'.mast #mapmaker-top::before { content:" not in js:
        bad.append("the touch layer makes it 44 px (38 + 6)")
    if 'grid-template-areas: "logo . map btn" "text text text text"' not in js or "grid-area: map;" not in js:
        bad.append("on a phone both buttons stay on the logo's row")
    if "@media (max-width: 389px)" not in js or "width: 88px" not in js:
        bad.append("at 320 px both fit")
    if "margin-left: auto" not in js or "#mapmaker-top + #watch-top { margin-left: 0; }" not in js:
        bad.append("the pair sits at the right")
    return bad


class MapMakerLink(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.page = read(LOBBY)
        cls.js = read(SCRIPT)

    def test_the_page_and_the_script_are_right(self):
        self.assertEqual(problems(self.page, self.js), [])

    def test_the_header_keeps_its_other_button(self):
        self.assertEqual(self.page.count('id="watch-top"'), 1)
        self.assertEqual(self.page.count("mapmaker-top"), 0, "the link is not written in the page (its line limit): the script adds it")
        self.assertEqual(self.page.count("/mapmaker.html"), 0)

    def test_the_script_is_plain_es5_and_names_no_other_site(self):
        self.assertNotRegex(self.js, r"https?://")
        for token in (r"\bconst\b", r"\blet\b", r"=>", r"`", r"\bimport\b", r"\bfetch\b", r"localStorage"):
            self.assertIsNone(re.search(token, self.js), token)

    @unittest.skipUnless(NODE, "node is not installed")
    def test_it_adds_one_link_before_matches_and_never_two(self):
        out = self.run_fake(self.js)
        self.assertEqual(out, '[["mapmaker-top","a","/mapmaker.html","btn sm","Map maker"],["watch-top",null,null,null,null]]')

    @unittest.skipUnless(NODE, "node is not installed")
    def test_without_matches_it_adds_nothing(self):
        self.assertEqual(self.run_fake(self.js, "global.NOMATCHES = true;"), "[]")

    def run_fake(self, js, prefix=""):
        with tempfile.TemporaryDirectory() as tmp:
            code, runner = os.path.join(tmp, "code.js"), os.path.join(tmp, "run.js")
            with open(code, "w", encoding="utf-8") as f:
                f.write(js)
            with open(runner, "w", encoding="utf-8") as f:
                f.write(prefix + FAKE)
            done = subprocess.run([NODE, runner, code], capture_output=True, text=True, timeout=60)
            self.assertEqual(done.returncode, 0, done.stderr)
            return done.stdout.strip()


class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/map_maker_link_mutants.json (the list that tools/mutate.py runs too, with `--keep-going --jobs 1`) on a COPY of the files, one at a time: each must make the
    checks above fail, and the copy with nothing changed must pass"""
    FILES = ("web/lobby.html", "web/front/map_maker_link.js", "tests/scripts/test_web_map_maker_link.py")

    @classmethod
    def setUpClass(cls):
        import json
        with open(os.path.join(REPO, "tests", "data", "map_maker_link_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_link_mutants.")
        for name in cls.FILES:
            os.makedirs(os.path.dirname(os.path.join(cls.folder, *name.split("/"))), exist_ok=True)
            shutil.copy(os.path.join(REPO, *name.split("/")), os.path.join(cls.folder, *name.split("/")))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.folder, ignore_errors=True)

    def run_command(self, command):
        done = subprocess.run(command, shell=True, cwd=self.folder, capture_output=True, text=True, timeout=120)
        return done.returncode, done.stdout + done.stderr

    def test_the_copy_with_nothing_changed_passes(self):
        status, out = self.run_command(self.spec["mutants"][0]["test"])
        self.assertEqual(status, 0, out[-3000:])

    def test_every_mutant_is_seen(self):
        ids = [m["id"] for m in self.spec["mutants"]]
        self.assertEqual(len(ids), len(set(ids)), "an id twice")
        missed = []
        for m in self.spec["mutants"]:
            path = os.path.join(self.folder, *m["file"].split("/"))
            text = read(path)
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
