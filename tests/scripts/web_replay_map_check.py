#!/usr/bin/env python3
"""A replay that carries its map (the chunk tmap, docs/REPLAYS.md "The file") in a REAL browser with the REAL game (opt-in; see tests/scripts/test_web_replay_map.sh).

The recording is the match that ANTS_CAMERA_REPLAY names (a match on one of the six original maps) made over: its head names a map that the site does not have ("Draft Map ~test-0badf00d.lvl"), and
the file carries the bytes of the original map, whose hash the head holds. The check answers the page's `fetch('/replays/<file>')` itself, as web_camera_check.py does, and writes down every
other address that the page asks for. What it checks, in a real page:

  * the file that carries its map is played: the game starts, the clock runs, no card is up, and the page asked the site for no map at all (the map came out of the file);
  * the same file without the chunk takes the old way: the page asks the site for the map by its name (which the check answers 404) and the card says that the map is not on the server;
  * a file whose chunk has one flipped byte is not believed: the page takes the old way, asks for the map by its name, and the card says so; the game is never given the damaged bytes.

Exit status 0: every check passed; 1: a check failed, or the browser or the page broke down during the check; 3: the check could not be made because the environment is not there (no browser, no page at
the address, no recording); 2 is the status of a bad command line.
"""
import argparse
import base64
import json
import os
import struct
import sys
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import web_aspect_check as aspect                                            # noqa: E402
from web_aspect_check import Browser, NotReachable, Tab                      # noqa: E402
from web_hidden_check import find_browser                                    # noqa: E402
from web_touch_check import wait_for                                         # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
NAME = "ants-DraftMap-20261011-120000Z.antsrep"
DRAFT = "Draft Map ~test-0badf00d.lvl"
MAGIC = b"\x89ARPL\r\n\x1a"

# What runs in the page before its own scripts: the page's fetch of the recording is answered here, a fetch of a map is written down and answered 404, every other fetch is the browser's
HOOK = r"""
(function () {
  var bytes = Uint8Array.from(atob(%s), function (c) { return c.charCodeAt(0); });
  var real = window.fetch;
  window.__maps = [];
  window.fetch = function (input, init) {
    var url = typeof input === 'string' ? input : input.url;
    if (url.indexOf('/replays/%s') !== -1) return Promise.resolve(new Response(bytes, { status: 200, headers: { 'content-type': 'application/octet-stream' } }));
    if (url.indexOf('/maps/files/') !== -1) { window.__maps.push(decodeURIComponent(url)); return Promise.resolve(new Response('', { status: 404 })); }
    return real.apply(this, arguments);
  };
})();
"""

STATE = r"""
JSON.stringify({ playing: typeof Module !== 'undefined' && Module._ants_replay_get ? Module._ants_replay_get(0) : -1, clock: document.getElementById('t-now') ? document.getElementById('t-now').textContent : '',
                 card: document.getElementById('rm-body') ? document.getElementById('rm-body').textContent : '', maps: window.__maps || [] })
"""


def fnv1a64(data):
    h = 0xCBF29CE484222325
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def chunks_of(data):
    out, pos = [], len(MAGIC)
    while pos + 12 <= len(data):
        length = struct.unpack_from("<I", data, pos + 4)[0]
        out.append((data[pos:pos + 4].decode(), data[pos + 8:pos + 8 + length]))
        pos += 12 + length
    return out


def chunk(tag, payload):
    raw = tag.encode()
    return raw + struct.pack("<I", len(payload)) + payload + struct.pack("<I", zlib.crc32(raw + payload) & 0xFFFFFFFF)


def varint(n):
    out = bytearray()
    while n >= 0x80:
        out.append((n & 0x7F) | 0x80)
        n >>= 7
    out.append(n)
    return bytes(out)


def read_varint(data, pos):
    value, shift = 0, 0
    while True:
        b = data[pos]
        pos += 1
        value |= (b & 0x7F) << shift
        if not b & 0x80:
            return value, pos
        shift += 7


def head_fields(payload):
    pos, fields = 4, []
    while pos < len(payload):
        ident, pos = read_varint(payload, pos)
        length, pos = read_varint(payload, pos)
        fields.append((ident, payload[pos:pos + length]))
        pos += length
    return payload[:4], fields


def made_over(data, map_bytes, with_map, flip=False):
    """The recording with the head's map renamed to DRAFT, and (with_map) the chunk tmap right after HEAD (a byte of it flipped when asked)."""
    chunks = chunks_of(data)
    prefix, fields = head_fields(chunks[0][1])
    hash_field = [f for i, f in fields if i == 4][0]
    if struct.unpack("<Q", hash_field)[0] != fnv1a64(map_bytes):
        raise ValueError("the map is not the file that the recording's head names")
    payload = prefix + b"".join(varint(i) + varint(len(f)) + f for i, f in [(i, DRAFT.encode() if i == 3 else f) for i, f in fields])
    out = MAGIC + chunk("HEAD", payload)
    if with_map:
        mine = bytearray(map_bytes)
        if flip:
            mine[len(mine) // 2] ^= 1
        out += chunk("tmap", bytes(mine))
    for tag, body in chunks[1:]:
        out += chunk(tag, body)
    return out


def state(tab):
    return json.loads(tab.ev(STATE))


def open_page(tab, web, data):
    tab.emulate(1440, 900, 1)
    tab.call("Page.addScriptToEvaluateOnNewDocument", {"source": HOOK % (json.dumps(base64.b64encode(data).decode()), NAME)})
    tab.open((web if web.endswith("/") else web + "/") + "play.html?replay=" + NAME + "&aspect=16:9", settle=0.5)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--web", required=True, help="the web page of the image, e.g. http://127.0.0.1:19980/")
    ap.add_argument("--replay", default=os.environ.get("ANTS_CAMERA_REPLAY", ""), help="a recording of a match on one of the original maps (default: ANTS_CAMERA_REPLAY)")
    ap.add_argument("--map", default="", help="that map's file (default: the original map that the recording's head names)")
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--ready-timeout", type=float, default=180.0, metavar="SECONDS", help="how long a page may take to get its game ready (default 180)")
    args = ap.parse_args()
    aspect.READY_TIMEOUT = args.ready_timeout
    if not args.replay or not os.path.isfile(args.replay):
        print("  SKIP: no recording to watch: ANTS_BOT_MATCH_OUT=/tmp/match.antsrep ANTS_TEST_FILTER=RA8.8 ./build/tests/test_app/test_replay_app writes one; give it as ANTS_CAMERA_REPLAY")
        return 3
    with open(args.replay, "rb") as f:
        data = f.read()
    _, fields = head_fields(chunks_of(data)[0][1])
    original = [f for i, f in fields if i == 3][0].decode()
    map_path = args.map or os.path.join(REPO, "Original-Ants", "Maps", original)
    if not os.path.isfile(map_path):
        print("  SKIP: the recording's map %s is not at %s (give it with --map)" % (original, map_path))
        return 3
    with open(map_path, "rb") as f:
        map_bytes = f.read()
    path = find_browser(args.browser)
    if not path:
        print("  SKIP: no Chromium-based browser found (give one with --browser or CHROME)")
        return 3
    failures, count = [], [0]

    def check(ok, what):
        count[0] += 1
        print("  %s: %s" % ("ok  " if ok else "FAIL", what))
        if not ok:
            failures.append(what)

    browser = None
    try:
        browser = Browser(path)
        for label, make in (("carries its map", lambda: made_over(data, map_bytes, True)), ("has no map chunk", lambda: made_over(data, map_bytes, False)),
                            ("carries a damaged map", lambda: made_over(data, map_bytes, True, flip=True))):
            tab = Tab(browser)
            try:
                print("[web replay map] a recording that %s" % label)
                open_page(tab, args.web, make())
                if label == "carries its map":
                    wait_for(lambda: state(tab)["playing"] == 2, 120, 0.25)
                    time.sleep(2.0)
                    s = state(tab)
                    check(s["playing"] == 2, "the game plays the file (the replay state is %s)" % s["playing"])
                    check(s["clock"] not in ("", "0:00", "0:00.0") and not s["card"].strip(), "the clock runs (%r) and no card is up (%r)" % (s["clock"], s["card"][:80]))
                    check(s["maps"] == [], "the page asked the site for no map (asked for %s)" % s["maps"])
                else:
                    wait_for(lambda: state(tab)["card"].strip() != "", 120, 0.25)
                    s = state(tab)
                    check(s["maps"] == [DRAFT], "the page asked the site for the map by its name (asked for %s)" % s["maps"])
                    check("not on the server" in s["card"] and s["playing"] != 2, "the card says that the map is not on the server, and nothing plays (%r)" % s["card"][:100])
            finally:
                tab.close()
    except NotReachable as e:
        if not count[0]:
            print("  SKIP: %s" % e)
            return 3
        print("  FAIL: %s" % e)
        return 1
    except (RuntimeError, TimeoutError, ConnectionError, OSError, ValueError) as e:
        print("  FAIL: the browser or the page broke down during the check: %r" % (e,))
        return 1
    finally:
        if browser is not None:
            browser.close()
    print("[web replay map] %d checks, %d failed" % (count[0], len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
