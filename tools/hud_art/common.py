"""Shared helpers of the HUD art generator (README.md).

Everything is drawn as SVG in CANVAS pixels (the 960 x 540 layout of the match screen) and rendered by headless Chrome at a device scale of 1, 2 or 3, so one drawing gives the 1x, 2x and 3x sprite at the
SAME footprint. Needs Google Chrome (or Chromium), Pillow and numpy. Nothing here names a place on any machine: the repository's root is found from this file, the work folder and the browser come from
`configure()` (the command line of make_hud_art.py) or from the environment (ANTS_HUD_ART_WORK, ANTS_CHROME).
"""
import os
import shutil
import subprocess
import tempfile

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__)) + '/'
ROOT = os.path.abspath(os.path.join(HERE, '..', '..')) + '/'          # the repository: the decoded sprites (asset_catalog/) and the animation table (docs/) are read from it

_work = None
_chrome = None


def configure(work=None, chrome=None):
    """work: the folder for the scratch files (made if it is not there); chrome: the browser's program (default: ANTS_CHROME, else the usual places)"""
    global _work, _chrome
    _work = work
    _chrome = chrome


def scratch():
    """the work folder with a trailing slash (made on first use)"""
    global _work
    if _work is None:
        _work = os.environ.get('ANTS_HUD_ART_WORK') or os.path.join(tempfile.gettempdir(), 'ants_hud_art')
    path = os.path.abspath(_work) + '/'
    os.makedirs(path, exist_ok=True)
    return path


def chrome():
    """the browser that renders the SVG pieces"""
    candidates = [_chrome, os.environ.get('ANTS_CHROME'), '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', '/Applications/Chromium.app/Contents/MacOS/Chromium']
    candidates += [shutil.which(n) for n in ('google-chrome', 'google-chrome-stable', 'chromium', 'chromium-browser', 'chrome')]
    for c in candidates:
        if c and os.path.isfile(c) and os.access(c, os.X_OK):
            return c
    raise SystemExit('no Chrome or Chromium found: pass --chrome PROGRAM or set ANTS_CHROME')


# --- the original's palette (read from the frame's own sprites) ---------------------------------------------------------------------------------------------
P = dict(
    black='#000000', d0='#133323', d1='#173b27', d2='#1b432b', d3='#23472f', d4='#132b1b', d5='#0b1307',
    g0='#13634b', g1='#17533f', g2='#13734b', g3='#17774f', g4='#1f7f57', g5='#27875f', g6='#338f67', g7='#3b976f',
    f1='#2b6b4f', f2='#2b6b5f', f3='#2b6357', f4='#2f6b4b', f5='#2b5f43', f6='#2b573f',
    hi='#73bf9b', mint='#bfefe3', white='#fbfbff',
    gold0='#f3db37', gold1='#d3b700', gold2='#b79b00', gold3='#977700', gold4='#775707', gold5='#473f07',
)


def svg(w, h, body, defs='', extra=''):
    """one sprite: w x h canvas pixels"""
    return ('<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="%s" height="%s" viewBox="0 0 %s %s" %s>'
            '<defs>%s</defs>%s</svg>' % (w, h, w, h, extra, defs, body))


def render_sheet(items, W, H, k, tag='sheet', bg='transparent', css=''):
    """items: [(x, y, svg_string)] placed at canvas pixel (x, y) of a W x H canvas-pixel sheet; returns the W*k x H*k picture (RGBA)."""
    html = ['<!doctype html><html><head><meta charset="utf-8"><style>html,body{margin:0;padding:0;background:%s;overflow:hidden}svg{position:absolute;display:block;overflow:hidden}%s</style></head><body>' % (bg, css)]
    for x, y, s in items:
        html.append('<div style="position:absolute;left:%spx;top:%spx">%s</div>' % (x, y, s))
    html.append('</body></html>')
    path = scratch() + 'sheet_%s_k%d.html' % (tag, k)
    out = scratch() + 'sheet_%s_k%d.png' % (tag, k)
    with open(path, 'w') as f:
        f.write('\n'.join(html))
    if os.path.exists(out):
        os.remove(out)
    cmd = [chrome(), '--headless=new', '--disable-gpu', '--hide-scrollbars', '--default-background-color=00000000' if bg == 'transparent' else '--default-background-color=ffffffff',
           '--force-device-scale-factor=%d' % k, '--window-size=%d,%d' % (W, H), '--virtual-time-budget=2000', '--screenshot=' + out, 'file://' + path]
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120)
    if not os.path.exists(out):
        raise SystemExit('the browser made no picture: %s' % ' '.join(cmd[:1]))
    im = Image.open(out).convert('RGBA')
    if im.size != (W * k, H * k):
        im = im.crop((0, 0, W * k, H * k))
    return im
