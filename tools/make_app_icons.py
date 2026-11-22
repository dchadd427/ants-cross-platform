#!/usr/bin/env python3
"""Makes the icons of the installable web app and of the browser tab from the original game's own icon: the 32 x 32 picture of 16 colors in Ants.exe, on a dark tile.

A developer's tool (it needs only Pillow: pip install pillow); not part of any build or test. No picture is read: every file is drawn from the grid below, which holds the
original's icon cell for cell (its only size; wrestool lists one icon group, 32 x 32 at 4 bits). Run it from anywhere:

    python3 tools/make_app_icons.py [--out DIR] [--favicon-dir DIR] [--check]

Defaults write web/app/ and web/ of the repository. Every "pixel" of the art is a square cell of whole device pixels, the same in the whole file, so the icon stays as sharp as the
original at every size (a whole-number zoom; a rim of whole device pixels makes up the rest):

  icon-512.png            36 x 36 cells of 14 px, a rim of 4 px   the icon on the dark tile, rounded corners in cell steps (transparent, with the rim)
  icon-192.png            38 x 38 cells of  5 px, a rim of 1 px   the same picture
  icon-maskable-512.png   56 x 56 cells of  9 px, a rim of 4 px   square, dark to every edge: the icon lies inside the circle of 80% of the width (the maskable safe zone)
  apple-touch-icon.png    36 x 36 cells of  5 px                   180 x 180, square, no transparency (iOS rounds it itself)
  favicon.png             32 x 32 cells of  1 px                   the icon alone on transparency, as the original has it, written to --favicon-dir
  favicon.ico             two frames: 32 px (as favicon.png) and 64 px (cells of 2 px), written to --favicon-dir

The colors are the original's 16-color palette (those it uses); the tile is the near black of the site's panels (101412).

--check reads every written PNG back and fails unless every cell is one constant color, all cells have the stated size, the rim is the tile's color (or transparent where the
corners are), and nothing but whole cells is transparent. The files are saved with a palette (at most 16 colors), optimised; the maskable one with one byte per pixel, because
the manifest test reads it as an 8 bit palette PNG to measure the safe circle. The manifest and the pages name each app file with ?v= and the first 8 hex digits of its sha256
(printed at the end), and the favicon links the same way, because the server lets a browser keep a .png for a week (the favicon for a month): after new files, put the new digits in
the manifest and in the head of every page.
"""
import argparse
import hashlib
import tempfile
from pathlib import Path

from PIL import Image

TILE = (0x10, 0x14, 0x12)
COLORS = {
    "K": (0x00, 0x00, 0x00),
    "n": (0x00, 0x00, 0x80),
    "B": (0x00, 0x00, 0xFF),
    "g": (0x00, 0x80, 0x00),
    "t": (0x00, 0x80, 0x80),
    "G": (0x00, 0xFF, 0x00),
    "C": (0x00, 0xFF, 0xFF),
    "M": (0x80, 0x80, 0x80),
    "R": (0xFF, 0x00, 0x00),
    "Y": (0xFF, 0xFF, 0x00),
    "W": (0xFF, 0xFF, 0xFF),
}

# The original's icon, 32 x 32 cells ("." is transparent).
ICON = [
    "..............R.................",
    ".............RRRnKKK.....RRR....",
    ".R..........RRRnKKtMM..RRRRR....",
    "..RRRR......RRRKKKnBGMRRRRRR....",
    "..RRRRRRR..RRRnKKRKKMWnRRRR.....",
    "...RRRRRRRRRRRnKKRRKKMnRRRR.....",
    "...RRRRRRRRRRKnKKKRRKKKRRR......",
    "....RRKnMRRKBgtttggMRRRRRR......",
    "....RKMBBKKgttGtGGggMRRRR.......",
    "....RKKnBtMttGtGCGCgBRRRR.......",
    "....RKnKMGtMGtGCGCWGBKRRRRR.....",
    "....RRnKKMGtMGCGCWCGBKRRRRRRR...",
    "..RRRRKnKKMWnCGCWWGBgKKRRRRRRRRR",
    "RRRRRKKKnKnMKgCWWGggBgGMRRRRRRR.",
    ".RRRRKnKBgKKnCWWWgggBgGYRRRRRR..",
    "..RRRKnKBgggGgGgBggBMKWYMRRRR...",
    "...RRKBKBBgggGgggBBBKKKYKRRR....",
    "...RRKBKnBBnnBgBBBBnKnMGKRR.....",
    "....RKBKnnntGYYGBnnnntGgKR......",
    "....RKBnKKtYMKWYGnKKnttnKRR.....",
    "...RRRBnKKGYKKKYYKKKKKnBBKR.....",
    "..RRRRnBKKGYKnMYYKKKKnBgGgnR....",
    "..RRRRRKnKtYYYYYGKKKBgGWWGgn....",
    ".RRRRRRKKKKBGYYGnnKKKKnBBggnR...",
    ".RRRRRRRKKKKKKnBggnKntgggBnnR...",
    "RRRRRRRRRKKKKnBgGGBnKKKKKKnRRR..",
    "RRRRRR...RRKKnBGCCGBK..RRRRRRR..",
    "..........RRKgnGWWGBK.....RRRR..",
    "..........RRKngBBgBBK...........",
    "...........RRKnngggBn...........",
    "...........RRRKKKKnn............",
    "............RR..................",
]


def corner_cut(cells, steps):
    cut = set()
    for j in range(steps):
        for i in range(steps):
            if (steps - i - 0.5) ** 2 + (steps - j - 0.5) ** 2 > steps ** 2:
                for x, y in ((i, j), (cells - 1 - i, j), (i, cells - 1 - j), (cells - 1 - i, cells - 1 - j)):
                    cut.add((x, y))
    return cut


def picture(cells, margin, tile=True, corner=0):
    grid = [[TILE if tile else None] * cells for _ in range(cells)]
    for y, row in enumerate(ICON):
        for x, ch in enumerate(row):
            if ch != ".":
                grid[margin + y][margin + x] = COLORS[ch]
    for x, y in corner_cut(cells, corner):
        assert not (margin <= x < margin + 32 and margin <= y < margin + 32 and ICON[y - margin][x - margin] != "."), "a rounded corner cuts the icon"
        grid[y][x] = None
    return grid


def render(grid, cell, rim=0, rim_color=None):
    n = len(grid)
    small = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    px = small.load()
    for y in range(n):
        for x in range(n):
            if grid[y][x] is not None:
                px[x, y] = grid[y][x] + (255,)
    image = small.resize((n * cell, n * cell), Image.NEAREST)
    if rim:
        whole = Image.new("RGBA", (n * cell + 2 * rim, n * cell + 2 * rim), rim_color + (255,) if rim_color else (0, 0, 0, 0))
        whole.paste(image, (rim, rim))
        image = whole
    return image


APP_FILES = ("icon-512.png", "icon-192.png", "icon-maskable-512.png", "apple-touch-icon.png")


def plan():
    # file name: (cells across, cell size in px, margin in cells, tile, corner steps, transparency allowed, rim in px, rim color)
    return {
        "icon-512.png": (36, 14, 2, True, 6, True, 4, None),
        "icon-192.png": (38, 5, 3, True, 6, True, 1, None),
        "icon-maskable-512.png": (56, 9, 12, True, 0, False, 4, TILE),
        "apple-touch-icon.png": (36, 5, 2, True, 0, False, 0, None),
        "favicon-64.png": (32, 2, 0, False, 0, True, 0, None),
        "favicon-32.png": (32, 1, 0, False, 0, True, 0, None),
    }


def save(image, path, opaque, bits=None):
    """bits=8 writes one byte per pixel (a few hundred bytes more): the repository's test reads the maskable icon as an 8 bit palette PNG."""
    more = {"bits": bits} if bits else {}
    if opaque:
        image.convert("RGB").quantize(colors=16, method=Image.MEDIANCUT, dither=Image.NONE).save(path, optimize=True, **more)
    else:
        image.quantize(colors=16, method=Image.FASTOCTREE, dither=Image.NONE).save(path, optimize=True, **more)


def check(path, cells, cell, opaque, rim=0, rim_color=None):
    im = Image.open(path).convert("RGBA")
    size = cells * cell + 2 * rim
    assert im.size == (size, size), "%s: size %r, wanted %d" % (path.name, im.size, size)
    px = im.load()
    seen = set()
    want_rim = rim_color + (255,) if rim_color else None
    for y in range(size):
        for x in range(size):
            if min(x, y) < rim or max(x, y) >= size - rim:
                assert (px[x, y] == want_rim) if want_rim else px[x, y][3] == 0, "%s: the rim is not as planned at pixel (%d, %d)" % (path.name, x, y)
    for cy in range(cells):
        for cx in range(cells):
            first = px[rim + cx * cell, rim + cy * cell]
            for y in range(rim + cy * cell, rim + cy * cell + cell):
                for x in range(rim + cx * cell, rim + cx * cell + cell):
                    assert px[x, y] == first, "%s: cell (%d, %d) is not one color at pixel (%d, %d)" % (path.name, cx, cy, x, y)
            assert first[3] in (0, 255), "%s: a half transparent cell (%d, %d)" % (path.name, cx, cy)
            assert not (opaque and first[3] == 0), "%s: a transparent cell (%d, %d) in a file that must have none" % (path.name, cx, cy)
            seen.add(first if first[3] else None)
    return len(seen)


def safe_zone(grid, cell, rim):
    n = len(grid)
    size = n * cell + 2 * rim
    far = 0.0
    for y in range(n):
        for x in range(n):
            if grid[y][x] is not None and grid[y][x] != TILE:
                for a, b in ((x, y), (x + 1, y), (x, y + 1), (x + 1, y + 1)):
                    far = max(far, ((rim + a * cell - size / 2) ** 2 + (rim + b * cell - size / 2) ** 2) ** 0.5 / size)
    return far


def main():
    here = Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", default=str(here / "web" / "app"), help="where the four app files go (default web/app)")
    ap.add_argument("--favicon-dir", default=str(here / "web"), help="where favicon.png and favicon.ico go (default web)")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    out, fav = Path(a.out), Path(a.favicon_dir)
    out.mkdir(parents=True, exist_ok=True)
    fav.mkdir(parents=True, exist_ok=True)
    total = 0
    with tempfile.TemporaryDirectory() as tmp:
        for name, (cells, cell, margin, tile, corner, clear, rim, rim_color) in plan().items():
            grid = picture(cells, margin, tile, corner)
            note = ""
            where = out if name in APP_FILES else Path(tmp)
            if name == "icon-maskable-512.png":
                far = safe_zone(grid, cell, rim)
                assert far <= 0.40, "the icon leaves the maskable safe zone: %.4f of the width" % far
                note = "  icon within %.3f of the width from the middle (safe zone 0.400)" % far
            save(render(grid, cell, rim, rim_color), where / name, not clear, 8 if name == "icon-maskable-512.png" else None)
            if a.check:
                note += "  checked: %d colors, every cell constant" % check(where / name, cells, cell, not clear, rim, rim_color)
            if name in APP_FILES:
                total += (where / name).stat().st_size
            if name == "favicon-32.png":
                (fav / "favicon.png").write_bytes((where / name).read_bytes())
            print("%-24s %3d px  %2d x %2d cells of %2d px  %6d bytes  ?v=%s%s" % (name, cells * cell + 2 * rim, cells, cells, cell, (where / name).stat().st_size,
                                                                                   hashlib.sha256((where / name).read_bytes()).hexdigest()[:8], note))
        frames = [Image.open(Path(tmp) / n).convert("RGBA") for n in ("favicon-32.png", "favicon-64.png")]
        frames[1].save(fav / "favicon.ico", format="ICO", append_images=frames[:1], sizes=[(64, 64), (32, 32)])
    for name in ("favicon.png", "favicon.ico"):
        print("%-24s %6d bytes  ?v=%s" % (name, (fav / name).stat().st_size, hashlib.sha256((fav / name).read_bytes()).hexdigest()[:8]))
    print("the four app files together: %d bytes" % total)


if __name__ == "__main__":
    main()
