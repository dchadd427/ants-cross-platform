#!/usr/bin/env python3
"""The front page's pictures, its font and the way they get into the web image (run by ./run_tests.sh --fast and by the CI).

The front page (web/lobby.html, served at "/") has the look of the 1998 game's own menus. Its pictures and its font are in web/front/ (served at /front/): cut out of the game's own
sprites and screenshots by tools/front_page_art/make_art.py (a developer's tool, not part of any build or test), and the game's own font, Libre Franklin, with its licence text.

  - the folder holds exactly the files that the tool makes (and the shared stylesheet, classic.css), every PNG is what its name says (its size is the one that the page gives its <img>), and
    together they stay small
  - the font is a byte copy of the game's own, with the licence beside it (SIL OFL), and THIRD_PARTY_NOTICES.md names the copy
  - the Dockerfile, the local web build and the CI's check of the image carry the folder (a page whose pictures are not in the image would be bare on the real site), and nginx revalidates
    the stylesheet like every page
  - the shared stylesheet (classic.css, linked by the changelog pages and Sprites and sounds): the font and the clay tile it names are in the folder, it loads nothing from another site, and
    the text colours that its components pair are readable
  - the tool's files say that they are a developer's tool
  - the panels that the mockup did not draw: the name step (a banner and a big button), a room (a smaller header, the games as wide as the room's panel), the notices (a name or a code that
    is refused, the blocked windows) and the exact size of the games' frames in a wide window (the arithmetic of the two breakpoints is checked against the style's own numbers)
  - the page uses the art: every picture it names is in the folder (and the folder holds nothing it does not use), each <img> has the size of its file, the Map Info lines are the ones in the level
    files, nothing is loaded from another site, the pictures that are not seen at once are lazy, the font is preloaded and the level buttons are radio buttons that the keyboard reaches
"""
import hashlib
import os
import re
import struct
import unittest
import zlib

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FRONT = os.path.join(REPO, "web", "front")
TOOL = os.path.join(REPO, "tools", "front_page_art")

# the pictures of the folder and their sizes (width, height): web/lobby.html gives the same sizes to the <img> that shows each of them
PICTURES = {
    "logo.png": (581, 218), "match_view.png": (761, 497), "clay.png": (256, 256),
    "lbl_pickamap.png": (145, 20), "lbl_mapinfo.png": (99, 22),
    "ant_green.png": (23, 40), "ant_red.png": (23, 40), "ant_blue.png": (23, 40), "ant_black.png": (23, 40),
    "preview_tiny.png": (300, 300), "preview_small.png": (300, 300), "preview_medium.png": (300, 300),
    "preview_gauntlet.png": (300, 300), "preview_treasure.png": (300, 300), "preview_islands.png": (300, 300),
    "qh_quickhelp.png": (257, 453), "qh_power.png": (362, 455),
}
FONT_FILES = ("LibreFranklin-Medium.ttf", "LibreFranklin-OFL.txt")
STYLE_FILES = ("classic.css",)          # the look of the other pages (the front page keeps its own inline copy): not made by the tool, not used by lobby.html
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


def read_png_pixels(path):
    """(width, height, rows) of a PNG that is not interlaced, each row a list of (r, g, b): a palette of any depth, or 8 bit RGB or RGBA (the standard library only)."""
    data = read_bytes(os.path.relpath(path, REPO))
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    pos, idat, palette = 8, b"", []
    width = height = depth = ctype = 0
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            assert interlace == 0 and (ctype == 3 or (ctype in (2, 6) and depth == 8)), "a PNG that this reader does not read: " + path
        elif kind == b"PLTE":
            palette = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    bits = depth * {3: 1, 2: 3, 6: 4}[ctype]                              # bits of a pixel
    stride = (bits * width + 7) // 8
    step = max(1, bits // 8)                                              # the bytes that a filter looks back (a palette of any depth: one)
    raw = zlib.decompress(idat)
    rows, above = [], bytearray(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            left = line[i - step] if i >= step else 0
            up = above[i]
            corner = above[i - step] if i >= step else 0
            if kind == 1:
                line[i] = (line[i] + left) & 255
            elif kind == 2:
                line[i] = (line[i] + up) & 255
            elif kind == 3:
                line[i] = (line[i] + ((left + up) >> 1)) & 255
            elif kind == 4:
                pa, pb, pc = abs(up - corner), abs(left - corner), abs(left + up - 2 * corner)
                line[i] = (line[i] + (left if pa <= pb and pa <= pc else up if pb <= pc else corner)) & 255
        above = line
        if ctype == 3:
            mask = (1 << depth) - 1
            rows.append([palette[(line[(x * depth) // 8] >> (8 - depth - (x * depth) % 8)) & mask] for x in range(width)])
        else:
            n = 3 if ctype == 2 else 4
            rows.append([tuple(line[x * n:x * n + 3]) for x in range(width)])
    return width, height, rows


class TheFolder(unittest.TestCase):
    def test_it_holds_exactly_the_files_that_the_tool_makes_and_the_shared_stylesheet(self):
        self.assertEqual(sorted(os.listdir(FRONT)), sorted(list(PICTURES) + list(FONT_FILES) + list(STYLE_FILES)))

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
        self.assertRegex(ci, r"ls -l index\.html play\.html lobby\.html [^\n]*front/logo\.png front/LibreFranklin-Medium\.ttf front/classic\.css")

    def test_git_does_not_ignore_the_pictures(self):
        ignore = read(".gitignore")
        self.assertIn("*.png", ignore)                                           # (the rule that would hide them)
        self.assertTrue(ignore.index("!web/front/**") > ignore.index("*.png"))      # (and the exception that comes after it)

    def test_nginx_revalidates_the_stylesheet_with_the_pages_and_has_no_rule_of_its_own_for_front(self):
        conf = "\n".join(line.split("#", 1)[0] for line in read("docker", "nginx.conf").splitlines())
        block = re.search(r"location ~\* \\\.\(html\|css\|js\)\$ \{(.*?)\n    \}", conf, re.S)
        self.assertIsNotNone(block, "the html, css and js block of docker/nginx.conf")
        self.assertIn('add_header Cache-Control "no-cache, must-revalidate" always;', block.group(1))                  # (AGENTS.md rule 6: the stylesheet is never stale)
        self.assertNotIn("expires", block.group(1))
        locations = re.findall(r"^\s*location\s+(.*?)\s*\{", conf, re.M)
        self.assertEqual([l for l in locations if "front" in l and "four" not in l], [])                              # (the folder is served by the default rules: files, the week for pictures)
        before = locations[:locations.index("~* \\.(html|css|js)$")]
        self.assertFalse([l for l in before if l.startswith("~*") and "css" in l], "an earlier regular-expression location would catch the stylesheet")


class TheSharedStylesheet(unittest.TestCase):
    """web/front/classic.css is the look of the changelog pages and of Sprites and sounds (the front page keeps its own inline copy of the colours)."""

    def setUp(self):
        self.css = read("web", "front", "classic.css")

    def token(self, name):
        return re.search(r"--%s: (#[0-9a-f]{6});" % name, self.css).group(1)

    @staticmethod
    def ratio(one, other):
        high, low = sorted((TheColours.luminance(one), TheColours.luminance(other)), reverse=True)
        return (high + 0.05) / (low + 0.05)

    def test_the_font_and_the_clay_tile_it_names_are_in_the_folder_beside_it(self):
        self.assertRegex(self.css, r'@font-face \{ font-family: "Libre Franklin"; src: url\("LibreFranklin-Medium\.ttf"\) format\("truetype"\);')       # (relative: it resolves next to the sheet)
        self.assertRegex(self.css, r'url\("clay\.png\?v=[0-9a-f]{8}"\)')                        # (the address carries the file's hash: TheClay)
        for name in ("LibreFranklin-Medium.ttf", "clay.png"):
            self.assertIn(name, os.listdir(FRONT))

    def test_it_loads_nothing_from_another_site(self):
        self.assertNotIn("@import", self.css)
        for target in re.findall(r"url\(\s*[\"']?([^\"')]+)", self.css):
            self.assertTrue(target.startswith("data:image/svg+xml,") or target.split("?")[0] in os.listdir(FRONT), target)          # (a file of this folder, its address may end in ?v=, or the select's own arrow, drawn in the sheet)
        self.assertNotRegex(self.css, r"url\(\s*[\"']?(?:https?:)?//")

    def test_it_has_the_pieces_that_every_page_needs(self):
        for needle in (".screen::after {", ".wrap {", ".btn, .banner {", ".btn:hover {", ".btn:active {", ".btn.sm {", ".btn.big {", ".banner {", ".panel {", ".site-head {", ".logo img {", ".bar {", ".bar .ver {",
                       "select, input[type=text], input[type=search] {", "[hidden] { display: none !important; }"):
            self.assertIn(needle, self.css, needle)
        self.assertRegex(self.css, r"button:focus-visible[^{]*\{ outline: 3px solid var\(--gold\);")                    # (a focused control shows it, on every page)

    def test_the_text_that_its_components_pair_is_at_least_4_5_to_1(self):
        tone = self.token
        pairs = (("cream on a button or a banner", "cream", "teal"), ("gold on a banner", "gold", "teal"), ("cream on a hovered button", "cream", "teal-hi"),
                 ("a chosen button", "pressed-ink", "pressed"), ("cream in a black box", "cream", "inset"), ("gold in a black box", "gold", "inset"),
                 ("a link in a black box", "mint", "inset"), ("small print in a black box", "muted", "inset"), ("a refused entry", "bad-ink", "bad-bg"))
        for what, text, background in pairs:
            self.assertGreaterEqual(self.ratio(tone(text), tone(background)), 4.5, "%s: %s on %s" % (what, text, background))
        placeholder = re.search(r"::placeholder \{ color: (#[0-9a-f]{6}); \}", self.css).group(1)
        self.assertGreaterEqual(self.ratio(placeholder, tone("inset")), 4.5)
        for end in re.findall(r"linear-gradient\(180deg, (#[0-9a-f]{6}), (#[0-9a-f]{6})\)", self.css)[0]:                  # (the footer bar's two ends)
            self.assertGreaterEqual(self.ratio(tone("cream"), end), 4.5, "cream on the footer bar " + end)
            self.assertGreaterEqual(self.ratio("#ffffff", end), 4.5, "the version on the footer bar " + end)
        self.assertGreaterEqual(self.ratio(tone("ink"), tone("clay")), 4.5, "ink on the flat clay behind the tile (the tile itself: TheClay)")           # (the text that sits on the bare page)


class TheClay(unittest.TestCase):
    """web/front/clay.png is the background of every page: the orange of the game's menus with a little noise and dirt, so that it is not so clean (tools/front_page_art/artlib.py makes it).
    The text that sits on the bare page is the ink of the pages, which has to stay readable on every part of it."""

    @classmethod
    def setUpClass(cls):
        cls.width, cls.height, cls.rows = read_png_pixels(os.path.join(FRONT, "clay.png"))
        css = read("web", "front", "classic.css")
        cls.flat = tuple(int(re.search(r"--clay: (#[0-9a-f]{6});", css).group(1)[i:i + 2], 16) for i in (1, 3, 5))
        cls.ink = re.search(r"--ink: (#[0-9a-f]{6});", css).group(1)
        cls.light = {c: TheColours.luminance("#%02x%02x%02x" % c) for row in cls.rows for c in row}
        cls.ink_light = TheColours.luminance(cls.ink)

    def test_every_page_names_the_tile_by_its_content(self):
        # docker/nginx.conf lets a browser keep a .png for a week without asking again, so a tile that changes under the same address stays the clean one for everybody who has been here: its address ends in
        # ?v= and the first 8 hex digits of the file's sha256, in the six places that name it (a new tile fails this until they say so)
        version = hashlib.sha256(read_bytes("web", "front", "clay.png")).hexdigest()[:8]
        for parts, count in ((("web", "lobby.html"), 1), (("web", "shell.html"), 4), (("web", "front", "classic.css"), 1)):
            text = read(*parts)
            self.assertEqual(len(re.findall(r"clay\.png", text)), count, "%s names the tile %d time(s)" % (parts[-1], count))
            self.assertEqual(len(re.findall(r'url\("(?:front/)?clay\.png\?v=%s"\)' % version, text)), count, "%s: the tile's address ends in ?v=%s" % (parts[-1], version))

    def test_it_has_noise_and_dirt_and_is_no_flat_orange(self):
        pixels = [c for row in self.rows for c in row]
        self.assertGreaterEqual(len(set(pixels)), 20, "the original's tile is two colours: a flat orange")
        commonest = max(pixels.count(c) for c in set(pixels))
        self.assertLess(commonest / len(pixels), 0.25, "no one colour fills the tile (the original's fills 96 percent of it)")
        deepest, lightest = min(self.light.values()), max(self.light.values())
        self.assertGreater(lightest / deepest, 1.7, "the tile has dirt that is much deeper than its lightest grit")

    def test_its_average_is_the_flat_clay_that_the_pages_name_behind_it(self):
        pixels = [c for row in self.rows for c in row]
        for channel, name in enumerate("rgb"):
            mean = sum(c[channel] for c in pixels) / len(pixels)
            self.assertLess(abs(mean - self.flat[channel]), 4, "the %s of the tile's average and of --clay: no seam where the flat colour shows" % name)

    def test_the_ink_stays_readable_on_it(self):
        def ratio(luminance):
            return (luminance + 0.05) / (self.ink_light + 0.05)
        self.assertGreaterEqual(ratio(min(self.light.values())), 3.0, "ink on the darkest speck of dirt")
        w, h = self.width, self.height
        light = [[self.light[c] for c in row] for row in self.rows]
        across = [[sum(light[y][(x + d) % w] for d in range(-2, 3)) for x in range(w)] for y in range(h)]               # (the 5 pixels of a row around each pixel, the tile wrapping)
        worst = min(ratio(sum(across[(y + d) % h][x] for d in range(-2, 3)) / 25) for y in range(h) for x in range(w))
        self.assertGreaterEqual(worst, 4.5, "ink on the average of the worst 5 x 5 pixels of the tile")

    def test_it_repeats_without_a_seam(self):
        w, h = self.width, self.height

        def grain(a, b):                       # how much two neighbouring pixels differ, on average
            return sum(abs(self.light[a[i]] - self.light[b[i]]) for i in range(len(a))) / len(a)
        columns = [[row[x] for row in self.rows] for x in range(w)]
        inside_x = sum(grain(columns[x], columns[x + 1]) for x in range(w - 1)) / (w - 1)
        inside_y = sum(grain(self.rows[y], self.rows[y + 1]) for y in range(h - 1)) / (h - 1)
        self.assertLess(grain(columns[w - 1], columns[0]), inside_x * 1.5 + 0.002, "the last column does not fit the first")
        self.assertLess(grain(self.rows[h - 1], self.rows[0]), inside_y * 1.5 + 0.002, "the last row does not fit the first")


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
        patterns = re.findall(r'"([\w%.]+\.png)"', text)                           # "clay.png", "ant_%s.png", "preview_%s.png" ...
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
        in_folder = set(os.listdir(FRONT)) - {"LibreFranklin-OFL.txt"} - set(STYLE_FILES)         # (the stylesheet is the other pages': tests/scripts/test_web_pages_classic.py)
        self.assertEqual(named - in_folder, set(), "named but not in web/front/")
        self.assertEqual(in_folder - named - previews, set(), "in web/front/ but not used by the page")
        self.assertIn("'front/preview_' + m.key + '.png'", self.page)

    def test_every_img_has_the_size_of_its_file(self):
        tags = re.findall(r"<img [^>]*>", self.page)
        self.assertGreaterEqual(len(tags), 11)                                               # (the logo, the picture of a match, the two labels of the map, its preview, the four ants and the two help sheets)
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


class ThePanelsThatTheMockupDidNotDraw(unittest.TestCase):
    def setUp(self):
        self.page = read("web", "lobby.html")
        self.style = self.page[:self.page.index("</style>")]

    def test_the_name_step_is_a_banner_and_a_big_button_in_the_pages_style(self):
        self.assertIn('<h2 id="who-title" class="banner who-title" hidden>', self.page)
        self.assertIn('<button id="who-go" type="button" class="btn big" hidden>', self.page)
        self.assertRegex(self.style, r"\.myname \.who-title \{[^}]*color: var\(--gold\)")
        self.assertRegex(self.style, r'input\[aria-invalid="true"\] \{[^}]*border-color')                # (a name that is refused marks its field)

    def test_a_notice_is_a_notice_and_a_hint_is_a_hint(self):
        for ident in ("name-msg", "popup-hint"):
            self.assertRegex(self.page, r'<p class="msg" id="%s"' % ident)
        self.assertRegex(self.page, r'<p class="hint" id="fill-hint" hidden>')                       # (what the leader's START does with the empty seats is not an error)
        self.assertRegex(self.style, r"\.msg \{[^}]*color: var\(--bad-ink\)")
        self.assertIn("#join-hint.bad {", self.style)

    def test_a_room_has_a_smaller_header_and_the_page_adds_its_class_once_a_room_is_shown(self):
        self.assertIn("document.body.classList.add('in-room');", self.page)
        self.assertEqual(self.page.count("classList.add('in-room')"), 1)
        block = re.search(r"@media \(min-width: 701px\) \{(.*?)\n        \}", self.style, re.S).group(1)             # (a phone's header is small already: the rules are for the wider pages)
        for needle in (".in-room .top {", ".in-room .logo img { width: 210px; }", ".in-room .tv, .in-room .links, .in-room .lead { display: none; }", ".in-room .wrap { max-width: 1332px; }"):
            self.assertIn(needle, block, needle)

    def test_the_games_the_strip_and_the_note_are_as_wide_as_the_room_s_panel(self):
        wrap = int(re.search(r"\.in-room \.wrap \{ max-width: (\d+)px; \}", self.style).group(1))
        grid = re.search(r"#grid \{([^}]*)\}", self.style).group(1)
        padding = int(re.search(r"padding: 0 (\d+)px", grid).group(1))
        self.assertEqual(int(re.search(r"max-width: (\d+)px", grid).group(1)), wrap)
        self.assertEqual(padding, int(re.search(r"\.wrap \{[^}]*padding: \d+px (\d+)px", self.style).group(1)))     # the page's own side padding
        self.assertEqual(int(re.search(r"#sync \{[^}]*max-width: (\d+)px", self.style, re.S).group(1)), wrap - 2 * padding)
        self.assertIn("calc(100%% - %dpx)" % (2 * padding), re.search(r"#sync \{(.*?)\}", self.style, re.S).group(1))
        self.assertRegex(self.style, r"\.frames-note \{[^}]*max-width: %dpx;[^}]*padding: 0 %dpx;" % (wrap, padding))

    def test_two_games_side_by_side_get_their_own_size_from_the_windows_that_can_hold_them(self):
        """A column is the picture and the 2 px border of its cell on both sides (964 and 644); the window needs two columns, the gap, the page's padding and a scrollbar."""
        grid = re.search(r"#grid \{([^}]*)\}", self.style).group(1)
        gap = int(re.search(r"gap: \d+px (\d+)px", grid).group(1))
        padding = int(re.search(r"padding: 0 (\d+)px", grid).group(1))
        border = int(re.search(r"\.cell \{ border: (\d+)px", self.style).group(1))
        scrollbar = 15
        for shape, columns, picture in (("16:9", 964, 960), ("4:3", 644, 640)):
            self.assertEqual(picture + 2 * border, columns)
            need = 2 * columns + gap + 2 * padding + scrollbar
            at = re.search(r"@media \(min-width: (\d+)px\) \{\s*body%s #grid \{ grid-template-columns: repeat\(2, %dpx\);" % (r':not\(\[data-aspect="4:3"\]\)' if shape == "16:9" else r'\[data-aspect="4:3"\]', columns), self.style)
            self.assertIsNotNone(at, shape)
            self.assertGreaterEqual(int(at.group(1)), need, "the window of %s px cannot hold two %s games (%d px needed)" % (at.group(1), shape, need))
            self.assertLess(int(at.group(1)) - need, 20, "the breakpoint of the %s games is far above what they need (%d px)" % (shape, need))

    def test_the_cells_keep_the_cells_border_and_the_frame_is_the_pictures_own_shape(self):
        self.assertIn(".frame { position: relative; aspect-ratio: 16 / 9; }", self.style)
        self.assertIn('body[data-aspect="4:3"] .frame { aspect-ratio: 4 / 3; }', self.style)
        for seat, place in (("3", "0"), ("0", "1"), ("1", "2"), ("2", "3")):                       # Black, Green, Red, Blue: the way the four hills lie
            self.assertIn('.cell[data-seat="%s"] { order: %s; }' % (seat, place), self.style)


class TheCardsAtManyWidths(unittest.TestCase):
    """The browser checks (tests/scripts/web_home_check.py, part front) measure the page from 320 to 1600 px; what needs no browser is read here."""

    def setUp(self):
        self.page = read("web", "lobby.html")
        self.style = self.page[:self.page.index("</style>")]

    def test_the_card_has_two_columns_from_1100_px_and_one_under_it(self):
        self.assertRegex(self.style, r"\.match-grid \{ display: grid; grid-template-columns: minmax\(0, 340px\) minmax\(0, 1fr\);")      # (the map and its picture, and the seats with the rest)
        one = re.search(r"@media \(max-width: (\d+)px\) \{\s*\.match-grid \{ grid-template-columns: minmax\(0, 1fr\);", self.style)
        self.assertIsNotNone(one)
        self.assertEqual(one.group(1), "1099")
        block = one.group(0) + self.style[self.style.index(one.group(0)) + len(one.group(0)):][:700]
        self.assertIn(".match-grid .left { display: grid; grid-template-columns: minmax(0, 1fr) 190px;", block)      # (one column: the map's choice and its picture side by side)
        self.assertIn(".preview { grid-column: 2; grid-row: 1 / span 3; margin: 0; }", block)
        self.assertRegex(self.style, r"\.cols \{ max-width: 1060px; \}")                                  # (a card of 1060 px at most: its rows are not stretched over a wide window)

    def test_a_seat_is_one_line_from_701_px_and_two_lines_on_a_phone_with_five_buttons_that_fit_320_px(self):
        self.assertIn('.seats4 li { grid-template-columns: 30px 140px auto minmax(0, 1fr); grid-template-areas: "ant who sit pick";', self.style)       # (the ant, the colour, Sit here, the five buttons: the order that Tab goes in; test_web_lobby.py pins the rest)
        phone = re.search(r"@media \(max-width: 700px\) \{(.*?)\n        \}", self.style, re.S).group(1)
        self.assertIn('.seats4 li { grid-template-columns: 26px minmax(0, 1fr) auto; grid-template-areas: "ant who sit" "pick pick pick";', phone)      # (the colour and Sit here, then the buttons under the whole row)
        self.assertIn(".roster.seats4 .pair label { flex: 0 0 auto; min-width: 0; padding: 5px 4px; font-size: 13px; }", phone)
        narrow = re.search(r"@media \(max-width: 480px\) \{(.*?)\n        \}", self.style, re.S).group(1)
        self.assertIn(".roster.seats4 .pair label { flex: 1 1 auto; }", narrow)                                    # (a phone: the five buttons share the row)
        narrowest = re.search(r"@media \(max-width: 360px\) \{(.*?)\n        \}", self.style, re.S).group(1)
        self.assertIn(".roster.seats4 .pair label { padding: 5px 2px; font-size: 12.5px; }", narrowest)             # (320 px: all five still on one line; web_home_check.py measures it in a browser)
        self.assertLess(self.style.index("@media (max-width: 700px)"), self.style.index("@media (max-width: 480px)"))     # (the narrower rules come later: they win)
        self.assertLess(self.style.index("@media (max-width: 480px)"), self.style.index("@media (max-width: 360px)"))

    def test_the_invitations_and_the_line_to_join_wrap_on_a_phone_and_never_scroll_sideways(self):
        phone = re.search(r"@media \(max-width: 700px\) \{(.*?)\n        \}", self.style, re.S).group(1)
        self.assertIn(".invite .who { flex: 1 0 100%; }", phone)
        self.assertIn(".invite input[type=text] { flex: 1 0 100%; }", phone)
        self.assertIn(".havecode input[type=text] { flex: 1 0 100%; max-width: none; }", phone)
        self.assertIn(".invite input[type=text] { flex: 1 1 200px; min-width: 0;", self.style)                      # (a field can shrink under its row: a long link never makes the page wider)
        self.assertIn(".invite { display: flex; flex-wrap: wrap;", self.style)

    def test_the_colour_of_an_invitation_shows_on_the_clay_in_a_ring(self):
        # the invitations are on the page's red-orange clay, where a red dot is lost: a dark ring and a cream one round each colour (checked by eye at 4x; the red and the clay are the same hue)
        self.assertIn(".invite .dot { width: 13px; height: 13px; margin-right: 8px; border: 2px solid #15100c; box-shadow: 0 0 0 2px #f4ead8; }", self.style)
        self.assertLess(self.style.index(".dot.black {"), self.style.index(".invite .dot {"))                      # (later: it wins over the black dot's grey border)


class TheColours(unittest.TestCase):
    """The text of the page is at least 4.5:1 against its background, in every state of the buttons and banners (the browser check measures the whole page; this reads the colours)."""

    def setUp(self):
        page = read("web", "lobby.html")
        self.style = page[:page.index("</style>")]

    def token(self, name):
        return re.search(r"--%s: (#[0-9a-f]{6});" % name, self.style).group(1)

    @staticmethod
    def luminance(colour):
        def channel(v):
            v /= 255.0
            return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
        r, g, b = (int(colour[i:i + 2], 16) for i in (1, 3, 5))
        return 0.2126 * channel(r) + 0.7152 * channel(g) + 0.0722 * channel(b)

    def ratio(self, one, other):
        high, low = sorted((self.luminance(one), self.luminance(other)), reverse=True)
        return (high + 0.05) / (low + 0.05)

    def test_the_banners_buttons_and_hovered_buttons_keep_their_text_readable(self):
        single = re.search(r"\.card\.match > \.banner \{ background-color: (#[0-9a-f]{6}); \}", self.style).group(1)
        for what, text, background in (("gold title on a teal banner", self.token("gold"), self.token("teal")),
                                       ("cream text on a teal button or banner", self.token("cream"), self.token("teal")),
                                       ("cream text on a hovered button", self.token("cream"), self.token("teal-hi")),
                                       ("cream text on the banner of the card", self.token("cream"), single),
                                       ("cream text in the black boxes", self.token("cream"), self.token("inset")),
                                       ("the hint in an empty field", re.search(r"::placeholder \{ color: (#[0-9a-f]{6}); \}", self.style).group(1), self.token("inset"))):
            self.assertGreaterEqual(self.ratio(text, background), 4.5, "%s: %s on %s" % (what, text, background))


if __name__ == "__main__":
    unittest.main()
