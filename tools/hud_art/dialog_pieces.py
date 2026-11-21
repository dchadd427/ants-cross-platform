"""The windows' sprites (README.md, "The windows"): the panel as a tile of clay and eight pieces of frame, the buttons, the switches and the slider thumbs as one sprite for each state, and the cutting of a window's fixed layer
into pieces. dialog_kit.py is the look, dialog_windows.py the geometry, dialogs.py renders all of it and writes the manifest rows and animations.
"""
import numpy as np

import dialog_kit as K
from dialog_windows import THUMB_H, THUMB_W, THUMB_ROW

# ---- the panel -----------------------------------------------------------------------------------------------------------------------------------------------------
E = 24                                            # the size of a corner and the depth of an edge piece, in canvas pixels. The darkening toward the frame (the one thing that is not periodic) is within
                                                  # one level of a repeat from 20 pixels in at 1x, 2x and 3x; 24 leaves only the rounding of the browser (check_periodic keeps it so)
PERIOD = K.FRAME_PERIOD                           # (160, 120): what the top and bottom pieces and the left and right pieces repeat by
PROBE = (2 * E + 2 * PERIOD[0], 2 * E + 2 * PERIOD[1])      # the frame that the eight pieces are cut from: two periods across and down inside the margins
PANEL_PIECES = ('tl', 't', 'tr', 'l', 'r', 'bl', 'b', 'br')      # the frame, on a transparent ground; the clay is its own tile (panel_c)


def panel_clay():
    """(w, h, svg) of panel_c: the clay with its tooth, one repeat (160 x 120). The game tiles it over the whole panel from the panel's top left, the last copy cut, and lays the frame pieces on top"""
    u = K.new_uid()
    w, h = PERIOD
    return w, h, K.svg(w, h, K.panel_clay(u, w, h), u)


def panel_probe():
    """(w, h, svg) of the panel's frame, which the eight pieces are cut from: everything the panel has over its clay, on a transparent ground"""
    u = K.new_uid()
    w, h = PROBE
    return w, h, K.svg(w, h, K.panel_frame(u, 0, 0, w, h), u)


def panel_boxes():
    """{piece: (x, y, w, h)} of the eight frame pieces in the probe: corners E x E, the top and bottom pieces one period wide and E high, the left and right ones E wide and one period high. Every piece
    is cut where the picture is the same one period further on, so the game can repeat a piece to any length"""
    W, H = PROBE
    pw, ph = PERIOD
    xs = {'l': (0, E), 'c': (E, pw), 'r': (W - E, E)}
    ys = {'t': (0, E), 'c': (E, ph), 'b': (H - E, E)}
    out = {}
    for name in PANEL_PIECES:
        v, h = ('t' if 't' in name else 'b' if 'b' in name else 'c'), ('l' if name.endswith('l') else 'r' if name.endswith('r') else 'c')
        out[name] = (xs[h][0], ys[v][0], xs[h][1], ys[v][1])
    return out


def check_periodic(probe, k):
    """the largest difference (in levels of any channel, 0 .. 255) between the frame at scale k and itself one period further on, over the part that lies E or more from the outer edge: along x for
    every row between columns E and W - E, along y for every column between rows E and H - E. The pieces repeat without a seam when it is 1 or less (a transparent pixel is all zeros)."""
    a = np.asarray(probe.convert('RGBA')).astype(int)
    pw, ph = PERIOD
    dx = np.abs(a[:, E * k:(E + pw) * k] - a[:, (E + pw) * k:(E + 2 * pw) * k]).max()
    dy = np.abs(a[E * k:(E + ph) * k, :] - a[(E + ph) * k:(E + 2 * ph) * k, :]).max()
    return int(max(dx, dy))


# ---- buttons, switches and thumbs ---------------------------------------------------------------------------------------------------------------------------
# kind: (face width, face height, words, size, lead). The sprite is 3 wider and 4 higher than the face: the face's top left is the sprite's, the cast shadow fills the rest.
DIALOG_BUTTONS = {
    'yes': (46, 20, ['Yes'], 11.5, None), 'no': (46, 20, ['No'], 11.5, None),
    'accept': (77, 20, ['Accept'], 11.5, None), 'decline': (77, 20, ['Decline'], 11.5, None), 'withdraw': (77, 20, ['Withdraw'], 11.5, None),
    'return': (95, 23, ['Return to', 'Game'], 9.6, 9.4),
}
FULLSCREEN_FACE = (94, 22)                        # the countdown card's own button (the sprite is 3 wider and 4 higher: the cast shadow)
SWITCH_FACE = (46, 20)
SWITCH_STATES = ('up', 'hover', 'down', 'chosen', 'chosen_hover')
STATES = ('up', 'hover', 'down')


def button_drawing(kind, state, origin):
    """(w, h, svg) of a dialog button: state up, hover or down. `origin`: the button's place in its window's own numbers (see dialog_kit.svg)"""
    fw, fh, words, size, lead = DIALOG_BUTTONS[kind]
    u = K.new_uid()
    return fw + 3, fh + 4, K.svg(fw + 3, fh + 4, K.button(u, origin[0], origin[1], fw, fh, words, state, size=size, lead=lead), u, origin)


def fullscreen_drawing(state, origin):
    """(w, h, svg) of the Fullscreen button of the countdown card: state up or down"""
    fw, fh = FULLSCREEN_FACE
    u = K.new_uid()
    return fw + 3, fh + 4, K.svg(fw + 3, fh + 4, K.button(u, origin[0], origin[1], fw, fh, ['Fullscreen'], state, size=11.5), u, origin)


def switch_drawing(word, state, origin):
    """(w, h, svg) of the On or Off switch of the options window. `chosen`: the one that is on (gold, sunk, its word lit); `chosen_hover`: that one with the ring of the pointer on it"""
    fw, fh = SWITCH_FACE
    u = K.new_uid()
    x, y = origin
    if state in ('chosen', 'chosen_hover'):
        b = K.button(u, x, y, fw, fh, [word], 'down', size=11.5, gold=True) + (K.switch_ring(x, y) if state == 'chosen_hover' else '')
    else:
        b = K.button(u, x, y, fw, fh, [word], state, size=11.5)
    return fw + 3, fh + 4, K.svg(fw + 3, fh + 4, b, u, origin)


def thumb_drawing(state, origin):
    """(w, h, svg) of the slider's thumb: the grip (49 x 16) at row THUMB_ROW of a picture two rows higher, which holds its shadow; state up, hover or held"""
    u = K.new_uid()
    h = THUMB_H + 2
    return THUMB_W, h, K.svg(THUMB_W, h, K.thumb(u, origin[0], origin[1] + THUMB_ROW, THUMB_W, THUMB_H, state), u, origin)


# ---- cutting a layer into sprites -----------------------------------------------------------------------------------------------------------------------------------
MIN_GAP = 4                                       # an empty row or column this wide (canvas pixels) is where a layer is cut


def empty_cells(pictures):
    """True for every canvas pixel that is empty (alpha 0 in all its device pixels) in every one of the layer's pictures [(scale, RGBA)], so that a cut never loses a pixel at any scale"""
    mask = None
    for k, im in pictures:
        a = np.asarray(im.convert('RGBA'))[..., 3]
        h, w = a.shape[0] // k, a.shape[1] // k
        e = a.reshape(h, k, w, k).max(axis=(1, 3)) == 0
        mask = e if mask is None else (mask & e)
    return mask


def _runs(filled):
    """[(first, last + 1)] of the stretches of False in a list that starts and ends with True"""
    out = []
    start = None
    for i, v in enumerate(filled):
        if not v and start is None:
            start = i
        elif v and start is not None:
            out.append((start, i))
            start = None
    return out


def xy_cut(empty, min_gap=MIN_GAP):
    """the boxes (x0, y0, x1, y1) in canvas pixels that a layer falls into. The layer is cut at the widest empty row or column gap of at least `min_gap` pixels (a row gap first when both are as wide),
    each part is trimmed to what it holds and cut again, until no gap that wide is left. Returned in reading order: top to bottom, left to right."""
    out = []

    def trim(x0, y0, x1, y1):
        sub = ~empty[y0:y1, x0:x1]
        rows = np.nonzero(sub.any(axis=1))[0]
        cols = np.nonzero(sub.any(axis=0))[0]
        if not len(rows):
            return None
        return int(x0 + cols[0]), int(y0 + rows[0]), int(x0 + cols[-1] + 1), int(y0 + rows[-1] + 1)

    def cut(box):
        box = trim(*box)
        if box is None:
            return
        x0, y0, x1, y1 = box
        sub = ~empty[y0:y1, x0:x1]
        best = None
        for axis, runs in (('y', _runs(sub.any(axis=1))), ('x', _runs(sub.any(axis=0)))):
            for a, b in runs:
                if b - a >= min_gap and (best is None or b - a > best[0]):
                    best = (b - a, axis, a, b)
        if best is None:
            out.append(box)
        elif best[1] == 'y':
            cut((x0, y0, x1, y0 + best[2]))
            cut((x0, y0 + best[3], x1, y1))
        else:
            cut((x0, y0, x0 + best[2], y1))
            cut((x0 + best[3], y0, x1, y1))
    cut((0, 0, empty.shape[1], empty.shape[0]))
    return sorted(out, key=lambda b: (b[1], b[0]))
