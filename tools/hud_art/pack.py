"""Packs the rendered sprites into the game's files: one PNG sheet per scale (sheet_1x.png, sheet_2x.png, sheet_3x.png) and the manifest `hud_art.txt`.

The manifest is plain text, one record a line, so that the game reads it without a JSON reader; its format is described once, at the top of include/ants_app/hud_art.hpp. Sprites that are the same
picture at every scale share one place in the sheets (the manifest still names each of them).
"""
import hashlib
import os

import numpy as np

from png_write import write_png

MAX_WIDTH = 1024            # canvas pixels: 3x of it is 3072, under the 4096 that every graphics card holds
PAD = 2                     # canvas pixels of the sprite's own edge repeated round it (so that a scaled copy never picks up a neighbor)


def skyline(sizes, width):
    """bottom-left skyline packing of [(key, w, h)] (in the order given) into a sheet `width` wide: ({key: (x, y)}, the height used)"""
    sky = [[0, 0, width]]                  # the contour: [x, top, w] segments, left to right
    spots = {}
    for key, w, h in sizes:
        best = None
        for i, seg in enumerate(sky):
            x = seg[0]
            if x + w > width:
                break
            y, need, j = 0, w, i
            while need > 0:
                y = max(y, sky[j][1])
                need -= sky[j][2]
                j += 1
            if best is None or (y + h, x) < (best[0], best[2]):
                best = (y + h, y, x)
        _, y, x = best
        spots[key] = (x, y)
        out = []
        for sx, sy, sw in sky:
            if sx + sw <= x or sx >= x + w:
                out.append([sx, sy, sw])
                continue
            if sx < x:
                out.append([sx, sy, x - sx])
            if sx + sw > x + w:
                out.append([x + w, sy, sx + sw - (x + w)])
        out.append([x, y + h, w])
        out.sort()
        sky = [out[0]]
        for seg in out[1:]:
            if seg[1] == sky[-1][1] and sky[-1][0] + sky[-1][2] == seg[0]:
                sky[-1][2] += seg[2]
            else:
                sky.append(seg)
    return spots, max(seg[1] for seg in sky)


def pack(images, rows, anims, out_dir, canvas=(960, 540), cursor_anims=(), derived=()):
    """images: {scale: {name: RGBA picture}}, rows: the catalog's sprite rows, anims: {name: [frame]} (those named in cursor_anims are marked `cursor`: their parts are placed from the click point);
    derived: the names of the sprites whose 2x picture the game makes from the 3x one when it loads the 2x sheet (derive.py: `images` holds that picture, the 2x sheet does not). Those sprites are packed
    first, in the top rows of every sheet, so that the game reads only those rows of the 3x sheet; writes the sheets and the manifest into out_dir and returns the sizes of the files"""
    scales = sorted(images)
    derived = set(derived)
    assert not derived or {2, 3} <= set(scales), 'a derived 2x picture needs the 3x one'
    names = [r['name'] for r in rows]
    # one cell for every different picture
    digest = {}
    cell_of = {}
    for r in rows:
        key = hashlib.sha256((b'derived' if r['name'] in derived else b'') + b''.join(images[k][r['name']].tobytes() for k in scales)).hexdigest()
        cell_of[r['name']] = digest.setdefault(key, r['name'])

    def order(n):
        return (-images[scales[0]][n].height, -images[scales[0]][n].width, n)

    def size_of(n):
        return (n, images[scales[0]][n].width // scales[0] + 2 * PAD, images[scales[0]][n].height // scales[0] + 2 * PAD)
    cells = sorted(set(cell_of.values()), key=order)
    top = [n for n in cells if n in derived]                            # the derived sprites' own cells: the band at the top of the sheets
    rest = [n for n in cells if n not in derived]
    sizes_top, sizes_rest = [size_of(n) for n in top], [size_of(n) for n in rest]
    best = None
    for width in range(max(w for _, w, _ in sizes_top + sizes_rest), MAX_WIDTH + 1, 4):        # the sheet that packs smallest (width times height) of the widths that hold the widest sprite
        spots_top, band_h = skyline(sizes_top, width) if top else ({}, 0)
        spots_rest, rest_h = skyline(sizes_rest, width)
        height = band_h + rest_h
        if best is None or width * height < best[0]:
            best = (width * height, width, height, dict(spots_top, **{n: (x, y + band_h) for n, (x, y) in spots_rest.items()}), band_h)
    _, sheet_w, sheet_h, spots, band_h = best
    place = {n: (spots[n][0] + PAD, spots[n][1] + PAD) for n in cells}
    os.makedirs(out_dir, exist_ok=True)
    files = {}
    for k in scales:
        sheet = np.zeros((sheet_h * k, sheet_w * k, 4), np.uint8)
        for n in cells:
            if k == 2 and n in derived:
                continue                                        # (the game makes these when it loads the sheet)
            a = np.asarray(images[k][n].convert('RGBA'))
            px, py = place[n]
            ext = np.pad(a, ((PAD * k, PAD * k), (PAD * k, PAD * k), (0, 0)), mode='edge')
            sheet[(py - PAD) * k:(py - PAD) * k + ext.shape[0], (px - PAD) * k:(px - PAD) * k + ext.shape[1]] = ext
        name = 'sheet_%dx.png' % k
        write_png(os.path.join(out_dir, name), sheet)
        files[k] = name
    lines = ['ants-hud-art 1', 'canvas %d %d' % canvas, 'sheetsize %d %d' % (sheet_w, sheet_h)]
    if derived:
        lines.append('derive2x %d' % band_h)
    lines += ['sheet %d %s' % (k, files[k]) for k in scales]
    for r in rows:
        w, h = r['size']
        sx, sy = place[cell_of[r['name']]]
        x0, y0 = r['rect_960x540'][:2]
        line = 'sprite %s %d %d %d %d %d %d %s %s' % (r['name'], w, h, sx, sy, x0, y0, r['anchor'][0], r['anchor'][1])
        sl = r.get('slice')
        if sl:
            line += ' %s %d %d %d %d' % (sl['axis'], sl['start'], sl['tile'][0], sl['tile'][1], sl['end'])
        lines.append(line)
    for name, frames in anims.items():
        lines.append('anim %s%s' % (name, ' cursor' if name in cursor_anims else ''))
        for f in frames:
            lines.append('frame %d %s' % (f['ms'], ' '.join('%s %d %d' % (p['sprite'], p['x'], p['y']) for p in f['parts'])))
    with open(os.path.join(out_dir, 'hud_art.txt'), 'w', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    return {n: os.path.getsize(os.path.join(out_dir, n)) for n in list(files.values()) + ['hud_art.txt']}
