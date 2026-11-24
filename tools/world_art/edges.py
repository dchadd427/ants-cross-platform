"""The edge tiles: two kinds of ground with an outline band between them, cut along a curve that joins the neighboring tiles' curves.

The original's table has edge tiles for water and mud (tiles wm and MW), gravel and mud (gm, mg), gravel and dirt (gd, dg), mud and dirt (md, dm) and for slate against dirt, gravel and mud
(ds, sd, gs, sg, ms, sm). Every region and every band is the original's own, read from the tile's pixels (a stair-step turned into a diagonal; the tile's own border left exactly as the
original's, so every join still meets), and everything in the regions is the new drawing: the plain tile's picture on each side, so that a field and its edges agree.

- Water-mud and gravel-mud (`bank_tile`): the band is a bank with a lit lip where it faces the light, a shaded face where it turns away, and the bank's shadow on the water or the gravel.
- Soft edges (gravel-dirt, mud-dirt; `soft_edge`): the original's dithered transition becomes clean contour steps in the original's own band colors, along the original's boundary.
- Rock edges (slate against dirt, gravel, mud; `rock_edge`): the original's rocks, outlines and slate bands traced shape for shape.
"""
import numpy as np

import tilekit as K
from catalog import tile_sprites
from dirt_slate import D1, D2, dirt_master
from gravel import GROUND, SHADOW, plain_gravel
from mud_water import MUD, WATER_SHADOW, plain_mud, water_tile
from tilekit import MS, N, T, Tile

WATER_BLUES = {(27, 59, 111), (43, 71, 115), (31, 75, 139), (43, 99, 123)}
GRAVEL_COLORS = {(143, 119, 111), (127, 127, 127), (139, 139, 115)}
BAND_WATER = {(67, 63, 55): 2, (51, 51, 51): 3, (43, 35, 35): 4}       # from the mud outwards
BAND_GRAVEL = {(91, 87, 75): 2, (99, 99, 99): 3}
# the band's steps redrawn: (shaded face, original, lit lip)
BAND_PAL_WATER = {2: ((52, 49, 43), (67, 63, 55), (98, 91, 78)), 3: ((38, 38, 38), (51, 51, 51), (80, 80, 80)), 4: ((30, 24, 24), (43, 35, 35), (70, 58, 56))}
BAND_PAL_GRAVEL = {2: ((72, 69, 59), (91, 87, 75), (122, 116, 100)), 3: ((80, 80, 80), (99, 99, 99), (134, 133, 128))}
LUM = np.array([0.3, 0.6, 0.1])                                         # the weights of a color's lightness


def classify(rgb, family):
    """the original edge tile as a map of classes: 0 the other kind (water or gravel), 1 mud, 2.. the band's steps from the mud outwards"""
    lab = np.zeros((T, T), int)
    water = family in ('wm', 'MW')
    bands = BAND_WATER if water else BAND_GRAVEL
    other = WATER_BLUES if water else GRAVEL_COLORS
    table = {MUD: 1}
    table.update(bands)
    for y in range(T):
        for x in range(T):
            c = tuple(int(v) for v in rgb[y, x])
            if c in other:
                lab[y, x] = 0
            elif c in table:
                lab[y, x] = table[c]
            else:       # a stray pixel: the nearest of the known colors
                best = min(list(table) + list(other), key=lambda k: sum((a - b) ** 2 for a, b in zip(k, c)))
                lab[y, x] = table.get(best, 0)
    return lab


_water = {}


def water_layer(rgb_edge, lab):
    """the frame of the plain water that this edge tile's water is (its water pixels equal that frame's), drawn"""
    water = lab == 0
    for f in range(4):
        if np.all(K.original(690 + f)[water] == rgb_edge[water]):
            break
    else:
        f = 0
    if f not in _water:
        _water[f] = water_tile(690 + f)
    return _water[f]


def bank_tile(number, family):
    """an edge tile of the water-mud (wm, MW) or gravel-mud (gm, mg) families"""
    rgb = K.original(number)
    lab = classify(rgb, family)
    water_side = family in ('wm', 'MW')
    cm = K.classmap(lab, 5 if water_side else 4, soft=0.25, border=1.0)
    out = water_layer(rgb, lab).rgb.copy() if water_side else plain_gravel().copy()
    out[cm == 1] = plain_mud((number * 7) % 6)[cm == 1]               # the clay of this tile: one of the six calm ones, by the sprite's number, so a bank's tiles do not repeat
    pal = BAND_PAL_WATER if water_side else BAND_PAL_GRAVEL
    bank = cm >= 1                                                    # the mud and its band: what stands as the bank
    out[K.shift_border(bank, 1.0, 1.1) & (cm == 0)] = WATER_SHADOW if water_side else SHADOW      # the bank's shadow on the other kind, to the lower right
    lit = bank & ~K.shift_border(bank, 0.95, 0.95)                    # the band, its steps as the original has them; lit where it faces the light, shaded where it turns away
    dark = bank & ~K.shift_border(bank, -0.95, -0.95)
    for k in sorted(pal):
        m = cm == k
        shade, base, lip = pal[k]
        out[m] = base
        out[m & dark] = shade
        out[m & lit] = lip
    tile = Tile(rgb=out)
    for k in pal:
        for c in pal[k]:
            tile.priority[K.code_of(c)] = 1.6                         # the colors that must not be voted away at 1x: the band's steps and the lit lip
    return tile


# --------------------------------------------------------------------------------------------- soft edges and rock edges

STEPS = {                                   # the lower ground, the original's band colors outward, and the ground above (the dirt)
    'gd': (GROUND, [(139, 139, 115), (159, 151, 135), (179, 167, 155)]),
    'md': (MUD, [(91, 87, 75), (99, 99, 99), (127, 127, 127), (147, 147, 147), (171, 171, 171)]),
}
STEPS['dg'] = STEPS['gd']
STEPS['dm'] = STEPS['md']


def enclosed(rgb, ground, limit=8):
    """The ground's region at 1x: its own color(s) plus the small marks that lie wholly inside it (flecks, speckle)."""
    g = K.is_col(rgb, ground) if isinstance(ground[0], int) else np.any([K.is_col(rgb, c) for c in ground], 0)
    out = g.copy()
    h, w = g.shape
    for comp in K.components(~g):
        if len(comp) > limit:
            continue
        cs = set(comp)
        if all((not (0 <= y + dy < h and 0 <= x + dx < w)) or g[y + dy, x + dx] or (y + dy, x + dx) in cs for y, x in comp for dy in (-1, 0, 1) for dx in (-1, 0, 1)):
            for y, x in comp:
                out[y, x] = True
    return out


def soft_edge(number, family):
    """gravel-dirt and mud-dirt: the original's dithered transition as clean contour steps in its own band colors"""
    rgb = K.original(number).copy()
    low, band = STEPS[family]
    if low == GROUND:
        rgb[enclosed(rgb, GROUND)] = GROUND                                                                     # the gravel's own flecks are not part of the transition
    lum = rgb.astype(float) @ LUM
    f0 = K.up_edge(lum)
    f = K.blur_edge(f0, 0.95)
    d = np.minimum.reduce(np.meshgrid(np.minimum((np.arange(N) + 0.5) / MS, T - (np.arange(N) + 0.5) / MS), np.minimum((np.arange(N) + 0.5) / MS, T - (np.arange(N) + 0.5) / MS)))
    w = K.smoothstep(d / 2.0)
    f = w * f + (1 - w) * K.blur_edge(f0, 0.45)                                                                 # near the tile's border less smoothing, so neighbors' steps meet
    lums = [float(np.array(low) @ LUM)] + [float(np.array(c) @ LUM) for c in band] + [203.5]
    thr = [(a + b) / 2 for a, b in zip(lums[:-1], lums[1:])]
    thr[0] += 3
    thr[-1] = min(thr[-1], 186.0)
    k = np.digitize(f, thr)
    out = np.zeros((N, N, 3), np.uint8)
    lo = plain_gravel() if low == GROUND else plain_mud((number * 7) % 6)
    out[k == 0] = lo[k == 0]
    for j, c in enumerate(band, 1):
        out[k == j] = c
    dm = dirt_master()
    out[k == len(band) + 1] = dm[k == len(band) + 1]
    return Tile(rgb=out)


def rock_edge(number, family):
    """the slate edges: the original's rocks, outlines and slate bands traced; the gravel, mud or dirt beside them from the plain tile's drawing"""
    rgb = K.original(number)
    regions = []
    if 'g' in family:
        regions.append((enclosed(rgb, GROUND), plain_gravel()))
    if 'm' in family:
        regions.append((K.is_col(rgb, MUD), plain_mud((number * 7) % 6)))
    if 'd' in family:
        regions.append((enclosed(rgb, (D1, D2), 3), dirt_master()))
    return Tile(rgb=K.trace(rgb, regions))


def edge_numbers(family, suffixes='ab'):
    """the sprites of every edge tile of a family (names like gm02a to gm09b), in order of the table's tile names"""
    out = []
    for k in range(2, 10):
        for suffix in suffixes:
            out += [number for number, _ in tile_sprites('%s%02d%s' % (family, k, suffix))]
    return out
