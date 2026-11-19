"""The windows' geometry (README.md, "The windows"): what each window draws that never changes, in the window's own numbers.

The numbers are those of the original's 640 x 480 picture, the same ones its animations use (the game draws a window at an offset that it applies itself): the options card is 442 x 440 at (18, 20),
the quick help is the whole 640 x 480, the dialogs are 320 x 224. Each function here returns one SVG: the drawing of the fixed layer WITHOUT the panel behind it and without what changes while the
window is open (switches, slider thumbs, buttons, the text of the edit fields, everything the game writes). dialog_pieces.py cuts such a layer into sprites.
"""
import dialog_kit as K
from dialog_kit import text, measure

CARD = (18, 20, 442, 440)              # the options card: x, y, w, h
SLIDER_Y = (179, 216, 253)             # the three sliders: the thumb's top (the track's top is one row above)
TRACK_X, TRACK_W, TRACK_H = 188, 235, 18
THUMB_W, THUMB_H = 49, 16
THUMB_ROW = 1                          # the thumb sprite is 18 rows high and holds the grip from its row 1 (room for the grip's shadow)
SWITCH_Y = 290                         # the four switches' tops; their lefts: chat on, chat off, quick help on, quick help off
SWITCH_X = {'chat_on': 103, 'chat_off': 151, 'help_on': 356, 'help_off': 404}
FIELD_SPOTS = (('F9', 79, 89, 370), ('F10', 79, 89, 401), ('F11', 287, 298, 370), ('F12', 287, 298, 401))      # label's right end, the ground's left and top
FIELD_W, FIELD_H = 144, 15

QH = (640, 480)
BODY = 9.4            # the quick help's small text (canvas pixels): the largest size at which Libre Baskerville keeps the original's line breaks in the original's boxes
FOOD_PAD = (226, 344, 32, 32)          # the pad under the food cursor (the original shows it on the clay)

COUNTDOWN = (400, 346)                 # include/ants_app/countdown_card.hpp
COUNT_BOX = (115, 80, 170, 122)        # the box with the count, card numbers
SEAT_SIZE = (180, 28)
DOT_COLORS = ('#3fae6a', '#d14b55', '#5c6fe0', '#6e6e76')

# the title's letters: (letter, middle x, middle y, height of the capital, tilt clockwise, width factor), in the window's numbers. The numbers are the original title's own letter boxes, each letter is
# set from the face and then moved: nothing is traced
OPTIONS_LETTERS = (('O', 76, 96, 49, -9, 1.12), ('P', 124.5, 102, 38, 6, 1.12), ('T', 162, 76, 61, -6, .80), ('I', 205, 77, 43, 4, 1.10),
                   ('O', 253, 74, 63, -2, 1.10), ('N', 326, 86, 53, 8, 1.0), ('S', 393, 93, 58, 13, 1.26))
# the headings that dance a little: (size factor, lift, tilt) letter by letter
SETTINGS_MOVES = ((1.06, 0, -3), (.97, .6, 2), (1.02, -.4, -2), (.98, .9, 3), (1.0, 0, -1.5), (1.04, -.5, 2), (.97, .5, -2.5), (1.02, 0, 2))
KEYS_MOVES = ((1.07, 0, -3), (.97, .5, 2), (1.0, 0, -1.5), (1.03, -.4, 2.5), (.98, .7, -2), (1.06, 0, 2), (.98, .4, -2.5), (1.02, -.3, 1.5), (.97, .6, 3), (1.04, 0, -2), (.98, .4, 2), (1.02, -.4, -1.5), (1.0, .3, 2.5))
QH_MOVES = ((1.05, 0, -2), (.98, .4, 1.5), (1.0, 0, -1), (1.02, -.3, 2), (.98, .4, -1.5), (1.04, 0, 1.5), (.98, .3, -2), (1.0, 0, 1), (1.02, -.3, -1.5))


def options_layer():
    """the options card's fixed layer, (w, h, svg) in the card's own numbers (the svg's origin is the card's corner)"""
    cx0, cy0, cw, ch = CARD
    u = K.new_uid()
    X = lambda x: x - cx0
    Y = lambda y: y - cy0
    mid = cw / 2.0
    b = K.jade_letters([(c, X(x), Y(y), h, rot, sx, 0) for c, x, y, h, rot, sx in OPTIONS_LETTERS])
    b += K.jade_ant(u + 'ant', X(OPTIONS_LETTERS[4][1]) - 12.6, Y(OPTIONS_LETTERS[4][2]) - 13.0, .84)       # the ant in the second O
    b += K.jade_letters(K.playful('Settings', mid, Y(157.5), 21.5, 2.2, SETTINGS_MOVES))
    for name, yb in (('Sound Volume', 189), ('Music Volume', 226), ('Map Scroll Rate', 264)):
        b += text(name, X(179), Y(yb), 15, 'jade', 'end')
    for i, sy in enumerate(SLIDER_Y):
        b += K.track(u, X(TRACK_X), Y(sy - 1), TRACK_W, TRACK_H)
        lo, hi = (('low', 'high'), ('low', 'high'), ('slow', 'fast'))[i]
        b += text(lo, X(189), Y(sy + 27), 10, 'small') + text(hi, X(421), Y(sy + 27), 10, 'small', 'end')
    b += text('Chat', X(40), Y(307), 21, 'jade')
    b += text('Quick Help', X(214), Y(307), 21, 'jade')
    b += text('at game start', X(215), Y(319.5), 9.5, 'small')
    b += K.jade_letters(K.playful('Quick Chat keys', mid + 6, Y(352), 21.5, 3.0, KEYS_MOVES))
    for lab, lx, fx, fy in FIELD_SPOTS:
        b += text(lab, X(lx), Y(fy + 13.5), 16, 'jade', 'end')
        b += K.field(u, X(fx), Y(fy), FIELD_W, FIELD_H)
    b += text('Type in your own message for use in QuickChat', X(31), Y(433), 9.6, 'small')
    return cw, ch, K.svg(cw, ch, b, u)


# ---- the quick help ---------------------------------------------------------------------------------------------------------------------------------------------
L_WELLS = ((14, 53, 247, 92), (14, 159, 247, 130), (14, 302, 247, 162))
R_WELLS = ((273, 54, 353, 74), (273, 130, 353, 48), (273, 180, 353, 44), (273, 226, 353, 42), (273, 270, 353, 49), (273, 321, 353, 50), (273, 373, 251, 92))
ARROWS = ((321, 143), (321, 190), (321, 237), (321, 286), (321, 337))
HILLS = (74, 121, 168, 215)             # the lefts of the four hill frames (the game draws the hills in them)


def quickhelp_layer():
    """the quick help's fixed layer, (w, h, svg) in the window's numbers: the title, the wells, the text, the arrows, the pad under the food cursor and the frames of the hills. The pictures that the
    original keeps (ants, power-ups, bombs, fire, hills) and the new cursors and pedestals are drawn over it by the game."""
    u = K.new_uid()
    size = BODY

    def body(s, x, y, anchor='end', bold=False):
        return text(s, x, y, size, 'body', anchor, bold=bold)

    def run(parts, x, y, anchor='end'):
        """a line of plain and bold parts [(text, bold)] that ends (or starts) at x"""
        wd = [measure(p, size) + (.5 * len(p) * size / 10.0 * .12 if b else 0) for p, b in parts]
        cur = x - sum(wd) if anchor == 'end' else x
        out = ''
        for (p, b), w_ in zip(parts, wd):
            out += text(p, cur, y, size, 'body', 'start', bold=b)
            cur += w_
        return out
    head = lambda s, x, y, anchor='end': text(s, x, y, 11, 'head', anchor)
    b = K.jade_letters(K.playful('Quick Help Game Instructions', 320, 28.5, 16.6, 1.6, QH_MOVES))
    b += body('Welcome to Microsoft Ants, a multi player internet game where you will be in charge of a team of Ants.', 320, 45.5, 'middle')
    for x, y, w, h in L_WELLS + R_WELLS:
        b += K.well(u, x, y, w, h)
    R = 255
    # how to win
    b += head('How to Win', 148, 66) + head('the Game', R, 66)
    b += body('Use your Ants', 150, 83) + body('to get the most', R, 83)
    b += body('Points', 163, 95) + run([('by getting ', False), ('Food', True), ('.', False)], R, 95)
    b += body('Check your score at the top of the screen,', R, 109) + body("and check the other player's scores by looking", R, 122) + body('at the bottom of the screen.', R, 136)
    # how to move
    b += head('How to Move', 143, 172) + head('Your Ants:', R, 172)
    b += body('1. Select an Ant', 142, 184) + body('by clicking on it', R, 184)
    b += body('– no click', 143, 197) + body('and drag needed.', R, 197)
    b += body('2. Click on where you want the Ant', 212, 212) + run([('to move, or push the ', False), ('Move button', True)], 212, 225)
    b += body('then click where you want', 212, 238) + body('the Ant to move.', 212, 251)
    wa = measure(' in any direction', size)
    b += head('Move Around the Board', R - wa, 270) + body(' in any direction', R, 270)
    b += body('by moving the cursor to the edges of the screen.', R, 284)
    # game play
    b += head('Game Play:', R, 315)
    b += body('Move', 66, 327) + run([('your Ants to ', False), ('Food', True), (' and they will', False)], R, 327)
    b += body('pick it up', 65, 341) + body('and take it back to your AntHill.', R, 341)
    b += body('Your cursor will look like this', 222, 357)
    b += run([('over a piece of ', False), ('Food', True), ('.', False)], 222, 371)
    b += run([('If you already have a piece of ', False), ('Food', True), (", you can't get", False)], R, 387)
    b += body("any more until it's dropped off at your AntHill.", R, 401)
    # power-ups
    Lx = 277
    b += head('Power Ups', Lx + 1, 66, 'start')
    b += body('Power Ups can drop from Special Flowers. Ants can pick up Power Ups', Lx, 78, 'start')
    b += body('which give them special powers. Each Ant can have only one Power Up.', Lx, 91, 'start')
    b += body('If you pick up another Power Up you drop the first Power Up.', Lx, 104, 'start')
    b += head('Power Up', 279, 121.5, 'start') + head('Ant', 358, 121.5, 'start') + head('Special Power', 409, 121.5, 'start')
    rows = (('SwimAnt', 144, [[('Can ', False), ('Swim', True), (' in water, ', False), ('builds & destroys', True)], [('Mud Mound Bridges with the shovel', False)]], (161, 173)),
            ('BombAnt', 194, [[('Can ', False), ('place & put out', True), (' Bombs', False)]], (219,)),
            ('Thief Ant', 240, [[('Can ', False), ('Steal Food', True), (' only from other AntHills', False)]], (263,)),
            ('FireAnt', 284, [[('Can ', False), ('place & put out', True), (' Walls of Fire', False)]], (314,)),
            ('CombAnt', 335, [[('Can ', False), ('attack', True), (' with Super Ant Attack Power', False)], [('& Auto Attack enemy Ants near it', False)]], (354, 366)))
    for (name, ny, lines, ys), (ax, ay) in zip(rows, ARROWS):
        b += body(name, 410, ny, 'start', bold=True)
        for ln, yy in zip(lines, ys):
            b += run(ln, 410, yy, 'start')
        b += K.arrow(u, ax, ay, 27, 22)
    # the steps
    b += head("Steps to Use an Ant's Special Power:", 278, 388, 'start')
    b += body('1. Select a powered Ant by clicking on it', 319, 403, 'start')
    b += run([('2. Push the ', False), ('Use special power', True)], 314, 419, 'start') + body('button', 310, 431, 'start', bold=True)
    b += body('3. Click where on the map to use that', 277, 446, 'start') + body('special power.', 277, 458, 'start')
    # the pad under the food cursor and the frames of the four hills
    b += K.S.rrect(FOOD_PAD[0], FOOD_PAD[1], FOOD_PAD[2], FOOD_PAD[3], 3, '#d9521a')
    for hx in HILLS:
        b += K.well(u, hx - 1, 408, 45, 45, 1, fill='#6b5a54')
    return QH[0], QH[1], K.svg(QH[0], QH[1], b, u)


# ---- the countdown card -----------------------------------------------------------------------------------------------------------------------------------------
def countdown_title():
    """the card's title, (w, h, svg): a 400 wide strip of the card's own numbers, the baseline at 44, the letters centered"""
    u = K.new_uid()
    w, h = COUNTDOWN[0], 64
    return w, h, K.svg(w, h, text('Get ready to play!', w / 2.0, 44, 27, 'jade', 'middle'), u)


COUNT_BOX_PICTURE = (176, 128)        # the bezel of the count box with its cast shadow and the shadow's soft edge: from (x - 4, y - 4) of the ground


def countdown_count_box():
    """the count box's bezel and ground, (w, h, svg): the ground is the box inset by 3 on every side (164 x 116), its bezel starts one pixel outside the box, which is the picture's top left"""
    u = K.new_uid()
    w, h = COUNT_BOX_PICTURE
    origin = (COUNT_BOX[0] - 1, COUNT_BOX[1] - 1)
    return w, h, K.svg(w, h, K.field(u, COUNT_BOX[0] + 3, COUNT_BOX[1] + 3, COUNT_BOX[2] - 6, COUNT_BOX[3] - 6), u, origin)


def countdown_seat():
    """one seat row without its color and its words, (w, h, svg): the sunken well"""
    u = K.new_uid()
    w, h = SEAT_SIZE
    return w, h, K.svg(w, h, K.S.sunken(u + 'r', 0, 0, w, h, 2, '#0b0f0d', wd='#010604', wl='#2e7159'), u)


def countdown_dot(i):
    """the color of seat i, (w, h, svg): a dark square with the color inside"""
    u = K.new_uid()
    return 12, 12, K.svg(12, 12, K.S.rrect(0, 0, 12, 12, 2, '#06150e') + K.S.rrect(1, 1, 10, 10, 1.5, DOT_COLORS[i]), u)
