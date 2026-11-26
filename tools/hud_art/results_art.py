"""The results screen's pieces (README.md, "The results screen"): the lettering, the nine-slice well that the page stands in, and the buttons, drawn in the windows' look (dialog_kit.py).

The game lays the screen out itself, in units of whole device pixels (include/ants_app/scorecard_layout.hpp), so nothing here has a place on the screen. Every piece is a sprite:

  titles      sc_victory_26 / _22, sc_defeat_26 / _22 ("Victory!" and "Defeat"), sc_gameover_36 / _26 ("Game Over": the waiting page) and sc_matchstats_15 ("Match Stats"), in the playful cast-metal
              letters; the number is the height of a capital
  lettering   sc_others ("other players..."), the headings of the second page (sc_g_damage, sc_g_food, sc_g_special, and under them sc_s_done, sc_s_taken, sc_s_trips, sc_s_points, sc_s_bombs,
              sc_s_fires, sc_s_raids, sc_s_loot) and the gold labels of the wells (sc_k_winner, sc_k_colony, sc_k_play, sc_k_mvp, sc_k_blood, sc_k_thief, sc_k_stand): the windows' lit switch word
  every one is cut to what it holds. Its manifest place is (x, y): where the alignment point of the line is in the sprite (the end of the line for the headings that stand over a column, the middle
              for the gold labels that are centered, its start for the rest) and where its baseline is, counted from the sprite's top left. The playful titles are cut as wide to the right of the
              line's middle as to the left of it, so that the middle of the sprite is the middle of the line (a centered title), and their x is where the line starts (a title that stands at the left)
  the well    sc_well_tl .. sc_well_br: the eight pieces of a well (an edit field's bezel of brushed steel round its dark ground) that the game stretches to any size: corners of WELL_CORNER, edges
              that are one repeat of the brushing (WELL_RUN) long; the dark ground in the middle is a fill
  buttons     sc_leave_<up|hover|down>: Leave Game as a whole picture (face 99 x 22, its cast shadow 3 across and 4 down); sc_watch_<state>: Watch (face 58 x 20); the buttons that are as wide as the
              screen asks are an end, a run of the brushing that the game repeats and the other end: sc_btn_<l|m|r>_<state> (face 40 high: the narrow screen's buttons, with the word on them:
              sc_leave_word_<state> "Leave Game", sc_bw_<room|hub>_<state> "Back to the room" and "Back to Ants & Uncles") and sc_sm_<l|m|r>_<state> (face 22 high: the wide screen's buttons and the
              link to the other page, with sc_sw_<allstats|back|room|hub>_<state> on them: "All stats ▸", "◂ Back to results", "Back to the room", "Back to Ants & Uncles")
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
SMALL_FACE_H = 22                 # the face of a button of the wide screen, and of the link to the other page
BIG_END = 12                      # the size of the ends of both
BIG_RUN = 128
BIG_WORD = 17.0                   # the em of the word on the big button
SMALL_WORD = 11.5                 # ... and on a small button
SMALL_BUTTON = (99, 22, 11.5)     # the face and em of the button that stands in a wide screen's corner
WATCH_BUTTON = (58, 20, 10.5)     # Watch
STATES = ('up', 'hover', 'down')

MOVES = ((1.12, 0, -4), (.92, 1, 3), (1.0, -1, -2), (.95, 1, 4), (1.06, 0, -3), (.9, -1, 2), (1.0, 1, -4), (1.05, 0, 3))       # the titles' playful letters: (size factor, lift, tilt)

# (sprite, words, material, em or height of a capital, space between the letters, alignment point). Materials: jade (cast metal on the clay), small (dark small letters), playful (jade letters that dance),
# gold (the windows' lit switch word)
LINES = (
    ('sc_victory_26', 'Victory!', 'playful', 26.0, 2.6, 'start'),
    ('sc_victory_22', 'Victory!', 'playful', 22.0, 2.2, 'start'),
    ('sc_defeat_26', 'Defeat', 'playful', 26.0, 2.6, 'start'),
    ('sc_defeat_22', 'Defeat', 'playful', 22.0, 2.2, 'start'),
    ('sc_gameover_36', 'Game Over', 'playful', 36.0, 3.6, 'start'),
    ('sc_gameover_26', 'Game Over', 'playful', 26.0, 2.6, 'start'),
    ('sc_matchstats_15', 'Match Stats', 'playful', 15.0, 1.4, 'start'),
    ('sc_others', 'other players...', 'jade', 14.0, None, 'start'),
    ('sc_g_damage', 'Damage', 'jade', 13.0, None, 'middle'),
    ('sc_g_food', 'Food', 'jade', 13.0, None, 'middle'),
    ('sc_g_special', 'Special ants', 'jade', 13.0, None, 'middle'),
    ('sc_s_done', 'Done', 'small', 8.6, None, 'end'),
    ('sc_s_taken', 'Taken', 'small', 8.6, None, 'end'),
    ('sc_s_trips', 'Trips', 'small', 8.6, None, 'end'),
    ('sc_s_points', 'Points', 'small', 8.6, None, 'end'),
    ('sc_s_bombs', 'Bombs set', 'small', 8.6, None, 'end'),
    ('sc_s_fires', 'Fires lit', 'small', 8.6, None, 'end'),
    ('sc_s_raids', 'Raids', 'small', 8.6, None, 'end'),
    ('sc_s_loot', 'Loot', 'small', 8.6, None, 'end'),
    ('sc_k_winner', 'WINNER', 'gold', 11.0, None, 'start'),
    ('sc_k_colony', 'LAST COLONY STANDING', 'gold', 11.0, None, 'start'),
    ('sc_k_play', 'PLAY OF THE GAME', 'gold', 10.0, None, 'start'),
    ('sc_k_mvp', 'MVP', 'gold', 9.5, None, 'middle'),
    ('sc_k_blood', 'FIRST BLOOD', 'gold', 9.5, None, 'middle'),
    ('sc_k_thief', 'TOP THIEF', 'gold', 9.5, None, 'middle'),
    ('sc_k_stand', 'LAST STAND', 'gold', 9.5, None, 'middle'),
)
PLAYFUL = tuple(n for n, _, material, *_ in LINES if material == 'playful')
SMALL_WORDS = (('allstats', 'All stats \u25b8'), ('back', '\u25c2 Back to results'), ('room', 'Back to the room'), ('hub', 'Back to Ants & Uncles'))
BIG_WORDS = (('room', 'Back to the room'), ('hub', 'Back to Ants & Uncles'))

NAMES = tuple(n for n, *_ in LINES) + tuple('sc_well_' + p for p in ('tl', 't', 'tr', 'l', 'r', 'bl', 'b', 'br')) + tuple('sc_leave_' + s for s in STATES) + tuple('sc_watch_' + s for s in STATES) + \
    tuple('sc_btn_%s_%s' % (p, s) for s in STATES for p in ('l', 'm', 'r')) + tuple('sc_leave_word_' + s for s in STATES) + \
    tuple('sc_sm_%s_%s' % (p, s) for s in STATES for p in ('l', 'm', 'r')) + tuple('sc_sw_%s_%s' % (n, s) for n, _ in SMALL_WORDS for s in STATES) + tuple('sc_bw_%s_%s' % (n, s) for n, _ in BIG_WORDS for s in STATES)


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
    body = text(words, x, base, size, 'label', align, gold=True) if material == 'gold' else text(words, x, base, size, material, align)
    return w, h, K.svg(w, h, body, K.new_uid()), x, base


def _word_cell(words, size, state, gold=False):
    """the word of a button that is as wide as the screen asks (the button's own stack: a dark seat under the letters, the cream face with its inner edges, or the gold of the page that is shown;
    pressed it is one pixel down and right)"""
    pad = 20
    hov = state == 'hover'
    sh = (1, 1) if state == 'down' else (0, 0)
    w = int(math.ceil(measure(words, size))) + 2 * pad
    h = int(size * 1.9) + 2 * pad
    base = pad + int(math.ceil(size * 1.15))
    cx = w / 2.0
    body = K._t(words, cx + sh[0], base + sh[1], size, 'middle', '#04120d', ' stroke="#04120d" stroke-opacity=".3" stroke-width="2" stroke-linejoin="round" fill-opacity=".3"')
    body += text(words, cx, base, size, 'label', 'middle', shift=sh, hover=hov, gold=gold)
    return w, h, K.svg(w, h, body, K.new_uid()), cx, base


def _well():
    gw, gh = WELL_GROUND
    u = K.new_uid()
    pw, ph = gw + 10, gh + 10
    return pw, ph, K.svg(pw, ph, K.field(u, 4, 4, gw, gh, flat=WELL_BEZEL), u)


def _strip(face_h, state):
    """a button as wide as an end, a run and the other end: its face face_h high, its cast shadow 3 across and 4 down"""
    u = K.new_uid()
    fw = 2 * BIG_END + BIG_RUN - 3
    return fw + 3, face_h + 4, K.svg(fw + 3, face_h + 4, K.button(u, 0, 0, fw, face_h, [], state), u)


def _watch(state):
    fw, fh, em = WATCH_BUTTON
    u = K.new_uid()
    return fw + 3, fh + 4, K.svg(fw + 3, fh + 4, K.button(u, 0, 0, fw, fh, ['Watch'], state, size=em), u)


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
        for key, words in SMALL_WORDS:
            w, h, svg, ax, base = _word_cell(words, SMALL_WORD, st)
            cells['sc_sw_%s_%s' % (key, st)] = (w, h, svg)
            trim['sc_sw_%s_%s' % (key, st)] = (ax, base)
        for key, words in BIG_WORDS:
            w, h, svg, ax, base = _word_cell(words, BIG_WORD, st)
            cells['sc_bw_%s_%s' % (key, st)] = (w, h, svg)
            trim['sc_bw_%s_%s' % (key, st)] = (ax, base)
        for prefix, face_h in (('sc_btn', BIG_FACE_H), ('sc_sm', SMALL_FACE_H)):
            w, h, svg = _strip(face_h, st)
            cells['_%s_%s' % (prefix, st)] = (w, h, svg)
            for part, (x0, x1) in (('l', (0, BIG_END)), ('m', (BIG_END, BIG_END + BIG_RUN)), ('r', (w - BIG_END, w))):
                cut['%s_%s_%s' % (prefix, part, st)] = ('_%s_%s' % (prefix, st), (x0, 0, x1, h), 0, 0)
        for prefix, make in (('sc_leave', _leave), ('sc_watch', _watch)):
            w, h, svg = make(st)
            cells['%s_%s' % (prefix, st)] = (w, h, svg)
            cut['%s_%s' % (prefix, st)] = ('%s_%s' % (prefix, st), (0, 0, w, h), 0, 0)
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
