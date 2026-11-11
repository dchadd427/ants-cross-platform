#!/usr/bin/env python3
"""The name that a person plays under on the web (web/front/player_name.js; run by ./run_tests.sh --fast and by the CI). Nobody types a name on the front page or on the game page any more: a person
who is logged in with Discord plays under the login's name, made to fit the game's name rules, and everybody else under a guest name that this browser keeps. The page asks the site's own
`GET /hub/me` when it opens and again at every seat while the answer was a login, so a log out in the chat (which leaves the page where it is) is known at the next room.

  - the script is read from the file: a plain ES5 script with no dependency, that asks the one page /hub/me with the cookie of this site (never a socket, never another page of the hub), keeps
    one key in localStorage (ants.guest) and nothing in the tab, writes no markup and touches no element;
  - the code RUNS in node (tests/scripts/web_player_name_check.js): the fitting of a login's name on a table (accents, full-width and fancy letters, emoji and other scripts, controls, blanks, 2 to 32
    characters, a computer player's name), what the answer of /hub/me says in the shape of today and in the shape with an other name, the guest name (six digits, the whole range, valid and invalid
    stored values, a browser that refuses its storage), and the person: one question shared by everybody who waits, a login asked again at every seat, a guest and a site with no login never asked
    again, a failed question asked again at the next seat only, the limit of the wait and the answers that come late;
  - tests/data/player_name_mutants.json changes one thing at a time in a COPY of the files: the checks must then fail. The whole list is run by tools/mutate.py (`--keep-going --jobs 1`); this file runs
    a spread of it in the quick tier (one or more for every rule) and keeps the whole list well made (each text is in its file exactly once).
The node checks need node; without it these tests are skipped and say so.
"""
import gzip
import json
import os
import re
import shutil
import sys
import subprocess
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCRIPTS = os.path.join(REPO, "tests", "scripts")
MODULE = os.path.join(REPO, "web", "front", "player_name.js")
LOBBY = os.path.join(REPO, "web", "lobby.html")
CHECK = os.path.join(SCRIPTS, "web_player_name_check.js")
SPEC = os.path.join(REPO, "tests", "data", "player_name_mutants.json")
NODE = shutil.which("node")
sys.path.insert(0, os.path.join(REPO, "tools"))
from size_limits import SCRIPT_BUDGET, SCRIPT_GZIP_BUDGET  # noqa: E402  (every byte budget: tools/size_limits.py)


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def code_of(js):
    return re.sub(r"(?m)^\s*//.*$", "", re.sub(r"/\*.*?\*/", "", re.sub(r"(?<![:'\"])//[^\n]*", "", js), flags=re.S))


def run_node(*args, cwd=None):
    done = subprocess.run([NODE, *args], capture_output=True, text=True, timeout=300, cwd=cwd)
    return done.returncode, done.stdout + done.stderr


class TheScript(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.js = read(MODULE)
        cls.code = code_of(cls.js)

    def test_it_is_a_plain_es5_script_with_no_dependency(self):
        self.assertNotRegex(self.js, r"(?m)^\s*(import|export)\s", "a page loads it with a script tag: no module syntax")
        self.assertNotRegex(self.code, r"\brequire\(", "no dependency")
        self.assertIn("module.exports = api", self.js)                       # (node loads it for the checks; a page gets the global)
        self.assertIn("root.AntsPlayerName = api", self.js)
        self.assertNotRegex(self.code, r"=>|\blet\b|\bconst\b|\basync\b|\bawait\b|`|\.\.\.|\bclass\b", "ES5, as the site's other scripts")

    def test_it_is_ascii_text_only(self):
        self.assertFalse(re.search(r"[^\x00-\x7f]", self.js), "no invisible or look-alike character in the file: a character is written as an escape")
        self.assertFalse(re.search(r"[^\x00-\x7f]", read(CHECK)))
        self.assertFalse(re.search(r"[^\x00-\x7f]", read(SPEC)))

    def test_it_asks_for_one_page_of_the_hub_and_never_opens_a_socket(self):
        self.assertEqual(re.findall(r"""['"](/[A-Za-z0-9_./-]*)['"]""", self.code), ["/hub/me"], "the one address in the script")
        self.assertNotRegex(self.code, r"WebSocket|/hub/ws|/hub/login|/hub/logout|/hub/forget|https?://|XMLHttpRequest|navigator\.|postMessage|BroadcastChannel")
        self.assertIn("var ME = '/hub/me';", self.js)
        self.assertIn("credentials: 'same-origin', cache: 'no-store', redirect: 'error'", self.js, "the cookie of this site only, never from a cache, no redirect followed")
        self.assertEqual(len(re.findall(r"\benv\.fetch\(", self.code)), 1, "one place asks")

    def test_it_keeps_one_key_in_the_browser_and_nothing_in_the_tab(self):
        self.assertNotRegex(self.code, r"sessionStorage|indexedDB|document\.cookie")
        self.assertEqual(re.findall(r"\bwin\.(\w+)", self.code), ["fetch", "fetch", "localStorage", "AbortController"], "the page's window gives the script its fetch, its localStorage and its AbortController, and nothing else")
        self.assertEqual(re.findall(r"""setItem\((\w+),""", self.code), ["GUEST_KEY"])
        self.assertIn("var GUEST_KEY = 'ants.guest';", self.js)

    def test_it_writes_no_markup_and_touches_no_element(self):
        self.assertNotRegex(self.code, r"innerHTML|outerHTML|insertAdjacentHTML|document\.|\bwindow\.|createElement|textContent|\beval\b|new Function|setTimeout\(\s*['\"]")

    def test_it_stays_small(self):
        data = self.js.encode("utf-8")
        self.assertLess(len(data), SCRIPT_BUDGET, "player_name.js is %d bytes" % len(data))                         # (the folder's own budget, tests/scripts/test_web_front.py, holds all of web/front/ too)
        self.assertLess(len(gzip.compress(data, 9)), SCRIPT_GZIP_BUDGET, "and %d bytes gzipped" % len(gzip.compress(data, 9)))

    def test_a_name_that_the_hub_says_is_cut_before_any_work(self):
        self.assertIn("var s = text.slice(0, CUT);", self.js)
        self.assertLess(self.js.index("var s = text.slice(0, CUT);"), self.js.index(".normalize('NFKD')"))


@unittest.skipUnless(NODE, "node is not installed: the name that a person plays under was NOT run (tests/scripts/web_player_name_check.js)")
class TheCheckThatNodeRuns(unittest.TestCase):
    def test_every_check_holds(self):
        status, out = run_node(CHECK, MODULE, LOBBY)
        self.assertEqual(status, 0, out[-3000:])
        self.assertRegex(out, r"\b[1-9][0-9]* checks, 0 failed")


@unittest.skipUnless(NODE, "node is not installed: the mutants of the name that a person plays under were NOT run (tests/data/player_name_mutants.json)")
class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/player_name_mutants.json on a COPY of the files, one at a time: each must make its command fail (a command that exits non-zero), and the copy with nothing changed must
    pass, so that a failure is the change's. The whole list runs with tools/mutate.py; the quick tier runs the ones of QUICK (every rule has one) and keeps the list well made."""

    FILES = ("web/front/player_name.js", "web/lobby.html", "web/shell.html", "web/front/lobby_rules.js", "web/front/lobby_net.js", "web/front/lobby_chat.js", "web/front/rematch.js", "web/front/game_modes.js", "web/front/game_type.js",
             "web/shapes.js", "web/home/home.js", "web/home/games.js", "tests/scripts/web_player_name_check.js", "tests/scripts/web_name_check.js", "tests/scripts/web_page_harness.js")
    QUICK = ("fit_without_nfkd", "fit_keeps_non_ascii", "fit_cuts_a_long_name", "fit_ignores_the_checker", "me_prefers_the_other_name", "me_guest_is_failed", "guest_five_digits", "guest_not_kept_in_storage",
             "login_not_asked_again", "guest_asked_again", "question_not_shared", "known_for_a_login", "no_limit_on_the_wait", "late_answer_is_heard", "not_found_is_a_failure", "request_without_the_cookie", "clock_called_as_a_method",
             "page_seat_connects_without_the_name", "page_name_of_a_new_seat_is_the_rooms", "page_connect_skips_open", "page_rejoin_with_a_name", "page_card_not_read_only", "name_block_lobby_goes_on_without_the_button",
             "name_block_lobby_hands_on_what_was_typed", "shell_gate_gives_no_name_to_the_game")

    @classmethod
    def setUpClass(cls):
        with open(SPEC, encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_player_name_mutants.")
        for name in cls.FILES:
            os.makedirs(os.path.dirname(os.path.join(cls.folder, *name.split("/"))), exist_ok=True)
            shutil.copy(os.path.join(REPO, *name.split("/")), os.path.join(cls.folder, *name.split("/")))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.folder, ignore_errors=True)

    def run_command(self, command):
        done = subprocess.run(command, shell=True, cwd=self.folder, capture_output=True, text=True, timeout=120)
        return done.returncode, done.stdout + done.stderr

    def test_the_list_is_well_made(self):
        ids = [m["id"] for m in self.spec["mutants"]]
        self.assertEqual(len(ids), len(set(ids)), "an id twice")
        for m in self.spec["mutants"]:
            text = read(os.path.join(REPO, *m["file"].split("/")))
            self.assertEqual(text.count(m["old"]), 1, "%s: %r is not in %s exactly once" % (m["id"], m["old"][:60], m["file"]))
            self.assertNotEqual(m["old"], m["new"], m["id"])
            self.assertTrue(m["test"].startswith("node tests/scripts/"), m["id"])
        for needed in self.QUICK:
            self.assertIn(needed, ids, "the quick tier names a mutant that the list does not have: " + needed)

    def test_the_copy_with_nothing_changed_passes_every_command(self):
        for command in sorted({m["test"] for m in self.spec["mutants"]}):
            status, out = self.run_command(command)
            self.assertEqual(status, 0, command + "\n" + out[-3000:])

    def test_the_quick_mutants_are_seen(self):
        missed = []
        for m in self.spec["mutants"]:
            if m["id"] not in self.QUICK:
                continue
            path = os.path.join(self.folder, *m["file"].split("/"))
            text = read(path)
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
