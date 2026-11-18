"""A hard-edged drawing canvas for the cursors: flat shapes on the pixel grid, drawn again on its own grid for each scale (1x, 2x, 3x), so that every upright and level edge sits on a whole output pixel and
only slanted and round edges are antialiased (8 samples for each output pixel). A shape is a mask (poly, rect, rrect, ellipse), the layers are put on top of one another in order and averaged
down to the output pixels. Sizes are canvas pixels (the original's own); the click point of a cursor is its sprite's place, not something drawn here."""
import math

import numpy as np
from PIL import Image, ImageDraw

SS = 8


class Canvas:
    def __init__(self, w, h, k):
        self.w, self.h, self.k = w, h, k; self.s = k * SS; self.pad = 3 * self.s
        self.size = (w * self.s + 2 * self.pad, h * self.s + 2 * self.pad)
    def _m(self, fn):
        im = Image.new('L', self.size, 0); fn(ImageDraw.Draw(im)); return np.asarray(im).astype(float) / 255.0
    def P(self, x, y): return (x * self.s + self.pad, y * self.s + self.pad)
    def poly(self, pts): return self._m(lambda d: d.polygon([self.P(x, y) for x, y in pts], fill=255))
    def rrect(self, x0, y0, x1, y1, r): return self._m(lambda d: d.rounded_rectangle([self.P(x0, y0), (self.P(x1, y1)[0] - 1, self.P(x1, y1)[1] - 1)], radius=r * self.s, fill=255))
    def rect(self, x0, y0, x1, y1): return self._m(lambda d: d.rectangle([self.P(x0, y0), (self.P(x1, y1)[0] - 1, self.P(x1, y1)[1] - 1)], fill=255))
    def ellipse(self, cx, cy, rx, ry, rot=0):
        n = 64; c, s = math.cos(math.radians(rot)), math.sin(math.radians(rot))
        return self.poly([(cx + rx * math.cos(t) * c - ry * math.sin(t) * s, cy + rx * math.cos(t) * s + ry * math.sin(t) * c) for t in [2 * math.pi * i / n for i in range(n)]])
    def inset(self, mask, d):
        """the shape d canvas pixels inside the mask's edge, hard-edged (straight edges move by exactly d)"""
        sg = d * self.s; p = int(sg * 3) + 2
        b = np.pad(mask, p, mode='constant'); h, w = b.shape
        fy = np.fft.fftfreq(h)[:, None]; fx = np.fft.rfftfreq(w)[None, :]
        g = np.fft.irfft2(np.fft.rfft2(b) * np.exp(-2 * (np.pi ** 2) * (sg ** 2) * (fx ** 2 + fy ** 2)), s=b.shape)[p:-p, p:-p]
        return ((g > 0.8413) & (mask > 0.5)).astype(float)
    def shift(self, m, dx, dy):
        out = np.zeros_like(m); h, w = m.shape; dx, dy = int(round(dx * self.s)), int(round(dy * self.s))
        out[max(dy, 0):h + min(dy, 0), max(dx, 0):w + min(dx, 0)] = m[max(-dy, 0):h + min(-dy, 0), max(-dx, 0):w + min(-dx, 0)]
        return out
    def out(self, layers):
        """layers: [(mask, color or an (h, w, 3) array)], drawn in order, hard-edged at the sample grid; averaged to the output pixels"""
        H, W = self.size[1], self.size[0]
        rgb = np.zeros((H, W, 3)); al = np.zeros((H, W))
        for m, col in layers:
            sel = m > 0.5
            if isinstance(col, tuple): rgb[sel] = col
            else: rgb[sel] = col[sel]
            al[sel] = 1
        p = self.pad; rgb = rgb[p:-p, p:-p]; al = al[p:-p, p:-p]
        h, w = self.h * self.k, self.w * self.k
        a = al.reshape(h, SS, w, SS).mean(axis=(1, 3))
        c = (rgb * al[..., None]).reshape(h, SS, w, SS, 3).mean(axis=(1, 3)) / np.maximum(a, 1e-6)[..., None]
        return Image.fromarray(np.dstack([c, a * 255]).round().clip(0, 255).astype(np.uint8), 'RGBA')
    def grad(self, c0, c1):
        """a color running from c0 at the upper left to c1 at the lower right"""
        H, W = self.size[1], self.size[0]
        yy, xx = np.mgrid[0:H, 0:W]; t = np.clip(((xx - self.pad) / float(self.w * self.s) + (yy - self.pad) / float(self.h * self.s)) / 1.3, 0, 1)[..., None]
        return np.array(c0, float) * (1 - t) + np.array(c1, float) * t


def downscale(im, f):
    """a picture f times smaller (f whole): the mean of each f x f block, colors weighted by alpha"""
    a = np.asarray(im.convert('RGBA')).astype(float)
    al = a[..., 3:4] / 255.0
    pm = np.dstack([a[..., :3] * al, a[..., 3:4]])
    h, w = pm.shape[0] // f, pm.shape[1] // f
    pm = pm[:h * f, :w * f].reshape(h, f, w, f, 4).mean(axis=(1, 3))
    al = pm[..., 3:4] / 255.0
    return Image.fromarray(np.dstack([np.where(al > 1e-6, pm[..., :3] / np.maximum(al, 1e-6), 0), pm[..., 3:4]]).round().clip(0, 255).astype(np.uint8), 'RGBA')
