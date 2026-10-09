#!/usr/bin/env python3
"""16:9 is the default picture of the web pages (run by ./run_tests.sh --fast and by the CI). The owner: 16:9 is to be the default everywhere. The pages already show 16:9 unless the
address says ?aspect=4:3 or the selector remembered a Classic 4:3, and a browser that once tried Classic 4:3 stayed on it for good (the choice was remembered under `ants.aspect`). The
pages now remember the choice under `ants.aspect.v2` and do not read the old key, so a Classic 4:3 that an earlier page remembered is forgotten once: every browser starts 16:9 until its
player picks Classic 4:3 again.

  - the pages' own code is RUN (node, when it is installed) on a table of addresses and of what a browser had stored, among them an old `ants.aspect` = 4:3 that must not give 4:3, and
    the selectors' clicks, which must write the new key and nothing else (tests/scripts/web_aspect_key_check.js). The front page's selector is run in its two modes: in the lobby (the room
    view) a pick keeps the match, changes the page at once and the hand-off address to the game carries the shape; in the legacy test room (the old addresses ?map=, ?play=here ...) a pick
    still asks and reloads the page with ?aspect=;
  - what needs no browser is read from the files: the new key is the one constant of each page and no call reads or writes the old key; the markup and the style of both pages start with
    the 16:9 picture (the game page's box, its selector and the front page's "Screen" buttons in its footer, and the frames of the legacy test room); the front page's hand-offs to the game
    (START, Rejoin, a game for one, the test room's links) pass the page's live shape on.
The same cases in a real browser: tests/scripts/web_aspect_check.py (opt-in, against a running page).
"""
import os
import re
import shutil
import subprocess
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SHELL = os.path.join(REPO, "web", "shell.html")
LOBBY = os.path.join(REPO, "web", "lobby.html")
CHECK_JS = os.path.join(REPO, "tests", "scripts", "web_aspect_key_check.js")
NEW_KEY = "ants.aspect.v2"


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


class PagesStartWith16x9(unittest.TestCase):
    def test_the_game_page_box_selector_and_code_start_with_16_9(self):
        page = read(SHELL)
        self.assertIn('<div id="game-stage" data-aspect="16:9">', page)
        self.assertRegex(page, r'id="aspect-16-9" data-aspect="16:9" aria-checked="true"')
        self.assertRegex(page, r'id="aspect-4-3" data-aspect="4:3" aria-checked="false"')
        self.assertRegex(page, r"#game-stage \{\s*--ar-w: 16;\s*--ar-h: 9;")              # the slot's own shape; only data-aspect="4:3" changes it
        self.assertRegex(page, r'#game-stage\[data-aspect="4:3"\] \{\s*--ar-w: 4;\s*--ar-h: 3;')
        self.assertRegex(page, r'#game-stage\[data-aspect="16:10"\] \{\s*--ar-w: 8;\s*--ar-h: 5;')       # the slot of the two other shapes (their smallest whole numbers)
        self.assertRegex(page, r'#game-stage\[data-aspect="21:9"\] \{\s*--ar-w: 7;\s*--ar-h: 3;')
        self.assertIn("var ANTS_ASPECT = '16:9';", page)
        self.assertIn("return { aspect: '16:9', source: 'default' };", page)                 # nothing says otherwise: 16:9 on every device, a phone held upright included

    def test_the_selector_has_the_four_shapes_in_this_order_with_these_hover_texts(self):
        page = read(SHELL)
        bar = page[page.index('<div class="view-bar" id="view-bar">'):page.index('id="lock-bar-label"')]
        buttons = re.findall(r'<button type="button" role="radio" id="aspect-([0-9-]+)" data-aspect="([0-9:]+)" aria-checked="(true|false)" title="([^"]+)">([^<]+)</button>', bar)
        self.assertEqual([(b[0], b[1], b[2], b[3], b[4]) for b in buttons], [
            ("4-3", "4:3", "false", "Classic 4:3: the original's picture (640 x 480)", "Classic 4:3"),
            ("16-10", "16:10", "false", "16:10: laptops and 16:10 monitors (960 x 600)", "16:10"),
            ("16-9", "16:9", "true", "16:9: the wide picture (960 x 540)", "16:9"),
            ("21-9", "21:9", "false", "21:9: ultrawide monitors (1260 x 540)", "21:9"),
        ])
        self.assertEqual(bar.count('<span class="fit-tag" hidden>fills your screen</span>'), 4)             # one tag under each button, hidden until the script says which shape is the screen's

    def test_the_page_and_the_game_agree_on_the_shapes(self):
        """The selector's order is the game's kAllAspects, ants_set_aspect takes the shape's place in it, and each canvas is a whole number of steps of the page's box."""
        page = read(SHELL)
        header = read(os.path.join(REPO, "include", "ants_app", "canvas_layout.hpp"))
        game = read(os.path.join(REPO, "src", "ants_app", "application.cpp"))
        order = re.search(r"kAllAspects = \{([^}]*)\}", header).group(1)
        self.assertEqual([x.strip() for x in order.split(",")], ["Aspect::Classic4x3", "Aspect::Wide16x10", "Aspect::Wide16x9", "Aspect::Ultra21x9"])
        self.assertIn("var SHAPE_ORDER = ['4:3', '16:10', '16:9', '21:9'];", page)
        self.assertIn("extern \"C\" EMSCRIPTEN_KEEPALIVE int ants_set_aspect(int shape) {", game)
        self.assertIn("Module._ants_set_aspect(ANTS_PAGE.shapeNumber(ANTS_ASPECT))", page)
        steps = {"4:3": (4, 3), "16:10": (8, 5), "16:9": (16, 9), "21:9": (7, 3)}
        canvas = {"4:3": (640, 480), "16:10": (960, 600), "16:9": (960, 540), "21:9": (1260, 540)}
        for name, (w, h) in steps.items():
            self.assertIn("'" + name + "': { w: %d, h: %d }" % (w, h), page)
            self.assertEqual((canvas[name][0] % w, canvas[name][1] % h), (0, 0), name)
            self.assertEqual(canvas[name][0] // w, canvas[name][1] // h, name)                           # the same number of steps on both axes: the canvas has the box's shape
        self.assertIn("kWideCanvasWidth = 960", header)
        self.assertIn("kWideCanvasHeight = 540", header)

    def test_the_selector_changes_the_picture_in_place(self):
        page = read(SHELL)
        block = page[page.index("ANTS_SELECTOR_BEGIN"):page.index("ANTS_SELECTOR_END")]
        self.assertNotIn("window.location.assign", block)                                                # no reload
        self.assertNotIn("window.confirm", block)                                                        # no "Leave the match to change the picture?"
        self.assertIn("antsSetAspect(value);", block)

    def test_the_front_page_frames_and_buttons_start_with_16_9(self):
        page = read(LOBBY)
        self.assertIn(".frame { position: relative; aspect-ratio: 16 / 9; }", page)                       # (the frames of the legacy test room; the lobby has none)
        self.assertIn('body[data-aspect="4:3"] .frame { aspect-ratio: 4 / 3; }', page)
        self.assertIn('body[data-aspect="16:10"] .frame { aspect-ratio: 16 / 10; }', page)
        self.assertIn('body[data-aspect="21:9"] .frame { aspect-ratio: 21 / 9; }', page)
        self.assertIn("var aspect = '16:9';", page)
        self.assertEqual(len(re.findall(r'<input type="radio" name="aspect"', page)), 4)                   # (the picture's four buttons, nothing else)
        self.assertIn('<input type="radio" name="aspect" id="aspect-16-9" value="16:9" checked><label for="aspect-16-9" title="16:9: the wide picture (960 x 540)">16:9</label>', page)        # 16:9 is the one that is checked
        self.assertEqual(len(re.findall(r'value="[0-9:]+" checked>', page)), 1)

    def test_the_front_page_selector_has_the_four_shapes_in_the_game_pages_order_with_its_hover_texts(self):
        page, shell = read(LOBBY), read(SHELL)
        bar = shell[shell.index('<div class="view-bar" id="view-bar">'):shell.index('id="lock-bar-label"')]
        game = [(shape, title, label) for shape, title, label in re.findall(r'data-aspect="([0-9:]+)" aria-checked="(?:true|false)" title="([^"]+)">([^<]+)</button>', bar)]
        row = page[page.index('<span class="shape">Screen'):page.index('</footer>')]
        front = re.findall(r'<input type="radio" name="aspect" id="aspect-[0-9-]+" value="([0-9:]+)"(?: checked)?><label for="aspect-[0-9-]+" title="([^"]+)">([^<]+)</label>', row)
        self.assertEqual([shape for shape, _, _ in front], ["4:3", "16:10", "16:9", "21:9"])
        self.assertEqual(front, game)                                                                     # the same names and the same hover texts as the game page's own buttons
        shapes = lambda text: re.search(r"var SHAPES = (\{.*?\});\n", text, re.S).group(1)
        self.assertEqual(shapes(page), shapes(shell))                                                     # (the table of the shapes is the game page's)

    def test_the_front_page_buttons_are_the_screen_buttons_at_the_right_of_the_footer(self):
        """The picture draws the footer as the version line, the footer links and, at the right, "Screen" with the buttons (the space that grows pushes them to the right; where the row is full they wrap to a line of their own)."""
        page = read(LOBBY)
        footer = page[page.index('<footer class="bar" id="footer-bar">'):page.index("</footer>")]
        row = footer[footer.index('<div class="bar-row">'):]
        self.assertLess(row.index('id="game-version-line"'), row.index('<nav aria-label="Footer links">'))
        self.assertLess(row.index('</nav>'), row.index('<span class="grow"></span>'))
        self.assertLess(row.index('<span class="grow"></span>'), row.index('<span class="shape">Screen <span class="seg" role="radiogroup" aria-label="Picture shape">'))
        order = [row.index('id="aspect-%s"' % shape) for shape in ("4-3", "16-10", "16-9", "21-9")]
        self.assertEqual(order, sorted(order))                                                             # Classic 4:3, 16:10, 16:9, 21:9, as drawn
        self.assertEqual(row.count('name="aspect"'), 4)                                                    # (all four buttons are in that one place)


class TheChoiceLivesUnderTheNewKey(unittest.TestCase):
    def test_each_page_names_the_new_key_once_and_no_call_uses_the_old_one(self):
        for path, constant in ((SHELL, "ANTS_ASPECT_KEY"), (LOBBY, "ASPECT_KEY")):
            page = read(path)
            name = os.path.basename(path)
            self.assertEqual(len(re.findall(r"var " + constant + r" = '" + re.escape(NEW_KEY) + r"';", page)), 1, name)
            for access in ("getItem", "setItem", "removeItem", "recall", "remember"):
                self.assertNotRegex(page, access + r"\(\s*['\"]ants\.aspect['\"]", name + ": " + access + " uses the old key")
            self.assertEqual(len(re.findall(r"\b" + constant + r"\b", page)) >= 3, True, name)      # (the constant, its read and its write)

    def test_the_two_pages_use_the_same_key_and_the_browser_check_does_too(self):
        self.assertIn("var ANTS_ASPECT_KEY = '" + NEW_KEY + "';", read(SHELL))
        self.assertIn("var ASPECT_KEY = '" + NEW_KEY + "';", read(LOBBY))
        browser = read(os.path.join(REPO, "tests", "scripts", "web_aspect_check.py"))
        self.assertIn("localStorage.setItem('" + NEW_KEY + "', '4:3')", browser)
        self.assertIn("localStorage.setItem('ants.aspect', '4:3')", browser)                  # (the old key is set on purpose: it must not give 4:3)


class TheFrontPageHandsTheShapeOnToTheGame(unittest.TestCase):
    """The front page passes the shape that is shown on to every game it opens, as the variable `aspect` AT THE MOMENT of the hand-off (not a copy made when the page loaded), so that a shape picked
    in the lobby, which keeps the match and does not reload the page, is the shape of the game that START opens. The builders of the addresses are run with node (web_aspect_key_check.js: the
    START address, a game for one); that the call sites pass the variable is read here."""

    def test_the_start_the_rejoin_and_the_game_for_one_pass_the_live_shape(self):
        page = read(LOBBY)
        self.assertIn("window.location.assign(new URL('./' + rejoinQuery({ room: code, seat: seat }, name, aspect), window.location.href).href);", page)                              # START (onStarting)
        self.assertIn("window.location.assign(new URL('./' + rejoinQuery(rejoinOffered, remembered.ok ? remembered.name : '', aspect), window.location.href).href);", page)         # the Rejoin button
        self.assertIn("window.location.assign(new URL('./' + LOCAL_PAGE + localGameQuery(key, [], name, aspect), window.location.href).href);", page)                               # START with nobody else (startAlone)
        self.assertIn("window.location.assign(new URL('./' + LOCAL_PAGE + localGameQuery(mapKey, levels, youName(), aspect), window.location.href).href);", page)                  # the old ?players=1 address
        block = page[page.index("// SELECTOR_BEGIN"):page.index("// SELECTOR_END")]
        self.assertIn("aspect = value;", block)                                                                                            # (a pick in the lobby changes the live variable, which the calls above read)

    def test_the_games_of_the_test_room_carry_the_shape_too(self):
        page = read(LOBBY)
        self.assertIn("if (own || aspectFromAddress) q += '&aspect=' + aspect;", page)                                                       # the room's own games always, the links for other players when the address named it
        self.assertIn("(aspectFromAddress ? '&aspect=' + aspect : '')", page)                                                                # the address of the test room (a reload comes back to it) and the join link of "Play in this tab"

    def test_the_two_builders_name_one_of_the_four_shapes_only(self):
        page = read(LOBBY)
        self.assertEqual(page.count("'&aspect=' + shapeOr169(shape)"), 2)                                                                   # (rejoinQuery and localGameQuery)
        self.assertNotIn("(shape === '4:3' ? '4:3' : '16:9')", page)                                                                          # (no builder is left that knows two shapes)


class TheQuickHelpStartThatTheBrowserCheckTaps(unittest.TestCase):
    """tests/scripts/web_aspect_check.py clicks the quick help's START! button where a person does. It used to tap the place that the original's 640 x 480 page has in the middle of the 16:9 canvas,
    which the wide pages of v0.2.0 left empty (the page was right: START! is in the bottom right corner of the wide page); it now reads the button's rectangle from the layout's own source and
    moves it as the wide page does. Here it is compared with what the game's own tests pin."""

    def pinned(self, test_file):
        with open(os.path.join(REPO, "tests", "test_app", test_file), encoding="utf-8") as f:
            found = re.search(r"quick_help_start_button\(\)\.up_rect\(\) == ButtonRect\(\{(\d+), (\d+), (\d+), (\d+)\}\)", f.read())
        self.assertIsNotNone(found, test_file + " no longer pins the quick help's START! rectangle")
        x, y, w, h = (int(v) for v in found.groups())
        return x + w / 2, y + h / 2

    def test_it_is_the_middle_of_the_button_that_the_game_pins_for_each_picture(self):
        import sys
        sys.path.insert(0, os.path.join(REPO, "tests", "scripts"))
        import web_aspect_check
        self.assertEqual(web_aspect_check.quick_help_start((960, 540)), self.pinned("test_wide_pages.cpp"))        # the wide page: (849, 497, 98, 27)
        self.assertEqual(web_aspect_check.quick_help_start((640, 480)), self.pinned("test_app_integration.cpp"))   # the original's own page: (529, 437, 98, 27)

    def test_the_browser_checks_do_not_tap_the_places_of_the_original_s_page_on_the_wide_canvas(self):
        for name in ("web_aspect_check.py", "web_edge_check.py"):
            with open(os.path.join(REPO, "tests", "scripts", name), encoding="utf-8") as f:
                text = f.read()
            self.assertNotIn("160 + 576", text, name)
            self.assertNotIn("(shape[0] - 640) / 2 + 576", text, name)


@unittest.skipUnless(shutil.which("node"), "node is not installed: the pages' own code for the picture's shape was NOT run (tests/scripts/web_aspect_key_check.js)")
class ThePagesOwnCode(unittest.TestCase):
    def test_a_table_of_addresses_and_stored_values_and_the_selectors(self):
        done = subprocess.run([shutil.which("node"), CHECK_JS, SHELL, LOBBY], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("0 failures", done.stdout)


if __name__ == "__main__":
    unittest.main()
