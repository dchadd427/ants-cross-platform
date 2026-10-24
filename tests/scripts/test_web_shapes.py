#!/usr/bin/env python3
"""The shapes of the picture live in ONE table per side, and the two tables are the same (run by ./run_tests.sh --fast and by the CI).

The game's table is `kAspectTable` in include/ants_app/canvas_layout.hpp (name, words, canvas); the pages' is `TABLE` in web/shapes.js (the same four fields, and the widest slot, which only a
page has). Everything else reads one of them: the canvas, `--aspect`, the settings key, the refusals' words and the Esc menu's picture row in the game; the game page, the front page, the settings
menu (and, through the page, the CSS slot and the box) on the web. Here:
  - the two tables agree row by row (name, words, canvas width and height), in the same order, which is also the place that `ants_set_aspect` takes;
  - every canvas is one that the pages can stand in (640 x 480, or 960 x 540 and more in both directions), and each row is wider than the one before;
  - no page, script or source file keeps a list of the shapes of its own: a quoted shape name or a shape's steps appears in web/shapes.js (and, for the game, canvas_layout.hpp) and nowhere else
    in the code; the three dropdowns (the game page's, the replay's Picture window, the front page's Picture row) are empty selects in the markup and are filled from the table by one function
    (AntsShapes.fillSelect), as is the settings menu's; each entry says the shape and its canvas ("16:9 · 960 x 540") and nothing else;
  - the pages load web/shapes.js before their own code, and the image, the web build and the CI's file check carry it.
The behavior of the table's functions (which shape fills a screen, the box of each shape, the place of a shape) is run by tests/scripts/web_aspect_key_check.js (test_web_aspect_default.py).
Mutants of both tables: tests/data/shape_table_mutants.json (tools/mutate.py; the page-side ones are run here on copies of the files).
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
ROOT = os.environ.get("ANTS_SHAPES_ROOT") or REPO          # (the mutants run this file on a copy of the files that it reads: tests/data/shape_table_mutants.json)
SHAPES_JS = os.path.join(ROOT, "web", "shapes.js")
SHELL = os.path.join(ROOT, "web", "shell.html")
LOBBY = os.path.join(ROOT, "web", "lobby.html")
MENU_JS = os.path.join(ROOT, "web", "game_menu.js")


def read(*parts):
    with open(os.path.join(ROOT, *parts), encoding="utf-8") as f:
        return f.read()


def game_rows():
    """[(Aspect name, shape name, words, width, height)] of kAspectTable, in its order."""
    text = read("include", "ants_app", "canvas_layout.hpp")
    block = re.search(r"kAspectTable = \{\{(.*?)\}\};", text, re.S).group(1)
    return [(a, n, l, int(w), int(h)) for a, n, l, w, h in re.findall(r'\{Aspect::(\w+), "([^"]+)", "([^"]+)", (\d+), (\d+)\},', block)]


def page_rows():
    """[(shape name, words, width, height, slot)] of the TABLE of web/shapes.js, in its order."""
    text = read("web", "shapes.js")
    block = text[text.index("SHAPE_TABLE_BEGIN"):text.index("SHAPE_TABLE_END")]
    return [(n, l, int(w), int(h), int(s)) for n, l, w, h, s in re.findall(r"\{ name: '([^']+)', label: '([^']+)', width: (\d+), height: (\d+), slot: (\d+) \}", block)]


class TheTwoTablesAgree(unittest.TestCase):
    def test_both_tables_have_the_rows_that_the_pages_have_always_offered(self):
        self.assertEqual([r[1] for r in game_rows()], ["4:3", "3:2", "16:10", "16:9", "20:9", "21:9", "32:9"])
        self.assertEqual([(r[1], r[3], r[4]) for r in game_rows()],
                         [("4:3", 640, 480), ("3:2", 960, 640), ("16:10", 960, 600), ("16:9", 960, 540), ("20:9", 1200, 540), ("21:9", 1260, 540), ("32:9", 1920, 540)])

    def test_the_tables_agree_row_by_row(self):
        game, page = game_rows(), page_rows()
        self.assertEqual(len(game), len(page), "a shape in one table and not in the other")
        for place, (g, p) in enumerate(zip(game, page)):
            self.assertEqual((g[1], g[2], g[3], g[4]), (p[0], p[1], p[2], p[3]), "row %d (the place that ants_set_aspect takes): the game's %s and the page's %s differ" % (place, g[1], p[0]))

    def test_every_canvas_is_one_that_the_pages_can_stand_in_and_the_rows_are_narrowest_to_widest(self):
        rows = game_rows()
        for _, name, _, w, h in rows:
            self.assertTrue((w, h) == (640, 480) or (w >= 960 and h >= 540), name + ": the pages exist for 640 x 480 and for 960 x 540 and more (CanvasLayout::page)")
        for before, row in zip(rows, rows[1:]):
            self.assertGreater(row[3] * before[4], before[3] * row[4], "%s is not wider than %s" % (row[1], before[1]))

    def test_each_row_has_its_own_name_words_and_enum_value(self):
        rows = game_rows()
        for column in (0, 1, 2):
            values = [r[column] for r in rows]
            self.assertEqual(len(values), len(set(values)), "a value of column %d twice" % column)
        enum = re.search(r"enum class Aspect : uint8_t \{(.*?)\};", read("include", "ants_app", "canvas_layout.hpp"), re.S).group(1)
        self.assertEqual(sorted(re.findall(r"^\s+(\w+)", enum, re.M)), sorted(r[0] for r in rows), "every value of Aspect has a row")

    def test_the_slot_of_a_page_is_960_css_pixels_for_the_classic_picture_1280_and_for_a_wider_canvas_its_own_width(self):
        """The rule (docs/PLAY_IN_BROWSER.md, "A 16:9 picture that fills the page"): the widest slot is the page's column of 1280 CSS pixels, or the canvas's own width when that is more (a monitor then shows 32:9 at 1x), and 960 for the classic picture."""
        rows = page_rows()
        for name, _, w, h, slot in rows:
            self.assertEqual(slot, 960 if (w, h) == (640, 480) else max(1280, w), name + ": its widest slot")
        self.assertEqual([r[4] for r in rows], [960, 1280, 1280, 1280, 1280, 1280, 1920])


class NoCopyOfTheListIsLeft(unittest.TestCase):
    NAMES = r"""'(?:4:3|3:2|16:10|20:9|21:9|32:9)'"""

    def test_no_script_or_page_quotes_a_shape_name_except_the_table(self):
        found = []
        for base, dirs, names in os.walk(os.path.join(ROOT, "web")):
            for name in names:
                if not name.endswith((".js", ".html")):
                    continue
                path = os.path.join(base, name)
                if os.path.samefile(path, SHAPES_JS):
                    continue
                with open(path, encoding="utf-8") as f:
                    for number, line in enumerate(f, 1):
                        if re.search(self.NAMES, line):
                            found.append("%s:%d" % (os.path.relpath(path, ROOT), number))
        self.assertEqual(found, [], "a list of shapes outside web/shapes.js")

    def test_the_slot_and_the_replay_bar_take_their_widest_width_from_the_shapes_row(self):
        """The cap of the game area (1280 CSS pixels, 1920 for 32:9) is the custom property --stage-max that the script sets from the row; no rule of the style says a width of its own."""
        page = read("web", "shell.html")
        stage = page[page.index("        #game-stage {\n"):page.index("        /* The field that a phone's keyboard types into")]
        self.assertIn("width: min(100%, var(--stage-max), calc((100vh - var(--chrome)) * var(--ar-w) / var(--ar-h)));", stage)
        self.assertIn("width: min(100%, var(--stage-max), calc((100dvh - var(--chrome)) * var(--ar-w) / var(--ar-h)));", stage)
        self.assertNotIn("1280px", stage)
        self.assertIn("--stage-max: 1280px;", page)                                                          # (the 16:9 default until the script sets the shape's own)

    def test_the_pages_keep_no_table_of_steps_and_no_rule_of_a_shape_in_their_style(self):
        for path in (SHELL, LOBBY, MENU_JS):
            text = read(os.path.relpath(path, ROOT))
            name = os.path.basename(path)
            for gone in ("SHAPE_ORDER", "var SHAPES = [", "var SHAPES = {", "{ w: 8, h: 5 }", "{ w: 7, h: 3 }", "--bar-max", "[data-aspect=\"4:3\"]", "[data-aspect=\"16:10\"]", "[data-aspect=\"21:9\"]", "[data-aspect=\"32:9\"]", "fit-tag", "data-aspect=\"4:3\" aria-checked"):
                self.assertNotIn(gone, text, name + " still has " + gone)

    def test_the_game_names_a_shape_in_one_header_only(self):
        found = []
        for folder in ("src", "include"):
            for base, dirs, names in os.walk(os.path.join(ROOT, folder)):
                for name in names:
                    if not name.endswith((".cpp", ".hpp")):
                        continue
                    path = os.path.join(base, name)
                    with open(path, encoding="utf-8") as f:
                        for number, line in enumerate(f, 1):
                            code = line.split("//")[0]
                            if re.search(r'"(?:3:2|16:10|20:9|21:9|32:9|16:9)"', code) and not path.endswith("canvas_layout.hpp"):
                                found.append("%s:%d" % (os.path.relpath(path, ROOT), number))
        self.assertEqual(found, [], "a shape's text in code outside the table's header")


class TheDropdownsAreTheTable(unittest.TestCase):
    """The three dropdowns are empty selects in the markup, so that no page can keep a shape list of its own; each is filled from the table by AntsShapes.fillSelect (the settings menu's too)."""

    def test_the_game_pages_dropdown_and_the_replays_picture_window_are_empty_selects(self):
        page = read("web", "shell.html")
        bar = page[page.index('<div class="view-bar" id="view-bar">'):page.index('id="lock-bar-label"')]
        self.assertEqual(re.findall(r'<select class="pick-select" id="aspect-select"[^>]*></select>', bar).__len__(), 1)
        self.assertIn('<label id="view-bar-label" for="aspect-select">Picture</label>', bar)
        pop = page[page.index('id="picpop"'):page.index('id="b-fsx"')]
        self.assertEqual(len(re.findall(r'<select class="pick-select" id="pic-select"[^>]*></select>', pop)), 1)
        self.assertNotIn("<option", page, "the entries come from the table, not from the markup")
        self.assertEqual(page.count("AntsShapes.fillSelect(select, document)"), 1, "the game page fills every .pick-select once, at the start")

    def test_the_front_pages_picture_row_is_an_empty_select_filled_from_the_table(self):
        page = read("web", "lobby.html")
        row = page[page.index('<div class="shape" id="shape-row">'):page.index('<div class="go">')]
        self.assertEqual(len(re.findall(r'<span class="shape-box"><select class="shape-select" id="aspect-select"[^>]*></select></span>', row)), 1)
        self.assertIn('<label class="shape-label" id="shape-label" for="aspect-select">Picture</label>', row)
        self.assertNotIn("<option", row)
        self.assertIn("AntsShapes.fillSelect($('aspect-select'), document)", page)

    def test_the_settings_menu_builds_its_dropdown_from_the_page_table(self):
        menu = read("web", "game_menu.js")
        self.assertIn("page.shapes.fillSelect(select, doc);", menu)
        self.assertIn("select:not([disabled])", menu)                                                       # (the panel's Tab wraps around it)

    def test_an_entry_is_the_shape_and_its_canvas_and_nothing_else(self):
        for _, label, w, h, _ in page_rows():
            self.assertRegex("%s \u00b7 %d x %d" % (label, w, h), r"^(Classic )?\d+:\d+ \u00b7 \d+ x \d+$")


class ThePagesLoadTheTable(unittest.TestCase):
    def test_both_pages_load_it_before_their_own_code(self):
        shell, lobby = read("web", "shell.html"), read("web", "lobby.html")
        self.assertEqual(shell.count('<script src="shapes.js"></script>'), 1)
        self.assertEqual(lobby.count('<script src="shapes.js"></script>'), 1)
        self.assertLess(shell.index('<script src="shapes.js">'), shell.index("var ANTS_PAGE = (function"))
        self.assertLess(lobby.index('<script src="shapes.js">'), lobby.index("SHAPES_BEGIN"))
        self.assertIn("shapes: window.AntsShapes", shell)                                                    # (the settings menu gets the table from the page)

    def test_the_games_page_exports_read_the_table(self):
        game = read("src", "ants_app", "application.cpp")
        self.assertIn("if (!aspect_at_place(shape, aspect)) return 0;", game)                                # (ants_set_aspect: the number is the shape's place in kAspectTable)
        self.assertIn("case 42: return aspect_place(g_web_app->aspect());", game)                            # (the probe that the browser check reads back)

    def test_the_image_the_web_build_and_the_ci_check_carry_it(self):
        self.assertIn("COPY web/shapes.js /usr/share/nginx/html/shapes.js", read("Dockerfile"))
        self.assertIn('"${SCRIPT_DIR}/web/shapes.js"', read("build_web.sh"))
        self.assertRegex(read(".github", "workflows", "ci.yml"), r"lvl_core_api\.js shapes\.js admin\.html")


@unittest.skipUnless(shutil.which("node"), "node is not installed: web/shapes.js was NOT run")
class TheTablesFunctions(unittest.TestCase):
    def run_js(self, code):
        done = subprocess.run([shutil.which("node"), "-e", "const S = require(process.argv[1]); " + code, SHAPES_JS], capture_output=True, text=True, timeout=60)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        return json.loads(done.stdout)

    def test_the_rows_that_the_functions_read_are_the_tables_rows_with_their_steps(self):
        rows = self.run_js("console.log(JSON.stringify(S.rows));")
        self.assertEqual([(r["name"], r["label"], r["width"], r["height"], r["slot"]) for r in rows], page_rows())
        self.assertEqual([(r["name"], r["stepW"], r["stepH"], r["place"], r["text"]) for r in rows],
                         [("4:3", 4, 3, 0, "Classic 4:3 \u00b7 640 x 480"), ("3:2", 3, 2, 1, "3:2 \u00b7 960 x 640"), ("16:10", 8, 5, 2, "16:10 \u00b7 960 x 600"), ("16:9", 16, 9, 3, "16:9 \u00b7 960 x 540"),
                          ("20:9", 20, 9, 4, "20:9 \u00b7 1200 x 540"), ("21:9", 7, 3, 5, "21:9 \u00b7 1260 x 540"), ("32:9", 32, 9, 6, "32:9 \u00b7 1920 x 540")])

    def test_a_text_is_a_shape_only_by_its_name(self):
        got = self.run_js("console.log(JSON.stringify([S.parse('4:3'), S.parse('16:10 '), S.parse(' 16:9'), S.parse('3:2'), S.parse('32:9'), S.parse('5:4'), S.parse('fit'), S.parse(''), S.parse(null), S.parse(16), S.parse('hasOwnProperty'), S.parse('__proto__'),"
                          " S.placeOf('21:9'), S.placeOf('32:9'), S.placeOf('wide'), S.placeOf(undefined), S.names()]));")
        self.assertEqual(got, ["4:3", None, None, "3:2", "32:9", None, None, None, None, None, None, None, 5, 6, -1, -1, ["4:3", "3:2", "16:10", "16:9", "20:9", "21:9", "32:9"]])

    def test_fill_select_makes_one_option_for_each_row_and_nothing_else(self):
        got = self.run_js("const made = []; const select = { appendChild(o) { made.push([o.value, o.textContent]); } }; const doc = { createElement(tag) { if (tag !== 'option') throw new Error(tag); return {}; } };"
                          " console.log(JSON.stringify([S.fillSelect(select, doc) === select, made]));")
        self.assertTrue(got[0])
        self.assertEqual(got[1], [[r[0], "%s \u00b7 %d x %d" % (r[1], r[2], r[3])] for r in page_rows()])

    def test_the_line_under_a_dropdown_names_the_shape_that_fills_the_screen(self):
        got = self.run_js("console.log(JSON.stringify([S.fillsLine(1920, 1080), S.fillsLine(1440, 900), S.fillsLine(2736, 1824), S.fillsLine(2560, 1080), S.fillsLine(3840, 1080), S.fillsLine(1280, 1024), S.fillsLine(1000, 1000), S.fillsLine(0, 0)]));")
        self.assertEqual(got, ["16:9 fills your screen", "16:10 fills your screen", "3:2 fills your screen", "21:9 fills your screen", "32:9 fills your screen", "Classic 4:3 fills your screen", "", ""])


# the classes that read the files (the mutants' own class is not among them: the copy runs these)
CHECKS = ["TheTwoTablesAgree", "NoCopyOfTheListIsLeft", "TheDropdownsAreTheTable", "ThePagesLoadTheTable", "TheTablesFunctions"]
READS = [os.path.join("include", "ants_app", "canvas_layout.hpp"), "Dockerfile", "build_web.sh", os.path.join(".github", "workflows", "ci.yml")]


class TheMutantsOfTheTable(unittest.TestCase):
    def test_the_page_side_mutants_are_all_noticed_by_this_check(self):
        """tests/data/shape_table_mutants.json (tools/mutate.py runs all of it, the game's side too): each broken copy of a page file must fail this check; run here on a copy of the files that it reads."""
        with open(os.path.join(REPO, "tests", "data", "shape_table_mutants.json"), encoding="utf-8") as f:
            spec = json.load(f)
        ids = [m["id"] for m in spec["mutants"]]
        self.assertEqual(len(ids), len(set(ids)), "an id twice")
        mine = [m for m in spec["mutants"] if m["test"].startswith("python3 tests/scripts/test_web_shapes.py")]
        self.assertGreaterEqual(len(mine), 18)
        keep = lambda folder, names: [n for n in names if not os.path.isdir(os.path.join(folder, n)) and not n.endswith((".js", ".html", ".cpp", ".hpp"))]
        for m in mine:
            with self.subTest(m["id"]):
                text = read(m["file"]) if ROOT == REPO else None
                self.assertEqual(text.count(m["old"]), 1, "%s: the text is not in the file exactly once" % m["id"])
                with tempfile.TemporaryDirectory() as tmp:
                    for folder in ("web", "src", "include"):
                        shutil.copytree(os.path.join(REPO, folder), os.path.join(tmp, folder), ignore=keep)
                    for rel in READS:
                        os.makedirs(os.path.dirname(os.path.join(tmp, rel)), exist_ok=True)
                        shutil.copy(os.path.join(REPO, rel), os.path.join(tmp, rel))
                    with open(os.path.join(tmp, m["file"]), "w", encoding="utf-8", newline="") as f:
                        f.write(text.replace(m["old"], m["new"]))
                    env = dict(os.environ, ANTS_SHAPES_ROOT=tmp)
                    done = subprocess.run([sys.executable, os.path.abspath(__file__)] + CHECKS, capture_output=True, text=True, timeout=300, env=env)
                    self.assertNotEqual(done.returncode, 0, "the check did not notice: " + m["id"])


if __name__ == "__main__":
    unittest.main()
