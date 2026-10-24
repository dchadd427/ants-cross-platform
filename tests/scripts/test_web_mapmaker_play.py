#!/usr/bin/env python3
"""The map maker's Play this map (run by ./run_tests.sh --fast, with the other python tests of tests/scripts, and by the CI; docs/MAP_EDITOR.md, "Play this map"): a test game on the map that is open in the
map maker, against bots, in a tab of its own.

  - the code is RUN (node, when it is installed): the verdict that the check's answers make (a plain refusal or four seats), the start rules on all 625 choices, the game page's whitelist of the address
    and of the hand-over, the strip and the way back, the dialogs on a fake page, and, when a built map core is there ($ANTS_LVL_CORE_DIR, or build_web/lvl_core), the real core on the 540 maps of the library
    (tests/scripts/web_mapmaker_play_check.js); the web job of the CI runs all of it from the image with --require;
  - the checks are shown able to fail: broken copies of the scripts (tests/data/mapmaker_play_mutants.json, which tools/mutate.py runs too, with `--keep-going --jobs 1`) are each refused by them;
  - the page's side is read from the source: the button, the menu entry, the lazy loading, that the bytes that go to the game are the open file's own, that nothing here stores, fetches or writes markup, and that
    the game page's whitelist, its start and its page carry the test game;
  - the opt-in browser check (tests/scripts/test_web_mapmaker.sh, part play) is there and says what it covers.
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FRONT = os.path.join(REPO, "web", "front")
CHECK = os.path.join(REPO, "tests", "scripts", "web_mapmaker_play_check.js")
NODE = shutil.which("node")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


WATCHDOG_S = 60                                               # the check of a broken copy takes a third of a second; a minute is a machine that is badly stuck


def core_dir():
    for candidate in (os.environ.get("ANTS_LVL_CORE_DIR", ""), os.path.join(REPO, "build_web", "lvl_core")):
        if candidate and os.path.isfile(os.path.join(candidate, "lvl_core.js")) and os.path.isfile(os.path.join(candidate, "lvl_core.wasm")):
            return candidate
    return None


def run_check(front=None, module=None):
    # a folder of broken copies (front): the check must fail, and the run ends at the first line that says so (tests/scripts/web_mapmaker_bail.js)
    args = [NODE] + (["--require", os.path.join(REPO, "tests", "scripts", "web_mapmaker_bail.js")] if front else []) + [CHECK] + (["--front", front] if front else []) + (["--module", module] if module else [])
    env = dict(os.environ)
    if front:
        env["ANTS_BAIL_WATCHDOG_S"] = str(WATCHDOG_S)         # (a run of a broken copy that is still alive after this long is killed by the preload, with a line that says so)
    return subprocess.run(args, capture_output=True, text=True, timeout=600 if not front else 3 * WATCHDOG_S, env=env)


@unittest.skipUnless(NODE, "node is not installed")
class ThePlayChecks(unittest.TestCase):
    def test_the_checks_pass_on_made_up_answers_and_on_the_real_core_when_it_is_there(self):
        done = run_check(module=core_dir())
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r"play check: \d+ checks passed")
        for part in ("A the verdict", "B the game page", "C the dialogs", "D the real core"):
            self.assertIn("RUNNING: " + part, done.stdout)
        if core_dir():
            self.assertIn("library: 540 maps", done.stdout)

    def test_the_watchdog_ends_a_run_that_does_not_end_and_says_so(self):
        with tempfile.TemporaryDirectory() as tmp:
            hang = os.path.join(tmp, "hang.js")
            with open(hang, "w", encoding="utf-8") as f:
                f.write("console.log('    FAILED: made up'); \n")                           # (the bail ends this one at once: the preload is in place)
            with open(os.path.join(tmp, "stuck.js"), "w", encoding="utf-8") as f:
                f.write("console.log('RUNNING: x'); setInterval(function () {}, 1000);\n")      # (a run that never ends by itself)
            bail = os.path.join(REPO, "tests", "scripts", "web_mapmaker_bail.js")
            env = dict(os.environ, ANTS_BAIL_WATCHDOG_S="1")
            done = subprocess.run([NODE, "--require", bail, hang], capture_output=True, text=True, timeout=60, env=env)
            self.assertEqual(done.returncode, 1)
            self.assertIn("FAILED: made up", done.stdout)
            done = subprocess.run([NODE, "--require", bail, os.path.join(tmp, "stuck.js")], capture_output=True, text=True, timeout=60, env=env)
            self.assertNotEqual(done.returncode, 0)
            self.assertIn("WATCHDOG: the run was still alive after 1 s", done.stderr)
            self.assertIn("RUNNING: x", done.stdout)                                           # (what the run had written is kept)
            done = subprocess.run([NODE, "--require", bail, hang], capture_output=True, text=True, timeout=60, env=dict(os.environ))
            self.assertEqual(done.returncode, 1)                                               # (without the setting there is no watchdog and nothing changes)

    def test_the_checks_can_fail_on_the_broken_copies(self):
        """tests/data/mapmaker_play_mutants.json (tools/mutate.py runs it too, with `--keep-going --jobs 1`): each broken copy must fail the check named for it"""
        with open(os.path.join(REPO, "tests", "data", "mapmaker_play_mutants.json"), encoding="utf-8") as f:
            spec = json.load(f)
        self.assertGreaterEqual(len(spec["mutants"]), 120)
        ids = [m["id"] for m in spec["mutants"]]
        self.assertEqual(len(ids), len(set(ids)), "an id twice")
        with tempfile.TemporaryDirectory() as tmp:
            for name in os.listdir(FRONT):
                if name.endswith(".js"):
                    shutil.copy(os.path.join(FRONT, name), os.path.join(tmp, name))
            for m in spec["mutants"]:
                with self.subTest(m["id"]):
                    name = os.path.basename(m["file"])
                    text = read(*m["file"].split("/"))
                    self.assertEqual(text.count(m["old"]), 1, "%s: the text is not in %s exactly once" % (m["id"], name))
                    if not m["test"].startswith("node tests/scripts/web_mapmaker_play_check.js"):
                        continue                # (a mutant of the pages' glue: another test of this folder notices it; tools/mutate.py runs it)
                    with open(os.path.join(tmp, name), "w", encoding="utf-8", newline="") as f:
                        f.write(text.replace(m["old"], m["new"]))
                    try:
                        done = run_check(tmp)
                    finally:
                        shutil.copy(os.path.join(FRONT, name), os.path.join(tmp, name))
                    self.assertNotEqual(done.returncode, 0, "the check did not notice: " + m["id"] + "\n" + done.stdout[-600:] + done.stderr[-600:])
                    # (a run that ended by its watchdog counts only when the check had said that it failed, a line of its own; one that ended by itself with a status counts as it always did, a crash at the load too)
                    if "WATCHDOG:" in done.stderr:
                        self.assertRegex(done.stdout, r"\b(FAILED|ERROR|CRASHED): ", "the run hung with no failing line of the check for " + m["id"] + " (" + done.stderr[-300:].strip() + ")")
                        print("NOTE: the run of the broken copy %s said that the check failed and then did not end by itself (killed by its watchdog after %d s): %s" % (m["id"], WATCHDOG_S, done.stderr.strip()[-200:]), file=sys.stderr)

    def test_every_mutant_of_the_glue_names_a_test_that_exists(self):
        with open(os.path.join(REPO, "tests", "data", "mapmaker_play_mutants.json"), encoding="utf-8") as f:
            spec = json.load(f)
        tests = {m["test"] for m in spec["mutants"]}
        self.assertEqual(tests, {"node tests/scripts/web_mapmaker_play_check.js", "node tests/scripts/web_lobby_check.js web/shell.html web/lobby.html", "python3 -m unittest tests.scripts.test_web_report.ThePagesOwnCode", "python3 -m unittest tests.scripts.test_web_mapmaker_play.ThePageWiring"})
        for m in spec["mutants"]:
            if m["file"] in ("web/shell.html", "web/front/mapmaker_app.js", "web/mapmaker.html", "web/front/mapmaker_dialog.js"):
                self.assertNotIn("play_check", m["test"], m["id"])


class ThePageWiring(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.page = read("web", "mapmaker.html")
        cls.app = read("web", "front", "mapmaker_app.js")
        cls.play = read("web", "front", "mapmaker_play.js")
        cls.game = read("web", "front", "test_game.js")
        cls.shell = read("web", "shell.html")
        cls.dialog = read("web", "front", "mapmaker_dialog.js")

    def play_function(self):
        return self.app.split("    function playMap() {")[1].split("    var ACTIONS = {")[0]

    def test_the_gold_button_is_the_first_of_the_top_bar_and_starts_off_until_the_core_is_there(self):
        bar = self.page.split('<div class="edmenu dsk">')[1].split("</div>")[0]
        first = re.findall(r"<button [^>]*>.*?</button>", bar)[0]
        self.assertEqual(first, '<button class="btn sm gold" type="button" data-act="play" disabled>&#9654; Play this map</button>')
        self.assertIn("play: playMap,", self.app, "the page's buttons find it by data-act")
        self.assertRegex(read("web", "front", "mapmaker.css"), r"\.btn\.gold \{ background: var\(--gold\)")

    def test_the_menu_of_a_phone_has_it_last_in_gold_with_its_sentence(self):
        entry = "{ text: '\\u25b6 Play this map', cls: 'gold', hint: 'Play this map: a test game against bots. You come back here with the map as you left it.', onClick: ACTIONS.play }, { head: 'Settings for this browser' }"
        self.assertIn(entry, self.app)
        self.assertIn("'btn' + (it.cls ? ' ' + it.cls : '')", self.dialog)
        self.assertIn("if (it.hint) list.appendChild(el('p', 'menuhint', it.hint));", self.dialog)

    def test_the_script_and_the_style_are_loaded_the_first_time_and_not_to_start(self):
        body = self.play_function()
        self.assertIn("if (dialogs.isOpen() || canvas.isStroking()) return;", body, "not over a dialog or in the middle of a stroke")
        self.assertIn("s.src = 'front/mapmaker_play.js'; c.rel = 'stylesheet'; c.href = 'front/mapmaker_play.css';", body)
        self.assertIn("if (win.MapPlay) return go();", body, "loaded once")
        self.assertIn("This part did not load", body, "a part that does not load is said")
        self.assertNotIn("MapPlay", self.app.split("var NEEDED = ")[1].split("];")[0], "the page does not need it to start")
        self.assertNotIn("mapmaker_play", self.page, "no tag in the page")

    def test_the_bytes_that_go_to_the_game_are_the_open_files_own(self):
        body = self.play_function()
        self.assertIn("map: function () { return map; }", body, "the open map: its bytes as the editor holds them")
        for forbidden in ("fileBytes", "rewrite", "serialize", "download"):
            self.assertNotIn(forbidden, body, "the page does not write the file again for the game (%s)" % forbidden)
        self.assertIn("bytes: m.bytes.slice()", self.play, "copied, so that a stroke afterwards does not reach the game")
        self.assertIn("core: core", body, "the check is the map core's own")
        self.assertIn("convert: ACTIONS.convert", body, "a Classic copy is the editor's own conversion")

    def test_nothing_of_it_stores_fetches_or_writes_markup(self):
        for name, text in (("mapmaker_play.js", self.play), ("test_game.js", self.game)):
            self.assertNotRegex(text, r"indexedDB|localStorage|sessionStorage|\bfetch\(|XMLHttpRequest|innerHTML|outerHTML|insertAdjacentHTML|document\.write|\beval\(|new Function|createObjectURL|MapStore|\.put\(", name + " keeps nothing, fetches nothing and writes no markup")
            self.assertNotRegex(text, r"https?://", name + " names no other site")
        self.assertNotIn("noopener", self.play, "the new tab needs its opener: that is how the game page takes the bytes")
        self.assertIn("'_blank')", self.play)
        css = read("web", "front", "mapmaker_play.css")
        self.assertNotRegex(css, r"https?://|@import|url\(")

    def test_both_pages_share_the_opener_policy_that_the_hand_over_needs(self):
        conf = read("docker", "nginx.conf")
        self.assertIn('add_header Cross-Origin-Opener-Policy "same-origin" always;', conf.split("location = /catalog")[0], "set for the whole server: the map maker and the game page keep their opener")

    def test_the_game_page_takes_the_test_game_through_its_whitelist_and_its_own_start(self):
        shell = self.shell
        self.assertIn('<script src="front/test_game.js"></script>', shell)
        self.assertRegex(shell, r"var test = !out\.map && !out\.file && window\.AntsTestGame \? window\.AntsTestGame\.fromAddress\(q\.get\('testmap'\), q\.get\('seats'\)\) : null;")
        self.assertIn("var levels = test ? null : localBots(q.get('bots'));", shell, "the front page's bots are not mixed into a test game")
        self.assertIn("ANTS_LOCAL_FILE = local.file; ANTS_TEST = local.test;", shell)
        self.assertIn("else if (ANTS_TEST && !window.AntsTestGame.begin(ANTS_TEST, window, function () { return Module; })) { /* a card says why the test game cannot start */ } else window.AntsMapFiles.beforeLocalGame(", shell, "a test game that cannot start shows its card and starts nothing; otherwise it goes through the page's one gate like every game on this computer")
        self.assertEqual(len(re.findall(r"ANTS_NAME_GATE\.when\(startGame\)", shell)), 2, "no third way to start a game")
        self.assertIn("if (typeof ANTS_TEST !== 'undefined' && ANTS_TEST) return; var sent = fetch('/stats/local'", shell, "a test game is not counted for the front page")

    def test_the_way_back_and_the_strip_say_their_words(self):
        for needle in ("Leave the test game?", "The game stops here. You go back to the map exactly as you left it.", "Keep playing", "Back to editor", "Not saved. Not counted.", "min-height:44px"):
            self.assertIn(needle, self.game)
        for needle in ("A test game on ", "Nothing is saved and no score is kept.", "Quick:", "You play one color. The other colors are bots or empty. A color needs ants and a hill on the map to be used.",
                       "This map is bigger than the game can play yet.", "Back from the test game. Your map is as you left it."):
            self.assertIn(needle, self.play)
        self.assertEqual(re.findall(r"CHOICES = (\[.*?\]\]);", self.play)[0], "[['y', 'You'], ['e', 'Bot Easy'], ['m', 'Bot Medium'], ['h', 'Bot Hard'], ['n', 'Empty']]")

    def test_the_browser_check_has_the_part(self):
        browser = read("tests", "scripts", "web_mapmaker_browser.py")
        self.assertRegex(browser, r'PARTS = \([^)]*"play"[,)]')
        self.assertIn("play.run_play(", browser)
        self.assertIn("play.run_play_phone(", browser)
        self.assertIn("  * play ", browser)
        self.assertTrue(os.path.isfile(os.path.join(REPO, "tests", "scripts", "web_mapmaker_play_browser.py")))
        self.assertIn('"play"', re.search(r"WITH_ATLAS = \([^)]*\)", browser).group(0), "the screenshots that the part takes show the real map maker, with the game's own pictures in it")


if __name__ == "__main__":
    unittest.main()
