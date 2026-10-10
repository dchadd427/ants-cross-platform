#!/usr/bin/env python3
"""The front page's pictures, its font and the way they get into the web image (run by ./run_tests.sh --fast and by the CI).

The front page (web/lobby.html, served at "/") is the lobby: four colour cards, the map and START, in the look of the 1998 game's own menus. Its pictures and its font are in web/front/ (served at
/front/): cut out of the game's own sprites and screenshots by tools/front_page_art/make_art.py (a developer's tool, not part of any build or test), and the game's own font, Libre Franklin, with its
licence text. The page's rules and its client of the game server are two scripts of the same folder (lobby_rules.js, lobby_net.js: tests of their own).

  - the folder holds exactly the files that the tool makes (and the shared stylesheet, classic.css, and the two scripts), every PNG is what its name says (its size is the one that the page gives its <img>
    or its style), and together they stay small
  - the font is a byte copy of the game's own, with the licence beside it (SIL OFL), and THIRD_PARTY_NOTICES.md names the copy
  - the Dockerfile, the local web build and the CI's check of the image carry the folder (a page whose pictures are not in the image would be bare on the real site), and nginx revalidates
    the stylesheet like every page
  - the shared stylesheet (classic.css, linked by the changelog pages and Sprites and sounds): the font and the clay tile it names are in the folder, it loads nothing from another site, and
    the text colours that its components pair are readable
  - the tool's files say that they are a developer's tool, and its list of the ant's pictures is the game's own animation
  - the ants: all four teams are ONE sheet (ants.png) that the page draws from CSS offsets; every pixel of it is what the game draws for each team, and the page's sizes, rows, columns and timing
    address the right cells of it, at the game's own pace
  - the panels that the pictures did not draw: the name step (a card with a banner, a field and a refusal line), the
    notices (a name or a code that is refused)
  - the page uses the art: every picture it names is in the folder (and the folder holds nothing it does not use), each <img> has the size of its file, the Map Info lines are the ones in the level
    files, nothing is loaded from another site, the pictures that are not seen at once are lazy, the font is preloaded and the picture's shape is chosen with radio buttons that the keyboard reaches
  - the layout at many widths (what needs no browser) and the contrast of the text of the page's components
"""
import ast
import functools
import hashlib
import json
import os
import re
import struct
import unittest
import zlib

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FRONT = os.path.join(REPO, "web", "front")
TOOL = os.path.join(REPO, "tools", "front_page_art")

# the pictures of the folder and their sizes (width, height): web/lobby.html gives the same sizes to the <img> that shows each of them, or (ants.png, clay.png) to its style
PICTURES = {
    "logo.png": (581, 218), "clay.png": (256, 256), "ants.png": (168, 164),
    "preview_tiny.png": (300, 300), "preview_small.png": (300, 300), "preview_medium.png": (300, 300),
    "preview_gauntlet.png": (300, 300), "preview_treasure.png": (300, 300), "preview_islands.png": (300, 300),
    "qh_quickhelp.png": (257, 453), "qh_power.png": (362, 455),
}
FONT_FILES = ("LibreFranklin-Medium.ttf", "LibreFranklin-OFL.txt")
STYLE_FILES = ("classic.css",)          # the look of the other pages (the front page keeps its own inline copy): not made by the tool, not used by lobby.html
SCRIPT_FILES = ("lobby_rules.js", "lobby_net.js")    # what the lobby page loads, in this order: its rules (pure, no browser) and its client of network protocol 16 (tests/scripts/test_web_lobby_net.py): scripts, not pictures
MAP_KEYS = ("tiny", "small", "medium", "gauntlet", "treasure", "islands")
BUDGET = 1000 * 1000        # bytes: the whole folder (about 0.66 MB: the pictures, the font and the scripts); a picture that grows past this is a decision, not an accident


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


def read_png_pixels(path, alpha=False):
    """(width, height, rows) of a PNG that is not interlaced, each row a list of (r, g, b), or of (r, g, b, alpha) with alpha=True: a palette of any depth (with its tRNS), or 8 bit RGB or RGBA
    (the standard library only)."""
    data = read_bytes(os.path.relpath(path, REPO))
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    pos, idat, palette, transparency = 8, b"", [], []
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
        elif kind == b"tRNS":
            transparency = list(body)                                         # (a palette's alpha, from its first entry on; the entries after the last one are opaque)
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    assert not alpha or ctype == 3 or not transparency, "a PNG with a colour key (tRNS), which this reader does not apply: " + path
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
            picked = [(line[(x * depth) // 8] >> (8 - depth - (x * depth) % 8)) & mask for x in range(width)]
            rows.append([palette[i] + ((transparency[i] if i < len(transparency) else 255,) if alpha else ()) for i in picked])
        else:
            n = 3 if ctype == 2 else 4
            rows.append([tuple(line[x * n:x * n + 3]) + (((line[x * n + 3] if ctype == 6 else 255),) if alpha else ()) for x in range(width)])
    return width, height, rows


def visible(pixel):
    """A pixel as the eye gets it: what is see-through is nothing, whatever colour it was saved with."""
    return pixel if pixel[3] else (0, 0, 0, 0)


@functools.lru_cache(maxsize=None)
def stand_animation():
    """The game's own animation of the standing ant on its start screen (docs/chd_table4_animations.json, the entry agst301: 12 steps, each one picture of ants.chd drawn at a place from the ant's
    origin): the sprites in the order of their first step, where each is drawn, the steps (sprite, milliseconds) and the whole time. A sprite that two steps show is drawn at the same place by both."""
    entries = [e for e in json.loads(read("docs", "chd_table4_animations.json")) if e["name"] == "agst301"]
    assert len(entries) == 1, "the animation of the standing ant"
    entry = entries[0]
    order, at, steps, clashes = [], {}, [], []
    for step in entry["subitems"]:
        assert step["frame_count"] == 1 and step["dx_per_tick"] == 0 and step["dy_per_tick"] == 0, "a step that moves or has more than one picture"
        frame = step["frames"][0]
        number, place = frame["sprite_index"], (frame["dx"], frame["dy"])
        if number not in at:
            order.append(number)
            at[number] = place
        elif at[number] != place:
            clashes.append(number)
        steps.append((number, step["duration_ms"]))
    return {"order": order, "at": at, "steps": steps, "total": entry["total_duration_ms"], "clashes": clashes}


# ---- reading the page's style (the standard library only) ----
def style_of(page):
    """The text of the page's <style>, without its comments."""
    return re.sub(r"/\*.*?\*/", "", page[page.index("<style>") + len("<style>"):page.index("</style>")], flags=re.S)


def css_blocks(css):
    """The top-level pieces of a style: (what stands before the brace, what stands between the braces) for each rule and each @-rule; the body of an @-rule holds its own rules."""
    out, depth, before, start, head = [], 0, 0, 0, ""
    for i, char in enumerate(css):
        if char == "{":
            if depth == 0:
                head, start = css[before:i].strip(), i + 1
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                out.append((head, css[start:i]))
                before = i + 1
    return out


def media(css, query):
    """The text inside every `@media <query> { ... }` of a style, joined."""
    return "\n".join(body for head, body in css_blocks(css) if head == "@media " + query)


class Sheet:
    """The rules of a style that are outside every @-rule (so a colour is the one that the page has before any narrower window changes the layout), by selector and property."""

    def __init__(self, css):
        self.rules = []
        for head, body in css_blocks(css):
            if head.startswith("@"):
                continue
            declarations = {}
            for part in re.split(r";(?![^(]*\))", body):                       # (a ; inside a url( ) is not the end of a declaration)
                if ":" in part:
                    name, value = part.split(":", 1)
                    declarations[name.strip()] = value.strip()
            self.rules.append(([s.strip() for s in head.split(",")], declarations))
        self.tokens = dict(re.findall(r"--([\w-]+): (#[0-9a-f]{6});", css))

    def value(self, selector, prop):
        """The last declaration of `prop` in a rule whose selector list holds `selector`, or None."""
        found = None
        for selectors, declarations in self.rules:
            if selector in selectors and prop in declarations:
                found = declarations[prop]
        return found

    def colours(self, selector, prop):
        """The colours of a declaration as '#rrggbb': a literal, a var(--token) that is a colour, each stop of a gradient (a var that is a picture or a gradient says nothing)."""
        value = self.value(selector, prop) or ""
        out = []
        for found in re.finditer(r"var\(--([\w-]+)\)|#([0-9a-fA-F]{6}|[0-9a-fA-F]{3})\b", value):
            if found.group(1):
                if found.group(1) in self.tokens:
                    out.append(self.tokens[found.group(1)])
            else:
                digits = found.group(2)
                out.append("#" + (("".join(c * 2 for c in digits)) if len(digits) == 3 else digits).lower())
        return out


class TheFolder(unittest.TestCase):
    def test_it_holds_exactly_the_files_that_the_tool_makes_the_shared_stylesheet_and_the_two_scripts(self):
        self.assertEqual(sorted(os.listdir(FRONT)), sorted(list(PICTURES) + list(FONT_FILES) + list(STYLE_FILES) + list(SCRIPT_FILES)))

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

    def test_the_ci_looks_for_the_pictures_the_font_the_scripts_and_the_stylesheet_in_the_image(self):
        ci = read(".github", "workflows", "ci.yml")
        self.assertRegex(ci, r"ls -l index\.html play\.html lobby\.html watch\.html replay_page\.js [^\n]*front/logo\.png front/LibreFranklin-Medium\.ttf front/ants\.png front/lobby_rules\.js front/lobby_net\.js front/classic\.css")

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
        # ?v= and the first 8 hex digits of the file's sha256, in the places that name it (a new tile fails this until they say so)
        version = hashlib.sha256(read_bytes("web", "front", "clay.png")).hexdigest()[:8]
        for parts, count in ((("web", "lobby.html"), 1), (("web", "watch.html"), 1), (("web", "shell.html"), 4), (("web", "front", "classic.css"), 1)):
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
        patterns = re.findall(r'"([\w%.]+\.png)"', text)                           # "clay.png", "ants.png", "preview_%s.png" ...
        expressions = [re.compile(pattern.replace(".", r"\.").replace("%d", r"\d").replace("%s", r"[a-z]+")) for pattern in patterns]
        for name in PICTURES:
            self.assertTrue(any(e.fullmatch(name) for e in expressions), name + " is not made by make_art.py")
        for pattern, expression in zip(patterns, expressions):
            self.assertTrue(any(expression.fullmatch(name) for name in PICTURES), pattern + " is made by make_art.py but is not in the folder")

    def test_the_tool_reads_the_catalog_of_the_repository_and_names_no_path_of_anybodys_machine(self):
        text = read("tools", "front_page_art", "artlib.py")
        self.assertIn('CATALOG = ROOT / "asset_catalog"', text)
        self.assertIn('CHD = ROOT / "Original-Ants" / "ants.chd"', text)
        self.assertNotIn("/home/", text)                                           # no path of anybody's machine

    def test_its_list_of_the_ants_pictures_is_the_animation_of_the_games_standing_ant(self):
        # make_art.STAND says which pictures of ants.chd make the sheet's columns, left to right, and where each is drawn from the ant's origin; the game's own animation (agst301, whose twelve steps show
        # seven pictures) says the same, and the tool's text says what it makes (this test reads the tool's file and does not run it)
        tree = ast.parse(read("tools", "front_page_art", "make_art.py"))
        stand = [ast.literal_eval(node.value) for node in tree.body if isinstance(node, ast.Assign) and [t.id for t in node.targets if isinstance(t, ast.Name)] == ["STAND"]]
        self.assertEqual(len(stand), 1, "make_art.py has one STAND")
        animation = stand_animation()
        self.assertEqual(animation["clashes"], [], "a picture that two steps draw at two places")
        self.assertEqual(list(stand[0].items()), [(number, animation["at"][number]) for number in animation["order"]])
        self.assertEqual(animation["order"], list(range(min(animation["order"]), max(animation["order"]) + 1)), "the pictures of the stand are the archive's neighbours: the tool's text says 'sprites a - b'")
        sheet = "ants.png"
        doc = " ".join(read("tools", "front_page_art", "make_art.py").split('"""')[1].split())
        width, height = PICTURES[sheet]
        columns, rows = len(animation["order"]), 4
        self.assertEqual((width % columns, height % rows), (0, 0))
        self.assertIn("a cell of %d x %d pixels" % (width // columns, height // rows), doc)
        self.assertIn("%d x %d in all" % (width, height), doc)
        self.assertIn("sprites %d - %d" % (min(animation["order"]), max(animation["order"])), doc)
        self.assertIn("one row for each colour (green, red, blue, black)", doc)


class TheRosterAnts(unittest.TestCase):
    """web/front/ants.png is the standing ant of the lobby's colour cards (the game's animation agst301: sprites 1481 - 1487 of Original-Ants/ants.chd) in the colours that the game gives each team: a row
    for each team, a column for each of the seven pictures. The game does not tint an ant: it adds a team's offset to every palette index of the sprite (src/ants_app/renderer.cpp,
    TextureCache::compose_palette), so each team shows another stretch of the same 256 colours. The archive is read here by a reader of this file's own (the tool's is artlib.chd_sprite) and every pixel
    of the sheet is compared with what the game draws. The page shows a cell by CSS (a box the size of a cell, the sheet as its background, moved by whole cells), so the style's numbers are compared too:
    the sizes of the cell and of the sheet, the row of each colour, the column of each step and the time of each step are the game's."""

    SHIFT = {"green": 60, "red": 40, "blue": 20, "black": 0}      # the order is the order of the sheet's rows, of the lobby's colours (seats 0 - 3) and of the renderer's table
    KEY = 254                                   # the palette index that is left transparent

    @classmethod
    def setUpClass(cls):
        data = read_bytes("Original-Ants", "ants.chd")
        version, _stamp, cls.table, _sounds, _tags, _animations, palette_bytes = struct.unpack_from("<7I", data, 0)
        assert version >= 9 and palette_bytes == 1024 and cls.table == 28 + palette_bytes, "an ants.chd of another layout"
        cls.data = data
        cls.palette = [tuple(data[28 + 4 * i:31 + 4 * i]) for i in range(256)]
        cls.animation = stand_animation()
        cls.order = cls.animation["order"]
        cls.at = cls.animation["at"]
        cls.sprites = {number: cls.sprite(number) for number in cls.order}
        cls.left = min(cls.at[n][0] for n in cls.order)
        cls.top = min(cls.at[n][1] for n in cls.order)
        cls.cell_w = max(cls.at[n][0] + cls.sprites[n][1] for n in cls.order) - cls.left        # (a cell holds all seven pictures at their places)
        cls.cell_h = max(cls.at[n][1] + cls.sprites[n][2] for n in cls.order) - cls.top
        cls.sheet_w, cls.sheet_h = cls.cell_w * len(cls.order), cls.cell_h * len(cls.SHIFT)
        cls.style = style_of(read("web", "lobby.html"))
        cls.sheet = Sheet(cls.style)

    @classmethod
    def sprite(cls, number):
        """(file name, width, height, rows of palette indices) of the sprite with this number of the archive."""
        (offset,) = struct.unpack_from("<I", cls.data, cls.table + 4 + 4 * number)
        pitch, width, height, length = struct.unpack_from("<4I", cls.data, offset)
        name = cls.data[offset + 16:offset + 16 + length].rstrip(b"\0").decode("ascii")
        first = offset + 16 + length
        return name, width, height, [list(cls.data[first + y * pitch:first + y * pitch + width]) for y in range(height)]

    def drawn(self):
        """The whole sheet as the game draws it: every row a team, every column a step's picture at its place in the cell; an index is looked up `shift` entries further on (the transparent index, and a
        sprite named by a digit, are not moved), and what the game leaves transparent is nothing."""
        sheet = [[(0, 0, 0, 0)] * self.sheet_w for _ in range(self.sheet_h)]
        for row, team in enumerate(self.SHIFT):
            for column, number in enumerate(self.order):
                name, _width, _height, pixels = self.sprites[number]
                shift = 0 if name[:1].isdigit() else self.SHIFT[team]
                x0 = column * self.cell_w + self.at[number][0] - self.left
                y0 = row * self.cell_h + self.at[number][1] - self.top
                for y, line in enumerate(pixels):
                    for x, index in enumerate(line):
                        if index != self.KEY:
                            source = (index + shift) & 0xFF
                            sheet[y0 + y][x0 + x] = visible(self.palette[source] + (0 if source == self.KEY else 255,))
        return sheet

    def px(self, value):
        """The number of CSS pixels in `calc(<n>px * var(--k))`, or minus that in `calc(-<n>px * var(--k))` (the ants are drawn at a whole number of times their size: --k)."""
        found = re.fullmatch(r"calc\((-?\d+)px \* var\(--k\)\)", value or "")
        self.assertIsNotNone(found, "calc(<n>px * var(--k)) expected, the style says %r" % value)
        return int(found.group(1))

    def test_the_shifts_are_the_ones_of_the_games_renderer(self):
        text = read("src", "ants_app", "renderer.cpp")
        for table in ("kOffset", "kRampOffset"):                                  # (the ant branch of compose_palette, and the HUD branch that the setup screen's portrait of a seat takes)
            found = re.search(table + r"\[4\]\s*=\s*\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}", text)
            self.assertIsNotNone(found, "the renderer no longer says its %s as this test reads it" % table)
            self.assertEqual([int(n) for n in found.groups()], [self.SHIFT[t] for t in ("green", "red", "blue", "black")], "%s: green, red, blue, black, the order of the players' colours" % table)
        tool = read("tools", "front_page_art", "artlib.py")                      # (the tool, which this test does not run, says the same numbers)
        self.assertIn("TEAMS = {%s}" % ", ".join('"%s": %d' % (t, n) for t, n in self.SHIFT.items()), tool)

    def test_the_sprites_are_the_standing_ant_of_the_games_animation(self):
        self.assertEqual(self.animation["clashes"], [])
        self.assertEqual(len(self.animation["steps"]), 12)
        self.assertEqual([number for number, _ms in self.animation["steps"]], [1481, 1482, 1483, 1484, 1485, 1486, 1481, 1482, 1487, 1483, 1484, 1486])
        self.assertEqual(sum(ms for _number, ms in self.animation["steps"]), self.animation["total"])
        self.assertEqual(self.order, [1481, 1482, 1483, 1484, 1485, 1486, 1487])
        self.assertEqual([self.sprites[n][0] for n in self.order], ["agst30%d.bmp" % (i + 1) for i in range(7)])             # (the standing ant of the start screen: not a name that a team's colours skip)
        self.assertEqual(self.sprites[1481][1:3], (23, 40))
        used = set()
        for number in self.order:
            indices = set(i for row in self.sprites[number][3] for i in row) - {self.KEY}
            self.assertTrue(indices <= set(range(80, 94)), "sprite %d draws outside the ant's ramp of 14 colours, the indices that each team shifts" % number)
            used |= indices
        self.assertEqual(sorted(used), list(range(80, 94)), "the ant is one ramp of 14 colours")
        self.assertEqual((self.cell_w, self.cell_h), (24, 41))                      # (what the page's style says, below, and what the tool says: the pictures together are as big as this)
        self.assertEqual((-self.left, -self.top), (11, 29))                           # (the ant's origin lies in the same place of every cell)

    def test_the_sheet_is_a_png_of_the_size_of_its_cells(self):
        self.assertEqual(png_size(os.path.join(FRONT, "ants.png")), (self.sheet_w, self.sheet_h))
        self.assertEqual((self.sheet_w, self.sheet_h), PICTURES["ants.png"])

    def test_every_cell_is_what_the_game_draws_for_its_team_pixel_for_pixel(self):
        width, height, rows = read_png_pixels(os.path.join(FRONT, "ants.png"), alpha=True)
        self.assertEqual((width, height), (self.sheet_w, self.sheet_h))
        rows = [[visible(p) for p in row] for row in rows]
        want = self.drawn()
        for y, (got_row, want_row) in enumerate(zip(rows, want)):
            if got_row != want_row:
                x = next(i for i, (a, b) in enumerate(zip(got_row, want_row)) if a != b)
                team = list(self.SHIFT)[y // self.cell_h]
                number = self.order[x // self.cell_w]
                self.fail("ants.png is not what the game draws: the %s ant, sprite %d, at (%d, %d) of the sheet: it has %s, the game draws %s (python3 tools/front_page_art/make_art.py makes it)"
                          % (team, number, x, y, got_row[x], want_row[x]))
        self.assertEqual(rows, want)

    def test_the_teams_differ_in_their_colours_and_not_in_their_shape(self):
        rows = [[visible(p) for p in row] for row in read_png_pixels(os.path.join(FRONT, "ants.png"), alpha=True)[2]]
        teams = {team: [row[:] for row in rows[i * self.cell_h:(i + 1) * self.cell_h]] for i, team in enumerate(self.SHIFT)}
        shapes = {team: [[p[3] for p in row] for row in cells] for team, cells in teams.items()}
        self.assertEqual(len({str(s) for s in shapes.values()}), 1, "the same pixels are transparent for every team")
        colours = {team: {p[:3] for row in cells for p in row if p[3]} for team, cells in teams.items()}
        for one in self.SHIFT:
            for other in self.SHIFT:
                if one < other:
                    self.assertEqual(colours[one] & colours[other], set(), "%s and %s share no colour of their body" % (one, other))

    def test_the_black_row_is_the_catalogs_pictures_of_the_sprites_as_they_are(self):
        # the catalog's pictures are converted from the archive's paletted bitmaps (docs/ASSET_CATALOG.md): they show that the reader above, and the tool's, read the right indices and the right palette
        _width, _height, rows = read_png_pixels(os.path.join(FRONT, "ants.png"), alpha=True)
        black = list(self.SHIFT).index("black")
        for column, number in enumerate(self.order):
            _w, _h, cut = read_png_pixels(os.path.join(REPO, "asset_catalog", "sprites", "sprite_%04d.png" % number), alpha=True)
            x0 = column * self.cell_w + self.at[number][0] - self.left
            y0 = black * self.cell_h + self.at[number][1] - self.top
            ours = [[visible(p) for p in row[x0:x0 + len(cut[0])]] for row in rows[y0:y0 + len(cut)]]
            self.assertEqual(ours, [[visible(p) for p in row] for row in cut], "the black ant of sprite %d" % number)

    def test_every_page_names_the_sheet_by_its_content(self):
        # docker/nginx.conf lets a browser keep a .png for a week without asking again, so a changed sheet under the same address stays the old one for everybody who has been here: its address ends in
        # ?v= and the first 8 hex digits of the file's sha256 (a new sheet fails this until the page says so)
        page = read("web", "lobby.html")
        version = hashlib.sha256(read_bytes("web", "front", "ants.png")).hexdigest()[:8]
        self.assertEqual(len(re.findall(r"ants\.png", page)), 1, "the page names the ants' sheet once")
        self.assertEqual(len(re.findall(r'url\("front/ants\.png\?v=%s"\)' % version, page)), 1, "the sheet's address ends in ?v=%s" % version)

    def test_the_style_draws_a_cell_of_the_sheet_in_a_box_of_the_cells_size(self):
        sheet = self.sheet
        self.assertEqual(self.px(sheet.value(".ant", "width")), self.cell_w)
        self.assertEqual(self.px(sheet.value(".ant", "height")), self.cell_h)
        background = re.fullmatch(r'url\("front/ants\.png\?v=[0-9a-f]{8}"\) no-repeat 0 0 / (calc\(\d+px \* var\(--k\)\)) (calc\(\d+px \* var\(--k\)\))', sheet.value(".ant", "background") or "")
        self.assertIsNotNone(background, "the ant's background is the sheet from its first cell, not repeated, at k times its size")
        self.assertEqual((self.px(background.group(1)), self.px(background.group(2))), (self.sheet_w, self.sheet_h))      # (the whole sheet, so that a whole number of cells is a whole number of pixels)
        self.assertEqual(sheet.value(".ant", "image-rendering"), "pixelated")
        self.assertEqual(self.px(sheet.value(".slot .antbox", "height")), self.cell_h)                      # (the box that holds it, and the first column of a card that is as wide as a cell and a margin)
        self.assertIn("calc(%dpx * var(--k) + 4px)" % self.cell_w, sheet.value(".slot", "grid-template-columns"))
        # the ants are drawn at a whole number of times their size, so that the pixels stay square (a phone: 2, a wide window: 3 or 4, the token that follows the pointer: 2)
        steps = re.findall(r"--k: ([^;}]+)[;}]", self.style)
        self.assertGreaterEqual(len(steps), 4)
        for step in steps:
            self.assertRegex(step.strip(), r"^[1-9]$", "--k is a whole number")

    def test_the_style_moves_the_sheet_to_the_row_of_each_colour(self):
        ids = re.findall(r"\{ id: '(\w+)', name: '\w+' \}", re.search(r"var COLOURS = \[(.*?)\];", read("web", "front", "lobby_rules.js"), re.S).group(1))
        self.assertEqual(ids, list(self.SHIFT), "the lobby's colours are the sheet's rows in this order")
        for row, colour in enumerate(ids):
            if row == 0:
                self.assertIsNone(self.sheet.value(".ant-" + colour, "background-position-y"), "the first row is where the background starts")
            else:
                self.assertEqual(self.px(self.sheet.value(".ant-" + colour, "background-position-y")), -row * self.cell_h, colour)       # (the sheet moves up by whole rows)
        self.assertEqual(read("web", "lobby.html").count("el('span', 'ant ant-' + c.id)"), 2)                 # (the card's ant and the token that follows the pointer: the class is the colour's id)

    def test_the_style_plays_the_steps_of_the_games_animation_in_the_games_time(self):
        animation = re.fullmatch(r"ant-stand (\d+)ms step-end infinite", self.sheet.value(".ant", "animation") or "")
        self.assertIsNotNone(animation, ".ant plays ant-stand, each picture held until the next (step-end), for ever")
        self.assertEqual(int(animation.group(1)), self.animation["total"])
        body = [b for head, b in css_blocks(self.style) if head == "@keyframes ant-stand"]
        self.assertEqual(len(body), 1)
        stops = re.findall(r"([\d.]+)% \{ background-position-x: (0|calc\(-\d+px \* var\(--k\)\)); \}", body[0])
        self.assertEqual(len(stops), len(self.animation["steps"]), "a stop for each step")
        start = 0
        for (percent, shift), (number, ms) in zip(stops, self.animation["steps"]):
            moved = 0 if shift == "0" else -self.px(shift)
            self.assertEqual(moved % self.cell_w, 0, "a whole cell")
            self.assertEqual(moved // self.cell_w, self.order.index(number), "the step of %d ms at %s%% shows sprite %d" % (ms, percent, number))
            self.assertAlmostEqual(float(percent), 100.0 * start / self.animation["total"], delta=0.0001, msg="where the step of sprite %d starts" % number)
            start += ms
        self.assertEqual(start, self.animation["total"])
        reduced = media(self.style, "(prefers-reduced-motion: reduce)")
        self.assertIn(".ant { animation: none; }", reduced)                                  # (a visitor who asked for less motion gets the first picture)
        self.assertEqual((self.sheet.value(".chip .ant", "animation"), self.sheet.value(".chip .ant", "background-position-x")), ("none", "0"))      # (and the token that follows the pointer shows the first picture: it is a still)


class ThePageUsesTheArt(unittest.TestCase):
    def setUp(self):
        self.page = read("web", "lobby.html")
        self.style = style_of(self.page)
        self.rules = read("web", "front", "lobby_rules.js")

    def maps(self):
        """(key, name, file, info) of each map of lobby_rules.js, in its order."""
        block = re.search(r"var MAPS = \[(.*?)\n    \];", self.rules, re.S).group(1)
        return [(m[0], m[1], m[2], m[3] or m[4]) for m in re.findall(r"\{ key: '([a-z]+)', name: '([A-Za-z]+)', file: '([A-Z]+\.LVL)', info: (?:'([^']*)'|\"([^\"]*)\") \}", block)]

    def test_every_picture_that_the_page_names_is_in_the_folder_and_every_picture_is_used(self):
        named = set(re.findall(r"front/([\w.-]+\.(?:png|ttf))", self.page))                 # the markup and the style name them; the script builds preview_<map>.png from a map's key
        previews = set("preview_%s.png" % key for key in MAP_KEYS)
        in_folder = set(os.listdir(FRONT)) - {"LibreFranklin-OFL.txt"} - set(STYLE_FILES) - set(SCRIPT_FILES)         # (the stylesheet is the other pages': tests/scripts/test_web_pages_classic.py; the scripts are not pictures)
        self.assertEqual(named - in_folder, set(), "named but not in web/front/")
        self.assertEqual(in_folder - named - previews, set(), "in web/front/ but not used by the page")
        self.assertIn("'front/preview_' + side.mapKey + '.png'", self.page)

    def test_every_img_has_the_size_of_its_file(self):
        tags = re.findall(r"<img [^>]*>", self.page)
        sources = sorted(re.search(r'src="front/([^"?]+)', tag).group(1) for tag in tags)
        self.assertEqual(sources, ["logo.png", "preview_treasure.png", "qh_power.png", "qh_quickhelp.png"])      # (the logo, the map's picture and the two help sheets; the ants are not <img>: the style draws them from ants.png, TheRosterAnts)
        for tag in tags:
            source = re.search(r'src="front/([^"?]+)"', tag).group(1)                      # (no ?v= on these: a picture that changes is a new file name or a new page)
            width = int(re.search(r'width="(\d+)"', tag).group(1))
            height = int(re.search(r'height="(\d+)"', tag).group(1))
            self.assertEqual(png_size(os.path.join(FRONT, source)), (width, height), source)
            self.assertIn("alt=", tag, source)
        shown = re.search(r'<img id="preview"[^>]*>', self.page).group(0)                   # (the script puts the map's six pictures in this one, all of one size)
        for key in MAP_KEYS:
            self.assertEqual(png_size(os.path.join(FRONT, "preview_%s.png" % key)), (int(re.search(r'width="(\d+)"', shown).group(1)), int(re.search(r'height="(\d+)"', shown).group(1))), key)

    def test_the_maps_picture_is_shown_at_its_own_size_and_square(self):
        width = png_size(os.path.join(FRONT, "preview_treasure.png"))[0]
        self.assertEqual(Sheet(self.style).value(".tv", "width"), "%dpx" % width)           # (a wide window: one picture pixel for each pixel of the screen)
        self.assertEqual(Sheet(self.style).value(".tv img", "aspect-ratio"), "1")

    def test_nothing_is_loaded_from_another_site(self):
        scripts = re.findall(r'<script[^>]*\bsrc="([^"]*)"', self.page)
        self.assertEqual(scripts, ["front/" + name for name in SCRIPT_FILES])               # (the page's own two scripts, the folder's, in this order and no other)
        self.assertEqual(len(re.findall(r"<script\b", self.page)), len(scripts) + 1)       # (and the one inline script that is the page's)
        self.assertNotRegex(self.page, r'<script[^>]*type="module"')
        for name in SCRIPT_FILES:
            self.assertNotRegex(read("web", "front", name), r"https?://", name + " names no other site")
        self.assertNotRegex(self.page, r'<link[^>]*href="(?:https?:)?//')
        self.assertNotRegex(self.page, r"url\(\s*[\"']?(?:https?:)?//")
        self.assertNotIn("@import", self.page)
        self.assertNotRegex(self.page, r"<(?:img|iframe|video|audio|source)[^>]*src=\"(?:https?:)?//")
        for target in re.findall(r"url\(\s*[\"']?([^\"')]+)", self.style):                  # (the style's addresses: a file of this folder, its address may end in ?v=, or the select's own arrow, drawn in the page)
            self.assertTrue(target.startswith("data:image/svg+xml,") or (target.startswith("front/") and target[len("front/"):].split("?")[0] in os.listdir(FRONT)), target)
        for url in set(re.findall(r'href="(https?://[^"]+)"', self.page)):                  # (the links that leave the page are the repository's)
            self.assertTrue(url.startswith("https://github.com/dchadd427/ants-cross-platform"), url)

    def test_the_map_info_lines_are_the_ones_of_the_level_files(self):
        maps = self.maps()
        self.assertEqual([m[0] for m in maps], list(MAP_KEYS))
        for key, _name, file, info in maps:
            self.assertEqual(file, key.upper() + ".LVL")
            with open(os.path.join(REPO, "Original-Ants", "Maps", file), "rb") as f:
                head = f.read(40)
            version, mode, minutes = struct.unpack("<IIH", head[:10])
            description = head[10:40].split(b"\0")[0].decode("latin-1")
            self.assertEqual(info, "%s (%d min)" % (description, minutes), key)               # what the setup screen shows in its Map Info box
        self.assertIn("mapInfo: m.info", self.rules)                                         # (and the line under the map is that text, as it is)
        self.assertIn("setText($('info'), side.mapInfo);", self.page)

    def test_the_six_maps_are_the_page_s_list_in_its_order_with_a_preview_each(self):
        maps = self.maps()
        self.assertEqual([m[0] for m in maps], list(MAP_KEYS))
        self.assertEqual([m[1] for m in maps], [key.capitalize() for key in MAP_KEYS])
        for key in MAP_KEYS:
            self.assertIn("preview_%s.png" % key, os.listdir(FRONT))
        self.assertIn("Rules.MAPS.forEach(function (m) { var o = el('option', '', m.name); o.value = m.key; $('map').appendChild(o); });", self.page)      # (the drop-down is the rules' list)
        default = re.search(r"var DEFAULT_MAP_KEY = '([a-z]+)';", self.rules).group(1)
        self.assertIn(default, MAP_KEYS)
        self.assertIn('<img id="preview" src="front/preview_%s.png" alt="The map: %s"' % (default, default.capitalize()), self.page)       # (the picture that the page opens with is the default map's)

    def test_the_pictures_that_are_not_seen_at_once_load_lazily_and_the_ones_that_are_do_not(self):
        tags = re.findall(r"<img [^>]*>", self.page)
        lazy = set(re.search(r'src="front/([^"]+)"', t).group(1) for t in tags if 'loading="lazy"' in t)
        at_once = set(re.search(r'src="front/([^"]+)"', t).group(1) for t in tags if 'loading="lazy"' not in t)
        self.assertEqual(lazy, {"qh_quickhelp.png", "qh_power.png"})                       # the two help sheets (behind "How it works")
        self.assertEqual(at_once, {"logo.png", "preview_treasure.png"})                    # the logo and the map are on the screen at once
        for tag in tags:
            if 'loading="lazy"' in tag:
                self.assertIn('decoding="async"', tag)

    def test_the_font_is_declared_and_preloaded_from_the_one_file(self):
        self.assertIn('<link rel="preload" href="front/LibreFranklin-Medium.ttf" as="font" type="font/ttf" crossorigin>', self.page)
        self.assertRegex(self.page, r'@font-face \{ font-family: "Libre Franklin"; src: url\("front/LibreFranklin-Medium\.ttf"\) format\("truetype"\);')

    def test_the_pictures_shape_buttons_are_radio_buttons_that_the_keyboard_reaches_and_shows(self):
        sheet = Sheet(self.style)
        self.assertEqual(sheet.value(".shape .seg input", "opacity"), "0")                   # hidden by being see-through: it keeps the focus, the arrows and Tab
        for prop in ("display", "visibility", "pointer-events"):
            self.assertIsNone(sheet.value(".shape .seg input", prop), prop)
        for tag in re.findall(r'<input type="radio"[^>]*>', self.page):
            self.assertNotRegex(tag, r"tabindex|hidden|disabled")
            self.assertRegex(self.page, r'<label for="%s"[ >]' % re.search(r'id="([^"]+)"', tag).group(1))      # (each one has the label that is its button)
        self.assertEqual(len(re.findall(r'<input type="radio" name="aspect"', self.page)), 4)             # (the four shapes of the game page's selector, in its order)
        self.assertIn('role="radiogroup" aria-label="Picture shape"', self.page)
        self.assertEqual(sheet.value(".shape .seg input:focus-visible + label", "outline"), "3px solid var(--gold)")        # the focused button shows it
        self.assertIsNotNone(sheet.value(".shape .seg input:checked + label", "background"))                                 # and so does the chosen one

    def test_the_footer_tags_the_shape_nearest_the_screen_only_for_a_mouse_and_leaves_room_for_the_tag(self):
        """The picture draws "fills your screen" under one shape's button (the script un-hides one tag, and the footer gets the room for it); a phone or a tablet (no hover) shows no tag and no extra room."""
        tags = re.findall(r'<span class="fit-tag" id="fit-([0-9-]+)" hidden>fills your screen</span>', self.page)
        self.assertEqual(tags, ["4-3", "16-10", "16-9", "21-9"])                                         # every tag starts hidden, one per shape, left to right
        self.assertEqual(self.style.count(".fit-tag[hidden] { display: none; }"), 1)
        self.assertIn(".bar.has-fit-tag { padding-bottom: 34px; }", self.style)
        self.assertRegex(self.style, r"@media \(hover: none\) \{ \.fit-tag \{ display: none; \} \.bar\.has-fit-tag \{ padding-bottom: calc\(14px \+ env\(safe-area-inset-bottom, 0px\)\); \} \}")
        # the touch rule is the LATER one (same specificity as the 34 px rule it overrides), and the last button's tag ends with the button (a centred one sticks out of the bar at the right)
        self.assertGreater(self.style.index("@media (hover: none) { .fit-tag { display: none; }"), self.style.index(".bar.has-fit-tag { padding-bottom: 34px; }"))
        self.assertIn(".shape .opt:last-child .fit-tag { left: auto; right: 0; transform: none; }", self.style)
        self.assertIn('<footer class="bar" id="footer-bar">', self.page)
        self.assertEqual(Sheet(self.style).value(".shape", "flex-wrap"), "wrap")                         # a 320 px window: the label above the four buttons, nothing pushed past the edge


class ThePanelsThatTheMockupDidNotDraw(unittest.TestCase):
    def setUp(self):
        self.page = read("web", "lobby.html")
        self.style = style_of(self.page)
        self.sheet = Sheet(self.style)

    def test_the_name_step_is_a_card_on_the_clay_with_a_banner_a_field_a_button_and_a_refusal_line(self):
        self.assertIn('<div class="name-modal" id="name-step" role="dialog" aria-modal="true" aria-labelledby="name-step-title" hidden>', self.page)
        self.assertIn('<div class="name-step-title" id="name-step-title">', self.page)
        self.assertIn('<label for="name-step-input">Your name</label>', self.page)
        self.assertRegex(self.page, r'<input type="text" id="name-step-input"[^>]*maxlength="32"[^>]*aria-describedby="name-step-msg name-step-hint">')           # (the refusal and the hint are read with the field)
        self.assertIn('<button type="button" id="name-step-go" class="btn">Join</button>', self.page)
        self.assertIn('<div class="name-step-msg" id="name-step-msg" role="alert"></div>', self.page)                                    # (a name that is refused is said aloud, in the red line under the field)
        self.assertIn('id="name-step-own"', self.page)
        self.assertEqual(self.sheet.colours(".name-step-title", "color"), [self.sheet.tokens["gold"]])                                      # (the banner of the game's pages: gold on teal, with the bevel)
        self.assertEqual(self.sheet.colours(".name-step-title", "background"), [self.sheet.tokens["teal"]])
        self.assertIn("var(--sheen)", self.sheet.value(".name-step-title", "background"))
        self.assertIn("var(--bevel)", self.sheet.value(".name-step-title", "box-shadow"))
        self.assertEqual(self.sheet.colours(".name-step", "background"), [self.sheet.tokens["clay"]])                                       # (the card lies on the same clay as the page, as the game page's own does)
        self.assertIn("var(--tile)", self.sheet.value(".name-step", "background"))
        for prop, token in (("color", "bad-ink"), ("background", "bad-bg"), ("border-left", "bad-edge")):                                   # (a refused name is the page's refusal red: the tokens of every refusal of the pages)
            self.assertEqual(self.sheet.colours(".name-step-msg", prop), [self.sheet.tokens[token]], prop)
        self.assertEqual(self.sheet.value(".name-step-msg:empty", "display"), "none")                                                       # (and takes no room when there is nothing to say)
        self.assertIn("NAME_MAX = 32;", self.page)

    def test_a_notice_is_a_notice_and_a_hint_is_a_hint(self):
        self.assertRegex(self.page, r'<p class="hint" id="joinhint" role="status">')                                 # (the line under the field of a room code that does not lead anywhere)
        self.assertIn('aria-describedby="joinhint"', self.page)
        for selector in (".hint:empty", ".name-step-msg:empty"):
            self.assertEqual(self.sheet.value(selector, "display"), "none", selector)
        self.assertIn('id="banner" role="status"', self.page)                                                        # (the strip at the top: what the page changed for you, or what did not work)


class TheCardsAtManyWidths(unittest.TestCase):
    """What the style says for the windows from a 320 px phone to a wide screen, read without a browser: the columns of the room, the colour cards, the map's side and START, the lines that wrap, and the
    order of the rules that lets the narrower ones win."""

    def setUp(self):
        self.page = read("web", "lobby.html")
        self.style = style_of(self.page)
        self.sheet = Sheet(self.style)
        self.phone = media(self.style, "(max-width: 720px)")

    def test_the_room_has_two_columns_from_1041_px_and_one_under_it(self):
        self.assertEqual(self.sheet.value(".grid", "grid-template-columns"), "minmax(0, 1fr) 340px")             # (the room, and the map's side: the picture, its drop-down, START)
        narrower = media(self.style, "(max-width: 1040px)")
        self.assertIn(".grid { grid-template-columns: minmax(0, 1fr);", narrower)
        self.assertIn(".side { grid-template-columns: minmax(0, 300px) minmax(0, 1fr);", narrower)                 # (one column: the map's picture beside its drop-down and START)
        self.assertIn(".side .maprow { grid-row: span 2; }", narrower)
        self.assertIn(":root { --k: 4; }", media(self.style, "(min-width: 1041px) and (min-height: 880px)"))        # (the ants 4 times their size instead of 3, only where both the width and the height have room)
        self.assertEqual(self.sheet.value(".wrap", "max-width"), self.sheet.value(".bar-in", "max-width"))          # (the footer lines up with the page)

    def test_the_four_colours_lie_black_green_red_blue_in_two_columns_and_one_on_a_phone(self):
        rules = read("web", "front", "lobby_rules.js")
        self.assertIn("var GRID = [3, 0, 1, 2];", rules)                                                      # (seats are Green 0, Red 1, Blue 2, Black 3: the cards lie as the four hills lie)
        self.assertIn("GRID.forEach(function (seat) { paintCard(v.cards[seat], m); });", self.page)             # (built in this reading order, so that Tab and a screen reader follow what is on screen)
        for selector in (".slot", ".slots", ".slot .antbox", ".slot .txt"):
            self.assertIsNone(self.sheet.value(selector, "order"), selector + " has no CSS order")
        self.assertEqual(self.sheet.value(".slots", "grid-template-columns"), "minmax(0, 1fr) minmax(0, 1fr)")
        self.assertIn(".slots { grid-template-columns: minmax(0, 1fr);", self.phone)                           # (the four cards one under the other, in the same order)
        self.assertIn(".slot { grid-template-columns: calc(24px * var(--k) + 4px) minmax(0, 1fr) 36px;", self.phone)     # (the dots get a column of their own, so they never sit on a name or a drop-down; 36 px wide and 44 high, so the names keep room beside the narrower cards)
        self.assertIn("body.guest .slot { grid-template-columns: calc(24px * var(--k) + 4px) minmax(0, 1fr);", self.phone)       # (a player has no dots: no column for them)
        self.assertIn(":root { --k: 2; }", self.phone)
        self.assertEqual(self.sheet.value(".grip", "position"), "absolute")                                    # (the dots lie on the card's corner, and on a phone they are a column of their own)
        self.assertIn(".slot .grip { position: static;", self.phone)

    def test_the_media_rules_come_in_the_order_that_lets_the_narrower_ones_win_and_the_two_sides_of_a_break_agree(self):
        order = [self.style.index("@media (%s)" % query) for query in ("max-width: 1040px", "max-width: 720px", "max-width: 374px", "max-width: 339px")]
        self.assertEqual(order, sorted(order), "the narrower rules come later: they win")
        self.assertIn("@media (min-width: 1041px) and (min-height: 880px)", self.style)                         # (1040 and 1041: the same break, seen from both sides)
        self.assertIn(".share { display: none; }", media(self.style, "(min-width: 721px)"))                    # (720 and 721: the Share button is for phones, and the script shows it only where a share sheet exists)
        self.assertIn(".share { display: inline-flex; }", self.phone)
        self.assertIn('<button class="btn share" type="button" id="share" hidden>Share</button>', self.page)

    def test_a_phone_puts_the_map_beside_its_picture_and_keeps_start_in_reach(self):
        self.assertIn(".side { display: contents; }", self.phone)                                              # (the map and START are two rows of the page's own grid, so that START can stay at the bottom)
        self.assertIn(".side .maprow { grid-row: auto; grid-template-columns: 112px minmax(0, 1fr);", self.phone)
        self.assertIn(".go { position: sticky; bottom: 0;", self.phone)
        self.assertIn("calc(10px + env(safe-area-inset-bottom, 0px))", self.phone)                              # (clear of the home bar of a phone: the page asks for the whole screen)
        self.assertIn("viewport-fit=cover", self.page)
        self.assertIn(".plan-long { display: none; } .plan-short { display: inline; }", self.phone)             # (the line under START is one short line, so that START and the line stay a small bar)
        self.assertIn(".go .plan { font-size: 14px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }", self.phone)
        self.assertIn(".info { text-align: left; grid-column: 2; }", self.phone)

    def test_the_invitation_and_the_line_to_join_wrap_on_a_phone_and_never_scroll_sideways(self):
        self.assertEqual(self.sheet.value(".invite", "grid-template-columns"), "minmax(0, 1fr) auto auto")        # (the link, Copy, Share)
        self.assertIn(".invite { grid-template-columns: minmax(0, 1fr) auto; }", self.phone)
        self.assertIn(".invite input { display: none; }", self.phone)                                         # (a phone: the buttons only; a long link would be a field that nobody reads on a phone)
        self.assertIn(".invite #copy { width: 100%; }", self.phone)
        self.assertEqual(self.sheet.value(".invite input", "width"), "100%")
        self.assertEqual(self.sheet.value(".invite input", "text-overflow"), "ellipsis")                        # (a field can shrink under its row: a long link never makes the page wider)
        self.assertEqual(self.sheet.value(".joinbox input", "min-width"), "0")
        self.assertEqual(self.sheet.value(".joinbox input", "flex"), "1")
        for selector in (".invite-note", ".invite-note .rt", ".rejoin", ".banner"):                           # (the lines of the page wrap: the note under the link, the strips)
            self.assertEqual(self.sheet.value(selector, "flex-wrap"), "wrap", selector)
        self.assertIn(".head { margin-bottom: 10px; flex-wrap: wrap; }", self.phone)

    def test_a_long_name_wraps_instead_of_widening_the_page(self):
        # (a name of 32 capital letters once widened the cards by 19 px: every box on the way from the page to the name may shrink, and the word breaks)
        for selector in (".pname", ".rmask span", ".name-step-title", ".go .plan"):
            self.assertEqual(self.sheet.value(selector, "overflow-wrap"), "anywhere", selector)
        for selector in (".slot .txt", ".pname", ".pname input"):
            self.assertEqual(self.sheet.value(selector, "min-width"), "0", selector)
        for selector in (".slots", ".grid", ".side"):
            self.assertIn("minmax(0, 1fr)", self.sheet.value(selector, "grid-template-columns"), selector)
        self.assertIn("minmax(0, 1fr)", self.sheet.value(".slot", "grid-template-columns"))                      # (the card's text column is a minmax(0, 1fr) track, not a content-sized one)
        self.assertEqual(self.sheet.value(".pname input", "width"), "100%")

    def test_a_narrow_phone_keeps_the_maps_name_whole(self):
        # the map's name (Gauntlet, Treasure) is not cut off at 320 px: under 375 px a smaller picture and smaller arrows, and under 340 px a smaller name (a browser measures it)
        narrow = media(self.style, "(max-width: 374px)")
        self.assertIn(".side .maprow { grid-template-columns: 96px minmax(0, 1fr);", narrow)
        self.assertIn(".tv { width: 96px; }", narrow)
        self.assertIn(".maprow .pick { grid-template-columns: 36px minmax(0, 1fr) 36px; gap: 4px; }", narrow)
        self.assertIn(".pick select { font-size: 16px; padding-left: 6px; padding-right: 26px;", narrow)
        self.assertIn(".pick select { font-size: 15px; padding-left: 4px; }", media(self.style, "(max-width: 339px)"))
        self.assertGreater(int(re.search(r"\.tv \{[^}]*(?<![-\w])width: (\d+)px;", self.phone).group(1)), 96)           # (a phone of 375 px or more keeps the bigger one)


class TheColours(unittest.TestCase):
    """The text of the page's components is at least 4.5:1 against its background, in every state of the buttons, banners and cards. Each pair is the colour of one rule against the background of another,
    read from the style, so a colour that changes is measured again; a pair whose rule is gone fails instead of passing by default."""

    def setUp(self):
        page = read("web", "lobby.html")
        self.style = style_of(page)
        self.sheet = Sheet(self.style)

    def token(self, name):
        return self.sheet.tokens[name]

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

    def check(self, pairs):
        """Each pair is (what, (selector, property) of the text's colour, (selector, property) of the background): every colour of the one against every colour of the other (the stops of a gradient)."""
        for what, (text_selector, text_prop), (back_selector, back_prop) in pairs:
            with self.subTest(what):
                text, back = self.sheet.colours(text_selector, text_prop), self.sheet.colours(back_selector, back_prop)
                self.assertTrue(text, "%s: the style no longer says %s of %s" % (what, text_prop, text_selector))
                self.assertTrue(back, "%s: the style no longer says %s of %s" % (what, back_prop, back_selector))
                for one in text:
                    for other in back:
                        self.assertGreaterEqual(self.ratio(one, other), 4.5, "%s: %s on %s" % (what, one, other))

    def test_the_banners_buttons_and_hovered_buttons_keep_their_text_readable(self):
        self.check((
            ("gold title on the name step's teal banner", (".name-step-title", "color"), (".name-step-title", "background")),
            ("cream text on a teal button", (".btn", "color"), (".btn", "background")),
            ("cream text on a hovered button", (".btn", "color"), (".btn:hover", "background-color")),
            ("cream text on a shape button", (".shape .seg label", "color"), (".shape .seg label", "background")),
            ("cream text on a hovered shape button", (".shape .seg label", "color"), (".shape .seg label:hover", "background-color")),
            ("the chosen shape button", (".shape .seg input:checked + label", "color"), (".shape .seg input:checked + label", "background")),
        ))

    def test_the_black_boxes_keep_their_text_readable(self):
        self.check((
            ("cream text in the black box", (".panel", "color"), (".panel", "background")),
            ("cream text in a strip", (".banner", "color"), (".banner", "background")),
            ("cream text in the strip of a game to rejoin", (".rejoin", "color"), (".rejoin", "background")),
            ("cream text of a toast", (".toast", "color"), (".toast", "background")),
            ("cream text in the link's field", (".invite input", "color"), (".invite input", "background")),
            ("cream text in the code's field", (".joinbox input", "color"), (".joinbox input", "background")),
            ("cream text in the name step's field", (".name-step input", "color"), (".name-step input", "background")),
            ("the hint in an empty field of the name step", (".name-step input::placeholder", "color"), (".name-step input", "background")),
            ("cream text of a colour's mode", (".mode", "color"), (".mode", "background")),
            ("cream text of the map's drop-down", (".pick select", "color"), (".pick select", "background")),
            ("the room code", (".code", "color"), (".panel", "background")),
            ("the room code's box", (".code b", "color"), (".code b", "background")),
            ("the note under the link", (".invite-note", "color"), (".panel", "background")),
            ("the links under the link", (".invite-note button", "color"), (".panel", "background")),
            ("a hint", (".hint", "color"), (".panel", "background")),
            ("the note under the colours", (".slots-note", "color"), (".panel", "background")),
            ("a refused team", (".teams-note.warn", "color"), (".panel", "background")),
            ("the name step's refusal", (".name-step-msg", "color"), (".name-step-msg", "background")),
        ))

    def test_the_cards_of_the_colours_keep_their_text_readable_in_every_state(self):
        backgrounds = (".slot", ".slot.k-open", ".slot.k-nobody", ".slot.drop")                  # a card, an open colour, a colour that is out, a card that a player is dragged over
        accents = {".slot": self.sheet.colours(".slot", "--accent")}
        for colour in re.findall(r"\.slot\[data-c=\"(\w+)\"\]", self.style):
            accents[colour] = self.sheet.colours('.slot[data-c="%s"]' % colour, "--accent")
        self.assertEqual(sorted(accents), sorted([".slot", "red", "blue", "black"]), "an accent for each colour (green is the card's own)")
        for name, accent in accents.items():
            for background in backgrounds:
                for one in accent:
                    for other in self.sheet.colours(background, "background"):
                        self.assertGreaterEqual(self.ratio(one, other), 4.5, "the name of the colour %s (%s) on %s" % (name, one, background))
        texts = (("a player's name and the card's words", ".slot", "color"), ("the state of a player", ".pstat", "color"), ("a player who is in the game", ".pstat.in", "color"),
                 ("a player whose game opens", ".pstat.wait", "color"), ("the pencil", ".edit", "color"), ("the pencil under the pointer", ".edit:hover", "color"),
                 ("the question of Remove", ".rmask span", "color"), ("the chip's small line", ".chip small", "color"))
        for what, selector, prop in texts:
            for background in backgrounds:
                for one in self.sheet.colours(selector, prop):
                    for other in self.sheet.colours(background, "background"):
                        self.assertGreaterEqual(self.ratio(one, other), 4.5, "%s on %s" % (what, background))
        self.check((
            ("an open colour's words", (".slot.k-open .pname", "color"), (".slot.k-open", "background")),
            ("a colour that is out", (".slot.k-nobody .pname", "color"), (".slot.k-nobody", "background")),
            ("a computer player's name", (".slot.k-bot .pname", "color"), (".slot", "background")),
            ("the name that is typed", (".pname input", "color"), (".pname input", "background")),
            ("the tag YOU", (".you", "color"), (".you", "background")),
            ("Remove", (".rm", "color"), (".rm", "background")),
            ("Remove under the pointer", (".rm:hover", "color"), (".rm:hover", "background")),
            ("the question's Remove", (".rmask .rm.yes", "color"), (".rmask .rm.yes", "background")),
            ("the question's Remove under the pointer", (".rmask .rm.yes", "color"), (".rmask .rm.yes:hover", "background")),
            ("Keep", (".rmask .rm.keep", "color"), (".rmask .rm.keep", "background")),
            ("Keep under the pointer", (".rmask .rm.keep", "color"), (".rmask .rm.keep:hover", "background")),
            ("the tab of a colour", (".slot .tab", "color"), (".slot .tab", "background")),
            ("the line that says where a player drops", (".slot.drop::after", "color"), (".slot.drop::after", "background")),
            ("the line that says where a tapped player goes", (".slot.picked-target::after", "color"), (".slot.picked-target::after", "background")),
            ("the token that follows the pointer", (".chip", "color"), (".chip", "background")),
            ("a team button", (".teamset button", "color"), (".teamset button", "background")),
            ("a team button under the pointer", (".teamset button:hover", "color"), (".teamset button", "background")),
            ("a team button that is lit", (".teamset button[aria-pressed=\"true\"]", "color"), (".teamset button[aria-pressed=\"true\"]", "background")),
            ("a team button that is refused", (".teamset button[aria-disabled=\"true\"]", "color"), (".teamset button[aria-disabled=\"true\"]", "background")),
        ))

    def test_the_footer_keeps_its_text_readable(self):
        self.check((
            ("the footer's text and links", (".bar", "color"), (".bar", "background")),
            ("the recording notice", (".bar .notice", "color"), (".bar", "background")),
            ("the version", (".bar .ver", "color"), (".bar .ver", "background")),
        ))
        self.assertEqual(len(self.sheet.colours(".bar", "background")), 2)                                  # (both ends of the bar are measured)

    def test_the_ink_stays_readable_on_the_clay_wherever_the_page_sets_it_there(self):
        # the text that sits on the bare clay or on the name step's card is the ink of the pages (the tile itself, the worst place of it: TheClay)
        self.check((
            ("the name step's label", (".name-step label", "color"), (":root", "--clay")),
            ("the name step's hint", (".name-step-hint", "color"), (":root", "--clay")),
            ("the line under START", (".go .plan", "color"), (":root", "--clay")),
        ))
        self.assertEqual(self.sheet.colours("body", "color"), [self.token("ink")])                           # (and the page's own text, where nothing says another colour, is that ink)
        self.assertGreaterEqual(self.ratio(self.token("ink"), self.token("clay")), 4.5)


if __name__ == "__main__":
    unittest.main()
