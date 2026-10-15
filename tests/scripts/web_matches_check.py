#!/usr/bin/env python3
"""The pages of the match history in a REAL browser (opt-in; see tests/scripts/test_web_matches.sh and docs/REPLAYS.md "The match pages").

No Docker image, no game server, no game program: the check serves the pages of web/ itself with a FAKE DOOR (GET /history with the server's filters and its refusals, GET /history/<id>, GET /maps,
GET /live, GET /replays/<file>: the shapes of docs/SERVER.md "Replays" and docs/REPLAYS.md "The match history") and the stand-in for the game of tests/scripts/web_live_check.py, which it reuses. The door
sends the two cross-origin isolation headers that the site's nginx sends on every page, so the frame of the match page is tried under them. The DevTools protocol is spoken with the client of
web_hidden_check.py (standard library only); the browser has its own profile and port, and nothing of yours is touched.
Parts (--only): list (every match, the cards, the filters and the search with their requests and the way back, the pager, the words of a busy server and of no match, a phone), match (the stats of a
match, mode 187, a recording of an older version, one that is gone, a match that is not counted yet, a server that fails), frame (the replay: nothing of the game is asked for before "Show replay", then
one frame of the game page with ?embed=1 and nothing but the picture and the bar, taken away by "Hide replay" and by the game's own Quit), stats (the Stats of the replay page: a button, the panel, a
phone's row, and no stats for a live match, a frame or a match with no record).

Exit status 0: every check passed; 1: a check failed; 3: the check could not be made (no browser); 2 is the status of a bad command line.
"""
import argparse
import json
import os
import re
import sys
import time
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import web_live_check as live                                                # noqa: E402
from web_aspect_check import Browser, NotReachable, Tab                    # noqa: E402
from web_hidden_check import find_browser                                    # noqa: E402

HOSTILE = "<img src=x onerror=window.__pwn=1>"
NOW = 1792060772
MAPS = ["TINY.LVL", "SMALL.LVL", "MEDIUM.LVL", "GAUNTLET.LVL", "TREASURE.LVL", "ISLANDS.LVL"]
ID_PEOPLE = "ants-TREASURE-20261015-051557Z"
ID_OLD = "ants-TREASURE-20261009-202133Z"
ID_REMOVED = "ants-TINY-20260915-110334Z"
ID_187 = "ants-ISLANDS-20261014-103932Z"
ID_COMMUNITY = "ants-Pond-20261015-040000Z"
ID_UNCOUNTED = "ants-SMALL-20261015-111111Z"
ID_LEFT = "ants-SMALL-20261014-120000Z"
READY = live.READY


def seat(n, colour, name, bot, **kw):
    s = {"seat": n, "colour": colour, "name": name, "bot": bot, "score": 0, "killed": 0, "lost": 0, "hatched": 0, "bombs_planted": 0, "bombs_defused": 0, "fires_lit": 0, "left": False, "winner": False}
    s.update(kw)
    return s


def record(ident, ended, map_, fmt, seats, rows, **kw):
    r = {"v": 1, "id": ident, "ended": ended, "map": map_, "game": "v0.12.43", "turns": 14405, "seconds": 720, "finished": True, "quitter": None, "mode": "highest-score", "format": fmt, "draw": False, "result": "decided",
         "computers_only": not any(s["bot"] == "" for s in seats), "seats": seats, "rows": rows, "recording": "watch", "file": ident + ".antsrep", "keep_days": 30}
    r.update(kw)
    return r


def make_records():
    people = record(ID_PEOPLE, NOW - 100, "TREASURE.LVL", "2v2",
                    [seat(0, "Green", HOSTILE, "", score=140, killed=7, lost=4, bombs_planted=2, fires_lit=16), seat(1, "Red", "Bea", "", score=1010, killed=7, lost=5),
                     seat(2, "Blue", "Pip", "Hard", score=2040, killed=3, lost=4, bombs_defused=11, fires_lit=14, winner=True), seat(3, "Black", "Rex", "Hard", score=70, killed=2, lost=6, hatched=1, bombs_planted=37, fires_lit=9)],
                    [{"seats": [2], "score": 2040, "killed": 3, "lost": 4, "hatched": 0, "winner": True}, {"seats": [1], "score": 1010, "killed": 7, "lost": 5, "hatched": 0, "winner": False},
                     {"seats": [0, 3], "score": 210, "killed": 9, "lost": 10, "hatched": 1, "winner": False}])
    community = record(ID_COMMUNITY, NOW - 900, "Pond.lvl", "1v1", [seat(0, "Green", "Ann", "", score=900, killed=2, lost=1, winner=True), seat(2, "Blue", "Cy", "Easy", score=300, killed=1, lost=2)],
                       [{"seats": [0], "score": 900, "killed": 2, "lost": 1, "hatched": 0, "winner": True}, {"seats": [2], "score": 300, "killed": 1, "lost": 2, "hatched": 0, "winner": False}])
    old = record(ID_OLD, NOW - 5 * 86400, "TREASURE.LVL", "1v1", [seat(0, "Green", "Ann", "", score=2945, killed=6, lost=1, winner=True), seat(3, "Black", "", "Hard", score=720, killed=1, lost=6, hatched=1)],
                 [{"seats": [0], "score": 2945, "killed": 6, "lost": 1, "hatched": 0, "winner": True}, {"seats": [3], "score": 720, "killed": 1, "lost": 6, "hatched": 1, "winner": False}], game="v0.12.0", recording="old")
    removed = record(ID_REMOVED, NOW - 29 * 86400, "TINY.LVL", "1v1", [seat(0, "Green", "", "Medium", score=2020), seat(1, "Red", "", "Hard", score=2020)],
                     [{"seats": [0], "score": 2020, "killed": 0, "lost": 0, "hatched": 0, "winner": False}, {"seats": [1], "score": 2020, "killed": 0, "lost": 0, "hatched": 0, "winner": False}],
                     draw=True, result="draw", recording="removed")
    removed.pop("file")
    m187 = record(ID_187, NOW - 3 * 86400, "ISLANDS.LVL", "ffa3", [seat(0, "Green", "Ann", "", score=12, killed=12, lost=3, winner=True), seat(1, "Red", "Bea", "", score=5, killed=5, lost=4), seat(2, "Blue", "Cy", "", score=0, lost=7)],
                  [{"seats": [0], "score": 12, "killed": 12, "lost": 3, "hatched": 0, "winner": True}, {"seats": [1], "score": 5, "killed": 5, "lost": 4, "hatched": 0, "winner": False}, {"seats": [2], "score": 0, "killed": 0, "lost": 7, "hatched": 0, "winner": False}],
                  mode="187", seconds=300, turns=6000)
    left = record(ID_LEFT, NOW - 2 * 86400, "SMALL.LVL", "ffa4", [seat(0, "Green", "Ann", "", score=500, killed=2, lost=1, winner=True), seat(1, "Red", "Quin", "", score=360, killed=1, lost=2, left=True),
                                                                    seat(2, "Blue", "Cy", "Hard", score=300), seat(3, "Black", "Di", "Hard", score=200)],
                  [{"seats": [0], "score": 500, "killed": 2, "lost": 1, "hatched": 0, "winner": True}, {"seats": [2], "score": 300, "killed": 0, "lost": 0, "hatched": 0, "winner": False},
                   {"seats": [3], "score": 200, "killed": 0, "lost": 0, "hatched": 0, "winner": False}], result="cut", finished=False, seconds=200)
    out = [people, community, left]
    for i in range(54):                                   # computer matches, newest first after the first two, the other fixtures among them
        m = MAPS[i % 6]
        ident = "ants-%s-%s-%06dZ" % (m[:-4], time.strftime("%Y%m%d", time.gmtime(NOW - 3600 * (i + 2))), 100000 + i)
        fmt = ("1v1", "ffa3", "2v2", "ffa4", "2v1")[i % 5]
        count = {"1v1": 2, "ffa3": 3, "2v2": 4, "ffa4": 4, "2v1": 3}[fmt]
        seats = [seat(k, ("Green", "Red", "Blue", "Black")[k], "Bot%d" % k, ("Hard", "Medium")[k % 2], score=1000 + 50 * ((i * 7 + k * 3) % 20), killed=(i + k) % 5, lost=(i * 2 + k) % 6) for k in range(count)]
        best = max(seats, key=lambda s: s["score"])
        best["winner"] = True
        rows = [{"seats": [s["seat"]], "score": s["score"], "killed": s["killed"], "lost": s["lost"], "hatched": 0, "winner": s["winner"]} for s in sorted(seats, key=lambda s: -s["score"])]
        out.append(record(ident, NOW - 3600 * (i + 2), m, fmt, seats, rows, result="cut" if i % 11 == 5 else "decided", finished=i % 11 != 5))
    out += [old, removed, m187]
    out.sort(key=lambda r: (-r["ended"], r["id"]))
    return out


RECORDS = make_records()
BY_ID = {r["id"]: r for r in RECORDS}
SORTS = {"new": lambda r: (-r["ended"], r["id"]), "old": lambda r: (r["ended"], r["id"]), "long": lambda r: (-r["turns"], -r["ended"]), "short": lambda r: (r["turns"], -r["ended"]),
         "kills": lambda r: (-max(s["killed"] for s in r["seats"]), -r["ended"]), "score": lambda r: (-max(s["score"] for s in r["seats"]), -r["ended"]), "hatched": lambda r: (-max(s["hatched"] for s in r["seats"]), -r["ended"])}
WORDS = {"who": ("all", "people", "computers"), "format": ("all", "1v1", "team", "ffa"), "result": ("all", "decided", "draw", "cut"), "rec": ("all", "watch", "old", "removed"),
         "colour": ("all", "green", "red", "blue", "black"), "mode": ("all", "highest-score", "187"), "sort": tuple(SORTS)}


def label(s):
    return "%s (bot, %s)" % (s["name"], s["bot"]) if s["name"] and s["bot"] else "Bot (%s)" % s["bot"] if s["bot"] else s["name"] or s["colour"]


def listing(query):
    """What the server's GET /history makes of a query: its filters and its refusals (a key twice, a key or a word that is not one: 400)."""
    pairs = urllib.parse.parse_qsl(query, keep_blank_values=True)
    q = {}
    for k, v in pairs:
        if k in q:
            return 400, {"error": "the key " + k + " is there twice"}
        if k not in WORDS and k not in ("q", "limit", "offset"):
            return 400, {"error": "unknown key " + k}
        if k in WORDS and v not in WORDS[k]:
            return 400, {"error": "the word of " + k + " is not one"}
        q[k] = v
    rs = list(RECORDS)
    if q.get("who") == "people":
        rs = [r for r in rs if not r["computers_only"]]
    if q.get("who") == "computers":
        rs = [r for r in rs if r["computers_only"]]
    fmt = q.get("format", "all")
    if fmt != "all":
        rs = [r for r in rs if (r["format"] == "1v1" if fmt == "1v1" else r["format"] in ("2v1", "2v2") if fmt == "team" else r["format"].startswith("ffa"))]
    for key, field in (("result", "result"), ("rec", "recording"), ("mode", "mode")):
        if q.get(key, "all") != "all":
            rs = [r for r in rs if r[field] == q[key]]
    if q.get("colour", "all") != "all":
        rs = [r for r in rs if any(s["colour"].lower() == q["colour"] for s in r["seats"])]
    if q.get("q"):
        t = q["q"].lower()
        rs = [r for r in rs if t in r["map"].lower() or any(t in label(s).lower() for s in r["seats"])]
    rs.sort(key=SORTS[q.get("sort", "new")])
    limit, offset = min(100, int(q.get("limit", 50))), int(q.get("offset", 0))
    page = []
    for r in rs[offset:offset + limit]:
        e = {k: v for k, v in r.items() if k not in ("v", "keep_days")}
        e["seats"] = [{k: v for k, v in s.items() if k not in ("bombs_planted", "bombs_defused", "fires_lit")} for s in r["seats"]]
        page.append(e)
    return 200, {"history": page, "count": len(rs), "total": len(RECORDS), "offset": offset, "limit": limit, "keep_days": 30, "sim_rules": 5}


PNG = bytes.fromhex("89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4890000000d49444154789c63f8cfc0f01f0005000201a5f645400000000049454e44ae426082")


class MatchesDoor(live.Door):
    """The fake door of web_live_check.py with the match history added: the lists and the matches of RECORDS, the list of the maps, the pages, and the headers of the site."""

    def __init__(self):
        super().__init__()
        self.history = "ok"                              # ok | busy (503) | off (404 as a server with no history answers) | down (500)

    @staticmethod
    def send(h, code, body, ctype="text/plain"):
        if isinstance(body, str):
            body = body.encode()
        h.send_response(code)
        h.send_header("Content-Type", ctype)
        h.send_header("Content-Length", str(len(body)))
        h.send_header("Cache-Control", "no-store")
        h.send_header("Cross-Origin-Opener-Policy", "same-origin")              # (the site sends both on every answer: docker/nginx.conf)
        h.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        h.end_headers()
        h.wfile.write(body)

    def route(self, h, path):
        query = urllib.parse.urlparse(h.path).query
        if path in ("/matches.html", "/match.html"):
            text = open(os.path.join(live.WEB, path[1:]), encoding="utf-8").read()
            for k, v in live.FILLED.items():
                text = text.replace(k, v)
            return self.send(h, 200, text, "text/html; charset=utf-8")
        if path == "/maps":
            return self.js(h, 200, {"format": 1, "maps": [{"file": f, "width": 60, "height": 60, "minutes": 12, "players": 4, "description": "", "plays": 0, "thumb": "/front/preview_%s.png" % f[:-4].lower(), "original": True} for f in MAPS]
                                    + [{"file": "Pond.lvl", "width": 40, "height": 40, "minutes": 20, "players": 4, "description": "", "plays": 3, "thumb": "/maps/previews/Pond.webp"}]})
        if path == "/maps/previews/Pond.webp":
            return self.send(h, 200, PNG, "image/png")
        if path == "/history" or path.startswith("/history/"):
            if self.history == "busy":
                return self.js(h, 503, {"error": "the match history is busy: try again in a moment"})
            if self.history == "off":
                return self.js(h, 404, {"error": "this server keeps no match history"})
            if self.history == "down":
                return self.send(h, 500, "{}", "application/json")
            if path == "/history":
                code, body = listing(query)
                return self.js(h, code, body)
            ident = path[len("/history/"):]
            if query or ident not in BY_ID:
                return self.js(h, 404, {"error": "no such match"})
            return self.js(h, 200, BY_ID[ident])
        m = re.match(r"^/replays/(ants-[A-Za-z0-9_]{1,24}-[0-9]{8}-[0-9]{6}Z(?:-[0-9]{1,4})?\.antsrep)$", path)
        if m:
            return self.send(h, 200, live.fake_file(live.TURNS_END, 1, m.group(1)[:-len(".antsrep")]), "application/octet-stream")
        return super().route(h, path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--only", default="", help="one part: list, match, frame or stats (default: all)")
    ap.add_argument("--shots", default="", help="a folder to save screenshots in")
    args = ap.parse_args()
    parts = ("list", "match", "frame", "stats")
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

    door = MatchesDoor()
    try:
        browser = Browser(path)
    except NotReachable as e:
        print("  SKIP: " + str(e))
        return 3
    print("[web matches] the pages of web/ against a fake door and a stand-in for the game, in %s" % os.path.basename(path))
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

    def fresh(**kw):
        state = dict(mode="ok", freeze=True, live=[live.ID, live.OTHER], live_door=True, history="ok", names=live.PLAYERS, seconds=676)
        state.update(kw)
        door.set(**state)
        with door.lock:
            door.hits[:] = []

    until = live.until
    cards = "document.querySelectorAll('#body .m').length"

    def hits(prefix):                                    # the requests that the door saw: "/history" is the list (with or without a query), "/history/" the matches, "/maps" the list of maps and no picture of one
        with door.lock:
            seen = list(door.hits)
        if prefix == "/history":
            return [h for h in seen if h == "/history" or h.startswith("/history?")]
        if prefix == "/maps":
            return [h for h in seen if h == "/maps"]
        return [h for h in seen if h.startswith(prefix)]

    try:
        if args.only in ("", "list"):
            print("[web matches] part list: every match, the filters, the search, the pager")
            fresh()
            tab = new_tab()
            open_page(tab, "/matches.html")
            check(until(tab, cards + " === 25") is True, "the first page shows 25 matches")
            check(hits("/history") == ["/history"], "the first look is the list with no query at all (the newest 50, cached by the site): %s" % hits("/history"))
            check(tab.ev("document.getElementById('count').textContent") == "Showing 1 to 25 of %d matches, newest first" % len(RECORDS), "the line over the list: %r" % tab.ev("document.getElementById('count').textContent"))
            check(tab.ev("!window.__pwn && document.querySelectorAll('#body img[src=x]').length === 0 && document.getElementById('body').textContent.indexOf('<img src=x') !== -1"), "a name that is markup is shown as text: nothing of it ran, nothing of it became an element")
            first = tab.ev("(function () { var c = document.querySelector('#body .m'); return [c.querySelector('h3').textContent, [].map.call(c.querySelectorAll('.side'), function (s) { return s.textContent; }), c.querySelectorAll('.side.w .won').length, c.querySelector('a.go').getAttribute('href'), c.querySelector('a.go').textContent]; })()")
            check(first[0] == "Treasure" and first[2] == 1 and first[3] == "match.html?id=" + ID_PEOPLE and first[4] == "Stats & replay", "the newest card: map, one winner, a link to its match page (%s)" % first)
            check(first[1][0].endswith("Pip (bot, Hard)2,040Won") and first[1][1] == "Bea1,010" and first[1][2] == HOSTILE + " & Rex (bot, Hard)210", "the sides are the rows of the record, with bots as Name (bot, Level): %s" % first[1])
            check(until(tab, "document.querySelector('#body .m:nth-child(2) .tv img') && document.querySelector('#body .m:nth-child(2) .tv img').getAttribute('src') === '/maps/previews/Pond.webp'") is True and hits("/maps") == ["/maps"], "a map of the players has the picture of the server's list, which is asked for once")
            labels = tab.ev("[].map.call(document.querySelectorAll('#body .m .side-col .rec'), function (r) { return r.textContent; })")
            check(set(labels) == {"Replay"}, "every match of the first page can be watched: its mark says Replay (%s)" % sorted(set(labels)))
            door.set(live=[live.ID, live.OTHER])
            check(until(tab, "!document.getElementById('live-btn').hidden && document.getElementById('live-btn').textContent === '2 live now'") is True, "two matches are on: the button 'Live now' says so and goes to the live page (%s)" % tab.ev("document.getElementById('live-btn').getAttribute('href')"))
            shot(tab, "list")
            tab.ev("window.__keep = 1")
            # who played: the address and the request change, and the page is not loaded again
            with door.lock:
                door.hits[:] = []
            tab.ev("document.querySelectorAll('#who button')[1].click()")
            check(until(tab, "location.search === '?who=people' && document.getElementById('count').textContent.indexOf('of the ') !== -1") is True, "With people: the address is ?who=people and the line says how many fit: %r" % tab.ev("document.getElementById('count').textContent"))
            check(hits("/history") == ["/history?who=people&limit=25&offset=0"] and tab.ev("window.__keep") == 1, "... one request, in the server's words, and no new page: %s" % hits("/history"))
            want = len([r for r in RECORDS if not r["computers_only"]])
            check(tab.ev(cards) == min(25, want) and tab.ev("document.querySelector('#fchips').hidden") is False, "... %d matches with people, and a chip for the filter" % want)
            # a filter, by the select
            with door.lock:
                door.hits[:] = []
            tab.ev("(function () { var s = document.getElementById('s-format'); s.value = 'ffa'; s.dispatchEvent(new Event('change')); })()")
            check(until(tab, "location.search === '?who=people&format=ffa'") is True and until(tab, "document.querySelectorAll('#body .m').length > 0 && [].every.call(document.querySelectorAll('#body .m .pill.fmt'), function (p, i) { return i % 1 === 0; })") is True, "the Format select adds its word to the address")
            check(hits("/history") == ["/history?who=people&format=ffa&limit=25&offset=0"], "... and to the request: %s" % hits("/history"))
            # the search: keys that come quickly are one request, after a pause
            with door.lock:
                door.hits[:] = []
            for text in ("z", "zz", "zzz"):
                tab.ev("(function () { var q = document.getElementById('q'); q.value = %s; q.dispatchEvent(new Event('input')); })()" % json.dumps(text))
                time.sleep(0.1)
            time.sleep(1.2)
            check(hits("/history") == ["/history?who=people&format=ffa&q=zzz&limit=25&offset=0"], "three keys within a third of a second are one request, after the pause: %s" % hits("/history"))
            check(until(tab, "document.getElementById('body').textContent.indexOf('No match fits those filters.') !== -1 && " + cards + " === 0") is True, "a search that nothing fits says so")
            tab.ev("document.querySelector('#body .note button').click()")
            check(until(tab, "location.search === '' && " + cards + " === 25") is True and tab.ev("document.getElementById('q').value") == "", "'Clear the filters' goes back to the plain list")
            # Back and Forward move between the views
            tab.ev("history.back()")
            check(until(tab, "location.search === '?who=people&format=ffa&q=zzz'") is True, "Back goes to the view before (the address is the state: %s)" % tab.ev("location.search"))
            check(tab.ev("window.__keep") == 1, "... and the page is still the same page")
            # the pager
            fresh()
            tab.close()
            tab = new_tab()
            open_page(tab, "/matches.html?page=2")
            check(until(tab, cards + " === 25") is True and hits("/history") == ["/history"] and tab.ev("document.getElementById('count').textContent").startswith("Showing 26 to 50 of"), "page 2 of the plain view is the second half of the same plain list: no other request (%s)" % hits("/history"))
            open_page(tab, "/matches.html?page=3")
            check(until(tab, "document.getElementById('count').textContent.indexOf('Showing 51 to') === 0") is True and hits("/history")[-1] == "/history?limit=25&offset=50", "page 3 is one query with its offset: %s" % hits("/history")[-1:])
            check(tab.ev("[].map.call(document.querySelectorAll('#pager .pg'), function (p) { return p.textContent; })") == ["‹ Newer", "1", "2", "3", "Older ›"], "the pager: %s" % tab.ev("[].map.call(document.querySelectorAll('#pager .pg'), function (p) { return p.textContent; })"))
            check(tab.ev("document.querySelector('#pager a.wide:first-child').getAttribute('href')") == "matches.html?page=2" and tab.ev("document.querySelector('#pager .wide:last-child').tagName") == "SPAN", "the last page has Newer, and Older is no link")
            tab.ev("document.querySelector('#pager a.wide:first-child').click()")
            check(until(tab, "location.search === '?page=2' && document.getElementById('count').textContent.indexOf('Showing 26 to 50') === 0") is True, "Newer goes back a page, without loading the page again")
            # a busy server, and the way out
            fresh(history="busy")
            tab.close()
            tab = new_tab()
            open_page(tab, "/matches.html?who=people")
            check(until(tab, "document.getElementById('body').textContent.indexOf('the match history is busy: try again in a moment') !== -1") is True and tab.ev("!!document.querySelector('#body .note button')"), "a busy server: its own words, as text, and a button to try again")
            door.set(history="ok")
            tab.ev("document.querySelector('#body .note button').click()")
            check(until(tab, cards + " > 0") is True, "'Try again' asks again and the list comes")
            door.set(history="off")
            tab.ev("document.querySelectorAll('#who button')[2].click()")
            check(until(tab, "document.getElementById('body').textContent.indexOf('this server keeps no match history') !== -1") is True, "a server with no history: its words")
            tab.close()
            # a phone
            fresh()
            tab = new_tab(390, 844, 2, True)
            open_page(tab, "/matches.html")
            until(tab, cards + " === 25")
            check(tab.ev("document.documentElement.scrollWidth <= window.innerWidth") is True, "a phone: no sideways scroll")
            check(tab.ev("getComputedStyle(document.getElementById('s-format').parentNode).display") == "none" and tab.ev("getComputedStyle(document.getElementById('s-sort').parentNode).display") != "none", "a phone: the sort is there, the other filters wait behind 'Filters'")
            tab.ev("document.querySelector('.filt-btn').click()")
            check(tab.ev("getComputedStyle(document.getElementById('s-format').parentNode).display") != "none" and tab.ev("document.documentElement.scrollWidth <= window.innerWidth") is True, "'Filters' opens them, still inside the window")
            shot(tab, "list-phone")
            tab.close()

        if args.only in ("", "match"):
            print("[web matches] part match: one match, with its stats")
            fresh()
            tab = new_tab()
            open_page(tab, "/match.html?id=" + ID_PEOPLE)
            check(until(tab, "!!document.getElementById('board')") is True, "the match page has its sections")
            check(tab.ev("[].map.call(document.querySelectorAll('#sections > section'), function (s) { return s.id; })") == ["result", "replay", "board", "about"], "the sections, in order: result, replay, scoreboard, about (%s)" % tab.ev("[].map.call(document.querySelectorAll('#sections > section'), function (s) { return s.id; })"))
            check(tab.ev("document.getElementById('replay').hidden") is True and tab.ev("document.getElementById('show').textContent").strip().endswith("Show replay"), "the replay is closed, its button says Show replay")
            check(tab.ev("document.querySelector('#result .verdict').textContent") == "Pip (bot, Hard) won2,040  –  1,010  –  210", "who won, and the sides' scores: %r" % tab.ev("document.querySelector('#result .verdict').textContent"))
            rows = tab.ev("[].map.call(document.querySelectorAll('#board .tbl tbody tr'), function (r) { return [].map.call(r.children, function (c) { return c.textContent; }).join('|'); })")
            check(rows[0] == "Pip (bot, Hard)Won|2,040|3|4|0|0|11|14" and rows[1] == "Bea|1,010|7|5|0|0|0|0" and rows[3].startswith("Rex (bot, Hard)|70|2|6|1|37|0|9") and rows[4] == "Side total|210|9|10|1|39|0|25",
                  "the scoreboard has every number of every seat, and the total of a side of two: %s" % rows)
            check(tab.ev("!window.__pwn && document.querySelectorAll('img[src=x]').length === 0 && document.getElementById('board').textContent.indexOf('<img src=x') !== -1"), "a name that is markup is shown as text")
            check(tab.ev("document.querySelector('#about').textContent").find("2 people, 2 computer players") != -1 and "60 × 60, 12 minutes" in tab.ev("document.getElementById('fact-map').textContent"), "about: who played and the size of the map (from the server's list)")
            check(tab.ev("document.querySelector('#result a.dl').getAttribute('href')") == "/replays/" + ID_PEOPLE + ".antsrep", "the recording can be downloaded")
            check(until(tab, "document.title.indexOf('Treasure, 2 v 2') === 0") is True, "the title of the tab names the match")
            shot(tab, "match")
            tab.close()
            tab = new_tab()
            open_page(tab, "/match.html?id=" + ID_187)
            until(tab, "!!document.getElementById('board')")
            check(tab.ev("[].map.call(document.querySelectorAll('#board .tbl thead th'), function (c) { return c.textContent; })")[:2] == ["Player", "Killed"] and tab.ev("document.querySelector('#result .verdict').textContent").startswith("Ann won12 kills"), "mode 187: no Score column, the sides' numbers are kills (%s)" % tab.ev("document.querySelector('#result .verdict').textContent"))
            tab.close()
            tab = new_tab()
            open_page(tab, "/match.html?id=" + ID_LEFT)
            until(tab, "!!document.getElementById('board')")
            rows = tab.ev("[].map.call(document.querySelectorAll('#board .tbl tbody tr'), function (r) { return r.children[0].textContent; })")
            check(rows == ["AnnWon", "Cy (bot, Hard)", "Di (bot, Hard)", "QuinLeft early"], "a seat that left has no row in the record: it is last in the scoreboard, marked Left early (%s)" % rows)
            check(tab.ev("document.querySelector('#result .verdict').textContent") == "Ann won500  \u2013  300  \u2013  200" and tab.ev("document.querySelectorAll('#board .share li').length") == 3, "... and it is no part of the verdict or of the share: %r" % tab.ev("document.querySelector('#result .verdict').textContent"))
            tab.close()
            tab = new_tab()
            open_page(tab, "/matches.html?q=quin")
            check(until(tab, cards + " === 1") is True and tab.ev("document.querySelector('#body .m .side .left').textContent") == "Left", "the card of that match shows who left (with no score)")
            tab.close()
            tab = new_tab()
            open_page(tab, "/match.html?id=" + ID_OLD)
            until(tab, "!!document.getElementById('board')")
            check(tab.ev("document.getElementById('show')") is None and tab.ev("document.getElementById('replay')") is None and "older version (v0.12.0)" in tab.ev("document.querySelector('#result .why').textContent"), "a recording of an older version: no button, the words say who made it")
            check(tab.ev("document.querySelector('#result a.dl').getAttribute('href')") == "/replays/" + ID_OLD + ".antsrep" and "Bot (Hard)" in tab.ev("document.getElementById('board').textContent"), "... the file can be downloaded, and a bot that an old record stored with no name is Bot (Hard)")
            tab.close()
            tab = new_tab()
            open_page(tab, "/match.html?id=" + ID_REMOVED)
            until(tab, "!!document.getElementById('board')")
            check(tab.ev("document.getElementById('show')") is None and tab.ev("document.querySelector('#result a.dl')") is None and "Replay removed" in tab.ev("document.querySelector('#result .why').textContent") and tab.ev("document.querySelector('#result .verdict').textContent").startswith("A draw"),
                  "a recording that is gone: the stats stay, the words say so, nothing to download; a draw has no winner")
            tab.close()
            tab = new_tab()
            open_page(tab, "/match.html?id=" + ID_UNCOUNTED)
            check(until(tab, "document.getElementById('note') && document.getElementById('note').textContent.indexOf('not in the history') !== -1") is True and tab.ev("[].map.call(document.querySelectorAll('#note a'), function (a) { return a.getAttribute('href'); })") == ["/play.html?replay=" + ID_UNCOUNTED + ".antsrep", "/matches.html"],
                  "a match that is not counted yet: the page says so, and offers its replay and the list")
            tab.close()
            for bad in ("/match.html", "/match.html?id=../etc/passwd", "/match.html?id=x"):
                fresh()
                tab = new_tab()
                open_page(tab, bad)
                check(until(tab, "document.getElementById('note') && document.getElementById('note').textContent.indexOf('not the address of a match') !== -1") is True and not hits("/history/"), "%s is no match: no request is made" % bad)
                tab.close()
            fresh(history="busy")
            tab = new_tab()
            open_page(tab, "/match.html?id=" + ID_PEOPLE)
            check(until(tab, "document.getElementById('note') && document.getElementById('note').textContent.indexOf('the match history is busy') !== -1") is True, "a busy server: its words and a button to try again")
            tab.close()
            fresh()
            tab = new_tab(390, 844, 2, True)
            open_page(tab, "/match.html?id=" + ID_PEOPLE)
            until(tab, "!!document.getElementById('board')")
            check(tab.ev("document.documentElement.scrollWidth <= window.innerWidth") is True and tab.ev("getComputedStyle(document.querySelector('#board .tbl')).display") == "none" and tab.ev("document.querySelectorAll('#board .sc-card').length") == 5, "a phone: no sideways scroll, a card for each seat and each side of two instead of the table")
            shot(tab, "match-phone")
            tab.close()

        if args.only in ("", "frame"):
            print("[web matches] part frame: the replay, only when it is asked for")
            fresh()
            tab = new_tab()
            open_page(tab, "/match.html?id=" + ID_PEOPLE)
            until(tab, "!!document.getElementById('board')")
            time.sleep(1.0)
            asked = [h for h in door.hits if not h.startswith(("/history", "/live", "/maps", "/front/", "/matches.css", "/history_page.js", "/replay_page.js", "/match.html", "/favicon"))]
            check(asked == [], "before the button: nothing of the game and no recording is asked for (%s)" % asked)
            check(tab.ev("document.querySelectorAll('iframe').length") == 0, "... and there is no frame")
            tab.ev("document.getElementById('show').click()")
            check(until(tab, "document.querySelectorAll('iframe').length === 1 && !document.getElementById('replay').hidden") is True, "Show replay: one frame, the replay section is open")
            check(tab.ev("document.querySelector('iframe').getAttribute('src')") == "/play.html?replay=" + ID_PEOPLE + ".antsrep&embed=1" and tab.ev("document.querySelector('iframe').getAttribute('allow')") == "fullscreen; autoplay",
                  "... the frame is the game page of this recording in embed mode (%s)" % tab.ev("document.querySelector('iframe').getAttribute('src')"))
            check(until(tab, "(function () { var f = document.querySelector('iframe'); try { return !!f.contentWindow.__stub && f.contentWindow.__stub.total > 0; } catch (e) { return false; } })()", 20) is True, "the game started inside the frame under the site's cross-origin headers")
            check(any(h == "/replays/" + ID_PEOPLE + ".antsrep" for h in door.hits), "... it fetched the recording")
            check(tab.ev("(function () { var d = document.querySelector('iframe').contentDocument; return [d.body.classList.contains('embed'), getComputedStyle(d.querySelector('header')).display, getComputedStyle(d.querySelector('footer')).display, d.getElementById('stats-btn') ? getComputedStyle(d.getElementById('stats-btn')).display : 'none', getComputedStyle(d.getElementById('rbar')).display !== 'none']; })()") == [True, "none", "none", "none", True],
                  "... in the frame only the picture and the bar are left: %s" % tab.ev("(function () { var d = document.querySelector('iframe').contentDocument; return [d.body.classList.contains('embed'), getComputedStyle(d.querySelector('header')).display, getComputedStyle(d.querySelector('footer')).display]; })()"))
            check(not any(h.startswith("/history/") and h != "/history/" + ID_PEOPLE for h in door.hits) and sum(1 for h in door.hits if h == "/history/" + ID_PEOPLE) == 1, "... and the frame did not ask for the stats again (the match page has them)")
            check(tab.ev("document.getElementById('show').textContent") == "Hide replay" and tab.ev("document.getElementById('show').getAttribute('aria-expanded')") == "true", "the button says Hide replay")
            shot(tab, "frame")
            tab.ev("document.getElementById('show').click()")
            check(until(tab, "document.querySelectorAll('iframe').length === 0 && document.getElementById('replay').hidden") is True, "Hide replay: the frame is gone and the section is closed")
            tab.ev("document.getElementById('show').click()")
            check(until(tab, "document.querySelectorAll('iframe').length === 1") is True, "Show replay again: one frame, not two")
            until(tab, "(function () { var f = document.querySelector('iframe'); try { return !!f.contentWindow.__stub && f.contentWindow.__stub.total > 0 && !!f.contentWindow.antsReplayLeave; } catch (e) { return false; } })()", 20)
            tab.ev("document.querySelector('iframe').contentWindow.antsReplayLeave()")
            check(until(tab, "document.querySelectorAll('iframe').length === 0 && document.getElementById('replay').hidden") is True and tab.ev("location.pathname") == "/match.html", "the game's own Quit inside the frame closes the replay and leaves the page where it is (it does not load the list into the frame)")
            tab.close()
            tab = new_tab(390, 844, 2, True)
            open_page(tab, "/match.html?id=" + ID_PEOPLE)
            until(tab, "!!document.getElementById('board')")
            tab.ev("document.getElementById('show').click()")
            until(tab, "(function () { var f = document.querySelector('iframe'); try { return !!f.contentWindow.__stub && f.contentWindow.__stub.total > 0; } catch (e) { return false; } })()", 20)
            time.sleep(1.0)
            check(tab.ev("document.documentElement.scrollWidth <= window.innerWidth") is True and tab.ev("(function () { var r = document.querySelector('iframe').getBoundingClientRect(); return r.left >= 0 && r.right <= window.innerWidth; })()") is True, "a phone: the frame fits the window")
            shot(tab, "frame-phone")
            tab.close()

        if args.only in ("", "stats"):
            print("[web matches] part stats: the Stats of the replay page")
            fresh()
            tab = new_tab()
            open_page(tab, "/play.html?replay=" + ID_PEOPLE + ".antsrep")
            until(tab, READY)
            check(until(tab, "!document.getElementById('stats-btn').hidden") is True and hits("/history/") == ["/history/" + ID_PEOPLE], "a replay of a match that has a record: the Stats button, after one look at the record (%s)" % hits("/history/"))
            check(tab.ev("document.getElementById('mstats').hidden") is True, "the panel is closed at first (no spoiler)")
            tab.ev("document.getElementById('stats-btn').click()")
            check(until(tab, "(" + live.SHOWN + ")('mstats')") is True and tab.ev("document.getElementById('stats-btn').getAttribute('aria-expanded')") == "true", "the button opens the panel")
            text = tab.ev("document.getElementById('mstats').textContent")
            check("Pip (bot, Hard)" in text and "2,040" in text and "Side total" in text and "Bombs set 37" in text and "The final numbers of the match." in text, "it holds the winner, the numbers and the total of a side (%r)" % text[:160])
            check(tab.ev("!window.__pwn && document.querySelectorAll('#mstats img[src=x]').length === 0 && document.getElementById('mstats').textContent.indexOf('<img src=x') !== -1"), "a name that is markup is text")
            check(tab.ev("document.querySelector('#mstats a').getAttribute('href')") == "/match.html?id=" + ID_PEOPLE, "it links to the match page")
            shot(tab, "stats")
            tab.ev("document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' }))")
            check(until(tab, "document.getElementById('mstats').hidden") is True, "Esc closes it")
            tab.close()
            tab = new_tab(390, 844, 2, True)
            open_page(tab, "/play.html?replay=" + ID_PEOPLE + ".antsrep")
            until(tab, READY)
            check(until(tab, "!document.getElementById('mstats-flow').hidden") is True and tab.ev("(" + live.SHOWN + ")('mstats')") is False and tab.ev("(" + live.SHOWN + ")('stats-btn')") is False, "a phone: the row under the controls, not the button and not the panel")
            check(tab.ev("document.querySelector('#mstats-flow .mbody') && getComputedStyle(document.querySelector('#mstats-flow .mbody')).display") == "none" and "Pip (bot, Hard) won with 2,040" in tab.ev("document.querySelector('#mstats-flow .mt').textContent"), "... closed, with who won on its line")
            tab.ev("document.querySelector('#mstats-flow .mt').click()")
            check(tab.ev("getComputedStyle(document.querySelector('#mstats-flow .mbody')).display") != "none" and tab.ev("document.documentElement.scrollWidth <= window.innerWidth") is True, "... it opens in the page, inside the window")
            shot(tab, "stats-phone")
            tab.close()
            # a seat that left: marked in the panel, and not in the verdict
            fresh()
            tab = new_tab()
            open_page(tab, "/play.html?replay=" + ID_LEFT + ".antsrep")
            until(tab, READY)
            until(tab, "!document.getElementById('stats-btn').hidden")
            tab.ev("document.getElementById('stats-btn').click()")
            check(until(tab, "(" + live.SHOWN + ")('mstats')") is True and tab.ev("document.querySelectorAll('#mstats .nm .left').length") == 1 and tab.ev("document.querySelector('#mstats .mv').textContent") == "Ann won500 \u2013 300 \u2013 200", "a seat that left: marked in the panel, and no part of the verdict (%r)" % tab.ev("document.querySelector('#mstats .mv').textContent"))
            tab.close()
            # no record, no stats
            for address, what in (("/play.html?live=" + live.ID, "a live match"), ("/play.html?replay=" + ID_PEOPLE + ".antsrep&embed=1", "the frame of a match page"), ("/play.html?replay=ants-TREASURE-20261001-000000Z.antsrep", "a match that has no record")):
                fresh()
                tab = new_tab()
                open_page(tab, address)
                until(tab, READY if "ants-TREASURE-2026100" not in address else "!!window.__stub", 10)
                time.sleep(1.5)
                check(tab.ev("document.getElementById('stats-btn').hidden") is True and (tab.ev("(" + live.SHOWN + ")('mstats-flow')") is False), "%s: no Stats" % what)
                if "live=" in address or "embed=1" in address:
                    check(not hits("/history/"), "%s: the record is not even asked for" % what)
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
    print("[web matches] %d checks, %d failed" % (count[0], len(failures)))
    for f in failures:
        print("  FAILED: " + f)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
