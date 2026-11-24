"""Packs the drawn pictures into the sheets that the game reads: one PNG sheet for each scale (sheet_1x.png, sheet_2x.png, sheet_3x.png) and the manifest `world_art.txt`.

The format of the files (the records, the one-pixel ring of edge pixels round each picture, the sheet widths) and the layout rule are in README.md, "The files" (their one home).
The pictures are read from the folder that draw_all.py wrote (`<folder>/<scale>x/sprite_NNNN[_vK].png`); one that is not the original sprite's size times its scale stops the packer.
"""
import importlib.util
import os

import numpy as np
from PIL import Image

from catalog import sprite_size

HERE = os.path.dirname(os.path.abspath(__file__))
SCALES = (1, 2, 3)
SHEET_WIDTH = {1: 1024, 2: 2048, 3: 3072}
MAX_SIDE = 4096                  # what every graphics card holds
RING = 1                         # pixels of the picture's own edge repeated round it, at every scale


def _png_writer():
    """the HUD art's PNG writer (libdeflate when it is installed, else Pillow alone), loaded by its path so that the two tools' module names never meet"""
    path = os.path.join(HERE, '..', 'hud_art', 'png_write.py')
    spec = importlib.util.spec_from_file_location('hud_art_png_write', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.write_png


def picture_name(number, variant=0):
    return 'sprite_%04d.png' % number if variant == 0 else 'sprite_%04d_v%d.png' % (number, variant)


def scan(folder):
    """the sorted (number, variant) of every picture in the 1x folder of a drawn set"""
    keys = []
    for name in os.listdir(os.path.join(folder, '1x')):
        stem = name[:-4] if name.endswith('.png') else None
        if stem is None or not stem.startswith('sprite_'):
            continue
        number, _, variant = stem[7:].partition('_v')
        keys.append((int(number), int(variant) if variant else 0))
    return sorted(keys)


def shelves(cells, width):
    """next-fit shelf packing of [(key, w, h)] (in the order given) into rows `width` wide: ({key: (x, y)}, the height used)"""
    spots = {}
    x = y = row = 0
    for key, w, h in cells:
        if w > width:
            raise SystemExit('the picture %s is wider than the sheet (%d pixels)' % (key, width))
        if x + w > width:
            y += row
            x = row = 0
        spots[key] = (x, y)
        x += w
        row = max(row, h)
    return spots, y + row


def pack(folder, out_dir, keys=None):
    """reads the pictures of a drawn set (`folder`/<scale>x/sprite_NNNN[_vK].png), writes the three sheets and the manifest into out_dir, returns {file name: size in bytes}"""
    write_png = _png_writer()
    keys = scan(folder) if keys is None else sorted(keys)
    sizes = {number: sprite_size(number) for number, _ in keys}
    order = sorted(keys, key=lambda k: (-sizes[k[0]][1], k[0], k[1]))
    pictures = {}                                  # (scale, key) -> RGBA array
    for scale in SCALES:
        for key in keys:
            path = os.path.join(folder, '%dx' % scale, picture_name(*key))
            with Image.open(path) as im:
                a = np.asarray(im.convert('RGBA'))
            w, h = sizes[key[0]]
            if a.shape[:2] != (h * scale, w * scale):
                raise SystemExit('%s is %d x %d, not the original sprite\'s %d x %d times %d' % (path, a.shape[1], a.shape[0], w, h, scale))
            pictures[(scale, key)] = a
    os.makedirs(out_dir, exist_ok=True)
    places, sheet_sizes, files = {}, {}, {}
    for scale in SCALES:
        cells = [(key, sizes[key[0]][0] * scale + 2 * RING, sizes[key[0]][1] * scale + 2 * RING) for key in order]
        spots, height = shelves(cells, SHEET_WIDTH[scale])
        if height > MAX_SIDE:
            raise SystemExit('the %dx sheet would be %d pixels high (the limit is %d)' % (scale, height, MAX_SIDE))
        sheet = np.zeros((height, SHEET_WIDTH[scale], 4), np.uint8)
        for key in keys:
            x, y = spots[key]
            a = pictures[(scale, key)]
            sheet[y:y + a.shape[0] + 2 * RING, x:x + a.shape[1] + 2 * RING] = np.pad(a, ((RING, RING), (RING, RING), (0, 0)), mode='edge')
            places[(scale, key)] = (x + RING, y + RING)
        files[scale] = 'sheet_%dx.png' % scale
        sheet_sizes[scale] = (SHEET_WIDTH[scale], height)
        write_png(os.path.join(out_dir, files[scale]), sheet)
    lines = ['ants-world-art 1']
    lines += ['sheet %d %s %d %d' % ((scale, files[scale]) + sheet_sizes[scale]) for scale in SCALES]
    for key in keys:
        w, h = sizes[key[0]]
        lines.append('sprite %d %d %d %d %s' % (key[0], key[1], w, h, ' '.join('%d %d' % places[(scale, key)] for scale in SCALES)))
    with open(os.path.join(out_dir, 'world_art.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    return {name: os.path.getsize(os.path.join(out_dir, name)) for name in list(files.values()) + ['world_art.txt']}
