#!/usr/bin/env python3
"""The front page's pictures, its font and the way they get into the web image (run by ./run_tests.sh --fast and by the CI).

The front page (web/lobby.html, served at "/") has the look of the 1998 game's own menus. Its pictures and its font are in web/front/ (served at /front/): cut out of the game's own
sprites and screenshots by tools/front_page_art/make_art.py (a developer's tool, not part of any build or test), and the game's own font, Libre Franklin, with its licence text.

  - the folder holds exactly the files that the tool makes, every PNG is what its name says (its size is the one that the page gives its <img>), and together they stay small
  - the font is a byte copy of the game's own, with the licence beside it (SIL OFL), and THIRD_PARTY_NOTICES.md names the copy
  - the Dockerfile, the local web build and the CI's check of the image carry the folder (a page whose pictures are not in the image would be bare on the real site)
  - the tool's files say that they are a developer's tool
"""
import os
import re
import struct
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FRONT = os.path.join(REPO, "web", "front")
TOOL = os.path.join(REPO, "tools", "front_page_art")

# the pictures of the folder and their sizes (width, height): web/lobby.html gives the same sizes to the <img> that shows each of them
PICTURES = {
    "logo.png": (581, 218), "match_view.png": (761, 497), "clay.png": (96, 96),
    "btn_start1.png": (98, 27), "btn_start2.png": (98, 27), "btn_start3.png": (97, 24),
    "lbl_pickamap.png": (145, 20), "lbl_mapinfo.png": (99, 22),
    "ant_green.png": (23, 40), "ant_red.png": (23, 40), "ant_blue.png": (23, 40), "ant_black.png": (23, 40),
    "preview_tiny.png": (300, 300), "preview_small.png": (300, 300), "preview_medium.png": (300, 300),
    "preview_gauntlet.png": (300, 300), "preview_treasure.png": (300, 300), "preview_islands.png": (300, 300),
    "qh_quickhelp.png": (257, 453), "qh_power.png": (362, 455),
}
FONT_FILES = ("LibreFranklin-Medium.ttf", "LibreFranklin-OFL.txt")
BUDGET = 1000 * 1000        # bytes: the whole folder (the pictures are 0.67 MB, the font 0.14 MB); a picture that grows past this is a decision, not an accident


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def read_bytes(*parts):
    with open(os.path.join(REPO, *parts), "rb") as f:
        return f.read()


def png_size(path):
    """(width, height) from the IHDR chunk, or None when the file is no PNG."""
    with open(path, "rb") as f:
        head = f.read(24)
    if head[:8] != b"\x89PNG\r\n\x1a\n" or head[12:16] != b"IHDR":
        return None
    return struct.unpack(">II", head[16:24])


class TheFolder(unittest.TestCase):
    def test_it_holds_exactly_the_files_that_the_tool_makes(self):
        self.assertEqual(sorted(os.listdir(FRONT)), sorted(list(PICTURES) + list(FONT_FILES)))

    def test_every_picture_is_a_png_of_the_size_that_the_page_gives_it(self):
        for name, size in PICTURES.items():
            self.assertEqual(png_size(os.path.join(FRONT, name)), size, name)

    def test_the_whole_folder_stays_small(self):
        total = sum(os.path.getsize(os.path.join(FRONT, name)) for name in os.listdir(FRONT))
        self.assertLess(total, BUDGET, "web/front/ is %d bytes" % total)

    def test_the_font_is_a_byte_copy_of_the_games_own_with_its_licence_beside_it(self):
        for name in FONT_FILES:
            self.assertEqual(read_bytes("web", "front", name), read_bytes("Original-Ants", name), name)
        self.assertRegex(read(".gitattributes"), r"(?m)^web/front/LibreFranklin-OFL\.txt\s+-text$")        # (git keeps the bytes of the licence text, as it does for the game's own copy)
        licence = read("web", "front", "LibreFranklin-OFL.txt")
        self.assertIn("SIL OPEN FONT LICENSE Version 1.1", licence)
        self.assertIn("Libre Franklin Project Authors", licence)

    def test_the_notices_name_the_copy(self):
        notices = read("THIRD_PARTY_NOTICES.md")
        for needle in ("web/front/", "LibreFranklin-Medium.ttf", "LibreFranklin-OFL.txt", "tools/front_page_art/make_art.py"):
            self.assertIn(needle, notices, needle)


class TheWayIntoTheImage(unittest.TestCase):
    def test_the_dockerfile_copies_the_folder_to_where_nginx_serves_front(self):
        self.assertIn("COPY web/front/ /usr/share/nginx/html/front/", read("Dockerfile"))

    def test_the_local_web_build_packs_it_too(self):
        script = read("build_web.sh")
        self.assertIn('mkdir -p "${DIST_DIR}/front"', script)
        self.assertIn('cp -f "${SCRIPT_DIR}/web/front"/* "${DIST_DIR}/front/"', script)

    def test_the_ci_looks_for_a_picture_and_the_font_in_the_image(self):
        ci = read(".github", "workflows", "ci.yml")
        self.assertRegex(ci, r"ls -l index\.html play\.html lobby\.html [^\n]*front/logo\.png front/LibreFranklin-Medium\.ttf")

    def test_git_does_not_ignore_the_pictures(self):
        ignore = read(".gitignore")
        self.assertIn("*.png", ignore)                                           # (the rule that would hide them)
        self.assertTrue(ignore.index("!web/front/**") > ignore.index("*.png"))      # (and the exception that comes after it)


class TheTool(unittest.TestCase):
    def test_its_files_say_that_they_are_a_developers_tool(self):
        for name in ("make_art.py", "make_logo.py", "artlib.py"):
            text = read("tools", "front_page_art", name)
            docstring = text.split('"""')[1]
            self.assertIn("developer's tool", docstring, name)
            self.assertIn("Pillow", docstring, name)
            self.assertIn("not part of any build or test", docstring, name)

    def test_it_makes_every_picture_of_the_folder_and_nothing_else(self):
        text = read("tools", "front_page_art", "make_art.py")
        patterns = re.findall(r'"([\w%.]+\.png)"', text)                           # "clay.png", "btn_start%d.png", "ant_%s.png", "preview_%s.png" ...
        expressions = [re.compile(pattern.replace(".", r"\.").replace("%d", r"\d").replace("%s", r"[a-z]+")) for pattern in patterns]
        for name in PICTURES:
            self.assertTrue(any(e.fullmatch(name) for e in expressions), name + " is not made by make_art.py")
        for pattern, expression in zip(patterns, expressions):
            self.assertTrue(any(expression.fullmatch(name) for name in PICTURES), pattern + " is made by make_art.py but is not in the folder")

    def test_the_tool_reads_the_catalog_of_the_repository_and_names_no_path_of_anybodys_machine(self):
        text = read("tools", "front_page_art", "artlib.py")
        self.assertIn('CATALOG = ROOT / "asset_catalog"', text)
        self.assertNotIn("/home/", text)                                           # no path of anybody's machine


if __name__ == "__main__":
    unittest.main()
