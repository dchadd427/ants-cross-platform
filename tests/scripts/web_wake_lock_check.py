#!/usr/bin/env python3
"""The screen that stays on while a recording plays, in a REAL browser (opt-in; see tests/scripts/test_web_wake_lock.sh, docs/PLAY_IN_BROWSER.md "The screen stays on" and web/wake_lock.js).

No Docker image, no game server, no game program: the check serves the pages of web/ itself, with the fake door and the stand-in for the game of tests/scripts/web_live_check.py (Module._ants_replay_get /
_ants_replay_do: the state of a recording, play and pause), so what it shows is the page's own part: that the REAL page calls the REAL web/wake_lock.js, that Chromium's REAL navigator.wakeLock is asked
for while the recording plays, and that the lock is let go on pause and at the end. The calls of navigator.wakeLock.request are noted by a stand-in that runs before the page (it still makes the real call).
It does not show a match that is played (that needs the real game: the node check tests/scripts/web_wake_lock_check.js runs the page's call with the game's answers), nor what a phone's screen does
(a browser on a computer holds no screen). The DevTools protocol is spoken with the client of web_hidden_check.py (standard library only); the browser has its own profile and port.
Parts (--only): replay (a recording that plays asks for the lock once, pause lets it go, play asks again, the end lets it go, "Watch again" asks again), live (a match that is watched asks while it
plays), hidden (the tab is put behind another: Chromium drops the lock, nothing is asked while it is hidden, the page asks again when it is shown, and not when the recording was paused meanwhile),
refused (the browser refuses the lock: the page plays on and says nothing), none (a browser with no wake lock: the page plays on, nothing is asked).

Exit status 0: every check passed; 1: a check failed; 3: the check could not be made (no browser); 2 is the status of a bad command line.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import web_live_check as live                                              # noqa: E402  (the fake door, the stand-in for the game and the waiting)
from web_aspect_check import Browser, NotReachable, Tab                    # noqa: E402
from web_hidden_check import find_browser                                  # noqa: E402

SPY = """
window.__wake = { calls: [], sentinels: [], refused: [], released: 0, errors: [], unhandled: 0, all: [] };
window.addEventListener('error', function (e) { var m = String(e.message) + ' @' + String(e.filename).split('/').pop() + ':' + e.lineno; window.__wake.all.push(m); if (/wake/i.test(m)) window.__wake.errors.push(m); });
window.addEventListener('unhandledrejection', function (e) { var m = String(e.reason && (e.reason.name + ' ' + e.reason.message)); window.__wake.all.push('unhandled ' + m); if (/NotAllowed|InvalidState|wake|refused by the check/i.test(m)) window.__wake.unhandled++; });
if (navigator.wakeLock) {
  var real = navigator.wakeLock.request.bind(navigator.wakeLock), mode = %s;
  navigator.wakeLock.request = function (type) {
    window.__wake.calls.push(type);
    var p = mode === 'refuse' ? Promise.reject(new DOMException('refused by the check', 'NotAllowedError')) : real(type);
    p.then(function (s) { window.__wake.sentinels.push(s); s.addEventListener('release', function () { window.__wake.released++; }); },
           function (e) { window.__wake.refused.push(String(e && e.name)); });
    return p;
  };
}
1"""
NONE = "try { Object.defineProperty(Navigator.prototype, 'wakeLock', { get: function () { return undefined; }, configurable: true }); } catch (e) {}\n1;\n"

HELD = "__wake.sentinels.length > 0 && !__wake.sentinels[__wake.sentinels.length - 1].released"
LAST_RELEASED = "__wake.sentinels.length > 0 && __wake.sentinels[__wake.sentinels.length - 1].released"


class WakeDoor(live.Door):
    """The fake door of web_live_check.py, and the scripts and styles of web/ that it does not serve (the game page needs the bar's script to get as far as the call that is checked here)."""

    def route(self, h, path):
        name = path[1:]
        full = os.path.join(live.WEB, name)
        if name and "/" not in name and name != "index.js" and name.endswith((".js", ".css")) and os.path.isfile(full):
            with open(full, "rb") as f:
                return self.send(h, 200, f.read(), "text/javascript" if name.endswith(".js") else "text/css")
        return super().route(h, path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--only", default="", help="one part: replay, live, hidden, refused or none (default: all)")
    args = ap.parse_args()
    parts = ("replay", "live", "hidden", "refused", "none")
    if args.only and args.only not in parts:
        print("--only is one of: " + ", ".join(parts))
        return 2
    path = find_browser(args.browser)
    if not path:
        print("  SKIP: no Chromium-based browser found (give one with --browser or CHROME)")
        return 3
    failures = []
    count = [0]

    def check(ok, what):
        count[0] += 1
        print("  %s: %s" % ("ok  " if ok else "FAIL", what))
        if not ok:
            failures.append(what)

    door = WakeDoor()
    try:
        browser = Browser(path)
    except NotReachable as e:
        print("  SKIP: " + str(e))
        return 3
    print("[web wake lock] the pages of web/ against a fake door and a stand-in for the game, in %s" % os.path.basename(path))
    tabs = []

    def new_tab(spy):
        tab = Tab(browser)
        tab.emulate(1440, 900, 1)
        tab.call("Page.addScriptToEvaluateOnNewDocument", {"source": spy})
        tabs.append(tab)
        return tab

    def open_page(tab, address):
        tab.call("Page.navigate", {"url": door.url(address)})

    def fresh(**kw):
        state = dict(mode="ok", freeze=True, live=[live.ID, live.OTHER], live_door=True, kept_at=1.0, kept_listed=True, names=live.PLAYERS, seconds=676)
        state.update(kw)
        door.set(**state)

    def calls(tab):
        return tab.ev("__wake.calls.length")

    try:
        if args.only in ("", "replay"):
            print("[web wake lock] part replay: a recording")
            fresh()
            tab = new_tab(SPY % "'real'")
            open_page(tab, "/play?replay=" + live.KEPT)
            check(live.until(tab, live.READY) is True, "a recording starts and plays")
            check(live.until(tab, "__wake.calls.length >= 1", 5) is True, "the page asks for the lock while the recording plays")
            time.sleep(1.5)
            check(tab.ev("__wake.calls") == ["screen"], "it asks for the screen lock, once (calls: %r)" % tab.ev("__wake.calls"))
            granted = live.until(tab, "__wake.sentinels.length + __wake.refused.length >= 1", 5)
            refused = tab.ev("__wake.refused")
            check(granted is True and not refused, "the browser gave the lock (%s)" % ("refused: %r" % refused if refused else "a sentinel is held"))
            check(tab.ev(HELD) is True, "the lock is held while the recording plays")
            tab.ev("document.getElementById('b-play').click()")
            check(live.until(tab, "__stub.state === 3", 3) is True, "Pause pauses the game")
            check(live.until(tab, LAST_RELEASED, 3) is True and tab.ev("__wake.released") == 1, "and the lock is let go when it is paused")
            time.sleep(1.5)
            check(calls(tab) == 1, "nothing is asked for while it stays paused (calls: %d)" % calls(tab))
            tab.ev("document.getElementById('b-play').click()")
            check(live.until(tab, "__stub.state === 2", 3) is True and live.until(tab, "__wake.calls.length === 2", 3) is True, "Play asks again")
            check(live.until(tab, HELD, 3) is True, "and the second lock is held")
            tab.ev("__stub.state = 5")
            check(live.until(tab, LAST_RELEASED, 3) is True and tab.ev("__wake.released") == 2, "the end of the match lets it go")
            tab.ev("document.getElementById('b-play').click()")
            check(live.until(tab, "__stub.state === 2 && __wake.calls.length === 3", 4) is True and live.until(tab, HELD, 3) is True, "\"Watch again\" asks again")
            check(tab.ev("__wake.errors") == [] and tab.ev("__wake.unhandled") == 0, "no error and no unhandled rejection on the page")
            tab.close()

        if args.only in ("", "live"):
            print("[web wake lock] part live: a match that is watched")
            fresh()
            tab = new_tab(SPY % "'real'")
            open_page(tab, "/play?live=" + live.ID)
            check(live.until(tab, live.READY) is True, "a live match starts")
            check(live.until(tab, "__wake.calls.length === 1", 5) is True and live.until(tab, HELD, 3) is True, "the page asks for the lock while the live match plays, and holds it")
            tab.ev("document.getElementById('b-play').click()")
            check(live.until(tab, LAST_RELEASED, 3) is True, "a live match that the viewer pauses lets it go")
            tab.close()

        if args.only in ("", "hidden"):
            print("[web wake lock] part hidden: the tab goes behind another and comes back")
            fresh()
            tab = new_tab(SPY % "'real'")
            open_page(tab, "/play?replay=" + live.KEPT)
            check(live.until(tab, live.READY) is True, "a recording starts and plays")
            check(live.until(tab, "__wake.calls.length === 1", 5) is True and live.until(tab, HELD, 3) is True, "the lock is held while it plays")
            other = new_tab("1")
            other.call("Page.navigate", {"url": "about:blank"})

            def hide():
                other.dt.call("Target.activateTarget", {"targetId": other.target})
                tab.call("Emulation.setFocusEmulationEnabled", {"enabled": False})

            def show():
                tab.dt.call("Target.activateTarget", {"targetId": tab.target})
                tab.call("Emulation.setFocusEmulationEnabled", {"enabled": True})

            hide()
            check(live.until(tab, "document.visibilityState === 'hidden'", 5) is True, "the tab is hidden")
            check(live.until(tab, "__wake.released >= 1", 5) is True, "the browser dropped the lock when the tab was hidden")
            time.sleep(2.0)
            check(calls(tab) == 1, "nothing is asked for while the tab is hidden (calls: %d)" % calls(tab))
            show()
            check(live.until(tab, "document.visibilityState === 'visible'", 5) is True, "the tab is shown again")
            check(live.until(tab, "__wake.calls.length === 2", 5) is True and live.until(tab, HELD, 3) is True, "the page asks again at once, and the new lock is held")
            hide()
            live.until(tab, "document.visibilityState === 'hidden'", 5)
            tab.ev("__stub.state = 3")
            time.sleep(1.0)
            show()
            live.until(tab, "document.visibilityState === 'visible'", 5)
            time.sleep(2.0)
            check(calls(tab) == 2 and tab.ev(LAST_RELEASED) is True, "a recording that was paused while the tab was hidden is not asked for when it is shown (calls: %d)" % calls(tab))
            other.close()
            tab.close()

        if args.only in ("", "refused"):
            print("[web wake lock] part refused: the browser says no")
            fresh()
            tab = new_tab(SPY % "'refuse'")
            open_page(tab, "/play?replay=" + live.KEPT)
            check(live.until(tab, live.READY) is True, "a recording starts")
            check(live.until(tab, "__wake.refused.length >= 1", 5) is True, "the lock was asked for and refused")
            time.sleep(2.5)
            check(calls(tab) == 1, "and is not asked for again at once (calls: %d)" % calls(tab))
            tab.ev("document.getElementById('b-play').click()")
            check(live.until(tab, "__stub.state === 3", 3) is True, "the page still works: Pause pauses")
            check(tab.ev("__wake.errors") == [] and tab.ev("__wake.unhandled") == 0, "the refusal shows nowhere: no error, no unhandled rejection")
            check(tab.ev("document.getElementById('toast').hidden") is True, "and no message")
            tab.close()

        if args.only in ("", "none"):
            print("[web wake lock] part none: a browser with no wake lock")
            fresh()
            tab = new_tab(NONE + SPY % "'real'")
            open_page(tab, "/play?replay=" + live.KEPT)
            check(live.until(tab, live.READY) is True, "a recording starts")
            time.sleep(1.5)
            check(tab.ev("typeof navigator.wakeLock") == "undefined" and calls(tab) == 0, "the browser has no wake lock and nothing is asked")
            tab.ev("document.getElementById('b-play').click()")
            check(live.until(tab, "__stub.state === 3", 3) is True and tab.ev("__wake.errors") == [], "the page works as ever, with no error")
            tab.close()
    except (RuntimeError, TimeoutError, ConnectionError) as e:
        check(False, "the browser or the page broke down: %s" % e)
    finally:
        for t in tabs:
            try:
                t.close()
            except (RuntimeError, TimeoutError):
                pass
        browser.close()
    print("[web wake lock] %d checks, %d failed" % (count[0], len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
