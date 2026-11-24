#!/usr/bin/env python3
"""The packer of tools/hud_art/pack.py (tools/hud_art/README.md): sprites that fit one page are on one page, as ever; sprites that do not fit a texture are split over more pages instead of failing (the
rule is tools/art_sheets.py's: tests/scripts/test_art_sheets.py).

It runs the packer on small made-up pictures with a small texture limit (so that no browser and no drawing tool is needed): the files that it writes, the manifest's `sheet` records of each page and the page
of each `sprite` record, that every sprite is where the manifest says in the sheet that the manifest says, that no two sprites overlap, that the derived sprites stay on the first page and that what cannot
fit the pages is refused. Needs Pillow and NumPy (CI's python has both).
"""
import os
import shutil
import sys
import tempfile
import unittest

try:
    import numpy as np
    from PIL import Image
except ImportError:
    Image = None

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools", "hud_art"))
sys.path.insert(0, os.path.join(REPO, "tools"))

COLORS = [(250, 0, 0), (0, 250, 0), (0, 0, 250), (250, 250, 0), (250, 0, 250), (0, 250, 250), (120, 60, 30), (30, 60, 120), (200, 100, 50)]


def make(count, side=10):
    """`count` sprites of one color each, `side` canvas pixels square, at 1x, 2x and 3x, and the rows that pack() takes"""
    names = ["s%d" % i for i in range(count)]
    images = {k: {n: Image.new("RGBA", (side * k, side * k), COLORS[i % len(COLORS)] + (255,)) for i, n in enumerate(names)} for k in (1, 2, 3)}
    rows = [{"name": n, "size": (side, side), "rect_960x540": [i, i, side, side], "anchor": ("left", "top")} for i, n in enumerate(names)]
    return images, rows


def read_manifest(folder):
    with open(os.path.join(folder, "hud_art.txt")) as f:
        return [line.split() for line in f.read().splitlines() if line.strip()]


@unittest.skipIf(Image is None, "Pillow and NumPy are needed")
class ThePacker(unittest.TestCase):
    def setUp(self):
        import pack
        self.pack = pack
        self.saved = (pack.LIMIT, pack.MAX_WIDTH)
        self.tmp = tempfile.mkdtemp(prefix="ants_hud_pack_")
        pack.LIMIT, pack.MAX_WIDTH = 120, 40        # a sheet of at most 40 x 40 canvas pixels at 3x: four sprites of 10 x 10 (cells of 14 with their padding) to a sheet

    def tearDown(self):
        self.pack.LIMIT, self.pack.MAX_WIDTH = self.saved
        shutil.rmtree(self.tmp, ignore_errors=True)

    def run_pack(self, count, derived=(), side=10):
        images, rows = make(count, side)
        for n in derived:                                                          # (the derived sprites are the smallest: they would be the last to be packed, and go on the second sheet if nothing kept them in the band)
            for k in (1, 2, 3):
                images[k][n] = Image.new("RGBA", (6 * k, 6 * k), (9, 9, 9, 255))
            for r in rows:
                if r["name"] == n:
                    r["size"] = (6, 6)
        files = self.pack.pack(images, rows, {}, self.tmp, derived=derived)
        return files, read_manifest(self.tmp), images

    def test_sprites_that_fit_are_on_one_page(self):
        files, manifest, _ = self.run_pack(4)
        self.assertEqual(sorted(files), ["hud_art.txt", "sheet_1x.png", "sheet_2x.png", "sheet_3x.png"])
        self.assertEqual([(w[1], w[2], w[3]) for w in manifest if w[0] == "sheet"], [("0", "1", "sheet_1x.png"), ("0", "2", "sheet_2x.png"), ("0", "3", "sheet_3x.png")])
        self.assertEqual([w[2] for w in manifest if w[0] == "sprite"], ["0"] * 4)

    def sheet_size(self, manifest, page):
        """the size of a page's 1x sheet, and the rows of its 2x and 3x sheets checked to be twice and three times it"""
        rows = {int(w[2]): (int(w[4]), int(w[5])) for w in manifest if w[0] == "sheet" and int(w[1]) == page}
        for k in (2, 3):
            self.assertEqual(rows[k], (rows[1][0] * k, rows[1][1] * k))
        return rows[1]

    def test_sprites_that_do_not_fit_go_on_a_second_page(self):
        files, manifest, images = self.run_pack(7)
        self.assertEqual(sorted(files), ["hud_art.txt", "sheet2_1x.png", "sheet2_2x.png", "sheet2_3x.png", "sheet_1x.png", "sheet_2x.png", "sheet_3x.png"])
        self.assertEqual([(w[1], w[2], w[3]) for w in manifest if w[0] == "sheet" and w[1] == "1"], [("1", "1", "sheet2_1x.png"), ("1", "2", "sheet2_2x.png"), ("1", "3", "sheet2_3x.png")])
        sprites = [w for w in manifest if w[0] == "sprite"]
        first, second = [w for w in sprites if w[2] == "0"], [w for w in sprites if w[2] == "1"]
        self.assertEqual((len(first), len(second)), (4, 3))                        # (the first page takes as many as it can)
        for page, rows in ((0, first), (1, second)):
            width, height = self.sheet_size(manifest, page)
            self.assertLessEqual(width * 3, 120)
            self.assertLessEqual(height * 3, 120)
            spots = [(int(w[5]), int(w[6]), int(w[3]), int(w[4])) for w in rows]
            for x, y, w, h in spots:
                self.assertTrue(x >= 0 and y >= 0 and x + w <= width and y + h <= height, "page %d: a sprite is outside its sheet" % page)
            for i, a in enumerate(spots):
                for b in spots[i + 1:]:
                    self.assertFalse(a[0] < b[0] + b[2] and b[0] < a[0] + a[2] and a[1] < b[1] + b[3] and b[1] < a[1] + a[3], "page %d: two sprites overlap" % page)
        # every sprite is its own picture, at its place in its own page's sheet, at every scale
        for k in (1, 2, 3):
            sheets = {"0": np.asarray(Image.open(os.path.join(self.tmp, "sheet_%dx.png" % k)).convert("RGBA")), "1": np.asarray(Image.open(os.path.join(self.tmp, "sheet2_%dx.png" % k)).convert("RGBA"))}
            for w in sprites:
                x, y = int(w[5]), int(w[6])
                got = sheets[w[2]][y * k:(y + 10) * k, x * k:(x + 10) * k]
                self.assertTrue((got == np.asarray(images[k][w[1]])).all(), "%s at %dx is not where the manifest says" % (w[1], k))

    def test_the_manifest_is_one_the_game_reads(self):
        import art_sheets
        files, manifest, _ = self.run_pack(7)
        with open(os.path.join(self.tmp, "hud_art.txt")) as f:
            lines = f.read().split("\n")[1:]
        sheets, taken = art_sheets.parse_sheet_rows([l for l in lines if l.startswith("sheet ")])
        self.assertEqual((len(sheets), taken), (6, 6))
        self.assertEqual(sorted(files), sorted([f for f, _, _ in sheets.values()] + ["hud_art.txt"]))

    def test_the_derived_sprites_stay_on_the_first_page(self):
        files, manifest, _ = self.run_pack(7, derived=("s5", "s6"))
        derived_rows = [w for w in manifest if w[0] == "derive2x"]
        self.assertEqual(len(derived_rows), 1)
        on_second = {w[1] for w in manifest if w[0] == "sprite" and w[2] == "1"}
        self.assertFalse(on_second & {"s5", "s6"})
        self.assertIn(("sheet", "1"), [(w[0], w[1]) for w in manifest])

    def test_more_pages_are_made_when_two_do_not_hold_the_sprites(self):
        files, manifest, _ = self.run_pack(9)                                      # four to a page: three pages
        self.assertEqual(sorted({w[2] for w in manifest if w[0] == "sprite"}), ["0", "1", "2"])
        self.assertIn("sheet3_3x.png", files)

    def test_what_cannot_fit_the_pages_is_refused_and_not_dropped(self):
        images, rows = make(17)                                                    # five pages of four: more than the game keeps (every picture its own: equal ones share a cell)
        for i, r in enumerate(rows):
            for k in (1, 2, 3):
                images[k][r["name"]].putpixel((0, 0), (i + 1, 7, 7, 255))
        with self.assertRaises(SystemExit):
            self.pack.pack(images, rows, {}, self.tmp)
        images, rows = make(1, side=50)                                            # a sprite that no sheet holds
        with self.assertRaises(SystemExit):
            self.pack.pack(images, rows, {}, self.tmp)


if __name__ == "__main__":
    unittest.main()
