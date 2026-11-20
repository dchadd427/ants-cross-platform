#!/usr/bin/env python3
"""Composes the HUD art's frame at one shape and scale from the committed sheets, as an independent reference for the game's own drawing (tests/test_app/test_hud_art_look.cpp).

usage: reference_frame.py WIDTH HEIGHT [--scale 1|2|3] [--art DIR] --out FILE.png

    python3 tools/hud_art/reference_frame.py 960 540 --out tests/data/hud_art/frame_16x9_1x.png

It follows only the rules that include/ants_app/hud_art.hpp states for the manifest (anchors, slices) and the picture's own layout: the frame's eight pieces; the pieces that stand at rest on an
idle match screen (the help, options and quit buttons, the Fullscreen button, the All button, the three score windows of the other teams). The map view is left transparent. Pure Python 3 with Pillow and numpy.
"""
import argparse
import os

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
# the x of the three bottom score boxes at each canvas width (the game's ScreenLayout::score_slot: the boxes of the original at 105, 254 and 402 moved by the strip's three cuts); the window is 2 left of the box
BOTTOM_BOXES = {640: (105, 254, 402), 960: (213, 468, 722), 1200: (293, 628, 962), 1260: (313, 668, 1022), 1920: (533, 1108, 1682)}


def read_manifest(folder):
    sheets, sprites, size = {}, {}, None
    with open(os.path.join(folder, 'hud_art.txt')) as f:
        for line in f:
            w = line.split()
            if not w:
                continue
            if w[0] == 'sheet':
                sheets[int(w[1])] = w[2]
            elif w[0] == 'sprite':
                s = dict(name=w[1], w=int(w[2]), h=int(w[3]), sx=int(w[4]), sy=int(w[5]), x=int(w[6]), y=int(w[7]), ax=w[8], ay=w[9], slice=None)
                if len(w) > 10:
                    s['slice'] = dict(axis=w[10], start=int(w[11]), t0=int(w[12]), t1=int(w[13]), end=int(w[14]))
                sprites[w[1]] = s
    return sheets, sprites


def compose(width, height, scale, folder):
    sheets, sprites = read_manifest(folder)
    sheet = np.asarray(Image.open(os.path.join(folder, sheets[scale])).convert('RGBA'))
    out = Image.new('RGBA', (width * scale, height * scale), (0, 0, 0, 0))
    k = scale

    def crop(s, x0, y0, w, h):
        return Image.fromarray(sheet[(s['sy'] + y0) * k:(s['sy'] + y0 + h) * k, (s['sx'] + x0) * k:(s['sx'] + x0 + w) * k])

    def put(img, x, y):
        out.alpha_composite(img, (x * k, y * k))

    def at(s):
        return s['x'] + (width - 960 if s['ax'] == 'right' else 0), s['y'] + (height - 540 if s['ay'] == 'bottom' else 0)

    def stretched(name, length):
        s = sprites[name]
        x, y = at(s)
        sl = s['slice']
        horizontal = sl['axis'] == 'x'
        full = s['w'] if horizontal else s['h']
        def piece(frm, n, pos):
            if n <= 0:
                return
            if horizontal:
                put(crop(s, frm, 0, n, s['h']), x + pos, y)
            else:
                put(crop(s, 0, frm, s['w'], n), x, y + pos)
        piece(0, min(sl['start'], length), 0)
        pos = sl['start']
        while pos < length - sl['end']:
            n = min(sl['t1'] - sl['t0'], length - sl['end'] - pos)
            piece(sl['t0'], n, pos)
            pos += n
        piece(full - min(sl['end'], length), min(sl['end'], length), length - min(sl['end'], length))

    def whole(name, x=None, y=None):
        s = sprites[name]
        px, py = at(s)
        put(crop(s, 0, 0, s['w'], s['h']), px if x is None else x, py if y is None else y)

    stretched('top_bar', 960 + width - 960)
    stretched('left_strip', 540 - 22 + height - 540)
    stretched('bottom_strip', 943 + width - 960)
    stretched('lower_panel', 540 - 268 - 19 + height - 540)
    for name in ('panel_top', 'minimap_frame', 'card', 'status_box'):
        whole(name)
    for box in BOTTOM_BOXES[width]:
        whole('score_window', box - 2, height - 17)
    for name in ('button_help_up', 'button_options_up', 'button_quit_up', 'button_fullscreen_up', 'send_all_up'):
        whole(name)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('width', type=int)
    ap.add_argument('height', type=int)
    ap.add_argument('--scale', type=int, default=1, choices=(1, 2, 3))
    ap.add_argument('--art', default=os.path.join(HERE, '..', '..', 'assets', 'hud_art'))
    ap.add_argument('--out', required=True)
    args = ap.parse_args()
    compose(args.width, args.height, args.scale, args.art).save(args.out, optimize=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
