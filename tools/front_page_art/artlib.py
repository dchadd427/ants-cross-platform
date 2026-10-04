"""Helpers of make_art.py: the game's sprites, the clay key, the team colours of the ant, the mouse arrow, saving a small PNG.

A developer's tool (it needs Pillow: pip install pillow); not part of any build or test.
"""
import colorsys
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
