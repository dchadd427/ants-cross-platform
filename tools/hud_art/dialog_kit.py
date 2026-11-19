"""The look of the windows' art: the clay panel in its green frame, wells, buttons, switches, sliders, edit fields and the lettering (README.md, "The windows").

Everything is drawn as SVG in canvas pixels and rendered by headless Chrome at a device scale of 1, 2 or 3 (common.render_sheet), so a piece has the same footprint at every scale, is smooth at 2x and
3x and lands on whole pixels at 1x. system.py is the bevel system, lettering.py holds the faces. The window geometry that uses these pieces is in dialog_windows.py.

The rules of the look: light from the top left; a raised thing = dark rim, lit edge up and left, shaded edge down and right, a face that falls from light to dark; a sunken thing = dark wall up and
left, lit lip down and right, a shadow that falls into it; every band is whole canvas pixels wide and every corner is a miter or a quarter round, so lines meet at 1x; lettering is SET IN A FACE
(Libre Baskerville) and then given the material, never traced. Text that the game itself writes at run time (names, digits, sentences) is not drawn here.
"""
import numpy as np
from PIL import Image, ImageFont

import common
import lettering as LET
import system as S

# ---- colors -----------------------------------------------------------------------------------------------------------------------------------------------------
CLAY = '#db4b13'                 # the original's clay (219, 75, 19)
FIELD = '#0f0f17'                # the edit fields' ground (15, 15, 23)
# the frame's ridge, top to bottom of a horizontal band (left to right of a vertical one): the HUD's rail greens around one highlight in the middle, so the bright line meets itself at all four corners
RIDGE = ['#05120c', '#184434', '#245c49', '#357a62', '#205241', '#13362a', '#05120c']
FRAME_W = len(RIDGE)
SPEC = '#dcfff0'                 # a specular edge or glint (always thin and small)

# ---- textures ---------------------------------------------------------------------------------------------------------------------------------------------------
TOOTH = (160, 120)               # the clay's tooth tile in canvas pixels; it is also the repeat of the panel's edge pieces (dialog_pieces.py)
TK = 3                           # the clay tile is made at 3 samples per canvas pixel
_files = {}


def _png(name, rgba):
    """the picture as a file of the work folder (written once per run): the SVG names it by its URL"""
    path = common.scratch() + name
    if path not in _files:
        Image.fromarray(rgba, 'RGBA').save(path)
        _files[path] = 'file://' + path
    return _files[path]


def _blur(a, sigma):
    h, w = a.shape
    fy = np.fft.fftfreq(h)[:, None]
    fx = np.fft.fftfreq(w)[None, :]
    return np.real(np.fft.ifft2(np.fft.fft2(a) * np.exp(-2 * (np.pi ** 2) * (sigma ** 2) * (fx ** 2 + fy ** 2))))


def clay_tile(seed=7, amp=30):
    """fired clay: broad soft blotches, a middle grain and a fine tooth, warm light and warm dark at a low alpha (no lines, no speckles of another hue). Periodic: its edges meet"""
    tw, th = TOOTH
    w, h = tw * TK, th * TK
    rng = np.random.default_rng(seed)
    n = _blur(rng.standard_normal((h, w)), 5.0 * TK)
    n /= n.std()
    f = _blur(rng.standard_normal((h, w)), 0.55 * TK)
    f /= f.std()
    m = _blur(rng.standard_normal((h, w)), 1.6 * TK)
    m /= m.std()
    v = .70 * n + .20 * m + .42 * f
    rgba = np.zeros((h, w, 4), np.uint8)
    rgba[..., :3] = np.where(v[..., None] > 0, (255, 196, 120), (96, 18, 0))
    rgba[..., 3] = np.clip(np.abs(v) * amp * .55, 0, amp).astype(np.uint8)
    return _png('dlg_clay.png', rgba)


# Brushed metal and glints are drawn for EACH scale on that scale's own pixel grid (as the cursors are), so the streaks are whole device pixels at 1x, 2x and 3x.
BRUSH_AMP = 120                  # the most a streak covers the metal under it (alpha, of 255)
BT_W, BT_H = 128, 32             # the controls' tile in canvas pixels: wider than any control and 32 rows, so the three sliders (37 rows apart) and two buttons side by side all differ
GT_W, GT_H = 128, 8              # the glint tile
FRAME_PERIOD = TOOTH                     # the frame's brushing and glint repeat with the panel's pieces: 160 across along the top and bottom, 120 down along the left and right


def _streaks(w, h, k, seed, lengths):
    """h rows of w device pixels: every row is one streak of the brush, long along x (periodic), of uneven strength, BROKEN (gaps where nothing shows). Values -1 .. 1."""
    rng = np.random.default_rng(seed)
    out = np.zeros((h, w))
    fx = np.fft.fftfreq(w)
    for r in range(h):
        length = rng.choice(lengths) * k                                         # how long the streaks of this row run, in device pixels
        v = np.real(np.fft.ifft(np.fft.fft(rng.standard_normal(w)) * np.exp(-2 * (np.pi ** 2) * (length ** 2) * fx ** 2)))
        v /= v.std()
        v = np.sign(v) * np.clip(np.abs(v) - .30, 0, None) / 1.1                 # the gaps: a streak fades out and another starts
        out[r] = np.clip(v, -1, 1) * rng.uniform(.45, 1.0)                       # some rows bite deeper than others
    return out


def _brush_rgba(width, k):
    """light and dark streaks one device pixel thick at 1x and 2x and one or two at 3x, running along x for `width` canvas pixels (periodic), BT_H rows"""
    w, h = width * k, BT_H * k
    n = _streaks(w, h, k, 11 + k, (4, 7, 11, 16, 24))
    if k == 3:                                                                   # a third of the streaks are two device pixels thick
        rng = np.random.default_rng(5)
        for r in range(0, h - 1):
            if rng.random() < .33:
                n[r + 1] = n[r] * .85
    rgba = np.zeros((h, w, 4), np.uint8)
    rgba[..., :3] = np.where(n[..., None] > 0, (206, 250, 230), (0, 8, 5))
    rgba[..., 3] = np.clip(np.abs(n) * BRUSH_AMP * np.where(n > 0, .8, 1.0), 0, 255).astype(np.uint8)      # (a light streak shows more than a dark one of the same alpha: held back a little, so the average does not rise)
    return rgba


def _glint_rgba(width, k):
    """a broken highlight: rows of the specular color whose strength comes and goes along x (never one clean line)"""
    n = _streaks(width * k, GT_H * k, k, 41 + k, (3, 5, 8, 13))
    rgba = np.zeros(n.shape + (4,), np.uint8)
    rgba[..., :3] = (232, 255, 244)
    rgba[..., 3] = np.clip(120 + n * 190, 0, 255).astype(np.uint8)
    return rgba


def _grain_rgba(k):
    """a very fine grain for the lettering's faces: single device pixels, a little lighter or darker, barely there"""
    n = np.random.default_rng(23 + k).standard_normal((64 * k, 64 * k))
    rgba = np.zeros(n.shape + (4,), np.uint8)
    rgba[..., :3] = np.where(n[..., None] > 0, (206, 250, 230), (0, 8, 5))
    rgba[..., 3] = np.clip(np.abs(n) * 16, 0, 34).astype(np.uint8)
    return rgba


def _marks(k):
    """the pictures that the drawings name by a mark (`@@NAME@@`), made for scale k"""
    return {'@@BRUSH@@': _png('dlg_brush_k%d.png' % k, _brush_rgba(BT_W, k)),
            '@@FBRUSH_H@@': _png('dlg_fbrush_h_k%d.png' % k, _brush_rgba(FRAME_PERIOD[0], k)),
            '@@FBRUSH_V@@': _png('dlg_fbrush_v_k%d.png' % k, np.ascontiguousarray(_brush_rgba(FRAME_PERIOD[1], k).transpose(1, 0, 2))),
            '@@GLINT@@': _png('dlg_glint_k%d.png' % k, _glint_rgba(GT_W, k)),
            '@@FGLINT@@': _png('dlg_fglint_k%d.png' % k, _glint_rgba(FRAME_PERIOD[0], k)),
            '@@GRAIN@@': _png('dlg_grain_k%d.png' % k, _grain_rgba(k))}


# ---- defs shared by a drawing ---------------------------------------------------------------------------------------------------------------------------------
_uid = [0]


def new_uid():
    """a name for one drawing's ids: ids are shared by all the drawings of a page, so each drawing has its own"""
    _uid[0] += 1
    return 'd%d' % _uid[0]


def _pattern(pid, w, h, mark):
    return '<pattern id="%s" width="%d" height="%d" patternUnits="userSpaceOnUse"><image href="%s" width="%d" height="%d" style="image-rendering:pixelated"/></pattern>' % (pid, w, h, mark, w, h)


def defs(uid):
    """the gradients, patterns and filters that the pieces below name: once per svg"""
    url = clay_tile()
    tw, th = TOOTH
    d = '<pattern id="%s_tooth" width="%d" height="%d" patternUnits="userSpaceOnUse"><image href="%s" width="%d" height="%d"/></pattern>' % (uid, tw, th, url, tw, th)
    d += '<linearGradient id="%s_well" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#dc9057"/><stop offset="1" stop-color="#e0945b"/></linearGradient>' % uid       # never brighter than the original's (225, 149, 92)
    d += _pattern('g_brush', BT_W, BT_H, '@@BRUSH@@') + _pattern('g_fbrush', FRAME_PERIOD[0], BT_H, '@@FBRUSH_H@@') + _pattern('g_fbrushv', BT_H, FRAME_PERIOD[1], '@@FBRUSH_V@@')
    d += _pattern('g_grain', 64, 64, '@@GRAIN@@') + _pattern('g_glint', GT_W, GT_H, '@@GLINT@@') + _pattern('g_fglint', FRAME_PERIOD[0], GT_H, '@@FGLINT@@')
    d += '<filter id="%s_vig" x="-10%%" y="-10%%" width="120%%" height="120%%"><feGaussianBlur stdDeviation="4.5"/></filter>' % uid
    d += '<filter id="%s_soft" x="-20%%" y="-20%%" width="140%%" height="140%%"><feGaussianBlur stdDeviation=".9"/></filter>' % uid
    d += '<filter id="%s_soft2" x="-20%%" y="-20%%" width="140%%" height="140%%"><feGaussianBlur stdDeviation="1.6"/></filter>' % uid
    # the frame's ridge: one gradient for the horizontal bands, one for the vertical ones (hard one-pixel rims, a smooth round between them)
    n = float(FRAME_W)
    stops = ''
    for i, c in enumerate(RIDGE):
        if i in (0, FRAME_W - 1):
            stops += '<stop offset="%.4f" stop-color="%s"/><stop offset="%.4f" stop-color="%s"/>' % (i / n, c, (i + 1) / n, c)
        else:
            stops += '<stop offset="%.4f" stop-color="%s"/>' % ((i + .5) / n, c)
    d += '<linearGradient id="%s_rh" x1="0" y1="0" x2="0" y2="1">%s</linearGradient><linearGradient id="%s_rv" x1="0" y1="0" x2="1" y2="0">%s</linearGradient>' % (uid, stops, uid, stops)
    return d


def svg(w, h, body, uid, origin=(0, 0)):
    """one drawing of w x h canvas pixels with the shared defs of `uid`. `origin` is the drawing's own number for its top left: a sprite is drawn at the place it has in its window's numbers, so that
    the brushing (whose tiles are laid from the window's corner) falls on it as it does in the window"""
    return ('<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="%s" height="%s" viewBox="%s %s %s %s"><defs>%s</defs>%s</svg>'
            % (w, h, origin[0], origin[1], w, h, defs(uid), body))


def render(items, W, H, k, tag):
    """items: [(x, y, svg)] on a W x H canvas; the picture at scale k (RGBA, transparent where nothing is drawn). The brushing and the glints of scale k are put in here."""
    marks = _marks(k)
    out = []
    for x, y, sv in items:
        for m, url in marks.items():
            sv = sv.replace(m, url)
        out.append((x, y, sv))
    return common.render_sheet(out, W, H, k, tag=tag, css=LET.font_css('lb'))


# ---- the panel -----------------------------------------------------------------------------------------------------------------------------------------------------
def _obb(uid, stops):
    return '<linearGradient id="%s" x1="0" y1="0" x2="0" y2="1">%s</linearGradient>' % (uid, ''.join('<stop offset="%s" stop-color="%s"/>' % s for s in stops))


def ridge(uid, x, y, w, h, t=FRAME_W):
    """a green ridge of thickness t around the rectangle (inside it): four mitered bands, each with the same profile across it"""
    x0, y0, x1, y1 = x, y, x + w, y + h

    def band(pts, g, bx, by, bw, bh):
        # each band's gradient is its own box (objectBoundingBox of a rect clipped by the trapezoid)
        cid = '%s_c%d' % (uid, band.n)
        band.n += 1
        return '<clipPath id="%s">%s</clipPath><rect x="%s" y="%s" width="%s" height="%s" fill="url(#%s_%s)" clip-path="url(#%s)"/>' % (cid, S.poly(pts, '#000'), bx, by, bw, bh, uid.split('~')[0], g, cid)
    band.n = 0
    # the ring in one flat green first: where two bands meet on a diagonal, what shows through the antialiased seam is the ridge's own color, never what lies behind
    s0 = '<path fill-rule="evenodd" fill="%s" d="M%s %sH%sV%sH%sZ M%s %sH%sV%sH%sZ"/>' % (RIDGE[4], x0, y0, x1, y1, x0, x0 + t, y0 + t, x1 - t, y1 - t, x0 + t)
    s = s0 + band([(x0, y0), (x1, y0), (x1 - t, y0 + t), (x0 + t, y0 + t)], 'rh', x0, y0, w, t)
    s += band([(x0, y1), (x0 + t, y1 - t), (x1 - t, y1 - t), (x1, y1)], 'rh', x0, y1 - t, w, t)
    s += band([(x0, y0), (x0 + t, y0 + t), (x0 + t, y1 - t), (x0, y1)], 'rv', x0, y0, t, h)
    s += band([(x1, y0), (x1, y1), (x1 - t, y1 - t), (x1 - t, y0 + t)], 'rv', x1 - t, y0, t, h)
    # the brushing runs along each band; its tiles repeat with the panel's edge pieces, so the pieces tile without a seam. A broken glint lies along the top band, which faces the light
    hb = lambda yy: '<rect x="%s" y="%s" width="%s" height="%s" fill="url(#g_fbrush)"/>' % (x0 + 2, yy, w - 4, t - 2)
    vb = lambda xx: '<rect x="%s" y="%s" width="%s" height="%s" fill="url(#g_fbrushv)"/>' % (xx, y0 + 2, t - 2, h - 4)
    clip = lambda pts, body: '<g clip-path="url(#%s)">%s</g>' % (pts, body)
    s += clip('%s_c0' % uid, hb(y0 + 1)) + clip('%s_c1' % uid, hb(y1 - t + 1)) + clip('%s_c2' % uid, vb(x0 + 1)) + clip('%s_c3' % uid, vb(x1 - t + 1))
    s += '<rect x="%s" y="%s" width="%s" height="1" fill="url(#g_fglint)" fill-opacity=".4"/>' % (x0 + t, y0 + 2, w - 2 * t)
    return s


def panel_clay(uid, w, h):
    """the panel's ground: opaque clay with its periodic tooth over it, w x h from the panel's top left (the tooth's tiles start there). The game repeats a w x h piece of it over a whole panel"""
    return '<rect x="0" y="0" width="%s" height="%s" fill="%s"/><rect x="0" y="0" width="%s" height="%s" fill="url(#%s_tooth)"/>' % (w, h, CLAY, w, h, uid)


def panel_frame(uid, x, y, w, h, t=FRAME_W):
    """everything the panel has over its clay, on a transparent ground: a faint darkening toward the frame all round (it dies away within about 24 pixels of the outer edge, so a panel of any size is
    the same eight pieces), a soft shadow that the frame's upper and left edges cast onto the clay, a lit lip along the lower right and the frame's ridge"""
    ix, iy, iw, ih = x + t, y + t, w - 2 * t, h - 2 * t
    cid = '%s_pc' % uid
    s = '<clipPath id="%s"><rect x="%s" y="%s" width="%s" height="%s"/></clipPath><g clip-path="url(#%s)">' % (cid, ix, iy, iw, ih, cid)
    s += '<rect x="%s" y="%s" width="%s" height="%s" fill="none" stroke="#5a1400" stroke-opacity=".22" stroke-width="12" filter="url(#%s_vig)"/>' % (ix - 2, iy - 2, iw + 4, ih + 4, uid)       # the darkening toward the frame
    s += '<path d="M%s %sV%sH%s" fill="none" stroke="#3a0c00" stroke-opacity=".62" stroke-width="5" filter="url(#%s_soft2)"/>' % (ix, iy + ih, iy, ix + iw, uid)                              # the frame's cast shadow: upper and left
    s += '</g>'
    s += '<path d="M%s %sH%sV%s" fill="none" stroke="#ffb27a" stroke-opacity=".30" stroke-width="1"/>' % (ix + 1, iy + ih - .5, ix + iw - .5, iy + 1)
    s += ridge(uid + '~p', x, y, w, h, t)
    return s


def well(uid, x, y, w, h, r=3, fill=None):
    """a sunken well in the clay for text and pictures: a dark wall up and left, a lit lip down and right, the lighter clay inside"""
    well.n = getattr(well, 'n', 0) + 1
    g = '%s_w%d' % (uid, well.n)
    m = min(w, h) / 2.0
    s = '<clipPath id="%s"><polygon points="%s,%s %s,%s %s,%s %s,%s %s,%s"/></clipPath>' % (g, x + w, y, x + w, y + h, x, y + h, x + m, y + h - m, x + w - m, y + m)
    s += S.rrect(x, y, w, h, r, '#96300a') + '<g clip-path="url(#%s)">%s</g>' % (g, S.rrect(x, y, w, h, r, '#ffb680'))
    s += S.rrect(x + 1, y + 1, w - 2, h - 2, max(r - 1, 0), fill or 'url(#%s_well)' % uid)
    return s


# ---- lettering -----------------------------------------------------------------------------------------------------------------------------------------------------
_tn = [0]
_font = []


def measure(s, size):
    """the advance width of a line at `size` (canvas pixels)"""
    if not _font:
        _font.append(ImageFont.truetype(LET.font_path('lb'), 400))
    return _font[0].getlength(s) * size / 400.0


def _t(s, x, y, size, anchor, fill, extra='', dx=0, dy=0, sp=0):
    esc = s.replace('&', '&amp;').replace('<', '&lt;')
    return '<text xml:space="preserve" x="%s" y="%s" font-family="LB" font-size="%s" text-anchor="%s" fill="%s"%s%s>%s</text>' % (
        x + dx, y + dy, size, anchor, fill, (' letter-spacing="%s"' % sp) if sp else '', extra, esc)


METAL_FACE = [(0, '#367f68'), (.28, '#286552'), (.34, '#367b63'), (.42, '#215848'), (1, '#11392d')]      # top to bottom of a letter: the face falls from light to dark, with one thin band of reflected light


def text(s, x, y, size, style, anchor='start', sp=0, bold=False, shift=(0, 0), hover=False, gold=False):
    """one line set in the face at (x, y) = the baseline's start / middle / end, `size` canvas pixels, in a material:
       body   the small dark text of the quick help (plain; `bold` thickens it)
       small  the small labels on the clay itself (low / high, hints): dark, plain
       head   the small headings in a well: deep green, heavier
       jade   headings: green letters that stand on the clay (dark rim and cast shadow, a face from light to deep green, a lit upper-left edge, a shaded lower-right edge)
       label  the buttons' words: the HUD's own stack (cream face, lit and shaded inner edges, one-pixel dark drop shadow); `hover` lights it, `gold` is the switch that is on
    """
    _tn[0] += 1
    u = 't%d' % _tn[0]
    x += shift[0]
    y += shift[1]
    T = lambda fill, extra='', dx=0, dy=0: _t(s, x, y, size, anchor, fill, extra, dx, dy, sp)
    if style in ('body', 'small', 'head'):
        col = {'small': '#000000', 'body': '#000000', 'head': '#03130c'}[style]       # (small and body: as dark as the original's black text, so no contrast is lost)
        wgt = {'head': 1.12, 'small': .66}.get(style, 1.04 if bold else .42) * size / 10.0
        return T(col, ' stroke="%s" stroke-width="%.2f" stroke-linejoin="round"' % (col, wgt))
    # the two that carry inner edges need the line's box for their masks
    wdt = measure(s, size) + sp * len(s)
    bx = x - (wdt if anchor == 'end' else wdt / 2.0 if anchor == 'middle' else 0) - 6
    X0, Y0, Wd, Ht = bx, y - size * 1.05 - 4, wdt + 12, size * 1.5 + 8

    def masks(edge):
        return ('<mask id="%s_a" maskUnits="userSpaceOnUse" x="%s" y="%s" width="%s" height="%s">%s%s</mask>' % (u, X0, Y0, Wd, Ht, T('#fff'), T('#000', '', edge, edge))
                + '<mask id="%s_b" maskUnits="userSpaceOnUse" x="%s" y="%s" width="%s" height="%s">%s%s</mask>' % (u, X0, Y0, Wd, Ht, T('#fff'), T('#000', '', -edge, -edge)))
    R = lambda fill, m, op='': '<rect x="%s" y="%s" width="%s" height="%s" fill="%s"%s mask="url(#%s_%s)"/>' % (X0, Y0, Wd, Ht, fill, op, u, m)
    if style == 'label':
        top, mid, bot = ('#ffffff', '#ffffff', '#e2f8ee') if hover else ('#fffdf0', '#f6fff9', '#d2f0e4')
        if gold:
            top, mid, bot = '#fff8c8', '#ffe66e', '#f0c93a'
        g = _obb(u + '_f', [(0, top), (.5, mid), (1, bot)])
        grow = ' stroke="url(#%s_f)" stroke-width="%.2f" stroke-linejoin="round"' % (u, LET.GROW)
        out = '<defs>%s%s</defs>' % (g, masks(LET.EDGE))
        out += T('#0b1307', ' fill-opacity=".9"', 1, 1)
        out += T('url(#%s_f)' % u, grow)
        out += R('#b98a00' if gold else '#8fcdb2', 'b', ' fill-opacity=".9"') + R('#ffffff', 'a')
        return out
    if style == 'jade':            # cast metal: a deep green-steel face with a faint band of light across it and grain, a thin specular upper-left edge, a dark lower-right edge
        edge = min(1.0, max(.36, size * .03))
        rim = min(2.2, max(1.0, size * .07))
        g = _obb(u + '_f', METAL_FACE)
        mm = '<mask id="%s_m" maskUnits="userSpaceOnUse" x="%s" y="%s" width="%s" height="%s">%s</mask>' % (u, X0, Y0, Wd, Ht, T('#fff'))
        out = '<defs>%s%s%s</defs>' % (g, masks(edge), mm)
        out += T('#3d0d00', ' fill-opacity=".5" stroke="#3d0d00" stroke-opacity=".5" stroke-width="%.2f" stroke-linejoin="round"' % rim, max(1, size * .05), max(1, size * .07))
        out += T('#030d09', ' stroke="#030d09" stroke-width="%.2f" stroke-linejoin="round"' % rim)
        out += T('url(#%s_f)' % u)
        out += R('url(#g_grain)', 'm')
        out += R('#04160f', 'b', ' fill-opacity=".9"') + R(SPEC, 'a', ' fill-opacity=".92"')
        return out
    raise ValueError(style)


CAP = .77        # the height of a capital of Libre Baskerville, in ems


def jade_letters(items):
    """letters of the face, each set on its own and then moved: items = [(character, middle x, middle y of its capital height, height of a capital, tilt in degrees clockwise, width factor, slant in degrees)].
    The jade stack of text(): the cast shadow, the rim, the face and the two inner edges are laid in the WINDOW's directions after the letter is turned, so every letter has the same light whatever
    its tilt. Set from the face, never traced."""
    out = ''
    for ch, cx, cy, hgt, rot, sx, skew in items:
        _tn[0] += 1
        u = 'q%d' % _tn[0]
        size = hgt / CAP
        tr = 'translate(%.2f %.2f) rotate(%s) skewX(%s) scale(%s 1)' % (cx, cy, rot, skew, sx)
        esc = ch.replace('&', '&amp;')
        G = lambda fill, extra='', dx=0, dy=0: ('<g transform="translate(%s %s)"><text transform="%s" x="0" y="%.2f" font-family="LB" font-size="%.2f" text-anchor="middle" fill="%s"%s>%s</text></g>'
                                               % (dx, dy, tr, hgt / 2.0, size, fill, extra, esc))
        edge = min(1.0, max(.36, size * .03))
        rim = min(2.2, max(1.0, size * .07))
        X0, Y0, Wd, Ht = cx - size, cy - size, 2 * size, 2 * size
        d = '<linearGradient id="%s_f" gradientUnits="userSpaceOnUse" x1="0" y1="%.2f" x2="0" y2="%.2f">%s</linearGradient>' % (u, cy - hgt * .56, cy + hgt * .62, ''.join('<stop offset="%s" stop-color="%s"/>' % st for st in METAL_FACE))
        d += '<mask id="%s_m" maskUnits="userSpaceOnUse" x="%s" y="%s" width="%s" height="%s">%s</mask>' % (u, X0, Y0, Wd, Ht, G('#fff'))
        d += '<mask id="%s_a" maskUnits="userSpaceOnUse" x="%s" y="%s" width="%s" height="%s">%s%s</mask>' % (u, X0, Y0, Wd, Ht, G('#fff'), G('#000', '', edge, edge))
        d += '<mask id="%s_b" maskUnits="userSpaceOnUse" x="%s" y="%s" width="%s" height="%s">%s%s</mask>' % (u, X0, Y0, Wd, Ht, G('#fff'), G('#000', '', -edge, -edge))
        R = lambda fill, m, op='': '<rect x="%s" y="%s" width="%s" height="%s" fill="%s"%s mask="url(#%s_%s)"/>' % (X0, Y0, Wd, Ht, fill, op, u, m)
        out += '<defs>%s</defs>' % d
        out += G('#3d0d00', ' fill-opacity=".5" stroke="#3d0d00" stroke-opacity=".5" stroke-width="%.2f" stroke-linejoin="round"' % (rim / sx), max(1, size * .05), max(1, size * .07))
        out += G('#030d09', ' stroke="#030d09" stroke-width="%.2f" stroke-linejoin="round"' % (rim / sx))
        out += R('url(#%s_f)' % u, 'm') + R('url(#g_grain)', 'm') + R('#04160f', 'b', ' fill-opacity=".9"') + R(SPEC, 'a', ' fill-opacity=".92"')
    return out


def playful(s, cx, base, hgt, sp, moves):
    """a line whose letters dance a little, as the original's headings do: `s` centered on cx with the capitals' feet on `base`, capitals `hgt` high, `sp` between letters; moves = one (size factor,
    lift, tilt) for each letter that is not a space (they repeat when the line is longer). -> items for jade_letters"""
    size = hgt / CAP
    adv = [measure(c, size) + sp for c in s]
    x = cx - (sum(adv) - sp) / 2.0
    items = []
    n = 0
    for c, a in zip(s, adv):
        if c != ' ':
            f, lift, tilt = moves[n % len(moves)]
            n += 1
            items.append((c, x + (a - sp) / 2.0, base - lift - hgt * f / 2.0, hgt * f, tilt, 1, 0))
        x += a
    return items


# ---- controls ---------------------------------------------------------------------------------------------------------------------------------------------------
def brush(x, y, w, h, r=0, op=1):
    """the brushing over a metal face"""
    return S.rrect(x, y, w, h, r, 'url(#g_brush)', (' fill-opacity="%s"' % op) if op != 1 else '')


def glint(x, y, w, op=1):
    """a broken highlight: one row"""
    return '<rect x="%s" y="%s" width="%s" height="1" fill="url(#g_glint)"%s/>' % (x, y, w, (' fill-opacity="%s"' % op) if op != 1 else '')


def contact(uid, x, y, w, h, r, op=.45):
    """the tight dark shadow where a part sits on the clay (below and right of it), under the softer cast shadow"""
    return '<g filter="url(#%s_soft)">%s</g>' % (uid, S.rrect(x + .8, y + 1.4, w, h, r, '#140300', ' fill-opacity="%s"' % op))


def button(uid, x, y, fw, fh, lines, state='up', size=11.5, r=3, lead=None, gold=False):
    """a button of the windows: a machined key of brushed green steel. Face fw x fh at (x, y), its cast shadow 2 right and 3 down with a tight contact shadow under it (so the picture is fw + 3 by
    fh + 4). A black rim, a thin lit upper-left edge with a broken glint along the top, a dark face that falls from a sheen under the top edge into shadow, brushed along x, a deep lower-right edge.
    Pressed: sunk into its seat, the words one pixel down and right. states: up, hover, down; `gold`: the chosen one of a pair of switches: pressed in deep and dark, its word lit gold with a glow."""
    _tn[0] += 1
    u = '%s_b%d' % (uid, _tn[0])
    hov = state == 'hover'
    up = [(0, '#3c8770'), (.15, '#2d6c5b'), (.5, '#255b4d'), (1, '#194238')] if hov else [(0, '#367a65'), (.15, '#286253'), (.5, '#215245'), (1, '#163a30')]
    d = S.vgrad(u + '_f', y + 2, y + fh - 2, up) + S.vgrad(u + '_d', y + 2, y + fh - 2, [(0, '#071c15'), (.3, '#0c2f25'), (1, '#144637')])
    d += S.vgrad(u + '_on', y + 2, y + fh - 2, [(0, '#030d09'), (.35, '#06170f'), (1, '#082219')])
    d += S.vgrad(u + '_rim', y, y + fh, [(0, '#0b3a2c'), (.1, '#0b3a2c'), (.105, '#05120c'), (1, '#05120c')])
    d += '<radialGradient id="%s_gl" cx=".5" cy=".55" r=".6"><stop offset="0" stop-color="#f3db37" stop-opacity=".30"/><stop offset="1" stop-color="#f3db37" stop-opacity="0"/></radialGradient>' % u
    b = '<defs>%s</defs>' % d
    if state != 'down':
        b += S.rrect(x + 2, y + 3, fw, fh, r, '#3a0c00', ' fill-opacity=".5"') + contact(uid, x, y, fw, fh, r)
        b += S.raised(u, x, y, fw, fh, r, 'url(#%s_f)' % u, hl='#7cc4a6' if hov else '#58a484', sh='#0a2a20', rim='url(#%s_rim)' % u)
        b += brush(x + 2, y + 2, fw - 4, fh - 4, max(r - 2, 0))
        b += glint(x + r + 1, y + 1, fw - 2 * r - 2, 1 if hov else .8)                                                # the top edge catches the light, unevenly
        sh = (0, 0)
    elif gold:
        b += S.sunken(u, x, y, fw, fh, r, 'url(#%s_on)' % u, wd='#020a07', wl='#3f8a6e')
        b += brush(x + 2, y + 2, fw - 4, fh - 4, max(r - 2, 0), .35)
        b += S.rrect(x + 2, y + 2, fw - 4, fh - 4, max(r - 2, 0), 'url(#%s_gl)' % u)                                 # the lamp: a gold glow under the word
        sh = (1, 1)
    else:
        b += S.sunken(u, x, y, fw, fh, r, 'url(#%s_d)' % u, wd='#020a07', wl='#3f8a6e')
        b += brush(x + 2, y + 2, fw - 4, fh - 4, max(r - 2, 0), .7)
        sh = (1, 1)
    lead = lead or size * .98
    cap = size * .70
    n = len(lines)
    y0 = y + fh / 2.0 - ((n - 1) * lead) / 2.0 + cap / 2.0 - .3
    for i, ln in enumerate(lines):
        if gold:
            b += '<g filter="url(#%s_soft2)" opacity=".7">%s</g>' % (uid, _t(ln, x + fw / 2.0 + sh[0], y0 + i * lead + sh[1], size, 'middle', '#f3db37'))
        else:
            b += _t(ln, x + fw / 2.0 + sh[0], y0 + i * lead + sh[1], size, 'middle', '#04120d', ' stroke="#04120d" stroke-opacity=".3" stroke-width="2" stroke-linejoin="round" fill-opacity=".3"')    # a dark seat under the words, so the streaks do not run through them
        b += text(ln, x + fw / 2.0, y0 + i * lead, size, 'label', 'middle', shift=sh, hover=hov, gold=gold)
    return b


def switch_ring(x, y):
    """the ring that lights a chosen switch (46 x 20 face at (x, y)) while the pointer is on it"""
    return S.rrect(x + .5, y + .5, 45, 19, 2.5, 'none', ' stroke="#a6e0c4" stroke-opacity=".9" stroke-width="1"')


def field(uid, x, y, w, h, wl=4, wt=4, wr=6, wb=6):
    """an edit field: the dark ground (x, y, w, h) where the game writes, in a bezel of brushed green steel of the original's widths (4 left and above, 6 right and below, of which the outer 2
    are its cast shadow): a lit upper-left edge with a broken glint, a deep lower-right one, and the field's own wall inside it. The picture is (w + 10) x (h + 10) from (x - 4, y - 4)."""
    _tn[0] += 1
    u = '%s_e%d' % (uid, _tn[0])
    X, Y, Wd, Ht = x - wl, y - wt, w + wl + wr - 2, h + wt + wb - 2
    b = S.rrect(X + 2, Y + 2, Wd, Ht, 2, '#3a0c00', ' fill-opacity=".5"') + contact(uid, X, Y, Wd, Ht, 2)
    b += '<defs>%s</defs>' % S.vgrad(u + '_bz', Y + 1, Y + Ht - 1, [(0, '#2d6c59'), (.2, '#205243'), (1, '#153a2f')])
    b += S.raised(u, X, Y, Wd, Ht, 2, 'url(#%s_bz)' % u, hl='#4c9778', sh='#0a2a20', rim='#05120c')
    b += brush(X + 2, Y + 2, Wd - 4, Ht - 4, 0)
    b += glint(X + 3, Y + 1, Wd - 6, .8)
    b += '<g shape-rendering="crispEdges">%s</g>' % S.bands(x - 1, y - 1, w + 2, h + 2, 0, 1, '#020a07', '#3f8a6e')
    b += '<rect x="%s" y="%s" width="%s" height="%s" fill="%s"/>' % (x, y, w, h, FIELD)
    return b


def track(uid, x, y, w, h):
    """a slider's track (the original's 235 x 18 strip; its outer 2 pixels below and right are a cast shadow): a rod of brushed green steel lying in a groove of the clay. The groove is deep and
    dark with a lit lip below. The rod: a dark body, a band of light along its upper third whose core is a BROKEN glint, a black underside with a thin reflected line, brushed along its length;
    a collar at each end."""
    _tn[0] += 1
    u = '%s_s%d' % (uid, _tn[0])
    fw, fh = w - 2, h - 3
    r = fh / 2.0
    tube = [(0, '#06140f'), (.12, '#17443a'), (.22, '#377c65'), (.30, '#72bd9f'), (.38, '#2c705c'), (.46, '#1a493c'), (.62, '#0e2a22'), (.82, '#06140f'), (.92, '#2a6b58'), (1, '#0a211a')]
    d = S.vgrad(u + '_f', y + 1, y + fh - 1, tube)
    d += S.vgrad(u + '_k', y, y + fh, [(0, '#06140f'), (.14, '#1f5445'), (.25, '#5aa98b'), (.31, '#e4fff3'), (.38, '#3a8068'), (.5, '#1f5445'), (.8, '#07160f'), (.93, '#1f5445'), (1, '#06140f')])
    b = '<defs>%s</defs>' % d
    b += contact(uid, x, y, fw, fh, r, .5)
    b += S.rrect(x, y + 1, fw + 1, fh + 1, r, '#ffb27a', ' fill-opacity=".30"')                                    # the groove's lit lip, below and right
    b += S.rrect(x, y, fw, fh, r, '#160400')                                                                         # the groove
    b += S.rrect(x + 1, y + 1, fw - 2, fh - 2, r - 1, 'url(#%s_f)' % u)
    b += brush(x + 1, y + 1, fw - 2, fh - 2, r - 1)
    b += glint(x + 8, y + 4, fw - 16)                                                                                # the specular core: one row, broken
    for cx in (x + 1, x + fw - 8):                                                                                   # the collars
        b += S.rrect(cx, y, 7, fh, 2.5, '#030b08') + S.rrect(cx + 1, y + 1, 5, fh - 2, 1.5, 'url(#%s_k)' % u)
    return b


def thumb(uid, x, y, w, h, state='up'):
    """a slider's thumb (the original's 49 x 16): a machined grip of brushed green steel riding on the rod. A bevelled block, a lit upper-left edge, a hard band of light across its upper third,
    brushed along x, seven cut ridges in its middle that each catch the light on one flank, a black lower-right edge and a tight contact shadow. hover: its edges light; held: lower, the ridges gold."""
    _tn[0] += 1
    u = '%s_h%d' % (uid, _tn[0])
    held = state == 'held'
    hov = state == 'hover'
    fw, fh = w - 2, h - 2
    body = [(0, '#2a6853'), (.15, '#448f75'), (.24, '#8fd0b4'), (.29, '#e9fff6' if not held else '#b4e6d1'), (.35, '#52a082'), (.48, '#2a6853'), (.78, '#11342a'), (.92, '#215546'), (1, '#0c261d')]
    d = S.vgrad(u + '_f', y + 1, y + fh - 1, body)
    b = '<defs>%s</defs>' % d
    b += '<g filter="url(#%s_soft)">%s</g>' % (uid, S.rrect(x + 1, y + (2.2 if not held else 1.5), fw, fh, 4, '#000000', ' fill-opacity=".85"'))                   # the contact shadow on the rod: tight and black
    b += S.rrect(x, y, fw, fh, 4, '#030b08') + S.rrect(x + 1, y + 1, fw - 2, fh - 2, 3, 'url(#%s_f)' % u)
    b += brush(x + 1, y + 1, fw - 2, fh - 2, 3)
    b += glint(x + 3, y + 4, fw - 6, .9 if not held else .5)                                                                       # the hard glint of the band of light, broken
    b += '<path d="M%s %sH%sM%s %sV%s" stroke="%s" stroke-opacity="%s" stroke-width="1" stroke-linecap="round" fill="none"/>' % (x + 4, y + 1.5, x + fw - 4, x + 1.5, y + 4, y + fh - 4, SPEC, '1' if hov or held else '.7')
    b += '<path d="M%s %sH%sM%s %sV%s" stroke="#020806" stroke-opacity=".8" stroke-width="1" stroke-linecap="round" fill="none"/>' % (x + 4, y + fh - 1.5, x + fw - 4, x + fw - 1.5, y + 4, y + fh - 4)
    cx = x + (fw + 1) // 2                                                                                                 # the middle ridge: the grip's middle (fw is odd)
    g = '<g shape-rendering="crispEdges">'
    for i in range(-3, 4):                                                                                                 # the ridges: a cut (black), then its lit flank
        rx = cx + i * 3 - 1
        g += '<rect x="%s" y="%s" width="1" height="%s" fill="#020806"/><rect x="%s" y="%s" width="1" height="%s" fill="%s"/>' % (rx, y + 3, fh - 6, rx + 1, y + 3, fh - 6, '#f3db37' if held else '#e4fff3')
    return b + g + '</g>'


def arrow(uid, x, y, w, h):
    """the arrow between a power-up and its ant: the clay's own color, a little darker than the panel"""
    hh = h / 2.0
    s = h * .28
    neck = w * .52
    pts = [(x, y + hh - s), (x + neck, y + hh - s), (x + neck, y), (x + w, y + hh), (x + neck, y + h), (x + neck, y + hh + s), (x, y + hh + s)]
    return S.poly(pts, '#d9521a')


def jade_ant(uid, x, y, s=1.0):
    """the ornament ant of the HUD (its outline: pieces.py) as a piece of the same metal as the letters: dark rim, a green-steel body lit from the upper left, a cast shadow. 30 x 30 at s = 1"""
    import pieces
    legs, feelers = pieces.ant_paths()
    line = '<path d="%s %s"/>' % (legs, feelers)
    body = '<ellipse cx="15" cy="7.4" rx="3.1" ry="3.3"/><ellipse cx="15" cy="14.6" rx="2.7" ry="3.5"/><ellipse cx="15" cy="23" rx="4.1" ry="5.2"/>'
    g = '<g transform="translate(%s %s) scale(%s)">' % (x, y, s)
    g += '<defs><radialGradient id="%s_jb" cx=".33" cy=".26" r=".85"><stop offset="0" stop-color="%s"/><stop offset=".2" stop-color="#4a977a"/><stop offset=".6" stop-color="#286552"/><stop offset="1" stop-color="#11392d"/></radialGradient></defs>' % (uid, SPEC)
    g += '<g transform="translate(1.2 1.6)" fill="#3d0d00" fill-opacity=".5" stroke="#3d0d00" stroke-opacity=".5" stroke-width="3" stroke-linecap="round" stroke-linejoin="round">%s%s</g>' % (line.replace('<path', '<path fill="none"'), body)
    g += '<g fill="#030d09" stroke="#030d09" stroke-width="3.1" stroke-linecap="round" stroke-linejoin="round">%s%s</g>' % (line.replace('<path', '<path fill="none"'), body)
    g += '<g fill="none" stroke="#367f68" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">%s</g>' % line
    g += '<g fill="url(#%s_jb)">%s</g>' % (uid, body)
    return g + '</g>'
