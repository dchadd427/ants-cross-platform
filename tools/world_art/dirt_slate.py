"""Plain dirt (sprites 728 to 731), plain slate (sprites 812 to 820) and the bridge (sprites 112 to 116), in the recipe of the gravel and the mud: matte, flat colors, hard color steps, the
original's shapes, colors and per-tile rhythm, as quiet as the original.

- Dirt: the original's light sand in its two tones. Where the original's speckle is densest (its diagonal bands) there are drifts of slightly darker sand, each a different length, width and place,
  all along the original's diagonal, so a field keeps the original's rhythm and its overall tone; loose grains at places of their own; the gray stones of the stony tiles keep their shapes and rims.
  Sixteen pictures of a plain tile (the game picks one by the cell's place, as for the gravel). Nothing touches the tile's border, so any two pictures join.
- Slate: the original's three tones as clean flat bands (the stair-steps become curves), its cracks as crisp lines.
- Bridge: each stage's mound traced shape for shape (clean curves, the original's colors; transparent outside); 1x is the original's pixels.
"""
import numpy as np

import tilekit as K
from catalog import sprite
from tilekit import MS, N, T, Tile

D1 = (219, 207, 199)           # the original's two sands
D2 = (199, 187, 175)
DRIFT = (205, 193, 182)        # a drift of slightly darker sand: between the two tones

_dirt = {}


def dirt_master(index=728, v=0):
    """The master (RGB) of picture v of the plain dirt tile `index`: the original's two tones; a few drifts of slightly darker sand along its diagonal; loose grains. Picture 0 of sprite 728 is
    also the dirt inside every edge tile."""
    if (index, v) in _dirt:
        return _dirt[(index, v)]
    rgb = K.original(index)
    out = np.zeros((N, N, 3), np.uint8)
    out[:] = D1
    r = np.random.default_rng(1000 * index + v)
    ys, xs = np.mgrid[0:N, 0:N]
    X = (xs + 0.5) / MS
    Y = (ys + 0.5) / MS
    for _ in range(int(r.choice([3, 4, 4, 5]))):                       # long thin drifts along the original's diagonal
        cx, cy = r.uniform(7, 25, 2)
        L = r.uniform(12, 25)
        wd = r.uniform(0.9, 2.1)
        ang = np.radians(-38 + r.uniform(-5, 5))
        ca, sa = np.cos(ang), np.sin(ang)
        u = (X - cx) * ca + (Y - cy) * sa
        w = -(X - cx) * sa + (Y - cy) * ca
        t = np.clip(u / (L / 2), -1, 1)
        half = wd * (1 - t * t) ** 0.6 * (1 + 0.28 * np.sin(u * r.uniform(0.7, 1.3) + r.uniform(0, 6)) + 0.15 * np.sin(u * r.uniform(1.8, 2.6) + r.uniform(0, 6)))      # a lens with an uneven edge
        m = (np.abs(u) < L / 2) & (np.abs(w - 0.6 * np.sin(u * 0.45 + r.uniform(0, 6))) < half) & (X > 1.2) & (X < T - 1.2) & (Y > 1.2) & (Y < T - 1.2)
        out[m] = DRIFT
    for _ in range(int(r.integers(7, 13))):
        cx, cy = r.uniform(1.5, T - 1.5, 2)
        out[((X - cx) / 0.62) ** 2 + ((Y - cy) / 0.46) ** 2 < 1] = D2
    others = sorted(set(map(tuple, rgb.reshape(-1, 3))) - {D1, D2})
    if others:                                                         # the gray stones of the stony tiles: their own shapes and rims
        lab = np.zeros((T, T), int)
        for k, c in enumerate(others, 1):
            lab[K.is_col(rgb, c)] = k
        cm = K.classmap(lab, len(others) + 1, soft=0.3, border=1.0)
        for k, c in enumerate(others, 1):
            out[cm == k] = c
    _dirt[(index, v)] = out
    return out


def dirt_tile(index, v):
    return Tile(rgb=dirt_master(index, v).copy())


def slate_tile(index):
    """a plain slate tile: the original's bands and cracks as clean shapes"""
    return Tile(rgb=K.trace(K.original(index), []))


def bridge_pictures(number):
    """{scale: RGBA array} of a bridge sprite: the original's shapes traced, transparent outside; 1x is the original's pixels"""
    im = sprite(number)
    a = np.asarray(im)
    h, w = a.shape[:2]
    opaque = a[..., 3] > 0
    cols = sorted(set(map(tuple, a[opaque][:, :3])))
    lab = np.zeros((h + 4, w + 4), int)
    for j, c in enumerate(cols, 1):
        lab[2:-2, 2:-2][opaque & (a[..., :3] == np.array(c)).all(-1)] = j
    cm = K.classmap(lab, len(cols) + 1, soft=0.3, border=0.01)
    rgb = np.zeros(cm.shape + (3,), float)
    alpha = (cm > 0).astype(float)
    for j, c in enumerate(cols, 1):
        rgb[cm == j] = c
    out = {1: np.array(im)}
    for scale, f in ((3, 4), (2, 6)):
        hh, ww = cm.shape[0] // f, cm.shape[1] // f
        aa = alpha.reshape(hh, f, ww, f).mean((1, 3))
        cc = (rgb * alpha[..., None]).reshape(hh, f, ww, f, 3).mean((1, 3)) / np.maximum(aa, 1e-6)[..., None]
        o = np.dstack([np.rint(cc), np.rint(aa * 255)]).astype(np.uint8)[2 * scale:-2 * scale, 2 * scale:-2 * scale]
        assert o.shape[:2] == (h * scale, w * scale)
        out[scale] = o
    return out
