"""The bomb's explosion (sprites 306 to 315, the ten frames of the blast): the approved version, drawn as light (README.md).

The original's own shapes, frames, sizes and offsets are kept; what changes is how the picture sits on the world, as with the fire wall (fire.py): no outline, see-through.
  - The fireball has six hard color steps from red to the white core and an opacity by heat (0.72 at the red rim to 0.98 in the white core), a soft edge that fades out, no outline.
  - The smoke is a matte, see-through veil in the original's three tones: its ragged cloud kept, its opacity 0.72 to 0.85 by tone, a little uneven inside, fading out at the edge instead of
    ending in a line. The ground, the water and the ant show through it where it is thin.
Each frame is two pictures: the whole (smoke and fire), packed under its sprite's number, and the light (the fire alone, over nothing), packed under 30000 plus it, for a pass that adds the fire
to the ground as light. 1x is the original's pixels, each with its class's color and opacity.

The drawing is on a master of 12 samples for each world pixel (and a margin of 4 pixels, cut off again), in single precision as it was approved; 3x and 2x are averages of it. The smoke's
unevenness is a value noise (Pillow's bicubic enlargement of a seeded random grid), so it is as reproducible as Pillow's resize is.
"""
import math

import numpy as np
from PIL import Image

from catalog import sprite

SS = 12                                                             # master samples for each world pixel
PAD = 4                                                             # world pixels of margin round a sprite while it is drawn
F = np.float32
HEAT = [(214, 34, 40), (250, 62, 66), (255, 112, 60), (255, 168, 12), (255, 222, 36), (255, 255, 238)]     # the six steps of the fireball, coolest red to the white core
HEAT_ALPHA = [0.0, 0.72, 0.78, 0.74, 0.80, 0.90, 0.98]             # their opacity (index 0: none)
SMOKE_ALPHA = (0.72, 0.85)                                          # the smoke's opacity: its darkest tone to its lightest


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


def paint(number):
    """the frame on the master: (color, opacity) of the whole, (color, opacity) of the fire alone, and the classes"""
    rgb1, al1, smoke1, lev1 = classify(number)
    fire1 = lev1 > 0
    H, W = (al1.shape[0] + 2 * PAD) * SS, (al1.shape[1] + 2 * PAD) * SS
    # the smoke: one veil under the fire, the original's ragged shape, fading at its edge, a little uneven inside
    sa = sstep(0.22, 0.66, field(al1, 0.55))
    lum1 = rgb1 @ np.array([0.3, 0.6, 0.1])
    tone = mblur(np.repeat(lum1[..., None], 3, 2), smoke1, 0.7)[..., 0] if smoke1.any() else np.full((H, W), 180.0, F)
    scol = mblur(rgb1, smoke1, 0.6) if smoke1.any() else np.zeros((H, W, 3), F) + 200
    A = sa * (SMOKE_ALPHA[0] + (SMOKE_ALPHA[1] - SMOKE_ALPHA[0]) * sstep(160, 205, tone)) * (0.9 + 0.1 * noise((H, W), 2.5 * SS, number))
    col = scol.copy()
    FA = np.zeros((H, W), F)
    fcol = np.zeros((H, W, 3), F)
    if fire1.any():
        K = np.zeros((H, W), int)
        prev = None
        for k in range(1, 7):                                       # the heat steps lie one inside the next
            reg = field(lev1 >= k, 0.45) > 0.5
            if prev is not None:
                reg &= prev
            K[reg] = k
            prev = reg
        fade = sstep(0.20, 0.62, field(fire1, 0.42))
        FA = gblur(np.array(HEAT_ALPHA, F)[K], 0.14 * SS) * fade
        fcol = np.array(HEAT, F)[np.clip(K - 1, 0, 5)]
        fcol = np.where((K > 0)[..., None], fcol, np.array(HEAT[0], F))
        col = col * (1 - FA[..., None]) + fcol * FA[..., None]
        A = A * (1 - FA) + FA
    return col, A, fcol, FA, (rgb1, al1, smoke1, lev1)


def pixels_1x(rgb1, smoke1, lev1, light):
    """1x: the original's pixels, each with its class's color and opacity (the smoke keeps the original's color); the light is the fire alone"""
    h, w = smoke1.shape
    out = np.zeros((h, w, 4), np.uint8)
    if not light:
        lum = rgb1 @ np.array([0.3, 0.6, 0.1])
        sa = SMOKE_ALPHA[0] + (SMOKE_ALPHA[1] - SMOKE_ALPHA[0]) * np.clip((lum - 160) / 45.0, 0, 1)
        out[smoke1, :3] = rgb1[smoke1]
        out[smoke1, 3] = np.round(sa[smoke1] * 255)
    for k in range(1, 7):
        m = lev1 == k
        out[m, :3] = HEAT[k - 1]
        out[m, 3] = int(round(HEAT_ALPHA[k] * 255))
    return out


def explosion_pictures(number):
    """({scale: RGBA array} of the whole frame, the same of its light) of the explosion's frame `number` (the original's sprite 306 to 315)"""
    col, A, fcol, FA, (rgb1, al1, smoke1, lev1) = paint(number)
    whole, light = {}, {}
    for scale in (1, 2, 3):
        whole[scale] = pixels_1x(rgb1, smoke1, lev1, False) if scale == 1 else down(col, A, scale)
        light[scale] = pixels_1x(rgb1, smoke1, lev1, True) if scale == 1 else down(fcol, FA, scale)
    return whole, light
