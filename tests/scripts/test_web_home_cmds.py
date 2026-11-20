#!/usr/bin/env python3
"""The commands of the chat dock of Home (web/home/cmds.js and cmds.css, docs/HOME_CHAT.md "Roles and commands", docs/HUB_COMMANDS.md; run by ./run_tests.sh --fast and by the CI).

A signed-in person types a slash in the chat's field and a list of THEIR commands opens over it (the hub sends each account its own rows, `cmds`, again whenever its role changes); `/bots` opens a builder (a box over the page, a
sheet on a phone) that only writes the command into the field; the private answers to the hub's `reply` frames are blocks of the chat's log; `/help` opens the same list and is never sent; `//x` says `/x`. The helper is loaded by
the dock (through the loader that home.js gives it) only when a `cmds` frame has a row. What this file checks:
  - the page and the other pages: web/lobby.html does not link it, only the dock asks for it, home.js hands the dock its loader, the game and the other pages never hear of it;
  - the folder: its own budgets (raw and gzipped), plain ES5, ASCII, no markup made from text, the kinds of element it makes, no storage, no socket, the only calls (two GETs of the admin page's API) and the only two files it
    asks for (the admin page's list of maps), nothing sent by itself, and nothing but the field, the log's block and the hub's own rows from the dock;
  - the style: nothing loaded from another site, no sideways scroll, every target of the helper and the builder at least 44 px, the sheet from the bottom on a phone, and the page's own tilted drag chip put right inside the
    admin page's list of maps;
  - the code RUNS in node: tests/scripts/web_home_cmds_check.js (the real dock and the real helper, a fake socket, a fake clock and a fake page);
  - tests/data/home_cmds_mutants.json changes one thing at a time in a COPY of the files: the checks must then fail (the last class of this file; tools/mutate.py runs the same list).
The places and sizes on a screen are measured in a real browser (tests/scripts/web_home_cmds_browser.js, opt-in, `--shots DIR`).
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
HOME = os.path.join(REPO, "web", "home")
WEB = os.path.join(REPO, "web")
CHECK = os.path.join(REPO, "tests", "scripts", "web_home_cmds_check.js")
LOBBY = os.path.join(REPO, "web", "lobby.html")
NODE = shutil.which("node")
FILES = ("cmds.js", "cmds.css")
sys.path.insert(0, os.path.join(REPO, "tools"))
from size_limits import CMDS_BUDGET, CMDS_GZIP_BUDGET  # noqa: E402  (every byte budget: tools/size_limits.py)


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def code_of(js):
    return re.sub(r"//.*", "", js)


class ThePageAndTheOtherPages(unittest.TestCase):
    def test_the_front_page_does_not_link_the_helper_the_dock_loads_it_later(self):
        page = read("web", "lobby.html")
        for word in ("home/cmds", "AntsHomeCmds", "cmds.js", "cmds.css"):
            self.assertNotIn(word, page, word)

    def test_only_the_dock_asks_for_it_and_home_hands_the_dock_its_loader(self):
        naming_it, naming_the_socket = set(), set()
        for base, _dirs, names in os.walk(WEB):
            for name in names:
                if os.path.splitext(name)[1] not in (".html", ".js", ".css"):
                    continue
                path = os.path.join(base, name)
                rel = os.path.relpath(path, REPO).replace(os.sep, "/")
                with open(path, encoding="utf-8", errors="replace") as f:
                    text = f.read()
                code = re.sub(r"/\*.*?\*/", "", re.sub(r"(?m)^\s*//.*|\s//.*", "", text), flags=re.S) if name.endswith((".js", ".css")) else text       # (a comment may name a file; the code may not)
                if re.search(r"home/cmds\b|AntsHomeCmds", code):
                    naming_it.add(rel)
                if re.search(r"/hub/ws", text):
                    naming_the_socket.add(rel)
        self.assertEqual(sorted(naming_it), ["web/home/chat.js", "web/home/cmds.js"], "the dock asks for it (its one place) and its own file names its global")
        self.assertEqual(sorted(naming_the_socket), ["web/home/chat.js", "web/hub_here.js"], "the helper opens no socket and names no door of the hub")
        self.assertIn("screen: function () { return screen; }, load: lazy });", read("web", "home", "home.js"), "home.js gives the dock the loader that it has for its other parts")

    def test_the_dock_asks_only_for_a_frame_that_has_a_row_and_only_once(self):
        js = code_of(read("web", "home", "chat.js"))
        self.assertEqual(len(re.findall(r"env\.load\(", js)), 1, "one place asks")
        self.assertIn("else if (cmdsRows.length && !cmdsAsked && env.load) {", js, "a row, not asked before, and a page that has a loader")
        self.assertEqual(js.count("setCmds(["), 2, "a visitor that is nobody, and a login that ended: no rows")

    def test_the_game_and_the_other_pages_never_hear_of_it(self):
        for rel in (("web", "shell.html"), ("web", "front", "lobby_net.js"), ("web", "front", "lobby_rules.js"), ("web", "front", "lobby_chat.js")):
            self.assertNotRegex(read(*rel), r"AntsHomeCmds|home/cmds", rel[-1])


class TheFolderAndTheScript(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.js = read("web", "home", "cmds.js")
        cls.code = code_of(cls.js)

    def test_it_is_under_its_budgets(self):
        raw = zipped = 0
        for name in FILES:
            with open(os.path.join(HOME, name), "rb") as f:
                data = f.read()
            raw += len(data)
            zipped += len(gzip.compress(data, 9))
        self.assertLess(raw, CMDS_BUDGET, "cmds.js and cmds.css are %d bytes" % raw)
        self.assertLess(zipped, CMDS_GZIP_BUDGET, "and %d bytes gzipped" % zipped)

    def test_the_sources_are_ascii_only(self):
        for name in FILES:
            with open(os.path.join(HOME, name), "rb") as f:
                data = f.read()
            self.assertIsNone(re.search(rb"[^\x00-\x7f]", data), name + " holds a character that is not ASCII (a visible one is written as \\uXXXX, an invisible one never appears)")

    def test_the_script_is_plain_es5(self):
        for token in (r"\bconst\b", r"\blet\b", r"=>", r"`", r"\bimport\b", r"\bclass\s+\w+\s*[{(]", r"\basync\b(?!\s*=)", r"\bawait\b", r"Object\.assign", r"\.includes\(", r"\.startsWith\(", r"\.padStart\("):
            self.assertIsNone(re.search(token, self.code), token)
        self.assertIn("'use strict';", self.js)

    def test_nothing_that_is_said_becomes_markup_a_link_or_code(self):
        self.assertNotRegex(self.code, r"innerHTML|outerHTML|insertAdjacentHTML|document\.write|\beval\b|new Function|srcdoc|createContextualFragment|DOMParser|setTimeout\(\s*['\"]")
        self.assertNotRegex(self.code, r"setAttribute\(\s*['\"](href|src|style|on\w+|srcdoc)")
        self.assertEqual(sorted(re.findall(r"\b\w+\.(?:href|src) = [^;]+;", self.code)), ["css.href = PICK_CSS;", "js.src = PICK;"], "the only addresses it sets are the admin page's two files, constants of this file")
        self.assertIn("PICK = '/admin_map_pick.js', PICK_CSS = '/admin.css', OPTIONS = '/admin/api/bot-options', SUMMARY = '/admin/api/bot-matches/summary';", self.js)
        made = sorted(set(re.findall(r"\bel\('(\w+)'", self.code)))
        self.assertEqual(made, ["b", "bdi", "button", "code", "div", "h3", "i", "input", "kbd", "label", "link", "mark", "option", "p", "script", "select", "span"], "text, boxes, the controls of the builder and the two files of the admin page's list: no a, img, iframe or style")
        self.assertEqual(self.code.count("createElement"), 1, "one place makes elements")
        self.assertIn("if (text !== undefined) e.textContent = text;", self.js)

    def test_it_keeps_nothing_in_the_browser_and_its_only_calls_are_two_gets_of_the_admin_pages_api(self):
        self.assertNotRegex(self.code, r"localStorage|sessionStorage|indexedDB|document\.cookie|new WebSocket|WebSocket\(|XMLHttpRequest|sendBeacon|\bcaches\b|postMessage")
        self.assertNotRegex(self.code, r"/hub\b|https?:")
        self.assertEqual(len(re.findall(r"\bfetch\b", self.code)), 1, "`var f = win.fetch;` is the one place that has it")
        self.assertIn("f.call(win, path, { credentials: 'same-origin', cache: 'no-store' })", self.js, "a GET with the cookie of the page and nothing else: no method, no body, no token")
        self.assertEqual(sorted(re.findall(r"\bgetJson\(([A-Z_]+),", self.code)), ["OPTIONS", "SUMMARY"], "the two calls of the builder (a call is made with a constant of this file, never an address that came from outside)")
        self.assertNotRegex(self.code, r"\bmethod\s*:|\bbody\s*:|X-Ants|csrf", "it changes nothing on the game server: starting and stopping are commands that the hub runs")

    def test_it_sends_nothing_and_takes_nothing_from_the_dock_but_four_things(self):
        self.assertNotRegex(self.code, r"dock\.(send|say|command|tap)\b|\.submit\(|requestSubmit|dispatchEvent|\.send\(")
        self.assertEqual(sorted(set(re.findall(r"\bdock\.(\w+)", self.code))), ["block", "field", "grow", "root"], "its field (to write into), the log's block (to answer in), the dock's height and its box")
        self.assertNotRegex(self.code, r"/mute [a-z]|/kick [a-z]|/ban [a-z]", "no command by a name, and no table of commands of its own: the rows are the hub's")
        self.assertNotRegex(self.code, r"\[\s*'/(help|mute|unmute|del|kick|mutes|ban|unban|bans|nopic|pic|ignore|unignore)'", "no row of a command in this file: what is drawn is what `cmds` carried")

    def test_the_largest_count_is_ten_thousand_and_the_longest_spacing_is_two_hours(self):
        self.assertIn("count: 10000, every: 120, map: 96,", self.js)
        self.assertIn("/^\\/[a-z][a-z0-9]{0,15}$/", self.js, "a row's word is a slash and a lower-case word")


class TheStyle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.css = read("web", "home", "cmds.css")
        cls.rules = re.findall(r"([^{}]+)\{([^{}]*)\}", re.sub(r"/\*.*?\*/", "", cls.css, flags=re.S))

    def body(self, selector):
        return next(body for sel, body in self.rules if " ".join(sel.split()) == selector)

    def test_nothing_is_loaded_from_another_site_and_there_is_no_picture(self):
        self.assertNotRegex(self.css, r"https?://|@import")
        self.assertEqual(re.findall(r"url\(", self.css), [])
        self.assertIsNone(re.search(r"overflow-x: *(scroll|auto)", self.css))

    def test_every_target_is_at_least_44_px(self):
        for selector in (".gh-row", ".gb-seg button", ".gb-presets button"):
            self.assertIn("min-height: 44px;", self.body(selector), selector)
        self.assertIn("height: 44px;", self.body(".gb-in"))
        self.assertIn("width: 44px; height: 44px;", self.body(".gb-x"))
        self.assertIn("min-height: 46px;", self.body(".gb-btns .btn"))
        for selector, body in self.rules:
            for m in re.finditer(r"(?<![-\w])(?:min-)?height: (\d+)px", body):
                if "bar" in selector or "gh-top" in selector or "gh-foot" in selector or "gb-head" in selector or "gc-bar" in selector:
                    continue
                self.assertGreaterEqual(int(m.group(1)), 44, "%s has a height of %s" % (" ".join(selector.split()), m.group(1)))

    def test_a_phone_has_the_list_full_width_and_the_builder_as_a_sheet_from_the_bottom(self):
        phone = re.search(r"@media \(max-width: 700px\) \{(.*?)\n\}", self.css, re.S).group(1)
        self.assertIn(".gh-pop { left: -6px; right: -6px; width: auto;", phone)
        self.assertIn(".gb { left: 0; right: 0; top: auto; bottom: 0; transform: none; width: auto; max-height: 94vh;", phone)

    def test_the_list_lies_over_the_field_and_the_builder_over_the_page(self):
        self.assertIn("bottom: 100%;", self.body(".gh-pop"))
        self.assertIn("position: absolute;", self.body(".gh-pop"))
        self.assertIn(".gc-form { position: relative; }", self.css, "the form is the place that the list is measured from, whether or not the emoji buttons were loaded")
        self.assertIn("z-index: 71;", self.body(".gb"))
        self.assertIn("position: fixed;", self.body(".gb-back"))

    def test_the_admin_pages_list_of_maps_is_right_over_the_front_page(self):
        host = self.body(".gcm-adm.adm")
        for token in ("position: fixed;", "width: 0;", "height: 0;", "min-height: 0;", "--frame-bevel:", "--rule:", "--pressed:", "--btn-shadow-sm:", "--box-shadow:", "--mint:"):
            self.assertIn(token, host, token)
        chip = self.body(".gcm-adm.adm .chip")
        for token in ("position: static;", "pointer-events: auto;", "transform: none;"):
            self.assertIn(token, chip, "the front page's own .chip is the tilted chip that follows a drag: fixed, with no pointer; the list's chips are the admin page's")
        self.assertIn("z-index: 80;", self.body(".gcm-adm.adm .scrim"))
        self.assertGreater(80, 71, "over the builder")


class TheWayIntoTheImage(unittest.TestCase):
    def test_the_ci_looks_for_the_script_and_the_style_in_the_image(self):
        self.assertIn(" home/cmds.js home/cmds.css ", read(".github", "workflows", "ci.yml"))

    def test_the_dockerfile_copies_the_folder_to_where_nginx_serves_home_and_the_local_build_packs_it(self):
        self.assertIn("COPY web/home/ /usr/share/nginx/html/home/\n", read("Dockerfile"))
        self.assertIn('cp -f "${SCRIPT_DIR}/web/home"/* "${DIST_DIR}/home/"', read("build_web.sh"))

    def test_the_admin_pages_two_files_are_in_the_image_too(self):
        ci = read(".github", "workflows", "ci.yml")
        self.assertIn(" admin.css admin_map_pick.js ", ci, "the list of maps that the builder borrows is served as it is for the admin page")

    def test_the_documentation_says_how_it_works(self):
        doc = read("docs", "HOME_CHAT.md")
        for needle in ("cmds.js", "`/help`", "`//x`", "/bots", "`cmds` frame", "`reply` frame"):
            self.assertIn(needle, doc, needle)


@unittest.skipUnless(NODE, "node is not installed: the helper was NOT run (tests/scripts/web_home_cmds_check.js)")
class TheCodeRuns(unittest.TestCase):
    def test_the_helper_against_the_real_dock_and_a_fake_socket(self):
        done = subprocess.run([NODE, CHECK, LOBBY], capture_output=True, text=True, timeout=300, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout[-4000:] + done.stderr[-2000:])
        self.assertRegex(done.stdout, r"home chat commands: \d+ checks, 0 failed")


class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/home_cmds_mutants.json (the list that tools/mutate.py runs too, with `--keep-going --jobs 1`) on a COPY of the files, one at a time: each must make the checks fail, and the
    copy with nothing changed must pass"""
    FILES = (["tools/size_limits.py", "web/lobby.html", "web/rejoin_key.js", "web/alert_core.js", "web/hub_here.js", "web/front/player_name.js", "web/shapes.js", "web/home/home.js", "web/home/home.css", "web/home/chat.js", "web/home/chat.css", "web/home/cmds.js", "web/home/cmds.css",
              "tests/scripts/test_web_home_cmds.py", "tests/scripts/web_home_cmds_check.js", "tests/scripts/web_page_harness.js", "tests/scripts/fake_hub.js", "Dockerfile", "build_web.sh", ".github/workflows/ci.yml", "docs/HOME_CHAT.md",
              "web/matches.html", "web/match.html", "web/legal.html", "web/mapmaker.html", "web/shell.html", "web/admin.html"]
             + ["web/front/" + n for n in ("classic.css", "lobby_rules.js", "lobby_net.js", "lobby_chat.js", "lobby_chat.css", "rematch.js", "game_modes.js", "game_type.js", "map_picker.js", "map_maker_link.js")])

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "home_cmds_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import mutant_shard                     # (here and not at the top: the copies in which the mutants run have this file but not that one, and run the other classes)
        cls.mutants = mutant_shard.from_environment(cls.spec["mutants"])
        cls.folder = tempfile.mkdtemp(prefix="ants_home_cmds_mutants.")
        for name in cls.FILES:
            os.makedirs(os.path.dirname(os.path.join(cls.folder, *name.split("/"))), exist_ok=True)
            shutil.copy(os.path.join(REPO, *name.split("/")), os.path.join(cls.folder, *name.split("/")))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.folder, ignore_errors=True)

    def run_command(self, command):
        done = subprocess.run(command, shell=True, cwd=self.folder, capture_output=True, text=True, timeout=600)
        return done.returncode, done.stdout + done.stderr

    def test_the_copy_with_nothing_changed_passes(self):
        for command in sorted({m["test"] for m in self.mutants}):
            status, out = self.run_command(command)
            self.assertEqual(status, 0, command + "\n" + out[-3000:])

    def test_every_mutant_is_seen(self):
        ids = [m["id"] for m in self.spec["mutants"]]
        self.assertEqual(len(ids), len(set(ids)), "an id twice")
        missed = []
        for m in self.mutants:
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
