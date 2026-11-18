"""The pedestals, their eight pictures and the ornament ants as carvings (relief.py). The pictures follow the original's drawings: same subject, same pose, same place on the face."""
import math, random
import numpy as np
from PIL import Image
import relief as R
from relief import Field, S

PW, PH = 56, 76              # the pedestal sprite (canvas pixels); the slab stands at (X0, Y0)
X0, Y0, SW, SH = 2, 2, 46, 63
IX, IY = X0 + 6, Y0 + 4      # the picture's 34 x 31 box on the face

def slab_outline(dx=0.0, dy=0.0):
    """the pedestal's outline: a tablet with rounded corners whose sides lean in at the waist (the original's rocker shape)"""
    pts = []; r = 5.0
    def side(t): return 1.5 * math.sin(math.pi * min(max((t - 0.12) / 0.62, 0), 1))        # how far the side leans in at height t (0 top .. 1 bottom)
    x0, y0, x1, y1 = X0 + dx, Y0 + dy, X0 + SW + dx, Y0 + SH + dy
    for a in range(180, 271, 15): pts.append((x0 + r + r * math.cos(math.radians(a)), y0 + r + r * math.sin(math.radians(a))))
    for a in range(270, 361, 15): pts.append((x1 - r + r * math.cos(math.radians(a)), y0 + r + r * math.sin(math.radians(a))))
    for i in range(1, 12): t = i / 12.0; pts.append((x1 - side(t), y0 + r + (SH - 2 * r) * t))
    for a in range(0, 91, 15): pts.append((x1 - r + r * math.cos(math.radians(a)), y1 - r + r * math.sin(math.radians(a))))
    for a in range(90, 181, 15): pts.append((x0 + r + r * math.cos(math.radians(a)), y1 - r + r * math.sin(math.radians(a))))
    for i in range(11, 0, -1): t = i / 12.0; pts.append((x0 + side(t), y0 + r + (SH - 2 * r) * t))
    return pts

# ---- the eight pictures, in a 34 x 31 box; each part: (shape, geometry, height, rounding, material) --------------------------------------------------------------
def picture(kind):
    E, L, P = 'ellipse', 'line', 'poly'
    if kind == 'move':            # the running ant: head, chest and hip as three blobs, arms and legs mid-stride, running to the right
        return [(E, (22.5, 5.6, 5.2, 4.3, -15), 2.6, 3.0, 0), (E, (17.2, 12.6, 5.4, 3.9, -32), 2.5, 3.0, 0), (E, (11.6, 18.6, 5.4, 4.0, -28), 2.5, 3.0, 0),
                (L, ([(19.6, 11), (25.6, 12.4), (29.2, 9.6)], 2.5), 1.7, 1.3, 0), (L, ([(14.6, 11.2), (9.6, 12), (7.2, 9.2)], 2.3), 1.5, 1.2, 0),
                (L, ([(14.4, 20.6), (20.4, 22.8), (19.4, 27.6), (22.6, 28.4)], 2.7), 1.8, 1.4, 0), (L, ([(8.8, 21), (5.6, 25.4), (1.8, 26.2)], 2.7), 1.7, 1.4, 0)]
    if kind == 'ally':            # two ants hand in hand, the second one gold (the other team)
        def fig(cx, m, mirror):
            sg = -1 if mirror else 1
            return [(E, (cx, 5.8, 4.2, 3.7, 0), 2.4, 2.8, m), (E, (cx - sg * .4, 13.4, 4.3, 3.5, 0), 2.3, 2.8, m), (E, (cx + sg * .6, 21.8, 4.6, 4.0, 0), 2.4, 2.8, m),
                    (L, ([(cx + sg * 3.2, 12.6), (cx + sg * 7, 16.2), (cx + sg * 8.4, 15.8)], 2.0), 1.5, 1.1, m), (L, ([(cx - sg * 3.4, 12), (cx - sg * 6.6, 14.6), (cx - sg * 5.4, 17.8)], 2.0), 1.4, 1.1, m),
                    (L, ([(cx - sg * 2, 24.6), (cx - sg * 4.6, 28)], 2.0), 1.3, 1.1, m), (L, ([(cx + sg * 3, 24.6), (cx + sg * 5, 28)], 2.0), 1.3, 1.1, m)]
        return fig(8.6, 0, False) + fig(25.4, 1, True)
    if kind == 'hatch':           # a real egg (narrow end up to the right, as the original holds it), cracked across: the cap has lifted a little along a zig-zag, a chip of shell stands off
        return [('fn', egg_relief, 0, 0, 0)]
    if kind == 'attack':          # after the original: a human-like figure seen from behind, from the waist up: round head, broad back, the left arm hanging, the right arm raised and bent in a flex with its fist up,
                                  # and a sash crossed in an X over the back (the pale zig-zag of the original). Same place and size as the original's 34 x 23 drawing (the lower part of the box).
        return [(E, (15, 9.4, 5.3, 4.4, 0), 2.7, 3.2, 0), (E, (15, 14.2, 2.4, 1.8, 0), 2.0, 1.6, 0),
                (P, [(4.4, 15.4), (25.6, 15.4), (23.4, 22), (20.6, 28.6), (9.4, 28.6), (6.6, 22)], 2.5, 3.2, 0),                                                  # the back, wide at the shoulders
                (L, ([(4.8, 16.6), (2.4, 21.6), (3.2, 27.4)], 3.1), 2.1, 1.7, 0),                                                                                 # the left arm, hanging
                (L, ([(25.2, 16.6), (30.4, 18)], 3.4), 2.3, 1.8, 0), (E, (28.2, 15.6, 3.0, 2.3, -12), 2.5, 2.2, 0),                                               # the right upper arm and its biceps
                (L, ([(30.6, 17.8), (29.6, 11.4)], 3.0), 2.2, 1.7, 0), (E, (29.2, 9.2, 2.6, 2.4, 0), 2.5, 2.2, 0),                                               # forearm up, fist
                (L, ([(7.4, 16.4), (20.2, 28)], 1.9), 3.4, 0.9, 2), (L, ([(22.6, 16.4), (9.8, 28)], 1.9), 3.4, 0.9, 2)]                                           # the sash, crossed
    if kind == 'bomb':            # the stick, bound three times, its fuse spitting sparks at the upper right
        a = math.radians(-48); cx, cy, hl, hw = 13.6, 17.6, 12.4, 5.0
        ux, uy = math.sin(-a), -math.cos(-a)          # along the stick, toward the fuse
        vx, vy = -uy, ux
        box = [(cx + ux * hl + vx * hw, cy + uy * hl + vy * hw), (cx + ux * hl - vx * hw, cy + uy * hl - vy * hw), (cx - ux * hl - vx * hw, cy - uy * hl - vy * hw), (cx - ux * hl + vx * hw, cy - uy * hl + vy * hw)]
        parts = [(P, box, 3.2, 3.6, 0)]
        for t in (-0.52, 0.0, 0.52):
            parts.append(('cut', ([(cx + ux * hl * t + vx * hw, cy + uy * hl * t + vy * hw), (cx + ux * hl * t - vx * hw, cy + uy * hl * t - vy * hw)], 1.0), 0.9, 0.8, 0))
        tx, ty = cx + ux * hl, cy + uy * hl
        parts += [(L, ([(tx - .6, ty + .6), (tx + 2.6, ty - 2.6), (tx + 5.4, ty - 2.2)], 1.5), 1.2, 0.9, 0),
                  (E, (tx + 7.6, ty - 3.4, 1.3, 1.3, 0), 1.3, 1.0, 2), (E, (tx + 5.4, ty - 5.6, .95, .95, 0), 1.0, 0.8, 2), (E, (tx + 8.8, ty + .4, .9, .9, 0), 1.0, 0.8, 2), (E, (tx + 8.6, ty - 6.6, .8, .8, 0), .9, 0.7, 2)]
        return parts
    if kind == 'fire':            # the flame, drawn as a raised line, and the match that lit it, a drop falling from its head
        fl = [(8.2, 1.6), (10.6, 6.4), (9.2, 10.6), (12.4, 14.6), (12, 20), (8.6, 23.4), (4.6, 21.6), (3, 16.6), (5.2, 11.6), (5, 7), (8.2, 1.6)]
        return [(L, (fl, 1.7), 1.5, 1.0, 0), (L, ([(7.6, 12.6), (9, 16.6), (7.4, 20)], 1.4), 1.2, 0.9, 0), (L, ([(2.6, 26), (8, 28.2), (13.6, 26.2)], 2.0), 1.3, 1.1, 0),
                (L, ([(14.6, 24.6), (26.6, 12)], 3.4), 2.2, 1.8, 0), (E, (28.6, 10, 3.5, 2.8, -46), 2.6, 2.4, 0), (E, (30.4, 19.2, 1.4, 2.0, 0), 1.4, 1.1, 0)]
    if kind == 'thief':           # Steal: a hand (wrist, palm, four fingers side by side with air between them, a thumb) reaching out of a dark opening at the upper left to grab a round crumb at the lower right
        d = (0.75, 0.66); p = (-0.66, 0.75)                      # along the reach, and across it
        base = (19.4, 16.8)                                      # the middle of the knuckle line
        parts = [('pit', [(0.5, 0.5), (12.6, 0.5), (12.6, 12.6), (0.5, 12.6)], 2.0, 1.1, 0),
                 (L, ([(4.2, 4.2), (11.4, 10)], 6.0), 2.6, 2.6, 0), (E, (15.4, 13.4, 6.3, 6.7, 41), 2.9, 3.4, 0)]
        for t, ln in ((-5.5, 6.0), (-1.85, 7.3), (1.8, 6.9), (5.4, 5.4)):
            b = (base[0] + p[0] * t, base[1] + p[1] * t)
            k1 = (b[0] + d[0] * ln * .55 + p[0] * .2, b[1] + d[1] * ln * .55 + p[1] * .2); tip = (b[0] + d[0] * ln + p[0] * .9, b[1] + d[1] * ln + p[1] * .9)
            parts.append((L, ([(b[0] - d[0] * 1.5, b[1] - d[1] * 1.5), k1, tip], 2.5), 2.0, 1.4, 0))
        parts += [(L, ([(18.6, 9.2), (23, 6.6), (26.4, 7.6)], 2.7), 2.0, 1.5, 0), (E, (29.2, 26.2, 3.0, 3.0, 0), 2.7, 2.8, 0)]
        return parts
    if kind == 'swim':            # Bridge: the shovel, blade up, and the water's wave
        return [(L, ([(0.8, 6.6), (2.8, 4.4), (4.8, 6.6), (6.8, 4.4), (8.8, 6.6), (10.8, 4.4), (12.8, 6.6), (14.8, 4.4)], 1.3), 1.0, 0.8, 0),
                (P, [(20.8, 6.52), (24.95, 2.71), (28.38, 2.29), (31.1, 3.47), (32.05, 6.29), (31.34, 9.67), (27.2, 13.48)], 2.5, 1.7, 0),      # a spade's blade: square shoulders where the shaft meets it, a blunt rounded nose
                (L, ([(24.4, 10.4), (10.2, 23.4)], 2.3), 1.8, 1.3, 0), (L, ([(10.2, 23.4), (5.6, 23.2), (3.6, 27.6), (8.4, 28.4), (10.2, 23.4)], 1.8), 1.4, 1.0, 0),
                (L, ([(22, 22.4), (24, 20.4), (26, 22.4), (28, 20.4), (30, 22.4), (32, 20.4)], 1.2), 0.9, 0.7, 0)]
    raise ValueError(kind)

ZIG = [(-1.0, .36), (-.62, .14), (-.26, .40), (.10, .12), (.46, .38), (.80, .16), (1.0, .30)]       # the crack: across the egg (-1 .. 1), how far along it (toward the narrow end)

def egg_relief(g, ox, oy):
    """the cracked egg as a height: one egg-shaped dome, cut in two along the zig-zag with crisp edges; the cap is moved off toward the narrow end, so the crack is a dark gap with a lit lip below it and a shaded one above"""
    A, B = 13.4, 9.6; cx, cy = ox + 16.4, oy + 15.6
    ang = math.radians(-34); ax, ay = math.cos(ang), math.sin(ang); nx, ny = -ay, ax
    def M(u, s, dx=0.0, dy=0.0):
        w = B * math.sqrt(max(1 - u * u, 0)) * (1 - 0.24 * u)
        return (cx + ax * A * u + nx * w * s + dx, cy + ay * A * u + ny * w * s + dy)
    def outline(dx=0.0, dy=0.0): return [M(math.cos(t), 1 if math.sin(t) >= 0 else -1, dx, dy) for t in [2 * math.pi * i / 72 for i in range(72)]]
    def uz(s):
        for (s0, u0), (s1, u1) in zip(ZIG, ZIG[1:]):
            if s0 <= s <= s1: return u0 + (u1 - u0) * (s - s0) / (s1 - s0)
        return ZIG[-1][1]
    zz = [(s, u) for s, u in ZIG]
    sx, sy = ax * 1.5 - nx * 0.5, ay * 1.5 - ny * 0.5                         # how far the cap has lifted
    lower = [M(-1 + (uz(-1) + 1) * i / 14.0, -1) for i in range(14, -1, -1)] + [M(-1 + (uz(1) + 1) * i / 14.0, 1) for i in range(0, 15)] + [M(u, s) for s, u in reversed(zz)]
    cap = [M(u, s, sx, sy) for s, u in zz] + [M(uz(1) + (1 - uz(1)) * i / 10.0, 1, sx, sy) for i in range(0, 11)] + [M(uz(-1) + (1 - uz(-1)) * i / 10.0, -1, sx, sy) for i in range(10, -1, -1)]
    dome = g.round(g.m_poly(outline()), 6.2) * 3.5
    dome_cap = g.round(g.m_poly(outline(sx, sy)), 6.2) * 3.7
    H = np.maximum(dome * g.round(g.m_poly(lower), 0.8), dome_cap * g.round(g.m_poly(cap), 0.8))
    chip = [M(.50, -1.16, sx, sy), M(.66, -1.34, sx, sy), M(.74, -1.12, sx, sy)]
    H = np.maximum(H, g.round(g.m_poly(chip), 0.6) * 1.5)
    return H

def lay(f, parts, ox, oy, scale=1.0):
    """the parts of a picture as one small relief (their union), returned as a height and its material"""
    g = Field(f.w, f.h)
    cuts = []
    for shape, geo, height, r, mat in parts:
        if shape == 'fn':
            Hn = geo(g, ox, oy); g.mat[Hn > np.maximum(g.H, 0.05)] = 3; g.H = np.maximum(g.H, Hn); continue
        if shape == 'ellipse': cx, cy, rx, ry, rot = geo; m = g.m_ellipse(ox + cx * scale, oy + cy * scale, rx * scale, ry * scale, rot)
        elif shape == 'poly': m = g.m_poly([(ox + x * scale, oy + y * scale) for x, y in geo])
        elif shape == 'pit': cuts.append((g.m_poly([(ox + x * scale, oy + y * scale) for x, y in geo]), height, r)); continue
        else:
            pts, w = geo; m = g.m_line([(ox + x * scale, oy + y * scale) for x, y in pts], w * scale)
            if shape == 'cut': cuts.append((m, height, r)); continue
        g.add(m, height * scale, r * scale, 3 if mat == 0 else mat, 'max')
    return g, cuts

_slab_cache = {}
def slab(kind, front=True):
    """the pedestal's body as a field: pillow-edged tablet, the face dished, the stair below, the picture standing on the face"""
    key = (kind if front else None, front)
    if key in _slab_cache: return _slab_cache[key]
    f = Field(PW, PH)
    body = f.m_poly(slab_outline())
    f.add(body, 5.0, 4.6, 0, 'max')
    if front:
        face = f.m_poly([(X0 + 4.6, Y0 + 3.6), (X0 + SW - 4.6, Y0 + 3.6), (X0 + SW - 5.6, Y0 + 36.6), (X0 + 5.6, Y0 + 36.6)])
        f.add(face, 1.3, 7.0, None, 'cut')                                                   # the dished face
        for i in range(5):                                                                    # the stair: five treads, each a little wider than the one above
            ins = 6.4 - i * 0.45; y = Y0 + 38.4 + i * 4.5
            f.add(f.m_poly([(X0 + ins, y), (X0 + SW - ins, y), (X0 + SW - ins + .3, y + 3.4), (X0 + ins - .3, y + 3.4)]), 1.5, 1.1, 4, 'add')
        g, cuts = lay(f, picture(kind), IX, IY)
        for m, d, r in cuts: f.add(m, d, r, None, 'cut')
        f.H = f.H + g.H; f.mat[g.H > 0.08] = g.mat[g.H > 0.08]
    f.body = np.clip(R.blur(body, 0.35 * S), 0, 1)
    _slab_cache[key] = f
    return f

def shift(a, dx, dy):
    """a picture or mask moved by whole samples"""
    out = np.zeros_like(a); h, w = a.shape[:2]; dx, dy = int(round(dx * S)), int(round(dy * S))
    out[max(dy, 0):h + min(dy, 0), max(dx, 0):w + min(dx, 0)] = a[max(-dy, 0):h + min(-dy, 0), max(-dx, 0):w + min(-dx, 0)]
    return out

def over(dst, src):
    """src over dst (float RGBA arrays, alpha 0 .. 1)"""
    a = src[..., 3:4]; b = dst[..., 3:4] * (1 - a); o = a + b
    rgb = np.where(o > 1e-6, (src[..., :3] * a + dst[..., :3] * b) / np.maximum(o, 1e-6), 0)
    return np.dstack([rgb, o])

def rgba(rgb, alpha): return np.dstack([rgb, alpha])

_img_cache = {}
def slab_rgba(kind, front=True):
    key = (kind if front else None, front)
    if key not in _img_cache:
        f = slab(kind, front)
        I = f.intensity(ao=0.20, ao_r=2.4)
        _img_cache[key] = rgba(R.colorize(f, I, gain=1.3, bias=0.015), f.body)
    return _img_cache[key]

def band_rgba():
    """the latch: a beveled gold band round the pedestal's seat, the seat's gold-lined walls inside it"""
    if 'band' not in _img_cache:
        f = Field(PW, PH)
        def rr(x0, y0, x1, y1, r):
            p = []
            for cx, cy, a0 in ((x0 + r, y0 + r, 180), (x1 - r, y0 + r, 270), (x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90)):
                for a in range(a0, a0 + 91, 15): p.append((cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a))))
            return p
        outer = f.m_poly(rr(X0 - 2.9, Y0 - 0.9, X0 + SW + 2.9, Y0 + SH + 4.9, 7.0)); inner = f.m_poly(rr(X0 + 0.5, Y0 + 2.5, X0 + SW - 0.5, Y0 + SH + 1.5, 4.4))
        f.mat[:] = 1
        f.add(np.clip(outer - inner, 0, 1), 2.8, 1.6, 1, 'max')
        f.H = f.H - inner * 1.6                                                               # the seat lies below the plate
        I = f.intensity(ao=0.26, ao_r=2.6)
        _img_cache['band'] = rgba(R.colorize(f, I, gain=1.0, bias=-0.03), np.clip(R.blur(outer, 0.35 * S), 0, 1))
    return _img_cache['band']

def shadow_rgba(mask, dx, dy, soft, strength):
    return rgba(np.zeros(mask.shape + (3,)) + np.array((3, 20, 13.0)), np.clip(R.blur(shift(mask, dx, dy), soft * S), 0, 1) * strength)

def socket_rgba(outline_only=False):
    key = 'sock%d' % outline_only
    if key not in _img_cache:
        f = Field(PW, PH); m = f.m_poly(slab_outline())
        f.add(m, 0.5 if outline_only else 3.4, 0.9 if outline_only else 2.2, None, 'cut')
        I = f.intensity(ao=0.30, ao_r=2.6)
        _img_cache[key] = rgba(R.colorize(f, I, gain=1.0), np.clip(R.blur(m, 0.8 * S), 0, 1) * (0.8 if outline_only else 1.0))
    return _img_cache[key]

RISE = [None, None, ('back', .86), ('back', .56), ('back', .2), ('front', .5), ('front', .76), ('front', .93), ('front', 1.0)]

def pedestal_image(kind, frame):
    """one pedestal frame as an RGBA picture at 6 samples per canvas pixel: up, press1, press2, latched, rise1 .. rise9"""
    out = np.zeros((PH * S, PW * S, 4))
    if frame.startswith('rise'):
        n = int(frame[4:])
        out = over(out, socket_rgba(n == 1))
        if n >= 3:
            side, fx = RISE[n - 1]
            src = slab_rgba(kind, side == 'front').copy()
            src[..., :3] *= (1 - (1 - fx) * 0.5)                                              # the turning slab is in its own shade
            im = Image.fromarray((np.dstack([src[..., :3], src[..., 3:4] * 255])).clip(0, 255).astype(np.uint8), 'RGBA')
            w = max(int(round(im.width * fx)), 1)
            sq = Image.new('RGBA', im.size, (0, 0, 0, 0)); cxs = int((X0 + SW / 2.0) * S)
            piece = im.resize((w, im.height), Image.LANCZOS)
            sq.paste(piece, (cxs - int(round(cxs * fx)), 0))
            a = np.asarray(sq).astype(float); out = over(out, np.dstack([a[..., :3], a[..., 3:4] / 255.0]))
        return R.to_image(out[..., :3], out[..., 3])
    dn = {'up': 0, 'press1': 1, 'press2': 2, 'latched': 2}[frame]
    s = slab_rgba(kind)
    if frame == 'up': out = over(out, shadow_rgba(s[..., 3], 4.2, 5.2, 1.5, .55))
    if frame == 'press1': out = over(out, shadow_rgba(s[..., 3], 2.6, 3.4, 1.2, .5))
    if frame in ('press2', 'latched'):
        b = band_rgba().copy()
        if frame == 'press2': b[..., 3] *= .6
        out = over(out, b)
        out = over(out, shadow_rgba(s[..., 3], 1.0, 3.0, 0.9, .5) * np.dstack([np.ones_like(b[..., :3]), b[..., 3:4] > 0.5]))
    sl = np.dstack([shift(s[..., c], 0, dn) for c in range(4)])
    out = over(out, sl)
    return R.to_image(out[..., :3], out[..., 3])

# ---- ornament ants ---------------------------------------------------------------------------------------------------------------------------------------
def ant_parts(seed):
    """one ant in its own coordinates (heading up, about 30 long): head, a chest of two lobes, the waist, the abdomen with two segment lines, six thin legs of three joints, two elbowed feelers"""
    r = random.Random(seed)
    turn = r.uniform(-18, 18); bend = r.uniform(-20, 20)
    def rot(p, a, o):
        c, s = math.cos(math.radians(a)), math.sin(math.radians(a)); x, y = p[0] - o[0], p[1] - o[1]
        return (o[0] + x * c - y * s, o[1] + x * s + y * c)
    neck, waist = (0, -7.8), (0, 0.6)
    hc = rot((0, -10.8), turn, neck); gc = rot((0, 7.4), bend, waist)
    parts = [('ellipse', (hc[0], hc[1], 3.7, 3.3, turn), 1.7, 2.2), ('ellipse', (0, -5.8, 2.9, 2.6, 0), 1.6, 2.0), ('ellipse', (0, -2.6, 2.4, 2.3, 0), 1.5, 1.8), ('ellipse', (0, 0.5, 1.3, 1.3, 0), 1.2, 1.2),
             ('ellipse', (gc[0], gc[1], 5.5, 6.6, bend), 1.9, 2.8)]
    for t in (-0.22, 0.26):
        a, b = rot((-5.2, 7.4 + t * 6.6 + 1.0), bend, waist), rot((5.2, 7.4 + t * 6.6 + 1.0), bend, waist); m = rot((0, 7.4 + t * 6.6 - 0.2), bend, waist)
        parts.append(('cut', ([a, m, b], 0.7), 0.45, 0.5))
    legs = [((1.9, -6.2), (6.4, -8.8), (8.0, -12.8), (9.6, -13.4)), ((2.3, -4.2), (7.6, -4.8), (10.0, -1.2), (11.6, -0.8)), ((2.0, -2.4), (6.6, 0.6), (8.2, 6.6), (9.8, 8.0))]
    for side in (1, -1):
        for root, knee, foot, tip in legs:
            a = r.uniform(-20, 20); l = r.uniform(.82, 1.1)
            pts = [root] + [rot((root[0] + (p[0] - root[0]) * l, root[1] + (p[1] - root[1]) * l), a + r.uniform(-8, 8), root) for p in (knee, foot, tip)]
            parts.append(('line', ([(side * x, y) for x, y in pts], 1.35), 0.95, 0.8))
        a = r.uniform(-18, 18); o = (side * 1.6, -13.2)
        fe = [rot(o, turn, neck), rot(rot((side * 4.2, -16.2), a, o), turn, neck), rot(rot((side * 7.4, -15.2), a + r.uniform(-14, 14), o), turn, neck)]
        parts.append(('line', (fe, 0.95), 0.7, 0.65))
    return parts

def ants_overlay(w, h, ants, keep=None):
    """the ornament ants of a w x h canvas-pixel piece as one translucent relief; `keep` is a mask (1 = the ants may show) that tucks them under frames"""
    f = Field(w, h)
    for cx, cy, heading, s, seed in ants:
        c, sn = math.cos(math.radians(heading)), math.sin(math.radians(heading))
        def T(p): return (cx + (p[0] * c - p[1] * sn) * s, cy + (p[0] * sn + p[1] * c) * s)
        cuts = []
        for shape, geo, height, rr in ant_parts(seed):
            if shape == 'ellipse':
                ex, ey, rx, ry, rot = geo; q = T((ex, ey)); f.add(f.m_ellipse(q[0], q[1], rx * s, ry * s, rot + heading), height * s, rr * s, None, 'max')
            else:
                pts, wd = geo; m = f.m_line([T(p) for p in pts], wd * max(s, .8))
                if shape == 'cut': cuts.append((m, height * s, rr * s))
                else: f.add(m, height * s, rr * s, None, 'max')
        for m, d, rr in cuts: f.H = f.H - f.round(m, rr) * d * (f.H > 0.3)
    I = f.intensity(ao=0.22, ao_r=1.6, soften=0.3)
    im = R.emboss_overlay(f, I, light_a=1.9, dark_a=2.2)
    if keep is not None:
        a = np.asarray(im).copy(); a[..., 3] = (a[..., 3] * keep).astype(np.uint8); im = Image.fromarray(a, 'RGBA')
    return im


# ---- the remaining HUD frames: the gold rim flash, the eggs of the tray and their hatching, the lunch box ---------------------------------------------------
def small(w, h, build, shadow=(0.8, 1.0, 0.55, 0.5), gain=1.15):
    """a small carved sprite, w x h canvas pixels: build(f) fills the field; returns the RGBA picture at 6 samples per pixel with a soft shadow under it"""
    f = Field(w, h); build(f)
    I = f.intensity(ao=0.2, ao_r=1.4, soften=0.25)
    body = np.clip(f.solid, 0, 1)
    out = np.zeros((h * S, w * S, 4))
    if shadow: out = over(out, shadow_rgba(body, shadow[0], shadow[1], shadow[2], shadow[3]))
    out = over(out, rgba(R.colorize(f, I, gain=gain), body))
    return R.to_image(out[..., :3], out[..., 3])

def egg_outline(cx, cy, A, B, ang, n=48):
    a = math.radians(ang); ax, ay = math.cos(a), math.sin(a); nx, ny = -ay, ax
    pts = []
    for i in range(n):
        t = 2 * math.pi * i / n; u = math.cos(t); w = B * math.sin(t) * (1 - 0.2 * u)
        pts.append((cx + ax * A * u + nx * w, cy + ay * A * u + ny * w))
    return pts

def egg_image(stage=0):
    """the tray's egg (12 x 16), standing a little tilted, narrow end up. stage 1: cracked across, the pale inside showing in the gap (the first hatching frame)"""
    def build(f):
        o = egg_outline(5.9, 8.2, 6.6, 4.7, -78)
        m = f.m_poly(o)
        if stage == 0:
            f.add(m, 2.6, 3.6, 5, 'max'); return
        f.add(m, 0.7, 1.2, 2, 'max')                                           # the pale inside, low
        zig = [(0.4, 6.6), (3.0, 8.0), (5.2, 6.2), (7.6, 8.0), (9.6, 6.4), (11.8, 7.6)]
        lower = f.m_poly([(0, 16), (0, zig[0][1] + .6)] + [(x, y + .6) for x, y in zig] + [(12, 16)]); upper = f.m_poly([(0, -1), (0, zig[0][1] - .9)] + [(x, y - .9) for x, y in zig] + [(12, -1)])
        dome = f.round(m, 3.6) * 2.6
        Hs = np.maximum(dome * f.round(m * lower, 0.5), shift(dome * f.round(m * upper, 0.5), 0.5, -0.5))
        f.mat[Hs > np.maximum(f.H, 0.75)] = 5; f.H = np.maximum(f.H, Hs)
    return small(12, 16, build)

def egg_open_image():
    """the second hatching frame (17 x 14): the shell fallen open in two halves, their pale insides up"""
    def build(f):
        def half(cx, cy, rx, ry, rot, dx, dy):
            outer = f.m_ellipse(cx, cy, rx, ry, rot); inner = f.m_ellipse(cx + dx, cy + dy, rx * .82, ry * .82, rot)
            f.add(outer, 0.6, 0.9, 2, 'max')
            Hs = f.round(np.clip(outer - inner, 0, 1), 0.9) * 2.0
            f.mat[Hs > np.maximum(f.H, 0.7)] = 5; f.H = np.maximum(f.H, Hs); f.solid = np.maximum(f.solid, np.clip(R.blur(outer, 0.3 * S), 0, 1))
        half(4.8, 7.4, 4.3, 5.8, -12, 1.5, -1.3); half(11.8, 8.4, 4.5, 4.9, 38, -1.3, -1.6)
    return small(17, 14, build)

def egg_bits_image():
    """the last hatching frame (19 x 6): bits of shell lying flat"""
    def build(f):
        for i, (pts, m) in enumerate(((((1, 4.6), (3.6, 1.6), (5, 4.8)), 2), (((6, 4.8), (8.2, 2.6), (10.4, 5)), 5), (((10.6, 4.8), (12.4, 3), (14, 5)), 5), (((14.4, 5), (16, 2.6), (18, 4.8)), 5))):
            f.add(f.m_poly(list(pts)), 1.0, 0.7, m, 'max')
    return small(19, 6, build, shadow=(0.5, 0.6, 0.4, 0.45))

def lunch_box_image():
    """the lunch box (34 x 40): a gold box with a lid, a clasp and a handle; shown on the card while every selected ant carries food"""
    def build(f):
        f.mat[:] = 1
        box = [(3.5, 14), (30.5, 14), (30.5, 34), (28.5, 36.5), (5.5, 36.5), (3.5, 34)]
        f.add(f.m_poly(box), 3.4, 2.6, 1, 'max')
        f.add(f.m_line([(11, 14.5), (11.6, 7.2), (22.4, 7.2), (23, 14.5)], 2.6), 2.2, 1.5, 1, 'max')                       # the handle
        f.add(f.m_line([(3.6, 20.6), (30.4, 20.6)], 1.0), 0.9, 0.6, None, 'cut')                                           # the lid's seam
        f.add(f.m_poly([(15, 18.6), (19, 18.6), (19, 23.4), (15, 23.4)]), 1.1, 0.8, 1, 'add')                             # the clasp
    return small(34, 40, build, shadow=(1.6, 2.0, 1.0, 0.5), gain=1.0)

def flash_image(level):
    """the gold rim flash (the original's butdefl / butdefr): a thin beveled gold line round the left, lower and right edges of a standing pedestal, in three strengths; drawn over the pedestal"""
    f = Field(PW, PH); f.mat[:] = 1
    cx, cy = X0 + SW / 2.0, Y0 + SH / 2.0
    pts = [(x + (0.7 if x > cx else -0.7), y + (0.7 if y > cy + 20 else 0)) for x, y in slab_outline() if y > Y0 + 6.5]
    m = f.m_line(pts, 1.3)
    f.add(m, 1.1, 0.7, 1, 'max')
    I = f.intensity(ao=0.1, ao_r=1.2, soften=0.2)
    return R.to_image(R.colorize(f, I, gain=1.0, bias=0.08 * (level - 2)), np.clip(R.blur(m, 0.3 * S), 0, 1) * (0.55, 0.8, 1.0)[level - 1])
