#!/usr/bin/env python3
"""The map picker of the front page in a REAL browser (opt-in; see tests/scripts/test_web_map_picker.sh and docs/PLAY_IN_BROWSER.md "Choosing a map").

Needs the web image of this tree served by nginx (tools/web_without_docker.py builds and serves it without Docker), the native `ants_server` of this tree, a Chromium-based browser and Python 3. The
check starts the server (the six originals and, as the site has them, all the players' maps (Community-Maps/ with its pictures), and a database with made-up play counts), puts the front door of web_map_fetch_check.py before the site
and drives the front page like a person: opens the list, switches the view, searches, sorts, picks with the keyboard, and shows what it sees as screenshots (--shots DIR). Parts:

  * desktop  the list opens (540 maps, the six originals first with their badge, then the most played), the Thumbnails switch is kept over a reload, a search with no result says "No maps found", a map
             chosen with the keyboard becomes the room's map (the server says so, the page shows its name and picture, and the file is fetched into the browser's cache at once), the list opens on it;
  * phone    the list is a sheet at the bottom of a 390 px screen, with no sideways scroll, in both views and when empty;
  * error    the list of maps is down: the dialog says so, the old selector with the six originals still works;
  * match    a map of the players chosen in the lobby with a computer player at Red: START opens the game, the game has the file and the match runs;
  * solo     alone in the room on a map of the players, desktop and phone: the line under START is "A game for one" (no computer player asked for), START opens the game page's own game for one, which fetches
             the file and runs (and is still going after a few seconds).

Exit status 0: every check passed; 1: a check failed or something broke; 3: the environment is not there; 2: a bad command line.
"""
import argparse
import json
import os
import shutil
import sqlite3
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from web_hidden_check import find_browser, free_port                          # noqa: E402
from web_rejoin_check import Player, Server, wait_for, REPO                    # noqa: E402
from web_map_fetch_check import Front                                           # noqa: E402

PARTS = ("desktop", "phone", "error", "match", "solo")
KEYS = {"ArrowDown": 40, "ArrowUp": 38, "ArrowLeft": 37, "ArrowRight": 39, "Enter": 13, "Escape": 27, "Home": 36, "End": 35, "Tab": 9}


class Visitor(Player):
    """A person at the front page (the room is made by the page itself)."""

    def press(self, key, shift=False):
        code = KEYS[key]
        params = {"key": key, "code": key, "windowsVirtualKeyCode": code, "nativeVirtualKeyCode": code, "modifiers": 8 if shift else 0}
        self.call("Input.dispatchKeyEvent", dict(params, type="rawKeyDown"))
        self.call("Input.dispatchKeyEvent", dict(params, type="keyUp"))

    def type(self, text):
        self.call("Input.insertText", {"text": text})

    def phone(self):
        self.call("Emulation.setDeviceMetricsOverride", {"width": 390, "height": 844, "deviceScaleFactor": 2, "mobile": True, "screenWidth": 390, "screenHeight": 844})

    def desktop(self):
        self.resize(1280, 1050)

    def open_front(self, web):
        self.call("Page.navigate", {"url": web})
        deadline = time.time() + 60
        while time.time() < deadline:
            time.sleep(0.4)
            if wait_for(lambda: self.ev("!!document.getElementById('code') && document.getElementById('code').textContent.length > 0 && !document.getElementById('start').disabled"), 1.0):
                return True
        return False

    def rows(self):
        return self.ev("document.querySelectorAll('#picker-panel [data-file]').length") or 0

    def names(self, n=8):
        return self.ev("Array.prototype.slice.call(document.querySelectorAll('#picker-panel [data-file] .pk-nm'), 0, %d).map(function (e) { return e.firstChild.textContent; })" % n) or []

    def open_list(self):
        self.ev("document.getElementById('browse').click()")
        return bool(wait_for(lambda: self.rows() > 5, 15, 0.2))

    def visible(self):
        return bool(self.ev("!document.getElementById('picker').hidden"))

    def selected(self):
        return self.ev("(function () { var e = document.querySelector('#picker-panel [aria-selected=\"true\"]'); return e ? e.getAttribute('data-file') : ''; })()")

    def focus_file(self):
        return self.ev("(function () { var e = document.activeElement; return e && e.getAttribute ? e.getAttribute('data-file') || e.className : ''; })()")

    def room_code(self):
        return (self.ev("document.getElementById('code').textContent") or "").replace(" ", "")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--web", required=True, help="the site's nginx, e.g. http://127.0.0.1:19990/ (the web image of this tree; it needs no game server)")
    ap.add_argument("--server", default=os.environ.get("ANTS_SERVER_BIN", os.path.join(REPO, "build", "src", "ants_server", "ants_server")), help="the ants_server program of this tree")
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--only", default="", help="run only the parts whose name contains this text (%s)" % ", ".join(PARTS))
    ap.add_argument("--shots", default="", help="a folder to save screenshots in")
    ap.add_argument("--ready-timeout", type=float, default=120.0, metavar="SECONDS", help="how long a game page may take to get its game ready (default 120)")
    args = ap.parse_args()
    site = args.web if args.web.endswith("/") else args.web + "/"
    failures = []
    count = [0]

    def check(ok, what):
        count[0] += 1
        print("  %s: %s" % ("ok  " if ok else "FAIL", what), flush=True)
        if not ok:
            failures.append(what)

    def wanted(name):
        return not args.only or args.only in name

    chrome = find_browser(args.browser)
    if not chrome:
        print("  SKIP: no Chromium-based browser found (give one with --browser or CHROME)")
        return 3
    if not os.path.isfile(args.server) or not os.access(args.server, os.X_OK):
        print("  SKIP: the game server program is not there (build the target ants_server, or give --server)")
        return 3
    try:
        urllib.request.urlopen(site, timeout=10).read(64)
    except (OSError, urllib.error.URLError) as e:
        print("  SKIP: nothing answers at %s (%s)" % (site, e))
        return 3

    work = tempfile.mkdtemp(prefix="ants_map_picker_check.")
    maps = os.path.join(work, "maps")
    os.makedirs(maps)
    for f in os.listdir(os.path.join(REPO, "Original-Ants", "Maps")):
        shutil.copy(os.path.join(REPO, "Original-Ants", "Maps", f), maps)
    with open(os.path.join(REPO, "Community-Maps", "maps.json"), encoding="utf-8") as f:
        catalogue = json.load(f)["maps"]

    ws_port, door_port = free_port(), free_port()
    server = Server(args.server, maps, ws_port, work)
    server.options = server.options + ["--replay-port", str(door_port), "--community-maps", os.path.join(REPO, "Community-Maps")]
    front = None
    visitors = []
    try:
        server.start()                                                       # (once, so that the database exists; then the play counts are put in and the server starts again)
        server.stop()
        db = sqlite3.connect(os.path.join(server.results, "ants.db"))
        for i, m in enumerate(catalogue):
            plays = (i * 7919) % 997 if i % 3 == 0 else 0
            if plays:
                db.execute("INSERT OR REPLACE INTO map_plays (map, hash, plays, last_played) VALUES (?1, ?2, ?3, ?4)", (m["file"], m["hash"], plays, 1760000000))
        db.commit()
        db.close()
        server.start()
        site_port = int(site.split("//", 1)[1].split("/")[0].split(":")[1])
        front = Front(site_port, ws_port, door_port)
        web = "http://127.0.0.1:%d/" % front.port
        listing = json.load(urllib.request.urlopen("http://127.0.0.1:%d/maps" % door_port, timeout=10))["maps"]
        top = [m for m in listing if not m.get("original")]
        top.sort(key=lambda m: (-m["plays"], m["file"][:-4].lower(), m["file"]))

        def visitor(name="Ann"):
            v = Visitor(chrome, name, 0, web, args.ready_timeout)
            visitors.append(v)
            return v

        if wanted("desktop"):
            print("part desktop", flush=True)
            v = visitor()
            v.desktop()
            check(v.open_front(web), "the front page is up with its room")
            check(v.ev("!document.getElementById('browse').hidden"), "the Browse all maps button is there")
            check(v.ev("!!document.getElementById('prev') && !!document.getElementById('next') && document.getElementById('map').options.length >= 6"), "the old selector (previous, next, the drop-down) is still there")
            check(v.open_list(), "the list opens")
            check(v.rows() == len(listing), "the list holds all %d maps (%d)" % (len(listing), v.rows()))
            names = v.names(8)
            check(names[:6] == ["Tiny", "Small", "Medium", "Gauntlet", "Treasure", "Islands"], "the six originals come first (%s)" % names[:6])
            check(v.ev("document.querySelectorAll('#picker-panel .pk-badge').length >= 6 && document.querySelector('#picker-panel [data-file] .pk-badge').textContent === 'Original'"), "the originals carry the Original badge")
            check(names[6] == top[0]["file"][:-4], "then the most played map of the players (%s)" % names[6])
            check(v.ev("document.querySelector('#picker-panel .pk-pl').textContent.length > 0"), "the rows show the play counts")
            check(v.ev("(function () { var i = document.querySelector('#picker-panel [data-file] img'); return i.getAttribute('width') === '64' && i.getAttribute('loading') === 'lazy' && i.getAttribute('alt') === '' && i.naturalWidth > 0; })()"), "the pictures are 64 px, lazy and loaded")
            check(v.ev("document.querySelectorAll('#picker-panel [data-file] img').length") == v.rows(), "every row has a picture (%s of %s)" % (v.ev("document.querySelectorAll('#picker-panel [data-file] img').length"), v.rows()))
            check(v.selected() == "TREASURE.LVL", "the room's map (Treasure) is the highlighted one (%s)" % v.selected())
            v.shot(args.shots, "picker-desktop-detail")
            v.ev("Array.prototype.slice.call(document.querySelectorAll('#picker-panel .pk-chip')).filter(function (b) { return b.textContent === 'Thumbnails'; })[0].click()")
            check(v.ev("document.querySelector('#picker-panel .pk-grid') !== null && document.querySelector('#picker-panel [data-file] img').getAttribute('width') === '130'"), "Thumbnails: a grid of 130 px pictures")
            check(v.ev("localStorage.getItem('ants.mapview')") == "thumbs", "the choice is kept in this browser")
            v.shot(args.shots, "picker-desktop-thumbnails")
            check(v.open_front(web) and v.open_list() and v.ev("document.querySelector('#picker-panel .pk-grid') !== null"), "after a reload the list opens in Thumbnails")
            v.ev("document.querySelector('#picker-panel .pk-q').focus()")
            v.type("zzqq")
            time.sleep(0.3)
            check(v.ev("document.querySelector('#picker-panel .pk-empty') !== null && document.querySelector('#picker-panel .pk-empty').textContent.indexOf('No map is called') === 0"), "a search with no result says that no map is called so")
            v.shot(args.shots, "picker-desktop-empty")
            v.ev("(function () { var q = document.querySelector('#picker-panel .pk-q'); q.value = ''; q.dispatchEvent(new Event('input', { bubbles: true })); })()")
            v.ev("Array.prototype.slice.call(document.querySelectorAll('#picker-panel .pk-chip')).filter(function (b) { return b.textContent === 'Detail'; })[0].click()")
            v.ev("document.querySelector('#picker-panel .pk-q').focus()")
            v.type("jungal")
            time.sleep(0.3)
            check(v.rows() >= 1, "the search finds maps by name (%d)" % v.rows())
            v.ev("(function () { var q = document.querySelector('#picker-panel .pk-q'); q.value = ''; q.dispatchEvent(new Event('input', { bubbles: true })); })()")
            v.ev("Array.prototype.slice.call(document.querySelectorAll('#picker-panel .pk-chip')).filter(function (b) { return b.textContent === 'Name'; })[0].click()")
            check(v.names(1)[0].lower() <= v.names(2)[1].lower(), "sorted by name")
            v.ev("Array.prototype.slice.call(document.querySelectorAll('#picker-panel .pk-chip')).filter(function (b) { return b.textContent === 'Originals, then plays'; })[0].click()")
            v.ev("document.querySelector('#picker-panel .pk-q').focus()")
            v.press("ArrowDown")
            check(bool(v.ev("document.activeElement.getAttribute('role') === 'option'")), "ArrowDown from the search box goes into the list")
            for _ in range(2):                                               # (the first arrow only went into the list, to the picked row, Treasure: the seventh row is two on)
                v.press("ArrowDown")
            chosen = v.selected()
            check(chosen == top[0]["file"], "two more arrows reach the most played map of the players (%s)" % chosen)
            v.press("Enter")
            time.sleep(0.6)
            check(not v.visible(), "Enter chooses and closes the list")
            check(v.ev("document.activeElement.id") == "browse", "the focus goes back to the Browse button")
            room = server.room(v.room_code()) or {}
            check(room.get("map") == chosen, "the server's room is on the chosen map (%s)" % room.get("map"))
            check(v.ev("document.getElementById('map-other').textContent") == chosen[:-4], "the drop-down shows its name")
            check(v.ev("document.getElementById('picker-panel').getAttribute('aria-labelledby')") == "picker-h", "the dialog is named by its heading")
            check(bool(wait_for(lambda: v.ev("document.getElementById('preview').naturalWidth") == 300, 10)), "the picture beside the selector is the map's own (300 px)")
            check(len(v.ev("document.getElementById('info').textContent")) > 0, "the Map Info line under the picture says something of it (%s)" % v.ev("document.getElementById('info').textContent"))
            check(("/maps/files/" + chosen) in front.requests, "the file was fetched at once (the browser's cache is warm)")
            v.shot(args.shots, "lobby-community-map-chosen")
            check(v.open_list() and v.selected() == chosen, "the list opens on the chosen map, highlighted")
            v.ev("document.querySelector('#picker-panel [aria-selected=\"true\"]').scrollIntoView({ block: 'center' })")
            v.shot(args.shots, "picker-desktop-selected")
            v.press("Escape")
            check(not v.visible(), "Escape closes the list")
            check(v.ev("document.getElementById('browse').getAttribute('aria-haspopup')") == "dialog", "the button says it opens a dialog")
            check(v.ev("document.querySelector('#picker-panel .pk-q').getAttribute('placeholder')") == "Search %d maps" % len(listing), "the search box says how many maps there are")
            check("30 seconds" in (v.ev("document.querySelector('#picker-panel .pk-fine').textContent") or ""), "the fine print about the plays is under the list")
            v.ev("document.getElementById('next').click()")
            time.sleep(0.5)
            room = server.room(v.room_code()) or {}
            check(room.get("map") == "TINY.LVL", "next from a map of the players goes to the first original (%s)" % room.get("map"))

            def pick_top():
                v.open_list()
                v.ev("document.querySelector('#picker-panel [data-file=\"%s\"]').click()" % top[0]["file"].replace('"', ''))
                v.ev("Array.prototype.slice.call(document.querySelectorAll('#picker-panel button')).filter(function (b) { return b.textContent === 'Choose map'; })[0].click()")
                time.sleep(0.6)
            pick_top()
            v.ev("document.getElementById('prev').click()")
            time.sleep(0.5)
            room = server.room(v.room_code()) or {}
            check(room.get("map") == "ISLANDS.LVL", "previous from a map of the players goes to the last original (%s)" % room.get("map"))
            pick_top()
            plan = v.ev("document.getElementById('plan').textContent") or ""
            check(plan.startswith("A game for one"), "alone on a map of the players the line under START is the same as for an original (%s)" % plan)
            check("computer player" not in plan, "and it asks for no computer player")
            v.shot(args.shots, "lobby-community-alone")
            check(v.ev("document.getElementById('start').disabled") is False, "START is on")
            v.open_list()
            check(v.ev("[document.querySelector('header.mast')].concat(['banner', 'rejoin', 'lobby', 'footer-bar'].map(function (id) { return document.getElementById(id); })).every(function (n) { return n.hasAttribute('inert'); })"), "the page behind the open list is inert")
            v.ev("document.body.focus(); document.activeElement.blur()")
            v.press("Escape")
            check(not v.visible() and not v.ev("[document.querySelector('header.mast')].concat(['banner', 'rejoin', 'lobby', 'footer-bar'].map(function (id) { return document.getElementById(id); })).some(function (n) { return n.hasAttribute('inert'); })"), "Escape closes the list and the page is live again")

            # START alone on a map of the players hands the page over to the game page's own game for one, with the map's file in its address (it fetches the file from the site)
            v.ev("document.getElementById('start').click()")
            check(bool(wait_for(lambda: (v.ev("window.location.pathname") or "").endswith("/play.html"), 20)), "START alone leaves for the game page")
            query = urllib.parse.parse_qs((v.ev("window.location.search") or "").lstrip("?"))
            check(query.get("mapfile") == [top[0]["file"]] and "map" not in query and "bots" not in query, "the game page is asked for a game for one on that map's file (%s)" % query)

        if wanted("phone"):
            print("part phone", flush=True)
            v = visitor("Bea")
            v.phone()
            check(v.open_front(web), "the front page is up on a phone")
            v.ev("(function () { var real = window.matchMedia.bind(window); window.matchMedia = function (q) { return /pointer:\\s*coarse/.test(q) ? { matches: true } : real(q); }; })()")      # (the check's browser has a mouse: it is told that this screen is a touch screen)
            check(v.open_list(), "the list opens on a phone")
            check(v.ev("document.documentElement.scrollWidth <= window.innerWidth"), "no sideways scroll with the list open")
            panel = v.ev("(function () { var r = document.getElementById('picker-panel').getBoundingClientRect(); return [r.left, r.right, r.bottom, window.innerWidth, window.innerHeight]; })()")
            check(panel[0] == 0 and panel[1] == panel[3] and abs(panel[2] - panel[4]) <= 1, "the list is a sheet over the whole width, at the bottom (%s)" % panel)
            check(v.ev("Math.round(document.querySelector('#picker-panel [data-file] img').getBoundingClientRect().width)") == 56, "the pictures are smaller on a phone")
            share = v.ev("(function () { var o = document.querySelector('#picker-panel .pk-out').getBoundingClientRect().height, p = document.getElementById('picker-panel').getBoundingClientRect().height; return o / p; })()")
            check(share > 0.45, "the list has at least 45%% of the sheet, the controls the rest (%.0f%%)" % (share * 100))
            check(v.ev("document.activeElement.className") != "pk-q", "no keyboard is called up: the search box has not taken the focus")
            check(v.ev("(function () { var s = document.querySelector('#picker-panel .pk-sorts'); return s.scrollWidth >= s.clientWidth && getComputedStyle(s).flexWrap === 'nowrap'; })()"), "the sort buttons are one row")
            v.shot(args.shots, "picker-phone-detail")
            v.ev("Array.prototype.slice.call(document.querySelectorAll('#picker-panel .pk-chip')).filter(function (b) { return b.textContent === 'Thumbnails'; })[0].click()")
            check(v.ev("document.documentElement.scrollWidth <= window.innerWidth"), "no sideways scroll in Thumbnails")
            v.shot(args.shots, "picker-phone-thumbnails")
            v.ev("document.querySelector('#picker-panel .pk-q').focus()")
            v.type("zzqq")
            time.sleep(0.3)
            v.shot(args.shots, "picker-phone-empty")
            check(v.ev("document.documentElement.scrollWidth <= window.innerWidth"), "no sideways scroll when empty")
            v.ev("localStorage.removeItem('ants.mapview')")
            v.press("Escape")
            check(not v.visible(), "Escape closes the sheet")
            v.shot(args.shots, "lobby-phone")

        if wanted("error"):
            print("part error", flush=True)
            front.maps_down = True
            v = visitor("Cy")
            v.desktop()
            check(v.open_front(web), "the front page is up while the list of maps is down")
            check(v.open_list() is False and "not available right now" in (v.ev("document.getElementById('picker-panel').textContent") or ""), "the dialog says that the list is not available")
            check("six original maps" in (v.ev("document.getElementById('picker-panel').textContent") or ""), "and that the six originals can be chosen with the arrows")
            v.shot(args.shots, "picker-error")
            v.press("Escape")
            v.ev("document.getElementById('next').click()")
            time.sleep(0.5)
            room = server.room(v.room_code()) or {}
            check(room.get("map", "").endswith(".LVL"), "the old selector still sets the room's map (%s)" % room.get("map"))
            front.maps_down = False

        if wanted("match"):
            print("part match", flush=True)
            v = visitor("Dee")
            v.desktop()
            check(v.open_front(web), "the front page is up")
            check(v.open_list(), "the list opens")
            v.ev("document.querySelector('#picker-panel .pk-q').focus()")
            v.type("jungal")
            time.sleep(0.3)
            v.ev("document.querySelector('#picker-panel [data-file]').click()")
            file = v.selected()
            v.ev("Array.prototype.slice.call(document.querySelectorAll('#picker-panel button')).filter(function (b) { return b.textContent === 'Choose map'; })[0].click()")
            time.sleep(0.6)
            check((server.room(v.room_code()) or {}).get("map") == file, "the room is on %s" % file)
            v.ev("(function () { var s = document.querySelector('#slots select[data-mode]'); s.value = 'medium'; s.dispatchEvent(new Event('change', { bubbles: true })); })()")
            time.sleep(0.5)
            v.ev("document.getElementById('start').click()")
            ready = wait_for(lambda: v.ev("!!window.isReadyToPlay"), args.ready_timeout, 0.5)
            check(bool(ready), "START opens the game page and its game is ready")
            check(bool(wait_for(lambda: v.in_match(), 90, 0.5)), "the match on the map of the players runs")
            check(bool(v.ev("(function () { try { return Module.FS.analyzePath(%s).exists; } catch (e) { return false; } })()" % json.dumps("Original-Ants/Maps/" + file))), "the game has the map file")

        if wanted("solo"):
            print("part solo", flush=True)
            def alone_on_a_map_of_the_players(v):
                check(v.open_front(web), "the front page is up with its room")
                check(v.open_list(), "the list opens")
                v.ev("document.querySelector('#picker-panel .pk-q').focus()")
                v.type("jungal")
                time.sleep(0.3)
                v.ev("document.querySelector('#picker-panel [data-file]').click()")
                chosen = v.selected()
                v.ev("Array.prototype.slice.call(document.querySelectorAll('#picker-panel button')).filter(function (b) { return b.textContent === 'Choose map'; })[0].click()")
                time.sleep(0.8)
                check((server.room(v.room_code()) or {}).get("map") == chosen, "the room is on %s" % chosen)
                plan = v.ev("document.getElementById('plan').textContent") or ""
                check(plan.startswith("A game for one") and "computer player" not in plan, "alone on %s: the line under START is a game for one (%s)" % (chosen, plan))
                check(v.ev("document.getElementById('start').disabled") is False, "START is on")
                return chosen
            v = visitor("Sol")
            v.phone()
            alone_on_a_map_of_the_players(v)
            v.shot(args.shots, "solo-lobby-phone")
            v = visitor("Sol")
            v.desktop()
            file = alone_on_a_map_of_the_players(v)
            v.shot(args.shots, "solo-lobby-desktop")
            v.ev("document.getElementById('start').click()")
            time.sleep(1.5)
            v.shot(args.shots, "solo-start-pressed")
            check(bool(wait_for(lambda: v.ev("!!window.isReadyToPlay"), args.ready_timeout, 0.5)), "START opens the game page and its game is ready")
            deadline = time.time() + 90                                      # (the quick help closes with Enter, and with --play the match starts at once; an Enter that comes while the loading screen is up is pressed again)
            while time.time() < deadline and not (v.state() or [0, 0])[1]:
                v.press("Enter")
                time.sleep(2)
            check(bool(wait_for(lambda: v.in_match(), 30, 0.5)), "the game for one on the map of the players runs (the quick help closed with Enter)")
            check(bool(v.ev("(function () { try { return Module.FS.analyzePath(%s).exists; } catch (e) { return false; } })()" % json.dumps("Original-Ants/Maps/" + file))), "the game has the map file")
            time.sleep(8)
            check(v.in_match(), "and it is still going after a few seconds: a game for one does not end by itself")
            v.shot(args.shots, "solo-game-desktop")

        bad = [(p.name, t) for p in visitors for _, level, t in p.console if level == "exception"]
        check(not bad, "no page threw an exception (%s)" % bad[:3])
    except Exception as e:                                                   # noqa: BLE001
        failures.append("the check broke: %r" % (e,))
        print("  FAIL: the check broke: %r (server: %s)" % (e, server.tail()), flush=True)
    finally:
        for v in visitors:
            try:
                v.close()
            except Exception:                                                # noqa: BLE001
                pass
        if front:
            front.stop()
        server.stop()
        shutil.rmtree(work, ignore_errors=True)
    print("%d checks, %d failed" % (count[0], len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
