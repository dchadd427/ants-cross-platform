#!/usr/bin/env python3
"""Makes the icons of the installable web app and of the browser tab as pixel art: the worker ant's head, front on, on the orange of the game's menus.

A developer's tool (it needs only Pillow: pip install pillow); not part of any build or test. No picture is read: every file is drawn from the grids below. Run it from anywhere:

    python3 tools/make_app_icons.py [--out DIR] [--favicon-dir DIR] [--check]

Defaults write web/app/ and web/ of the repository. Every "pixel" of the art is a square cell of whole device pixels, the same in the whole file:

  icon-512.png            32 x 32 cells of 16 px   the head (22 x 22 cells with its dark edge) in the middle, rounded corners in cell steps (transparent)
  icon-192.png            32 x 32 cells of  6 px   the same picture
  icon-maskable-512.png   34 x 34 cells of 15 px   square, orange to every edge (and a rim of 1 px of the same orange: 34 x 15 = 510): the head is a little smaller here and lies
                                                   inside the circle of 80% of the width (the maskable safe zone), with room to spare for a launcher's round cut
  apple-touch-icon.png    30 x 30 cells of  6 px   180 x 180, square, no transparency (iOS rounds it itself)
  favicon.png             16 x 16 cells of  2 px   the small head (HEAD16: drawn for this size, not shrunk) alone on transparency, written to --favicon-dir
  favicon.ico             three frames: 16 px (HEAD16 at 1 px per cell), 32 px (as favicon.png) and 48 px (the large head, 24 x 24 cells of 2 px), written to --favicon-dir

The colors of the head are the original worker sprite's own: greys c3c3cf a79fab 87838b 6b5f6f, violets 57475b 372347 2f1b3b, near black 17171b,
the eyes' white fbfff7. The ground is the orange of the menus (db4b13) and the flat shadow, one cell down and right, is the red of the site's buttons (9d0d17).

--check reads every written PNG back and fails unless every cell is one constant color, all cells have the stated size, and nothing but whole cells is transparent.
The files are saved with a palette (at most 16 colors), optimised; the maskable one with one byte per pixel, because the manifest test reads it as an 8 bit palette PNG to
measure the safe circle. The manifest and the pages name each app file with ?v= and the first 8 hex digits of its sha256 (printed at the end), and the favicon links the same way, because the
server lets a browser keep a .png for a week (the favicon for a month): after new files, put the new digits in the manifest and in the head of every page.
"""
import argparse
import hashlib
import tempfile
from pathlib import Path

from PIL import Image

CLAY = (0xDB, 0x4B, 0x13)
SHADOW = (0x9D, 0x0D, 0x17)
COLORS = {
    "O": (0x17, 0x17, 0x1B),         # the dark edge
    "K": (0x17, 0x17, 0x1B),         # pupil
    "D": (0x2F, 0x1B, 0x3B),         # antennae, jaws
    "d": (0x57, 0x47, 0x5B),         # their light side
    "B": (0x37, 0x23, 0x47),         # brow
    "H": (0xC3, 0xC3, 0xCF),         # head: lightest
    "L": (0xA7, 0x9F, 0xAB),         # head: light
    "M": (0x87, 0x83, 0x8B),         # head: middle
    "S": (0x6B, 0x5F, 0x6F),         # head: shade
    "s": (0x57, 0x47, 0x5B),         # head: deep shade
    "W": (0xFB, 0xFF, 0xF7),         # the eyes' white
}

# The head on 20 x 20 cells. The dark edge (one cell) is added around everything but the thin antennae and jaws (D, d): 22 x 22 cells in all.
HEAD20 = [
    "...DDD........DDD...",
    ".DD..DD......DD..DD.",
    ".DD..DD......DD..DD.",
    ".D...DD......DD...D.",
    ".....DD......DD.....",
    "....HHHHHHLLLLLL....",
    "...HHHHHHLLLLLLLL...",
    "..LHHHHLLLLLLLLLLM..",
    "..LLLLLLLLLLLLLLMM..",
    "..BBBBLLLLLLLLBBBB..",
    "..MWWWBBBLLBBBWWWM..",
    "..MWWWWWWLLWWWWWWM..",
    "..MWWWWKKMMKKWWWWM..",
    "..MWWWWKKMMKKWWWWS..",
    "..SMWWWWMMMMWWWWMS..",
    "..SMMMMMMMMMMMMMSS..",
    "...SMMMMMMMMMMMSS...",
    "....SSMMMMMMMMSS....",
    "....DDD.SSSS.DDD....",
    "....DD........DD....",
]

# The small head on 16 x 16 cells, its dark edge drawn by hand (O): for 16 and 32 px, where the large one would turn to mush.
HEAD16 = [
    "..DD........DD..",
    ".D..D......D..D.",
    ".D..D......D..D.",
    "...OOOOOOOOOO...",
    "..OHHHHLLLLLLO..",
    ".OHHHLLLLLLLLMO.",
    ".OBBBLLLLLLBBBO.",
    ".OWWWBBLLBBWWWO.",
    ".OWWWWWLLWWWWWO.",
    ".OWWWKKMMKKWWWO.",
    ".OWWWKKMMKKWWWO.",
    ".OMWWWMMMMWWWSO.",
    ".OSMMMMMMMMMSSO.",
    "..OSSMMMMMMSSO..",
    "...OOOOOOOOOO...",
    "....DD....DD....",
]


def with_edge(grid):
    """The grid one cell larger on every side, with the dark edge in every empty cell next to a painted one (not next to D or d)."""
    n = len(grid[0])
    rows = ["." * (n + 2)] + ["." + r + "." for r in grid] + ["." * (n + 2)]
    out = [list(r) for r in rows]
    for y in range(len(rows)):
        for x in range(n + 2):
            if rows[y][x] != ".":
                continue
            for a, b in ((0, 1), (0, -1), (1, 0), (-1, 0)):
                yy, xx = y + a, x + b
                if 0 <= yy < len(rows) and 0 <= xx < n + 2 and rows[yy][xx] not in ".Dd":
                    out[y][x] = "O"
                    break
    return ["".join(r) for r in out]


HEAD22 = with_edge(HEAD20)


def corner_cut(cells, steps):
    """The cells cut away in each corner of a square of `cells` cells: a quarter circle of `steps` cells, in whole cells."""
    cut = set()
    for j in range(steps):
        for i in range(steps):
            if (steps - i - 0.5) ** 2 + (steps - j - 0.5) ** 2 > steps ** 2:
                for x, y in ((i, j), (cells - 1 - i, j), (i, cells - 1 - j), (cells - 1 - i, cells - 1 - j)):
                    cut.add((x, y))
    return cut


def picture(cells, head, margin, ground="plain", shadow=True, corner=0):
    """One picture as a square of `cells` x `cells` colors (None = transparent): the head with its top left at (margin, margin)."""
    if ground == "none":
        grid = [[None] * cells for _ in range(cells)]
    else:
        grid = [[CLAY] * cells for _ in range(cells)]
    if shadow:
        for y, row in enumerate(head):
            for x, ch in enumerate(row):
                if ch != "." and margin + y + 1 < cells and margin + x + 1 < cells:
                    grid[margin + y + 1][margin + x + 1] = SHADOW
    for y, row in enumerate(head):
        for x, ch in enumerate(row):
            if ch != ".":
                grid[margin + y][margin + x] = COLORS[ch]
    for x, y in corner_cut(cells, corner):
        grid[y][x] = None
    return grid


def render(grid, cell, rim=0):
    """The picture with every cell `cell` device pixels wide and tall (RGBA), inside a rim of `rim` pixels of the ground's orange."""
    n = len(grid)
    small = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    px = small.load()
    for y in range(n):
        for x in range(n):
            if grid[y][x] is not None:
                px[x, y] = grid[y][x] + (255,)
    image = small.resize((n * cell, n * cell), Image.NEAREST)
    if rim:
        whole = Image.new("RGBA", (n * cell + 2 * rim, n * cell + 2 * rim), CLAY + (255,))
        whole.paste(image, (rim, rim))
        image = whole
    return image


# file name: (cells across, cell size in px, head, margin in cells, ground, shadow, corner steps, transparency allowed, rim in px)
APP_FILES = ("icon-512.png", "icon-192.png", "icon-maskable-512.png", "apple-touch-icon.png")


def plan(ground="plain"):
    return {
        "icon-512.png": (32, 16, HEAD22, 5, ground, True, 6, True, 0),
        "icon-192.png": (32, 6, HEAD22, 5, ground, True, 6, True, 0),
        "icon-maskable-512.png": (34, 15, HEAD22, 6, ground, True, 0, False, 1),
        "apple-touch-icon.png": (30, 6, HEAD22, 4, ground, True, 0, False, 0),
        "favicon-48.png": (24, 2, HEAD22, 1, "none", False, 0, True, 0),
        "favicon-32.png": (16, 2, HEAD16, 0, "none", False, 0, True, 0),
        "favicon-16.png": (16, 1, HEAD16, 0, "none", False, 0, True, 0),
    }


def save(image, path, opaque, bits=None):
    """A palette of at most 16 colors (the art has no more), optimised; an opaque file carries no transparency at all.
    bits=8 writes one byte per pixel (a few hundred bytes more): the repository's test reads the maskable icon as an 8 bit palette PNG."""
    more = {"bits": bits} if bits else {}
    if opaque:
        image.convert("RGB").quantize(colors=16, method=Image.MEDIANCUT, dither=Image.NONE).save(path, optimize=True, **more)
    else:
        image.quantize(colors=16, method=Image.FASTOCTREE, dither=Image.NONE).save(path, optimize=True, **more)


def check(path, cells, cell, opaque, rim=0):
    """Reads the file back: the stated size, every cell one constant color, transparency only where allowed, the rim all orange. Returns the number of colors."""
    im = Image.open(path).convert("RGBA")
    size = cells * cell + 2 * rim
    assert im.size == (size, size), "%s: size %r, wanted %d" % (path.name, im.size, size)
    px = im.load()
    seen = set()
    for y in range(size):
        for x in range(size):
            if min(x, y) < rim or max(x, y) >= size - rim:
                assert px[x, y] == CLAY + (255,), "%s: the rim is not the ground's orange at pixel (%d, %d)" % (path.name, x, y)
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
    """The largest distance of a painted head cell (not the ground, not the shadow) from the middle, as a share of the width: at most 0.40 for a maskable icon."""
    n = len(grid)
    size = n * cell + 2 * rim
    far = 0.0
    for y in range(n):
        for x in range(n):
            if grid[y][x] is not None and grid[y][x] not in (CLAY, SHADOW):
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
        for name, (cells, cell, head, margin, ground, shadow, corner, clear, rim) in plan().items():
            grid = picture(cells, head, margin, ground, shadow, corner)
            note = ""
            where = out if name in APP_FILES else Path(tmp)
            if name == "icon-maskable-512.png":
                far = safe_zone(grid, cell, rim)
                assert far <= 0.40, "the head leaves the maskable safe zone: %.4f of the width" % far
                note = "  head within %.3f of the width from the middle (safe zone 0.400)" % far
            save(render(grid, cell, rim), where / name, not clear, 8 if name == "icon-maskable-512.png" else None)
            if a.check:
                note += "  checked: %d colors, every cell constant" % check(where / name, cells, cell, not clear, rim)
            if name in APP_FILES:
                total += (where / name).stat().st_size
            if name == "favicon-32.png":
                (fav / "favicon.png").write_bytes((where / name).read_bytes())
            print("%-24s %3d px  %2d x %2d cells of %2d px  %6d bytes  ?v=%s%s" % (name, cells * cell + 2 * rim, cells, cells, cell, (where / name).stat().st_size,
                                                                                   hashlib.sha256((where / name).read_bytes()).hexdigest()[:8], note))
        frames = [Image.open(Path(tmp) / n).convert("RGBA") for n in ("favicon-16.png", "favicon-32.png", "favicon-48.png")]
        frames[2].save(fav / "favicon.ico", format="ICO", append_images=frames[:2], sizes=[(48, 48), (32, 32), (16, 16)])
    for name in ("favicon.png", "favicon.ico"):
        print("%-24s %6d bytes  ?v=%s" % (name, (fav / name).stat().st_size, hashlib.sha256((fav / name).read_bytes()).hexdigest()[:8]))
    print("the four app files together: %d bytes" % total)


if __name__ == "__main__":
    main()
