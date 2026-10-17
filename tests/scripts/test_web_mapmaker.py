#!/usr/bin/env python3
"""The map maker's page (run by ./run_tests.sh --fast, with the other python tests of tests/scripts, and by the CI; docs/MAP_EDITOR.md): web/mapmaker.html and its scripts and style in web/front/.

  - the code is RUN (node, when it is installed): the reader of a file, the view (pan and zoom, that a frame draws only the tiles on the screen), the sizes and their sentences, the checks
    card and the colors of the tiles (tests/scripts/web_mapmaker_check.js), and the check is shown able to fail: broken copies of the scripts are refused by it;
  - the reader's view of all 540 maps of the library is compared with a second reading of the same files here, in Python, with struct (rows, columns and a digest of every tile);
  - the page and the real map core together (tests/scripts/web_mapmaker_core_check.js) when a built module is there ($ANTS_LVL_CORE_DIR, or build_web/lvl_core); without one it says so, and
    the web job of the CI runs it from the image;
  - the page's side is read from the source: the markup, the scripts in their order, every id that the code asks for, what the buttons do, the refusal of a module of another interface
    version, that nothing in the page can write over a file or put a sentence in as markup, and that the image, the CI's check of the image and the local web build carry the page;
  - the opt-in browser check (tests/scripts/test_web_mapmaker.sh) is there and says what it covers.
"""
import array
import hashlib
import os
import re
import shutil
import struct
import subprocess
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FRONT = os.path.join(REPO, "web", "front")
CHECK = os.path.join(REPO, "tests", "scripts", "web_mapmaker_check.js")
CORE_CHECK = os.path.join(REPO, "tests", "scripts", "web_mapmaker_core_check.js")
NODE = shutil.which("node")
SCRIPTS = ("mapmaker_file.js", "mapmaker_tiles.js", "mapmaker_view.js", "mapmaker_sizes.js", "mapmaker_checks.js", "mapmaker_dialog.js", "mapmaker_canvas.js", "mapmaker_app.js")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def run_check(front=None):
    args = [NODE, CHECK] + (["--front", front] if front else [])
    return subprocess.run(args, capture_output=True, text=True, timeout=300)


def library():
    found = []
    for folder in ("Original-Ants/Maps", "Community-Maps"):
        for name in sorted(os.listdir(os.path.join(REPO, folder))):
            if name.lower().endswith(".lvl"):
                found.append(os.path.join(folder, name))
    return found


def read_map(path):
    """(rows, columns, digest) of a version 8 file, read here with struct: the digest is the SHA-256 of the words of both layers, cell by cell (tile of layer 1, tile of layer 2)."""
    with open(os.path.join(REPO, path), "rb") as f:
        data = f.read()
    names = struct.unpack_from("<H", data, 40)[0] + 1
    at = 42 + 11 * names
    rows, columns = struct.unpack_from("<II", data, at)
    rows &= 0xFFFF
    columns &= 0xFFFF
    layer1 = at + 8
    layer2 = layer1 + 6 * rows * columns
    words = array.array("H")
    for i in range(rows * columns):
        words.append(struct.unpack_from("<H", data, layer1 + 6 * i)[0])
        words.append(struct.unpack_from("<H", data, layer2 + 6 * i)[0])
    return rows, columns, hashlib.sha256(words.tobytes()).hexdigest()


@unittest.skipUnless(NODE, "node is not installed")
class TheCodeRuns(unittest.TestCase):
    def test_the_checks_pass(self):
        done = run_check()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("0 failed", done.stdout)
        for part in ("A the file reader", "B the view", "C the sizes", "D the checks card", "E the colors of the tiles"):
            self.assertIn("RUNNING: %s ... PASS" % part, done.stdout)

    def test_the_reader_sees_every_map_of_the_library_as_a_second_reading_does(self):
        done = subprocess.run([NODE, CHECK, "--list-maps"], capture_output=True, text=True, timeout=300)
        self.assertEqual(done.returncode, 0, done.stderr)
        seen = {}
        for line in done.stdout.splitlines():
            name, rows, columns, digest = line.split("\t")
            seen[name] = (int(rows), int(columns), digest)
        files = library()
        self.assertEqual(len(files), 540)
        self.assertEqual(sorted(seen), sorted(files))
        for path in files:
            self.assertEqual(seen[path], read_map(path), path)

    def test_the_check_can_fail_on_broken_copies_of_the_scripts(self):
        mutants = {
            "a zoom that loses the point under the pointer": ("mapmaker_view.js", "view.cx = column - (x - w / 2) / view.scale;", "view.cx = column;"),
            "a second layer read from the wrong place": ("mapmaker_file.js", "layer2 = at + 6 * cells, end", "layer2 = at + 6 * cells + 2, end"),
            "a frame that draws tiles that are not on the screen": ("mapmaker_view.js", "var c0 = Math.max(0, Math.floor(view.cx - w / (2 * view.scale))),", "var c0 = 0,"),
            "a grid that keeps 32 bits of rows in a version 8 file": ("mapmaker_file.js", "rows &= 0xFFFF; columns &= 0xFFFF;", "rows += 0; columns += 0;"),
            "a refusal that keeps the core's long prefix": ("mapmaker_sizes.js", "/^a blank map of [^:]*? cannot be written: /", "/^nothing to strip: /"),
            "a limit written in the card by hand": ("mapmaker_sizes.js", "[c.rows + ' rows', true]", "['90 rows', true]"),
            "a sentence that is not capitalized": ("mapmaker_checks.js", "body.appendChild(el('div', 'msg', capital(f.message)));", "body.appendChild(el('div', 'msg', f.message));"),
            "a link where the finding has no place": ("mapmaker_checks.js", "if (typeof f.row !== 'number' || f.row < 0) return null;", "if (typeof f.row !== 'number') return null;"),
            "a hill with the wrong color": ("mapmaker_tiles.js", "[246, 246, '#4a5fc8']", "[246, 246, '#c0392b']"),
            "a meter that cannot show the Extended limit": ("mapmaker_sizes.js", "side: Math.floor(Math.sqrt(e.cells))", "side: 512"),
        }
        for what, (name, old, new) in mutants.items():
            with self.subTest(what):
                text = read("web", "front", name)
                self.assertEqual(text.count(old), 1, "the text that this mutant changes is not in %s exactly once: %s" % (name, old))
                with tempfile.TemporaryDirectory() as tmp:
                    for script in SCRIPTS:
                        shutil.copy(os.path.join(FRONT, script), os.path.join(tmp, script))
                    with open(os.path.join(tmp, name), "w", encoding="utf-8") as f:
                        f.write(text.replace(old, new))
                    done = run_check(tmp)
                self.assertNotEqual(done.returncode, 0, "the check did not notice: " + what)
                self.assertIn("FAILED", done.stdout)


def built_module():
    for candidate in (os.environ.get("ANTS_LVL_CORE_DIR", ""), os.path.join(REPO, "build_web", "lvl_core")):
        if candidate and os.path.isfile(os.path.join(candidate, "lvl_core.js")) and os.path.isfile(os.path.join(candidate, "lvl_core.wasm")):
            return candidate
    return None


@unittest.skipUnless(NODE, "node is not installed")
class ThePageAndTheRealCore(unittest.TestCase):
    def test_the_page_reads_what_the_core_reports(self):
        module = built_module()
        if not module:
            self.skipTest("no built map core (cmake --build build_web --target lvl_core, or ANTS_LVL_CORE_DIR): the web job of the CI runs this from the image, with --require")
        done = subprocess.run([NODE, CORE_CHECK, "--module", module, "--require"], capture_output=True, text=True, timeout=300)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("0 failed", done.stdout)

    def test_a_missing_module_is_a_skip_unless_it_is_required(self):
        with tempfile.TemporaryDirectory() as tmp:
            done = subprocess.run([NODE, CORE_CHECK, "--module", tmp], capture_output=True, text=True, timeout=60)
            self.assertEqual(done.returncode, 0)
            self.assertIn("SKIP", done.stdout)
            done = subprocess.run([NODE, CORE_CHECK, "--module", tmp, "--require"], capture_output=True, text=True, timeout=60)
            self.assertEqual(done.returncode, 1)


class ThePage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.page = read("web", "mapmaker.html")
        cls.css = read("web", "front", "mapmaker.css")
        cls.code = {name: read("web", "front", name) for name in SCRIPTS}
        cls.app = cls.code["mapmaker_app.js"]

    def test_the_page_is_in_the_classic_look_and_has_one_title_and_its_own_style(self):
        self.assertIn('<link rel="stylesheet" href="/front/classic.css">\n<link rel="stylesheet" href="/front/mapmaker.css">', self.page)
        self.assertEqual(self.page.count("<title>"), 1)
        self.assertIn("<title>Ants (1998) - Map maker</title>", self.page)
        self.assertNotIn("<style", self.page, "the page's style is web/front/mapmaker.css")
        self.assertIn('name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover"', self.page)

    def test_the_scripts_are_loaded_in_their_order_with_the_core_first_and_the_app_last(self):
        tags = re.findall(r'<script src="([^"]+)"></script>', self.page)
        self.assertEqual(tags, ["lvl_core.js", "lvl_core_api.js"] + ["front/" + name for name in SCRIPTS])
        self.assertEqual(self.page.count("<script"), len(tags), "no inline script")
        self.assertLess(self.page.index("front/mapmaker_app.js"), self.page.index("</body>"))

    def test_every_id_that_the_code_asks_for_is_in_the_page_once(self):
        ids = re.findall(r'\bid="([^"]+)"', self.page)
        self.assertEqual(len(ids), len(set(ids)), "an id is used twice")
        asked = set(re.findall(r"\$\('([^']+)'\)", self.app))
        self.assertTrue({"map", "mini", "minimap", "where", "boot", "map-name", "file-in", "dialogs", "top", "body"} <= asked, sorted(asked))
        self.assertEqual(sorted(asked - set(ids)), [], "the app asks for ids that the page does not have")
        for dialog_id in re.findall(r"""['"]?\b(size-rows|size-columns)\b""", self.code["mapmaker_dialog.js"]):
            self.assertNotIn(dialog_id, ids, "the dialog's boxes are made by the dialog")

    def test_every_button_that_the_page_has_is_wired_and_starts_off_until_the_core_is_there(self):
        acts = re.findall(r'data-act="([a-z-]+)"', self.page)
        self.assertEqual(sorted(set(acts)), ["check", "download", "menu", "new", "open", "zoom-in", "zoom-out"])
        for button in re.findall(r"<button[^>]*data-act[^>]*>", self.page):
            self.assertIn(" disabled", button, button)
        self.assertIn("'new': newMap, open:", self.app)
        self.assertIn("download: download, check: showChecks", self.app)
        self.assertIn("menu: function () { dialogs.menu([{ text: 'New'", self.app)
        self.assertIn('<input class="nm" id="map-name" type="text" maxlength="40"', self.page)
        self.assertRegex(self.page, r'<label class="sr" for="map-name">Name of the map</label>')
        self.assertRegex(self.page, r'<input type="file" id="file-in" accept="\.lvl,\.LVL" hidden>')

    def test_the_page_refuses_a_module_of_another_interface_version_before_it_asks_it_anything(self):
        self.assertIn("var found = LvlCore.moduleVersion(module);", self.app)
        self.assertIn("if (found !== LvlCore.EXPECTED_INTERFACE) return fatal(", self.app)
        self.assertLess(self.app.index("LvlCore.moduleVersion(module)"), self.app.index("start(module);"))
        self.assertLess(self.app.index("function start(module) {"), self.app.index("core = LvlCore.wrap(module);"))
        self.assertIn("Reload the page", self.app)
        self.assertIn("createLvlCore", self.app.split("function boot()")[1], "a module that did not load at all is told too")

    def test_a_map_is_read_as_a_place_that_takes_both_kinds_and_the_limits_come_from_the_core(self):
        self.assertEqual(self.app.count("core.checkMap("), 2)
        self.assertEqual(len(re.findall(r"core\.(?:checkMap|readReport)\([^)]*'extended'\)", self.app)), 3)
        self.assertIn("core.limits('new_map')", self.app)
        self.assertIn("core.limits('extended')", self.app)
        self.assertNotRegex(self.code["mapmaker_dialog.js"], r"\b(10000|10,000|262144|262,144|130|512)\b", "a limit is the core's, never a number of the dialog")
        self.assertNotRegex(self.code["mapmaker_sizes.js"], r"\b(10000|10,000|262144|262,144|130|512)\b")

    def test_nothing_can_write_over_a_file_and_no_sentence_is_put_in_as_markup(self):
        for name, text in self.code.items():
            for word in ("innerHTML", "outerHTML", "insertAdjacentHTML", "document.write", "eval(", "new Function", "showSaveFilePicker", "showOpenFilePicker", "createWritable", "FileSystemFileHandle", "XMLHttpRequest", "fetch("):
                self.assertNotIn(word, text, "%s has %s" % (name, word))
        self.assertNotIn("overwrite", self.app.lower())
        self.assertIn("a.download = win.MapFile.downloadName(map.name);", self.app, "a download is a new file the browser saves, named by the map")

    def test_the_page_names_nothing_private(self):
        for name in ("mapmaker.html",) + tuple("front/" + n for n in SCRIPTS) + ("front/mapmaker.css",):
            text = read("web", name)
            self.assertNotRegex(text, r"BW_|ant model|MAP_MAKER|bigger-maps", name)

    def test_the_style_has_the_three_layouts_the_joined_tag_and_only_tokens_it_defines(self):
        self.assertIn("@media (max-width: 600px) {", self.css)
        self.assertIn("@media (min-width: 601px) and (max-height: 450px) {", self.css)
        self.assertIn("@media (max-width: 359px) { .tags .tag-ext, .tags .badge-nv { display: none; } .tags .tag-join { display: inline-block; } }", self.css)
        defined = set(re.findall(r"--([a-z-]+):", read("web", "front", "classic.css") + self.css))
        used = set(re.findall(r"var\(--([a-z-]+)", self.css))
        self.assertEqual(sorted(used - defined), [])
        self.assertRegex(self.css, r"\.cv canvas\.map \{[^}]*touch-action: none;")
        self.assertRegex(self.css, r"\.screen\.ed \{ height: 100vh; height: 100dvh;")

    def test_a_dialog_holds_the_focus_closes_on_escape_and_leaves_the_page_behind_it_inert(self):
        dialog = self.code["mapmaker_dialog.js"]
        for part in ("role', 'dialog'", "aria-modal", "e.key === 'Escape'", "e.key !== 'Tab'", "setAttribute('inert', '')", "removeAttribute('inert')", "opener.focus()"):
            self.assertIn(part, dialog, part)
        self.assertIn("radiogroup", dialog)
        self.assertIn("aria-invalid", dialog)
        self.assertIn("aria-live", dialog)

    def test_the_size_boxes_and_the_cards_say_what_the_pictures_say(self):
        dialog = self.code["mapmaker_dialog.js"]
        self.assertIn("'Works in the original game and in the new version.'", dialog)
        self.assertIn("'Bigger and taller. Works in the new version only: the original game cannot open it.'", dialog)
        self.assertIn("'Only the new version can open an Extended map. You can turn it into a Classic map later if it fits.'", dialog)
        self.assertIn("'Make it an Extended map'", dialog)
        self.assertIn("'What kind of map do you want to make?'", dialog)
        self.assertIn("' cells of ' + Sizes.number(live.max) + ' \u00b7 the file will be ' + live.text", dialog)


class TheImageCarriesThePage(unittest.TestCase):
    def test_the_dockerfile_the_ci_and_the_local_build_have_it(self):
        self.assertRegex(read("Dockerfile"), r"(?m)^COPY web/mapmaker\.html /usr/share/nginx/html/mapmaker\.html$")
        self.assertRegex(read(".github", "workflows", "ci.yml"), r"ls -l [^\n]*\bmapmaker\.html\b")
        self.assertRegex(read(".github", "workflows", "ci.yml"), r"ls -l [^\n]*\bfront/mapmaker_app\.js\b")
        self.assertIn('web/mapmaker.html', read("build_web.sh"))
        ci = read(".github", "workflows", "ci.yml")
        self.assertIn("node tests/scripts/web_mapmaker_core_check.js --module lvl_core_from_image --require", ci)

    def test_every_file_of_the_page_is_in_front_and_the_folder_is_copied_whole(self):
        self.assertIn("COPY web/front/ /usr/share/nginx/html/front/", read("Dockerfile"))
        for name in SCRIPTS + ("mapmaker.css",):
            self.assertTrue(os.path.isfile(os.path.join(FRONT, name)), name)

    def test_no_file_is_over_the_line_ceiling_of_the_hygiene_check(self):
        for name in SCRIPTS + ("mapmaker.css",):
            self.assertLess(len(read("web", "front", name).splitlines()), 1500, name)
        self.assertLess(len(read("web", "mapmaker.html").splitlines()), 1500)

    def test_the_opt_in_browser_check_is_there_and_says_what_it_covers(self):
        script = read("tests", "scripts", "test_web_mapmaker.sh")
        self.assertIn("OPT-IN, not part of ./run_tests.sh", script)
        self.assertIn("web_mapmaker_browser.py", script)
        self.assertTrue(os.access(os.path.join(REPO, "tests", "scripts", "test_web_mapmaker.sh"), os.X_OK))
        text = read("tests", "scripts", "web_mapmaker_browser.py")
        for part in ("panned", "zoomed", "two fingers", "keyboard", "New map", "opened from the disk", "Download", "Check map", "512 x 512", "another interface version"):
            self.assertIn(part, text, part)


if __name__ == "__main__":
    unittest.main()
