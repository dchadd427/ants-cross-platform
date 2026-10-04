#!/usr/bin/env python3
"""The web game in a REALLY hidden browser tab (opt-in; see tests/scripts/test_web_hidden.sh and docs/NETWORK_PORT.md, "Known gap").

Needs a running stack (the web page and the game server with demo rooms: docker-compose.stack.yml), a Chromium-based browser and Python 3; nothing else (the DevTools
protocol is spoken over a WebSocket written here with the standard library). It starts the browser with a throwaway profile on a free port (headless; nothing of yours is
touched), opens two seats of a demo room for two players in two tabs, puts the second tab in front so that the first one is hidden for real, and watches the room through the
control interface for the given time:

  * the room keeps running at 20 ticks a second (the hidden seat is driven by the server's messages, not by its frames), with both players in it;
  * the state hashes that both games report (the page posts one every 100 ticks on the broadcast channel "ants-sync") are equal, tick for tick;
  * the hidden seat is never called lagging (no Lag notice, message type 25, reaches either page: the server tells the room when a seat is 3 s behind);
  * the hidden page starts no music while it is hidden (no .mp3 / .MID file is opened after the match began);
  * after the hidden period the seat is still in the match (the room is still running with two players).

Exit status 0: every check passed; 1: a check failed; 3: the check could not be made (no browser, the page did not come up, the browser does not hide tabs, the stack does not
answer); 2 is the status of a bad command line.
"""
import argparse
import base64
import json
import os
import queue
import random
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse
import urllib.request

# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
# a WebSocket client (RFC 6455, text frames, what the DevTools protocol needs)
# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------


class WebSocket:
    def __init__(self, url):
        u = urllib.parse.urlparse(url)
        self.sock = socket.create_connection((u.hostname, u.port), timeout=15)
        key = base64.b64encode(os.urandom(16)).decode()
        path = (u.path or "/") + ("?" + u.query if u.query else "")
        request = ("GET %s HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n"
                   % (path, u.hostname, u.port, key))
        self.sock.sendall(request.encode())
        buf = b""
        while b"\r\n\r\n" not in buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("the browser closed the connection during the handshake")
            buf += chunk
        head, _, self.pending = buf.partition(b"\r\n\r\n")
        if b" 101 " not in head.split(b"\r\n")[0]:
            raise ConnectionError("the browser refused the WebSocket: " + head.decode("latin-1", "replace")[:200])
        self.sock.settimeout(None)
        self.lock = threading.Lock()

    def _read(self, n):
        while len(self.pending) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("closed")
            self.pending += chunk
        data, self.pending = self.pending[:n], self.pending[n:]
        return data

    def send(self, text):
        payload = text.encode("utf-8")
        header = bytearray([0x81])
        n = len(payload)
        if n < 126:
            header.append(0x80 | n)
        elif n < 65536:
            header.append(0x80 | 126)
            header += struct.pack(">H", n)
        else:
            header.append(0x80 | 127)
            header += struct.pack(">Q", n)
        mask = os.urandom(4)
        header += mask
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        with self.lock:
            self.sock.sendall(bytes(header) + masked)

    def receive(self):
        """The next text message (None when the connection closed); pings are answered, fragments joined."""
        message = b""
        while True:
            try:
                b0, b1 = self._read(2)
            except (ConnectionError, OSError):
                return None
            opcode, length = b0 & 0x0F, b1 & 0x7F
            if length == 126:
                length = struct.unpack(">H", self._read(2))[0]
            elif length == 127:
                length = struct.unpack(">Q", self._read(8))[0]
            data = self._read(length) if length else b""
            if opcode == 0x8:
                return None
            if opcode == 0x9:                                       # ping: pong with the same data
                with self.lock:
                    self.sock.sendall(bytes([0x8A, 0x80 | len(data)]) + b"\0\0\0\0" + data)
                continue
            if opcode == 0xA:
                continue
            message += data
            if b0 & 0x80:
                return message.decode("utf-8", "replace")

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


class DevTools:
    """The browser endpoint of the DevTools protocol: targets (tabs) and their sessions."""

    def __init__(self, port):
        with urllib.request.urlopen("http://127.0.0.1:%d/json/version" % port, timeout=10) as r:
            self.ws = WebSocket(json.load(r)["webSocketDebuggerUrl"])
        self.next_id = 0
        self.waiting = {}
        self.reader = threading.Thread(target=self._pump, daemon=True)
        self.reader.start()

    def _pump(self):
        while True:
            text = self.ws.receive()
            if text is None:
                for q in list(self.waiting.values()):
                    q.put({"error": {"message": "connection closed"}})
                return
            msg = json.loads(text)
            q = self.waiting.pop(msg.get("id"), None) if "id" in msg else None
            if q is not None:
                q.put(msg)

    def call(self, method, params=None, session=None, timeout=60):
        self.next_id += 1
        mid = self.next_id
        q = queue.Queue()
        self.waiting[mid] = q
        msg = {"id": mid, "method": method, "params": params or {}}
        if session:
            msg["sessionId"] = session
        self.ws.send(json.dumps(msg))
        try:
            reply = q.get(timeout=timeout)
        except queue.Empty:
            raise TimeoutError(method)
        if "error" in reply:
            raise RuntimeError("%s: %s" % (method, reply["error"]))
        return reply.get("result", {})

    def new_tab(self, hook, background=False):
        target = self.call("Target.createTarget", {"url": "about:blank", "background": background})["targetId"]
        session = self.call("Target.attachToTarget", {"targetId": target, "flatten": True})["sessionId"]
        self.call("Page.enable", session=session)
        self.call("Runtime.enable", session=session)
        self.call("Page.addScriptToEvaluateOnNewDocument", {"source": hook}, session=session)
        return target, session

    def open(self, session, url):
        self.call("Page.navigate", {"url": url}, session=session)

    def front(self, target):
        self.call("Target.activateTarget", {"targetId": target})

    def evaluate(self, session, expression):
        r = self.call("Runtime.evaluate", {"expression": expression, "returnByValue": True, "awaitPromise": True}, session=session)
        if "exceptionDetails" in r:
            details = r["exceptionDetails"]
            raise RuntimeError("the page raised: %s" % ((details.get("exception") or {}).get("description") or details.get("text", details)))
        return r["result"].get("value")

    def close_tab(self, target):
        try:
            self.call("Target.closeTarget", {"targetId": target}, timeout=10)
        except (RuntimeError, TimeoutError):
            pass


# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
# what runs in the page: the visibility, the state hashes that the games report, and the music files that the game opens
# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

HOOK = r"""
(function () {
  if (window.__check) return;
  window.__check = true;
  var t0 = performance.now();
  window.__visibility = [];
  document.addEventListener('visibilitychange', function () { window.__visibility.push({ t: performance.now() - t0, hidden: document.hidden }); });
  window.__sync = [];
  try {
    var bc = new BroadcastChannel('ants-sync');
    bc.onmessage = function (e) { if (e.data && e.data.ants === 'sync') window.__sync.push({ t: performance.now() - t0, seat: e.data.seat, tick: e.data.tick, hash: e.data.hash, room: e.data.room }); };
  } catch (e) {}
  window.__ws = { messages: 0, lag: 0 };                                      // the messages of the game server (binary; the first byte is the message type) and the Lag notices among them (type 25)
  try {
    var NativeWebSocket = window.WebSocket;
    var CountingWebSocket = function (url, protocols) {
      var ws = protocols === undefined ? new NativeWebSocket(url) : new NativeWebSocket(url, protocols);
      ws.addEventListener('message', function (e) {
        window.__ws.messages++;
        try { if (e.data instanceof ArrayBuffer && e.data.byteLength > 0 && new Uint8Array(e.data)[0] === 25) window.__ws.lag++; } catch (x) {}
      });
      return ws;
    };
    CountingWebSocket.prototype = NativeWebSocket.prototype;
    ['CONNECTING', 'OPEN', 'CLOSING', 'CLOSED'].forEach(function (k) { CountingWebSocket[k] = NativeWebSocket[k]; });
    window.WebSocket = CountingWebSocket;
  } catch (e) {}
  window.__music = [];
  var timer = setInterval(function () {
    if (window.Module && Module.FS && Module.FS.open && !Module.FS.__wrapped) {
      var open = Module.FS.open;
      Module.FS.open = function (path) {
        try { if (/\.(mp3|mid)$/i.test(String(path))) window.__music.push({ t: performance.now() - t0, path: String(path), hidden: document.hidden }); } catch (e) {}
        return open.apply(this, arguments);
      };
      Module.FS.__wrapped = true;
      clearInterval(timer);
    }
  }, 20);
})();
"""


def find_browser(explicit):
    candidates = [explicit] if explicit else []
    candidates += [
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
        "/Applications/Chromium.app/Contents/MacOS/Chromium",
        "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge",
        "google-chrome", "google-chrome-stable", "chromium", "chromium-browser", "microsoft-edge",
    ]
    for c in candidates:
        if c and (os.path.isfile(c) and os.access(c, os.X_OK) or shutil.which(c)):
            return c if os.path.isfile(c) else shutil.which(c)
    return None


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def control_room(ctl, secret, code):
    request = urllib.request.Request(ctl.rstrip("/") + "/rooms", headers={"Authorization": "Bearer " + secret})
    with urllib.request.urlopen(request, timeout=10) as r:
        rooms = json.load(r).get("rooms", [])
    for room in rooms:
        if room.get("code") == code:
            return room
    return None


def main():
    signal.signal(signal.SIGTERM, lambda signum, frame: sys.exit(143))            # (a stopped run still closes the browser and removes the profile: `finally` runs)
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--web", required=True, help="the web page of the stack, e.g. http://127.0.0.1:19980/")
    ap.add_argument("--ctl", required=True, help="the control interface of the game server, e.g. http://127.0.0.1:4010")
    ap.add_argument("--secret", required=True, help="the bearer secret of the control interface (docker exec ants-server cat /results/control-secret)")
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--seconds", type=int, default=45, help="how long one seat stays hidden (default 45)")
    args = ap.parse_args()

    failures = []
    checks = [0]

    def check(ok, what):
        checks[0] += 1
        print("  %s: %s" % ("ok  " if ok else "FAIL", what))
        if not ok:
            failures.append(what)

    browser = find_browser(args.browser)
    if not browser:
        print("  SKIP: no Chromium-based browser found (give one with --browser or CHROME)")
        return 3
    web = args.web if args.web.endswith("/") else args.web + "/"
    code = "demo-small-2p-hid%d" % random.randint(10000, 99999)
    profile = tempfile.mkdtemp(prefix="ants_hidden_profile.")
    port = free_port()
    process = subprocess.Popen(
        [browser, "--headless=new", "--remote-debugging-port=%d" % port, "--user-data-dir=" + profile, "--no-first-run", "--no-default-browser-check",
         "--disable-extensions", "--autoplay-policy=no-user-gesture-required", "about:blank"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    tabs = []
    devtools = None
    try:
        for _ in range(100):
            try:
                devtools = DevTools(port)
                break
            except (OSError, ValueError, ConnectionError):
                time.sleep(0.2)
        if devtools is None:
            print("  SKIP: the browser did not start")
            return 3
        for t in devtools.call("Target.getTargets")["targetInfos"]:
            if t["type"] == "page":
                devtools.close_tab(t["targetId"])
        print("[web hidden] a room for two (%s) in %s; seat 0 is a tab that is hidden from the start (opened behind another) for %d s" % (code, os.path.basename(browser), args.seconds))
        tab_blank, session_blank = devtools.new_tab("")
        tabs.append(tab_blank)
        devtools.front(tab_blank)
        tab_a, session_a = devtools.new_tab(HOOK, background=True)       # seat 0: opened in the background, so it is hidden the whole time
        tabs.append(tab_a)
        devtools.open(session_a, web + "?join=/ws&room=%s&seat=0&name=Hidden" % code)
        room = None
        deadline = time.time() + 120                                # the game data is about 9 MB: a cold start takes a while
        while time.time() < deadline:
            room = control_room(args.ctl, args.secret, code)
            if room and room.get("joined", 0) >= 1:
                break
            time.sleep(1)
        if not room or room.get("joined", 0) < 1:
            print("  SKIP: the first seat did not come into the room in time (the page or the server is not up): %s" % room)
            return 3
        time.sleep(3)
        music_before = devtools.evaluate(session_a, "window.__music.length")      # (the files that the game opened until now: the intro of the setup screen)
        tab_b, session_b = devtools.new_tab(HOOK)                          # seat 1 joins in front: the match starts while seat 0 is hidden
        tabs.append(tab_b)
        devtools.open(session_b, web + "?join=/ws&room=%s&seat=1&name=Shown" % code)
        devtools.front(tab_b)
        room = None
        deadline = time.time() + 120                                # the game data is about 9 MB: a cold start takes a while
        while time.time() < deadline:
            room = control_room(args.ctl, args.secret, code)
            if room and room.get("state") == "running":
                break
            time.sleep(1)
        if not room or room.get("state") != "running":
            print("  SKIP: the match did not start in time (the page or the server is not up): %s" % room)
            return 3
        hidden = devtools.evaluate(session_a, "document.hidden")
        if hidden is not True:
            print("  SKIP: this browser does not hide a tab that another tab covers (document.hidden is %r): the check would say nothing" % (hidden,))
            return 3
        # the match opens with the "Get ready to play!" dialog and its simulation waits for it (network protocol 12): the room's ticks count from the first turn, which the server seals
        # 5 s after the match began; the rate is measured from there (a window that began inside the dialog would count its seconds as a slow clock)
        for _ in range(60):
            room = control_room(args.ctl, args.secret, code)
            if room is None or room.get("ticks", 0) > 0:
                break
            time.sleep(0.5)
        time.sleep(3)
        first = control_room(args.ctl, args.secret, code)
        t_first = time.time()
        samples = []
        while time.time() - t_first < args.seconds:
            time.sleep(5)
            now = control_room(args.ctl, args.secret, code)
            if now is None:
                break
            samples.append((time.time(), now))
        last_time, last = samples[-1] if samples else (time.time(), first)
        seconds = last_time - t_first
        rate = (last["ticks"] - first["ticks"]) / seconds if seconds > 0 else 0.0
        print("  the room: %d -> %d ticks in %.1f s (%.2f ticks a second), state %s, %s players" % (first["ticks"], last["ticks"], seconds, rate, last.get("state"), last.get("joined")))
        check(devtools.evaluate(session_a, "document.hidden") is True and devtools.evaluate(session_a, "window.__visibility.length") == 0, "the seat stayed hidden for the whole period (no visibility change)")
        check(last.get("state") == "running" and last.get("joined") == 2, "the room is still running with both players (nobody was dropped)")
        check(19.0 <= rate <= 21.0, "the room ran at 20 ticks a second (%.2f)" % rate)
        feeds = {}
        for name, session in (("hidden", session_a), ("shown", session_b)):
            feeds[name] = json.loads(devtools.evaluate(session, "JSON.stringify(window.__sync)"))
        by_tick = {}
        for entry in feeds["hidden"] + feeds["shown"]:
            if entry.get("room") == code:
                by_tick.setdefault(entry["tick"], {})[entry["seat"]] = entry["hash"]
        compared = [t for t, seats in by_tick.items() if len(seats) == 2]
        mismatched = [t for t in compared if by_tick[t][0] != by_tick[t][1]]
        check(len(compared) >= max(2, args.seconds // 5 - 2), "the hash reports of both seats were compared at %d ticks" % len(compared))
        check(not mismatched, "every state hash of the two games is equal (%d mismatches)" % len(mismatched))
        reports = [e for e in feeds["shown"] if e.get("seat") == 0 and e.get("room") == code]      # the hidden seat's own reports, stamped when the shown page got them
        if len(reports) >= 3:
            progress = (reports[-1]["tick"] - reports[0]["tick"]) / max(0.001, (reports[-1]["t"] - reports[0]["t"]) / 1000.0)
            check(19.0 <= progress <= 21.0, "the hidden seat itself executed 20 ticks a second (%.2f, from its own hash reports)" % progress)
        else:
            check(False, "the hidden seat reported its hash %d times (at least 3 expected)" % len(reports))
        ws = {name: json.loads(devtools.evaluate(session, "JSON.stringify(window.__ws)")) for name, session in (("hidden", session_a), ("shown", session_b))}
        check(all(w["messages"] > 20 * args.seconds // 2 for w in ws.values()), "both pages received the server's turns (%d and %d messages)" % (ws["hidden"]["messages"], ws["shown"]["messages"]))
        check(ws["hidden"]["lag"] == 0 and ws["shown"]["lag"] == 0, "nobody was called lagging: no Lag notice reached either page (hidden %d, shown %d)" % (ws["hidden"]["lag"], ws["shown"]["lag"]))
        music = json.loads(devtools.evaluate(session_a, "JSON.stringify(window.__music)"))
        late = [m for m in music[music_before:] if m["hidden"]]
        check(not late, "the hidden page opened no music file after the match began (%s)" % ", ".join(m["path"] for m in late))
        devtools.front(tab_a)
        time.sleep(3)
        after = control_room(args.ctl, args.secret, code)
        check(after is not None and after.get("state") == "running" and after.get("joined") == 2, "shown again, the seat is still in the running match")
        check(devtools.evaluate(session_a, "document.hidden") is False, "the tab is shown")
    except (RuntimeError, TimeoutError, ConnectionError, OSError, ValueError, KeyError) as error:
        print("  %s: the browser or the stack could not be driven (%s: %s)" % ("FAIL" if failures else "SKIP", type(error).__name__, error))
        return 1 if failures else 3
    finally:
        for t in tabs:
            try:
                if devtools is not None:
                    devtools.close_tab(t)
            except Exception:
                pass
        process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
        shutil.rmtree(profile, ignore_errors=True)
    print("[web hidden] %d checks, %d failed" % (checks[0], len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
