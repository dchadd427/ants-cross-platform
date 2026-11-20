#!/usr/bin/env python3
"""The update notice and the "What's new" window in a REAL browser (opt-in; see tests/scripts/test_web_update_notice.sh and docs/SITE_NAV.md "The update notice").

No Docker image, no game server, no game program: the check serves the pages of web/ itself, each stamped with a made-up version, a made-up /whats_new.json that it changes between the parts, the changelog page
made by tools/changelog_to_html.py, and for the game page the stand-in of tests/scripts/web_live_check.py (a recording that plays and a live match that does not). The DevTools protocol is spoken with the client of
web_hidden_check.py (standard library only); the browser has its own profile and port, nothing of yours is touched, and it is closed at the end (and when the check fails). What it shows is the page's own part
that the node check (tests/scripts/web_update_notice_check.js) cannot: the strip is where it belongs on every page, at 1000 x 700 and at 390 x 844 on a touch screen (under the bar, first among the front page's
strips, nothing under it overlapped, no sideways scroll, buttons a finger can press), Reload reloads, Later keeps it away in the tab and after a reload, the room's note shows in a room only, the game page's strip is
there for a recording and not for a live match and goes while a match runs; and the window opens over each page, fits the screen, folds the older releases and opens one at a press, scrolls inside itself, and
closes with Close and with Escape.

Exit status 0: every check passed; 1: a check failed; 3: the check could not be made (no browser); 2 is the status of a bad command line.
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import web_live_check as live                                                # noqa: E402
from web_aspect_check import Browser, NotReachable, Tab                    # noqa: E402
from web_hidden_check import find_browser                                    # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools"))
import changelog_to_html                                                      # noqa: E402

WEB = os.path.join(REPO, "web")
OWN = "v1.2.0"                                       # (the version that every page is stamped with)
STAMPS = {"@@SITE_TITLE@@": "", "@@SITE_FOOTER@@": "", "@@GAME_VERSION@@": OWN, "@@BUILD_ID@@": "abc1234", "@@BUILD_DATE@@": "2026-01-02 09:30 PT", "@@BUILD_TIMESTAMP@@": "0", "@@DATA_SIZE@@": "2000"}
TYPES = {".html": "text/html; charset=utf-8", ".js": "text/javascript", ".css": "text/css", ".png": "image/png", ".ttf": "font/ttf", ".json": "application/json", ".ico": "image/x-icon"}
PAGES = {"/": "lobby.html", "/matches": "matches.html", "/match": "match.html", "/legal": "legal.html", "/mapmaker": "mapmaker.html", "/admin": "admin.html"}


def line(text, *bold):
    return [["b", bold[0]], ["t", " " + text]] if bold else [["t", text]]


def release(version, date, n):
    return {"version": version, "date": date, "items": [line("It does something.", "Thing %s one." % version)] + [line("Another line %d of %s." % (k, version)) for k in range(n - 1)], "fixes": [line("A fix in %s." % version)]}


NEWER = {"version": "1.3.0", "releases": [release("1.3.0", "2026-01-03", 2), release("1.2.0", "2026-01-02", 3), release("1.1.0", "2026-01-01", 2), release("1.0.0", "2025-12-30", 4)]}
SAME = {"version": "1.2.0", "releases": NEWER["releases"][1:]}


class NoticeDoor(live.Door):
    """The fake door of web_live_check with the pages of web/ stamped with OWN and the data file of the update notice, which a part sets (None: a 404)."""

    def __init__(self):
        self.data = None
        super().__init__()

    def page_text(self, path):
        if path == "/changelog":
            with open(os.path.join(REPO, "CHANGELOG.md"), encoding="utf-8") as f:
                return changelog_to_html.convert(f.read(), changelog_to_html.DEFAULT_REPO_BLOB, version=OWN, build_id="abc1234", build_date="2026-01-02 09:30 PT")
        if path == "/asset_catalog/":
            with open(os.path.join(REPO, "asset_catalog", "index.html"), encoding="utf-8") as f:
                return f.read()
        with open(os.path.join(WEB, PAGES[path]), encoding="utf-8") as f:
            text = f.read()
        for k, v in STAMPS.items():
            text = text.replace(k, v)
        return text

    def route(self, h, path):
        if path in PAGES or path in ("/changelog", "/asset_catalog/"):
            return self.send(h, 200, self.page_text(path), "text/html; charset=utf-8")
        if path == "/__blank":
            return self.send(h, 200, "<!doctype html><title>blank</title>", "text/html")
        if path == "/whats_new.json":
            return self.send(h, 404, "not found") if self.data is None else self.send(h, 200, json.dumps(self.data), "application/json")
        if path != "/play":
            f = os.path.join(WEB, path.lstrip("/"))
            if os.path.isfile(f) and not path.startswith("/index."):
                with open(f, "rb") as fh:
                    return self.send(h, 200, fh.read(), TYPES.get(os.path.splitext(f)[1], "application/octet-stream"))
            if path == "/live":
                return self.js(h, 200, {"live": [], "count": 0})
        return super().route(h, path)


LOOK = """(function (sel) {
    var s = document.querySelector(sel), de = document.documentElement, bar = document.querySelector('header.sb');
    if (!s) return null;
    var r = s.getBoundingClientRect(), b = bar ? bar.getBoundingClientRect() : null;
    var rect = function (e) { var q = e.getBoundingClientRect(); return [Math.round(q.left), Math.round(q.top), Math.round(q.right), Math.round(q.bottom)]; };
    return { rect: rect(s), bar: b ? rect(bar) : null, hscroll: de.scrollWidth > innerWidth + 1, iw: innerWidth, ih: innerHeight, parent: s.parentNode.className || s.parentNode.tagName, first: s.parentNode.firstElementChild === s,
             previous: s.previousElementSibling ? s.previousElementSibling.tagName : '', buttons: [].map.call(s.querySelectorAll('button'), function (x) { var q = x.getBoundingClientRect(); return [x.textContent, Math.round(q.width), Math.round(q.height), Math.round(q.left), Math.round(q.right)]; }),
             text: s.innerText };
})"""


def until(tab, expression, seconds=8.0, step=0.2):
    deadline = time.time() + seconds
    value = None
    while time.time() < deadline:
        try:
            value = tab.ev(expression)
        except (RuntimeError, TimeoutError):
            value = None
        if value:
            return value
        time.sleep(step)
    return value


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--only", default="", help="one part: strip, window or game (default: all)")
    ap.add_argument("--shots", default="", help="a folder to save screenshots in")
    args = ap.parse_args()
    parts = ("strip", "window", "game")
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

    door = NoticeDoor()
    try:
        browser = Browser(path)
    except NotReachable as e:
        print("  SKIP: " + str(e))
        return 3
    print("[web update notice] the pages of web/ against a made-up data file, in %s" % os.path.basename(path))
    tabs = []
    try:
        def new_tab(width, height, dpr=1, mobile=False):
            tab = Tab(browser)
            tab.emulate(width, height, dpr, mobile)
            tabs.append(tab)
            return tab

        def open_page(tab, address, seen=None):
            tab.call("Page.navigate", {"url": door.url("/__blank")})        # (the page's own address space: the storage that the next page starts with)
            until(tab, "document.title === 'blank'")
            tab.ev("try { sessionStorage.clear(); localStorage.clear(); %s } catch (e) {}" % ("localStorage.setItem('ants.seenVersion', %s);" % json.dumps(seen) if seen else ""))
            tab.call("Page.navigate", {"url": door.url(address)})

        def look(tab, selector=".upd-strip"):
            raw = tab.ev("JSON.stringify((%s)(%s))" % (LOOK, json.dumps(selector)))
            return json.loads(raw) if raw and raw != "null" else None

        windows = [("desk", 1000, 700, 1, False), ("phone", 390, 844, 2, True)]
        sites = [("front page", "/"), ("Matches", "/matches"), ("Match", "/match?id=ants-TINY-20261015-051557Z"), ("Legal", "/legal"), ("changelog", "/changelog"), ("Map maker", "/mapmaker"), ("admin page", "/admin")]

        if args.only in ("", "strip"):
            for label, width, height, dpr, mobile in windows:
                tab = new_tab(width, height, dpr, mobile)
                for name, address in sites:
                    door.data = NEWER
                    open_page(tab, address)
                    came = until(tab, "!!document.querySelector('.upd-strip')", 8)
                    where = "%s, %s" % (name, label)
                    check(bool(came), "%s: a newer version is out and the strip comes" % where)
                    if not came:
                        continue
                    s = look(tab)
                    check(not s["hscroll"] and s["rect"][0] >= 10 and s["rect"][2] <= s["iw"] - 10, "%s: the strip is inside the window, not up against its edge, and the page does not scroll sideways %s" % (where, s["rect"]))
                    if name == "front page":
                        check(s["first"] and "strips" in s["parent"], "%s: the strip is the first of the front page's strips" % where)
                    else:
                        check(s["previous"] == "HEADER" and s["bar"] is not None and s["rect"][1] >= s["bar"][3] - 1, "%s: it is right under the bar and covers nothing of it %s %s" % (where, s["rect"], s["bar"]))
                    buttons = {b[0]: b for b in s["buttons"]}
                    check(sorted(buttons) == ["Later", "Reload"] and all(b[2] >= 38 and b[1] >= 44 and b[4] <= s["iw"] and b[3] >= 0 for b in buttons.values()), "%s: Reload and Later are there, a finger wide and tall, inside the window %s" % (where, s["buttons"]))
                    check("new version" in s["text"].lower(), "%s: it says so (%r)" % (where, s["text"][:60]))
                    if name == "front page" and label == "desk":
                        tab.save_shot(args.shots, "strip_front_page_desk")
                        check(not tab.ev("(function(){ var n = document.querySelector('.upd-strip .u-room'); return !!n && getComputedStyle(n).display !== 'none'; })()"), "front page: the note about the seat is not shown on Home")
                        tab.ev("document.body.setAttribute('data-screen', 'room')")
                        check(bool(tab.ev("(function(){ var n = document.querySelector('.upd-strip .u-room'); return !!n && getComputedStyle(n).display !== 'none' && /seat/.test(n.textContent); })()")), "front page: in a room the strip says that a reload keeps the seat")
                        tab.ev("document.body.setAttribute('data-screen', 'home')")
                    if name == "Matches":
                        tab.save_shot(args.shots, "strip_matches_" + label)
                        tab.ev("window.__marker = 1")
                        tab.ev("document.querySelectorAll('.upd-strip button')[0].click()")
                        time.sleep(0.2)
                        check(bool(until(tab, "!window.__marker && performance.getEntriesByType('navigation')[0].type === 'reload'", 6)), "Matches, %s: Reload reloads the page" % label)
                        until(tab, "!!document.querySelector('.upd-strip')", 8)
                        tab.ev("document.querySelectorAll('.upd-strip button')[1].click()")
                        check(not tab.ev("!!document.querySelector('.upd-strip')"), "Matches, %s: Later takes the strip away" % label)
                        tab.call("Page.navigate", {"url": door.url("/legal")})
                        time.sleep(4.5)
                        check(not tab.ev("!!document.querySelector('.upd-strip')"), "Legal, %s: and it stays away on the next page of the tab" % label)
                door.data = SAME
                open_page(tab, "/matches")
                time.sleep(4.5)
                check(not tab.ev("!!document.querySelector('.upd-strip')"), "%s: the same version is live: no strip" % label)
                door.data = None
                open_page(tab, "/matches")
                time.sleep(4.5)
                check(not tab.ev("!!document.querySelector('.upd-strip') || !!document.querySelector('.upd-win')"), "%s: no data file (a 404): nothing, and no error" % label)
                door.data = NEWER
                open_page(tab, "/asset_catalog/")
                time.sleep(4.5)
                check(not tab.ev("!!document.querySelector('.upd-strip')"), "%s: the catalog is not stamped with a version: nothing" % label)

        if args.only in ("", "window"):
            door.data = SAME
            for label, width, height, dpr, mobile in windows:
                tab = new_tab(width, height, dpr, mobile)
                for name, address in sites:
                    if name == "admin page":
                        open_page(tab, address, seen="1.0.0")
                        time.sleep(3.5)
                        check(not tab.ev("!!document.querySelector('.upd-win')"), "admin page, %s: no window there" % label)
                        continue
                    open_page(tab, address, seen="1.0.0")
                    came = until(tab, "!!document.querySelector('.upd-win')", 8)
                    where = "%s, %s" % (name, label)
                    check(bool(came), "%s: the window comes on a page that is newer than the version that was seen" % where)
                    if not came:
                        continue
                    info = json.loads(tab.ev("""JSON.stringify((function () {
                        var w = document.querySelector('.upd-win .upd-box'), r = w.getBoundingClientRect(), body = document.querySelector('.upd-win .u-wb'), de = document.documentElement;
                        return { rect: [Math.round(r.left), Math.round(r.top), Math.round(r.right), Math.round(r.bottom)], iw: innerWidth, ih: innerHeight, hscroll: de.scrollWidth > innerWidth + 1, scrolls: body.scrollHeight > body.clientHeight + 1,
                                 versions: [].map.call(document.querySelectorAll('.upd-win .u-rv'), function (x) { return x.textContent; }), details: document.querySelectorAll('.upd-win details').length, open: document.querySelectorAll('.upd-win details[open]').length,
                                 focus: document.activeElement && document.activeElement.textContent, stored: localStorage.getItem('ants.seenVersion'), links: [].map.call(document.querySelectorAll('.upd-win a'), function (a) { return a.textContent; }),
                                 close: (function () { var q = [].filter.call(document.querySelectorAll('.upd-win button'), function (x) { return x.textContent === 'Close'; })[0].getBoundingClientRect(); return [Math.round(q.width), Math.round(q.height)]; })() };
                    })())"""))
                    check(info["versions"] == ["v1.2.0", "v1.1.0"] and info["details"] == 1 and info["open"] == 0, "%s: the releases since the version that was seen, the newest open and the other folded %s" % (where, info["versions"]))
                    check(0 <= info["rect"][0] and info["rect"][2] <= info["iw"] and 0 <= info["rect"][1] and info["rect"][3] <= info["ih"] and not info["hscroll"], "%s: the window fits the screen %s of %dx%d" % (where, info["rect"], info["iw"], info["ih"]))
                    check(info["focus"] == "Close" and info["close"][0] >= 44 and info["close"][1] >= 44 and info["links"] == [], "%s: the focus is on Close, a finger wide, and there is no All changes (the file reaches back to the version that was seen) %s" % (where, info["links"]))
                    check(info["stored"] == "1.2.0", "%s: the version is written down as seen" % where)
                    if name == "Matches":
                        tab.save_shot(args.shots, "window_matches_" + label)
                        tab.ev("document.querySelector('.upd-win summary').click()")
                        check(bool(tab.ev("document.querySelectorAll('.upd-win details[open]').length === 1")), "Matches, %s: a press on a folded release opens it" % label)
                        tab.save_shot(args.shots, "window_matches_opened_" + label)
                    tab.key("Escape")
                    check(not tab.ev("!!document.querySelector('.upd-win')"), "%s: Escape closes it" % where)
                open_page(tab, "/matches", seen="1.0.0")
                until(tab, "!!document.querySelector('.upd-win')", 8)
                tab.ev("[].filter.call(document.querySelectorAll('.upd-win button'), function (x) { return x.textContent === 'Close'; })[0].click()")
                check(not tab.ev("!!document.querySelector('.upd-win')"), "Matches, %s: Close closes it" % label)
                tab.call("Page.navigate", {"url": door.url("/matches")})
                time.sleep(3.5)
                check(not tab.ev("!!document.querySelector('.upd-win')"), "Matches, %s: and it does not come again for the same version" % label)
                door.data = {"version": "1.2.0", "releases": [release("1.2.0", "2026-01-20", 3)] + [release("1.1.%d" % k, "2026-01-%02d" % (10 + k), 3) for k in range(9, 4, -1)]}
                open_page(tab, "/matches", seen="1.1.0")
                until(tab, "!!document.querySelector('.upd-win')", 8)
                several = json.loads(tab.ev("""JSON.stringify({ v: [].map.call(document.querySelectorAll('.upd-win .u-rv'), function (x) { return x.textContent; }), a: [].map.call(document.querySelectorAll('.upd-win a'), function (x) { return x.textContent; }),
                    t: document.querySelector('.upd-win .u-nn').textContent, d: document.querySelectorAll('.upd-win details').length, o: document.querySelectorAll('.upd-win details[open]').length,
                    box: (function () { var r = document.querySelector('.upd-win .upd-box').getBoundingClientRect(); return [Math.round(r.bottom), innerHeight]; })() })"""))
                check(several["v"] == ["v1.2.0", "v1.1.9", "v1.1.8", "v1.1.7", "v1.1.6"] and several["a"] == ["All changes"] and "newest 5" in several["t"] and several["d"] == 4 and several["o"] == 0, "Matches, %s: six releases are newer: the newest five, four of them folded, and All changes %s" % (label, several))
                check(several["box"][0] <= several["box"][1], "Matches, %s: the window is not taller than the screen" % label)
                tab.save_shot(args.shots, "window_several_" + label)
                door.data = SAME

        if args.only in ("", "game"):
            tab = new_tab(1000, 700)
            door.data = NEWER
            door.set(mode="ok", freeze=True)
            open_page(tab, "/play?replay=" + live.KEPT)
            until(tab, "!!window.AntsUpdate && !!document.querySelector('header.sb')", 15)
            time.sleep(4.5)
            check(not tab.ev("!!document.querySelector('.upd-strip')"), "game page: while the game loads (the stand-in never says that it is ready) there is no strip, though a newer version is known")
            tab.ev("window.isReadyToPlay = true")                       # (what the page sets when the game has started: the stand-in game does not run that part)
            came = until(tab, "!!document.querySelector('.upd-strip')", 6)
            check(bool(came), "game page, a recording: the strip comes once the game is ready")
            if came:
                s = look(tab)
                check(s["previous"] == "HEADER" and s["rect"][1] >= s["bar"][3] - 1 and not s["hscroll"], "game page: it is under the bar and nothing sideways %s" % s["rect"])
                check(not tab.ev("!!document.querySelector('.upd-win')"), "game page: no window")
            tab.ev("window.Module._ants_match_running = function () { return 1; }")
            check(bool(until(tab, "!document.querySelector('.upd-strip')", 4)), "game page: while the game says that a match runs, the strip goes")
            tab.ev("window.Module._ants_match_running = function () { return 0; }")
            check(bool(until(tab, "!!document.querySelector('.upd-strip')", 4)), "game page: when the match is over it comes back")
            open_page(tab, "/play?live=" + live.ID)
            time.sleep(6.5)
            check(not tab.ev("!!document.querySelector('.upd-strip')"), "game page, a live match watched: nothing")
    finally:
        for tab in tabs:
            try:
                tab.close()
            except Exception:                                          # noqa: BLE001  (the browser may be gone already)
                pass
        browser.close()
    print("%s: %d checks, %d failed" % ("PASSED" if not failures else "FAILED", count[0], len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
