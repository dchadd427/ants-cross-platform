"""One bevel system for every piece. Everything is on whole canvas pixels; every plate is built as nested exact insets (no half-overlapping rectangles).

  raised  : from the outside in:  rim (1 px, dark, all sides)  |  bevel (1 px: lit on the top and left, shaded at the bottom and right)  |  face
  sunken  : from the outside in:  wall (1 px: dark on the top and left, lit at the bottom and right)  |  rim (1 px, dark)  |  field
  ridge   : the frame's own edge bands (the original's columns and rows, one pixel each) are used as they are, so that every piece meets its neighbors on the same pixels

The light comes from the top left. Colors are the original's family (deep greens, the mint highlight)."""
import json, os
from common import P

HERE = os.path.dirname(os.path.abspath(__file__)) + '/'
GLYPHS = json.load(open(HERE + 'glyphs.json'))

RIM = '#06150e'          # black-green where the original has black (buttons); there are no other drawn outlines
HL = '#7fc7a6'          # raised: the lit bevel (top, left)
SH = '#124c37'          # raised: the shaded bevel (bottom, right)
WALL_D = '#0b3022'      # sunken: the dark wall (top, left)
WALL_L = '#8fe6c2'      # sunken: the lit wall (bottom, right)
INK = '#e9fbf3'         # lettering
INK_SH = '#062014'

def rgb(t): return '#%02x%02x%02x' % t

def poly(pts, fill, extra=''):
    return '<polygon points="%s" fill="%s"%s/>' % (' '.join('%s,%s' % p for p in pts), fill, extra)

def bands(x, y, w, h, i, t, tl, br):
    """four mitred bands of thickness t at inset i of the rectangle (x, y, w, h): `tl` for the top and left, `br` for the bottom and right (exact 45 degree joins)"""
    x0, y0, x1, y1 = x + i, y + i, x + w - i, y + h - i
    top = [(x0, y0), (x1, y0), (x1 - t, y0 + t), (x0 + t, y0 + t)]
    left = [(x0, y0), (x0 + t, y0 + t), (x0 + t, y1 - t), (x0, y1)]
    bot = [(x0, y1), (x0 + t, y1 - t), (x1 - t, y1 - t), (x1, y1)]
    right = [(x1, y0), (x1, y1), (x1 - t, y1 - t), (x1 - t, y0 + t)]
    return poly(top, tl) + poly(left, tl) + poly(bot, br) + poly(right, br)

def rrect(x, y, w, h, r, fill, extra=''):
    return '<rect x="%s" y="%s" width="%s" height="%s"%s fill="%s"%s/>' % (x, y, w, h, (' rx="%s"' % r) if r else '', fill, extra)

def raised(uid, x, y, w, h, r, face, hl=HL, sh=SH, rim=RIM):
    """rim, bevel, face as nested exact shapes. `face` is a fill (color or url(#gradient))."""
    s = rrect(x, y, w, h, r, rim)
    s += '<clipPath id="%s_c"><rect x="%s" y="%s" width="%s" height="%s"%s/></clipPath>' % (uid, x + 1, y + 1, w - 2, h - 2, (' rx="%s"' % max(r - 1, 0)) if r > 1 else '')
    s += '<g clip-path="url(#%s_c)">%s</g>' % (uid, bands(x, y, w, h, 1, 1, hl, sh))
    s += rrect(x + 2, y + 2, w - 4, h - 4, max(r - 2, 0), face)
    return s

def sunken(uid, x, y, w, h, r, field, wd=WALL_D, wl=WALL_L, rim=RIM):
    """wall, rim, field as nested exact shapes"""
    s = '<clipPath id="%s_c"><rect x="%s" y="%s" width="%s" height="%s"%s/></clipPath>' % (uid, x, y, w, h, (' rx="%s"' % r) if r else '')
    s += '<g clip-path="url(#%s_c)">%s</g>' % (uid, bands(x, y, w, h, 0, 1, wd, wl))
    s += rrect(x + 1, y + 1, w - 2, h - 2, max(r - 1, 0), rim)
    s += rrect(x + 2, y + 2, w - 4, h - 4, max(r - 2, 0), field)
    return s

def vgrad(uid, y0, y1, stops, x=None):
    """a vertical gradient in sprite coordinates (rows y0 .. y1)"""
    st = ''.join('<stop offset="%s" stop-color="%s"/>' % (o, c) for o, c in stops)
    return '<linearGradient id="%s" gradientUnits="userSpaceOnUse" x1="0" y1="%s" x2="0" y2="%s">%s</linearGradient>' % (uid, y0, y1, st)

def hgrad(uid, x0, x1, stops):
    st = ''.join('<stop offset="%s" stop-color="%s"/>' % (o, c) for o, c in stops)
    return '<linearGradient id="%s" gradientUnits="userSpaceOnUse" x1="%s" y1="0" x2="%s" y2="0">%s</linearGradient>' % (uid, x0, x1, st)

def glyph(name, x, y, fill=None, shadow=INK_SH, uid='g', sh=1.0, thick=0.0, fill_op=1):
    """the traced letters of the original (sprite pixels), placed at the sprite's own place plus (x, y); a clean shadow one pixel down and right"""
    g = GLYPHS[name]
    ox, oy = x + g['x'], y + g['y']
    f = fill or 'url(#%s_f)' % uid
    s = '<path fill-rule="evenodd" d="%s" fill="%s" fill-opacity=".85" transform="translate(%s %s)"/>' % (g['d'], shadow, ox + sh, oy + sh)
    s += '<path fill-rule="evenodd" d="%s" fill="%s" transform="translate(%s %s)"%s/>' % (g['d'], f, ox, oy, (' stroke="%s" stroke-width="%s" stroke-linejoin="round"' % (f, thick)) if thick else '')
    return s

def ink_defs(uid, y0, y1):
    return vgrad(uid + '_f', y0, y1, [(0, '#ffffff'), (.55, '#effff9'), (1, '#c4f0e2')])


def walls(field, widths, cols, uid=None):
    """the frame around a field (fx, fy, fw, fh), widths = (left, top, right, bottom) in whole pixels, cols = {'L','T','R','B': [outer .. inner]} one color per pixel.
    A sunken window as the original draws it: the dark walls (left, top) own the corners they share with the lit ones, the lit walls (right, bottom) start inside, so the left side stays dark and quiet
    all the way down (no bright line toward a name that stands next to the window)."""
    fx, fy, fw, fh = field
    wl, wt, wr, wb = widths
    def rect(level):
        return (fx - max(wl - level, 0), fy - max(wt - level, 0), fx + fw + max(wr - level, 0), fy + fh + max(wb - level, 0))
    def r(x0, y0, x1, y1, col): return '<rect x="%s" y="%s" width="%s" height="%s" fill="%s"/>' % (x0, y0, x1 - x0, y1 - y0, col) if x1 > x0 and y1 > y0 else ''
    lit = dark = ''
    for j in range(max(widths)):
        ox0, oy0, ox1, oy1 = rect(j); ix0, iy0, ix1, iy1 = rect(j + 1)
        col = lambda side: cols[side][min(j, len(cols[side]) - 1)]
        if wb > j: lit += r(ix0, iy1, ox1, oy1, col('B'))
        if wr > j: lit += r(ix1, iy0, ox1, oy1, col('R'))
        if wl > j: dark += r(ox0, oy0, ix0, oy1, col('L'))
        if wt > j: dark += r(ox0, oy0, ox1, iy0, col('T'))
    return '<g shape-rendering="crispEdges">%s%s</g>' % (lit, dark)
