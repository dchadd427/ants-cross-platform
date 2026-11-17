#!/usr/bin/env python3
"""Makes the HUD art of the game from the drawings in this folder (README.md).

usage: make_hud_art.py [--out DIR] [--work DIR] [--chrome PROGRAM]

    python3 tools/hud_art/make_hud_art.py                  writes assets/hud_art/sheet_1x.png, sheet_2x.png, sheet_3x.png and hud_art.txt

Needs Python 3 with Pillow and numpy, and Google Chrome or Chromium (headless; it renders the SVG pieces). About half a minute. The scratch files go to --work (default: a folder of the
system's temporary folder, or ANTS_HUD_ART_WORK); nothing else is written.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import common  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', default=os.path.join(common.ROOT, 'assets', 'hud_art'), help='the folder of the sheets and the manifest')
    ap.add_argument('--work', default=None, help='the folder of the scratch files')
    ap.add_argument('--chrome', default=None, help="the browser's program")
    args = ap.parse_args()
    common.configure(work=args.work, chrome=args.chrome)
    import sprites
    import pack
    images, rows, anims = sprites.export()
    sizes = pack.pack(images, rows, anims, args.out)
    for name, size in sizes.items():
        print('%-14s %9d bytes' % (name, size))
    print('%d sprites, %d animations' % (len(rows), len(anims)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
