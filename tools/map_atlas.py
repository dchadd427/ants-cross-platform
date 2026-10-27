#!/usr/bin/env python3
"""The map maker's sprite atlas: the game's own pictures of the tiles, in two small files and an index, made from the public decoded sprites and the public animation table.

usage: map_atlas.py [--sprites DIR] [--catalog FILE] OUT_DIR

Reads asset_catalog/sprites/sprite_NNNN.png (every sprite of ants.chd, decoded) and asset_catalog/catalog_data.js (the sprite viewer's data: every animation with its frames, the sprite of each and its
x/y offset; it is the table of docs/chd_table4_animations.json, which the image's build does not carry, and tests/scripts/test_map_atlas.py holds the two equal), nothing else: never the original
program (AGENTS.md rule 4). A tile id of the map IS the animation number; its picture is the animation's first sub-item with all its parts (frames) put together as the game draws
them (the last stored part first: a flower is its head, its stems, its leaves and its shadow), drawn with the x/y offset of the box of the parts from the top-left corner of the cell
(docs/MAP_EDITOR.md, "Sprites"). Writes, with the Python standard library only (zlib, struct: the image build has no more than that):

  ground.png    the 239 ground tiles (ids 431 to 669, all 32 x 32, offset 0,0), 16 to a row, a 256-color PNG
  objects.png   the pictures of the objects that the editor offers (rocks, power-ups, bombs, hills, food, the bigger objects, the starting ant) and the commonest small ones, packed, 256 colors
  atlas.json    the index: where each picture is in its sheet, its offset, its average color; for every other animation of one part the number of the sprite file (the page fetches
                those from /asset_catalog/sprites/ when a map shows them) with its offset and average color; and `names`, the name of every animation by its number (1,344 of them): the page
                resolves a tile of a map through the map's own dictionary the way the game does (Renderer::set_level: a dictionary entry that is not the name of the animation of its own number
                is looked up by name), so any animation can be the picture of a tile, not only the ones whose number a library map uses

It runs when the web image is built (Dockerfile, build_web.sh), so the repository keeps no generated picture. A palette of more than 255 colors in a sheet is an error, never a quiet loss.
"""
import argparse
import json
import os
import re
import struct
import sys
import zlib

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
GROUND_FIRST, GROUND_LAST, GROUND_COLUMNS, TILE = 431, 669, 16, 32
ANT = 811                                        # agst301: the standing worker ant (the starting ants' picture)
ANT_TYPES = {62: 897, 63: 1085, 64: 774, 65: 963, 66: 676}      # the 5 specialist standing ants (Combat, Thief, Bomber, Swimmer, Fire) for Block-3 default ant types
ANT_FACES = {811: (813, 814, 815), 897: (899, 900, 896), 1085: (1087, 1088, 1084), 774: (776, 777, 773), 963: (965, 966, 962), 676: (678, 679, 675)}      # (NE, E, SE) of the standing ant of each type, whose south picture is the key; the west pictures are these turned round
ANT_RAMPS = {                                    # the 14-step palette ramps (+0 black, +20 blue, +40 red, +60 green) for the standing worker ant
    "black": [(23, 23, 27), (39, 23, 51), (47, 27, 59), (47, 31, 51), (55, 35, 71), (67, 51, 71), (71, 51, 83), (87, 71, 91), (87, 71, 99), (107, 95, 111), (135, 131, 139), (167, 159, 171), (195, 195, 207), (251, 255, 247)],
    "blue": [(15, 19, 35), (31, 31, 59), (35, 35, 71), (23, 27, 55), (39, 43, 83), (35, 39, 79), (51, 59, 99), (47, 47, 103), (71, 79, 115), (75, 75, 135), (107, 107, 167), (147, 147, 199), (195, 195, 231), (239, 243, 255)],
    "red": [(39, 23, 27), (55, 31, 39), (67, 31, 39), (55, 27, 35), (79, 35, 43), (71, 31, 47), (91, 43, 55), (91, 35, 55), (103, 59, 67), (111, 35, 71), (147, 67, 87), (183, 111, 111), (219, 179, 167), (255, 247, 243)],
    "green": [(11, 35, 31), (31, 47, 43), (39, 55, 55), (19, 55, 47), (39, 63, 63), (27, 75, 63), (55, 75, 75), (39, 95, 79), (71, 99, 95), (55, 119, 99), (87, 151, 111), (127, 183, 127), (187, 215, 175), (251, 255, 251)],
}
FOOD_STAGES = list(range(253, 287)) + list(range(340, 344)) + list(range(356, 377)) + list(range(378, 389)) + list(range(390, 404)) + list(range(1331, 1334))        # the stages of all 32 food items that the Food tool puts (mapmaker_blocks.js)
PLANTS = [404, 405, 406, 407, 408, 410, 411, 412, 413, 414, 415, 416, 420, 421]     # the 14 top-layer plants (clovers and flowers, walk-under canopy and power-up droppers)
BIGGER = [1323, 1324, 1321, 1322, 1320, 1326, 301, 237, 1329, 1327, 242, 346, 329, 236, 238, 350, 326, 418, 417, 289, 325, 409, 324, 349, 315, 345, 292, 354, 352]      # mapmaker_palette.js BIGGER
ANT_FACE_IDS = [i for faces in ANT_FACES.values() for i in faces]
PARTED = [300, 328]      # the two other objects of a library map whose picture has several parts (medrock8, stick2a): fetched one by one they would show their first part only, so they are in the sheet
COMMON_SMALL = [134, 317, 318, 294, 336, 344, 309, 341, 340, 302, 303, 305]      # small and in the most library maps (the power-ups and rocks above are the others)
IN_THE_SHEET = sorted(set(list(range(302, 309)) + list(range(62, 67)) + list(range(129, 133)) + list(range(245, 249)) + [ANT] + list(ANT_TYPES.values()) + ANT_FACE_IDS + FOOD_STAGES + PLANTS + BIGGER + COMMON_SMALL + PARTED))
# every other animation of one part (a sprite): fetched one by one when a map shows it. A tile of a map is the animation of its dictionary name (docs/MAP_EDITOR.md, "Sprites"), so any of the
# 1,344 can be one, and the page cannot know which before it reads the map; an animation of several parts that the sheet does not hold (a menu screen, a piece of an ant) is left out
FAR_RANGES = [(0, 1343)]
MAX_COLORS = 255                                 # one palette entry is the transparent color


def read_png(path):
    """(width, height, rgba bytes) of an 8-bit RGBA PNG that is not interlaced (what every decoded sprite is)."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("%s is not a PNG" % path)
    at, idat, header = 8, [], None
    while at < len(data):
        n, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + n]
        at += 12 + n
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat.append(body)
    w, h, depth, color, _c, _f, interlace = header
    if (depth, color, interlace) != (8, 6, 0):
        raise SystemExit("%s: only 8-bit RGBA PNG files that are not interlaced are read" % path)
    raw, stride, out, prev = zlib.decompress(b"".join(idat)), 4 * w, bytearray(), bytearray(4 * w)
    for y in range(h):
        kind, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if kind == 1:
            for i in range(4, stride):
                line[i] = (line[i] + line[i - 4]) & 255
        elif kind == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif kind == 3:
            for i in range(stride):
                line[i] = (line[i] + (((line[i - 4] if i >= 4 else 0) + prev[i]) >> 1)) & 255
        elif kind == 4:
            for i in range(stride):
                a, b, c = (line[i - 4] if i >= 4 else 0), prev[i], (prev[i - 4] if i >= 4 else 0)
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 255
        elif kind != 0:
            raise SystemExit("%s: unknown PNG filter" % path)
        out += line
        prev = line
    return w, h, bytes(out)


def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)


def write_png(path, w, h, pixels, palette):
    """An 8-bit indexed PNG: `pixels` is w * h palette indexes; palette[0] is transparent when `palette` has a 4th value, i.e. [(r, g, b, a), ...]."""
    # (the pixels are palette indexes, so the filters of the PNG rarely help: every line is stored as it is and zlib finds the repeats)
    rows = [b"\x00" + bytes(pixels[y * w:(y + 1) * w]) for y in range(h)]
    plte = b"".join(bytes(c[:3]) for c in palette)
    trns = bytes(c[3] for c in palette)
    out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0)) + chunk(b"PLTE", plte)
    if any(c[3] != 255 for c in palette):
        out += chunk(b"tRNS", trns.rstrip(b"\xff"))
    out += chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(out)
    return len(out)


class Sheet:
    """Pictures packed in rows (shelves) into one indexed image; each is padded by a transparent pixel so that a smoothed, scaled draw takes nothing from its neighbor."""

    def __init__(self, width):
        self.width, self.items, self.x, self.y, self.shelf = width, [], 1, 1, 0

    def add(self, key, w, h, rgba):
        if w + 2 > self.width:
            raise SystemExit("picture %s is %d pixels wide: the sheet is %d" % (key, w, self.width))
        if self.x + w + 1 > self.width:
            self.x, self.y, self.shelf = 1, self.y + self.shelf + 1, 0
        self.items.append((key, self.x, self.y, w, h, rgba))
        self.x += w + 1
        self.shelf = max(self.shelf, h)
        return self.items[-1][1:3]

    def height(self):
        return self.y + self.shelf + 1

    def indexed(self, what):
        """(width, height, indexes, palette) of the sheet; an error when it holds more colors than a PNG palette can name next to the transparent one."""
        w, h, colors, pixels = self.width, self.height(), {}, bytearray(self.width * self.height())
        for _key, x0, y0, iw, ih, rgba in self.items:
            for y in range(ih):
                for x in range(iw):
                    r, g, b, a = rgba[4 * (y * iw + x):4 * (y * iw + x) + 4]
                    if a < 128:
                        continue
                    colors.setdefault((r, g, b), len(colors) + 1)
                    if len(colors) > MAX_COLORS:
                        raise SystemExit("%s holds more than %d colors: more than a 256-color PNG can name" % (what, MAX_COLORS))
                    pixels[(y0 + y) * w + x0 + x] = colors[(r, g, b)]
        palette = [(0, 0, 0, 0)] + [c + (255,) for c, _i in sorted(colors.items(), key=lambda t: t[1])]
        return w, h, bytes(pixels), palette


def average(rgba):
    """The mean color (0xRRGGBB) of the pixels that are drawn (alpha 128 or more); 0x808080 for a picture with none."""
    n = r = g = b = 0
    for i in range(0, len(rgba), 4):
        if rgba[i + 3] >= 128:
            n, r, g, b = n + 1, r + rgba[i], g + rgba[i + 1], b + rgba[i + 2]
    return 0x808080 if not n else ((r + n // 2) // n << 16) | ((g + n // 2) // n << 8) | ((b + n // 2) // n)


def read_parts(path):
    """{animation number: [(sprite number, dx, dy), ...]}: every part (frame) of the first sub-item of every animation of the viewer's data (a JavaScript file: `{ id: N, name: "...", subitems: [ { ... frames: [ { dx: X, dy: Y, sprite_id: S, ...
    The game draws all the parts of a sub-item, the last stored one first and the first on top (Renderer::draw_frame_parts); a flower is its head, its stems, its leaves and its shadow."""
    with open(path, encoding="utf-8") as f:
        text = f.read()
    start = text.find("animations: [")
    if start < 0:
        raise SystemExit("%s has no animations" % path)
    found = list(re.finditer(r'\n\s*\{ id: (\d+), name: "[^"]*", subitems: \[', text[start:]))
    table = {}
    for k, m in enumerate(found):
        block = text[start + m.end():start + (found[k + 1].start() if k + 1 < len(found) else len(text) - start)]
        frames = re.search(r"frames: \[(.*?)\n\s*\] \}", block, re.S)
        parts = [(int(p.group(3)), int(p.group(1)), int(p.group(2))) for p in re.finditer(r"\{ dx: (-?\d+), dy: (-?\d+), sprite_id: (\d+),", frames.group(1))] if frames else []
        if parts:
            table[int(m.group(1))] = parts
    if sorted(table) != list(range(1344)):
        raise SystemExit("%s: the animations 0 to 1343 were expected, found %d" % (path, len(table)))
    return table


def read_names(path):
    """[name of animation 0, name of animation 1, ...] from the viewer's data (the same `{ id: N, name: "..." ...` lines as read_parts); the table of the docs is held to it by tests/scripts/test_map_atlas.py."""
    with open(path, encoding="utf-8") as f:
        text = f.read()
    start = text.find("animations: [")
    if start < 0:
        raise SystemExit("%s has no animations" % path)
    found = {int(m.group(1)): m.group(2) for m in re.finditer(r'\n\s*\{ id: (\d+), name: "([^"\\]*)", subitems: \[', text[start:])}
    if sorted(found) != list(range(1344)):
        raise SystemExit("%s: the animations 0 to 1343 were expected, found %d" % (path, len(found)))
    return [found[i] for i in range(1344)]


def read_catalog(path):
    """{animation number: (sprite number, dx, dy)} of frame 0 of the first sub-item of every animation of the viewer's data (see read_parts)."""
    return {ident: parts[0] for ident, parts in read_parts(path).items()}


def first_frame(table, ident):
    """(sprite number, dx, dy) of frame 0 of the first sub-item of animation `ident`, or None when it has no picture."""
    return table.get(ident)


def composite(parts, sprite):
    """(width, height, rgba, dx, dy) of all the parts of a sub-item drawn as the game draws them (the last stored part first, the first one on top, a pixel that is drawn at alpha 128 or more
    replaces what is under it) on one canvas that is the box of the parts; (dx, dy) is where that picture's corner lies from the cell's anchor."""
    pics = [(dx, dy) + sprite(number) for number, dx, dy in parts]
    x0, y0 = min(p[0] for p in pics), min(p[1] for p in pics)
    x1, y1 = max(p[0] + p[2] for p in pics), max(p[1] + p[3] for p in pics)
    w, h, rgba = x1 - x0, y1 - y0, bytearray(4 * (x1 - x0) * (y1 - y0))
    for dx, dy, pw, ph, prgba in reversed(pics):
        for y in range(ph):
            for x in range(pw):
                a = 4 * (y * pw + x)
                if prgba[a + 3] >= 128:
                    at = 4 * ((dy - y0 + y) * w + dx - x0 + x)
                    rgba[at:at + 4] = prgba[a:a + 4]
    return w, h, bytes(rgba), x0, y0


def build(sprites, catalog_path, out):
    parts_table = read_parts(catalog_path)
    table = {ident: parts[0] for ident, parts in parts_table.items()}

    def sprite(number):
        return read_png(os.path.join(sprites, "sprite_%04d.png" % number))

    ground = Sheet(GROUND_COLUMNS * TILE)
    ground.x = ground.y = 0
    averages, cells = [], GROUND_LAST - GROUND_FIRST + 1
    for ident in range(GROUND_FIRST, GROUND_LAST + 1):
        found = first_frame(table, ident)
        if not found:
            raise SystemExit("ground tile %d has no picture" % ident)
        w, h, rgba = sprite(found[0])
        if (w, h, found[1], found[2]) != (TILE, TILE, 0, 0):
            raise SystemExit("ground tile %d is not 32 x 32 at 0,0" % ident)
        index = ident - GROUND_FIRST
        ground.items.append((ident, (index % GROUND_COLUMNS) * TILE, (index // GROUND_COLUMNS) * TILE, w, h, rgba))
        averages.append("%06x" % average(rgba))
    ground.y, ground.shelf = ((cells + GROUND_COLUMNS - 1) // GROUND_COLUMNS) * TILE - 1, 0     # (height() = y + shelf + 1: exactly the rows of tiles)

    objects, items = Sheet(1024), {}
    order = []
    ant_rgbas = {}
    for ident in IN_THE_SHEET:
        found = first_frame(table, ident)
        if not found:
            raise SystemExit("object %d has no picture" % ident)
        parts = parts_table[ident]
        if len(parts) > 1:                       # a picture of several parts (a flower: head, stems, leaves, shadow): all of them, as the game draws them
            w, h, rgba, dx, dy = composite(parts, sprite)
            found = (None, dx, dy)
        else:
            w, h, rgba = sprite(found[0])
        if ident == ANT or ident in ANT_TYPES.values() or ident in ANT_FACE_IDS:
            ant_rgbas[ident] = (w, h, rgba)
        order.append((h, w, ident, found, rgba))
    for h, w, ident, found, rgba in sorted(order, key=lambda t: (-t[0], -t[1], t[2])):
        x, y = objects.add(ident, w, h, rgba)
        items[str(ident)] = [x, y, w, h, found[1], found[2], average(rgba)]

    def recolor_ant(ident):
        out_teams = {"black": items[str(ident)][:2]}
        aw, ah, argba = ant_rgbas[ident]
        for team in ("blue", "red", "green"):
            remap = dict(zip(ANT_RAMPS["black"], ANT_RAMPS[team]))
            recolored = bytearray(argba)
            for i in range(0, len(recolored), 4):
                if recolored[i + 3] >= 128:
                    r, g, b = remap.get((recolored[i], recolored[i + 1], recolored[i + 2]), (recolored[i], recolored[i + 1], recolored[i + 2]))
                    recolored[i], recolored[i + 1], recolored[i + 2] = r, g, b
            ax, ay = objects.add("ant_%d_%s" % (ident, team), aw, ah, bytes(recolored))
            out_teams[team] = [ax, ay]
        return out_teams

    ants = recolor_ant(ANT)
    ant_types = {}
    for ptile, aident in sorted(ANT_TYPES.items()):
        entry = recolor_ant(aident)
        entry["id"] = aident
        ant_types[str(ptile)] = entry
    ant_faces = {}
    for south, faces in sorted(ANT_FACES.items()):
        for ident in faces:
            ant_faces[str(ident)] = recolor_ant(ident)

    far = {}
    for lo, hi in FAR_RANGES:
        for ident in range(lo, hi + 1):
            if str(ident) in items or ident in IN_THE_SHEET or GROUND_FIRST <= ident <= GROUND_LAST or len(parts_table.get(ident, ())) != 1:
                continue
            found = first_frame(table, ident)
            far[str(ident)] = [found[0], found[1], found[2], average(sprite(found[0])[2])]

    os.makedirs(out, exist_ok=True)
    w, h, pixels, palette = ground.indexed("the ground sheet")
    sizes = {"ground.png": write_png(os.path.join(out, "ground.png"), w, h, pixels, palette)}
    gw, gh = w, h
    w, h, pixels, palette = objects.indexed("the objects sheet")
    sizes["objects.png"] = write_png(os.path.join(out, "objects.png"), w, h, pixels, palette)
    index = {
        "v": 1,
        "ground": {"png": "ground.png", "first": GROUND_FIRST, "count": cells, "columns": GROUND_COLUMNS, "w": gw, "h": gh, "avg": "".join(averages)},
        "objects": {"png": "objects.png", "w": w, "h": h, "items": items},
        "far": far,
        "names": read_names(catalog_path),
        "ant": ANT,
        "ants": ants,
        "antTypes": ant_types,
        "faces": {str(k): list(v) for k, v in ANT_FACES.items()},
        "antFaces": ant_faces,
    }
    text = json.dumps(index, sort_keys=True, separators=(",", ":"))
    with open(os.path.join(out, "atlas.json"), "w", encoding="utf-8") as f:
        f.write(text)
    sizes["atlas.json"] = len(text)
    return index, sizes


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("out")
    parser.add_argument("--sprites", default=os.path.join(REPO, "asset_catalog", "sprites"))
    parser.add_argument("--catalog", default=os.path.join(REPO, "asset_catalog", "catalog_data.js"))
    args = parser.parse_args()
    # the owner's rule (tools/library_guard.py has the whole rule; the image build carries this file alone, so the two folder names are checked here, in lower case): nothing is written into the map library
    if any(part.lower() in ("original-ants", "community-maps") for part in re.split(r"[\\/]", os.path.abspath(args.out) + "/" + os.path.realpath(args.out))):
        print("map_atlas: %s is in the map library (the originals' folder or the players' maps): no tool writes there; nothing written" % args.out, file=sys.stderr)
        sys.exit(2)
    index, sizes = build(args.sprites, args.catalog, args.out)
    print("map atlas: " + ", ".join("%s %d bytes" % (k, v) for k, v in sorted(sizes.items())) + "; %d ground tiles, %d pictures in the sheet, %d fetched one by one" % (index["ground"]["count"], len(index["objects"]["items"]), len(index["far"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
