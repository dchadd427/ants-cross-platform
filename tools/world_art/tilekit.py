"""The drawing kit of the world art: a tile is drawn once on a master of MS x MS samples for each world pixel, with hard edges and flat colors only (nothing is anti-aliased there), and the three
scales are made from it: 3x and 2x are box averages of the master, 1x is the color that most of each pixel's samples have, so that every 1x pixel is a color of the palette (pixel art drawn on purpose,
never a filtered copy). The shading is by one rule: one light from the upper left and above, a few hard steps (a lit top, a body, a shaded side) and a small cast shadow; never a gradient or noise.

What is taken from the original's tile is where things are and how big (the places of the flecks, the streaks and the bubbles, the shapes of the regions of an edge tile and of its outline band) and its
palette; the drawing is new (README.md).
"""
import numpy as np

from catalog import sprite

MS = 12                    # samples of the master for each world pixel
T = 32                     # a ground tile is 32 x 32 world pixels
N = T * MS


# ------------------------------------------------------------------------------------------------ fields on the master

def smoothstep(x):
    x = np.clip(x, 0, 1)
    return x * x * (3 - 2 * x)


def blur(a, sigma):
    """Gaussian blur, sigma in world pixels, of a master array (zero outside)"""
    pad = int(4 * sigma * MS) + 2
    b = np.pad(a, pad, mode='constant')
    ky = np.fft.fftfreq(b.shape[0])[:, None]
    kx = np.fft.fftfreq(b.shape[1])[None, :]
    g = np.exp(-2 * (np.pi * sigma * MS) ** 2 * (kx ** 2 + ky ** 2))
    return np.real(np.fft.ifft2(np.fft.fft2(b) * g))[pad:-pad, pad:-pad]


def blur_edge(a, sigma):
    """Gaussian blur of a master array with its border continued outward"""
    pad = int(4 * sigma * MS) + 2
    b = np.pad(a, pad, mode='edge')
    ky = np.fft.fftfreq(b.shape[0])[:, None]
    kx = np.fft.fftfreq(b.shape[1])[None, :]
    g = np.exp(-2 * (np.pi * sigma * MS) ** 2 * (kx ** 2 + ky ** 2))
    return np.real(np.fft.ifft2(np.fft.fft2(b) * g))[pad:-pad, pad:-pad]


def big(m):
    """a 1x array repeated MS x MS times: the master of it, pixel for pixel"""
    return np.kron(np.asarray(m).astype(float), np.ones((MS, MS)))


def _cubic(t, a=-0.5):
    t = np.abs(t)
    return np.where(t <= 1, (a + 2) * t ** 3 - (a + 3) * t ** 2 + 1, np.where(t < 2, a * t ** 3 - 5 * a * t ** 2 + 8 * a * t - 4 * a, 0.0))


def weights_at(x, n, pad=3):
    """cubic-convolution weights that read an n-sample line (padded by `pad` samples each side) at the source coordinates `x` (sample i is centered at i)"""
    i0 = np.floor(x).astype(int)
    w = np.zeros((len(x), n + 2 * pad))
    for k in range(-1, 3):
        idx = i0 + k
        w[np.arange(len(x)), np.clip(idx + pad, 0, n + 2 * pad - 1)] += _cubic(x - idx)
    return w


def _weights(n, pad=3):
    """cubic-convolution weights that read an n-sample line (padded by `pad` samples each side) at MS samples per source sample"""
    return weights_at((np.arange(n * MS) + 0.5) / MS - 0.5, n, pad)


def up_edge(m):
    """a 1x array enlarged to the master by cubic convolution, its own border continued outward (so a region that touches the tile's border keeps reaching past it)"""
    p = np.pad(np.asarray(m, float), 3, mode='edge')
    return _weights(p.shape[0] - 6) @ p @ _weights(p.shape[1] - 6).T


def up_cubic(m):
    """a 1x mask enlarged to the master by cubic convolution (a smooth field that keeps thin lines their width), zero outside"""
    h, w = m.shape
    p = np.pad(m.astype(float), 3)
    return _weights(h) @ p @ _weights(w).T


def classmap(lab, nclass, soft=0.2, border=1.0):
    """The tile's classes (a 1x map of integers) as a master map of the same integers: each class's indicator is enlarged by a smooth curve (a stair-step becomes a diagonal) and every sample takes the
    class with the strongest field, so nothing is left over and nothing overlaps. Within `border` world pixels of the tile's edge the curve gives way to the pixel itself, so that two neighboring
    tiles' outlines meet exactly where the original's do."""
    h, w = lab.shape
    d = np.minimum.reduce(np.meshgrid(np.minimum((np.arange(w * MS) + 0.5) / MS, w - (np.arange(w * MS) + 0.5) / MS), np.minimum((np.arange(h * MS) + 0.5) / MS, h - (np.arange(h * MS) + 0.5) / MS)))
    wgt = smoothstep(d / border)
    fields = []
    for k in range(nclass):
        ind = (lab == k).astype(float)
        f = up_edge(ind)
        if soft > 0:
            f = blur_edge(f, soft)
        fields.append(wgt * f + (1 - wgt) * big(ind))
    return np.argmax(np.stack(fields), axis=0)


def shift(mask, dx, dy):
    """a master mask moved by (dx, dy) world pixels, nothing coming in from outside"""
    sx, sy = int(round(dx * MS)), int(round(dy * MS))
    out = np.zeros_like(mask)
    h, w = mask.shape
    ys0, ys1 = max(0, sy), min(h, h + sy)
    xs0, xs1 = max(0, sx), min(w, w + sx)
    out[ys0:ys1, xs0:xs1] = mask[ys0 - sy:ys1 - sy, xs0 - sx:xs1 - sx]
    return out


def shift_border(mask, dx, dy):
    """a master mask moved by (dx, dy) world pixels, the tile's border continued outward (what comes in is what was at the border)"""
    sx, sy = int(round(dx * MS)), int(round(dy * MS))
    h, w = mask.shape
    ys = np.clip(np.arange(h) - sy, 0, h - 1)
    xs = np.clip(np.arange(w) - sx, 0, w - 1)
    return mask[np.ix_(ys, xs)]


LIGHT = np.array([-0.55, -0.65, 0.52])          # from the upper left, and above
LIGHT = LIGHT / np.linalg.norm(LIGHT)


def dome(mask, sigma, hi=0.80, lo=0.22, k=1.7):
    """A drawn thing's form from its outline: the mask is taken for a low dome and its slope against the light is cut into three hard steps. Returns 0 outside, 1 the shaded side, 2 the body, 3 the lit top."""
    h = blur(mask.astype(float), sigma)
    gy, gx = np.gradient(h)
    s = sigma * MS * 2.5 * k
    nx, ny = -gx * s, -gy * s
    nz = np.ones_like(nx)
    nrm = np.sqrt(nx * nx + ny * ny + nz * nz)
    lam = (nx * LIGHT[0] + ny * LIGHT[1] + nz * LIGHT[2]) / nrm
    out = np.where(lam > hi, 3, np.where(lam < lo, 1, 2))
    return np.where(mask, out, 0)


# ------------------------------------------------------------------------------------------------ the tile

def pixel_mode(rgb, weights=None):
    """1x from the master: every pixel is the color that most of its samples have (a color of the palette, never a blend); `weights` {color as an integer: factor} makes a color count for more"""
    h = rgb.shape[0] // MS
    w = rgb.shape[1] // MS
    code = (rgb[..., 0].astype(np.int64) << 16) | (rgb[..., 1].astype(np.int64) << 8) | rgb[..., 2].astype(np.int64)
    uniq, inv = np.unique(code, return_inverse=True)
    inv = inv.reshape(code.shape)
    blocks = inv.reshape(h, MS, w, MS).transpose(0, 2, 1, 3).reshape(h, w, MS * MS)
    wt = np.ones(len(uniq)) if weights is None else np.array([weights.get(int(u), 1.0) for u in uniq])
    counts = np.zeros((h, w, len(uniq)))
    for k in range(len(uniq)):
        counts[..., k] = (blocks == k).sum(axis=2) * wt[k]
    pick = counts.argmax(axis=2)
    out = np.zeros((h, w, 3), np.uint8)
    for k, u in enumerate(uniq):
        out[pick == k] = ((int(u) >> 16) & 255, (int(u) >> 8) & 255, int(u) & 255)
    return out


def code_of(color):
    return (int(color[0]) << 16) | (int(color[1]) << 8) | int(color[2])


class Tile:
    """A tile's master: flat colors painted in order (a mask and a color); the alpha is always 255. `priority` makes a color count for more when 1x takes the most frequent color of a pixel."""

    def __init__(self, base=(0, 0, 0), rgb=None):
        self.rgb = np.zeros((N, N, 3), np.uint8) if rgb is None else rgb
        if rgb is None:
            self.rgb[:] = base
        self.priority = {}

    def paint(self, mask, color, prio=None):
        self.rgb[mask] = color
        if prio is not None:
            self.priority[code_of(color)] = prio

    def pictures(self):
        """{scale: RGBA array} for 1, 2 and 3"""
        out = {}
        for scale, factor in ((3, 4), (2, 6), (1, 12)):
            if scale == 1:
                arr = pixel_mode(self.rgb, self.priority)
            else:
                a = self.rgb.astype(float)
                arr = np.clip(np.rint(a.reshape(N // factor, factor, N // factor, factor, 3).mean(axis=(1, 3))), 0, 255).astype(np.uint8)
            out[scale] = np.dstack([arr, np.full(arr.shape[:2], 255, np.uint8)])
        return out


# ------------------------------------------------------------------------------------------------ the original's tile

def original(number):
    """the RGB pixels of a decoded sprite (T x T for a tile)"""
    return np.array(sprite(number).convert('RGB'))


def components(mask):
    """8-connected components of a small boolean array: lists of (y, x)"""
    h, w = mask.shape
    seen = np.zeros_like(mask, bool)
    out = []
    for y in range(h):
        for x in range(w):
            if mask[y, x] and not seen[y, x]:
                st, comp = [(y, x)], []
                seen[y, x] = True
                while st:
                    cy, cx = st.pop()
                    comp.append((cy, cx))
                    for dy in (-1, 0, 1):
                        for dx in (-1, 0, 1):
                            ny, nx = cy + dy, cx + dx
                            if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not seen[ny, nx]:
                                seen[ny, nx] = True
                                st.append((ny, nx))
                out.append(comp)
    return out


def is_col(rgb, color):
    """the pixels of a 1x picture that have this color"""
    return (rgb == np.array(color)).all(-1)


def trace(rgb, regions, soft=0.25):
    """The original's tile as clean shapes in its own exact colors; `regions` [(1x mask, master picture)] are filled from a picture instead (a plain tile's drawing), in the order given."""
    lab = np.full(rgb.shape[:2], -1, int)
    n = len(regions)
    for k, (m, _) in enumerate(regions):
        lab[m & (lab < 0)] = k
    cols = sorted(set(map(tuple, rgb[lab < 0].reshape(-1, 3))))
    for j, c in enumerate(cols):
        lab[is_col(rgb, c) & (lab < 0)] = n + j
    cm = classmap(lab, n + len(cols), soft=soft, border=1.0)
    out = np.zeros((N, N, 3), np.uint8)
    for k, (_, pic) in enumerate(regions):
        out[cm == k] = pic[cm == k]
    for j, c in enumerate(cols):
        out[cm == n + j] = c
    return out
