"""The gravel: the original's flecks (where they are, how big) redrawn as a few quiet pebbles on the original's pink-brown ground.

The plain gravel tile of the original (sprites 726 and 727) is a muted pink-brown field with about a dozen small gray and olive fleck clusters and some single pixels, at the same places in every
tile: that is what makes a tile countable. Here the two biggest fleck places of a tile become two pebbles (outline: the cluster's own, smoothed; a lit top, a shaded side and a small cast shadow),
a few single pixels become quiet grains, and the ground gets a slight hand-drawn tone (flat patches a few levels lighter or darker). The game picks one of sixteen pictures of a plain tile by the
cell's surroundings (0 to 5 calm, 6 to 8 stony, 9 to 11 damp, 12 to 13 worn, 14 to 15 cracked), so the variants are those five kinds of ground, every one on the same lattice of the original's flecks.

The pebbles blend in as the original's flecks do: measured in CIELAB, the original's fleck is a neutral gray (127, 127, 127) or an olive (139, 139, 115) whose LIGHTNESS differs from the ground by only
+1 to +5; it is the hue that makes it a mark. Every pebble keeps that hue, and its lightness is set around the ground's by a few steps (a lit top +5.0, the body +1.5, a shaded side -2.5, the cast
shadow -3.0); no outline.
"""
import numpy as np

import tilekit as K
from tilekit import MS, N, T, Tile

GROUND = (143, 119, 111)


# ------------------------------------------------------------------------------------------------ colors: the hue of a fleck at a lightness around the ground's

def lab(rgb):
    """CIELAB (D65) of an sRGB color or an array of them"""
    a = np.asarray(rgb, float) / 255.0
    a = np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)
    m = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    xyz = a @ m.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], axis=-1)


def lab_to_rgb(value):
    """the sRGB color (whole numbers) of a CIELAB value"""
    value = np.asarray(value, float)
    fy = (value[0] + 16) / 116
    fx, fz = fy + value[1] / 500, fy - value[2] / 200

    def cube(t):
        return t ** 3 if t ** 3 > 0.008856 else (t - 16 / 116) / 7.787
    xyz = np.array([cube(fx) * 0.95047, cube(fy), cube(fz) * 1.08883])
    m = np.array([[3.2406, -1.5372, -0.4986], [-0.9689, 1.8758, 0.0415], [0.0557, -0.2040, 1.0570]])
    c = m @ xyz
    c = np.where(c <= 0.0031308, 12.92 * c, 1.055 * np.maximum(c, 0) ** (1 / 2.4) - 0.055)
    return tuple(int(v) for v in np.clip(np.rint(c * 255), 0, 255))


_GROUND_LAB = lab(np.array(GROUND, float))
_HUE = {'gray': lab(np.array([127, 127, 127.0])), 'olive': lab(np.array([139, 139, 115.0]))}


def tone(kind, d_lightness):
    """a color of the fleck's hue whose lightness is the ground's plus d_lightness"""
    return lab_to_rgb([_GROUND_LAB[0] + d_lightness, _HUE[kind][1], _HUE[kind][2]])


PALETTE = {kind: (tone(kind, 5.0), tone(kind, 1.5), tone(kind, -2.5)) for kind in ('gray', 'olive')}      # kind: lit top, body, shaded side
SHADOW = lab_to_rgb([_GROUND_LAB[0] - 3.0, _GROUND_LAB[1], _GROUND_LAB[2]])                                 # the ground under a pebble's shadow
SHADOW_OFFSET = (0.30, 0.38)         # the cast shadow's offset, world pixels
PEBBLE = dict(t=0.40, sigma=0.46)    # how full and how round a pebble is beside its cluster

TONE = {'light': (146, 121, 113), 'dark': (140, 116, 108)}      # the ground's own tone, a few levels off its base
PATCH_DARK = (135, 112, 105)         # a damp patch
PATCH_PALE = (149, 125, 115)         # a dusty patch
CRACK = (106, 85, 82)
CRACK_LIT = (154, 130, 120)

# extra pebbles for the stony and damp kinds: x, y, half width, half height, turn (degrees), color. None lies on a place of the original's flecks.
EXTRA = [(6.6, 17.6, 1.7, 1.25, 20, 'gray'), (14.8, 24.6, 1.4, 1.1, -10, 'olive'), (27.4, 16.2, 1.8, 1.3, 5, 'gray'), (11.6, 5.8, 1.35, 1.0, 0, 'gray'),
         (21.6, 29.6, 1.6, 1.15, 15, 'gray'), (29.8, 22.4, 1.2, 1.0, 0, 'olive'), (3.4, 12.2, 1.3, 1.0, -20, 'gray'), (17.8, 14.4, 1.2, 0.95, 10, 'gray'),
         (8.6, 29.0, 1.4, 1.0, 0, 'olive'), (24.6, 8.2, 1.3, 1.05, -15, 'gray')]
DAMP = [(10, 22, 5.0, 3.2, 20), (25, 12, 4.2, 2.6, -25), (7, 5, 3.6, 2.4, 10), (20, 27, 4.6, 2.8, -10)]
DUST = [(9, 11, 6.0, 3.8, 15), (24, 22, 5.2, 3.4, -20), (16, 28, 5.0, 2.8, 5)]
CRACKS = [
    [[(2, 9), (7, 11.5), (12, 11), (17, 14.5), (22, 14), (30, 17.5)], [(12, 11), (14, 6.5), (13.5, 3)]],
    [[(4, 30), (9, 25.5), (8.5, 20), (14, 16), (20, 17), (26, 12), (31, 11)], [(14, 16), (15, 22)]],
    [[(0, 21), (6, 22.5), (10, 27), (17, 26), (23, 30), (31, 29)], [(17, 26), (19, 20.5), (24, 19)]],
    [[(7, 0), (9, 6), (6.5, 11), (11, 15.5), (10, 21)], [(11, 15.5), (17, 18), (23, 17.5), (29, 21)]],
]


def kind_of_variant(v):
    return 'calm' if v < 6 else 'stony' if v < 9 else 'damp' if v < 12 else 'worn' if v < 14 else 'cracked'


# ------------------------------------------------------------------------------------------------ shapes

def ellipse(cx, cy, rx, ry, turn=0.0):
    """the master mask of an ellipse (center, half axes and turn in degrees, world pixels)"""
    X = (np.arange(N) + 0.5) / MS
    Y = (np.arange(N) + 0.5) / MS
    c, s = np.cos(np.radians(turn)), np.sin(np.radians(turn))
    dx, dy = X[None, :] - cx, Y[:, None] - cy
    u, w = (dx * c + dy * s) / rx, (-dx * s + dy * c) / ry
    return u * u + w * w <= 1.0


def line_mask(pts, width):
    """the master mask of a polyline of the given width"""
    X = (np.arange(N) + 0.5) / MS
    Y = (np.arange(N) + 0.5) / MS
    d = np.full((N, N), 1e9)
    for (x0, y0), (x1, y1) in zip(pts[:-1], pts[1:]):
        px, py = X[None, :] - x0, Y[:, None] - y0
        dx, dy = x1 - x0, y1 - y0
        t = np.clip((px * dx + py * dy) / (dx * dx + dy * dy), 0, 1)
        d = np.minimum(d, np.hypot(px - t * dx, py - t * dy))
    return d <= width / 2.0


def stone_from_cluster(comp, flip_x, flip_y):
    """the master mask of a pebble whose outline is the original cluster's (smoothed, a little fuller), as (mask, (y, x) of its origin in samples)"""
    ys = np.array([c[0] for c in comp])
    xs = np.array([c[1] for c in comp])
    cy, cx = ys.mean(), xs.mean()
    if flip_x:
        xs = np.round(2 * cx - xs).astype(int)
    if flip_y:
        ys = np.round(2 * cy - ys).astype(int)
    pad = 6
    y0, x0 = ys.min() - pad, xs.min() - pad
    h, w = ys.max() - ys.min() + 1 + 2 * pad, xs.max() - xs.min() + 1 + 2 * pad
    m = np.zeros((h, w))
    m[ys - y0, xs - x0] = 1.0
    f = K.up_cubic(m)
    f = K.blur_edge(f, PEBBLE['sigma'])
    return (f >= PEBBLE['t']), (y0 * MS, x0 * MS)


def paste_mask(mask_local, origin):
    out = np.zeros((N, N), bool)
    oy, ox = origin
    h, w = mask_local.shape
    ys0, xs0 = max(0, oy), max(0, ox)
    ys1, xs1 = min(N, oy + h), min(N, ox + w)
    if ys1 > ys0 and xs1 > xs0:
        out[ys0:ys1, xs0:xs1] = mask_local[ys0 - oy:ys1 - oy, xs0 - ox:xs1 - ox]
    return out


def draw_stone(tile, mask, kind):
    """a pebble: its cast shadow on the ground, then its shaded side, body and lit top"""
    tile.paint(K.shift(mask, *SHADOW_OFFSET) & ~mask, SHADOW)
    bands = K.dome(mask, PEBBLE['sigma'] * 1.15)
    lit, body, shade = PALETTE[kind]
    tile.paint(bands == 1, shade, 3)
    tile.paint(bands == 2, body)
    tile.paint(bands == 3, lit, 3)


# ------------------------------------------------------------------------------------------------ the tile

def clusters(index):
    """The original's fleck clusters of three or more pixels, biggest first: (size, x, y, pixels, kind), and its single pixels as (x, y, kind)."""
    rgb = K.original(index)
    lab_map = np.zeros((T, T), int)
    lab_map[np.all(rgb == (127, 127, 127), axis=2)] = 1
    lab_map[np.all(rgb == (139, 139, 115), axis=2)] = 2
    big, singles = [], []
    for comp in K.components(lab_map > 0):
        ys, xs = [c[0] for c in comp], [c[1] for c in comp]
        cx, cy = np.mean(xs) + 0.5, np.mean(ys) + 0.5
        kind = 'olive' if sum(lab_map[y, x] == 2 for y, x in comp) * 2 >= len(comp) else 'gray'
        if len(comp) >= 3:
            big.append((len(comp), cx, cy, comp, kind))
        else:
            singles.append((cx, cy, kind))
    big.sort(key=lambda c: (-c[0], c[2], c[1]))
    return big, singles


def pick(big, n, min_dist):
    """the first n clusters, biggest first, that lie at least min_dist apart"""
    out = []
    for c in big:
        if all(np.hypot(c[1] - o[1], c[2] - o[2]) >= min_dist for o in out):
            out.append(c)
        if len(out) == n:
            break
    return out


def gravel_tile(index, variant):
    """a plain gravel tile (sprite 726 or 727), picture `variant` of sixteen"""
    kind = kind_of_variant(variant)
    big, singles = clusters(index)
    rs = np.random.RandomState(9000 + index * 31 + variant)
    tile = Tile(GROUND)
    for i in range(2):                                           # the ground's own tone: flat, hard-edged patches a few levels off the base
        x, y = rs.uniform(4, 28, 2)
        tile.paint(ellipse(x, y, rs.uniform(5, 9), rs.uniform(3, 5.5), rs.uniform(-30, 30)), TONE['light'] if (i + variant) % 2 else TONE['dark'])
    if kind == 'damp':
        for i in rs.choice(len(DAMP), 1, replace=False):
            x, y, rx, ry, turn = DAMP[i]
            tile.paint(ellipse(x, y, rx, ry, turn), PATCH_DARK)
    if kind == 'worn':
        for i in rs.choice(len(DUST), 1, replace=False):
            x, y, rx, ry, turn = DUST[i]
            tile.paint(ellipse(x, y, rx, ry, turn), PATCH_PALE)
    if kind == 'cracked':
        pattern = CRACKS[(variant - 14 + rs.randint(0, 2) * 2) % len(CRACKS)]
        for pts in pattern:
            tile.paint(K.shift(line_mask(pts, 0.95), -0.5, -0.5) & ~line_mask(pts, 0.95), CRACK_LIT)
            tile.paint(line_mask(pts, 0.95), CRACK)
    # which pebbles: the original's two biggest fleck places (and, in the stony kind, two more), by the kind of ground
    anchors = pick(big, 2, 12)
    extras = 0
    if kind == 'calm':
        chosen, grains = anchors, 8
    elif kind == 'stony':
        chosen, extras, grains = anchors + pick([c for c in big if c not in anchors], 2, 7), 1, 6
    elif kind == 'damp':
        chosen, extras, grains = anchors, 1, 6
    else:
        chosen, grains = anchors[:1], 3                          # worn and cracked
    stones = []
    for _, cx, cy, comp, col in chosen:
        local, origin = stone_from_cluster(comp, variant % 2 == 1, variant // 2 % 2 == 1)
        stones.append((cy, paste_mask(local, origin), col))
    if extras:
        for i in rs.permutation(len(EXTRA))[:extras]:
            x, y, rx, ry, turn, col = EXTRA[i]
            stones.append((y, ellipse(x, y, rx, ry, turn), col))
    if grains and singles:                                       # the quiet grains: the original's single pixels, in the pebbles' own color, not stones
        for i in rs.permutation(len(singles))[:grains]:
            x, y, col = singles[i]
            tile.paint(ellipse(x, y, 0.62, 0.56, 0), PALETTE[col][1])
    stones.sort(key=lambda s: s[0])
    for _, mask, col in stones:
        draw_stone(tile, mask, col)
    return tile


_plain = {}


def plain_gravel():
    """the master (RGB) of picture 0 of the plain gravel tile 726: the gravel that lies beside the mud and the slate in the edge tiles"""
    if 'g' not in _plain:
        _plain['g'] = gravel_tile(726, 0).rgb.copy()
    return _plain['g']
