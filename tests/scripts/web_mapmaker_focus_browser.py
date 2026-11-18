#!/usr/bin/env python3
"""Typing in the map maker's boxes, in a REAL browser (opt-in; see tests/scripts/test_web_mapmaker_focus.sh and docs/MAP_EDITOR.md).

A person who types in a box of a dialog must keep the focus: after EVERY key the box is the same node, still in the page, with the focus, holding what was typed, and the caret is where the typing
left it (a key in the middle of a text goes in the middle). An input method (a composition) is not cut off. The node check (web_mapmaker_*_check.js, with the fake DOM of web_mapmaker_fake_dom.js)
does this on a fake that behaves as a browser does; this one does it with the browser's own keys (the DevTools protocol's Input.dispatchKeyEvent, imeSetComposition and insertText), so what
a fake cannot know (that a box that a script takes out of the page and puts back has lost the focus, and that a composition is gone with it) is shown by Chromium.

Needs a Chromium-based browser (CHROME=/path/to/it to name one) and Python 3, nothing else: the page is a small one of its own that this script serves on a port of its own, and it loads the REAL
scripts of web/front/ (the dialogs, the properties, copy and save, submit for approval) with stand-ins for the map core and the server (the page's own start needs the built core, which this check does
not). Parts (every one runs in its own dialog, one dialog at a time):

  * submit      Submit for approval: the name of the map, the note (a box of lines), the player name shown as the maker (the box that lost the focus after each key), the same box after "Change my player name",
                a key in the middle of a text, an input method in the maker's box;
  * properties  Map properties of a Classic map (a line) and of an Extended map (a box of lines): the description (a key that does not fit is not taken and the caret stays), the minutes and the eggs
                (a letter is not taken and the caret stays);
  * size        New map and Resize map: the rows and the columns (the live line and the note under them change with every key);
  * open        Open a map: the search box (the list under it is made again with every key);
  * copy        Copy this map: the name (a name that is taken is told under the box with every key).

Exit status 0: every check passed; 1: a check failed, or the browser broke down; 3: no browser is installed (the shell wrapper says so and exits 0; with --strict it is 1). The broken copies of
tests/data/mapmaker_focus_browser_mutants.json must fail it (tools/mutate.py, run by hand: it needs the browser).
"""
import argparse
import json
import os
import shutil
import sys
import threading
import time
import http.server

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from web_aspect_check import Browser, NotReachable, Tab                      # noqa: E402
from web_hidden_check import find_browser, free_port                         # noqa: E402
import quick_http                                                           # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
FRONT = os.path.join(REPO, "web", "front")
SCRIPTS = ("mapmaker_checks.js", "mapmaker_sizes.js", "mapmaker_dialog.js", "mapmaker_props.js", "mapmaker_store.js", "mapmaker_convert.js", "mapmaker_submit.js")
STYLES = ("classic.css", "mapmaker.css")
FAILURES = []


PAGE = """<!doctype html>
<meta charset="utf-8"><title>focus</title>
<link rel="stylesheet" href="front/classic.css"><link rel="stylesheet" href="front/mapmaker.css">
<header id="top"><button id="opener" type="button">Open</button></header><main id="page"><p>The page behind the dialog.</p></main><div id="host"></div>
%s
<script src="harness.js"></script>
"""

# The page's own dialogs on stand-ins: a core that answers what the dialogs ask, a store of maps and a door of the server
HARNESS = r"""
(function () {
    'use strict';
    var doc = document, host = doc.getElementById('host'), top = doc.getElementById('top'), main = doc.getElementById('page');
    var dialogs = MapDialog.create(doc, host, [top, main]), closed = [];
    var limits = MapSizes.limitsOf({ classic: { rows: 90, columns: 130, cells: 10000 } }, { classic: { rows: 256, columns: 256, cells: 65536 }, extended: { rows: 1024, columns: 1024, cells: 262144 } });
    function make(kind, rows, columns) {
        if (kind === 'classic' ? rows > 90 || columns > 130 || rows * columns > 10000 : rows > 1024 || columns > 1024 || rows * columns > 262144)
            return { status: 1, error: 'a blank map of ' + rows + ' rows x ' + columns + ' columns cannot be written: the grid has ' + rows + ' rows x ' + columns + ' columns, more than the allowed size' };
        return { status: 0, bytes: new Uint8Array([kind === 'classic' ? 8 : 9]) };
    }
    function core(limit) {
        return { setProperties: function (bytes, description) { return { status: 0, json: { ok: true, changed: false, bytes: bytes.length, description_bytes: description.length, description_limit: limit, edits: [], reasons: [], warnings: [] } }; } };
    }
    function props(kind, text) {
        var raw = new Uint8Array(Array.prototype.map.call(text, function (c) { return c.charCodeAt(0); }));
        return { description: text, raw: raw, minutes: 12, eggs: 9, tile: 0x7FFE, kind: kind };
    }
    var map = new Uint8Array([8, 1, 2, 3]);
    var door = { status: 401, text: function () { return Promise.resolve('{}'); } };
    function submit(saved) {
        var kept = saved ? [{ id: '#maker', name: 'Mia' }] : [];
        var sent = { open: function (cb) { cb('ready'); }, all: function (cb) { cb(null, kept); }, put: function () {}, remove: function () {}, status: function () { return 'ready'; } };
        var env = {
            doc: doc, dialogs: dialogs, sent: sent, fetch: function () { return Promise.resolve(door); }, later: function (f, ms) { return setTimeout(f, ms); }, Abort: null, now: function () { return Date.now(); },
            store: { inLibrary: function (n) { return n.toLowerCase() === 'tiny'; }, libraryState: function () { return 'ready'; }, onLibrary: function () {} },
            parse: function () { return { kind: 'classic', rows: 31, columns: 31, rules: 0 }; }, thumb: function () { return null; }, tags: function (h, kind) { h.textContent = kind; },
            current: function () { return { name: 'Sunny Picnic', bytes: map, file: { kind: 'classic', rows: 31, columns: 31, rules: 0 }, report: { works: true, headline: 'Works in the original game', findings: [], kind: 'classic' } }; },
            showFinding: function () {}, onOpenCopy: function () {}, onSee: function () {}, clipboard: null, link: function () { return location.href; }
        };
        var sub = MapSubmit.create(env);
        sub.load(function () { sub.open(); });
    }
    var entries = [];
    for (var i = 0; i < 120; i++) entries.push({ file: 'Map ' + i + '.lvl', name: (i % 3 ? 'Garden ' : 'Mosh Pit ') + i, original: false, order: i, width: 60, height: 60, minutes: 10, players: 4, description: 'A map number ' + i, plays: i, thumb: '' });
    var library = { loadLibrary: function () {}, libraryState: function () { return 'ready'; }, libraryList: function () { return entries; }, fetchMap: function (f, cb) { cb('', new Uint8Array([8])); }, onLibrary: function () {} };
    var OPEN = {
        'submit': function () { submit(false); },
        'submit-saved': function () { submit(true); },
        'props-classic': function () { MapProps.open({ doc: doc, dialogs: dialogs, core: core(29), props: props('classic', 'A picnic'), bytes: function () { return map; }, apply: function () { return ''; } }); },
        'props-extended': function () { MapProps.open({ doc: doc, dialogs: dialogs, core: core(255), props: props('extended', 'A picnic'), bytes: function () { return map; }, apply: function () { return ''; } }); },
        'newmap': function () { MapDialog.newMap({ doc: doc, dialogs: dialogs, limits: limits, make: make, onMade: function () {} }); },
        'resize': function () { MapDialog.resizeMap({ doc: doc, dialogs: dialogs, limits: limits, kind: 'classic', rows: 31, columns: 31, onResize: function () {} }); },
        'open': function () { MapDialog.openMap({ doc: doc, dialogs: dialogs, store: library, numbered: function (n) { return n + ' 2'; }, onDisk: function () {}, onChoose: function () {} }); },
        'copy': function () {
            var cur = { name: 'Valley', bytes: map, file: { rows: 40, columns: 40, rules: 0 }, kind: 'classic', report: { headline: 'Works in the original game' } };
            MapConvert.create({ doc: doc, dialogs: dialogs, core: {}, limits: { classic: { rows: 90, columns: 130, cells: 10000, openCells: 65536 }, extended: { side: 512, cells: 262144, longest: 1024 } }, current: function () { return cur; },
                thumb: function () { return null; }, tags: function (h, kind) { h.textContent = kind; }, onMade: function () {}, crop: function () {}, taken: function (n) { return ['valley', 'valley 2'].indexOf(n.toLowerCase()) >= 0; } }).copy();
        }
    };
    window.__h = {
        open: function (what) { dialogs.close(); OPEN[what](); return dialogs.isOpen(); },
        close: function () { dialogs.close(); },
        pick: function (sel) { window.__f = doc.querySelector(sel); window.__sel = sel; return !!window.__f; },
        find: function (sel) { window.__f = doc.querySelector(sel); window.__sel = sel; if (window.__f) window.__f.focus(); return !!window.__f; },
        state: function () {
            var f = window.__f;
            return JSON.stringify({ same: !!f && doc.activeElement === f, inPage: !!f && f.isConnected && doc.querySelector(window.__sel) === f, value: f ? f.value : null, at: f ? f.selectionStart : null, to: f ? f.selectionEnd : null });
        },
        caret: function (n) { window.__f.setSelectionRange(n, n); },
        click: function (text) { var b = Array.prototype.filter.call(doc.querySelectorAll('button'), function (x) { return x.textContent === text; })[0]; if (b) b.click(); return !!b; },
        active: function () { return doc.activeElement ? doc.activeElement.id || doc.activeElement.tagName : ''; }
    };
    window.__ready = true;
})();
"""


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        path = self.path.split("?")[0]
        if path in ("/", "/index.html"):
            body, kind = (PAGE % "\n".join('<script src="front/%s"></script>' % s for s in SCRIPTS)).encode("utf-8"), "text/html; charset=utf-8"
        elif path == "/harness.js":
            body, kind = HARNESS.encode("utf-8"), "application/javascript"
        elif path.startswith("/front/") and os.path.basename(path) in SCRIPTS + STYLES + ("LibreFranklin-Medium.ttf",):
            with open(os.path.join(FRONT, os.path.basename(path)), "rb") as f:
                body = f.read()
            kind = "text/css" if path.endswith(".css") else "font/ttf" if path.endswith(".ttf") else "application/javascript"
        else:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def fail(what):
    FAILURES.append(what)
    print("    FAILED: " + what)


def wait_for(condition, timeout=8.0, step=0.1):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            if condition():
                return True
        except (RuntimeError, TimeoutError):
            pass
        time.sleep(step)
    return False


class Box:
    """One dialog, one box: the keys of a person (real key events of the browser) and what the page has after each of them"""

    def __init__(self, tab, dialog, selector, label):
        self.tab, self.label = tab, label
        if not tab.ev("__h.open(%s)" % json.dumps(dialog)):
            fail("%s: the dialog did not open" % label)
            self.ok = False
            return
        time.sleep(0.15)
        self.ok = bool(wait_for(lambda: tab.ev("__h.find(%s)" % json.dumps(selector)), 5))
        if not self.ok:
            fail("%s: the box %s is not in the page" % (label, selector))

    def retarget(self, selector, label):
        """the box that the page has now under this selector (not given the focus: what the page did with it is what is looked at)"""
        self.label = label
        self.ok = bool(self.tab.ev("__h.pick(%s)" % json.dumps(selector)))
        if not self.ok:
            fail("%s: the box %s is not in the page" % (label, selector))

    def state(self):
        return json.loads(self.tab.ev("__h.state()"))

    def key(self, ch):
        if ch == "\n":
            params = {"key": "Enter", "code": "Enter", "windowsVirtualKeyCode": 13, "text": "\r", "unmodifiedText": "\r"}
        else:
            params = {"key": ch, "text": ch, "unmodifiedText": ch}
        self.tab.call("Input.dispatchKeyEvent", dict(params, type="keyDown"))
        self.tab.call("Input.dispatchKeyEvent", {"type": "keyUp", "key": params["key"]})

    def type(self, text, what=""):
        """every character is a key; after each one the box is the same node, in the page, with the focus"""
        if not self.ok:
            return
        for i, ch in enumerate(text):
            self.key(ch)
            s = self.state()
            if not s["inPage"]:
                return fail("%s%s: after key %d (%r) the box was taken out of the page and another put in its place" % (self.label, what, i + 1, ch))
            if not s["same"]:
                return fail("%s%s: after key %d (%r) the box lost the focus (it is on %s)" % (self.label, what, i + 1, ch, self.tab.ev("__h.active()")))

    def expect(self, value, caret=None, what=""):
        if not self.ok:
            return
        s = self.state()
        if s["value"] != value:
            fail("%s%s: the box holds %r, not %r" % (self.label, what, s["value"], value))
        elif caret is not None and (s["at"] != caret or s["to"] != caret):
            fail("%s%s: the caret is at %r, not %d" % (self.label, what, s["at"], caret))

    def clear(self):
        if self.ok:
            self.tab.ev("(function () { __f.value = ''; })()")

    def caret(self, n):
        if self.ok:
            self.tab.ev("__h.caret(%d)" % n)

    def compose(self, steps, commit, what=" (an input method)"):
        """an input method: the text of the composition grows, then it is committed; the box must be the same node with the focus all the way, and hold the committed text"""
        if not self.ok:
            return
        before = self.state()["value"]
        at = self.state()["at"]
        for text in steps:
            self.tab.call("Input.imeSetComposition", {"text": text, "selectionStart": len(text), "selectionEnd": len(text)})
            s = self.state()
            if not s["inPage"] or not s["same"]:
                return fail("%s%s: the composition %r was cut off: the box was taken from under it" % (self.label, what, text))
        self.tab.call("Input.insertText", {"text": commit})
        s = self.state()
        if not s["inPage"] or not s["same"]:
            return fail("%s%s: the box is not the same node with the focus after the composition" % (self.label, what))
        want = before[:at] + commit + before[at:]
        if s["value"] != want:
            fail("%s%s: the box holds %r, not %r" % (self.label, what, s["value"], want))


def run(tab):
    # ---- submit
    print("  submit")
    b = Box(tab, "submit", "#sub-name", "Submit, the name of the map")
    b.type(" Two"); b.expect("Sunny Picnic Two", 16)
    b = Box(tab, "submit", "#sub-note", "Submit, the note")
    b.type("Two ants\nmeet here"); b.expect("Two ants\nmeet here", 18)
    b = Box(tab, "submit", "#sub-maker", "Submit, the maker's name (the box that lost the focus after each key)")
    b.type("Mia Pip"); b.expect("Mia Pip", 7)
    b.caret(3); b.type("X", " in the middle"); b.expect("MiaX Pip", 4, " in the middle")
    b = Box(tab, "submit", "#sub-maker", "Submit, the maker's name")
    b.compose(["n", "ni", "ni h"], "你")
    b = Box(tab, "submit-saved", "#sub-name", "Submit with a saved name")
    b.type(" 2"); b.expect("Sunny Picnic 2", 14)
    if b.ok:
        if not tab.ev("__h.click('Change my player name')"):
            fail("Submit with a saved name: there is no button 'Change my player name'")
        else:
            time.sleep(0.15)
            if tab.ev("__h.active()") != "sub-maker":
                fail("Submit with a saved name: 'Change my player name' did not give the focus to the box (%s has it)" % tab.ev("__h.active()"))
            b.retarget("#sub-maker", "Submit, after 'Change my player name'")
            b.clear(); b.type("Pip"); b.expect("Pip", 3)
    # ---- properties
    print("  properties")
    b = Box(tab, "props-classic", "#p-desc", "Properties, the description of a Classic map")
    b.type(" at play"); b.expect("A picnic at play", 16)
    b.caret(2); b.type("X", " in the middle"); b.expect("A Xpicnic at play", 3, " in the middle")
    b.compose(["n", "ni"], "你")
    b = Box(tab, "props-classic", "#p-desc", "Properties, the description of a Classic map")
    b.clear(); b.type("a" * 29); b.expect("a" * 29, 29)
    b.caret(10); b.type("Z", " that makes it too long")
    s = b.state() if b.ok else None
    if s and (len(s["value"]) != 29 or s["at"] != 11 or not s["same"]):
        fail("Properties, the description: a key that makes the text too long: the end is cut and the caret is after the key (value %d long, caret %r)" % (len(s["value"]), s["at"]))
    b = Box(tab, "props-extended", "#p-desc", "Properties, the description of an Extended map (a box of lines)")
    b.type(" by the river"); b.expect("A picnic by the river", 21)
    b.caret(2); b.type("\n", " a line break in the middle")
    s = b.state() if b.ok else None
    if s and (s["value"] != "A  picnic by the river" or s["at"] != 3 or not s["same"]):
        fail("Properties, the description of an Extended map: a line break in the middle becomes a space and the caret is after it (%r, caret %r)" % (s["value"], s["at"]))
    for sel, name in (("#p-minutes", "the minutes"), ("#p-eggs", "the eggs")):
        b = Box(tab, "props-classic", sel, "Properties, " + name)
        b.clear(); b.type("45"); b.expect("45", 2)
        b.caret(1); b.type("x", " a letter in the middle"); b.expect("45", 1, " a letter in the middle")
    # ---- size
    print("  size")
    for dialog, rows, columns, name in (("newmap", "#size-rows", "#size-columns", "New map"), ("resize", "#resize-rows", "#resize-columns", "Resize")):
        b = Box(tab, dialog, rows, name + ", the rows")
        b.clear(); b.type("120"); b.expect("120", 3)
        b = Box(tab, dialog, columns, name + ", the columns")
        b.clear(); b.type("2000"); b.expect("2000", 4)
        b.caret(1); b.type("5", " in the middle"); b.expect("25000", 2, " in the middle")
    # ---- open
    print("  open")
    b = Box(tab, "open", ".open-q", "Open a map, the search")
    b.type("mosh pit"); b.expect("mosh pit", 8)
    b.compose(["n", "ni"], "你")
    # ---- copy
    print("  copy")
    b = Box(tab, "copy", "input[type=text]", "Copy, the name")
    b.clear(); b.type("Valley 2"); b.expect("Valley 2", 8)
    b.caret(3); b.type("X", " in the middle"); b.expect("ValXley 2", 4, " in the middle")
    tab.ev("__h.close()")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--browser", help="the path of a Chromium-based browser (default: CHROME, or the usual places)")
    ap.add_argument("--strict", action="store_true", help="a missing browser is a failure, not a skip (tools/mutate.py runs the check so: a skip would count a broken copy as caught)")
    ap.add_argument("--front", help="the folder of the scripts and styles (default: web/front; a folder of broken copies shows that the check can fail)")
    args = ap.parse_args()
    global FRONT
    FRONT = os.path.abspath(args.front) if args.front else FRONT
    path = find_browser(args.browser or os.environ.get("CHROME"))
    if not path:
        print("  no Chromium-based browser found (CHROME=/path/to/it names one)")
        return 1 if args.strict else 3
    server = quick_http.QuickThreadingHTTPServer(("127.0.0.1", free_port()), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = "http://127.0.0.1:%d/" % server.server_address[1]
    browser = None
    try:
        browser = Browser(path)
        tab = Tab(browser)
        navigation = tab.call("Page.navigate", {"url": url})
        if navigation.get("errorText"):
            raise NotReachable("the page does not load: " + navigation["errorText"])
        if not wait_for(lambda: tab.ev("window.__ready === true"), 10, 0.2):               # (a browser that is slow to begin gets one more try)
            tab.call("Page.navigate", {"url": url})
            if not wait_for(lambda: tab.ev("window.__ready === true"), 20, 0.2):
                print("  FAILED: the page did not start (a script of web/front/ did not load)")
                return 1
        run(tab)
    except NotReachable as e:
        print("  " + str(e))
        return 1 if args.strict else 3
    finally:
        if browser:
            browser.close()
        server.shutdown()
    print("  %d checks failed" % len(FAILURES) if FAILURES else "  every box keeps the focus with every key, the caret and the composition")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
