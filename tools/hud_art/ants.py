"""Ornament ants: every ant is its own drawing (heading, head turn, bent abdomen, six legs each with its own swing and reach, from a seed), embossed into the plate.
The relief is built OUTSIDE the ant's rotation (shadow one step down and right, light one step up and left, face lit from the top left), so every ant has the same light whatever its heading
(never rotating the finished ant and its shadow with it). The body dominates (three blobs, as in the original's etchings); the legs are short and thick, not a spider's fan."""
import math, random

LEGS = [  # root, knee, foot for the right side (heading up = -y); the left side is mirrored
    ((1.9, -6.0), (5.6, -8.2), (6.4, -11.8)),
    ((2.3, -4.4), (6.4, -4.6), (7.6, -1.6)),
    ((2.0, -2.8), (6.2, 0.2), (7.0, 5.8)),
]

def _rot(p, a, o=(0, 0)):
    c, s = math.cos(math.radians(a)), math.sin(math.radians(a))
    x, y = p[0] - o[0], p[1] - o[1]
    return (o[0] + x * c - y * s, o[1] + x * s + y * c)

def _f(v): return ('%.2f' % v).rstrip('0').rstrip('.')

def shapes(seed):
    """the ant in its own coordinates (heading up, about 29 long): (body svg, legs path d, feelers path d, blob centres with a radius)"""
    r = random.Random(seed)
    turn = r.uniform(-18, 18); bend = r.uniform(-20, 20)
    neck, waist = (0, -7.6), (0, 0.4)
    head_c = _rot((0, -10.4), turn, neck)
    gas_c = _rot((0, 7.0), bend, waist)
    body = '<ellipse cx="%s" cy="%s" rx="3.9" ry="3.5" transform="rotate(%s %s %s)"/>' % (_f(head_c[0]), _f(head_c[1]), _f(turn), _f(head_c[0]), _f(head_c[1]))
    body += '<ellipse cx="0" cy="-4.2" rx="3.1" ry="4.0"/><circle cx="0" cy="0.4" r="1.6"/>'
    body += '<path d="M0 0.8 C6.9 0.8 7.7 11.4 0 13.5 C-7.7 11.4 -6.9 0.8 0 0.8Z" transform="rotate(%s 0 0.4)"/>' % _f(bend)
    legs = ''
    for side in (1, -1):
        for root, knee, foot in LEGS:
            a = r.uniform(-20, 20); l = r.uniform(.82, 1.12)
            k = _rot((root[0] + (knee[0] - root[0]) * l, root[1] + (knee[1] - root[1]) * l), a, root)
            f = _rot((root[0] + (foot[0] - root[0]) * l, root[1] + (foot[1] - root[1]) * l), a + r.uniform(-12, 12), root)
            legs += 'M%s %s Q%s %s %s %s ' % (_f(side * root[0]), _f(root[1]), _f(side * k[0]), _f(k[1]), _f(side * f[0]), _f(f[1]))
    feel = ''
    for side in (1, -1):
        a = r.uniform(-18, 18)
        p0 = _rot((side * 1.6, -12.6), turn, neck)
        p1 = _rot(_rot((side * 3.8, -15.0), a, (side * 1.6, -12.6)), turn, neck)
        p2 = _rot(_rot((side * 6.2, -14.4), a + r.uniform(-14, 14), (side * 1.6, -12.6)), turn, neck)
        feel += 'M%s %s Q%s %s %s %s ' % (_f(p0[0]), _f(p0[1]), _f(p1[0]), _f(p1[1]), _f(p2[0]), _f(p2[1]))
    return body, legs, feel, [(head_c, 3.5), ((0, -4.2), 3.2), (gas_c, 6.2)]

def ant(uid, cx, cy, heading, s, seed, depth=1.0):
    """an embossed ant with its middle at (cx, cy), heading in degrees clockwise from up, scale s (1 = 29 canvas pixels long)"""
    body, legs, feel, blobs = shapes(seed)
    tr = 'translate(%s %s) rotate(%s) scale(%s)' % (_f(cx), _f(cy), _f(heading), _f(s))
    def layer(col, lw):
        return ('<g transform="%s" fill="%s"><g fill="none" stroke="%s" stroke-linecap="round" stroke-linejoin="round"><path d="%s" stroke-width="%s"/><path d="%s" stroke-width="%s"/></g>%s</g>'
                % (tr, col, col, legs, lw, feel, _f(float(lw) * .72), body))
    R = 18 * s
    g = '<defs><mask id="%s_m" maskUnits="userSpaceOnUse" x="%s" y="%s" width="%s" height="%s">%s</mask>' % (uid, _f(cx - R), _f(cy - R), _f(2 * R), _f(2 * R), layer('#fff', '1.9'))
    g += '<linearGradient id="%s_g" x1="0" y1="0" x2="1" y2="1"><stop offset=".2" stop-color="#4aa985"/><stop offset=".55" stop-color="#2c8463"/><stop offset=".85" stop-color="#1d6a4f"/></linearGradient></defs>' % uid
    g += '<g transform="translate(%s %s)" opacity="%s">%s</g>' % (_f(.9 * depth), _f(.9 * depth), .55, layer('#03170e', '1.9'))          # the cast shadow: down and right
    g += '<g transform="translate(%s %s)" opacity="%s">%s</g>' % (_f(-.55 * depth), _f(-.55 * depth), .7, layer('#a4e6c6', '2.1'))        # the lit edge: up and left
    g += '<rect x="%s" y="%s" width="%s" height="%s" fill="url(#%s_g)" mask="url(#%s_m)"/>' % (_f(cx - R), _f(cy - R), _f(2 * R), _f(2 * R), uid, uid)
    for (bx, by), br in blobs:                                                                                                         # a soft gleam on each blob, toward the light
        px, py = _rot((bx * s, by * s), heading)
        rr = br * s
        g += '<ellipse cx="%s" cy="%s" rx="%s" ry="%s" fill="#d8fff0" fill-opacity=".34" transform="rotate(-45 %s %s)"/>' % (
            _f(cx + px - rr * .3), _f(cy + py - rr * .3), _f(rr * .46), _f(rr * .26), _f(cx + px - rr * .3), _f(cy + py - rr * .3))
    return g
