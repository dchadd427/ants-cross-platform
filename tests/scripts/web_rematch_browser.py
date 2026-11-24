#!/usr/bin/env python3
"""The rematch of a lobby room in REAL browsers (opt-in; see tests/scripts/test_web_rematch.sh and docs/PLAY_IN_BROWSER.md "After a match").

Needs the web image of this tree served by nginx (tools/web_without_docker.py builds and serves it without Docker), the native `ants_server` of this tree, a Chromium-based browser and Python 3. The
check starts the server (with the options of docker-compose.stack.yml and a start countdown of 0 s, so that the match does not wait half a minute), puts a front door before the site (/ws goes to the
server, everything else to the site), and drives two people, each a browser of their own: Ann makes a room on the front page and Bob joins it through its link; Ann presses START, both pages go to the
game page and the match runs; Bob quits (Ctrl+Q, Yes: with one other side left the rules end the match) and both games show the results.

  * the game page: under the picture, the strip "The match is over" with its words, a chip for each player ("on the results screen" while nobody is back) and the two choices "Back to the room" and "Back to Ants & Uncles"; the strip lies
    between the picture and the Picture row, the picture makes room for it (the picture, the strip and the Picture row are in the window together; the page's text below them scrolls
    as before), and nothing of it is markup;
  * Back to the room: Ann's button takes her tab to /?room=<code>: the front page is in the same room, with the strip "Back from the match." at the top, Ann leads, Bob's color says "Still on the
    results screen…" and the note under START says that it waits for him; Bob's chips follow ("Ann · host · here"); Ann's START is answered by the room ("Bob is not back from the match yet.") and
    nothing starts; Bob's way back is the game's own results screen (Enter: "Back to the room"), which takes his tab to the same room ("Back in the room" on Ann's screen, the note is gone) and Ann's START takes both
    pages to the game again: the rematch runs;
  * a room that Ann made public is public when it comes back, and when Bob is back too (nobody is asked to make it public again);
  * in all of it the key is in no address and no console line.
--shots DIR saves the pictures that the check looks at (r1: the game page when the match is over; r2: the front page after it), to compare with the approved pictures.

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
from web_lobby_chat_browser import Visitor                                      # noqa: E402

ANN_DIGITS, BOB_DIGITS = "100001", "100002"               # (a guest's name is "Guest" and six digits kept by the browser: ants.guest, web/front/player_name.js; nobody types a name any more)
ANN, BOB = "Guest " + ANN_DIGITS, "Guest " + BOB_DIGITS

STRIP = ("(function () { var s = document.getElementById('rematch'); if (!s || s.hidden) return null; var q = function (c) { var n = s.querySelector(c); return n ? n.textContent : null; };"
         " var go = s.querySelector('.rmt-go'); return { title: q('.rmt-title'), text: q('.rmt-text'), button: go && !go.hidden ? q('.rmt-go .btn') : null, note: q('.rmt-note'),"
         " chips: Array.prototype.map.call(s.querySelectorAll('.rmt-chip'), function (c) { return [c.textContent, c.className]; }), markup: s.querySelectorAll('script, img, a').length }; })()")
PLACE = ("(function () { var st = document.getElementById('game-stage').getBoundingClientRect(), s = document.getElementById('rematch').getBoundingClientRect();"
         " return { below: s.top >= st.bottom - 1, fits: st.top >= 0 && s.bottom <= window.innerHeight + 1 }; })()")
BAR = ("(function () { var b = document.getElementById('backbar'); if (!b || b.hidden) return null; var a = b.querySelector('a');"
       " return { lead: b.querySelector('b').textContent, text: b.textContent, link: a ? [a.textContent, a.getAttribute('href'), a.target] : null, first: document.body.firstElementChild === b }; })()")
CARDS = ("Array.prototype.map.call(document.querySelectorAll('#slots > li'), function (li) { var p = li.querySelector('.pstat'); return [li.getAttribute('data-i'), p ? p.textContent : '', p ? p.className : '']; })"
         ".sort(function (a, b) { return a[0] - b[0]; })")
PUBLIC = ("(function () { var b = document.querySelectorAll('.vis .vbtn'); return b.length === 2 ? [b[0].getAttribute('aria-pressed'), b[1].getAttribute('aria-pressed')] : null; })()")      # (the leader's Private | Public switch: which one is pressed)
NOTE = "(function () { var n = document.getElementById('go-status'); return n && /^START waits for /.test(n.textContent) ? n.textContent : ''; })()"       # (the line under START: the room's status line says whose return START waits for)
SEAT_ENDED = "ended while you were away"                    # (the front page's notice for a key that found no seat: web/front/lobby_rules.js, banner seatgone)
BANNER = "(function () { var b = document.getElementById('banner'); return b && !b.hidden ? [document.getElementById('banner-text').textContent, b.className] : null; })()"


def seat_ended(tab):
    banner = tab.ev(BANNER)
    return bool(banner) and SEAT_ENDED in banner[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--web", required=True, help="the site's nginx, e.g. http://127.0.0.1:19980/ (the web image of this tree; it needs no game server)")
    ap.add_argument("--server", default=os.environ.get("ANTS_SERVER_BIN", os.path.join(REPO, "build", "src", "ants_server", "ants_server")), help="the ants_server program of this tree")
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--ready-timeout", type=float, default=180.0, help="seconds that a game page may take to load its game")
    ap.add_argument("--shots", default="", help="a folder to save screenshots in")
    args = ap.parse_args()
    site = args.web if args.web.endswith("/") else args.web + "/"
    failures = []

    def check(ok, what):
        print("  %s: %s" % ("ok  " if ok else "FAIL", what), flush=True)
        if not ok:
            failures.append(what)
        return ok

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

    work = tempfile.mkdtemp(prefix="ants_rematch_check.")
    ws_port = free_port()
    server = Server(args.server, os.path.join(REPO, "Original-Ants", "Maps"), ws_port, work)
    server.options = server.options + ["--start-countdown-seconds", "0"]
    front = None
    people = []
    try:
        server.start()
        site_port = int(site.split("//", 1)[1].split("/")[0].split(":")[1])
        front = Front(site_port, ws_port, site_port)
        web = "http://127.0.0.1:%d/" % front.port
        ann = Visitor(chrome, "Ann", 0, web, args.ready_timeout)
        bob = Visitor(chrome, "Bob", 1, web, args.ready_timeout)
        people.extend([ann, bob])
        for p, digits in ((ann, ANN_DIGITS), (bob, BOB_DIGITS)):
            p.resize(1280, 1025)                                                  # (picture R1's window)
            p.call("Page.addScriptToEvaluateOnNewDocument", {"source": "try { window.localStorage.setItem('ants.guest', %r); } catch (e) { }" % digits})

        # ---- a match of a lobby room, to its end by the rules ----
        if not check(ann.open_front(web, "Ann"), "Ann's front page is up with its room"):
            return 1
        code = ann.room_code()
        if not check(bob.join(web, code, "Bob"), "Bob opens the link and joins"):
            return 1
        ann.ev("document.querySelectorAll('.vis .vbtn')[1].click()")                  # (Ann makes the room public: it stays what she chose, through the match and back)
        check(bool(wait_for(lambda: ann.ev(PUBLIC) == ["false", "true"], 10, 0.25)), "Ann makes the room public (%s)" % ann.ev(PUBLIC))
        ann.ev("document.getElementById('start').click()")
        for p in people:
            check(bool(wait_for(lambda: p.ev("!!window.isReadyToPlay"), args.ready_timeout, 0.5)), "%s's tab goes to the game page and the game is ready" % p.name)
        if not check(bool(wait_for(lambda: ann.in_match() and bob.in_match(), 60, 0.5)), "the match runs on both"):
            return 1
        time.sleep(3)                                                             # (the first turns: the quit must be a command of the running match)

        def press(t, name, code_name, vk, text="", modifiers=0):
            for kind in ("keyDown", "keyUp"):
                t.call("Input.dispatchKeyEvent", {"type": kind, "key": name, "code": code_name, "windowsVirtualKeyCode": vk, "text": text if kind == "keyDown" else "", "modifiers": modifiers})

        bob.ev("document.getElementById('canvas').focus(); 1")
        bob.call("Input.dispatchKeyEvent", {"type": "keyDown", "key": "Control", "code": "ControlLeft", "windowsVirtualKeyCode": 17, "modifiers": 2})
        press(bob, "q", "KeyQ", 81, modifiers=2)
        bob.call("Input.dispatchKeyEvent", {"type": "keyUp", "key": "Control", "code": "ControlLeft", "windowsVirtualKeyCode": 17, "modifiers": 0})
        check(bool(wait_for(lambda: bob.ev("Module._ants_probe(5)") == 1, 5)), "Bob's Ctrl+Q opens the quit dialog")
        press(bob, "y", "KeyY", 89, "y")
        check(bool(wait_for(lambda: ann.ev("Module._ants_match_running()") == 0 and bob.ev("Module._ants_match_running()") == 0, 20, 0.25)), "Yes, with one other side left: the match is over on both")

        # ---- the game page: the strip under the picture ----
        strips = {}
        for p in people:
            strips[p.name] = wait_for(lambda: p.ev(STRIP), 20, 0.25)
            s = strips[p.name] or {}
            check(s.get("title") == "The match is over" and s.get("text") == "Everybody can go back to the room together: same host, same colors, same map. Your seat is kept for you."
                  and s.get("button") == "Back to the room" and s.get("note") == "The same two buttons are on the game’s results screen." and s.get("markup") == 0, "%s's game page shows the strip of picture R1 (%s)" % (p.name, s))
        check(bool(wait_for(lambda: [c[0] for c in (ann.ev(STRIP) or {}).get("chips", [])] == [ANN + " · on the results screen", BOB + " · on the results screen"], 10, 0.25)),
              "nobody is back yet: both chips say \"on the results screen\" (%s)" % (ann.ev(STRIP) or {}).get("chips"))
        place = ann.ev(PLACE) or {}
        check(place == {"below": True, "fits": True}, "the strip lies under the picture, and the picture and the strip are in the window together (%s)" % place)
        ann.shot(args.shots, "r1_game_page_match_over")

        # ---- Ann goes back to the room ----
        ann.ev("document.querySelector('#rematch .rmt-go .btn').click(); 1")
        check(bool(wait_for(lambda: ann.ev("location.pathname + location.search") == "/?room=" + code, 20, 0.25)), "Back to the room takes Ann's tab to the room's address")
        check(bool(wait_for(lambda: ann.ev("!!document.getElementById('code') && document.getElementById('code').textContent.replace(' ', '') === %r && !document.getElementById('start').disabled" % code), 30, 0.25)),
              "the front page is in the same room, and Ann may press START")
        time.sleep(1.0)
        check(not seat_ended(ann), "the room took Ann's seat back with her key: no \"seat ended\" notice (%s)" % ann.ev(BANNER))
        ann.resize(1280, 1400)                                                    # (picture R2's window)
        bar = wait_for(lambda: ann.ev(BAR), 10, 0.25) or {}
        check(bar.get("lead") == "Back from the match." and bar.get("text", "").startswith("Back from the match. " + ANN + " won with ") and bar.get("text", "").endswith(" Same room, same colors, same map.Stats and replay")
              and bar.get("first") is True and (bar.get("link") or [None])[0] == "Stats and replay", "the strip at the top of the front page (picture R2): %s" % bar)
        cards = wait_for(lambda: (lambda c: c if c and c[1][1] == "Still on the results screen…" else None)(ann.ev(CARDS)), 10, 0.25) or []
        check([c[1] for c in cards[:2]] == ["Host. You start the match.", "Still on the results screen…"] and "away" in (cards[1][2] if len(cards) > 1 else ""), "Ann leads; Bob's color says that he is still on the results screen (%s)" % cards)
        check(ann.ev(NOTE) == "START waits for " + BOB + ". You can wait, or press Remove on their color to start without them.", "the note under START (%s)" % ann.ev(NOTE))
        check(bool(wait_for(lambda: [c[0] for c in (bob.ev(STRIP) or {}).get("chips", [])] == [ANN + " · host · here", BOB + " · on the results screen"], 10, 0.25)), "Bob's strip: Ann is the host and here (%s)" % (bob.ev(STRIP) or {}).get("chips"))
        time.sleep(0.5)
        ann.shot(args.shots, "r2_lobby_after_the_match")
        ann.ev("document.getElementById('start').click(); 1")
        banner = wait_for(lambda: ann.ev(BANNER), 10, 0.25) or [None, None]
        check(banner[0] == BOB + " is not back from the match yet. START waits for them: wait, or press Remove on their color to start without them." and "warn" in (banner[1] or ""), "START waits: the room tells Ann why (%s)" % banner)
        check(not ann.ev("document.body.classList.contains('guest')") and ann.ev("location.pathname") == "/", "nothing starts")
        check(ann.ev(PUBLIC) == ["false", "true"], "the room that comes back is public, as Ann made it, with nobody asked to do it again (%s)" % ann.ev(PUBLIC))

        # ---- Bob comes back; the rematch starts ----
        bob.resize(1280, 1025)
        time.sleep(0.5)
        for kind in ("rawKeyDown", "keyUp"):                                        # (the game's own results screen, in the picture, not the strip's button: Enter takes the way that keeps the seat, "Back to the room"; the buttons' places depend on the window)
            bob.call("Input.dispatchKeyEvent", {"type": kind, "key": "Enter", "code": "Enter", "windowsVirtualKeyCode": 13, "nativeVirtualKeyCode": 13})
            time.sleep(0.1)
        check(bool(wait_for(lambda: bob.ev("location.pathname + location.search") == "/?room=" + code and bob.ev("document.body.classList.contains('guest')"), 30, 0.25)), "Bob's Enter on the game's results screen (\"Back to the room\") takes him to the room, as a player (not to Home)")
        time.sleep(1.0)
        check(not seat_ended(bob) and not seat_ended(ann), "Bob's seat was kept for him: no \"seat ended\" notice on either screen (%s, %s)" % (bob.ev(BANNER), ann.ev(BANNER)))
        check(bool(wait_for(lambda: (ann.ev(CARDS) or [[0, ""], [1, ""]])[1][1] == "Back in the room" and ann.ev(NOTE) == "", 10, 0.25)), "Ann's screen: Bob is back in the room, nothing is waited for (%s)" % ann.ev(CARDS))
        check(ann.ev(PUBLIC) == ["false", "true"], "... and it is still public when Bob is back too (%s)" % ann.ev(PUBLIC))
        ann.ev("document.getElementById('start').click(); 1")
        for p in people:
            check(bool(wait_for(lambda: p.ev("location.search").startswith("?join=") and p.ev("!!window.isReadyToPlay"), args.ready_timeout, 0.5)), "START takes %s to the game again" % p.name)
        check(bool(wait_for(lambda: ann.in_match() and bob.in_match(), 60, 0.5)), "the rematch runs on both")

        # ---- the key is in no address and no console line ----
        keys = [k for p in people for k in p.key_hexes()]
        lines = [t for p in people for (_, _, t) in p.console]
        addresses = [p.ev("location.href") or "" for p in people]
        check(all(k not in text for k in keys for text in lines + addresses), "the keys are in no address and no console line")
        return 1 if failures else 0
    except (RuntimeError, TimeoutError, OSError) as e:
        print("  FAIL: the check broke down: %s (server: %s)" % (e, server.tail()), flush=True)
        return 1
    finally:
        for p in people:
            p.close()
        if front is not None:
            front.stop()
        server.stop()
        shutil.rmtree(work, ignore_errors=True)
        if failures:
            print("[web rematch] %d check(s) failed" % len(failures), flush=True)


if __name__ == "__main__":
    sys.exit(main())
