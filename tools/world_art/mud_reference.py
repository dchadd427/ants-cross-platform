#!/usr/bin/env python3
"""Writes the golden file of the dense mud: the reference values that the game's C++ port of the mud fields (`mud_fields.py`, README.md) is compared with.

usage: mud_reference.py [--out FILE]

    python3 tools/world_art/mud_reference.py            writes tests/data/world_art_mud_golden.txt (about 10 s; needs numpy)

The values are those of the approved math (mud_fields.py) on one small synthetic map, 24 columns x 20 rows of cells (768 x 640 world pixels), all plain mud except a block of 3 x 3 cells, a column of
12 cells and a block of 2 x 2 cells; the border of the map is not a bank. The file is plain text, one record a line, every number with nine decimals (the integers are whole numbers):

    ants-world-art-mud-golden 1
    mask <24 characters>                                                             20 rows of the map: # plain mud, . not
    hash <ix> <iy> <seed> <value>                                                    the lattice hash, ix and iy whole numbers, some of them negative
    vnoise <x> <y> <seed> <value>                                                    the value noise at a point in lattice units
    fbm <x> <y> <wavelength> <seed> <octaves> <gain> <stretch0> <stretch1> <angle> <value>        the fractal noise with the parameters that mud_fields.py really uses
    depth <x> <y> <tiles>                                                            the depth in tiles at a world point (bilinear on the grid of 4 pixels)
    celldepth <col> <row> <tiles>                                                    the depth at the center of a cell
    wetpatch <x> <y> <mult r g b> <add r g b>                                        looks 1 and 4 at ONE sample at a world point (a pixel's center): the colors' factor and the levels added
    ripple_median <value>                                                            the median of the ripple field's gradient (|grad phi|) over the world square 0 to 2048 at 2 samples a pixel

Nothing in it depends on the platform but the last digits of numpy's functions, far below nine decimals.
"""
import argparse
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..'))

import library_guard  # noqa: E402  (no tool writes into the map library)
import mud_fields as M  # noqa: E402
from catalog import ROOT  # noqa: E402

HEADER = 'ants-world-art-mud-golden 1'
OUT = os.path.join(ROOT, 'tests', 'data', 'world_art_mud_golden.txt')
COLUMNS, ROWS = 24, 20

# the sample points; the lists are fixed, so that the file is the same on every run
HASHES = [(0, 0, 7), (1, 0, 7), (-1, 0, 7), (0, -1, 13), (-5, -7, 91), (12, 34, 41), (-100, 250, 55), (3000, -2500, 56), (17, 17, 21), (-1, -1, 77), (2147, 1800, 63), (-3, 5, 122)]
VNOISES = [(0.0, 0.0, 7), (0.5, 0.5, 7), (1.25, 3.75, 13), (-0.5, 2.5, 41), (-3.3, -4.7, 91), (7.123456789, 2.987654321, 55), (10.0, 10.0, 21), (0.999999999, 0.000000001, 77),
           (5.5, -0.25, 63), (-12.75, 8.125, 122), (100.33, 50.66, 7), (2.0, 3.0, 13)]
PLAIN = ((1.0, 1.0), 0.0)
FBMS = [  # (x, y, wavelength, seed, octaves, gain, (stretch0, stretch1), angle): the sets of fields.py (the depth's wander, the wet darkness, the sheen, the patches, the ripple's warp, the ripple, its cover and its breaks)
    (100.5, 200.25, 190.0, 91, 2, 0.5, (1.0, 1.0), 0.0), (400.0, 320.0, 260.0, 7, 3, 0.5, (1.0, 1.0), 0.0), (50.75, 600.5, 260.0, 13, 3, 0.5, (1.7, 1.0), -24.0),
    (700.125, 40.0, 320.0, 41, 2, 0.5, (1.0, 1.0), 0.0), (333.5, 333.5, 210.0, 55, 2, 0.5, (1.0, 1.0), 0.0), (12.0, 500.0, 210.0, 56, 2, 0.5, (1.0, 1.0), 0.0),
    (256.0, 128.0, 300.0, 21, 3, 0.55, (1.0, 1.0), 0.0), (600.0, 600.0, 340.0, 77, 2, 0.5, (1.0, 1.0), 0.0), (0.5, 0.5, 110.0, 63, 2, 0.5, (1.0, 1.0), 0.0),
    (767.5, 639.5, 190.0, 91, 2, 0.5, (1.0, 1.0), 0.0), (-25.5, 12.25, 300.0, 21, 3, 0.55, (1.0, 1.0), 0.0), (180.0, -40.0, 260.0, 13, 3, 0.5, (1.7, 1.0), -24.0),
    (480.0, 160.75, 260.0, 7, 3, 0.5, (1.0, 1.0), 0.0), (800.0, 700.0, 320.0, 41, 2, 0.5, (1.0, 1.0), 0.0), (90.0, 90.0, 110.0, 63, 2, 0.5, (1.0, 1.0), 0.0), (222.5, 444.5, 340.0, 77, 2, 0.5, (1.0, 1.0), 0.0),
]
CELLS = [(5, 5), (4, 4), (15, 7), (14, 7), (23, 0), (0, 19), (10, 10), (12, 10), (20, 16), (19, 15), (3, 3), (0, 0), (7, 12), (16, 19), (22, 8), (11, 2), (8, 2), (13, 5), (17, 18), (9, 13)]
DEPTH_POINTS = [(0.0, 0.0), (768.0, 640.0), (0.0, 640.0), (768.0, 0.0), (176.0, 176.0), (496.0, 200.0), (2.0, 320.0), (766.0, 320.0)]      # the corners and edges of the map (the grid clamps there), a bank's inside and a column
WETPATCH_PIXELS = [   # pixels whose depth runs from 0 through the starts of the looks (1.5 the patches, 1.8 the darkness, 3 the sheen, 5 the full strength) to 7 and above (the wander's depth counts), then the corners and a few others
    (659, 522), (465, 313), (126, 99), (536, 228), (436, 268), (439, 58), (756, 567), (272, 212), (68, 199), (567, 412), (596, 596), (401, 395), (566, 4), (600, 370), (607, 76), (599, 204),
    (528, 589), (396, 552), (39, 14), (174, 391), (711, 328), (271, 383), (674, 167), (0, 369), (716, 42), (315, 622), (742, 184), (507, 506), (431, 488), (65, 100), (141, 19), (340, 382),
    (0, 0), (767, 0), (0, 639), (767, 639), (176, 176), (496, 200), (300, 150), (100, 500),
]
RIPPLE_SIZE = 2048                       # the world square of the median, from 0 and as many world pixels each way
RIPPLE_SAMPLES = 2                       # samples for each world pixel, no more


def synthetic_mask():
    """the map: all plain mud but the 3 x 3 cells at columns 4 to 6, rows 4 to 6, the column 15 at rows 2 to 13 and the 2 x 2 cells at columns 20 and 21, rows 15 and 16"""
    mask = np.ones((ROWS, COLUMNS), bool)
    mask[4:7, 4:7] = False
    mask[2:14, 15] = False
    mask[15:17, 20:22] = False
    return mask


def ripple_median():
    """the median of |grad phi| of the ripple field over the world square [0, 2048)^2 at 2 samples for each world pixel (4096 x 4096), the gradient by np.gradient with the sample spacing, as the looks measure it; taken
    a band of rows at a time (each with the row above and below it, so that every gradient is the one of the whole square)"""
    n, side = RIPPLE_SAMPLES, RIPPLE_SIZE * RIPPLE_SAMPLES
    coords = (np.arange(side) + 0.5) / n
    band = 256
    parts = []
    for a in range(0, side, band):
        b = min(a + band, side)
        r0, r1 = max(a - 1, 0), min(b + 1, side)
        X, Y = np.meshgrid(coords, coords[r0:r1])
        gy, gx = np.gradient(M.warped_phi(X, Y), 1.0 / n)
        parts.append(np.hypot(gx, gy)[a - r0:b - r0])
    return float(np.median(np.concatenate(parts)))


def fmt(v):
    """a number with nine decimals (a minus zero is a zero)"""
    s = '%.9f' % v
    return '0.000000000' if s == '-0.000000000' else s


def records(median=None):
    """the lines of the file, without the header; `median` (the ripple field's) is computed when it is not given"""
    mask = synthetic_mask()
    D = M.depth_grid(mask)
    out = ['mask ' + ''.join('#' if v else '.' for v in row) for row in mask]
    for ix, iy, seed in HASHES:
        out.append('hash %d %d %d %s' % (ix, iy, seed, fmt(M._hash(np.array(ix), np.array(iy), seed))))
    for x, y, seed in VNOISES:
        out.append('vnoise %s %s %d %s' % (fmt(x), fmt(y), seed, fmt(M.vnoise(np.array(x), np.array(y), seed))))
    for x, y, wavelength, seed, octaves, gain, stretch, angle in FBMS:
        value = M.fbm(np.array(x), np.array(y), wavelength, seed, octaves, gain, stretch, angle)
        out.append('fbm %s %s %s %d %d %s %s %s %s %s' % (fmt(x), fmt(y), fmt(wavelength), seed, octaves, fmt(gain), fmt(stretch[0]), fmt(stretch[1]), fmt(angle), fmt(value)))
    rs = np.random.RandomState(20260)
    points = DEPTH_POINTS + [(rs.randint(0, 768 * 4) / 4.0, rs.randint(0, 640 * 4) / 4.0) for _ in range(40 - len(DEPTH_POINTS))]      # quarters of a pixel: exact in binary and in nine decimals
    for x, y in points:
        out.append('depth %s %s %s' % (fmt(x), fmt(y), fmt(M.depth_at(D, np.array(x), np.array(y)))))
    for col, row in CELLS:
        out.append('celldepth %d %d %s' % (col, row, fmt(M.cell_depth(D, col, row))))
    for px, py in WETPATCH_PIXELS:
        x, y = px + 0.5, py + 0.5                                              # a pixel's center
        looks = M.looks_at(D, np.array([[x]]), np.array([[y]]), ('wet', 'patch'))
        out.append('wetpatch %s %s %s %s' % (fmt(x), fmt(y), ' '.join(fmt(v) for v in looks['mult'][0, 0]), ' '.join(fmt(v) for v in looks['add'][0, 0])))
    out.append('ripple_median ' + fmt(ripple_median() if median is None else median))
    return out


def text(median=None):
    return '\n'.join([HEADER] + records(median)) + '\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', default=OUT, help='the file to write (default: tests/data/world_art_mud_golden.txt)')
    args = ap.parse_args()
    try:
        library_guard.require_writable(args.out)
    except library_guard.LibraryRefused as err:
        print(err, file=sys.stderr)
        return 2
    content = text()
    with open(args.out, 'w', encoding='utf-8', newline='\n') as f:
        f.write(content)
    print('%s: %d records' % (os.path.basename(args.out), content.count('\n') - 1))
    return 0


if __name__ == '__main__':
    sys.exit(main())
