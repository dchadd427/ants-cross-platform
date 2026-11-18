"""Every element at the original's measured rectangle; one bevel system (system.py).
Footprints (16:9, 960 x 540; the right-hand pieces are anchored to the right edge, V = W - 182):
  1  top bar (0, 0, W, 22), bottom strip (17, H - 19, W - 17, 19)
  2  panel top plate (V, 22, 182, 8), minimap frame (V, 30, 182, 106)
  3  card (V, 136, 182, 117), status box (V, 253, 182, 16)
  4  buttons Help (V + 18, 7), Options (V + 67, 7), Quit (V + 121, 7), each 23 high; faces 41 / 46 / 41 x 18 so that the gaps between them are both 8
The frame's edge bands (chamfer, ridge, rail) are the original's own columns and rows, so that every piece meets its neighbors on the same pixels."""
from common import P, svg
import system as S
from system import rgb, poly, rrect, raised, sunken, bands, vgrad, hgrad, glyph, ink_defs
import texture as tex2
import lettering as LET
import ants as ANT

LETTER = 'lb'          # which open-licensed face the labels are set in (lettering.py)

K_ = (0, 0, 0)
CHAMFER = [K_, K_, (19, 51, 35), (27, 67, 43), (23, 59, 39), (19, 51, 35), (19, 51, 35), (23, 83, 63), (43, 99, 87), (31, 127, 87)]      # from the view outward: rows 21 .. 12 of the top bar (cols 16 .. 7 of the left strip)
RIDGE = [K_, K_, (19, 71, 47), (31, 127, 87), (115, 191, 155), (51, 143, 103), (51, 143, 103), (23, 119, 79)]                              # the bottom strip's rows 0 .. 7
RAIL = {0: K_, 1: K_, 2: (27, 71, 53), 3: (51, 143, 103), 4: (115, 191, 155), 5: (59, 151, 111), 6: (39, 135, 95), 7: (23, 119, 79)}      # the panel's rail: black view border and the ridge (local columns, 0 = x 778), at every y from 22 down
GROOVE = {13: (23, 115, 79), 14: (31, 127, 87), 15: (31, 127, 87), 16: (19, 99, 75), 17: (19, 55, 47), 18: (23, 59, 39), 19: (23, 59, 39), 20: (27, 75, 63), 21: K_}      # the minimap pocket's left wall, y 30 .. 126 only (the original has no groove anywhere else)
SURF_H = 540
BOTTOM_BOXES = {640: (105, 254, 402), 960: (213, 468, 722), 1200: (293, 628, 962), 1260: (313, 668, 1022), 1920: (533, 1108, 1682)}   # the bottom score boxes' left edges per picture width (docs/VIEW_AND_HUD.md)

# ---- the plate surface, in absolute canvas coordinates so that neighbors carry the same sheen, veins and gradient --------------------------------------------
def surf_defs(uid, ox, oy):
    t = 'patternTransform="translate(%d %d)"' % (-ox % 192, -oy % 128)
    d = vgrad(uid + '_bg', -oy, SURF_H - oy, [(0, '#24664a'), (.5, '#246449'), (1, '#256250')])
    d += ('<linearGradient id="%s_b1" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#5fc9a0" stop-opacity="0"/><stop offset=".5" stop-color="#5fc9a0" stop-opacity=".15"/><stop offset="1" stop-color="#5fc9a0" stop-opacity="0"/></linearGradient>'
          '<linearGradient id="%s_b2" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#0f3a2a" stop-opacity="0"/><stop offset=".5" stop-color="#0f3a2a" stop-opacity=".17"/><stop offset="1" stop-color="#0f3a2a" stop-opacity="0"/></linearGradient>'
          '<filter id="%s_sb" x="-20%%" y="-20%%" width="140%%" height="140%%"><feGaussianBlur stdDeviation="5"/></filter>' % (uid, uid, uid))
    d += ('<pattern id="%s_sh" width="192" height="128" patternUnits="userSpaceOnUse" %s><g filter="url(#%s_sb)">'
          '<path d="M-20 128 L50 -4 L92 -4 L22 128Z M104 128 L160 -4 L186 -4 L132 128Z M172 128 L212 -4 L240 -4 L200 128Z" fill="url(#%s_b1)"/>'
          '<path d="M-60 128 L-8 -4 L22 -4 L-34 128Z M60 128 L112 -4 L130 -4 L78 128Z" fill="url(#%s_b2)"/></g></pattern>' % (uid, t, uid, uid, uid))
    d += '<pattern id="%s_vn" width="192" height="128" patternUnits="userSpaceOnUse" %s><image href="%s" width="192" height="128"/></pattern>' % (uid, t, tex2.veins(amp=12, blur=2.2))
    d += '<pattern id="%s_sp" width="64" height="64" patternUnits="userSpaceOnUse" patternTransform="translate(%d %d)"><image href="%s" width="64" height="64"/></pattern>' % (uid, -ox % 64, -oy % 64, tex2.speck(amp=5))
    d += ('<linearGradient id="%s_lt" gradientUnits="userSpaceOnUse" x1="%d" y1="%d" x2="%d" y2="%d"><stop offset="0" stop-color="#a8ffd8" stop-opacity=".07"/><stop offset=".6" stop-color="#a8ffd8" stop-opacity="0"/></linearGradient>'
          % (uid, -ox, -oy, 900 - ox, 520 - oy))
    return d

def surf(uid, x, y, w, h):
    return ''.join('<rect x="%s" y="%s" width="%s" height="%s" fill="url(#%s_%s)"/>' % (x, y, w, h, uid, k) for k in ('bg', 'sh', 'vn', 'sp', 'lt'))

def rail_cols(y, h, cols=None):
    return '<g shape-rendering="crispEdges">%s</g>' % ''.join('<rect x="%d" y="%s" width="1" height="%s" fill="%s"/>' % (c, y, h, rgb(col)) for c, col in RAIL.items())

WALL_TL = {'L': ['#173b27', '#132b1b'], 'T': ['#23472f'], 'R': ['#17774f', '#3f9c76'], 'B': ['#13634b', '#3f9c76']}     # the windows' walls: dark above and left, lit below and right (the original's widths: left 2, top 1, right 1 or 2, bottom 2)

def window(uid, field, widths):
    """a recessed display at the original's field rectangle; the walls have the original's own widths"""
    return S.walls(field, widths, WALL_TL) + rrect(field[0], field[1], field[2], field[3], 0, '#000')

def hband(x, y, w, color):
    return '<rect x="%s" y="%s" width="%s" height="1" fill="%s"/>' % (x, y, w, color)

# ---- 1  top bar -----------------------------------------------------------------------------------------------------------------------------------------
def topbar(W, uid='tb'):
    V = W - 182
    defs = surf_defs(uid, 0, 0) + LET.defs(uid + 't')
    b = [surf(uid, 0, 0, W, 22)]
    for y, c in enumerate(['#13634b', '#13634b', '#2b6b5f', '#2b6b5f']):          # the top rim: rows 0 .. 3
        b.append(hband(0, y, W, c))
    # chamfer along the view's top edge: the original's ten rows, from x = 17 to the mitre
    ch = [rgb(c) for c in CHAMFER]
    # where the chamfer's rows meet the panel rail's columns at the view's upper right corner: the original ends its dark rows on a steep step (about two rows per column), so the dark corner is
    # small and the rail's bright column comes up to row 19. EDGE[b] is how far row 21 - b runs past the panel's left edge V (the original's extents)
    EDGE = [0, 2, 3, 4, 4, 5, 5, 6, 6, 7]
    g = '<g shape-rendering="crispEdges">'
    for bi in range(10):
        if bi < 2:                                                                  # the two black rows stop at the view's corner; the pixels beyond are the chamfer's dark green
            g += hband(17, 21 - bi, V + 1 - bi - 17, ch[bi]) + hband(V + 1 - bi, 21 - bi, EDGE[bi] + bi, ch[2])
        else:
            g += hband(17, 21 - bi, V + EDGE[bi] + 1 - 17, ch[bi])
            if bi <= 7: g += hband(V + EDGE[bi], 21 - bi, 1, '#1b4b3f')                 # each dark row ends on one pixel of the original's dark teal (27, 75, 63), as the original's rows do: the step is soft, not a black saw
    for c in range(2, 8):
        b0 = next(bi for bi in range(10) if EDGE[bi] >= c)                          # the lowest chamfer row that covers this column; the rail's column stands under it
        if b0 > 0: g += '<rect x="%d" y="%d" width="1" height="%d" fill="%s"/>' % (V + c, 22 - b0, b0, rgb(RAIL[c]))
    b.append(g + '</g>')
    # the rounded corner at the left: concentric quarter rings about (17, 22), one per band, ending on the left strip's own columns
    defs += ('<linearGradient id="%s_lp" gradientUnits="userSpaceOnUse" x1="17" y1="11" x2="7" y2="22"><stop offset="0" stop-color="%s"/><stop offset="1" stop-color="%s"/></linearGradient>'
             '<linearGradient id="%s_lp2" gradientUnits="userSpaceOnUse" x1="17" y1="11" x2="7" y2="22"><stop offset="0" stop-color="%s"/><stop offset="1" stop-color="%s"/></linearGradient>'
             % (uid, ch[9], rgb((43, 95, 67)), uid, ch[8], rgb((43, 95, 67))))
    for bi in range(10):
        r = bi + .5
        col = 'url(#%s_lp)' % uid if bi == 9 else ('url(#%s_lp2)' % uid if bi == 8 else ch[bi])
        b.append('<path d="M%s 22 A%s %s 0 0 1 17 %s" fill="none" stroke="%s" stroke-width="1"/>' % (17 - r, r, r, 22 - r, col))
    # windows: the clock (digits 62 .. 131) and the local score (the game's 54 x 14 box at W - 238)
    b.append(window(uid + 'wc', (61, 4, 68, 14), (2, 1, 2, 2)))                      # the clock's field: x 61 .. 128, y 4 .. 17
    b.append(window(uid + 'ws', (W - 238, 4, 54, 14), (2, 1, 1, 2)))                  # the local score's field (the game fills it)
    b.append(LET.label('Time', LETTER, uid + 't'))
    return svg(W, 22, ''.join(b), defs)

def bottom_strip(W, H, uid='bs', windows=True):
    """the strip under the view: the original's ridge rows from x = 17 to the view's right edge, the quarter rings that turn the ridge up into the panel's rail (about the view's corner, the rings end on the rail's own columns),
    and no further: the plate to the right of the corner stays as it is (the game draws the version and the frame rate on it)"""
    V = W - 182
    w = W - 17
    cx = V - 17
    boxes = BOTTOM_BOXES[W]
    defs = surf_defs(uid, 17, H - 19)
    b = [surf(uid, 0, 0, w, 19)]
    b.append('<g shape-rendering="crispEdges">%s</g>' % ''.join(hband(0, i, cx, rgb(c)) for i, c in enumerate(RIDGE)))
    rail = [RAIL[c] for c in range(8)]
    for i in range(8):
        r = i + .5
        if i < 2:
            col = rgb(K_)
        else:
            defs += ('<linearGradient id="%s_a%d" gradientUnits="userSpaceOnUse" x1="%s" y1="0" x2="%s" y2="%s"><stop offset="0" stop-color="%s"/><stop offset="1" stop-color="%s"/></linearGradient>'
                     % (uid, i, cx + r, cx, r, rgb(rail[i]), rgb(RIDGE[i])))
            col = 'url(#%s_a%d)' % (uid, i)
        b.append('<path d="M%s 0 A%s %s 0 0 1 %s %s" fill="none" stroke="%s" stroke-width="1"/>' % (cx + r, r, r, cx, r, col))
    for i, bx in enumerate(boxes if windows else ()):
        b.append(window(uid + 'w%d' % i, (bx - 17, 3, 54, 14), (2, 1, 1, 2)))
    ax, ay, hd, s, seed = CORNER_ANT
    return svg(w, 19, ''.join(b), defs)

def score_window(uid='sw'):
    """a bottom score window alone: 57 x 17, the game's 54 x 14 field at (2, 1); its place is (box x - 2, H - 17)"""
    return svg(57, 17, window(uid, (2, 1, 54, 14), (2, 1, 1, 2)))

# ---- 2  panel top plate and minimap frame -------------------------------------------------------------------------------------------------------
def panel_top(W, uid='pt'):
    V = W - 182
    defs = surf_defs(uid, V, 22)
    return svg(182, 8, surf(uid, 0, 0, 182, 8) + rail_cols(0, 8), defs)

DARK_RAMP = ['#2b573f', '#133323', '#173b27', '#23472f']          # the pocket's top wall, y 30 .. 33, outer to inner (the original's rows)
LIT_RAMP = ['#1f7f57', '#3f9c76', '#73c9a4', '#2a8563']            # its right wall x 923 .. 920 and bottom wall y 130 .. 127, outer to inner

def ant_paths():
    legs = ('M13.3 11.6 Q8 8.6 4.2 11.4 L3 15.6 M16.7 11.6 Q22 8.6 25.8 11.4 L27 15.6 M12.9 14.6 Q6.8 15 3.8 19.6 L4.4 24 M17.1 14.6 Q23.2 15 26.2 19.6 L25.6 24 '
            'M13.3 17.6 Q9.4 21.4 9.2 27 M16.7 17.6 Q20.6 21.4 20.8 27')
    return legs, 'M13.8 5.4 Q10.4 1.8 6.6 3.4 M16.2 5.4 Q19.6 1.8 23.4 3.4'

def ant_ornament(uid, x, y, raw=False):
    """a raised line-art ant (30 x 30): shadow below right, light above left, the ant's own color between"""
    legs, ant = ant_paths()
    line = '<path d="%s %s"/>' % (legs, ant)
    body = '<ellipse cx="15" cy="7.4" rx="3.1" ry="3.3"/><ellipse cx="15" cy="14.6" rx="2.7" ry="3.5"/><ellipse cx="15" cy="23" rx="4.1" ry="5.2"/>'
    g = '<g>' if raw else '<g transform="translate(%s %s) scale(.9)">' % (x, y)
    g += '<g fill="none" stroke-linecap="round" stroke-linejoin="round"><g transform="translate(1 1)" stroke="#06201a" stroke-opacity=".7" stroke-width="1.5">%s</g><g transform="translate(-.5 -.5)" stroke="#9fe3c3" stroke-opacity=".8" stroke-width="1.3">%s</g><g stroke="#3a9a74" stroke-width="1.25">%s</g></g>' % (line, line, line)
    g += '<g transform="translate(1 1)" fill="#06201a" fill-opacity=".7">%s</g><g transform="translate(-.5 -.5)" fill="#a8ecca" fill-opacity=".85">%s</g>' % (body, body)
    g += '<defs><radialGradient id="%s_ab" cx=".35" cy=".3" r=".8"><stop offset="0" stop-color="#58c79b"/><stop offset="1" stop-color="#1f6a4d"/></radialGradient></defs><g fill="url(#%s_ab)">%s</g>' % (uid, uid, body)
    return g + '</g>'

def minimap_frame(W, uid='mm'):
    """(V, 30, 182, 106). The pocket as the original has it: the image's black frame (799, 34, 121, 93); a top wall y 30 .. 33 from x 794; a lit right wall x 920 .. 923 and a lit bottom wall
    y 127 .. 130 (from x 791); the left wall is the groove, x 791 .. 798, y 30 .. 126 (nowhere else does the original have a groove). All coordinates below are local: x - V, y - 30."""
    V = W - 182
    defs = surf_defs(uid, V, 30) + LET.defs(uid + 'm')
    b = [surf(uid, 0, 0, 182, 106), rail_cols(0, 106)]
    # left wall: lip (c13 .. c15) and groove (c16 .. c20) down to y 126, black c21 is the image frame's own column
    g = ''
    for c, col in GROOVE.items():
        if c == 21: continue
        g += '<rect x="%d" y="0" width="1" height="%d" fill="%s"/>' % (c, (100 - (c - 13)) if c <= 15 else 97, rgb(col))      # the lit lip (columns 13 .. 15) runs on down and turns into the bottom wall
    b.append('<g shape-rendering="crispEdges">%s</g>' % g)
    # top wall: four one-pixel rows from the groove to the right wall, mitred at the right (the corner is rounded by the right wall's own columns)
    defs += '<clipPath id="%s_pk"><path d="M13 0 H143 A3 3 0 0 1 146 3 V98 A3 3 0 0 1 143 101 H13Z"/></clipPath>' % uid
    w = ''
    for j in range(4):
        d, l = DARK_RAMP[j], LIT_RAMP[j]
        w += poly([(18, j), (146 - j, j), (145 - j, j + 1), (18, j + 1)], d)                              # top ring j (x 794 ..)
        w += poly([(146 - j, j), (146 - j, 101 - j), (145 - j, 100 - j), (145 - j, j + 1)], l)           # right ring j
        w += poly([(13 + j, 101 - j), (146 - j, 101 - j), (145 - j, 100 - j), (13 + j, 100 - j)], l)     # bottom ring j: starts one pixel further right per ring, a mitre with the lip's columns
    b.append('<g clip-path="url(#%s_pk)">%s</g>' % (uid, w))
    b.append('<rect x="21" y="4" width="121" height="93" fill="#000"/>')
    b.append(LET.label('Map', LETTER, uid + 'm'))
    return svg(182, 106, ''.join(b), defs)

# ---- 3  card and status box ---------------------------------------------------------------------------------------------------------------------------
def runner(uid, x, y):
    """the running ant on the pedestal (23 x 29): head, two body blobs, four limbs and two feelers"""
    limbs = 'M12.6 15.2 L17.6 17.8 L16.8 24.6 M6.8 20.2 L3.4 23.8 L1.8 28 M13.2 10.4 L18.6 10.6 L21.8 8 M9.8 11.2 L4.8 12.8 L2.8 9.6 M16.8 2 Q19.4 -.2 22 .6 M19 4.2 Q21.6 3.6 22.6 5.4'
    head = '<ellipse cx="16.2" cy="5.4" rx="3.9" ry="3.6"/>'
    body = '<ellipse cx="11.2" cy="12.6" rx="4.6" ry="3.3" transform="rotate(-40 11.2 12.6)"/><ellipse cx="6.4" cy="18.4" rx="4.7" ry="3.5" transform="rotate(-40 6.4 18.4)"/>'
    g = '<g transform="translate(%s %s)" stroke-linecap="round" stroke-linejoin="round">' % (x, y)
    g += '<defs><radialGradient id="%s_hd" cx=".3" cy=".25" r=".9"><stop offset="0" stop-color="#d4fff0"/><stop offset=".5" stop-color="#6fd1a8"/><stop offset="1" stop-color="#1f7a58"/></radialGradient>' % uid
    g += '<linearGradient id="%s_lm" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#9ff0cd"/><stop offset="1" stop-color="#2a8c66"/></linearGradient></defs>' % uid
    g += '<g transform="translate(1 1)" fill="#021a10" fill-opacity=".6" stroke="#021a10" stroke-opacity=".6" stroke-width="2.2">%s%s<path d="%s" fill="none"/></g>' % (head, body, limbs)
    g += '<path d="%s" fill="none" stroke="url(#%s_lm)" stroke-width="2.6"/><g fill="url(#%s_hd)">%s%s</g>' % (limbs, uid, uid, head, body)
    g += '<ellipse cx="15" cy="3.6" rx="2" ry="1" fill="#fff" fill-opacity=".7"/><ellipse cx="10.2" cy="11.2" rx="2.6" ry="1.1" fill="#fff" fill-opacity=".5" transform="rotate(-40 10.2 11.2)"/>'
    return g + '</g>'

GREEN = ('#d4fff0', '#6fd1a8', '#1f7a58', '#9ff0cd', '#2a8c66')
GOLD = ('#fff6c0', '#f0c93a', '#8a6400', '#ffe488', '#a07a08')

def emboss(uid, fills='', strokes='', sw=2.2, pal=GREEN, extra=''):
    """one pedestal picture in the runner's hand: a soft shadow one pixel down and right, rounded limbs in a light-to-dark gradient, blobs lit from the top left"""
    g = ('<defs><radialGradient id="%s_hd" cx=".3" cy=".25" r=".9"><stop offset="0" stop-color="%s"/><stop offset=".5" stop-color="%s"/><stop offset="1" stop-color="%s"/></radialGradient>'
         '<linearGradient id="%s_lm" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="%s"/><stop offset="1" stop-color="%s"/></linearGradient></defs>' % (uid, pal[0], pal[1], pal[2], uid, pal[3], pal[4]))
    st = ('<path d="%s" fill="none"/>' % strokes) if strokes else ''
    g += '<g transform="translate(1 1)" fill="#021a10" fill-opacity=".6" stroke="#021a10" stroke-opacity=".6" stroke-width="%s" stroke-linecap="round" stroke-linejoin="round">%s%s</g>' % (sw + .1, fills, st)
    if strokes: g += '<path d="%s" fill="none" stroke="url(#%s_lm)" stroke-width="%s" stroke-linecap="round" stroke-linejoin="round"/>' % (strokes, uid, sw)
    g += '<g fill="url(#%s_hd)">%s</g>%s' % (uid, fills, extra)
    return g

def mini_ant(uid, x, y, s, pal, flip=False):
    """a small standing ant figure for the TeamUp picture"""
    fills = '<ellipse cx="8" cy="4" rx="3.6" ry="3.4"/><ellipse cx="8" cy="11" rx="3" ry="3.4"/><ellipse cx="8" cy="18.4" rx="3.6" ry="4.2"/>'
    strokes = 'M5.4 10 L1.2 7.4 M10.6 10 L15.4 12.6 M5.6 13 L1.6 16 M10.4 13 L14.4 16.6 M6 1.4 Q4 -1.2 1.6 -.2 M10 1.4 Q12 -1.2 14.4 -.2'
    t = 'translate(%s %s) scale(%s %s)' % (x + (16 * s if flip else 0), y, -s if flip else s, s)
    return '<g transform="%s">%s</g>' % (t, emboss(uid, fills, strokes, 2.0, pal))

def icon(uid, kind):
    """the picture on a pedestal, in a 34 x 30 box (the sunken panel's inside)"""
    if kind == 'move':
        return '<g transform="translate(-4.7 -4.4) scale(.9)">%s</g>' % runner(uid, 12, 7)
    if kind == 'ally':            # TeamUp: two ants side by side, the second one gold (the other team's)
        link = '<path d="M14.6 16.4 Q17 18.4 19.4 16.4" fill="none" stroke="#ffe488" stroke-width="1.6" stroke-linecap="round"/>'
        return mini_ant(uid + 'a', 3.5, 5.4, .92, GREEN) + mini_ant(uid + 'b', 16.5, 5.4, .92, GOLD, flip=True) + link
    if kind == 'hatch':           # an egg with a crack
        fills = '<ellipse cx="17" cy="15" rx="8.6" ry="11.2" transform="rotate(28 17 15)"/>'
        ex = ('<path d="M10.6 12.4 L15 14.6 L13.2 17.6 L18.2 19.2 L17 22.6" fill="none" stroke="#0b3526" stroke-width="1.3" stroke-linejoin="round" stroke-linecap="round"/>'
              '<path d="M10.1 11.9 L14.5 14.1 L12.7 17.1 L17.7 18.7" fill="none" stroke="#e6fff6" stroke-opacity=".55" stroke-width=".6" stroke-linejoin="round" stroke-linecap="round"/>'
              '<ellipse cx="13.6" cy="8.6" rx="3.4" ry="1.7" fill="#fff" fill-opacity=".5" transform="rotate(-38 13.6 8.6)"/>')
        return emboss(uid, fills, '', 2, GREEN, ex)
    if kind == 'attack':          # a combat ant, front view, both arms flexed
        fills = ('<ellipse cx="17" cy="7.6" rx="4.4" ry="4"/><ellipse cx="17" cy="15.4" rx="4.2" ry="3.6"/><ellipse cx="17" cy="23.4" rx="4" ry="4.4"/>'
                 '<circle cx="7.6" cy="6.4" r="2.5"/><circle cx="26.4" cy="6.4" r="2.5"/>')
        strokes = 'M13.4 15 L7.4 13.4 L7.6 8 M20.6 15 L26.6 13.4 L26.4 8 M14.6 18.6 L9.6 22.6 L6.6 27 M19.4 18.6 L24.4 22.6 L27.4 27 M15 4.2 Q13.4 .6 10.6 1.4 M19 4.2 Q20.6 .6 23.4 1.4'
        return emboss(uid, fills, strokes, 2.3)
    if kind == 'bomb':            # a stick with a lit fuse
        fills = '<rect x="11.4" y="9" width="9" height="18.6" rx="2.6" transform="rotate(42 15.9 18.3)"/>'
        strokes = 'M22.6 11.4 Q25.6 7.4 28.4 9.6'
        ex = ('<g transform="rotate(42 15.9 18.3)" fill="#0b3526" fill-opacity=".55"><rect x="11.4" y="13.2" width="9" height="1.4"/><rect x="11.4" y="18" width="9" height="1.4"/><rect x="11.4" y="22.8" width="9" height="1.4"/></g>'
              '<g fill="#fff3b0"><circle cx="29.6" cy="7" r="1.5"/><circle cx="26.4" cy="4.2" r="1"/><circle cx="31" cy="11.2" r=".9"/><circle cx="30.4" cy="3" r=".8"/></g>')
        return emboss(uid, fills, strokes, 1.7, GREEN, ex)
    if kind == 'fire':            # a flame over a low wall
        fills = '<path d="M17 2.4 C22.4 8.6 26.4 13 24.6 19.6 C23.4 23.8 20 26 17 26 C12.4 26 8.4 22.6 9.2 17.6 C9.7 14.6 12 12.8 12.4 9.6 C14.4 11.4 15.2 13.4 15.6 15.2 C16.6 10.8 16 6.6 17 2.4Z"/>'
        strokes = 'M6.4 27.6 H27.6'
        ex = '<path d="M17.2 14.6 C20.2 18.2 20.6 21.6 17.6 24.4 C14.6 22.8 14.2 18.6 17.2 14.6Z" fill="#effff8" fill-opacity=".6"/>'
        return emboss(uid, fills, strokes, 2.2, GREEN, ex)
    if kind == 'thief':           # Steal: a hand reaching down to the left for a crumb (fingers side by side, thumb above, the arm coming in from the upper right)
        fills = '<ellipse cx="20.4" cy="13.6" rx="5" ry="5.6" transform="rotate(-50 20.4 13.6)"/><circle cx="6.4" cy="24.6" r="2.3"/>'
        strokes = 'M17 10.4 L8.6 12 M16.6 13.4 L7.4 16.4 M17.4 16.4 L9.4 20.6 M19.4 18.6 L14 23.4 M21.4 9 L17.6 4.6'
        ex = '<path d="M24.2 10.4 L30.4 4.4" fill="none" stroke="url(#%s_lm)" stroke-width="5" stroke-linecap="round"/>' % uid
        return emboss(uid, fills, strokes, 2.2, GREEN, ex)
    if kind == 'swim':            # Bridge: a shovel over water
        fills = '<path d="M13.2 17.4 L17.6 21.8 Q14.6 27.6 9.4 27.4 Q6.6 27 6.6 24.4 Q6.6 19.6 13.2 17.4Z"/>'
        strokes = 'M15.4 19.6 L26 9 M23.6 6.6 L28.4 11.4'
        ex = ('<path d="M4.6 9.4 q1.8 -2 3.6 0 t3.6 0 t3.6 0 M19.6 25.6 q1.8 -2 3.6 0 t3.6 0" fill="none" stroke="#9ff0cd" stroke-width="1.2" stroke-linecap="round"/>')
        return emboss(uid, fills, strokes, 2.2, GREEN, ex)
    raise ValueError(kind)

KINDS = [('move', 'Move', 0), ('ally', 'TeamUp', 0), ('hatch', 'Hatch', 0), ('attack', 'Attack', 1), ('bomb', 'Bomb', 1), ('fire', 'FireWall', 1), ('thief', 'Steal', 1), ('swim', 'Bridge', 1)]   # kind, label, slot (0 left at x 798, 1 right at x 859)
SLOT_X = (20, 81)          # local x of the pedestal in the card (the original's bases: (477, 157) and (538, 157))

def slab(uid, x, y, kind, front=True, rim=None):
    """the pedestal's body, 46 x 66 at (x, y): raised rim and bevel, a sunken panel with the picture, four steps made of one-pixel bands; front=False is its blank back (the rise animation)"""
    defs = (vgrad(uid + '_rim', y, y + 66, [(0, '#0f5a42'), (8 / 66.0, '#0f5a42'), (8.5 / 66.0, rim or '#06150e'), (1, rim or '#06150e')]) + hgrad(uid + '_b', x, x + 46, [(0, '#3aa57f'), (.2, '#2a8765'), (.7, '#1b6149'), (1, '#124a37')])
            + vgrad(uid + '_p', y + 6, y + 36, [(0, '#134a37'), (.5, '#1a5f45'), (1, '#27805f')]))
    g = '<defs>%s</defs>' % defs
    g += raised(uid + 'r', x, y, 46, 66, 4, 'url(#%s_b)' % uid, sh='#1c6a4c', rim='url(#%s_rim)' % uid)
    if not front: return g
    g += sunken(uid + 's', x + 4, y + 4, 38, 34, 2, 'url(#%s_p)' % uid, wd='#1f5a45', wl='#6fd4aa', rim='#1d6a4f')
    sx, sw, sy = x + 4, 38, y + 41
    rows = ['#06150e', '#8fe6c2', '#2f8f6a', '#2f8f6a', '#14503a']
    st = ''
    for i in range(4):
        for k, c in enumerate(rows):
            st += hband(sx, sy + 5 * i + k, sw, c)
    st += hband(sx, sy + 20, sw, '#06150e')
    g += '<g shape-rendering="crispEdges">%s</g>' % st
    g += '<g transform="translate(%s %s)">%s</g>' % (x + 6, y + 6, icon(uid + 'i', kind))
    return g

def gold_ring(uid, x, y, op=1):
    """the gold-lined socket a latched pedestal sits in (the original's latched pedestals have a gold surround)"""
    d = vgrad(uid + '_au', y + 6, y + 68, [(0, '#f3db37'), (.5, '#d3b700'), (1, '#977700')])
    return '<defs>%s</defs><rect x="%s" y="%s" width="50" height="64" rx="6" fill="url(#%s_au)" fill-opacity="%s"/><rect x="%s" y="%s" width="46" height="60" rx="4" fill="#3a2a05" fill-opacity="%s"/>' % (d, x - 2, y + 5, uid, op, x, y + 7, op)

def pedestal(uid, x, y, state='up', kind='move'):
    """a pedestal at (x, y) = (798, 157) or (859, 157) in the 960 layout (the original's black outline spans y 165 .. 222). States: up (cast shadow), press1 and press2 (the two frames of the press: it sinks one and two
    pixels, the shadow shrinks, the gold begins to show), latched (two pixels down inside a gold-lined socket, no shadow), down (= press1)"""
    if state == 'down': state = 'press1'
    dn = {'up': 0, 'press1': 1, 'press2': 2, 'latched': 2}[state]
    g = ''
    if state == 'up': g += rrect(x + 5, y + 20, 46, 51, 4, '#020e08', ' fill-opacity=".5"')
    if state == 'press1': g += rrect(x + 3, y + 14, 46, 55, 4, '#020e08', ' fill-opacity=".5"')
    if state == 'press2': g += gold_ring(uid + 'g', x, y, .55)
    if state == 'latched': g += gold_ring(uid + 'g', x, y)
    g += '<g transform="translate(0 %d)">%s</g>' % (dn, slab(uid, x, y, kind, rim='#2a1e03' if state == 'latched' else None))
    return g

RISE = [None, None, ('back', .86), ('back', .56), ('back', .2), ('front', .5), ('front', .76), ('front', .93), ('front', 1.0)]     # the nine frames of the rise (60 ms each): outline, socket, then the slab turning

def socket(uid, x, y, outline_only=False):
    if outline_only: return '<rect x="%s" y="%s" width="45" height="58" rx="4" fill="none" stroke="#3f9c76" stroke-opacity=".7" stroke-width="1"/>' % (x + .5, y + 7.5)
    d = vgrad(uid + '_so', y + 7, y + 66, [(0, '#07170f'), (1, '#12402f')])
    return '<defs>%s</defs>%s' % (d, sunken(uid + 'sk', x, y + 7, 46, 59, 4, 'url(#%s_so)' % uid, wd='#0b3022', wl='#3f9c76', rim='#06150e'))

def pedestal_rise(uid, x, y, n, kind='move'):
    """frame n (1 .. 9) of the pedestal rising out of the plate: the socket appears, the slab turns about its upright axis from its blank back to its face. Sinking is the same frames backwards;
    an icon swap is frames 9 .. 5 of the old kind, then 6 .. 9 of the new one."""
    if n == 1: return socket(uid, x, y, True)
    g = socket(uid, x, y)
    if n == 2: return g
    side, f = RISE[n - 1]
    cx = x + 23
    g += '<g transform="translate(%s 0) scale(%s 1) translate(%s 0)">%s<rect x="%s" y="%s" width="46" height="59" rx="4" fill="#020e08" fill-opacity="%s"/></g>' % (
        cx, f, -cx, slab(uid + 'f', x, y, kind, front=(side == 'front')), x, y + 7, round((1 - f) * .55, 3))
    return g

def stop_button(uid, cx, cy, state='up'):
    """the Stop button at the original's rectangles: the warm knob is 23 x 23 about (930.5, 213.5) (rim r 11.5, domed face r 10.5 with a lit crescent above left and a shaded one below right, the same light as every bevel);
    the sunken round well is 30 across about (931, 214) (wall r 15, rim r 14, floor); (cx, cy) is the knob's centre local to the card"""
    lit = state == 'down'
    wx, wy = cx + .5, cy + .5
    face = ('#fff7a8', '#f3d11a', '#a87a00') if lit else ('#e2b93c', '#96700f', '#3a2a05')
    d = ('<linearGradient id="%s_w" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="%s"/><stop offset=".5" stop-color="%s"/><stop offset="1" stop-color="%s"/></linearGradient>'
         '<radialGradient id="%s_k" cx=".36" cy=".3" r=".8"><stop offset="0" stop-color="%s"/><stop offset=".5" stop-color="%s"/><stop offset="1" stop-color="%s"/></radialGradient>'
         '<pattern id="%s_m" width="2.5" height="2.5" patternUnits="userSpaceOnUse" patternTransform="rotate(45)"><rect width="1.25" height="1.25" fill="#000" fill-opacity="%s"/><rect x="1.25" y="1.25" width="1.25" height="1.25" fill="#000" fill-opacity="%s"/></pattern>'
         '<clipPath id="%s_c"><circle cx="%s" cy="%s" r="10.5"/></clipPath>'
         '<linearGradient id="%s_rm" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#6a560e"/><stop offset=".5" stop-color="#4a3c0a"/><stop offset="1" stop-color="#2e2506"/></linearGradient>'
         % (uid, '#a9efd0', '#2f8a68', '#0b3022', uid, face[0], face[1], face[2], uid, '0' if lit else '.34', '0' if lit else '.34', uid, cx, cy, uid))
    g = '<defs>%s</defs>' % d
    g += '<circle cx="%s" cy="%s" r="13.5" fill="#020e08" fill-opacity=".4" transform="translate(1 1)"/>' % (wx, wy)
    g += '<circle cx="%s" cy="%s" r="15" fill="url(#%s_w)"/>' % (wx, wy, uid)
    g += '<circle cx="%s" cy="%s" r="14" fill="#1b5a43"/>' % (wx, wy)
    g += '<circle cx="%s" cy="%s" r="13" fill="#23765a"/>' % (wx, wy)
    g += '<circle cx="%s" cy="%s" r="11.5" fill="url(#%s_rm)"/>' % (cx, cy, uid)
    g += '<circle cx="%s" cy="%s" r="10.5" fill="url(#%s_k)"/>' % (cx, cy, uid)
    g += '<g clip-path="url(#%s_c)"><rect x="%s" y="%s" width="21" height="21" fill="url(#%s_m)"/></g>' % (uid, cx - 10.5, cy - 10.5, uid)
    g += '<path d="M%s %s A10 10 0 0 1 %s %s" fill="none" stroke="#ffe9a0" stroke-opacity=".45" stroke-width="1"/>' % (cx - 9.8, cy + 2.2, cx + 2.2, cy - 9.8)
    g += '<path d="M%s %s A10 10 0 0 1 %s %s" fill="none" stroke="#1a1302" stroke-opacity=".6" stroke-width="1"/>' % (cx + 9.8, cy - 2.2, cx - 2.2, cy + 9.8)
    g += '<ellipse cx="%s" cy="%s" rx="4.2" ry="2.2" fill="#fff" fill-opacity="%s" transform="rotate(-30 %s %s)"/>' % (cx - 2.5, cy - 5, '.4' if lit else '.1', cx - 2.5, cy - 5)
    return g

def card_base(W, uid='cd'):
    """(V, 136, 182, 116): the plate, the rail and the Status label: what the static frame holds; the pedestals, their labels and Stop are drawn over it by the game"""
    V = W - 182
    defs = surf_defs(uid, V, 136) + LET.defs(uid + 'g')
    return svg(182, 116, surf(uid, 0, 0, 182, 116) + rail_cols(0, 116) + LET.label('Status', LETTER, uid + 'g'), defs)

def stop_sprite(state='up', uid='sp'):
    """the Stop label and knob (the original's panel butcanu / butcand), 52 x 64 at (V + 130, 172)"""
    return svg(52, 64, '<g transform="translate(-130 -36)">%s%s</g>' % (LET.label('Stop', LETTER, uid + 'g'), stop_button(uid + 's', 152.5, 77.5, state)), LET.defs(uid + 'g'))

def label_sprite(text, uid='lb'):
    """a pedestal's label alone, 64 x 16 at (V + 16 or V + 77, 136)"""
    slot = dict((lab, s) for _, lab, s in KINDS)[text]
    return svg(64, 16, '<g transform="translate(%d 0)">%s</g>' % (-(16 + 61 * slot), LET.label(text, LETTER, uid + 'g')), LET.defs(uid + 'g'))

def pedestal_sprite(kind, frame, uid='ps'):
    """one pedestal frame alone, 56 x 76; frame is up, press1, press2, latched or rise1 .. rise9. Its place is (V + 18 + 61 * slot, 156)."""
    body = pedestal_rise(uid, 2, 1, int(frame[4:]), kind) if frame.startswith('rise') else pedestal(uid, 2, 1, frame, kind)
    return svg(56, 76, body)

def card(W, uid='cd', pedestal_state='up', stop_state='up', left=('move', None), right=None, stop=True):
    """the card as the game shows it: base, the left pedestal (kind, state), the right pedestal, Stop. A state 'riseN' draws that frame of the rise (no label yet)."""
    V = W - 182
    defs = surf_defs(uid, V, 136) + LET.defs(uid + 'g')
    b = [surf(uid, 0, 0, 182, 116), rail_cols(0, 116), LET.label('Status', LETTER, uid + 'g')]
    labels = dict((k, lab) for k, lab, _ in KINDS)
    for slot, sel in enumerate((left, right)):
        if not sel: continue
        kind, state = sel[0], (sel[1] or pedestal_state)
        if state.startswith('rise'):
            b.append(pedestal_rise(uid + 'p%d' % slot, SLOT_X[slot], 21, int(state[4:]), kind))
        else:
            b.append(LET.label(labels[kind], LETTER, uid + 'g'))
            b.append(pedestal(uid + 'p%d' % slot, SLOT_X[slot], 21, state, kind))
    if stop:
        b.append(LET.label('Stop', LETTER, uid + 'g'))
        b.append(stop_button(uid + 's', 152.5, 77.5, stop_state))
    return svg(182, 116, ''.join(b), defs)

def status_box(W, uid='st'):
    """(V, 252, 182, 16): the status box at the original's rectangle: field (800, 254, 141, 12) inside a dark rim (19,43,27 in the original) and a one-pixel wall (dark above and left, lit below and right), x 798 .. 942, y 252 .. 267;
    plate around it on every side, the ridge at the left, no groove"""
    V = W - 182
    defs = surf_defs(uid, V, 252) + vgrad(uid + '_f', 2, 14, [(0, '#68b38e'), (.18, '#74c09b'), (.85, '#78c49f'), (1, '#8bd6b0')])
    b = [surf(uid, 0, 0, 182, 16), rail_cols(0, 16)]
    b.append(sunken(uid + 'b', 20, 0, 145, 16, 0, 'url(#%s_f)' % uid, wd='#1b4b3f', wl='#3f9c76', rim='#132b1b'))
    return svg(182, 16, ''.join(b), defs)

# ---- 5  the rest of the frame: left strip, lower panel (chat), ornaments ----------------------------------------------------------------------------------
CHAMFER_COLS = [(43, 95, 67), (43, 95, 67), (23, 83, 63), (19, 51, 35), (19, 51, 35), (23, 59, 39), (27, 67, 43), (19, 51, 35), K_, K_]     # the left strip's columns x = 7 .. 16
# the ornaments the original etches into the panel (clusters measured on the clean 16:9 frame: x, y, w, h); a y at or below 417 is anchored to the bottom (moves with the extra height)
ORNAMENTS = [   # (middle x, middle y, heading clockwise from up, scale, seed) on the 16:9 layout, at the places of the original's etched clusters; a y at or below 417 is anchored to the bottom
    (791.5, 306, 170, .90, 3), (791.5, 341, -8, .95, 11), (791.5, 450, 12, .85, 5), (792, 492, 190, .80, 21),      # left of the log and of the input line (their legs tuck under the frames, as the original's do)
    (884, 472, -84, .88, 9), (934, 470.5, 62, .72, 14),                                                           # between the log and the input line
    (953, 306, 8, .85, 2), (952.5, 341, 196, .90, 17), (952.5, 436, -10, 1.0, 23), (954, 478, 22, .80, 31),        # right of the log, cut by the picture's edge
]
HEADER_ANT = (860, 288.5, -72, .6, 4)       # the small ant that walks on the rule after the Chat word (the original has one there)
CORNER_ANT = (27, 11.5, 74, .7, 6)         # on the corner plate under the version (local to the panel's left edge and the strip's first row), cut by the bottom edge

def left_strip(W, H, uid='ls'):
    """(0, 22, 17, H - 22): plate, the chamfer's columns (the original's, x 7 .. 15), the two rule lines at y 283 and 286 (white, with a black line under each), and the quarter rings that turn the chamfer into the bottom strip's ridge"""
    h = H - 22
    defs = surf_defs(uid, 0, 22)
    b = [surf(uid, 0, 0, 17, h)]
    cy = H - 19 - 22                                    # local y of the corner's centre (the strip's first row)
    cols = ''.join('<rect x="%d" y="0" width="1" height="%d" fill="%s"/>' % (7 + i, cy, rgb(c)) for i, c in enumerate(CHAMFER_COLS))
    b.append('<g shape-rendering="crispEdges">%s</g>' % cols)
    # the corner: a plain 45 degree mitre on whole pixels, like every other frame of the system: band j of the chamfer (column x = 16 - j) runs down to row cy + j and owns the corner pixel
    # (the dark wall owns the corner, as at the windows); the strip's ridge row j starts right of it.
    mit = ''
    for jb in range(10):
        mit += '<rect x="%d" y="%d" width="1" height="%d" fill="%s"/>' % (16 - jb, cy, jb + 1, rgb(CHAMFER_COLS[9 - jb]))
        if jb < 8 and jb > 0: mit += '<rect x="%d" y="%d" width="%d" height="1" fill="%s"/>' % (17 - jb, cy + jb, jb, rgb(RIDGE[jb]))
    b.append('<g shape-rendering="crispEdges">%s</g>' % mit)
    # the rule: rows 283 and 286 (local 261 and 264), x 0 .. 9 white, x 10 .. 15 lit, black rows under them
    rl = ''
    for y in (261, 264):
        rl += '<rect x="0" y="%d" width="10" height="1" fill="#fbfbff"/><rect x="10" y="%d" width="6" height="1" fill="#338f67"/><rect x="0" y="%d" width="17" height="1" fill="#000"/>' % (y, y, y + 1)
    b.append('<g shape-rendering="crispEdges">%s</g>' % rl)
    return svg(17, h, ''.join(b), defs)

def lower_panel(W, H, uid='lp', bottom='up'):
    """(V, 268, 182, H - 19 - 268): the panel below the status box: ridge, the chat header (two rule lines and the "Chat" label), the log window, the input line, the ornaments, "Send to:" and the All button"""
    V = W - 182
    dy = H - 480; sh = dy - 60                           # bottom-anchored things move by sh relative to the 16:9 measurements
    h = H - 19 - 268
    defs = surf_defs(uid, V, 268) + LET.defs(uid + 'l') + vgrad(uid + '_tf', 2, 24, [(0, '#4ba283'), (.42, '#3b8f70'), (.43, '#2f7a5c'), (1, '#1f5f46')])
    b = [surf(uid, 0, 0, 182, h), rail_cols(0, h)]
    # the rule lines (abs y 283, 286 -> local 15, 18): white, a black line under each; from x = 781 to the right edge
    rl = ''
    for y in (15, 18):
        rl += '<rect x="3" y="%d" width="179" height="1" fill="#fbfbff"/><rect x="3" y="%d" width="179" height="1" fill="#000"/>' % (y, y + 1)
    b.append('<g shape-rendering="crispEdges">%s</g>' % rl)
    b.append(LET.label('Chat', LETTER, uid + 'l'))
    # ornaments
    defs += '<clipPath id="%s_oc"><rect x="8" y="0" width="174" height="%d"/></clipPath>' % (uid, h)
    # the log window: sprite wchat (799, 298, 143, 103 + dy) with a one-pixel wall outside; the input line: wtype (799, 423 + dy, 143, 14)
    b.append(sunken(uid + 'g', 20, 297 - 268, 145, 105 + dy, 0, '#bfefe3', wd='#1b4b3f', wl='#3f9c76', rim='#1b432b'))
    b.append(sunken(uid + 'i', 20, 422 + dy - 268, 145, 16, 0, '#fbfbff', wd='#1b4b3f', wl='#3f9c76', rim='#1b432b'))
    b.append(LET.label('Send to:', LETTER, uid + 'l', dy=sh))
    return svg(182, h, ''.join(b), defs)

SEND_W = {'All': 42, 'Team': 44}

def all_button(W=960, H=540, state='up', uid='ab', name='All'):
    """a Send-to button (sprites butallu and butteamu, 44 x 24 and 46 x 24, at (V + 74, H - 37) and (V + 121, H - 37)). The face is the original's 42 x 20 (the original's lower four rows are the button's dark shadow, not its face); under the face a dark lip three rows deep (the original's dark lower edge: its outline measures 42 x 23) and a soft cast shadow 2 right and 4 down. Pressed: a sunken 42 x 20 face, label one pixel down and right."""
    Fw = SEND_W[name]; hov = state == 'hover'
    up_stops = [(0, '#5cb08c' if hov else '#44977a'), (.3, '#4ea07f' if hov else '#3a8a6c'), (.62, '#3a8a6a' if hov else '#2f7a5c'), (1, '#2a7459' if hov else '#23674f')]
    defs = vgrad(uid + '_f', 2, 18, up_stops) + vgrad(uid + '_d', 2, 18, [(0, '#0d3526'), (.3, '#14513b'), (1, '#1e6a4d')]) + LET.defs(uid + 'l', hov)
    if state != 'down':
        b = rrect(2, 4, Fw, 20, 3, '#020e08', ' fill-opacity=".45"') + rrect(0, 3, Fw, 20, 3, '#06150e', ' fill-opacity=".92"') + raised(uid, 0, 0, Fw, 20, 3, 'url(#%s_f)' % uid, hl='#a6e0c4' if hov else S.HL) + LET.label(name, LETTER, uid + 'l', hover=hov)
    else:
        b = sunken(uid, 0, 0, Fw, 20, 3, 'url(#%s_d)' % uid) + LET.label(name, LETTER, uid + 'l', shift=(1, 1))
    return svg(Fw + 2 + (1 if state == 'down' else 0), 24, b, defs)

# ---- 4  buttons --------------------------------------------------------------------------------------------------------------------------------------
FACE_W = {'Help': 44, 'Options': 49, 'Quit': 44}          # the original's faces are 44 / 50 / 44 wide; Options is one pixel narrower so that both gaps are 5
SPRITE_W = {'Help': 46, 'Options': 52, 'Quit': 46}

def button(name, state='up', uid='bt'):
    """the face is the original's silhouette: 44 x 19 (pressed 44 x 20) at the sprite's origin; the lettering is the traced original at the original's own place; the cast shadow is 2 px right and 3 px down"""
    W, Fw = SPRITE_W[name], FACE_W[name]
    hov = state == 'hover'
    up_stops = [(0, '#5cb08c' if hov else '#44977a'), (.3, '#4ea07f' if hov else '#3a8a6c'), (.62, '#3a8a6a' if hov else '#2f7a5c'), (1, '#2a7459' if hov else '#23674f')]
    defs = vgrad(uid + '_rim', 0, 19, [(0, '#0f5a42'), (.1, '#0f5a42'), (.105, '#06150e'), (1, '#06150e')]) + vgrad(uid + '_f', 2, 17, up_stops) + vgrad(uid + '_d', 2, 18, [(0, '#0d3526'), (.3, '#14513b'), (1, '#1e6a4d')]) + LET.defs(uid + 'i', hov)
    b = ''
    if state != 'down':
        b += rrect(2, 3, Fw, 19, 3, '#020e08', ' fill-opacity=".5"')
        b += raised(uid, 0, 0, Fw, 19, 3, 'url(#%s_f)' % uid, hl='#a6e0c4' if hov else S.HL, rim='url(#%s_rim)' % uid)
        b += LET.label(name, LETTER, uid + 'i', hover=hov)
    else:
        b += sunken(uid, 0, 0, Fw, 20, 3, 'url(#%s_d)' % uid)
        b += LET.label(name, LETTER, uid + 'i', shift=(1, 1))
    return svg(W if state != 'down' else W + 1, 23, b, defs)
