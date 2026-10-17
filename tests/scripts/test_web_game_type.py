#!/usr/bin/env python3
"""The lobby's Game type control (run by ./run_tests.sh --fast, with the other python tests of tests/scripts, and by the CI): the leader's field with its list, the guest's read-only line and the tag by the
room's name, all made from the ROWS of web/front/game_modes.js (the page needs no new button or code for a new type; the type's number must still become the protocol's last game mode), with the leader's last choice kept in the browser (docs/PLAY_IN_BROWSER.md,
"Game type").

  - the code is RUN (node, when it is installed): the rows and the pure functions (the mode a new room starts with, the tag, the guest's line, the mode that this browser does not know, storage that is
    missing or refuses) and the control on a small fake of the DOM (tests/scripts/web_game_modes_check.js), and the check is shown able to fail: broken copies of the scripts are refused by it
    (the fallbacks of the remembered choice and of the unknown mode among them);
  - the page's side is read from the source: the markup, the scripts in their order, the one stylesheet, what the page does with a new room, a choice and the room's mode;
  - the opt-in browser check (tests/scripts/test_web_game_type.sh) is there and says what it covers.
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MODES = os.path.join(REPO, "web", "front", "game_modes.js")
CONTROL = os.path.join(REPO, "web", "front", "game_type.js")
CHECK_JS = os.path.join(REPO, "tests", "scripts", "web_game_modes_check.js")
NODE = shutil.which("node")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def run_check(modes=MODES, control=CONTROL):
    return subprocess.run([NODE, CHECK_JS, modes, control], capture_output=True, text=True, timeout=120)


class ThePage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.page = read("web", "lobby.html")
        cls.modes = read("web", "front", "game_modes.js")
        cls.control = read("web", "front", "game_type.js")
        cls.css = read("web", "front", "game_type.css")

    def test_the_markup_has_the_host_of_the_control_after_the_map_and_the_tag_by_the_rooms_name(self):
        self.assertEqual(self.page.count('<div id="gtype"></div>'), 1)
        self.assertLess(self.page.index('id="browse"'), self.page.index('id="gtype"'))
        self.assertLess(self.page.index('id="gtype"'), self.page.index('id="shape-row"'), "the field sits under the map and above the picture's shape, as the approved picture shows")
        self.assertIn('<span class="hl"><h2 id="room-h">Your room</h2><span class="modetag" id="mode-tag" hidden></span></span>', self.page)

    def test_the_scripts_and_the_stylesheet_are_loaded_in_their_places(self):
        self.assertIn('<script src="front/rematch.js"></script><script src="front/game_modes.js"></script><script src="front/game_type.js"></script>\n<script src="front/map_picker.js"></script>', self.page)
        self.assertEqual(self.page.count('<link rel="stylesheet" href="front/game_type.css">'), 1)
        self.assertGreater(self.page.index('href="front/game_type.css"'), self.page.index("</style>"), "the sheet comes after the page's own style")

    def test_the_page_stops_with_its_line_when_the_new_scripts_did_not_come(self):
        self.assertIn("if (!Rules || !Net || !Chat || !Rematch || !Modes || !GameType) {", self.page)

    def test_a_choice_is_a_plan_with_the_mode_and_is_kept_in_the_browser(self):
        self.assertIn("onChoose: function (id) { Modes.remember(storage, id); if (sendPlan({ mode: id })) pendingMode = id; else render(); }", self.page)
        self.assertIn("try { storage = window.localStorage; } catch (e)", self.page)       # (the page works without the storage)

    def test_a_new_room_starts_with_the_leaders_last_choice_and_a_rematch_or_a_joined_room_does_not(self):
        self.assertIn("pendingMode = -1; if (e.created) newRoom = true;", self.page)
        self.assertIn("if (newRoom) { if (room.leader !== room.you) newRoom = false; else if (!room.starting) { var want = Modes.wanted(storage, room.mode); if (want < 0 || client.setPlan(Rules.planWith(room, model, { mode: want }))) { newRoom = false; if (want >= 0) pendingMode = want; } } }", self.page)
        self.assertEqual(self.page.count("newRoom = true"), 1, "only the room that this page made (the Welcome that says created) is new")

    def test_every_plan_the_page_sends_carries_the_pending_mode(self):
        self.assertIn("pendingMode = Rules.pendingAfter(pendingMode, room, model);", self.page)
        self.assertEqual(self.page.count("Rules.withMode(client.room, pendingMode)"), 1)
        self.assertIn("Rules.planWith(Rules.withMode(room, pendingMode), model, { sides: [0, 0, 0, 0] })", self.page)
        self.assertEqual(len(re.findall(r"Rules\.planWith\(", self.page)), 3, "every plan is built with the pending mode, except the first plan of a new room, which sets it")

    def test_the_room_decides_what_the_control_and_the_tag_show(self):
        self.assertIn("gameType.paint({ id: m.mode, guest: v.guest, disabled: v.ro });", self.page)
        self.assertIn("setText($('mode-tag'), Modes.tagOf(m.mode)); $('mode-tag').hidden = !Modes.tagOf(m.mode);", self.page)

    def test_the_page_stays_within_its_line_limit_and_the_control_is_not_in_it(self):
        self.assertLessEqual(self.page.count("\n") + 1, 1500)
        for word in ("gt-combo", "gt-opt", "aria-activedescendant"):
            self.assertNotIn(word, self.page, word)

    def test_the_rows_are_data_and_the_control_has_no_mode_in_it(self):
        self.assertIn("var MODES = [", self.modes)
        for word in ("187", "highest-score", "Highest score"):
            self.assertNotIn(word, self.control, word + " is a row of data, not code of the control")
        for word in ("187", "highest-score"):
            self.assertNotIn(word, read("web", "front", "game_type.css"), word)

    def test_the_scripts_are_es5_and_put_no_text_into_markup(self):
        for text in (self.modes, self.control):
            code = re.sub(r"'(?:[^'\\\n]|\\.)*'|\"(?:[^\"\\\n]|\\.)*\"", "''", text)
            code = re.sub(r"//.*$", "", code, flags=re.M)
            for token, why in ((r"=>", "an arrow function"), (r"\bconst\b", "const"), (r"\blet\b", "let"), (r"`", "a template string"), (r"\bclass\b", "a class"), (r"\.\.\.", "a spread")):
                self.assertIsNone(re.search(token, code), "the game type scripts are ES5: they have " + why)
        self.assertNotIn("innerHTML", self.control)

    def test_the_words_are_the_approved_ones_and_american(self):
        for words in ("Game type", "Highest score", "The original game. Food and power-ups; the highest score wins.", "A fight with no food. Kills decide.", "No food and no power-ups. The most kills wins.", "New mode"):
            self.assertIn(words, self.modes + self.control)
        for text in (self.modes, self.control, self.css):
            self.assertIsNone(re.search("colo" + "ur", text, re.I))

    def test_the_sheet_is_a_bottom_sheet_on_a_phone_with_rows_of_64_px(self):
        phone = self.css[self.css.index("@media (max-width: 600px)"):]
        for rule in ("position: fixed", "bottom: 0", "max-height: 60vh", "min-height: 64px", ".gt-title { display: block"):
            self.assertIn(rule, phone, rule)
        self.assertIn("max-height: 300px", self.css)
        self.assertIn("overflow-y: auto", self.css)

    def test_the_opt_in_browser_check_is_there(self):
        script = read("tests", "scripts", "test_web_game_type.sh")
        self.assertIn("web_game_type_browser.py", script)
        self.assertTrue(os.access(os.path.join(REPO, "tests", "scripts", "test_web_game_type.sh"), os.X_OK))
        text = read("tests", "scripts", "web_game_type_browser.py")
        for part in ("leader", "keys", "phone", "remember", "rows"):
            self.assertIn('"%s"' % part, text)


@unittest.skipUnless(NODE, "node is not installed: the game type's rows and control were NOT run (tests/scripts/web_game_modes_check.js)")
class TheCodeThatNodeRuns(unittest.TestCase):
    def test_the_rows_the_functions_and_the_control_keep_to_the_pictures(self):
        done = run_check()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r"\b[1-9][0-9]* checks, 0 failed")

    def test_the_check_says_nothing_without_its_files(self):
        done = subprocess.run([NODE, CHECK_JS], capture_output=True, text=True, timeout=60)
        self.assertNotEqual(done.returncode, 0)


@unittest.skipUnless(NODE, "node is not installed: the game type's rows and control were NOT run (tests/scripts/web_game_modes_check.js)")
class TheCheckIsNotVacuous(unittest.TestCase):
    """Each mutant is a copy of a script with one thing broken; the check must refuse it (the pattern of tests/scripts/test_web_matches.py)."""

    MODES_MUTANTS = {
        "a remembered word that no mode has is not turned into the default": ("return byWord(word) || defaultMode();", "return byWord(word);"),
        "a mode that is not known has no tag": ("return { known: false, id: id, name: name, tag: UNKNOWN_NAME, line: '', note: '' };", "return { known: false, id: id, name: name, tag: '', line: '', note: '' };"),
        "a mode that is not known has no plain name": ("var UNKNOWN_NAME = 'New mode';", "var UNKNOWN_NAME = '';"),
        "the server's name is not used for a guest": ("var name = typeof serverName === 'string' && serverName !== '' ? serverName : UNKNOWN_NAME;", "var name = UNKNOWN_NAME;"),
        "storage that refuses a read throws": ("try { return storage ? storage.getItem(STORAGE_KEY) : null; } catch (e) { return null; }", "return storage ? storage.getItem(STORAGE_KEY) : null;"),
        "storage that refuses a write throws": ("try { storage.setItem(STORAGE_KEY, m.word); return true; } catch (e) { return false; }", "storage.setItem(STORAGE_KEY, m.word); return true;"),
        "the default mode has a tag": ("tag: '', line: 'The original game.", "tag: 'HS', line: 'The original game."),
        "the room that has the mode already is told to change": ("return id === roomId ? -1 : id;", "return id;"),
    }
    CONTROL_MUTANTS = {
        "Escape chooses": ("else if (k === 'Escape') { e.preventDefault(); close(); }", "else if (k === 'Escape') { e.preventDefault(); choose(active); }"),
        "a guest can open the list": ("if (open || state.guest || state.disabled) return;", "if (open) return;"),
        "a disabled field opens": ("if (open || state.guest || state.disabled) return;", "if (open || state.guest) return;"),
        "the chosen row is not marked": ("li.setAttribute('aria-selected', String(rows[i].id === s.id))", "li.setAttribute('aria-selected', 'false')"),
        "Tab does not take the option": ("else if (k === 'Tab') choose(active);", "else if (k === 'Tab') close();"),
        "a focus moved away by a script leaves the list open": ("if (open && e && e.relatedTarget && !field.contains(e.relatedTarget)) close();", "if (false) close();"),
        "the field is named by its value too": ("combo.setAttribute('aria-labelledby', 'gt-label');", "combo.setAttribute('aria-labelledby', 'gt-label gt-name');"),
        "a paint with the same stamp repaints": ("if (stamp === last) return;", ""),
        "Enter does not choose": ("else if (k === 'Enter' || k === ' ') { e.preventDefault(); choose(active); }", "else if (k === 'Enter' || k === ' ') { e.preventDefault(); close(); }"),
        "the list does not close after a choice": ("            var m = rows[i];\n            close();", "            var m = rows[i];"),
        "a click outside does not close": ("if (open && !field.contains(e.target)) close();", "if (false) close();"),
        "the chosen row is not scrolled into view": ("if (items[i].scrollIntoView) items[i].scrollIntoView({ block: 'nearest' });", ""),
        "the guest's line is shown to the leader": ("guest.hidden = !state.guest;", "guest.hidden = false;"),
        "a mode is chosen again when it is the room's": ("if (m && m.id !== state.id) o.onChoose(m.id);", "if (m) o.onChoose(m.id);"),
    }

    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.mkdtemp(prefix="ants_game_type_mutants.")

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.folder, ignore_errors=True)

    def refused(self, which, why, old, new):
        source = read("web", "front", which)
        self.assertEqual(source.count(old), 1, "the text that this mutant changes is not in " + which + " (once): " + why)
        folder = os.path.join(self.folder, re.sub(r"\W+", "_", why))
        os.makedirs(folder)
        for name in ("game_modes.js", "game_type.js"):
            shutil.copy(os.path.join(REPO, "web", "front", name), os.path.join(folder, name))
        with open(os.path.join(folder, which), "w", encoding="utf-8") as f:
            f.write(source.replace(old, new))
        done = run_check(os.path.join(folder, "game_modes.js"), os.path.join(folder, "game_type.js"))
        self.assertNotEqual(done.returncode, 0, "the check accepted a copy where " + why)

    def test_the_rows_and_the_functions(self):
        for why, (old, new) in self.MODES_MUTANTS.items():
            with self.subTest(why):
                self.refused("game_modes.js", why, old, new)

    def test_the_control(self):
        for why, (old, new) in self.CONTROL_MUTANTS.items():
            with self.subTest(why):
                self.refused("game_type.js", why, old, new)


if __name__ == "__main__":
    unittest.main()
