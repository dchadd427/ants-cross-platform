#!/usr/bin/env python3
"""The packed world art (assets/world_art/, made by tools/world_art/make_world_art.py; tools/world_art/README.md): run by ./run_tests.sh and the CI with the other python tests of tests/scripts.

Nothing is drawn here (drawing takes about 40 seconds): the committed manifest and the three committed sheets are read, and held to the rules that the game's reader relies on:
  - TheManifest: the header, the three sheet rows (real PNG files of 8-bit RGBA with the sizes they state, as wide as 1024, 2048 and 3072 pixels and at most 4096 high), the sprite records
    (ten whole numbers each, sorted by number and variant, none twice, UTF-8 with LF and nothing else);
  - ThePictures: every picture is the size of the original's sprite (asset_catalog/sprites) times the scale, lies inside its sheet with its one-pixel ring, no two pictures (rings included) overlap,
    and the ring repeats the picture's own edge pixels (a corner pixel repeats the corner);
  - TheSet: the numbers are exactly every sprite of the ground tiles 431 to 669 of docs/chd_table4_animations.json, the bridge sprites 112 to 116 and the bigger bubbles of the mud (10000 or 20000
    plus the bubble sprites 641 to 650, each the size of its sprite: 32 x 32), and variants 1 to 15 exist exactly for the plain tiles 640 to 657, 726 to 731 and 901 and the bigger bubbles;
  - TheChecksAreNotVacuous: each rule above refuses a copy of the real data that is broken in that one way.
Needs numpy and Pillow (the tests are skipped only on a machine that has none; CI has both).
"""
import json
import os
import re
import unittest

try:
    import numpy as np
    from PIL import Image
except ImportError:
    np = Image = None

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ART = os.path.join(REPO, "assets", "world_art")
SPRITES = os.path.join(REPO, "asset_catalog", "sprites")
TABLE = os.path.join(REPO, "docs", "chd_table4_animations.json")

SCALES = (1, 2, 3)
WIDTH = {1: 1024, 2: 2048, 3: 3072}
MAX_SIDE = 4096
RING = 1
VARIANTS = 16
VARIANT_TILES = set(range(640, 658)) | set(range(726, 732)) | {901}
GROUND_TILES = (431, 669)
BRIDGE = set(range(112, 117))
BIG_BUBBLES = {base + sprite for base in (10000, 20000) for sprite in range(641, 651)}      # a bigger bubble's number: 10000 (medium) or 20000 (large) plus the original's bubble sprite
NUMBER = re.compile(r"0|[1-9][0-9]*")


def parse_manifest(text):
    """(sheets {scale: (file, width, height)}, sprites [(number, variant, w, h, {scale: (x, y)})]); ValueError with the reason when the text is not the format"""
    if "\r" in text or not text.endswith("\n") or text.endswith("\n\n"):
        raise ValueError("one record a line, every line ends with LF, no empty line")
    lines = text[:-1].split("\n")
    if lines[0] != "ants-world-art 1":
        raise ValueError("the first line is the header")
    sheets = {}
    for scale, line in zip(SCALES, lines[1:4]):
        parts = line.split(" ")
        if len(parts) != 5 or parts[0] != "sheet" or parts[1] != str(scale) or parts[2] != "sheet_%dx.png" % scale or not all(NUMBER.fullmatch(p) for p in parts[3:]):
            raise ValueError("line %d is the sheet row of scale %d" % (scale + 1, scale))
        sheets[scale] = (parts[2], int(parts[3]), int(parts[4]))
    sprites = []
    for line in lines[4:]:
        parts = line.split(" ")
        if len(parts) != 11 or parts[0] != "sprite" or not all(NUMBER.fullmatch(p) for p in parts[1:]):
            raise ValueError("not a sprite record: %r" % line)
        n = [int(p) for p in parts[1:]]
        sprites.append((n[0], n[1], n[2], n[3], {s: (n[4 + 2 * i], n[5 + 2 * i]) for i, s in enumerate(SCALES)}))
    keys = [(s[0], s[1]) for s in sprites]
    if keys != sorted(set(keys)):
        raise ValueError("the sprite records are sorted by number and variant, none twice")
    return sheets, sprites


def read_sheets(sheets, folder=ART):
    """{scale: RGBA array of the sheet file}; the files must be 8-bit RGBA PNGs of the size that the manifest states"""
    out = {}
    for scale, (name, w, h) in sheets.items():
        with Image.open(os.path.join(folder, name)) as im:
            if im.format != "PNG" or im.mode != "RGBA" or im.size != (w, h):
                raise ValueError("%s is %s %s %s, the manifest says an RGBA PNG of %d x %d" % (name, im.format, im.mode, im.size, w, h))
            out[scale] = np.asarray(im).copy()
    return out


_sizes = {}


def original_size(number):
    """the size of the original's sprite that a number stands for (a bigger bubble is the size of its bubble sprite)"""
    sprite = number % 10000
    if sprite not in _sizes:
        with Image.open(os.path.join(SPRITES, "sprite_%04d.png" % sprite)) as im:
            _sizes[sprite] = im.size
    return _sizes[sprite]


def sheet_problems(arrays):
    out = []
    for scale, a in arrays.items():
        if a.shape[1] != WIDTH[scale]:
            out.append("the %dx sheet is %d pixels wide, not %d" % (scale, a.shape[1], WIDTH[scale]))
        if not 0 < a.shape[0] <= MAX_SIDE:
            out.append("the %dx sheet is %d pixels high (1 to %d)" % (scale, a.shape[0], MAX_SIDE))
    return out


def size_problems(sprites):
    """a sprite's size is the original's"""
    return ["sprite %d is %d x %d, the original's is %d x %d" % ((s[0], s[2], s[3]) + original_size(s[0])) for s in sprites if (s[2], s[3]) != original_size(s[0])]


def place_problems(arrays, sprites):
    """every picture with its ring lies inside the sheet of its scale and no two of them share a pixel"""
    out = []
    for scale, a in arrays.items():
        cover = np.zeros(a.shape[:2], np.uint8)
        for number, variant, w, h, places in sprites:
            x, y = places[scale]
            x0, y0, x1, y1 = x - RING, y - RING, x + w * scale + RING, y + h * scale + RING
            if x0 < 0 or y0 < 0 or x1 > a.shape[1] or y1 > a.shape[0]:
                out.append("sprite %d variant %d at %dx is not inside its sheet" % (number, variant, scale))
                continue
            cover[y0:y1, x0:x1] += 1
        if cover.max() > 1:
            out.append("the pictures of the %dx sheet overlap (%d pixels)" % (scale, (cover > 1).sum()))
    return out


def ring_problems(arrays, sprites):
    """the ring is the picture's own edge repeated: a cell is the picture padded by one pixel of its edge"""
    out = []
    for scale, a in arrays.items():
        for number, variant, w, h, places in sprites:
            x, y = places[scale]
            picture = a[y:y + h * scale, x:x + w * scale]
            cell = a[y - RING:y + h * scale + RING, x - RING:x + w * scale + RING]
            if not np.array_equal(cell, np.pad(picture, ((RING, RING), (RING, RING), (0, 0)), mode="edge")):
                out.append("the ring of sprite %d variant %d at %dx is not its edge" % (number, variant, scale))
    return out


def expected_numbers():
    with open(TABLE, encoding="utf-8") as f:
        table = json.load(f)
    assert table[GROUND_TILES[0]]["entry_index"] == GROUND_TILES[0] and table[GROUND_TILES[1]]["entry_index"] == GROUND_TILES[1]
    ground = {f["sprite_index"] for entry in table[GROUND_TILES[0]:GROUND_TILES[1] + 1] for item in entry["subitems"] for f in item["frames"]}
    return ground | BRIDGE | BIG_BUBBLES


def set_problems(sprites, numbers):
    """the numbers drawn are exactly `numbers`, and a number has variants 0 to 15 if it is a plain tile or a bigger bubble and only 0 if it is not"""
    out = []
    found = {}
    for number, variant, _, _, _ in sprites:
        found.setdefault(number, set()).add(variant)
    for number in sorted(numbers - set(found)):
        out.append("sprite %d is missing" % number)
    for number in sorted(set(found) - numbers):
        out.append("sprite %d is not a ground tile or a bridge sprite" % number)
    for number, variants in sorted(found.items()):
        want = set(range(VARIANTS)) if number in VARIANT_TILES | BIG_BUBBLES else {0}
        if variants != want:
            out.append("sprite %d has the variants %s, not %s" % (number, sorted(variants), sorted(want)))
    return out


@unittest.skipUnless(np and Image, "numpy and Pillow are needed")
class Committed(unittest.TestCase):
    """the committed files, read once"""

    loaded = None

    @classmethod
    def setUpClass(cls):
        if Committed.loaded is None:
            with open(os.path.join(ART, "world_art.txt"), "rb") as f:
                raw = f.read()
            text = raw.decode("utf-8")
            sheets, sprites = parse_manifest(text)
            Committed.loaded = (raw, text, sheets, sprites, read_sheets(sheets))
        cls.raw, cls.text, cls.sheets, cls.sprites, cls.arrays = Committed.loaded


class TheManifest(Committed):
    def test_the_text_is_in_the_format(self):
        self.assertEqual(self.raw, self.text.encode("utf-8"))
        self.assertTrue(self.text.startswith("ants-world-art 1\nsheet 1 sheet_1x.png "))
        self.assertEqual(self.text.count("\nsheet "), 3)
        self.assertEqual(self.text.count("\nsprite "), len(self.sprites))
        self.assertEqual(len(self.text.split("\n")) - 1, 4 + len(self.sprites))

    def test_the_sheets_are_the_files_that_the_manifest_names(self):
        self.assertEqual(sorted(self.sheets), [1, 2, 3])
        self.assertEqual(sorted(os.listdir(ART)), ["sheet_1x.png", "sheet_2x.png", "sheet_3x.png", "world_art.txt"])
        for scale in SCALES:
            self.assertEqual(self.arrays[scale].shape[:2], (self.sheets[scale][2], self.sheets[scale][1]))

    def test_the_sheets_are_as_wide_as_the_rule_and_not_higher_than_a_graphics_card_holds(self):
        self.assertEqual(sheet_problems(self.arrays), [])


class ThePictures(Committed):
    def test_every_sprite_is_the_originals_size(self):
        self.assertEqual(size_problems(self.sprites), [])

    def test_every_picture_and_its_ring_lie_inside_the_sheet_and_none_overlaps(self):
        self.assertEqual(place_problems(self.arrays, self.sprites), [])

    def test_the_ring_repeats_the_edge(self):
        self.assertEqual(ring_problems(self.arrays, self.sprites), [])


class TheSet(Committed):
    def test_the_numbers_are_every_ground_tile_the_bridge_and_the_bigger_bubbles_with_variants_of_the_plain_tiles_and_the_bubbles(self):
        numbers = expected_numbers()
        self.assertEqual(set_problems(self.sprites, numbers), [])
        self.assertEqual(len({s[0] for s in self.sprites}), len(numbers))

    def test_the_count_of_the_set(self):
        self.assertEqual(len(self.sprites), len(expected_numbers()) + (VARIANTS - 1) * (len(VARIANT_TILES) + len(BIG_BUBBLES)))

    def test_the_bigger_bubbles_are_the_numbers_of_both_classes_each_32_by_32_with_sixteen_variants(self):
        self.assertEqual(sorted(BIG_BUBBLES), list(range(10641, 10651)) + list(range(20641, 20651)))
        records = [s for s in self.sprites if s[0] in BIG_BUBBLES]
        self.assertEqual([(s[0], s[1]) for s in records], [(n, v) for n in sorted(BIG_BUBBLES) for v in range(VARIANTS)])
        self.assertEqual({(s[2], s[3]) for s in records}, {(32, 32)})


class TheChecksAreNotVacuous(Committed):
    def broken(self, sprites, index, **change):
        """a copy of the sprite records with record `index` changed: number, variant, w, h or the places"""
        s = list(sprites[index])
        for key, i in (("number", 0), ("variant", 1), ("w", 2), ("h", 3), ("places", 4)):
            if key in change:
                s[i] = change[key]
        return sprites[:index] + [tuple(s)] + sprites[index + 1:]

    def test_the_syntax_is_refused_when_it_is_broken(self):
        lines = self.text.split("\n")
        for why, text in {
            "CRLF": self.text.replace("\n", "\r\n"),
            "no LF at the end": self.text[:-1],
            "an empty line at the end": self.text + "\n",
            "another header": self.text.replace("ants-world-art 1", "ants-world-art 2", 1),
            "the sheets swapped": "\n".join([lines[0], lines[2], lines[1]] + lines[3:]),
            "a sheet of another file": self.text.replace("sheet_2x.png", "sheet_two.png", 1),
            "a sprite record with a word": self.text.replace("\nsprite 112 0 ", "\nsprite 112 zero ", 1),
            "a sprite record with two spaces": self.text.replace("\nsprite 112 0 ", "\nsprite 112  0 ", 1),
            "a sprite record with a number short": self.text.replace("\nsprite 112 0 ", "\nsprite 112 ", 1),
            "a number with a leading zero": self.text.replace("\nsprite 112 0 ", "\nsprite 0112 0 ", 1),
            "another kind of record": self.text + "frame 1 2 3\n",
            "records out of order": "\n".join(lines[:4] + [lines[5], lines[4]] + lines[6:]),
            "a sprite twice": "\n".join(lines[:5] + [lines[4]] + lines[5:]),
        }.items():
            with self.assertRaises(ValueError, msg=why):
                parse_manifest(text)

    def test_a_sheet_file_that_is_not_what_the_manifest_states_is_refused(self):
        sheets = dict(self.sheets)
        sheets[1] = (sheets[1][0], sheets[1][1], sheets[1][2] + 1)
        with self.assertRaises(ValueError):
            read_sheets(sheets)
        sheets[1] = (sheets[1][0], sheets[1][1] + 1, sheets[1][2] - 1)
        with self.assertRaises(ValueError):
            read_sheets(sheets)

    def test_a_sheet_of_the_wrong_width_or_too_high_is_found(self):
        a = self.arrays[2]
        self.assertEqual(len(sheet_problems({2: a[:, :-1]})), 1)
        self.assertEqual(len(sheet_problems({1: np.zeros((MAX_SIDE + 1, WIDTH[1], 4), np.uint8)})), 1)
        self.assertEqual(len(sheet_problems({1: np.zeros((0, WIDTH[1], 4), np.uint8)})), 1)

    def test_a_size_that_is_not_the_originals_is_found(self):
        self.assertEqual(len(size_problems(self.broken(self.sprites, 0, w=self.sprites[0][2] + 1))), 1)
        self.assertEqual(len(size_problems(self.broken(self.sprites, 0, h=self.sprites[0][3] - 1))), 1)

    def test_a_picture_off_the_sheet_or_on_another_is_found(self):
        first, other = self.sprites[0], self.sprites[1]
        outside = dict(first[4])
        outside[1] = (self.arrays[1].shape[1] - first[2], first[4][1][1])
        self.assertEqual(len(place_problems(self.arrays, self.broken(self.sprites, 0, places=outside))), 1)
        outside[1] = (0, first[4][1][1])                                            # the ring would be left of the sheet
        self.assertEqual(len(place_problems(self.arrays, self.broken(self.sprites, 0, places=outside))), 1)
        onto = dict(first[4])
        onto[1] = (other[4][1][0] + 1, other[4][1][1])                             # one pixel right of another: the rings overlap
        self.assertEqual(len(place_problems(self.arrays, self.broken(self.sprites, 0, places=onto))), 1)

    def test_a_ring_that_is_not_the_edge_is_found(self):
        number, variant, w, h, places = self.sprites[10]
        x, y = places[1]
        for dy, dx in ((-1, 0), (h, w - 1), (0, -1), (h - 1, w), (-1, -1), (h, w)):      # a side of the ring and a corner of it
            a = {1: self.arrays[1].copy()}
            a[1][y + dy, x + dx] = a[1][y + dy, x + dx] ^ 1
            self.assertEqual(len(ring_problems(a, [self.sprites[10]])), 1, (dy, dx))

    def test_a_number_that_is_missing_or_not_a_ground_sprite_or_with_other_variants_is_found(self):
        numbers = expected_numbers()
        self.assertEqual(len(set_problems([s for s in self.sprites if s[0] != 112], numbers)), 1)
        self.assertEqual(len(set_problems(self.sprites, numbers | {5000})), 1)
        self.assertEqual(len(set_problems(self.sprites, numbers - {112})), 1)
        self.assertEqual(len(set_problems([s for s in self.sprites if (s[0], s[1]) != (640, 15)], numbers)), 1)
        self.assertEqual(len(set_problems(self.sprites + [(640, 16, 32, 32, {})], numbers)), 1)
        self.assertEqual(len(set_problems(self.sprites + [(112, 1, 16, 16, {})], numbers)), 1)
        self.assertEqual(len(set_problems([s for s in self.sprites if s[0] != 901 or s[1] == 0], numbers)), 1)

    def test_a_bigger_bubble_that_is_missing_short_of_a_variant_extra_or_the_wrong_size_is_found(self):
        numbers = expected_numbers()
        self.assertEqual(len(set_problems([s for s in self.sprites if s[0] != 10641], numbers)), 1)
        self.assertEqual(len(set_problems([s for s in self.sprites if s[0] != 20650], numbers)), 1)
        self.assertEqual(len(set_problems([s for s in self.sprites if (s[0], s[1]) != (20645, 15)], numbers)), 1)
        self.assertEqual(len(set_problems([s for s in self.sprites if s[0] != 10650 or s[1] == 0], numbers)), 1)
        self.assertEqual(len(set_problems(self.sprites + [(10651, 0, 32, 32, {})], numbers)), 1)
        self.assertEqual(len(set_problems(self.sprites + [(10641, 16, 32, 32, {})], numbers)), 1)
        first = next(i for i, s in enumerate(self.sprites) if s[0] == 10641)
        self.assertEqual(len(size_problems(self.broken(self.sprites, first, w=16))), 1)
        self.assertEqual(len(size_problems(self.broken(self.sprites, first, h=64))), 1)


if __name__ == "__main__":
    unittest.main()
