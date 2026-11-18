#!/usr/bin/env python3
"""Renders the two hands of the cursors in Blender and writes their sprites (README.md).

usage: make_hands.py [--blender PROGRAM]

    python3 tools/hud_art/make_hands.py --blender /path/to/blender      writes hands/c_select_{1,2,3}x.png and hands/c_scroll_{1,2,3}x.png

This step is optional: the sprites are committed, and make_hud_art.py only reads them. Run it when hands/glove_fit.py changes. Needs Blender 4.x or newer (headless: `blender -b --python`), Pillow and numpy.
`--blender` or ANTS_BLENDER names the program. The render is at 12 samples for each canvas pixel; each scale is the mean of blocks of it (crisp.downscale).
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__)) + '/'
sys.path.insert(0, HERE)

from crisp import downscale  # noqa: E402

HANDS = ('c_select', 'c_scroll')
SAMPLES = 12


def blender(given):
    for c in (given, os.environ.get('ANTS_BLENDER'), '/Applications/Blender.app/Contents/MacOS/Blender', shutil.which('blender')):
        if c and os.path.isfile(c) and os.access(c, os.X_OK):
            return c
    raise SystemExit('no Blender found: pass --blender PROGRAM or set ANTS_BLENDER')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--blender', default=None, help="Blender's program")
    args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='ants_hands_') as work:
        subprocess.run([blender(args.blender), '-b', '--python', HERE + 'hands/glove_fit.py', '--', work], check=True, stdout=subprocess.DEVNULL)
        for hand in HANDS:
            render = Image.open(os.path.join(work, '%s_%dx.png' % (hand, SAMPLES))).convert('RGBA')
            for k in (1, 2, 3):
                downscale(render, SAMPLES // k).save(HERE + 'hands/%s_%dx.png' % (hand, k), optimize=True)
            print(hand, 'written at 1x, 2x and 3x')
    return 0


if __name__ == '__main__':
    sys.exit(main())
