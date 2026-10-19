#!/usr/bin/env python3
"""The map maker's sprite atlas (tools/map_atlas.py; docs/MAP_EDITOR.md, "Sprites"; run by ./run_tests.sh --tools and by the CI): the game's pictures of the ground and of the objects that the editor offers,
made from the public decoded sprites and the animation table of the sprite viewer's data, and the places that carry them.

  - the atlas is built here (standard library only, as the image's build does it) and read back with a PNG reader of this file: every one of the 239 ground tiles (ids 431 to 669) has its picture, pixel for
    pixel the sprite of frame 0 of its animation; every object that the editor offers (rocks, power-ups, bombs, hills, the food piles' stages, the 29 bigger objects, the starting ant) is in the sheet with the
    offset of that frame and its own size, none overlaps another, and a picture the sheet does not hold is in the index with its sprite number (the page fetches it from /asset_catalog/sprites/);
  - the index: the average colors, the numbers that the page reads, the sizes of the files (small), the same bytes every time, a palette of more than 255 colors refused;
  - what the library of maps puts on layer 2: every anchored tile has a picture (the sheet or the index) but the ones of the menus and the stray ground tiles, which the page draws nothing for;
  - the places: the Dockerfile makes the atlas after the compile layers and copies it, .dockerignore lets the animation table in, the local web build makes it, nginx revalidates it, the CI's check of the
    image lists it, THIRD_PARTY_NOTICES.md names it, and the tool reads nothing but the two public inputs (never the original program).
"""
import importlib.util
import json
import os
import re
import struct
import sys
import tempfile
import unittest
import zlib

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
spec = importlib.util.spec_from_file_location("map_atlas", os.path.join(REPO, "tools", "map_atlas.py"))
map_atlas = importlib.util.module_from_spec(spec)
spec.loader.exec_module(map_atlas)
SPRITES = os.path.join(REPO, "asset_catalog", "sprites")
TABLE = os.path.join(REPO, "docs", "chd_table4_animations.json")                     # the table as the docs keep it: the truth that the tool's own input (the viewer's data) is held to
CATALOG = os.path.join(REPO, "asset_catalog", "catalog_data.js")
EMPTY2 = 0x7FFE
# the ids that the library anchors on layer 2 and the page draws nothing for: elements of the game's menus and screens (ids below 34 and a few others), a cursor, two ground ids that stray onto layer 2
NOT_DRAWN = {0, 1, 46, 47, 48, 55, 59, 60, 61, 69, 70, 80, 88, 97, 98, 135, 154, 164, 227, 243, 244, 476, 485}


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def indexed(path):
    """(width, height, rows of (r, g, b, a)) of an 8-bit indexed PNG (every filter type that a writer may use)"""
    with open(path, "rb") as f:
        data = f.read()
    at, idat, palette, trns, header = 8, [], [], b"", None
    while at < len(data):
        n, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + n]
        if zlib.crc32(kind + body) & 0xFFFFFFFF != struct.unpack(">I", data[at + 8 + n:at + 12 + n])[0]:
            raise AssertionError("a chunk of %s has a bad checksum" % path)
        at += 12 + n
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            palette = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif kind == b"tRNS":
            trns = body
        elif kind == b"IDAT":
            idat.append(body)
    w, h, depth, color, _c, _f, interlace = header
    assert (depth, color, interlace) == (8, 3, 0), "an 8-bit indexed PNG, not interlaced"
    raw, prev, rows = zlib.decompress(b"".join(idat)), bytearray(w), []
    for y in range(h):
        kind, line = raw[y * (w + 1)], bytearray(raw[y * (w + 1) + 1:(y + 1) * (w + 1)])
        for i in range(w):
            a, b, c = (line[i - 1] if i else 0), prev[i], (prev[i - 1] if i else 0)
            if kind == 1:
                line[i] = (line[i] + a) & 255
            elif kind == 2:
                line[i] = (line[i] + b) & 255
            elif kind == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif kind == 4:
                line[i] = (line[i] + map_atlas_paeth(a, b, c)) & 255
        prev = line
        rows.append([(palette[v] + (trns[v] if v < len(trns) else 255,)) for v in line])
    return w, h, rows


def map_atlas_paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)


def sprite_pixels(number):
    w, h, rgba = map_atlas.read_png(os.path.join(SPRITES, "sprite_%04d.png" % number))
    return w, h, [[tuple(rgba[4 * (y * w + x):4 * (y * w + x) + 4]) for x in range(w)] for y in range(h)]


def library_anchors():
    """{tile id: number of anchored cells} of layer 2 over the 540 maps of the library, read with struct"""
    found = {}
    for folder in ("Original-Ants/Maps", "Community-Maps"):
        for name in sorted(os.listdir(os.path.join(REPO, folder))):
            if not name.lower().endswith(".lvl"):
                continue
            with open(os.path.join(REPO, folder, name), "rb") as f:
                b = f.read()
            version = struct.unpack_from("<I", b, 0)[0]
            at = 40 if version == 8 else 18 + struct.unpack_from("<H", b, 16)[0]
            at += 2 + 11 * (struct.unpack_from("<H", b, at)[0] + 1)
            rows, columns = struct.unpack_from("<II", b, at)
            if version == 8:
                rows, columns = rows & 0xFFFF, columns & 0xFFFF
            at += 8
            layer2 = at + 6 * rows * columns
            for i in range(rows * columns):
                tile, flags = struct.unpack_from("<HH", b, layer2 + 6 * i)
                if tile != EMPTY2 and flags & 1:
                    found[tile] = found.get(tile, 0) + 1
    return found


class TheAtlas(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.out = cls.tmp.name
        cls.index, cls.sizes = map_atlas.build(SPRITES, CATALOG, cls.out)
        with open(TABLE, encoding="utf-8") as f:
            cls.table = json.load(f)
        cls.ground = indexed(os.path.join(cls.out, "ground.png"))
        cls.objects = indexed(os.path.join(cls.out, "objects.png"))

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def frame0(self, ident):
        entry = self.table[ident]
        frame = entry["subitems"][0]["frames"][0]
        return frame["sprite_index"], frame["dx"], frame["dy"]

    def test_the_viewers_table_is_the_animation_table_of_the_docs(self):
        """the image's build has no docs/, so the tool reads the animation table from asset_catalog/catalog_data.js: for all 1,344 tiles it gives the sprite and the offset of frame 0 that the docs' table gives"""
        found = map_atlas.read_catalog(CATALOG)
        self.assertEqual(sorted(found), list(range(1344)))
        for ident in range(1344):
            self.assertEqual(found[ident], self.frame0(ident), "tile %d (%s)" % (ident, self.table[ident]["name"]))

    def test_every_ground_tile_has_its_picture_pixel_for_pixel(self):
        g = self.index["ground"]
        self.assertEqual((g["first"], g["count"], g["columns"]), (431, 239, 16))
        w, h, rows = self.ground
        self.assertEqual((w, h), (g["columns"] * 32, 15 * 32), "the 239 tiles in rows of 16")
        self.assertEqual((g["w"], g["h"]), (w, h))
        for ident in range(431, 670):
            number, dx, dy = self.frame0(ident)
            self.assertEqual((dx, dy), (0, 0), "ground tile %d is drawn at its cell's corner" % ident)
            sw, sh, pixels = sprite_pixels(number)
            self.assertEqual((sw, sh), (32, 32), "ground tile %d is 32 x 32" % ident)
            k = ident - 431
            x0, y0 = (k % 16) * 32, (k // 16) * 32
            for y in range(32):
                self.assertEqual(rows[y0 + y][x0:x0 + 32], pixels[y], "tile %d, row %d" % (ident, y))
        self.assertEqual(len(g["avg"]), 239 * 6)

    def test_the_average_color_of_a_ground_tile_is_the_mean_of_its_pixels(self):
        for ident in (431, 439, 476, 485, 487, 571, 669):
            _w, _h, pixels = sprite_pixels(self.frame0(ident)[0])
            flat = [p for row in pixels for p in row]
            mean = [(sum(p[i] for p in flat) + len(flat) // 2) // len(flat) for i in range(3)]
            at = (ident - 431) * 6
            self.assertEqual(self.index["ground"]["avg"][at:at + 6], "%02x%02x%02x" % tuple(mean), "tile %d" % ident)

    def test_every_object_that_the_editor_offers_is_in_the_sheet_with_the_offset_of_its_frame(self):
        script = r"""
const P = require(%r), B = require(%r), pick = (l) => (l || []).map((r) => r[0]);
console.log(JSON.stringify({ objects: pick(P.OBJECTS), plants: pick(P.PLANTS), bigger: pick(P.BIGGER), hills: B.TEAMS.map((t) => t.hill), food: Object.keys(B.FOOD_NAMES).map(Number), stages: [].concat.apply([], Object.keys(B.FOOD).map((n) => B.FOOD[n].stages.map((s) => s[1]))).filter((t) => t !== 0x7FFE) }));
""" % (os.path.join(REPO, "web", "front", "mapmaker_palette.js"), os.path.join(REPO, "web", "front", "mapmaker_blocks.js"))
        import shutil
        import subprocess
        node = shutil.which("node")
        if not node:
            self.skipTest("node is not installed: the list of what the editor offers is read by it")
        offered = json.loads(subprocess.run([node, "-e", script], capture_output=True, text=True, check=True).stdout)
        wanted = set(offered["objects"]) | set(offered["plants"]) | set(offered["bigger"]) | set(offered["hills"]) | set(offered["food"]) | set(offered["stages"]) | {self.index["ant"], 129, 130, 131, 132}
        self.assertEqual(len(offered["bigger"]), 29)
        items = self.index["objects"]["items"]
        for ident in sorted(wanted):
            self.assertIn(str(ident), items, "tile %d (%s) that the editor offers has no picture in the sheet" % (ident, self.table[ident]["name"]))
        self.assertEqual(self.index["ant"], 811)
        self.assertEqual(self.table[811]["name"], "agst301", "the starting ants' picture is the standing worker ant")
        w, h, rows = self.objects
        self.assertEqual((self.index["objects"]["w"], self.index["objects"]["h"]), (w, h))
        boxes = []
        for key, (x, y, iw, ih, dx, dy, avg) in items.items():
            number, fdx, fdy = self.frame0(int(key))
            sw, sh, pixels = sprite_pixels(number)
            self.assertEqual((iw, ih, dx, dy), (sw, sh, fdx, fdy), "tile %s: the size and the offset are those of frame 0" % key)
            self.assertTrue(x >= 1 and y >= 1 and x + iw + 1 <= w and y + ih + 1 <= h, "tile %s is inside the sheet with a margin of a pixel" % key)
            for row in range(ih):
                for col in range(iw):
                    want, got = pixels[row][col], rows[y + row][x + col]
                    self.assertEqual(got[3] == 0, want[3] < 128, "tile %s pixel %d,%d: transparent where the sprite is" % (key, row, col))
                    if want[3] >= 128:
                        self.assertEqual(got[:3], want[:3], "tile %s pixel %d,%d" % (key, row, col))
            boxes.append((int(key), x, y, x + iw, y + ih))
            self.assertTrue(0 <= avg <= 0xFFFFFF)
        ants = self.index["ants"]
        self.assertEqual(sorted(ants), ["black", "blue", "green", "red"])
        ant_it = items["811"]
        self.assertEqual(ants["black"], ant_it[:2])
        aw, ah = ant_it[2], ant_it[3]
        _sw, _sh, ant_pixels = sprite_pixels(self.frame0(811)[0])
        for team in ("blue", "red", "green"):
            ax, ay = ants[team]
            self.assertTrue(ax >= 1 and ay >= 1 and ax + aw + 1 <= w and ay + ah + 1 <= h)
            remap = dict(zip(map_atlas.ANT_RAMPS["black"], map_atlas.ANT_RAMPS[team]))
            for row in range(ah):
                for col in range(aw):
                    want, got = ant_pixels[row][col], rows[ay + row][ax + col]
                    self.assertEqual(got[3] == 0, want[3] < 128)
                    if want[3] >= 128:
                        self.assertEqual(got[:3], remap[want[:3]])
            boxes.append(("ant_" + team, ax, ay, ax + aw, ay + ah))
        for i, a in enumerate(boxes):
            for b in boxes[i + 1:]:
                self.assertTrue(a[3] + 1 <= b[1] or b[3] + 1 <= a[1] or a[4] + 1 <= b[2] or b[4] + 1 <= a[2], "pictures %s and %s are a pixel apart at least" % (a[0], b[0]))

    def test_the_other_objects_of_the_library_are_in_the_index_by_their_sprite_number(self):
        far, items = self.index["far"], self.index["objects"]["items"]
        for key, (number, dx, dy, avg) in far.items():
            self.assertNotIn(key, items, "tile %s is in the sheet or fetched alone, not both" % key)
            self.assertEqual((number, dx, dy), self.frame0(int(key)), "tile %s" % key)
            self.assertTrue(os.path.isfile(os.path.join(SPRITES, "sprite_%04d.png" % number)), "its sprite file is in asset_catalog/sprites/")
            self.assertTrue(0 <= avg <= 0xFFFFFF)
        self.assertFalse(set(map(int, far)) & set(range(431, 670)), "no ground tile is fetched as an object")

    def test_every_object_that_a_library_map_anchors_has_a_picture_but_the_few_that_belong_to_the_menus(self):
        anchored = library_anchors()
        drawable = set(map(int, self.index["objects"]["items"])) | set(map(int, self.index["far"]))
        missing = {t for t in anchored if t not in drawable}
        self.assertEqual(missing, {t for t in anchored if t in NOT_DRAWN} & missing, "an id of a library map has no picture and is not one that the page leaves undrawn: %s" % sorted(missing - NOT_DRAWN))
        self.assertGreater(len(drawable & set(anchored)), 150, "the pictures cover the objects of the library")
        weight = sum(n for t, n in anchored.items() if t in drawable)
        self.assertGreater(weight / sum(anchored.values()), 0.99, "and 99% of the anchored cells")

    def test_a_picture_wider_than_the_sheet_is_refused(self):
        with self.assertRaises(SystemExit) as stopped:
            map_atlas.Sheet(64).add(7, 63, 1, bytes(4 * 63))
        self.assertIn("picture 7", str(stopped.exception))

    def test_the_files_are_small_and_the_same_every_time(self):
        self.assertLess(self.sizes["ground.png"], 40000, "ground.png (about 35 KB)")
        self.assertLess(self.sizes["objects.png"], 165000, "objects.png (about 155 KB)")
        self.assertLess(self.sizes["atlas.json"], 12000, "atlas.json (about 8 KB)")
        self.assertLess(sum(self.sizes.values()), 210000, "the whole atlas (about 199 KB: the page fetches all of it at start)")
        with tempfile.TemporaryDirectory() as again:
            map_atlas.build(SPRITES, CATALOG, again)
            for name in ("ground.png", "objects.png", "atlas.json"):
                with open(os.path.join(self.out, name), "rb") as a, open(os.path.join(again, name), "rb") as b:
                    self.assertEqual(a.read(), b.read(), name + " differs between two builds")

    def test_a_sheet_of_more_than_255_colors_is_refused_not_cut(self):
        sheet = map_atlas.Sheet(302)
        rgba = bytearray()
        for i in range(300):
            rgba += bytes((i % 256, i // 256, 7, 255))
        sheet.add(1, 300, 1, bytes(rgba))
        with self.assertRaises(SystemExit) as stopped:
            sheet.indexed("a test sheet")
        self.assertIn("more than 255 colors", str(stopped.exception))

    def test_the_tool_reads_the_two_public_inputs_and_nothing_else(self):
        text = read("tools", "map_atlas.py")
        self.assertNotRegex(text, r"Ants\.exe|Ants\.exe\.c|decompil|disassembl|Original-Ants", "the original program, its decompilation or the game's folder")
        self.assertIn("asset_catalog/sprites", text)
        self.assertIn("asset_catalog/catalog_data.js", text)
        imports = set(re.findall(r"^import (\w+)", text, re.M)) | set(re.findall(r"^from (\w+) import", text, re.M))
        self.assertEqual(imports, {"argparse", "json", "os", "re", "struct", "sys", "zlib"}, "the standard library only: the image's build has no more")


class ThePlacesThatCarryIt(unittest.TestCase):
    def test_the_dockerfile_makes_the_atlas_after_the_compile_layers_and_the_image_copies_it(self):
        text = read("Dockerfile")
        make = text.index("RUN python3 tools/map_atlas.py mapmaker_atlas")
        self.assertGreater(make, text.index("RUN cmake --build build_web"), "after the game is compiled, so that it does not rebuild it")
        self.assertLess(make, text.index("FROM nginx:alpine AS runner"))
        for line in ("COPY asset_catalog/sprites/ /src/asset_catalog/sprites/", "COPY asset_catalog/catalog_data.js /src/asset_catalog/catalog_data.js", "COPY tools/map_atlas.py /src/tools/map_atlas.py",
                     "COPY --from=builder /src/mapmaker_atlas/ /usr/share/nginx/html/mapmaker_atlas/"):
            self.assertIn(line, text)
        self.assertIn("test -s mapmaker_atlas/atlas.json", text, "an atlas that is empty stops the build")

    def test_the_image_build_still_has_nothing_of_docs(self):
        lines = read(".dockerignore").splitlines()
        self.assertIn("docs", lines)
        self.assertEqual([l for l in lines if l.startswith("!docs")], [], "the atlas reads the viewer's data, not docs/ (no image copies a document: tests/scripts/test_deploy_filter.py)")
        self.assertNotRegex(read("Dockerfile"), r"(?m)^COPY\s+(--\S+\s+)*docs")

    def test_the_local_web_build_makes_the_atlas_and_nginx_revalidates_it(self):
        self.assertIn('python3 "${SCRIPT_DIR}/tools/map_atlas.py" "${DIST_DIR}/mapmaker_atlas"', read("build_web.sh"))
        conf = read("docker", "nginx.conf")
        block = re.search(r"location \^~ /mapmaker_atlas/ \{(.*?)\n    \}", conf, re.S)
        self.assertTrue(block, "a location of its own, so that the PNG rule (a week) does not keep an old sheet beside a new index")
        self.assertIn('add_header Cache-Control "no-cache, must-revalidate" always;', block.group(1))
        self.assertIn("try_files $uri =404;", block.group(1))

    def test_the_ci_checks_that_the_image_has_the_atlas_and_the_scripts_that_load_it(self):
        text = read(".github", "workflows", "ci.yml")
        for name in ("front/mapmaker_ground.js", "front/mapmaker_sprites.js", "mapmaker_atlas/atlas.json", "mapmaker_atlas/ground.png", "mapmaker_atlas/objects.png", "asset_catalog/sprites/sprite_0726.png"):
            self.assertIn(name, text, name)

    def test_the_notices_name_the_atlas_next_to_the_sprites(self):
        text = read("THIRD_PARTY_NOTICES.md")
        self.assertRegex(text, r"map maker.{0,200}tools/map_atlas\.py", "one line names the map maker's atlas and the tool that makes it")
        self.assertIn("original's artwork too", text)


if __name__ == "__main__":
    unittest.main()
