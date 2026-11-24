"""The four teams' bombs (sprites 298 to 305: the red, green, black and blue bomb, two frames each, the second with the fuse alight), the approved design, seen from above (README.md).

The bomb is the original's 12 x 24 canister drawn again in the game's own view: a short drum of charge with a flat round top (a circle lies 0.4 as high as it is wide) standing in an open iron
cup, the team's color a band that wraps the cup, the fuse out of the middle of the top, one light from the upper left, a thin dark outline so that it stands off any ground at 1x. It is drawn by
rule on a master of 6 samples for each world pixel; 3x, 2x and 1x are averages of it (the 1x outline has blended pixels). Nothing of the original is read but the sprite's size.
"""
import numpy as np

from canvas import Canvas
from catalog import sprite_size
from tilekit import blur_edge, smoothstep as smooth

SAMPLES = 6                                                         # master samples for each world pixel
EDGE = 1.0 / SAMPLES                                                # the soft edge of a shape: one sample
TILT = 0.40                                                         # how flat a circle lies in this view (the original's charge top is about 12 wide and 5 high)
TEAMS = {'red': (298, 299), 'green': (300, 301), 'black': (302, 303), 'blue': (304, 305)}      # the sprites of a team's bomb: not alight, alight
TEAM = {                                                            # body, light, shade
    'red': ((228, 20, 20), (255, 110, 120), (140, 0, 8)),
    'green': ((44, 132, 60), (120, 196, 84), (6, 64, 46)),
    'black': ((70, 86, 106), (146, 160, 186), (26, 34, 46)),
    'blue': ((70, 100, 230), (144, 192, 250), (46, 44, 152)),
}
INK = (22, 18, 24)
STEEL = ((74, 78, 88), (168, 174, 186), (34, 36, 44))
MAROON = ((126, 8, 10), (190, 52, 44), (66, 0, 6))


def disc(c, cx, cy, rx, ry=None, soft=EDGE):
    ry = rx if ry is None else ry
    d = np.hypot((c.X - cx) / rx, (c.Y - cy) / ry)
    return smooth((1.0 - d) * min(rx, ry) / soft + 0.5)


def seg(c, x0, y0, x1, y1, width, soft=EDGE):
    px, py = c.X - x0, c.Y - y0
    dx, dy = x1 - x0, y1 - y0
    t = np.clip((px * dx + py * dy) / (dx * dx + dy * dy), 0, 1)
    d = np.hypot(px - t * dx, py - t * dy)
    return smooth((width / 2.0 - d) / soft + 0.5)


def outline(c, grow=0.42):
    """a thin dark line round everything drawn so far"""
    a = blur_edge(c.a, grow * 0.6, SAMPLES)
    ring = smooth((a - 0.08) / 0.12) * (1 - c.a)
    c.rgb = c.rgb + np.array(INK, float) * ring[..., None]
    c.a = c.a + ring


def fuse(c, pts, lit):
    """the cord along the points (the last is its end), twisted, with its charred end and a spark when it is alight"""
    for (x0, y0), (x1, y1) in zip(pts[:-1], pts[1:]):
        c.over(INK, seg(c, x0, y0, x1, y1, 1.5))
    for (x0, y0), (x1, y1) in zip(pts[:-1], pts[1:]):
        c.over((214, 200, 176), seg(c, x0, y0, x1, y1, 0.86))
        c.over((246, 240, 228), seg(c, x0 - 0.12, y0 - 0.12, x1 - 0.12, y1 - 0.12, 0.34))
    for i in range(1, 9):                                                                              # the twist of the cord
        f = i / 9.0
        k = min(int(f * (len(pts) - 1)), len(pts) - 2)
        g = f * (len(pts) - 1) - k
        x, y = pts[k][0] + g * (pts[k + 1][0] - pts[k][0]), pts[k][1] + g * (pts[k + 1][1] - pts[k][1])
        c.over((150, 128, 100), 0.8 * seg(c, x - 0.3, y + 0.3, x + 0.3, y - 0.3, 0.2))
    sx, sy = pts[-1]
    c.over((60, 40, 30), disc(c, sx - 0.25, sy + 0.1, 0.55))                                           # the charred end
    if lit:
        c.over((255, 170, 40), 0.42 * disc(c, sx, sy, 2.6, soft=1.6))
        ang = np.arctan2(c.Y - sy, c.X - sx)
        d = np.hypot(c.X - sx, c.Y - sy)
        star = 1.75 * (0.42 + 0.58 * np.abs(np.cos(3 * ang + 0.5)) ** 3)
        c.over((226, 70, 14), smooth((star - d) / EDGE + 0.5))
        c.over((255, 190, 30), smooth((0.66 * star - d) / EDGE + 0.5))
        c.over((255, 255, 214), smooth((0.34 * star - d) / EDGE + 0.5))
        for dx, dy in ((1.9, -1.2), (-1.5, -1.6), (1.2, 1.5)):
            c.over((255, 214, 70), disc(c, sx + dx, sy + dy, 0.26))
    else:
        c.over((226, 70, 14), disc(c, sx, sy, 0.62))
        c.over((255, 214, 70), disc(c, sx + 0.05, sy - 0.05, 0.3))


def round_shade(nx, mat):
    """across a round form from its left edge (-1) to its right (1): light, body, shade, and a little reflected light at the far edge"""
    body, light, shade = (np.array(v, float) for v in mat)
    col = body[None, None, :] + (light - body)[None, None, :] * smooth((0.50 - np.abs(nx + 0.45)) / 0.40)[..., None]
    col = col + (shade[None, None, :] - col) * smooth((nx - 0.10) / 0.60)[..., None]
    return col + (body[None, None, :] - col) * (0.40 * smooth((nx - 0.82) / 0.12))[..., None]


def bomb(team, lit):
    """one bomb of a team on the canvas, its fuse alight or not"""
    c = Canvas(12, 24, SAMPLES)
    X, Y = c.X, c.Y
    cx = 6.0
    arc = lambda nx: np.sqrt(np.clip(1 - nx * nx, 0, 1))
    c.over((0, 0, 0), 0.36 * disc(c, 7.6, 21.7, 4.9, 1.9, soft=1.0))                                   # the shadow, thrown down and to the right
    # the cup: an open iron cup seen from above, wide at its rim and narrowing to its foot
    rim_y, rim_rx, foot_y, foot_rx = 13.3, 5.45, 21.3, 3.2
    t = np.clip((Y - rim_y) / (foot_y - rim_y), 0, 1.3)
    half = rim_rx + (foot_rx - rim_rx) * np.clip(t, 0, 1)
    nx = (X - cx) / half
    bottom = foot_y + foot_rx * TILT * arc((X - cx) / foot_rx)
    cup = smooth((1 - np.abs(nx)) * half / EDGE + 0.5) * smooth((Y - (rim_y - 0.2)) / EDGE + 0.5) * smooth((bottom - Y) / EDGE + 0.5)
    c.over(round_shade(nx, MAROON), cup)
    c.over(STEEL[2], cup * smooth((Y - (bottom - 0.9)) / EDGE + 0.5))                                  # its foot ring
    c.over(STEEL[1], cup * smooth((Y - (bottom - 0.9)) / EDGE + 0.5) * smooth((-0.2 - nx) / 0.3))
    # the team's color: a band that wraps the cup
    b0 = rim_y + 1.45 + (half * TILT) * arc(nx) * 0.95
    b1 = rim_y + 6.75 + (half * TILT) * arc(nx) * 0.95
    band = cup * smooth((Y - b0) / EDGE + 0.5) * smooth((b1 - Y) / EDGE + 0.5)
    c.over(INK, cup * smooth((Y - (b0 - 0.38)) / EDGE + 0.5) * smooth((b1 + 0.38 - Y) / EDGE + 0.5))
    c.over(round_shade(nx, TEAM[team]), band)
    c.over((255, 255, 255), 0.5 * band * smooth((0.07 - np.abs(nx + 0.52)) / 0.04))
    # the rim: an open ellipse, dark inside
    ell = lambda rx, y0=rim_y: np.hypot((X - cx) / rx, (Y - y0) / (rx * TILT))
    c.over(round_shade((X - cx) / rim_rx, STEEL), smooth((1 - ell(rim_rx)) * rim_rx * TILT / EDGE + 0.5))
    c.over((26, 8, 12), smooth((1 - ell(rim_rx - 0.85)) * (rim_rx - 0.85) * TILT / EDGE + 0.5))
    # the charge: a short drum with straight sides and a flat round top, standing in the cup
    top_y, ch_rx, side = 8.3, 4.55, 4.0
    cn = (X - cx) / ch_rx
    low = top_y + side + ch_rx * TILT * arc(cn)
    drum = smooth((1 - np.abs(cn)) * ch_rx / EDGE + 0.5) * smooth((Y - top_y) / EDGE + 0.5) * smooth((low - Y) / EDGE + 0.5)
    c.over(INK, smooth((1 - np.abs((X - cx) / (ch_rx + 0.3))) * ch_rx / EDGE + 0.5) * smooth((Y - top_y) / EDGE + 0.5) * smooth((low + 0.3 - Y) / EDGE + 0.5))
    c.over(round_shade(cn, ((190, 8, 124), (236, 84, 178), (104, 6, 138))), drum)
    c.over((70, 4, 96), 0.55 * drum * smooth((Y - (low - 1.3)) / 1.0))                                 # darker where it goes down into the cup
    d = np.hypot((X - cx) / ch_rx, (Y - top_y) / (ch_rx * TILT))
    face = smooth((1 - d) * ch_rx * TILT / EDGE + 0.5)
    c.over(INK, smooth((1 - np.hypot((X - cx) / (ch_rx + 0.3), (Y - top_y) / (ch_rx * TILT + 0.3))) * ch_rx * TILT / EDGE + 0.5))
    lightd = np.hypot((X - (cx - 1.4)) / ch_rx, (Y - (top_y - 0.5)) / (ch_rx * TILT))
    top = np.array((204, 24, 138), float)[None, None, :] + (np.array((232, 70, 170), float) - np.array((204, 24, 138), float))[None, None, :] * smooth((0.9 - lightd) / 0.9)[..., None]
    c.over(top, face)
    c.over((252, 150, 210), 0.9 * face * smooth((d - 0.80) / 0.08) * smooth((-(X - cx) - (Y - top_y) * 1.5) / 2.0 + 0.4))   # the top's rim catches the light
    c.over((150, 8, 120), 0.7 * face * smooth((d - 0.82) / 0.08) * smooth(((X - cx) + (Y - top_y) * 1.5) / 2.0))
    c.over((60, 10, 50), disc(c, cx, top_y + 0.1, 0.8, 0.42))                                          # where the fuse goes in
    for k in (-0.72, -0.25, 0.3, 0.75):                                                                # rivets on the rim's near edge: the charge stands inside it
        x, y = cx + k * (rim_rx - 0.42), rim_y + (rim_rx - 0.42) * TILT * np.sqrt(1 - k * k)
        c.over((28, 30, 36), disc(c, x + 0.08, y + 0.1, 0.3))
        c.over((204, 210, 220), disc(c, x - 0.04, y - 0.05, 0.19))
    fuse(c, [(cx, top_y), (5.2, 5.6), (6.3, 3.4), (8.7, 2.3)], lit)
    outline(c)
    return c


def bomb_pictures(number):
    """{scale: RGBA array} of the bomb sprite `number` (298 to 305)"""
    for team, (plain, alight) in TEAMS.items():
        if number in (plain, alight):
            c = bomb(team, number == alight)
            if (c.w, c.h) != sprite_size(number):
                raise SystemExit('the bomb is %d x %d, not the original sprite %d\'s %d x %d' % ((c.w, c.h, number) + sprite_size(number)))
            return c.pictures()
    raise KeyError(number)

