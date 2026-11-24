"""The fields of the dense mud: a standalone port of the approved generator, for the C++ port to be held to (README.md, `mud_reference.py`).

The deep middle of a big mud field looks different from its banks: darker and wetter, with ripples, brown and gray color patches, and a dull sheen. Every look is a function of the world pixel and of
the DEPTH there, the distance in tiles to the nearest cell that is not plain mud (a bank has depth 0, a point four tiles from every bank has depth 4); nothing else is read, so a look crosses a tile
border without a seam, repeats nowhere (value noise on a hashed lattice, each octave on a turned lattice) and is the same on every client. Each look's weight is exactly 0 until its first depth, so a
small mud area and the banks keep the approved pixels. It is drawing only: no rule of the game reads any of it.

`mask` is a boolean array, one row of cells for each row of the map, True for a cell of plain mud (any of its bubbling tiles); the border of the map is not a bank. Positions are world pixels (a cell is
32 x 32). Only numpy is needed.
"""
import numpy as np

TILE = 32.0
CAP = 7.0                                                   # depths above this are the same
STEP = 4                                                    # the depth grid's spacing in world pixels
DEPTH_SCALE = 1.0                                           # 1.0 = as approved; a smaller number makes every depth count for more (the looks begin sooner)
WANDER = 0.45                                               # the depth lines wander by this many tiles at most, so that no iso-line follows the cell grid


def smooth(t):
    t = np.clip(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def ramp(v, a, b):
    """0 up to `a`, 1 from `b`, a smooth step between"""
    return smooth((v - a) / (b - a))


# ------------------------------------------------------------------------------------------------ depth

def depth_grid(mask):
    """the depth in tiles on a grid of STEP world pixels (the points are the grid cells' centers): [rows * 32 / STEP, columns * 32 / STEP]"""
    m = np.asarray(mask, bool)
    H, W = m.shape
    gh, gw = H * 32 // STEP, W * 32 // STEP
    D = np.full((gh, gw), CAP * TILE)
    gx = (np.arange(gw) + 0.5) * STEP
    gy = (np.arange(gh) + 0.5) * STEP
    R = int((CAP + 1) * TILE)
    pad = np.pad(m, 1, constant_values=False)
    for cy in range(H):
        for cx in range(W):
            if m[cy, cx]:
                continue
            if not pad[cy:cy + 3, cx:cx + 3].any():                       # not beside any mud cell
                continue
            x0, x1, y0, y1 = cx * 32, cx * 32 + 32, cy * 32, cy * 32 + 32
            ia, ib = max(0, int((x0 - R) // STEP)), min(gw, int((x1 + R) // STEP) + 1)
            ja, jb = max(0, int((y0 - R) // STEP)), min(gh, int((y1 + R) // STEP) + 1)
            dx = np.maximum(np.maximum(x0 - gx[ia:ib], 0), gx[ia:ib] - x1)
            dy = np.maximum(np.maximum(y0 - gy[ja:jb], 0), gy[ja:jb] - y1)
            d = np.hypot(dx[None, :], dy[:, None])
            D[ja:jb, ia:ib] = np.minimum(D[ja:jb, ia:ib], d)
    return D / TILE


def depth_at(D, X, Y):
    """the depth in tiles at world coordinates (arrays), bilinear on the grid `D` of depth_grid (a point outside the grid takes the nearest grid point's)"""
    gh, gw = D.shape
    u = np.clip(X / STEP - 0.5, 0, gw - 1.001)
    v = np.clip(Y / STEP - 0.5, 0, gh - 1.001)
    i, j = np.floor(u).astype(int), np.floor(v).astype(int)
    fu, fv = u - i, v - j
    return (D[j, i] * (1 - fu) * (1 - fv) + D[j, i + 1] * fu * (1 - fv) + D[j + 1, i] * (1 - fu) * fv + D[j + 1, i + 1] * fu * fv) / DEPTH_SCALE


def cell_depth(D, cx, cy):
    """the depth in tiles at the center of a cell"""
    return float(depth_at(D, np.array([cx * 32 + 16.0]), np.array([cy * 32 + 16.0]))[0])


# ------------------------------------------------------------------------------------------------ noise

def _hash(ix, iy, seed):
    """a lattice point's value, 0 to 1, from integers (a 32-bit hash; the products are taken modulo 2^32 whatever the sign of ix and iy)"""
    n = (np.asarray(ix).astype(np.int64) * 374761393 + np.asarray(iy).astype(np.int64) * 668265263 + int(seed) * 2246822519) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    n = n ^ (n >> 16)
    return (n & 0xFFFFFF) / float(0x1000000)


def vnoise(x, y, seed):
    """smooth value noise in lattice units, 0 to 1 (a quintic blend of the four lattice points round (x, y))"""
    ix, iy = np.floor(x), np.floor(y)
    fx, fy = x - ix, y - iy
    u = fx * fx * fx * (fx * (fx * 6 - 15) + 10)
    v = fy * fy * fy * (fy * (fy * 6 - 15) + 10)
    a, b = _hash(ix, iy, seed), _hash(ix + 1, iy, seed)
    c, d = _hash(ix, iy + 1, seed), _hash(ix + 1, iy + 1, seed)
    return (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v


def fbm(x, y, wavelength, seed, octaves=3, gain=0.5, stretch=(1.0, 1.0), angle=0.0):
    """Fractal value noise, roughly 0 to 1 around 0.5 (std about .12); `wavelength` in world pixels of the first octave; `stretch` lengthens the features along `angle` (degrees)."""
    c, s = np.cos(np.radians(angle)), np.sin(np.radians(angle))
    u0, w0 = (x * c + y * s) / stretch[0], (-x * s + y * c) / stretch[1]
    tot, amp, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        ca, sa = np.cos(np.radians(33.0 * o + 17.0)), np.sin(np.radians(33.0 * o + 17.0))        # every octave on a turned lattice: no feature lines up with the tile grid
        u, w = u0 * ca + w0 * sa, -u0 * sa + w0 * ca
        tot = tot + amp * vnoise(u / wavelength * 2 ** o + 17.3 * o, w / wavelength * 2 ** o + 5.1 * o, seed + 31 * o)
        norm += amp
        amp *= gain
    return tot / norm


def stretch01(v, k=3.0):
    """spread a noise value's usual range (about .25 to .75) over 0 to 1"""
    return np.clip((v - 0.5) * k + 0.5, 0.0, 1.0)


# ------------------------------------------------------------------------------------------------ the looks

LIGHT_XY = np.array([-0.55, -0.65]) / np.hypot(-0.55, -0.65)             # the light, from the upper left (as in the drawing)

P = dict(
    # look 1: wetter, deeper middle
    wet_d=(1.8, 5.0), wet_dark=(0.200, 0.220, 0.230),        # the mud's colors times (1 - dark) at full depth (a little less on red than on blue: damp clay is warm, not cold)
    wet_var=0.10,                                              # how much the darkness varies across the middle (slow noise), 0..1
    sheen_d=(3.0, 6.0), sheen_add=(8.0, 8.5, 10.0), sheen_wl=260.0, sheen_cut=(0.45, 0.95),
    # look 2: ripples and swirls (a soft ridge: a lit flank and a shaded flank, about six pixels across)
    rip_d=(1.8, 5.0), rip_amp=4.6, rip_sigma=2.2, rip_gap=30.0, rip_wl=300.0, rip_cover=(0.30, 0.46), rip_break=(0.36, 0.52),
    # look 4: broad color patches
    patch_d=(1.5, 5.0), patch_wl=320.0, patch_brown=(6.0, 1.5, -6.0), patch_gray=(5.0, 5.0, 5.5), patch_cut=(0.0, 0.65),
)


def sample_grid(crop, scale, ss):
    """the world coordinates (X, Y) of the samples of a crop (x, y, w, h in world pixels) drawn at `scale` with ss x ss samples for each output pixel, and the samples for each world pixel"""
    x0, y0, w, h = crop
    n = scale * ss
    X = x0 + (np.arange(w * n) + 0.5) / n
    Y = y0 + (np.arange(h * n) + 0.5) / n
    return np.meshgrid(X, Y), n


def reduce_samples(a, n):
    """box-average a supersampled array down to the output pixels (n x n samples each)"""
    h, w = a.shape[0] // n, a.shape[1] // n
    return a.reshape(h, n, w, n, *a.shape[2:]).mean(axis=(1, 3))


def warped_phi(X, Y, p=None):
    """the ripple field: a smooth field whose contour lines are the flow lines (they close into swirls round its hills and hollows), on the world coordinates warped by 70 pixels"""
    p = dict(P, **(p or {}))
    warp = 70.0
    xs = X + warp * (fbm(X, Y, 210.0, 55, 2) - 0.5) * 2
    ys = Y + warp * (fbm(X, Y, 210.0, 56, 2) - 0.5) * 2
    return fbm(xs, ys, p['rip_wl'], 21, 3, gain=0.55)


def looks_at(D, X, Y, which=('wet', 'ripple', 'patch'), p=None, n=1):
    """The looks at world coordinates X, Y (arrays of one shape) over the depth grid `D`, `n` samples for each world pixel (the spacing of the ripple's gradient is 1 / n; the ripple needs a grid of
    at least two samples each way): 'mult' (.., 3) the colors' factor, 'add' (.., 3) the levels added to every mud pixel, 'rip' (..) the levels added to the plain mud's pixels only (not to a bubble)
    and 'depth' the depth in tiles that the looks used, with its wander. which: any of 'wet' (look 1: darker and wetter, a dull sheen), 'ripple' (look 2), 'patch' (look 4)."""
    p = dict(P, **(p or {}))
    d = depth_at(D, X, Y)
    d = d + WANDER * (fbm(X, Y, 190.0, 91, 2) - 0.5) * 2                       # the depth lines wander a little, so no iso-line follows the cell grid
    mult = np.ones(X.shape + (3,))
    add = np.zeros(X.shape + (3,))
    rip = np.zeros(X.shape)
    if 'wet' in which:
        w = ramp(d, *p['wet_d'])
        var = 1.0 - p['wet_var'] * (1.0 - stretch01(fbm(X, Y, 260.0, 7, 3)))
        for k in range(3):
            mult[..., k] = 1.0 - p['wet_dark'][k] * w * var
        sh = ramp(stretch01(fbm(X, Y, p['sheen_wl'], 13, 3, stretch=(1.7, 1.0), angle=-24.0), 3.0), *p['sheen_cut']) * ramp(d, *p['sheen_d'])
        for k in range(3):
            add[..., k] += p['sheen_add'][k] * sh
    if 'patch' in which:
        w = ramp(d, *p['patch_d'])
        q = stretch01(fbm(X, Y, p['patch_wl'], 41, 2), 2.4) * 2 - 1                # -1 .. 1
        br, gr = ramp(q, *p['patch_cut']), ramp(-q, *p['patch_cut'])
        for k in range(3):
            add[..., k] += w * (br * p['patch_brown'][k] + gr * p['patch_gray'][k])
    if 'ripple' in which:
        w = ramp(d, *p['rip_d'])
        phi = warped_phi(X, Y, p)
        gy, gx = np.gradient(phi, 1.0 / n)
        g = np.hypot(gx, gy) + 1e-9
        # the signed distance in world pixels to the nearest contour of phi, the contours at multiples of a level step chosen for the gap between lines
        step = p['rip_gap'] * np.median(g)
        t = phi / step
        sd = (t - np.floor(t) - 0.5) * step / g                                      # world pixels from the line midway between two levels... lines are at the half
        nrm = (gx * LIGHT_XY[0] + gy * LIGHT_XY[1]) / g                              # the side of the line that faces the light is lit
        side = sd * np.sign(nrm)                                                     # negative on the lit side
        x = side / p['rip_sigma']
        line = -2.3 * x * np.exp(-x * x)                                             # the slope of a soft ridge, lit one side and shaded the other (+-1 at most)
        cover = ramp(fbm(X, Y, 340.0, 77, 2), *p['rip_cover'])
        broken = ramp(fbm(X, Y, 110.0, 63, 2), *p['rip_break'])                     # a line fades in and out along its length: marks, not contour lines
        rip = p['rip_amp'] * line * cover * broken * w
    return dict(mult=mult, add=add, rip=rip, depth=d)


def layers(mask, scale, crop, which=('wet', 'ripple', 'patch'), ss=3, p=None):
    """The looks over a crop (x, y, w, h in world pixels) of the map drawn at an output scale, each output pixel the mean of ss x ss samples: {'mult': (h, w, 3), 'add': (h, w, 3), 'rip': (h, w), 'depth': (h, w)}
    (looks_at has the meanings)."""
    (X, Y), n = sample_grid(crop, scale, ss)
    out = looks_at(depth_grid(mask), X, Y, which, p, n)
    return {k: reduce_samples(v, ss) for k, v in out.items()}
