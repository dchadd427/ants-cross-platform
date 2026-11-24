"""Smaller sheets without a visible change (README.md, "Size"): the windows' sprites are made with fewer distinct levels where the picture is busy anyway, and their 2x pictures are not stored at all:
the game makes them from the 3x ones (derive.py).

  quantize()   rounds the red, green, blue and alpha of a sprite to a multiple of `step`, but only where the neighborhood is busy (an edge, a stroke of the brush, the grain): the difference to the
               neighbors hides the rounding. A smooth ramp, a flat color, a dither of a level and the 0 and 255 of alpha stay exactly as they are, so a bevel never gets bands.
  tooth()      the clay tile, which is busy everywhere: all of it rounded to a multiple of `step` (a difference of at most step / 2 levels)

The steps are set per kind of sprite (STEP below); the manifest and the sheets of the game are made from the rounded pictures, so what is in the sheets is what the game draws. Nothing here touches the
sprites of the original's HUD: only the pictures that dialogs.export() makes pass through it.
"""
import numpy as np
from PIL import Image

import derive as DV

STEP = {'lettering': 24, 'fitting': 16, 'clay': 2}      # levels: plain lettering; buttons, switches, pedestals, frame, brushed headings, slider tracks; the clay's tooth
BUSY = {'lettering': 8, 'fitting': 6}                   # levels: how far a pixel is from the average of its four neighbors where the picture counts as busy (an edge, a stroke of the brush, grain)
CANDIDATES = ('qh_part_', 'op_part_', 'cd_title')       # the sprites that may be plain lettering (plain_lettering measures which are)
EXACT = ('cd_dot_',)                                    # the sprites that are never rounded: the color squares of the countdown card's rows (they are 11 x 11 pixels, and the color names the player)
DERIVE_MAX = 3.5                                        # levels: the most that a plain lettering's 2x picture made from the 3x one may differ (mean, over the pixels that show) from the one drawn at 2x
DERIVE_LIMIT = 25                                       # levels: the 2x picture of any sprite made from the 3x one may differ this much (mean) from the one drawn at 2x: more means the 3x picture is not the same drawing
DERIVE_LIMITS = {'sc_k_': 45}                           # the gold labels of the results screen (small capitals, a dark seat under every stroke): thin strokes whose edges are most of what shows, so the same drawing differs more between two grids
PERIODIC = {'panel_t': 'x', 'panel_b': 'x', 'panel_l': 'y', 'panel_r': 'y'}      # frame pieces that the game repeats along an axis: rounded as if the picture went on round that axis


def _pad(a, mode_x, mode_y):
    p = np.pad(a, ((1, 1), (0, 0)) + ((0, 0),) * (a.ndim - 2), mode=mode_y)
    return np.pad(p, ((0, 0), (1, 1)) + ((0, 0),) * (a.ndim - 2), mode=mode_x)


def _deviation(a, wrap):
    """per pixel: how far (levels, the largest of the four channels) it is from the average of its four neighbors. A smooth ramp is 0 whatever its slope, and so is a flat color; the +-1 of a dither
    is 1 or less; an edge, the brush and grain are many levels"""
    mx, my = ('wrap' if 'x' in wrap else 'edge'), ('wrap' if 'y' in wrap else 'edge')
    p = _pad(a.astype(np.int16), mx, my)
    h, w = a.shape[:2]
    mean = (p[:h, 1:w + 1].astype(np.float32) + p[2:h + 2, 1:w + 1] + p[1:h + 1, :w] + p[1:h + 1, 2:w + 2]) / 4.0
    return np.abs(a.astype(np.float32) - mean).max(axis=2)


def _busy(a, threshold, wrap):
    """True where a pixel is `threshold` levels or more from the average of its neighbors, or is next to such a pixel"""
    mx, my = ('wrap' if 'x' in wrap else 'edge'), ('wrap' if 'y' in wrap else 'edge')
    g = _deviation(a, wrap) >= threshold
    q = _pad(g, mx, my)
    return g | q[:-2, 1:-1] | q[2:, 1:-1] | q[1:-1, :-2] | q[1:-1, 2:]


def background(pic):
    """the color that most of the picture's smooth, opaque pixels have (RGBA array of 4 numbers; None when it has none): the ground that lettering stands on"""
    a = np.asarray(pic.convert('RGBA'))
    flat = (_deviation(a, '') <= 1) & (a[..., 3] == 255)
    px = a[flat][:, :3].astype(np.int64)
    if len(px) == 0:
        return None
    values, counts = np.unique((px[:, 0] << 16) | (px[:, 1] << 8) | px[:, 2], return_counts=True)
    m = int(values[counts.argmax()])
    return np.array([(m >> 16) & 255, (m >> 8) & 255, m & 255, 255], np.float32)


def quantize(pic, step, threshold, wrap='', origin=None):
    """`pic` (RGBA) with every channel rounded to a multiple of `step` where the picture is busy (alpha 0 and 255 kept, the color of a clear pixel zero); `wrap`: 'x' and 'y' for the axes along which the
    picture repeats (the neighbors of its last column are those of its first). `origin`: the color the multiples count from (the ground of lettering: the rounding then leaves a pixel of that color as it
    is, and does not draw a ring round every letter); None counts from 0."""
    a = np.asarray(pic.convert('RGBA'))
    busy = _busy(a, threshold, wrap)
    o = np.zeros(4, np.float32) if origin is None else origin
    r = np.clip(o + np.round((a.astype(np.float32) - o) / step) * step, 0, 255)
    r[..., 3] = np.where((a[..., 3] == 0) | (a[..., 3] == 255), a[..., 3], r[..., 3])
    out = np.where(busy[..., None], r, a).astype(np.uint8)
    out[out[..., 3] == 0, :3] = 0
    return Image.fromarray(out, 'RGBA')


def tooth(pic, step):
    """the clay tile (opaque) with red, green and blue rounded to a multiple of `step` everywhere"""
    a = np.asarray(pic.convert('RGBA')).copy()
    assert (a[..., 3] == 255).all(), 'the clay is opaque'
    a[..., :3] = np.clip(np.round(a[..., :3].astype(np.float32) / step) * step, 0, 255).astype(np.uint8)
    return Image.fromarray(a, 'RGBA')


def derive_error(p2, p3):
    """the mean difference (levels, over the pixels that show in either) between the 2x picture `p2` and the one made from the 3x picture `p3`"""
    d = np.asarray(DV.derive(p3)).astype(int)
    n = np.asarray(p2.convert('RGBA')).astype(int)
    shown = np.maximum(d[..., 3], n[..., 3]) > 0
    return float(np.abs(d - n)[..., :3].max(axis=2)[shown].mean()) if shown.any() else 0.0


def plain_lettering(images):
    """the names of the sprites that are plain lettering (images: {scale: {name: picture}}, as drawn): the 2x picture made from the 3x one is within DERIVE_MAX of the one drawn at 2x, which the brushed
    headings, the slider tracks and the buttons are not (their brush is drawn on each scale's own grid). Their edges are the only busy part, so they take the larger step"""
    return {n for n in images[3] if n.startswith(CANDIDATES) and derive_error(images[2][n], images[3][n]) <= DERIVE_MAX}


def shrink(images):
    """every picture of `images` ({scale: {name: picture}}, the windows' sprites as drawn) made smaller to store, in place: rounded to fewer levels (quantize, tooth), and the 2x picture of each sprite
    replaced by the one that the game makes from the 3x one when it loads the 2x sheet. Returns the names of those sprites (all of them)."""
    assert 2 in images and 3 in images, 'the 2x pictures are made from the 3x ones'
    plain = plain_lettering(images)
    drawn = dict(images[2])                                      # (the pictures drawn at 2x, to say how far the derived ones are)
    for pics in images.values():
        for name in list(pics):
            if name == 'panel_c':
                pics[name] = tooth(pics[name], STEP['clay'])
            elif name.startswith(EXACT):
                continue
            else:
                kind = 'lettering' if name in plain else 'fitting'
                pics[name] = quantize(pics[name], STEP[kind], BUSY[kind], PERIODIC.get(name, ''), background(pics[name]) if kind == 'lettering' else None)
    worst = ('', 0.0)
    for name in images[3]:
        err = derive_error(drawn[name], images[3][name])
        limit = next((v for prefix, v in DERIVE_LIMITS.items() if name.startswith(prefix)), DERIVE_LIMIT)
        if err > limit:
            raise SystemExit('the 2x picture of %s made from its 3x one differs from the one drawn at 2x by %.1f levels (the limit is %d): it is not the same drawing' % (name, err, limit))
        worst = max(worst, (name, err), key=lambda t: t[1])
        images[2][name] = DV.derive(images[3][name])
    print('2x made from 3x: %d sprites, the largest mean difference from a drawing at 2x is %.1f levels (%s)' % (len(images[3]), worst[1], worst[0]), flush=True)
    return set(images[3])
