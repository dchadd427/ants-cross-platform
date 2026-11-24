"""Packs pictures into a PAGE of the sheets that the game reads: one PNG sheet for each scale (page 0: sheet_1x.png, sheet_2x.png, sheet_3x.png; another page: <name>_1x.png ...) and puts the page's
records into the manifest `world_art.txt`, which keeps the records of its other pages as they are.

The format of the files (the pages, the records, the one-pixel ring of edge pixels round each picture, the sheet widths) and the layout rule are in README.md, "The files" (their one home).
The pictures are read from a folder (`<folder>/<scale>x/sprite_NNNN[_vK].png`: draw_all.py writes page 0's, pack_pictures.py reads any other page's); one that is not the original sprite's size times its
scale stops the packer.
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


def read_manifest(out_dir):
    """the manifest of a folder: ({(page, scale): (file, width, height)}, [(number, variant, page, w, h, x1, y1, x2, y2, x3, y3)]); empty when the folder has none"""
    path = os.path.join(out_dir, 'world_art.txt')
    sheets, sprites = {}, []
    if not os.path.exists(path):
        return sheets, sprites
    with open(path, encoding='utf-8') as f:
        lines = f.read().split('\n')
    if lines[0] != 'ants-world-art 2':
        raise SystemExit('%s is not a manifest of format 2 (its first line is "%s")' % (path, lines[0]))
    for line in lines[1:]:
        words = line.split()
        if not words:
            continue
        if words[0] == 'sheet':
            sheets[(int(words[1]), int(words[2]))] = (words[3], int(words[4]), int(words[5]))
        elif words[0] == 'sprite':
            sprites.append(tuple(int(v) for v in words[1:]))
    return sheets, sprites


def write_manifest(out_dir, sheets, sprites):
    """writes the manifest: the sheet rows page by page, then the sprite rows sorted by number and variant; a picture may be on one page only"""
    keys = [(row[0], row[1]) for row in sprites]
    if len(set(keys)) != len(keys):
        raise SystemExit('a picture (number and variant) is on two pages')
    pages = sorted({page for page, _ in sheets})
    if pages != list(range(len(pages))):
        raise SystemExit('the pages are not 0, 1, 2 ... in order: %s' % pages)
    lines = ['ants-world-art 2']
    lines += ['sheet %d %d %s %d %d' % ((page, scale) + sheets[(page, scale)]) for page in pages for scale in SCALES]
    lines += ['sprite ' + ' '.join(str(v) for v in row) for row in sorted(sprites, key=lambda row: (row[0], row[1]))]
    with open(os.path.join(out_dir, 'world_art.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')


def pack(folder, out_dir, page=0, name='sheet', keys=None):
    """reads the pictures of a drawn set (`folder`/<scale>x/sprite_NNNN[_vK].png), writes the three sheets of the page (<name>_<scale>x.png) and the manifest into out_dir (the records of the page
    replace the page's old ones; those of the other pages stay), returns {file name: size in bytes}"""
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
    old_sheets, old_sprites = read_manifest(out_dir)
    sheets = {k: v for k, v in old_sheets.items() if k[0] != page}
    sprites = [row for row in old_sprites if row[2] != page]
    taken = {(row[0], row[1]) for row in sprites}
    for key in keys:
        if key in taken:
            raise SystemExit('the picture %d (variant %d) is on another page already' % key)
    places, files = {}, {}
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
        files[scale] = '%s_%dx.png' % (name, scale)
        sheets[(page, scale)] = (files[scale], SHEET_WIDTH[scale], height)
        write_png(os.path.join(out_dir, files[scale]), sheet)
    for key in keys:
        w, h = sizes[key[0]]
        sprites.append((key[0], key[1], page, w, h) + tuple(v for scale in SCALES for v in places[(scale, key)]))
    write_manifest(out_dir, sheets, sprites)
    return {name_: os.path.getsize(os.path.join(out_dir, name_)) for name_ in list(files.values()) + ['world_art.txt']}
