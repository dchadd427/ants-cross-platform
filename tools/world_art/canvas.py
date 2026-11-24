"""The canvas of the things that lie on the ground (the bomb, the fire wall): a picture drawn on a master of `samples` x `samples` for each world pixel and averaged down to a scale (README.md).

Color is kept multiplied by its opacity, so that the average of a see-through edge has no dark fringe; a pixel that nothing covers is (0, 0, 0, 0). The scale s takes the average of
(samples / s) x (samples / s) samples, so `samples` is a multiple of 1, 2 and 3 (6 or 12).
"""
import numpy as np


class Canvas:
    def __init__(self, w, h, samples):
        self.w, self.h, self.samples = w, h, samples
        c = (np.arange(w * samples) + 0.5) / samples
        r = (np.arange(h * samples) + 0.5) / samples
        self.X, self.Y = np.meshgrid(c, r)                       # the world coordinates of every sample
        self.rgb = np.zeros((h * samples, w * samples, 3))
        self.a = np.zeros((h * samples, w * samples))

    def over(self, color, alpha):
        """lays `color` (one color, or one for every sample) over what is there, with the opacity `alpha` (an array of the master's size, clipped to 0 to 1)"""
        alpha = np.clip(np.asarray(alpha, float), 0.0, 1.0)
        col = np.array(color, float)
        if col.ndim == 1:
            col = col[None, None, :]
        self.rgb = self.rgb * (1 - alpha[..., None]) + col * alpha[..., None]
        self.a = self.a * (1 - alpha) + alpha

    def picture(self, scale):
        """the picture at `scale` (1, 2 or 3) as an RGBA array of uint8: the average of the samples of each pixel, its color divided by its opacity again"""
        f = self.samples // scale
        h, w = self.a.shape
        rgb = self.rgb.reshape(h // f, f, w // f, f, 3).mean(axis=(1, 3))
        a = self.a.reshape(h // f, f, w // f, f).mean(axis=(1, 3))
        out = np.where(a[..., None] > 1e-4, rgb / np.maximum(a[..., None], 1e-4), 0.0)
        return np.dstack([np.clip(np.rint(out), 0, 255), np.clip(np.rint(a * 255), 0, 255)]).astype(np.uint8)

    def pictures(self):
        """{scale: RGBA array} for 1, 2 and 3"""
        return {scale: self.picture(scale) for scale in (1, 2, 3)}
