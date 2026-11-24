#!/usr/bin/env python3
"""Makes the world art of the game: the ground tiles, the edge tiles and the bridge, drawn at 1x, 2x and 3x and packed into the sheets that the game reads (README.md).

usage: make_world_art.py [--out DIR] [--work DIR]

    python3 tools/world_art/make_world_art.py              writes assets/world_art/sheet_1x.png, sheet_2x.png, sheet_3x.png and world_art.txt

Needs Python 3 with Pillow and numpy; the libdeflate library is optional (sheets 5% smaller: tools/hud_art/png_write.py). The drawing is deterministic. `--work DIR` keeps the drawn pictures
(<DIR>/<scale>x/sprite_NNNN.png); without it they go to a temporary folder that is removed at the end.
"""
import argparse
import os
import shutil
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..'))

import draw_all  # noqa: E402
import library_guard  # noqa: E402  (no tool writes into the map library)
import pack  # noqa: E402
from catalog import ROOT  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', default=os.path.join(ROOT, 'assets', 'world_art'), help='the folder of the sheets and the manifest')
    ap.add_argument('--work', default=None, help='the folder that keeps the drawn pictures (default: a temporary folder)')
    args = ap.parse_args()
    try:
        library_guard.require_writable(args.out)
        if args.work:
            library_guard.require_writable(args.work)
    except library_guard.LibraryRefused as err:
        print(err, file=sys.stderr)
        return 2
    started = time.time()
    work = args.work or tempfile.mkdtemp(prefix='ants_world_art_')
    try:
        keys = draw_all.draw_all(work)
        print('drawn: %d pictures at 1x, 2x and 3x in %.0f s' % (len(keys), time.time() - started), flush=True)
        sizes = pack.pack(work, args.out, keys)
    finally:
        if not args.work:
            shutil.rmtree(work, ignore_errors=True)
    for name, size in sizes.items():
        print('%-14s %9d bytes' % (name, size))
    print('%-14s %9d bytes (%.0f s in all)' % ('total', sum(sizes.values()), time.time() - started))
    return 0


if __name__ == '__main__':
    sys.exit(main())
