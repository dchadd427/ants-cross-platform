"""The mud, its bubbles and the water.

MUD. The original's plain mud (sprite 640) is one flat gray (75, 75, 75), and its bubbles (sprites 641 to 657 and 901) are small rings, domes and splashes of a few brown-gray and olive pixels. Here the
mud stays CLEAN: the original's gray with only a very quiet clay tone (flat, hard-edged patches three or four levels off the base), no stone, lump, lip or mark. The bubbles are the original's own
pictures (every frame, every place) drawn as domes and rings with a lit side, a shaded side and a shadow, as subtle as the original's.

WATER. The original's water (sprites 690 to 693, four frames of 150 ms) is deep blue with two diagonal streaks a tile that move over the frames. Here the water is as quiet as the original's: its
own streaks in its own four colors as clean shapes, each with a thin, slightly darker underside (matte; no highlights added).
"""
import numpy as np

import tilekit as K
from gravel import ellipse
from tilekit import T, Tile

MUD = (75, 75, 75)
MUD_TONE = {'light': (77, 77, 76), 'dark': (73, 73, 74)}


def mud_tile(variant):
    """the plain mud of a cell of this variant (a Tile), without bubbles"""
    tile = Tile(MUD)
    rs = np.random.RandomState(9500 + variant)
    for i in range(2):
        x, y = rs.uniform(3, 29, 2)
        tile.paint(ellipse(x, y, rs.uniform(7, 12), rs.uniform(4, 7), rs.uniform(-30, 30)), MUD_TONE['light'] if (i + variant) % 2 else MUD_TONE['dark'])
    return tile


_plain = {}


def plain_mud(variant):
    """the master (RGB) of the plain mud of a variant: the clay beside the other kinds in the edge tiles"""
    if variant not in _plain:
        _plain[variant] = mud_tile(variant).rgb.copy()
    return _plain[variant]


# --------------------------------------------------------------------------------------------- bubbles

BUBBLE_COLORS = {(75, 75, 75): 0, (91, 87, 75): 1, (99, 99, 99): 2, (115, 115, 95): 3, (127, 127, 127): 4, (147, 147, 147): 4, (67, 63, 55): 5}
BUB = {1: (99, 95, 85), 2: (113, 112, 108), 3: (131, 130, 118), 4: (158, 157, 152), 5: (48, 46, 45)}      # rim, light flank, lighter, brightest, hollow
BUB_SHADOW = (54, 54, 56)


def bubble_frame(sprite_number, variant):
    """the mud of the variant with the original's bubble of that frame drawn as a dome or ring with form"""
    tile = mud_tile(variant)
    rgb = K.original(sprite_number)
    lab = np.zeros((T, T), int)
    for y in range(T):
        for x in range(T):
            lab[y, x] = BUBBLE_COLORS.get(tuple(int(v) for v in rgb[y, x]), 0)
    if not lab.any():
        return tile
    cm = K.classmap(lab, 6, soft=0.25, border=0.0001)
    under = cm > 0
    tile.paint(K.shift(under, 0.9, 1.0) & ~under, BUB_SHADOW)
    form = K.dome(under, 0.55, hi=0.84, lo=0.18)
    for k in (1, 2, 3, 4, 5):
        m = cm == k
        if not m.any():
            continue
        col = BUB[k]
        if k == 5:
            tile.paint(m, col)
            continue
        shade = tuple(int(c * 0.80) for c in col)
        lit = tuple(min(255, int(c * 1.10 + 6)) for c in col)
        tile.paint(m & (form == 1), shade)
        tile.paint(m & (form == 2), col)
        tile.paint(m & (form == 3), lit)
    return tile


# --------------------------------------------------------------------------------------------- water

WATER = (27, 59, 111)
WATER_UNDER = (22, 51, 101)
WATER_SHADOW = (17, 40, 87)           # the bank's shadow on the water in an edge tile


def water_tile(sprite_number):
    """a frame of the water: the original's streaks as clean shapes, each with a thin darker underside"""
    out = K.trace(K.original(sprite_number), [])
    streak = ~(out == np.array(WATER)).all(-1)
    under = K.shift_border(streak, 0.45, 0.55) & ~streak
    out[under] = WATER_UNDER
    return Tile(rgb=out)
