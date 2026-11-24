"""How a pack of new pictures is split over sheets: the one home of the rule for the tools that make packs (tools/hud_art/pack.py, tools/world_art/pack.py) and the scripts that check them.
The format of the records and what a page is are at the top of include/ants_app/art_sheets.hpp (their one home); this module is the same thing for Python:

  - `sheet_rows` writes the manifest's `sheet <page> <scale> <png file> <width> <height>` records, `parse_sheet_rows` reads them back (and refuses what the game's reader refuses);
  - `fill_pages` splits a list of pictures, in the packer's order, over pages: each page is filled as far as it goes (a sheet a texture can hold: LIMIT pixels a side at most, every scale), the
    rest go on the next page;
  - `file_name` is the name of a page's sheet file for a pack: the first page's is `<name>_<scale>x.png`, the others' `<name><number of the page in the pack>_<scale>x.png`;
  - `compare` is the check of a generator against the files that the repository holds (tools/hud_art/check_sheets.py, tools/world_art/check_sheets.py): the manifest and every sheet that a
    manifest names, byte for byte (the same pixels in other bytes pass, unless strict: another compression).
"""
import os
import re

SCALES = (1, 2, 3)
LIMIT = 4096                  # a side of a sheet in pixels: what every graphics card takes as a texture
MAX_PAGES = 4                 # what the game's reader keeps (kArtMaxPages)
_NUMBER = re.compile(r'0|[1-9][0-9]*')


def file_name(name, first_page, page, scale):
    """the file of the sheet of `page` at `scale` for a pack whose pages from `first_page` on are `name`'s: `sheet_1x.png` for the first, `sheet2_1x.png` for the next"""
    return '%s%s_%dx.png' % (name, '' if page == first_page else str(page - first_page + 1), scale)


def sheet_rows(sheets):
    """the `sheet` records, page by page and scale by scale: sheets is {(page, scale): (file, width, height)}; pages 0, 1, 2 ... with the three scales each, or ValueError"""
    pages = sorted({page for page, _ in sheets})
    if not pages or pages != list(range(len(pages))) or len(pages) > MAX_PAGES:
        raise ValueError('the pages are 0, 1, 2 ... in order, at most %d: %s' % (MAX_PAGES, pages))
    rows = []
    for page in pages:
        for scale in SCALES:
            if (page, scale) not in sheets:
                raise ValueError('page %d has no sheet for the scale %d' % (page, scale))
            file, width, height = sheets[(page, scale)]
            rows.append('sheet %d %d %s %d %d' % (page, scale, file, width, height))
    return rows


def parse_sheet_rows(lines):
    """{(page, scale): (file, width, height)} of the `sheet` records in `lines` (they must be the first records, page by page), and how many lines they took; ValueError when a record is wrong
    in a way that the game's reader refuses (ArtSheets::add and finish)"""
    sheets = {}
    taken = 0
    for line in lines:
        parts = line.split(' ')
        if parts[0] != 'sheet':
            break
        page, scale = divmod(len(sheets), 3)
        scale += 1
        if len(parts) != 6 or not all(_NUMBER.fullmatch(p) for p in (parts[1], parts[2], parts[4], parts[5])) or parts[1] != str(page) or parts[2] != str(scale) or page >= MAX_PAGES:
            raise ValueError('line %d is the sheet record of page %d scale %d: sheet <page> <scale> <png file> <width> <height>' % (taken + 1, page, scale))
        file = parts[3]
        if not file or file == '.' or '/' in file or '\\' in file or '..' in file:
            raise ValueError('a sheet\'s file is a name in the folder: %r' % file)
        width, height = int(parts[4]), int(parts[5])
        if not (1 <= width <= 16384 and 1 <= height <= 16384):
            raise ValueError('a sheet\'s size is out of range: %d x %d' % (width, height))
        sheets[(page, scale)] = (file, width, height)
        taken += 1
    if not sheets or len(sheets) % 3:
        raise ValueError('every page has the three sheet records, for scale 1, 2 and 3')
    return sheets, taken


def fill_pages(items, fits, max_pages=MAX_PAGES):
    """splits `items` (in the packer's order) over pages, each filled as far as it goes. `fits(page, chunk)` says whether `chunk` (a list of items, the first ones left) is a page's content: its layout
    (anything that is not None) or None when no sheet can hold it; it must be monotonic (a chunk that fits is still one when an item is taken out). Returns [(layout, chunk)] for the pages in order.
    The first page may hold no item when its own fixed content (a band that always sits on it) fills it; any other page that cannot hold even one item is an error (SystemExit), and so are more
    than `max_pages` pages"""
    pages = []
    left = list(items)
    while True:
        page = len(pages)
        if page >= max_pages:
            raise SystemExit('the pictures need more than %d pages' % max_pages)
        whole = fits(page, left)
        if whole is not None:
            pages.append((whole, left))
            return pages
        lo, hi = 0, len(left) - 1                   # the most items that the page takes: not all of them (they were tried), at least none
        while lo < hi:
            mid = (lo + hi + 1) // 2
            if fits(page, left[:mid]) is not None:
                lo = mid
            else:
                hi = mid - 1
        layout = fits(page, left[:lo])
        if layout is None:
            raise SystemExit('page %d cannot hold what it has to hold even with no picture of the rest' % page)
        if lo == 0 and page > 0:
            raise SystemExit('a picture does not fit a page of its own: %r' % (left[0],))
        pages.append((layout, left[:lo]))
        left = left[lo:]


def sheet_files(text, pages=None):
    """the sheet files that the `sheet` records of a manifest's text name, in order (only those of `pages` when it is given)"""
    out = []
    for line in text.split('\n'):
        parts = line.split()
        if len(parts) == 6 and parts[0] == 'sheet' and (pages is None or int(parts[1]) in pages):
            out.append(parts[3])
    return out


def _read(path):
    with open(path, 'rb') as f:
        return f.read()


def same_pixels(a, b):
    """True when two PNG files hold the same 8-bit RGBA picture"""
    import numpy as np
    from PIL import Image
    x = np.asarray(Image.open(a).convert('RGBA'))
    y = np.asarray(Image.open(b).convert('RGBA'))
    return x.shape == y.shape and bool(np.array_equal(x, y))


def compare(assets, built, manifest, strict=False, keep=None, pages=None):
    """(exit code, lines of the report) for the files of the folder `assets` against the ones in `built`: the manifest (`keep(line)` says which of its lines count, all of them when it is not given: a
    generator that makes some pages only compares those) and every sheet file that the manifest of either folder names (of `pages` when given). 0 when all are the same, or the same pixels in other bytes
    and not `strict`; 1 otherwise"""
    lines = []
    code = 0
    texts = {}
    for folder in (assets, built):
        path = os.path.join(folder, manifest)
        texts[folder] = _read(path).decode('utf-8') if os.path.isfile(path) else ''
    names = [manifest]
    for folder in (built, assets):
        for name in sheet_files(texts[folder], pages):
            if name not in names:
                names.append(name)
    for name in names:
        have, made = os.path.join(assets, name), os.path.join(built, name)
        if not os.path.isfile(made):
            lines.append('%-16s the generator did not write it' % name)
            code = 1
        elif not os.path.isfile(have):
            lines.append('%-16s the repository has no such file' % name)
            code = 1
        elif name == manifest and keep is not None:
            a = [l for l in texts[assets].split('\n') if keep(l)]
            b = [l for l in texts[built].split('\n') if keep(l)]
            lines.append('%-16s %s' % (name, 'the same records (%d)' % len(a) if a == b else 'DIFFERENT (%d records in the repository, %d built)' % (len(a), len(b))))
            code = code if a == b else 1
        elif _read(have) == _read(made):
            lines.append('%-16s the same bytes (%d)' % (name, os.path.getsize(have)))
        elif name.endswith('.png') and same_pixels(have, made):
            lines.append('%-16s the same pixels, other bytes (%d in the repository, %d built): the compression differs, libdeflate is not the one that made the repository\'s' % (name, os.path.getsize(have), os.path.getsize(made)))
            code = 1 if strict else code
        else:
            lines.append('%-16s DIFFERENT (%d in the repository, %d built)' % (name, os.path.getsize(have), os.path.getsize(made)))
            code = 1
    return code, lines
