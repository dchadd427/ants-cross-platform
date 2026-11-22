#!/usr/bin/env python3
"""The desktop game's icon files (src/ants_app/app_icon_data.inc, ants.ico and ants.rc; docs/INSTALL_AS_APP.md, "The desktop game's icon"): run by ./run_tests.sh --fast and by the CI.

The picture is drawn by tools/make_app_icons.py from the one grid that also draws the web icons. What this holds the committed files to, without a window (the window and the application are
tests/test_app/test_app_icon.cpp): the tool writes the data include byte for byte as committed, writes an ants.ico whose five frames (16, 32, 48, 64 and 256 pixels) are the grid at their sizes by
nearest neighbor and are the committed file's frames, and the resource script names an icon file that exists beside it. Needs Pillow (the runners of CI have none: skipped there).
"""
import os
import re
import subprocess
import sys
import tempfile
import unittest

try:
    from PIL import Image
except ImportError:
    Image = None

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DESKTOP = os.path.join(REPO, "src", "ants_app")
SIZES = [(16, 16), (32, 32), (48, 48), (64, 64), (256, 256)]


def frames(path):
    with Image.open(path) as ico:
        assert sorted(ico.info["sizes"]) == SIZES, ico.info["sizes"]
        return {size: ico.ico.getimage(size).convert("RGBA").tobytes() for size in SIZES}


@unittest.skipUnless(Image, "Pillow is needed")
class TheDesktopIconFiles(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        folders = {name: os.path.join(cls.tmp.name, name) for name in ("app", "web", "desktop")}
        done = subprocess.run([sys.executable, "-I", os.path.join(REPO, "tools", "make_app_icons.py"), "--out", folders["app"], "--favicon-dir", folders["web"],
                               "--desktop-dir", folders["desktop"], "--check"], capture_output=True, text=True, timeout=300)
        assert done.returncode == 0, done.stdout + done.stderr
        cls.made = folders["desktop"]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_the_tool_writes_only_the_two_files_of_the_desktop_game(self):
        self.assertEqual(sorted(os.listdir(self.made)), ["ants.ico", "app_icon_data.inc"])

    def test_the_data_include_is_what_the_tool_writes(self):
        with open(os.path.join(self.made, "app_icon_data.inc"), "rb") as made, open(os.path.join(DESKTOP, "app_icon_data.inc"), "rb") as kept:
            self.assertEqual(made.read(), kept.read(), "src/ants_app/app_icon_data.inc is not what tools/make_app_icons.py writes (run it)")

    def test_the_icon_file_has_the_five_frames_and_they_are_what_the_tool_draws(self):
        made, kept = frames(os.path.join(self.made, "ants.ico")), frames(os.path.join(DESKTOP, "ants.ico"))
        for size in SIZES:
            self.assertEqual(made[size], kept[size], "the %d px frame of src/ants_app/ants.ico is not what tools/make_app_icons.py draws (run it)" % size[0])

    def test_the_frames_keep_the_originals_hard_edges_and_transparency(self):
        kept = frames(os.path.join(DESKTOP, "ants.ico"))
        for size in SIZES:
            data = kept[size]
            alphas = {data[i] for i in range(3, len(data), 4)}
            self.assertEqual(alphas, {0, 255}, "the %d px frame has a blended edge" % size[0])
            self.assertEqual(data[3], 0, "the %d px frame is not transparent in its corner" % size[0])
        colors = {kept[(256, 256)][i:i + 3] for i in range(0, len(kept[(256, 256)]), 4) if kept[(256, 256)][i + 3]}
        self.assertLessEqual(len(colors), 16)

    def test_the_resource_script_names_an_icon_file_that_exists_beside_it(self):
        with open(os.path.join(DESKTOP, "ants.rc"), encoding="utf-8") as f:
            text = f.read()
        named = re.findall(r'^\s*\w+\s+ICON\s+"([^"]+)"', text, re.M)
        self.assertEqual(len(named), 1)
        self.assertTrue(os.path.isfile(os.path.join(DESKTOP, named[0])), named[0])


if __name__ == "__main__":
    unittest.main()
