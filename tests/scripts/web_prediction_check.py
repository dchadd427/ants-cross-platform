#!/usr/bin/env python3
"""The prediction of one's own orders in a REAL browser (opt-in; see tests/scripts/test_web_prediction.sh and docs/NETWORK_PORT.md, "Prediction of one's own orders").

Needs a running stack (the web page and the game server with demo rooms: docker-compose.stack.yml), a Chromium-based browser and Python 3; nothing else (the DevTools protocol is
spoken with the helpers of tests/scripts/web_hidden_check.py). It starts the browser with a throwaway profile on a free port (headless; nothing of yours is touched) and plays two
matches of a public room for two players (the room that the create block of the first Hello makes: Small, two seats), in two WINDOWS (both are shown, so both draw frames), the first
window's player giving its orders with the mouse (a rubber band over the hill, then a right click on open ground: the game's own HUD and the page's real input path), the second window's
player now and then too, so that the first one has other players' orders to correct for. The second window always has the page's address as it is (the prediction is on by default):

  * the first match with nothing said on the first window's address: the prediction is ON (the game says so through its `ants_probe`, src/ants_app/application.cpp), it predicted the
    orders that were given and never went into a cool-down, the corner's "delay" (what the click FEELS: the frame that took it to the frame that shows it) is under 60 ms while the
    network's delay (the confirmed engine's, which the picture no longer waits for) is what it always was and larger (the second window predicts too);
  * the second match with ?prediction=off on the first window's address (the second window still predicts: a window that predicts and a window that does not): the prediction is OFF,
    nothing was predicted, and the corner's delay is the network's;
  * in both: the room is still running with both players at the end, and the state hashes that the two games report (the page posts one every 100 ticks on the broadcast channel
    "ants-sync") are equal tick for tick (the confirmed matches are identical, whatever a window predicts), and the game's own frame function stays cheap (a mean under 8 ms, a
    frame is 16.7 ms).

With --rtt MS and --jitter MS the page and its WebSocket go through a small TCP proxy of this script that delays every chunk of bytes, in each direction, by MS / 2 plus a random 0 to
JITTER ms, never reordering the bytes of one direction (what TCP does): the round trip is then MS + JITTER on average. The stack's own latency is a few milliseconds on one machine; this is
how a link of 100 or 180 ms is tried. Without either option nothing is delayed.

What it cannot say: what a real display adds, the loss and the bandwidth of a real link (the proxy only delays), or anything about Safari and Firefox.

Exit status 0: every check passed; 1: a check failed; 3: the check could not be made (no browser, the page did not come up, the stack does not answer); 2 is a bad command line.
"""
import argparse
import asyncio
import os
import random
import signal
import statistics
import sys
import threading
import time
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import browser_process  # noqa: E402  (how the browser is started and stopped)
import web_hidden_check as hidden  # noqa: E402  (the DevTools helpers, the hook that records the games' state hashes, the control interface)

WINDOW = (1000, 640)


class DelayProxy:
    """A TCP proxy on the loopback address that delays every chunk of bytes by half of the round trip plus a random part of the jitter, in each direction.

    The delay of a chunk is never less than the one before it in the same direction (the bytes of a connection arrive in order, as over TCP: a chunk that drew a long delay holds the ones
    behind it). Nothing is dropped and the bandwidth is not limited."""

    def __init__(self, host, port, rtt_ms, jitter_ms, seed=1):
        self.target = (host, port)
        self.half = rtt_ms / 2000.0
        self.jitter = jitter_ms / 1000.0
        self.rng = random.Random(seed)
        self.loop = asyncio.new_event_loop()
        self.server = None
        self.port = 0
        self.ready = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self.thread.start()
        if not self.ready.wait(10):
            raise OSError("the delay proxy did not start")
        return self.port

    def _run(self):
        asyncio.set_event_loop(self.loop)
        self.server = self.loop.run_until_complete(asyncio.start_server(self._client, "127.0.0.1", 0))
        self.port = self.server.sockets[0].getsockname()[1]
        self.ready.set()
        self.loop.run_forever()

    async def _client(self, reader, writer):
        try:
            up_reader, up_writer = await asyncio.open_connection(*self.target)
        except OSError:
            writer.close()
            return
        await asyncio.gather(self._pipe(reader, up_writer), self._pipe(up_reader, writer), return_exceptions=True)
        for w in (writer, up_writer):
            w.close()

    async def _pipe(self, reader, writer):
        """Chunks from `reader` to `writer`, each due at its own time; the reading goes on while the writing waits, so the throughput is the link's, not one chunk per delay."""
        due_queue = asyncio.Queue()
        loop = asyncio.get_running_loop()

        async def read():
            last = 0.0
            try:
                while True:
                    data = await reader.read(65536)
                    last = max(last, loop.time() + self.half + self.rng.uniform(0.0, self.jitter))
                    await due_queue.put((last, data or None))
                    if not data:
                        return
            except (OSError, asyncio.IncompleteReadError):
                await due_queue.put((max(last, loop.time()), None))

        reading = asyncio.ensure_future(read())
        try:
            while True:
                due, data = await due_queue.get()
                wait = due - loop.time()
                if wait > 0:
                    await asyncio.sleep(wait)
                if data is None:
                    if writer.can_write_eof():
                        writer.write_eof()
                    return
                writer.write(data)
                await writer.drain()
        except (OSError, RuntimeError):
            pass
        finally:
            reading.cancel()

    def stop(self):
        async def shut():
            self.server.close()
            current = asyncio.current_task()
            tasks = [t for t in asyncio.all_tasks() if t is not current]
            for t in tasks:
                t.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)
            self.loop.stop()

        try:
            asyncio.run_coroutine_threadsafe(shut(), self.loop)
            self.thread.join(timeout=10)
        except RuntimeError:
            pass


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
    """One match of two windows; returns what the first window's game reported (None: the match did not start)."""
    code = "pred%s%d" % (mode, random.randint(10000, 99999))       # (a code is only a name: the room is what the create block of its first Hello says)
    block = "&roommap=small&roomseats=2"                          # (protocol 15: two seats on Small; every link of a room that the front page made carries it right after the code)
    a = Seat(devtools, web + "?join=/ws&room=%s%s&seat=0&name=Alice" % (code, block) + {"default": "", "off": "&prediction=off"}[mode])
    b = Seat(devtools, web + "?join=/ws&room=%s%s&seat=1&name=Bob" % (code, block))
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
        corner, network, cooling = [], [], 0
        a.probe(15)                                                   # (the frames' work is counted from here)
        for i in range(orders):
            a.order(rng.randint(80, 700), rng.randint(90, 480))
            if i % 2 == 1:
                b.order(rng.randint(80, 700), rng.randint(90, 480))
            time.sleep(1.4)
            corner.append(a.probe(7))
            network.append(a.probe(8))
            cooling += (a.probe(9) == 2) + (b.probe(9) == 2)          # (a cool-down: the prediction gave itself up for a while, see "The budget" in docs/NETWORK_PORT.md)
        reading = {
            "state": a.probe(9), "predicted": a.probe(10), "rebuilds": a.probe(11), "other_predicted": b.probe(10), "cooling": cooling,
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
        reading["compared"] = len(both)
        reading["mismatches"] = len(wrong)
        return reading
    finally:
        a.close()
        b.close()


def median(values):
    return statistics.median(values) if values else None


def mean(values):
    return round(statistics.mean(values), 1) if values else None


def summary(label, reading):
    """One line of numbers for a match (the corner's delay, the network's, the frame function's cost, the prediction's counters)."""
    return ("  numbers %s: felt median %s mean %s ms | network median %s mean %s ms | frame mean %d longest %d us over %d frames | predicted %d (second window %d) rebuilds %d | "
            "state %d, cool-downs seen %d | hashes compared %d, mismatches %d"
            % (label, median(reading["corner"]), mean(reading["corner"]), median(reading["network"]), mean(reading["network"]), reading["frame_us"], reading["frame_max_us"],
               reading["frames"], reading["predicted"], reading["other_predicted"], reading["rebuilds"], reading["state"], reading["cooling"], reading["compared"], reading["mismatches"]))


def main():
    signal.signal(signal.SIGTERM, lambda signum, frame: sys.exit(143))            # (a stopped run still closes the browser and removes the profile: `finally` runs)
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--web", required=True, help="the web page of the stack, e.g. http://127.0.0.1:19980/")
    ap.add_argument("--ctl", required=True, help="the control interface of the game server, e.g. http://127.0.0.1:4010")
    ap.add_argument("--secret", required=True, help="the bearer secret of the control interface (docker exec ants-server cat /results/control-secret)")
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--orders", type=int, default=12, help="how many orders the first window gives in a match (default 12)")
    ap.add_argument("--rtt", type=int, default=0, help="delay the page and its WebSocket by this round trip in ms (a proxy of this script; default 0: none)")
    ap.add_argument("--jitter", type=int, default=0, help="add a random 0 to this many ms to every delayed chunk (default 0)")
    args = ap.parse_args()
    if args.rtt < 0 or args.jitter < 0:
        ap.error("--rtt and --jitter are not negative")

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
    proxy = None
    link = "the stack's own link"
    if args.rtt > 0 or args.jitter > 0:
        target = urllib.parse.urlsplit(web)
        proxy = DelayProxy(target.hostname or "127.0.0.1", target.port or 80, args.rtt, args.jitter)
        try:
            web = "http://127.0.0.1:%d/" % proxy.start()
        except OSError as error:
            print("  SKIP: %s" % error)
            return 3
        link = "a link delayed by %d ms (round trip) plus 0 to %d ms of jitter a chunk each way" % (args.rtt, args.jitter)
    port = hidden.free_port()
    process, profile = browser_process.start(
        [browser, "--headless=new", "--remote-debugging-port=%d" % port, "--user-data-dir={profile}", "--no-first-run", "--no-default-browser-check", "--disable-extensions",
         "--autoplay-policy=no-user-gesture-required", "--window-size=%d,%d" % WINDOW, "--disable-background-timer-throttling", "--disable-renderer-backgrounding",
         "--disable-backgrounding-occluded-windows", "about:blank"], "ants_prediction_profile.")
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
        print("[web prediction] two matches of a public room for two (Small, made by the first Hello's create block), two windows each, in %s over %s: nothing said (the default) and ?prediction=off"
              % (os.path.basename(browser), link))
        on = play(devtools, web, args.ctl, args.secret, "default", args.orders, check, "the default")
        if on is None:
            return 3
        off = play(devtools, web, args.ctl, args.secret, "off", args.orders, check, "?prediction=off")
        if off is None:
            return 3
        print(summary("default", on))
        print(summary("?prediction=off", off))

        felt_on, net_on = median(on["corner"]), median(on["network"])
        check(on["state"] == 1, "the default: the game says its prediction is on (%d)" % on["state"])
        check(on["cooling"] == 0, "the default: the prediction never went into a cool-down in either window (%d samples)" % on["cooling"])
        check(on["predicted"] >= args.orders * 2 // 3, "the default: the orders were predicted (%d of %d given)" % (on["predicted"], args.orders))
        check(on["other_predicted"] >= 1, "the default: the second window predicted its orders too (%d)" % on["other_predicted"])
        check(felt_on is not None and felt_on <= 60, "the default: the corner's delay is what the click feels, under 60 ms (median %s ms of %s)" % (felt_on, on["corner"]))
        check(net_on is not None and net_on >= 40, "the default: the network's delay is still measured and is larger (median %s ms of %s)" % (net_on, on["network"]))
        check(felt_on is not None and net_on is not None and felt_on + 20 <= net_on, "the default: the click is felt sooner than the confirmed engine applies it (%s ms against %s ms)" % (felt_on, net_on))
        check(0 < on["frame_us"] <= 8000, "the default: the game's frame function costs %d us on average (%d frames, the longest %d us)" % (on["frame_us"], on["frames"], on["frame_max_us"]))
        corner_off, net_off = median(off["corner"]), median(off["network"])
        check(off["state"] == 0, "?prediction=off: the game says its prediction is off (%d)" % off["state"])
        check(off["predicted"] == 0, "?prediction=off: nothing was predicted (%d)" % off["predicted"])
        check(corner_off is not None and net_off is not None and abs(corner_off - net_off) <= 20 and net_off >= 40,
              "?prediction=off: the corner's delay is the network's (%s ms against %s ms)" % (corner_off, net_off))
        check(0 < off["frame_us"] <= 8000, "?prediction=off: the game's frame function costs %d us on average (%d frames, the longest %d us)" % (off["frame_us"], off["frames"], off["frame_max_us"]))
        print("[web prediction] %d checks, %d failed" % (checks[0], len(failures)))
        return 1 if failures else 0
    finally:
        if devtools is not None:
            try:
                devtools.call("Browser.close", timeout=5)
            except Exception:  # noqa: BLE001
                pass
        browser_process.stop(process, profile)
        if proxy is not None:
            proxy.stop()


if __name__ == "__main__":
    sys.exit(main())
