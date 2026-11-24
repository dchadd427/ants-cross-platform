#!/usr/bin/env python3
"""Packs pictures that were made outside this generator (the grass and the plants, later the hills) into a page of the world art.

usage: pack_pictures.py [--out DIR] [--page N] [--name NAME] FOLDER

    python3 tools/world_art/pack_pictures.py --page 1 --name plants FOLDER

FOLDER holds `1x/`, `2x/` and `3x/` with one `sprite_NNNN.png` for each picture (NNNN is the original sprite's number; each picture the original sprite's size times its scale, 8-bit RGBA with its
own alpha). They are packed into the sheets `NAME_1x.png`, `NAME_2x.png` and `NAME_3x.png` of page N of `assets/world_art/` (or --out; when they do not fit one page the rest go on the next, as
`NAME2_1x.png` ... and the page after N: tools/art_sheets.py), and the pages' records replace their old ones in `world_art.txt`; the other pages of the manifest stay as they are (make_world_art.py
draws page 0). The pictures themselves are not kept in the repository: the sheets are their only copy. The format and the layout rule
are in README.md, "The files".

Needs Python 3 with Pillow and numpy; the libdeflate library is optional (sheets 5% smaller: tools/hud_art/png_write.py).
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..'))

import library_guard  # noqa: E402  (no tool writes into the map library)
import pack  # noqa: E402
from catalog import ROOT  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('folder', help='the folder of the pictures: 1x/, 2x/ and 3x/ with sprite_NNNN.png in each')
    ap.add_argument('--out', default=os.path.join(ROOT, 'assets', 'world_art'), help='the folder of the sheets and the manifest')
    ap.add_argument('--page', type=int, default=1, help='the page to make (1, 2, 3; page 0 is the generator\'s)')
    ap.add_argument('--name', default='plants', help='the first part of the sheets\' file names')
    args = ap.parse_args()
    if not 1 <= args.page <= 3:
        print('the page is 1, 2 or 3 (page 0 is drawn by make_world_art.py)', file=sys.stderr)
        return 2
    if not args.name.isalnum() or args.name == 'sheet':
        print('the name is letters and digits, and not "sheet" (the first page\'s)', file=sys.stderr)
        return 2
    try:
        library_guard.require_writable(args.out)
    except library_guard.LibraryRefused as err:
        print(err, file=sys.stderr)
        return 2
    keys = pack.scan(args.folder)
    sizes = pack.pack(args.folder, args.out, page=args.page, name=args.name, keys=keys)
    print('%d pictures on page %d' % (len(keys), args.page))
    for name, size in sizes.items():
        print('%-16s %9d bytes' % (name, size))
    return 0


if __name__ == '__main__':
    sys.exit(main())
