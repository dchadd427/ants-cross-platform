#!/usr/bin/env python3
"""The map maker's page (run by ./run_tests.sh --fast, with the other python tests of tests/scripts, and by the CI; docs/MAP_EDITOR.md): web/mapmaker.html and its scripts and style in web/front/.

  - the code is RUN (node, when it is installed): the reader of a file, the view (pan and zoom, that a frame draws only the tiles on the screen), the sizes and their sentences, the checks
    card and the colors of the tiles (tests/scripts/web_mapmaker_check.js), the drawing (the brush, a stroke, fill, undo and redo as small patches, the dictionary, the palette and the tools
    on a fake page: tests/scripts/web_mapmaker_edit_check.js), and the checks are shown able to fail: broken copies of the scripts are refused by them;
  - the reader's view of all 540 maps of the library is compared with a second reading of the same files here, in Python, with struct (rows, columns and a digest of every tile, and the lists
    of starting ants and food piles after the layers: tests/scripts/web_mapmaker_blocks_check.js, with broken copies that it must refuse);
  - the page and the real map core together (tests/scripts/web_mapmaker_core_check.js) when a built module is there ($ANTS_LVL_CORE_DIR, or build_web/lvl_core); without one it says so, and
    the web job of the CI runs it from the image;
  - the properties of the map (tests/scripts/web_mapmaker_props_check.js): the words of the dialog, the change that the history takes, the dialog on a small fake of the DOM with a stand-in for the
    core, and broken copies of the scripts that it must refuse; with a built module also the 540 maps and the dialog on the real core (web_mapmaker_core_check.js, case I);
  - copy, convert and crop (tests/scripts/web_mapmaker_convert_check.js): the arithmetic of the frame (on thousands of random pulls it stays on the map and within the limits), the names of a new map, the
    Crop to fit screen and the dialogs on a small fake of the DOM with a stand-in for the core, and broken copies of the scripts that it must refuse; with a built module also the real flows
    (web_mapmaker_core_check.js, case J);
  - the page's side is read from the source: the markup, the scripts in their order, every id that the code asks for, what the buttons do, the refusal of a module of another interface
    version, that nothing in the page can write over a file or put a sentence in as markup, and that the image, the CI's check of the image and the local web build carry the page;
  - the opt-in browser check (tests/scripts/test_web_mapmaker.sh) is there and says what it covers.
"""
import array
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FRONT = os.path.join(REPO, "web", "front")
CHECK = os.path.join(REPO, "tests", "scripts", "web_mapmaker_check.js")
DIALOG_CHECK = os.path.join(REPO, "tests", "scripts", "web_mapmaker_dialog_check.js")
PROPS_CHECK = os.path.join(REPO, "tests", "scripts", "web_mapmaker_props_check.js")
CONVERT_CHECK = os.path.join(REPO, "tests", "scripts", "web_mapmaker_convert_check.js")
SAVE_CHECK = os.path.join(REPO, "tests", "scripts", "web_mapmaker_save_check.js")
MINE_CHECK = os.path.join(REPO, "tests", "scripts", "web_mapmaker_mine_check.js")
SUBMIT_CHECK = os.path.join(REPO, "tests", "scripts", "web_mapmaker_submit_check.js")
EDIT_CHECK = os.path.join(REPO, "tests", "scripts", "web_mapmaker_edit_check.js")
CORE_CHECK = os.path.join(REPO, "tests", "scripts", "web_mapmaker_core_check.js")
NODE = shutil.which("node")
BAIL = os.path.join(REPO, "tests", "scripts", "web_mapmaker_bail.js")
BLOCKS_CHECK = os.path.join(REPO, "tests", "scripts", "web_mapmaker_blocks_check.js")
OBJECTS_CHECK = os.path.join(REPO, "tests", "scripts", "web_mapmaker_objects_check.js")
GROUND_CHECK = os.path.join(REPO, "tests", "scripts", "web_mapmaker_ground_check.js")
SPRITES_CHECK = os.path.join(REPO, "tests", "scripts", "web_mapmaker_sprites_check.js")
EDGES_CHECK = os.path.join(REPO, "tests", "scripts", "web_mapmaker_edges_check.js")
ANTS_CHECK = os.path.join(REPO, "tests", "scripts", "web_mapmaker_ants_check.js")
PICKUP_CHECK = os.path.join(REPO, "tests", "scripts", "web_mapmaker_pickup_check.js")
SCRIPTS = ("mapmaker_file.js", "mapmaker_tiles.js", "mapmaker_solid.js", "mapmaker_edit.js", "mapmaker_blocks.js", "mapmaker_objects.js", "mapmaker_palette.js", "mapmaker_view.js", "mapmaker_sizes.js", "mapmaker_checks.js", "mapmaker_dialog.js", "mapmaker_props.js", "mapmaker_crop.js", "mapmaker_store.js", "mapmaker_mine.js", "mapmaker_convert.js", "mapmaker_canvas.js", "mapmaker_tools.js", "mapmaker_app.js")
LAZY_SCRIPTS = ("mapmaker_ground.js", "mapmaker_sprites.js", "mapmaker_edges.js", "mapmaker_ants.js", "mapmaker_pickup.js")                      # loaded by the app after the page is up (mapmaker_app.js, loadParts): not tags of the page, but the checks and the broken copies need them
ALL_SCRIPTS = SCRIPTS + LAZY_SCRIPTS


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


# The slowest run of one check that this file makes, alone on a four-core machine, is about 13 s (the whole Pick up check on the library); 600 s is a stall, not a slow runner. A run that passes this
# limit fails with what it printed so far and the load of the machine (a 2 s check once took more than 300 s on a macOS runner and nothing said why).
CHECK_LIMIT = 600


def run_check(front=None, script=CHECK, extra=()):
    """node runs a check on the real scripts (front=None: the whole check, which must pass) or on a folder of BROKEN copies (front=DIR: the check must fail, and the run ends at the first line that says so:
    tests/scripts/web_mapmaker_bail.js; the sweeps of the 540 maps that follow in the other cases would only repeat a verdict that is known)"""
    args = [NODE] + (["--require", BAIL] if front else []) + [script] + (["--front", front] if front else []) + list(extra)
    try:
        return subprocess.run(args, capture_output=True, text=True, timeout=CHECK_LIMIT)
    except subprocess.TimeoutExpired as stopped:
        def text(b):
            return b.decode("utf-8", "replace") if isinstance(b, bytes) else (b or "")
        raise AssertionError("the check did not end in %d s: %s\nload average %s, %d cores\nstdout so far: %s\nstderr so far: %s" % (
            CHECK_LIMIT, " ".join(os.path.relpath(a, REPO) if os.path.isabs(a) and a.startswith(REPO) else a for a in args[1:]), ", ".join("%.1f" % v for v in os.getloadavg()) if hasattr(os, "getloadavg") else "unknown",
            os.cpu_count() or 0, text(stopped.stdout)[-1500:], text(stopped.stderr)[-1500:])) from None


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


def read_blocks(path):
    """what the page reads of the two lists after the layers, read here with struct: (block 1 reads, block 2 reads, records, start markers of tile 152 to 155, piles as 'row,column,units,points,threshold,tile,...')"""
    with open(os.path.join(REPO, path), "rb") as f:
        data = f.read()
    names = struct.unpack_from("<H", data, 40)[0] + 1
    at = 42 + 11 * names
    rows, columns = struct.unpack_from("<II", data, at)
    rows &= 0xFFFF
    columns &= 0xFFFF
    at += 8 + 12 * rows * columns
    if at + 2 > len(data):
        return (0, 0, 0, (0, 0, 0, 0), 0, "")
    count = struct.unpack_from("<H", data, at)[0]
    if at + 2 + 6 * count > len(data):
        return (0, 0, 0, (0, 0, 0, 0), 0, "")
    marks = [0, 0, 0, 0]
    for i in range(count):
        tile = struct.unpack_from("<H", data, at + 2 + 6 * i)[0]
        if 152 <= tile <= 155:
            marks[tile - 152] += 1
    at += 2 + 6 * count
    if at + 2 > len(data):
        return (1, 0, count, tuple(marks), 0, "")
    piles = []
    n = struct.unpack_from("<H", data, at)[0]
    at += 2
    for _ in range(n):
        if at + 10 > len(data):
            return (1, 0, count, tuple(marks), 0, "")
        row, column, units, points, stages = struct.unpack_from("<HHHHH", data, at)
        if stages == 0 or at + 10 + 4 * stages > len(data):
            return (1, 0, count, tuple(marks), 0, "")
        flat = [v for k in range(stages) for v in struct.unpack_from("<HH", data, at + 10 + 4 * k)]
        piles.append(",".join(str(v) for v in [row, column, units, points] + flat))
        at += 10 + 4 * stages
    return (1, 1, count, tuple(marks), len(piles), ";".join(piles))


def read_objects(path):
    """{tile: (objects, {footprint: count})} of the objects that stand on more than one cell in a version 8 file, read here with struct: the layer-2 cells of one tile and one anchor, where the anchor cell is
    one of them and is flagged. A footprint is the tuple of the (row, column) offsets from the anchor."""
    with open(os.path.join(REPO, path), "rb") as f:
        data = f.read()
    names = struct.unpack_from("<H", data, 40)[0] + 1
    at = 42 + 11 * names
    rows, columns = struct.unpack_from("<II", data, at)
    rows &= 0xFFFF
    columns &= 0xFFFF
    layer2 = at + 8 + 6 * rows * columns
    groups = {}
    for i in range(rows * columns):
        tile, flags, third = struct.unpack_from("<HHH", data, layer2 + 6 * i)
        if tile == 0x7FFE:
            continue
        row, column = divmod(i, columns)
        group = groups.setdefault((tile, third & 255, third >> 8), [[], False])
        group[0].append((row - (third & 255), column - (third >> 8)))
        if row == (third & 255) and column == (third >> 8) and flags & 1:
            group[1] = True
    out = {}
    for (tile, _, _), (cells, origin) in groups.items():
        if origin and len(cells) > 1 and not (245 <= tile <= 248 or 129 <= tile <= 132) and not tile_flags()[tile] & 0x16:
            objects, shapes = out.setdefault(tile, (0, {}))
            key = tuple(sorted(cells))
            shapes[key] = shapes.get(key, 0) + 1
            out[tile] = (objects + 1, shapes)
    return out


_FLAGS = []


def tile_flags():
    """kTileFlags of src/ants_sim/movement_tables_data.inc: the flag bits of the game's 1,344 tile ids (0x01 solid, 0x02 food, 0x04 power-up, 0x10 plant)"""
    if not _FLAGS:
        text = read("src", "ants_sim", "movement_tables_data.inc")
        start = text.index("kTileFlags[1344] = {")
        body = re.sub(r"/\*.*?\*/", "", text[text.index("{", start) + 1:text.index("};", start)], flags=re.S)
        _FLAGS.extend(int(v, 0) for v in re.findall(r"0x[0-9a-fA-F]+|\d+", body))
    return _FLAGS


@unittest.skipUnless(NODE, "node is not installed")
class TheCodeRuns(unittest.TestCase):
    def test_the_checks_pass(self):
        done = run_check()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("0 failed", done.stdout)
        for part in ("A the file reader", "B the view", "C the sizes", "D the checks card", "E the colors of the tiles"):
            self.assertIn("RUNNING: %s ... PASS" % part, done.stdout)

    def test_the_dialogs_pass_their_checks_on_a_fake_dom(self):
        done = run_check(script=DIALOG_CHECK)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("RUNNING: A the box ... PASS", done.stdout)
        self.assertIn("RUNNING: B New map ... PASS", done.stdout)

    def test_the_dialog_check_can_fail_on_broken_copies_of_the_script(self):
        mutants = {
            "a page that stays live behind a dialog": ("setAttribute('inert', '')", "setAttribute('data-was-inert', '')"),
            "Escape heard only inside the box (not after a click on plain text)": ("doc.addEventListener('keydown', onKey, true);", "scrim.addEventListener('keydown', onKey);"),
            "a key listener that is never taken off": ("doc.removeEventListener('keydown', onKey, true);", "void 0;"),
            "a Tab that does not wrap": ("{ e.preventDefault(); firstItem.focus(); }", "{ e.preventDefault(); }"),
            "a focus that is not given back": ("opener.focus();", "void 0;"),
            "an Extended map that loses the sizes typed for the Classic map": ("if (!keepSizes) {", "if (true) {"),
            "a refusal that is not shown": ("setNote('bad', '!', why.text, button);", "setNote('', '', '');"),
            "a Make map that stays on for a size that is too big": ("ctl.setDisabled('make', true);\n        }\n\n        function make()", "}\n\n        function make()"),
        }
        for what, (old, new) in mutants.items():
            with self.subTest(what):
                text = read("web", "front", "mapmaker_dialog.js")
                self.assertEqual(text.count(old), 1, "the text that this mutant changes is not in mapmaker_dialog.js exactly once: %s" % old)
                with tempfile.TemporaryDirectory() as tmp:
                    for script in ALL_SCRIPTS:
                        shutil.copy(os.path.join(FRONT, script), os.path.join(tmp, script))
                    with open(os.path.join(tmp, "mapmaker_dialog.js"), "w", encoding="utf-8") as f:
                        f.write(text.replace(old, new))
                    done = run_check(tmp, DIALOG_CHECK)
                self.assertNotEqual(done.returncode, 0, "the check did not notice: " + what)

    def test_the_properties_pass_their_checks_on_a_fake_dom(self):
        done = run_check(script=PROPS_CHECK)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("0 failed", done.stdout)
        for part in ("A the words", "B the change", "C the dialog on a Classic map", "C2 a description of 30 bytes", "C3 the numbers and the ants", "D the dialog on an Extended map", "D2 a text the core refuses",
                     "E Apply and Cancel", "F text is text"):
            self.assertIn("RUNNING: %s ... PASS" % part, done.stdout)

    def test_the_properties_check_can_fail_on_broken_copies_of_the_scripts(self):
        mutants = {
            "a counter that counts letters, not bytes": ("mapmaker_props.js", "var b = bytes(), n = b.length, over", "var b = bytes(), n = Array.from(st.text).length, over"),
            "a counter that always says letters count double": ("mapmaker_props.js", "(n > chars ? ' (letters like", "(true ? ' (letters like"),
            "a counter that never says letters count double": ("mapmaker_props.js", "(n > chars ? ' (letters like", "(false ? ' (letters like"),
            "a cut that is not told": ("mapmaker_props.js", "cutNote.hidden = value === typed;", "cutNote.hidden = true;"),
            "a box that does not stop at what fits": ("mapmaker_props.js", "if (after > limit && after > before) value =", "if (false) value ="),
            "a text that grows past a limit it came with": ("mapmaker_props.js", "before <= limit ? cutTo(value, limit) : st.text;", "cutTo(value, limit);"),
            "a text cut in the middle of a letter": ("mapmaker_props.js", "if (used + n > limit) break;", "if (used > limit) break;"),
            "a description of the file that is not kept as it came": ("mapmaker_props.js", "function bytes() { return st.text === now.description ? now.raw : encode(st.text); }", "function bytes() { return encode(st.text); }"),
            "an old description read as UTF-8 with holes": ("mapmaker_props.js", "{ fatal: true }", "{ fatal: false }"),
            "a change put on from the first piece to the last": ("mapmaker_props.js", "var made = splices.slice().reverse(), out = oldBytes;", "var made = splices.slice(), out = oldBytes;"),
            "a change that is not held to the new file": ("mapmaker_props.js", "for (i = 0; i < out.length; i++) if (out[i] !== newBytes[i]) return null;", "void 0;"),
            "a piece that is not cut down to what differs": ("mapmaker_props.js", "while (head < removed.length && head < inserted.length && removed[head] === inserted[head]) head++;", "void 0;"),
            "an Apply that closes the dialog when the core refuses": ("mapmaker_props.js", "onClick: function (c) { if (apply()) c.close(); } }]", "onClick: function (c) { apply(); c.close(); } }]"),
            "a Cancel that applies": ("mapmaker_props.js", "{ id: 'cancel', text: 'Cancel', ghost: true }", "{ id: 'cancel', text: 'Cancel', ghost: true, onClick: function () { apply(); } }"),
            "minutes that can be 0": ("mapmaker_props.js", "minutes: { min: 1, max: 120 }", "minutes: { min: 0, max: 120 }"),
            "eggs that can be any number": ("mapmaker_props.js", "eggs: { min: 0, max: 255 }", "eggs: { min: 0, max: 65535 }"),
            "a value the map came with that is cut to the range": ("mapmaker_props.js", "(v === now[name] ? v : clamp(v, range))", "clamp(v, range)"),
            "a number box that keeps letters": ("mapmaker_props.js", "var digits = input.value.replace(/\\D/g, '');", "var digits = input.value;"),
            "a none that loses the none of the file": ("mapmaker_props.js", "(typeIndex(now.tile) === 0 ? now.tile : NONE)", "NONE"),
            "ant types that stop at the ends": ("mapmaker_props.js", "var next = (i + step + TYPES.length) % TYPES.length;", "var next = Math.max(0, Math.min(TYPES.length - 1, i + step));"),
            "a sentence of the core that is not finished with a full stop": ("mapmaker_props.js", "return /\\.$/.test(t) ? t : t + '.';", "return t;"),
            "a Cut it to N that does not cut": ("mapmaker_props.js", "st.text = cutTo(st.text, limit); textBox.value = st.text;", "textBox.value = st.text;"),
            "a line break that stays": ("mapmaker_props.js", "textBox.value.replace(/[\\r\\n]+/g, ' ')", "textBox.value"),
            "an Apply that stays on when the core refuses": ("mapmaker_props.js", "if (ctl) ctl.setDisabled('apply', !verdict.ok);", "void 0;"),
            "a core asked as a place that takes Classic maps only": ("mapmaker_props.js", "now.eggs, now.tile, 'extended');", "now.eggs, now.tile, 'new_map');"),
            "a plural that says 1 minutes": ("mapmaker_props.js", "function plural(n, one, many) { return n + ' ' + (n === 1 ? one : many); }", "function plural(n, one, many) { return n + ' ' + many; }"),
            "a map that comes with no description shown with empty quotes": ("mapmaker_props.js", "props.description ? '\\u201c' + props.description + '\\u201d' : 'No description'", "'\\u201c' + props.description + '\\u201d'"),
            "a dialog whose box of lines is not in the Tab trap": ("mapmaker_dialog.js", "textarea:not([disabled]), ", ""),
        }
        for what, (name, old, new) in mutants.items():
            with self.subTest(what):
                text = read("web", "front", name)
                self.assertEqual(text.count(old), 1, "the text that this mutant changes is not in %s exactly once: %s" % (name, old))
                with tempfile.TemporaryDirectory() as tmp:
                    for script in ALL_SCRIPTS:
                        shutil.copy(os.path.join(FRONT, script), os.path.join(tmp, script))
                    with open(os.path.join(tmp, name), "w", encoding="utf-8") as f:
                        f.write(text.replace(old, new))
                    done = run_check(tmp, PROPS_CHECK)
                self.assertNotEqual(done.returncode, 0, "the check did not notice: " + what)

    def test_copy_convert_and_crop_pass_their_checks_on_a_fake_dom(self):
        done = run_check(script=CONVERT_CHECK)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("0 failed", done.stdout)
        for part in ("A the frame", "B the names", "C the Crop to fit screen", "D Copy this map", "E Start an Extended map, Make a Classic map, the reasons", "F Crop it to fit"):
            self.assertIn("RUNNING: %s ... PASS" % part, done.stdout)

    def test_the_convert_check_can_fail_on_broken_copies_of_the_scripts(self):
        crop, convert = "mapmaker_crop.js", "mapmaker_convert.js"
        mutants = {
            "a frame that starts at the top left": (crop, "row: Math.floor((rows - b.rows) / 2), column: Math.floor((columns - b.columns) / 2)", "row: 0, column: 0"),
            "a start frame that is as wide as the limit allows, whatever the cells": (crop, "columns: Math.max(1, Math.min(m.columns, Math.floor(m.cells / m.rows)))", "columns: m.columns"),
            "a frame that can be moved off the map (rows)": (crop, "row: clamp(f.row + dr, 0, rows - f.rows)", "row: f.row + dr"),
            "a frame that can be moved off the map (columns)": (crop, "column: clamp(f.column + dc, 0, columns - f.columns)", "column: f.column + dc"),
            "a pull that is not held to the columns of the limit": (crop, "if (right - left > m.columns) { if (w) left = right - m.columns; else right = left + m.columns; }", "void 0;"),
            "a pull that is not held to the rows of the limit": (crop, "if (bottom - top > m.rows) { if (n) top = bottom - m.rows; else bottom = top + m.rows; }", "void 0;"),
            "a pull that is not held to the cells of the limit": (crop, "if ((bottom - top) * (right - left) > m.cells) {", "if (false) {"),
            "a frame that is shortened at its top for a side pull": (crop, "else top += Math.floor((bottom - top - fit) / 2), bottom = top + fit;", "else bottom = top + fit;"),
            "a shortening that says nothing": (crop, "note = w || e ? 'rows' : '';", "note = '';"),
            "a stop that says it shortened the frame": (crop, "note = w || e ? 'rows' : '';", "note = 'rows';"),
            "a left edge that can pass the right one": (crop, "left = clamp(left + dc, 0, right - 1)", "left = clamp(left + dc, 0, right)"),
            "a bottom edge that can leave the map": (crop, "bottom = clamp(bottom + dr, top + 1, rows)", "bottom = bottom + dr"),
            "an arrow that goes the wrong way": (crop, "ArrowLeft: [0, -1]", "ArrowLeft: [0, 1]"),
            "an arrow that moves two cells": (crop, "return d ? move(f, d[0], d[1], rows, columns) : null;", "return d ? move(f, d[0] * 2, d[1] * 2, rows, columns) : null;"),
            "a last row that is one too many": (crop, "range: 'Rows ' + f.row + ' to ' + (f.row + f.rows - 1)", "range: 'Rows ' + f.row + ' to ' + (f.row + f.rows)"),
            "rows that count from 1": (crop, "range: 'Rows ' + f.row + ' to '", "range: 'Rows ' + (f.row + 1) + ' to '"),
            "a widest shape that is as tall as the tallest": (crop, "rows: Math.max(1, Math.min(l.rows, rows, Math.floor(l.cells / wc))), columns: wc", "rows: Math.min(l.rows, rows), columns: wc"),
            "a sentence that swaps rows and columns": (crop, "so it can be ' + f.rows + ' rows tall at most", "so it can be ' + f.columns + ' rows tall at most"),
            "a pointer that counts pixels as cells": (crop, "dc = Math.round((e.clientX - grab.x) * k)", "dc = Math.round((e.clientX - grab.x))"),
            "a handle that moves the whole frame": (crop, "if (grab.h) { out = resize(", "if (false) { out = resize("),
            "a pointer that was let go and still drags": (crop, "['pointerup', 'pointercancel', 'lostpointercapture'].forEach(function (t) { box.addEventListener(t, function () { grab = null; }); });", "void 0;"),
            "an Escape that does nothing": (crop, "if (e.key === 'Escape') { e.stopPropagation(); leave(env.onCancel); }", "if (false) { }"),
            "a key listener that is never taken off": (crop, "doc.removeEventListener('keydown', onKey, true);", "void 0;"),
            "a picture that stays in the stage": (crop, "stage.removeChild(pic); host.hidden = true;", "host.hidden = true;"),
            "a Use this part that hands over the start frame": (crop, "function onUse() { var f = frame;", "function onUse() { var f = start(L, R, C);"),
            "an editor that stays hidden": (crop, "host.hidden = true; if (env.editor) env.editor.hidden = false;", "host.hidden = true;"),
            "keys that are swallowed whatever they are": (crop, "if (out) { e.preventDefault(); set(out, ''); }", "{ e.preventDefault(); if (out) set(out, ''); }"),
            "a shape that is never marked": (crop, "x.chip.className = 'chip' + (x.s.rows === frame.rows && x.s.columns === frame.columns ? ' on' : '')", "x.chip.className = 'chip'"),
            "a note that stays after the frame is moved": (crop, "note('warn', '!', why ? noteText(frame, L) : ''); show(); }", "if (why) note('warn', '!', noteText(frame, L)); show(); }"),
            "a picture with the wrong proportions": (crop, "stage.style.setProperty('--ar', C + '/' + R);", "stage.style.setProperty('--ar', R + '/' + C);"),
            "a limit that is a number of the page": (crop, "It never has more than ' + L.rows + ' rows, '", "It never has more than 90 rows, '"),
            "a copy that does not count on from the number": (convert, "n = m ? parseInt(m[2], 10) + 1 : 2", "n = m ? parseInt(m[2], 10) : 2"),
            "a name that is taken and used again": (convert, "while (taken(tried));", "while (false);"),
            "a name that goes over 40 characters": (convert, "Array.from(name).slice(0, MAX_NAME - tail)", "Array.from(name).slice(0, MAX_NAME)"),
            "names that differ in capitals": ("mapmaker_store.js", "function key(name) { return clean(name).toLowerCase(); }", "function key(name) { return clean(name); }"),
            "a name with blanks round it": ("mapmaker_store.js", ".replace(/^ | $/g, '')", ""),
            "reasons in the order of the core": (convert, "return rank(a) - rank(b); });", "return 0; });"),
            "a short reason that keeps the semicolon": (convert, ".replace('; ', ' and ')", ".replace('; ', ', ')"),
            "a name of the open map that is free": (convert, "f.ok = !!name && !taken(name);", "f.ok = !!name;"),
            "a copy that is the open map's own array": (convert, "env.onMade(cur.bytes.slice(), nm.name());", "env.onMade(cur.bytes, nm.name());"),
            "a Make new map that stays on for a taken name": (convert, "'Next: what changes';\n                ctl.setDisabled('make', !nm.ok);", "'Next: what changes';\n                ctl.setDisabled('make', false);"),
            "a Make the new Extended map that stays on for a taken name": (convert, "if (ctl) ctl.setDisabled('make', !nm.ok); });", "if (ctl) ctl.setDisabled('make', false); });"),
            "a Classic choice that is never dimmed": (convert, ", !fits)", ", false)"),
            "an Extended map that is not the core's": (convert, "var made = core.convertToExtended(cur.bytes);", "var made = { status: 0, json: { ok: true }, bytes: cur.bytes };"),
            "an Extended name that is the open map's": (convert, "suffixed(cur.name, 'Extended')", "cur.name"),
            "a Classic name that is the open map's": (convert, "suffixed(w.sourceName, 'Classic')", "w.sourceName"),
            "a crop that is offered when it does not help": (convert, "sizes ? act('Crop it to fit...', crop) : ''", "act('Crop it to fit...', crop)"),
            "a shortening that is offered when it does not help": (convert, "text ? act('Shorten the description...', shorten) : ''", "act('Shorten the description...', shorten)"),
            "a crop with the corner swapped": (convert, "core.crop(w.bytes, f.row, f.column, f.rows, f.columns, 'extended')", "core.crop(w.bytes, f.column, f.row, f.rows, f.columns, 'extended')"),
            "a Cancel of the crop screen that goes nowhere": (convert, "onCancel: function () { classic(w); }", "onCancel: function () { }"),
            "a second crop that forgets the first": (convert, "row: was.row + f.row", "row: f.row"),
            "a Keep it Extended that makes a map": (convert, "{ id: 'keep', text: 'Keep it Extended' }", "{ id: 'keep', text: 'Keep it Extended', onClick: function () { env.onMade(w.bytes, 'x'); } }"),
            "a part that does not say what the core cut": (convert, "(cr ? cr.notes : []).concat(res.json.notes)", "res.json.notes"),
            "a badge on the wrong card": (convert, "(which === 'same') === ext ? h('span', 'badge-nv', 'New version only') : ''", "(which === 'same') ? h('span', 'badge-nv', 'New version only') : ''"),
        }
        for what, (name, old, new) in mutants.items():
            with self.subTest(what):
                text = read("web", "front", name)
                self.assertEqual(text.count(old), 1, "the text that this mutant changes is not in %s exactly once: %s" % (name, old))
                with tempfile.TemporaryDirectory() as tmp:
                    for script in ALL_SCRIPTS:
                        shutil.copy(os.path.join(FRONT, script), os.path.join(tmp, script))
                    with open(os.path.join(tmp, name), "w", encoding="utf-8") as f:
                        f.write(text.replace(old, new))
                    done = run_check(tmp, CONVERT_CHECK)
                self.assertNotEqual(done.returncode, 0, "the check did not notice: " + what)

    def test_save_as_a_new_map_and_the_drafts_pass_their_checks_on_a_fake_dom(self):
        done = run_check(script=SAVE_CHECK)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("0 failed", done.stdout)
        for part in ("A the names", "B the store on a fake database", "C the site's list of maps", "D the Save dialog", "E the name check in the other dialogs", "F a Classic name that is taken"):
            self.assertIn("RUNNING: %s ... PASS" % part, done.stdout)

    def test_the_save_check_can_fail_on_the_broken_copies_of_the_list(self):
        """tests/data/mapmaker_save_mutants.json (tools/mutate.py runs it too, with `--keep-going --jobs 1`): each broken copy of the store or of the name check must fail the check"""
        with open(os.path.join(REPO, "tests", "data", "mapmaker_save_mutants.json"), encoding="utf-8") as f:
            spec = json.load(f)
        self.assertGreaterEqual(len(spec["mutants"]), 40)
        ids = [m["id"] for m in spec["mutants"]]
        self.assertEqual(len(ids), len(set(ids)), "an id twice")
        with tempfile.TemporaryDirectory() as tmp:
            for name in ALL_SCRIPTS:
                shutil.copy(os.path.join(FRONT, name), os.path.join(tmp, name))
            for m in spec["mutants"]:
                with self.subTest(m["id"]):
                    name = os.path.basename(m["file"])
                    text = read(*m["file"].split("/"))
                    self.assertEqual(text.count(m["old"]), 1, "%s: the text is not in %s exactly once" % (m["id"], name))
                    with open(os.path.join(tmp, name), "w", encoding="utf-8", newline="") as f:
                        f.write(text.replace(m["old"], m["new"]))
                    try:
                        done = run_check(tmp, SAVE_CHECK)
                    finally:
                        shutil.copy(os.path.join(FRONT, name), os.path.join(tmp, name))
                    self.assertNotEqual(done.returncode, 0, "the check did not notice: " + m["id"])

    def test_the_drawing_passes_its_checks(self):
        done = run_check(script=EDIT_CHECK)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("0 failed", done.stdout)
        for part in ("A the brush and the line", "B strokes", "C fill", "D undo and redo", "E the dictionary", "F the palette", "G the tools on a fake page", "H the canvas on a fake page", "H2 a tap tool on the canvas", "I hills",
                     "J starting ants and food piles", "J2 the history of the lists is bounded", "K Hills, Ants and Food on a fake page", "L the Bigger tool on a fake page", "M the properties of the map as one step"):
            self.assertIn("RUNNING: %s ... PASS" % part, done.stdout)

    def test_the_drawing_check_can_fail_on_broken_copies_of_the_scripts(self):
        mutants = {
            "an object in a version 9 file that is its own anchor by a number that only version 8 means": ("mapmaker_edit.js", "if (file.version !== 8) return 0;", "if (file.version === 0) return 0;"),
            "an eraser that takes one cell of a hill and leaves the rest": ("mapmaker_edit.js", "var cells = objectCells(f, idx), n = 0;", "var cells = [idx], n = 0;"),
            "an object put on a hill that leaves the rest of the hill": ("mapmaker_edit.js", "n += removeObject(idx);       // (a bigger object goes whole: never half a hill)", "void 0;"),
            "a history that has no end": ("mapmaker_edit.js", "while (undo.length > 1 && (undo.length > MAX_STROKES || records > MAX_RECORDS))", "while (false)"),
            "a history bounded by strokes only": ("mapmaker_edit.js", "undo.length > MAX_STROKES || records > MAX_RECORDS", "undo.length > MAX_STROKES"),
            "an undo that puts the new words back": ("mapmaker_edit.js", "var words = forward ? patch.after : patch.before", "var words = forward ? patch.before : patch.after"),
            "a new stroke that keeps the redo line": ("mapmaker_edit.js", "                redo = [];\n                patch.id = ++serial;", "                patch.id = ++serial;"),
            "a fill that leaks through a wall to the east": ("mapmaker_edit.js", "if (c < f.columns - 1 && !mark[i + 1] && tile(f, layer, i + 1) === want)", "if (c < f.columns - 1 && !mark[i + 1])"),
            "a brush that is not cut by the map's edge": ("mapmaker_edit.js", "return { r0: Math.max(0, row - lo), r1: Math.min(file.rows - 1, row + hi), c0: Math.max(0, column - lo), c1: Math.min(file.columns - 1, column + hi) };", "return { r0: row - lo, r1: row + hi, c0: column - lo, c1: column + hi };"),
            "a line that has gaps": ("mapmaker_edit.js", "            out.push([r, c]);\n        }\n        return out;", "            if ((r + c) % 2) out.push([r, c]);\n        }\n        return out;"),
            "a stroke that forgets the first words of a cell it touches twice": ("mapmaker_edit.js", "if (seen[key]) return;\n                seen[key] = 1;", "seen[key] = 1;"),
            "a dictionary that grows without telling its length": ("mapmaker_edit.js", "wr(out, file.dictAt, id);", "void 0;"),
            "a name written over a name that is there": ("mapmaker_edit.js", "if (b[slot] === 0x2E && b[slot + 1] === 0 && name)", "if (name)"),
            "a patch that holds a copy of the map": ("mapmaker_edit.js", "var patch = { keys: Uint32Array.from(k), before: Uint16Array.from(b), after: Uint16Array.from(a) };", "var patch = { keys: Uint32Array.from(k), before: Uint16Array.from(b.concat(new Array(1000).fill(0))), after: Uint16Array.from(a) };"),
            "a name of the palette that is not the game's": ("mapmaker_palette.js", "[302, 'smlrock1', 'Rock 1']", "[302, 'smlrock0', 'Rock 1']"),
            "a power-up of the wrong tile": ("mapmaker_palette.js", "[64, 'pu_bomb', 'Bomber ant power-up']", "[65, 'pu_bomb', 'Bomber ant power-up']"),
            "Hills that cannot be chosen": ("mapmaker_palette.js", "{ id: 'hills', label: 'Hills', key: 'h', places: true, palette: true }", "{ id: 'hills', label: 'Hills', key: 'h', palette: true }"),
            "a hill that is drawn with a brush": ("mapmaker_palette.js", "{ id: 'hills', label: 'Hills', key: 'h', places: true, palette: true }", "{ id: 'hills', label: 'Hills', key: 'h', draws: true, palette: true }"),
            "Food that is not on the letter F": ("mapmaker_palette.js", "{ id: 'food', label: 'Food', key: 'f', places: true, palette: true }", "{ id: 'food', label: 'Food', key: 'v', places: true, palette: true }"),
            "a team with another color than its hill": ("mapmaker_palette.js", "color: Tiles.TEAMS[t.key]", "color: Tiles.TEAMS.red"),
            "a hill whose entrance is not flagged": ("mapmaker_blocks.js", "r === row && c === column ? 1 : 0, word);", "0, word);"),
            "a hill in a version 9 map that names its anchor by a number that only version 8 means": ("mapmaker_blocks.js", "var word = file.version === 8 ?", "var word = file.version === 0 ?"),
            "a hill of 3 columns": ("mapmaker_blocks.js", "for (r = row - 1; r <= row + 2; r++) for (c = column - 1; c <= column + 2; c++) {\n            var word", "for (r = row - 1; r <= row + 2; r++) for (c = column - 1; c <= column + 1; c++) {\n            var word"),
            "a hill that does not leave the place it was in": ("mapmaker_blocks.js", "for (i = 0; i < old.length; i++) putCell(change, old[i], EMPTY2, 0, 0);", "void 0;"),
            "a hill that takes half of the hill it is put over": ("mapmaker_blocks.js", "if (Edit.tile(file, 2, idx) !== EMPTY2) takeObject(file, change, idx);", "if (Edit.tile(file, 2, idx) !== EMPTY2) putCell(change, idx, EMPTY2, 0, 0);"),
            "a hill that is put where half of it is off the map": ("mapmaker_blocks.js", "if (row < 1 || column < 1 || row + 2 >= file.rows", "if (row < 0 || column < 0 || row + 2 >= file.rows"),
            "a hill whose tile the dictionary is not told of": ("mapmaker_blocks.js", "addName(change, t.hill, t.hillName);", "void 0;"),
            "ants that all stand on one cell": ("mapmaker_blocks.js", "return good.concat(rest).slice(0, count);", "return good.concat(rest).slice(0, count).map(function (c) { return good[0] || c; });"),
            "ants of a team that take the plants and the other teams' ants with them": ("mapmaker_blocks.js", "if (teamOfMarker(lay.records[i].tile) === teamIndex) { if (insertAt < 0)", "if (true) { if (insertAt < 0)"),
            "a number of ants that is not kept in its range": ("mapmaker_blocks.js", "var n = clampTo(count, MIN_ANTS, MAX_ANTS), cells", "var n = count, cells"),
            "ants put on objects and on solid ground": ("mapmaker_blocks.js", "if (Edit.tile(file, 2, idx) === EMPTY2 && (Edit.flags(file, 1, idx) & 1) === 0) good.push(item); else rest.push(item);", "good.push(item);"),
            "ants put on the ants of another team": ("mapmaker_blocks.js", "if (taken[idx]) continue;", "void 0;"),
            "a splice that holds more than what changed": ("mapmaker_blocks.js", "while (head < old.length && head < next.length && old[head] === next[head]) head++;", "void 0;"),
            "a pile whose stages are one short": ("mapmaker_blocks.js", "4: { units: 30, points: 25, stages: [[30, 264], [20, 265], [10, 266], [0, EMPTY2]] },", "4: { units: 30, points: 25, stages: [[30, 264], [20, 265], [0, EMPTY2]] },"),
            "a food tile with another name than the game's": ("mapmaker_blocks.js", "280: 'fdfrl1'", "280: 'fdfrl2'"),
            "a pile that is put on a pile and leaves both": ("mapmaker_blocks.js", "if (p.row === row && p.column === column) takePileCells(file, change, p); else list.push", "if (p.row === row && p.column === column) takePileCells(file, change, p); list.push"),
            "a pile taken away that leaves its cell of layer 2": ("mapmaker_blocks.js", "if (tile !== EMPTY2 && stageTiles(pile).indexOf(tile) >= 0) takeObject(file, change, idx);", "void 0;"),
            "an eraser that does not know a pile by its body": ("mapmaker_blocks.js", "(anchor >= 0 && ix.piles[anchor] || []).forEach(function (k) { hits[k] = true; any = true; });", "void 0;"),
            "a food list with a pile that has no stages that is changed": ("mapmaker_blocks.js", "if (stages === 0 || at + 10 + 4 * stages > b.length) return out;", "if (at + 10 + 4 * stages > b.length) return out;"),
            "an eraser that asks what else goes after it took the object away": ("mapmaker_edit.js", "var more = hooks.erase ? hooks.erase(f, idx) : null, n = tile(f, 2, idx) === EMPTY2 ? 0 : removeObject(idx);", "var n = tile(f, 2, idx) === EMPTY2 ? 0 : removeObject(idx), more = hooks.erase ? hooks.erase(f, idx) : null;"),
            "an undo that puts the splices back in the order they were made": ("mapmaker_edit.js", "var s = list[forward ? i : list.length - 1 - i],", "var s = list[i],"),
            "a stroke that goes on with the bytes from before a splice": ("mapmaker_edit.js", "f = self.file = hooks.rebase(spliceAt(f.bytes, spliceStart(f, s), s.removed.length, s.inserted));", "self.file = hooks.rebase(spliceAt(f.bytes, spliceStart(f, s), s.removed.length, s.inserted));"),
            "a patch that forgets its splices": ("mapmaker_edit.js", "if (splices.length) patch.splices = splices.slice();", "void 0;"),
            "a step with only a splice in it that is not kept": ("mapmaker_edit.js", "if (patch.keys.length || patch.splices) history.push(patch);", "if (patch.keys.length) history.push(patch);"),
            "a history that weighs a splice as nothing": ("mapmaker_edit.js", "n += 1 + ((patch.splices[i].removed.length + patch.splices[i].inserted.length + 15) >> 4);", "n += 0;"),
            "a splice that is put at a place counted from the start of the file": ("mapmaker_edit.js", "at = spliceStart(self.file, s),", "at = s.rel,"),
            "a hill that takes another team's hill away without asking": ("mapmaker_blocks.js", "if (other >= 0 && other !== teamIndex) return {", "if (false) return {"),
            "ants whose start marker the dictionary is not told of": ("mapmaker_blocks.js", "addName(change, t.marker, t.markerName);", "void 0;"),
            "a pile whose stages are not kept in their range": ("mapmaker_blocks.js", "var set = FOOD[clampTo(stages, MIN_STAGES, MAX_STAGES)]", "var set = FOOD[stages]"),
            "a step of splices only that says it changed nothing": ("mapmaker_edit.js", "return patch.keys.length + (patch.splices ? patch.splices.length : 0);", "return patch.keys.length;"),
            "a stroke taken back that leaves the lists as they were made": ("mapmaker_edit.js", "return applyFull(patch, false);", "return applyPatch(f, patch, false);"),
            "an undo that forces a splice on bytes that are not where it says": ("mapmaker_edit.js", "if (!sameBytes(self.file.bytes, at, out)) continue;", "void 0;"),
            "arrow keys in the team swatches that choose tiles": ("mapmaker_tools.js", "if (hasTeams()) chooseTeam(id); else chooseTile(id);", "chooseTile(id);"),
            "ants that do not all fit and say nothing": ("mapmaker_tools.js", "if (put < state.ants) fits = ' (only ' + put + ' fit)';", "void 0;"),
            "a tap tool that begins a stroke": ("mapmaker_tools.js", "tapOnly: function (cell) { return !!editor && (mine() ? mine().tapOnly(cell) : places()); },", "tapOnly: function (cell) { return false; },"),
            "a hill whose square is one cell": ("mapmaker_tools.js", "(state.tool === 'hills' ? Blocks.HILL : 1)", "1"),
            "a hill without its door marked": ("mapmaker_tools.js", "door: function () { return !!editor && (state.tool === 'hills' || state.tool === 'bigger'); },", "door: function () { return false; },"),
            "a brush that takes a hill away": ("mapmaker_edit.js", "if (kind === 'objects' && hooks.guard) {", "if (false) {"),
            "a brush that is refused only on the cells that are in the way": ("mapmaker_edit.js", "if (why) { refused = why; return 0; } }\n                    }", "if (why) { refused = why; } }\n                    }"),
            "a refusal that is said to nobody": ("mapmaker_tools.js", "told = true;\n                    if (env.say) env.say('It cannot be put there', why);", "told = true;"),
            "an editor that is not given the guard": ("mapmaker_tools.js", "erase: Blocks.eraseAt, guard: Blocks.blockedText }", "erase: Blocks.eraseAt }"),
            "a hill refused in the words of nobody": ("mapmaker_blocks.js", "TEAMS[teamOfHill(Edit.tile(file, 2, idx))].label + '\\u2019s hill is here. Move it first, or take it away with Erase.'", "'Something is here.'"),
            "a bigger object put by the path of a food pile": ("mapmaker_tools.js", "else if (state.tool === 'bigger') change = Objects.place(file, bigNow(), cell.row, cell.column);", "void 0;"),
            "a sheet of bigger objects that marks no one as chosen": ("mapmaker_tools.js", "chosen = tool === 'bigger' ? state.big : tileOf(), have = itemOf(tool, chosen);\n                        grid.setAttribute('role', 'radiogroup'); grid.setAttribute('aria-label', tool === 'bigger' ? label : label + ' to paint with');", "chosen = tileOf(), have = itemOf(tool, chosen);\n                        grid.setAttribute('role', 'radiogroup'); grid.setAttribute('aria-label', tool === 'bigger' ? label : label + ' to paint with');"),
            "a bigger object picked that is not kept": ("mapmaker_tools.js", "else if (state.tool === 'bigger') state.big = id; else state.objects = id;", "else state.objects = id;"),
            "a bigger object whose box the canvas is not told": ("mapmaker_tools.js", "shape: function () { return editor && state.tool === 'bigger' ? bigNow().box : null; },", "shape: function () { return null; },"),
            "a palette of bigger objects that shows swatches beside the map": ("mapmaker_tools.js", "host.swatches.hidden = tool === 'food' || tool === 'bigger';", "host.swatches.hidden = tool === 'food';"),
            "a button that does not say how many cells": ("mapmaker_tools.js", "plural(item.cells.length, 'cell', 'cells') + ', ' + (b.c1 - b.c0 + 1)", "'' + (b.c1 - b.c0 + 1)"),
            "a chip whose squares are all filled": ("mapmaker_tools.js", "if (item.cells.some(function (rc) { return rc[0] === r && rc[1] === c; })) cell.style.background = item.color;", "cell.style.background = item.color;"),
            "a chip of a bigger object that is only a color": ("mapmaker_tools.js", "if (tool === 'bigger') { if (!picture(chip, true, item.id, 30)) footprint(chip, item); } else {", "if (tool === 'bigger') { chip.style.background = item.color; } else {"),
            "a sheet that does not say the whole name to a screen reader": ("mapmaker_tools.js", "b.setAttribute('aria-label', bigText(item)); } else who.textContent = item.label;", "} else who.textContent = item.label;"),
            "a rock put without its solid bit": ("mapmaker_edit.js", "if (Solid.sets(tileId)) setSolid(idx, true);", "void 0;"),
            "a change whose solid bits are not applied": ("mapmaker_edit.js", "for (i = 0; ch.solid && i < ch.solid.length; i++) setSolid(ch.solid[i][0], ch.solid[i][1]);", "void 0;"),
            "a pile of one stage": ("mapmaker_tools.js", "[Blocks.MIN_STAGES, Blocks.MAX_STAGES]", "[1, Blocks.MAX_STAGES]"),
            "a team that cannot be chosen": ("mapmaker_tools.js", "function chooseTeam(id) { state.team = id; fits = ''; showPalette(); }", "function chooseTeam(id) { fits = ''; showPalette(); }"),
            "a place with no room that is refused in silence": ("mapmaker_tools.js", "if (change.error) { if (env.say) env.say(", "if (change.error) { if (false) env.say("),
            "a thing put that the canvas is not told of": ("mapmaker_tools.js", "if (dirty) changed(dirty);\n            else {", "if (false) changed(dirty);\n            else {"),
            "a change that only gave the dictionary a name that the page is not told of": ("mapmaker_tools.js", "if (env.file() !== was) env.onEdit();", "void 0;"),
            "an eraser that does not take ants and piles": ("mapmaker_tools.js", "Edit.create(file, { rebase: env.rebase, erase: Blocks.eraseAt, guard: Blocks.blockedText })", "Edit.create(file, { rebase: env.rebase })"),
            "a stepper that goes past the most ants": ("mapmaker_tools.js", "return state.tool === 'ants' ? [Blocks.MIN_ANTS, Blocks.MAX_ANTS]", "return state.tool === 'ants' ? [Blocks.MIN_ANTS, 99]"),
            "a sheet whose stepper does not change the page's number": ("mapmaker_tools.js", "more.addEventListener('click', function () { stepBy(1); show(); onChange(); });", "more.addEventListener('click', function () { show(); onChange(); });"),
            "a line under the palette that does not say what the map has": ("mapmaker_tools.js", "if (state.tool === 'ants') return who + ': ' + (facts.ants[state.team]", "if (state.tool === 'ants') return who + ': ' + (false"),
            "a tap tool that is turned into a stroke by the canvas": ("mapmaker_canvas.js", "var tapTool = !panning && !!cell && tapOnly(cell);", "var tapTool = false;"),
            "a tap that does not put": ("mapmaker_canvas.js", "if (placing && tap) painter.begin(tap);", "void 0;"),
            "undo that the keys do not reach": ("mapmaker_tools.js", "if (k === 'z') {", "if (k === 'q') {"),
            "a letter that is a tool while typing in a box": ("mapmaker_tools.js", "|| typing(e.target)) return;", ") return;"),
            "a letter that is a tool while a dialog is open": ("mapmaker_tools.js", "if (e.defaultPrevented || !editor || env.busy() ||", "if (e.defaultPrevented || !editor ||"),
            "a fill for the eraser": ("mapmaker_tools.js", "host.fill.disabled = !on || state.tool === 'erase';", "host.fill.disabled = !on;"),
            "an undo in the middle of a stroke": ("mapmaker_tools.js", "function undo() { if (editor && !canvas.isStroking())", "function undo() { if (editor)"),
            "a stroke that does not tell the canvas what changed": ("mapmaker_tools.js", "                current.segment(from.row, from.column, to.row, to.column, state.brush);\n                canvas.markDirty(current.takeDirty());", "                current.segment(from.row, from.column, to.row, to.column, state.brush);"),
            "a tile that is not named in the dictionary before it is used": ("mapmaker_tools.js", "if (made.grew) editor.rebase(env.rebase(made.bytes));", "void 0;"),
            "a history that forgets the strokes it dropped when it says a map is clean": ("mapmaker_edit.js", "isClean: function () { return undo.length === 0 && dropped === 0; },", "isClean: function () { return undo.length === 0; },"),
            "a saved marker that forgets the strokes that were dropped": ("mapmaker_edit.js", "return lastId() === savedId && dropped === savedDropped;", "return lastId() === savedId;"),
            "an anchor that does not wrap in a small version 9 map": ("mapmaker_edit.js", "if (file.rows <= 256 && file.columns <= 256) { r &= 255; c &= 255; }", "void 0;"),
            "an object that is skipped when only its tile is the same": ("mapmaker_edit.js", "if (tile(f, 2, idx) === tileId && flags(f, 2, idx) === 1 && third(f, 2, idx) === ownAnchor(f, idx)) return 0;", "if (tile(f, 2, idx) === tileId) return 0;"),
            "a patch that keeps cells that came back to what they were": ("mapmaker_edit.js", "if (t === before[3 * i] && fl === before[3 * i + 1] && th === before[3 * i + 2]) continue;", "void 0;"),
            "a name written in the dictionary that the page is not told of": ("mapmaker_tools.js", "if (made.named || made.grew) env.onEdit();", "void 0;"),
            "an undo that does not tell the canvas": ("mapmaker_tools.js", "            canvas.markDirty(dirty);\n            canvas.refreshMini();", "            canvas.refreshMini();"),
            "a Fill that stays on": ("mapmaker_tools.js", "state.fill = false;                                    // (Fill is for one click: the next press paints again)", "void 0;"),
            "a tile the dictionary cannot hold that is refused in silence": ("mapmaker_tools.js", "if (made.error) { if (env.say) env.say(", "if (made.error) { if (false) env.say("),
            "a palette button that does not open the sheet": ("mapmaker_tools.js", "host.tile.addEventListener('click', openPicker);", "void 0;"),
            "a sheet whose tile is not chosen": ("mapmaker_tools.js", "chooseTile(item.id); ctl.close(); });\n                            grid.appendChild(b);", "ctl.close(); });\n                            grid.appendChild(b);"),
            "a stroke that goes on after the mouse button was let go outside": ("mapmaker_canvas.js", "if (e.pointerType === 'mouse' && e.buttons === 0) return endStroke();", "void 0;"),
            "a stroke that goes on after the pointer was lost": ("mapmaker_canvas.js", "canvas.addEventListener('lostpointercapture', function (e) { if (stroke && stroke.id === e.pointerId) endStroke(true); });", "void 0;"),
            "a stroke that goes on after the window lost the focus": ("mapmaker_canvas.js", "win.addEventListener('blur', function () { if (stroke) endStroke(true); });", "void 0;"),
            "a stroke that goes on after the page was hidden": ("mapmaker_canvas.js", "doc.addEventListener('visibilitychange', function () { if (stroke && doc.visibilityState === 'hidden') endStroke(true); });", "void 0;"),
            "a finger that paints before it is known not to be the first of two": ("mapmaker_canvas.js", "if (mouse) startStroke(e.pointerId, cell); else pending = { id: e.pointerId, cell: cell };", "startStroke(e.pointerId, cell);"),
            "a brush that does not reach the painter": ("mapmaker_tools.js", "(paints() ? state.brush : (places()", "(paints() ? 1 : (places()"),
            "a splice of the header counted from the end of layer 2": ("mapmaker_edit.js", "return s.abs ? s.at : file.blocksAt + s.rel;", "return file.blocksAt + (s.abs ? s.at : s.rel);"),
            "a change of the properties that is not one step": ("mapmaker_tools.js", "var dirty = editor.act(change); if (dirty) changed(dirty); return !!dirty; }", "var dirty = editor.act(change); if (dirty) changed(dirty); if (dirty) editor.act(change); return !!dirty; }"),
            "a tool whose keys are the map's cursor keys first": ("mapmaker_canvas.js", "if (painter && painter.key && painter.key(e)) { e.preventDefault(); hover = null; redraw(); return; }", "void 0;"),
            "a lost pointer that is dropped, not abandoned": ("mapmaker_canvas.js", "if (stroke && stroke.id === e.pointerId) endStroke(true); });", "if (stroke && stroke.id === e.pointerId) endStroke(); });"),
            "a window that loses the focus drops the thing": ("mapmaker_canvas.js", "win.addEventListener('blur', function () { if (stroke) endStroke(true); });", "win.addEventListener('blur', function () { if (stroke) endStroke(); });"),
            "a page that is hidden drops the thing": ("mapmaker_canvas.js", "doc.visibilityState === 'hidden') endStroke(true); });", "doc.visibilityState === 'hidden') endStroke(); });"),
            "a lost pointer that the tool is never told of": ("mapmaker_canvas.js", "if (abandon && painter.abandon) painter.abandon(); else painter.end(); }", "painter.end(); }"),
            "a lift that is told to the tool while the stroke goes on": ("mapmaker_canvas.js", "setStroke(null);          // (the stroke is over when the tool is told: Pick up writes its change then, and the page does not take a change while a stroke goes on)\n                if (cancelled) painter.cancel(); else painter.end();", "if (cancelled) painter.cancel(); else painter.end();\n                setStroke(null);"),
            "a move that is not told where the pointer is": ("mapmaker_canvas.js", "if (painter.away) painter.away(whereOf(e));\n                var inside", "var inside"),
            "a lift that is not told where it is": ("mapmaker_canvas.js", "if (painter.away && !cancelled) painter.away(whereOf(e));\n", ""),
            "a pointer off the canvas that is not off the map": ("mapmaker_canvas.js", "outside: p.x < 0 || p.y < 0 || p.x > w || p.y > h || !cellOf(p),", "outside: !cellOf(p),"),
            "a pointer past the map's edge that is not off the map": ("mapmaker_canvas.js", "outside: p.x < 0 || p.y < 0 || p.x > w || p.y > h || !cellOf(p),", "outside: p.x < 0 || p.y < 0 || p.x > w || p.y > h,"),
            "a cancelled pointer that is told as a lift": ("mapmaker_canvas.js", "if (painter.away && !cancelled) painter.away(whereOf(e));", "if (painter.away) painter.away(whereOf(e));"),
            "a tool that is not told about the mouse over the map": ("mapmaker_canvas.js", "if (painter && painter.hover) painter.hover(over);          // (a tool that shows what a click would do: Pick up)", "void 0;"),
            "a tool that is not told that the mouse left the map": ("mapmaker_canvas.js", "hover = null; if (painter && painter.hover) painter.hover(null); if (file) { chrome();", "hover = null; if (file) { chrome();"),
            "a tool that is not told which cell is pressed": ("mapmaker_canvas.js", "var tapTool = !panning && !!cell && tapOnly(cell);", "var tapTool = !panning && !!cell && tapOnly();"),
            "a change of the properties that is put in while a stroke goes on": ("mapmaker_tools.js", "function act(change) { if (!editor || canvas.isStroking()) return false;", "function act(change) { if (!editor) return false;"),
        }
        for what, (name, old, new) in mutants.items():
            with self.subTest(what):
                text = read("web", "front", name)
                self.assertEqual(text.count(old), 1, "the text that this mutant changes is not in %s exactly once: %s" % (name, old))
                with tempfile.TemporaryDirectory() as tmp:
                    for script in ALL_SCRIPTS:
                        shutil.copy(os.path.join(FRONT, script), os.path.join(tmp, script))
                    with open(os.path.join(tmp, name), "w", encoding="utf-8") as f:
                        f.write(text.replace(old, new))
                    done = run_check(tmp, EDIT_CHECK)
                self.assertNotEqual(done.returncode, 0, "the check did not notice: " + what)

    def test_the_bigger_objects_pass_their_checks_on_the_library_and_on_made_up_maps(self):
        done = run_check(script=OBJECTS_CHECK)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("0 failed", done.stdout)
        for part in ("A solid tiles are the game's", "B the list is what the library says", "C put and taken away on the library", "D what is in the way", "E edges, a wide map, the dictionary", "F small objects are solid too"):
            self.assertIn("RUNNING: %s ... PASS" % part, done.stdout)

    def test_the_bigger_objects_check_can_fail_on_broken_copies_of_the_scripts(self):
        mutants = {
            "a footprint that is one cell short": ("mapmaker_palette.js", "[1324, 'grassmed1', '-1,-2 -1,-1 -1,0 -1,1 -1,2 0,-2 0,-1 0,0 0,1 0,2']", "[1324, 'grassmed1', '-1,-2 -1,-1 -1,0 -1,1 -1,2 0,-2 0,-1 0,0 0,1']"),
            "a name that is not the game's": ("mapmaker_palette.js", "[1323, 'grass4',", "[1323, 'grass5',"),
            "a tile that is not the one that the library uses": ("mapmaker_palette.js", "[354, 'yelcray',", "[355, 'yelcray',"),
            "a footprint without its anchor cell": ("mapmaker_palette.js", "[315, 'pin3', '0,0 1,0']", "[315, 'pin3', '1,0 2,0']"),
            "a list that is not the most used first": ("mapmaker_palette.js", "        [1323, 'grass4', '-1,-1 -1,0 0,-1 0,0'],\n        [1324,", "        [1324, 'grassmed1', '-1,-2 -1,-1 -1,0 -1,1 -1,2 0,-2 0,-1 0,0 0,1 0,2'],\n        [1323, 'grass4', '-1,-1 -1,0 0,-1 0,0'],\n        [1324x,"),
            "a table of solid tiles with one wrong digit": ("mapmaker_solid.js", "'000000000000000c7", "'000000000000000c6"),
            "a hill that owns solid bits": ("mapmaker_solid.js", "return is(id) && !(id >= 245 && id <= 248);", "return is(id);"),
            "an anchor in a version 9 map that only version 8 means": ("mapmaker_objects.js", "file.version === 8 ? ((row & 255) | ((column & 255) << 8))", "file.version === 0 ? ((row & 255) | ((column & 255) << 8))"),
            "an origin that is not flagged": ("mapmaker_objects.js", "put(cell.idx, item.id, origin ? 1 : 0,", "put(cell.idx, item.id, 0,"),
            "every cell flagged as the origin": ("mapmaker_objects.js", "put(cell.idx, item.id, origin ? 1 : 0,", "put(cell.idx, item.id, 1,"),
            "an object under the new one that goes in halves": ("mapmaker_objects.js", "var gone = Edit.objectCells(file, idx);", "var gone = [idx];"),
            "an object that takes a hill away without asking": ("mapmaker_blocks.js", "if (teamOfHill(t) >= 0) return 'hill';", "void 0;"),
            "an object that is put over starting ants": ("mapmaker_blocks.js", "if (lay.ok1 && ix.markers[idx]) return 'ants';", "void 0;"),
            "an object that is put over the body of a food pile": ("mapmaker_blocks.js", "if (ix.piles[idx] || (anchor >= 0 && ix.piles[anchor])) return 'pile';", "if (ix.piles[idx]) return 'pile';"),
            "a tile that the dictionary is not told of": ("mapmaker_objects.js", "names: [[item.id, item.name]],", "names: [],"),
            "an object without its solid bits": ("mapmaker_objects.js", "if (Solid.sets(item.id)) change.solid.push([cell.idx, true]);", "void 0;"),
            "an object that is not cut by the edge of the map": ("mapmaker_objects.js", "if (r >= 0 && c >= 0 && r < file.rows && c < file.columns) out.push(", "out.push("),
            "an eraser that leaves the solid bit of a tile that is not in the table": ("mapmaker_edit.js", "if (Solid.clears(gone)) setSolid(cells[i], false);", "if (Solid.sets(gone)) setSolid(cells[i], false);"),
            "a pile taken away that leaves the solid bits of its cells": ("mapmaker_edit.js", "if (c[1] === EMPTY2 && Solid.clears(was)) setSolid(c[0], false);", "void 0;"),
            "a solid bit cleared under a hill": ("mapmaker_solid.js", "function clears(id) { return id !== 0x7FFE && !(id >= 245 && id <= 248); }", "function clears(id) { return id !== 0x7FFE; }"),
            "the same object on the same place that is a new step": ("mapmaker_objects.js", "w[2]) continue;      // (already so)", "w[2] && false) continue;      // (already so)"),
            "a box that the canvas does not take from the object": ("mapmaker_view.js", "if (shape) return {", "if (false) return {"),
            "an eraser that takes one cell of a bigger object": ("mapmaker_edit.js", "var cells = objectCells(f, idx), n = 0;", "var cells = [idx], n = 0;"),
            "a rock whose solid bit is not set by the Objects tool": ("mapmaker_edit.js", "if (Solid.sets(tileId)) setSolid(idx, true);", "void 0;"),
            "a solid bit that row 0 loses": ("mapmaker_edit.js", "if (!on && idx < f.columns) return;", "void 0;"),
            "a solid bit that takes the other flags with it": ("mapmaker_edit.js", "on ? (was | 1) : (was & ~1)", "on ? 1 : 0"),
        }
        for what, (name, old, new) in mutants.items():
            with self.subTest(what):
                text = read("web", "front", name)
                self.assertEqual(text.count(old), 1, "the text that this mutant changes is not in %s exactly once: %s" % (name, old))
                with tempfile.TemporaryDirectory() as tmp:
                    for script in ALL_SCRIPTS:
                        shutil.copy(os.path.join(FRONT, script), os.path.join(tmp, script))
                    with open(os.path.join(tmp, name), "w", encoding="utf-8") as f:
                        f.write(text.replace(old, new))
                    done = run_check(tmp, OBJECTS_CHECK)
                self.assertNotEqual(done.returncode, 0, "the check did not notice: " + what)

    def test_the_objects_that_stand_on_several_cells_are_counted_as_a_second_reading_does(self):
        done = subprocess.run([NODE, OBJECTS_CHECK, "--list-objects"], capture_output=True, text=True, timeout=300)
        self.assertEqual(done.returncode, 0, done.stderr)
        seen = {}
        for line in done.stdout.splitlines():
            tile, maps, objects, cells, share = (int(v) for v in line.split("\t"))
            seen[tile] = (maps, objects, cells, share)
        wanted = {}
        for path in library():
            for tile, (objects, shapes) in read_objects(path).items():
                total = wanted.setdefault(tile, [0, 0, {}])
                total[0] += 1
                total[1] += objects
                for shape, count in shapes.items():
                    total[2][shape] = total[2].get(shape, 0) + count
        expect = {}
        for tile, (maps, objects, shapes) in wanted.items():
            best = max(shapes.items(), key=lambda kv: (kv[1], len(kv[0])))
            expect[tile] = (maps, objects, len(best[0]), best[1])
        self.assertEqual(seen, expect)

    def test_the_lists_of_the_library_pass_their_checks(self):
        done = run_check(script=BLOCKS_CHECK)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("0 failed", done.stdout)
        for part in ("A the lists of the library are written back as they are", "B changes and undo, byte for byte", "C a hill taken away and put back", "D only what is named changes"):
            self.assertIn("RUNNING: %s ... PASS" % part, done.stdout)

    def test_the_lists_check_can_fail_on_broken_copies_of_the_scripts(self):
        mutants = {
            "a pile that is one byte too long": ("mapmaker_blocks.js", "pile = { rel: at - base, length: 10 + 4 * stages,", "pile = { rel: at - base, length: 11 + 4 * stages,"),
            "a record of block 1 that is read from the wrong place": ("mapmaker_blocks.js", "column: rd(b, at + 6 + 6 * i) });", "column: rd(b, at + 4 + 6 * i) });"),
            "a list that is written with the wrong count": ("mapmaker_blocks.js", "wr(out, 0, records.length);", "wr(out, 0, records.length + 1);"),
            "a splice that is not the exact piece": ("mapmaker_blocks.js", "return { rel: rel + head, removed: old.slice(head, old.length - tail), inserted: next.slice(head, next.length - tail) };", "return { rel: rel + head, removed: old.slice(head, old.length - tail + 1), inserted: next.slice(head, next.length - tail) };"),
            "a hill put back that is anchored somewhere else": ("mapmaker_blocks.js", "var word = file.version === 8 ? ((row & 255) | ((column & 255) << 8))", "var word = file.version === 8 ? ((column & 255) | ((row & 255) << 8))"),
            "a team's ants put that touch the pile list": ("mapmaker_blocks.js", "var records = kept.slice(0, insertAt).concat(fresh, kept.slice(insertAt)), change = emptyChange(), splice = diffSplice(file, lay.b1.rel, lay.b1.length, block1Bytes(records));", "var records = kept.slice(0, insertAt).concat(fresh, kept.slice(insertAt)), change = emptyChange(), splice = diffSplice(file, lay.b1.rel, lay.b1.length + 2, block1Bytes(records));"),
            "an undo that gives back every byte but the lists": ("mapmaker_edit.js", "self.file = hooks.rebase(spliceAt(self.file.bytes, at, out.length, into));", "void 0;"),
        }
        for what, (name, old, new) in mutants.items():
            with self.subTest(what):
                text = read("web", "front", name)
                self.assertEqual(text.count(old), 1, "the text that this mutant changes is not in %s exactly once: %s" % (name, old))
                with tempfile.TemporaryDirectory() as tmp:
                    for script in ALL_SCRIPTS:
                        shutil.copy(os.path.join(FRONT, script), os.path.join(tmp, script))
                    with open(os.path.join(tmp, name), "w", encoding="utf-8") as f:
                        f.write(text.replace(old, new))
                    done = run_check(tmp, BLOCKS_CHECK)
                self.assertNotEqual(done.returncode, 0, "the check did not notice: " + what)

    def test_the_lists_of_every_map_are_read_as_a_second_reading_does(self):
        done = subprocess.run([NODE, BLOCKS_CHECK, "--list-blocks"], capture_output=True, text=True, timeout=300)
        self.assertEqual(done.returncode, 0, done.stderr)
        seen = {}
        for line in done.stdout.splitlines():
            name, ok1, ok2, records, marks, piles, listing = line.split("\t")
            seen[name] = (int(ok1), int(ok2), int(records), tuple(int(v) for v in marks.split(",")), int(piles), listing)
        files = library()
        self.assertEqual(sorted(seen), sorted(files))
        for path in files:
            self.assertEqual(seen[path], read_blocks(path), path)

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
            "a meter that counts an opened Classic map against the limit of a new one": ("mapmaker_sizes.js", "(cells <= limits.classic.cells ? limits.classic.cells : limits.classic.openCells)", "limits.classic.cells"),
            "a download named like a Windows device": ("mapmaker_file.js", "clean = '_' + clean;", "void 0;"),
            "a meter that cannot show the Extended limit": ("mapmaker_sizes.js", "side: Math.floor(Math.sqrt(e.cells))", "side: 512"),
        }
        for what, (name, old, new) in mutants.items():
            with self.subTest(what):
                text = read("web", "front", name)
                self.assertEqual(text.count(old), 1, "the text that this mutant changes is not in %s exactly once: %s" % (name, old))
                with tempfile.TemporaryDirectory() as tmp:
                    for script in ALL_SCRIPTS:
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


@unittest.skipUnless(NODE, "node is not installed")
class TheTimeAndTheBail(unittest.TestCase):
    """what the checks use so that a busy runner changes no verdict and no time: the preload that ends a run of a broken copy at its first failed line, the CPU clock and the turns of the loop"""

    def node(self, source, *flags):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "probe.js")
            with open(path, "w", encoding="utf-8") as f:
                f.write(source)
            return subprocess.run([NODE] + list(flags) + [path], capture_output=True, text=True, timeout=60)

    def test_a_run_ends_at_the_first_line_that_says_failed_and_a_run_without_one_goes_on(self):
        for line in ("    FAILED: a thing", "    ERROR: a thing", "    CRASHED: a thing", "\n    FAILED: threw an error"):
            source = "console.log('RUNNING: A ... PASS'); console.log(%s); console.log('LATER'); process.exitCode = 0;" % json.dumps(line)
            stopped = self.node(source, "--require", BAIL)
            self.assertEqual(stopped.returncode, 1, line + stopped.stdout + stopped.stderr)
            self.assertIn("a thing" if "threw" not in line else "threw an error", stopped.stdout, "the line that says it is printed first")
            self.assertNotIn("LATER", stopped.stdout, line)
            whole = self.node(source)
            self.assertEqual(whole.returncode, 0)
            self.assertIn("LATER", whole.stdout, "without the preload the run goes on")
        quiet = self.node("console.log('RUNNING: A ... PASS'); console.log('RUNNING: L ... SKIP: no core'); console.log('0 failed'); console.log('LATER');", "--require", BAIL)
        self.assertEqual(quiet.returncode, 0, quiet.stdout + quiet.stderr)
        self.assertIn("LATER", quiet.stdout, "a pass, a skip and the word failed in a sentence do not end a run")

    def test_the_clock_counts_the_work_and_not_the_wait_and_settle_comes_after_every_chain_of_turns(self):
        done = self.node("""
const C = require(%s);
const t0 = C.now(), w0 = Date.now();
Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 300);                 // (a wait: wall time and no work)
const waited = C.now() - t0, wall = Date.now() - w0;
const t1 = C.now(); let x = 0; for (let i = 0; i < 2e8 && C.now() - t1 < 60; i++) x += i;     // (work)
let hops = 0; const chain = () => { if (++hops < 900) setImmediate(chain); }; setImmediate(chain);
C.settle().then(() => console.log(JSON.stringify({ waited, wall, worked: C.now() - t1, hops })));
""" % json.dumps(os.path.join(REPO, "tests", "scripts", "web_mapmaker_clock.js")))
        self.assertEqual(done.returncode, 0, done.stderr)
        got = json.loads(done.stdout)
        self.assertGreaterEqual(got["wall"], 250)
        self.assertLess(got["waited"], 100, "a wait of 300 ms is no CPU time: " + done.stdout)
        self.assertGreaterEqual(got["worked"], 60, done.stdout)
        self.assertEqual(got["hops"], 900, "the chain of 900 turns is over when settle() is")


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
        wanted = ["lvl_core.js", "lvl_core_api.js"] + ["front/" + name for name in SCRIPTS]
        self.assertEqual(sorted(tags), sorted(wanted), "the page loads these scripts and no other")
        order = [tags.index(name) for name in wanted]
        self.assertEqual(order, sorted(order), "the core first, then the helpers before the dialogs, the canvas and the app (the app last)")
        self.assertEqual(tags[-1], "front/mapmaker_app.js")
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

    def test_the_crop_screen_is_in_the_page_with_every_id_its_code_asks_for_and_the_app_hands_it_what_it_needs(self):
        ids = re.findall(r'\bid="([^"]+)"', self.page)
        asked = set(re.findall(r"\$\('([^']+)'\)", self.code["mapmaker_crop.js"]))
        self.assertTrue({"crop-stage", "crop-frame", "crop-fill", "crop-note", "crop-use", "crop-cancel", "crop-name", "crop-tags", "crop-whole", "crop-shapes", "crop-nothing", "crop-chips", "crop-big", "crop-cells", "crop-range"} <= asked, sorted(asked))
        self.assertEqual(sorted(asked - set(ids)), [], "the crop screen asks for ids that the page does not have")
        self.assertRegex(self.page, r'<div class="screen crop" id="crop" hidden>')
        self.assertEqual(re.findall(r'<i class="h ([a-z]+)" data-h="([a-z]+)"></i>', self.page), [(h, h) for h in ("nw", "n", "ne", "w", "e", "sw", "s", "se")], "the eight handles of the frame")
        self.assertIn("busy: function () { return dialogs.isOpen() || !$('crop').hidden || !$('mine').hidden; }", self.app, "the tools' keys do nothing while the crop screen or My maps is open")
        self.assertIn("MapCrop.open({ doc: doc, $: $, host: $('crop'), editor: $('editor'), limits: limits.classic,", self.app, "the limits of the frame are the core's")
        self.assertIn("{ text: 'Copy...', onClick: ACTIONS.copy }", self.app, "the phone's Menu has Copy...")
        self.assertIn("copy: convertWith('copy'), save: convertWith('save'), convert: convertWith('convert')", self.app)
        self.assertIn("onMade: function (bytes, name) { leave(function () {", self.app, "the new map takes the place of the open one only after the question about unsaved drawing")
        self.assertEqual(re.findall(r'<button class="(?:link)" type="button" id="convert-link" data-act="convert" disabled></button>', self.page), ['<button class="link" type="button" id="convert-link" data-act="convert" disabled></button>'])

    def test_every_button_that_the_page_has_is_wired_and_starts_off_until_the_core_is_there(self):
        acts = re.findall(r'data-act="([a-z-]+)"', self.page)
        self.assertEqual(sorted(set(acts)), ["check", "convert", "copy", "download", "menu", "mine", "new", "open", "play", "props", "resize", "save", "submit", "zoom-in", "zoom-out"])
        for button in re.findall(r"<button[^>]*data-act[^>]*>", self.page):
            self.assertIn(" disabled", button, button)
        self.assertIn("'new': function () { leave(newMap); }, open: function () { leave(openMap); }, download: download, check: showChecks,", self.app)
        self.assertIn("menu: function () { dialogs.menu([{ text: 'New'", self.app)
        self.assertIn("{ text: 'Properties...', onClick: ACTIONS.props }", self.app, "the phone's Menu has Properties...")
        self.assertIn("props: openProps", self.app)
        self.assertEqual(re.findall(r'<button class="(?:btn sm|link)" type="button" data-act="props" disabled>([A-Za-z]+)</button>', self.page), ["Properties", "Edit", "Edit"], "the top bar's button, the summary's Edit link and the one under the map")
        self.assertIn('<input class="nm" id="map-name" type="text" maxlength="40"', self.page)
        self.assertRegex(self.page, r'<label class="sr" for="map-name">Name of the map</label>')
        self.assertRegex(self.page, r'<input type="file" id="file-in" accept="\.lvl,\.LVL" hidden>')

    def test_a_script_that_did_not_load_is_told_for_every_script_of_the_page(self):
        needed = re.search(r"var NEEDED = \[([^\]]*)\]", self.app).group(1)
        for name, text in self.code.items():
            if name == "mapmaker_app.js":
                continue
            for global_name in re.findall(r"root\.(Map[A-Za-z]+) = factory", text):
                self.assertIn("'%s'" % global_name, needed, "%s makes %s, which the app must list in NEEDED" % (name, global_name))

    def test_the_page_refuses_a_module_of_another_interface_version_before_it_asks_it_anything(self):
        self.assertIn("var found = LvlCore.moduleVersion(module);", self.app)
        self.assertIn("if (found !== LvlCore.EXPECTED_INTERFACE) return fatal(", self.app)
        self.assertLess(self.app.index("LvlCore.moduleVersion(module)"), self.app.index("start(module);"))
        self.assertLess(self.app.index("function start(module) {"), self.app.index("core = LvlCore.wrap(module);"))
        self.assertIn("Reload the page", self.app)
        self.assertIn("createLvlCore", self.app.split("function boot()")[1], "a module that did not load at all is told too")

    def test_a_map_is_read_as_a_place_that_takes_both_kinds_and_the_limits_come_from_the_core(self):
        self.assertEqual(self.app.count("core.checkMap("), 6, "the open map's three, the two of My maps (a saved map is read as a place that takes both kinds too) and the one of Submit for approval (the bytes that are sent are the bytes that were judged)")
        self.assertEqual(len(re.findall(r"core\.(?:checkMap|readReport)\([^)]*'extended'\)", self.app)), 7)
        self.assertIn("core.limits('new_map')", self.app)
        self.assertIn("core.limits('extended')", self.app)
        self.assertNotRegex(self.code["mapmaker_dialog.js"], r"\b(10000|10,000|262144|262,144|130|512)\b", "a limit is the core's, never a number of the dialog")
        self.assertNotRegex(self.code["mapmaker_sizes.js"], r"\b(10000|10,000|262144|262,144|130|512)\b")

    def test_save_as_a_new_map_is_in_the_top_bar_and_the_menu_and_the_page_keeps_work_in_the_browser_only_when_it_may(self):
        self.assertEqual(re.findall(r'data-act="(new|open|mine|copy|save|props|download)" disabled>([^<]+)</button>', self.page)[:7],
                         [("new", "New"), ("open", "Open"), ("mine", "My maps"), ("copy", "Copy..."), ("save", "Save as new..."), ("props", "Properties"), ("download", "Download")], "the top bar is in the order of the picture, with Properties as before and My maps after Open")
        app = self.app
        self.assertIn("{ text: 'Save as new...', onClick: ACTIONS.save }", app, "the phone's Menu has it")
        self.assertIn("save: convertWith('save')", app)
        self.assertIn("store.open(restoreWork);", app, "the work of the last visit is looked for once the database is open")
        self.assertIn("store.keepWork(map.name, map.file.kind, map.bytes,", app, "the open map's own bytes are kept (the one copy)")
        self.assertIn("if (tools.isSaved()) store.clearWork(", app, "a map that is saved or downloaded leaves nothing in the slot")
        self.assertIn("if (map && !tools.isSaved() && (store.status() !== 'ready' || keepPending))", app, "the browser's question only when the work is not safe in the browser")
        self.assertIn("else tools.markUnsaved();", app, "work picked up is still not saved")
        self.assertIn("store.add(name, map.file.kind, made.bytes,", app)
        self.assertEqual(len(re.findall(r"store\.(add|keepWork)\(", app)), 2, "the page writes to the store in two places only")
        self.assertIn("MapStore.create({ idb: (function () { try { return win.indexedDB || null; } catch (e) { return null; } })()", app, "a browser that refuses the database does not stop the page")
        self.assertIn("taken: function (name) { return MapStore.key(name) === MapStore.key(map.name) || store.has(name); },", app, "the open map, a draft and a library map are taken")
        self.assertIn("canvas.setFile(file);\n        tools.setMap(file);\n        keepSoon();", app, "a map that takes the screen leaves the slot as it should")
        self.assertIn("buttons: (can ? [{ id: 'save', text: 'Save as a new map first'", app, "the question before a map is left offers Save as a new map when the browser keeps maps")

    def test_my_maps_pass_their_checks_on_a_fake_dom(self):
        done = run_check(script=MINE_CHECK)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("0 failed", done.stdout)
        for part in ("A the store: list and delete", "B the list, the filters and the empty states", "C Open, Copy and a map that cannot be read", "D Delete and its question", "E Escape, closing and waiting", "F Copy of a saved map"):
            self.assertIn("RUNNING: %s ... PASS" % part, done.stdout)

    def test_the_my_maps_check_can_fail_on_the_broken_copies_of_the_list(self):
        """tests/data/mapmaker_mine_mutants.json (tools/mutate.py runs it too, with `--keep-going --jobs 1`): each broken copy of the list, the store's list and delete, or Copy's saved map must fail the check"""
        with open(os.path.join(REPO, "tests", "data", "mapmaker_mine_mutants.json"), encoding="utf-8") as f:
            spec = json.load(f)
        self.assertGreaterEqual(len(spec["mutants"]), 30)
        ids = [m["id"] for m in spec["mutants"]]
        self.assertEqual(len(ids), len(set(ids)), "an id twice")
        with tempfile.TemporaryDirectory() as tmp:
            for name in ALL_SCRIPTS:
                shutil.copy(os.path.join(FRONT, name), os.path.join(tmp, name))
            for m in spec["mutants"]:
                with self.subTest(m["id"]):
                    name = os.path.basename(m["file"])
                    text = read(*m["file"].split("/"))
                    self.assertEqual(text.count(m["old"]), 1, "%s: the text is not in %s exactly once" % (m["id"], name))
                    if "web_mapmaker_mine_check" not in m["test"]:
                        continue                # (a page-level mutant is judged by the pinned source of this file: tools/mutate.py runs it)
                    with open(os.path.join(tmp, name), "w", encoding="utf-8", newline="") as f:
                        f.write(text.replace(m["old"], m["new"]))
                    try:
                        done = run_check(tmp, MINE_CHECK)
                    finally:
                        shutil.copy(os.path.join(FRONT, name), os.path.join(tmp, name))
                    self.assertNotEqual(done.returncode, 0, "the check did not notice: " + m["id"])

    def test_submit_for_approval_and_my_submissions_pass_their_checks_on_a_fake_dom_and_a_fake_server(self):
        done = run_check(script=SUBMIT_CHECK)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn(" 0 failed", done.stdout)
        for part in ("A the words", "B the sent store", "C the dialog", "D what is sent, and the answers", "F My submissions", "G a send that may have arrived, twins, a 404"):
            self.assertIn("RUNNING: %s ... PASS" % part, done.stdout)

    def test_the_submit_check_can_fail_on_the_broken_copies_of_the_words_the_call_the_dialog_the_list_and_the_tabs(self):
        """tests/data/mapmaker_submit_mutants.json (tools/mutate.py runs it too, with `--keep-going --jobs 1`)"""
        with open(os.path.join(REPO, "tests", "data", "mapmaker_submit_mutants.json"), encoding="utf-8") as f:
            spec = json.load(f)
        self.assertGreaterEqual(len(spec["mutants"]), 40)
        ids = [m["id"] for m in spec["mutants"]]
        self.assertEqual(len(ids), len(set(ids)), "an id twice")
        with tempfile.TemporaryDirectory() as tmp:
            for name in SCRIPTS + ("mapmaker_submit.js",):
                shutil.copy(os.path.join(FRONT, name), os.path.join(tmp, name))
            for m in spec["mutants"]:
                with self.subTest(m["id"]):
                    name = os.path.basename(m["file"])
                    text = read(*m["file"].split("/"))
                    self.assertEqual(text.count(m["old"]), 1, "%s: the text is not in %s exactly once" % (m["id"], name))
                    script = os.path.join(REPO, m["test"].split(" ", 1)[1])
                    with open(os.path.join(tmp, name), "w", encoding="utf-8", newline="") as f:
                        f.write(text.replace(m["old"], m["new"]))
                    try:
                        done = run_check(tmp, script)
                    finally:
                        shutil.copy(os.path.join(FRONT, name), os.path.join(tmp, name))
                    self.assertNotEqual(done.returncode, 0, "the check did not notice: " + m["id"])

    def test_the_pages_rules_for_the_makers_name_are_the_servers(self):
        """The page's copy of the maker-name rules (mapmaker_submit.js: LOOK, makerProblem) is held to the server's (src/ants_server/map_names.cpp: kLookalikes, kWords): the same look-alike letters
        with the same Latin letters, the same reserved words, the same script ranges, the same bot rule and the same link marks"""
        cpp = read("src", "ants_server", "map_names.cpp")
        server = dict((chr(int(code, 16)), letter) for code, letter in re.findall(r"\{(0x[0-9A-F]{4}), '(\w)'\}", cpp))
        look = re.search(r"var LOOK = '([^']+)'", read("web", "front", "mapmaker_submit.js")).group(1)
        self.assertEqual(len(look) % 2, 0)
        page = dict((look[i], look[i + 1].lower()) for i in range(0, len(look), 2))      # (the page lower-cases the whole name afterwards)
        self.assertEqual(len(page) * 2, len(look), "a letter twice in LOOK")
        self.assertEqual(page, server, "the look-alike letters of the page and of the server")
        self.assertGreaterEqual(len(server), 40)
        words = re.search(r"kWords\[\] = \{([^}]+)\}", cpp).group(1)
        server_words = re.findall(r'"(\w+)"', words)
        page_words = re.search(r"\^\(the\)\?\(([a-z|]+)\)\$", read("web", "front", "mapmaker_submit.js")).group(1).split("|")
        self.assertEqual(sorted(page_words), sorted(server_words), "the reserved words")
        self.assertEqual(len(server_words), 11)
        # the accents: the server's table (U+00C0 .. U+017F) is the page's rule (NFD without the marks, and the strokes of ACCENTS) on every one of those characters
        table = re.search(r'const char kAccents\[\] = "([^"]+)";', cpp).group(1)
        self.assertEqual(len(table), 0x180 - 0xC0)
        probe = "".join(chr(c) for c in range(0xC0, 0x180))
        done = subprocess.run([NODE, "-e", "const S = require(%s); const o = []; for (const c of %s) o.push(S.skeletons(c)[0] || '.'); console.log(JSON.stringify(o));" %
                               (json.dumps(os.path.join(FRONT, "mapmaker_submit.js")), json.dumps(probe))], capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stderr)
        page_table = "".join("." if x == "." else ("?" if ord(x[0]) >= 0x80 else x) for x in json.loads(done.stdout))
        expected = "".join("." if t == "." else ("i" if t == "l" else t) for t in table)          # (the page's "l" is "i" too)
        self.assertEqual(page_table.replace("?", "."), expected, "the accents of the page and of the server")
        # the digits that look like letters, and the "l"
        self.assertIn('"oi eas"', cpp)
        self.assertIn("'oi eas'", read("web", "front", "mapmaker_submit.js"))
        # the link marks are the same text in both
        self.assertIn('lower.find("://")', cpp)
        self.assertIn('lower.find("www.")', cpp)
        self.assertIn('lower.find("```")', cpp)

    def test_a_player_never_publishes_and_nothing_can_be_replaced_by_the_script_that_sends_maps(self):
        submit = read("web", "front", "mapmaker_submit.js")
        # the calls: a map goes to the owner's queue (POST /submissions), the list is read (GET), a waiting map is withdrawn: nothing else, nothing of the owner's side or the lobby's lists
        self.assertEqual(sorted(re.findall(r"calls\('(GET|POST|PUT|DELETE|PATCH)', ([^,]+),", submit)), [("GET", "'/submissions'"), ("GET", "'/submissions'"), ("POST", "'/submissions' + q"), ("POST", "'/submissions/' + enc(rec.id) + '/withdraw'")])
        self.assertNotRegex(submit, r"/admin|/maps|PUT|DELETE|PATCH|innerHTML|outerHTML|insertAdjacentHTML|document\.write|eval\(|new Function|localStorage|sessionStorage", "the owner's page and the lists are not touched, and text is text")
        self.assertIn("function enc(t) { return encodeURIComponent(t); }", submit, "a space is %20 and a plus stays a plus")
        self.assertIn("var ready = works && !twin && !big && door !== 'off' && door !== 'checking' && !bad() && !sending;", submit, "the button works only for a map that the checker says works, when the door is on")
        self.assertIn("if (sending || bad() || !works) return;", submit)
        # the bytes that are sent are the bytes the dialog judged: the dialog takes both from one answer of the page
        self.assertIn("return { name: map.name, bytes: made.bytes, file: file, report: r.json };", self.app)
        self.assertIn("own()[0], bytes, function (res)", submit, "a send never uses a token that came from a link")
        # the script is loaded when it is needed; nothing in the page fails without it
        self.assertNotIn("mapmaker_submit.js", self.page)
        self.assertIn("tag.src = 'front/mapmaker_submit.js';", self.app)
        self.assertIn("submit: function () { if (!dialogs.isOpen() && !canvas.isStroking()) submitOr(function () { sent.open(); }); }", self.app)
        self.assertIn("{ text: 'Submit for approval...', onClick: ACTIONS.submit }, { text: 'My submissions', onClick: ACTIONS.subs }", self.app, "the phone's Menu has both")
        self.assertRegex(self.page, r'<button class="btn sm" type="button" data-act="submit" disabled>Submit for approval\.\.\.</button>')
        self.assertIn('id="mine-tabs"', self.page)
        self.assertIn("var NEEDED = [", self.app)
        self.assertNotIn("'MapSubmit'", self.app.split("var NEEDED = [")[1].split("]")[0], "the page starts without the script that sends maps")
        # a status link keeps the token in the fragment, never in a request line
        self.assertIn("'#sent=' + tokens.join(',')", submit)
        self.assertIn("win.history.replaceState(null, '', win.location.pathname + win.location.search)", self.app, "the address loses the token once it is taken in")

    def test_my_maps_is_in_the_page_and_the_menu_and_only_the_players_own_saved_copy_can_be_deleted(self):
        ids = re.findall(r'\bid="([^"]+)"', self.page)
        asked = set(re.findall(r"\$\('([^']+)'\)", self.code["mapmaker_mine.js"]))
        self.assertTrue({"mine", "mine-list", "mine-status", "mine-note", "mine-chips", "mine-new", "mine-back", "mine-title"} <= asked, sorted(asked))
        self.assertEqual(sorted(asked - set(ids)), [], "the list asks for ids that the page does not have")
        self.assertRegex(self.page, r'<section class="mine" id="mine" hidden aria-labelledby="mine-title">')
        app = self.app
        self.assertIn("{ text: 'My maps', onClick: ACTIONS.mine }", app, "the phone's Menu has My maps")
        self.assertIn("mine: function () { if (!dialogs.isOpen() && !canvas.isStroking()) mine.show(); }", app)
        self.assertIn("MapDialog.create(doc, $('dialogs'), [$('top'), $('body'), $('mine')])", app, "the list is inert behind a dialog")
        self.assertIn("tools.setMap(file);\n        keepSoon();\n        if (mine) mine.hide();", app, "a map that takes the screen closes the list (and starts its own history)")
        self.assertIn("onOpen: function (rec) { if (!dialogs.isOpen()) leave(function () { var error = setMap(new win.Uint8Array(rec.bytes), rec.name);", app, "a saved map opens as the working map after the question about unsaved drawing, from a copy of its bytes")
        self.assertIn("converterOf().copy(read(rec))", app)
        self.assertIn("onBack: function () { if (dialogs.isOpen()) return; mine.hide(); var b = doc.querySelector('[data-act=\"mine\"]'); (b && b.offsetParent ? b : doc.querySelector('[data-act=\"menu\"]')).focus(); },", app, "Back and Escape put the focus on My maps again (the Menu on a phone)")
        # a saved map is never written over and never published: the only calls on the drafts are add (Save), list and remove (this list); the page has no submit
        self.assertEqual(len(re.findall(r"store\.remove\(", self.code["mapmaker_mine.js"])), 1)
        self.assertNotIn("store.remove(", app)
        self.assertEqual(len(re.findall(r"\.delete\(", self.code["mapmaker_store.js"])), 3, "three deletes: the slot of unsaved work, a saved map of the drafts store, and a submission record of the sent store (its own database)")
        self.assertIn("run('drafts', 'readwrite', function (s) { return s.delete(k); }", self.code["mapmaker_store.js"], "a saved map is deleted from the drafts store only")
        for name in ("mapmaker_mine.js", "mapmaker_app.js", "mapmaker_store.js"):
            self.assertNotRegex(self.code[name], r"\.put\(\{ key|\.clear\(\)|deleteDatabase|deleteObjectStore", name)
        self.assertNotRegex(self.code["mapmaker_mine.js"], r"(?i)publish|approval|fetch", "My maps sends nothing (the player\'s submissions are the other script\'s)")
        self.assertIn("'Delete this saved map?'", self.code["mapmaker_mine.js"], "a delete is asked about first")

    def test_nothing_can_write_over_a_file_and_no_sentence_is_put_in_as_markup(self):
        for name, text in self.code.items():
            for word in ("innerHTML", "outerHTML", "insertAdjacentHTML", "document.write", "eval(", "new Function", "showSaveFilePicker", "showOpenFilePicker", "createWritable", "FileSystemFileHandle", "XMLHttpRequest", "fetch("):
                if word == "fetch(" and name == "mapmaker_store.js":
                    continue                # (the one read of the site's own list of maps: the next lines pin it)
                self.assertNotIn(word, text, "%s has %s" % (name, word))
        store = self.code["mapmaker_store.js"]
        self.assertEqual(store.count("fetch("), 2)
        self.assertIn("env.fetch('/maps/all')", store, "the first request is a read of the site's list of maps")
        self.assertIn("env.fetch(fileUrl(file))", store, "the second request downloads a chosen map file from /maps/files/")
        self.assertNotRegex(store, r"method|POST|PUT|body:", "the page sends nothing to the site")
        self.assertNotIn("s.put({ key", store, "a draft is added, never put (put would replace one)")
        self.assertEqual(len(re.findall(r"\.put\(", store)), 2, "the puts are the one slot of unsaved work and the sent store's own records (a submission is not a map of My maps)")
        self.assertNotIn("overwrite", self.app.lower())
        self.assertIn("a.download = win.MapFile.downloadName(map.name);", self.app, "a download is a new file the browser saves, named by the map")

    def test_the_tools_are_in_the_page_in_the_order_of_the_picture_and_all_nine_work(self):
        tools = re.findall(r'<button class="tool" type="button" data-tool="([a-z]+)"[^>]*>', self.page)
        self.assertEqual(tools, ["ground", "objects", "bigger", "hills", "ants", "food", "erase", "pickup", "pan"])
        for button in re.findall(r'<button class="tool"[^>]*>', self.page):
            self.assertIn(" disabled", button, "every button starts off until the core is there: " + button)
        self.assertNotIn("Comes in the next update", self.page)
        for tool, letter in (("ground", "G"), ("objects", "O"), ("bigger", "K"), ("hills", "H"), ("ants", "A"), ("food", "F"), ("erase", "E"), ("pickup", "P"), ("pan", "M")):
            self.assertRegex(self.page, r'data-tool="%s"[^>]*aria-keyshortcuts="%s"' % (tool, letter))
        for part in ('id="stepper"', 'id="step-less"', 'id="step-more"', 'id="step-val"', 'aria-label="Fewer"', 'aria-label="More"'):
            self.assertIn(part, self.page, part)
        self.assertNotIn("come next", self.code["mapmaker_tools.js"])
        self.assertRegex(self.page, r'data-tool="ground" aria-pressed="true"', "Ground is the tool at the start, as in the picture")
        for part in ('id="undo-btn"', 'id="redo-btn"', 'id="fill-btn"', 'id="palette"', 'id="brush-range"', 'id="brush-val"', 'type="range"', 'min="1" max="9" step="1"', 'aria-keyshortcuts="Control+Z Meta+Z"', 'data-brush="1"'):
            self.assertIn(part, self.page, part)
        self.assertEqual(re.findall(r'data-brush="(\d+)"', self.page), ["1"])
        palette = self.code["mapmaker_palette.js"]
        self.assertEqual(re.findall(r"key: '([a-z])'", palette), ["g", "o", "k", "h", "a", "f", "e", "p", "m"])

    def test_a_map_that_was_drawn_on_is_asked_about_and_handed_over_as_the_core_writes_it(self):
        app = self.app
        self.assertIn("core.rewrite(map.bytes, 'extended')", app)
        self.assertLess(app.index("if (tools.isClean()) return { bytes: map.bytes };\n        var written = core.rewrite("), app.index("a.download = win.MapFile.downloadName(map.name);"))
        self.assertIn("function leave(then) {", app)
        self.assertIn("'new': function () { leave(newMap); }", app)
        self.assertIn("open: function () { leave(openMap); }", app)
        self.assertIn("'beforeunload'", app)
        self.assertIn("if (tools.isSaved()) return then();", app, "a map that was downloaded as it is can be left without a question")
        self.assertIn("        tools.markSaved();", app)
        self.assertIn("canvas.setFile(file);\n        tools.setMap(file);", app, "a map that is opened starts its own history")
        self.assertIn("canvas.swapFile(file);", app, "the view stays where it is when the dictionary grows")
        self.assertNotIn("showMap()", app, "a check never puts the view back at its start")
        self.assertIn("win.setTimeout(function () {\n            if (canvas.isStroking()) return checkSoon();", app, "the check waits for a hand that is still painting")

    def test_a_stroke_paints_only_the_cells_it_changed(self):
        canvas = self.code["mapmaker_canvas.js"]
        for part in ("View.redrawCell(", "View.redrawPixelCell(", "View.blitPixels(", "var MANY = 3000;", "function sameAsFull()", "canvas.setAttribute('data-paint'"):
            self.assertIn(part, canvas, part)
        self.assertIn("full = null;", canvas.split("function measure()")[1].split("function chrome()")[0], "a canvas that changed size is painted whole")
        for part in ("setPointerCapture", "pointercancel", "e.button !== 1", "spaceHeld", "pending"):
            self.assertIn(part, canvas, part)

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
        self.assertIn("@media (min-width: 601px) and (max-width: 1220px) and (min-height: 451px) {", self.css, "the top bar wraps in a narrow window (eight buttons since Submit for approval..., nine since Play this map)")
        self.assertIn("@media (min-width: 1221px) and (max-width: 1480px) { .edmenu { gap: 4px; } .edmenu .btn.sm { padding-left: 6px; padding-right: 6px; font-size: 14px; } }", self.css, "and is tighter above it, so the kind tag is never covered")
        for part in (".mine { position: absolute; inset: 0; z-index: 8; overflow: auto; }", ".mine { position: static; overflow: visible; flex: 1 0 auto; }", ".mlist { grid-template-columns: 1fr 1fr; gap: 10px; }"):
            self.assertIn(part, self.css, "My maps: the screen over the editor, flowing on a phone, two columns sideways")
        self.assertRegex(self.css, r"\.mchip \{ min-height: 44px;", "the filters are 44 pixels")

    def test_a_dialog_holds_the_focus_closes_on_escape_and_leaves_the_page_behind_it_inert(self):
        dialog = self.code["mapmaker_dialog.js"]
        for part in ("role', 'dialog'", "aria-modal", "e.key === 'Escape'", "e.key !== 'Tab'", "setAttribute('inert', '')", "removeAttribute('inert')", "opener.focus()"):
            self.assertIn(part, dialog, part)
        self.assertIn("radiogroup", dialog)
        self.assertIn("aria-invalid", dialog)
        self.assertIn("aria-live", dialog)

    def test_the_size_boxes_and_the_cards_say_what_they_should(self):
        dialog = self.code["mapmaker_dialog.js"]
        self.assertIn("'Works in the original game and in the new version.'", dialog)
        self.assertIn("'Bigger and taller. Works in the new version only: the original game cannot open it.'", dialog)
        self.assertIn("'Only the new version can open an Extended map.'", dialog)
        self.assertIn("'Make it an Extended map'", dialog)
        self.assertIn("'What kind of map do you want to make?'", dialog)
        self.assertIn("' cells of ') + Sizes.number(live.max) + ' \u00b7 the file will be ' + live.text", dialog)


@unittest.skipUnless(NODE, "node is not installed")
class TheGroundAndTheSprites(unittest.TestCase):
    """the five kinds of ground and the game's pictures (docs/MAP_EDITOR.md, "Ground" and "Sprites"): the checks run, broken copies fail them, and the page's side is read from the source"""
    atlas = None

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.atlas = cls.tmp.name
        made = subprocess.run([sys.executable, os.path.join(REPO, "tools", "map_atlas.py"), cls.atlas], capture_output=True, text=True)
        if made.returncode != 0:
            raise AssertionError("the atlas could not be made: " + made.stdout + made.stderr)
        cls.page = read("web", "mapmaker.html")
        cls.app = read("web", "front", "mapmaker_app.js")
        cls.tools = read("web", "front", "mapmaker_tools.js")

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_the_ground_passes_its_checks(self):
        done = run_check(script=GROUND_CHECK)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("0 failed", done.stdout)
        for part in ("A the names and the kinds", "B the facts", "C the library", "D the palette and the colors", "E the sheet of a kind"):
            self.assertIn("RUNNING: %s ... PASS" % part, done.stdout)

    def test_the_sprites_pass_their_checks_on_the_real_atlas(self):
        done = run_check(script=SPRITES_CHECK, extra=["--atlas", self.atlas])
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("0 failed", done.stdout)
        for part in ("A loading", "B a whole frame", "C pictures and flat colors", "D the layer kept between frames", "E the zoom", "F a toy that is fetched alone", "G the ants and the ghost", "H chips"):
            self.assertIn("RUNNING: %s ... PASS" % part, done.stdout)

    def test_the_checks_can_fail_on_the_broken_copies_of_the_ground_the_tools_and_the_sprites(self):
        """tests/data/mapmaker_sprites_mutants.json (tools/mutate.py runs it too, with `--keep-going --jobs 1`): each broken copy must fail the check named for it"""
        with open(os.path.join(REPO, "tests", "data", "mapmaker_sprites_mutants.json"), encoding="utf-8") as f:
            spec = json.load(f)
        self.assertGreaterEqual(len(spec["mutants"]), 60)
        ids = [m["id"] for m in spec["mutants"]]
        self.assertEqual(len(ids), len(set(ids)), "an id twice")
        checks = {"web_mapmaker_ground_check": GROUND_CHECK, "web_mapmaker_sprites_check": SPRITES_CHECK, "web_mapmaker_edit_check": EDIT_CHECK, "web_mapmaker_check.js": CHECK}
        with tempfile.TemporaryDirectory() as tmp:
            for name in ALL_SCRIPTS:
                shutil.copy(os.path.join(FRONT, name), os.path.join(tmp, name))
            for m in spec["mutants"]:
                with self.subTest(m["id"]):
                    name = os.path.basename(m["file"])
                    text = read(*m["file"].split("/"))
                    self.assertEqual(text.count(m["old"]), 1, "%s: the text is not in %s exactly once" % (m["id"], name))
                    if "web_mapmaker_browser" in m["test"]:
                        continue                # (a mutant that only the real browser can notice: tools/mutate.py runs it with ANTS_WEB_URL set)
                    script = [v for k, v in checks.items() if k in m["test"]][0]
                    with open(os.path.join(tmp, name), "w", encoding="utf-8", newline="") as f:
                        f.write(text.replace(m["old"], m["new"]))
                    try:
                        done = run_check(tmp, script, ["--atlas", self.atlas] if script == SPRITES_CHECK else [])
                    finally:
                        shutil.copy(os.path.join(FRONT, name), os.path.join(tmp, name))
                    self.assertNotEqual(done.returncode, 0, "the check did not notice: " + m["id"])

    def test_the_three_lazy_scripts_are_loaded_by_the_app_and_not_by_the_page(self):
        tags = re.findall(r'<script src="([^"]+)"></script>', self.page)
        for name in LAZY_SCRIPTS:
            self.assertNotIn("front/" + name, tags, name + " loads after the page is up")
            self.assertIn(name, self.app)
        self.assertIn("script('mapmaker_ground.js'); script('mapmaker_sprites.js'); script('mapmaker_edges.js');", self.app)
        self.assertIn("s.src = 'front/' + name;", self.app)
        self.assertIn('<div class="atlasnote" id="atlas-note" role="status" hidden></div>', self.page, "the plain line over the map when the atlas does not load")
        self.assertIn("The game\\u2019s pictures could not be loaded, so the map is drawn in flat colors. Your map is not affected.", self.app)
        self.assertIn("onFail: function () { canvas.setSprites(null); tools.use(win.MapGround, null); note(flat); }", self.app, "a failed atlas leaves the flat colors")
        self.assertNotIn("MapGround", self.app.split("var NEEDED = ")[1].split("];")[0], "the page does not need the lazy parts to start")

    def test_fix_edges_passes_its_checks_on_the_library_and_on_real_bytes(self):
        done = run_check(script=EDGES_CHECK)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r"edges check: \d+ checks passed")
        self.assertIn("library: 540 maps", done.stdout)

    def test_the_checks_can_fail_on_the_broken_copies_of_fix_edges(self):
        """tests/data/mapmaker_edges_mutants.json (tools/mutate.py runs it too, with `--keep-going --jobs 1`): each broken copy must fail the check named for it"""
        with open(os.path.join(REPO, "tests", "data", "mapmaker_edges_mutants.json"), encoding="utf-8") as f:
            spec = json.load(f)
        self.assertGreaterEqual(len(spec["mutants"]), 35)
        ids = [m["id"] for m in spec["mutants"]]
        self.assertEqual(len(ids), len(set(ids)), "an id twice")
        with tempfile.TemporaryDirectory() as tmp:
            for name in ALL_SCRIPTS:
                shutil.copy(os.path.join(FRONT, name), os.path.join(tmp, name))
            for m in spec["mutants"]:
                with self.subTest(m["id"]):
                    name = os.path.basename(m["file"])
                    text = read(*m["file"].split("/"))
                    self.assertEqual(text.count(m["old"]), 1, "%s: the text is not in %s exactly once" % (m["id"], name))
                    if "web_mapmaker_browser" in m["test"]:
                        continue                # (a mutant that only the real browser can notice: tools/mutate.py runs it with ANTS_WEB_URL set)
                    with open(os.path.join(tmp, name), "w", encoding="utf-8", newline="") as f:
                        f.write(text.replace(m["old"], m["new"]))
                    try:
                        done = run_check(tmp, EDGES_CHECK, [])
                    finally:
                        shutil.copy(os.path.join(FRONT, name), os.path.join(tmp, name))
                    self.assertNotEqual(done.returncode, 0, "the check did not notice: " + m["id"])

    def test_the_ants_box_passes_its_checks_on_the_library(self):
        done = run_check(script=ANTS_CHECK)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r"\d+ checks, 0 failed")

    def test_the_checks_can_fail_on_the_broken_copies_of_the_ants_box(self):
        """tests/data/mapmaker_ants_mutants.json (tools/mutate.py runs it too, with `--keep-going --jobs 1`): each broken copy must fail the check named for it"""
        with open(os.path.join(REPO, "tests", "data", "mapmaker_ants_mutants.json"), encoding="utf-8") as f:
            spec = json.load(f)
        self.assertGreaterEqual(len(spec["mutants"]), 30)
        ids = [m["id"] for m in spec["mutants"]]
        self.assertEqual(len(ids), len(set(ids)), "an id twice")
        with tempfile.TemporaryDirectory() as tmp:
            for name in ALL_SCRIPTS:
                shutil.copy(os.path.join(FRONT, name), os.path.join(tmp, name))
            for m in spec["mutants"]:
                with self.subTest(m["id"]):
                    name = os.path.basename(m["file"])
                    text = read(*m["file"].split("/"))
                    self.assertEqual(text.count(m["old"]), 1, "%s: the text is not in %s exactly once" % (m["id"], name))
                    if "web_mapmaker_browser" in m["test"]:
                        continue                # (a mutant that only the real browser can notice: tools/mutate.py runs it with ANTS_WEB_URL set)
                    with open(os.path.join(tmp, name), "w", encoding="utf-8", newline="") as f:
                        f.write(text.replace(m["old"], m["new"]))
                    try:
                        done = run_check(tmp, ANTS_CHECK, [])
                    finally:
                        shutil.copy(os.path.join(FRONT, name), os.path.join(tmp, name))
                    self.assertNotEqual(done.returncode, 0, "the check did not notice: " + m["id"])

    def test_the_ants_box_is_loaded_on_demand_with_its_style_and_the_tool_hands_it_the_canvas(self):
        tags = re.findall(r'<script src="([^"]+)"></script>', self.page)
        self.assertNotIn("front/mapmaker_ants.js", tags)
        self.assertIn("s.src = 'front/mapmaker_ants.js'; c.rel = 'stylesheet'; c.href = 'front/mapmaker_ants.css';", self.app)
        self.assertIn("if (id === 'ants') askAnts();", self.tools)
        self.assertNotIn("MapAnts", self.app.split("var NEEDED = ")[1].split("];")[0], "the page does not need the box to start: without it the tool is the clump of before")
        self.assertIn("act: act", self.tools, "one change, one step of the history")
        self.assertRegex(read(".github", "workflows", "ci.yml"), r"ls -l [^\n]*\bfront/mapmaker_ants\.js\b[^\n]*\bfront/mapmaker_ants\.css\b")
        ants = read("web", "front", "mapmaker_ants.js")
        self.assertNotRegex(ants, r"innerHTML|insertAdjacentHTML|document\.write|\beval\(|new Function", "words are put in with textContent only")

    def test_the_pick_up_tool_passes_its_checks_on_made_up_maps_and_on_the_library(self):
        done = run_check(script=PICKUP_CHECK)
        self.assertEqual(done.returncode, 0, done.stdout[-3000:] + done.stderr[-2000:])
        self.assertRegex(done.stdout, r"\d+ checks, 0 failed")

    def test_the_checks_can_fail_on_the_broken_copies_of_the_pick_up_tool(self):
        """tests/data/mapmaker_pickup_mutants.json (tools/mutate.py runs it too, with `--keep-going --jobs 1`): each broken copy must fail the check (--quick: 40 maps of the library, not all)"""
        with open(os.path.join(REPO, "tests", "data", "mapmaker_pickup_mutants.json"), encoding="utf-8") as f:
            spec = json.load(f)
        self.assertGreaterEqual(len(spec["mutants"]), 100)
        ids = [m["id"] for m in spec["mutants"]]
        self.assertEqual(len(ids), len(set(ids)), "an id twice")
        with tempfile.TemporaryDirectory() as tmp:
            for name in ALL_SCRIPTS:
                shutil.copy(os.path.join(FRONT, name), os.path.join(tmp, name))
            for m in spec["mutants"]:
                with self.subTest(m["id"]):
                    name = os.path.basename(m["file"])
                    text = read(*m["file"].split("/"))
                    self.assertEqual(text.count(m["old"]), 1, "%s: the text is not in %s exactly once" % (m["id"], name))
                    if "web_mapmaker_browser" in m["test"]:
                        continue                # (a mutant that only the real browser can notice: tools/mutate.py runs it with ANTS_WEB_URL set)
                    with open(os.path.join(tmp, name), "w", encoding="utf-8", newline="") as f:
                        f.write(text.replace(m["old"], m["new"]))
                    try:
                        done = run_check(tmp, PICKUP_CHECK, ["--quick"])
                    finally:
                        shutil.copy(os.path.join(FRONT, name), os.path.join(tmp, name))
                    self.assertNotEqual(done.returncode, 0, "the check did not notice: " + m["id"])

    def test_the_pick_up_tool_is_loaded_on_demand_with_its_style_and_the_tool_hands_it_the_canvas(self):
        tags = re.findall(r'<script src="([^"]+)"></script>', self.page)
        self.assertNotIn("front/mapmaker_pickup.js", tags)
        self.assertIn("add('script', 'script', 'src', 'front/mapmaker_pickup.js');", self.app)
        self.assertIn("add('style', 'link', 'href', 'front/mapmaker_pickup.css', function (n) { n.rel = 'stylesheet'; });", self.app)
        self.assertIn("if (id === 'pickup') askPick();", self.tools)
        self.assertNotIn("MapPickup", self.app.split("var NEEDED = ")[1].split("];")[0], "the page does not need the tool to start: a part that did not load is said, and the tool is Pan")
        self.assertIn("act: act", self.tools, "one change, one step of the history")
        self.assertRegex(read(".github", "workflows", "ci.yml"), r"ls -l [^\n]*\bfront/mapmaker_pickup\.js\b[^\n]*\bfront/mapmaker_pickup\.css\b")
        pickup = read("web", "front", "mapmaker_pickup.js")
        self.assertNotRegex(pickup, r"innerHTML|outerHTML|insertAdjacentHTML|document\.write|\beval\(|new Function", "words are put in with textContent only")
        self.assertNotRegex(pickup, r"XMLHttpRequest|fetch\(|localStorage|indexedDB|createObjectURL|\.bytes\[[^\]]*\]\s*=[^=]", "the tool reads and gives changes; it writes no byte itself and keeps nothing")
        self.assertRegex(pickup, r"^[\x00-\x7f]*$", "ASCII only (the dot and the arrows are escapes)")
        css = read("web", "front", "mapmaker_pickup.css")
        self.assertRegex(css, r"\.pk-b \{[^}]*min-height: 44px", "a button is 44 pixels high")
        self.assertRegex(css, r"\.pk-arrow \{[^}]*min-height: 48px; min-width: 48px", "a nudge arrow is 48 pixels")
        self.assertRegex(css, r"@media \(max-width: 600px\) \{\s*\.pk-bar \{ position: fixed; left: 0; right: 0; bottom: 0;", "on a phone the bar is at the foot of the screen")
        self.assertRegex(css, r"\.pk-help \{[^}]*min-height: 36px", "the link to the help is a tap target (44 pixels on a touch screen)")
        self.assertIn("@media (pointer: coarse), (max-width: 600px) { .pk-help { min-height: 44px; } }", css)
        root = read("web", "front", "classic.css") + read("web", "front", "mapmaker.css")
        for token in set(re.findall(r"var\(--([a-z-]+)", css)):
            self.assertRegex(root, r"--%s:" % token, "the style uses only tokens that the page defines: --" + token)
        self.assertNotIn("data-tool=\"move\"", self.page)
        self.assertIn('data-tool="pickup"', self.page)
        self.assertIn('data-tool="pan"', self.page)
        self.assertNotRegex(self.tools, r"\bmove: '", "no hint for a tool that is not there")
        self.assertNotIn("moveAt", read("web", "front", "mapmaker_blocks.js"), "what Pick up replaces is gone (the old drag of the Move tool)")

    def test_the_fix_edges_button_is_in_the_page_and_wired_by_the_app_once_the_parts_are_there(self):
        self.assertRegex(self.page, r'<button class="eb" type="button" id="edges-btn" disabled>Fix edges\.\.\.</button>')
        self.assertIn("if (!win.MapEdges || !win.MapGround) return;", self.app, "the button works only when both lazy scripts are there")
        self.assertIn("apply: tools.act", self.app, "one change, one step of the history")
        self.assertIn("MapView.visible(st.view, map.file, st.width, st.height)", self.app, "the part you see is the one that the canvas draws")
        self.assertNotIn("MapEdges", self.app.split("var NEEDED = ")[1].split("];")[0], "the page does not need Fix edges to start")
        self.assertRegex(read(".github", "workflows", "ci.yml"), r"ls -l [^\n]*\bfront/mapmaker_edges\.js\b")

    def test_the_editor_is_not_changed_by_the_pictures(self):
        """the bytes of the open file stay the one copy: nothing of the new code writes a byte, and the sprite layer only reads"""
        sprites, ground, edges = read("web", "front", "mapmaker_sprites.js"), read("web", "front", "mapmaker_ground.js"), read("web", "front", "mapmaker_edges.js")
        for name, text in (("mapmaker_sprites.js", sprites), ("mapmaker_ground.js", ground), ("mapmaker_edges.js", edges)):
            self.assertNotRegex(text, r"\.bytes\[[^\]]*\]\s*=[^=]|\.set\(|createObjectURL|indexedDB|localStorage|\.put\(", name + " reads the map and writes nothing")
        self.assertNotIn("XMLHttpRequest", sprites)
        self.assertEqual(re.findall(r"farBase \+ ", sprites), ["farBase + "], "the one request besides the atlas: a toy's own sprite file")
        self.assertIn("'/asset_catalog/sprites/'", sprites)
        self.assertIn("'mapmaker_atlas/'", sprites)


class TheImageCarriesThePage(unittest.TestCase):
    def test_the_dockerfile_the_ci_and_the_local_build_have_it(self):
        self.assertRegex(read("Dockerfile"), r"(?m)^COPY web/mapmaker\.html /usr/share/nginx/html/mapmaker\.html$")
        self.assertRegex(read(".github", "workflows", "ci.yml"), r"ls -l [^\n]*\bmapmaker\.html\b")
        self.assertRegex(read(".github", "workflows", "ci.yml"), r"ls -l [^\n]*\bfront/mapmaker_app\.js\b")
        self.assertIn('web/mapmaker.html', read("build_web.sh"))
        ci = read(".github", "workflows", "ci.yml")
        self.assertIn("node tests/scripts/web_mapmaker_core_check.js --module lvl_core_from_image --require", ci)
        self.assertIn("node tests/scripts/web_mapmaker_pickup_check.js --module lvl_core_from_image --require", ci)

    def test_the_map_maker_has_its_own_version_file_and_prints_it_in_the_bottom_footer(self):
        ver = read("web", "MAPMAKER_VERSION").strip()
        self.assertRegex(ver, r"^\d+\.\d+\.\d+$", "web/MAPMAKER_VERSION is MAJOR.MINOR.PATCH")
        page = read("web", "mapmaker.html")
        self.assertIn('<footer class="bar edfoot" id="edfoot">', page)
        self.assertIn('Map Maker <span id="mapmaker-version">v%s</span>' % ver, page)
        self.assertIn('<span id="mapmaker-build">local</span>', page)
        for href in ('href="/"', 'href="matches.html"', 'href="changelog.html"', 'href="legal.html"'):
            self.assertIn(href, page)

    def test_every_file_of_the_page_is_in_front_and_the_folder_is_copied_whole(self):
        self.assertIn("COPY web/front/ /usr/share/nginx/html/front/", read("Dockerfile"))
        for name in ALL_SCRIPTS + ("mapmaker.css",):
            self.assertTrue(os.path.isfile(os.path.join(FRONT, name)), name)

    def test_no_file_is_over_the_line_ceiling_of_the_hygiene_check(self):
        for name in ALL_SCRIPTS + ("mapmaker.css",):
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
