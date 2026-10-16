#!/usr/bin/env python3
"""The pages' side of the rematch of a lobby room (web/front/rematch.js and rematch.css, wired into web/shell.html and web/lobby.html; run by ./run_tests.sh --fast, with the other python tests of
tests/scripts, and by the CI). When the rules end the match that a lobby room began, the game server opens a lobby room with the same code at once, with the match's plan and a seat kept for each person
of the match who played in a browser (docs/SERVER.md "Lobby rooms"); the game page shows "The match is over" under the picture with "Back to the lobby", and the front page that the button opens shows
"Back from the match." at the top, with the words of each colour and the note under START (docs/PLAY_IN_BROWSER.md "After a match").

  - tests/scripts/web_rematch_check.js runs the library without a browser, on tables and with a small fake of the DOM (a name is TEXT: the fake throws when markup is assigned), the game page's strip
    with the real client of web/front/lobby_net.js on a fake socket; it reads the game's own text of a match that is over from tests/data/match_over.txt (test_network_app N5.99 holds the game to it);
  - the pages: the strip's element, the stylesheet and the scripts in their places, the hooks (the game's word, the layout that counts the strip, the front page's strip, the note under START, START
    that waits), and the limits of tools/check_hygiene.py for the two pages;
  - the stylesheet: the classes that the scripts set, in the colours of the approved pictures;
  - the mutants of tests/data/rematch_mutants.json that need no build (the pages' own: rematch.js, lobby_net.js, lobby_rules.js) are run here on a COPY of the files, one at a time: each must make its
    check fail; the C++ ones are run by tools/mutate.py (the deep tier).
The node check needs node; without it these tests are skipped and say so.
"""
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCRIPTS = os.path.join(REPO, "tests", "scripts")
FRONT = os.path.join(REPO, "web", "front")
CHECK = os.path.join(SCRIPTS, "web_rematch_check.js")
REMATCH = os.path.join(FRONT, "rematch.js")
CSS = os.path.join(FRONT, "rematch.css")
NET = os.path.join(FRONT, "lobby_net.js")
GOLDEN = os.path.join(REPO, "tests", "data", "match_over.txt")
LOBBY = os.path.join(REPO, "web", "lobby.html")
SHELL = os.path.join(REPO, "web", "shell.html")
NODE = shutil.which("node")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def run_node(*args):
    done = subprocess.run([NODE, *args], capture_output=True, text=True, timeout=300)
    return done.returncode, done.stdout + done.stderr


def code_of(text):
    """the script without its comments and the words of the page (for the rules about what the code may use)"""
    text = "\n".join(re.sub(r"^\s*//.*$", "", line) for line in text.splitlines())
    text = re.sub(r"'(?:[^'\\\n]|\\.)*'|\"(?:[^\"\\\n]|\\.)*\"", "''", text)
    return re.sub(r"//.*$", "", text, flags=re.M)


class TheScript(unittest.TestCase):
    def test_it_is_a_plain_script_with_no_dependency(self):
        text = read(REMATCH)
        self.assertNotRegex(text, r"(?m)^\s*(import|export)\s", "a page loads it with a script tag: no module syntax")
        self.assertNotRegex(text, r"\brequire\(", "no dependency")
        self.assertIn("module.exports", text)
        self.assertIn("root.AntsRematch = api", text)

    def test_it_is_written_in_es5(self):
        code = code_of(read(REMATCH))
        for token, why in ((r"=>", "an arrow function"), (r"\bconst\b", "const"), (r"\blet\b", "let"), (r"`", "a template string"), (r"\bclass\b", "a class"), (r"\.\.\.", "a spread")):
            self.assertIsNone(re.search(token, code), "rematch.js is ES5: it has " + why)

    def test_a_name_is_never_put_into_markup(self):
        code = code_of(read(REMATCH))
        for token in ("innerHTML", "outerHTML", "insertAdjacentHTML", "document.write", "createContextualFragment", "DOMParser", r"\beval\(", r"new Function", "setTimeout\\(\\s*['\"]"):
            self.assertIsNone(re.search(token, code), "rematch.js uses " + token)
        self.assertIn("textContent", code)

    def test_it_names_no_other_site_and_keeps_the_key_in_the_tab(self):
        """the key of the seat goes from the game's own entry to the seat's Hello and to this tab's session storage, nowhere else: no address of another site, no cookie, no local storage written"""
        text = read(REMATCH)
        self.assertNotRegex(text, r"https?://")
        code = code_of(text)
        self.assertNotRegex(code, r"document\.cookie|indexedDB|postMessage|sendBeacon")
        self.assertEqual(re.findall(r"(?<!function )storageSet\((\w+)", code), ["session", "session"], "only the session storage is written (the two hand-over entries)")


class ThePages(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.shell = read(SHELL)
        cls.lobby = read(LOBBY)

    def test_the_game_page_has_the_strip_between_the_picture_and_the_picture_row(self):
        page = self.shell
        strip = page.index('<section class="rematch" id="rematch" aria-label="The match is over" hidden></section>')
        self.assertLess(page.index('<div id="game-stage"'), strip)
        self.assertLess(strip, page.index('<div class="view-bar" id="view-bar">'))
        self.assertIn('<link rel="stylesheet" href="front/rematch.css">', page)

    def test_the_game_page_loads_the_client_and_the_strip_before_it_listens(self):
        page = self.shell
        net, rematch = page.index('<script src="front/lobby_net.js"></script>'), page.index('<script src="front/rematch.js"></script>')
        hook = page.index("AntsRematch.gamePage({ win: window, doc: document, Net: window.AntsLobbyNet, host: document.getElementById('rematch') })")
        self.assertLess(net, rematch)
        self.assertLess(rematch, hook)
        self.assertLess(hook, page.index("// NAME_BEGIN"))
        self.assertGreater(hook, page.index("// ANTS_PAGE_END"), "the hook is outside the blocks that other checks run")

    def test_the_layout_of_the_game_page_counts_the_strip(self):
        """the picture's box is what is left of the window: the strip under it takes its height (measureChrome), and a change of its size lays the box out again"""
        page = self.shell
        measure = page[page.index("function measureChrome()"):page.index("function layoutBox(")]
        self.assertIn("var strip = document.getElementById('rematch'); if (strip && !strip.hidden && !ANTS_REPLAY_MODE) barHeight += strip.offsetHeight", measure)
        self.assertIn("observer.observe(document.getElementById('rematch'));", page)
        self.assertIn("function relayout() { try { win.dispatchEvent(new win.Event('resize')); }", read(REMATCH))

    def test_the_front_page_loads_the_strip_and_uses_it(self):
        page = self.lobby
        self.assertIn('<link rel="stylesheet" href="front/rematch.css">', page)
        self.assertLess(page.index('<script src="front/lobby_rules.js"></script>'), page.index('<script src="front/rematch.js"></script>'))
        self.assertIn('<p class="start-note" id="start-note" role="status" hidden></p>', page)
        self.assertIn("var Rematch = window.AntsRematch;", page)
        self.assertIn('<div class="screen" id="screen">', page)
        self.assertIn("Rematch.frontStrip({ doc: document, before: $('screen'),", page)
        self.assertIn("back = Rematch.readBack(window.sessionStorage, stored.code, Date.now());", page)
        self.assertIn("community: communityOf, back: back }", page)
        self.assertIn("setText($('start-note'), side.note); $('start-note').hidden = !side.note;", page)
        self.assertIn("showBanner('host', { guest: !e.you, leaver: e.before, host: e.name, back: e.back })", page)

    def test_the_front_page_lets_the_match_go_at_start_and_for_another_room(self):
        page = self.lobby
        self.assertIn("forgetSession(); leaveBack();              // (Back from the match makes a room again", page)
        self.assertIn("function startOwn(kind) {\n            forgetSession(); leaveBack();", page)
        self.assertIn("c.on('gone', mine(function () { lastStarting = false; leaveBack(); }));", page)
        self.assertIn("if (back && p.code !== back.code) leaveBack();", page)

    def test_the_pages_stay_within_their_limits(self):
        """tools/check_hygiene.py holds the two pages to their sizes: the rematch's code is in front/rematch.js and front/rematch.css"""
        self.assertLessEqual(len(self.shell.splitlines()), 3989)
        self.assertLessEqual(len(self.lobby.splitlines()), 1500)


class TheStylesheet(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.css = read(CSS)

    def test_every_class_that_the_scripts_set_has_a_rule(self):
        for selector in (".rematch {", ".rematch[hidden]", ".rmt-title {", ".rmt-text {", ".rmt-who {", ".rmt-chip {", ".rmt-chip i {", ".rmt-chip.away {", ".rmt-go {", ".rmt-go[hidden]", ".rematch .btn.big {",
                         ".rmt-note {", ".backbar {", ".backbar b {", ".backbar a {", ".pstat.back::before {", ".pstat.away::before {", ".start-note {", ".start-note[hidden]"):
            self.assertIn(selector, self.css)

    def test_the_colours_of_the_pictures(self):
        """picture R1: the panel in the gold frame, the chips; picture R2: the green strip at the top, the dots of back and away, the gold note under START"""
        css = self.css
        self.assertRegex(css, r"\.rematch \{[^}]*border: 3px solid var\(--gold\)")
        self.assertRegex(css, r"\.rmt-chip \{[^}]*background: #0b1612; border: 1px solid #1d4a3d")
        self.assertRegex(css, r"\.backbar \{[^}]*color: #d6ffe8; background: #173b2c; border-bottom: 3px solid #2f9a68")
        self.assertRegex(css, r"\.pstat\.back::before \{ background: #6fe3a4; \}")
        self.assertRegex(css, r"\.start-note \{[^}]*color: #ffe9a8; background: #0b1612; border: 1px solid var\(--gold\)")

    def test_a_narrow_window_has_one_column(self):
        self.assertRegex(self.css, r"@media \(max-width: 760px\) \{\s*\.rematch \{ grid-template-columns: minmax\(0, 1fr\);")


@unittest.skipUnless(NODE, "node is not installed: the rematch's pages were NOT run (tests/scripts/web_rematch_check.js)")
class TheChecksThatNodeRuns(unittest.TestCase):
    def test_the_strips_keep_to_the_tables_and_the_fake_pages(self):
        status, out = run_node(CHECK, REMATCH, NET, GOLDEN, LOBBY)
        self.assertEqual(status, 0, out[-6000:])
        self.assertRegex(out, r"\b[1-9][0-9]* checks, 0 failed")

    def test_the_check_says_how_to_use_it_when_it_has_no_arguments(self):
        status, out = run_node(CHECK)
        self.assertEqual(status, 2)
        self.assertIn("usage: web_rematch_check.js", out)


@unittest.skipUnless(NODE, "node is not installed: the rematch's pages were NOT run (tests/scripts/web_rematch_check.js)")
class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/rematch_mutants.json that need no build, on a COPY of the files, one at a time: each must make its check fail (a command that exits non-zero), and the copy with
    nothing changed must pass, so that a failure is the change's"""

    FILES = ("web/front/rematch.js", "web/front/lobby_net.js", "web/front/lobby_rules.js", "web/front/lobby_chat.js", "web/lobby.html", "web/shell.html", "tests/data/match_over.txt", "tests/data/lobby_messages.txt",
             "tests/scripts/web_rematch_check.js", "tests/scripts/web_lobby_rules_check.js", "tests/scripts/web_lobby_client_check.js", "tests/scripts/web_lobby_net_check.js", "tests/scripts/web_name_check.js",
             "include/ants_net/flood.hpp", "include/ants_net/protocol.hpp")      # (the client check reads the server's limits, the rules check the server's sentences)

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "rematch_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.pages = [m for m in cls.spec["mutants"] if "build" not in m]
        cls.folder = tempfile.mkdtemp(prefix="ants_rematch_mutants.")
        for name in cls.FILES:
            os.makedirs(os.path.dirname(os.path.join(cls.folder, *name.split("/"))), exist_ok=True)
            shutil.copy(os.path.join(REPO, *name.split("/")), os.path.join(cls.folder, *name.split("/")))
        maps = os.path.join(REPO, "Original-Ants", "Maps")                           # (the rules check reads the level files of the six maps and looks for their pictures)
        os.makedirs(os.path.join(cls.folder, "Original-Ants", "Maps"))
        for name in os.listdir(maps):
            if name.endswith(".LVL"):
                shutil.copy(os.path.join(maps, name), os.path.join(cls.folder, "Original-Ants", "Maps", name))
        for name in os.listdir(FRONT):
            if re.match(r"preview_[a-z]+\.png$", name):
                open(os.path.join(cls.folder, "web", "front", name), "wb").close()

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.folder, ignore_errors=True)

    def run_command(self, command):
        done = subprocess.run(command, shell=True, cwd=self.folder, capture_output=True, text=True, timeout=300)
        return done.returncode, done.stdout + done.stderr

    def test_the_copy_with_nothing_changed_passes_every_command(self):
        for command in sorted({m["test"] for m in self.pages}):
            status, out = self.run_command(command)
            self.assertEqual(status, 0, command + "\n" + out[-3000:])

    def test_the_list_is_whole(self):
        ids = [m["id"] for m in self.spec["mutants"]]
        self.assertEqual(len(ids), len(set(ids)), "an id twice")
        self.assertGreaterEqual(len(self.pages), 30)
        self.assertGreaterEqual(len(ids) - len(self.pages), 25, "the server's, the room's and the game's own mutants (run by tools/mutate.py)")
        for needed in ("lobby_reserved_seat_leads", "lobby_start_while_reserved", "lobby_newcomer_before_anybody_is_back", "lobby_desktop_seat_kept", "lobby_reservation_ends_early", "server_no_rematch",
                       "server_rematch_after_any_end", "server_rematch_without_a_place", "app_page_told_after_the_key_is_gone", "page_back_says_leave", "page_desktop_on_the_results_screen",
                       "net_key_only_takes_another_seat", "rules_desktop_on_the_results_screen", "rules_note_for_a_player"):
            self.assertIn(needed, ids)
        for m in self.spec["mutants"]:
            self.assertEqual(read(os.path.join(REPO, *m["file"].split("/"))).count(m["old"]), 1, "%s: the text to change is not in %s exactly once" % (m["id"], m["file"]))

    def test_every_mutant_is_seen(self):
        missed = []
        for m in self.pages:
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
        self.assertEqual(missed, [], "mutants that no check saw")


if __name__ == "__main__":
    unittest.main()
