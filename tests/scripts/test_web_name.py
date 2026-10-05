#!/usr/bin/env python3
"""The player's name on the web pages (run by ./run_tests.sh --fast and by the CI). The owner: "the ability for somebody to type in their name to beta.playants.org ... so their name goes into
the game instead of random", and "when joining a link from somebody else, it should ask you first what you want your name to be".

  - the pages' own code is RUN (node, when it is installed): the rules of a name (the desktop start menu's), the name step, the gate that holds the game of a shared link back until the name
    is chosen, and the whole of web/lobby.html with a small fake of the browser's DOM: the shared field is remembered and filled in, its name goes to the seat that this person plays, bad
    names start nothing, a name with < > & is only ever text, an empty field falls back to the random names, a shared link asks first (tests/scripts/web_name_check.js);
  - what needs no browser is read from the files: the old join-only field is gone, the one field is there, docs/PLAY_IN_BROWSER.md and docs/NETWORK_PORT.md say what the page does.
"""
import os
import re
import shutil
import subprocess
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SHELL = os.path.join(REPO, "web", "shell.html")
LOBBY = os.path.join(REPO, "web", "lobby.html")
CHECK_JS = os.path.join(REPO, "tests", "scripts", "web_name_check.js")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


class TheMarkup(unittest.TestCase):
    def test_the_front_page_has_one_name_field_for_starting_inviting_and_joining(self):
        page = read(LOBBY)
        self.assertEqual(len(re.findall(r'<input[^>]*id="player-name"', page)), 1)
        self.assertNotIn('id="join-name"', page)                                           # (the field that only the Join form had)
        self.assertNotIn("ants-four-name", page)
        self.assertRegex(page, r'<section id="who"[^>]*>')
        field = re.search(r'<input[^>]*id="player-name"[^>]*>', page).group(0)
        self.assertIn('maxlength="32"', field)
        self.assertIn('placeholder="Player"', field)
        self.assertLess(page.index('id="who"'), page.index('id="cards"'))                   # near the top: before the card (a match with its seats, its invitations and START) and the line of a join
        self.assertLess(page.index('id="who"'), page.index('id="join-code"'))
        self.assertLess(page.index('id="who"'), page.index('id="map-pick"'))
        self.assertRegex(page, r'<label class="lab" for="player-name">Your name</label>')      # (the field has a visible label)

    def test_the_page_makes_no_markup_from_text(self):
        page = read(LOBBY)
        self.assertNotRegex(page, r"\.innerHTML\s*[+]?=")
        self.assertNotIn("document.write", page)
        self.assertNotIn("insertAdjacentHTML", page)

    def test_the_game_page_has_the_card_of_the_step_and_starts_the_game_through_the_gate(self):
        page = read(SHELL)
        self.assertRegex(page, r'<div class="name-modal" id="name-step"[^>]*hidden>')
        self.assertEqual(len(re.findall(r"ANTS_NAME_GATE\.when\(startGame\)", page)), 2)
        self.assertEqual(len(re.findall(r"\bstartGame\(\)", page)), 1)                      # only the definition: function startGame()


class TheDocuments(unittest.TestCase):
    def test_the_browser_page_and_the_network_notes_say_what_the_pages_do(self):
        page = read(os.path.join(REPO, "docs", "PLAY_IN_BROWSER.md"))
        self.assertIn("`ants.name`", page)
        self.assertIn("asks for your name first, every time", page)
        notes = read(os.path.join(REPO, "docs", "NETWORK_PORT.md"))
        self.assertIn("asksForName", notes)
        self.assertIn("no `name` parameter at all", notes)


@unittest.skipUnless(shutil.which("node"), "node is not installed: the pages' own code for the player's name was NOT run (tests/scripts/web_name_check.js)")
class ThePagesOwnCode(unittest.TestCase):
    def test_the_rules_the_step_the_gate_and_the_whole_of_the_play_online_page(self):
        done = subprocess.run([shutil.which("node"), CHECK_JS, SHELL, LOBBY], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn(" 0 failures", done.stdout)


if __name__ == "__main__":
    unittest.main()
