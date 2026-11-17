"""Sculpted relief instead of flat symbols. Every pedestal, pedestal picture and ornament ant is built as a HEIGHT FIELD (rounded solids: pillow slab, dished face, stair, the picture's
parts as small domes and ridges, grooves cut in), then lit like a carving: one soft light from the upper left, shading that follows the form, darkening in the recesses (ambient occlusion),
a muted highlight, no drawn outlines. The colors are the original palette's greens and golds on a ramp. Drawn at 6 samples per canvas pixel and averaged down to 1x, 2x, 3x.

Coordinates are canvas pixels; heights are canvas pixels too."""
import math
import numpy as np
from PIL import Image, ImageDraw

S = 6                                   # samples per canvas pixel
LIGHT = np.array([-0.56, -0.66, 0.50]); LIGHT = LIGHT / np.linalg.norm(LIGHT)
HALF = LIGHT + np.array([0, 0, 1.0]); HALF = HALF / np.linalg.norm(HALF)
AMB, KD, KS = 0.20, 0.66, 0.10
FLAT = AMB + KD * LIGHT[2]              # the brightness of the flat plate

GREEN = [(0.00, (6, 22, 15)), (0.18, (17, 50, 36)), (0.36, (25, 84, 63)), (0.53, (36, 110, 82)), (0.68, (52, 140, 104)), (0.84, (98, 180, 144)), (1.00, (172, 226, 204))]
GOLD = [(0.00, (44, 31, 3)), (0.25, (104, 76, 6)), (0.50, (168, 136, 6)), (0.70, (214, 186, 28)), (0.86, (240, 220, 80)), (1.00, (255, 246, 176))]
PALE = [(0.00, (40, 90, 70)), (0.5, (150, 215, 185)), (1.00, (240, 255, 248))]
TAN = [(0.00, (38, 24, 6)), (0.28, (112, 76, 20)), (0.52, (178, 136, 58)), (0.74, (226, 196, 132)), (1.00, (255, 242, 222))]          # the eggs' shell

def blur(a, sigma):
    """gaussian blur (sigma in samples), edges held"""
    if sigma <= 0: return a
    p = int(sigma * 3) + 2
    b = np.pad(a, p, mode='edge')
    h, w = b.shape
    fy = np.fft.fftfreq(h)[:, None]; fx = np.fft.rfftfreq(w)[None, :]
    G = np.exp(-2 * (np.pi ** 2) * (sigma ** 2) * (fx ** 2 + fy ** 2))
    return np.fft.irfft2(np.fft.rfft2(b) * G, s=b.shape)[p:-p, p:-p]

class Field:
    """a w x h canvas-pixel patch: height, material (0 green, 1 gold, 2 pale) and the silhouette of what is solid"""
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.H = np.zeros((h * S, w * S)); self.mat = np.zeros((h * S, w * S), np.uint8); self.solid = np.zeros((h * S, w * S))

    # ---- masks ---------------------------------------------------------------------------------------------------------------------------------------------
    def mask(self, draw_fn):
        im = Image.new('L', (self.w * S, self.h * S), 0); draw_fn(ImageDraw.Draw(im)); return np.asarray(im).astype(float) / 255.0
    def m_poly(self, pts): return self.mask(lambda d: d.polygon([(x * S, y * S) for x, y in pts], fill=255))
    def m_ellipse(self, cx, cy, rx, ry, rot=0):
        n = 40; c, s = math.cos(math.radians(rot)), math.sin(math.radians(rot))
        return self.m_poly([(cx + rx * math.cos(t) * c - ry * math.sin(t) * s, cy + rx * math.cos(t) * s + ry * math.sin(t) * c) for t in [2 * math.pi * i / n for i in range(n)]])
    def m_line(self, pts, width):
        def fn(d):
            q = [(x * S, y * S) for x, y in pts]; r = width * S / 2.0
            d.line(q, fill=255, width=max(int(round(width * S)), 1), joint='curve')
            for x, y in q: d.ellipse([x - r, y - r, x + r, y + r], fill=255)
        return self.mask(fn)

    @staticmethod
    def round(mask, r):
        """the solid's profile: 0 at the edge of the mask, 1 inside, rounded over about r canvas pixels (a quarter-round bevel; shapes smaller than r become domes)"""
        g = blur(mask, r * S * 0.55)
        t = np.clip((g - 0.5) * 2.0, 0, 1)
        return np.sqrt(1 - (1 - t) ** 2) * (mask > 0.02)

    # ---- building ------------------------------------------------------------------------------------------------------------------------------------------
    def add(self, mask, height, r, mat=None, mode='max'):
        """a solid of `height` with edges rounded over r. 'max': joined to what is there as a union (a crease where two solids meet); 'add': laid on top of the surface; 'cut': carved out of it"""
        prof = self.round(mask, r) * height
        if mode == 'max':
            if mat is not None: self.mat[prof > np.maximum(self.H, 0.03 * height)] = mat
            self.H = np.maximum(self.H, prof)
        elif mode == 'add':
            if mat is not None: self.mat[prof > 0.03 * height] = mat
            self.H = self.H + prof
        else: self.H = self.H - prof
        self.solid = np.maximum(self.solid, np.clip(blur(mask, 0.3 * S), 0, 1)) if mode != 'cut' else self.solid
        return prof

    # ---- light ---------------------------------------------------------------------------------------------------------------------------------------------
    def intensity(self, ao=0.16, ao_r=2.2, soften=0.35):
        H = blur(self.H, soften * S) * S                      # heights in samples, so that slopes are true
        gy, gx = np.gradient(H)
        n = np.dstack([-gx, -gy, np.ones_like(H)]); n /= np.linalg.norm(n, axis=2, keepdims=True)
        dif = np.clip(n @ LIGHT, 0, 1); spec = np.clip(n @ HALF, 0, 1) ** 28
        occ = np.clip(blur(H, ao_r * S) - H, 0, None) / S     # how far a point lies below its surroundings, in canvas pixels
        return AMB + KD * dif + KS * spec - ao * occ

def ramp(I, stops):
    xs = [s[0] for s in stops]; out = np.zeros(I.shape + (3,))
    for c in range(3): out[..., c] = np.interp(I, xs, [s[1][c] for s in stops])
    return out

def colorize(f, I, gain=1.0, bias=0.0):
    """the lit field as colors: brightness to the material's ramp; the flat plate's brightness lands on the plate's own green"""
    t = np.clip(0.53 + (I - FLAT) * gain + bias, 0, 1)
    rgb = ramp(t, GREEN)
    for m, stops, lift in ((1, GOLD, 0.06), (2, PALE, 0.2), (3, GREEN, 0.17), (4, GREEN, 0.07), (5, TAN, 0.08)):          # 3: the pictures, a lighter jade than the face (as the original's); 4: the stair's treads
        sel = f.mat == m
        if sel.any(): rgb[sel] = ramp(np.clip(t + lift, 0, 1), stops)[sel]
    return rgb

def to_image(rgb, alpha):
    a = np.dstack([np.clip(rgb, 0, 255), np.clip(alpha, 0, 1)[..., None] * 255]).astype(np.uint8)
    return Image.fromarray(a, 'RGBA')

def down(im, k):
    """the 6-sample picture averaged to scale k (1, 2 or 3), premultiplied so that edges do not darken"""
    a = np.asarray(im).astype(float); al = a[..., 3:4] / 255.0
    pm = np.dstack([a[..., :3] * al, a[..., 3:4]])
    f = S // k; h, w = pm.shape[0] // f, pm.shape[1] // f
    pm = pm[:h * f, :w * f].reshape(h, f, w, f, 4).mean(axis=(1, 3))
    al = pm[..., 3:4] / 255.0
    rgb = np.where(al > 1e-6, pm[..., :3] / np.maximum(al, 1e-6), 0)
    return Image.fromarray(np.dstack([rgb, pm[..., 3:4]]).round().clip(0, 255).astype(np.uint8), 'RGBA')

def emboss_overlay(f, I, tint=(22, 104, 76), tint_a=0.30, light_a=1.5, dark_a=1.9, shadow=0.0):
    """a relief that belongs to the plate itself (the ornament ants): only the light and the shade it adds, laid over the plate, and a faint deeper green on the bodies"""
    d = I - FLAT
    la = np.clip(d * light_a, 0, 0.8); da = np.clip(-d * dark_a, 0, 0.8); ta = f.solid * tint_a
    # composite the three translucent layers into one RGBA picture (tint under, then shade, then light)
    col = np.zeros(I.shape + (3,)); al = np.zeros(I.shape)
    for c, a in (((np.array(tint, float)), ta), (np.array((4, 26, 17), float), da), (np.array((165, 228, 200), float), la)):
        col = (c[None, None, :] * a[..., None] + col * (al * (1 - a))[..., None]) / np.maximum(a + al * (1 - a), 1e-6)[..., None]
        al = a + al * (1 - a)
    return to_image(col, al)
