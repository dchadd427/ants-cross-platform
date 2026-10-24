#!/usr/bin/env python3
"""The settings menu of the game page (Esc, or a Settings button; docs/GAME_MENU.md; run by ./run_tests.sh --fast and by the CI). The game's side is tests/test_app/test_game_menu.cpp; what the page and its
script can say without a browser is checked here:

  - the page: has no tag and no markup of the menu (the script fetches itself and makes its buttons and panel), loads it once the game runs and never in the frame of a match page, retries once and then says
    so in the toast, calls it with the pieces of the page that it needs, and has no key handler of its own for Esc (the game asks, through window.antsGameMenuAsk, when Esc has nothing to cancel);
  - the script: puts every text in with textContent (no markup is ever written), reads and writes the game only through the five exports of src/ants_app/application_game_menu.cpp, uses the numbers of
    game_menu.hpp for rows, facts and acts, handles by name only rows that the C++ table has, listens to nothing outside the panel's life, and leaves the lifts of keys to the game;
  - the style: no button of the menu lies over the picture in fullscreen, on any device (the header, under-the-picture and replay-bar buttons are all that is left);
  - the table: the two JSON files that the page's check draws from (tests/data/game_menu_*.json, kept equal to the game's own text by test_game_menu) name exactly the rows of the C++ table;
  - the exports: the five of them are in the game, the page's EM_JS call and the HUD's Esc hook are there;
  - the image: carries the script and the style (Dockerfile, build_web.sh, the CI's list of files);
  - the script RUNS on a strict fake of the DOM (node, when it is installed: tests/scripts/web_game_menu_check.js);
  - the browser check (tests/scripts/web_game_menu_check.py, opt-in: tests/scripts/test_web_game_menu.sh): its command line and its skip without a page.
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
SHELL = os.path.join(REPO, "web", "shell.html")
SCRIPT = os.path.join(REPO, "web", "game_menu.js")
STYLE = os.path.join(REPO, "web", "game_menu.css")
CHECK_JS = os.path.join(REPO, "tests", "scripts", "web_game_menu_check.js")
BROWSER_CHECK = os.path.join(REPO, "tests", "scripts", "web_game_menu_check.py")
BROWSER_RUNNER = os.path.join(REPO, "tests", "scripts", "test_web_game_menu.sh")
GLUE = os.path.join(REPO, "src", "ants_app", "application_game_menu.cpp")
TABLE = os.path.join(REPO, "src", "ants_app", "game_menu.cpp")
HEADER = os.path.join(REPO, "include", "ants_app", "game_menu.hpp")
DATA = os.path.join(REPO, "tests", "data")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def table_keys():
    """The names of the rows of the C++ table, in its order"""
    return re.findall(r'\{k[A-Za-z]+, "([a-z_]+)", Section::', read(TABLE))


class ThePage(unittest.TestCase):
    def setUp(self):
        self.page = read(SHELL)
        start = self.page.index("function antsLoadGameMenu(again) {")
        self.loader = self.page[start:self.page.index("\n        }\n", start) + 10]

    def test_the_page_has_no_tag_and_no_markup_of_the_menu(self):
        self.assertNotIn('src="game_menu', self.page, "the script is fetched once the game runs, not by a tag")
        for name in ("game-menu", "settings-btn", "gm-"):
            self.assertNotIn('id="%s' % name, self.page, "the script makes the buttons and the panel")
            self.assertNotIn('class="%s' % name, self.page)

    def test_it_is_fetched_once_the_game_runs_and_not_in_a_frame(self):
        ready = self.page[self.page.index("isReadyToPlay = true;"):]
        ready = ready[:ready.index("hideLoadingScreen();")]
        self.assertIn("if (!ANTS_EMBED) antsLoadGameMenu();", ready)
        self.assertEqual(len(re.findall(r"antsLoadGameMenu\(", self.page)), 3, "the definition, its retry and the one call")
        self.assertIn("script.src = 'game_menu.js?v=' + ANTS_BUILD_VER;", self.loader)
        self.assertIn("document.head.appendChild(script);", self.loader)

    def test_a_script_that_fails_is_tried_once_more_and_then_said_in_the_toast(self):
        self.assertIn("setTimeout(function () { antsLoadGameMenu(true); }, 2500)", self.loader)
        self.assertIn("if (again) { toast.textContent = 'The settings menu could not load. Press Reset to try again.'; toast.hidden = false; }", self.loader)

    def test_it_is_called_with_the_pieces_of_the_page_that_it_needs(self):
        for part in ("stage: stageElement", "canvas: canvas", "toast: toast", "module: function () { return Module; }", "ready: function () { return isReadyToPlay; }", "replay: ANTS_REPLAY_MODE", "live: ANTS_LIVE_MODE",
                     "isFullscreen: isFullscreen", "enter: enterFullscreen", "leave: leaveFullscreen", "isLocked: isLocked", "releaseLock: releaseLock", "requestLock: requestLock"):
            self.assertIn(part, self.loader)
        self.assertEqual(self.page.count("AntsGameMenu.forPage("), 1)

    def test_the_page_has_no_escape_handler_for_the_menu_the_game_asks(self):
        self.assertNotIn("antsGameMenuAsk", self.page, "the script defines the function that the game calls when Esc has nothing to cancel; the page adds no key handler for it")

    def test_the_help_text_says_what_esc_does(self):
        self.assertIn("with nothing to deselect open the settings menu", self.page)
        self.assertIn("a phone has no Esc, so there you leave fullscreen first to open the settings menu", self.page)
        self.assertIn("<kbd>Esc</kbd>&#8197;deselect, then the settings menu", self.page)

    def test_the_image_carries_the_script_and_the_style(self):
        dockerfile = read(os.path.join(REPO, "Dockerfile"))
        for name in ("game_menu.js", "game_menu.css"):
            self.assertIn("COPY web/%s /usr/share/nginx/html/%s" % (name, name), dockerfile)
        self.assertIn("admin_page.js game_menu.js game_menu.css &&", read(os.path.join(REPO, ".github", "workflows", "ci.yml")))
        self.assertIn('web/game_menu.js" "${SCRIPT_DIR}/web/game_menu.css" "${DIST_DIR}/"', read(os.path.join(REPO, "build_web.sh")))


class TheScript(unittest.TestCase):
    def setUp(self):
        self.script = read(SCRIPT)

    def test_it_writes_no_markup_and_runs_no_text(self):
        for bad in ("innerHTML", "outerHTML", "insertAdjacentHTML", "document.write", "eval(", "new Function", "setAttribute('on", "javascript:"):
            self.assertNotIn(bad, self.script)
        self.assertNotRegex(self.script, r"https?://", "it loads nothing from anywhere but the page's own folder")
        self.assertTrue(all(ord(c) < 128 for c in self.script), "plain ASCII: a character beyond it is written as an escape")

    def test_it_reads_and_writes_the_game_only_through_the_five_exports(self):
        used = set(re.findall(r"_ants_menu_(\w+)", self.script))
        self.assertEqual(used, {"describe", "get", "set", "do", "open"})
        glue = read(GLUE)
        for name in used:
            self.assertRegex(glue, r'EMSCRIPTEN_KEEPALIVE [^\n]*ants_menu_%s\(' % name)
        self.assertNotIn("_ants_probe", self.script, "the menu has its own export; it does not read the probes of the browser checks")

    def test_the_numbers_of_the_facts_and_acts_are_the_games(self):
        header = read(HEADER)
        facts = dict((k, int(v)) for k, v in re.findall(r"kFact(\w+) = (\d+)", header))
        self.assertEqual(facts, {"Where": 100, "NeedsAnswer": 101, "Leave": 102})
        self.assertIn("function where() { return get(100); }", self.script)
        self.assertIn("function answered() { return get(101) === 0; }", self.script)
        self.assertEqual(len(re.findall(r"get\(102\)", self.script)), 2, "what Leave game would do: the question and its follow-up")
        acts = dict((k, int(v)) for k, v in re.findall(r"kAct(\w+) = (\d+)", header))
        self.assertEqual(acts, {"Leave": 1, "QuickHelp": 2, "OptionsWindow": 3})
        self.assertIn("call('_ants_menu_do', 1)", self.script)
        self.assertIn("row.key === 'quickhelp' ? 2 : 3", self.script)
        self.assertNotIn("kind === 3", self.script, "what asks nothing is the table's word (asks), not a number of the page's")
        self.assertIn("!found.asks", self.script)
        self.assertRegex(header, r"Held = 3,")
        self.assertRegex(header, r"Watch = 4,")

    def test_the_rows_that_it_handles_by_name_are_rows_of_the_table(self):
        keys = set(table_keys())
        self.assertEqual(len(keys), 14)
        for name in re.findall(r"^\s{12}(\w+): \{$", self.script, re.M):
            self.assertIn(name, keys, "the page's own row %s is in the table" % name)
        for name in re.findall(r"row\.key === '(\w+)'", self.script):
            self.assertIn(name, keys)
        self.assertEqual(set(re.findall(r"^\s{12}(\w+): \{$", self.script, re.M)), {"picture", "lockmouse"})

    def test_it_draws_what_the_table_says_and_has_no_row_of_its_own(self):
        for label in ("Master volume", "Mute everything", "Sound effects volume", "Participate in chat", "Map scroll speed"):
            self.assertNotIn(label, self.script, "the words of a row are the game's, in its table")

    def test_it_listens_to_nothing_outside_the_panels_life(self):
        self.assertNotIn("window.addEventListener", self.script)
        self.assertNotIn("stage.addEventListener", self.script)
        self.assertEqual(len(re.findall(r"listen\(doc, ", self.script)), 2, "the keys and fullscreen (two names): all removed when it closes")
        self.assertEqual(len(re.findall(r"listen\(win, name, keepPointerFree, true\)", self.script)), 1, "the pointer lock (three names) is heard on the window in the capture phase: the page's own handler there stops the event before the document's")
        self.assertNotIn("'keyup'", self.script, "the lifts of keys are the game's: swallowed, its window system would read the next press as a repeat")
        self.assertIn("removers[i]()", self.script)

    def test_the_stage_is_the_panels_parent_and_the_page_behind_it_is_inert(self):
        self.assertIn("stage.appendChild(scrim);", self.script)
        self.assertIn("k.setAttribute('inert', '');", self.script)
        self.assertIn("behind[j].removeAttribute('inert');", self.script)


class TheStyle(unittest.TestCase):
    """Nothing is laid over the game's picture in fullscreen, on any device (docs/GAME_MENU.md, "The Settings button"): the button that was there is gone, its style and its script too"""

    def test_nothing_of_the_menu_lies_over_the_picture_in_fullscreen(self):
        css, script = read(STYLE), read(SCRIPT)
        for name in ("gm-fs", "settings-btn-fs"):
            self.assertNotIn(name, css, "the style of the button over the picture is gone")
            self.assertNotIn(name, script, "the script makes no button over the picture")
        self.assertNotIn("stage.appendChild(", re.sub(r"stage\.appendChild\(scrim\);", "", script), "only the panel's dimmed screen goes into the stage")
        self.assertNotIn("position: absolute", css.split("/* the dimmed screen and the panel */")[0], "no button of the menu is placed over the picture")


class TheTable(unittest.TestCase):
    def test_the_page_checks_draw_from_the_games_own_text(self):
        for name, watching in (("game_menu_match.json", False), ("game_menu_watch.json", True)):
            data = json.loads(read(os.path.join(DATA, name)))
            self.assertEqual(data["v"], 1)
            keys = [r["key"] for r in data["rows"]]
            self.assertTrue(set(keys) <= set(table_keys()), "every row of the file is a row of the table: %s" % keys)
            self.assertEqual("game" in [s["id"] for s in data["sections"]], not watching)
            self.assertEqual("chat" in keys, not watching)
            self.assertTrue(all(r["sec"] in [s["id"] for s in data["sections"]] for r in data["rows"]))
        match = json.loads(read(os.path.join(DATA, "game_menu_match.json")))
        self.assertEqual([r["key"] for r in match["rows"]], table_keys(), "a match shows every row, in the order of the table")

    def test_the_new_rows_are_tagged_and_only_they(self):
        match = json.loads(read(os.path.join(DATA, "game_menu_match.json")))
        self.assertEqual([r["key"] for r in match["rows"] if r["new"]], ["master", "mute", "hp_numbers"])
        self.assertEqual([r["key"] for r in match["rows"] if r["page"]], ["fullscreen", "picture", "lockmouse"])
        self.assertEqual([r["key"] for r in match["rows"] if r["live"]], ["master"])

    def test_the_game_asks_for_the_menu_through_the_pages_function(self):
        glue = read(GLUE)
        self.assertIn("window.antsGameMenuAsk", glue)
        self.assertIn("hud_.set_on_menu_request", read(os.path.join(REPO, "src", "ants_app", "application.cpp")))
        self.assertIn("UTF8ToString", read(os.path.join(REPO, "src", "ants_app", "CMakeLists.txt")), "the page reads the game's JSON with Module.UTF8ToString")


@unittest.skipUnless(shutil.which("node"), "node is not installed: the page's code for the settings menu was NOT run (tests/scripts/web_game_menu_check.js)")
class ThePagesOwnCode(unittest.TestCase):
    def test_the_menu_with_a_strict_fake_of_the_dom(self):
        done = subprocess.run([shutil.which("node"), CHECK_JS, SCRIPT, DATA], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r"\d+ checks, 0 failures")


@unittest.skipUnless(shutil.which("node"), "node is not installed: the mutants of the page's script were NOT run")
class TheMutants(unittest.TestCase):
    def test_the_check_can_fail_on_every_broken_copy_of_the_script(self):
        """tests/data/game_menu_mutants.json (tools/mutate.py runs it too, with `--keep-going --jobs 1`, the game's side as well): each broken copy of the script must fail the page's check"""
        spec = json.loads(read(os.path.join(DATA, "game_menu_mutants.json")))
        ids = [m["id"] for m in spec["mutants"]]
        self.assertEqual(len(ids), len(set(ids)), "an id twice")
        mine = [m for m in spec["mutants"] if m["file"] == "web/game_menu.js"]
        self.assertGreaterEqual(len(mine), 60)
        self.assertGreaterEqual(len(spec["mutants"]) - len(mine), 30, "the game's side has its own")
        text = read(SCRIPT)
        with tempfile.TemporaryDirectory() as tmp:
            copy = os.path.join(tmp, "game_menu.js")
            for m in mine:
                with self.subTest(m["id"]):
                    self.assertEqual(text.count(m["old"]), 1, "%s: the text is not in the script exactly once" % m["id"])
                    with open(copy, "w", encoding="utf-8", newline="") as f:
                        f.write(text.replace(m["old"], m["new"]))
                    done = subprocess.run([shutil.which("node"), CHECK_JS, copy, DATA], capture_output=True, text=True, timeout=120)
                    self.assertNotEqual(done.returncode, 0, "the check did not notice: " + m["id"])


class TheBrowserCheck(unittest.TestCase):
    def test_a_missing_page_address_is_status_2(self):
        done = subprocess.run([sys.executable, BROWSER_CHECK], capture_output=True, text=True, timeout=60)
        self.assertEqual(done.returncode, 2, done.stdout + done.stderr)

    def test_the_runner_says_skip_without_a_page(self):
        env = dict(os.environ)
        env.pop("ANTS_WEB_URL", None)
        done = subprocess.run(["bash", BROWSER_RUNNER], capture_output=True, text=True, timeout=60, env=env)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("SKIP", done.stdout)

    def test_the_check_names_what_it_looks_at(self):
        text = read(BROWSER_CHECK)
        for part in ("game-menu", "settings-btn-narrow", "settings-btn-bar", "Module._ants_menu_get(100)", "gm-leave", "embed=1", "390, 844", "844, 390", "hold_esc"):
            self.assertIn(part, text)


if __name__ == "__main__":
    unittest.main()
