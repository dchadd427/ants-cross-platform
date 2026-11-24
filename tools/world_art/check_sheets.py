#!/usr/bin/env python3
"""Says whether the generator makes the sheets of the world art's first page that the repository holds (README.md): it draws them again in a folder of its own and compares them with assets/world_art/,
byte for byte: the sheets of page 0 and the manifest's records of that page. The other pages (the grass and the plants, made outside this repository) are not drawn by the generator and are not compared.

usage: check_sheets.py [--assets DIR] [--strict]

    python3 tools/world_art/check_sheets.py        about a minute; exit 0 when page 0 is the same files, 1 when it is not

The comparison is tools/art_sheets.py's, the same as the HUD art's (tools/hud_art/check_sheets.py). Needs what make_world_art.py needs (Python 3 with Pillow and numpy), so it is not part of the tests that
CI runs: run it after a change to the generator, and before the sheets are committed. The drawing is deterministic on one machine; when only the compression differs (a machine without libdeflate) the
check says so and passes, and `--strict` makes it fail.
"""
import argparse
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import art_sheets  # noqa: E402  (the one home of how a pack is split over sheets, and of this comparison)

MANIFEST = 'world_art.txt'


def first_page(line):
    """whether a line of the manifest is the header or belongs to the first page: its `sheet 0 ...` records and its `sprite <number> <variant> 0 ...` records"""
    parts = line.split()
    return bool(parts) and (parts[0] == 'ants-world-art' or (parts[0] == 'sheet' and len(parts) > 1 and parts[1] == '0') or (parts[0] == 'sprite' and len(parts) > 3 and parts[3] == '0'))


def compare(assets, built, strict=False):
    """(exit code, lines of the report) for page 0 of `assets` against the ones in `built` (art_sheets.compare)"""
    return art_sheets.compare(assets, built, MANIFEST, strict, keep=first_page, pages={0})


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--assets', default=os.path.join(ROOT, 'assets', 'world_art'), help='the folder that holds the sheets to check')
    ap.add_argument('--strict', action='store_true', help='fail when only the compression differs')
    args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='ants_world_art_check_') as tmp:
        built = os.path.join(tmp, 'sheets')
        done = subprocess.run([sys.executable, os.path.join(HERE, 'make_world_art.py'), '--out', built], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        if done.returncode != 0:
            print('the generator stopped (%d):\n%s' % (done.returncode, done.stderr[-2000:]), file=sys.stderr)
            return 2
        code, lines = compare(args.assets, built, args.strict)
    print('\n'.join(lines))
    print('the generator makes the repository\'s first page' if code == 0 else 'the generator does not make the repository\'s first page: draw it again (make_world_art.py) and commit the result, or find what differs')
    return code


if __name__ == '__main__':
    sys.exit(main())
