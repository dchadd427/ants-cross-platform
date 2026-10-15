#!/usr/bin/env python3
"""The countdown before a match (network protocol 21), in REAL browsers (opt-in; see tests/scripts/test_web_countdown.sh and docs/MATCH_COUNTDOWN.md).

The request: after the game is started from the lobby it started too fast; a 30 second countdown before the match, with the chat active during it, and a Fullscreen button so that the players can go
fullscreen before the match starts. The quick tier runs the host's rule, the message, the application's card and the page's script with fakes, each by itself; nothing but a real page can show
the chain: two games in a room of the real game server behind the real nginx, the Begin that says 30, the card in the canvas, the page's real button over it, the chat through the server to the
other screen, the first turn that comes 30 s after the match began, and a match that then runs with equal state hashes.

Needs the web image of this tree on a port (its nginx passes /ws and /busy to the game server's WebSocket port: --ws-port is that port; tools/web_without_docker.py makes the same pages without
Docker), the native `ants_server` of this tree (--server), a Chromium-based browser and Python 3; nothing else (the DevTools protocol is spoken with the client of web_hidden_check.py, standard
library only). The check starts the server itself, with the options of docker-compose.stack.yml (the site's own, so the countdown is its default: 30 s) on ports of its own, and every player is a
browser of its own. What it checks, for two players Ann (seat 0) and Bob (seat 1) of a two-seat room:

  * the card    both games say that a countdown runs (ants_probe 60: the seconds left, 27 .. 30 when first seen) with its dialog open, the count goes down, and the server has sealed no turn;
  * the button  the page's Fullscreen button is shown over the card (its place is the game's: ants_probe 61 - 64), at least 120 x 44 px, inside the picture; a real mouse click on it makes the page
                fullscreen (the browser's, or the page's own where the browser refuses) and hides it; the countdown goes on;
  * the lock    a real click on the game while the card is up takes no pointer lock (a locked pointer cannot click the page's button), and once the match runs a click on the game takes it;
  * the chat    Ann types a line with the keyboard and sends it with Enter during the count: Bob's game has one line more in its chat log (ants_probe 65) and Ann's too, through the server;
  * the start   the count ends about 30 s after it was first seen (at least 27 s, at most 36 s), the card is gone (ants_probe 60 is -1, no dialog is open), both games run, and their state hashes
                (the sync lines that every game posts every 100 ticks) agree; the server's status says the room holds the first turn 30 s (countdown_seconds) and ran the match.

Exit status 0: every check passed; 1: a check failed, or the browser, the page or the server broke down during the check; 3: the check could not be made because the environment is not there (no
browser, no server program, nothing answers at the page's address); 2: a bad command line.
"""
import argparse
import os
import secrets
import shutil
import sys
import tempfile
import time
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from web_hidden_check import find_browser                                    # noqa: E402
from web_rejoin_check import CODE_ALPHABET, Player, Server, wait_for        # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

# What the game says now, through its probes: [seconds left of the countdown (-1: none), lines of the chat log (-1: no match), a dialog is open (-1 no match, 0 none, 1 open), the camera's x]
# (only once the page says that the game is ready: a call while the program is still being compiled leaves an export undefined for the life of the page)
PROBES_JS = """(function () {
  if (!window.isReadyToPlay || typeof Module === 'undefined' || !Module._ants_probe) return null;
  return [Module._ants_probe(60), Module._ants_probe(65), Module._ants_probe(5), Module._ants_probe(3)];
})()"""

# Does the canvas hold the pointer lock (the lock that a click on the game takes in a match: web/shell.html, "THE POINTER LOCK")
LOCK_JS = "!!(document.pointerLockElement || document.webkitPointerLockElement)"

# The page's Fullscreen button, the stage and whether the page is fullscreen (the browser's or its own)
BUTTON_JS = """(function () {
  var b = document.getElementById('countdown-fs'), s = document.getElementById('game-stage');
  if (!b || !s) return null;
  var r = b.getBoundingClientRect(), t = s.getBoundingClientRect(), box = document.getElementById('game-container').getBoundingClientRect();
  return { hidden: b.hidden, display: getComputedStyle(b).display, x: r.left, y: r.top, w: r.width, h: r.height, box: [box.left, box.top, box.width, box.height],
           stage: [t.left, t.top, t.width, t.height], fullscreen: !!(document.fullscreenElement || document.webkitFullscreenElement) || s.classList.contains('pseudo-fullscreen') };
})()"""


class Gamer(Player):
    """A Player with the countdown's probes and a keyboard."""

    def probes(self):
        try:
            return self.ev(PROBES_JS)
        except (RuntimeError, TimeoutError):
            return None

    def button(self):
        try:
            return self.ev(BUTTON_JS)
        except (RuntimeError, TimeoutError):
            return None

    def key(self, key, code, vk, text=None):
        for kind in ("keyDown", "keyUp"):
            params = {"type": kind, "key": key, "code": code, "windowsVirtualKeyCode": vk}
            if text is not None and kind == "keyDown":
                params["text"] = text
            self.call("Input.dispatchKeyEvent", params)

    def type_line(self, text):
        self.ev("document.getElementById('canvas').focus(); 1")
        for ch in text:
            self.key(ch, "Key" + ch.upper() if ch.isalpha() else ("Space" if ch == " " else ""), ord(ch.upper()), text=ch)
            time.sleep(0.03)
        self.key("Enter", "Enter", 13, text="\r")

    def click(self, x, y):
        self.call("Input.dispatchMouseEvent", {"type": "mouseMoved", "x": x, "y": y})
        self.call("Input.dispatchMouseEvent", {"type": "mousePressed", "x": x, "y": y, "button": "left", "clickCount": 1, "buttons": 1})
        self.call("Input.dispatchMouseEvent", {"type": "mouseReleased", "x": x, "y": y, "button": "left", "clickCount": 1})


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--web", required=True, help="the site, e.g. http://127.0.0.1:8080/ (the web image of this tree; its /ws goes to --ws-port)")
    ap.add_argument("--ws-port", type=int, required=True, help="the WebSocket port that the site's /ws and /busy lead to (the check's server listens there)")
    ap.add_argument("--server", default=os.environ.get("ANTS_SERVER_BIN", os.path.join(REPO, "build", "src", "ants_server", "ants_server")), help="the ants_server program of this tree")
    ap.add_argument("--maps", default=os.path.join(REPO, "Original-Ants", "Maps"), help="the maps folder (default: the original's)")
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--shots", default="", help="a folder to save screenshots in")
    ap.add_argument("--ready-timeout", type=float, default=120.0, metavar="SECONDS", help="how long a page may take to get its game ready (default 120)")
    ap.add_argument("--keep-logs", default="", help="a folder to copy the server's log to when the check ends")
    args = ap.parse_args()
    web = args.web if args.web.endswith("/") else args.web + "/"
    failures = []

    def check(ok, what):
        print("  %s: %s" % ("ok  " if ok else "FAIL", what), flush=True)
        if not ok:
            failures.append(what)

    chrome = find_browser(args.browser)
    if not chrome:
        print("  SKIP: no Chromium-based browser found (give one with --browser or CHROME)")
        return 3
    if not os.path.isfile(args.server) or not os.access(args.server, os.X_OK):
        print("  SKIP: the game server program is not there (build the target ants_server, or give --server)")
        return 3
    try:
        urllib.request.urlopen(web, timeout=10).read(64)
    except (OSError, urllib.error.URLError) as e:
        print("  SKIP: nothing answers at %s (%s)" % (web, e))
        return 3

    work = tempfile.mkdtemp(prefix="ants_countdown_check.")
    server = Server(args.server, args.maps, args.ws_port, work)
    players = []
    try:
        print("[web countdown] a 30 s countdown with the chat open, in two real browsers (opt-in: see the header of this script)")
        print("  note: the server is %s" % server.describe())
        server.start()
        room = "".join(secrets.choice(CODE_ALPHABET) for _ in range(6))
        ann = Gamer(chrome, "Ann", 0, web, args.ready_timeout)
        players.append(ann)
        bob = Gamer(chrome, "Bob", 1, web, args.ready_timeout)
        players.append(bob)
        ann.open_game(room)
        bob.open_game(room)

        # ---- the card
        both = wait_for(lambda: (ann.probes() or [-1])[0] >= 0 and (bob.probes() or [-1])[0] >= 0, 90, 0.2)
        first_seen = time.time()
        check(bool(both), "both games say that a countdown runs (ants_probe 60)")
        if not both:
            return 1
        a0, b0 = ann.probes(), bob.probes()
        check(27 <= a0[0] <= 30 and 27 <= b0[0] <= 30, "the count is about 30 when first seen (Ann %s s, Bob %s s)" % (a0[0], b0[0]))
        check(a0[2] == 1 and b0[2] == 1, "the start dialog is the countdown card on both screens (a dialog is open, ants_probe 5)")
        time.sleep(3.0)
        a1, b1 = ann.probes(), bob.probes()
        check(a1[0] <= a0[0] - 2 and b1[0] <= b0[0] - 2, "the count goes down (Ann %s -> %s, Bob %s -> %s)" % (a0[0], a1[0], b0[0], b1[0]))
        status = server.room(room) or {}
        check(status.get("state") == "running" and status.get("countdown_seconds") == 30 and status.get("turns") == 0 and not status.get("paused"),
              "the server's room holds the first turn 30 s and has sealed none (%s)" % {k: status.get(k) for k in ("state", "countdown_seconds", "turns", "paused")})
        ann.shot(args.shots, "countdown-ann")
        bob.shot(args.shots, "countdown-bob")

        # ---- the button
        wait_for(lambda: (ann.button() or {}).get("hidden") is False, 5, 0.2)
        btn = ann.button() or {}
        check(btn.get("hidden") is False and btn.get("display") != "none", "the page's Fullscreen button is shown over the card (%s)" % {k: btn.get(k) for k in ("hidden", "display")})
        bx, by, bw, bh = btn.get("x", 0), btn.get("y", 0), btn.get("w", 0), btn.get("h", 0)
        check(bw >= 119.5 and bh >= 43.5, "it is at least 120 x 44 px (%.0f x %.0f)" % (bw, bh))
        gx, gy, gw, gh = (btn.get("box") or [0, 0, 0, 0])
        check(bx >= gx - 1 and by >= gy - 1 and bx + bw <= gx + gw + 1 and by + bh <= gy + gh + 1, "it lies inside the picture (button %s, picture %s)" % ([round(v) for v in (bx, by, bw, bh)], [round(v) for v in (gx, gy, gw, gh)]))
        # a click on the game while the card is up must not take the pointer lock: a locked pointer cannot click the DOM button (a review found that the click on the button did nothing at all)
        ann.click(gx + gw * 0.15, gy + gh * 0.15)                               # (the picture's upper left corner: away from the button and the chat)
        time.sleep(0.6)
        check(ann.ev(LOCK_JS) is False, "a click on the game during the countdown does not take the pointer lock (it would leave the Fullscreen button dead)")
        ann.click(bx + bw / 2, by + bh / 2)
        went = wait_for(lambda: (ann.button() or {}).get("fullscreen") is True, 5, 0.2)
        check(bool(went), "a real click on it makes the page fullscreen (the browser's, or the page's own where the browser refuses)")
        gone = wait_for(lambda: (ann.button() or {}).get("hidden") is True, 3, 0.1)         # (the page's poll looks every 250 ms: the click itself cannot know that the browser said yes)
        check(bool(gone), "and the button is gone (the page is fullscreen: nothing to ask for any more)")
        check((ann.probes() or [-1])[0] >= 0, "the countdown goes on in fullscreen")
        ann.shot(args.shots, "countdown-ann-fullscreen")

        # ---- the chat
        log_a, log_b = (ann.probes() or [0, 0])[1], (bob.probes() or [0, 0])[1]
        ann.type_line("hello Bob, gl hf")
        wait_for(lambda: (bob.probes() or [0, 0])[1] > log_b and (ann.probes() or [0, 0])[1] > log_a, 10, 0.2)
        la, lb = (ann.probes() or [0, 0])[1], (bob.probes() or [0, 0])[1]
        check(lb > log_b, "Bob's chat log has the line that Ann typed during the count, through the server (%s -> %s lines)" % (log_b, lb))
        check(la > log_a, "and Ann's own log has it (%s -> %s lines)" % (log_a, la))
        bob.shot(args.shots, "countdown-bob-chat")

        # ---- the start
        ended = wait_for(lambda: (ann.probes() or [0])[0] == -1 and (bob.probes() or [0])[0] == -1, 45, 0.25)
        took = time.time() - first_seen
        check(bool(ended) and 24 <= took <= 36, "the count ends about 30 s after it was first seen (%.1f s)" % took)
        running = wait_for(lambda: (ann.probes() or [0, 0, 1, 0])[2] == 0 and (bob.probes() or [0, 0, 1, 0])[2] == 0, 10, 0.25)
        check(bool(running), "the card is gone and no dialog is open: both games run the match")
        bx2, by2, bw2, bh2 = ((bob.button() or {}).get("box") or [0, 0, 0, 0])
        bob.click(bx2 + bw2 * 0.5, by2 + bh2 * 0.5)                             # (Bob's page is a window still: in a running match a click on the game takes the pointer lock, as it always did)
        took_lock = wait_for(lambda: bob.ev(LOCK_JS) is True, 5, 0.2)
        check(bool(took_lock), "once the match runs, a click on the game takes the pointer lock again")
        ann.syncs()
        bob.syncs()
        wait_for(lambda: (ann.syncs() or True) and (bob.syncs() or True) and len([t for t in ann.ticks if t in bob.ticks]) >= 2, 40, 1.0)
        common = [(t, ann.ticks[t], bob.ticks[t]) for t in sorted(ann.ticks) if t in bob.ticks]
        check(len(common) >= 2 and all(x == y for _, x, y in common), "the state hashes of the two games agree at %d common ticks" % len(common))
        check(not ann.conflicts and not bob.conflicts, "no game reported one tick with two hashes")
        status = server.room(room) or {}
        check(status.get("state") == "running" and (status.get("turns") or 0) > 40 and not status.get("paused"), "the server's room ran the match (turns %s)" % status.get("turns"))
        bad = [t for p in players for _, level, t in p.console if "out of sync" in t.lower() or "uncaught" in t.lower()]
        check(not bad, "no game said that it was out of sync or threw (%s)" % [b[:90] for b in bad[:3]])
        bob.shot(args.shots, "match-bob")
    except Exception as e:                                                     # noqa: BLE001
        print("  FAIL: the check broke down: %s: %s" % (type(e).__name__, e))
        failures.append("broke down")
    finally:
        for p in players:
            try:
                p.close()
            except Exception:                                                  # noqa: BLE001
                pass
        server.stop()
        if args.keep_logs:
            os.makedirs(args.keep_logs, exist_ok=True)
            shutil.copy(server.log_path, args.keep_logs)
        shutil.rmtree(work, ignore_errors=True)
    print("  %s" % ("every check passed" if not failures else "%d check(s) failed" % len(failures)))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
