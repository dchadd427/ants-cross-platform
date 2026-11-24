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
BUBBLE_SPRITES = range(641, 651)              # the original's bubble frames that have bigger copies: two sets of five (big_bubbles.py)
BIG_BUBBLE_CLASS = {'M': 10000, 'L': 20000}   # the number of a bigger bubble's picture is this plus the original's sprite number (10641 to 10650 medium, 20641 to 20650 large)
BOMBS = range(298, 306)                       # the four teams' bombs, two frames each (bomb.py)
EXPLOSION = range(306, 316)                   # the bomb's explosion, ten frames (explosion.py)
FIRE_WALL = range(316, 321)                   # the fire wall, five frames (fire.py)
EXPLOSION_LIGHT = 30000                       # the number of an explosion frame's light (the fire alone) is this plus the original's sprite number (30306 to 30315)
FIRE_WALL_BASE = 50000                        # the number of a fire wall's picture is this plus the original's sprite number (50316 to 50320): 316 to 319 are also the Fire ant's own frames, which keep the original's pictures

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


def original_number(number):
    """the original's sprite that a picture's number stands for: itself, or for a bigger bubble, an explosion's light or the fire wall (10000, 20000, 30000 or 50000 plus the sprite) its sprite"""
    return number % 10000


def big_bubble_numbers():
    """the numbers of the pictures of the bigger bubbles, sorted"""
    return sorted(base + sprite for base in BIG_BUBBLE_CLASS.values() for sprite in BUBBLE_SPRITES)


def sprite_size(number):
    """(width, height) of the original's sprite that a picture's number stands for, read from its file's header"""
    with Image.open(sprite_path(original_number(number))) as im:
        return im.size


def effect_numbers():
    """the numbers of the bombs, the explosion with its light and the fire wall, sorted"""
    return sorted(set(BOMBS) | set(EXPLOSION) | {EXPLOSION_LIGHT + n for n in EXPLOSION} | {FIRE_WALL_BASE + n for n in FIRE_WALL})
