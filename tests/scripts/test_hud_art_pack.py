#!/usr/bin/env python3
"""The packer of tools/hud_art/pack.py (tools/hud_art/README.md): sprites that fit one sheet are on one sheet, as ever; sprites that do not fit a texture are split over a second sheet instead of failing.

It runs the packer on small made-up pictures with a small texture limit (so that no browser and no drawing tool is needed): the files that it writes, the manifest's `sheet2size`, `sheet2` and `sprite2`
records, that every sprite is where the manifest says in the sheet that the manifest says, that no two sprites overlap, and that the derived sprites stay on the first sheet. Needs Pillow and NumPy (CI's python has both).
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

    def test_sprites_that_fit_are_on_one_sheet(self):
        files, manifest, _ = self.run_pack(4)
        self.assertEqual(sorted(files), ["hud_art.txt", "sheet_1x.png", "sheet_2x.png", "sheet_3x.png"])
        self.assertFalse([w for w in manifest if w[0] in ("sheet2size", "sheet2", "sprite2")])
        self.assertEqual(len([w for w in manifest if w[0] == "sprite"]), 4)

    def test_sprites_that_do_not_fit_go_on_a_second_sheet(self):
        files, manifest, images = self.run_pack(7)
        self.assertEqual(sorted(files), ["hud_art.txt", "sheet2_1x.png", "sheet2_2x.png", "sheet2_3x.png", "sheet_1x.png", "sheet_2x.png", "sheet_3x.png"])
        sheet = {w[0]: w for w in manifest if w[0] in ("sheetsize", "sheet2size")}
        self.assertEqual([w[1] for w in manifest if w[0] == "sheet2"], ["1", "2", "3"])
        self.assertEqual([w[2] for w in manifest if w[0] == "sheet2"], ["sheet2_1x.png", "sheet2_2x.png", "sheet2_3x.png"])
        first, second = [w for w in manifest if w[0] == "sprite"], [w for w in manifest if w[0] == "sprite2"]
        self.assertEqual((len(first), len(second)), (4, 3))                        # (the first sheet takes as many as it can)
        for kind, rows, size in (("sheetsize", first, sheet["sheetsize"]), ("sheet2size", second, sheet["sheet2size"])):
            width, height = int(size[1]), int(size[2])
            self.assertLessEqual(width * 3, 120)
            self.assertLessEqual(height * 3, 120)
            spots = [(int(w[4]), int(w[5]), int(w[2]), int(w[3])) for w in rows]
            for x, y, w, h in spots:
                self.assertTrue(x >= 0 and y >= 0 and x + w <= width and y + h <= height, "%s: a sprite is outside its sheet" % kind)
            for i, a in enumerate(spots):
                for b in spots[i + 1:]:
                    self.assertFalse(a[0] < b[0] + b[2] and b[0] < a[0] + a[2] and a[1] < b[1] + b[3] and b[1] < a[1] + a[3], "%s: two sprites overlap" % kind)
        # every sprite is its own picture, at its place in its own sheet, at every scale
        for k in (1, 2, 3):
            sheets = {"sprite": np.asarray(Image.open(os.path.join(self.tmp, "sheet_%dx.png" % k)).convert("RGBA")), "sprite2": np.asarray(Image.open(os.path.join(self.tmp, "sheet2_%dx.png" % k)).convert("RGBA"))}
            for w in manifest:
                if w[0] not in sheets:
                    continue
                x, y = int(w[4]), int(w[5])
                got = sheets[w[0]][y * k:(y + 10) * k, x * k:(x + 10) * k]
                self.assertTrue((got == np.asarray(images[k][w[1]])).all(), "%s at %dx is not where the manifest says" % (w[1], k))

    def test_the_derived_sprites_stay_on_the_first_sheet(self):
        files, manifest, _ = self.run_pack(7, derived=("s5", "s6"))
        derived_rows = [w for w in manifest if w[0] == "derive2x"]
        self.assertEqual(len(derived_rows), 1)
        on_second = {w[1] for w in manifest if w[0] == "sprite2"}
        self.assertFalse(on_second & {"s5", "s6"})
        self.assertIn("sheet2size", [w[0] for w in manifest])

    def test_what_cannot_fit_two_sheets_is_refused_and_not_dropped(self):
        images, rows = make(9)
        with self.assertRaises(AssertionError):
            self.pack.pack(images, rows, {}, self.tmp)


if __name__ == "__main__":
    unittest.main()
