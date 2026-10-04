#!/usr/bin/env python3
"""The prediction of one's own orders in a REAL browser (opt-in; see tests/scripts/test_web_prediction.sh and docs/NETWORK_PORT.md, "Prediction of one's own orders").

Needs a running stack (the web page and the game server with demo rooms: docker-compose.stack.yml), a Chromium-based browser and Python 3; nothing else (the DevTools protocol is
spoken with the helpers of tests/scripts/web_hidden_check.py). It starts the browser with a throwaway profile on a free port (headless; nothing of yours is touched) and plays three
matches of a demo room for two players, in two WINDOWS (both are shown, so both draw frames), the first window's player giving its orders with the mouse (a rubber band over the hill,
then a right click on open ground: the game's own HUD and the page's real input path), the second window's player now and then too, so that the first one has other players' orders to
correct for:

  * the first match with ?prediction=on on the first window's address: the prediction is ON (the game says so through its `ants_probe`, src/ants_app/application.cpp), it predicted the
    orders that were given, the corner's "delay" (what the click FEELS: the frame that took it to the frame that shows it) is under 60 ms while the network's delay (the confirmed
    engine's, which the picture no longer waits for) is what it always was and larger (the second window says nothing and does not predict: the two games must stay identical);
  * the second match with nothing said (the default is OFF) and the third with ?prediction=off: the prediction is OFF, nothing was predicted, and the corner's delay is the network's;
  * in all three: the room is still running with both players at the end, and the state hashes that the two games report (the page posts one every 100 ticks on the broadcast channel
    "ants-sync") are equal tick for tick, between a window that predicts and a window that does not (the confirmed matches are identical), and the game's own frame function stays
    cheap (a mean under 8 ms, a frame is 16.7 ms).

What it cannot say: the felt delay at a round trip that the stack does not have (this is the stack's own latency, a few milliseconds on one machine; the measurements at 60 and 200 ms
are in docs/audit/rollback_notes.md), what a real display adds, or anything about Safari and Firefox.

Exit status 0: every check passed; 1: a check failed; 3: the check could not be made (no browser, the page did not come up, the stack does not answer); 2 is a bad command line.
"""
import argparse
import os
import random
import shutil
import signal
import statistics
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import web_hidden_check as hidden  # noqa: E402  (the DevTools helpers, the hook that records the games' state hashes, the control interface)

WINDOW = (1000, 640)


class Seat:
    """A window with the game page, and what the game believes (ants_probe)."""

    def __init__(self, devtools, url):
        self.devtools = devtools
        self.target = devtools.call("Target.createTarget", {"url": "about:blank", "newWindow": True, "width": WINDOW[0], "height": WINDOW[1]})["targetId"]
        self.session = devtools.call("Target.attachToTarget", {"targetId": self.target, "flatten": True})["sessionId"]
        devtools.call("Page.enable", session=self.session)
        devtools.call("Runtime.enable", session=self.session)
        devtools.call("Emulation.setFocusEmulationEnabled", {"enabled": True}, session=self.session)
        devtools.call("Page.addScriptToEvaluateOnNewDocument", {"source": hidden.HOOK}, session=self.session)
        devtools.call("Page.navigate", {"url": url}, session=self.session)
        self.geometry = None

    def probe(self, what):
        return self.devtools.evaluate(self.session, "(function(){try{return Module._ants_probe(%d);}catch(e){return -99;}})()" % what)

    def ev(self, expression):
        return self.devtools.evaluate(self.session, expression)

    def canvas(self):
        left, top, width, height = self.ev("(function(){var b=document.getElementById('canvas').getBoundingClientRect();return [b.left,b.top,b.width,b.height];})()")
        self.geometry = (left, top, width, height)

    def mouse(self, kind, gx, gy, button="none", clicks=0):
        left, top, width, height = self.geometry
        self.devtools.call("Input.dispatchMouseEvent", {"type": kind, "x": left + gx * width / 960.0, "y": top + gy * height / 540.0, "button": button,
                                                        "buttons": {"none": 0, "left": 1, "right": 2}[button], "clickCount": clicks}, session=self.session)

    def select_all(self):
        """A rubber band over the map view: every ant of the player that stands there."""
        self.mouse("mouseMoved", 40, 60)
        self.mouse("mousePressed", 40, 60, "left", 1)
        for k in range(1, 9):
            self.mouse("mouseMoved", 40 + k * 90, 60 + k * 55, "left", 0)
        self.mouse("mouseReleased", 760, 500, "left", 1)

    def order(self, gx, gy):
        """A right click on open ground: the group's move order (the original's, executed at the release)."""
        self.mouse("mouseMoved", gx, gy)
        self.mouse("mousePressed", gx, gy, "right", 1)
        self.mouse("mouseReleased", gx, gy, "right", 1)

    def close(self):
        self.devtools.close_tab(self.target)


def play(devtools, web, ctl, secret, mode, orders, check, label):
    """One match of two windows; returns what the first window's game reported."""
    code = "demo-small-2p-pred%s%d" % (mode, random.randint(10000, 99999))
    a = Seat(devtools, web + "?join=/ws&room=%s&seat=0&name=Alice" % code + {"on": "&prediction=on", "off": "&prediction=off", "default": ""}[mode])
    b = Seat(devtools, web + "?join=/ws&room=%s&seat=1&name=Bob" % code)
    try:
        deadline = time.time() + 150                                # the game data is about 15 MB: a cold start takes a while
        ready = False
        while time.time() < deadline:
            try:
                room = hidden.control_room(ctl, secret, code)
                if room and room.get("state") == "running" and room.get("ticks", 0) > 20 and a.probe(3) >= 0 and b.probe(3) >= 0 and a.probe(5) == 0 and b.probe(5) == 0:
                    ready = True
                    break
            except (RuntimeError, TimeoutError, OSError, ValueError):
                pass
            time.sleep(1.0)
        if not ready:
            print("  SKIP: the match did not start in time (the page or the server is not up)")
            return None
        time.sleep(2.0)
        a.canvas()
        b.canvas()
        a.select_all()
        b.select_all()
        time.sleep(0.5)
        rng = random.Random(11)
        corner, network = [], []
        a.probe(15)                                                   # (the frames' work is counted from here)
        for i in range(orders):
            a.order(rng.randint(80, 700), rng.randint(90, 480))
            if i % 2 == 1:
                b.order(rng.randint(80, 700), rng.randint(90, 480))
            time.sleep(1.4)
            corner.append(a.probe(7))
            network.append(a.probe(8))
        reading = {
            "state": a.probe(9), "predicted": a.probe(10), "rebuilds": a.probe(11),
            "frame_us": a.probe(12), "frame_max_us": a.probe(13), "frames": a.probe(14),
            "corner": [v for v in corner if v >= 0], "network": [v for v in network if v >= 0],
        }
        # the room is still running with both players, and the two games agree on every state hash they both reported
        time.sleep(6.0)                                               # (a report comes every 100 ticks: five seconds)
        room = hidden.control_room(ctl, secret, code)
        check(bool(room) and room.get("state") == "running" and room.get("joined", 0) == 2,
              "%s: the room is still running with both players (%s, %s joined, %s ticks)" % (label, (room or {}).get("state"), (room or {}).get("joined"), (room or {}).get("ticks")))
        seen = {}
        for seat in (a, b):
            for report in seat.ev("window.__sync") or []:
                seen.setdefault(report["tick"], {})[report["seat"]] = report["hash"]
        both = [tick for tick, by_seat in seen.items() if len(by_seat) == 2]
        check(len(both) >= 3, "%s: the hash reports of both seats were compared at %d ticks" % (label, len(both)))
        wrong = [tick for tick in both if seen[tick][0] != seen[tick][1]]
        check(not wrong, "%s: every state hash of the two games is equal (%d mismatches%s)" % (label, len(wrong), (", first at tick %d" % min(wrong)) if wrong else ""))
        return reading
    finally:
        a.close()
        b.close()


def median(values):
    return statistics.median(values) if values else None


def main():
    signal.signal(signal.SIGTERM, lambda signum, frame: sys.exit(143))            # (a stopped run still closes the browser and removes the profile: `finally` runs)
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--web", required=True, help="the web page of the stack, e.g. http://127.0.0.1:19980/")
    ap.add_argument("--ctl", required=True, help="the control interface of the game server, e.g. http://127.0.0.1:4010")
    ap.add_argument("--secret", required=True, help="the bearer secret of the control interface (docker exec ants-server cat /results/control-secret)")
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--orders", type=int, default=12, help="how many orders the first window gives in a match (default 12)")
    args = ap.parse_args()

    failures = []
    checks = [0]

    def check(ok, what):
        checks[0] += 1
        print("  %s: %s" % ("ok  " if ok else "FAIL", what))
        if not ok:
            failures.append(what)

    browser = hidden.find_browser(args.browser)
    if not browser:
        print("  SKIP: no Chromium-based browser found (give one with --browser or CHROME)")
        return 3
    web = args.web if args.web.endswith("/") else args.web + "/"
    profile = tempfile.mkdtemp(prefix="ants_prediction_profile.")
    port = hidden.free_port()
    process = subprocess.Popen(
        [browser, "--headless=new", "--remote-debugging-port=%d" % port, "--user-data-dir=" + profile, "--no-first-run", "--no-default-browser-check", "--disable-extensions",
         "--autoplay-policy=no-user-gesture-required", "--window-size=%d,%d" % WINDOW, "--disable-background-timer-throttling", "--disable-renderer-backgrounding",
         "--disable-backgrounding-occluded-windows", "about:blank"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    devtools = None
    try:
        for _ in range(100):
            try:
                devtools = hidden.DevTools(port)
                break
            except (OSError, ValueError, ConnectionError):
                time.sleep(0.2)
        if devtools is None:
            print("  SKIP: the browser did not start")
            return 3
        print("[web prediction] three matches of a demo room for two, two windows each, in %s: ?prediction=on, the default (off), ?prediction=off" % os.path.basename(browser))
        on = play(devtools, web, args.ctl, args.secret, "on", args.orders, check, "?prediction=on")
        if on is None:
            return 3
        default = play(devtools, web, args.ctl, args.secret, "default", args.orders, check, "the default")
        if default is None:
            return 3
        off = play(devtools, web, args.ctl, args.secret, "off", args.orders, check, "?prediction=off")
        if off is None:
            return 3

        felt_on, net_on = median(on["corner"]), median(on["network"])
        check(on["state"] == 1, "?prediction=on: the game says its prediction is on (%d)" % on["state"])
        check(on["predicted"] >= args.orders * 2 // 3, "?prediction=on: the orders were predicted (%d of %d given)" % (on["predicted"], args.orders))
        check(felt_on is not None and felt_on <= 60, "?prediction=on: the corner's delay is what the click feels, under 60 ms (median %s ms of %s)" % (felt_on, on["corner"]))
        check(net_on is not None and net_on >= 40, "?prediction=on: the network's delay is still measured and is larger (median %s ms of %s)" % (net_on, on["network"]))
        check(felt_on is not None and net_on is not None and felt_on + 20 <= net_on, "?prediction=on: the click is felt sooner than the confirmed engine applies it (%s ms against %s ms)" % (felt_on, net_on))
        check(0 < on["frame_us"] <= 8000, "?prediction=on: the game's frame function costs %d us on average (%d frames, the longest %d us)" % (on["frame_us"], on["frames"], on["frame_max_us"]))
        for label, reading in (("the default", default), ("?prediction=off", off)):
            corner_off, net_off = median(reading["corner"]), median(reading["network"])
            check(reading["state"] == 0, "%s: the game says its prediction is off (%d)" % (label, reading["state"]))
            check(reading["predicted"] == 0, "%s: nothing was predicted (%d)" % (label, reading["predicted"]))
            check(corner_off is not None and net_off is not None and abs(corner_off - net_off) <= 20 and net_off >= 40,
                  "%s: the corner's delay is the network's (%s ms against %s ms)" % (label, corner_off, net_off))
            check(0 < reading["frame_us"] <= 8000, "%s: the game's frame function costs %d us on average (%d frames)" % (label, reading["frame_us"], reading["frames"]))
        print("[web prediction] %d checks, %d failed" % (checks[0], len(failures)))
        return 1 if failures else 0
    finally:
        if devtools is not None:
            try:
                devtools.call("Browser.close", timeout=5)
            except Exception:  # noqa: BLE001
                pass
        try:
            process.terminate()
            process.wait(timeout=10)
        except Exception:  # noqa: BLE001
            try:
                process.kill()
            except Exception:  # noqa: BLE001
                pass
        shutil.rmtree(profile, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
