"""The "ants!" logo of the game's title screen without its clay, with a soft shadow of its own (used by make_art.py).

A developer's tool (it needs Pillow: pip install pillow); not part of any build or test.
"""
import colorsys
from collections import deque

from PIL import Image, ImageFilter

from artlib import CLAY, CLAY_DOT, sprite

ANT = (150, 44, 286, 190)        # the ant mascot of the lettering: its stripes are the clay's own orange and its legs share the colour of the shadow


def make_logo():
    logo = sprite(162).crop((0, 48, 593, 270))
    pixels = logo.load()
    width, height = logo.size

    def in_ant(x, y):
        return ANT[0] <= x <= ANT[2] and ANT[1] <= y <= ANT[3]

    def is_clay(pixel):
        return pixel[:3] in (CLAY, CLAY_DOT)

    def reddish(pixel):
        hue, saturation, _ = colorsys.rgb_to_hsv(pixel[0] / 255, pixel[1] / 255, pixel[2] / 255)
        degrees = hue * 360
        return saturation > 0.22 and (degrees < 52 or degrees > 295)

    remove = [[False] * width for _ in range(height)]
    for y in range(height):                                      # outside the ant: every orange or reddish pixel (clay, its fringe, the old shadow) goes
        for x in range(width):
            if not in_ant(x, y) and (is_clay(pixels[x, y]) or reddish(pixels[x, y])):
                remove[y][x] = True
    queue = deque()                                              # inside the ant's box only the clay that is connected to the outside goes (4 neighbours, exact clay)
    seen = [[False] * width for _ in range(height)]
    for y in range(height):
        for x in range(width):
            if in_ant(x, y) and is_clay(pixels[x, y]):
                for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                    if not (0 <= nx < width and 0 <= ny < height) or remove[ny][nx] or not in_ant(nx, ny):
                        queue.append((x, y))
                        seen[y][x] = True
                        break
    while queue:
        x, y = queue.popleft()
        remove[y][x] = True
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if 0 <= nx < width and 0 <= ny < height and in_ant(nx, ny) and not seen[ny][nx] and is_clay(pixels[nx, ny]):
                seen[ny][nx] = True
                queue.append((nx, ny))
    mask = Image.new("L", (width, height), 0)
    mask_pixels = mask.load()
    for y in range(height):
        for x in range(width):
            if remove[y][x]:
                pixels[x, y] = (0, 0, 0, 0)
            else:
                mask_pixels[x, y] = 255
    blur = mask.filter(ImageFilter.GaussianBlur(3.2))
    shadow = Image.new("RGBA", (width, height), (30, 12, 6, 0))
    shadow.putalpha(blur.point(lambda v: int(v * 0.38)))
    out = Image.new("RGBA", (width + 16, height + 16), (0, 0, 0, 0))
    out.alpha_composite(shadow, (9, 9))
    out.alpha_composite(logo, (0, 0))
    return out.crop(out.getbbox())
