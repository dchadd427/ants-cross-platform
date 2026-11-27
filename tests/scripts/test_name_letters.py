#!/usr/bin/env python3
"""The letters of a name (hub/name_letters.py; docs/HUB_NICKNAMES.md, "The letters"; run by ./run_tests.sh --tools and the CI, with the other python tests of tests/scripts).

The table is ONE file; tools/gen_name_letters.py makes the game's table (src/ants_net/name_letters_data.inc) and the page's (the generated block of web/front/name_letters.js) from it.
  - TheGenerator: the committed outputs are what the generator makes (`--check`), and `--check` fails on an output that was edited by hand or a table that overlaps itself.
  - TheFont: every letter of the table is drawn by the game's font file (its own character table is read, not assumed: a letter that the file lacks would be a box in the results screen), is a Unicode
    letter, is in NFC and is no combining mark, and the table holds what the pictures promised (the Latin and Vietnamese letters, the modern Cyrillic ones) and nothing of Greek or the other scripts.
  - TheScript: web/front/name_letters.js and src/ants_net/name_rules.cpp agree with the table on every code point (tests/scripts/web_name_letters_check.js, tests/test_net/test_lobby_names.cpp N4.70).
The node check needs node; without it the test is skipped and says so.
"""
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unicodedata
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, REPO)
sys.path.insert(0, os.path.join(REPO, "tools"))
from hub import name_letters  # noqa: E402
import gen_name_letters  # noqa: E402

FONT = os.path.join(REPO, "Original-Ants", "LibreFranklin-Medium.ttf")
WEB_FONT = os.path.join(REPO, "web", "front", "LibreFranklin-Medium.ttf")
GENERATOR = os.path.join(REPO, "tools", "gen_name_letters.py")
CHECK = os.path.join(REPO, "tests", "scripts", "web_name_letters_check.js")
NODE = shutil.which("node")


def font_code_points(path):
    """The code points that a TrueType file draws, read from its own 'cmap' table (a format 12 or format 4 subtable for Unicode)."""
    with open(path, "rb") as f:
        data = f.read()
    count = struct.unpack(">H", data[4:6])[0]
    tables = {}
    for i in range(count):
        tag, _, offset, length = struct.unpack(">4sIII", data[12 + 16 * i:28 + 16 * i])
        tables[tag.decode()] = (offset, length)
    base = tables["cmap"][0]
    _, subtables = struct.unpack(">HH", data[base:base + 4])
    best = None
    for i in range(subtables):
        platform, encoding, offset = struct.unpack(">HHI", data[base + 4 + 8 * i:base + 12 + 8 * i])
        fmt = struct.unpack(">H", data[base + offset:base + offset + 2])[0]
        if (platform, encoding) in ((3, 10), (0, 4), (0, 6)) and fmt == 12:
            best = (base + offset, fmt)
            break
        if (platform, encoding) in ((3, 1), (0, 3)) and fmt == 4 and best is None:
            best = (base + offset, fmt)
    assert best is not None, "no Unicode character table in the font"
    start, fmt = best
    points = set()
    if fmt == 12:
        groups = struct.unpack(">I", data[start + 12:start + 16])[0]
        for g in range(groups):
            first, last, glyph = struct.unpack(">III", data[start + 16 + 12 * g:start + 28 + 12 * g])
            if glyph != 0:
                points.update(range(first, last + 1))
    else:
        double = struct.unpack(">H", data[start + 6:start + 8])[0]
        segments = double // 2
        ends = struct.unpack(">%dH" % segments, data[start + 14:start + 14 + double])
        starts = struct.unpack(">%dH" % segments, data[start + 16 + double:start + 16 + 2 * double])
        deltas = struct.unpack(">%dh" % segments, data[start + 16 + 2 * double:start + 16 + 3 * double])
        range_base = start + 16 + 3 * double
        offsets = struct.unpack(">%dH" % segments, data[range_base:range_base + double])
        for i in range(segments):
            for c in range(starts[i], ends[i] + 1):
                if c == 0xFFFF:
                    continue
                if offsets[i] == 0:
                    glyph = (c + deltas[i]) & 0xFFFF
                else:
                    at = range_base + 2 * i + offsets[i] + 2 * (c - starts[i])
                    glyph = struct.unpack(">H", data[at:at + 2])[0]
                    glyph = (glyph + deltas[i]) & 0xFFFF if glyph else 0
                if glyph:
                    points.add(c)
    return points


def table_code_points():
    return {cp for ranges in name_letters.ALPHABETS.values() for first, last in ranges for cp in range(first, last + 1)}


class TheGenerator(unittest.TestCase):
    def test_the_committed_tables_are_what_the_generator_makes(self):
        run = subprocess.run([sys.executable, GENERATOR, "--check"], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)

    def test_a_table_that_was_edited_without_the_generator_fails_the_check(self):
        with tempfile.TemporaryDirectory() as root:
            for rel in (gen_name_letters.INC, gen_name_letters.JS, "hub/name_letters.py", "hub/__init__.py"):
                os.makedirs(os.path.dirname(os.path.join(root, rel)), exist_ok=True)
                shutil.copy(os.path.join(REPO, rel), os.path.join(root, rel))
            self.assertEqual(subprocess.run([sys.executable, GENERATOR, "--check", "--root", root], capture_output=True).returncode, 0)
            path = os.path.join(root, gen_name_letters.INC)
            with open(path, encoding="utf-8") as f:
                text = f.read()
            with open(path, "w", encoding="utf-8") as f:
                f.write(text.replace("{0x41, 0x5A, 0}", "{0x41, 0x5B, 0}"))
            self.assertEqual(subprocess.run([sys.executable, GENERATOR, "--check", "--root", root], capture_output=True).returncode, 1)
            subprocess.run([sys.executable, GENERATOR, "--root", root], capture_output=True, check=True)
            self.assertEqual(subprocess.run([sys.executable, GENERATOR, "--check", "--root", root], capture_output=True).returncode, 0)
            module = os.path.join(root, "hub", "name_letters.py")
            with open(module, encoding="utf-8") as f:
                text = f.read()
            with open(module, "w", encoding="utf-8") as f:
                f.write(text.replace("(0x61, 0x7A),", "(0x61, 0x7A), (0x70, 0x72),"))           # a range that overlaps another
            run = subprocess.run([sys.executable, GENERATOR, "--check", "--root", root], capture_output=True, text=True)
            self.assertEqual(run.returncode, 1)
            self.assertIn("overlap", run.stderr)


class TheFont(unittest.TestCase):
    def test_the_game_and_the_web_game_use_one_font_file(self):
        with open(FONT, "rb") as a, open(WEB_FONT, "rb") as b:
            self.assertEqual(a.read(), b.read())

    def test_every_letter_of_the_table_is_drawn_by_the_font_and_is_a_plain_letter(self):
        drawn = font_code_points(FONT)
        for cp in sorted(table_code_points()):
            ch = chr(cp)
            self.assertIn(cp, drawn, "U+%04X %s is not in the font" % (cp, ch))
            self.assertEqual(unicodedata.category(ch)[0], "L", "U+%04X is no letter" % cp)
            self.assertEqual(unicodedata.normalize("NFC", ch), ch, "U+%04X is not in NFC" % cp)
        for ch in name_letters.DIGITS:
            self.assertIn(ord(ch), drawn)

    def test_the_table_holds_what_was_promised_and_nothing_of_the_other_scripts(self):
        points = table_code_points()
        for ch in "AZazÀÖØöøÿĀžäöüßñåøçłšżőğıșțƠơƯưẠỹễđĐ":
            self.assertIn(ord(ch), points, ch)
        for ch in "ЁАяёЄІЇҐґЎўЂЈЉЊЋЏ":
            self.assertIn(ord(ch), points, ch)
        for ch in "αΑאاกऀ一あ가×÷ſѠӘ\U0001f600́ ":      # Greek, Hebrew, Arabic, Thai, Devanagari, CJK, kana, Hangul, two signs, the long s, an old letter, Cyrillic Ext, an emoji, a mark, a blank
            self.assertNotIn(ord(ch), points, repr(ch))
        self.assertEqual(name_letters.alphabet("a"), "latin")
        self.assertEqual(name_letters.alphabet("Ж"), "cyrillic")
        self.assertIsNone(name_letters.alphabet("3"))
        self.assertTrue(name_letters.is_digit("7") and not name_letters.is_digit("٣") and not name_letters.is_digit("77"))
        # the two alphabets share no code point
        both = [{cp for first, last in ranges for cp in range(first, last + 1)} for ranges in name_letters.ALPHABETS.values()]
        self.assertFalse(both[0] & both[1])


@unittest.skipUnless(NODE, "node is not installed: the page's twin of the table is not run")
class TheScript(unittest.TestCase):
    def test_the_page_and_the_table_agree_on_every_code_point_and_every_case(self):
        expected = json.dumps([[first, last, index] for index, ranges in enumerate(name_letters.ALPHABETS.values()) for first, last in ranges])
        run = subprocess.run([NODE, CHECK, expected], capture_output=True, text=True, cwd=REPO)          # (the table as this file's own module has it: the page's generated copy is held to it code point by code point)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
