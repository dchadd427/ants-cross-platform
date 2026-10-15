#!/usr/bin/env python3
"""The chat of the lobby page (web/front/lobby_chat.js and lobby_chat.css, the client's say() in web/front/lobby_net.js; run by ./run_tests.sh --fast, with the other python tests of tests/scripts,
and by the CI). The people in a room talk before START: a panel under the colours with the room's lines in the colours of the seats, a field, the small lines of the system ("Sam joined as Red."),
a toast for a line that arrives while the panel is off screen. The room's own chat is the game's (docs/NETWORK_PORT.md "Chat in the waiting room"); the server already relays a page's line, so
nothing of the server or of the protocol changed (network protocol 21).

  - tests/scripts/web_lobby_chat_check.js runs the library without a browser: what may be typed (printable ASCII, 100 characters, no empty line), what the box says in each state of the room, the
    words, and the whole box with a small fake of the DOM (names and lines are TEXT: the fake throws when markup is assigned; the last 200 lines; the follow of the newest line; the toast; what a
    line that the client does not take does; the field shut while the link is down or START waits; the page changing its client);
  - the page: the new section, the stylesheet, the script in its place, the hooks, and the limit of 1500 lines of tools/check_hygiene.py (web/lobby.html is at it: the map list's own stylesheet moved
    to its own file to make room);
  - the stylesheet: the look of the lobby (its tokens, the black box with the green frame, the accents of the four colours), a field that a phone does not zoom into, the places at each width;
  - this file also changes one thing at a time in a COPY of the library (a character range, a count, a word, a colour, a rule of the box): the check must then fail.
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
FRONT = os.path.join(REPO, "web", "front")
CHECK = os.path.join(SCRIPTS, "web_lobby_chat_check.js")
CHAT = os.path.join(FRONT, "lobby_chat.js")
CSS = os.path.join(FRONT, "lobby_chat.css")
RULES = os.path.join(FRONT, "lobby_rules.js")
NET = os.path.join(FRONT, "lobby_net.js")
LOBBY = os.path.join(REPO, "web", "lobby.html")
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
        text = read(CHAT)
        self.assertNotRegex(text, r"(?m)^\s*(import|export)\s", "a page loads it with a script tag: no module syntax")
        self.assertNotRegex(text, r"\brequire\(", "no dependency")
        self.assertIn("module.exports", text)                                  # (node loads it for the checks; a page gets the global)
        self.assertIn("root.AntsLobbyChat = api", text)

    def test_it_is_written_in_es5(self):
        """the page runs in any browser that the game page runs in: no arrow, no const or let, no template string, no class, no spread"""
        code = code_of(read(CHAT))
        for token, why in ((r"=>", "an arrow function"), (r"\bconst\b", "const"), (r"\blet\b", "let"), (r"`", "a template string"), (r"\bclass\b", "a class"), (r"\.\.\.", "a spread")):
            self.assertIsNone(re.search(token, code), "lobby_chat.js is ES5: it has " + why)

    def test_a_name_or_a_line_is_never_put_into_markup(self):
        """every name and line is set as text: no innerHTML, outerHTML, insertAdjacentHTML, document.write, createContextualFragment or eval in the script (the page's own code is held to the same by its other tests)"""
        code = code_of(read(CHAT))
        for token in ("innerHTML", "outerHTML", "insertAdjacentHTML", "document.write", "createContextualFragment", "DOMParser", r"\beval\(", r"new Function", "setTimeout\\(\\s*['\"]"):
            self.assertIsNone(re.search(token, code), "lobby_chat.js uses " + token)
        self.assertIn("textContent", code)

    def test_it_names_no_other_site_and_stores_nothing(self):
        text = read(CHAT)
        self.assertNotRegex(text, r"https?://")
        self.assertNotRegex(code_of(text), r"localStorage|sessionStorage|document\.cookie|indexedDB", "the lines are kept in the page, nowhere else")


class ThePage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.page = read(LOBBY)

    def test_the_page_is_within_the_line_limit_of_the_hygiene_check(self):
        self.assertLessEqual(self.page.count("\n"), 1500)

    def test_the_section_is_in_the_grid_between_the_room_and_the_map_with_its_name(self):
        grid = self.page.index('<div class="grid" id="lobby">')
        room = self.page.index('aria-labelledby="room-h"', grid)
        chat = self.page.index('<section class="panel chat" id="chat" hidden></section>', room)
        side = self.page.index('<aside class="side"', chat)
        self.assertTrue(grid < room < chat < side)
        self.assertLess(chat - room, 4000, "directly after the room's panel")

    def test_the_stylesheets_come_after_the_pages_own_style(self):
        """so that their rules win a tie by order, as the rules that moved out of the page did"""
        style_end = self.page.index("</style>")
        for name in ("map_picker.css", "lobby_chat.css"):
            tag = '<link rel="stylesheet" href="front/%s">' % name
            self.assertEqual(self.page.count(tag), 1, name)
            self.assertGreater(self.page.index(tag), style_end, name)
            self.assertLess(self.page.index(tag), self.page.index("</head>"), name)

    def test_the_script_is_loaded_after_the_client_and_before_the_map_list_and_the_pages_own_code(self):
        self.assertIn('<script src="front/lobby_rules.js"></script>\n<script src="front/lobby_net.js"></script>\n<script src="front/lobby_chat.js"></script>\n<script src="front/map_picker.js"></script>', self.page)
        self.assertLess(self.page.index("front/lobby_chat.js"), self.page.index("AntsLobbyChat"), "loaded before the page's own code uses it")

    def test_the_page_stops_with_its_line_when_the_script_did_not_come(self):
        self.assertRegex(self.page, r"if \(!Rules \|\| !Net \|\| !Chat\) \{")

    def test_the_page_mounts_the_box_with_its_colours_its_client_and_its_toast_and_gives_it_every_client(self):
        self.assertRegex(self.page, r"var chat = Chat\.mount\(\{ doc: document, host: \$\('chat'\), colours: COLOURS, say: function \(text\) \{ return !!client && client\.say\(text\); \}, toast: say \}\);")
        hook = self.page[self.page.index("function hook(c) {"):self.page.index("function makeClient(p)")]
        self.assertIn("chat.attach(c);", hook, "a client of the room, a link that the host's code gave or a probe that was taken, is the box's")
        self.assertEqual(self.page.count("chat.attach("), 1)

    def test_the_box_is_inert_with_the_page_behind_the_open_map_list(self):
        """the section is inside #lobby, which the open list makes inert: nothing of the chat takes a key while the list is open"""
        self.assertRegex(self.page, r"\[document\.querySelector\('header\.mast'\), \$\('banner'\), \$\('rejoin'\), \$\('lobby'\), \$\('footer-bar'\)\]")

    def test_no_name_or_line_reaches_the_page_as_markup(self):
        self.assertNotRegex(self.page, r"\.innerHTML\s*=")


class TheStylesheet(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.css = re.sub(r"/\*.*?\*/", "", read(CSS), flags=re.S)

    def test_it_uses_the_lobbys_own_look_and_nothing_from_another_site(self):
        for token in ("var(--cream)", "var(--muted)", "var(--edge)", "var(--frame)", "var(--frame-hi)", "var(--frame-lo)"):
            self.assertIn(token, self.css, token)
        self.assertNotRegex(self.css, r"https?://|@import|url\(\s*[\"']?(?:https?:)?//")

    def test_the_four_colours_have_the_accents_of_the_cards(self):
        page = read(LOBBY)
        cards = dict(re.findall(r'\.slot\[data-c="(\w+)"\] \{ --accent: (#[0-9a-f]{6}); \}', page))
        self.assertEqual(sorted(cards), ["black", "blue", "red"])
        default = re.search(r"\.slot \{ --accent: (#[0-9a-f]{6});", page).group(1)
        cards["green"] = default
        for colour, value in cards.items():
            self.assertRegex(self.css, r'\.chat-log li\[data-c="%s"\] \{ --accent: %s; \}' % (colour, value) if colour != "green" else r"--accent: %s;" % re.escape(default), colour)

    def test_a_phone_does_not_zoom_into_the_field(self):
        field = re.search(r"\.chat-form input \{([^}]*)\}", self.css).group(1)
        size = re.search(r"font-size:\s*(\d+)px", field)
        self.assertIsNotNone(size)
        self.assertGreaterEqual(int(size.group(1)), 16, "iOS Safari zooms into a field below 16 px")

    def test_the_button_and_the_field_are_a_finger_tall(self):
        self.assertRegex(self.css, r"\.chat-form input \{[^}]*min-height:\s*(4[4-9]|[5-9]\d)px")
        self.assertRegex(self.css, r"\.chat-form \.btn \{[^}]*min-height:\s*(4[4-9]|[5-9]\d)px")

    def test_the_places_at_each_width(self):
        self.assertRegex(self.css, r"@media \(min-width: 1041px\) \{[^@]*\.grid > \.chat \{ grid-column: 1 / -1; grid-row: 2; \}")          # (a wide window: across the page, under the room and the map)
        self.assertRegex(self.css, r"@media \(max-width: 720px\)")                                                                      # (a phone: shorter box)
        self.assertRegex(self.css, r"\.chat-log li\.sys \{[^}]*font-size:\s*14px")                                                      # (the system's lines are the smaller size, as a notice in the game's box is)

    def test_the_log_scrolls_by_itself_and_leaves_the_page_alone(self):
        log = re.search(r"\.chat-log \{([^}]*)\}", self.css).group(1)
        self.assertRegex(log, r"overflow-y:\s*auto")
        self.assertRegex(log, r"overscroll-behavior:\s*contain")
        self.assertRegex(self.css, r"\.chat-log > :first-child \{ margin-top: auto; \}")                                                 # (the newest line at the bottom, as in the game's box)

    def test_long_words_wrap(self):
        self.assertRegex(self.css, r"\.chat-log li \{[^}]*overflow-wrap:\s*anywhere")

    def test_the_box_is_dimmed_with_the_cards_while_the_link_is_down(self):
        self.assertRegex(self.css, r"body\.offline \.chat-log \{ filter: saturate\(\.4\) brightness\(\.72\); \}")


@unittest.skipUnless(NODE, "node is not installed: the lobby page's chat was NOT run (tests/scripts/web_lobby_chat_check.js)")
class TheChecksThatNodeRuns(unittest.TestCase):
    def test_the_box_keeps_to_the_tables_and_the_fake_page(self):
        status, out = run_node(CHECK, CHAT, RULES, NET)
        self.assertEqual(status, 0, out[-6000:])
        self.assertRegex(out, r"\b[1-9][0-9]* checks, 0 failed")

    def test_the_check_says_how_to_use_it_when_it_has_no_arguments(self):
        status, out = run_node(CHECK)
        self.assertEqual(status, 2)
        self.assertIn("usage: web_lobby_chat_check.js", out)


@unittest.skipUnless(NODE, "node is not installed: the lobby page's chat was NOT run (tests/scripts/web_lobby_chat_check.js)")
class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/lobby_chat_mutants.json (the list that tools/mutate.py runs too, with `--keep-going --jobs 1`) on a COPY of the files, one at a time: each must make a check
    fail (a command that exits non-zero), and the copy with nothing changed must pass, so that a failure is the change's"""

    FILES = ("web/front/lobby_chat.js", "web/front/lobby_rules.js", "web/front/lobby_net.js", "tests/scripts/web_lobby_chat_check.js", "tests/scripts/web_lobby_client_check.js",
             "tests/scripts/web_lobby_net_check.js", "tests/data/lobby_messages.txt")

    @classmethod
    def setUpClass(cls):
        import json
        with open(os.path.join(REPO, "tests", "data", "lobby_chat_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_chat_mutants.")
        for name in cls.FILES:
            os.makedirs(os.path.dirname(os.path.join(cls.folder, *name.split("/"))), exist_ok=True)
            shutil.copy(os.path.join(REPO, *name.split("/")), os.path.join(cls.folder, *name.split("/")))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.folder, ignore_errors=True)

    def run_command(self, command):
        done = subprocess.run(command, shell=True, cwd=self.folder, capture_output=True, text=True, timeout=120)
        return done.returncode, done.stdout + done.stderr

    def test_the_copy_with_nothing_changed_passes_every_command(self):
        for command in sorted({m["test"] for m in self.spec["mutants"]}):
            status, out = self.run_command(command)
            self.assertEqual(status, 0, command + "\n" + out[-3000:])

    def test_the_list_is_whole(self):
        ids = [m["id"] for m in self.spec["mutants"]]
        self.assertEqual(len(ids), len(set(ids)), "an id twice")
        self.assertGreaterEqual(len(ids), 60)
        for needed in ("chat_ascii_upper_includes_del", "chat_ascii_lower_includes_control", "chat_line_101", "net_chat_burst_of_six", "net_chat_has_no_budget", "net_encode_chat_accent", "net_valid_line_of_101",
                       "chat_a_line_is_markup", "chat_sends_while_shut",
                       "net_chat_at_the_servers_own_rate", "net_chat_burst_without_reserve", "net_chat_rate_without_margin"):
            self.assertIn(needed, ids, "the proof of the pace and its margin under the server's, the ASCII rule and the text rule needs " + needed)

    def test_every_mutant_is_seen(self):
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
