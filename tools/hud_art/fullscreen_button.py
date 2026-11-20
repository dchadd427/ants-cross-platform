"""The Fullscreen button of the HUD's top right corner, right of Quit: a small button of the same make as its neighbors (pieces.button(): the same bevel system, face colors and cast shadow) with a
picture in the labels' cream and one-pixel shadow. The picture: four corner brackets (go fullscreen) or their inward twin (leave fullscreen), two pixels thick, in an 11 x 11 box.

The room for it: Quit's face ends at x 942 and its cast shadow at 944; from 945 to the picture's edge (959) the plate is plain. A button as high as its neighbors with a readable picture needs a face about
17 wide, a gap and a margin, so the three older buttons sit 8 pixels further left (sprites.py) and this one's face is 17 x 19 at (939, 7), its sprite 21 x 23.
"""
import common
import system as S

X, Y = 939, 7                    # the sprite's place in the 960 x 540 picture (the face's top left)
W, H = 21, 23
FACE_W = 17
ENTER = ((0, 0, 4, 2), (0, 0, 2, 4), (7, 0, 4, 2), (9, 0, 2, 4), (0, 9, 4, 2), (0, 7, 2, 4), (7, 9, 4, 2), (9, 7, 2, 4))        # rectangles (x, y, w, h) in the 11 x 11 box
LEAVE = ((0, 2, 4, 2), (2, 0, 2, 4), (7, 2, 4, 2), (7, 0, 2, 4), (0, 7, 4, 2), (2, 7, 2, 4), (7, 7, 4, 2), (7, 7, 2, 4))


def _icon(rects, x, y, fill):
    return ''.join('<rect x="%s" y="%s" width="%s" height="%s" fill="%s"/>' % (x + a, y + b, w, h, fill) for a, b, w, h in rects)


def button(state='up', leave=False):
    """the button's svg (21 x 23 canvas pixels, the face at (0, 0)): state up, hover or down; `leave` for the picture that leaves fullscreen"""
    u = 'fs%s%s' % (state, 'l' if leave else 'e')
    hov = state == 'hover'
    up = [(0, '#5cb08c' if hov else '#44977a'), (.3, '#4ea07f' if hov else '#3a8a6c'), (.62, '#3a8a6a' if hov else '#2f7a5c'), (1, '#2a7459' if hov else '#23674f')]      # pieces.button()'s own
    d = S.vgrad(u + '_rim', 0, 19, [(0, '#0f5a42'), (.1, '#0f5a42'), (.105, '#06150e'), (1, '#06150e')]) + S.vgrad(u + '_f', 2, 17, up) + S.vgrad(u + '_d', 2, 18, [(0, '#0d3526'), (.3, '#14513b'), (1, '#1e6a4d')])
    top, mid, bot = ('#ffffff', '#ffffff', '#d6f6ea') if hov else ('#fffbe8', '#f0fff4', '#c1ecda')
    d += '<linearGradient id="%s_i" gradientUnits="userSpaceOnUse" x1="0" y1="4" x2="0" y2="16"><stop offset="0" stop-color="%s"/><stop offset=".5" stop-color="%s"/><stop offset="1" stop-color="%s"/></linearGradient>' % (u, top, mid, bot)
    rects = LEAVE if leave else ENTER
    if state != 'down':
        b = S.rrect(2, 3, FACE_W, 19, 3, '#020e08', ' fill-opacity=".5"') + S.raised(u, 0, 0, FACE_W, 19, 3, 'url(#%s_f)' % u, hl='#a6e0c4' if hov else S.HL, rim='url(#%s_rim)' % u)
        ox, oy = 3, 4
    else:
        b = S.sunken(u, 0, 0, FACE_W, 20, 3, 'url(#%s_d)' % u)
        ox, oy = 4, 5
    b += _icon(rects, ox + 1, oy + 1, '#0b1307') + _icon(rects, ox, oy, 'url(#%s_i)' % u)
    return common.svg(W, H, b, d)
