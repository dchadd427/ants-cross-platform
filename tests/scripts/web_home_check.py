#!/usr/bin/env python3
"""The front page and the way into and out of a game, in a REAL browser (opt-in; see tests/scripts/test_web_home.sh and docs/NETWORK_PORT.md, "The front page").

The owner: "when you click the play online button it opens in a new tab can we just make that the default. you can play single on the play online tab by setting to 1 player consolidate them".
Needs a running web page that has nginx's routes (the web image of this tree: `docker build -t ants-beta .`, run it on a port; the game server is not needed), a Chromium-based browser and
Python 3; nothing else (the DevTools protocol is spoken with the client of web_hidden_check.py, standard library only). Each part opens the site in a throwaway headless browser (its own
profile and port; nothing of yours is touched). What it checks:

  * the FRONT PAGE at "/" (a first visit: the two cards, Treasure, Medium opponents in all three bases, 2 players on the Host card; the header links, the footer's version and build, the picture's two buttons,
    the name field, the font; the line of numbers (not there, or well formed); 15 widths from 320 to 1600 px (no sideways scroll; Host and Join side by side from 900 to 1219 px only); the
    contrast of all text at 1440 and 390 px);
  * the LEVEL BUTTONS: each opponent has a group of radio buttons (None, Easy, Medium, Hard) that the keyboard drives as a browser's radio buttons: the arrows move and check, Tab goes to the next group,
    the focused button shows its outline;
  * PLAY: START takes THIS tab to the game page on the chosen map (no new tab or window is opened), the game's own arguments are the chosen map, the setup screen's own
    START (--play), the Medium bots of the three other bases and the typed name; the quick help closes with Enter and the match starts at once, with the "Get ready" dialog; the bots'
    scores, which the HUD shows at the bottom ("Bot (Medium)"), rise from 0, so their ants move (the bots are run by the game in the browser: no server); with Opponents None the match
    has no bots (no labels, nothing changes) and no --bot argument;
  * MENU: the game page's header link goes back to the front page in the same tab, and asks first while a match runs ("Leave the game and go back to the menu?": No stays, Yes leaves); the
    front page remembers the choices;
  * the OLD ADDRESSES: /?join=...&room=... and /?embed=1 open the game page (a shared link asks for a name), /four.html?room=... goes to /?room=... and the front page asks for the name of a
    shared link, /play.html with nothing is today's front page (the setup screen, no arguments);
  * the HOST: 3 players and Host the match make the room panel, and "Play in this tab" takes this tab to the game page with the room, the name and the bots of the leader's START;
  * the GAME PAGE in the front page's look (web/shell.html at /play.html): the clay, the frame, the font and the logo; the loading screen (the logo, a teal bar in a black box) and its failure card;
    19 widths from 320 to 1600 px with no sideways scroll, the header's seven controls (one row with "More" up to 700 px: the five other links, over the picture), the picture, the bar and the guide
    inside the frame, a phone on its side; the logo goes back to the front page as Menu does (it asks first while a match runs or a room is joined: No stays); the two pairs under the game
    (the chosen one is pressed in; the clicks keep their ids and what they remember); the name step of a shared link; the contrast of every text (4.5:1) in each of these states.
Exit status 0: every check passed; 1: a check failed; 3: the check could not be made because the environment is not there (no browser, nothing answers at the page's address).
"""
import argparse
import json
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import web_aspect_check as aspect                                            # noqa: E402
from web_aspect_check import Browser, NotReachable, Tab, read_png            # noqa: E402
from web_hidden_check import find_browser                                    # noqa: E402


# The contrast of every visible text with its background (WCAG: (L1 + 0.05) / (L2 + 0.05)); text on the clay tile is measured against the tile's two ends and text in the footer against the ends of
# its gradient, so the number is the worst case. Returns JSON: how many texts, the lowest ratio, and the three lowest.
CONTRAST_JS = """(function () {
  function parse(c) { var m = c.match(/rgba?\\(([^)]+)\\)/); if (!m) return null; var p = m[1].split(',').map(parseFloat); return { r: p[0], g: p[1], b: p[2], a: p.length > 3 ? p[3] : 1 }; }
  function lin(v) { v /= 255; return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); }
  function lum(c) { return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b); }
  function ratio(a, b) { var l1 = lum(a), l2 = lum(b); if (l1 < l2) { var t = l1; l1 = l2; l2 = t; } return (l1 + 0.05) / (l2 + 0.05); }
  var CLAY = [{ r: 219, g: 75, b: 19 }, { r: 251, g: 51, b: 91 }], BAR = [{ r: 0x2b, g: 0x68, b: 0x5f }, { r: 0x2b, g: 0x6b, b: 0x4f }];
  function bgOf(el) {
    for (var e = el; e && e.nodeType === 1; e = e.parentElement) {
      if (e.classList && e.classList.contains('bar')) return BAR;
      var c = parse(getComputedStyle(e).backgroundColor);
      if (e.tagName === 'BODY' || e.tagName === 'HTML') return CLAY;
      if (c && c.a > 0.99) return [c];
    }
    return CLAY;
  }
  var rows = [], walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
  while (walker.nextNode()) {
    var n = walker.currentNode, t = n.textContent.replace(/\\s+/g, ' ').trim(), el = n.parentElement;
    if (!t || !el || el.closest('[hidden]') || el.closest('script,style,noscript,option')) continue;
    var cs = getComputedStyle(el), r = el.getBoundingClientRect();
    if (cs.display === 'none' || cs.visibility === 'hidden' || parseFloat(cs.opacity) === 0 || r.width === 0 || r.height === 0) continue;
    var fg = parse(cs.color), worst = 99;
    bgOf(el).forEach(function (bg) { worst = Math.min(worst, ratio(fg, bg)); });
    rows.push([Math.round(worst * 100) / 100, t.slice(0, 40)]);
  }
  rows.sort(function (a, b) { return a[0] - b[0]; });
  return JSON.stringify({ texts: rows.length, lowest: rows.slice(0, 3) });
})()"""


def wait_for(condition, timeout=10.0, step=0.1):
    """The condition's value once it is true (a page that is being replaced has no execution context for a moment: that is not true yet), else the last value."""
    def look():
        try:
            return condition()
        except (RuntimeError, TimeoutError):
            return None
    deadline = time.time() + timeout
    value = look()
    while not value and time.time() < deadline:
        time.sleep(step)
        value = look()
    return value


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--web", required=True, help="the site, e.g. http://127.0.0.1:19980/")
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--shots", default="", help="a folder to save screenshots in")
    ap.add_argument("--ready-timeout", type=float, default=120.0, metavar="SECONDS", help="how long a page may take to get its game ready (default 120)")
    ap.add_argument("--only", default="", help="run only the parts whose name contains this text (front, levels, play, alone, menu, old, host, game)")
    ap.add_argument("--bot-seconds", type=float, default=45.0, help="how long the bots play before their scores are compared (default 45)")
    args = ap.parse_args()
    aspect.READY_TIMEOUT = args.ready_timeout
    web = args.web if args.web.endswith("/") else args.web + "/"

    failures = []
    count = [0]

    def check(ok, what):
        count[0] += 1
        print("  %s: %s" % ("ok  " if ok else "FAIL", what))
        if not ok:
            failures.append(what)

    def wanted(name):
        return not args.only or args.only in name

    path = find_browser(args.browser)
    if not path:
        print("  SKIP: no Chromium-based browser found (give one with --browser or CHROME)")
        return 3
    browser = None
    try:
        browser = Browser(path)
        tab = Tab(browser)
        dialogs = []
        answer = {"accept": True}

        def on_dialog(msg):
            if msg.get("method") == "Page.javascriptDialogOpening" and msg.get("sessionId") == tab.session:
                dialogs.append(msg["params"].get("message", ""))
                try:
                    tab.call("Page.handleJavaScriptDialog", {"accept": answer["accept"]})
                except (RuntimeError, TimeoutError):
                    pass

        tab.dt.handlers.append(on_dialog)

        def fresh_tab():
            """A new tab, the old one closed: a tab that was emulated as a phone, or whose window was resized while its page was up, goes on reporting the media of a touch screen (no hover, so no
            Fullscreen mouse control on the game page) whatever it is set to afterwards; a page that is loaded in a new tab, after one emulation of its size, has a mouse."""
            nonlocal tab
            old = tab
            tab = Tab(browser)
            old.close()

        def pages():
            """The number of tabs and windows of the browser (a new tab is a new page target)."""
            return len([t for t in browser.devtools.call("Target.getTargets")["targetInfos"] if t["type"] == "page"])

        def load(url, ready=False, clear=True, settle=1.0):
            tab.emulate(1440, 900, 1)
            if ready:
                tab.open(url, settle=settle)
            else:
                tab.open(url, wait=False)
                time.sleep(settle + 0.5)

        def clear_storage():
            tab.open(web + "lobby.html", wait=False)
            time.sleep(1.0)
            tab.ev("try { localStorage.clear(); sessionStorage.clear(); } catch (e) {} 1")

        def value(expression):
            return tab.ev(expression)

        def shot(name):
            tab.save_shot(args.shots, name)

        def score_strip(cv):
            """The rectangle of the bottom of the picture where the HUD shows the other players' scores: below the map view, whose last rows show the player's own ants (they move, bots or not), and
            left of the version and the frame counter (which change at every frame; the three score boxes end at 0.81 of the canvas's width, those start at 0.87). The canvas's rectangle ->
            [x0, y0, x1, y1] in page pixels. Measured: over 35 s of play 487 of its pixels change with the three Medium bots and none without them."""
            return [int(cv[0] + cv[2] * 0.17), int(cv[1] + cv[3] * 0.965), int(cv[0] + cv[2] * 0.85), int(cv[1] + cv[3] * 0.995)]

        def start_by_enter():
            """The quick help closes with Enter and, with --play, the match starts at once (an Enter that comes while the loading screen is still up only ends that: it is pressed again)."""
            tab.ev("document.getElementById('canvas').focus(); 1")
            key = lambda kind: tab.call("Input.dispatchKeyEvent", {"type": kind, "key": "Enter", "code": "Enter", "windowsVirtualKeyCode": 13, "text": "\r" if kind == "keyDown" else ""})
            for _ in range(4):
                key("keyDown")
                key("keyUp")
                if wait_for(lambda: tab.ev("Module._ants_match_running()") == 1, 2.5):
                    break
            else:
                return False
            return bool(wait_for(lambda: tab.ev("Module._ants_probe(5)") == 0, 60, 0.5))          # the "get ready" dialog closes by itself

        # ------------------------------------------------------------------------------------------------------------------------------------------------------------
        if wanted("front"):
            print("[web home] the front page: a first visit")
            clear_storage()
            load(web, settle=1.5)
            shot("home_front")
            info = json.loads(value("""JSON.stringify({title: document.title, path: location.pathname,
                hostPlayers: Array.prototype.filter.call(document.querySelectorAll('input[name=players]'), function (r) { return r.checked; }).map(function (r) { return r.value; }).join(),
                mapSolo: document.getElementById('map-solo').value, mapHost: document.getElementById('map-host').value,
                opponents: [1, 2, 3].map(function (n) { var r = document.querySelector('input[name=opponent-' + n + ']:checked'); return r ? r.value : '?'; }).join(),
                start: document.getElementById('play').getAttribute('aria-label'), host: document.getElementById('host').textContent.trim(),
                cards: [!document.getElementById('cards').hidden, !document.getElementById('how').hidden], name: document.getElementById('player-name').value,
                aspect: document.querySelector('input[name=aspect]:checked').value,
                links: Array.prototype.map.call(document.querySelectorAll('header a'), function (a) { return [a.textContent.trim(), a.getAttribute('href'), a.getAttribute('target')]; }),
                footer: document.querySelector('footer').textContent, scrollW: document.documentElement.scrollWidth, innerW: window.innerWidth})"""))
            check(info["title"].startswith("Ants (1998)") and info["path"] == "/", "the front page is at / and is titled Ants (1998) (%r)" % info["title"])
            check(info["mapSolo"] == "treasure" and info["mapHost"] == "treasure" and info["opponents"] == "medium,medium,medium" and info["hostPlayers"] == "2",
                  "a first visit: Treasure on both cards, Medium opponents in all three bases, 2 players on the Host card (%s, %s, %s, %s)" % (info["mapSolo"], info["mapHost"], info["opponents"], info["hostPlayers"]))
            check(info["start"] == "Start the game" and info["host"] == "Host the match" and info["cards"] == [True, True], "the two cards are there, with START and Host the match, and \"How it works\" (%s, %r)" % (info["start"], info["host"]))
            check(info["name"] == "" and info["aspect"] == "16:9", "no name yet, the picture is 16:9")
            hrefs = [l[1] for l in info["links"]]
            check("/asset_catalog/" in hrefs and "/changelog.html" in hrefs and any("github.com" in h and "issues" not in h for h in hrefs) and any(h.endswith("/issues") for h in hrefs),
                  "the header has the links: Sprites and sounds, Changelog, GitHub, Feedback (%s)" % [l[0] for l in info["links"]])
            check(all(l[2] == "_blank" for l in info["links"]), "the links that leave the page keep their new tab")
            check("Version" in info["footer"] and "@@" not in info["footer"] and "build" in info["footer"], "the footer names the version and the build (%r)" % info["footer"][:80])
            check(info["scrollW"] <= info["innerW"], "no horizontal scroll at 1440")
            check(bool(value("Array.from(document.fonts).some(function (f) { return f.family.indexOf('Libre Franklin') !== -1 && f.status === 'loaded'; })")), "the game's own font, Libre Franklin, is loaded from the site")
            stats = json.loads(value("JSON.stringify({hidden: document.getElementById('stats').hidden, text: document.getElementById('stats').textContent.replace(/\\s+/g, ' ').trim()})"))
            check(stats["hidden"] or re.match(r"^\d[\d,]* matches? being played \u00b7 \d[\d,]* players? online( \d[\d,]* games? played \(\d[\d,]* today\))?$", stats["text"]) is not None,
                  "the line of numbers is either not there (a site with no /stats) or says what it counts (%r)" % (stats,))
            def contrast(where):
                """Every text of the page is at least 4.5:1 (the help is opened for it, and closed again)."""
                found = json.loads(value("var how = document.getElementById('how'), was = how.open; how.open = true; var found = " + CONTRAST_JS + "; how.open = was; found"))
                check(found["texts"] > 40 and found["lowest"][0][0] >= 4.5, "%s: the text contrast is at least 4.5:1 for all %d texts (lowest: %s)" % (where, found["texts"], found["lowest"]))

            contrast("1440 px")
            # many widths (the layout changes at 700, 899/900, 1100 and 1219/1220 px): no sideways scroll at any of them, and on the online card Host and Join side by side from 900 to 1219 px only
            sizes = []
            for width, beside in ((320, False), (360, False), (700, False), (701, False), (768, False), (899, False), (900, True), (1024, True), (1100, True), (1101, True), (1219, True),
                                  (1220, False), (1280, False), (1366, False), (1600, False)):
                tab.emulate(width, 900, 1)
                time.sleep(0.3)
                m = json.loads(value("""JSON.stringify({scrollW: document.documentElement.scrollWidth, innerW: window.innerWidth,
                    host: document.querySelector('.online .block').getBoundingClientRect().toJSON(), join: document.querySelector('.online .block + .block').getBoundingClientRect().toJSON()})"""))
                sideways = m["scrollW"] > m["innerW"]
                together = m["join"]["left"] >= m["host"]["right"] - 1 and abs(m["join"]["top"] - m["host"]["top"]) < 2
                stacked = m["join"]["top"] >= m["host"]["bottom"] - 1
                if sideways or together != beside or (not beside and not stacked):
                    sizes.append((width, m["scrollW"], m["innerW"], "side by side" if together else "stacked"))
            check(not sizes, "no sideways scroll from 320 to 1600 px, and Host and Join are side by side from 900 to 1219 px only (wrong: %s)" % (sizes,))
            tab.emulate(390, 844, 2, mobile=True)
            tab.open(web, wait=False)
            time.sleep(1.5)
            phone = json.loads(value("JSON.stringify({scrollW: document.documentElement.scrollWidth, innerW: window.innerWidth, left: document.getElementById('who').getBoundingClientRect().left})"))
            check(phone["scrollW"] <= phone["innerW"] and phone["left"] >= 15, "phone width (390): no horizontal scroll, 16 px gutters (%s)" % phone)
            contrast("390 px")
            shot("home_front_phone")
            tab.emulate(1440, 900, 1)

        # ------------------------------------------------------------------------------------------------------------------------------------------------------------
        if wanted("levels"):
            print("[web home] the level buttons: radio groups that the keyboard drives")
            clear_storage()
            load(web, settle=1.5)

            def key(name, code):
                for kind in ("keyDown", "keyUp"):
                    tab.call("Input.dispatchKeyEvent", {"type": kind, "key": name, "code": name, "windowsVirtualKeyCode": code})

            def level(seat):
                return tab.ev("var r = document.querySelector('input[name=opponent-%d]:checked'); r ? r.value : '?'" % seat)

            groups = json.loads(value("""JSON.stringify([1, 2, 3].map(function (n) { var f = document.querySelector('input[name=opponent-' + n + ']').closest('fieldset');
                return [f.querySelector('legend').textContent, Array.prototype.map.call(f.querySelectorAll('input[type=radio]'), function (r) { return r.value; }).join()]; }))"""))
            check(groups == [["Red", ",easy,medium,hard"], ["Blue", ",easy,medium,hard"], ["Black", ",easy,medium,hard"]], "each opponent is a fieldset named by its legend, with the buttons None, Easy, Medium, Hard (%s)" % groups)
            tab.ev("document.getElementById('opponent-1-medium').focus(); 1")
            key("ArrowRight", 39)
            check(level(1) == "hard" and tab.ev("document.activeElement.id") == "opponent-1-hard", "ArrowRight moves from Medium to Hard and checks it (%s)" % level(1))
            key("ArrowLeft", 37)
            key("ArrowLeft", 37)
            key("ArrowLeft", 37)
            check(level(1) == "" and tab.ev("document.activeElement.id") == "opponent-1-none", "ArrowLeft three times goes to None (an empty value: no bot) (%r)" % level(1))
            key("ArrowDown", 40)
            check(level(1) == "easy", "ArrowDown is the same as ArrowRight (%r)" % level(1))
            check(level(2) == "medium" and level(3) == "medium", "the other two groups did not move (%s, %s)" % (level(2), level(3)))
            key("Tab", 9)
            check(tab.ev("document.activeElement.id") == "opponent-2-medium", "Tab leaves the group for the next one, at its checked button (%s)" % tab.ev("document.activeElement.id"))
            outline = json.loads(value("""(function () { var l = document.querySelector('label[for=opponent-2-medium]'); var c = getComputedStyle(l); return JSON.stringify([c.outlineStyle, c.outlineWidth, c.outlineColor]); })()"""))
            check(outline[0] != "none" and outline[1] == "3px", "the focused button has a visible outline of 3 px (%s)" % outline)
            key("ArrowRight", 39)
            check(level(2) == "hard" and level(1) == "easy", "the arrows of the next group move that group only (Blue %s, Red %s)" % (level(2), level(1)))
            key("ArrowRight", 39)
            check(level(2) == "" and tab.ev("document.activeElement.id") == "opponent-2-none", "... and go round from Hard to None, as a browser's radio buttons do (%r)" % level(2))
            key("ArrowLeft", 37)
            key("Tab", 9)
            check(tab.ev("document.activeElement.id") == "opponent-3-medium", "Tab goes on to the third group (%s)" % tab.ev("document.activeElement.id"))
            key("Tab", 9)
            check(tab.ev("document.activeElement.id") == "teams", "... and from there to the Teams, which three bots make a choice of (%s)" % tab.ev("document.activeElement.id"))
            seen = json.loads(value("""JSON.stringify([1, 2, 3].map(function (n) { return document.querySelector('input[name=opponent-' + n + ']:checked').value; }))"""))
            check(seen == ["easy", "hard", "medium"], "the three groups hold Easy, Hard, Medium after the keys (%s)" % seen)
            tab.ev("var n = document.getElementById('player-name'); n.value = 'Key'; n.dispatchEvent(new Event('input')); 1")
            tab.ev("document.getElementById('play').click(); 1")
            ok = wait_for(lambda: tab.ev("location.pathname") != "/", 15)
            where = tab.ev("location.pathname + location.search") if ok else ""
            check(ok and "bots=easy,hard,medium" in where and "map=treasure" in where, "START plays what the keys chose: bots=easy,hard,medium (%s)" % where)
            load(web, settle=1.0)

        # ------------------------------------------------------------------------------------------------------------------------------------------------------------
        def play_with(opponents, label, seconds):
            clear_storage()
            load(web, settle=1.5)
            tab.ev("var level = %s; [1, 2, 3].forEach(function (n) { document.getElementById('opponent-' + n + '-' + (level || 'none')).click(); }); 1" % json.dumps(opponents))
            tab.ev("var n = document.getElementById('player-name'); n.value = 'Bob'; n.dispatchEvent(new Event('input')); 1")
            before = pages()
            tab.ev("document.getElementById('play').click(); 1")
            ok = wait_for(lambda: tab.ev("location.pathname") == "/play.html", 20)
            check(ok, "%s: START takes this tab to /play.html" % label)
            if not ok:
                return None
            check(pages() == before, "%s: no new tab or window was opened (%d pages before and after)" % (label, before))
            if not wait_for(lambda: tab.ev("!!window.isReadyToPlay"), args.ready_timeout):
                check(False, "%s: the game became ready" % label)
                return None
            time.sleep(1.0)
            a = json.loads(tab.ev("JSON.stringify({args: ANTS_ARGS, search: location.search})"))
            map_args = a["args"][a["args"].index("--map"):a["args"].index("--map") + 3] if "--map" in a["args"] else []
            check(map_args == ["--map", "Original-Ants/Maps/TREASURE.LVL", "--play"], "%s: the game is given the map and --play (%s)" % (label, map_args))
            seats = [a["args"][i + 1] for i, x in enumerate(a["args"]) if x == "--bot"]
            want = ["1:%s" % opponents, "2:%s" % opponents, "3:%s" % opponents] if opponents else []
            check(seats == want, "%s: the bots of the three other bases: %s (wanted %s)" % (label, seats, want))
            check("--name" in a["args"] and a["args"][a["args"].index("--name") + 1] == "Bob", "%s: the typed name is the game's (%s)" % (label, a["args"]))
            check("--aspect" in a["args"] and a["args"][a["args"].index("--aspect") + 1] == "16:9", "%s: the picture's shape goes with it" % label)
            check("name=" not in a["search"] and "map=treasure" in a["search"], "%s: the address bar keeps the map but not the name (%s)" % (label, a["search"]))
            check(not tab.ev("document.getElementById('name-step') && !document.getElementById('name-step').hidden"), "%s: the game does not ask for a name (the front page chose it)" % label)
            return a

        if wanted("play"):
            print("[web home] Play with Medium bots: a game on this computer, in this tab")
            a = play_with("medium", "play", args.bot_seconds)
            if a is not None:
                started = start_by_enter()
                check(started, "play: Enter at the quick help starts the match at once (no setup screen in between), and its 'get ready' dialog closes")
                shot("home_play_start")
                if started:
                    rows0 = None
                    t0 = time.time()
                    first = tab.shot()
                    time.sleep(max(0.0, args.bot_seconds - (time.time() - t0)))
                    later = tab.shot()
                    g = tab.geometry()
                    cv = g["canvas"]
                    box = score_strip(cv)
                    changed = aspect.count_changed_pixels(first, later, box[0], box[1], box[2], box[3])
                    shot("home_play_later")
                    check(changed > 60, "play: the bots' scores at the bottom of the picture changed in %.0f s (%d pixels of the strip): their ants move" % (args.bot_seconds, changed))
                    check(tab.ev("Module._ants_match_running()") == 1, "play: the match is running in this tab")
                    check(not any("Bot (" in d for d in dialogs), "play: no dialog was shown")
                    # the Menu: the way back to the front page, in the same tab; it asks first while a match runs
                    before = pages()
                    dialogs.clear()
                    answer["accept"] = False
                    tab.ev("document.getElementById('menu-btn').click(); 1")
                    time.sleep(0.8)
                    check(len(dialogs) == 1 and "menu" in dialogs[0].lower() and tab.ev("location.pathname") == "/play.html", "menu: while a match runs the link asks first (%s) and No stays in the game" % dialogs)
                    answer["accept"] = True
                    tab.ev("document.getElementById('menu-btn').click(); 1")
                    back = wait_for(lambda: tab.ev("location.pathname") == "/" and bool(tab.ev("document.getElementById('player-name') ? 1 : 0")), 20)
                    check(back and pages() == before, "menu: Yes goes back to the front page in the same tab (no new tab)")
                    remembered = json.loads(tab.ev("JSON.stringify({map: document.getElementById('map-solo').value, opponents: [1, 2, 3].map(function (n) { return document.querySelector('input[name=opponent-' + n + ']:checked').value; }).join(), name: document.getElementById('player-name').value})")) if back else {}
                    check(remembered == {"map": "treasure", "opponents": "medium,medium,medium", "name": "Bob"}, "menu: the front page remembers the choices and the name (%s)" % remembered)

        if wanted("alone"):
            print("[web home] Play with Opponents: None: the original's single player, alone")
            a = play_with("", "alone", args.bot_seconds)
            if a is not None:
                started = start_by_enter()
                check(started, "alone: Enter at the quick help starts the match")
                if started:
                    first = tab.shot()
                    time.sleep(min(20.0, args.bot_seconds))
                    later = tab.shot()
                    cv = tab.geometry()["canvas"]
                    box = score_strip(cv)
                    changed = aspect.count_changed_pixels(first, later, box[0], box[1], box[2], box[3])
                    check(changed <= 5, "alone: the strip where the other players' scores would be does not change (%d pixels): nobody plays but you" % changed)

        # ------------------------------------------------------------------------------------------------------------------------------------------------------------
        if wanted("old"):
            print("[web home] the old addresses")
            clear_storage()
            before = pages()
            load(web + "?join=/ws&room=demo-small-2p-abc123&aspect=16:9", ready=False, settle=2.0)
            info = json.loads(value("JSON.stringify({stage: !!document.getElementById('game-stage'), lobby: !!document.getElementById('player-name'), ask: !document.getElementById('name-step').hidden, args: ANTS_ARGS})"))
            check(info["stage"] and not info["lobby"] and info["ask"], "a shared game link (/?join=/ws&room=...) opens the game page, which asks for a name")
            check("--join-url" in info["args"] and "--map" not in info["args"] and "--play" not in info["args"], "... a game of the server: no local parameter reaches it (%s)" % info["args"])
            load(web + "?embed=1&aspect=16:9", ready=False, settle=1.5)
            check(tab.ev("!!document.getElementById('game-stage') && document.body.classList.contains('embed')"), "/?embed=1 opens the game page as a frame's game")
            load(web + "four.html?room=demo-small-2p-abc123&fill=medium", ready=False, settle=2.0)
            info = json.loads(value("JSON.stringify({path: location.pathname, search: location.search, lobby: !!document.getElementById('player-name'), ask: !document.getElementById('who-go').hidden, title: document.getElementById('who-title').textContent})"))
            check(info["path"] == "/" and info["search"].startswith("?room=demo-small-2p-abc123") and info["lobby"], "/four.html?room=... goes to /?room=... for good (%s%s)" % (info["path"], info["search"]))
            check(info["ask"] and "demo-small-2p-abc123" in info["title"], "... and the front page asks for the name of the shared link (%r)" % info["title"])
            load(web + "play.html", ready=True, settle=1.0)
            info = json.loads(value("JSON.stringify({args: ANTS_ARGS})"))
            given = [a for a in info["args"] if a != "./this.program"]                             # (the runtime puts the program's own name in front)
            check(given == ["--aspect", "16:9"], "/play.html with nothing is today's front page: no argument but the picture's shape (%s)" % given)
            shot("home_play_plain")
            check(pages() == before, "no new tab was opened by any of it")

        if wanted("host"):
            print("[web home] the host: 3 players, Create the match, Play in this tab")
            clear_storage()
            load(web, settle=1.5)
            tab.ev("document.getElementById('players-3').click(); var f = document.getElementById('fill'); f.value = 'easy'; f.dispatchEvent(new Event('change')); 1")
            tab.ev("var n = document.getElementById('player-name'); n.value = 'Ann'; n.dispatchEvent(new Event('input')); 1")
            check(tab.ev("document.getElementById('host').textContent.trim()") == "Host the match" and tab.ev("document.getElementById('players-3').checked") is True and tab.ev("document.getElementById('fill').value") == "easy",
                  "3 players and Easy bots for the empty seats on the Host card")
            tab.ev("document.getElementById('host').click(); 1")
            time.sleep(0.8)
            room = tab.ev("document.getElementById('room-code').textContent")
            check(tab.ev("!document.getElementById('room-panel').hidden") and room.startswith("demo-treasure-3p-"), "Host the match makes the room panel (%s)" % room)
            check(tab.ev("document.body.classList.contains('in-room') && document.getElementById('cards').hidden && document.getElementById('how').hidden && getComputedStyle(document.querySelector('.tv')).display === 'none'"),
                  "... in its room mode: the two cards and the header's picture give way to the room")
            before = pages()
            tab.ev("document.getElementById('play-tab').click(); 1")
            ok = wait_for(lambda: tab.ev("location.pathname") == "/" and "join=" in tab.ev("location.search"), 15)
            time.sleep(1.0)
            a = json.loads(tab.ev("JSON.stringify({search: location.search, args: ANTS_ARGS})")) if ok else {"search": "", "args": []}
            check(ok and ("room=" + room) in a["search"] and pages() == before, "Play in this tab takes this tab to the game page of the room (%s), no new tab" % a["search"])
            check("--join-url" in a["args"] and "--fill-bots" in a["args"] and a["args"][a["args"].index("--fill-bots") + 1] == "easy" and "--name" in a["args"], "... with the room, the leader's bots and the name in the game's arguments (%s)" % a["args"])

        # ------------------------------------------------------------------------------------------------------------------------------------------------------------
        if wanted("game"):
            print("[web home] the game page in the front page's look")
            play = web + "play.html"
            fresh_tab()

            def contrast_of(where, at_least, prepare="", restore=""):
                """Every visible text of the game page is at least 4.5:1 (`prepare` shows what is closed: the guide's panels, the More list; `restore` closes it again)."""
                found = json.loads(value(prepare + "; var found = " + CONTRAST_JS + "; " + restore + "; found"))
                check(found["texts"] >= at_least and found["lowest"][0][0] >= 4.5, "%s: the text contrast is at least 4.5:1 for all %d texts (lowest: %s)" % (where, found["texts"], found["lowest"]))

            def hold(pattern):
                """Hold the requests that match `pattern` (the DevTools Fetch domain): they are asked for and never answered until release() is called."""
                held = []

                def on_event(msg):
                    if msg.get("method") == "Fetch.requestPaused" and msg.get("sessionId") == tab.session:
                        held.append(msg["params"]["requestId"])

                tab.dt.handlers.append(on_event)
                tab.call("Fetch.enable", {"patterns": [{"urlPattern": pattern, "requestStage": "Request"}]})

                def release():
                    tab.call("Fetch.disable")
                    tab.dt.handlers.remove(on_event)
                    for request in held:
                        try:
                            tab.call("Fetch.continueRequest", {"requestId": request})
                        except (RuntimeError, TimeoutError):
                            pass
                return release

            def centre(selector):
                return json.loads(value("JSON.stringify((function () { var b = document.querySelector(%s).getBoundingClientRect(); return [b.x + b.width / 2, b.y + b.height / 2]; })())" % json.dumps(selector)))

            # --- the look, and the loading screen: the game's index.js is held, so that the page stays at its loading screen (the page's own watchdog is 60 s), and then, with a watchdog of
            # 2.5 s, at its failure card
            clear_storage()
            release = hold("*index.js*")
            script = None
            try:
                tab.emulate(1440, 900, 1)
                tab.open(play + "?aspect=16:9", wait=False)
                reached = wait_for(lambda: tab.ev("document.getElementById('status-text').textContent") == "Starting the game...", 90)
                check(bool(reached), "the loading screen: the data downloads and the page says \"Starting the game...\" while the game's program is held")
                loading = json.loads(value("""JSON.stringify((function () {
                    var o = document.getElementById('splash-overlay'), logo = o.querySelector('.splash-logo'), bar = document.getElementById('progress-container'), s = getComputedStyle(o);
                    var l = logo.getBoundingClientRect(), box = document.getElementById('game-container').getBoundingClientRect(), body = getComputedStyle(document.body);
                    var first = document.querySelector('header .btn'), f = getComputedStyle(first);
                    return { shown: s.display !== 'none' && parseFloat(s.opacity) > 0.99, bg: s.backgroundImage, logoLoaded: logo.complete && logo.naturalWidth === 581, logo: [l.width, l.height],
                             barShown: getComputedStyle(bar).display !== 'none', barBg: getComputedStyle(bar).backgroundColor, fill: getComputedStyle(document.getElementById('progress-fill')).backgroundColor,
                             fillWidth: document.getElementById('progress-fill').style.width, old: !!o.querySelector('.splash-title'), text: document.getElementById('status-text').textContent,
                             inside: l.left >= box.left && l.right <= box.right && l.top >= box.top && l.bottom <= box.bottom,
                             page: { bg: body.backgroundImage, font: body.fontFamily, button: f.backgroundColor, shadow: f.boxShadow, frame: getComputedStyle(document.body, '::after').boxShadow,
                                     fontLoaded: Array.from(document.fonts).some(function (x) { return x.family.indexOf('Libre Franklin') !== -1 && x.status === 'loaded'; }) } };
                })())"""))
                check(loading["shown"] and "front/clay.png" in loading["bg"] and not loading["old"], "the loading screen is the clay with no \"ANTS\" word on it (%s)" % loading["bg"])
                check(loading["logoLoaded"] and loading["inside"] and 100 < loading["logo"][0] <= 240, "... it shows the \"ants!\" logo (%.0f x %.0f px), whole and inside the picture's box" % tuple(loading["logo"]))
                check(loading["barShown"] and loading["barBg"] == "rgb(7, 11, 15)" and loading["fill"] == "rgb(43, 99, 87)" and loading["fillWidth"] == "100%", "... and a teal bar in a black box (%s, %s, %s)" % (loading["barBg"], loading["fill"], loading["fillWidth"]))
                page = loading["page"]
                check("front/clay.png" in page["bg"] and page["font"].startswith('"Libre Franklin"') and page["fontLoaded"], "the page is on the clay, in the game's own font Libre Franklin (loaded from the site)")
                check(page["button"] == "rgb(43, 99, 87)" and "rgb(157, 13, 23)" in page["shadow"] and "rgb(43, 95, 67)" in page["frame"] and "157, 13, 23" in page["frame"],
                      "a button of the header is the teal one with the red shadow, and the page has the thin green frame with its red line (%s)" % page["shadow"][:60])
                shot("game_loading_1440")
                contrast_of("the loading screen at 1440 px", 20)
                tab.emulate(390, 844, 2, mobile=True)
                time.sleep(0.6)
                inside = value("(function () { var l = document.querySelector('.splash-logo').getBoundingClientRect(), b = document.getElementById('game-container').getBoundingClientRect(); return l.left >= b.left && l.right <= b.right && l.top >= b.top && l.bottom <= b.bottom; })()")
                check(inside is True, "the loading screen of a phone (390 px): the logo and the message fit the small picture's box")
                shot("game_loading_390")
                contrast_of("the loading screen at 390 px", 20)
                # More opened while the loading screen is up: its list is above the loading screen (the picture's box keeps that screen's layer to itself)
                x, y = centre("header .more summary")
                tab.click(x, y)
                time.sleep(0.4)
                over = json.loads(value("""JSON.stringify((function () { var list = document.querySelector('.more-list'), r = list.getBoundingClientRect(), splash = document.getElementById('splash-overlay'), o = splash.getBoundingClientRect(), was = splash.style.pointerEvents;
                    splash.style.pointerEvents = 'auto';                                 // (the loading screen takes no pointer: let it, so that the point's topmost element is what is painted there)
                    var x = (r.left + r.right) / 2, y = (r.top + r.bottom) / 2, mid = document.elementFromPoint(x, y);
                    splash.style.pointerEvents = was;
                    return { open: document.querySelector('header .more').open, above: !!(mid && mid.closest('.more-list')), over: x >= o.left && x <= o.right && y >= o.top && y <= o.bottom }; })())"""))
                check(over["open"] and over["above"] and over["over"], "More opened over the loading screen: its list is on top of it, not under it (%s)" % over)
                shot("game_loading_390_more")
                tab.click(x, y)
                time.sleep(0.3)
                # the watchdog (2.5 s here) puts the failure card up: the front page's notice with the teal button, no logo (the card needs the room)
                script = tab.call("Page.addScriptToEvaluateOnNewDocument", {"source": "window.ANTS_START_TIMEOUT_MS = 2500;"})["identifier"]
                tab.emulate(1440, 900, 1)
                tab.open(play + "?aspect=16:9", wait=False)
                card_up = wait_for(lambda: tab.ev("!!document.getElementById('load-failure')"), 90)
                check(bool(card_up), "the game that does not start puts its failure card up (the page's watchdog)")
                if card_up:
                    card = json.loads(value("""JSON.stringify((function () {
                        var c = document.getElementById('load-failure'), b = c.querySelector('button'), bs = getComputedStyle(b), cs = getComputedStyle(c), box = document.getElementById('game-container').getBoundingClientRect(), r = c.getBoundingClientRect();
                        return { text: c.innerText.replace(/\\s+/g, ' '), button: bs.backgroundColor, shadow: bs.boxShadow, bg: cs.backgroundColor, logo: getComputedStyle(document.querySelector('.splash-logo')).display,
                                 clicks: getComputedStyle(document.getElementById('splash-overlay')).pointerEvents, inside: r.left >= box.left && r.right <= box.right && r.top >= box.top && r.bottom <= box.bottom };
                    })())"""))
                    check("could not be started" in card["text"] and "Reload" in card["text"] and card["button"] == "rgb(43, 99, 87)" and "rgb(157, 13, 23)" in card["shadow"] and card["bg"] == "rgb(59, 13, 16)",
                          "the failure card is the front page's notice with the teal Reload button (%s)" % card["text"][:70])
                    check(card["logo"] == "none" and card["clicks"] == "auto" and card["inside"], "... the logo gives it the room, it takes the clicks, and it is inside the picture's box")
                    shot("game_load_failed")
                    contrast_of("the failure card", 4)
            finally:
                release()
                if script:
                    tab.call("Page.removeScriptToEvaluateOnNewDocument", {"identifier": script})
            fresh_tab()

            # --- the page at 19 widths: no sideways scroll, the header's controls, the picture, the bar and the guide inside the frame
            LAYOUT = """JSON.stringify((function () {
                var de = document.documentElement, rect = function (e) { var b = e.getBoundingClientRect(); return [b.left, b.top, b.right, b.bottom]; };
                var shown = function (e) { return !!e && getComputedStyle(e).display !== 'none' && e.getBoundingClientRect().width > 0 && (!e.checkVisibility || e.checkVisibility()); };
                var controls = Array.prototype.filter.call(document.querySelectorAll('header a, header button, header summary'), shown).map(function (e) { return { text: e.innerText.trim(), rect: rect(e) }; });
                return { inner: [de.clientWidth, window.innerHeight], scroll: [de.scrollWidth, de.scrollHeight], controls: controls, box: rect(document.getElementById('game-container')), bar: rect(document.querySelector('footer.bar')),
                         panel: rect(document.getElementById('info-panel')), header: rect(document.querySelector('header')), more: shown(document.querySelector('header .more')), view: rect(document.getElementById('view-bar')),
                         seg: Array.prototype.filter.call(document.querySelectorAll('.seg button'), shown).map(rect) };
            })())"""
            clear_storage()
            load(play + "?aspect=16:9", ready=True, settle=1.5)
            wrong = {"scroll": [], "header": [], "overlap": [], "frame": [], "picture": [], "foot": []}
            for width in (320, 360, 375, 390, 414, 440, 441, 480, 600, 699, 700, 701, 768, 900, 1024, 1100, 1280, 1440, 1600):
                tab.emulate(width, 900, 1)
                time.sleep(0.5)
                m = json.loads(value(LAYOUT))
                narrow = width <= 700
                if m["scroll"][0] > m["inner"][0]:
                    wrong["scroll"].append((width, m["scroll"][0], m["inner"][0]))
                texts = [c["text"] for c in m["controls"]]
                want = (["", "Menu", "Full" if width <= 440 else "Fullscreen", "More"] if narrow else ["", "Menu", "Sprites and sounds", "Changelog", "Reset", "Fullscreen", "GitHub", "Feedback"])
                height = m["header"][3] - m["header"][1]
                if texts != want or m["more"] != narrow or (narrow and height > 60):
                    wrong["header"].append((width, texts, "More" if m["more"] else "no More", round(height)))
                for i, a in enumerate(m["controls"]):
                    for b in m["controls"][i + 1:]:
                        same_row = min(a["rect"][3], b["rect"][3]) - max(a["rect"][1], b["rect"][1]) > 0.5 * (a["rect"][3] - a["rect"][1])
                        if same_row and min(a["rect"][2], b["rect"][2]) - max(a["rect"][0], b["rect"][0]) > 0.5:
                            wrong["overlap"].append((width, a["text"] or "logo", b["text"] or "logo"))
                if any(c["rect"][0] < 10 or c["rect"][2] > m["inner"][0] - 10 for c in m["controls"]) or any(s[0] < 10 or s[2] > m["inner"][0] - 10 for s in m["seg"]) or m["panel"][0] < 10 or m["panel"][2] > m["inner"][0] - 10:
                    wrong["frame"].append((width, [round(v) for v in m["panel"]]))
                ring, shadow = (5, 3) if narrow else (8, 4)
                box = m["box"]
                if box[0] - ring < 10 or box[2] + ring + shadow > m["inner"][0] - 10 or box[2] - box[0] < 16 or abs((box[0] + box[2]) / 2 - m["inner"][0] / 2) > 1.5:
                    wrong["picture"].append((width, [round(v, 1) for v in box]))
                if abs(m["bar"][0]) > 0.5 or abs(m["bar"][2] - m["inner"][0]) > 0.5:
                    wrong["foot"].append((width, [round(v, 1) for v in m["bar"]]))
            check(not wrong["scroll"], "no sideways scroll from 320 to 1600 px (wrong: %s)" % (wrong["scroll"],))
            check(not wrong["header"], "the header: up to 700 px one row of the logo, Menu, Fullscreen (Full up to 440 px) and More; above it the logo and the seven controls in their order (wrong: %s)" % (wrong["header"],))
            check(not wrong["overlap"], "no control of the header lies over another or over the logo, in one row or two, at every width (wrong: %s)" % (wrong["overlap"][:6],))
            check(not wrong["frame"], "the header's controls, the pair buttons and the guide stay inside the thin green frame at every width (wrong: %s)" % (wrong["frame"],))
            check(not wrong["picture"], "the picture with its ring and shadow is centred and inside the frame at every width (wrong: %s)" % (wrong["picture"],))
            check(not wrong["foot"], "the footer's bar spans the window at every width (wrong: %s)" % (wrong["foot"],))

            # --- 1440 px: the page that is up, in every part
            fresh_tab()
            load(play + "?aspect=16:9", ready=True, settle=1.5)
            shot("game_1440")
            footer = json.loads(value("""JSON.stringify({text: document.querySelector('footer').innerText.replace(/\\s+/g, ' '), ids: [!!document.getElementById('game-version'), !!document.getElementById('game-build-id')],
                links: Array.prototype.map.call(document.querySelectorAll('footer nav a'), function (a) { return a.innerText; }), bg: getComputedStyle(document.querySelector('footer')).backgroundImage})"""))
            check("Version" in footer["text"] and "build" in footer["text"] and "@@" not in footer["text"] and footer["ids"] == [True, True] and "linear-gradient" in footer["bg"] and footer["links"] == ["Menu", "Sprites and sounds", "Changelog", "GitHub", "Feedback"],
                  "the footer is the front page's emerald bar with the version, the build and the links (%r)" % footer["text"][:90])
            contrast_of("the game page at 1440 px", 60, "document.querySelectorAll('#info-panel details').forEach(function (d) { d.open = true; })")

            # --- the pairs under the game: the chosen button is pressed in, a click keeps its id and what it remembers
            pairs = json.loads(value("""JSON.stringify(['aspect-16-9', 'aspect-4-3', 'lock-on', 'lock-off'].map(function (id) { var e = document.getElementById(id), s = getComputedStyle(e);
                return [id, e.getAttribute('aria-checked'), s.backgroundColor, s.transform, (s.boxShadow.match(/rgb\(157, 13, 23\) (\d+)px (\d+)px/) || []).slice(1).join('x')]; }))"""))
            pressed, raised = ("rgb(16, 43, 37)", "matrix(1, 0, 0, 1, 1, 2)"), ("rgb(43, 99, 87)", "none")
            check([tuple(p[2:4]) for p in pairs] == [pressed, raised, pressed, raised] and [p[1] for p in pairs] == ["true", "false", "true", "false"],
                  "the pairs under the game: 16:9 and Locked are pressed in (dark, moved), Classic 4:3 and Free are teal (%s)" % pairs)
            check([p[4] for p in pairs] == ["1x1", "2x3", "1x1", "2x3"], "... a teal button has the front page's red shadow (2 x 3 px), a pressed one the small one (1 x 1 px) (%s)" % [p[4] for p in pairs])
            x, y = centre("#lock-off")
            tab.click(x, y)
            time.sleep(0.4)
            after = json.loads(value("JSON.stringify(['lock-on', 'lock-off'].map(function (id) { var e = document.getElementById(id); return [e.getAttribute('aria-checked'), getComputedStyle(e).backgroundColor]; }).concat([localStorage.getItem('ants.pointerlock')]))"))
            check(after == [["false", raised[0]], ["true", pressed[0]], "off"], "Free clicked: it is pressed in, Locked is teal, and the browser remembers \"off\" (%s)" % (after,))
            x, y = centre("#lock-on")
            tab.click(x, y)
            time.sleep(0.4)
            check(value("document.getElementById('lock-on').getAttribute('aria-checked') + '/' + localStorage.getItem('ants.pointerlock')") == "true/on", "Locked clicked again: pressed in, remembered \"on\"")
            x, y = centre("#aspect-4-3")
            tab.click(x, y)
            back = wait_for(lambda: tab.ev("!!window.isReadyToPlay && location.search.indexOf('aspect=4:3') !== -1"), 60)
            time.sleep(1.0)
            chosen = json.loads(value("JSON.stringify([['aspect-16-9', 'aspect-4-3'].map(function (id) { var e = document.getElementById(id); return [e.getAttribute('aria-checked'), getComputedStyle(e).backgroundColor]; }), localStorage.getItem('ants.aspect.v2'), document.getElementById('game-stage').getAttribute('data-aspect')])")) if back else []
            check(chosen == [[["false", raised[0]], ["true", pressed[0]]], "4:3", "4:3"], "Classic 4:3 clicked: the page restarts with it, it is pressed in, 16:9 is teal, the browser remembers it (%s)" % (chosen,))
            shot("game_classic_1440")
            clear_storage()

            # --- a phone held upright and on its side
            tab.emulate(390, 844, 2, mobile=True)
            tab.open(play + "?aspect=16:9", settle=1.5)
            m = json.loads(value(LAYOUT))
            tip = value("getComputedStyle(document.getElementById('mobile-tip-banner')).display")
            panels = value("Array.prototype.map.call(document.querySelectorAll('#info-panel details'), function (d) { return d.open ? 1 : 0; }).reduce(function (a, b) { return a + b; }, 0)")
            check(m["scroll"][0] <= m["inner"][0] and tip == "flex" and panels == 1 and m["more"] is True, "a phone held upright (390 px): no sideways scroll, the tip is shown, one panel of the guide is open, More is there (%s, %s, %d)" % (m["scroll"][0], tip, panels))
            shot("game_390")
            contrast_of("the game page at 390 px", 40, "document.querySelectorAll('#info-panel details').forEach(function (d) { d.open = true; })",
                        "document.querySelectorAll('#info-panel details').forEach(function (d, i) { d.open = i === 0; })")
            # More: a <details>, no script: closed at first, it opens over the picture with the other five links, whole and in the window, and closes again
            check(value("document.querySelector('header .more').open") is False, "More is closed at first")
            x, y = centre("header .more summary")
            tab.click(x, y)
            time.sleep(0.4)
            more = json.loads(value("""JSON.stringify((function () {
                var list = document.querySelector('.more-list'), r = list.getBoundingClientRect(), items = Array.prototype.map.call(list.querySelectorAll('a, button'), function (e) { var b = e.getBoundingClientRect(); return [e.innerText.trim(), b.left, b.right, b.top, b.bottom]; });
                var mid = document.elementFromPoint((r.left + r.right) / 2, r.top + 8), picture = document.getElementById('game-container').getBoundingClientRect();
                return { open: document.querySelector('header .more').open, items: items, list: [r.left, r.right, r.top, r.bottom], above: !!(mid && mid.closest('.more-list')), overPicture: r.top < picture.bottom && r.bottom > picture.top && r.left < picture.right };
            })())"""))
            check(more["open"] and [i[0] for i in more["items"]] == ["Sprites and sounds", "Changelog", "Reset", "GitHub", "Feedback"] and more["list"][0] >= 10 and more["list"][1] <= 390 - 10 and all(i[1] >= 10 and i[2] <= 380 for i in more["items"]),
                  "More opens the other five links, whole and inside the window (%s)" % ([i[0] for i in more["items"]],))
            check(more["above"] and more["overPicture"], "... over the picture and above it (the loading screen's layer does not cover it)")
            shot("game_390_more")
            contrast_of("the More list", 5)
            tab.click(x, y)
            time.sleep(0.3)
            check(value("document.querySelector('header .more').open") is False, "... and closes with the same click")
            tab.emulate(844, 390, 2, mobile=True)
            tab.open(play + "?aspect=16:9", settle=1.5)
            m = json.loads(value(LAYOUT))
            check(m["scroll"][0] <= m["inner"][0] and m["view"][3] <= m["inner"][1] + 1 and m["header"][3] - m["header"][1] <= 60 and not m["more"],
                  "a phone on its side (844 x 390): the header is one slim row, the whole picture and the bar under it fit the window, no sideways scroll (bar ends at %.0f of %d)" % (m["view"][3], m["inner"][1]))
            shot("game_844")
            tab.emulate(1440, 900, 1)

            # --- the logo is the way back to the front page, as Menu is: it asks while a match runs or a room is joined (No stays), and does not ask at the setup screen
            clear_storage()
            load(play, ready=True, settle=1.0)
            before = pages()
            dialogs.clear()
            tab.ev("document.getElementById('logo-link').click(); 1")
            back = wait_for(lambda: tab.ev("location.pathname") == "/" and bool(tab.ev("document.getElementById('player-name') ? 1 : 0")), 20)
            check(back and not dialogs and pages() == before, "the logo at the setup screen (no match): back to the front page in this tab, nothing asked (%s)" % dialogs)
            tab.emulate(1440, 900, 1)
            tab.open(play + "?map=treasure&bots=medium&name=Ann", settle=2.0)
            if start_by_enter():
                dialogs.clear()
                answer["accept"] = False
                tab.ev("document.getElementById('logo-link').click(); 1")
                time.sleep(0.8)
                check(len(dialogs) == 1 and "Leave the game and go back to the menu?" in dialogs[0] and tab.ev("location.pathname") == "/play.html",
                      "the logo while a match runs asks \"Leave the game and go back to the menu?\" (%s) and No stays in the game" % dialogs)
                check(tab.ev("Module._ants_match_running()") == 1, "... the match goes on")
                answer["accept"] = True
                tab.ev("document.getElementById('logo-link').click(); 1")
                back = wait_for(lambda: tab.ev("location.pathname") == "/" and bool(tab.ev("document.getElementById('player-name') ? 1 : 0")), 20)
                check(back and pages() == before, "... and Yes goes back to the front page in the same tab (no new tab)")
            else:
                check(False, "the local game started (Enter at the quick help)")
            answer["accept"] = True
            dialogs.clear()
            tab.open(play + "?join=/ws&room=demo-small-2p-abc123&name=Ann&aspect=16:9", wait=False)
            time.sleep(2.0)
            answer["accept"] = False
            tab.ev("document.getElementById('logo-link').click(); 1")
            time.sleep(0.8)
            check(len(dialogs) == 1 and "menu" in dialogs[0].lower() and tab.ev("location.pathname") == "/play.html", "the logo in a joined room asks too, and No stays (%s)" % dialogs)
            answer["accept"] = True
            dialogs.clear()

            # --- the name step of a shared link: the front page's name step (a banner, the black field, the teal button), whole in a small window, with its notice
            clear_storage()
            for label, width, height, dpr, mobile in (("1440 px", 1440, 900, 1, False), ("390 px", 390, 844, 2, True)):
                tab.emulate(width, height, dpr, mobile)
                tab.open(play + "?join=/ws&room=demo-small-2p-abc123&aspect=16:9", wait=False)
                shown = wait_for(lambda: tab.ev("!document.getElementById('name-step').hidden"), 20)
                step = json.loads(value("""JSON.stringify((function () {
                    var c = document.querySelector('.name-step'), r = c.getBoundingClientRect(), i = document.getElementById('name-step-input'), b = document.getElementById('name-step-go'), t = document.getElementById('name-step-title');
                    return { inside: r.left >= 0 && r.right <= window.innerWidth && r.top >= 0 && r.bottom <= window.innerHeight, bg: getComputedStyle(c).backgroundImage, field: getComputedStyle(i).backgroundColor, button: getComputedStyle(b).backgroundColor,
                             title: getComputedStyle(t).backgroundColor, text: t.textContent };
                })())""")) if shown else {}
                check(bool(shown) and step["inside"] and "front/clay.png" in step["bg"] and step["field"] == "rgb(7, 11, 15)" and step["button"] == "rgb(43, 99, 87)" and step["title"] == "rgb(43, 99, 87)",
                      "the name step of a shared link at %s: a clay card with a teal banner, the black field and the teal Join button, whole in the window (%s)" % (label, step))
                contrast_of("the name step at %s" % label, 4)
                tab.ev("var i = document.getElementById('name-step-input'); i.value = 'Zo\\u00eb'; document.getElementById('name-step-go').click(); 1")
                time.sleep(0.3)
                note = json.loads(value("JSON.stringify((function () { var m = document.getElementById('name-step-msg'), s = getComputedStyle(m), r = m.getBoundingClientRect(), c = document.querySelector('.name-step').getBoundingClientRect(); return { text: m.textContent, bg: s.backgroundColor, inside: r.bottom <= c.bottom && r.right <= c.right }; })())"))
                check("printable ASCII" in note["text"] and note["bg"] == "rgb(59, 13, 16)" and note["inside"], "... a name that is refused says so in the front page's notice, inside the card (%s)" % note["text"][:50])
                contrast_of("the name step's notice at %s" % label, 5)
                shot("game_name_step_%s" % label.split()[0])
            tab.emulate(1440, 900, 1)
            clear_storage()
    except NotReachable as e:
        print("  SKIP: %s" % e)
        return 3
    except Exception as e:                                      # noqa: BLE001  (a page that hangs or crashes is a failure, never a skip)
        print("  FAIL: the check broke down: %s: %s" % (type(e).__name__, e))
        return 1
    finally:
        if browser is not None:
            browser.close()
    print("[web home] %d checks, %d failed" % (count[0], len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
