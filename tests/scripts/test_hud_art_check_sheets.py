#!/usr/bin/env python3
"""The comparison of tools/hud_art/check_sheets.py (tools/hud_art/README.md): run by ./run_tests.sh --fast and by the CI.

The check builds the HUD art's sheets again with the generator (which needs a browser that CI does not have: it is run by hand) and compares the files with assets/hud_art/. What is held here, without a browser,
is the comparison itself: the same files pass, a file that differs in a pixel fails, the same pixels in other bytes (another compression) pass and fail with --strict, and a missing file fails. The sheets
that the repository holds are the ones that it compares. Needs Pillow and NumPy (CI's python has both).
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


def png(path, pixel=0, level=9):
    a = np.zeros((8, 8, 4), np.uint8)
    a[..., 3] = 255
    a[3, 3, 0] = pixel
    Image.fromarray(a, "RGBA").save(path, compress_level=level)


@unittest.skipIf(Image is None, "Pillow and NumPy are needed")
class TheComparison(unittest.TestCase):
    def setUp(self):
        import check_sheets
        self.check = check_sheets
        self.tmp = tempfile.mkdtemp(prefix="ants_check_sheets_")
        self.have = os.path.join(self.tmp, "have")
        self.made = os.path.join(self.tmp, "made")
        os.makedirs(self.have)
        os.makedirs(self.made)

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def fill(self, folder, pixel=0, level=9):
        with open(os.path.join(folder, "hud_art.txt"), "w") as f:
            f.write("ants-hud-art 1\n")
        for name in self.check.FILES[1:]:
            png(os.path.join(folder, name), pixel, level)

    def test_the_same_files_pass(self):
        self.fill(self.have)
        self.fill(self.made)
        code, lines = self.check.compare(self.have, self.made)
        self.assertEqual(code, 0, lines)
        self.assertTrue(all("the same bytes" in line for line in lines), lines)

    def test_a_pixel_that_differs_fails(self):
        self.fill(self.have)
        self.fill(self.made)
        png(os.path.join(self.made, "sheet_2x.png"), pixel=1)
        code, lines = self.check.compare(self.have, self.made)
        self.assertEqual(code, 1)
        self.assertIn("DIFFERENT", [line for line in lines if line.startswith("sheet_2x")][0])

    def test_a_manifest_that_differs_fails(self):
        self.fill(self.have)
        self.fill(self.made)
        with open(os.path.join(self.made, "hud_art.txt"), "a") as f:
            f.write("sprite x 1 1 0 0 0 0 left top\n")
        self.assertEqual(self.check.compare(self.have, self.made)[0], 1)

    def test_the_same_pixels_in_other_bytes_pass_unless_strict(self):
        self.fill(self.have, level=9)
        self.fill(self.made, level=0)
        self.assertNotEqual(self.check.read(os.path.join(self.have, "sheet_1x.png")), self.check.read(os.path.join(self.made, "sheet_1x.png")))
        code, lines = self.check.compare(self.have, self.made)
        self.assertEqual(code, 0, lines)
        self.assertIn("the same pixels", lines[1])
        self.assertEqual(self.check.compare(self.have, self.made, strict=True)[0], 1)

    def test_a_file_that_is_missing_fails(self):
        self.fill(self.have)
        self.fill(self.made)
        os.remove(os.path.join(self.made, "sheet_3x.png"))
        self.assertEqual(self.check.compare(self.have, self.made)[0], 1)
        os.remove(os.path.join(self.have, "sheet_1x.png"))
        self.assertEqual(self.check.compare(self.have, self.made)[0], 1)

    def test_a_second_sheet_is_compared_too(self):
        self.fill(self.have)
        self.fill(self.made)
        for name in self.check.SECOND:
            png(os.path.join(self.have, name))
            png(os.path.join(self.made, name))
        code, lines = self.check.compare(self.have, self.made)
        self.assertEqual(code, 0, lines)
        self.assertEqual(len([line for line in lines if line.startswith("sheet2_")]), 3, lines)
        png(os.path.join(self.made, "sheet2_2x.png"), pixel=1)
        self.assertEqual(self.check.compare(self.have, self.made)[0], 1)
        os.remove(os.path.join(self.made, "sheet2_3x.png"))
        self.assertEqual(self.check.compare(self.have, self.made)[0], 1)

    def test_a_second_sheet_that_the_repository_lacks_fails(self):
        self.fill(self.have)
        self.fill(self.made)
        for name in self.check.SECOND:
            png(os.path.join(self.made, name))
        code, lines = self.check.compare(self.have, self.made)
        self.assertEqual(code, 1)
        self.assertIn("no such file", [line for line in lines if line.startswith("sheet2_1x")][0])

    def test_the_repository_holds_the_four_files(self):
        folder = os.path.join(REPO, "assets", "hud_art")
        for name in self.check.FILES:
            self.assertTrue(os.path.isfile(os.path.join(folder, name)), name)


if __name__ == "__main__":
    unittest.main()
