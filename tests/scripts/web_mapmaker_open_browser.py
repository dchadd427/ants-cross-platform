#!/usr/bin/env python3
"""The Map Maker's "Start from a game map" in a REAL browser (opt-in; see tests/scripts/test_web_mapmaker_open.sh and docs/MAP_EDITOR.md).

Needs the web image of this tree served by nginx (tools/web_without_docker.py builds and serves it without Docker; the game server is not needed: the page then has no list from the site and offers the
six originals), a Chromium-based browser and Python 3. A throwaway headless browser opens the editor like a person:

  * originals   every one of the six original maps is fetched from the site's /maps/files/ and is byte for byte the file of Original-Ants/Maps (read only);
  * open        Open, Start from a game map, TREASURE, Use as starting point: the map opens as a COPY under a free name, with the size of the original, and the original file on the disk is untouched;
  * words       a map that the site answers 404 for says "not available from the site right now", a 500 says the site could not send it, and a request that fails says "check your connection".

Exit status 0: every check passed; 1: a check failed or something broke; 3: the environment is not there.
"""
import argparse
import hashlib
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from web_aspect_check import Browser, NotReachable, Tab                      # noqa: E402
from web_hidden_check import find_browser                                    # noqa: E402
import web_mapmaker_browser as editor                                        # noqa: E402

REPO = editor.REPO
ORIGINALS = ("TINY.LVL", "SMALL.LVL", "MEDIUM.LVL", "GAUNTLET.LVL", "TREASURE.LVL", "ISLANDS.LVL")
# the editor takes its fetch at boot: this wraps it before the page runs, and answers three of the original maps with a 404, a 500 and a failed request
WRAP = r"""
(function () {
    var real = window.fetch.bind(window);
    window.__realFetch = real;
    window.fetch = function (url, init) {
        var u = String(url);
        if (u.indexOf('/maps/files/SMALL.LVL') === 0) return Promise.resolve(new Response('{"error":"not found"}', { status: 404 }));
        if (u.indexOf('/maps/files/MEDIUM.LVL') === 0) return Promise.resolve(new Response('oops', { status: 500 }));
        if (u.indexOf('/maps/files/GAUNTLET.LVL') === 0) return Promise.reject(new TypeError('Failed to fetch'));
        return real(url, init);
    };
})();
"""


USE = "[].filter.call(document.querySelectorAll('.open-dlg .dlg-foot .btn'), function (b) { return b.textContent === 'Use as starting point'; })[0]"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_original(name):
    with open(os.path.join(REPO, "Original-Ants", "Maps", name), "rb") as f:
        return f.read()


def run(tab, args, check):
    wait_for, Page = editor.wait_for, editor.Page
    tab.emulate(1280, 800, 1)
    before = dict((n, sha(read_original(n))) for n in ORIGINALS)
    page = Page(tab, args.web)
    check(page.open(init_script=WRAP) and page.ready(), "the editor starts with its map core")

    print("[web map maker open] the six originals from the site")
    for name in ORIGINALS:
        page.ev("window.__got = undefined; __realFetch('/maps/files/%s', {cache: 'no-store'}).then(function (r) { return r.arrayBuffer().then(function (b) { return crypto.subtle.digest('SHA-256', b).then(function (d) {"
                " window.__got = [r.status, b.byteLength, Array.prototype.map.call(new Uint8Array(d), function (x) { return ('0' + x.toString(16)).slice(-2); }).join('')]; }); }); }).catch(function (e) { window.__got = ['error', String(e)]; })" % name)
        wait_for(lambda: page.ev("window.__got !== undefined"), 15, 0.05)
        got = page.ev("window.__got")
        check(got and got[0] == 200 and got[1] == len(read_original(name)) and got[2] == before[name], "%s comes from the site byte for byte (%s)" % (name, got))

    print("[web map maker open] Open, Start from a game map, TREASURE")
    page.press_act("open")
    check(wait_for(lambda: page.ev("!!document.querySelector('.open-dlg')"), 8), "the Open a map box opens")
    check(wait_for(lambda: page.ev("document.querySelectorAll('.open-row[data-file]').length") >= 6, 8), "the list of game maps shows the six originals")
    check(page.ev("!!document.querySelector('.open-row[data-file=\"TREASURE.LVL\"]')"), "Treasure is in the list")
    page.ev("document.querySelector('.open-row[data-file=\"TREASURE.LVL\"]').click()")
    check(page.ev("document.querySelector('.open-row.sel').getAttribute('data-file')") == "TREASURE.LVL", "Treasure is the chosen row")
    page.ev(USE + ".click()")
    check(wait_for(lambda: not page.ev("!!document.querySelector('.open-dlg')"), 15), "the box closes once the map has come (no 'check your connection')")
    s = page.state()
    check(s["name"] not in ("", "TREASURE") and "reasure" in s["name"], "the map opens as a copy under a free name (%s)" % s["name"])
    check(s["tags"] and s["tags"][0] == "Classic map", "it is a Classic map (%s)" % s["tags"])
    copy = s
    page.choose_file(os.path.join(REPO, "Original-Ants", "Maps", "TREASURE.LVL"))
    check(wait_for(lambda: page.state()["name"] == "TREASURE"), "the same file opened from the disk, for the size")
    disk = page.state()
    check((copy["rows"], copy["columns"]) == (disk["rows"], disk["columns"]), "the copy has the size of the original (%sx%s)" % (copy["rows"], copy["columns"]))
    check(all(sha(read_original(n)) == before[n] for n in ORIGINALS), "the original files on the disk are untouched")

    print("[web map maker open] the words when a map does not come")
    expected = {"SMALL.LVL": "not available from the site right now", "MEDIUM.LVL": "The site could not send this map", "GAUNTLET.LVL": "Check your connection"}
    for name, words in expected.items():
        page.press_act("open")
        check(wait_for(lambda: page.ev("!!document.querySelector('.open-dlg')"), 8), "%s: the box opens" % name)
        wait_for(lambda: page.ev("!!document.querySelector('.open-row[data-file=\"%s\"]')" % name), 8)
        page.ev("document.querySelector('.open-row[data-file=\"%s\"]').click()" % name)
        page.ev(USE + ".click()")
        check(wait_for(lambda: "bad" in (page.ev("(document.querySelector('.open-copy-note') || {}).className || ''") or ""), 15), "%s: the note turns red" % name)
        text = page.ev("document.querySelector('.open-copy-note').textContent")
        check(words in text, "%s: the note says: %s" % (name, text))
        check(not page.ev(USE + ".disabled"), "%s: the box can be used again" % name)
        page.key("Escape")
        wait_for(lambda: not page.ev("!!document.querySelector('.open-dlg')"), 8)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--web", required=True, help="the address of the folder that holds mapmaker.html, e.g. http://127.0.0.1:19980/")
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    args = ap.parse_args()
    failures = []
    count = [0]

    def check(ok, what):
        count[0] += 1
        print("  %s %s" % ("ok  " if ok else "FAIL", what))
        if not ok:
            failures.append(what)

    path = find_browser(args.browser)
    if not path:
        print("  SKIP: no Chromium-based browser found (give one with --browser or CHROME)")
        return 3
    browser = None
    try:
        browser = Browser(path)
        tab = Tab(browser)
        try:
            run(tab, args, check)
        finally:
            tab.close()
    except NotReachable as e:
        print("  SKIP: %s" % e)
        return 3
    except (RuntimeError, TimeoutError, ConnectionError, OSError, ValueError) as e:
        print("  FAIL: the browser or the page broke down during the check: %s" % e)
        return 1
    finally:
        if browser is not None:
            browser.close()
    print("[web map maker open] %d checks, %d failed" % (count[0], len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
