"""Lettering: an open-licensed (SIL OFL 1.1) serif set into each label's own ink box (measured on the original screenshot), then ONE post-processing stack on every label:
the original's one-pixel dark drop shadow (down and right), a shaded lower-right edge, a lit upper-left edge, and a cream-to-pale-green fill. Light from the top left, like the rest of the art."""
import base64, json, os
from PIL import Image, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__)) + '/'
FONTS = {   # key: (file, css family, licence text, name): both are SIL Open Font License 1.1 and are used here at generation time only (fonts/); the lettering is baked into the sprites
    'lb': ('LibreBaskerville-Regular.ttf', 'LB', 'LibreBaskerville-OFL.txt', 'Libre Baskerville Regular'),
    'li': ('Lora-BoldItalic.ttf', 'LBI', 'Lora-OFL.txt', 'Lora Bold Italic'),
}
CHAT_KEY = 'li'       # the Chat word is a script in the original: a true italic (same licence family as the others), not the upright face slanted
# text and where to search for its ink in the original (abs, 16:9); the layout of the label (origin, in the 960 layout) is where the piece that carries it starts
LABELS = {
    'Time': ((14, 2, 58, 18), (0, 0)), 'Map': ((924, 30, 960, 52), (778, 30)), 'Move': ((798, 136, 850, 154), (778, 136)), 'Stop': ((908, 176, 956, 196), (778, 136)),
    'Chat': ((790, 268, 850, 296), (778, 268)), 'Send to:': ((796, 498, 850, 520), (778, 268)), 'All': ((858, 507, 892, 522), (852, 503)),
    'Status': ((798, 228, 860, 252), (778, 136)), 'Help': ((800, 11, 836, 24), (796, 7)), 'Options': ((848, 11, 892, 24), (845, 7)), 'Quit': ((902, 11, 938, 24), (899, 7)),
}
# the pedestals' labels and the Team button's label. Their ink boxes come from the original's label sprites (sprite x + 320 for the 960 layout, one row in from the top and bottom).
EXTRA = {'TeamUp': (797, 143, 47, 8, (778, 136)), 'Hatch': (803, 141, 36, 9, (778, 136)), 'Attack': (862, 142, 40, 8, (778, 136)), 'Bomb': (864, 141, 35, 9, (778, 136)),
         'FireWall': (859, 142, 48, 8, (778, 136)), 'Steal': (866, 142, 33, 8, (778, 136)), 'Bridge': (862, 142, 39, 10, (778, 136)), 'Team': (906, 508, 31, 9, (899, 503))}
for _n, (_x, _y, _w, _h, _o) in EXTRA.items(): LABELS[_n] = ((0, 0, 0, 0), _o)
S0 = 200
_targets = None
_metrics = {}

def targets():
    """the ink boxes of the labels (x, y, w, h) in the 960 layout: measured once on the original's own screen and kept in label_boxes.json"""
    global _targets
    if _targets is None:
        with open(HERE + 'label_boxes.json') as f:
            _targets = json.load(f)
        for n, (x, y, w, h, o) in EXTRA.items(): _targets[n] = [x, y, w, h]
    return _targets

def metrics(key, text):
    k = (key, text)
    if k not in _metrics:
        from PIL import ImageDraw
        f = ImageFont.truetype(HERE + 'fonts/' + FONTS[key][0], S0)
        im = Image.new('L', (2000, 700), 0)
        ImageDraw.Draw(im).text((100, 400), text, font=f, fill=255, anchor='ls')
        x0, y0, x1, y1 = im.point(lambda v: 255 if v > 100 else 0).getbbox()
        _metrics[k] = (x0 - 100, y0 - 400, x1 - 100, y1 - 400)
    return _metrics[k]

def font_css(key):
    """every face a picture uses (the labels' face and the Chat word's italic)"""
    out = ''
    for kk in dict.fromkeys((key, CHAT_KEY)):
        fn, fam, _, _ = FONTS[kk]
        b = base64.b64encode(open(HERE + 'fonts/' + fn, 'rb').read()).decode()
        out += "@font-face{font-family:'%s';src:url(data:font/ttf;base64,%s)}\n" % (fam, b)
    return out

def defs(uid, hover=False):
    top, mid, bot = ('#ffffff', '#ffffff', '#d6f6ea') if hover else ('#fffbe8', '#f0fff4', '#c1ecda')
    return ('<linearGradient id="%s_lf" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="%s"/><stop offset=".5" stop-color="%s"/><stop offset="1" stop-color="%s"/></linearGradient>' % (uid, top, mid, bot))

EDGE = .32          # the width of the bevel inside the letters, in canvas pixels
GROW = .44          # how much the face is thickened (a stroke of this width, half of it outside), in canvas pixels: keeps the hairlines of the serif alive at 1x without making the letters bold
_n = [0]

def label(name, key, uid, shift=(0, 0), hover=False, pressed=False, skew=0, dy=0):
    """the label as svg in the coordinates of the piece that carries it (origin = its place in the 960 layout); shift moves the whole label (pressed buttons: 1, 1).
    ONE scale for both directions (a word is never stretched into its ink box), set by the ink box's height and centered on the
    ink box; and the bevel is INSIDE the letter (a lit band along the upper-left edges, a shaded band along the lower-right ones, cut out of the glyph by masks), so the letters keep the face's own weight."""
    (_, origin) = LABELS[name]
    bx, by, bw, bh = targets()[name]
    if name == 'Chat': key = CHAT_KEY
    l, t, r, b = metrics(key, name)
    by += dy
    sx, sy = bw / float(r - l), bh / float(b - t)
    s = sy * min(1.12, max(1.0, (sx / sy) ** .5))      # a word the original drew wide gets a little more size, never a stretch
    if (r - l) * s > bw + 1: s = (bw + 1) / float(r - l)
    if name in EXTRA and name != 'Team' or name == 'Move':
        # the eight pedestal labels share ONE size (capital height 7.5 canvas pixels, between the original's 7 and 8), whatever each word's own ink box says, so that Move beside Attack reads as one set
        s = 7.5 / -metrics(key, 'H')[1]
        if (r - l) * s > bw + 5: s = (bw + 5) / float(r - l)
    cx = bx + bw / 2.0; cy = by + bh / 2.0
    tx = cx - origin[0] - (l + r) / 2.0 * s + shift[0]
    ty = cy - origin[1] - (t + b) / 2.0 * s + shift[1]
    fam = FONTS[key][1]
    tr = 'translate(%.3f %.3f) scale(%.5f)' % (tx, ty, s)
    _n[0] += 1
    u = '%s_%d' % (uid, _n[0])
    extra_path = ''
    def txt(fill, ddx, ddy, op=None, extra=''):
        body = '<text x="0" y="0" font-family="%s" font-size="%d">%s</text>' % (fam, S0, name)
        if extra_path: body += '<path d="%s"/>' % extra_path
        return ('<g transform="translate(%s %s)"><g transform="%s" fill="%s"%s%s>%s</g></g>' % (ddx, ddy, tr, fill, (' fill-opacity="%s"' % op) if op else '', extra, body))
    grow = ' stroke="url(#%s_lf)" stroke-width="%.2f" stroke-linejoin="round"' % (uid, GROW / s)
    X0, Y0, Wd, Ht = bx - origin[0] - 8 + shift[0], by - origin[1] - 8 + shift[1], bw + 60, bh + 16
    d = ('<mask id="%s_a" maskUnits="userSpaceOnUse" x="%s" y="%s" width="%s" height="%s">%s%s</mask>' % (u, X0, Y0, Wd, Ht, txt('#fff', 0, 0), txt('#000', EDGE, EDGE))        # the upper-left inner band
         + '<mask id="%s_b" maskUnits="userSpaceOnUse" x="%s" y="%s" width="%s" height="%s">%s%s</mask>' % (u, X0, Y0, Wd, Ht, txt('#fff', 0, 0), txt('#000', -EDGE, -EDGE)))  # the lower-right inner band
    out = '<defs>%s</defs>' % d
    if name == 'Chat':
        out += txt('#0b1307', 0, 0, '.5', ' stroke="#0b1307" stroke-opacity=".5" stroke-width="%.2f" stroke-linejoin="round"' % (1.8 / s))   # a dark halo, so that the white rule lines behind the word do not run into the letters
    out += txt('#0b1307', 1, 1, '.9')                       # the original's drop shadow: one pixel down and right, dark
    out += txt('url(#%s_lf)' % uid, 0, 0, None, grow)       # the face: cream to pale green
    out += '<rect x="%s" y="%s" width="%s" height="%s" fill="#8fcdb2" fill-opacity=".9" mask="url(#%s_b)"/>' % (X0, Y0, Wd, Ht, u)   # the shaded lower-right band
    out += '<rect x="%s" y="%s" width="%s" height="%s" fill="#ffffff" mask="url(#%s_a)"/>' % (X0, Y0, Wd, Ht, u)                    # the lit upper-left band
    return out

def key_info(key):
    fn, fam, lic, name = FONTS[key]
    return name, lic
