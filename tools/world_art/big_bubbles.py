"""The bigger bubbles of the mud: the approved bubble drawing (mud_water.py) at a larger size, for the deep middle of a big mud field (docs/WORLD_ART.md).

Each of the original's ten bubble frames (sprites 641 to 645, the set 'b', and 646 to 650, the set 'c') is drawn again `f` times bigger about the point where its set's bubbles grow from, on the same
clean mud, with the same colors, the same lit side, shaded side and cast shadow; the shadow and the soft edge of the shape grow with it (f = 1 is exactly the approved frame). Two size classes: M (the
set b 1.6 times bigger, the set c 1.07) and L (b 2.2, c 1.15). The pack holds every frame in both classes under the numbers 10000 + sprite (M) and 20000 + sprite (L), with the sixteen variants of the mud.

    python3 tools/world_art/big_bubbles.py       checks that no size reaches the tile's outer pixels (f = 1 is mud_water.bubble_frame itself)
"""
import numpy as np

import mud_water
import tilekit as K
from catalog import BIG_BUBBLE_CLASS, BUBBLE_SPRITES, VARIANT_COUNT, original_number

ANCHOR = {'b': (15.6, 16.2), 'c': (15.9, 16.0)}                        # the point a set's bubbles grow about (world pixels): the set's common center
SET_OF = {**{sprite: 'b' for sprite in range(641, 646)}, **{sprite: 'c' for sprite in range(646, 651)}}
SIZES = {'M': {'b': 1.6, 'c': 1.07}, 'L': {'b': 2.2, 'c': 1.15}}      # how many times bigger, by size class and set


def up_scaled(m, f, anchor):
    """a 1x array enlarged f times about `anchor` (world pixels) onto the master, by cubic convolution, its own border continued outward"""
    p = np.pad(np.asarray(m, float), 3, mode='edge')
    coords = (np.arange(K.N) + 0.5) / K.MS                              # the world coordinate of each master sample
    ax, ay = anchor
    sx = ax + (coords - ax) / f - 0.5
    sy = ay + (coords - ay) / f - 0.5
    return K.weights_at(sy, K.T) @ p @ K.weights_at(sx, K.T).T


def classmap_scaled(lab, nclass, f, anchor, soft=0.25):
    """the master map of the classes of a 1x map, f times bigger about `anchor`: each class's indicator is enlarged, softened by `soft` x f and the strongest field wins (as tilekit.classmap)"""
    fields = []
    for k in range(nclass):
        field = up_scaled((lab == k).astype(float), f, anchor)
        fields.append(K.blur_edge(field, soft * f) if soft > 0 else field)
    return np.argmax(np.stack(fields), axis=0)


def bubble_tile(sprite, variant, f=1.0):
    """the mud of the variant with the original's bubble of a frame (sprites 641 to 650), f times bigger: a Tile"""
    anchor = ANCHOR[SET_OF[sprite]]
    tile = mud_water.mud_tile(variant)
    lab = mud_water.bubble_classes(sprite)
    if lab.any():
        cm = classmap_scaled(lab, 6, f, anchor) if f != 1.0 else K.classmap(lab, 6, soft=0.25, border=0.0001)
        mud_water.draw_bubble(tile, cm, f)
    return tile


def big_bubble(number, variant):
    """the pictures ({scale: RGBA array}) of a bigger bubble's number (10641 to 10650 medium, 20641 to 20650 large) and variant"""
    sprite = original_number(number)
    size = {base: name for name, base in BIG_BUBBLE_CLASS.items()}[number - sprite]
    return bubble_tile(sprite, variant, SIZES[size][SET_OF[sprite]]).pictures()


def check_inside():
    """the sizes whose bubble reaches the outermost pixel rows or columns of the tile (it would meet the next tile): ['sprite 641 variant 3 size M', ...], none expected"""
    found = []
    for sprite in BUBBLE_SPRITES:
        for variant in range(VARIANT_COUNT):
            plain = mud_water.mud_tile(variant).pictures()[1]
            for size, factors in SIZES.items():
                d = np.any(bubble_tile(sprite, variant, factors[SET_OF[sprite]]).pictures()[1] != plain, axis=2)
                if d[0, :].any() or d[-1, :].any() or d[:, 0].any() or d[:, -1].any():
                    found.append('sprite %d variant %d size %s' % (sprite, variant, size))
    return found


if __name__ == '__main__':
    found = check_inside()
    print('\n'.join('reaches the edge of the tile: ' + f for f in found) if found else 'no bigger bubble reaches the tile\'s outer pixels (%d frames x %d variants x %d sizes)' % (len(BUBBLE_SPRITES), VARIANT_COUNT, len(SIZES)))
    raise SystemExit(1 if found else 0)
