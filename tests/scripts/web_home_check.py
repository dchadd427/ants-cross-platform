#!/usr/bin/env python3
"""The front page and the way into and out of a game, in a REAL browser (opt-in; see tests/scripts/test_web_home.sh and docs/NETWORK_PORT.md, "The front page").

The owner: "when you click the play online button it opens in a new tab can we just make that the default. you can play single on the play online tab by setting to 1 player consolidate them".
Needs a running web page that has nginx's routes (the web image of this tree: `docker build -t ants-beta .`, run it on a port; the game server is not needed), a Chromium-based browser and
Python 3; nothing else (the DevTools protocol is spoken with the client of web_hidden_check.py, standard library only). Each part opens the site in a throwaway headless browser (its own
profile and port; nothing of yours is touched). What it checks:

  * the FRONT PAGE at "/" (a first visit: Treasure, 1 player, Medium opponents in all three bases, the button reads Play; the header links, the footer's version and build, the picture's selector, the name field);
  * PLAY: the button of 1 player takes THIS tab to the game page on the chosen map (no new tab or window is opened), the game's own arguments are the chosen map, the setup screen's own
    START (--play), the Medium bots of the three other bases and the typed name; the quick help closes with Enter and the match starts at once, with the "Get ready" dialog; the bots'
    scores, which the HUD shows at the bottom ("Bot (Medium)"), rise from 0, so their ants move (the bots are run by the game in the browser: no server); with Opponents None the match
    has no bots (no labels, nothing changes) and no --bot argument;
  * MENU: the game page's header link goes back to the front page in the same tab, and asks first while a match runs ("Leave the game and go back to the menu?": No stays, Yes leaves); the
    front page remembers the choices;
  * the OLD ADDRESSES: /?join=...&room=... and /?embed=1 open the game page (a shared link asks for a name), /four.html?room=... goes to /?room=... and the front page asks for the name of a
    shared link, /play.html with nothing is today's front page (the setup screen, no arguments);
  * the HOST: 3 players and Create the match make the room panel, and "Play in this tab" takes this tab to the game page with the room, the name and the bots of the leader's START.
Exit status 0: every check passed; 1: a check failed; 3: the check could not be made because the environment is not there (no browser, nothing answers at the page's address).
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import web_aspect_check as aspect                                            # noqa: E402
from web_aspect_check import Browser, NotReachable, Tab, read_png            # noqa: E402
from web_hidden_check import find_browser                                    # noqa: E402


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
    ap.add_argument("--only", default="", help="run only the parts whose name contains this text (front, play, alone, menu, old, host)")
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
            info = json.loads(value("""JSON.stringify({title: document.title, path: location.pathname, players: document.getElementById('players').value, map: document.getElementById('map').value,
                opponents: ['opponent-1', 'opponent-2', 'opponent-3'].map(function (id) { return document.getElementById(id).value; }).join(), button: document.getElementById('create').textContent, solo: !document.getElementById('solo-line').hidden,
                fill: !document.getElementById('fill-line').hidden, name: document.getElementById('player-name').value, aspect: document.getElementById('aspect-select').value,
                links: Array.prototype.map.call(document.querySelectorAll('header a'), function (a) { return [a.textContent.trim(), a.getAttribute('href'), a.getAttribute('target')]; }),
                footer: document.querySelector('footer').textContent, scrollW: document.documentElement.scrollWidth, innerW: window.innerWidth})"""))
            check(info["title"].startswith("Ants (1998)") and info["path"] == "/", "the front page is at / and is titled Ants (1998) (%r)" % info["title"])
            check(info["players"] == "1" and info["map"] == "treasure" and info["opponents"] == "medium,medium,medium", "a first visit: 1 player, Treasure, Medium opponents in all three bases (%s, %s, %s)" % (info["players"], info["map"], info["opponents"]))
            check(info["button"] == "Play" and info["solo"] and not info["fill"], "the button reads Play and the row is Opponents, not Empty seats at START (%r)" % info["button"])
            check(info["name"] == "" and info["aspect"] == "16:9", "no name yet, the picture is 16:9")
            hrefs = [l[1] for l in info["links"]]
            check("/asset_catalog/" in hrefs and "/changelog.html" in hrefs and any("github.com" in h and "issues" not in h for h in hrefs) and any(h.endswith("/issues") for h in hrefs),
                  "the header has the links: Sprites and sounds, Changelog, GitHub, Feedback (%s)" % [l[0] for l in info["links"]])
            check(all(l[2] == "_blank" for l in info["links"]), "the links that leave the page keep their new tab")
            check("Version" in info["footer"] and "@@" not in info["footer"] and "build" in info["footer"], "the footer names the version and the build (%r)" % info["footer"][:80])
            check(info["scrollW"] <= info["innerW"], "no horizontal scroll at 1440")
            tab.emulate(390, 844, 2, mobile=True)
            tab.open(web, wait=False)
            time.sleep(1.5)
            phone = json.loads(value("JSON.stringify({scrollW: document.documentElement.scrollWidth, innerW: window.innerWidth, left: document.getElementById('who').getBoundingClientRect().left})"))
            check(phone["scrollW"] <= phone["innerW"] and phone["left"] >= 15, "phone width (390): no horizontal scroll, 16 px gutters (%s)" % phone)
            shot("home_front_phone")
            tab.emulate(1440, 900, 1)

        # ------------------------------------------------------------------------------------------------------------------------------------------------------------
        def play_with(opponents, label, seconds):
            clear_storage()
            load(web, settle=1.5)
            tab.ev("['opponent-1', 'opponent-2', 'opponent-3'].forEach(function (id) { var o = document.getElementById(id); o.value = %s; o.dispatchEvent(new Event('change')); }); 1" % json.dumps(opponents))
            tab.ev("var n = document.getElementById('player-name'); n.value = 'Bob'; n.dispatchEvent(new Event('input')); 1")
            before = pages()
            tab.ev("document.getElementById('create').click(); 1")
            ok = wait_for(lambda: tab.ev("location.pathname") == "/play.html", 20)
            check(ok, "%s: Play takes this tab to /play.html" % label)
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
                    remembered = json.loads(tab.ev("JSON.stringify({players: document.getElementById('players').value, map: document.getElementById('map').value, opponents: ['opponent-1', 'opponent-2', 'opponent-3'].map(function (id) { return document.getElementById(id).value; }).join(), name: document.getElementById('player-name').value})")) if back else {}
                    check(remembered == {"players": "1", "map": "treasure", "opponents": "medium,medium,medium", "name": "Bob"}, "menu: the front page remembers the choices and the name (%s)" % remembered)

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
            tab.ev("var p = document.getElementById('players'); p.value = '3'; p.dispatchEvent(new Event('change')); var f = document.getElementById('fill'); f.value = 'easy'; f.dispatchEvent(new Event('change')); 1")
            tab.ev("var n = document.getElementById('player-name'); n.value = 'Ann'; n.dispatchEvent(new Event('input')); 1")
            check(tab.ev("document.getElementById('create').textContent") == "Create the match" and tab.ev("document.getElementById('fill-line').hidden") is False and tab.ev("document.getElementById('solo-line').hidden") is True,
                  "3 players: the button reads Create the match and the row is Empty seats at START")
            tab.ev("document.getElementById('create').click(); 1")
            time.sleep(0.8)
            room = tab.ev("document.getElementById('room-code').textContent")
            check(tab.ev("!document.getElementById('room-panel').hidden") and room.startswith("demo-treasure-3p-"), "Create the match makes the room panel (%s)" % room)
            before = pages()
            tab.ev("document.getElementById('play-tab').click(); 1")
            ok = wait_for(lambda: tab.ev("location.pathname") == "/" and "join=" in tab.ev("location.search"), 15)
            time.sleep(1.0)
            a = json.loads(tab.ev("JSON.stringify({search: location.search, args: ANTS_ARGS})")) if ok else {"search": "", "args": []}
            check(ok and ("room=" + room) in a["search"] and pages() == before, "Play in this tab takes this tab to the game page of the room (%s), no new tab" % a["search"])
            check("--join-url" in a["args"] and "--fill-bots" in a["args"] and a["args"][a["args"].index("--fill-bots") + 1] == "easy" and "--name" in a["args"], "... with the room, the leader's bots and the name in the game's arguments (%s)" % a["args"])
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
