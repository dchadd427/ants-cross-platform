#!/usr/bin/env python3
"""A first visit starts in the shape NEAREST the player's screen (run by ./run_tests.sh --fast and by the CI). A visit is a first one when nothing under the key `ants.aspect.v2` names a shape and the
address has no ?aspect= that does: then both pages (the game page, which a replay and a live match are too, and the front page) start in the shape nearest the device's screen by landscape ratio, with no
cutoff (the 8% cutoff stays only for the "fills your screen" tag), a phone held upright and a screen that cannot be asked in 16:9, and never store that shape: only a press of a selector writes the key, so
a visitor who never pressed one follows the screen at every visit and anyone who picked a shape (16:9 included) keeps it. The old key `ants.aspect` is not read.

  - the pages' own code is RUN (node, when it is installed) on a table of screens, addresses and what a browser had stored, among them an old `ants.aspect` = 4:3 that must not give 4:3, a stored
    16:9 on a 16:10 laptop that must stay 16:9, the middles between two shapes (both sides), phones held upright and sideways, and a table with extra rows (the shape table grows: every row is
    considered with no other change), and the selectors' clicks, which must write the key and nothing else (tests/scripts/web_aspect_key_check.js). The front page's selector: a pick keeps the match, changes
    the page at once and the hand-off address to the game carries the shape (the page has no game open, so a pick never asks and never reloads);
  - what needs no browser is read from the files: the key is the one constant of each page and no call reads or writes the old key; the one place that stores a pick is the selectors' own handler; both
    pages ask `AntsShapes.firstVisit(window)` and nothing else for the third answer; the markup of both pages starts in the 16:9 picture until that script runs (the game page's box, and the dropdowns,
    which are empty until the page fills them from the table); the front page's hand-offs to the game (START, Rejoin, a game for one) pass the player's pick on (and nothing before a pick); the nearest shape computed here, from
    the game's own table (kAspectTable) and by the rule in the docs, is the one the page's code gives.
The same cases in a real browser: tests/scripts/web_aspect_check.py (opt-in, against a running page).
"""
import json
import math
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


class PagesMarkupStartsIn16x9AndTheScriptPicks(unittest.TestCase):
    def test_the_game_page_box_selector_and_code_start_with_16_9_until_the_script_picks(self):
        page = read(SHELL)
        self.assertIn('<div id="game-stage" data-aspect="16:9">', page)
        self.assertNotIn("data-aspect=\"4:3\" aria-checked", page)                                          # (the buttons of the old selector are gone: a dropdown that the script fills)
        self.assertRegex(page, r":root \{\s*--chrome: 180px;[^}]*--ar-w: 16;\s*--ar-h: 9;\s*--stage-max: 1280px;")      # the slot's own shape; the script sets the other shapes' numbers (web/shapes.js's rows: slotVars), a rule for each shape is gone
        self.assertNotIn('#game-stage[data-aspect=', page)
        self.assertIn("var ANTS_ASPECT = '16:9';", page)
        self.assertIn("function resolveAspect(fromAddress, remembered, firstVisit) {", page)
        self.assertIn("return { aspect: AntsShapes.parse(firstVisit) || AntsShapes.FALLBACK, source: 'default' };", page)     # nothing says otherwise: the first visit's shape, never a stored one
        self.assertIn("ANTS_PAGE.resolveAspect(fromAddress, remembered, AntsShapes.firstVisit(window))", page)
        self.assertEqual(page.count("source: 'default' }"), 1)                                                   # (one place answers 'default', and it asks the table)

    def test_the_selector_is_one_empty_host_with_a_label_and_a_line_under_it(self):
        page = read(SHELL)
        bar = page[page.index('<div class="view-bar" id="view-bar">'):page.index('id="lock-bar-label"')]
        self.assertEqual(bar.strip().splitlines()[1:3], [
            '        <span id="view-bar-label">Picture</span>',
            '        <span class="pick" id="aspect-pick"><span class="fit-note" id="fit-note" hidden></span></span>',
        ])                                                                                                  # (the picker, web/shape_picker.js, puts the button and its list in the host: the label names the button by id)
        self.assertNotIn("role=\"radio\" id=\"aspect-", page)                                                 # (no button for a shape any more: the entries come from web/shapes.js)

    def test_the_page_and_the_game_agree_on_the_shapes(self):
        """The selector's order is the game's table (kAspectTable, which kAllAspects is made from), ants_set_aspect takes the shape's place in it, and each canvas is a whole number of steps of the page's box.
        The two tables themselves are compared row by row in test_web_shapes.py."""
        page = read(SHELL)
        header = read(os.path.join(REPO, "include", "ants_app", "canvas_layout.hpp"))
        game = read(os.path.join(REPO, "src", "ants_app", "application.cpp"))
        table = re.search(r"kAspectTable = \{\{(.*?)\}\};", header, re.S).group(1)
        self.assertEqual(re.findall(r"\{(Aspect::\w+),", table), ["Aspect::Classic4x3", "Aspect::Wide3x2", "Aspect::Wide16x10", "Aspect::Wide16x9", "Aspect::Wide20x9", "Aspect::Ultra21x9", "Aspect::Super32x9"])
        self.assertIn("kAllAspects = shape_table_detail::aspects_of_table();", header)
        self.assertIn("extern \"C\" EMSCRIPTEN_KEEPALIVE int ants_set_aspect(int shape) {", game)
        self.assertIn("Module._ants_set_aspect(ANTS_PAGE.shapeNumber(ANTS_ASPECT))", page)
        shapes = read(os.path.join(REPO, "web", "shapes.js"))
        steps = {"4:3": (4, 3), "3:2": (3, 2), "16:10": (8, 5), "16:9": (16, 9), "20:9": (20, 9), "21:9": (7, 3), "32:9": (32, 9)}
        canvas = {"4:3": (640, 480), "3:2": (960, 640), "16:10": (960, 600), "16:9": (960, 540), "20:9": (1200, 540), "21:9": (1260, 540), "32:9": (1920, 540)}
        for name, (w, h) in steps.items():
            self.assertRegex(shapes, r"name: '%s', label: '[^']+', width: %d, height: %d," % (name, canvas[name][0], canvas[name][1]))
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
        self.assertIn("onPick: antsPickShape }", block)                                                   # (a choice in either dropdown is the pick: the settings menu calls the same function)

    def test_the_front_page_dropdown_is_empty_markup_until_the_script_fills_it_from_the_table(self):
        page = read(LOBBY)
        self.assertNotIn(".frame {", page)                                                                 # (the lobby has no games in frames: the test room that had them is gone)
        self.assertIn("var aspect = '16:9';", page)                                                         # (what the page shows until the block below runs)
        self.assertIn("else aspect = AntsShapes.firstVisit(window);", page)
        self.assertEqual(len(re.findall(r'name="aspect"', page)), 0)                                         # (no radio button for a shape any more)
        self.assertEqual(len(re.findall(r'<select class="shape-select" id="aspect-select"', page)), 1)       # (the picture's one dropdown, nothing else)
        self.assertIn("AntsShapes.fillSelect($('aspect-select'), document).value = aspect;", page)           # (filled from the table, the first visit's shape chosen)

    def test_the_front_page_and_the_game_page_make_their_dropdowns_with_the_same_function_and_say_the_same_line(self):
        page, shell = read(LOBBY), read(SHELL)
        self.assertNotIn("var SHAPES = {", page + shell)                                                  # (no table of its own: both pages read web/shapes.js, which test_web_shapes.py pins to the game's)
        self.assertIn('<script src="shapes.js"></script>', page)
        self.assertIn('<script src="shapes.js"></script>', shell)
        self.assertEqual(page.count("AntsShapes.fillsLine(window.screen.width, window.screen.height)"), 1)
        self.assertEqual(shell.count("AntsShapes.fillsLine(window.screen.width, window.screen.height)"), 1)
        self.assertNotIn("fit-tag", page + shell)                                                         # (the tag under a button is the quiet line under the dropdown now)

    def test_the_front_page_dropdown_is_the_picture_row_of_the_match_column_under_the_map_and_above_start(self):
        """The picture draws the match column as the map picker, the map's line, the "Picture" row (the label above the dropdown, the quiet line under it) and START!; the footer has no selector."""
        page = read(LOBBY)
        side = page[page.index('<aside class="side"'):page.index('</aside>')]
        footer = page[page.index('<footer class="bar" id="footer-bar">'):page.index("</footer>")]
        self.assertLess(side.index('id="map"'), side.index('<div class="shape" id="shape-row">'))
        self.assertLess(side.index('id="info"'), side.index('<div class="shape" id="shape-row">'))
        self.assertLess(side.index('<div class="shape" id="shape-row">'), side.index('id="start"'))
        self.assertIn('<label class="shape-label" id="shape-label" for="aspect-select">Picture</label><span class="shape-box"><select class="shape-select" id="aspect-select" aria-describedby="fit-note"', side)
        self.assertLess(side.index('id="aspect-select"'), side.index('id="fit-note"'))                    # (the line is under the dropdown)
        for gone in ('aspect-select', 'class="shape"', 'fit-note', 'class="grow"', "Screen "):
            self.assertNotIn(gone, footer, "the footer has no selector: " + gone)


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


class TheFirstVisitStartsNearTheScreen(unittest.TestCase):
    """What is read from the files: how the third answer is asked, and that nothing but a selector stores. What the pages DO is run in web_aspect_key_check.js."""

    def test_both_pages_ask_the_one_function_once(self):
        self.assertEqual(read(SHELL).count("AntsShapes.firstVisit("), 1)
        self.assertEqual(read(LOBBY).count("AntsShapes.firstVisit("), 1)
        self.assertNotIn("firstVisit", read(os.path.join(REPO, "web", "game_menu.js")))                  # (the settings menu only follows the page's shape)

    def test_only_a_press_of_a_selector_stores_the_choice(self):
        """A shape that is only the first visit's must never become a stored choice by itself: the key is written in one place of each page, and that place is the selector's handler."""
        shell, lobby = read(SHELL), read(LOBBY)
        block = shell[shell.index("ANTS_SELECTOR_BEGIN"):shell.index("ANTS_SELECTOR_END")]
        self.assertEqual(len(re.findall(r"setItem\(\s*ANTS_ASPECT_KEY\b", shell)), 1)
        self.assertEqual(len(re.findall(r"setItem\(\s*ANTS_ASPECT_KEY\b", block)), 1)
        self.assertEqual(shell.count("function antsPickShape(value) {"), 1)                                  # (the one function that a choice calls: the dropdowns' change and the settings menu)
        self.assertEqual(len(re.findall(r"antsPickShape\(", block)), 1)                                       # (its definition: the dropdowns are handed the function itself, nothing else writes the key)
        self.assertEqual(len(re.findall(r"onPick: antsPickShape\b", block)), 1)                              # (... as the one `onPick` of the picker that both dropdowns are made with)
        selector = lobby[lobby.index("// SELECTOR_BEGIN"):lobby.index("// SELECTOR_END")]
        self.assertEqual(len(re.findall(r"remember\(\s*ASPECT_KEY\b", lobby)), len(re.findall(r"remember\(\s*ASPECT_KEY\b", selector)))
        self.assertGreaterEqual(len(re.findall(r"remember\(\s*ASPECT_KEY\b", selector)), 1)
        shape_block = lobby[lobby.index("// SHAPE_BEGIN"):lobby.index("// SHAPE_END")]
        self.assertNotIn("remember(", shape_block)                                                       # (the block that picks the first visit's shape only reads)
        self.assertNotIn("setItem", shape_block)

    def test_the_shape_code_has_no_second_list_of_default_screens(self):
        shapes = read(os.path.join(REPO, "web", "shapes.js"))
        code = shapes[:shapes.index("// SHAPE_TABLE_BEGIN")] + shapes[shapes.index("// SHAPE_TABLE_END"):]
        code = "\n".join(line for line in code.splitlines() if not line.strip().startswith("//"))
        self.assertEqual(len(re.findall(r"'16:9'", code)), 1)                                             # (outside the table and the comments only FALLBACK names a shape: the rest comes from the rows)
        self.assertIn("var FALLBACK = '16:9';", shapes)

    @unittest.skipUnless(shutil.which("node"), "node is not installed: the nearest shape was NOT compared with the game's table")
    def test_the_pages_nearest_shape_is_the_rule_worked_out_here_from_the_games_table(self):
        """The rule in docs/PLAY_IN_BROWSER.md, written again here over the rows of kAspectTable (the game's table): the smallest ratio of ratios wins, the first row on a tie, upright is
        16:9, a size that is none is 16:9. A row added to the C++ table is considered by this test and by the page's code with no other change."""
        header = read(os.path.join(REPO, "include", "ants_app", "canvas_layout.hpp"))
        table = re.search(r"kAspectTable = \{\{(.*?)\}\};", header, re.S).group(1)
        rows = [(name, int(w), int(h)) for name, w, h in re.findall(r'\{Aspect::\w+, "([^"]+)", "[^"]*", (\d+), (\d+)\}', table)]
        self.assertGreaterEqual(len(rows), 7)
        shapes = [(name, (w // math.gcd(w, h)) / (h // math.gcd(w, h))) for name, w, h in rows]

        def nearest(w, h, inner):
            if not (w > 0 and h > 0 and math.isfinite(w) and math.isfinite(h)):
                return "16:9"
            if not (w >= h or (inner is not None and inner[0] > inner[1])):
                return "16:9"
            ratio = max(w, h) / min(w, h)
            best, best_off = None, math.inf
            for name, shape in shapes:
                off = max(shape / ratio, ratio / shape) - 1
                if off < best_off:
                    best, best_off = name, off
            return best

        screens = [(1000 + 3 * i, 1000, None) for i in range(0, 1100)]                                   # ratios 1.0 .. 4.3
        screens += [(1920, 1080, None), (1366, 768, None), (1440, 900, None), (3440, 1440, None), (5120, 1440, None), (1280, 1024, None), (844, 390, None), (915, 412, None), (2736, 1824, None), (3840, 1080, None)]
        screens += [(390, 844, (390, 664)), (768, 1024, (768, 900)), (390, 844, (844, 340)), (1080, 1920, (1080, 1700)), (0, 0, None), (1920, -5, None), (float("nan"), 1080, None)]
        script = ("const S = require(process.argv[1]); const input = JSON.parse(process.argv[2]);"
                  "console.log(JSON.stringify(input.map(([w, h, inner]) => S.firstVisit({ screen: { width: w === null ? NaN : w, height: h }, innerWidth: inner ? inner[0] : undefined, innerHeight: inner ? inner[1] : undefined }))));")
        payload = json.dumps([[None if (isinstance(w, float) and math.isnan(w)) else w, h, inner] for w, h, inner in screens])
        done = subprocess.run([shutil.which("node"), "-e", script, os.path.join(REPO, "web", "shapes.js"), payload], capture_output=True, text=True, timeout=60)
        self.assertEqual(done.returncode, 0, done.stderr)
        got = json.loads(done.stdout)
        want = [nearest(w, h, inner) for w, h, inner in screens]
        self.assertEqual([(s, g) for s, g, x in zip(screens, got, want) if g != x], [])
        self.assertEqual(len(set(want) - {"16:9"}) + 1, len(rows))                                         # (the sweep reaches every shape)


class TheFrontPageHandsTheShapeOnToTheGame(unittest.TestCase):
    """The front page passes the player's PICK on to every game it opens, as the variable `pickedShape` AT THE MOMENT of the hand-off (not a copy made when the page loaded), so that a shape picked in the lobby,
    which keeps the match and does not reload the page, is the shape of the game that START opens. A first visit's shape is not a pick and is not handed on: the game page works out its own when it
    loads (a phone that opens the front page upright and is turned sideways would otherwise carry its upright 16:9 into the game). The builders of the addresses are run with node
    (web_aspect_key_check.js: the START address, a game for one); that the call sites pass the variable is read here."""

    def test_the_start_the_rejoin_and_the_game_for_one_pass_the_live_shape(self):
        page = read(LOBBY)
        self.assertIn("window.location.assign(new URL('./' + rejoinQuery({ room: code, seat: seat }, name, pickedShape), window.location.href).href);", page)                              # START (onStarting)
        self.assertIn("window.location.assign(new URL('./' + rejoinQuery(rejoinOffered, remembered.ok ? remembered.name : '', pickedShape), window.location.href).href);", page)         # the Rejoin button
        self.assertIn("window.location.assign(new URL(address, window.location.href).href);", page)                               # START with nobody else (startAlone)
        self.assertIn("var address = soloAddress(model, myName(), pickedShape);", page)                                                  # (the game for one's address carries the live pick)
        block = page[page.index("// SELECTOR_BEGIN"):page.index("// SELECTOR_END")]
        self.assertIn("aspect = value;", block)                                                                                            # (a pick in the lobby changes the live variable, which the calls above read)
        self.assertIn("pickedShape = value;", block)                                                                                            # (and it is a pick, the shown shape too)
        shape_block = page[page.index("// SHAPE_BEGIN"):page.index("// SHAPE_END")]
        self.assertIn("aspect = pickedShape = asked;", shape_block)                                                                              # (the address and the stored pick are picks)
        self.assertIn("aspect = pickedShape = remembered;", shape_block)
        self.assertNotRegex(shape_block, r"pickedShape = AntsShapes\.firstVisit")                                                               # (the first visit's shape is not)

    def test_every_game_address_of_the_page_is_one_of_those_three_builders(self):
        page = read(LOBBY)
        self.assertNotIn("aspectFromAddress", page)                                                                                          # (the links for other players that carried the shape only when the address named it were the test room's)
        self.assertEqual(len(re.findall(r"'&aspect=' \+", page)), 1)                                                                          # (aspectQuery is the one place that writes it; rejoinQuery and localGameQuery call it, START's hand-off is rejoinQuery)

    def test_the_two_builders_name_a_picked_shape_and_nothing_else(self):
        page = read(LOBBY)
        self.assertIn("function aspectQuery(shape) { return isShape(shape) ? '&aspect=' + shape : ''; }", page)                           # (a text that names no shape leaves it out: the game page decides)
        self.assertEqual(page.count("+ aspectQuery(shape);"), 2)                                                                            # (rejoinQuery and localGameQuery)
        self.assertNotIn("shapeOr169", page)
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
        self.assertEqual(web_aspect_check.quick_help_start((640, 480)), self.pinned("test_app_integration_basics.cpp"))   # the original's own page: (529, 437, 98, 27)

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
