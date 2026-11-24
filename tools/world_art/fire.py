"""The fire wall (sprites 316 to 320, the five frames of the wall, packed under the numbers 50316 to 50320): the approved fire as light (README.md).

The original's fire is read as a map of levels (its eight fire colors, hottest last, and its three browns, the coals), and drawn again as light: the same silhouette, tongues, flecks and frames,
no outline at any scale, and see-through by heat. The opacity goes by the level (the white core nearly opaque, the cool red-orange rim mostly transparent), each level's region is a hard step of
color, the edge fades out over about a pixel instead of ending in a line, the loose flecks are faint sparks, and the coals lie under the flames as one ash bed in the original's three browns. The
game draws it as light (blended and then added to what is under it, with a warm glow on the ground): the picture holds the see-through-ness itself, so its alpha must stay as it is.

2x and 3x: each level's region (every pixel at least that hot) is enlarged by cubic convolution, which keeps a one-pixel line one pixel wide and turns a staircase into a diagonal, rounded by a very
small blur and cut at its half, on a master of 12 samples for each world pixel; 3x and 2x are averages of it. 1x is the original's own pixels, each with its level's color and opacity.
"""
import numpy as np

from canvas import Canvas
from catalog import sprite
from tilekit import MS, big, blur, smoothstep, up_cubic

# the original's eight fire colors, coolest first, and its three browns (the coals), darkest first
FIRE = [(219, 75, 19), (235, 107, 19), (231, 147, 11), (239, 175, 7), (255, 211, 0), (244, 239, 95), (255, 255, 97), (255, 255, 255)]
BROWN = [(75, 55, 31), (111, 71, 43), (127, 87, 19)]
# the light's colors: the original's eight, the cool ones a little more saturated (a see-through red-orange over a grey ground loses chroma, so it starts with more)
LIGHT = [(238, 62, 8), (250, 98, 6), (255, 140, 0), (255, 178, 0), (255, 214, 0), (246, 240, 98), (255, 255, 104), (255, 255, 255)]
ALPHA = [0.0, 0.30, 0.45, 0.58, 0.70, 0.80, 0.88, 0.93, 0.98]      # opacity of a level (1 = the coolest red-orange, 8 = white)
COAL_ALPHA = 0.58                                                   # opacity of the ash bed
SHAPE = dict(thr=0.5, thr_halo=0.40, soft=0.22, spark_r=0.52, spark_boost=1.6)     # where a region's edge lies in the enlarged field (the body, the thin halo), the rounding blur, a fleck's half-size, and a fleck's opacity over its level's
FADE = dict(sigma=0.38, lo=0.20, hi=0.62)                           # the edge: a blurred silhouette read through a smooth ramp
SOFT_ALPHA = 0.12                                                   # the blur that melts one level's opacity into the next (world pixels)
SPARK_ALPHA_BLUR = 0.22                                             # the blur of a loose fleck's opacity (world pixels)


def levels(number):
    """the original's frame as a map: 0 empty, 1 to 8 the fire level, -1 to -3 the coal"""
    a = np.array(sprite(number))
    h, w = a.shape[:2]
    lev = np.zeros((h, w), int)
    for y in range(h):
        for x in range(w):
            if a[y, x, 3] == 0:
                continue
            c = tuple(int(v) for v in a[y, x, :3])
            if c in FIRE:
                lev[y, x] = FIRE.index(c) + 1
            elif c in BROWN:
                lev[y, x] = -(BROWN.index(c) + 1)
            else:
                raise SystemExit('sprite %d has a pixel of a color that is neither a fire color nor a coal: %s' % (number, c))
    return lev


def spark(shape, cx, cy, r):
    """a loose fleck: a small square with rounded corners (the original's pixel, with no corners to stair-step), as a boolean mask of the master"""
    h, w = shape
    X = (np.arange(w) + 0.5) / MS
    Y = (np.arange(h) + 0.5) / MS
    return (np.abs(X[None, :] - cx) ** 4 + np.abs(Y[:, None] - cy) ** 4) <= r ** 4


def level_map(lev):
    """per master sample, 0 nothing or 1 to 8 the original's level: the body with its torn bits (K), and apart from it the loose flecks (KS)"""
    h, w = lev.shape
    shape = (h * MS, w * MS)
    fire = lev > 0
    pad = np.pad(fire, 1)
    full = np.ones_like(fire)                                       # the pixels with a full 3 x 3 block of fire around them
    for dy in (0, 1, 2):
        for dx in (0, 1, 2):
            full &= pad[dy:dy + h, dx:dx + w]
    p3 = np.pad(full, 1)
    main = np.zeros_like(fire)                                      # the solid body: every fire pixel in some full 3 x 3 block
    for dy in (0, 1, 2):
        for dx in (0, 1, 2):
            main |= p3[dy:dy + h, dx:dx + w]
    main &= fire
    others = fire & ~main
    padf = np.pad(fire, 1)
    n4 = padf[0:h, 1:w + 1].astype(int) + padf[2:h + 2, 1:w + 1] + padf[1:h + 1, 0:w] + padf[1:h + 1, 2:w + 2]
    sparks = others & (n4 == 0)                                     # a fire pixel with no fire neighbor
    K = np.zeros(shape, int)
    KS = np.zeros(shape, int)

    def levels_of(sel, thr):
        prev = None
        out = np.zeros(shape, int)
        for k in range(1, 9):
            reg = blur(up_cubic((lev >= k) & sel), SHAPE['soft']) >= thr
            if prev is not None:
                reg &= prev                                         # the regions lie one inside the next
            out[reg] = k
            prev = reg
        return out
    K = np.maximum(K, levels_of(main, SHAPE['thr']))
    K = np.maximum(K, levels_of(others & ~sparks, SHAPE['thr_halo']))
    for k in range(1, 9):
        for y, x in zip(*np.nonzero(sparks & (lev == k))):
            m = spark(shape, x + 0.5, y + 0.5, SHAPE['spark_r'])
            KS[m] = np.maximum(KS[m], k)
    return K, KS


def lay_coals(m, lev):
    """the coals as one closed ash bed in the original's three browns (the darkest bed first)"""
    coal = lev < 0
    reg = (up_cubic(coal) >= SHAPE['thr']) | (blur(big(coal), 0.7) >= 0.40)
    reg2 = (blur(big(coal & (lev <= -2)), 0.5) >= 0.38) & reg
    reg3 = (up_cubic(coal & (lev == -3)) >= 0.42) & reg2
    for region, color in zip((reg, reg2, reg3), BROWN):
        m.over(color, region.astype(float) * COAL_ALPHA)


def draw(lev):
    """the fire as light on a canvas of 12 samples for each world pixel"""
    h, w = lev.shape
    Kb, Ks = level_map(lev)
    alpha_tab = np.array(ALPHA)
    cover = blur((Kb > 0).astype(float), FADE['sigma'])
    fade = smoothstep((cover - FADE['lo']) / (FADE['hi'] - FADE['lo']))          # where the shape ends the see-through-ness fades to nothing
    A = blur(alpha_tab[Kb], SOFT_ALPHA) * fade
    As = blur(alpha_tab[Ks], SPARK_ALPHA_BLUR) * SHAPE['spark_boost']            # a loose fleck: its own faint spark
    K = np.where(As > A, Ks, Kb)
    A = np.maximum(A, As)
    col = np.array(LIGHT)[np.clip(K - 1, 0, 7)].astype(float)                   # hard-stepped; where the soft opacity reaches past the last region it keeps the nearest region's color
    m = Canvas(w, h, MS)
    lay_coals(m, lev)                                                            # the ash bed darkens the ground a little; the see-through flames lie over it
    m.rgb = m.rgb * (1 - A[..., None]) + col * A[..., None]
    m.a = m.a * (1 - A) + A
    return m


def pixels_1x(lev):
    """1x: the original's pixels, each with its level's color and opacity (the coals with the ash bed's)"""
    h, w = lev.shape
    out = np.zeros((h, w, 4), np.uint8)
    for y in range(h):
        for x in range(w):
            v = lev[y, x]
            if v > 0:
                c = LIGHT[v - 1]
                out[y, x] = (c[0], c[1], c[2], int(round(ALPHA[v] * 255)))
            elif v < 0:
                c = BROWN[-v - 1]
                out[y, x] = (c[0], c[1], c[2], int(round(COAL_ALPHA * 255)))
    return out


def fire_pictures(number):
    """{scale: RGBA array} of the fire wall's frame `number` (the original's sprite 316 to 320)"""
    lev = levels(number)
    m = draw(lev)
    return {1: pixels_1x(lev), 2: m.picture(2), 3: m.picture(3)}
