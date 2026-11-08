#!/usr/bin/env python3
"""The player's name on the web pages (run by ./run_tests.sh --fast and by the CI). The requirement: nobody types a name on beta.playants.org. A person who is logged in with Discord plays under the
login's name, made to fit the game's rules; everybody else plays under a guest name ("Guest" and six digits) that the browser keeps; a link from somebody else still asks first, with a card that shows the
name and cannot be typed in. The name comes from front/player_name.js (tests/scripts/test_web_player_name.py).

  - the pages' own code is RUN (node, when it is installed): the rules of a name (the desktop start menu's), the name step, the gate that holds the game of a shared link back until its card is done, and the
    whole of web/lobby.html (the lobby: a room on the game server) with a small fake of the browser's DOM, its real scripts and a scripted server and hub: a plain visit makes a room under the login's name or the
    guest name, a name that the browser remembered from earlier typing is ignored, a hub that does not answer in time, a log out without a reload, a seat that is held, a shared link and a typed code ask first and
    start nothing before the card is done, a link made for somebody else carries no name, START hands the seat over with the key in the storage (tests/scripts/web_name_check.js);
  - what needs no browser is read from the files: the name card is the front page's only name field and it is read-only, no page writes a name into the browser's storage, the page makes no markup from text,
    the recording notice is in the footer and in the card, docs/PLAY_IN_BROWSER.md and docs/NETWORK_PORT.md say what the page does.
  What the fake DOM cannot say (the look, the phone widths, the pointer, the real focus, the real WebSocket) is the job of the browser checks.
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
    def test_the_front_page_has_one_name_card_and_no_other_field_for_a_name(self):
        page = read(LOBBY)
        self.assertEqual(len(re.findall(r'<input[^>]*id="name-step-input"', page)), 1)
        for gone in ('id="player-name"', 'id="join-name"', 'id="who"', "ants-four-name"):         # (the one-card page's field, the Join form's own field, its section and its key)
            self.assertNotIn(gone, page)
        self.assertEqual(len([t for t in re.findall(r"<input[^>]*>", page) if re.search(r'\bid="[^"]*name[^"]*"|placeholder="[^"]*name', t, re.I)]), 1)
        field = re.search(r'<input[^>]*id="name-step-input"[^>]*>', page).group(0)
        self.assertIn(" readonly ", field)                                                    # (the name is shown, never typed)
        for typing in ("maxlength", "placeholder", "autocomplete", "autocapitalize", "spellcheck"):
            self.assertNotIn(typing, field, "a field that is not typed in has nothing for typing: " + typing)
        self.assertRegex(page, r'<label for="name-step-input">Your name</label>')                # (the field has a visible label)
        self.assertRegex(page, r'<div class="name-modal" id="name-step" role="dialog" aria-modal="true" aria-labelledby="name-step-title" hidden>')      # (a card over the page, hidden until a link or a code asks)
        for part in ("name-step-title", "name-step-go", "name-step-msg", "name-step-hint", "who-notice", "name-step-own"):
            self.assertEqual(page.count('id="%s"' % part), 1, part)
        self.assertRegex(page, r'<div class="name-step-msg" id="name-step-msg" role="alert"></div>')      # (a line about the room, such as a code that no room has, is said there, and aloud)
        self.assertLess(page.index('id="name-step-title"'), page.index('id="name-step-input"'))
        self.assertLess(page.index('id="name-step-input"'), page.index('id="name-step-go"'))
        self.assertLess(page.index('id="name-step-go"'), page.index('id="name-step-own"'))         # (and the way out is the last thing on the card)

    def test_no_page_writes_a_name_into_the_browsers_storage_and_the_guest_name_has_one_key(self):
        found, guest = {}, set()
        for base, _dirs, names in os.walk(os.path.join(REPO, "web")):
            for name in names:
                if os.path.splitext(name)[1] not in (".html", ".js"):
                    continue
                path = os.path.join(base, name)
                text = read(path)
                rel = os.path.relpath(path, REPO).replace(os.sep, "/")
                if "ants.name" in text:
                    found[rel] = re.findall(r"[\w.]*\(\s*'ants\.name'", text)
                if "ants.guest" in text:
                    guest.add(rel)
        self.assertEqual(found, {"web/shell.html": ["window.localStorage.getItem('ants.name'"]}, "a name that an earlier version remembered is read by the game page's game on this computer (an address with no name) and by nothing else, and written by nothing")
        self.assertEqual(guest, {"web/front/player_name.js"}, "the guest name's key is the one script's own")

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

    def test_both_name_screens_say_that_online_matches_are_recorded(self):
        # The game page's card says that anybody can watch the recordings, live or later; the front page, as the lobby picture draws it, says what is kept and what is public in two places:
        # in the footer, for a visitor who is not asked for a name, and in the name card (under the hint; only the step of a game on this computer hides it, by script).
        first = "Online matches are recorded."
        owner = "Recordings are kept for 30 days. Results and names are kept for good and are public."       # (the owner's own sentence: both pages say it as it is; extra words go in a sentence before it)
        game = first + " Anybody can watch them, live or later. " + owner
        front = first + " Recordings are kept for 30 days. Results and names are kept for good and are public."
        self.assertEqual(read(LOBBY).count(front), 2)
        self.assertEqual(read(SHELL).count(game), 1)
        self.assertIn('<p class="notice" id="footer-notice">' + front + "</p>", read(LOBBY))
        self.assertIn('<div class="name-step-hint name-step-notice" id="who-notice">' + front + "</div>", read(LOBBY))
        self.assertIn('<div class="name-step-hint name-step-notice">' + game + "</div>", read(SHELL))
        self.assertNotIn("Anybody can watch them", read(LOBBY))                                     # (the extra sentence is the game page's only)
        self.assertIn(owner, read(SHELL))
        self.assertIn(owner, read(LOBBY))
        docs = read(os.path.join(REPO, "docs", "SERVER.md"))                                       # (the one document that quotes them: it also says when the line is true)
        self.assertIn(front.replace("&rsquo;", "'"), docs)
        self.assertIn(game.replace("&rsquo;", "'"), docs)


class TheDocuments(unittest.TestCase):
    def test_the_browser_page_and_the_network_notes_say_what_the_pages_do(self):
        page = read(os.path.join(REPO, "docs", "PLAY_IN_BROWSER.md"))
        self.assertIn("`ants.name`", page)
        self.assertIn("asks for your name first, every time", page)
        server = read(os.path.join(REPO, "docs", "SERVER.md"))
        self.assertIn("A line says what the server does with an online match", server)      # (the notice of the lobby's footer and of both name cards, and the step that has none)
        self.assertIn("not in the step of a game on this computer", server)
        self.assertIn("`who-notice`", server)      # (what an operator who changes the line must leave where it is)
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
