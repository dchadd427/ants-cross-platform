#!/usr/bin/env python3
"""The lines of the front page's chat never overlap, in a REAL browser (opt-in; docs/HOME_CHAT.md).

The log (`.gc-log`) is a column of flex items that scrolls. A flex item may shrink by default, so a full log could squeeze a line below the height of its wrapped text, and its words would draw over the
next line (seen on a phone, 2026-11-17). This check serves a page of its own with the REAL web/home/chat.css (and the site's web/front/classic.css), fills a log of the phone's size with long lines
(administrators' and moderators' lines among them), a day divider and short lines, and measures every child of the log: none may be lower than its own content, and no line may take a min-height
from a rule meant for something else (a button's .gc-admin once gave every administrator's line 46 px, and the full log squeezed them to it); and with a stray min-height put on every
line on purpose, the log's own guard (`.gc-log > * { flex-shrink: 0; }`) still keeps every line whole. It runs at 390, 360 and 320 px wide and on a desktop.

Needs a Chromium-based browser (CHROME=/path/to/it names one) and Python 3. Exit status 0: every check passed; 1: a check failed or the browser broke down; 3: no browser (with --strict that is 1).
--home DIR takes the styles from another folder (a broken copy shows that the check can fail: tests/data/home_chat_lines_mutants.json).
"""
import argparse
import http.server
import json
import os
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from web_aspect_check import Browser, NotReachable, Tab                      # noqa: E402
from web_hidden_check import find_browser, free_port                         # noqa: E402
import quick_http                                                           # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
HOME = os.path.join(REPO, "web", "home")
FRONT = os.path.join(REPO, "web", "front")
LONG = "it runs the simulated games where it has the old bot play against the new bot and see if there's an improvement and it does it like 2000 games in like a second"

PAGE = """<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<link rel="stylesheet" href="/front/classic.css"><link rel="stylesheet" href="/home/chat.css">
<style>body { margin: 0; } .frame { height: 480px; display: flex; flex-direction: column; } .frame .gc-body { height: 100%%; }</style></head>
<body><div class="frame"><div class="gc-body"><div class="gc-log" id="log"></div></div></div>
<script>
var log = document.getElementById('log'), LONG = %s;
function line(t, text, cls) { var d = document.createElement('div'); d.className = 'gc-line' + (cls || ''); var s = document.createElement('span'); s.className = 'gc-t'; s.textContent = t; d.appendChild(s);
  var w = document.createElement('b'); w.className = 'gc-who'; w.textContent = 'dAwGy: '; d.appendChild(w); var b = document.createElement('bdi'); b.className = 'gc-text'; b.textContent = text; d.appendChild(b); log.appendChild(d); }
function day(word) { var d = document.createElement('div'); d.className = 'gc-day'; d.textContent = word; log.appendChild(d); }
for (var i = 0; i < 12; i++) line('11:55 AM', i %% 3 ? LONG : 'short', i %% 2 ? ' gc-mod gc-admin' : (i %% 3 ? ' gc-mod' : ''));       // (an administrator's line carries gc-admin: no other rule may give it a size)
day('Today');
for (var j = 0; j < 8; j++) line('2:58 PM', j %% 2 ? LONG + ' ' + LONG : 'no bots!', ' gc-mod gc-admin');
window.__ready = true;
</script></body></html>""" % json.dumps(LONG)


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/":
            body, kind = PAGE.encode("utf-8"), "text/html; charset=utf-8"
        elif path in ("/home/chat.css", "/front/classic.css"):
            base = HOME if path.startswith("/home/") else FRONT
            with open(os.path.join(base, os.path.basename(path)), "rb") as f:
                body, kind = f.read(), "text/css"
        else:
            self.send_response(404)
            self.end_headers()
            return
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


MEASURE = """JSON.stringify([...document.getElementById('log').children].map(function (e) { var r = e.getBoundingClientRect(), c = getComputedStyle(e); return { cls: e.className, h: Math.round(r.height), need: e.scrollHeight, minh: c.minHeight }; }))"""


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--browser")
    ap.add_argument("--strict", action="store_true")
    ap.add_argument("--home", help="the folder of chat.css (default: web/home)")
    args = ap.parse_args()
    global HOME
    HOME = os.path.abspath(args.home) if args.home else HOME
    path = find_browser(args.browser or os.environ.get("CHROME"))
    if not path:
        print("  no Chromium-based browser found (CHROME=/path/to/it names one)")
        return 1 if args.strict else 3
    server = quick_http.QuickThreadingHTTPServer(("127.0.0.1", free_port()), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = "http://127.0.0.1:%d/" % server.server_address[1]
    failures, browser = [], None
    try:
        browser = Browser(path)
        tab = Tab(browser)
        for width, height, mobile in ((390, 844, True), (360, 640, True), (320, 568, True), (1280, 800, False)):
            tab.emulate(width, height, 3 if mobile else 1, mobile=mobile)
            tab.call("Page.navigate", {"url": url})
            deadline = time.time() + 15
            while time.time() < deadline and tab.ev("window.__ready === true") is not True:
                time.sleep(0.2)
            time.sleep(0.3)
            rows = json.loads(tab.ev(MEASURE))
            squeezed = [r for r in rows if r["h"] + 1 < r["need"]]
            sized = [r for r in rows if "gc-line" in r["cls"] and r["minh"] not in ("auto", "0px")]       # (a rule of another element that matches a line's class, as gc-admin once did, gives it a height)
            ok = len(rows) > 20 and not squeezed and not sized
            print("  %s %dx%d: %d lines, none lower than its text" % ("ok  " if ok else "FAIL", width, height, len(rows)) if ok else "  FAIL %dx%d: %d of %d lines squeezed, %d with a min-height of their own, e.g. %s" % (width, height, len(squeezed), len(rows), len(sized), (squeezed or sized)[:2]))
            if not ok:
                failures.append(width)
            # the guard itself: a stray rule that gives every line a small min-height (as .gc-admin once did) must still not let the full log squeeze a line below its text
            tab.ev("(function () { var s = document.createElement('style'); s.textContent = '.gc-line { min-height: 20px; }'; document.head.appendChild(s); })()")
            time.sleep(0.2)
            stray = [r for r in json.loads(tab.ev(MEASURE)) if r["h"] + 1 < r["need"]]
            print("  %s %dx%d with a stray min-height on every line: %s" % ("ok  " if not stray else "FAIL", width, height, "no line squeezed" if not stray else "%d squeezed" % len(stray)))
            if stray:
                failures.append(width)
    except NotReachable as e:
        print("  " + str(e))
        return 1 if args.strict else 3
    finally:
        if browser:
            browser.close()
        server.shutdown()
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
