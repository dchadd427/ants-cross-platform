#!/usr/bin/env python3
"""The "Game type" control of the front page's lobby in REAL browsers (opt-in; see tests/scripts/test_web_game_type.sh and docs/PLAY_IN_BROWSER.md "Game type").

Needs the web image of this tree served by nginx (tools/web_without_docker.py builds and serves it without Docker), the native `ants_server` of this tree, a Chromium-based browser and Python 3. The
check starts the server, puts a front door before the site (/ws goes to the server, everything else to the site) and drives the front page like a person. Parts:

  * leader   the closed field says "Highest score" with its line and no tag; a click opens the list (a drop-down, one row for each mode, the chosen one aria-selected); picking 187 sends the plan (the
             server's room says "187"), the field, the note and the tag by the room's name change, the choice is kept in the browser; a second browser that joins sees the read-only line "Game type: 187"
             and the tag, and both change when the leader picks Highest score again; the guest has no field;
  * keys     Down opens the list on the chosen row, Down and Up move (aria-activedescendant), Enter chooses, Escape closes and chooses nothing, a letter jumps to a mode, Tab takes the row under the cursor;
  * phone    a 390 px screen: the list is a bottom sheet with a title and rows of 64 px or more, over a dimmed page, no sideways scroll; a tap on a row chooses; a tap outside closes;
  * remember a new room starts with the leader's last choice (a new tab of the same browser); a remembered word that no mode has gives the default; with localStorage refused the page works and
             every room starts with the default;
  * pending  the leader picks 187 and changes a computer player's level before the room has answered: the second plan carries the pick, the room stays on 187;
  * survive  the leader's open list keeps its row and its focus when the room changes under it (a guest joins);
  * rows     the control made on a list of three modes (a row added to the data) with one that this browser does not know: the list holds three rows and scrolls inside itself when it is long, the unknown
             mode reads "New mode" with that tag, nothing is thrown.
--shots DIR saves the pictures that the check looks at, to compare with the approved ones.

Exit status 0: every check passed; 1: a check failed or something broke; 3: the environment is not there; 2: a bad command line.
"""
import argparse
import os
import shutil
import sys
import tempfile
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from web_hidden_check import find_browser, free_port                          # noqa: E402
from web_rejoin_check import Server, wait_for, REPO                            # noqa: E402
from web_map_fetch_check import Front                                           # noqa: E402
from web_map_picker_browser import Visitor                                      # noqa: E402

PARTS = ("leader", "keys", "phone", "remember", "pending", "survive", "rows")
BLOCK = "Object.defineProperty(window, 'localStorage', { get: function () { throw new Error('storage refused'); } });"


class Person(Visitor):
    def text(self, sel):
        return self.ev("(function () { var e = document.querySelector(%r); return e ? e.textContent : null; })()" % sel)

    def shown(self, sel):
        return bool(self.ev("(function () { var e = document.querySelector(%r); return !!e && !e.hidden && e.getClientRects().length > 0; })()" % sel))

    def combo(self):
        return self.text("#gt-name")

    def tag(self):
        return self.text("#mode-tag") if self.shown("#mode-tag") else ""

    def is_open(self):
        return self.ev("document.getElementById('gt-combo').getAttribute('aria-expanded')") == "true" and self.shown("#gt-list")

    def active(self):
        return self.ev("(function () { var c = document.getElementById('gt-combo'); var a = c.getAttribute('aria-activedescendant'); return a ? document.getElementById(a).getAttribute('data-mode') : ''; })()")

    def char(self, c):
        params = {"key": c, "code": "Key" + c.upper() if c.isalpha() else "Digit" + c, "text": c, "unmodifiedText": c}
        self.call("Input.dispatchKeyEvent", dict(params, type="keyDown"))
        self.call("Input.dispatchKeyEvent", dict(params, type="keyUp"))

    def click(self, sel):
        self.ev("document.querySelector(%r).click()" % sel)

    def join(self, web, code, name):
        self.call("Page.navigate", {"url": "%s?room=%s" % (web, code)})
        if not wait_for(lambda: self.ev("!document.getElementById('name-step').hidden"), 30, 0.2):
            return False
        self.ev("(function () { var i = document.getElementById('name-step-input'); i.value = %r; i.dispatchEvent(new Event('input', { bubbles: true })); document.getElementById('name-step-go').click(); })()" % name)
        return bool(wait_for(lambda: self.ev("document.getElementById('name-step').hidden && document.querySelectorAll('#slots .slot.k-person').length >= 2"), 30, 0.2))

    def errors(self):
        return [c for c in self.console if c[1] == "exception"]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--web", required=True, help="the site's nginx, e.g. http://127.0.0.1:19990/ (the web image of this tree; it needs no game server)")
    ap.add_argument("--server", default=os.environ.get("ANTS_SERVER_BIN", os.path.join(REPO, "build", "src", "ants_server", "ants_server")), help="the ants_server program of this tree")
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--only", default="", help="run only the parts whose name contains this text (%s)" % ", ".join(PARTS))
    ap.add_argument("--shots", default="", help="a folder to save screenshots in")
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

    work = tempfile.mkdtemp(prefix="ants_game_type_check.")
    maps = os.path.join(work, "maps")
    os.makedirs(maps)
    for f in os.listdir(os.path.join(REPO, "Original-Ants", "Maps")):
        shutil.copy(os.path.join(REPO, "Original-Ants", "Maps", f), maps)
    ws_port, door_port = free_port(), free_port()
    server = Server(args.server, maps, ws_port, work)
    server.options = server.options + ["--replay-port", str(door_port), "--community-maps", os.path.join(REPO, "Community-Maps")]
    front = None
    people = []
    try:
        server.start()
        site_port = int(site.split("//", 1)[1].split("/")[0].split(":")[1])
        front = Front(site_port, ws_port, door_port)
        web = "http://127.0.0.1:%d/" % front.port

        def person(name="Ann", block=False):
            v = Person(chrome, name, 0, web, 120.0)
            people.append(v)
            if block:
                v.call("Page.addScriptToEvaluateOnNewDocument", {"source": BLOCK})
            return v

        def mode_of(v):
            return (server.room(v.room_code()) or {}).get("mode")

        def mode_is(v, word):
            return bool(wait_for(lambda: mode_of(v) == word, 10, 0.2))

        if wanted("leader"):
            print("part leader", flush=True)
            ann = person()
            ann.desktop()
            check(ann.open_front(web), "the front page is up with its room")
            check(ann.combo() == "Highest score" and ann.text("#gt-line") == "The original game. Food and power-ups; the highest score wins.", "closed: the default mode's name with its line under it")
            check(ann.tag() == "" and not ann.shown("#gt-note"), "no tag by the room's name and no note for the default mode")
            check(ann.ev("(function () { var c = document.getElementById('gt-combo'); return c.getAttribute('role') === 'combobox' && c.getAttribute('aria-haspopup') === 'listbox' && c.getAttribute('aria-expanded') === 'false'; })()"), "the field is a combobox that says it has a listbox and is closed")
            check(ann.text("#gt-label") == "Game type" and not ann.shown(".gt-guest"), "the label says Game type and the guest's line is not shown")
            ann.shot(args.shots, "leader-1a-closed")
            ann.click("#gt-combo")
            check(ann.is_open(), "a click opens the list")
            check(ann.ev("document.querySelectorAll('#gt-list [role=option]').length") == 2 and ann.ev("document.querySelector('#gt-opt-0').getAttribute('aria-selected')") == "true", "two rows, the chosen one aria-selected")
            check(ann.text("#gt-opt-1 .gt-oname") == "187" and ann.text("#gt-opt-1 .gt-oline") == "A fight with no food. Kills decide.", "the 187 row: name and one line")
            ann.shot(args.shots, "leader-1b-open")
            ann.click("#gt-opt-1")
            check(not ann.is_open(), "choosing a row closes the list")
            check(mode_is(ann, "187"), "the plan reached the room: its mode is 187 (%s)" % mode_of(ann))
            check(bool(wait_for(lambda: ann.combo() == "187", 10, 0.2)), "the field says 187 (%s)" % ann.combo())
            check(ann.text("#gt-note") == "No food and no power-ups. The most kills wins." and ann.shown("#gt-note"), "the note shows under the field")
            check(ann.tag() == "187", "the tag 187 is by the room's name (%s)" % ann.tag())
            check(ann.ev("localStorage.getItem('ants.lobby.gameType')") == "187", "the choice is kept in this browser")
            check(ann.ev("document.querySelector('#start').textContent") == "START!", "START is unchanged")
            ann.shot(args.shots, "leader-1c-187")
            bob = person("Bob")
            bob.desktop()
            check(bob.join(web, ann.room_code(), "Bob"), "a second browser joins by the link")
            check(bool(wait_for(lambda: bob.text(".gt-guest") == "Game type: 187", 10, 0.2)), "the guest's line says Game type: 187 (%s)" % bob.text(".gt-guest"))
            check(not bob.shown("#gt-combo") and not bob.shown("#gt-line") and bob.tag() == "187" and bob.text(".gt-gline") == "A fight with no food. Kills decide.", "the guest has no field, sees the line under it and the tag")
            bob.shot(args.shots, "guest-187")
            ann.click("#gt-combo")
            ann.click("#gt-opt-0")
            check(mode_is(ann, "highest-score"), "the leader picks Highest score again: the room follows")
            check(bool(wait_for(lambda: bob.text(".gt-guest") == "Game type: Highest score" and bob.tag() == "", 10, 0.2)), "the guest's line and tag change by themselves (%s, tag '%s')" % (bob.text(".gt-guest"), bob.tag()))
            bob.shot(args.shots, "guest-default")
            check(ann.tag() == "" and not ann.shown("#gt-note"), "the leader's tag and note are gone")
            check(not ann.errors() and not bob.errors(), "no page threw anything")

        if wanted("keys"):
            print("part keys", flush=True)
            v = person()
            v.desktop()
            check(v.open_front(web), "the front page is up with its room")
            v.ev("document.getElementById('gt-combo').focus()")
            v.press("ArrowDown")
            check(v.is_open() and v.active() == "0", "Down opens the list on the chosen row")
            v.press("ArrowDown")
            check(v.active() == "1" and v.ev("document.getElementById('gt-combo').getAttribute('aria-activedescendant')") == "gt-opt-1", "Down moves to 187 (aria-activedescendant names it)")
            v.press("ArrowDown")
            check(v.active() == "1", "Down at the last row stays")
            v.press("ArrowUp")
            check(v.active() == "0", "Up moves back")
            v.press("Escape")
            check(not v.is_open() and mode_of(v) == "highest-score" and v.ev("document.activeElement.id") == "gt-combo", "Escape closes, chooses nothing and keeps the focus on the field")
            v.press("End")
            check(not v.is_open(), "End on a closed field does nothing")
            v.press("Enter")
            v.press("End")
            check(v.is_open() and v.active() == "1", "Enter opens, End goes to the last row")
            v.press("Enter")
            check(not v.is_open() and mode_is(v, "187"), "Enter chooses 187 and closes")
            wait_for(lambda: v.combo() == "187", 10, 0.2)                    # (the field follows the room's answer)
            v.press("ArrowDown")
            v.press("Home")
            v.press("Enter")
            check(mode_is(v, "highest-score"), "Home and Enter choose the first row")
            wait_for(lambda: v.combo() == "Highest score", 10, 0.2)
            v.char("1")
            time.sleep(0.2)
            check(v.is_open() and v.active() == "1", "a letter (or digit) opens the list on the mode that starts with it")
            v.press("Tab")
            check(not v.is_open() and mode_is(v, "187"), "Tab takes the row under the cursor (187) and closes the list")
            v.click("#gt-combo")
            v.ev("document.body.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }))")
            check(not v.is_open(), "a click outside closes the list")
            check(not v.errors(), "no page threw anything")

        if wanted("phone"):
            print("part phone", flush=True)
            v = person()
            v.phone()
            check(v.open_front(web), "the front page is up with its room (390 px)")
            v.ev("document.getElementById('gt-combo').scrollIntoView({ block: 'center' })")
            v.shot(args.shots, "phone-closed")
            v.click("#gt-combo")
            check(v.is_open(), "a tap opens the list")
            geo = v.ev("(function () { var p = document.querySelector('.gt-pop').getBoundingClientRect(); var o = document.querySelector('#gt-opt-1').getBoundingClientRect(); return [p.left, p.right, p.bottom, window.innerWidth, window.innerHeight, o.height, p.height]; })()")
            check(geo[0] == 0 and geo[1] == geo[3] and abs(geo[2] - geo[4]) < 1, "the list is a sheet on the bottom edge, as wide as the screen (%s)" % geo)
            check(geo[5] >= 64 and geo[6] <= geo[4] * 0.6 + 1, "rows are at least 64 px tall and the sheet is at most 60%% of the screen (%.0f, %.0f)" % (geo[5], geo[6]))
            check(v.shown(".gt-title") and v.text(".gt-title") == "Game type" and v.shown(".gt-scrim"), "the sheet has its title and a dimmed page behind it")
            check(v.ev("document.documentElement.scrollWidth") <= 390, "no sideways scroll")
            v.shot(args.shots, "phone-open")
            v.click("#gt-opt-1")
            check(mode_is(v, "187") and not v.is_open() and bool(wait_for(lambda: v.combo() == "187", 10, 0.2)), "a tap on a row chooses it and closes the sheet")
            v.shot(args.shots, "phone-187")
            v.click("#gt-combo")
            v.ev("document.querySelector('.gt-scrim').click()")
            check(not v.is_open(), "a tap outside the sheet closes it")
            check(not v.errors(), "no page threw anything")

        if wanted("remember"):
            print("part remember", flush=True)
            v = person()
            v.desktop()
            check(v.open_front(web) and v.combo() == "Highest score", "a browser that remembers nothing starts with Highest score")
            v.click("#gt-combo")
            v.click("#gt-opt-1")
            check(mode_is(v, "187"), "the leader picks 187")
            v.open_tab()
            v.desktop()
            check(v.open_front(web), "a new tab makes a new room")
            second = v.room_code()
            check(bool(wait_for(lambda: v.combo() == "187" and v.tag() == "187", 10, 0.2)), "the new room's field and tag start with 187, the last choice (%s)" % v.combo())
            check(mode_is(v, "187"), "the new room's own mode is 187 (%s)" % mode_of(v))
            v.ev("localStorage.setItem('ants.lobby.gameType', 'no-such-mode')")
            v.open_tab()
            v.desktop()
            check(v.open_front(web) and v.room_code() != second, "another new tab, a word that no mode has remembered")
            check(v.combo() == "Highest score" and mode_of(v) == "highest-score", "it falls back to Highest score (%s, %s)" % (v.combo(), mode_of(v)))
            b = person("Cy", block=True)
            b.desktop()
            check(b.open_front(web), "with localStorage refused the page still has its room")
            check(b.combo() == "Highest score" and mode_of(b) == "highest-score", "and starts with Highest score")
            b.click("#gt-combo")
            b.click("#gt-opt-1")
            check(mode_is(b, "187") and bool(wait_for(lambda: b.combo() == "187" and b.tag() == "187", 10, 0.2)), "the control still chooses 187")
            check(not b.errors() and not v.errors(), "no page threw anything")

        if wanted("pending"):
            print("part pending", flush=True)
            v = person()
            v.desktop()
            check(v.open_front(web), "the front page is up with its room")
            v.ev("""(function () {
                document.getElementById('gt-combo').click(); document.getElementById('gt-opt-1').click();
                var s = document.querySelector('#slots select.mode'); s.value = 'easy'; s.dispatchEvent(new Event('change', { bubbles: true }));
            })()""")
            check(bool(wait_for(lambda: v.ev("document.querySelectorAll('#slots .slot.k-bot').length") >= 1, 10, 0.2)), "the second plan (a computer player) reached the room")
            time.sleep(1.0)
            check(mode_of(v) == "187" and v.combo() == "187" and v.tag() == "187", "the room stayed on 187: the second plan carried the pick (%s)" % mode_of(v))
            v.click("#gt-combo")
            v.click("#gt-opt-0")
            check(mode_is(v, "highest-score"), "a later pick still works")
            check(not v.errors(), "no page threw anything")

        if wanted("survive"):
            print("part survive", flush=True)
            ann = person()
            ann.desktop()
            check(ann.open_front(web), "the front page is up with its room")
            ann.ev("document.getElementById('gt-combo').focus()")
            ann.press("ArrowDown")
            ann.press("ArrowDown")
            check(ann.is_open() and ann.active() == "1", "the list is open on 187's row")
            bob = person("Bob")
            bob.desktop()
            check(bob.join(web, ann.room_code(), "Bob"), "a guest joins while the list is open")
            time.sleep(0.5)
            check(ann.is_open() and ann.active() == "1" and ann.ev("document.activeElement.id") == "gt-combo", "the list is still open on the same row and the focus is on the field")
            ann.press("Enter")
            check(mode_is(ann, "187"), "Enter then chooses the row that was under the cursor")
            check(not ann.errors(), "no page threw anything")

        if wanted("rows"):
            print("part rows", flush=True)
            v = person()
            v.desktop()
            check(v.open_front(web), "the front page is up")
            made = v.ev("""(function () {
                var M = window.AntsGameModes, rows = M.MODES.slice();
                for (var i = 0; i < 7; i++) rows.push({ id: 2 + i, word: 'extra-' + i, name: 'Extra mode ' + i, tag: 'X' + i, line: 'A made-up mode, number ' + i + '.', note: '' });
                M.MODES.push.apply(M.MODES, rows.slice(2));
                var host = document.createElement('div'); host.style.cssText = 'position:fixed;left:20px;top:20px;width:340px;z-index:5;background:#123;padding:10px';
                document.body.appendChild(host);
                window.__chosen = [];
                var c = window.AntsGameType.create({ doc: document, host: host, modes: M, onChoose: function (id) { window.__chosen.push(id); } });
                window.__c = c; window.__host = host;
                c.paint({ id: 0, guest: false, disabled: false });
                return host.querySelectorAll('[role=option]').length;
            })()""")
            check(made == 9, "a list of nine rows (two real, seven added as data) is made with no other code (%s)" % made)
            v.ev("window.__host.querySelector('.gt-combo').click()")
            time.sleep(0.2)
            sizes = v.ev("(function () { var l = window.__host.querySelector('.gt-list'); return [l.clientHeight, l.scrollHeight, getComputedStyle(l).overflowY]; })()")
            check(sizes[0] <= 300 and sizes[1] > sizes[0] and sizes[2] == "auto", "the long list is at most 300 px high and scrolls inside itself (%s)" % sizes)
            v.ev("window.__host.querySelector('.gt-combo').dispatchEvent(new KeyboardEvent('keydown', { key: 'End', bubbles: true }))")
            check(v.ev("(function () { var l = window.__host.querySelector('.gt-list'); return l.scrollTop > 0; })()"), "End scrolls the last row into view")
            v.ev("window.__host.querySelector('.gt-combo').dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }))")
            check(v.ev("JSON.stringify(window.__chosen)") == "[8]", "Enter on the last row chooses its number (%s)" % v.ev("JSON.stringify(window.__chosen)"))
            r = v.ev("""(function () {
                var M = window.AntsGameModes, out = [];
                window.__c.paint({ id: 77, guest: false, disabled: false });
                out.push(window.__host.querySelector('.gt-name').textContent, M.tagOf(77), M.guestLine(77), M.guestLine(77, 'Brawl'));
                window.__c.paint({ id: 77, guest: true, disabled: false });
                out.push(window.__host.querySelector('.gt-guest').textContent, window.__host.querySelectorAll('[aria-selected=true]').length);
                return JSON.stringify(out);
            })()""")
            check(r == '["New mode","New mode","Game type: New mode","Game type: Brawl","Game type: New mode",0]', "a mode this browser does not know: New mode for its name and tag, the server's name for a guest when it sends one, no row marked (%s)" % r)
            check(not v.errors(), "no page threw anything")
    finally:
        for v in people:
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
