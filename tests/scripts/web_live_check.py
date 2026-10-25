#!/usr/bin/env python3
"""Watching a match that is being played, in a REAL browser (opt-in; see tests/scripts/test_web_live.sh and docs/REPLAYS.md "Watching a replay").

No Docker image, no game server, no game program: the check serves the pages of web/ itself, with a FAKE DOOR (GET /live, GET /live/<id>, GET /replays, GET /replays/<file>: the shapes of
docs/SERVER.md "Replays") and a stand-in for the game (tests/scripts/web_live_stub.js: Module._ants_replay_get / _ants_replay_do as src/ants_app/application.cpp has them). What it shows is the
page's own part: the glue that fetches the growing file again and again and tells the game, the tag and the notes, the cards, the live page. It does not show that the real game follows a file (the
C++ tests do). The DevTools protocol is spoken with the client of web_hidden_check.py (standard library only); the browser has its own profile and port, and nothing of yours is touched.
Parts (--only): list (the "Live now" page, web/live.html: the matches, hostile names as text, a match that comes later, the words when the door is not there), watch (a match at its present: the tag, Pause and
"Jump to live", the file that grows, the feed that stops and comes back), ends (a match that ends and was kept, one that ends and was not kept, a kept replay that is slow to appear), cards
(a link to a match that is over, to one that is not live, an id that is not of the shape), replay (a plain replay is as it was: no live marks, no polling), rewind (Back 30 seconds: where a press seeks, play / pause and speed kept, not on a live match, the bar at 390 px), picture (a spectator's Picture button: the four shapes, the bar as wide as the picture, Esc and a click elsewhere, a phone's place for the button, fullscreen), bar (the REPLAY tag lies outside the picture
in a phone's and a computer's window, and a phone's fullscreen bar: a finger on the picture never calls it, a tap or a swipe up from the bottom edge does, a swipe down or its tab hides it, the first time a
note says how to bring it back; a mouse works as ever).

Exit status 0: every check passed; 1: a check failed; 3: the check could not be made (no browser); 2 is the status of a bad command line.
"""
import argparse
import http.server
import json
import os
import re
import socketserver
import sys
import threading
import time
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from web_aspect_check import Browser, NotReachable, Tab                    # noqa: E402
from web_hidden_check import find_browser                                    # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
WEB = os.path.join(HERE, "..", "..", "web")
FILLED = {"@@SITE_TITLE@@": "", "@@SITE_FOOTER@@": "", "@@GAME_VERSION@@": "v0.0.0", "@@BUILD_ID@@": "20260101-0000", "@@BUILD_DATE@@": "2026-01-01 00:00 PT", "@@BUILD_TIMESTAMP@@": "0", "@@DATA_SIZE@@": "2000"}
ID = "TREASURE-20261009-082104Z"
OTHER = "ISLANDS-20261009-082904Z"
KEPT = "ants-TREASURE-20261009-083210Z.antsrep"
HOSTILE = "Green (<img src=x onerror=window.__pwn=1>)"
PLAYERS = ["Green (Mika)", "Red (Bot (Medium))", "Blue (Bot (Medium))", "Black (Bot (Medium))"]
TURNS_END = 20060


BAR_WINDOWS = [(320, 844, True), (360, 844, True), (390, 844, True), (412, 844, True), (420, 844, True), (430, 844, True), (470, 844, True), (520, 844, True), (640, 900, False), (667, 375, True), (700, 900, False), (701, 900, False), (703, 900, False),
               (710, 1024, True), (740, 1024, True), (768, 1024, True), (800, 1024, True), (844, 390, True), (850, 1024, True), (1024, 768, False), (1440, 900, False)]
MAIN_BAR = {                                                       # what the bar of v0.12.24 (no Back 30 seconds) measured at each size: the timeline's width and its faults (a window below about 770 px was narrow for it already)
    "320x844 page a": (280, ('lap',)),
    "320x844 page c": (280, ('lap',)),
    "320x844 full a": (271, ('lap',)),
    "320x844 full c": (271, ('lap',)),
    "360x844 page a": (320, ()),
    "360x844 page c": (320, ()),
    "360x844 full a": (300, ('lap',)),
    "360x844 full c": (300, ('lap',)),
    "390x844 page a": (350, ()),
    "390x844 page c": (350, ()),
    "390x844 full a": (323, ('lap',)),
    "390x844 full c": (323, ('lap',)),
    "412x844 page a": (372, ()),
    "412x844 page c": (372, ()),
    "412x844 full a": (346, ('lap',)),
    "412x844 full c": (346, ('lap',)),
    "420x844 page a": (380, ()),
    "420x844 page c": (380, ()),
    "420x844 full a": (353, ('lap',)),
    "420x844 full c": (353, ('lap',)),
    "430x844 page a": (390, ()),
    "430x844 page c": (390, ()),
    "430x844 full a": (361, ('lap',)),
    "430x844 full c": (361, ('lap',)),
    "470x844 page a": (430, ()),
    "470x844 page c": (430, ()),
    "470x844 full a": (398, ()),
    "470x844 full c": (398, ()),
    "520x844 page a": (480, ()),
    "520x844 page c": (480, ()),
    "520x844 full a": (451, ()),
    "520x844 full c": (451, ()),
    "640x900 page a": (585, ()),
    "640x900 page c": (585, ()),
    "640x900 full a": (564, ()),
    "640x900 full c": (564, ()),
    "667x375 page a": (627, ()),
    "667x375 page c": (627, ()),
    "667x375 full a": (611, ()),
    "667x375 full c": (611, ()),
    "700x900 page a": (645, ()),
    "700x900 page c": (645, ()),
    "700x900 full a": (609, ()),
    "700x900 full c": (609, ()),
    "701x900 page a": (0, ()),
"701x900 page c": (0, ('clip',)),
"701x900 full a": (0, ('off',)),
"701x900 full c": (0, ('clip', 'off')),
"703x900 page a": (0, ()),
"703x900 page c": (0, ('clip',)),
"703x900 full a": (0, ('off',)),
"703x900 full c": (0, ('clip', 'off')),
"710x1024 page a": (0, ()),
    "710x1024 page c": (0, ('clip',)),
    "710x1024 full a": (0, ('off',)),
    "710x1024 full c": (0, ('clip', 'off')),
    "740x1024 page a": (0, ()),
    "740x1024 page c": (0, ('clip',)),
    "740x1024 full a": (0, ('off',)),
    "740x1024 full c": (0, ('clip', 'off')),
    "768x1024 page a": (8, ()),
    "768x1024 page c": (7, ()),
    "768x1024 full a": (0, ()),
    "768x1024 full c": (0, ('clip',)),
    "800x1024 page a": (40, ()),
    "800x1024 page c": (39, ()),
    "800x1024 full a": (0, ()),
    "800x1024 full c": (0, ('clip',)),
    "844x390 page a": (131, ()),
    "844x390 page c": (130, ()),
    "844x390 full a": (634, ()),
    "844x390 full c": (634, ()),
    "850x1024 page a": (90, ()),
    "850x1024 page c": (89, ()),
    "850x1024 full a": (29, ()),
    "850x1024 full c": (28, ()),
    "1024x768 page a": (132, ()),
    "1024x768 page c": (131, ()),
    "1024x768 full a": (194, ()),
    "1024x768 full c": (193, ()),
    "1440x900 page a": (420, ()),
    "1440x900 page c": (419, ()),
    "1440x900 full a": (412, ()),
    "1440x900 full c": (411, ()),
}

# a match that is being played: its bar was measured before the button and must stay exactly so (every box relative to Play, the gaps, the timeline and the clock, the page's width)
LIVE_BAR = {"320x844 page":{"boxes":[["b-play",0,0,64,50],["clock",76,12,76,39],["livebox",88,1,174,50],["b-live",88,1,174,50],["b-cam",0,98,280,142],["picwrap",186,1,268,50],["speed",0,54,280,94]],"gap":"12px 4px","tl":280,"clockw":0,"doc":320,"vw":320},"320x844 full":{"boxes":[["b-play",0,0,64,50],["clock",76,11,76,39],["livebox",88,-7,163,58],["b-live",88,-7,163,58],["b-cam",175,7,219,43],["picwrap",231,1,303,50],["b-fsx",315,4,359,46],["speed",0,62,359,102]],"gap":"12px 4px","tl":359,"clockw":0,"doc":320,"vw":320},"360x844 page":{"boxes":[["b-play",0,0,64,50],["clock",76,12,81,39],["livebox",93,4,214,46],["b-live",93,4,214,46],["b-cam",0,98,320,142],["picwrap",226,7,308,43],["speed",0,54,320,94]],"gap":"12px 4px","tl":320,"clockw":5,"doc":360,"vw":360},"360x844 full":{"boxes":[["b-play",0,0,64,50],["clock",76,11,76,39],["livebox",88,-7,164,58],["b-live",88,-7,164,58],["b-cam",176,7,220,43],["picwrap",232,1,303,50],["b-fsx",315,4,359,46],["speed",0,62,359,102]],"gap":"12px 4px","tl":359,"clockw":0,"doc":360,"vw":360},"390x844 page":{"boxes":[["b-play",0,0,64,50],["clock",76,11,103,39],["livebox",115,4,240,46],["b-live",115,4,240,46],["b-cam",0,98,350,142],["picwrap",252,7,338,43],["speed",0,54,350,94]],"gap":"12px 4px","tl":350,"clockw":27,"doc":390,"vw":390},"390x844 full":{"boxes":[["b-play",0,0,64,50],["clock",76,11,76,39],["livebox",88,-7,168,58],["b-live",88,-7,168,58],["b-cam",180,7,224,43],["picwrap",236,1,311,50],["b-fsx",323,4,367,46],["speed",0,62,367,102]],"gap":"12px 4px","tl":367,"clockw":0,"doc":390,"vw":390},"412x844 page":{"boxes":[["b-play",0,0,64,50],["clock",76,11,125,38],["livebox",137,4,262,46],["b-live",137,4,262,46],["b-cam",0,98,372,142],["picwrap",274,7,360,43],["speed",0,54,372,94]],"gap":"12px 4px","tl":372,"clockw":49,"doc":412,"vw":412},"412x844 full":{"boxes":[["b-play",0,0,64,50],["clock",76,11,76,39],["livebox",88,-7,168,58],["b-live",88,-7,168,58],["b-cam",180,7,224,43],["picwrap",236,1,312,50],["b-fsx",324,4,368,46],["speed",0,62,368,102]],"gap":"12px 4px","tl":367,"clockw":0,"doc":412,"vw":412},"420x844 page":{"boxes":[["b-play",0,0,64,50],["clock",76,11,133,39],["livebox",145,4,270,46],["b-live",145,4,270,46],["b-cam",0,98,380,142],["picwrap",282,7,368,43],["speed",0,54,380,94]],"gap":"12px 4px","tl":380,"clockw":57,"doc":420,"vw":420},"420x844 full":{"boxes":[["b-play",0,0,64,50],["clock",76,11,76,39],["livebox",88,-7,168,58],["b-live",88,-7,168,58],["b-cam",180,7,224,43],["picwrap",236,1,312,50],["b-fsx",324,4,368,46],["speed",0,62,368,102]],"gap":"12px 4px","tl":367,"clockw":0,"doc":420,"vw":420},"430x844 page":{"boxes":[["b-play",0,0,64,50],["clock",76,11,143,38],["livebox",155,4,280,46],["b-live",155,4,280,46],["b-cam",0,98,390,142],["picwrap",292,7,378,43],["speed",0,54,390,94]],"gap":"12px 4px","tl":390,"clockw":67,"doc":430,"vw":430},"430x844 full":{"boxes":[["b-play",0,0,64,50],["clock",76,11,76,39],["livebox",88,-8,168,57],["b-live",88,-8,168,57],["b-cam",180,7,224,43],["picwrap",236,0,311,49],["b-fsx",323,4,367,46],["speed",0,61,367,101]],"gap":"12px 4px","tl":367,"clockw":0,"doc":430,"vw":430},"520x844 page":{"boxes":[["b-play",0,0,64,50],["clock",76,12,225,39],["livebox",237,4,366,46],["b-live",237,4,366,46],["b-cam",0,98,480,142],["picwrap",378,7,468,43],["speed",0,54,480,94]],"gap":"12px 4px","tl":480,"clockw":149,"doc":520,"vw":520},"520x844 full":{"boxes":[["b-play",0,0,64,50],["clock",76,12,93,39],["livebox",105,4,234,46],["b-live",105,4,234,46],["b-cam",246,7,290,43],["picwrap",302,7,392,43],["b-fsx",404,4,450,46],["speed",0,54,450,94]],"gap":"12px 4px","tl":451,"clockw":18,"doc":520,"vw":520},"700x900 page":{"boxes":[["b-play",0,0,64,50],["clock",76,11,390,39],["livebox",402,4,531,46],["b-live",402,4,531,46],["b-cam",0,98,645,142],["picwrap",543,7,633,43],["speed",0,54,645,94]],"gap":"12px 4px","tl":645,"clockw":314,"doc":685,"vw":700},"700x900 full":{"boxes":[["b-play",0,0,64,50],["clock",76,11,251,39],["livebox",263,4,392,46],["b-live",263,4,392,46],["b-cam",404,7,448,43],["picwrap",460,7,550,43],["b-fsx",562,4,608,46],["speed",0,54,608,94]],"gap":"12px 4px","tl":609,"clockw":176,"doc":700,"vw":700},"701x900 page":{"boxes":[["b-play",0,0,64,50],["clock",76,10,363,39],["livebox",375,6,508,44],["b-live",375,6,508,44],["b-cam",0,94,626,138],["picwrap",520,7,614,43],["speed",0,54,626,90]],"gap":"12px 4px","tl":626,"clockw":287,"doc":686,"vw":701},"701x900 full":{"boxes":[["b-play",0,0,64,50],["clock",76,11,275,40],["livebox",287,6,420,44],["b-live",287,6,420,44],["b-cam",432,7,476,43],["picwrap",488,7,582,43],["b-fsx",594,6,640,44],["speed",0,54,640,90]],"gap":"12px 4px","tl":641,"clockw":200,"doc":701,"vw":701},"703x900 page":{"boxes":[["b-play",0,0,64,50],["clock",76,10,365,39],["livebox",377,6,510,44],["b-live",377,6,510,44],["b-cam",0,94,628,138],["picwrap",522,7,616,43],["speed",0,54,628,90]],"gap":"12px 4px","tl":628,"clockw":289,"doc":688,"vw":703},"703x900 full":{"boxes":[["b-play",0,0,64,50],["clock",76,11,275,40],["livebox",287,6,420,44],["b-live",287,6,420,44],["b-cam",432,7,476,43],["picwrap",488,7,582,43],["b-fsx",594,6,640,44],["speed",0,54,640,90]],"gap":"12px 4px","tl":641,"clockw":200,"doc":703,"vw":703},"768x1024 page":{"boxes":[["b-play",0,0,64,50],["clock",76,10,445,39],["livebox",457,6,590,44],["b-live",457,6,590,44],["b-cam",0,94,708,138],["picwrap",602,7,696,43],["speed",0,54,708,90]],"gap":"12px 4px","tl":708,"clockw":369,"doc":768,"vw":768},"768x1024 full":{"boxes":[["b-play",0,0,64,50],["clock",76,11,351,40],["livebox",363,6,496,44],["b-live",363,6,496,44],["b-cam",508,7,552,43],["picwrap",564,7,658,43],["b-fsx",670,6,716,44],["speed",0,54,716,90]],"gap":"12px 4px","tl":716,"clockw":275,"doc":768,"vw":768},"844x390 page":{"boxes":[["b-play",0,0,64,50],["clock",76,11,560,40],["livebox",572,10,684,40],["b-live",572,10,684,40],["b-cam",0,88,784,132],["picwrap",696,7,772,43],["speed",0,54,784,84]],"gap":"12px 4px","tl":784,"clockw":484,"doc":844,"vw":844},"844x390 full":{"boxes":[["b-play",0,0,48,40],["clock",56,8,335,32],["livebox",343,4,462,36],["b-live",343,4,462,36],["b-cam",470,2,514,38],["picwrap",522,4,600,36],["b-fsx",608,1,654,39],["speed",0,42,654,74]],"gap":"8px 2px","tl":654,"clockw":279,"doc":844,"vw":844},"1000x800 page":{"boxes":[["b-play",0,0,64,50],["clock",76,10,489,39],["livebox",501,6,635,44],["b-live",501,6,635,44],["b-cam",0,94,753,138],["picwrap",647,7,741,43],["speed",0,54,753,90]],"gap":"12px 4px","tl":752,"clockw":413,"doc":985,"vw":1000},"1000x800 full":{"boxes":[["b-play",0,0,64,50],["clock",76,11,561,40],["livebox",573,6,706,44],["b-live",573,6,706,44],["b-cam",718,7,762,43],["picwrap",774,7,868,43],["b-fsx",880,6,926,44],["speed",0,54,926,90]],"gap":"12px 4px","tl":926,"clockw":485,"doc":1000,"vw":1000},"1024x768 page":{"boxes":[["b-play",0,0,64,50],["clock",100,10,198,39],["livebox",216,6,349,44],["b-live",216,6,349,44],["b-cam",367,7,535,43],["picwrap",827,7,921,43],["speed",553,7,809,43]],"gap":"18px 0px","tl":0,"clockw":99,"doc":1009,"vw":1024},"1024x768 full":{"boxes":[["b-play",0,0,64,50],["clock",154,11,261,40],["livebox",279,6,412,44],["b-live",279,6,412,44],["b-cam",430,7,474,43],["picwrap",766,7,860,43],["b-fsx",878,6,924,44],["speed",492,7,748,43]],"gap":"18px 0px","tl":55,"clockw":107,"doc":1024,"vw":1024},"1440x900 page":{"boxes":[["b-play",0,0,64,50],["clock",380,10,486,39],["livebox",504,6,637,44],["b-live",504,6,637,44],["b-cam",655,7,823,43],["picwrap",1115,7,1209,43],["speed",841,7,1097,43]],"gap":"18px 0px","tl":280,"clockw":107,"doc":1425,"vw":1440},"1440x900 full":{"boxes":[["b-play",0,0,64,50],["clock",372,11,479,40],["livebox",497,6,630,44],["b-live",497,6,630,44],["b-cam",648,7,692,43],["picwrap",984,7,1078,43],["b-fsx",1096,6,1142,44],["speed",710,7,966,43]],"gap":"18px 0px","tl":272,"clockw":107,"doc":1440,"vw":1440}}


def fake_file(turns, complete, ident=ID):
    head = ("ANTSFAKE %d %d %s\n" % (turns, complete, ident)).encode()
    return head + b"\0" * (4000 - len(head))


class Door:
    """The state of the fake door, and the server that answers for it."""

    def __init__(self):
        self.lock = threading.Lock()
        self.mode = "ok"                      # ok | down (every answer is a 500) | ended (the match is over and kept) | endednokeep | notlive
        self.base = 13520                     # the turns of the match when the clock below was set
        self.t0 = time.time()
        self.freeze = True                    # a match that does not move (the playhead waits at the Limit)
        self.live = [ID, OTHER]               # what GET /live lists
        self.live_door = True                 # False: the door answers 404 (a server with no replays)
        self.kept_at = 0.0                    # the kept replay is there from this time on (0: from the start)
        self.kept_listed = True
        self.names = PLAYERS
        self.hits = []
        door = self

        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *a):
                pass

            def do_GET(self):
                with door.lock:
                    door.hits.append(self.path)
                    door.route(self, urllib.parse.urlparse(self.path).path)

        class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
            daemon_threads = True
            allow_reuse_address = True

        self.server = Server(("127.0.0.1", 0), Handler)
        self.port = self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def url(self, path):
        return "http://127.0.0.1:%d%s" % (self.port, path)

    def turns(self):
        return self.base if self.freeze else self.base + int((time.time() - self.t0) * 20)

    def set(self, **kw):
        with self.lock:
            if "seconds" in kw:
                self.base = int(kw.pop("seconds") * 20)
                self.t0 = time.time()
            for k, v in kw.items():
                setattr(self, k, v)

    def seen(self, prefix):
        with self.lock:
            return [h for h in self.hits if h.startswith(prefix)]

    @staticmethod
    def send(h, code, body, ctype="text/plain"):
        if isinstance(body, str):
            body = body.encode()
        h.send_response(code)
        h.send_header("Content-Type", ctype)
        h.send_header("Content-Length", str(len(body)))
        h.send_header("Cache-Control", "no-store")
        h.end_headers()
        h.wfile.write(body)

    def js(self, h, code, obj):
        self.send(h, code, json.dumps(obj), "application/json")

    def kept_here(self):
        return time.time() >= self.kept_at and (self.kept_at or self.mode == "ended")

    def route(self, h, path):
        if path in ("/play.html", "/live.html"):
            text = open(os.path.join(WEB, "shell.html" if path == "/play.html" else "live.html"), encoding="utf-8").read()
            for k, v in FILLED.items():
                text = text.replace(k, v)
            return self.send(h, 200, text, "text/html; charset=utf-8")
        if path in ("/replay_page.js", "/history_page.js", "/match_stats.js", "/death_map.js", "/index.js"):
            name = path[1:] if path != "/index.js" else None
            body = open(os.path.join(WEB, name) if name else os.path.join(HERE, "web_live_stub.js"), "rb").read()
            return self.send(h, 200, body, "text/javascript")
        if path == "/index.data":
            return self.send(h, 200, b"\0" * 2000, "application/octet-stream")
        if path == "/index.wasm":
            return self.send(h, 200, b"\0asm", "application/wasm")
        if path in ("/matches.css", "/match_stats.css", "/death_map.css", "/replay_stats.css"):
            return self.send(h, 200, open(os.path.join(WEB, path[1:]), "rb").read(), "text/css")
        if path.startswith("/front/") or path in ("/favicon.png", "/favicon.ico"):
            f = os.path.join(WEB, path[1:])
            if os.path.isfile(f):
                ctype = "image/png" if f.endswith(".png") else "image/jpeg" if f.endswith(".jpg") else "font/ttf" if f.endswith(".ttf") else "application/octet-stream"
                return self.send(h, 200, open(f, "rb").read(), ctype)
        if path == "/replays":
            entries = [{"file": "ants-TREASURE-20261008-143209Z.antsrep", "ended": 1791469929, "map": "TREASURE.LVL", "players": PLAYERS, "seconds": 720, "finished": True, "game": "v0.0.0", "sim_rules": 1}]
            if self.kept_listed and self.kept_here():
                entries.insert(0, {"file": KEPT, "ended": 1791534730, "map": "TREASURE.LVL", "players": PLAYERS, "seconds": 1003, "finished": True, "game": "v0.0.0", "sim_rules": 1})
            return self.js(h, 200, {"replays": entries, "count": len(entries), "keep_days": 30, "sim_rules": 1})
        if path.startswith("/replays/"):
            if path[len("/replays/"):] == KEPT and self.kept_here():
                return self.send(h, 200, fake_file(TURNS_END, 1), "application/octet-stream")
            return self.js(h, 404, {"error": "no such replay"})
        if path == "/live":
            if not self.live_door:
                return self.js(h, 404, {"error": "this server keeps no replays"})
            if self.mode == "down":
                return self.send(h, 500, "{}", "application/json")
            turns = self.turns()
            live = [{"id": i, "map": i.split("-")[0] + ".LVL", "started": 1791533464, "turns": turns, "seconds": turns // 20, "players": list(self.names)} for i in self.live]
            return self.js(h, 200, {"live": live, "count": len(live), "sim_rules": 1})
        m = re.match(r"^/live/([A-Za-z0-9_\-]+)$", path)
        if m:
            if self.mode == "down":
                return self.send(h, 500, "{}", "application/json")
            if m.group(1) != ID or self.mode in ("ended", "endednokeep", "notlive"):
                over = m.group(1) == ID and self.mode in ("ended", "endednokeep")
                return self.js(h, 404, {"error": "no such live match", "ended": over, "replay": KEPT if over and self.mode == "ended" else ""})
            return self.send(h, 200, fake_file(self.turns(), 0), "application/octet-stream")
        return self.send(h, 404, "not found")


def until(tab, expression, seconds=15.0, step=0.25):
    """Waits for the expression (evaluated in the page) to be truthy; its last value is returned."""
    deadline = time.time() + seconds
    value = None
    while time.time() < deadline:
        try:
            value = tab.ev(expression)
        except (RuntimeError, TimeoutError):
            value = None                                                       # (the page is not there yet)
        if value:
            return value
        time.sleep(step)
    return value


READY = "!!window.__stub && __stub.total > 0 && !document.getElementById('rtag').hidden"
TAG = "document.getElementById('rtag-s').textContent"
NOTE = "(function () { var n = document.getElementById('endnote'); return n.hidden ? '' : n.textContent; })()"
SHOWN = "function (id) { var e = document.getElementById(id); return !!e && !e.hidden && getComputedStyle(e).display !== 'none' && e.getClientRects().length > 0; }"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--only", default="", help="one part: list, watch, ends, cards, replay, rewind, picture, bar or scrub (default: all)")
    ap.add_argument("--shots", default="", help="a folder to save screenshots in")
    args = ap.parse_args()
    parts = ("list", "watch", "ends", "cards", "replay", "rewind", "picture", "bar", "scrub")
    if args.only and args.only not in parts:
        print("--only is one of: " + ", ".join(parts))
        return 2
    path = find_browser(args.browser)
    if not path:
        print("  SKIP: no Chromium-based browser found (give one with --browser or CHROME)")
        return 3
    failures = []
    count = [0]

    def check(ok, what):
        count[0] += 1
        print("  %s: %s" % ("ok  " if ok else "FAIL", what))
        if not ok:
            failures.append(what)

    door = Door()
    try:
        browser = Browser(path)
    except NotReachable as e:
        print("  SKIP: " + str(e))
        return 3
    print("[web live] the pages of web/ against a fake door and a stand-in for the game, in %s" % os.path.basename(path))
    tabs = []

    def new_tab(width=1440, height=900, dpr=1, mobile=False):
        tab = Tab(browser)
        tab.emulate(width, height, dpr, mobile)
        tabs.append(tab)
        return tab

    def open_page(tab, address):
        tab.call("Page.navigate", {"url": door.url(address)})

    def shot(tab, name):
        tab.save_shot(args.shots, name)

    def touch(tab, points, pause=0.03):                                        # (a finger: down at the first point, along the others, up at the last)
        x, y = points[0]
        tab.call("Input.dispatchTouchEvent", {"type": "touchStart", "touchPoints": [{"x": x, "y": y}]})
        for x, y in points[1:]:
            time.sleep(pause)
            tab.call("Input.dispatchTouchEvent", {"type": "touchMove", "touchPoints": [{"x": x, "y": y}]})
        time.sleep(pause)
        tab.call("Input.dispatchTouchEvent", {"type": "touchEnd", "touchPoints": []})

    def centre(tab, ident):
        r = tab.ev("(function () { var r = document.getElementById('%s').getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2, r.width, r.height]; })()" % ident)
        return r[0], r[1]

    def fresh(**kw):                                                           # (the door as a match at 11:16 that does not move, unless said otherwise)
        state = dict(mode="ok", freeze=True, live=[ID, OTHER], live_door=True, kept_at=0.0, kept_listed=True, names=PLAYERS, seconds=676)
        state.update(kw)
        door.set(**state)
        with door.lock:
            door.hits[:] = []

    try:
        if args.only in ("", "list"):
            print("[web live] part list: the page 'Live now' (web/live.html)")
            fresh(names=[HOSTILE, "Red (Bot (Medium))", "Blue (Bot (Medium))", "Black (Bot (Medium))"])
            tab = new_tab()
            open_page(tab, "/live.html")
            check(until(tab, "document.querySelectorAll('#body .liveentry').length === 2") is True, "two matches are on the page")
            check(tab.ev("document.getElementById('count').textContent") == "2 matches", "the page counts them: 2 matches")
            check(tab.ev("[].map.call(document.querySelectorAll('#body .liveentry .act a'), function (a) { return a.getAttribute('href'); })") == ["/play.html?live=" + ID, "/play.html?live=" + OTHER],
                  "each entry's button goes to the player with the match's id")
            check(tab.ev("document.querySelector('.mast .tag').textContent") == "Matches being played right now.", "the title says what the page has")
            check(tab.ev("!window.__pwn && document.querySelectorAll('#body img[src=x]').length === 0 && document.getElementById('body').textContent.indexOf('<img src=x') !== -1"),
                  "a name that is markup is shown as text: nothing of it ran, nothing of it became an element")
            check(tab.ev("(function () { var c = [].map.call(document.querySelectorAll('#body .liveentry .side i'), function (i) { return i.getAttribute('data-c'); }); var t = [].map.call(document.querySelectorAll('#body .liveentry .side'), function (l) { return l.title; }); return c.length >= 8 && c.every(function (v) { return ['Green', 'Red', 'Blue', 'Black'].indexOf(v) !== -1; }) && t.join().indexOf('undefined') === -1 && document.getElementById('body').textContent.indexOf('undefined') === -1; })()"),
                  "the player chips carry their color (data-c is a color word) and no chip or title says undefined")
            check(tab.ev("document.body.textContent.indexOf('Earlier matches') === -1 && document.querySelector('.mast a[href=\"/matches.html\"]') !== null"), "the earlier matches are not here: the list of all matches is one button away")
            shot(tab, "list-live")
            door.set(live=[ID, OTHER, "SMALL-20261009-090000Z"])
            check(until(tab, "document.querySelectorAll('#body .liveentry').length === 3", 14) is True, "a match that starts later shows up by itself (the page looks again every 10 seconds)")
            tab.close()
            tab = new_tab()
            door.set(live=[])
            open_page(tab, "/live.html")
            check(until(tab, "document.getElementById('body').textContent.indexOf('No match is on right now.') !== -1") is True, "no match: the page says so")
            shot(tab, "list-nothing-live")
            tab.close()
            tab = new_tab()
            door.set(live_door=False)
            open_page(tab, "/live.html")
            check(until(tab, "document.getElementById('body').textContent.indexOf('doesn\u2019t show matches that are being played') !== -1") is True, "a door that answers 404: the page says that this server has none")
            tab.close()
            tab = new_tab(390, 844, 2, True)
            fresh()
            open_page(tab, "/live.html")
            until(tab, "document.querySelectorAll('#body .liveentry').length === 2")
            check(tab.ev("document.documentElement.scrollWidth <= window.innerWidth") is True, "a phone: no sideways scroll")
            tab.close()

        if args.only in ("", "watch"):
            print("[web live] part watch: a match at its present")
            fresh()
            tab = new_tab()
            open_page(tab, "/play.html?live=" + ID)
            check(until(tab, READY) is True, "the page starts the game with the file of the match")
            check(tab.ev("__stub.args.slice(-3)") == ["--replay", "/replay.antsrep", "--replay-live"] and tab.ev("__stub.live") == 1, "the game is asked to follow the file (--replay-live) and does")
            check(tab.ev("document.body.classList.contains('live') && document.getElementById('rtag-b').textContent") == "LIVE", "the tag says LIVE")
            time.sleep(1.5)
            check(tab.ev(TAG) == "about 10 s behind", "at the present: 'about 10 s behind' (it is: %r)" % tab.ev(TAG))
            check(tab.ev("(" + SHOWN + ")('livenow')") is True and tab.ev("(" + SHOWN + ")('b-live')") is False, "the red Live mark is up, 'Jump to live' is not (there is nothing to jump to)")
            check(tab.ev("[].every.call(document.querySelectorAll('[data-dl]'), function (a) { return getComputedStyle(a).display === 'none'; })") is True, "no Download while the file is not whole")
            check(tab.ev("document.querySelector('#r-title .pill.lv') !== null && document.getElementById('r-title').textContent.indexOf('Treasure') === 0"), "the header says Treasure and that it is live")
            check(tab.ev("document.getElementById('watch-link').getAttribute('aria-current')") == "page", "the footer's 'Matches' is marked")
            check(tab.ev("document.getElementById('watch-link').hasAttribute('target')") is False, "and it goes to the list in this tab (nothing is being played here: a match played is not left by it)")
            shot(tab, "watching")
            tab.ev("(function () { __stub.state = 3; __stub.turn = Math.max(0, Module._ants_replay_get(8) - 50 * 20); })()")             # paused, 50 seconds behind
            check(until(tab, "/^Paused · \\d+:\\d\\d behind$/.test(" + TAG + ")") is True, "paused behind: 'Paused · m:ss behind' (it is: %r)" % tab.ev(TAG))
            check(until(tab, "(" + SHOWN + ")('b-live')") is True, "'Jump to live' is up")
            shot(tab, "behind")
            tab.ev("document.getElementById('b-live').click()")
            check(until(tab, "__stub.seeks.length > 0 && __stub.seeks[__stub.seeks.length - 1] >= Module._ants_replay_get(8)") is True, "'Jump to live' seeks to the Limit")
            tab.close()
            tab = new_tab()
            open_page(tab, "/play.html?live=" + ID)
            until(tab, READY)
            door.set(freeze=False, seconds=676)
            before = tab.ev("__stub.total")
            seen = []
            for _ in range(24):                                              # 12 seconds of a match that moves: the file grows by the feed (every 4 s) and the tag stays at 'about 10 s behind'
                time.sleep(0.5)
                seen.append(tab.ev(TAG))
            check(tab.ev("__stub.total") > before + 100 and tab.ev("__stub.extends") >= 2, "the file grows: the page writes it again and the game is told (Extend): %d -> %d turns, %d Extends" % (before, tab.ev("__stub.total"), tab.ev("__stub.extends")))
            check(all(t == "about 10 s behind" for t in seen), "while the match moves the playhead stays at the present (the tag was %s)" % sorted(set(seen)))
            check(tab.ev("__stub.ignored") == 0, "every file that the page wrote was the same match with more turns")
            door.set(mode="down")
            check(until(tab, NOTE + " === 'The live feed stopped. Trying again…'", 14) is True, "the door stops answering: 'The live feed stopped. Trying again…'")
            shot(tab, "feed-stopped")
            door.set(mode="ok")
            check(until(tab, NOTE + " === ''", 14) is True, "the door answers again: the note goes away")
            tab.close()
            door.set(freeze=True)
            tab = new_tab(390, 844, 2, True)
            open_page(tab, "/play.html?live=" + ID)
            until(tab, READY)
            time.sleep(1.5)
            check(tab.ev("document.documentElement.scrollWidth <= window.innerWidth") is True, "a phone: no sideways scroll")
            check(tab.ev("(function () { var r = document.getElementById('rbar').getBoundingClientRect(); return r.left >= 0 && r.right <= window.innerWidth; })()") is True, "a phone: the bar is inside the screen")
            shot(tab, "watching-phone")
            tab.close()

        if args.only in ("", "ends"):
            print("[web live] part ends: a match that ends")
            fresh()
            tab = new_tab()
            open_page(tab, "/play.html?live=" + ID)
            until(tab, READY)
            door.set(mode="ended", kept_at=time.time() + 6)                # (kept: the replay is in the store a few seconds later)
            check(until(tab, "location.search === '?replay=" + KEPT + "'", 20) is True, "the match is over and kept: the address becomes the replay's (after the file was slow to come)")
            check(until(tab, "__stub.complete === 1") is True and tab.ev("__stub.ignored") == 0, "the game got the whole file (Extend took it)")
            check(tab.ev("document.body.classList.contains('live')") is False and tab.ev("document.getElementById('rtag-b').textContent") == "REPLAY", "the red marks are gone: REPLAY")
            check(tab.ev(NOTE) == "The match is over. It is now in the list as a replay.", "the note says so (%r)" % tab.ev(NOTE))
            check(tab.ev("document.querySelector('[data-dl]').getAttribute('href')") == "/replays/" + KEPT and tab.ev("(" + SHOWN + ")('b-live')") is False, "Download is back and goes to the file")
            tab.ev("(function () { __stub.turn = __stub.total - 30; })()")
            time.sleep(2.5)
            shot(tab, "ends")
            tab.close()
            fresh()
            tab = new_tab()
            open_page(tab, "/play.html?live=" + ID)
            until(tab, READY)
            door.set(mode="endednokeep")
            check(until(tab, NOTE + " === 'The match is over.'", 20) is True and tab.ev("__stub.liveOver") == 1, "a match that was not kept: 'The match is over.' and the game is told (LiveOver)")
            check(tab.ev("location.search") == "?live=" + ID and tab.ev("document.body.classList.contains('live')") is False, "its address stays; the red marks are gone")
            tab.close()

        if args.only in ("", "cards"):
            print("[web live] part cards: a link that cannot be followed")
            fresh(mode="ended")
            for size, tab in (("wide", new_tab()), ("phone", new_tab(390, 844, 2, True))):
                open_page(tab, "/play.html?live=" + ID)
                check(until(tab, "document.getElementById('rm-title').textContent === 'This match is over'") is True, "%s: the link of a match that is over has its card" % size)
                check(tab.ev("[].map.call(document.querySelectorAll('#rm-body a'), function (a) { return a.getAttribute('href'); })") == ["/play.html?replay=" + KEPT, "/matches.html"], "%s: 'Watch the replay' goes to the kept file, '‹ All matches' to the list" % size)
                check(until(tab, "document.querySelectorAll('#r-meta .chip').length === 4") is True and tab.ev("(" + SHOWN + ")('splash-overlay')") is True, "%s: the header says what the match was and the clay stays behind the card" % size)
                check(tab.ev("document.querySelector('[data-dl]').getAttribute('href')") == "/replays/" + KEPT, "%s: Download goes to the kept file" % size)
                shot(tab, "over-" + size)
                tab.close()
            fresh()
            for address, what in (("/play.html?live=NOSUCH-20261009-082104Z", "a match that is not there"), ("/play.html?live=..%2F..%2Fetc%2Fpasswd", "an id that is not of the shape"), ("/play.html?live=", "no id"),
                                  ("/play.html?live=TREASURE-20261009-082104Z%3Cb%3E", "an id with markup")):
                tab = new_tab()                                                # (a tab for each: the card of the one before must not be mistaken for this one's)
                open_page(tab, address)
                check(until(tab, "document.getElementById('rm-title').textContent === 'That match isn’t live'") is True, "%s: 'That match isn’t live'" % what)
                check(tab.ev("[].map.call(document.querySelectorAll('#rm-body a'), function (a) { return a.getAttribute('href'); })") == ["/matches.html"], "%s: its card goes back to the list" % what)
                if "NOSUCH" in address:
                    shot(tab, "not-live")
                tab.close()
            check([h for h in door.seen("/live/") if "NOSUCH" not in h] == [], "the page asked the door about no id that is not of the shape")

        if args.only in ("", "replay"):
            print("[web live] part replay: a plain replay is as it was")
            fresh(kept_at=1.0)                                                 # (the replay of the match is in the store)
            tab = new_tab()
            open_page(tab, "/play.html?replay=" + KEPT)
            check(until(tab, READY) is True, "a replay starts")
            check(tab.ev("document.body.classList.contains('live') || __stub.args.indexOf('--replay-live') !== -1") is False and tab.ev("document.getElementById('rtag-b').textContent") == "REPLAY", "it has no live marks and is not asked to follow")
            check(tab.ev("(" + SHOWN + ")('livenow')") is False and tab.ev("(" + SHOWN + ")('b-live')") is False and tab.ev("document.querySelector('[data-dl]').getAttribute('href')") == "/replays/" + KEPT, "no Live mark, no Jump to live, Download is there")
            time.sleep(5)
            check(door.seen("/live") == [], "it never asks the door for a live match")
            tab.close()
            tab = new_tab()
            open_page(tab, "/play.html?replay=ants-NOPE-20260101-000000Z.antsrep")
            check(until(tab, "document.getElementById('rm-title').textContent === 'That replay is gone'") is True, "a file that is gone: 'That replay is gone'")
            tab.close()

        if args.only in ("", "rewind"):
            print("[web live] part rewind: Back 30 seconds")
            fresh(kept_at=1.0)
            seeks = "__stub.seeks.slice()"
            geo = ("(function () { var q = function (id) { var r = document.getElementById(id).getBoundingClientRect(); return [r.left, r.top, r.right, r.bottom]; };"
                   " return { play: q('b-play'), rew: q('b-rew'), bar: q('rbar'), vw: window.innerWidth }; })()")
            tab = new_tab()
            open_page(tab, "/play.html?replay=" + KEPT)
            check(until(tab, READY) is True, "a replay starts")
            check(tab.ev("(" + SHOWN + ")('b-rew')") is True and tab.ev("document.getElementById('b-rew').parentNode === document.getElementById('rbar')"), "Back 30 seconds is in the bar")
            check(tab.ev("document.getElementById('b-play').nextElementSibling === document.getElementById('b-rew')") is True, "it is right of Pause")
            check(tab.ev("[document.getElementById('b-rew').getAttribute('aria-label'), document.getElementById('b-rew').title, document.querySelector('#b-rew .n').textContent]") == ["Back 30 seconds", "Back 30 seconds", "30"], "its name, its tip and the 30")
            same = ("(function () { var a = getComputedStyle(document.getElementById('b-play')), b = getComputedStyle(document.getElementById('b-rew'));"
                    " return ['color', 'backgroundColor', 'borderTopColor', 'borderTopWidth', 'boxShadow', 'borderRadius'].every(function (k) { return a[k] === b[k]; }); })()")
            check(tab.ev(same) is True, "the same color, ground, edge, bevel and corners as Pause")
            size = tab.ev("(function () { var r = document.getElementById('b-rew').getBoundingClientRect(); return [r.width, r.height]; })()")
            check(size[0] >= 64 and size[1] >= 50, "at least 64 x 50 px (%d x %d)" % (size[0], size[1]))
            bare = ("(function () { var s = getComputedStyle(document.querySelector('#b-rew .n')); return s.backgroundColor === 'rgba(0, 0, 0, 0)' && s.borderTopWidth === '0px' && s.boxShadow === 'none'"
                    " && s.color === getComputedStyle(document.querySelector('#b-rew svg')).fill; })()")
            check(tab.ev(bare) is True, "the 30 has no box, edge or shadow of its own and the color of the triangles")
            tab.ev("__stub.turn = 1000; __stub.state = 2; __stub.speed = 200")
            cx, cy = centre(tab, "b-rew")
            tab.click(cx, cy)
            check(until(tab, "__stub.seeks.length === 1") is True and 400 <= tab.ev(seeks)[0] <= 440, "a click seeks 30 s back (the match plays at 2x: turn about 1000 -> 400, got %r)" % tab.ev(seeks))
            check(until(tab, "__stub.state === 2", 4) is True and tab.ev("__stub.speed") == 200, "a match that plays plays on, at the same speed")
            tab.ev("__stub.seeks.length = 0; __stub.turn = 1000; __stub.state = 3")
            time.sleep(0.6)
            tab.click(cx, cy)
            check(until(tab, "__stub.seeks.length === 1") is True and tab.ev(seeks) == [400], "paused: the same jump")
            check(until(tab, "__stub.state === 3", 4) is True and tab.ev("__stub.speed") == 200, "and it stays paused, at the same speed")
            for turn, want in ((300, 0), (599, 0), (600, 0), (601, 1), (0, 0)):
                tab.ev("__stub.seeks.length = 0; __stub.turn = %d; __stub.state = 3" % turn)
                time.sleep(0.6)
                tab.click(cx, cy)
                check(until(tab, "__stub.seeks.length === 1") is True and tab.ev(seeks) == [want], "from turn %d it goes to turn %d, never below 0" % (turn, want))
            tab.ev("__stub.seeks.length = 0; __stub.turn = 2000; __stub.state = 3")
            time.sleep(0.6)
            tab.focus_js = tab.ev("document.getElementById('b-rew').focus(); document.activeElement.id")
            tab.call("Input.dispatchKeyEvent", {"type": "keyDown", "key": "Enter", "code": "Enter", "windowsVirtualKeyCode": 13})
            tab.call("Input.dispatchKeyEvent", {"type": "keyUp", "key": "Enter", "code": "Enter", "windowsVirtualKeyCode": 13})
            check(until(tab, "__stub.seeks.length === 1", 3) is True and tab.ev(seeks) == [1400], "Enter on the focused button presses it")
            tab.ev("document.activeElement.blur(); __stub.turn = 1000; __stub.state = 3; __stub.speed = 100")
            time.sleep(0.6)
            shot(tab, "rewind-desktop")
            r = tab.ev("(function () { var a = document.getElementById('b-play').getBoundingClientRect(), b = document.getElementById('rbar').getBoundingClientRect(); return [a.left - 8, a.top - 8, b.width, a.height + 16]; })()")
            if args.shots:
                import base64
                os.makedirs(args.shots, exist_ok=True)
                png = tab.call("Page.captureScreenshot", {"format": "png", "clip": {"x": r[0], "y": r[1], "width": 330, "height": r[3], "scale": 3}})["data"]
                with open(os.path.join(args.shots, "rewind-close.png"), "wb") as f:
                    f.write(base64.b64decode(png))
            tab.close()
            tab = new_tab(390, 844, 2, True)                                   # a phone upright
            open_page(tab, "/play.html?replay=" + KEPT)
            until(tab, READY)
            time.sleep(1.0)
            g = tab.ev(geo)
            check(tab.ev("document.documentElement.scrollWidth <= window.innerWidth") is True and g["bar"][0] >= 0 and g["bar"][2] <= g["vw"] and g["rew"][2] <= g["bar"][2] + 0.5, "390 px: the bar fits and so does the button (bar %r, button %r)" % (g["bar"], g["rew"]))
            tab.ev("__stub.turn = 1000; __stub.state = 3; __stub.speed = 100; __stub.seeks.length = 0")
            rx, ry = centre(tab, "b-rew")
            tab.tap(rx, ry)
            check(until(tab, "__stub.seeks.length === 1") is True and tab.ev(seeks) == [400], "a finger's tap seeks 30 s back")
            until(tab, "__stub.state === 2")
            tab.ev("__stub.turn = 1000; __stub.state = 3")
            time.sleep(0.6)
            shot(tab, "rewind-phone")
            fx, fy = centre(tab, "fullscreen-btn-r")
            tab.tap(fx, fy)
            check(until(tab, "document.getElementById('rbar').classList.contains('over')", 10) is True, "the phone goes fullscreen")
            time.sleep(0.6)
            g = tab.ev(geo)
            check(g["rew"][0] >= 0 and g["rew"][2] <= g["vw"] and g["bar"][2] <= g["vw"] + 0.5, "fullscreen on a phone: the button is inside the screen (%r)" % g["rew"])
            shot(tab, "rewind-phone-fullscreen")
            tab.close()
            tab = new_tab(844, 390, 2, True)                                   # a phone on its side, fullscreen
            open_page(tab, "/play.html?replay=" + KEPT)
            until(tab, READY)
            time.sleep(1.0)
            fx, fy = centre(tab, "fullscreen-btn-r")
            tab.tap(fx, fy)
            check(until(tab, "document.getElementById('rbar').classList.contains('over')", 10) is True, "a phone on its side goes fullscreen")
            time.sleep(0.6)
            g = tab.ev(geo)
            check(g["rew"][0] >= 0 and g["rew"][2] <= g["vw"] and g["bar"][2] <= g["vw"] + 0.5 and g["rew"][0] >= g["play"][2] - 0.5, "on its side: the button is right of Pause and inside the screen (%r)" % g["rew"])
            shot(tab, "rewind-landscape-fullscreen")
            tab.close()
            # the bar at many window sizes, in the page and in fullscreen, with a short and the longest clock: nothing is worse than it was before the button (MAIN_BAR: the timeline's width in px and the
            # faults that the bar of v0.12.24 had at that size), the timeline is at most 20 px narrower, and nothing lies on anything, leaves the screen, is cut off or scrolls sideways that did not before
            probe = ("(function () { var o = { boxes: [], vw: window.innerWidth, doc: document.documentElement.scrollWidth };"
                     " ['b-play', 'b-rew', 'clock', 'b-cam', 'picwrap', 'b-fsx'].forEach(function (id) { var e = document.getElementById(id); if (!e || getComputedStyle(e).display === 'none' || e.hidden || !e.getClientRects().length) return;"
                     " var r = e.getBoundingClientRect(); o.boxes.push([id, r.left, r.top, r.right, r.bottom]); });"
                     " o.tl = document.getElementById('tl').getBoundingClientRect().width; var c = document.getElementById('clock'); o.clip = c.scrollWidth - c.clientWidth; return o; })()")
            for (width, height, mobile) in BAR_WINDOWS:
                for full in (False, True):
                    for key, (total, turn) in (("a", (20000, 1000)), ("c", (74460, 74460))):               # 0:50 / 16:40 and 62:03 / 62:03
                        fresh(kept_at=1.0)
                        tab = new_tab(width, height, 2 if mobile else 1, mobile)
                        open_page(tab, "/play.html?replay=" + KEPT)
                        until(tab, READY)
                        tab.ev("__stub.total = %d; __stub.turn = %d; __stub.state = 3" % (total, turn))
                        time.sleep(0.6)
                        if full:
                            fx, fy = centre(tab, "fullscreen-btn-r")
                            (tab.tap if mobile else tab.click)(fx, fy)
                            until(tab, "document.getElementById('rbar').classList.contains('over')", 10)
                            time.sleep(0.6)
                        g = tab.ev(probe)
                        boxes = g["boxes"]
                        faults = set()
                        for n, a in enumerate(boxes):
                            if a[1] < -0.5 or a[3] > g["vw"] + 0.5:
                                faults.add("off")
                            for b in boxes[n + 1:]:
                                if not (a[3] <= b[1] + 0.5 or b[3] <= a[1] + 0.5 or a[4] <= b[2] + 0.5 or b[4] <= a[2] + 0.5):
                                    faults.add("lap")
                        if g["clip"] > 0:
                            faults.add("clip")
                        if g["doc"] > g["vw"]:
                            faults.add("hscroll")
                        label = "%dx%d %s %s" % (width, height, "full" if full else "page", key)
                        was_tl, was = MAIN_BAR[label]
                        check(faults <= set(was) and g["tl"] >= was_tl - 20, "%s: no new fault (%s, before: %s) and the timeline is %d px (before: %d)" % (label, sorted(faults) or "none", list(was) or "none", g["tl"], was_tl))
                        if args.shots and (width, height) in ((360, 844), (390, 844), (768, 1024), (844, 390), (1024, 768)) and key == "c":
                            shot(tab, "rewind-bar-%dx%d-%s" % (width, height, "fullscreen" if full else "page"))
                        tab.close()
            # a live bar is exactly what it was: the same boxes, gaps, timeline, clock and page width at every size, in the page and in fullscreen
            livebar = ("(function () { var o = { boxes: [], vw: window.innerWidth, doc: document.documentElement.scrollWidth };"
                       " ['b-play', 'clock', 'livebox', 'b-live', 'b-cam', 'picwrap', 'b-fsx', 'speed'].forEach(function (id) { var e = document.getElementById(id); if (!e || getComputedStyle(e).display === 'none' || e.hidden || !e.getClientRects().length) return;"
                       " var r = e.getBoundingClientRect(); o.boxes.push([id, Math.round(r.left), Math.round(r.top), Math.round(r.right), Math.round(r.bottom)]); });"
                       " var p0 = o.boxes[0]; o.boxes = o.boxes.map(function (b) { return [b[0], b[1] - p0[1], b[2] - p0[2], b[3] - p0[1], b[4] - p0[2]]; });"
                       " var cs = getComputedStyle(document.getElementById('rbar')); o.gap = cs.columnGap + ' ' + cs.rowGap; o.tl = Math.round(document.getElementById('tl').getBoundingClientRect().width);"
                       " o.clockw = Math.round(document.getElementById('clock').getBoundingClientRect().width); return o; })()")
            for label, was in LIVE_BAR.items():
                size, mode = label.split(" ")
                width, height = [int(n) for n in size.split("x")]
                mobile = (width, height) in ((320, 844), (360, 844), (390, 844), (412, 844), (420, 844), (430, 844), (520, 844), (768, 1024), (844, 390))
                fresh()
                tab = new_tab(width, height, 2 if mobile else 1, mobile)
                open_page(tab, "/play.html?live=" + ID)
                until(tab, READY)
                tab.ev("__stub.state = 3; __stub.turn = Math.max(0, Module._ants_replay_get(8) - 50 * 20)")
                time.sleep(0.7)
                if mode == "full":
                    fx, fy = centre(tab, "fullscreen-btn-r")
                    (tab.tap if mobile else tab.click)(fx, fy)
                    until(tab, "document.getElementById('rbar').classList.contains('over')", 10)
                    time.sleep(0.6)
                g = tab.ev(livebar)
                near = lambda a, b: len(a) == len(b) and all(x[0] == y[0] and all(abs(u - v) <= 1 for u, v in zip(x[1:], y[1:])) for x, y in zip(a, b))        # (a pixel for the rounding of a fraction)
                off = [k for k in ("boxes", "gap", "tl", "clockw", "doc", "vw") if (not near(g[k], was[k]) if k == "boxes" else g[k] != was[k])]
                check(not off, "live bar %s is as it was (differs in %s)" % (label, ", ".join("%s: %s, was %s" % (k, g[k], was[k]) for k in off) or "nothing"))
                tab.close()
            fresh()
            tab = new_tab()                                                    # a match that is being played: no button
            open_page(tab, "/play.html?live=" + ID)
            check(until(tab, READY) is True, "a live match starts")
            time.sleep(0.5)
            check(tab.ev("(" + SHOWN + ")('b-rew')") is False and tab.ev("document.getElementById('b-rew').hidden") is True, "a live match has no Back 30 seconds")
            check(tab.ev("document.getElementById('rbar').classList.contains('islive')") is True, "(the bar is the live one)")
            shot(tab, "rewind-live-none")
            tab.close()

        if args.only in ("", "picture"):
            print("[web live] part picture: the replay bar's Picture button, and the bar as wide as the picture")
            fresh(kept_at=1.0)
            width_of = "(function () { var s = document.getElementById('game-stage').getBoundingClientRect(), b = document.getElementById('rbar').getBoundingClientRect(); return [Math.round(s.width), Math.round(b.width), Math.round(s.left), Math.round(b.left)]; })()"
            state = "(function () { return [document.getElementById('picpop').hidden, document.getElementById('picbtn').getAttribute('aria-expanded'), document.activeElement && document.activeElement.id]; })()"
            shown_shape = "(document.querySelector('#pic-select-list [aria-selected=true]') || document.body).getAttribute('data-shape')"
            key = lambda tab, name, code, vk: [tab.call("Input.dispatchKeyEvent", {"type": kind, "key": name, "code": code, "windowsVirtualKeyCode": vk}) for kind in ("keyDown", "keyUp")]
            for query, shape in (("", "16:9"), ("&aspect=4:3", "4:3"), ("&aspect=16:10", "16:10"), ("&aspect=21:9", "21:9"), ("&aspect=32:9", "32:9")):
                tab = new_tab()
                tab.ev("try { localStorage.removeItem('ants.aspect.v2'); } catch (e) {}")
                open_page(tab, "/play.html?replay=" + KEPT + query)
                check(until(tab, READY) is True, "a replay starts (%s)" % shape)
                time.sleep(0.5)
                got = tab.ev(width_of)
                check(got[0] > 300 and abs(got[0] - got[1]) <= 1 and abs(got[2] - got[3]) <= 1, "%s: the bar is as wide as the picture and stands under it (stage %s, bar %s, left %s / %s)" % (shape, got[0], got[1], got[2], got[3]))
                check(tab.ev("document.getElementById('game-stage').getAttribute('data-aspect')") == shape, "%s: the picture has the shape that the address asks for" % shape)
                check(tab.ev("getComputedStyle(document.getElementById('view-bar')).display") == "none", "%s: the player's Picture row stays hidden" % shape)
                bx = tab.ev("(function () { var r = document.getElementById('picbtn').getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2]; })()")
                tab.click(bx[0], bx[1])
                check(tab.ev(state)[:2] == [False, "true"], "%s: the button opens the window and says so" % shape)
                pos = tab.ev("(function () { var p = document.getElementById('picpop').getBoundingClientRect(), b = document.getElementById('picbtn').getBoundingClientRect(), r = document.getElementById('rbar').getBoundingClientRect();"
                             " return [p.bottom <= b.top, Math.abs(p.right - b.right) <= 2, p.left >= 0 && p.right <= window.innerWidth, p.top >= 0, [].map.call(document.querySelectorAll('#pic-select-list [role=option]'), function (x) { return x.getAttribute('data-shape'); }).join(' ')]; })()")
                check(pos == [True, True, True, True, "4:3 3:2 16:10 16:9 20:9 21:9 32:9"], "%s: the window opens above the bar at its right end, inside the screen, with the dropdown of the seven shapes (%s)" % (shape, pos))
                check(tab.ev(shown_shape) == shape, "%s: the dropdown shows the picture's shape" % shape)
                if shape == "16:9":
                    shot(tab, "picture-open")
                    check(tab.ev("(function () { var n = document.getElementById('pic-note'); return (n.hidden ? '' : n.textContent) === AntsShapes.fillsLine(window.screen.width, window.screen.height); })()") is True,
                          "the quiet line under the dropdown names the shape that fills the screen, as in a match, and says nothing when none does")
                    ox, oy = 200, 200
                    tab.click(ox, oy)
                    check(tab.ev(state)[:2] == [True, "false"], "a click elsewhere closes the window")
                    tab.click(bx[0], bx[1])
                    key(tab, "Escape", "Escape", 27)
                    check(tab.ev(state) == [True, "false", "picbtn"], "Esc closes the window and gives the focus back to the button")
                    tab.click(bx[0], bx[1])
                    tab.ev("document.getElementById('pic-select').focus(); 1")
                    key(tab, "Enter", "Enter", 13)
                    check(tab.ev("document.getElementById('pic-select-list').hidden") is False and tab.ev("document.getElementById('pic-select').getAttribute('aria-expanded')") == "true", "Enter on the dropdown opens its list")
                    key(tab, "Escape", "Escape", 27)
                    check(tab.ev("document.getElementById('pic-select-list').hidden") is True and tab.ev(state)[0] is False and tab.ev("document.activeElement.id") == "pic-select", "Esc with the list open closes the list alone: the window stays open and the focus stays on the dropdown")
                    key(tab, "Enter", "Enter", 13)
                    key(tab, "ArrowDown", "ArrowDown", 40)
                    key(tab, "ArrowDown", "ArrowDown", 40)                                          # (16:9, 20:9, 21:9: the mark moves, nothing is chosen yet)
                    check(tab.ev("document.getElementById('game-stage').getAttribute('data-aspect')") == "16:9", "the arrows only move the mark: the picture is still 16:9")
                    key(tab, "Enter", "Enter", 13)                                                  # (the choice; the window stays open)
                    time.sleep(0.5)
                    got = tab.ev(width_of)
                    check(tab.ev("document.getElementById('game-stage').getAttribute('data-aspect')") == "21:9" and abs(got[0] - got[1]) <= 1 and got[0] > 1200, "a shape chosen in the window changes the picture at once and the bar follows it (stage %s, bar %s)" % (got[0], got[1]))
                    check(tab.ev("localStorage.getItem('ants.aspect.v2')") == "21:9" and "aspect=21:9" in tab.ev("location.search") and tab.ev(state)[0] is False, "the choice is remembered and put in the address; the window stays open")
                    check(tab.ev(shown_shape) == "21:9" and tab.ev("document.getElementById('pic-select-list').hidden") is True, "the chosen shape is the dropdown's, and its list is shut")
                    tab.call("Page.reload")
                    check(until(tab, READY) is True and tab.ev("document.getElementById('game-stage').getAttribute('data-aspect')") == "21:9", "the page, opened again with the address as it was, keeps the shape")
                    tab.call("Page.navigate", {"url": door.url("/play.html?replay=" + KEPT)})
                    check(until(tab, READY) is True and tab.ev("document.getElementById('game-stage').getAttribute('data-aspect')") == "21:9", "the address without ?aspect= follows the remembered choice")
                tab.close()
            tab = new_tab(390, 844, 2, True)
            open_page(tab, "/play.html?replay=" + KEPT)
            check(until(tab, READY) is True, "a replay starts on a phone")
            time.sleep(0.5)
            row = tab.ev("(function () { var c = document.getElementById('clock').getBoundingClientRect(), b = document.getElementById('picbtn').getBoundingClientRect(), p = document.getElementById('b-play').getBoundingClientRect();"
                         " return [Math.abs((b.top + b.bottom) / 2 - (c.top + c.bottom) / 2) <= 12, b.left >= c.right - 1, b.right <= window.innerWidth, Math.abs((p.top + p.bottom) / 2 - (c.top + c.bottom) / 2) <= 12]; })()")
            check(row == [True, True, True, True], "a phone: the button takes the place beside the clock, on the play button's row (%s)" % row)
            check(tab.ev("document.documentElement.scrollWidth <= window.innerWidth") is True, "a phone: no sideways scroll")
            bx = tab.ev("(function () { var r = document.getElementById('picbtn').getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2]; })()")
            tab.tap(bx[0], bx[1])
            check(tab.ev("(function () { var p = document.getElementById('picpop').getBoundingClientRect(); return !document.getElementById('picpop').hidden && p.left >= 0 && p.right <= window.innerWidth && p.bottom <= document.getElementById('picbtn').getBoundingClientRect().top; })()") is True, "a phone: the window opens above the button, inside the screen")
            shot(tab, "picture-phone")
            tab.close()
            for width, height in ((320, 640), (360, 740), (390, 844)):
                tab = new_tab(width, height, 2, True)
                open_page(tab, "/play.html?replay=" + KEPT + "&aspect=21:9")
                check(until(tab, READY) is True, "a %d px phone, 21:9: the replay starts" % width)
                time.sleep(0.5)
                bx = tab.ev("(function () { var r = document.getElementById('picbtn').getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2]; })()")
                tab.tap(bx[0], bx[1])
                box = tab.ev("(function () { var p = document.getElementById('picpop').getBoundingClientRect(); return [Math.round(p.left), Math.round(p.right), Math.round(p.top), window.innerWidth, document.documentElement.scrollWidth]; })()")
                check(not tab.ev("document.getElementById('picpop').hidden") is True and box[0] >= 0 and box[1] <= box[3] and box[2] >= 0 and box[4] <= box[3], "a %d px phone: the window lies inside the screen, with no sideways scroll (x %d to %d of %d)" % (width, box[0], box[1], box[3]))
                if width == 320:
                    shot(tab, "picture-320")
                tab.close()
            tab = new_tab()
            open_page(tab, "/play.html?replay=" + KEPT + "&aspect=4:3")
            check(until(tab, READY) is True, "a replay in fullscreen: it starts")
            fx, fy = centre(tab, "fullscreen-btn-r")
            tab.click(fx, fy)
            if until(tab, "document.getElementById('rbar').classList.contains('over')", 10) is True:
                bx = tab.ev("(function () { var r = document.getElementById('picbtn').getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2]; })()")
                tab.click(bx[0], bx[1])
                check(tab.ev("(function () { var p = document.getElementById('picpop').getBoundingClientRect(), b = document.getElementById('picbtn').getBoundingClientRect(); return !document.getElementById('picpop').hidden && p.bottom <= b.top && p.top >= 0 && p.right <= window.innerWidth; })()") is True, "fullscreen: the window opens above the floating bar, on the picture")
                shot(tab, "picture-fullscreen")
                time.sleep(4)
                check(tab.ev("document.getElementById('rbar').classList.contains('idle')") is False, "fullscreen: the bar stays while the window is open (it would go after 3 seconds)")
            else:
                check(False, "fullscreen: the bar lies over the picture")
            tab.close()

        if args.only in ("", "bar"):
            print("[web live] part bar: the REPLAY tag outside the picture, and the bar of a fullscreen page")
            fresh(kept_at=1.0)
            outside = ("(function () { var t = document.getElementById('rtag').getBoundingClientRect(), g = document.getElementById('game-container').getBoundingClientRect(), b = document.getElementById('rbar').getBoundingClientRect();"
                       " return t.top >= g.bottom && t.bottom <= b.top && t.left >= 0 && t.right <= window.innerWidth && t.width > 0 && document.getElementById('rbar').contains(document.getElementById('rtag')); })()")
            for label, size in (("a phone upright", (390, 844, 2, True)), ("a computer window", (1440, 900, 1, False)), ("a phone on its side", (800, 360, 2, True))):
                tab = new_tab(*size)
                open_page(tab, "/play.html?replay=" + KEPT)
                until(tab, READY)
                time.sleep(1.0)
                check(tab.ev(outside) is True, "%s: the REPLAY tag lies under the picture and over the bar, inside the screen, with nothing of it over the picture" % label)
                if label == "a phone upright":
                    shot(tab, "tag-phone")
                    check(tab.ev("document.documentElement.scrollWidth <= window.innerWidth") is True, "%s: no sideways scroll" % label)
                    check(tab.ev("getComputedStyle(document.getElementById('b-tab')).display") == "none" and tab.ev("document.getElementById('redge').hidden") is True, "%s: no tab and no bottom strip outside fullscreen" % label)
                if label == "a phone on its side":
                    shot(tab, "tag-side")
                    print("[web live] a phone on its side: fullscreen")
                    fx, fy = centre(tab, "fullscreen-btn-r")
                    tab.tap(fx, fy)                                               # (a finger: the page now knows that touch is what is used)
                    check(until(tab, "document.getElementById('rbar').classList.contains('over')", 10) is True, "the bar lies over the picture in fullscreen")
                    idle = "document.getElementById('rbar').classList.contains('idle')"
                    edge_up = "!document.getElementById('redge').hidden"
                    note_up = "!document.getElementById('rnote').hidden"
                    check(tab.ev(idle) is False and tab.ev(edge_up) is False, "fullscreen opens with the bar up (and no strip)")
                    shot(tab, "fs-bar-up")
                    check(tab.ev("(function () { var t = document.getElementById('rtag').getBoundingClientRect(), b = document.getElementById('rbar').getBoundingClientRect(); return t.bottom <= b.top + 4 && t.left >= b.left; })()") is True, "the tag lies above the bar, at its left, and goes with it")
                    check(until(tab, idle, 6) is True, "playing: the bar goes by itself after 3 seconds")
                    check(tab.ev(edge_up) is True and tab.ev(note_up) is True and tab.ev("document.getElementById('rnote').textContent") == "Swipe up from the bottom edge to bring the bar back",
                          "a finger was used: the strip is there and the note says how to bring the bar back")
                    shot(tab, "fs-bar-gone-note")
                    check(until(tab, "document.getElementById('rnote').hidden", 6) is True, "the note goes after 3 seconds")
                    w, h = 800, 360
                    touch(tab, [(w / 2, h / 2)])                                  # a tap on the picture
                    time.sleep(0.4)
                    check(tab.ev(idle) is True, "a tap on the picture does not call the bar")
                    touch(tab, [(300, 200), (350, 180), (400, 160), (450, 140), (500, 120)])      # a drag across the picture (the map)
                    time.sleep(0.4)
                    check(tab.ev(idle) is True, "a drag across the picture does not call it either")
                    check(tab.ev("getComputedStyle(document.getElementById('rbar')).opacity") == "0", "the picture is clear: the bar and its tag are not drawn")
                    shot(tab, "fs-clear")
                    touch(tab, [(w / 2, h - 20), (w / 2, h - 40), (w / 2, h - 70), (w / 2, h - 100)])   # a swipe up from the bottom edge
                    check(until(tab, "!" + idle, 3) is True and tab.ev(edge_up) is False, "a swipe up from the bottom edge calls the bar (and the strip goes)")
                    check(until(tab, idle, 6) is True, "it goes by itself again")
                    check(tab.ev(note_up) is False, "and the note does not come a second time in this visit")
                    touch(tab, [(w / 2, h - 20)])                                 # a tap there
                    check(until(tab, "!" + idle, 3) is True, "a tap on the bottom edge calls it too")
                    bx, by = (lambda r: (r[0], r[1]))(tab.ev("(function () { var r = document.querySelector('#speed button[data-s=\"2\"]').getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2]; })()"))
                    check(until(tab, idle, 6) is True and by > h - 56, "the bar is away again (its 2x button lies in the strip along the bottom edge)")
                    touch(tab, [(bx, by)])                                        # a tap on the strip where the 2x button will be: the bar comes up and the tap is not a click on the button
                    time.sleep(0.6)
                    check(tab.ev(idle) is False and tab.ev("__stub.speed") == 100, "a tap on the strip over a button calls the bar and does not press the button (the speed is %r)" % tab.ev("__stub.speed"))
                    tab.ev("__stub.state = 3")                                    # the match is paused: the bar stays
                    time.sleep(4.0)
                    check(tab.ev(idle) is False, "paused: the bar stays up")
                    tx, ty = centre(tab, "tl")
                    touch(tab, [(tx, ty), (tx, ty + 30), (tx, ty + 60)])         # a swipe down that starts on the timeline: the timeline's own
                    time.sleep(0.4)
                    check(tab.ev(idle) is False, "a swipe down that starts on the timeline does not hide the bar")
                    until(tab, "__stub.state === 3")                              # (the timeline's seek is a jump of 0.4 s in the stand-in: the bar is called again when it ends)
                    time.sleep(0.8)
                    cx, cy = centre(tab, "clock")
                    touch(tab, [(cx, cy), (cx, cy + 15), (cx, cy + 30), (cx, cy + 50)])           # a swipe down on the bar
                    check(until(tab, idle, 3) is True, "a swipe down on the bar hides it, paused or not")
                    touch(tab, [(w / 2, h - 20)])
                    until(tab, "!" + idle, 3)
                    ux, uy = centre(tab, "b-tab")
                    touch(tab, [(ux, uy)])                                        # a tap on the tab
                    check(until(tab, idle, 3) is True, "a tap on the tab hides it")
                    tab.mouse("mouseMoved", 400, 100, button="none")
                    check(until(tab, "!" + idle, 3) is True and tab.ev(edge_up) is False, "a mouse that moves calls the bar as ever (and the strip is not for a mouse)")
                    tab.ev("__stub.state = 2")
                    check(until(tab, idle, 6) is True and tab.ev(edge_up) is False, "after a mouse the bar goes by itself and leaves no strip over the game's edge")
                    shot(tab, "fs-mouse")
                tab.close()
            # a phone upright in fullscreen: the tag at the bar's left end and the tab at its right end must not lie on each other, with a short tag (a replay, paused) and a long one (a live match)
            apart = ("(function () { var a = document.getElementById('rtag').getBoundingClientRect(), b = document.getElementById('b-tab').getBoundingClientRect();"
                     " return [a.left, a.right, b.left, b.right, window.innerWidth, a.right <= b.left || b.right <= a.left, b.left >= 0 && b.right <= window.innerWidth && a.left >= 0, getComputedStyle(document.getElementById('b-tab')).display]; })()")
            for label, address, wait_for in (("a phone upright, a replay paused", "/play.html?replay=" + KEPT, "Paused"), ("a phone upright, a live match", "/play.html?live=" + ID, "about")):
                fresh(kept_at=1.0) if address.find("replay=") != -1 else fresh()
                tab = new_tab(390, 844, 2, True)
                open_page(tab, address)
                until(tab, READY)
                time.sleep(1.0)
                if wait_for == "Paused":
                    tab.ev("__stub.state = 3")
                until(tab, "document.getElementById('rtag-s').textContent.indexOf('%s') !== -1" % wait_for, 6)
                fx, fy = centre(tab, "fullscreen-btn-r")
                tab.tap(fx, fy)
                check(until(tab, "document.getElementById('rbar').classList.contains('over')", 10) is True, "%s: the bar lies over the picture in fullscreen" % label)
                a = tab.ev(apart)
                check(a[7] != "none" and a[5] is True and a[6] is True, "%s: the tag (x %d to %d) and the tab (x %d to %d) lie apart, inside the %d px screen" % ((label, a[0], a[1], a[2], a[3], a[4])))
                shot(tab, "fs-phone-" + ("paused" if wait_for == "Paused" else "live"))
                tab.close()
        if args.only in ("", "scrub"):
            print("[web live] part scrub: a tap on the bar seeks to the spot, and a finger is never given the pointer lock")
            # A browser that holds the pointer lock reports a finger's touches at x = 0 (a desktop Chromium does, and a phone's Chrome may): the bar then took every tap for the start. A finger is
            # not locked; a mouse is, as ever. The lock is recorded here (the stand-in's canvas), not taken.
            seeks = "__stub.seeks.slice()"
            record = ("(function () { window.__lock = 0; window.__unlock = 0; var c = document.getElementById('canvas'); c.requestPointerLock = function () { window.__lock++; };"
                      " document.exitPointerLock = function () { window.__unlock++; }; })()")
            fresh(kept_at=1.0)
            tab = new_tab(844, 390, 2, True)
            open_page(tab, "/play.html?replay=" + KEPT)
            until(tab, READY)
            time.sleep(1.0)
            tab.ev(record)
            fx, fy = centre(tab, "fullscreen-btn-r")
            tab.tap(fx, fy)
            check(until(tab, "document.getElementById('rbar').classList.contains('over')", 10) is True, "a phone goes fullscreen")
            time.sleep(0.6)
            check(tab.ev("window.__lock") == 0, "a finger's tap on Fullscreen asks for no pointer lock (asked %r times)" % tab.ev("window.__lock"))
            tab.close()
            fresh(kept_at=1.0)
            tab = new_tab(1440, 900, 1, False)
            open_page(tab, "/play.html?replay=" + KEPT)
            until(tab, READY)
            time.sleep(1.0)
            tab.ev(record)
            fx, fy = centre(tab, "fullscreen-btn-r")
            tab.click(fx, fy)
            check(until(tab, "window.__lock >= 1", 10) is True, "a mouse's click on Fullscreen asks for the lock as ever")
            tab.close()
            fresh(kept_at=1.0)
            tab = new_tab(844, 390, 2, True)
            open_page(tab, "/play.html?replay=" + KEPT)
            until(tab, READY)
            time.sleep(1.0)
            tab.ev(record + "; Object.defineProperty(document, 'pointerLockElement', { configurable: true, get: function () { return document.getElementById('canvas'); } })")
            tx, ty = centre(tab, "tl")
            tab.tap(tx, ty)
            check(until(tab, "window.__unlock >= 1", 3) is True, "a finger that touches while the lock is held lets it go")
            tab.close()
            # The page's own fullscreen (a headless browser's element fullscreen takes no touch), with a lock that the stand-in holds: a mouse took it, and the bar lies in the picture's box.
            # (The taps at 25, 50 and 90 % pass on the page before the fix too: they show that a bar without a lock is read right. What fails before is the lock: the checks above, and the two below.)
            held = ("(function () { window.__locked = false; window.__unlock = 0; var c = document.getElementById('canvas');"
                    " Object.defineProperty(document, 'pointerLockElement', { configurable: true, get: function () { return window.__locked ? c : null; } });"
                    " document.exitPointerLock = function () { window.__unlock++; setTimeout(function () { window.__locked = false; window.dispatchEvent(new Event('pointerlockchange')); }, 20); };"
                    " window.__hold = function () { window.__locked = true; window.dispatchEvent(new Event('pointerlockchange')); }; })()")
            rail = "(function () { var r = document.getElementById('rail').getBoundingClientRect(); return [r.left, r.top, r.width, r.height]; })()"
            for what in ("rail", "zero"):
                fresh(kept_at=1.0)
                tab = new_tab(844, 390, 2, True)
                open_page(tab, "/play.html?replay=" + KEPT)
                until(tab, READY)
                time.sleep(1.0)
                tab.ev("Element.prototype.requestFullscreen = undefined; Element.prototype.webkitRequestFullscreen = undefined")      # (the page's own fullscreen)
                fx, fy = centre(tab, "fullscreen-btn-r")
                tab.tap(fx, fy)
                check(until(tab, "document.getElementById('rbar').classList.contains('over')", 10) is True, "fullscreen, the page's own")
                tab.ev(held)
                tab.ev("__hold()")
                time.sleep(0.4)
                check(tab.ev("document.getElementById('rbar').parentNode === document.getElementById('game-container')") is True, "a held lock: the bar lies in the picture's box")
                rx, ry, rw, rh = tab.ev(rail)
                tab.ev("__stub.seeks.length = 0")
                if what == "rail":                                        # a finger taps the bar while a mouse's lock is held: the lock goes (the bar moves back) in the middle of the tap
                    tab.tap(rx + rw / 2, ry + rh / 2)
                    check(until(tab, "__stub.seeks.length === 1", 3) is True and abs(tab.ev(seeks)[0] - 10030) <= 40, "a tap on the bar right after a held lock still seeks (got %r)" % tab.ev(seeks))
                    check(until(tab, "document.getElementById('rbar').parentNode === document.getElementById('game-stage')", 3) is True, "and the bar is back in the stage when the tap is over")
                else:                                                     # the locked browser's touch at x = 0 (and the lock cursor at 0, 0): not a tap on the start
                    tab.ev("(function () { var t = document.getElementById('tl'); ['pointerdown', 'pointerup'].forEach(function (n) { t.dispatchEvent(new PointerEvent(n, { bubbles: true, pointerId: 7, pointerType: 'touch', clientX: 0, clientY: 0 })); }); })()")
                    time.sleep(0.5)
                    check(tab.ev(seeks) == [] and tab.ev("window.__unlock") >= 1, "a finger reported at x = 0 under a held lock seeks nowhere (seeks %r) and lets the lock go" % tab.ev(seeks))
                    time.sleep(0.4)
                    rx, ry, rw, rh = tab.ev(rail)                           # (the bar went back to the stage)
                    tab.ev("__stub.seeks.length = 0")
                    tab.tap(rx + rw * 0.9, ry + rh / 2)
                    check(until(tab, "__stub.seeks.length === 1", 3) is True and abs(tab.ev(seeks)[0] - 18054) <= 40, "and the next tap lands (got %r)" % tab.ev(seeks))
                tab.close()
            tab = new_tab(844, 390, 2, True)
            open_page(tab, "/play.html?replay=" + KEPT)
            until(tab, READY)
            time.sleep(1.0)
            tab.ev("Element.prototype.requestFullscreen = undefined; Element.prototype.webkitRequestFullscreen = undefined")
            fx, fy = centre(tab, "fullscreen-btn-r")
            tab.tap(fx, fy)
            until(tab, "document.getElementById('rbar').classList.contains('over')", 10)
            time.sleep(0.6)
            for fraction in (0.25, 0.5, 0.9):
                rx, ry, rw, rh = tab.ev(rail)
                tab.ev("__stub.seeks.length = 0")
                tab.tap(rx + rw * fraction, ry + rh / 2)
                want = round(20060 * fraction)
                check(until(tab, "__stub.seeks.length === 1") is True and abs(tab.ev(seeks)[0] - want) <= 40, "fullscreen: a tap at %d%% of the bar seeks to turn about %d (got %r)" % (fraction * 100, want, tab.ev(seeks)))
                time.sleep(0.6)
            tab.close()
    except (RuntimeError, TimeoutError, ConnectionError) as e:
        check(False, "the browser or the page broke down: %s" % e)
    finally:
        for t in tabs:
            try:
                t.close()
            except (RuntimeError, TimeoutError):
                pass
        browser.close()
    print("[web live] %d checks, %d failed" % (count[0], len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
