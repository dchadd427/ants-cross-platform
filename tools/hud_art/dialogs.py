"""The windows' art as sprites and animations: renders the drawings of dialog_kit.py / dialog_windows.py / dialog_pieces.py at 1x, 2x and 3x and gives pack.py what it needs, in the same way as
cursors.py does (README.md, "The windows").

  the panel          panel_c (the clay, one repeat) and eight pieces of frame on a transparent ground (panel_tl .. panel_br): the game tiles the clay over the whole panel, then lays the frame on it
  the buttons        dlg_btn_<yes|no|accept|decline|withdraw|return>_<up|hover|down>, dlg_btn_fullscreen_<up|down>
  the options window op_on_* and op_off_* (the switches), op_thumb_* (the slider's thumb), op_part_NN (the fixed layer, cut into pieces)
  the quick help     qh_part_NN (the fixed layer), qh_pedestal_move and qh_pedestal_special (small copies of the HUD's pedestals)
  the countdown card cd_title, cd_count_box, cd_seat, cd_dot_0 .. cd_dot_3
  window animations  one animation per picture of the original that a piece replaces (same name, same first frame time), with the places in the window's own numbers

Places: every window's numbers are those of the original's 640 x 480 picture; the countdown card's are the card's own (its top left is (0, 0)): the game puts the card in the middle of the map view.
"""
from PIL import Image

import dialog_kit as K
import dialog_pieces as DP
import dialog_windows as DW
import orig_anim as OA
import shrink as SH

SHEET_W = 700                        # the drawing sheet of the browser (not the game's sheet)
PEDESTALS = (('qh_pedestal_move', 'pedestal_move_up', (217, 201)), ('qh_pedestal_special', 'pedestal_bomb_up', (474, 404)))
PEDESTAL_SIZE = (41, 56)
QH_CURSORS = (('cursor_curfd01', (226, 344)), ('cursor_c_normal', (244, 236)), ('cursor_c_normal', (501, 439)))     # the HUD's own cursor sprites that the quick help shows
DIALOG_CORNER = (100, 100)           # the dialogs' own numbers start here in the original's 640 x 480 picture
FULLSCREEN_AT = (125, 306)           # the countdown card's Fullscreen button, in the card's numbers
DIALOG_PLACE = {'yes': (180, 260), 'no': (292, 260), 'accept': (152, 260), 'decline': (284, 260), 'withdraw': (220, 260), 'return': (351, 425)}
BUTTON_ANIMS = (        # the original's button animations: (button, (names of the up, hover and down pictures), place)
    ('yes', ('yes1', 'yes2', 'yes3'), (180, 260)), ('yes', ('dyn_byes1', 'dyn_byes2', 'dyn_byes3'), (180, 260)),
    ('no', ('no1', 'no2', 'no3'), (292, 260)), ('no', ('dyn_bno1', 'dyn_bno2', 'dyn_bno3'), (292, 260)),
    ('accept', ('dad_bacc1', 'dad_bacc2', 'dad_bacc3'), (152, 260)), ('decline', ('dad_bdec1', 'dad_bdec2', 'dad_bdec3'), (284, 260)),
    ('withdraw', ('dw_bwith1', 'dw_bwith2', 'dw_bwith3'), (220, 260)),
    ('return', ('breturn1', 'breturn2', 'breturn3'), (351, 425)), ('return', ('qh_return1', 'qh_return2', 'qh_return3'), (529, 437)),
)
SWITCH_ANIMS = (        # (switch, its word, names of the pictures for the states up, hover, chosen, chosen with the pointer on it, down)
    ('chat_on', 'on', ('op_conu', 'op_conr', 'op_cond', 'op_condr', 'op_conh')), ('chat_off', 'off', ('op_coffu', 'op_coffr', 'op_coffd', 'op_coffdr', 'op_coffh')),
    ('help_on', 'on', ('op_honu', 'op_honr', 'op_hond', 'optondr', 'op_honh')), ('help_off', 'off', ('op_hoffu', 'op_hoffr', 'op_hoffd', 'op_hoffdr', 'op_hoffh')),
)
SWITCH_ORDER = ('up', 'hover', 'chosen', 'chosen_hover', 'down')
THUMB_STATES = ('up', 'hover', 'held')


def _ms(name):
    """the first frame's time of the original's animation `name` (1000 when the original has none of that name)"""
    entry = OA.TABLE.get(name)
    return entry['subitems'][0]['duration_ms'] if entry else 1000


def _row(name, w, h, x, y):
    return {'name': name, 'size': [w, h], 'rect_960x540': [x, y, w, h], 'anchor': ['left', 'top'], 'group': 'window'}


def _anim(ms, parts):
    return [{'ms': ms, 'parts': [{'sprite': s, 'x': x, 'y': y} for s, x, y in parts]}]


def _drawings():
    """({sprite name: (w, h, place x, place y, svg)} of the sprites that are drawn one by one, {layer: (w, h, svg, origin)} of the layers that are cut into pieces)"""
    one = {}

    def add(name, d, x, y):
        one[name] = (d[0], d[1], x, y, d[2])
    w, h, svg = DP.panel_probe()
    one['_panel'] = (w, h, 0, 0, svg)
    w, h, svg = DP.panel_clay()
    one['panel_c'] = (w, h, 0, 0, svg)
    card = lambda x, y: (x - DW.CARD[0], y - DW.CARD[1])                      # a place in the window's numbers as a place on the options card
    for kind in DP.DIALOG_BUTTONS:
        x, y = DIALOG_PLACE[kind]
        origin = card(x, y) if kind == 'return' else (x - DIALOG_CORNER[0], y - DIALOG_CORNER[1])
        for st in DP.STATES:
            add('dlg_btn_%s_%s' % (kind, st), DP.button_drawing(kind, st, origin), x, y)
    for st in ('up', 'down'):
        add('dlg_btn_fullscreen_' + st, DP.fullscreen_drawing(st, FULLSCREEN_AT), 0, 0)
    for word, switch in (('On', 'chat_on'), ('Off', 'chat_off')):
        x, y = DW.SWITCH_X[switch], DW.SWITCH_Y
        for st in DP.SWITCH_STATES:
            add('op_%s_%s' % (word.lower(), st), DP.switch_drawing(word, st, card(x, y)), x, y)
    x, y = DW.TRACK_X + 185, DW.SLIDER_Y[0] - DW.THUMB_ROW                    # the thumb at the sound slider's full setting
    for st in THUMB_STATES:
        add('op_thumb_' + st, DP.thumb_drawing(st, card(x, y)), x, y)
    add('cd_count_box', DW.countdown_count_box(), DW.COUNT_BOX[0] - 1, DW.COUNT_BOX[1] - 1)
    add('cd_seat', DW.countdown_seat(), 0, 0)
    for i in range(4):
        add('cd_dot_%d' % i, DW.countdown_dot(i), 0, 0)
    layers = {}
    for key, (w, h, svg), origin in (('op_part', DW.options_layer(), DW.CARD[:2]), ('qh_part', DW.quickhelp_layer(), (0, 0)), ('cd_title', DW.countdown_title(), (0, 0))):
        layers[key] = (w, h, svg, origin)
    return one, layers


def _shelf(sizes, width):
    """shelf packing of [(key, w, h)] on a sheet `width` wide, 2 pixels apart: ({key: (x, y)}, the height used)"""
    spots = {}
    x = y = rowh = 0
    for key, w, h in sorted(sizes, key=lambda s: (-s[2], s[0])):
        if x + w > width:
            x = 0
            y += rowh + 2
            rowh = 0
        spots[key] = (x, y)
        x += w + 2
        rowh = max(rowh, h)
    return spots, y + rowh


def _fitted(src, k, w, h):
    """a pack sprite made smaller to w x h canvas pixels at scale k: always from the 3x picture, so the small picture is smooth at every scale"""
    return src.resize((int(round(w * k)), int(round(h * k))), Image.LANCZOS)


def export(scales, images):
    """renders the windows' sprites: ({scale: {sprite name: RGBA picture of size * scale}}, the manifest rows, the animations, the names of the sprites whose 2x picture the game makes from the 3x one: all of them).
    The pictures are rounded to fewer levels where that cannot be seen (shrink.py). `images` is what the pack has so far (the pedestals and the cursors that the quick help shows are made from it):
    it must hold scale 3."""
    assert 3 in scales, 'the small pedestals are made from the 3x pictures'
    one, layers = _drawings()
    panel = one.pop('_panel')
    clay = one.pop('panel_c')
    # the big drawings are each rendered on a page of their own at its corner: a drawing is rendered a little differently (at the edges of the lettering) at another place of a page, and these are
    # the ones that carry the lettering and the panel
    solo = {'_panel': (panel[0], panel[1], panel[4]), 'panel_c': (clay[0], clay[1], clay[4])}
    solo.update({'layer:' + n: (d[0], d[1], d[2]) for n, d in layers.items()})
    sizes = [(n, d[0], d[1]) for n, d in one.items()]
    spots, height = _shelf(sizes, SHEET_W)
    pics = {}
    for k in scales:
        sheet = K.render([(spots[n][0], spots[n][1], d[4]) for n, d in one.items()], SHEET_W, height, k, 'dialogs')
        pics[k] = {key: sheet.crop((spots[key][0] * k, spots[key][1] * k, (spots[key][0] + w) * k, (spots[key][1] + h) * k)) for key, w, h in sizes}
        for key, (w, h, svg) in solo.items():
            pics[k][key] = K.render([(0, 0, svg)], w, h, k, 'dialogs_' + key.replace(':', '_'))
        print('dialogs', k, len(sizes) + len(solo), flush=True)
    for k in scales:
        err = DP.check_periodic(pics[k]['_panel'], k)
        if err > 1:
            raise SystemExit('the frame of the panel is not periodic at %dx beyond %d pixels from its edge (largest difference %d levels): raise dialog_pieces.E' % (k, DP.E, err))
    out = {k: {} for k in scales}
    rows = []
    # the panel: the clay, and the eight pieces of its frame
    for k in scales:
        out[k]['panel_c'] = pics[k]['panel_c']
    rows.append(_row('panel_c', clay[0], clay[1], 0, 0))
    for name, (x, y, w, h) in DP.panel_boxes().items():
        for k in scales:
            out[k]['panel_' + name] = pics[k]['_panel'].crop((x * k, y * k, (x + w) * k, (y + h) * k))
        rows.append(_row('panel_' + name, w, h, 0, 0))
    # the sprites that are drawn one by one
    for name, (w, h, x, y, _) in one.items():
        for k in scales:
            out[k][name] = pics[k][name]
        rows.append(_row(name, w, h, x, y))
    # the layers, cut into pieces
    parts = {}
    for key, (w, h, _, origin) in layers.items():
        mask = DP.empty_cells([(k, pics[k]['layer:' + key]) for k in scales])
        boxes = DP.xy_cut(mask, 10 ** 6 if key == 'cd_title' else DP.MIN_GAP)
        parts[key] = []
        for i, (x0, y0, x1, y1) in enumerate(boxes):
            name = key if key == 'cd_title' else '%s_%02d' % (key, i)
            for k in scales:
                out[k][name] = pics[k]['layer:' + key].crop((x0 * k, y0 * k, x1 * k, y1 * k))
            rows.append(_row(name, x1 - x0, y1 - y0, x0 + origin[0], y0 + origin[1]))
            parts[key].append((name, x0 + origin[0], y0 + origin[1]))
    # the small copies of the pedestals
    for name, src, (x, y) in PEDESTALS:
        for k in scales:
            out[k][name] = _fitted(images[3][src].convert('RGBA'), k, *PEDESTAL_SIZE)
        rows.append(_row(name, PEDESTAL_SIZE[0], PEDESTAL_SIZE[1], x, y))
    for cursor, _ in QH_CURSORS:
        assert cursor in images[scales[0]], 'the quick help shows the HUD sprite %s' % cursor
    return out, rows, _animations(parts), SH.shrink(out)


def _animations(parts):
    anims = {}
    for kind, names, (x, y) in BUTTON_ANIMS:
        for n, st in zip(names, DP.STATES):
            anims[n] = _anim(_ms(n), [('dlg_btn_%s_%s' % (kind, st), x, y)])
    for switch, word, names in SWITCH_ANIMS:
        for n, st in zip(names, SWITCH_ORDER):
            anims[n] = _anim(_ms(n), [('op_%s_%s' % (word, st), DW.SWITCH_X[switch], DW.SWITCH_Y)])
    anims['op_window'] = _anim(1000, parts['op_part'])
    anims['qh_window'] = _anim(1000, parts['qh_part'] + [(n, x, y) for n, _, (x, y) in PEDESTALS] + [(n, x, y) for n, (x, y) in QH_CURSORS])
    anims['cd_card'] = _anim(1000, parts['cd_title'] + [('cd_count_box', DW.COUNT_BOX[0] - 1, DW.COUNT_BOX[1] - 1)])
    return anims
