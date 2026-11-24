#!/usr/bin/env python3
"""Says whether the generator makes the sheets that the repository holds (README.md): it builds them again in a folder of its own and compares them with assets/hud_art/, byte for byte.

usage: check_sheets.py [--assets DIR] [--work DIR] [--chrome PROGRAM] [--strict]

    python3 tools/hud_art/check_sheets.py          about a minute; exit 0 when the manifest and the three sheets are the same files, 1 when they are not

Needs what make_hud_art.py needs (Python 3 with Pillow and numpy, Google Chrome or Chromium), so it is not part of the tests that CI runs: run it after a change to the generator, and before the sheets are
committed. The drawing is deterministic on one machine with one browser; a different browser can draw a pixel differently, and a machine without libdeflate writes the same pixels in larger files (README.md,
"Size"): when only the compression differs, the check says so and passes, and `--strict` makes it fail.
"""
import argparse
import os
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
FILES = ('hud_art.txt', 'sheet_1x.png', 'sheet_2x.png', 'sheet_3x.png')


def read(path):
    with open(path, 'rb') as f:
        return f.read()


def same_pixels(a, b):
    """True when two PNG files hold the same 8-bit RGBA picture"""
    x = np.asarray(Image.open(a).convert('RGBA'))
    y = np.asarray(Image.open(b).convert('RGBA'))
    return x.shape == y.shape and bool(np.array_equal(x, y))


def compare(assets, built, strict=False):
    """(exit code, lines of the report) for the files of `assets` against the ones in `built`"""
    lines = []
    code = 0
    for name in FILES:
        have, made = os.path.join(assets, name), os.path.join(built, name)
        if not os.path.isfile(made):
            lines.append('%-14s the generator did not write it' % name)
            code = 1
        elif not os.path.isfile(have):
            lines.append('%-14s the repository has no such file' % name)
            code = 1
        elif read(have) == read(made):
            lines.append('%-14s the same bytes (%d)' % (name, os.path.getsize(have)))
        elif name.endswith('.png') and same_pixels(have, made):
            lines.append('%-14s the same pixels, other bytes (%d in the repository, %d built): the compression differs, libdeflate is not the one that made the repository\'s' % (name, os.path.getsize(have), os.path.getsize(made)))
            code = 1 if strict else code
        else:
            lines.append('%-14s DIFFERENT (%d in the repository, %d built)' % (name, os.path.getsize(have), os.path.getsize(made)))
            code = 1
    return code, lines


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--assets', default=os.path.join(ROOT, 'assets', 'hud_art'), help='the folder that holds the sheets to check')
    ap.add_argument('--work', default=None, help='the folder of the scratch files (default: a folder of the system\'s temporary folder)')
    ap.add_argument('--chrome', default=None, help="the browser's program")
    ap.add_argument('--strict', action='store_true', help='fail when only the compression differs')
    args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='ants_hud_art_check_') as tmp:
        built = os.path.join(tmp, 'sheets')
        cmd = [sys.executable, os.path.join(HERE, 'make_hud_art.py'), '--out', built, '--work', args.work or os.path.join(tmp, 'work')]
        if args.chrome:
            cmd += ['--chrome', args.chrome]
        done = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        if done.returncode != 0:
            print('the generator stopped (%d):\n%s' % (done.returncode, done.stderr[-2000:]), file=sys.stderr)
            return 2
        code, lines = compare(args.assets, built, args.strict)
    print('\n'.join(lines))
    print('the generator makes the repository\'s sheets' if code == 0 else 'the generator does not make the repository\'s sheets: build them again (make_hud_art.py) and commit the result, or find what differs')
    return code


if __name__ == '__main__':
    sys.exit(main())
