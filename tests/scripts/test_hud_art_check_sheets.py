#!/usr/bin/env python3
"""The check of tools/hud_art/check_sheets.py (tools/hud_art/README.md): run by ./run_tests.sh --fast and by the CI.

The check builds the HUD art's sheets again with the generator (which needs a browser that CI does not have: it is run by hand) and compares the files with assets/hud_art/. The comparison itself is
tools/art_sheets.py's, the same as the world art's, and is held in tests/scripts/test_art_sheets.py; what is its own is held here: that it compares `hud_art.txt` and the sheets that this manifest names,
and that the files of the repository are the ones that it compares. Needs Pillow and NumPy (CI's python has both).
"""
import importlib.util
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
sys.path.insert(0, os.path.join(REPO, "tools"))


def load(name, *path):
    """a module by its file, under a name of its own (the tools of the HUD art and of the world art have modules of the same names: check_sheets, pack)"""
    spec = importlib.util.spec_from_file_location(name, os.path.join(REPO, *path))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@unittest.skipIf(Image is None, "Pillow and NumPy are needed")
class TheCheck(unittest.TestCase):
    def setUp(self):
        import art_sheets
        self.sheets, self.check = art_sheets, load("hud_art_check_sheets", "tools", "hud_art", "check_sheets.py")
        self.tmp = tempfile.mkdtemp(prefix="ants_hud_check_")

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def folder(self, name, pages, pixel=0):
        """a folder with a manifest that names the sheets of `pages` pages, and the sheets"""
        path = os.path.join(self.tmp, name)
        os.makedirs(path)
        sheets = {(page, k): (self.sheets.file_name("sheet", 0, page, k), 8 * k, 8 * k) for page in range(pages) for k in (1, 2, 3)}
        with open(os.path.join(path, "hud_art.txt"), "w") as f:
            f.write("ants-hud-art 2\n" + "\n".join(self.sheets.sheet_rows(sheets)) + "\n")
        for file, w, h in sheets.values():
            a = np.zeros((h, w, 4), np.uint8)
            a[..., 3] = 255
            a[3, 3, 0] = pixel
            Image.fromarray(a, "RGBA").save(os.path.join(path, file))
        return path

    def test_it_compares_the_manifest_and_the_sheets_that_the_manifest_names_page_by_page(self):
        have, made = self.folder("have", 2), self.folder("made", 2)
        code, lines = self.check.compare(have, made)
        self.assertEqual(code, 0, lines)
        self.assertEqual([line.split()[0] for line in lines], ["hud_art.txt", "sheet_1x.png", "sheet_2x.png", "sheet_3x.png", "sheet2_1x.png", "sheet2_2x.png", "sheet2_3x.png"])
        self.assertEqual(self.check.compare(have, self.folder("other", 2, pixel=1))[0], 1)

    def test_a_second_page_that_only_one_folder_has_fails(self):
        self.assertEqual(self.check.compare(self.folder("have", 1), self.folder("made", 2))[0], 1)
        self.assertEqual(self.check.compare(self.folder("more", 2), self.folder("less", 1))[0], 1)

    def test_the_repository_holds_the_files_that_it_compares(self):
        folder = os.path.join(REPO, "assets", "hud_art")
        with open(os.path.join(folder, self.check.MANIFEST), encoding="utf-8") as f:
            names = self.sheets.sheet_files(f.read())
        self.assertGreaterEqual(len(names), 3)
        for name in [self.check.MANIFEST] + names:
            self.assertTrue(os.path.isfile(os.path.join(folder, name)), name)


if __name__ == "__main__":
    unittest.main()
