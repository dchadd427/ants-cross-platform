"""The 2x pictures of the lettering are not stored: the game makes them from the 3x ones when it loads the 2x sheet (include/ants_app/hud_art.hpp, `derive2x`). This is the same arithmetic in Python, so that
the generator packs what the game will make and can say how far it is from a picture drawn at 2x.

Every 3 x 3 pixels of the 3x picture give 2 x 2 pixels of the 2x one: an output pixel covers 3 / 2 source pixels along each axis, so the source pixels are weighted (2, 1, 0) for the first output pixel
and (0, 1, 2) for the second, and the average is taken over premultiplied color (a clear pixel counts for nothing). All whole numbers, rounded half up: the game and this file agree to the level.
"""
import numpy as np
from PIL import Image

WEIGHTS = np.array([[2, 1, 0], [0, 1, 2]], np.int64)       # [output pixel of the pair][source pixel of the three]


def two_thirds(pixels):
    """RGBA array (h, w, 4) with h and w multiples of 3 -> RGBA array (h * 2 / 3, w * 2 / 3, 4)"""
    a = np.asarray(pixels, np.uint8).astype(np.int64)
    h, w = a.shape[:2]
    assert h % 3 == 0 and w % 3 == 0, 'the 3x picture is a whole number of 3 x 3 blocks'
    blocks = a.reshape(h // 3, 3, w // 3, 3, 4)                       # [by, sy, bx, sx, c]
    alpha = blocks[..., 3]
    out = np.zeros((h // 3, 2, w // 3, 2, 4), np.uint8)
    # total weight of an output pixel is 9; premultiplied sums for the color, plain sums for alpha
    for oy in range(2):
        for ox in range(2):
            wgt = WEIGHTS[oy][:, None] * WEIGHTS[ox][None, :]         # [sy, sx]
            wa = wgt[None, :, None, :] * alpha                        # [by, sy, bx, sx]
            total = wa.sum(axis=(1, 3))                               # sum of weight * alpha, up to 9 * 255
            for c in range(3):
                s = (wa * blocks[..., c]).sum(axis=(1, 3))
                out[:, oy, :, ox, c] = np.where(total > 0, (2 * s + total) // (2 * np.maximum(total, 1)), 0)
            out[:, oy, :, ox, 3] = (2 * total + 9) // 18
    return out.reshape(h // 3 * 2, w // 3 * 2, 4)


def derive(pic):
    """the 2x picture (PIL RGBA) made from the 3x one"""
    return Image.fromarray(two_thirds(np.asarray(pic.convert('RGBA'))), 'RGBA')
