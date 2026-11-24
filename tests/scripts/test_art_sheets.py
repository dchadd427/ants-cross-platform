#!/usr/bin/env python3
"""The sheets of the packs of new pictures, in Python (tools/art_sheets.py; the format and the game's reader are include/ants_app/art_sheets.hpp, tested in test_art_sheets of the application tests):
the one home of the `sheet` records, of the rule that splits a pack over pages and of the comparison that the checks of the generators use (tools/hud_art/check_sheets.py, tools/world_art/check_sheets.py).

  - TheRecords: the records written are the records read back, and every way to break one is refused the way the game's reader refuses it; the file names of the pages of a pack;
  - ThePages: a page is filled as far as it goes and the rest go on the next, in order and with nothing lost; what cannot be held is an error;
  - TheComparison: the same files pass, a pixel that differs fails, the same pixels in other bytes (another compression) pass and fail with strict, a missing file fails, a page that only one of the
    folders has is compared, and a generator that makes some pages only compares those;
  - TheCommittedPacks: the manifests that the repository holds read with the shared parser and name sheet files that are there, of the size they state.
Needs Pillow and NumPy (CI's python has both).
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
sys.path.insert(0, os.path.join(REPO, "tools"))

import art_sheets  # noqa: E402

SHEETS = {(0, 1): ("sheet_1x.png", 8, 8), (0, 2): ("sheet_2x.png", 16, 16), (0, 3): ("sheet_3x.png", 24, 24),
          (1, 1): ("more_1x.png", 4, 4), (1, 2): ("more_2x.png", 8, 8), (1, 3): ("more_3x.png", 12, 12)}


class TheRecords(unittest.TestCase):
    def test_what_is_written_is_what_is_read(self):
        rows = art_sheets.sheet_rows(SHEETS)
        self.assertEqual(rows[0], "sheet 0 1 sheet_1x.png 8 8")
        self.assertEqual(len(rows), 6)
        self.assertEqual(art_sheets.parse_sheet_rows(rows), (SHEETS, 6))
        self.assertEqual(art_sheets.parse_sheet_rows(rows + ["sprite a 0 1 1 0 0 0 0 left top"])[1], 6)       # (it stops at the first record that is not a sheet's)

    def test_the_writer_refuses_pages_that_are_not_whole_or_not_in_order(self):
        for why, sheets in {
            "no page": {},
            "a page without a scale": {k: v for k, v in SHEETS.items() if k != (1, 3)},
            "a page that is missing in the middle": {(0, 1): SHEETS[(0, 1)], (0, 2): SHEETS[(0, 2)], (0, 3): SHEETS[(0, 3)], (2, 1): SHEETS[(1, 1)], (2, 2): SHEETS[(1, 2)], (2, 3): SHEETS[(1, 3)]},
            "more pages than the game keeps": {(p, s): ("f%d_%d.png" % (p, s), 4, 4) for p in range(art_sheets.MAX_PAGES + 1) for s in (1, 2, 3)},
        }.items():
            with self.assertRaises(ValueError, msg=why):
                art_sheets.sheet_rows(sheets)

    def test_the_reader_refuses_what_the_games_reader_refuses(self):
        good = art_sheets.sheet_rows(SHEETS)
        for why, rows in {
            "scales out of order": [good[1], good[0]] + good[2:],
            "a page out of order": good[3:] + good[:3],
            "a record twice": good[:1] + good,
            "only some of a page": good[:4],
            "no sheet at all": [],
            "four fields": ["sheet 0 1 sheet_1x.png 8"],
            "a word for a number": [good[0].replace(" 8 8", " eight 8")] + good[1:],
            "a leading zero": [good[0].replace("sheet 0 1", "sheet 00 1")] + good[1:],
            "a leading zero in a size": [good[0].replace(" 8 8", " 08 8")] + good[1:],
            "a file in another folder": [good[0].replace("sheet_1x.png", "../sheet_1x.png")] + good[1:],
            "a file in a sub folder": [good[0].replace("sheet_1x.png", "sub/sheet_1x.png")] + good[1:],
            "a file with a back slash": [good[0].replace("sheet_1x.png", "sub\\sheet_1x.png")] + good[1:],
            "no size": [good[0].replace(" 8 8", " 0 8")] + good[1:],
            "a size beyond what a sheet may be": [good[0].replace(" 8 8", " 16385 8")] + good[1:],
        }.items():
            with self.assertRaises(ValueError, msg=why):
                art_sheets.parse_sheet_rows(rows)

    def test_the_file_names_of_the_pages_of_a_pack(self):
        self.assertEqual([art_sheets.file_name("sheet", 0, p, 2) for p in (0, 1, 2)], ["sheet_2x.png", "sheet2_2x.png", "sheet3_2x.png"])
        self.assertEqual([art_sheets.file_name("plants", 1, p, 3) for p in (1, 2)], ["plants_3x.png", "plants2_3x.png"])

    def test_the_sheet_files_a_manifest_names(self):
        text = "ants-x 2\n" + "\n".join(art_sheets.sheet_rows(SHEETS)) + "\nsprite a 0 1 1 0 0 0 0 left top\n"
        self.assertEqual(art_sheets.sheet_files(text), [f for f, _, _ in (SHEETS[k] for k in sorted(SHEETS))])
        self.assertEqual(art_sheets.sheet_files(text, {1}), ["more_1x.png", "more_2x.png", "more_3x.png"])
        self.assertEqual(art_sheets.sheet_files(""), [])


class ThePages(unittest.TestCase):
    @staticmethod
    def fits(capacity, first_fixed=0):
        """pages that hold `capacity` items, the first one `first_fixed` fewer (a band that always sits on it); the layout is the count"""
        def f(page, chunk):
            return len(chunk) if len(chunk) <= capacity - (first_fixed if page == 0 else 0) else None
        return f

    def test_what_fits_is_one_page(self):
        self.assertEqual(art_sheets.fill_pages(list(range(5)), self.fits(5)), [(5, [0, 1, 2, 3, 4])])
        self.assertEqual(art_sheets.fill_pages([], self.fits(5)), [(0, [])])

    def test_a_page_is_filled_as_far_as_it_goes_and_the_rest_go_on_the_next_in_order(self):
        pages = art_sheets.fill_pages(list(range(10)), self.fits(4))
        self.assertEqual([chunk for _, chunk in pages], [[0, 1, 2, 3], [4, 5, 6, 7], [8, 9]])
        self.assertEqual([layout for layout, _ in pages], [4, 4, 2])

    def test_the_first_page_may_hold_only_what_always_sits_on_it(self):
        pages = art_sheets.fill_pages(list(range(3)), self.fits(4, first_fixed=4))
        self.assertEqual([chunk for _, chunk in pages], [[], [0, 1, 2]])
        pages = art_sheets.fill_pages(list(range(6)), self.fits(4, first_fixed=3))
        self.assertEqual([chunk for _, chunk in pages], [[0], [1, 2, 3, 4], [5]])

    def test_what_cannot_be_held_is_an_error_and_nothing_is_dropped(self):
        with self.assertRaises(SystemExit):
            art_sheets.fill_pages(list(range(5)), lambda page, chunk: len(chunk) if not chunk else None)            # not even one item fits a page
        with self.assertRaises(SystemExit):
            art_sheets.fill_pages(list(range(17)), self.fits(4))                                                      # five pages: more than the game keeps
        self.assertEqual(len(art_sheets.fill_pages(list(range(17)), self.fits(4), max_pages=5)), 5)
        with self.assertRaises(SystemExit):
            art_sheets.fill_pages([1], lambda page, chunk: None)                                                      # the first page's own content does not fit


@unittest.skipIf(Image is None, "Pillow and NumPy are needed")
class TheComparison(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="ants_art_sheets_")
        self.have = os.path.join(self.tmp, "have")
        self.made = os.path.join(self.tmp, "made")
        os.makedirs(self.have)
        os.makedirs(self.made)

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    @staticmethod
    def png(path, pixel=0, level=9):
        a = np.zeros((8, 8, 4), np.uint8)
        a[..., 3] = 255
        a[3, 3, 0] = pixel
        Image.fromarray(a, "RGBA").save(path, compress_level=level)

    def fill(self, folder, pages=(0,), pixel=0, level=9, extra=""):
        sheets = {k: v for k, v in SHEETS.items() if k[0] in pages}
        with open(os.path.join(folder, "pack.txt"), "w") as f:
            f.write("ants-pack 2\n" + "\n".join(art_sheets.sheet_rows(sheets)) + "\n" + extra)
        for file, _, _ in sheets.values():
            self.png(os.path.join(folder, file), pixel, level)

    def compare(self, **kw):
        return art_sheets.compare(self.have, self.made, "pack.txt", **kw)

    @staticmethod
    def read(name, folder):
        with open(os.path.join(folder, name), "rb") as f:
            return f.read()

    def test_the_same_files_pass(self):
        self.fill(self.have)
        self.fill(self.made)
        code, lines = self.compare()
        self.assertEqual(code, 0, lines)
        self.assertEqual(len(lines), 4)
        self.assertTrue(all("the same bytes" in line for line in lines), lines)

    def test_a_pixel_that_differs_fails(self):
        self.fill(self.have)
        self.fill(self.made)
        self.png(os.path.join(self.made, "sheet_2x.png"), pixel=1)
        code, lines = self.compare()
        self.assertEqual(code, 1)
        self.assertIn("DIFFERENT", [line for line in lines if line.startswith("sheet_2x")][0])

    def test_a_manifest_that_differs_fails(self):
        self.fill(self.have)
        self.fill(self.made, extra="sprite x 0 1 1 0 0 0 0 left top\n")
        self.assertEqual(self.compare()[0], 1)

    def test_the_same_pixels_in_other_bytes_pass_unless_strict(self):
        self.fill(self.have, level=9)
        self.fill(self.made, level=0)
        self.assertNotEqual(self.read("sheet_1x.png", self.have), self.read("sheet_1x.png", self.made))
        code, lines = self.compare()
        self.assertEqual(code, 0, lines)
        self.assertIn("the same pixels", lines[1])
        self.assertEqual(self.compare(strict=True)[0], 1)

    def test_a_file_that_is_missing_fails(self):
        self.fill(self.have)
        self.fill(self.made)
        os.remove(os.path.join(self.made, "sheet_3x.png"))
        self.assertEqual(self.compare()[0], 1)
        os.remove(os.path.join(self.have, "sheet_1x.png"))
        self.assertEqual(self.compare()[0], 1)

    def test_a_second_page_is_compared_too(self):
        self.fill(self.have, pages=(0, 1))
        self.fill(self.made, pages=(0, 1))
        code, lines = self.compare()
        self.assertEqual(code, 0, lines)
        self.assertEqual(len([line for line in lines if line.startswith("more_")]), 3, lines)
        self.png(os.path.join(self.made, "more_2x.png"), pixel=1)
        self.assertEqual(self.compare()[0], 1)
        os.remove(os.path.join(self.made, "more_3x.png"))
        self.assertEqual(self.compare()[0], 1)

    def test_a_page_that_the_repository_lacks_fails(self):
        self.fill(self.have)
        self.fill(self.made, pages=(0, 1))
        code, lines = self.compare()
        self.assertEqual(code, 1)
        self.assertIn("no such file", [line for line in lines if line.startswith("more_1x")][0])

    def test_a_page_that_only_the_repository_has_fails_too(self):
        self.fill(self.have, pages=(0, 1))
        self.fill(self.made)
        code, lines = self.compare()
        self.assertEqual(code, 1)
        self.assertIn("did not write", [line for line in lines if line.startswith("more_1x")][0])

    def test_a_generator_that_makes_some_pages_only_compares_those(self):
        def first_page(line):
            parts = line.split()
            return bool(parts) and (parts[0] == "ants-pack" or (parts[0] == "sheet" and parts[1] == "0"))
        self.fill(self.have, pages=(0, 1))
        self.fill(self.made, pages=(0,))
        code, lines = art_sheets.compare(self.have, self.made, "pack.txt", keep=first_page, pages={0})
        self.assertEqual(code, 0, lines)
        self.assertEqual(len(lines), 4, lines)
        self.png(os.path.join(self.made, "sheet_3x.png"), pixel=1)
        self.assertEqual(art_sheets.compare(self.have, self.made, "pack.txt", keep=first_page, pages={0})[0], 1)
        self.fill(self.made, pages=(0,), extra="sheet 0 9 x.png 1 1\n")
        self.assertEqual(art_sheets.compare(self.have, self.made, "pack.txt", keep=lambda line: line.startswith("sheet 0 9"), pages={0})[0], 1)


@unittest.skipIf(Image is None, "Pillow and NumPy are needed")
class TheCommittedPacks(unittest.TestCase):
    def test_each_manifest_names_the_sheet_files_that_its_folder_holds_of_the_size_it_states(self):
        for folder, manifest in (("hud_art", "hud_art.txt"), ("world_art", "world_art.txt")):
            path = os.path.join(REPO, "assets", folder)
            with open(os.path.join(path, manifest), encoding="utf-8") as f:
                lines = [l for l in f.read().split("\n") if l.startswith("sheet ")]
            sheets, taken = art_sheets.parse_sheet_rows(lines)
            self.assertEqual(taken, len(lines), folder)
            self.assertGreaterEqual(len(sheets), 3, folder)
            for (page, scale), (file, width, height) in sheets.items():
                with Image.open(os.path.join(path, file)) as im:
                    self.assertEqual((im.format, im.mode, im.size), ("PNG", "RGBA", (width, height)), "%s page %d at %dx" % (folder, page, scale))
                self.assertLessEqual(max(width, height), art_sheets.LIMIT, "%s: %s is a sheet a texture cannot hold" % (folder, file))


if __name__ == "__main__":
    unittest.main()
