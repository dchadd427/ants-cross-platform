#!/usr/bin/env python3
"""The packed world art (assets/world_art/, made by tools/world_art/make_world_art.py; tools/world_art/README.md): run by ./run_tests.sh and the CI with the other python tests of tests/scripts.

Nothing is drawn here (drawing takes about 40 seconds): the committed manifest and the committed sheets (three for each of its pages) are read, and held to the rules that the game's reader relies on:
  - TheManifest: the header, the three sheet rows of each page (real PNG files of 8-bit RGBA with the sizes they state, as wide as 1024, 2048 and 3072 pixels and at most 4096 high), the sprite records
    (eleven whole numbers each, sorted by number and variant, none twice, on a page that is there, UTF-8 with LF and nothing else);
  - ThePictures: every picture is the size of the original's sprite (asset_catalog/sprites) times the scale, lies inside its sheet with its one-pixel ring, no two pictures (rings included) overlap,
    and the ring repeats the picture's own edge pixels (a corner pixel repeats the corner, a partly transparent edge pixel its color and its opacity);
  - TheSet: the numbers of the first page (the ground, the bridge and the effects) are exactly every sprite of the ground tiles 431 to 669 of docs/chd_table4_animations.json, the bridge sprites 112 to 116, the bigger bubbles of the mud (10000 or 20000
    plus the bubble sprites 641 to 650, each the size of its sprite: 32 x 32), the four teams' bombs 298 to 305, the explosion 306 to 315 with its light (30000 plus them) and the fire wall (50000
    plus the sprites 316 to 320), and variants 1 to 15 exist exactly for the plain tiles 640 to 657, 726 to 731 and 901 and the bigger bubbles;
  - TheEffects: the bombs are solid; the explosion, its light and the fire wall are see-through by heat (no opaque pixel, more than one level of opacity, not empty but for the light of the four
    smoke-only frames), and a frame's light (the fire with the bloom round it) is never more than the bloom's share more opaque than the whole frame;
  - ThePlants: the second page holds exactly the grass and the plants (46 sprites, one picture each, the original's size), none of them on the first page, and every picture is visible and stays in
    the place of its 1x picture at 2x and 3x;
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
PAGES = (0, 1)                                                  # the pages of the committed pack: 0 the ground, the bridge and the effects, 1 the grass and the plants
FILE_NAME = {0: "sheet", 1: "plants"}                           # the first part of the files' names of each page
MAX_SIDE = 4096
RING = 1
VARIANTS = 16
VARIANT_TILES = set(range(640, 658)) | set(range(726, 732)) | {901}
GROUND_TILES = (431, 669)
BRIDGE = set(range(112, 117))
BIG_BUBBLES = {base + sprite for base in (10000, 20000) for sprite in range(641, 651)}      # a bigger bubble's number: 10000 (medium) or 20000 (large) plus the original's bubble sprite
BOMBS = set(range(298, 306))                                    # the four teams' bombs, two frames each
EXPLOSION = set(range(306, 316))                                # the bomb's explosion, ten frames
EXPLOSION_LIGHT = {30000 + n for n in EXPLOSION}                # each frame's fire alone, for a pass that adds it to the ground as light
FIRE_WALL = {50000 + n for n in range(316, 321)}                # the fire wall's five frames (316 to 319 are also the Fire ant's own, which keep the original's pictures, hence 50000 plus)
EFFECTS = BOMBS | EXPLOSION | EXPLOSION_LIGHT | FIRE_WALL
SEE_THROUGH = EXPLOSION | EXPLOSION_LIGHT | FIRE_WALL           # drawn as light: opacity by heat
NO_FIRE = {30312, 30313, 30314, 30315}                          # the last four frames of the blast are smoke only: their light is empty
PLANTS = set(range(479, 487)) | set(range(573, 602)) | set(range(2779, 2788))        # leaves, sticks and the mushroom (479 to 486), the clovers, the flowers, the plant, the ivy and the animated flower's parts (573 to 601), the grass (2779 to 2787)
NUMBER = re.compile(r"0|[1-9][0-9]*")
BLOOM = 64                                                      # how much more opaque (of 255) a frame's light may be than the frame itself: the glow round the hot parts of the fire reaches a little past it
PLACE_TOLERANCE = 4                                             # world pixels: how far a plant's visible edge at 2x and 3x may lie from its 1x picture's


def parse_manifest(text):
    """(sheets {(page, scale): (file, width, height)}, sprites [(number, variant, page, w, h, {scale: (x, y)})]); ValueError with the reason when the text is not the format"""
    if "\r" in text or not text.endswith("\n") or text.endswith("\n\n"):
        raise ValueError("one record a line, every line ends with LF, no empty line")
    lines = text[:-1].split("\n")
    if lines[0] != "ants-world-art 2":
        raise ValueError("the first line is the header")
    sheets = {}
    at = 1
    while at < len(lines) and lines[at].startswith("sheet "):
        page, scale = divmod(len(sheets), 3)
        scale += 1
        parts = lines[at].split(" ")
        if len(parts) != 6 or parts[1] != str(page) or parts[2] != str(scale) or not re.fullmatch(r"[a-z0-9]+_%dx\.png" % scale, parts[3]) or not all(NUMBER.fullmatch(p) for p in parts[4:]):
            raise ValueError("line %d is the sheet row of page %d scale %d" % (at + 1, page, scale))
        sheets[(page, scale)] = (parts[3], int(parts[4]), int(parts[5]))
        at += 1
    if not sheets or len(sheets) % 3:
        raise ValueError("every page has the three sheet rows, for scale 1, 2 and 3")
    sprites = []
    for line in lines[at:]:
        parts = line.split(" ")
        if len(parts) != 12 or parts[0] != "sprite" or not all(NUMBER.fullmatch(p) for p in parts[1:]):
            raise ValueError("not a sprite record: %r" % line)
        n = [int(p) for p in parts[1:]]
        if n[2] * 3 >= len(sheets):
            raise ValueError("a sprite on a page that the manifest does not have: %r" % line)
        sprites.append((n[0], n[1], n[2], n[3], n[4], {s: (n[5 + 2 * i], n[6 + 2 * i]) for i, s in enumerate(SCALES)}))
    keys = [(s[0], s[1]) for s in sprites]
    if keys != sorted(set(keys)):
        raise ValueError("the sprite records are sorted by number and variant, none twice (on any page)")
    return sheets, sprites


def read_sheets(sheets, folder=ART):
    """{(page, scale): RGBA array of the sheet file}; the files must be 8-bit RGBA PNGs of the size that the manifest states"""
    out = {}
    for key, (name, w, h) in sheets.items():
        with Image.open(os.path.join(folder, name)) as im:
            if im.format != "PNG" or im.mode != "RGBA" or im.size != (w, h):
                raise ValueError("%s is %s %s %s, the manifest says an RGBA PNG of %d x %d" % (name, im.format, im.mode, im.size, w, h))
            out[key] = np.asarray(im).copy()
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
    for (page, scale), a in arrays.items():
        if a.shape[1] != WIDTH[scale]:
            out.append("the %dx sheet of page %d is %d pixels wide, not %d" % (scale, page, a.shape[1], WIDTH[scale]))
        if not 0 < a.shape[0] <= MAX_SIDE:
            out.append("the %dx sheet of page %d is %d pixels high (1 to %d)" % (scale, page, a.shape[0], MAX_SIDE))
    return out


def size_problems(sprites):
    """a sprite's size is the original's"""
    return ["sprite %d is %d x %d, the original's is %d x %d" % ((s[0], s[3], s[4]) + original_size(s[0])) for s in sprites if (s[3], s[4]) != original_size(s[0])]


def place_problems(arrays, sprites):
    """every picture with its ring lies inside the sheet of its page at its scale and no two of them share a pixel"""
    out = []
    for (page, scale), a in arrays.items():
        cover = np.zeros(a.shape[:2], np.uint8)
        for number, variant, on, w, h, places in sprites:
            if on != page:
                continue
            x, y = places[scale]
            x0, y0, x1, y1 = x - RING, y - RING, x + w * scale + RING, y + h * scale + RING
            if x0 < 0 or y0 < 0 or x1 > a.shape[1] or y1 > a.shape[0]:
                out.append("sprite %d variant %d at %dx is not inside its sheet" % (number, variant, scale))
                continue
            cover[y0:y1, x0:x1] += 1
        if cover.max() > 1:
            out.append("the pictures of the %dx sheet of page %d overlap (%d pixels)" % (scale, page, (cover > 1).sum()))
    return out


def ring_problems(arrays, sprites):
    """the ring is the picture's own edge repeated: a cell is the picture padded by one pixel of its edge"""
    out = []
    for (page, scale), a in arrays.items():
        for number, variant, on, w, h, places in sprites:
            if on != page:
                continue
            x, y = places[scale]
            picture = a[y:y + h * scale, x:x + w * scale]
            cell = a[y - RING:y + h * scale + RING, x - RING:x + w * scale + RING]
            if not np.array_equal(cell, np.pad(picture, ((RING, RING), (RING, RING), (0, 0)), mode="edge")):
                out.append("the ring of sprite %d variant %d at %dx is not its edge" % (number, variant, scale))
    return out


def crops(arrays, sprites, numbers):
    """(number, scale, RGBA picture without its ring) of every picture of these numbers in the sheets given"""
    for number, variant, page, w, h, places in sprites:
        if number in numbers:
            for (on, scale), a in arrays.items():
                if on == page:
                    x, y = places[scale]
                    yield number, scale, a[y:y + h * scale, x:x + w * scale]


def alpha_problems(arrays, sprites):
    """the bombs are solid (more than half of their visible pixels opaque); the explosion, its light and the fire wall are see-through by heat: not one opaque pixel, more than one level of opacity,
    and not empty (the light of the four smoke-only frames is the one that is)"""
    out = []
    for number, scale, pic in crops(arrays, sprites, BOMBS | SEE_THROUGH):
        alpha = pic[..., 3]
        visible = alpha > 0
        if number in BOMBS:
            if (alpha == 255).sum() * 2 <= visible.sum():
                out.append("the bomb %d at %dx is not solid" % (number, scale))
        elif not visible.any():
            if number not in NO_FIRE:
                out.append("sprite %d at %dx is empty" % (number, scale))
        elif alpha.max() == 255:
            out.append("sprite %d at %dx has an opaque pixel: it is drawn as see-through by heat" % (number, scale))
        elif len(np.unique(alpha[visible])) < 2:
            out.append("sprite %d at %dx has one level of opacity" % (number, scale))
    return out


def light_problems(arrays, sprites):
    """a frame's light (the fire and the bloom round its hot parts) is never more than BLOOM more opaque than the whole frame, pixel for pixel"""
    out = []
    whole = {(n, s): pic for n, s, pic in crops(arrays, sprites, EXPLOSION)}
    for number, scale, pic in crops(arrays, sprites, EXPLOSION_LIGHT):
        other = whole.get((number - 30000, scale))
        if other is None or other.shape != pic.shape:
            out.append("the light %d at %dx has no whole picture of its size" % (number, scale))
        elif (pic[..., 3].astype(int) > other[..., 3].astype(int) + BLOOM).any():
            out.append("the light %d at %dx is more opaque than its frame and the bloom" % (number, scale))
    return out


def plant_problems(arrays, sprites):
    """every picture of the grass and the plants shows something at each scale, and its visible part (the pixels that are not clear) lies where its 1x picture's does, to within PLACE_TOLERANCE world pixels:
    a scale that is drawn for another place would move the plant when the window is enlarged"""
    out = []
    for number, scale, pic in crops(arrays, [s for s in sprites if s[2] == 1], PLANTS):
        if not (pic[..., 3] > 0).any():
            out.append("the plant %d at %dx is empty" % (number, scale))
    edges = {}
    for number, scale, pic in crops(arrays, [s for s in sprites if s[2] == 1], PLANTS):
        rows, cols = np.nonzero(pic[..., 3] > 0)
        if rows.size:
            edges[(number, scale)] = (cols.min() / scale, (cols.max() + 1) / scale, rows.min() / scale, (rows.max() + 1) / scale)
    for number in sorted(PLANTS):
        one = edges.get((number, 1))
        for scale in (2, 3):
            other = edges.get((number, scale))
            if one is not None and other is not None and max(abs(a - b) for a, b in zip(one, other)) > PLACE_TOLERANCE:
                out.append("the plant %d at %dx is not where its 1x picture is" % (number, scale))
    return out


def expected_numbers():
    with open(TABLE, encoding="utf-8") as f:
        table = json.load(f)
    assert table[GROUND_TILES[0]]["entry_index"] == GROUND_TILES[0] and table[GROUND_TILES[1]]["entry_index"] == GROUND_TILES[1]
    ground = {f["sprite_index"] for entry in table[GROUND_TILES[0]:GROUND_TILES[1] + 1] for item in entry["subitems"] for f in item["frames"]}
    return ground | BRIDGE | BIG_BUBBLES | EFFECTS


def set_problems(sprites, numbers):
    """the numbers drawn are exactly `numbers`, and a number has variants 0 to 15 if it is a plain tile or a bigger bubble and only 0 if it is not"""
    out = []
    found = {}
    for number, variant, _, _, _, _ in sprites:
        found.setdefault(number, set()).add(variant)
    for number in sorted(numbers - set(found)):
        out.append("sprite %d is missing" % number)
    for number in sorted(set(found) - numbers):
        out.append("sprite %d is not a sprite of the pack" % number)
    for number, variants in sorted(found.items()):
        want = set(range(VARIANTS)) if number in VARIANT_TILES | BIG_BUBBLES else {0}
        if variants != want:
            out.append("sprite %d has the variants %s, not %s" % (number, sorted(variants), sorted(want)))
    return out


@unittest.skipUnless(np and Image, "numpy and Pillow are needed")
class Committed(unittest.TestCase):
    """the committed files, read once; `first` and `plants` are the sprite records of the first page and of the second"""

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
        cls.first = [s for s in cls.sprites if s[2] == 0]
        cls.plants = [s for s in cls.sprites if s[2] == 1]


class TheManifest(Committed):
    def test_the_text_is_in_the_format(self):
        self.assertEqual(self.raw, self.text.encode("utf-8"))
        self.assertTrue(self.text.startswith("ants-world-art 2\nsheet 0 1 sheet_1x.png "))
        self.assertEqual(self.text.count("\nsheet "), 3 * len(PAGES))
        self.assertEqual(self.text.count("\nsprite "), len(self.sprites))
        self.assertEqual(len(self.text.split("\n")) - 1, 1 + 3 * len(PAGES) + len(self.sprites))

    def test_the_sheets_are_the_files_that_the_manifest_names(self):
        self.assertEqual(sorted(self.sheets), [(page, scale) for page in PAGES for scale in SCALES])
        names = ["%s_%dx.png" % (FILE_NAME[page], scale) for page in PAGES for scale in SCALES]
        self.assertEqual(sorted(os.listdir(ART)), sorted(names + ["world_art.txt"]))
        self.assertEqual([self.sheets[key][0] for key in sorted(self.sheets)], sorted(names, key=lambda n: (n.split("_")[0] != "sheet", n)))
        for key in self.sheets:
            self.assertEqual(self.arrays[key].shape[:2], (self.sheets[key][2], self.sheets[key][1]))

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
        self.assertEqual(set_problems(self.first, numbers), [])
        self.assertEqual(len({s[0] for s in self.first}), len(numbers))

    def test_the_count_of_the_set(self):
        self.assertEqual(len(self.first), len(expected_numbers()) + (VARIANTS - 1) * (len(VARIANT_TILES) + len(BIG_BUBBLES)))
        self.assertEqual(len(self.first), 1020)

    def test_the_bigger_bubbles_are_the_numbers_of_both_classes_each_32_by_32_with_sixteen_variants(self):
        self.assertEqual(sorted(BIG_BUBBLES), list(range(10641, 10651)) + list(range(20641, 20651)))
        records = [s for s in self.sprites if s[0] in BIG_BUBBLES]
        self.assertEqual([(s[0], s[1]) for s in records], [(n, v) for n in sorted(BIG_BUBBLES) for v in range(VARIANTS)])
        self.assertEqual({(s[3], s[4]) for s in records}, {(32, 32)})


class TheEffects(Committed):
    def test_the_bombs_are_solid_and_the_explosion_its_light_and_the_fire_wall_are_see_through(self):
        self.assertEqual(alpha_problems(self.arrays, self.sprites), [])

    def test_the_light_is_never_more_opaque_than_the_frame_it_belongs_to_and_its_bloom(self):
        self.assertEqual(light_problems(self.arrays, self.sprites), [])

    def test_the_effects_are_each_one_picture_of_the_originals_size(self):
        records = [s for s in self.sprites if s[0] in EFFECTS]
        self.assertEqual([(s[0], s[1]) for s in records], [(n, 0) for n in sorted(EFFECTS)])
        self.assertEqual(len(records), 33)
        self.assertEqual({(s[3], s[4]) for s in records if s[0] in BOMBS}, {(12, 24)})
        for s in records:
            self.assertEqual((s[3], s[4]), original_size(s[0]))
            self.assertEqual(s[2], 0)


class ThePlants(Committed):
    def test_the_second_page_is_the_grass_and_the_plants_each_one_picture(self):
        self.assertEqual([(s[0], s[1]) for s in self.plants], [(n, 0) for n in sorted(PLANTS)])
        self.assertEqual(len(PLANTS), 46)
        self.assertEqual(size_problems(self.plants), [])
        self.assertEqual(PLANTS & {s[0] for s in self.first}, set())

    def test_every_picture_is_there_and_stays_where_its_1x_picture_is(self):
        self.assertEqual(plant_problems(self.arrays, self.sprites), [])


class TheNumbersAreOnePageEach(Committed):
    def test_no_number_is_on_two_pages_and_the_pages_are_in_order(self):
        keys = [(s[0], s[1]) for s in self.sprites]
        self.assertEqual(len(keys), len(set(keys)))
        self.assertEqual(sorted({s[2] for s in self.sprites}), list(PAGES))


class TheChecksAreNotVacuous(Committed):
    def broken(self, sprites, index, **change):
        """a copy of the sprite records with record `index` changed: number, variant, page, w, h or the places"""
        s = list(sprites[index])
        for key, i in (("number", 0), ("variant", 1), ("page", 2), ("w", 3), ("h", 4), ("places", 5)):
            if key in change:
                s[i] = change[key]
        return sprites[:index] + [tuple(s)] + sprites[index + 1:]

    def test_the_syntax_is_refused_when_it_is_broken(self):
        lines = self.text.split("\n")
        for why, text in {
            "CRLF": self.text.replace("\n", "\r\n"),
            "no LF at the end": self.text[:-1],
            "an empty line at the end": self.text + "\n",
            "another header": self.text.replace("ants-world-art 2", "ants-world-art 1", 1),
            "a page short of a sheet row": "\n".join(lines[:6] + lines[7:]),
            "a sprite on a page that is not there": self.text.replace("\nsprite 112 0 0 ", "\nsprite 112 0 2 ", 1),
            "a sheet row for a page out of order": self.text.replace("\nsheet 1 1 ", "\nsheet 2 1 ", 1),
            "a number on two pages": self.text.replace("\nsprite 479 0 1 ", "\nsprite 112 0 1 ", 1),
            "the sheets swapped": "\n".join([lines[0], lines[2], lines[1]] + lines[3:]),
            "a sheet of another file": self.text.replace("sheet_2x.png", "sheet_two.png", 1),
            "a sprite record with a word": self.text.replace("\nsprite 112 0 ", "\nsprite 112 zero ", 1),
            "a sprite record with two spaces": self.text.replace("\nsprite 112 0 ", "\nsprite 112  0 ", 1),
            "a sprite record with a number short": self.text.replace("\nsprite 112 0 ", "\nsprite 112 ", 1),
            "a number with a leading zero": self.text.replace("\nsprite 112 0 ", "\nsprite 0112 0 ", 1),
            "another kind of record": self.text + "frame 1 2 3\n",
            "records out of order": "\n".join(lines[:7] + [lines[8], lines[7]] + lines[9:]),
            "a sprite twice": "\n".join(lines[:8] + [lines[7]] + lines[8:]),
        }.items():
            with self.assertRaises(ValueError, msg=why):
                parse_manifest(text)

    def test_a_sheet_file_that_is_not_what_the_manifest_states_is_refused(self):
        for key in ((0, 1), (1, 3)):
            sheets = dict(self.sheets)
            sheets[key] = (sheets[key][0], sheets[key][1], sheets[key][2] + 1)
            with self.assertRaises(ValueError):
                read_sheets(sheets)
            sheets[key] = (sheets[key][0], sheets[key][1] + 1, sheets[key][2] - 1)
            with self.assertRaises(ValueError):
                read_sheets(sheets)

    def test_a_sheet_of_the_wrong_width_or_too_high_is_found(self):
        a = self.arrays[(1, 2)]
        self.assertEqual(len(sheet_problems({(1, 2): a[:, :-1]})), 1)
        self.assertEqual(len(sheet_problems({(0, 1): np.zeros((MAX_SIDE + 1, WIDTH[1], 4), np.uint8)})), 1)
        self.assertEqual(len(sheet_problems({(1, 1): np.zeros((0, WIDTH[1], 4), np.uint8)})), 1)

    def test_a_size_that_is_not_the_originals_is_found(self):
        self.assertEqual(len(size_problems(self.broken(self.sprites, 0, w=self.sprites[0][3] + 1))), 1)
        self.assertEqual(len(size_problems(self.broken(self.sprites, 0, h=self.sprites[0][4] - 1))), 1)

    def test_a_picture_off_the_sheet_or_on_another_is_found(self):
        first, other = self.sprites[0], self.sprites[1]
        outside = dict(first[5])
        outside[1] = (self.arrays[(0, 1)].shape[1] - first[3], first[5][1][1])
        self.assertEqual(len(place_problems(self.arrays, self.broken(self.sprites, 0, places=outside))), 1)
        outside[1] = (0, first[5][1][1])                                            # the ring would be left of the sheet
        self.assertEqual(len(place_problems(self.arrays, self.broken(self.sprites, 0, places=outside))), 1)
        onto = dict(first[5])
        onto[1] = (other[5][1][0] + 1, other[5][1][1])                             # one pixel right of another: the rings overlap
        self.assertEqual(len(place_problems(self.arrays, self.broken(self.sprites, 0, places=onto))), 1)
        narrow = self.plants[0]                                                    # a picture of the second page placed as far right as the first page's sheet is wide: outside the second's
        self.assertEqual(len(place_problems(self.arrays, self.broken(self.sprites, self.sprites.index(narrow), places={1: (WIDTH[1] - 1, 1), 2: narrow[5][2], 3: narrow[5][3]}))), 1)
        moved = self.broken(self.sprites, self.sprites.index(narrow), page=0)      # the same places on another page overlap the pictures that are there
        self.assertGreaterEqual(len(place_problems(self.arrays, moved)), 1)

    def test_a_ring_that_is_not_the_edge_is_found(self):
        number, variant, page, w, h, places = self.sprites[10]
        x, y = places[1]
        for dy, dx in ((-1, 0), (h, w - 1), (0, -1), (h - 1, w), (-1, -1), (h, w)):      # a side of the ring and a corner of it
            a = {(0, 1): self.arrays[(0, 1)].copy()}
            a[(0, 1)][y + dy, x + dx] = a[(0, 1)][y + dy, x + dx] ^ 1
            self.assertEqual(len(ring_problems(a, [self.sprites[10]])), 1, (dy, dx))
        record = self.plants[0]                                                    # and on the second page
        x, y = record[5][3]
        a = {(1, 3): self.arrays[(1, 3)].copy()}
        a[(1, 3)][y - RING, x] ^= 1
        self.assertEqual(len(ring_problems(a, [record])), 1)

    def only(self, scale, number):
        """the sheet of a scale, copied, and the record of a number: for a copy of the data that is broken in one picture"""
        record = next(s for s in self.sprites if s[0] == number)
        return {(record[2], scale): self.arrays[(record[2], scale)].copy()}, record

    def picture_of(self, arrays, scale, record):
        x, y = record[5][scale]
        return arrays[(record[2], scale)][y:y + record[4] * scale, x:x + record[3] * scale]

    def test_an_opaque_or_flat_or_empty_copy_of_a_see_through_picture_or_a_see_through_bomb_is_found(self):
        for number, scale in ((50316, 1), (50320, 3), (306, 2), (309, 3), (30306, 1), (30311, 3)):
            a, record = self.only(scale, number)
            pic = self.picture_of(a, scale, record)
            self.assertEqual(alpha_problems(a, [record]), [], number)
            kept = pic.copy()
            pic[..., 3] = np.where(kept[..., 3] > 0, 255, 0)                                   # every visible pixel opaque
            self.assertEqual(len(alpha_problems(a, [record])), 1, "opaque %d" % number)
            pic[..., 3] = np.where(kept[..., 3] >= 128, 255, 0)                                # a cut-out: the original's kind of alpha
            self.assertEqual(len(alpha_problems(a, [record])), 1, "cut out %d" % number)
            pic[..., 3] = np.where(kept[..., 3] > 0, 128, 0)                                   # one level of opacity
            self.assertEqual(len(alpha_problems(a, [record])), 1, "flat %d" % number)
            pic[...] = 0                                                                       # nothing at all
            self.assertEqual(len(alpha_problems(a, [record])), 1, "empty %d" % number)
        a, record = self.only(1, 298)
        pic = self.picture_of(a, 1, record)
        pic[..., 3] = pic[..., 3] // 2                                                         # a bomb you can see through
        self.assertEqual(len(alpha_problems(a, [record])), 1)

    def test_the_light_of_a_smoke_only_frame_may_be_empty_and_no_other_picture_may(self):
        for number in sorted(NO_FIRE):
            a, record = self.only(1, number)
            self.assertEqual(alpha_problems(a, [record]), [], number)
        a, record = self.only(1, 30310)
        self.picture_of(a, 1, record)[...] = 0
        self.assertEqual(len(alpha_problems(a, [record])), 1)

    def test_a_light_that_is_more_opaque_than_its_frame_and_the_bloom_or_has_no_frame_is_found(self):
        wholes = [s for s in self.sprites if s[0] in (306, 30306)]
        arrays = {(0, 3): self.arrays[(0, 3)].copy()}
        self.assertEqual(light_problems(arrays, wholes), [])
        light = next(s for s in wholes if s[0] == 30306)
        pic = self.picture_of(arrays, 3, light)
        whole = self.picture_of(arrays, 3, next(s for s in wholes if s[0] == 306))
        y, x = np.argwhere((pic[..., 3] > 0) & (whole[..., 3] <= 180))[0]
        pic[y, x, 3] = int(whole[y, x, 3]) + BLOOM          # as opaque as the frame and the whole bloom: allowed
        self.assertEqual(light_problems(arrays, wholes), [])
        pic[y, x, 3] = int(whole[y, x, 3]) + BLOOM + 1      # one level more: found
        self.assertEqual(len(light_problems(arrays, wholes)), 1)
        self.assertEqual(len(light_problems(arrays, [light])), 1)                              # its frame is not there

    def test_the_ring_of_a_partly_transparent_edge_repeats_its_color_and_its_opacity(self):
        record = next(s for s in self.sprites if s[0] == 50316)
        x, y = record[5][3]
        arrays = {(0, 3): self.arrays[(0, 3)].copy()}
        edge = arrays[(0, 3)][y:y + record[4] * 3, x:x + record[3] * 3][0]                     # the picture's top row
        column = int(np.argmax((edge[:, 3] > 0) & (edge[:, 3] < 255)))
        self.assertTrue(0 < edge[column, 3] < 255)                                             # a partly transparent edge pixel
        self.assertEqual(ring_problems(arrays, [record]), [])
        arrays[(0, 3)][y - RING, x + column, 3] ^= 1                                           # the ring above it, one level of opacity apart
        self.assertEqual(len(ring_problems(arrays, [record])), 1)
        arrays[(0, 3)][y - RING, x + column, 3] ^= 1
        arrays[(0, 3)][y - RING, x + column, 0] ^= 1                                           # or one level of its red
        self.assertEqual(len(ring_problems(arrays, [record])), 1)

    def test_a_number_that_is_missing_or_not_a_ground_sprite_or_with_other_variants_is_found(self):
        numbers = expected_numbers()
        self.assertEqual(len(set_problems([s for s in self.first if s[0] != 112], numbers)), 1)
        self.assertEqual(len(set_problems(self.first, numbers | {5000})), 1)
        self.assertEqual(len(set_problems(self.first, numbers - {112})), 1)
        self.assertEqual(len(set_problems([s for s in self.first if (s[0], s[1]) != (640, 15)], numbers)), 1)
        self.assertEqual(len(set_problems(self.first + [(640, 16, 0, 32, 32, {})], numbers)), 1)
        self.assertEqual(len(set_problems(self.first + [(112, 1, 0, 16, 16, {})], numbers)), 1)
        self.assertEqual(len(set_problems([s for s in self.first if s[0] != 901 or s[1] == 0], numbers)), 1)

    def test_a_bigger_bubble_that_is_missing_short_of_a_variant_extra_or_the_wrong_size_is_found(self):
        numbers = expected_numbers()
        self.assertEqual(len(set_problems([s for s in self.first if s[0] != 10641], numbers)), 1)
        self.assertEqual(len(set_problems([s for s in self.first if s[0] != 20650], numbers)), 1)
        self.assertEqual(len(set_problems([s for s in self.first if (s[0], s[1]) != (20645, 15)], numbers)), 1)
        self.assertEqual(len(set_problems([s for s in self.first if s[0] != 10650 or s[1] == 0], numbers)), 1)
        self.assertEqual(len(set_problems(self.first + [(10651, 0, 0, 32, 32, {})], numbers)), 1)
        self.assertEqual(len(set_problems(self.first + [(10641, 16, 0, 32, 32, {})], numbers)), 1)
        first = next(i for i, s in enumerate(self.first) if s[0] == 10641)
        self.assertEqual(len(size_problems(self.broken(self.first, first, w=16))), 1)
        self.assertEqual(len(size_problems(self.broken(self.first, first, h=64))), 1)

    def test_an_effect_that_is_missing_extra_with_variants_or_the_wrong_size_is_found(self):
        numbers = expected_numbers()
        for number in (298, 305, 306, 315, 30306, 30315, 50316, 50320):
            self.assertEqual(len(set_problems([s for s in self.first if s[0] != number], numbers)), 1, number)
        self.assertEqual(len(set_problems(self.first + [(316, 0, 0, 29, 33, {})], numbers)), 1)        # the original's own fire sprite is not a number of the pack
        self.assertEqual(len(set_problems(self.first + [(40306, 0, 0, 29, 29, {})], numbers)), 1)
        self.assertEqual(len(set_problems(self.first + [(50316, 1, 0, 29, 33, {})], numbers)), 1)
        for number in (298, 30306, 50320):
            first = next(i for i, s in enumerate(self.first) if s[0] == number)
            self.assertEqual(len(size_problems(self.broken(self.first, first, w=self.first[first][3] + 1))), 1)
            self.assertEqual(len(size_problems(self.broken(self.first, first, h=self.first[first][4] - 1))), 1)

    def test_a_plant_that_is_empty_or_drawn_for_another_place_is_found(self):
        record = next(s for s in self.plants if s[0] == 2779)
        arrays = {(1, scale): self.arrays[(1, scale)].copy() for scale in SCALES}
        self.assertEqual(plant_problems(arrays, [record]), [])
        pic = self.picture_of(arrays, 3, record)
        content = pic.copy()
        pic[:, 6:] = content[:, :-6]                                                      # two world pixels to the right: within what the scales may differ
        pic[:, :6] = 0
        self.assertEqual(plant_problems(arrays, [record]), [])
        pic[...] = 0
        pic[:, 18:] = content[:, :-18]                                                    # six world pixels to the right: not the same place
        self.assertEqual(len(plant_problems(arrays, [record])), 1)
        pic[...] = 0                                                                      # nothing at all
        self.assertEqual(len(plant_problems(arrays, [record])), 1)

    def test_a_plant_that_is_missing_or_extra_or_on_the_first_page_is_found(self):
        wanted = PLANTS
        self.assertEqual({s[0] for s in self.plants}, wanted)
        self.assertEqual(len(size_problems(self.broken(self.plants, 0, w=self.plants[0][3] + 1))), 1)
        self.assertEqual(len(size_problems(self.broken(self.plants, 0, h=self.plants[0][4] - 1))), 1)
        self.assertNotEqual({s[0] for s in self.plants[1:]}, wanted)
        self.assertNotEqual({s[0] for s in self.plants} | {5000}, wanted)
        self.assertEqual(len(set_problems(self.first + self.plants[:1], expected_numbers())), 1)             # a plant among the first page's pictures is not one of its numbers


if __name__ == "__main__":
    unittest.main()
