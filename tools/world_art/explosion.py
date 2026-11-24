"""The bomb's explosion (sprites 306 to 315, the ten frames of the blast): the approved smooth version, drawn as light (README.md).

The original's own shapes, frames, sizes and offsets are kept (where the fire and the smoke are comes from the original's frames, read as classes: classify); what changes is how the picture sits on
the world, as with the fire wall (fire.py): no outline, see-through, and no hard step anywhere.
  - The fire is one continuous heat field: a smooth color and opacity ramp from the dark red rim through orange and yellow to a white core, a fine curl of flame on it, and a bloom round the hot parts.
  - The smoke is a volume: billowing puffs with a lit side (toward the upper left) and a shaded side, lit warm by the fire near it, its rim broken up by the billows and fading out at a soft edge; its
    opacity is 0.72 to 0.85 by the original's tone. The ground, the water and the ant show through it where it is thin.
Each frame is two pictures: the whole (smoke and fire), packed under its sprite's number, and the light (the fire and its bloom alone, over nothing), packed under 30000 plus it, for a pass that adds
the fire to the ground as light. No color of the original is copied: only its shapes and how light its smoke is.

The drawing is on a master of 12 samples for each world pixel (and a margin of 4 pixels, cut off again), in single precision as it was approved; 1x, 2x and 3x are all averages of that one
painting. The unevenness of the smoke and the flame is a value noise (Pillow's bicubic enlargement of a seeded random grid), so it is as reproducible as Pillow's resize is.
"""
import math

import numpy as np
from PIL import Image

from catalog import sprite

SS = 12                                                             # master samples for each world pixel
PAD = 4                                                             # world pixels of margin round a sprite while it is drawn
F = np.float32
LIGHT = np.array([-0.62, -0.78])                                    # toward the light on screen (up and left), as in the original's shading
RAMP_T = [0.0, 0.08, 0.20, 0.36, 0.54, 0.72, 0.90, 1.0]            # the fire's heat ...
RAMP_C = [(150, 14, 24), (206, 30, 38), (246, 58, 60), (255, 108, 54), (255, 164, 22), (255, 216, 44), (255, 246, 170), (255, 255, 240)]       # ... to its color: dark red, the red rim, orange, yellow, the white core
RAMP_A = [0.0, 0.55, 0.74, 0.78, 0.80, 0.88, 0.95, 0.98]           # ... to its opacity
SMOKE_ALPHA = (0.72, 0.85)                                          # the smoke's opacity: its thin parts to its thick parts


def box(a, r, axis):
    """a box average of radius r along an axis, the border continued outward"""
    if r <= 0:
        return a
    pw = [(0, 0)] * a.ndim
    pw[axis] = (r + 1, r)
    c = np.cumsum(np.pad(a, pw, mode='edge'), axis=axis, dtype=np.float64)
    n = a.shape[axis]
    return (np.take(c, np.arange(2 * r + 1, 2 * r + 1 + n), axis=axis) - np.take(c, np.arange(n), axis=axis)).astype(F) / (2 * r + 1)


def gblur(a, sx):
    """a Gaussian blur (three box passes), sigma in master samples"""
    a = a.astype(F)
    r = int(round((math.sqrt(12 * sx * sx / 3 + 1) - 1) / 2))
    for axis in (0, 1):
        for _ in range(3):
            a = box(a, r, axis)
    return a


def up(a):
    return np.repeat(np.repeat(a, SS, 0), SS, 1)


def pad(a):
    return np.pad(a, [(PAD, PAD), (PAD, PAD)] + [(0, 0)] * (a.ndim - 2))


def field(m1, sig):
    """a 1x mask as a smooth field of the master's size (its 0.5 level is the smooth contour)"""
    return gblur(up(pad(m1.astype(F))), sig * SS)


def mblur(rgb1, m1, sig):
    """the original's colors inside a 1x mask, blurred without bleeding in from outside it, and defined everywhere"""
    m = up(pad(m1.astype(F)))
    c = up(pad(rgb1.astype(F))) * m[..., None]
    out = None
    ok = None
    for k in (1, 2.5, 6, 20):
        den = gblur(m, sig * SS * k)
        num = np.stack([gblur(c[..., i], sig * SS * k) for i in range(3)], -1)
        cur = num / np.maximum(den, 1e-6)[..., None]
        if out is None:
            out = cur
            ok = den > 0.03
        else:
            out = np.where(ok[..., None], out, cur)
            ok = ok | (den > 0.03)
    return out


def flood(allowed, seed):
    """the parts of `allowed` that touch `seed` (4-connected)"""
    m = seed & allowed
    while True:
        n = m.copy()
        n[1:] |= m[:-1]
        n[:-1] |= m[1:]
        n[:, 1:] |= m[:, :-1]
        n[:, :-1] |= m[:, 1:]
        n &= allowed
        if (n == m).all():
            return m
        m = n


def noise(shape, cell, seed):
    """smooth value noise from -1 to 1, its cells `cell` master samples wide"""
    r = np.random.default_rng(seed)
    h, w = shape
    gh, gw = max(2, int(h / cell) + 2), max(2, int(w / cell) + 2)
    im = Image.fromarray(r.random((gh, gw)).astype(F)).resize((int(gw * cell), int(gh * cell)), Image.BICUBIC)
    return np.asarray(im)[:h, :w] * 2 - 1


def sstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def down(rgb, a, k):
    """the master (color and opacity) averaged to the scale k, the color weighted by the opacity, the margin removed"""
    b = SS // k
    H, W = a.shape
    h, w = H // b, W // b
    aa = a.reshape(h, b, w, b).mean((1, 3))
    cc = (rgb * a[..., None]).reshape(h, b, w, b, 3).mean((1, 3)) / np.maximum(aa, 1e-6)[..., None]
    o = np.dstack([np.clip(np.round(cc), 0, 255), np.round(aa * 255)]).astype(np.uint8)
    p = PAD * k
    return o[p:h - p, p:w - p]


def classify(number):
    """the original's frame as classes: its colors (RGB), its opaque pixels, the smoke's pixels, and the heat 0 to 6 of every pixel (6 the white core)"""
    a = np.asarray(sprite(number)).astype(int)
    al = a[..., 3] > 0
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    lev = np.zeros(al.shape, int)
    white = al & (r > 240) & (g > 240) & (b > 240)
    lilac = al & (b > g + 8) & (b > 200) & (g > 150)
    smoke = al & (np.minimum(np.minimum(r, g), b) >= 100) & ~white & ~lilac
    fire = al & ~smoke
    lev[fire] = 2
    lev[fire & (r < 200) & (g < 60)] = 1
    lev[fire & (b >= 100) & (g < 130)] = 3
    lev[fire & (g >= 90) & (b < 70)] = 4
    lev[fire & (r > 240) & (g > 190) & (b < 70)] = 5
    lev[white | lilac] = 6
    lev[fire & (r < 170) & (g >= 90) & (b < 70)] = 3
    cool = np.pad((lev > 0) & (lev < 6), 2)
    h, w = al.shape
    near = np.zeros_like(al)                                        # within two pixels of a cooler flame
    for dy in range(5):
        for dx in range(5):
            near |= cool[dy:dy + h, dx:dx + w]
    w6 = lev == 6
    stray = w6 & ~flood(w6, w6 & near)                              # a white fleck out in the smoke, touching no flame, is smoke, not fire (the white core stays)
    lev[stray] = 0
    smoke = smoke | stray
    return a[..., :3], al, smoke, lev


def mix(a, b, t):
    a = np.asarray(a, F)
    b = np.asarray(b, F)
    if np.ndim(t) == 2 and (a.ndim in (1, 3) or b.ndim in (1, 3)):
        t = t[..., None]
    return a * (1 - t) + b * t


def ramp(t, ys):
    ys = np.asarray(ys, F)
    if ys.ndim == 2:
        return np.stack([np.interp(t, RAMP_T, ys[:, c]) for c in range(ys.shape[1])], -1).astype(F)
    return np.interp(t, RAMP_T, ys).astype(F)


def fbm(shape, cell, seed, octaves=4):
    """smooth noise of several sizes, about -1 to 1"""
    out = np.zeros(shape, F)
    amp = 1.0
    tot = 0.0
    for o in range(octaves):
        out += amp * noise(shape, max(2.0, cell * SS / 2 ** o), seed * 31 + o)
        tot += amp
        amp *= 0.55
    return out / tot


def paint(number):
    """the frame on the master: (color, opacity) of the whole and (color, opacity) of the fire and its bloom alone"""
    rgb1, al1, smoke1, lev1 = classify(number)
    H, W = (al1.shape[0] + 2 * PAD) * SS, (al1.shape[1] + 2 * PAD) * SS
    shape = (H, W)
    # the smoke: a volume
    body = field(al1, 0.75)
    wide = field(al1, 1.6)
    billow = fbm(shape, 5.0, number, 3)
    fine = fbm(shape, 2.0, number + 50, 2)
    thick = sstep(0.10, 0.92, body * (1 + 0.42 * billow) + 0.14 * wide)                 # how much smoke is in the way: full in the middle of a puff, thinning to nothing, its rim broken up by the billows
    hgt = gblur(thick * (1 + 0.60 * billow + 0.10 * fine), 1.1 * SS)
    gy, gx = np.gradient(hgt)
    slope = (gx * LIGHT[0] + gy * LIGHT[1]) * SS * 1.6                                   # lit where the puff's side faces the upper left
    shade = np.clip(0.90 + 0.30 * np.tanh(-slope * 1.5) + 0.10 * (hgt - 0.5), 0.60, 1.14)
    tone = mblur(np.repeat((rgb1 @ np.array([0.3, 0.6, 0.1]))[..., None], 3, 2), smoke1, 0.9)[..., 0] if smoke1.any() else np.full(shape, 190.0, F)       # only how light the original's smoke is there (its three tones), not its color
    k = sstep(150, 212, tone)
    base = mix(np.array((150, 140, 134), F), np.array((226, 216, 204), F), k)
    scol = base * shade[..., None]
    scol = mix(scol, np.array((112, 104, 104), F), np.clip(0.55 - shade, 0, 1) * 0.9)  # the shaded side falls a little cool and dark
    A = thick * (SMOKE_ALPHA[0] + (SMOKE_ALPHA[1] - SMOKE_ALPHA[0]) * k) * np.clip(0.90 + 0.14 * fine, 0, 1)
    # the fire: one heat field
    FA = np.zeros(shape, F)
    fcol = np.zeros(shape + (3,), F)
    col = scol
    LA = FA
    if (lev1 > 0).any():
        heat1 = np.array([0.0, 0.20, 0.36, 0.52, 0.68, 0.84, 1.0], F)[lev1]
        soft = gblur(up(pad(heat1)), 0.62 * SS)
        core = gblur(up(pad(heat1)), 0.30 * SS)
        T = np.clip(0.62 * soft + 0.40 * core, 0, 1)
        T = np.clip(T ** 1.25 + 0.10 * fbm(shape, 1.6, number + 90, 3) * sstep(0.04, 0.3, T), 0, 1)      # smooth, with the fine curl of flame
        fcol = ramp(T, RAMP_C)
        FA = ramp(T, RAMP_A) * sstep(0.015, 0.16, T)
        warm = np.clip(gblur(FA, 2.6 * SS) * 1.8, 0, 1)
        scol = mix(scol, scol * np.array((1.10, 0.90, 0.66), F) + np.array((26, 8, 0), F), warm * 0.75)   # the smoke near the fire is lit warm by it
        col = scol * (1 - FA[..., None]) + fcol * FA[..., None]
        A = A * (1 - FA) + FA
        bloom = np.clip(gblur(FA * T, 1.5 * SS) * 1.5, 0, 1) * 0.55                      # the light picture: the fire, and a bloom round its hot parts
        LA = np.clip(FA + bloom * (1 - FA), 0, 1)
        fcol = mix(np.array((255, 176, 70), F) * np.ones(shape + (3,), F), fcol, np.clip(FA / np.maximum(LA, 1e-4), 0, 1))
    ys, xs = np.mgrid[0:H, 0:W]
    edge = np.minimum(np.minimum(xs - PAD * SS, (W - PAD * SS - 1) - xs), np.minimum(ys - PAD * SS, (H - PAD * SS - 1) - ys)) / SS
    fade = np.clip(edge / 1.5, 0, 1).astype(F)                                           # nothing is cut by the sprite's border: the last pixel and a half fades out
    return np.clip(col, 0, 255), A * fade, fcol, LA * fade


def explosion_pictures(number):
    """({scale: RGBA array} of the whole frame, the same of its light) of the explosion's frame `number` (the original's sprite 306 to 315)"""
    col, A, fcol, FA = paint(number)
    return {scale: down(col, A, scale) for scale in (1, 2, 3)}, {scale: down(fcol, FA, scale) for scale in (1, 2, 3)}
