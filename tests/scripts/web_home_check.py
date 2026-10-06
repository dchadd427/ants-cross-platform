#!/usr/bin/env python3
"""The front page and the way into and out of a game, in a REAL browser (opt-in; see tests/scripts/test_web_home.sh and docs/NETWORK_PORT.md, "The front page").

The owner, on a phone: "I don't see a way to change colors or send an invite to another person should all be right there. We don't need separate AI and online. Only do online." The front page is
ONE card, New match: four seats (exactly one You, every other Friend, a bot of a level or Nobody), the Teams, an invitation for each Friend and START!, which takes THIS tab into a match on the game
server. Needs a running web page that has nginx's routes (the web image of this tree: `docker build -t ants-beta .`, run it on a port), a Chromium-based browser and Python 3; the parts that play
need the site's /ws to lead to a game server: --ws-port is the port that it leads to, and the check starts the native `ants_server` of this tree there (as web_rejoin_check.py does). The DevTools
protocol is spoken with the client of web_hidden_check.py, standard library only. Each part opens the site in a throwaway headless browser (its own profile and port; nothing of yours is touched). Parts:

  * front    the page at "/" (a first visit: ONE card with Treasure, You at Green, a Friend in the other three seats, START on, "Have a code?"; the header links, the footer's version and build,
             the picture's two buttons, the name field, the font; the line of numbers (not there, or well formed); 21 widths from 320 to 1600 px: no sideways scroll, the five buttons of every seat on
             ONE line, the seat's line (colour, buttons and Sit here on one line from 701 px, two under it), two columns from 1100 px; the contrast of all text at 1440 and 390 px, also with an invitation
             and the note about changed links up);
  * seats    the keyboard and the pointer: the arrows move and check inside a seat's group, Tab goes on to Sit here and the next seat, the focused button shows its outline; Sit here (the key Enter and the
             pointer) moves You and the focus goes to the seat that You left; the Teams offer the pairs of the seats that play; a Friend seat shows its invitation, Copy link puts exactly that link on the
             clipboard; with every other seat Nobody START is on and says that it starts a game for one on this computer;
  * play     START with bots only takes THIS tab to the game page (no new tab): the game's arguments are the room (demo-treasure-4p-<code>), your seat, the plan (a Medium bot in every other seat),
             --start-when 1, your name and the shape; the match starts without a START of the player's after the quick help closes (the "Get ready" dialog), the server's status lists the three bots
             (Bot (Medium), seats 1 - 3) and the player, and the bots' scores, which the HUD shows at the bottom, rise from 0; the game page's Menu link asks first (a room is joined) and, with Yes,
             goes back to the front page in the same tab, which remembers its choices;
  * solo     START with every other seat Nobody takes THIS tab to the game page of a game on this computer, the original's single player (the map and the name, no room, no bot): the match runs
             with its one colony (a room of the server needs two people), at Green whatever seat was You;
  * friend   two people, each a browser of their own: the host sits at Blue, a Friend at Black, nobody else; START takes the host to the room and the room WAITS (the server's status: waiting, one
             player); the friend opens the invitation link (asked for a name first), joins the seat that the link names, and the match starts by itself, with no START pressed after that, for both;
             the server's status lists both seats and the empty ones, no START of a game that does not lead was heard, and the two games' state hashes agree;
  * old      the old addresses: /?join=...&room=... and /?embed=1 open the game page (a shared link asks for a name), /four.html?room=... goes to /?room=... and the front page asks for the name of a
             shared link (with its way back to the front page), /play.html with nothing is today's front page (the setup screen, no arguments), /?map=...&players=1 asks for a name and plays a game on
             this computer;
  * room     an address that hosts a match (/?map=treasure&players=3&fill=easy&teams=0+1) makes the room panel (the team is a word of the room's code), and "Play in this tab" takes this tab to the
             game page with the room, the name and the bots of the leader's START;
  * game     the GAME PAGE in the front page's look (web/shell.html at /play.html): the clay, the frame, the font and the logo; the loading screen (the logo, a teal bar in a black box) and its failure
             card; 19 widths from 320 to 1600 px with no sideways scroll, the header's seven controls (one row with "More" up to 700 px: the five other links, over the picture), the picture, the bar
             and the guide inside the frame, a phone on its side; the logo goes back to the front page as Menu does (it asks first while a match runs or a room is joined: No stays); the two pairs
             under the game (the chosen one is pressed in; the clicks keep their ids and what they remember); the name step of a shared link; the contrast of every text (4.5:1) in each of these states.
Exit status 0: every check passed; 1: a check failed; 3: the check could not be made because the environment is not there (no browser, nothing answers at the page's address, and for the parts
that play no game server of this tree or a site whose /ws does not lead to it).
"""
import argparse
import json
import os
import re
import shutil
import sys
import tempfile
import time
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import web_aspect_check as aspect                                            # noqa: E402
import web_rejoin_check as rejoin                                            # noqa: E402  (its Server: the site's game server of this tree; its Player: a person's own browser)
from web_aspect_check import Browser, NotReachable, Tab                    # noqa: E402
from web_hidden_check import find_browser                                    # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
PARTS = ("front", "seats", "play", "solo", "friend", "old", "room", "game")
NEEDS_SERVER = ("play", "friend")                                            # (the parts that play a match need the site's /ws to lead to a game server)

# The contrast of every visible text with its background (WCAG: (L1 + 0.05) / (L2 + 0.05)); text on the clay tile is measured against the tile's two ends (its deepest and its lightest broad shade: CLAY_DEEP
# and CLAY_LIGHT of tools/front_page_art/artlib.py; tests/scripts/test_web_front.py holds the whole tile to the ink's 4.5 : 1) and text in the footer against the ends of its gradient, so the number is the worst case. Returns JSON: how many texts, the lowest ratio, and the three lowest.
CONTRAST_JS = """(function () {
  function parse(c) { var m = c.match(/rgba?\\(([^)]+)\\)/); if (!m) return null; var p = m[1].split(',').map(parseFloat); return { r: p[0], g: p[1], b: p[2], a: p.length > 3 ? p[3] : 1 }; }
  function lin(v) { v /= 255; return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); }
  function lum(c) { return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b); }
  function ratio(a, b) { var l1 = lum(a), l2 = lum(b); if (l1 < l2) { var t = l1; l1 = l2; l2 = t; } return (l1 + 0.05) / (l2 + 0.05); }
  var CLAY = [{ r: 216, g: 71, b: 16 }, { r: 233, g: 94, b: 36 }], BAR = [{ r: 0x2b, g: 0x68, b: 0x5f }, { r: 0x2b, g: 0x6b, b: 0x4f }];
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
    if (cs.display === 'none' || cs.visibility === 'hidden' || parseFloat(cs.opacity) === 0 || r.width === 0 || r.height === 0 || el.closest('.sr')) continue;
    var fg = parse(cs.color), worst = 99;
    bgOf(el).forEach(function (bg) { worst = Math.min(worst, ratio(fg, bg)); });
    rows.push([Math.round(worst * 100) / 100, t.slice(0, 40)]);
  }
  rows.sort(function (a, b) { return a[0] - b[0]; });
  return JSON.stringify({ texts: rows.length, lowest: rows.slice(0, 3) });
})()"""

# What the card shows now: who is You, the choice in every seat's group, the Teams, the invitations, START and the line under it, the note about changed links
CARD_JS = """(function () {
  function el(id) { return document.getElementById(id); }
  return JSON.stringify({
    map: el('map-pick').value,
    you: [0, 1, 2, 3].filter(function (s) { return !el('seat-you-' + s).hidden; }),
    seats: [0, 1, 2, 3].map(function (s) { var r = document.querySelector('input[name=seat-' + s + ']:checked'); return r ? r.value : '?'; }),
    teams: el('teams').value, teamsShown: !el('teams-line').hidden,
    teamOptions: Array.prototype.map.call(el('teams').options, function (o) { return o.value; }),
    invites: el('invite-list').children.length, invitesShown: !el('invites').hidden,
    links: Array.prototype.map.call(el('invite-list').querySelectorAll('input'), function (i) { return i.value; }),
    startOff: el('play').disabled, note: el('start-note').textContent, linksNote: !el('links-note').hidden });
})()"""

# The layout of the card at the width of the window: the sideways scroll, the two columns, and for every seat that is not You the line of its colour, of its five buttons and of Sit here
LAYOUT_JS = """(function () {
  function rect(e) { var r = e.getBoundingClientRect(); return { left: r.left, right: r.right, top: r.top, bottom: r.bottom, width: r.width, height: r.height }; }
  var left = document.querySelector('.match-grid .left'), right = document.querySelector('.match-grid .right');
  var rows = [];
  for (var s = 0; s < 4; s++) {
    var li = document.getElementById('seat-row-' + s);
    if (!document.getElementById('seat-you-' + s).hidden) { rows.push({ seat: s, you: true }); continue; }
    var labels = Array.prototype.slice.call(li.querySelectorAll('.pair label')).map(rect);
    var pair = li.querySelector('.pair'), sit = document.getElementById('sit-' + s), nm = li.querySelector('.nm');
    var tops = labels.map(function (r) { return r.top; });
    rows.push({ seat: s, you: false, oneLine: Math.max.apply(null, tops) - Math.min.apply(null, tops) < 6,       /* (the checked button is pressed in by 2 px; a second line would be 30 px lower) */ buttons: labels.length, rowRect: rect(li), pair: rect(pair), sit: rect(sit), nm: rect(nm),
                inside: labels.every(function (r) { var l = rect(li); return r.left >= l.left - 0.5 && r.right <= l.right + 0.5; }) && rect(sit).right <= rect(li).right + 0.5 });
  }
  return JSON.stringify({ scrollW: document.documentElement.scrollWidth, innerW: window.innerWidth, left: rect(left), right: rect(right), rows: rows });
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
    ap.add_argument("--ws-port", type=int, default=0, help="the WebSocket port that the site's /ws and /busy lead to: the check starts its game server there (needed by the parts that play: %s)" % ", ".join(NEEDS_SERVER))
    ap.add_argument("--server", default=os.environ.get("ANTS_SERVER_BIN", os.path.join(REPO, "build", "src", "ants_server", "ants_server")), help="the ants_server program of this tree")
    ap.add_argument("--maps", default=os.path.join(REPO, "Original-Ants", "Maps"), help="the maps folder (default: the original's)")
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--shots", default="", help="a folder to save screenshots in")
    ap.add_argument("--ready-timeout", type=float, default=120.0, metavar="SECONDS", help="how long a page may take to get its game ready (default 120)")
    ap.add_argument("--only", default="", help="run only the parts whose name contains this text (%s)" % ", ".join(PARTS))
    ap.add_argument("--bot-seconds", type=float, default=45.0, help="how long the bots play before their scores are compared (default 45)")
    args = ap.parse_args()
    aspect.READY_TIMEOUT = args.ready_timeout
    web = args.web if args.web.endswith("/") else args.web + "/"

    failures = []
    count = [0]

    def check(ok, what):
        count[0] += 1
        print("  %s: %s" % ("ok  " if ok else "FAIL", what), flush=True)
        if not ok:
            failures.append(what)

    def note(text):
        print("  note: %s" % text, flush=True)

    def wanted(name):
        return not args.only or args.only in name

    path = find_browser(args.browser)
    if not path:
        print("  SKIP: no Chromium-based browser found (give one with --browser or CHROME)")
        return 3
    plays = any(wanted(p) for p in NEEDS_SERVER)
    server = None
    work = None
    if plays:
        if not args.ws_port:
            note("no --ws-port: the parts that play (%s) are left out" % ", ".join(NEEDS_SERVER))
        elif not os.path.isfile(args.server) or not os.access(args.server, os.X_OK):
            print("  SKIP: the game server program is not there (build the target ants_server, or give --server)")
            return 3
    can_play = plays and bool(args.ws_port) and os.path.isfile(args.server)

    browser = None
    people = []
    try:
        try:
            urllib.request.urlopen(web, timeout=10).read(64)
        except (OSError, urllib.error.URLError) as e:
            print("  SKIP: nothing answers at %s (%s)" % (web, e))
            return 3
        if can_play:
            work = tempfile.mkdtemp(prefix="ants_home_check.")
            server = rejoin.Server(args.server, args.maps, args.ws_port, work)
            server.start()
            try:
                busy = json.loads(urllib.request.urlopen(web + "busy", timeout=10).read().decode("utf-8", "replace"))
            except (OSError, urllib.error.URLError, ValueError) as e:
                print("  SKIP: the site's /busy does not answer as the game server's does (%s): does the site's /ws lead to port %d?" % (e, args.ws_port))
                return 3
            if not (isinstance(busy, dict) and set(busy) == {"matches", "players"}):
                print("  SKIP: the site's /busy is not the game server's answer (%r): does the site's /ws lead to port %d?" % (busy, args.ws_port))
                return 3
            print("[web home] the server: %s" % server.describe(), flush=True)
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

        def load(url, ready=False, settle=1.0):
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

        def card():
            return json.loads(value(CARD_JS))

        def type_name(text):
            tab.ev("var n = document.getElementById('player-name'); n.value = %s; n.dispatchEvent(new Event('input')); 1" % json.dumps(text))

        def centre(selector):
            """The middle of an element in the window, after it was scrolled into view."""
            return json.loads(value("(function () { var e = document.querySelector(%s); e.scrollIntoView({ block: 'center' }); var r = e.getBoundingClientRect(); return JSON.stringify({ x: r.left + r.width / 2, y: r.top + r.height / 2 }); })()" % json.dumps(selector)))

        def real_click(selector):
            p = centre(selector)
            tab.click(p["x"], p["y"])

        def key(name, code, text=""):
            for kind in ("keyDown", "keyUp"):
                tab.call("Input.dispatchKeyEvent", {"type": kind, "key": name, "code": name, "windowsVirtualKeyCode": code, "text": text if kind == "keyDown" else ""})

        def score_strip(cv):
            """The rectangle of the bottom of the picture where the HUD shows the other players' scores: below the map view, whose last rows show the player's own ants (they move, bots or not), and
            left of the version and the frame counter (which change at every frame; the three score boxes end at 0.81 of the canvas's width, those start at 0.87). The canvas's rectangle ->
            [x0, y0, x1, y1] in page pixels."""
            return [int(cv[0] + cv[2] * 0.17), int(cv[1] + cv[3] * 0.965), int(cv[0] + cv[2] * 0.85), int(cv[1] + cv[3] * 0.995)]

        def enter(t):
            """One press of Enter in the game's picture of the tab or person `t` (the quick help closes with it)."""
            t.ev("document.getElementById('canvas') && document.getElementById('canvas').focus(); 1")
            for kind in ("keyDown", "keyUp"):
                t.call("Input.dispatchKeyEvent", {"type": kind, "key": "Enter", "code": "Enter", "windowsVirtualKeyCode": 13, "text": "\r" if kind == "keyDown" else ""})

        def close_quick_help(presses=4):
            """The quick help closes with Enter (an Enter that comes while the loading screen is still up only ends that: it is pressed again, a few seconds apart, until the match runs)."""
            pressed = 0
            for _ in range(presses):
                enter(tab)
                pressed += 1
                if wait_for(lambda: tab.ev("Module._ants_match_running()") == 1, 4.0):
                    break
            return pressed

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
                cards: document.querySelectorAll('.card').length, selects: document.querySelectorAll('select').length, host: !!document.getElementById('host'), solo: !!document.getElementById('map-solo'),
                banner: document.querySelector('.card .banner').textContent,
                start: (function () { var b = document.getElementById('play'), r = b.getBoundingClientRect(), cs = getComputedStyle(b);
                    return {text: b.textContent.trim(), label: b.getAttribute('aria-label'), w: Math.round(r.width), h: Math.round(r.height), bg: cs.backgroundImage, font: cs.fontSize}; })(),
                corners: Array.prototype.map.call(document.querySelectorAll('.btn, .banner, .pair label'), function (e) { return getComputedStyle(e).borderTopLeftRadius; }),
                faces: Array.prototype.map.call(document.querySelectorAll('.btn, .banner, .pair input:not(:checked) + label'), function (e) { return getComputedStyle(e).backgroundImage; }),
                chosen: Array.prototype.map.call(document.querySelectorAll('.pair input:checked + label'), function (e) { return getComputedStyle(e).backgroundImage; }),
                shown: [!document.getElementById('cards').hidden, !document.getElementById('how').hidden], name: document.getElementById('player-name').value,
                aspect: document.querySelector('input[name=aspect]:checked').value, code: !!document.getElementById('join-code') && !!document.getElementById('join-go'), haveCode: document.querySelector('.havecode .lab').textContent,
                links: Array.prototype.map.call(document.querySelectorAll('header a'), function (a) { return [a.textContent.trim(), a.getAttribute('href'), a.getAttribute('target')]; }),
                footer: document.querySelector('footer').textContent, scrollW: document.documentElement.scrollWidth, innerW: window.innerWidth})"""))
            now = card()
            check(info["title"].startswith("Ants (1998)") and info["path"] == "/", "the front page is at / and is titled Ants (1998) (%r)" % info["title"])
            check(info["cards"] == 1 and info["banner"] == "New match" and not info["host"] and not info["solo"] and info["selects"] == 2, "ONE card, New match (a select for the map and one for the Teams; no Host button, no second card) (%s cards, %r)" % (info["cards"], info["banner"]))
            check(now["map"] == "treasure" and now["you"] == [0] and now["seats"] == ["friend", "friend", "friend", "friend"] and now["teams"] == "ffa", "a first visit: Treasure, You at Green, a Friend in the other seats, free for all (%s)" % now)
            check(now["teamsShown"] and now["teamOptions"] == ["ffa", "0+1", "0+2", "0+3"] and now["invitesShown"] and now["invites"] == 3 and not now["startOff"] and now["note"].startswith("Starts when your friends are in"),
                  "... four seats play: the Teams offer Green with each of the others; an invitation for each Friend; START is on and says that it starts when the friends are in (%r)" % now["note"])
            start = info["start"]
            check(start["text"] == "START!" and start["label"] is None and "url(" not in start["bg"] and 150 <= start["w"] <= 260 and 44 <= start["h"] <= 64,
                  "START is a real button with its own text, drawn by the browser (no picture, so no blocky edges) at a modest size: 150 - 260 x 44 - 64 px, not the 294 x 81 of the original's picture blown up (%s)" % (start,))
            corners = [float(c[:-2]) if c and c.endswith("px") else None for c in info["corners"]]
            check(len(corners) >= 10 and None not in corners and len(set(corners)) == 1 and 8 <= corners[0] <= 12,
                  "every button, banner and two-state button has rounded corners, all alike (8 - 12 px; they were 2 and 3): START, the links, the seats' buttons and the rest (%d of them: %s)" % (len(corners), sorted(set(info["corners"]))))
            check(len(info["faces"]) >= 10 and all("linear-gradient" in f and "url(" not in f for f in info["faces"]) and info["chosen"] and all(f == "none" for f in info["chosen"]),
                  "... and their faces are lit from above (a gradient over the teal, %d of them), while a chosen two-state button is a flat dark plate (%d)" % (len(info["faces"]), len(info["chosen"])))
            check(info["shown"] == [True, True] and info["code"] and info["haveCode"] == "Have a code?", "\"How it works\" and \"Have a code?\" with its field and Join are there (%r)" % (info["haveCode"],))
            check(info["name"] == "" and info["aspect"] == "16:9", "no name yet, the picture is 16:9")
            hrefs = [l[1] for l in info["links"]]
            check("/asset_catalog/" in hrefs and "/changelog.html" in hrefs and any("github.com" in h and "issues" not in h for h in hrefs) and any(h.endswith("/issues") for h in hrefs),
                  "the header has the links: Sprites and sounds, Changelog, GitHub, Feedback (%s)" % [l[0] for l in info["links"]])
            check(all(l[2] == "_blank" for l in info["links"]), "the links that leave the page keep their new tab")
            check("Version" in info["footer"] and "@@" not in info["footer"] and "build" in info["footer"], "the footer names the version and the build (%r)" % info["footer"][:80])
            check(info["scrollW"] <= info["innerW"], "no horizontal scroll at 1440")
            check(bool(value("Array.from(document.fonts).some(function (f) { return f.family.indexOf('Libre Franklin') !== -1 && f.status === 'loaded'; })")), "the game's own font, Libre Franklin, is loaded from the site")
            stats = json.loads(value("JSON.stringify({hidden: document.getElementById('stats').hidden, text: document.getElementById('stats').textContent.replace(/\\s+/g, ' ').trim()})"))
            check(stats["hidden"] or re.match(r"^\d[\d,]* matches? being played · \d[\d,]* players? online( \d[\d,]* games? played \(\d[\d,]* today\))?$", stats["text"]) is not None,
                  "the line of numbers is either not there (a site with no /stats) or says what it counts (%r)" % (stats,))

            def contrast(where):
                """Every text of the page is at least 4.5:1 (the help is opened for it, and closed again)."""
                found = json.loads(value("var how = document.getElementById('how'), was = how.open; how.open = true; var found = " + CONTRAST_JS + "; how.open = was; found"))
                check(found["texts"] > 40 and found["lowest"][0][0] >= 4.5, "%s: the text contrast is at least 4.5:1 for all %d texts (lowest: %s)" % (where, found["texts"], found["lowest"]))

            contrast("1440 px")
            # the invitations of a first visit, the note about changed links (a copy, then a change) and a START that is for one player: every text of them is readable too
            tab.ev("document.querySelector('#invite-list .btn').click(); 1")
            time.sleep(0.4)
            tab.ev("document.getElementById('seat-2-hard').click(); 1")
            c = card()
            check(c["invites"] == 2 and c["invitesShown"] and c["linksNote"], "two invitations and, after a copy and a change, the note about changed links are up (%s)" % {k: c[k] for k in ("invites", "linksNote")})
            contrast("1440 px with an invitation and its note")
            tab.ev("document.getElementById('seat-1-nobody').click(); document.getElementById('seat-2-nobody').click(); document.getElementById('seat-3-nobody').click(); 1")
            c = card()
            check(not c["startOff"] and c["note"] == "Starts at once, on this computer: just you on the map, no opponents." and not c["teamsShown"] and not c["invitesShown"],
                  "with no other player START stays on and the line under it says that it starts a game for one on this computer (%r)" % c["note"])
            contrast("1440 px with START for one player")
            clear_storage()
            load(web, settle=1.0)
            # many widths: no sideways scroll at any of them; the five buttons of a seat on ONE line; the seat's line (the colour, the buttons and Sit here on one line from 701 px up, the buttons under the
            # colour up to 700 px); the map and the seats in two columns from 1100 px up, one above the other under it
            wrong = []
            for width in (320, 340, 360, 375, 390, 414, 480, 600, 700, 701, 768, 900, 1024, 1099, 1100, 1219, 1220, 1280, 1366, 1440, 1600):
                tab.emulate(width, 900, 1, width <= 480)       # (a phone's scrollbar is an overlay and takes no width: a browser that shows classic ones leaves 305 px of a 320 px window, which five buttons cannot share)
                time.sleep(0.3)
                m = json.loads(value(LAYOUT_JS))
                problems = []
                if m["scrollW"] > m["innerW"]:
                    problems.append("scrolls sideways (%d > %d)" % (m["scrollW"], m["innerW"]))
                two = m["right"]["left"] >= m["left"]["right"] - 1 and abs(m["right"]["top"] - m["left"]["top"]) < 30
                if two != (width >= 1100):
                    problems.append("%s columns" % ("two" if two else "one"))
                if not two and m["right"]["top"] < m["left"]["bottom"] - 1:
                    problems.append("the seats overlap the map")
                for r in m["rows"]:
                    if r["you"]:
                        continue
                    if not (r["oneLine"] and r["buttons"] == 5):
                        problems.append("seat %d: the buttons are not on one line" % r["seat"])
                    if not r["inside"]:
                        problems.append("seat %d: something sticks out of its row" % r["seat"])
                    beside = abs(r["pair"]["top"] - r["nm"]["top"]) < 25 and r["sit"]["left"] >= r["pair"]["right"] - 1
                    under = r["pair"]["top"] >= r["nm"]["bottom"] - 3 and r["sit"]["top"] < r["pair"]["top"]
                    if (width >= 701 and not beside) or (width <= 700 and not under):
                        problems.append("seat %d: %s" % (r["seat"], "the line is not one line" if width >= 701 else "the buttons are not under the colour"))
                if problems:
                    wrong.append((width, problems[:3]))
            check(not wrong, "21 widths from 320 to 1600 px: no sideways scroll, the five buttons of a seat on one line, a seat on one line from 701 px and on two under it, two columns from 1100 px (wrong: %s)" % (wrong,))
            for width, height, name in ((1366, 900, "home_front_1366"), (768, 1000, "home_front_768")):
                tab.emulate(width, height, 1)
                time.sleep(0.4)
                shot(name)
            tab.emulate(390, 844, 2, mobile=True)
            tab.open(web, wait=False)
            time.sleep(1.5)
            phone = json.loads(value("JSON.stringify({scrollW: document.documentElement.scrollWidth, innerW: window.innerWidth, left: document.getElementById('who').getBoundingClientRect().left, startBottom: document.getElementById('play').getBoundingClientRect().bottom})"))
            check(phone["scrollW"] <= phone["innerW"] and phone["left"] >= 15, "phone width (390): no horizontal scroll, 16 px gutters (%s)" % phone)
            contrast("390 px")
            shot("home_front_phone")
            tab.ev("document.getElementById('seat-1-friend').click(); document.getElementById('seat-2-friend').click(); 1")
            time.sleep(0.4)
            shot("home_front_phone_friends")
            tab.emulate(1440, 900, 1)

        # ------------------------------------------------------------------------------------------------------------------------------------------------------------
        if wanted("seats"):
            print("[web home] the seats: the keyboard and the pointer, Sit here, the Teams, the invitations, START (always on)")
            clear_storage()
            browser.devtools.call("Browser.grantPermissions", {"origin": re.match(r"^https?://[^/]+", web).group(0), "permissions": ["clipboardReadWrite", "clipboardSanitizedWrite"]})
            load(web, settle=1.5)
            groups = json.loads(value("""JSON.stringify([0, 1, 2, 3].map(function (n) { var f = document.querySelector('input[name=seat-' + n + ']').closest('fieldset');
                return [f.querySelector('legend').textContent, Array.prototype.map.call(f.querySelectorAll('input[type=radio]'), function (r) { return r.value; }).join()]; }))"""))
            check(groups == [[c, "friend,easy,medium,hard,nobody"] for c in ("Green", "Red", "Blue", "Black")], "every seat is a fieldset named by its colour (its legend) with the buttons Friend, Easy, Medium, Hard, Nobody (%s)" % groups)
            tab.ev("[0, 1, 2, 3].forEach(function (n) { document.getElementById('seat-' + n + '-medium').click(); }); 1")          # (a first visit has a Friend in every seat: Medium bots here, also in the seat that You leaves)
            tab.ev("document.getElementById('seat-1-medium').focus(); 1")
            key("ArrowRight", 39)
            c = card()
            check(c["seats"][1] == "hard" and tab.ev("document.activeElement.id") == "seat-1-hard", "ArrowRight moves from Medium to Hard and checks it (%s)" % c["seats"])
            key("ArrowRight", 39)
            key("ArrowRight", 39)
            check(card()["seats"][1] == "friend" and tab.ev("document.activeElement.id") == "seat-1-friend", "ArrowRight twice more goes round from Hard over Nobody to Friend, as a browser's radio buttons do (%s)" % card()["seats"])
            check(card()["invites"] == 1 and card()["invitesShown"], "Friend at Red: the invitation is there")
            key("ArrowLeft", 37)
            key("ArrowLeft", 37)
            c = card()
            check(c["seats"] == ["medium", "hard", "medium", "medium"] and not c["invitesShown"], "ArrowLeft twice goes back round from Friend over Nobody to Hard: the groups of the other seats did not move, the invitation is gone (%s)" % c["seats"])
            key("ArrowDown", 40)
            check(card()["seats"][1] == "nobody", "ArrowDown is ArrowRight (%s)" % card()["seats"])
            key("ArrowUp", 38)
            check(card()["seats"][1] == "hard", "ArrowUp is ArrowLeft (%s)" % card()["seats"])
            key("Tab", 9)
            check(tab.ev("document.activeElement.id") == "sit-1", "Tab leaves the group for that seat's Sit here (%s)" % tab.ev("document.activeElement.id"))
            outline = json.loads(value("""(function () { var c = getComputedStyle(document.activeElement); return JSON.stringify([c.outlineStyle, c.outlineWidth]); })()"""))
            check(outline[0] != "none" and outline[1] == "3px", "the focused Sit here shows a visible outline of 3 px (%s)" % outline)
            key("Tab", 9)
            check(tab.ev("document.activeElement.id") == "seat-2-medium", "Tab goes on to the next seat, at its checked button (%s)" % tab.ev("document.activeElement.id"))
            outline = json.loads(value("""(function () { var l = document.querySelector('label[for=seat-2-medium]'); var c = getComputedStyle(l); return JSON.stringify([c.outlineStyle, c.outlineWidth]); })()"""))
            check(outline[0] != "none" and outline[1] == "3px", "the focused button shows a visible outline of 3 px (%s)" % outline)
            key("Tab", 9)
            key("Enter", 13, "\r")
            c = card()
            check(c["you"] == [2] and tab.ev("document.activeElement.id") == "sit-0", "Enter on Sit here at Blue: You are at Blue, and the focus goes to the Sit here of Green, the seat that You left (%s, %s)" % (c["you"], tab.ev("document.activeElement.id")))
            check(c["seats"] == ["medium", "hard", "medium", "medium"], "... the choices of the seats stayed (Green kept Medium, Blue keeps its own while it is You) (%s)" % c["seats"])
            real_click("#sit-3")
            check(card()["you"] == [3], "a click of the pointer on Sit here at Black: You are at Black (%s)" % card()["you"])
            check(bool(tab.ev("var r = document.getElementById('seat-name-3'); !r.hidden && r.textContent.length > 0")), "... and the name stands under You at Black")
            type_name("Maya")
            check(tab.ev("document.getElementById('seat-name-3').textContent") == "Maya", "the name that is typed is under You as it is typed")
            real_click("label[for=seat-1-friend]")
            real_click("label[for=seat-2-hard]")
            c = card()
            check(c["seats"][1] == "friend" and c["seats"][2] == "hard" and c["invites"] == 1, "a click on Friend (Red) and on Hard (Blue) with the pointer: the choices and one invitation (%s)" % c["seats"])
            check(c["teamsShown"] and c["teamOptions"] == ["ffa", "0+1", "0+2", "0+3"], "Green, Red, Blue and Black play (You at Black): the Teams offer Green with each of the others (%s)" % c["teamOptions"])
            tab.ev("document.getElementById('seat-0-nobody').click(); 1")
            c = card()
            check(c["teamOptions"] == ["ffa", "1+2", "1+3", "2+3"], "Green is Nobody: three seats play (Red, Blue, Black) and the Teams offer the pairs of them (%s)" % c["teamOptions"])
            tab.ev("var t = document.getElementById('teams'); t.value = '2+3'; t.dispatchEvent(new Event('change')); 1")
            c = card()
            room_code = re.search(r"room=([^&]+)", c["links"][0]).group(1) if c["links"] else ""
            check(c["teams"] == "2+3" and re.match(r"^demo-treasure-4p-t23-[a-z2-9]{6}$", room_code) is not None, "Blue + Black against Red is a word of the room's code (t23) (%s)" % room_code)
            link = c["links"][0]
            q = {k: v for k, v in (kv.split("=", 1) for kv in link.split("?", 1)[1].split("&"))}
            check(q.get("seat") == "1" and q.get("fill") == "none,none,hard,none" and q.get("start") == "2" and q.get("aspect") == "16:9" and "name" not in q and "teams" not in q and not re.search(r"key|token", link, re.I),
                  "the link of the Friend at Red: its seat, the plan (a Hard bot at Blue; none for You, the friend and Nobody), two people to wait for, the shape; no name, no teams (the code has them), no key (%s)" % link.split("?", 1)[1])
            share = tab.ev("typeof navigator.share === 'function'")
            buttons = tab.ev("Array.prototype.map.call(document.querySelectorAll('#invite-list .invite .btn'), function (b) { return b.textContent; })")
            check(buttons == (["Copy link", "Share"] if share else ["Copy link"]), "the invitation has Copy link, and Share where the browser has it (%s; share: %s)" % (buttons, share))
            # what the page hands to the clipboard is recorded (the page's own call is still made), and the clipboard itself is read where the browser lets this page do it
            tab.ev("window.__copied = []; (function () { var c = navigator.clipboard; if (c && c.writeText) { var w = c.writeText.bind(c); c.writeText = function (t) { window.__copied.push(t); return w(t); }; } })(); 1")
            real_click("#invite-list .invite .btn")
            wait_for(lambda: tab.ev("document.querySelector('#invite-list .invite .btn').textContent") == "Copied", 3.0, 0.05)
            handed = tab.ev("window.__copied")
            check(handed == [link], "Copy link hands exactly that link to the clipboard, once (%r)" % ([h[:90] for h in handed] if isinstance(handed, list) else handed))
            check(tab.ev("document.querySelector('#invite-list .invite .btn').textContent") == "Copied", "... and the button says Copied")
            clip = tab.ev("navigator.clipboard.readText().catch(function (e) { return 'unreadable: ' + e.name; })")
            if isinstance(clip, str) and clip.startswith("unreadable: "):
                note("the browser did not let the page read its clipboard (%s): what was handed to it is what was checked" % clip)
            else:
                check(clip == link, "... and that link is what the clipboard holds (%r)" % (clip[:90] if isinstance(clip, str) else clip))
            tab.ev("var m = document.getElementById('map-pick'); m.value = 'small'; m.dispatchEvent(new Event('change')); 1")
            c = card()
            check(c["linksNote"] and re.search(r"room=demo-small-4p-t23-", c["links"][0]) is not None, "another map after a copy: a new room, new links, and the note says to copy them again (%s)" % c["linksNote"])
            # two Friends (Blue is one too): the note stays up until every link that was sent is the one that is shown for its seat
            tab.ev("document.getElementById('seat-2-friend').click(); 1")
            real_click("#invite-list .invite:nth-child(1) .btn")
            real_click("#invite-list .invite:nth-child(2) .btn")
            time.sleep(0.4)
            check(not card()["linksNote"], "two Friends, both links copied: no note")
            tab.ev("var m = document.getElementById('map-pick'); m.value = 'tiny'; m.dispatchEvent(new Event('change')); 1")
            check(card()["linksNote"], "... another map: the note is up")
            real_click("#invite-list .invite:nth-child(1) .btn")
            check(card()["linksNote"], "... one of the links copied again: the note stays (the link that was sent to the other friend is the old room's)")
            real_click("#invite-list .invite:nth-child(2) .btn")
            check(not card()["linksNote"], "... both copied again: the note goes")
            shot("home_seats")
            tab.ev("['1', '2'].forEach(function (n) { document.getElementById('seat-' + n + '-nobody').click(); }); 1")
            c = card()
            check(not c["startOff"] and c["note"] == "Starts at once, on this computer: just you on the map, no opponents. Alone you play Green." and not c["teamsShown"] and not c["invitesShown"],
                  "everybody else Nobody (You at Black): START is on, the line says that it is a game for one on this computer and that You play Green, the Teams and the invitations are gone (%r)" % c["note"])

        # ------------------------------------------------------------------------------------------------------------------------------------------------------------
        if wanted("play") and not can_play:
            note("play: left out (no game server: give --ws-port)")
        if wanted("play") and can_play:
            print("[web home] START with bots only: this tab goes into a match against Medium bots, which starts by itself")
            clear_storage()
            load(web, settle=1.5)
            type_name("Bob")
            tab.ev("[0, 1, 2, 3].forEach(function (n) { document.getElementById('seat-' + n + '-medium').click(); }); 1")          # (a first visit has a Friend in every seat: Medium bots here)
            before = pages()
            tab.ev("document.getElementById('play').click(); 1")
            ok = wait_for(lambda: "join=" in tab.ev("location.search"), 20)
            check(ok, "START takes this tab to the game page of a room")
            if ok and wait_for(lambda: tab.ev("!!window.isReadyToPlay"), args.ready_timeout):
                time.sleep(1.0)
                check(pages() == before, "no new tab or window was opened (%d pages before and after)" % before)
                a = json.loads(tab.ev("JSON.stringify({args: ANTS_ARGS, search: location.search})"))
                arg = lambda name: a["args"][a["args"].index(name) + 1] if name in a["args"] else None
                code = arg("--room") or ""
                check(re.match(r"^demo-treasure-4p-[a-z2-9]{6}$", code) is not None and arg("--seat") == "0" and arg("--fill-bots") == "none,medium,medium,medium" and arg("--start-when") == "1" and arg("--name") == "Bob" and arg("--aspect") == "16:9",
                      "the game's arguments: the room (%s), seat 0, the plan none,medium,medium,medium, --start-when 1, the name and the shape (%s)" % (code, [x for x in a["args"] if x != "./this.program"]))
                check("--map" not in a["args"] and "--play" not in a["args"] and "--bot" not in a["args"] and "--teams" not in a["args"], "... no local game's argument (no --map, --play, --bot) and no --teams (free for all)")
                check(a["search"] == "?join=/ws&room=%s&fill=none,medium,medium,medium&aspect=16:9&start=1&seat=0" % code,
                      "the address bar keeps START's room, plan, shape and number of people, and the seat that the room gave this window (the game tells the page: it stands at the end), but not the name (the game page takes it out) (%s)" % a["search"])
                check(not tab.ev("document.getElementById('name-step') && !document.getElementById('name-step').hidden"), "the game does not ask for a name (the front page chose it)")
                pressed = close_quick_help()
                started = bool(wait_for(lambda: tab.ev("Module._ants_match_running()") == 1, 30))
                check(started, "the match starts by itself once the quick help is closed (Enter pressed %d time(s) before the match ran)" % pressed)
                shot("home_play_start")
                if started:
                    check(bool(wait_for(lambda: tab.ev("Module._ants_probe(5)") == 0, 60, 0.5)), "... with the \"Get ready\" dialog, which closes by itself")
                    status = server.room(code) or {}
                    bots = status.get("bots", [])
                    names = {p.get("seat"): p.get("name") for p in status.get("players", [])}
                    check(status.get("state") == "running" and [b.get("seat") for b in bots] == [1, 2, 3] and all(b.get("level") == "medium" and b.get("fill") for b in bots) and names.get(0) == "Bob" and status.get("joined") == 4,
                          "the server's status: running, Bot (Medium) at the seats 1, 2 and 3 (seated by the leader's START), Bob at seat 0 (%s)" % {"state": status.get("state"), "joined": status.get("joined"), "bots": [(b.get("seat"), b.get("name")) for b in bots]})
                    check(status.get("ignored_start_requests", 0) == 0, "no START that was not the leader's was heard (%s)" % status.get("ignored_start_requests"))
                    first = tab.shot()
                    t0 = time.time()
                    time.sleep(max(0.0, args.bot_seconds - 1.0))
                    later = tab.shot()
                    cv = tab.geometry()["canvas"]
                    box = score_strip(cv)
                    changed = aspect.count_changed_pixels(first, later, box[0], box[1], box[2], box[3])
                    shot("home_play_later")
                    check(changed > 60, "the bots' scores at the bottom of the picture changed in %.0f s (%d pixels of the strip): their ants move" % (time.time() - t0, changed))
                    check(tab.ev("Module._ants_match_running()") == 1, "the match is still running in this tab")
                    check(not any("out of sync" in d for d in dialogs), "no dialog said that the game was out of sync")
                    # the Menu: the way back to the front page, in the same tab; it asks first while a match runs
                    before = pages()
                    dialogs.clear()
                    answer["accept"] = False
                    tab.ev("document.getElementById('menu-btn').click(); 1")
                    time.sleep(0.8)
                    check(len(dialogs) == 1 and "menu" in dialogs[0].lower() and "join=" in tab.ev("location.search"), "menu: while a match runs the link asks first (%s) and No stays in the game" % dialogs)
                    answer["accept"] = True
                    tab.ev("document.getElementById('menu-btn').click(); 1")
                    back = wait_for(lambda: tab.ev("location.search") == "" and bool(tab.ev("document.getElementById('player-name') ? 1 : 0")), 20)
                    check(back and pages() == before, "menu: Yes goes back to the front page in the same tab (no new tab)")
                    if back:
                        remembered = card()
                        check(remembered["map"] == "treasure" and remembered["you"] == [0] and remembered["seats"] == ["medium", "medium", "medium", "medium"] and tab.ev("document.getElementById('player-name').value") == "Bob",
                              "menu: the front page has its choices again and remembers the name (%s)" % {k: remembered[k] for k in ("map", "you", "seats")})

        # ------------------------------------------------------------------------------------------------------------------------------------------------------------
        if wanted("solo"):
            print("[web home] START with every other seat Nobody: a game for one on this computer, in this tab")
            clear_storage()
            load(web, settle=1.5)
            type_name("Bob")
            tab.ev("[1, 2, 3].forEach(function (n) { document.getElementById('seat-' + n + '-nobody').click(); }); 1")
            c = card()
            check(not c["startOff"] and c["note"] == "Starts at once, on this computer: just you on the map, no opponents." and not c["teamsShown"] and not c["invitesShown"],
                  "every other seat Nobody: START is on and says that it is a game for one on this computer (%r)" % c["note"])
            before = pages()
            tab.ev("document.getElementById('play').click(); 1")
            ok = wait_for(lambda: tab.ev("location.pathname") == "/play.html", 20)
            where = tab.ev("location.pathname + location.search") if ok else ""
            check(ok and "map=treasure" in where and "bots=" not in where and "join=" not in where and "room=" not in where,
                  "START takes this tab to /play.html with the map: no bots, no room (the game page takes the name out of the address: it is in the game's arguments) (%s)" % where)
            if ok and wait_for(lambda: tab.ev("!!window.isReadyToPlay"), args.ready_timeout):
                check(pages() == before, "no new tab or window was opened (%d pages before and after)" % before)
                a = json.loads(tab.ev("JSON.stringify({args: ANTS_ARGS})"))
                given = [x for x in a["args"] if x != "./this.program"]
                check("--map" in given and "--play" in given and "--bot" not in given and "--join-url" not in given and "--room" not in given and "--teams" not in given and given[given.index("--name") + 1:][:1] == ["Bob"],
                      "the game's arguments are a game on this computer with no bot: --map, --play and the name, no --bot, no room, no --teams (%s)" % given)
                check(not tab.ev("document.getElementById('name-step') && !document.getElementById('name-step').hidden"), "the game does not ask for a name (the front page chose it)")
                started = start_by_enter()
                check(started, "the match starts once the quick help is closed (Enter), with the \"Get ready\" dialog, which closes by itself")
                if started:
                    time.sleep(3.0)
                    check(tab.ev("Module._ants_match_running()") == 1, "... and goes on with its one colony (the match does not end at once)")
                    check(not any("out of sync" in d for d in dialogs), "no dialog said that the game was out of sync")
                    shot("home_solo_running")
            else:
                check(False, "the game page of a game on this computer is ready")

        if wanted("friend") and not can_play:
            note("friend: left out (no game server: give --ws-port)")
        if wanted("friend") and can_play:
            print("[web home] a Friend seat: the room waits for the friend, who opens the invitation; the match starts by itself")
            host = rejoin.Player(path, "Host", 2, web, args.ready_timeout)
            people.append(host)
            guest = rejoin.Player(path, "Pal", 3, web, args.ready_timeout)
            people.append(guest)
            host.call("Page.navigate", {"url": web})
            wait_for(lambda: host.ev("!!document.getElementById('play')"), 20)
            time.sleep(1.0)
            host.ev("""var n = document.getElementById('player-name'); n.value = 'Host'; n.dispatchEvent(new Event('input'));
                       document.getElementById('sit-2').click(); document.getElementById('seat-3-friend').click(); document.getElementById('seat-0-nobody').click(); document.getElementById('seat-1-nobody').click(); 1""")
            seats = json.loads(host.ev(CARD_JS))
            check(seats["you"] == [2] and seats["seats"][3] == "friend" and seats["invites"] == 1 and not seats["startOff"] and seats["note"].startswith("Starts when your friend is in"),
                  "the host sits at Blue, a Friend at Black, Green and Red are Nobody: one invitation, START on, and the line says that it waits for the friend (%s)" % {k: seats[k] for k in ("you", "seats", "invites")})
            invite = seats["links"][0]
            room_code = re.search(r"room=([^&]+)", invite).group(1)
            check(re.match(r"^demo-treasure-4p-[a-z2-9]{6}$", room_code) is not None and "seat=3" in invite and "start=2" in invite and "name=" not in invite and "fill=" not in invite, "the invitation: the room, seat 3, two people to wait for, no name, no plan (no bots) (%s)" % invite.split("?", 1)[1])
            host.ev("document.getElementById('play').click(); 1")
            ready = wait_for(lambda: "join=" in host.ev("location.search") and host.ev("!!window.isReadyToPlay"), args.ready_timeout)
            check(bool(ready), "START takes the host's tab to the game page of the room")
            if ready:
                hargs = json.loads(host.ev("JSON.stringify(ANTS_ARGS)"))
                harg = lambda name: hargs[hargs.index(name) + 1] if name in hargs else None
                check(harg("--room") == room_code and harg("--seat") == "2" and harg("--start-when") == "2" and harg("--name") == "Host" and "--fill-bots" not in hargs,
                      "the host's game: the same room as the invitation, seat 2, --start-when 2, no plan (%s)" % [x for x in hargs if x != "./this.program"])
                for _ in range(5):                                       # (nobody else is in and the plan has no bot: an Enter in the waiting room starts nothing, so the quick help is closed with several, a while apart)
                    enter(host)
                    time.sleep(2.0)
                time.sleep(2.0)
                status = server.room(room_code) or {}
                check(status.get("state") == "waiting" and status.get("joined") == 1 and not host.in_match(), "the room waits: the server says waiting with one player, and the host's game does not run a match (%s)" % {k: status.get(k) for k in ("state", "joined", "leader")})
                guest.call("Page.navigate", {"url": invite})
                asked = wait_for(lambda: guest.ev("(function () { var s = document.getElementById('name-step'); return !!s && !s.hidden; })()"), 60)
                check(bool(asked), "the friend opens the invitation and is asked for a name first (the link has none)")
                if asked:
                    guest.ev("document.getElementById('name-step-input').value = 'Pal'; document.getElementById('name-step-go').click(); 1")
                    gready = wait_for(lambda: guest.ev("!!window.isReadyToPlay"), args.ready_timeout)
                    check(bool(gready), "the friend's game is ready after the name")
                    gargs = json.loads(guest.ev("JSON.stringify(ANTS_ARGS)")) if gready else []
                    garg = lambda name: gargs[gargs.index(name) + 1] if name in gargs else None
                    check(garg("--room") == room_code and garg("--seat") == "3" and garg("--start-when") == "2" and garg("--name") == "Pal", "the friend's game: the room, seat 3, --start-when 2, the name that was typed (%s)" % [x for x in gargs if x != "./this.program"])
                    # the friend closes the quick help too (an Enter in the waiting room of a game that does not lead sends nothing); nobody presses START
                    began = time.time()
                    started = None
                    both = False
                    last_enter = 0.0
                    while time.time() - began < 150 and not both:
                        status = server.room(room_code) or {}
                        if started is None and status.get("state") in ("running", "loading"):
                            started = time.time() - began
                        both = started is not None and host.in_match() and guest.in_match()
                        shown = guest.state()
                        if not both and shown is not None and shown[1] != 1 and time.time() - last_enter >= 3.0 and time.time() - began > 4.0:
                            enter(guest)
                            last_enter = time.time()
                        time.sleep(0.5)
                    check(started is not None, "the match starts by itself when the friend is in (nobody pressed START after that: %s s after the friend's name)" % (round(started) if started is not None else "never"))
                    check(bool(both), "both games run the match, the \"Get ready\" dialog closed")
                    status = server.room(room_code) or {}
                    names = {p.get("seat"): p.get("name") for p in status.get("players", [])}
                    check(status.get("joined") == 2 and names == {2: "Host", 3: "Pal"} and not status.get("bots"), "the server's status: Host at Blue, Pal at Black, nobody else (Green and Red are Nobody), no bot (%s)" % names)
                    check(status.get("ignored_start_requests", 0) == 0, "no START of a game that did not lead was heard (%s)" % status.get("ignored_start_requests"))
                    for _ in range(40):
                        host.syncs()
                        guest.syncs()
                        common = [t for t in sorted(host.ticks) if t in guest.ticks]
                        if len(common) >= 2:
                            break
                        time.sleep(1.0)
                    common = [t for t in sorted(host.ticks) if t in guest.ticks]
                    check(len(common) >= 2 and all(host.ticks[t] == guest.ticks[t] for t in common) and not host.conflicts and not guest.conflicts, "the two games' state hashes agree at the ticks that both reported (%s)" % common[:6])
                    host.shot(args.shots, "home_friend_host")
                    guest.shot(args.shots, "home_friend_guest")
            for p in people:
                try:
                    p.close()
                except Exception:                                        # noqa: BLE001
                    pass
            people.clear()

        # ------------------------------------------------------------------------------------------------------------------------------------------------------------
        if wanted("old"):
            print("[web home] the old addresses")
            clear_storage()
            before = pages()
            load(web + "?join=/ws&room=demo-small-2p-abc123&aspect=16:9", ready=False, settle=2.0)
            info = json.loads(value("JSON.stringify({stage: !!document.getElementById('game-stage'), lobby: !!document.getElementById('player-name'), ask: !document.getElementById('name-step').hidden, args: ANTS_ARGS})"))
            check(info["stage"] and not info["lobby"] and info["ask"], "a shared game link (/?join=/ws&room=...) opens the game page, which asks for a name")
            check(tab.ev("(function () { var a = document.getElementById('name-step-back'); return !!a && a.getAttribute('href') === '/' && !!a.closest('.name-step') && a.textContent.indexOf('front page') !== -1; })()"),
                  "... and the name step has its way out: a link to the front page inside the card")
            check("--join-url" in info["args"] and "--map" not in info["args"] and "--play" not in info["args"], "... a game of the server: no local parameter reaches it (%s)" % info["args"])
            load(web + "?embed=1&aspect=16:9", ready=False, settle=1.5)
            check(tab.ev("!!document.getElementById('game-stage') && document.body.classList.contains('embed')"), "/?embed=1 opens the game page as a frame's game")
            load(web + "four.html?room=demo-small-2p-abc123&fill=medium", ready=False, settle=2.0)
            info = json.loads(value("JSON.stringify({path: location.pathname, search: location.search, lobby: !!document.getElementById('player-name'), ask: !document.getElementById('who-go').hidden, title: document.getElementById('who-title').textContent, back: !document.getElementById('who-back').hidden && document.getElementById('who-back').getAttribute('href')})"))
            check(info["path"] == "/" and info["search"].startswith("?room=demo-small-2p-abc123") and info["lobby"], "/four.html?room=... goes to /?room=... for good (%s%s)" % (info["path"], info["search"]))
            check(info["ask"] and "demo-small-2p-abc123" in info["title"] and info["back"] == "/", "... and the front page asks for the name of the shared link, with its way back to the front page (%r)" % info["title"])
            load(web + "play.html", ready=True, settle=1.0)
            info = json.loads(value("JSON.stringify({args: ANTS_ARGS})"))
            given = [a for a in info["args"] if a != "./this.program"]                             # (the runtime puts the program's own name in front)
            check(given == ["--aspect", "16:9"], "/play.html with nothing is today's front page: no argument but the picture's shape (%s)" % given)
            shot("home_play_plain")
            load(web + "?map=small&players=1&fill=hard", ready=False, settle=1.5)
            info = json.loads(value("JSON.stringify({go: document.getElementById('who-go').textContent, hidden: document.getElementById('who-go').hidden, cards: document.getElementById('cards').hidden})"))
            check(not info["hidden"] and info["go"] == "Play" and info["cards"], "/?map=small&players=1&fill=hard still asks for a name (its button says Play) and plays a game on this computer")
            tab.ev("document.getElementById('who-go').click(); 1")
            ok = wait_for(lambda: tab.ev("location.pathname") == "/play.html", 20)
            where = tab.ev("location.pathname + location.search") if ok else ""
            check(ok and "map=small" in where and "bots=hard" in where, "... after the name it takes this tab to /play.html with the map and the Hard bots (%s)" % where)
            check(pages() == before, "no new tab was opened by any of it")

        # ------------------------------------------------------------------------------------------------------------------------------------------------------------
        if wanted("room"):
            print("[web home] an address that hosts a match: the room panel, and Play in this tab")
            clear_storage()
            load(web + "?map=treasure&players=3&fill=easy&teams=0%2B1", settle=1.5)
            type_name("Ann")
            tab.ev("document.getElementById('who-go').click(); 1")
            time.sleep(0.8)
            room = tab.ev("document.getElementById('room-code').textContent")
            check(tab.ev("!document.getElementById('room-panel').hidden") and re.match(r"^demo-treasure-3p-t01-[a-z2-9]{6}$", room) is not None, "the address makes the room panel, and the room's code names its team (%s)" % room)
            check(tab.ev("document.body.classList.contains('in-room') && document.getElementById('cards').hidden && document.getElementById('how').hidden && getComputedStyle(document.querySelector('.tv')).display === 'none'"),
                  "... in its room mode: the card and the header's picture give way to the room")
            before = pages()
            tab.ev("document.getElementById('play-tab').click(); 1")
            ok = wait_for(lambda: tab.ev("location.pathname") == "/" and "join=" in tab.ev("location.search"), 15)
            time.sleep(1.0)
            a = json.loads(tab.ev("JSON.stringify({search: location.search, args: ANTS_ARGS})")) if ok else {"search": "", "args": []}
            check(ok and ("room=" + room) in a["search"] and pages() == before, "Play in this tab takes this tab to the game page of the room (%s), no new tab" % a["search"])
            check("--join-url" in a["args"] and "--fill-bots" in a["args"] and a["args"][a["args"].index("--fill-bots") + 1] == "easy" and "--name" in a["args"], "... with the room, the leader's bots and the name in the game's arguments (%s)" % a["args"])
            check("--room" in a["args"] and a["args"][a["args"].index("--room") + 1] == room and "--teams" not in a["args"], "... the room's team is in its code (the game reads it there), so there is no --teams (%s)" % a["args"])
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

            def centre_xy(selector):
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
                             page: { bg: body.backgroundImage, font: body.fontFamily, button: f.backgroundColor, face: f.backgroundImage, shadow: f.boxShadow, radius: f.borderTopLeftRadius, frame: getComputedStyle(document.body, '::after').boxShadow,
                                     fontLoaded: Array.from(document.fonts).some(function (x) { return x.family.indexOf('Libre Franklin') !== -1 && x.status === 'loaded'; }) } };
                })())"""))
                check(loading["shown"] and "front/clay.png" in loading["bg"] and not loading["old"], "the loading screen is the clay with no \"ANTS\" word on it (%s)" % loading["bg"])
                check(loading["logoLoaded"] and loading["inside"] and 100 < loading["logo"][0] <= 240, "... it shows the \"ants!\" logo (%.0f x %.0f px), whole and inside the picture's box" % tuple(loading["logo"]))
                check(loading["barShown"] and loading["barBg"] == "rgb(7, 11, 15)" and loading["fill"] == "rgb(43, 99, 87)" and loading["fillWidth"] == "100%", "... and a teal bar in a black box (%s, %s, %s)" % (loading["barBg"], loading["fill"], loading["fillWidth"]))
                page = loading["page"]
                check("front/clay.png" in page["bg"] and page["font"].startswith('"Libre Franklin"') and page["fontLoaded"], "the page is on the clay, in the game's own font Libre Franklin (loaded from the site)")
                check(page["button"] == "rgb(43, 99, 87)" and "rgb(157, 13, 23)" in page["shadow"] and "rgb(43, 95, 67)" in page["frame"] and "157, 13, 23" in page["frame"],
                      "a button of the header is the teal one with the red shadow, and the page has the thin green frame with its red line (%s)" % page["shadow"][:60])
                check(page["radius"].endswith("px") and 6 <= float(page["radius"][:-2]) <= 12, "... and the button's corners are rounded, as on the front page (%s)" % page["radius"])
                check("linear-gradient" in page["face"] and "url(" not in page["face"], "... and its face is lit from above, as on the front page (%s)" % page["face"][:40])
                shot("game_loading_1440")
                contrast_of("the loading screen at 1440 px", 20)
                tab.emulate(390, 844, 2, mobile=True)
                time.sleep(0.6)
                inside = value("(function () { var l = document.querySelector('.splash-logo').getBoundingClientRect(), b = document.getElementById('game-container').getBoundingClientRect(); return l.left >= b.left && l.right <= b.right && l.top >= b.top && l.bottom <= b.bottom; })()")
                check(inside is True, "the loading screen of a phone (390 px): the logo and the message fit the small picture's box")
                shot("game_loading_390")
                contrast_of("the loading screen at 390 px", 20)
                # More opened while the loading screen is up: its list is above the loading screen (the picture's box keeps that screen's layer to itself)
                x, y = centre_xy("header .more summary")
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
            x, y = centre_xy("#lock-off")
            tab.click(x, y)
            time.sleep(0.4)
            after = json.loads(value("JSON.stringify(['lock-on', 'lock-off'].map(function (id) { var e = document.getElementById(id); return [e.getAttribute('aria-checked'), getComputedStyle(e).backgroundColor]; }).concat([localStorage.getItem('ants.pointerlock')]))"))
            check(after == [["false", raised[0]], ["true", pressed[0]], "off"], "Free clicked: it is pressed in, Locked is teal, and the browser remembers \"off\" (%s)" % (after,))
            x, y = centre_xy("#lock-on")
            tab.click(x, y)
            time.sleep(0.4)
            check(value("document.getElementById('lock-on').getAttribute('aria-checked') + '/' + localStorage.getItem('ants.pointerlock')") == "true/on", "Locked clicked again: pressed in, remembered \"on\"")
            x, y = centre_xy("#aspect-4-3")
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
            x, y = centre_xy("header .more summary")
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
                back = json.loads(value("""JSON.stringify((function () {
                    var c = document.querySelector('.name-step').getBoundingClientRect(), a = document.getElementById('name-step-back'), r = a.getBoundingClientRect(), h = document.querySelector('.name-step-hint').getBoundingClientRect();
                    return { href: a.getAttribute('href'), text: a.textContent, bg: getComputedStyle(a).backgroundColor, inside: r.left >= c.left && r.right <= c.right && r.top >= h.bottom && r.bottom <= c.bottom };
                })())"""))
                check(back["href"] == "/" and "front page" in back["text"] and back["bg"] == "rgb(43, 99, 87)" and back["inside"],
                      "... and its way out at %s: a teal \"Back to the front page\" link under the hint, inside the card (%s)" % (label, back))
                real_click("#name-step-back")
                time.sleep(1.0)
                check(tab.ev("location.pathname + location.search") == "/" and tab.ev("!!document.getElementById('player-name')"), "... which takes the friend to the front page (the card, no room in the address)")
            tab.emulate(1440, 900, 1)
            clear_storage()
    except NotReachable as e:
        print("  SKIP: %s" % e)
        return 3
    except Exception as e:                                      # noqa: BLE001  (a page that hangs or crashes is a failure, never a skip)
        print("  FAIL: the check broke down: %s: %s" % (type(e).__name__, e))
        return 1
    finally:
        for p in people:
            try:
                p.close()
            except Exception:                                   # noqa: BLE001
                pass
        if browser is not None:
            browser.close()
        if server is not None:
            server.stop()
        if work:
            shutil.rmtree(work, ignore_errors=True)
    print("[web home] %d checks, %d failed" % (count[0], len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
