"""Helpers of make_art.py: the game's sprites, the clay key, the clay of the pages, the team colours of the ant, the mouse arrow, saving a small PNG.

A developer's tool (it needs Pillow: pip install pillow); not part of any build or test.
"""
import colorsys
import random
import re
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
CATALOG = ROOT / "asset_catalog"           # the sprites that the repository already ships, extracted from Original-Ants/ants.chd
CLAY = (0xDB, 0x4B, 0x13)                  # the orange of every menu screen of the game
CLAY_DOT = (0xFB, 0x33, 0x5B)              # the pink dots that its dithering leaves in it


def _catalog():
    text = (CATALOG / "catalog_data.js").read_text(encoding="utf-8")
    start = text.find("sprites:")
    return {int(i): (name, int(w), int(h), file) for i, name, w, h, file in
            re.findall(r'\{ id: (\d+), name: "([^"]+)", width: (\d+), height: (\d+), file: "([^"]+)" \}', text[start:])}


SPRITES = _catalog()


def sprite(number):
    """The sprite with this id of asset_catalog/catalog_data.js, as RGBA."""
    return Image.open(CATALOG / SPRITES[number][3]).convert("RGBA")


def key_clay(image):
    """The clay behind a hand-lettered label or a button (and its pink dots) becomes transparent; the maroon edge of the picture stays."""
    image = image.convert("RGBA")
    pixels = image.load()
    for y in range(image.height):
        for x in range(image.width):
            if pixels[x, y][:3] in (CLAY, CLAY_DOT):
                pixels[x, y] = (0, 0, 0, 0)
    return image


# The clay of the pages (web/front/clay.png): the orange of the game's menus with a little noise and dirt, so that it is not so clean. The tile is made, not cut from a sprite (the original's tile is
# a flat orange with a few pink dots), and it is the same tile every time: only random() of a seeded generator is used, whose sequence Python keeps for a seed. It repeats without a seam.
CLAY_SIZE = 256
CLAY_DEEP = (216, 71, 16)          # the deepest broad shade: ink (#0c0805) on it is 4.59 : 1, so a whole patch of it still holds the 4.5 : 1 that the text on the bare page needs
CLAY_LIGHT = (233, 94, 36)         # the lightest broad shade
CLAY_DIRT = (168, 58, 18)          # a speck of dirt: a brown that holds ink at 3.1 : 1 by itself
CLAY_GRIT = (224, 130, 98)         # a pale grain of grit
INK = (0x0C, 0x08, 0x05)           # the ink of the pages (--ink)


def _luminance(colour):
    """The relative luminance of WCAG 2.x (the same arithmetic as tests/scripts/test_web_front.py)."""
    def channel(value):
        value /= 255
        return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4
    r, g, b = (channel(v) for v in colour)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def _mix(one, other, amount):
    return tuple(int(round(one[i] + (other[i] - one[i]) * amount)) for i in range(3))


def _noise(draw, size, cells):
    """Smooth random numbers from 0 to 1 that wrap at the edges: a lattice of cells x cells values, smoothly blended."""
    lattice = [[draw() for _ in range(cells)] for _ in range(cells)]
    step = size / cells

    def blend(position):
        low = int(position)
        fraction = position - low
        return low % cells, (low + 1) % cells, fraction * fraction * (3 - 2 * fraction)

    out = []
    for y in range(size):
        y0, y1, ty = blend(y / step)
        row = []
        for x in range(size):
            x0, x1, tx = blend(x / step)
            top = lattice[y0][x0] * (1 - tx) + lattice[y0][x1] * tx
            bottom = lattice[y1][x0] * (1 - tx) + lattice[y1][x1] * tx
            row.append(top * (1 - ty) + bottom * ty)
        out.append(row)
    return out


def dirty_clay(size=CLAY_SIZE, seed=1998, mottling=0.6, grain=0.8, specks=0.010):
    """The clay of the pages: CLAY with a little noise and dirt. On average it is the original's orange (so the flat colour behind it, and the pictures cut from the original's sprites, sit right).

    Added to it: a broad mottling (patches a little deeper and a little lighter than the clay), grain (each pixel a step off its neighbours), pale grit and small brown specks of dirt (one to four
    pixels). The ink of the pages keeps its contrast: the deepest shade holds 4.5 : 1, a speck is never darker than 3 : 1, and a speck is only put where the 5 x 5 pixels around it still hold 4.5 : 1 on
    average (tests/scripts/test_web_front.py reads the picture and checks all three)."""
    draw = random.Random(seed).random
    broad = _noise(draw, size, size // 32)
    fine = _noise(draw, size, size // 10)
    levels = 9
    shades = [_mix(CLAY_DEEP, CLAY, i / 4) for i in range(5)] + [_mix(CLAY, CLAY_LIGHT, i / 4) for i in range(1, 5)]       # deep .. clay .. light, nine steps
    pixels = [[None] * size for _ in range(size)]
    for y in range(size):
        for x in range(size):
            wanted = 0.5 + mottling * (0.6 * broad[y][x] + 0.4 * fine[y][x] - 0.5) * 1.6 + (draw() - 0.5) * grain
            pixels[y][x] = shades[int(round(max(0.0, min(1.0, wanted)) * (levels - 1)))]
    light = [[_luminance(c) for c in row] for row in pixels]
    needed = (4.5 * (_luminance(INK) + 0.05)) - 0.05                           # the luminance that holds 4.5 : 1 against the ink

    def window(cx, cy):
        return sum(light[(cy + dy) % size][(cx + dx) % size] for dy in range(-2, 3) for dx in range(-2, 3)) / 25

    def pick(count):
        return int(draw() * count)

    specks = int(size * size * specks)
    for _ in range(size * size * 4):
        if specks <= 0:
            break
        cx, cy = pick(size), pick(size)
        cells = [(cx, cy)] + [((cx + pick(3) - 1) % size, (cy + pick(3) - 1) % size) for _ in range((0, 0, 1, 1, 2, 3)[pick(6)])]
        shade = (0.55, 0.8, 1.0)[pick(3)]
        old = [(x, y, pixels[y][x], light[y][x]) for x, y in cells]
        for x, y in cells:
            pixels[y][x] = _mix(pixels[y][x], CLAY_DIRT, shade)
            light[y][x] = _luminance(pixels[y][x])
        if all(window((cx + dx) % size, (cy + dy) % size) >= needed for dx in range(-3, 4) for dy in range(-3, 4)):
            specks -= len(cells)
        else:
            for x, y, colour, value in reversed(old):
                pixels[y][x], light[y][x] = colour, value
    for _ in range(int(size * size * 0.0035)):
        x, y = pick(size), pick(size)
        pixels[y][x] = _mix(pixels[y][x], CLAY_GRIT, (0.4, 0.7)[pick(2)])
    image = Image.new("RGB", (size, size))
    image.putdata([c for row in pixels for c in row])
    return image


# The grey and dark purple body of the black team's ant is tinted to the colour of each team, (hue 0..1, saturation, value gain); the saturated parts (gold, pink) stay.
TEAMS = {"green": (0.40, 0.55, 1.10), "red": (0.01, 0.72, 1.05), "blue": (0.62, 0.62, 1.10), "black": None}


def recolor_ant(image, team):
    spec = TEAMS[team]
    image = image.convert("RGBA")
    if spec is None:
        return image
    hue, saturation, gain = spec
    pixels = image.load()
    for y in range(image.height):
        for x in range(image.width):
            r, g, b, a = pixels[x, y]
            if a == 0:
                continue
            h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            if s < 0.42 and not (v > 0.93 and s < 0.08):             # the body; pure white (eye whites, highlights) stays
                v2 = min(1.0, v * gain + 0.04)
                s2 = saturation * (0.35 + 0.65 * min(1.0, v2 * 1.15))
                if v2 < 0.22:
                    s2 *= 0.7
                nr, ng, nb = colorsys.hsv_to_rgb(hue, s2, v2)
                pixels[x, y] = (int(nr * 255), int(ng * 255), int(nb * 255), a)
    return image


def remove_cursor(image, box=(476, 266, 500, 296)):
    """The game draws its mouse arrow (white with a black outline) in the middle of a screenshot; it is painted over from the pixels around it."""
    image = image.convert("RGB")
    pixels = image.load()
    x0, y0, x1, y1 = box
    mask = set()
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b = pixels[x, y]
            if min(r, g, b) >= 200 or max(r, g, b) <= 12 or (r, g, b) in ((227, 159, 7), (143, 83, 59)):
                mask.add((x, y))
    grown = set(mask)                                                 # one pixel wider, so that no outline is left
    for x, y in mask:
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                grown.add((x + dx, y + dy))
    mask = grown
    known = {}
    for _ in range(40):
        if not mask:
            break
        filled = {}
        for x, y in sorted(mask):
            around = []
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    if dx == 0 and dy == 0:
                        continue
                    n = (x + dx, y + dy)
                    if n not in mask:
                        around.append(pixels[n])
                    elif n in known:
                        around.append(known[n])
            if around:
                filled[(x, y)] = tuple(sum(c[i] for c in around) // len(around) for i in range(3))
        if not filled:
            break
        for point, colour in filled.items():
            pixels[point] = colour
            known[point] = colour
            mask.discard(point)
    return image


def save_png(image, path):
    """As small as PNG goes without changing a pixel: a picture of at most 256 colours (with their transparency) is saved with a palette, and every file is optimised."""
    image = image.convert("RGBA") if "A" in image.getbands() else image.convert("RGB")
    colours = image.getcolors(maxcolors=256)
    if colours is None:
        image.save(path, optimize=True)
        return
    rgba = [c if len(c) == 4 else c + (255,) for _, c in sorted(colours, key=lambda item: -item[0])]
    index = {c: i for i, c in enumerate(rgba)}
    packed = Image.new("P", image.size)
    packed.putdata([index[p if len(p) == 4 else p + (255,)] for p in image.getdata()])
    packed.putpalette([v for c in rgba for v in c[:3]])
    alpha = bytes(c[3] for c in rgba)
    if min(alpha) < 255:
        packed.save(path, optimize=True, transparency=alpha)
    else:
        packed.save(path, optimize=True)
    again = Image.open(path).convert("RGBA")
    assert again.tobytes() == image.convert("RGBA").tobytes(), path          # a palette is only used when it is exact
