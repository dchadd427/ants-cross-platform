"""Draws every picture of the world art at 1x, 2x and 3x: the ground tiles, the edge tiles, the bridge and the bigger bubbles of the mud (README.md has the list).

The pictures are written as `<folder>/<scale>x/sprite_NNNN.png` (NNNN is the original's sprite number, or 10000 or 20000 plus it for a bigger bubble; a position variant of a plain tile is
`sprite_NNNN_vK.png`, K from 1 to 15), each at the original sprite's size times the scale. The drawing is deterministic (every random choice has a fixed seed), so the same inputs give the same pictures.
"""
import multiprocessing
import os

from PIL import Image

import big_bubbles
import dirt_slate
import edges
import gravel
import mud_water
from catalog import BRIDGE, VARIANT_COUNT, big_bubble_numbers, ground_numbers, tile_sprites

MUD_AND_BUBBLES = list(range(640, 658)) + [901]       # the plain mud (640) and its bubbles, sixteen pictures each
GRAVEL = (726, 727)
BANK_FAMILIES = (('wm', 'a'), ('MW', 'a'), ('gm', 'ab'), ('mg', 'ab'))
SOFT_FAMILIES = ('gd', 'dg', 'md', 'dm')
ROCK_FAMILIES = ('ds', 'sd', 'gs', 'sg', 'ms', 'sm')


def picture_name(number, variant=0):
    return 'sprite_%04d.png' % number if variant == 0 else 'sprite_%04d_v%d.png' % (number, variant)


def jobs():
    """every picture to draw, in a fixed order: [(sprite number, variant, kind, argument)]"""
    out = []
    for v in range(VARIANT_COUNT):
        out.append((640, v, 'mud', None))
        out += [(number, v, 'bubble', None) for number in MUD_AND_BUBBLES[1:]]
        out += [(number, v, 'gravel', None) for number in GRAVEL]
        out += [(tile_sprites('d01' + c)[0][0], v, 'dirt', None) for c in 'abcd']
        out += [(number, v, 'bigbubble', None) for number in big_bubble_numbers()]
    out += [(690 + f, 0, 'water', None) for f in range(4)]
    out += [(tile_sprites('s01' + c)[0][0], 0, 'slate', None) for c in 'abcdefghi']
    for family, suffixes in BANK_FAMILIES:
        out += [(number, 0, 'bank', family) for number in edges.edge_numbers(family, suffixes)]
    for family in SOFT_FAMILIES:
        out += [(number, 0, 'soft', family) for number in edges.edge_numbers(family)]
    for family in ROCK_FAMILIES:
        out += [(number, 0, 'rock', family) for number in edges.edge_numbers(family)]
    out += [(number, 0, 'bridge', None) for number in BRIDGE]
    return out


def draw(job):
    """one job: (sprite number, variant, {scale: RGBA array})"""
    number, variant, kind, arg = job
    if kind == 'mud':
        pictures = mud_water.mud_tile(variant).pictures()
    elif kind == 'bubble':
        pictures = mud_water.bubble_frame(number, variant).pictures()
    elif kind == 'bigbubble':
        pictures = big_bubbles.big_bubble(number, variant)
    elif kind == 'gravel':
        pictures = gravel.gravel_tile(number, variant).pictures()
    elif kind == 'dirt':
        pictures = dirt_slate.dirt_tile(number, variant).pictures()
    elif kind == 'water':
        pictures = mud_water.water_tile(number).pictures()
    elif kind == 'slate':
        pictures = dirt_slate.slate_tile(number).pictures()
    elif kind == 'bank':
        pictures = edges.bank_tile(number, arg).pictures()
    elif kind == 'soft':
        pictures = edges.soft_edge(number, arg).pictures()
    elif kind == 'rock':
        pictures = edges.rock_edge(number, arg).pictures()
    else:
        pictures = dirt_slate.bridge_pictures(number)
    return number, variant, pictures


def draw_all(folder, workers=None, say=print):
    """draws and writes every picture; returns the sorted list of (sprite number, variant)"""
    todo = jobs()
    keys = [(j[0], j[1]) for j in todo]
    if len(set(keys)) != len(keys):
        raise SystemExit('a picture is drawn twice')
    expected = set(ground_numbers()) | set(BRIDGE) | set(big_bubble_numbers())      # every sprite of the ground's tiles and of the bridge and the numbers of the bigger bubbles, no other
    if {number for number, _ in keys} != expected:
        raise SystemExit('the pictures drawn are not every sprite of the ground tiles and the bridge and every bigger bubble: missing %s, extra %s' % (sorted(expected - {k[0] for k in keys}), sorted({k[0] for k in keys} - expected)))
    for scale in (1, 2, 3):
        os.makedirs(os.path.join(folder, '%dx' % scale), exist_ok=True)
    workers = workers or min(8, os.cpu_count() or 1)
    done = 0
    with multiprocessing.Pool(workers) as pool:
        for number, variant, pictures in pool.imap_unordered(draw, todo, chunksize=8):
            for scale, rgba in pictures.items():
                Image.fromarray(rgba).save(os.path.join(folder, '%dx' % scale, picture_name(number, variant)))
            done += 1
            if done % 100 == 0:
                say('%d of %d pictures' % (done, len(todo)))
    return sorted(keys)
