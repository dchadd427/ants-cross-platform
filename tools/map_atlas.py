#!/usr/bin/env python3
"""The map maker's sprite atlas: the game's own pictures of the tiles, in two small files and an index, made from the public decoded sprites and the public animation table.

usage: map_atlas.py [--sprites DIR] [--table FILE] OUT_DIR

Reads asset_catalog/sprites/sprite_NNNN.png (every sprite of ants.chd, decoded) and docs/chd_table4_animations.json (every animation: its frames, the sprite of each and its x/y offset), nothing
else: never the original program (AGENTS.md rule 4). A tile id of the map IS the animation number; its picture is frame 0 of the animation's first sub-item, drawn with that frame's x/y offset
from the top-left corner of the cell (docs/MAP_EDITOR.md, "Sprites"). Writes, with the Python standard library only (zlib, struct: the image build has no more than that):

  ground.png    the 239 ground tiles (ids 431 to 669, all 32 x 32, offset 0,0), 16 to a row, a 256-color PNG
  objects.png   the pictures of the objects that the editor offers (rocks, power-ups, bombs, hills, food, the bigger objects, the starting ant) and the commonest small ones, packed, 256 colors
  atlas.json    the index: where each picture is in its sheet, its offset, its average color; and for every other object of the library the number of the sprite file (the page fetches
                those few from /asset_catalog/sprites/ when a map shows them) with its offset and average color

It runs when the web image is built (Dockerfile, build_web.sh), so the repository keeps no generated picture. A palette of more than 255 colors in a sheet is an error, never a quiet loss.
"""
import argparse
import json
import os
import struct
import sys
import zlib

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
GROUND_FIRST, GROUND_LAST, GROUND_COLUMNS, TILE = 431, 669, 16, 32
ANT = 811                                        # agst301: the standing worker ant (the starting ants' picture)
FOOD_STAGES = list(range(264, 272)) + [280] + list(range(373, 377))        # the stages of the piles that the Food tool puts (mapmaker_blocks.js)
BIGGER = [1323, 1324, 1321, 1322, 1320, 1326, 301, 237, 1329, 1327, 242, 346, 329, 236, 238, 350, 326, 418, 417, 289, 325, 409, 324, 349, 315, 345, 292, 354, 352]      # mapmaker_palette.js BIGGER
COMMON_SMALL = [134, 317, 318, 294, 336, 344, 309, 341, 340, 302, 303, 305]      # small and in the most library maps (the power-ups and rocks above are the others)
IN_THE_SHEET = sorted(set(list(range(302, 309)) + list(range(62, 67)) + list(range(129, 133)) + list(range(245, 249)) + [ANT] + FOOD_STAGES + BIGGER + COMMON_SMALL))
# every other object that a library map anchors on layer 2 and that is a thing on the ground (not a screen element of the menus): fetched one by one
FAR_RANGES = [(34, 38), (133, 134), (236, 242), (245, 430), (1320, 1334)]
FAR_NOT = {243, 244}                             # the mouse pictures of the menus
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


def first_frame(table, ident):
    """(sprite number, dx, dy) of frame 0 of the first sub-item of animation `ident`, or None when it has no picture."""
    entry = table[ident]
    if entry["entry_index"] != ident:
        raise SystemExit("the animation table is not in order at %d" % ident)
    if not entry["subitems"] or not entry["subitems"][0]["frames"]:
        return None
    frame = entry["subitems"][0]["frames"][0]
    return frame["sprite_index"], frame["dx"], frame["dy"]


def build(sprites, table_path, out):
    with open(table_path, encoding="utf-8") as f:
        table = json.load(f)

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
    for ident in IN_THE_SHEET:
        found = first_frame(table, ident)
        if not found:
            raise SystemExit("object %d has no picture" % ident)
        w, h, rgba = sprite(found[0])
        order.append((h, w, ident, found, rgba))
    for h, w, ident, found, rgba in sorted(order, key=lambda t: (-t[0], -t[1], t[2])):
        x, y = objects.add(ident, w, h, rgba)
        items[str(ident)] = [x, y, w, h, found[1], found[2], average(rgba)]

    far = {}
    for lo, hi in FAR_RANGES:
        for ident in range(lo, hi + 1):
            if ident in FAR_NOT or str(ident) in items or ident in IN_THE_SHEET:
                continue
            found = first_frame(table, ident)
            if found:
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
        "ant": ANT,
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
    parser.add_argument("--table", default=os.path.join(REPO, "docs", "chd_table4_animations.json"))
    args = parser.parse_args()
    index, sizes = build(args.sprites, args.table, args.out)
    print("map atlas: " + ", ".join("%s %d bytes" % (k, v) for k, v in sorted(sizes.items())) + "; %d ground tiles, %d pictures in the sheet, %d fetched one by one" % (index["ground"]["count"], len(index["objects"]["items"]), len(index["far"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
