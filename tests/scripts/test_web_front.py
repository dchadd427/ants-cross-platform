#!/usr/bin/env python3
"""The front page's pictures, its font and the way they get into the web image (run by ./run_tests.sh --fast and by the CI).

The front page (web/lobby.html, served at "/") has the look of the 1998 game's own menus. Its pictures and its font are in web/front/ (served at /front/): cut out of the game's own
sprites and screenshots by tools/front_page_art/make_art.py (a developer's tool, not part of any build or test), and the game's own font, Libre Franklin, with its licence text.

  - the folder holds exactly the files that the tool makes, every PNG is what its name says (its size is the one that the page gives its <img>), and together they stay small
  - the font is a byte copy of the game's own, with the licence beside it (SIL OFL), and THIRD_PARTY_NOTICES.md names the copy
  - the Dockerfile, the local web build and the CI's check of the image carry the folder (a page whose pictures are not in the image would be bare on the real site)
  - the tool's files say that they are a developer's tool
  - the page uses the art: every picture it names is in the folder (and the folder holds nothing it does not use), each <img> has the size of its file, the Map Info lines are the ones in the level
    files, nothing is loaded from another site, the pictures that are not seen at once are lazy, the font is preloaded and the level buttons are radio buttons that the keyboard reaches
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
MAP_KEYS = ("tiny", "small", "medium", "gauntlet", "treasure", "islands")
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


class ThePageUsesTheArt(unittest.TestCase):
    def setUp(self):
        self.page = read("web", "lobby.html")

    def test_every_picture_that_the_page_names_is_in_the_folder_and_every_picture_is_used(self):
        named = set(re.findall(r"front/([\w.-]+\.(?:png|ttf))", self.page))                 # the markup and the style name them; the script builds preview_<map>.png from a map's key
        previews = set("preview_%s.png" % key for key in MAP_KEYS)
        in_folder = set(os.listdir(FRONT)) - {"LibreFranklin-OFL.txt"}
        self.assertEqual(named - in_folder, set(), "named but not in web/front/")
        self.assertEqual(in_folder - named - previews, set(), "in web/front/ but not used by the page")
        self.assertIn("'front/preview_' + m.key + '.png'", self.page)

    def test_every_img_has_the_size_of_its_file(self):
        tags = re.findall(r"<img [^>]*>", self.page)
        self.assertGreaterEqual(len(tags), 12)
        for tag in tags:
            source = re.search(r'src="front/([^"]+)"', tag).group(1)
            width = int(re.search(r'width="(\d+)"', tag).group(1))
            height = int(re.search(r'height="(\d+)"', tag).group(1))
            self.assertEqual(png_size(os.path.join(FRONT, source)), (width, height), source)
            self.assertIn("alt=", tag, source)

    def test_nothing_is_loaded_from_another_site(self):
        self.assertNotRegex(self.page, r"<script[^>]*\bsrc=")
        self.assertNotRegex(self.page, r'<link[^>]*href="(?:https?:)?//')
        self.assertNotRegex(self.page, r"url\(\s*[\"']?(?:https?:)?//")
        self.assertNotIn("@import", self.page)
        self.assertNotRegex(self.page, r"<(?:img|iframe|video|audio|source)[^>]*src=\"(?:https?:)?//")
        for url in set(re.findall(r'href="(https?://[^"]+)"', self.page)):                  # (the links that leave the page are the repository's)
            self.assertTrue(url.startswith("https://github.com/dchadd427/ants-cross-platform"), url)

    def test_the_map_info_lines_are_the_ones_of_the_level_files(self):
        block = re.search(r"var MAP_INFO = \{(.*?)\};", self.page, re.S).group(1)
        info = {m[0]: m[1] or m[2] for m in re.findall(r"(\w+): (?:'([^']*)'|\"([^\"]*)\")", block)}
        self.assertEqual(sorted(info), sorted(MAP_KEYS))
        for key in MAP_KEYS:
            with open(os.path.join(REPO, "Original-Ants", "Maps", key.upper() + ".LVL"), "rb") as f:
                head = f.read(40)
            version, mode, minutes = struct.unpack("<IIH", head[:10])
            description = head[10:40].split(b"\0")[0].decode("latin-1")
            self.assertEqual(info[key], "%s (%d min)" % (description, minutes), key)               # what the setup screen shows in its Map Info box

    def test_the_six_maps_are_the_page_s_list_in_its_order_with_a_preview_each(self):
        keys = re.findall(r"\{ key: '([a-z]+)', name: '[A-Za-z]+' \}", re.search(r"var MAPS = \[(.*?)\];", self.page, re.S).group(1))
        self.assertEqual(keys, list(MAP_KEYS))
        for key in keys:
            self.assertIn("preview_%s.png" % key, os.listdir(FRONT))

    def test_the_pictures_that_are_not_seen_at_once_load_lazily_and_the_ones_that_are_do_not(self):
        lazy = set(re.search(r'src="front/([^"]+)"', t).group(1) for t in re.findall(r"<img [^>]*loading=\"lazy\"[^>]*>", self.page))
        self.assertEqual(lazy, {"match_view.png", "qh_quickhelp.png", "qh_power.png"})            # the header's picture (not shown on a phone) and the two help sheets (behind "How it works")

    def test_the_font_is_declared_and_preloaded_from_the_one_file(self):
        self.assertIn('<link rel="preload" href="front/LibreFranklin-Medium.ttf" as="font" type="font/ttf" crossorigin>', self.page)
        self.assertRegex(self.page, r'@font-face \{ font-family: "Libre Franklin"; src: url\("front/LibreFranklin-Medium\.ttf"\) format\("truetype"\);')

    def test_the_level_buttons_are_radio_buttons_that_the_keyboard_reaches_and_shows(self):
        style = self.page[:self.page.index("</style>")]
        rule = re.search(r"\.pair input \{([^}]*)\}", style).group(1)
        self.assertIn("opacity: 0", rule)                                                           # hidden by being see-through: it keeps the focus, the arrows and Tab
        for forbidden in ("display: none", "visibility: hidden", "pointer-events"):
            self.assertNotIn(forbidden, rule)
        self.assertNotRegex(self.page, r'<input type="radio"[^>]*(?:tabindex|hidden|disabled)')
        self.assertIn(".pair input:focus-visible + label { outline: 3px solid var(--gold);", style)  # the focused button shows it
        self.assertIn(".pair input:checked + label {", style)


if __name__ == "__main__":
    unittest.main()
