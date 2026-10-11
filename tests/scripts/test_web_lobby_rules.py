#!/usr/bin/env python3
"""The lobby page's rules (web/front/lobby_rules.js; run by ./run_tests.sh --tools, with the other python tests of tests/scripts, and by the CI). The front page is the game's lobby; everything in it
that needs no browser is in this one script, so that it can be checked with no screen: the player's name (the same block as the game page's, held byte for byte), the room's code (made, shown in
two groups, read back from what a person typed), the six maps (against the level files of the original game), the model of a room (who is the host, who the guest, what each of the four cards
shows and may do, the START button and the plan sentence), the teams (the rules of the live match card: who may be on a team, the refusals, the bytes of a plan), the move of a colour, the
sentences of the server (which of them means what) and every message strip of the page.

  - tests/scripts/web_lobby_rules_check.js holds all of it to tables: every exported function is called with real inputs (the check fails when one is never called), the Room messages of the
    tables are real bytes that web/front/lobby_net.js reads, the pictures of the design are compared word for word (pictures 1, 2, 3, 5, 5b, 6, 13, 13b and the strips of 15),
    and a few hundred seeded random rooms must keep the rules that hold in every room;
  - this file also changes one thing at a time in a COPY of the library (a letter in the alphabet, a rule of the teams, a word, a number) and in the page's copies of the name block, or in a
    level file or a sentence of the server: the check must then fail, so a check that passes whatever the library does would be seen here.
The node check needs node; without it these tests are skipped and say so.
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCRIPTS = os.path.join(REPO, "tests", "scripts")
CHECK = os.path.join(SCRIPTS, "web_lobby_rules_check.js")
FRONT = os.path.join(REPO, "web", "front")
RULES = os.path.join(FRONT, "lobby_rules.js")
NET = os.path.join(FRONT, "lobby_net.js")
LOBBY = os.path.join(REPO, "web", "lobby.html")
SHELL = os.path.join(REPO, "web", "shell.html")
MAPS = os.path.join(REPO, "Original-Ants", "Maps")
PROTOCOL = os.path.join(REPO, "include", "ants_net", "protocol.hpp")
NODE = shutil.which("node")


def run_node(*args):
    done = subprocess.run([NODE, *args], capture_output=True, text=True, timeout=300)
    return done.returncode, done.stdout + done.stderr


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def fail_lines(out):
    return {line for line in out.splitlines() if line.startswith("FAIL")}


class TheFiles(unittest.TestCase):
    def test_the_script_is_a_plain_script_with_no_dependency(self):
        text = read(RULES)
        self.assertNotRegex(text, r"(?m)^\s*(import|export)\s", "a page loads it with a script tag: no module syntax")
        self.assertNotRegex(text, r"\brequire\(", "no dependency")
        self.assertIn("module.exports", text)                                  # (node loads it for the checks; a page gets the global)
        self.assertIn("root.AntsLobbyRules = api", text)

    def test_the_script_is_written_in_es5(self):
        """the page runs in any browser that the game page runs in: no arrow, no const or let, no template string, no class (the comments are cut off first)"""
        code = "\n".join(re.sub(r"^\s*//.*$", "", line) for line in read(RULES).splitlines())
        code = re.sub(r"'(?:[^'\\\n]|\\.)*'|\"(?:[^\"\\\n]|\\.)*\"", "''", code)    # (the words of the page are not code)
        code = re.sub(r"//.*$", "", code, flags=re.M)                          # (nor the comments at the end of a line)
        for token, why in ((r"=>", "an arrow function"), (r"\bconst\b", "const"), (r"\blet\b", "let"), (r"`", "a template string"), (r"\bclass\b", "a class"), (r"\.\.\.", "a spread")):
            self.assertIsNone(re.search(token, code), "lobby_rules.js is ES5: it has " + why)

    def test_the_page_loads_the_script(self):
        text = read(LOBBY)
        self.assertRegex(text, r"<script[^>]+src=\"front/lobby_rules\.js\"", "web/lobby.html loads front/lobby_rules.js with a script tag")
        self.assertRegex(text, r"<script[^>]+src=\"front/lobby_net\.js\"", "... and front/lobby_net.js, which the rules do not need but the page does")
        self.assertLess(text.index("front/lobby_rules.js"), text.index("AntsLobbyRules"), "the script is loaded before the page's own code uses it")

    def test_the_check_is_given_the_files_it_names(self):
        for path in (CHECK, RULES, NET, LOBBY, SHELL, PROTOCOL):
            self.assertTrue(os.path.isfile(path), path)
        self.assertEqual(len([n for n in os.listdir(MAPS) if n.endswith(".LVL")]), 6, "the six level files of the original game")


@unittest.skipUnless(NODE, "node is not installed: the lobby page's rules were NOT run (tests/scripts/web_lobby_rules_check.js)")
class TheChecksThatNodeRuns(unittest.TestCase):
    def test_the_rules_keep_to_the_tables_and_the_pictures(self):
        status, out = run_node(CHECK, RULES, LOBBY, SHELL)
        self.assertEqual(status, 0, out[-6000:])
        self.assertRegex(out, r"\b[1-9][0-9]* checks, 0 failed")

    def test_the_check_says_how_to_use_it_when_it_has_no_arguments(self):
        status, out = run_node(CHECK)
        self.assertEqual(status, 2)
        self.assertIn("usage: web_lobby_rules_check.js", out)

    def test_the_check_takes_the_repository_folder_as_a_fourth_argument(self):
        status, out = run_node(CHECK, RULES, LOBBY, SHELL, REPO)
        self.assertEqual(status, 0, out[-6000:])
        self.assertRegex(out, r"\b[1-9][0-9]* checks, 0 failed")


@unittest.skipUnless(NODE, "node is not installed: the lobby page's rules were NOT run (tests/scripts/web_lobby_rules_check.js)")
class TheCheckIsNotVacuous(unittest.TestCase):
    """a copy of the files with ONE thing changed: the check must fail (and a copy with nothing changed must pass, so that a failure is the change's)"""

    def tree(self, folder):
        """a repository folder of copies: web/front/lobby_rules.js and lobby_net.js, the (empty) preview pictures, web/lobby.html, web/shell.html, the level files and protocol.hpp"""
        os.makedirs(os.path.join(folder, "web", "front"))
        os.makedirs(os.path.join(folder, "Original-Ants", "Maps"))
        os.makedirs(os.path.join(folder, "include", "ants_net"))
        shutil.copy(RULES, os.path.join(folder, "web", "front", "lobby_rules.js"))
        shutil.copy(NET, os.path.join(folder, "web", "front", "lobby_net.js"))
        for name in os.listdir(FRONT):
            if re.match(r"preview_[a-z]+\.png$", name):
                open(os.path.join(folder, "web", "front", name), "wb").close()      # (the check looks for the picture; it does not read it)
        shutil.copy(LOBBY, os.path.join(folder, "web", "lobby.html"))
        shutil.copy(SHELL, os.path.join(folder, "web", "shell.html"))
        for name in os.listdir(MAPS):
            if name.endswith(".LVL"):
                shutil.copy(os.path.join(MAPS, name), os.path.join(folder, "Original-Ants", "Maps", name))
        shutil.copy(PROTOCOL, os.path.join(folder, "include", "ants_net", "protocol.hpp"))

    def run_check(self, folder):
        return run_node(CHECK, os.path.join(folder, "web", "front", "lobby_rules.js"), os.path.join(folder, "web", "lobby.html"), os.path.join(folder, "web", "shell.html"), folder)

    def change(self, relative, old, new, count=1):
        """copy the tree, replace `old` by `new` (exactly `count` times) in the one file and run the check; returns (status, output)"""
        with tempfile.TemporaryDirectory() as folder:
            self.tree(folder)
            path = os.path.join(folder, *relative.split("/"))
            text = read(path)
            self.assertEqual(text.count(old), count, "%s has %d of %r, the test expects %d" % (relative, text.count(old), old[:60], count))
            with open(path, "w", encoding="utf-8", newline="") as f:
                f.write(text.replace(old, new))
            return self.run_check(folder)

    baseline = None

    def baseline_fails(self):
        """the FAIL lines of the copy with nothing changed (none, when the library is right): a change is seen when the check says something that it did not say before"""
        if TheCheckIsNotVacuous.baseline is None:
            with tempfile.TemporaryDirectory() as folder:
                self.tree(folder)
                TheCheckIsNotVacuous.baseline = fail_lines(self.run_check(folder)[1])
        return TheCheckIsNotVacuous.baseline

    def assert_seen(self, status, out, what):
        self.assertNotEqual(status, 0, "the check did not see " + what)
        new = fail_lines(out) - self.baseline_fails()
        self.assertTrue(new, "the check failed, but only with what it says about the unchanged copy: it did not see " + what)

    def must_fail(self, relative, old, new, count=1):
        status, out = self.change(relative, old, new, count)
        self.assert_seen(status, out, "the change of %r to %r in %s" % (old[:50], new[:50], relative))

    def test_a_copy_with_nothing_changed_passes(self):
        with tempfile.TemporaryDirectory() as folder:
            self.tree(folder)
            status, out = self.run_check(folder)
        self.assertEqual(status, 0, out[-6000:])
        self.assertRegex(out, r"\b[1-9][0-9]* checks, 0 failed")

    # ---- the library ------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    def test_a_letter_that_is_hard_to_read_in_the_alphabet_of_the_codes(self):
        self.must_fail("web/front/lobby_rules.js", "'abcdefghjkmnpqrstuvwxyz23456789'", "'abcdefghijkmnpqrstuvwxyz23456789'")

    def test_a_code_of_five_symbols(self):
        self.must_fail("web/front/lobby_rules.js", "for (var i = 0; i < 6; i++) {", "for (var i = 0; i < 5; i++) {")

    def test_a_name_of_32_characters_that_is_too_long(self):
        self.must_fail("web/front/lobby_rules.js", "name.length > 32", "name.length > 31")

    def test_three_players_on_one_button_of_the_teams(self):
        self.must_fail("web/front/lobby_rules.js", ".length > 2; });", ".length > 3; });")

    def test_the_sentence_of_the_teams(self):
        self.must_fail("web/front/lobby_rules.js", "' against '", "' vs '")

    def test_the_seats_in_the_order_of_the_server_instead_of_the_cards(self):
        self.must_fail("web/front/lobby_rules.js", "var GRID = [3, 0, 1, 2];", "var GRID = [0, 1, 2, 3];")

    def test_the_map_info_line_of_a_map(self):
        self.must_fail("web/front/lobby_rules.js", "Race for your life! (10 min)", "Race for your life! (9 min)")

    # ---- the name block that the pages carry (byte for byte the same in both) ----------------------------------------------------------------------------------------------------------------
    def test_the_name_block_of_the_lobby_page_changed(self):
        self.must_fail("web/lobby.html", "'A name has at most '", "'A name can have at most '")

    def test_the_name_block_of_the_game_page_changed(self):
        self.must_fail("web/shell.html", "var NAME_MAX = 32;", "var NAME_MAX = 31;")

    # ---- what the library is held to: the level files and the server's sentences -----------------------------------------------------------------------------------------------------------------
    def test_a_level_file_with_other_minutes(self):
        with tempfile.TemporaryDirectory() as folder:
            self.tree(folder)
            path = os.path.join(folder, "Original-Ants", "Maps", "TREASURE.LVL")
            with open(path, "rb") as f:
                data = bytearray(f.read())
            data[8] = (data[8] + 1) % 256                                        # (the minutes of the Map Info line: a u16 at byte 8)
            with open(path, "wb") as f:
                f.write(data)
            status, out = self.run_check(folder)
        self.assert_seen(status, out, "a level file with other minutes")

    def test_a_missing_level_file(self):
        with tempfile.TemporaryDirectory() as folder:
            self.tree(folder)
            os.remove(os.path.join(folder, "Original-Ants", "Maps", "ISLANDS.LVL"))
            status, out = self.run_check(folder)
        self.assert_seen(status, out, "a missing level file")

    def test_a_sentence_of_the_server_that_changed(self):
        self.must_fail("include/ants_net/protocol.hpp", "This map cannot be played with these colours.", "This map cannot be played with these colors.")


if __name__ == "__main__":
    unittest.main()
