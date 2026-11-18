"""The new cursors of the HUD art: one sprite for every sprite of the original's sixteen cursor animations, at 1x, 2x and 3x, and the animations that play them.

Every cursor keeps what makes it the original's: the same animation (name, number of frames, frame times), the same size of every sprite and the same place of its corner from the click point (`dx`, `dy`).
All of these are read from the repository's decoded catalog (orig_anim.py, cat_orig.py: numbers only; no picture of the original is read), so nothing here can move a click point. Only the picture changes.

The drawings are flat and hard-edged (crisp.Canvas: each scale is drawn on its own pixel grid, nothing is scaled up from 1x), in the original's own colors and proportions:
- the pointer: the standard arrow in a white face, a gold bevel behind it to the right and below, a dark outline;
- the edge-scroll arrows: a yellow face with a gold wall below and right, a dark outline (one straight arrow and one diagonal arrow, turned and mirrored);
- move, special target, attack, food and no entry: every frame, with the original's proportions and colors;
- the two hands (pointing and open) are shaded renders made in Blender: their sprites are committed in hands/ with their source (hands/glove_fit.py, run by make_hands.py), and the generator reads them.

`export()` gives what pack.py needs: the pictures, the manifest rows and the animations. A cursor animation is marked `cursor` in the manifest (hud_art.hpp): its parts are placed from the click point.
"""
import math
import os

import numpy as np
from PIL import Image

import cat_orig as cat
import orig_anim as OA
from crisp import Canvas

HERE = os.path.dirname(os.path.abspath(__file__)) + '/'
HANDS = HERE + 'hands/'

# The animations, in the order of the catalog: the original's names. c_scroll (the open hand) and c_cant (the no-entry sign) are in the original's archive but the original's program never shows them.
CURSORS = ['c_normal', 'c_select', 'c_scroll', 'c_mov1', 'c_targ1', 'c_attack', 'c_food', 'c_cant', 'CUR_N', 'CUR_NE', 'CUR_E', 'CUR_SE', 'CUR_S', 'CUR_SW', 'CUR_W', 'CUR_NW']

INK = (16, 12, 6)
WHITE = (255, 255, 255)


def sprite_names():
    """[(sprite name, (w, h))] of every sprite that the cursors use, in the order of their first frame; the name is the original's file name without its extension"""
    seen = {}
    for anim in CURSORS:
        for frame in cat.ANIMS[anim]:
            assert len(frame) == 1, 'a cursor frame has one part'
            _, _, file, w, h = frame[0]
            seen.setdefault(file[:-4], (w, h))
    return list(seen.items())


# ---- the pointer ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
# The white face is the standard arrow: the tip, an upright left edge, the long diagonal, two barbs whose bases run in to a narrow slanted tail with a squared end. The seven corners were measured on
# a picture of that arrow (its white inside: tip (143, 74), right barb (327, 246), right notch (240, 254), tail end (291, 395) and (270, 399), left notch (211, 263), left barb (143, 327); the shape with
# its outline is 232 x 374) and are scaled by one factor into the original's 13 x 23 box, the room for the bevel taken off first. The outline's tip is the box's upper left corner: the click point.
REF = [(143, 74), (327, 246), (240, 254), (291, 395), (270, 399), (211, 263), (143, 327)]
REF_BOX = (125, 45, 357, 419)
BEVEL = (1.4, 1.8)                     # the gold: the same arrow again this far right and down behind the white one
_S = min((13 - BEVEL[0]) / float(REF_BOX[2] - REF_BOX[0]), (23 - BEVEL[1]) / float(REF_BOX[3] - REF_BOX[1]))
POINTER_FACE = [((x - REF_BOX[0]) * _S, (y - REF_BOX[1]) * _S) for x, y in REF]
POINTER_GOLD = (244, 186, 28)
POINTER_BROWN = (182, 118, 52)         # the band of the gold next to the white that is the darker color
POINTER_BROWN_BAND = 0.8


def pointer(w, h, k):
    c = Canvas(w, h, k)
    white = c.poly(POINTER_FACE)
    steps = 8                                                                 # the gold: the face swept from its own place to the bevel's, so that no gap opens at the notches
    gold = white.copy()
    for i in range(1, steps + 1):
        gold = np.maximum(gold, c.poly([(x + BEVEL[0] * i / steps, y + BEVEL[1] * i / steps) for x, y in POINTER_FACE]))
    near = white.copy()
    for i in range(1, steps + 1):
        near = np.maximum(near, c.poly([(x + BEVEL[0] * POINTER_BROWN_BAND * i / steps / 1.6, y + POINTER_BROWN_BAND * i / steps) for x, y in POINTER_FACE]))
    body = gold.copy()                                                        # the outline: everything grown by one pixel in every direction
    for a in range(0, 360, 15):
        body = np.maximum(body, c.shift(gold, math.cos(math.radians(a)), math.sin(math.radians(a))))
    return c.out([(body, INK), (gold, POINTER_GOLD), (near, POINTER_BROWN), (white, WHITE)])


# ---- the edge-scroll arrows -----------------------------------------------------------------------------------------------------------------------------------------------------------
# A slab: a bright yellow face, a darker gold wall for its thickness below and right of the face (the light is at the upper left, as the original's), a dark line where face and wall meet, a dark
# outline one pixel wide. One straight arrow (pointing right, 20 x 21) and one diagonal arrow (pointing up and right, 19 x 19), turned and mirrored.
SCROLL_FACE = (246, 240, 96)
SCROLL_WALL = (196, 154, 30)
SCROLL_SEAM = (150, 114, 22)
E_PTS = [(0, 6), (9, 6), (9, 0), (10, 0), (20, 10.5), (10, 21), (9, 21), (9, 15), (0, 15)]
NE_PTS = [(2, 0), (19, 0), (19, 17), (13.5, 11.5), (6.5, 19), (0, 12.5), (7.5, 5.5)]


def scroll_points(name):
    if name in ('cur_e', 'cur_w', 'cur_n', 'cur_s'):
        p = E_PTS
        if name == 'cur_w':
            p = [(20 - x, y) for x, y in p]
        if name == 'cur_n':
            p = [(y, 20 - x) for x, y in p]
        if name == 'cur_s':
            p = [(y, x) for x, y in p]
    else:
        p = NE_PTS
        if name in ('cur_nw', 'cur_sw'):
            p = [(19 - x, y) for x, y in p]
        if name in ('cur_se', 'cur_sw'):
            p = [(x, 19 - y) for x, y in p]
    return p


def scroll(name, w, h, k):
    c = Canvas(w, h, k)
    body = c.poly(scroll_points(name))
    inner = c.inset(body, 1.0)
    face = inner * c.shift(inner, -1, -2)                                      # the inner shape less a band one pixel wide at its right and two at its bottom (the wall)
    seam = inner * c.shift(inner, -0.5, -1.5) * (1 - face)                     # a dark gold line along the face's lower right edge
    return c.out([(body, INK), (inner, SCROLL_WALL), (seam, SCROLL_SEAM), (face, SCROLL_FACE)])


# ---- move, special target, attack, food, no entry --------------------------------------------------------------------------------------------------------------------------------------
# The original's own colors and proportions, measured at 1x; every frame. A dark outline only where the original has one (move, target). Thickness as the original shows it: a darker wall or shade
# below and right of the lit face.
BROWN = (143, 83, 59)
AMBER = (227, 159, 7)
YEL = (255, 243, 63)
RED = (251, 51, 91)
DRED = (187, 0, 0)
PINK = (247, 159, 211)
ORANGE = (239, 175, 7)
ORANGE_D = (199, 119, 15)
RED2 = (255, 0, 0)


def union(*masks):
    out = masks[0]
    for m in masks[1:]:
        out = np.maximum(out, m)
    return out


def line(c, x0, y0, x1, y1, width):
    dx, dy = x1 - x0, y1 - y0
    length = math.hypot(dx, dy)
    nx, ny = -dy / length * width / 2.0, dx / length * width / 2.0
    return c.poly([(x0 + nx, y0 + ny), (x1 + nx, y1 + ny), (x1 - nx, y1 - ny), (x0 - nx, y0 - ny)])


def ring(c, cx, cy, r, thickness):
    return np.clip(c.ellipse(cx, cy, r, r) - c.ellipse(cx, cy, r - thickness, r - thickness), 0, 1)


def wedge(c, cx, cy, a0, a1, radius=60):
    pts = [(cx, cy)] + [(cx + radius * math.cos(math.radians(a)), cy + radius * math.sin(math.radians(a))) for a in np.linspace(a0, a1, 12)]
    return c.poly(pts)


def move(w, h, k):
    """a broad arrow pointing up on a block: a white top, its front faces in two golds, a dark outline"""
    c = Canvas(w, h, k)
    sx, sy = w / 25.0, h / 21.0
    X = lambda x: math.floor(x * sx + 1e-6)
    Y = lambda y: int(round(y * sy))
    xl, xb, xw = X(2), X(5), X(6)
    y6, y10, y15, y21 = Y(6), Y(10), Y(15), h
    body = c.poly([(w / 2.0, 0), (w, y6), (w, y10), (w - xb, y10), (w - xl, y15), (w - xl, y21), (xl, y21), (xl, y15), (xb, y10), (0, y10), (0, y6)])
    inner = c.inset(body, 1.0)
    f1 = max(int(round((y10 - y6 - 1) * 0.62)), 1)                             # the wings' front: an upper brown band and a lower amber one, as the original's
    wing = union(c.rect(0, y6, xw, y10), c.rect(w - xw, y6, w, y10))
    wing_lo = union(c.rect(0, y6 + f1, xw, y10), c.rect(w - xw, y6 + f1, w, y10))
    b0 = y15 + 1
    b1 = b0 + max(int(round((y21 - 1 - b0) * 0.45)), 1)
    return c.out([(body, INK), (inner, WHITE), (inner * wing, BROWN), (inner * wing_lo, AMBER), (inner * c.rect(0, b0, w, y21), BROWN), (inner * c.rect(0, b1, w, y21), AMBER)])


def target(w, h, k):
    """four wedges pointing at the click point: white, the side away from the light yellow, a dark outline"""
    c = Canvas(w, h, k)
    b, dp = {26: (6, 7), 20: (5, 6), 12: (4, 5)}[w]
    cx, cy = w / 2.0, h / 2.0
    cyl = math.floor(cy) + (0.0 if h % 2 == 0 else 0.5)
    tris = [[(cx - b, 0), (cx + b, 0), (cx + b, 1), (cx + 1, dp), (cx - 1, dp), (cx - b, 1)], [(cx - b, h), (cx + b, h), (cx + b, h - 1), (cx + 1, h - dp), (cx - 1, h - dp), (cx - b, h - 1)],
            [(0, cyl - b), (1, cyl - b), (dp, cyl - 1), (dp, cyl + 1), (1, cyl + b), (0, cyl + b)], [(w, cyl - b), (w - 1, cyl - b), (w - dp, cyl - 1), (w - dp, cyl + 1), (w - 1, cyl + b), (w, cyl + b)]]
    body = union(*[c.poly(t) for t in tris])
    inner = c.inset(body, 0.9)
    face = inner * c.shift(inner, -1.2, -1.2)
    return c.out([(body, INK), (inner, YEL), (face, WHITE)])


def attack(w, h, k):
    """four anchors closing on the click point; then a pale diamond ring between their caps; then a white core. No outline (the original has none)"""
    c = Canvas(w, h, k)
    cx = w / 2.0
    cyl = float(h // 2)
    length = {28: 4, 22: 3, 18: 2, 12: 0, 8: 0}[w]
    foot = w >= 22

    def anchor(T):
        """one anchor pointing inward from the top edge, mapped by T to its side"""
        m = c.poly([T(p) for p in [(cx - 3, 0), (cx + 3, 0), (cx + 5, 3), (cx - 5, 3)]])
        stripe = None
        if length:
            m = union(m, c.poly([T(p) for p in [(cx - 1, 3), (cx + 1, 3), (cx + 1, 3 + length), (cx - 1, 3 + length)]]))
            stripe = c.poly([T(p) for p in [(cx - 1, 1), (cx + 1, 1), (cx + 1, 3 + length), (cx - 1, 3 + length)]])
        if foot:
            for q in ([(cx - 3, 3 + length), (cx + 3, 3 + length), (cx + 3, 4 + length), (cx - 3, 4 + length)], [(cx - 3, 4 + length), (cx - 1, 4 + length), (cx - 1, 5 + length), (cx - 3, 5 + length)],
                      [(cx + 1, 4 + length), (cx + 3, 4 + length), (cx + 3, 5 + length), (cx + 1, 5 + length)]):
                m = union(m, c.poly([T(p) for p in q]))
        return m, stripe

    turns = [lambda p: p, lambda p: (p[0], h - p[1]), lambda p: (p[1], cyl + (p[0] - cx)), lambda p: (w - p[1], cyl + (p[0] - cx))]
    red = np.zeros((c.size[1], c.size[0]))
    stripe = np.zeros_like(red)
    for T in turns:
        m, st = anchor(T)
        red = union(red, m)
        if st is not None:
            stripe = union(stripe, st)
    layers = [(red, DRED), (red * c.shift(red, -0.75, -0.75), RED), (stripe * red, PINK)]
    if w == 18:
        d = lambda r: c.poly([(cx, h / 2.0 - r), (cx + r, h / 2.0), (cx, h / 2.0 + r), (cx - r, h / 2.0)])
        diamond = np.clip(d(5.5) - d(3.5), 0, 1)
        layers += [(diamond, PINK), (np.clip(d(5.0) - d(4.0), 0, 1), WHITE)]
    if w == 12:
        layers += [(c.rrect(3, 3, 9, 10, 1.5), PINK), (c.rrect(4, 4, 8, 9, 1), WHITE)]
    if w == 8:
        layers += [(c.rrect(1, 2, 7, 7, 1.5), PINK), (c.rrect(2, 3, 6, 6, 1), WHITE)]
    return c.out(layers)


# The food chunk is a handled lunch box with a bright left face and a light lid, 10 x 12 canvas pixels, as the original's: a stepped, chipped silhouette on whole pixels (so its edges are hard at every
# scale), a pale highlight along the top, a darker amber edge on its lower right. Rows are (y, from x, to x), half open.
CHUNK = (10, 12)
CHUNK_SILHOUETTE = [(0, 4, 7), (1, 3, 9), (2, 3, 10), (3, 1, 10), (4, 0, 10), (5, 0, 10), (6, 0, 9), (7, 0, 10), (8, 0, 10), (9, 0, 9), (10, 1, 8), (11, 2, 6)]
CHUNK_FACE = [(y, 0, 1) for y in range(5, 12)] + [(y, 2, 4) for y in range(6, 12)] + [(1, 3, 4), (2, 3, 4), (3, 3, 4)]       # the bright left face
CHUNK_STRIP = [(y, 1, 2) for y in range(6, 12)]                                                                                # the olive strip between its two bright columns
CHUNK_LID = [(2, 8, 10), (3, 1, 3), (3, 8, 9), (4, 0, 2), (4, 6, 7), (5, 1, 5)]                                                # the light band of the lid
CHUNK_LIGHT = [(2, 5, 7), (3, 5, 8), (4, 2, 6)]                                                                                # the lid's own highlight
CHUNK_TOP = [(0, 4, 7), (1, 3, 4)]                                                                                             # the pale edge of the top
CHUNK_SHADE = [(7, 9, 10), (8, 9, 10), (9, 8, 9), (10, 7, 8), (11, 5, 6)]                                                      # the darker edge of the lower right
G_RIM = (255, 255, 97)
G_LIT = (255, 211, 0)
G_MID = (228, 195, 54)
G_DK = (227, 159, 7)
G_HANDLE = (235, 219, 87)
G_SALMON = (205, 134, 86)
G_SHADE = (196, 128, 4)


def pixel_rows(c, ox, oy, rows):
    m = np.zeros((c.size[1], c.size[0]))
    for y, x0, x1 in rows:
        m = np.maximum(m, c.rect(ox + x0, oy + y, ox + x1, oy + y + 1))
    return m


def chunk(c, ox, oy, rim):
    """the food chunk with its corner at (ox, oy); `rim`: a color for a one pixel rim round it"""
    sil = pixel_rows(c, ox, oy, CHUNK_SILHOUETTE)
    layers = []
    if rim:
        grown = np.zeros_like(sil)
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                grown = np.maximum(grown, c.shift(sil, dx, dy))
        layers.append((grown, rim))
    for rows, color in ((CHUNK_SILHOUETTE, G_DK), (CHUNK_FACE, G_LIT), (CHUNK_STRIP, G_MID), (CHUNK_LID, G_MID), (CHUNK_LIGHT, G_HANDLE), (CHUNK_TOP, G_RIM), (CHUNK_SHADE, G_SHADE)):
        layers.append((pixel_rows(c, ox, oy, rows) * sil, color))
    return layers


def shell(c, cx, cy, r_out, width):
    return np.clip(c.ellipse(cx, cy, r_out, r_out) - c.ellipse(cx, cy, r_out - width, r_out - width), 0, 1)


def brackets(c, w, curved):
    """four brackets that stand at the edges of the sprite, or the whole ring (`curved` False), as the original's: a tube of four bands, outside in olive, pale yellow, amber and salmon in the middle of
    a bracket (olive and amber at its ends); the dim first frame is amber and yellow. A bracket is an arc of its own, tighter than a circle round the sprite: the top and bottom ones have a radius of 9 canvas
    pixels and are 14 wide, the left and right ones a radius of 11 and are 13 tall"""
    cx = cy = w / 2.0
    full = np.ones((c.size[1], c.size[0]))
    if not curved:
        r = w / 2.0
        s1, s2, s3, s4 = (shell(c, cx, cy, r - i, 1) for i in range(4))
        return [(s1, G_MID), (s2, G_RIM), (s3, G_MID), (s3, G_DK), (s4, G_DK), (s4, G_SALMON)]
    bands = [np.zeros_like(full) for _ in range(6)]           # olive, pale, olive, amber, amber, salmon (each the union over the four brackets)
    dim = [np.zeros_like(full) for _ in range(2)]             # the first frame: amber, yellow
    for angle, radius, half_chord, (x, y) in ((-90, 9.0, 7.0, (cx, 9.0)), (90, 9.0, 7.0, (cx, w - 9.0)), (180, 11.0, 6.5, (11.0, cy)), (0, 11.0, 6.5, (w - 11.0, cy))):
        phi = math.degrees(math.asin(half_chord / radius))
        span, tip, mid = (wedge(c, x, y, angle - phi * f, angle + phi * f) for f in (1.0, 0.9, 0.68))
        if w == 32:
            dim[0] = union(dim[0], shell(c, x, y, radius, 3) * span)
            dim[1] = union(dim[1], shell(c, x, y, radius - 1, 2) * span)
            continue
        s1, s2, s3, s4 = (shell(c, x, y, radius - i, 1) for i in range(4))
        for n, mask in enumerate((s1 * tip, s2 * tip, s3 * span, s3 * mid, s4 * span, s4 * mid)):
            bands[n] = union(bands[n], mask)
    if w == 32:
        return [(dim[0], G_DK), (dim[1], G_LIT)]
    return list(zip(bands, (G_MID, G_RIM, G_MID, G_DK, G_DK, G_SALMON)))


def food(sprite, w, h, k):
    """the food chunk, and four brackets that close on it (then a ring); then the chunk alone (once with a pale rim, once with a white one), then smaller. No outline"""
    c = Canvas(w, h, k)
    if sprite == 'curfd05':
        return c.out(chunk(c, 1, 1, G_RIM))
    if sprite == 'curfd06':
        return c.out(chunk(c, 1, 1, WHITE))
    if sprite == 'curfd08':
        return c.out(chunk(c, 0, 0, None))
    return c.out(brackets(c, w, w != 21) + chunk(c, round(w / 2.0 - CHUNK[0] / 2.0), round(h / 2.0 - CHUNK[1] / 2.0), None), snap=k == 1)       # (at 1x the bands are pixel art, as the original's)


def no_entry(size, k):
    """a ring and a bar; orange as it grows, red at full size, its inner edge away from the light darker. No outline"""
    c = Canvas(size, size, k)
    m = size / 2.0
    if size <= 5:
        d = c.ellipse(m, m, 2.5, 2.5)
        return c.out([(d, ORANGE_D), (d * c.shift(d, -0.6, -0.6), ORANGE)])
    thickness = {13: 3, 18: 4, 23: 5}[size]
    rg = ring(c, m, m, m, thickness)
    kk = (m - thickness / 2.0) * 0.7071
    shape = union(rg, line(c, m - kk, m - kk, m + kk, m + kk, thickness - 0.5))
    lit, dark = (RED2, DRED) if size >= 23 else (ORANGE, ORANGE_D)
    return c.out([(shape, dark), (shape * c.shift(shape, -0.8, -0.8), lit)])


# ---- the hands --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
HAND_SPRITES = ('c_select', 'c_scroll')


def hand(sprite, w, h, k):
    """the committed sprite of a hand at scale k (hands/<sprite>_<k>x.png), checked against the original's size"""
    im = Image.open(HANDS + '%s_%dx.png' % (sprite, k)).convert('RGBA')
    assert im.size == (w * k, h * k), (sprite, k, im.size)
    return im


def draw(sprite, w, h, k):
    if sprite in HAND_SPRITES:
        return hand(sprite, w, h, k)
    if sprite == 'c_normal':
        return pointer(w, h, k)
    if sprite.startswith('cur_'):
        return scroll(sprite, w, h, k)
    if sprite.startswith('c_mov'):
        return move(w, h, k)
    if sprite.startswith('c_targ'):
        return target(w, h, k)
    if sprite.startswith('c_atk'):
        return attack(w, h, k)
    if sprite.startswith('curfd'):
        return food(sprite, w, h, k)
    if sprite.startswith('c_cant'):
        return no_entry(w, k)
    raise ValueError(sprite)


def export(scales=(1, 2, 3)):
    """({scale: {sprite name: RGBA picture of size * scale}}, the manifest rows, {animation: [frame]}), the same shapes that sprites.export gives for the HUD. A sprite is named `cursor_` and the original's
    sprite name; its row has its place at (0, 0) (not used: the parts of a cursor animation are placed from the click point)."""
    images = {k: {} for k in scales}
    rows = []
    for name, (w, h) in sprite_names():
        for k in scales:
            im = draw(name, w, h, k)
            assert im.size == (w * k, h * k), (name, k, im.size)
            images[k]['cursor_' + name] = im
        rows.append({'name': 'cursor_' + name, 'size': [w, h], 'rect_960x540': [0, 0, w, h], 'anchor': ['left', 'top'], 'group': 'cursor'})
    anims = {}
    for anim in CURSORS:
        times = [s['duration_ms'] for s in OA.TABLE[anim]['subitems']]
        frames = cat.ANIMS[anim]
        assert len(times) == len(frames), anim
        anims[anim] = [{'ms': ms, 'parts': [{'sprite': 'cursor_' + f[0][2][:-4], 'x': f[0][0], 'y': f[0][1]}]} for ms, f in zip(times, frames)]
    return images, rows, anims
