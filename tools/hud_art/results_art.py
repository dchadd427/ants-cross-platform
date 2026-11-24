"""The results screen's pieces (README.md, "The results screen"): the lettering, the nine-slice well that the rows stand in, and the Leave Game button, drawn in the windows' look (dialog_kit.py).

The game lays the screen out itself, in units of whole device pixels (include/ants_app/scorecard_layout.hpp), so nothing here has a place on the screen. Every piece is a sprite:

  lettering   sc_title ("Game Results"), sc_score_38 / _33 / _28 / _s ("Your Score" in the playful letters, three big sizes and a small one: the number is the height of a capital), sc_winner,
              sc_others, the column headings sc_h_score, sc_h_friendly, sc_h_enemy, sc_h_new (and under them sc_s_lost, sc_s_killed, sc_s_hatched) and the three of the 187 page, sc_h_kills,
              sc_h_antslost, sc_h_antsleft. Each is cut to what it holds. Its manifest place is (x, y): where the alignment point of the line is in the sprite (the end of the line for the headings
              that stand over a column, its start for the rest) and where its baseline is, counted from the sprite's top left. The playful titles are cut as wide to the right of the line's middle as to
              the left of it, so that the middle of the sprite is the middle of the line (a centered title), and their x is where the line starts (a title that stands at the left)
  the well    sc_well_tl .. sc_well_br: the eight pieces of a well (an edit field's bezel of brushed steel round its dark ground) that the game stretches to any size: corners of WELL_CORNER, edges
              that are one repeat of the brushing (WELL_RUN) long; the dark ground in the middle is a fill
  the button  sc_leave_<up|hover|down>: Leave Game as a whole picture (face 99 x 22, its cast shadow 3 across and 4 down); for a narrow screen the button is as wide as the screen asks:
              sc_btn_<l|m|r>_<state> (a left end, a run of the brushing that the game repeats, a right end; face 40 high) with the word on it, sc_leave_word_<state>
"""
import math

import numpy as np
from PIL import Image

import dialog_kit as K
from dialog_kit import text, measure

WELL_CORNER = 12                  # the size of a well's corner piece (the bezel is 4 wide, its cast shadow 2 more, the rest is the ground and the soft edge)
WELL_RUN = 128                    # the length of an edge piece: the brushing's period (dialog_kit.BT_W)
WELL_GROUND = (WELL_RUN + 2 * WELL_CORNER - 10, 32 + 2 * WELL_CORNER - 10)      # the ground of the drawn well: its picture is 10 larger each way
WELL_BEZEL = '#1d4a3d'            # one color for the whole bezel: it must not fall from top to bottom, or an edge that is repeated would show the fall again and again
BIG_FACE_H = 40                   # the face of the narrow screen's Leave Game button
BIG_END = 12                      # the size of its ends
BIG_RUN = 128
BIG_WORD = 17.0                   # the em of the word on the big button
SMALL_BUTTON = (99, 22, 11.5)     # the face and em of the button that stands in a wide screen's corner
STATES = ('up', 'hover', 'down')

MOVES = ((1.12, 0, -4), (.92, 1, 3), (1.0, -1, -2), (.95, 1, 4), (1.06, 0, -3), (.9, -1, 2), (1.0, 1, -4), (1.05, 0, 3))       # the titles' playful letters: (size factor, lift, tilt)

# (sprite, words, material, em or height of a capital, space between the letters, alignment point)
LINES = (
    ('sc_winner', 'Winner!', 'jade', 17.0, None, 'start'),
    ('sc_others', 'other players...', 'jade', 15.0, None, 'start'),
    ('sc_h_score', 'Score', 'jade', 13.0, None, 'end'),
    ('sc_h_friendly', 'Friendly', 'jade', 13.0, None, 'end'),
    ('sc_h_enemy', 'Enemy', 'jade', 13.0, None, 'end'),
    ('sc_h_new', 'New Ants', 'jade', 13.0, None, 'end'),
    ('sc_s_lost', 'Ants Lost', 'small', 8.6, None, 'end'),
    ('sc_s_killed', 'Ants Killed', 'small', 8.6, None, 'end'),
    ('sc_s_hatched', 'Hatched', 'small', 8.6, None, 'end'),
    ('sc_h_kills', 'Kills', 'jade', 13.0, None, 'end'),
    ('sc_h_antslost', 'Ants lost', 'jade', 13.0, None, 'end'),
    ('sc_h_antsleft', 'Ants left', 'jade', 13.0, None, 'end'),
    ('sc_title', 'Game Results', 'playful', 14.5, 1.6, 'start'),
    ('sc_score_38', 'Your Score', 'playful', 38.0, 3.8, 'start'),
    ('sc_score_33', 'Your Score', 'playful', 33.0, 3.3, 'start'),
    ('sc_score_28', 'Your Score', 'playful', 28.0, 2.8, 'start'),
    ('sc_score_s', 'Your Score', 'playful', 15.0, 1.4, 'start'),
)
PLAYFUL = tuple(n for n, _, material, *_ in LINES if material == 'playful')

NAMES = tuple(n for n, *_ in LINES) + tuple('sc_well_' + p for p in ('tl', 't', 'tr', 'l', 'r', 'bl', 'b', 'br')) + tuple('sc_leave_' + s for s in STATES) + \
    tuple('sc_btn_%s_%s' % (p, s) for s in STATES for p in ('l', 'm', 'r')) + tuple('sc_leave_word_' + s for s in STATES)


def _line_cell(words, material, size, space, align):
    """one line on a generous cell: (w, h, svg, alignment x, baseline y) in the cell's own numbers. A playful line is centered in its cell, and its alignment point is where the line starts"""
    pad = 20
    if material == 'playful':
        em = size / K.CAP
        total = sum(measure(c, em) + space for c in words) - space
        pad = 24 + int(size * .3)
        w = int(math.ceil(total)) + 2 * pad
        asc = int(size * 1.5) + 20
        h = asc + int(size * .5) + 20
        cx = w / 2.0
        body = K.jade_letters(K.playful(words, cx, asc, size, space, MOVES))
        return w, h, K.svg(w, h, body, K.new_uid()), cx - total / 2.0, asc
    w = int(math.ceil(measure(words, size))) + 2 * pad
    h = int(size * 1.9) + 2 * pad
    base = pad + int(math.ceil(size * 1.15))
    x = {'start': pad, 'end': w - pad, 'middle': w / 2.0}[align]
    return w, h, K.svg(w, h, text(words, x, base, size, material, align), K.new_uid()), x, base


def _word_cell(words, size, state):
    """the word of the big button (the button's own stack: a dark seat under the letters, the cream face with its inner edges; pressed it is one pixel down and right)"""
    pad = 20
    hov = state == 'hover'
    sh = (1, 1) if state == 'down' else (0, 0)
    w = int(math.ceil(measure(words, size))) + 2 * pad
    h = int(size * 1.9) + 2 * pad
    base = pad + int(math.ceil(size * 1.15))
    cx = w / 2.0
    body = K._t(words, cx + sh[0], base + sh[1], size, 'middle', '#04120d', ' stroke="#04120d" stroke-opacity=".3" stroke-width="2" stroke-linejoin="round" fill-opacity=".3"')
    body += text(words, cx, base, size, 'label', 'middle', shift=sh, hover=hov)
    return w, h, K.svg(w, h, body, K.new_uid()), cx, base


def _well():
    gw, gh = WELL_GROUND
    u = K.new_uid()
    pw, ph = gw + 10, gh + 10
    return pw, ph, K.svg(pw, ph, K.field(u, 4, 4, gw, gh, flat=WELL_BEZEL), u)


def _big_button(state):
    u = K.new_uid()
    fw = 2 * BIG_END + BIG_RUN - 3
    return fw + 3, BIG_FACE_H + 4, K.svg(fw + 3, BIG_FACE_H + 4, K.button(u, 0, 0, fw, BIG_FACE_H, [], state), u)


def _leave(state):
    fw, fh, em = SMALL_BUTTON
    u = K.new_uid()
    return fw + 3, fh + 4, K.svg(fw + 3, fh + 4, K.button(u, 0, 0, fw, fh, ['Leave Game'], state, size=em), u)


def drawings():
    """({cell name: (w, h, svg)} of everything that is drawn, {name: (cell name, box in the cell, alignment x, baseline y)} of the sprites that are cut from a cell without a trim,
    {name: (alignment x, baseline y)} of the lettering that is trimmed to what it holds, in the cell's numbers). The cells are drawn on a sheet of their own (dialogs.export)."""
    cells = {}
    cut = {}
    trim = {}
    for name, words, material, size, space, align in LINES:
        w, h, svg, ax, base = _line_cell(words, material, size, space, align)
        cells[name] = (w, h, svg)
        trim[name] = (ax, base)
    for st in STATES:
        w, h, svg, ax, base = _word_cell('Leave Game', BIG_WORD, st)
        cells['sc_leave_word_' + st] = (w, h, svg)
        trim['sc_leave_word_' + st] = (ax, base)
        w, h, svg = _big_button(st)
        cells['_big_' + st] = (w, h, svg)
        for part, (x0, x1) in (('l', (0, BIG_END)), ('m', (BIG_END, BIG_END + BIG_RUN)), ('r', (w - BIG_END, w))):
            cut['sc_btn_%s_%s' % (part, st)] = ('_big_' + st, (x0, 0, x1, h), 0, 0)
        w, h, svg = _leave(st)
        cells['sc_leave_' + st] = (w, h, svg)
        cut['sc_leave_' + st] = ('sc_leave_' + st, (0, 0, w, h), 0, 0)
    w, h, svg = _well()
    cells['_well'] = (w, h, svg)
    c, r = WELL_CORNER, WELL_RUN
    for part, box in (('tl', (0, 0, c, c)), ('t', (c, 0, c + r, c)), ('tr', (w - c, 0, w, c)), ('l', (0, c, c, c + 32)), ('r', (w - c, c, w, c + 32)),
                      ('bl', (0, h - c, c, h)), ('b', (c, h - c, c + r, h)), ('br', (w - c, h - c, w, h))):
        cut['sc_well_' + part] = ('_well', box, 0, 0)
    return cells, cut, trim


def trim_box(pic3):
    """the box (canvas pixels, one pixel of air round it) that holds everything that shows of a lettering's 3x picture"""
    a = np.asarray(pic3.convert('RGBA'))[..., 3]
    ys, xs = np.nonzero(a > 0)
    assert len(xs), 'a lettering that shows nothing'
    return (int(xs.min()) // 3 - 1, int(ys.min()) // 3 - 1, -(-int(xs.max() + 1) // 3) + 1, -(-int(ys.max() + 1) // 3) + 1)


def pieces(cells, cut, trim, cell_pics, scales):
    """({scale: {sprite name: picture}}, [(name, w, h, x, y)] its manifest rows' sizes and places) from the pictures of the cells (`cell_pics`: {scale: {cell name: picture}}); `cells`, `cut` and
    `trim` are what drawings() gave"""
    out = {k: {} for k in scales}
    rows = []
    for name, (cell, box, ax, base) in cut.items():
        for k in scales:
            out[k][name] = cell_pics[k][cell].crop((box[0] * k, box[1] * k, box[2] * k, box[3] * k))
        rows.append((name, box[2] - box[0], box[3] - box[1], ax, base))
    for name, (ax, base) in trim.items():
        x0, y0, x1, y1 = trim_box(cell_pics[3][name])
        w, h = cells[name][:2]
        if name in PLAYFUL:                                              # as far right of the cell's middle as left of it
            half = max(w / 2.0 - x0, x1 - w / 2.0)
            x0, x1 = int(math.floor(w / 2.0 - half)), int(math.ceil(w / 2.0 + half))
        x0, y0, x1, y1 = max(x0, 0), max(y0, 0), min(x1, w), min(y1, h)
        for k in scales:
            out[k][name] = cell_pics[k][name].crop((x0 * k, y0 * k, x1 * k, y1 * k))
        rows.append((name, x1 - x0, y1 - y0, int(round(ax - x0)), int(round(base - y0))))
    return out, rows
