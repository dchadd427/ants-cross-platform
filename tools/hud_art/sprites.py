"""The sprite catalog of the HUD art: every drawn or carved piece, its place and anchor, and the animations of the original that the pieces replace.
Sprites are drawn for the 960 x 540 layout (the two tall pieces for 960 x 640, so that they hold a whole repeating tile) and rendered at 1x, 2x and 3x. The catalog gives for each sprite its
size and place in canvas pixels, the edge it is anchored to, and, where it repeats, the slice: `start` pixels kept at the near end, a `tile` that repeats (192 across or 128 down: the plate
texture's period), `end` pixels kept at the far end. pack.py makes the sheets and the manifest of the game from it."""
import json, os
from PIL import Image
from common import *
import pieces as PC, lettering as LET

V0 = 778
FRAMES = ['up', 'press1', 'press2', 'latched'] + ['rise%d' % n for n in range(6, 10)]

RASTER = {}        # sprites that are carvings, not drawings: name -> (kind, frame) for carve.pedestal_image

def ant_layers():
    """the ornament ants of the three pieces that carry them, as translucent reliefs at 6 samples per pixel (laid over the drawn sprite; they tuck under the window frames)"""
    import numpy as np, carve
    from relief import S
    out = {}
    dy = 160; sh = 100                                                        # the lower panel is drawn for 960 x 640
    ants = [(x - 778, y + (sh if y >= 417 else 0) - 268, hd, s, seed) for x, y, hd, s, seed in PC.ORNAMENTS + [PC.HEADER_ANT]]
    keep = np.ones((353 * S, 182 * S)); keep[:, :8 * S] = 0
    keep[29 * S:(29 + 105 + dy) * S, 20 * S:165 * S] = 0; keep[(422 + dy - 268) * S:(422 + dy - 268 + 16) * S, 20 * S:165 * S] = 0
    out['lower_panel'] = carve.ants_overlay(182, 353, ants, keep)
    keep = np.zeros((106 * S, 182 * S)); keep[:, 147 * S:] = 1
    out['minimap_frame'] = carve.ants_overlay(182, 106, [(163, 80, -24, 1.05, 12)], keep)
    ax, ay, hd, s, seed = PC.CORNER_ANT
    out['bottom_strip'] = carve.ants_overlay(943, 19, [(V0 - 17 + ax, ay, hd, s, seed)])
    return out

def catalog():
    """[(name, svg, entry)]: entry = the manifest row without the file names"""
    c = []
    def add(name, s, x, y, ax='left', ay='top', slice_=None, note=None, group=None):
        w, h = int(s.split('width="')[1].split('"')[0]), int(s.split('height="')[1].split('"')[0])
        e = {'name': name, 'size': [w, h], 'rect_960x540': [x, y, w if not (slice_ and slice_['axis'] == 'x') else w, h], 'anchor': [ax, ay]}
        if slice_: e['slice'] = slice_
        if note: e['note'] = note
        if group: e['group'] = group
        c.append((name, s, e))
    add('top_bar', PC.topbar(960, uid='s_tb'), 0, 0, 'stretch', 'top', {'axis': 'x', 'start': 140, 'tile': [140, 332], 'end': 320}, 'row 21 between x 16 and the panel is the map view\'s first row: draw the view over it', 'frame')
    add('left_strip', PC.left_strip(960, 640, uid='s_ls'), 0, 22, 'left', 'stretch', {'axis': 'y', 'start': 270, 'tile': [270, 398], 'end': 120}, 'drawn 618 tall; at 960 x 540 it is 518 tall', 'frame')
    add('bottom_strip', PC.bottom_strip(960, 540, uid='s_bs', windows=False), 17, 521, 'stretch', 'bottom', {'axis': 'x', 'start': 0, 'tile': [0, 192], 'end': 206}, 'the three score windows are separate (score_window)', 'frame')
    add('score_window', PC.score_window('s_sw'), 211, 523, 'left', 'bottom', None, 'three of them, at the game\'s score box x - 2 for the width (640: 105 254 402; 960: 213 468 722; 1200: 293 628 962; 1260: 313 668 1022; 1920: 533 1108 1682); the game fills the 54 x 14 field at (2, 1)', 'frame')
    add('lower_panel', PC.lower_panel(960, 640, uid='s_lp'), V0, 268, 'right', 'stretch', {'axis': 'y', 'start': 92, 'tile': [92, 220], 'end': 104}, 'drawn 353 tall; at 960 x 540 it is 253 tall. When the panel is shorter than start + end (4:3: 193), draw the start part, then the end part over it', 'frame')
    add('panel_top', PC.panel_top(960, uid='s_pt'), V0, 22, 'right', 'top', group='frame')
    add('minimap_frame', PC.minimap_frame(960, uid='s_mm'), V0, 30, 'right', 'top', note='the game draws the 119 x 91 minimap at (22, 5)', group='frame')
    add('card', PC.card_base(960, uid='s_cd'), V0, 136, 'right', 'top', note='the pedestals, their labels and Stop are drawn over it', group='frame')
    add('status_box', PC.status_box(960, uid='s_st'), V0, 252, 'right', 'top', group='frame')
    for nm, x in (('Help', 18), ('Options', 67), ('Quit', 121)):
        for st in ('up', 'hover', 'down'):
            add('button_%s_%s' % (nm.lower(), st), PC.button(nm, st, uid='s_b%s%s' % (nm, st)), V0 + x, 7, 'right', 'top', group='buttons')
    for nm, x in (('All', 74), ('Team', 121)):
        for st in ('up', 'hover', 'down'):
            add('send_%s_%s' % (nm.lower(), st), PC.all_button(state=st, uid='s_a%s%s' % (nm, st), name=nm), V0 + x, 503, 'right', 'bottom', group='buttons')
    for st in ('up', 'down'):
        add('stop_%s' % st, PC.stop_sprite(st, uid='s_sp' + st), V0 + 130, 172, 'right', 'top', group='card')
    for kind, lab, slot in PC.KINDS:
        add('label_%s' % kind, PC.label_sprite(lab, uid='s_l' + kind), V0 + 16 + 61 * slot, 136, 'right', 'top', group='card')
    for n in range(1, 6):
        add('pedestal_rise%d' % n, svg(56, 76, ''), V0 + 18, 156, 'right', 'top', note='shared by every kind; the right slot is 61 further right', group='pedestal'); RASTER['pedestal_rise%d' % n] = ('move', 'rise%d' % n)
    for kind, lab, slot in PC.KINDS:
        for fr in FRAMES:
            add('pedestal_%s_%s' % (kind, fr), svg(56, 76, ''), V0 + 18 + 61 * slot, 156, 'right', 'top', group='pedestal'); RASTER['pedestal_%s_%s' % (kind, fr)] = (kind, fr)
    # the remaining HUD frames (carved): the rim flash, the tray's egg and its hatching, the lunch box
    import carve
    for n in (1, 2, 3):
        add('pedestal_flash%d' % n, svg(56, 76, ''), V0 + 18, 156, 'right', 'top', note='drawn over a standing pedestal; the right slot is 61 further right', group='pedestal'); RASTER['pedestal_flash%d' % n] = (lambda n=n: carve.flash_image(n))
    add('egg', svg(12, 16, ''), V0 + 78, 158, 'right', 'top', note='the tray holds 1 to 9 of them: see animations egg1 .. egg9', group='card'); RASTER['egg'] = lambda: carve.egg_image(0)
    add('egg_hatch1', svg(12, 16, ''), V0 + 78, 158, 'right', 'top', group='card'); RASTER['egg_hatch1'] = lambda: carve.egg_image(1)
    add('egg_hatch2', svg(17, 14, ''), V0 + 75, 159, 'right', 'top', group='card'); RASTER['egg_hatch2'] = carve.egg_open_image
    add('egg_hatch3', svg(19, 6, ''), V0 + 73, 167, 'right', 'top', group='card'); RASTER['egg_hatch3'] = carve.egg_bits_image
    add('lunch_box', svg(34, 40, ''), V0 + 139, 131, 'right', 'top', note='shown while every selected ant carries food', group='card'); RASTER['lunch_box'] = carve.lunch_box_image
    return c

def animations():
    """every HUD animation of the original that this set replaces: its name, and its frames (each: the original's time in ms and the sprites that make the frame, with their place at 960 x 540; x moves with the panel)"""
    import orig_anim as OA
    T = OA.TABLE
    def ms(name): return [s['duration_ms'] for s in T[name]['subitems']]
    out = {}
    def P(sprite, x, y): return {'sprite': sprite, 'x': x, 'y': y}
    for nm, o, x in (('help', 'buthlp', 18), ('options', 'butopt', 67), ('quit', 'butqit', 121)):
        for st, suf in (('up', 'u'), ('hover', 'r'), ('down', 'd')): out[o + suf] = [{'ms': ms(o + suf)[0], 'parts': [P('button_%s_%s' % (nm, st), V0 + x, 7)]}]
    for nm, o, x in (('all', 'butall', 74), ('team', 'butals', 121)):
        for st, suf in (('up', 'u'), ('hover', 'r'), ('down', 'd')): out[o + suf] = [{'ms': ms(o + suf)[0], 'parts': [P('send_%s_%s' % (nm, st), V0 + x, 503)]}]
    out['butcanu'] = [{'ms': 1000, 'parts': [P('stop_up', V0 + 130, 172)]}]; out['butcand'] = [{'ms': 1000, 'parts': [P('stop_down', V0 + 130, 172)]}]
    ON = {'move': 'mov', 'ally': 'aly', 'hatch': 'egg', 'attack': 'att', 'bomb': 'bom', 'fire': 'fir', 'thief': 'thf', 'swim': 'swm'}
    for kind, lab, slot in PC.KINDS:
        o = ON[kind]; x = V0 + 18 + 61 * slot; lx = V0 + 16 + 61 * slot
        def fr(name, t, label=True): return {'ms': t, 'parts': ([P('label_' + kind, lx, 136)] if label else []) + [P(name, x, 156)]}
        up = 'butswmup' if kind == 'swim' else 'but%su' % o
        out[up] = [fr('pedestal_%s_up' % kind, ms(up)[0])]
        out['but%sd' % o] = [fr('pedestal_%s_latched' % kind, ms('but%sd' % o)[0])]
        out['but%s2d' % o] = [fr('pedestal_%s_press%d' % (kind, i + 1), t) for i, t in enumerate(ms('but%s2d' % o))]
        rise = ['pedestal_rise%d' % n for n in range(1, 6)] + ['pedestal_%s_rise%d' % (kind, n) for n in range(6, 10)]
        ru = 'trnbwalu' if kind == 'fire' else 'trnb%su' % o
        if ru in T: out[ru] = [fr(n, 60, False) for n in rise]
        if 'trnb%sd' % o in T: out['trnb%sd' % o] = [fr(n, 60, False) for n in reversed(rise)]
        if 'trna%sd' % o in T: out['trna%sd' % o] = [fr(n, 60, False) for n in (rise[8], rise[7], rise[6], rise[5], rise[4])]          # the original has 5 frames here, and so has this
        if 'trna%su' % o in T: out['trna%su' % o] = [fr(n, 60, False) for n in rise[5:]]                                            # and 4 here
    out['butgone'] = [{'ms': 1000, 'parts': [P('pedestal_rise1', V0 + 18, 156)]}]
    for side, slot in (('l', 0), ('r', 1)):
        out['butdef' + side] = [{'ms': t, 'parts': [P('pedestal_flash%d' % n, V0 + 18 + 61 * slot, 156)]} for t, n in zip(ms('butdef' + side), (3, 2, 1, 2))]
    for n in range(1, 10):
        out['egg%d' % n] = [{'ms': 1000, 'parts': [P('egg', p[0] + 320, p[1]) for p in OA.cat.ANIMS['egg%d' % n][0]]}]
        fs = OA.cat.ANIMS['egg%dh' % n]
        out['egg%dh' % n] = [{'ms': t, 'parts': [P('egg_hatch%d' % (i + 1), f[0][0] + 320, f[0][1])]} for i, (t, f) in enumerate(zip(ms('egg%dh' % n), fs))]
    out['UI_LBOX'] = [{'ms': 1000, 'parts': [P('lunch_box', 597 + 320, 131)]}]
    return out

def export(scales=(1, 2, 3)):
    """renders the catalog: ({scale: {sprite name: RGBA picture of size * scale}}, the catalog's rows, the animations)"""
    cat = catalog()
    # shelf packing on a sheet 1000 wide (the drawing sheet of the browser, not the game's sheet)
    place = []; x = y = rowh = 0
    for name, s, e in cat:
        w, h = e['size']
        if x + w > 1000: x = 0; y += rowh + 2; rowh = 0
        place.append((x, y)); x += w + 2; rowh = max(rowh, h)
    H = y + rowh
    import relief, carve
    out = {}
    layers = None
    for k in scales:
        sheet = render_sheet([(px, py, s) for (px, py), (_, s, _) in zip(place, cat)], 1000, H, k, tag='sprites', css=LET.font_css('lb'))
        layers = ant_layers() if layers is None else layers
        out[k] = {}
        for (px, py), (name, s, e) in zip(place, cat):
            w, h = e['size']
            im = sheet.crop((px * k, py * k, (px + w) * k, (py + h) * k))
            if name in RASTER: im = relief.down(RASTER[name]() if callable(RASTER[name]) else carve.pedestal_image(*RASTER[name]), k)
            if name in layers: im.alpha_composite(relief.down(layers[name], k))
            out[k][name] = im
        print('sprites', k, len(cat), flush=True)
    return out, [e for _, _, e in cat], animations()
