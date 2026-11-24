"""Paths and readers shared by the world art modules (README.md).

Nothing here names a place on a machine: the repository's root is found from this file. The only inputs of the drawing are the decoded sprites of `asset_catalog/sprites/` (their shapes and
colors) and the animation table `docs/chd_table4_animations.json` (which sprites a tile's animation uses); no picture of the original program is read.
"""
import json
import os

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
SPRITES = os.path.join(ROOT, 'asset_catalog', 'sprites')
TABLE = os.path.join(ROOT, 'docs', 'chd_table4_animations.json')

GROUND_FIRST, GROUND_LAST = 431, 669          # the animations (tiles) of the ground in the table
BRIDGE = range(112, 117)                      # the five sprites of the bridge
VARIANT_COUNT = 16                            # a plain tile has sixteen pictures (the position variants 0 to 15)

_table = None


def table():
    global _table
    if _table is None:
        with open(TABLE, encoding='utf-8') as f:
            _table = json.load(f)
    return _table


def anim(name):
    """the animation of a tile by its name (the exact name first, then any case)"""
    entries = table()
    for entry in entries:
        if entry['name'] == name:
            return entry
    for entry in entries:
        if entry['name'].lower() == name.lower():
            return entry
    raise KeyError(name)


def tile_sprites(name):
    """the distinct sprites of a tile's animation, in order of their first frame: [(sprite number, frame position)]"""
    seen, out = set(), []
    for position, item in enumerate(anim(name)['subitems']):
        number = item['frames'][0]['sprite_index']
        if number not in seen:
            seen.add(number)
            out.append((number, position))
    return out


def ground_numbers():
    """every sprite that the animations of the ground use, sorted"""
    return sorted({f['sprite_index'] for entry in table()[GROUND_FIRST:GROUND_LAST + 1] for item in entry['subitems'] for f in item['frames']})


def sprite_path(number):
    return os.path.join(SPRITES, 'sprite_%04d.png' % number)


def sprite(number):
    """a decoded sprite of the original as an RGBA picture"""
    return Image.open(sprite_path(number)).convert('RGBA')


def sprite_size(number):
    """(width, height) of a decoded sprite of the original, read from its file's header"""
    with Image.open(sprite_path(number)) as im:
        return im.size
