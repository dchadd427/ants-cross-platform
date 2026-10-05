#!/usr/bin/env python3
"""The way back to a running match, in REAL browsers (opt-in; see tests/scripts/test_web_rejoin.sh and docs/NETWORK_PORT.md, "What the clients do in release B").

The owner: "if your browser crashed or your power went out, you could ... hold the game paused until you get back". The server holds the seat of a player whose connection is lost (the default since
release B), the game page comes back to its seat by itself (a reload, a restart of the server), and the front page has a Rejoin button for the player who closed the tab. Nothing but a browser
can show that the whole chain works: the page's storage, the game's WebSocket through nginx, the server, the catch-up, the other player's screen.

Needs the web image of this tree on a port (`docker build -t ants-beta .`; its nginx passes /ws and /busy to the game server's WebSocket port: --ws-port is that port), the native `ants_server` of
this tree (--server), a Chromium-based browser and Python 3; nothing else (the DevTools protocol is spoken with the client of web_hidden_check.py, standard library only). The check starts the server
itself, with the options of docker-compose.stack.yml (the site's own: the demo rooms, and NO reconnect option: what holds the seats is the default) on ports of its own, and every player is a
browser of its own (its own profile: its own local storage, as a player's own computer has; nothing of yours is touched). Parts (--only NAME):

  * reload   two players join a two-seat room by the room's links and the match starts; the first reloads its page: it is back in the running match within 20 s, with NO "Get ready" dialog; the
             other player's screen said that the seat was missing and, when the pause had lasted 3 s or more (the server held the resume countdown: its status says so), "... is back: the match goes
             on in N" (the game's own words, read through ants_probe 16 and 10000 +); the match goes on and the two games' state hashes (the sync lines that every game posts every 100 ticks) agree;
             the server's status says it was paused, took a player back and is not paused any more;
  * restart  the same match; the server is stopped with SIGTERM and started again on the same folder and ports: both pages say "Connection lost. Reconnecting...", both come back by themselves,
             the room came back from its restart record (the server's clock for a pause begins at the restore: players back within 3 s of it resume at once, and a pause of 3 s or more is
             counted down on a screen), and the match goes on with equal hashes;
  * rejoin   the first player CLOSES its tab in the middle of the match (the key stays in the browser's storage), a new tab opens the front page: "Rejoin your match (CODE)" is there with its note,
             pressing it puts the player back in its seat (the address has no key), the other player sees "... is back", the match goes on with equal hashes;
  * leave    two players in a match; the second leaves ON PURPOSE through the page's Menu button and says yes to its question ("Leave the game and go back to the menu?"): the page goes to the front
             page, the server drops the seat at once (the room is never paused: a closed tab would have held the seat and stopped the match for the other player), the first player's screen never
             says that a connection was lost, the key is gone from the second browser's storage and the front page offers no Rejoin;
  * none     the front page with no entry, with an old one (it is removed), one of another server (it stays), malformed ones and a fresh one of this site (the button is there, then gone with the
             entry; a phone-sized window with a room code of 32 characters has no sideways scroll).
In every part the key is in no address, no text of any page and no console line.

Exit status 0: every check passed; 1: a check failed, or the browser, the page or the server broke down during the check (a page that hangs is a failure, never a skip); 3: the check could not be made
because the environment is not there (no browser, no server program, nothing answers at the page's address, the site's /busy does not come from the server that was started); 2: a bad command line.
"""
import argparse
import base64
import json
import os
import re
import secrets
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from web_aspect_check import Browser, NotReachable                           # noqa: E402
from web_hidden_check import find_browser, free_port                         # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
PARTS = ("reload", "restart", "rejoin", "leave", "none")

# What runs in every new document of a player's tab: the state hashes that the game posts on the broadcast channel (every 100 ticks), and an id of the document (a reload makes a new one).
HOOK = r"""
(function () {
  if (window.__rejoin_check) return;
  window.__rejoin_check = true;
  window.__docId = Math.random().toString(36).slice(2);
  window.__syncs = [];
  try {
    var bc = new BroadcastChannel('ants-sync');
    bc.onmessage = function (e) { if (e.data && e.data.ants === 'sync') window.__syncs.push({ seat: e.data.seat, tick: e.data.tick, hash: e.data.hash, room: e.data.room, at: Date.now() }); };
  } catch (e) {}
})();
"""

# The game's own state, read through its probes: [document id, match running (0 or 1), a dialog is open (-1 no match, 0 none, 1 open)]. null while the page has no game yet.
# (Only once the page says that the game is ready: index.js binds each export to the program at its first call, and a call while the program is still being compiled leaves it
# undefined for the life of the page. A slow connection made the check do that to itself.)
STATE_JS = """(function () {
  if (!window.isReadyToPlay || typeof Module === 'undefined' || !Module._ants_probe || !Module._ants_match_running) return null;
  return [window.__docId, Module._ants_match_running(), Module._ants_probe(5)];
})()"""

# The lines that the game's overlay shows now (ants_probe 16 and 10000 +: the page cannot read the canvas); null when the game does not answer
OVERLAY_JS = """(function () {
  if (!window.isReadyToPlay || typeof Module === 'undefined' || !Module._ants_probe) return null;
  var n = Module._ants_probe(16);
  if (n < 0) return null;
  var out = [];
  for (var l = 0; l < n; l++) {
    var s = '';
    for (var i = 0; i < 400; i++) { var c = Module._ants_probe(10000 + 1000 * l + i); if (c <= 0) break; s += String.fromCharCode(c); }
    out.push(s);
  }
  return out;
})()"""

WAY_BACK_LINE = r"^Connection lost\. Reconnecting\.\.\. \d+:\d\d"                  # (an attempt number may follow)
COUNTDOWN_LINE = r"(?i)the match goes on in \d+$"                                  # ("Ann is back: the match goes on in 7", or "The match goes on in 7" when nobody is named)


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


def shape(text):
    """A line of the overlay with its numbers made N: the seconds that count and the percent that grows are the same line."""
    return re.sub(r"\d+", "N", text)


class Server:
    """The game server of the site, the program of this tree, started as docker-compose.stack.yml starts it (its demo options, no reconnect option) on ports of the check's own."""

    def __init__(self, binary, maps, ws_port, work):
        self.binary = binary
        self.maps = maps
        self.ws_port = ws_port
        self.tcp_port = free_port()
        self.ctl_port = free_port()
        self.results = os.path.join(work, "results")
        self.secret = "rejoin-check-" + secrets.token_hex(8)
        self.process = None
        self.log_path = os.path.join(work, "server.log")
        self.options = self.stack_options()

    @staticmethod
    def stack_options():
        """The options that the stack gives the server (the public site's: tests/scripts/stack_command.py reads docker-compose.stack.yml as the stack starts it)."""
        done = subprocess.run([sys.executable, os.path.join(REPO, "tests", "scripts", "stack_command.py"), os.path.join(REPO, "docker-compose.stack.yml"), "--demo"], capture_output=True, text=True, timeout=30)
        if done.returncode != 0 or not done.stdout.strip():
            raise RuntimeError("the stack's options could not be read: " + done.stderr.strip())
        return done.stdout.split()

    def command(self):
        return [self.binary, "--maps", self.maps, "--port", str(self.tcp_port), "--ws-port", str(self.ws_port), "--ctl-port", str(self.ctl_port), "--results-dir", self.results] + self.options

    def describe(self):
        """The command line as it is shown: no path of this machine."""
        words = self.command()
        shown = [os.path.basename(words[0])]
        skip = False
        for i, w in enumerate(words[1:], 1):
            if skip:
                skip = False
                shown.append({"--maps": "<maps>", "--results-dir": "<results>"}.get(words[i - 1], w))
                continue
            shown.append(w)
            skip = w in ("--maps", "--results-dir")
        return " ".join(shown)

    def start(self):
        env = dict(os.environ, ANTS_SERVER_SECRET=self.secret)
        with open(self.log_path, "ab") as log:
            self.process = subprocess.Popen(self.command(), stdout=log, stderr=log, env=env)
        for _ in range(100):
            if self.process.poll() is not None:
                raise RuntimeError("the server stopped at its start (status %s): %s" % (self.process.returncode, self.tail()))
            try:
                urllib.request.urlopen("http://127.0.0.1:%d/healthz" % self.ctl_port, timeout=1).read()
                return
            except (OSError, urllib.error.URLError):
                time.sleep(0.1)
        raise RuntimeError("the server did not answer at its control port: " + self.tail())

    def stop(self, sig=signal.SIGTERM):
        """The seconds that the server took to end after the signal (None when it was not running)."""
        if self.process is None or self.process.poll() is not None:
            return None
        began = time.time()
        self.process.send_signal(sig)
        try:
            self.process.wait(timeout=20)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        return time.time() - began

    def tail(self, lines=12):
        try:
            with open(self.log_path, encoding="utf-8", errors="replace") as f:
                return " | ".join(f.read().strip().splitlines()[-lines:])
        except OSError:
            return ""

    def room(self, code):
        """The room's status from the control interface (None when there is none)."""
        request = urllib.request.Request("http://127.0.0.1:%d/rooms/%s" % (self.ctl_port, code), headers={"Authorization": "Bearer " + self.secret})
        try:
            with urllib.request.urlopen(request, timeout=5) as r:
                return json.load(r)
        except (OSError, urllib.error.URLError, ValueError):
            return None


class Player:
    """One person at a computer of their own: a browser with its own profile and storage, and the tab that the game runs in."""

    def __init__(self, path, name, seat, web, ready_timeout):
        self.name = name
        self.seat = seat
        self.web = web
        self.ready_timeout = ready_timeout
        self.browser = Browser(path)
        self.dt = self.browser.devtools
        self.console = []                                                    # (time, level, text) of every console line of the tabs that were open
        self.dialogs = []
        self.ticks = {}                                                      # tick -> hash that this player's game reported (every document of every tab)
        self.conflicts = []                                                  # a tick that was reported with two hashes
        self.target = None
        self.session = None
        self.dt.handlers.append(self._on_event)
        self.open_tab()

    # ---- the tab
    def open_tab(self, width=1280, height=720):
        self.target, self.session = self.dt.new_tab(HOOK)
        self.call("Emulation.setFocusEmulationEnabled", {"enabled": True})
        self.resize(width, height)

    def resize(self, width, height):
        self.call("Emulation.setDeviceMetricsOverride", {"width": width, "height": height, "deviceScaleFactor": 1, "mobile": False, "screenWidth": width, "screenHeight": height})

    def close_tab(self):
        """The player closes the tab (a closed window, a crashed browser: the game does not say goodbye, the match is not left)."""
        if self.target is not None:
            self.dt.close_tab(self.target)
        self.target = None
        self.session = None

    def call(self, method, params=None):
        return self.dt.call(method, params or {}, session=self.session)

    def ev(self, expression):
        return self.dt.evaluate(self.session, expression)

    def _on_event(self, msg):
        session = self.session
        if session is None or msg.get("sessionId") != session:
            return
        method = msg.get("method")
        params = msg.get("params", {})
        if method == "Runtime.consoleAPICalled":
            text = " ".join(str(a.get("value", a.get("description", ""))) for a in params.get("args", []))
            self.console.append((time.time(), params.get("type", ""), text))
        elif method == "Runtime.exceptionThrown":
            details = params.get("exceptionDetails", {})
            self.console.append((time.time(), "exception", str((details.get("exception") or {}).get("description") or details.get("text", ""))))
        elif method == "Page.javascriptDialogOpening":
            self.dialogs.append(params.get("message", ""))
            try:
                self.dt.call("Page.handleJavaScriptDialog", {"accept": True}, session=session)
            except (RuntimeError, TimeoutError):
                pass

    # ---- the page
    def game_url(self, room):
        """The room's link for this seat, as the front page makes it for a window of its own (web/lobby.html, gameUrl)."""
        return "%s?join=/ws&room=%s&seat=%d&name=%s&aspect=16:9" % (self.web, room, self.seat, self.name)

    def open_game(self, room):
        """Opens the game page of this seat and waits until its game is ready."""
        self.call("Page.navigate", {"url": self.game_url(room)})
        deadline = time.time() + self.ready_timeout
        while time.time() < deadline:
            time.sleep(0.5)
            if wait_for(lambda: self.ev("!!window.isReadyToPlay"), 1.0):
                return
        raise TimeoutError("the game did not become ready at " + self.game_url(room))

    def state(self):
        """[document id, match running (0 or 1), dialog (-1 no match, 0 none, 1 open)], or None while there is no game in the page."""
        try:
            return self.ev(STATE_JS)
        except (RuntimeError, TimeoutError):
            return None

    def in_match(self):
        s = self.state()
        return s is not None and s[1] == 1 and s[2] == 0

    def overlay(self):
        try:
            return self.ev(OVERLAY_JS)
        except (RuntimeError, TimeoutError):
            return None

    def syncs(self):
        """Takes the state hashes that this document's game has posted (every one is kept over reloads and tabs in self.ticks)."""
        try:
            posted = self.ev("window.__syncs || []") or []
        except (RuntimeError, TimeoutError):
            return
        for s in posted:
            known = self.ticks.get(s["tick"])
            if known is not None and known != s["hash"]:
                self.conflicts.append((s["tick"], known, s["hash"]))
            self.ticks[s["tick"]] = s["hash"]

    def key_hexes(self):
        """The hex of the keys that this browser holds (the check reads them to prove that they are nowhere else; they are never printed)."""
        try:
            return self.ev("""(function () { var out = []; for (var i = 0; i < localStorage.length; i++) { var name = localStorage.key(i);
                if (name.indexOf('ants.rejoin.') === 0) { try { out.push(JSON.parse(localStorage.getItem(name)).k); } catch (e) {} } } return out; })()""") or []
        except (RuntimeError, TimeoutError):
            return []

    def stored_entries(self):
        """The names of the entries of the game's key storage in this browser (names only: room and seat)."""
        return self.ev("Object.keys(localStorage).filter(function (k) { return k.indexOf('ants.rejoin.') === 0; }).sort()") or []

    def shot(self, directory, name):
        if not directory:
            return
        os.makedirs(directory, exist_ok=True)
        with open(os.path.join(directory, name + ".png"), "wb") as f:
            f.write(base64.b64decode(self.call("Page.captureScreenshot", {"format": "png"})["data"]))

    def close(self):
        self.browser.close()


class Timeline:
    """What a player's screen said over time: the distinct lines of the overlay, in the order they came (the numbers in them made N), with the moment of the first of each."""

    def __init__(self):
        self.start = time.time()
        self.seen = []                                                       # [(seconds, shapes of the lines, the lines)]
        self.last = None

    def add(self, lines):
        if lines is None:
            return
        key = tuple(shape(x) for x in lines)
        if key != self.last:
            self.seen.append((time.time() - self.start, key, list(lines)))
            self.last = key

    def lines(self):
        return [line for _, _, lines in self.seen for line in lines]

    def first(self, pattern):
        """The seconds at which a line that matches `pattern` first showed, else None."""
        for at, _, lines in self.seen:
            if any(re.search(pattern, line) for line in lines):
                return at
        return None

    def texts(self):
        return [" / ".join(lines) if lines else "(nothing)" for _, _, lines in self.seen]

    def account(self):
        """What the screen said and when, for the person who reads the output: "0s: (nothing); 3s: Ann (Green) lost the connection, waiting 0:01; 9s: Ann is back: the match goes on in 10; 19s: (nothing)"."""
        return "; ".join("%.0fs: %s" % (at, " / ".join(lines) if lines else "(nothing)") for at, _, lines in self.seen)


class Watch:
    """Looks at the screens of some players (what their overlays say, the state hashes that their games post) and at the server's status of the room, every few tenths of a second."""

    def __init__(self, server, room, players):
        self.server = server
        self.room = room
        self.players = list(players)
        self.timelines = {p: Timeline() for p in self.players}
        self.status = []                                                     # (seconds, paused, resume seconds, seats absent)
        self.begin = time.time()

    def look(self):
        for p in self.players:
            if p.target is not None:
                self.timelines[p].add(p.overlay())
                p.syncs()
        status = self.server.room(self.room)
        if status:
            self.status.append((time.time() - self.begin, bool(status.get("paused")), int(status.get("resume_seconds") or 0), len(status.get("absent") or [])))

    def until(self, done, limit, step=0.25):
        """Looks until done() is true or `limit` seconds have passed; whether done() came true."""
        end = time.time() + limit
        while time.time() < end:
            self.look()
            if done():
                return True
            time.sleep(step)
        self.look()
        return bool(done())

    def tl(self, player):
        return self.timelines[player]

    def counted_down(self):
        """The server held the resume countdown at some look (its status said resume_seconds above 0)."""
        return any(resume > 0 for _, _, resume, _ in self.status)

    def paused_for(self):
        """About how long the server said the match was paused, in seconds (from the first look at paused to the first look after it)."""
        first = next((t for t, paused, _, _ in self.status if paused), None)
        if first is None:
            return 0.0
        last = next((t for t, paused, _, _ in self.status if t > first and not paused), None)
        return (last if last is not None else self.status[-1][0]) - first


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--web", required=True, help="the site, e.g. http://127.0.0.1:8080/ (the web image of this tree; its /ws goes to --ws-port)")
    ap.add_argument("--ws-port", type=int, required=True, help="the WebSocket port that the site's /ws and /busy lead to (the check's server listens there)")
    ap.add_argument("--server", default=os.environ.get("ANTS_SERVER_BIN", os.path.join(REPO, "build", "src", "ants_server", "ants_server")), help="the ants_server program of this tree")
    ap.add_argument("--maps", default=os.path.join(REPO, "Original-Ants", "Maps"), help="the maps folder (default: the original's)")
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--only", default="", help="run only the parts whose name contains this text (%s)" % ", ".join(PARTS))
    ap.add_argument("--shots", default="", help="a folder to save screenshots in")
    ap.add_argument("--ready-timeout", type=float, default=120.0, metavar="SECONDS", help="how long a page may take to get its game ready (default 120)")
    ap.add_argument("--back-seconds", type=float, default=20.0, help="how long a reloaded game may take to be back in the match (default 20)")
    ap.add_argument("--keep-logs", default="", help="a folder to copy the server's log to when the check ends")
    args = ap.parse_args()
    web = args.web if args.web.endswith("/") else args.web + "/"
    failures = []
    count = [0]
    began_all = time.time()

    def check(ok, what):
        count[0] += 1
        print("  %s: %s" % ("ok  " if ok else "FAIL", what), flush=True)
        if not ok:
            failures.append(what)

    def note(text):
        print("  note: %s" % text, flush=True)

    def wanted(name):
        return not args.only or args.only in name

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

    work = tempfile.mkdtemp(prefix="ants_rejoin_check.")
    players = []
    server = Server(args.server, args.maps, args.ws_port, work)

    def new_room():
        return "demo-tiny-2p-" + secrets.token_hex(3)

    def begin_match(room):
        """Two players, each on a computer of their own, take the two seats of a room through the room's links; returns when the match runs for both and no dialog is open."""
        a = Player(chrome, "Ann", 0, web, args.ready_timeout)
        players.append(a)
        b = Player(chrome, "Bob", 1, web, args.ready_timeout)
        players.append(b)
        a.open_game(room)
        b.open_game(room)
        return a, b, bool(wait_for(lambda: a.in_match() and b.in_match(), 60, 0.5))

    def common_ticks(a, b, after_tick):
        """The ticks after `after_tick` that both players' games reported: [(tick, hash a, hash b)]."""
        return [(t, a.ticks[t], b.ticks[t]) for t in sorted(a.ticks) if t > after_tick and t in b.ticks]

    def agree_afterwards(watch, a, b, label, after_tick, need=2, limit=60):
        """The games' state hashes agree: at least `need` ticks after `after_tick` that both reported, all equal, and no tick that one game reported with two hashes."""
        watch.until(lambda: len(common_ticks(a, b, after_tick)) >= need, limit)
        common = common_ticks(a, b, after_tick)
        check(len(common) >= need, "%s: the two games reported %d common ticks after the way back (%s)" % (label, len(common), [t for t, _, _ in common][:6]))
        check(all(x == y for _, x, y in common), "%s: their state hashes agree at every one of them" % label)
        check(not a.conflicts and not b.conflicts, "%s: no game reported one tick with two hashes (%s)" % (label, a.conflicts + b.conflicts))

    def key_nowhere(label, ps, earlier=None):
        """The keys of these browsers are in no address, no text of a page and no console line (the check reads them from the storage to look for them, and prints none). `earlier`: {player: keys}
        that were read before the storage was emptied (a player who left no longer holds one)."""
        leaked = []
        searched = 0
        for p in ps:
            if p.target is None:
                continue
            for key in sorted(set(p.key_hexes()) | set((earlier or {}).get(p, []))):
                searched += 1
                page = p.ev("[location.href, document.body ? document.body.innerText : '', document.title].join('\\n')") or ""
                if key.lower() in page.lower():
                    leaked.append("%s's page" % p.name)
                if any(key.lower() in text.lower() for _, _, text in p.console):
                    leaked.append("%s's console" % p.name)
        check(searched > 0 and not leaked, "%s: none of the %d key(s) in the browsers is in any address, page text or console line (%s)" % (label, searched, leaked or "searched"))

    def server_says(room, label, **want):
        status = server.room(room) or {}
        got = {k: status.get(k) for k in want}
        ok = all(v(got[k]) if callable(v) else got[k] == v for k, v in want.items())
        check(ok, "%s: the server's status says %s (it says %s)" % (label, {k: ("..." if callable(v) else v) for k, v in want.items()}, got))
        return status

    def clean_consoles(label, ps):
        bad = [t for p in ps for _, level, t in p.console if re.search(r"out of sync|Could not|Failed to|exception|Uncaught", t, re.I)]
        check(not bad, "%s: no game said that it was out of sync or failed (%s)" % (label, [b[:90] for b in bad[:3]]))

    def end_part():
        for p in players:
            try:
                p.close()
            except Exception:                                                # noqa: BLE001
                pass
        players.clear()

    try:
        server.start()
        # the site's /busy must come from the server that was just started: the site and the check agree on the port
        try:
            busy = json.loads(urllib.request.urlopen(web + "busy", timeout=10).read().decode("utf-8", "replace"))
        except (OSError, urllib.error.URLError, ValueError) as e:
            print("  SKIP: the site's /busy does not answer as the game server's does (%s): does the site's /ws lead to port %d?" % (e, args.ws_port))
            return 3
        if not (isinstance(busy, dict) and set(busy) == {"matches", "players"}):
            print("  SKIP: the site's /busy is not the game server's answer (%r): does the site's /ws lead to port %d?" % (busy, args.ws_port))
            return 3
        print("[web rejoin] the server: %s" % server.describe())

        # ------------------------------------------------------------------------------------------------------------------------------------------------------------
        if wanted("reload"):
            print("[web rejoin] reload: a page that is reloaded in the middle of a match takes its seat again, and the other player waits")
            room = new_room()
            a, b, running = begin_match(room)
            check(running, "two players joined %s through the room's links and the match runs, with no dialog open" % room)
            if running:
                watch = Watch(server, room, [a, b])
                watch.until(lambda: a.ticks and b.ticks, 40)
                check(bool(a.ticks and b.ticks), "both games report their state hash (every 100 ticks) from the start (ticks %s and %s)" % (sorted(a.ticks)[:2], sorted(b.ticks)[:2]))
                server_says(room, "before the reload", state="running", reconnect=True, paused=False)
                check(a.stored_entries() == ["ants.rejoin.%s.0" % room] and b.stored_entries() == ["ants.rejoin.%s.1" % room], "each browser holds the key of its own seat, one entry a seat (%s, %s)" % (a.stored_entries(), b.stored_entries()))
                a.shot(args.shots, "rejoin_reload_before")

                def reload_a(label, latency_ms):
                    """The first player reloads its page (on a connection with `latency_ms` of latency for every request when it is more than 0); returns the watch, A's way back in seconds and the tick before."""
                    before_tick = max(list(a.ticks) + list(b.ticks))
                    doc_before = (a.state() or [None])[0]
                    watch = Watch(server, room, [a, b])
                    if latency_ms:
                        a.call("Network.enable")
                        a.call("Network.emulateNetworkConditions", {"offline": False, "latency": latency_ms, "downloadThroughput": -1, "uploadThroughput": -1})
                    seen_dialog = set()
                    back = [None]
                    reloaded_at = time.time()
                    a.call("Page.reload", {"ignoreCache": False})

                    def a_is_back():
                        s = a.state()
                        if s is not None:
                            seen_dialog.add(s[2])
                            if s[0] != doc_before and s[1] == 1 and s[2] == 0 and back[0] is None:
                                back[0] = time.time() - reloaded_at
                        return back[0] is not None

                    watch.until(a_is_back, args.back_seconds + 10, step=0.2)
                    if latency_ms:
                        a.call("Network.emulateNetworkConditions", {"offline": False, "latency": 0, "downloadThroughput": -1, "uploadThroughput": -1})
                    check(back[0] is not None and back[0] <= args.back_seconds, "%s: the reloaded game is back in the running match %s after the reload (at most %d s)" % (label, "%.1f s" % back[0] if back[0] is not None else "never", args.back_seconds))
                    check(1 not in seen_dialog, "%s: ... and no \"Get ready\" dialog opened on the way (what the probe saw: %s)" % (label, sorted(seen_dialog)))
                    # the match is settled when the server holds nothing any more: no pause, no countdown (a blip leaves no trace in its status but the counter of rejoins)
                    watch.until(lambda: back[0] is not None and bool(watch.status) and not watch.status[-1][1] and watch.status[-1][2] == 0 and watch.tl(b).last == (), 45)
                    return watch, before_tick

                def said(watch):
                    tl_b = watch.tl(b)
                    return (tl_b.first(r"^Ann \(Green\) (lost the connection, waiting \d+:\d\d|is coming back\.\.\. \d+%)$"), tl_b.first(r"^Ann is back: the match goes on in \d+$"), tl_b)

                # a quick reload (everything in the browser's cache): the pause is a blip, which the server ends at once
                watch, before_tick = reload_a("a quick reload", 0)
                missing, countdown, tl_b = said(watch)
                note("a quick reload: the match was paused for about %.1f s; the other screen said: %s" % (watch.paused_for(), tl_b.account()))
                if watch.counted_down():
                    check(countdown is not None, "a quick reload: the pause lasted 3 s or more, so the server held the countdown and the other screen said \"Ann is back: the match goes on in N\" (%s)" % [t for t in tl_b.texts() if "back" in t][:2])
                else:
                    check(countdown is None, "a quick reload: a pause of under 3 s is a blip: the server resumes at once with no countdown, and the other screen counted nothing down")
                check(not any(re.search(WAY_BACK_LINE, line) for line in tl_b.lines()), "a quick reload: the other player's own connection never failed (its screen never said \"Connection lost\")")
                agree_afterwards(watch, a, b, "after the quick reload", before_tick)
                server_says(room, "after the quick reload", state="running", paused=False, rejoins=lambda n: isinstance(n, int) and n >= 1, absent=[])

                # a slow reload (every request of the page takes 1.5 s more: a phone on a bad connection): the seat is away for seconds, the other screen says so, and counts the match back in
                watch, before_tick = reload_a("a slow reload", 1500)
                missing, countdown, tl_b = said(watch)
                note("a slow reload: the match was paused for about %.1f s; the other screen said: %s" % (watch.paused_for(), tl_b.account()))
                check(watch.counted_down(), "a slow reload: the seat was away for about %.1f s, the server held the countdown (its status said resume_seconds above 0)" % watch.paused_for())
                check(missing is not None, "a slow reload: the other player's screen said that the seat was missing (\"Ann (Green) lost the connection, waiting ...\"): %s" % [t for t in tl_b.texts() if "Ann" in t][:3])
                check(countdown is not None, "a slow reload: ... and then \"Ann is back: the match goes on in N\": %s" % [t for t in tl_b.texts() if "back" in t][:3])
                check(missing is not None and countdown is not None and missing < countdown, "a slow reload: ... in that order (%s s, then %s s after the reload began)" % ("%.1f" % missing if missing is not None else "-", "%.1f" % countdown if countdown is not None else "-"))
                check(tl_b.last == (), "a slow reload: the other screen says nothing at the end: the match goes on (the last thing it said: %s)" % (tl_b.texts()[-1:],))
                check(not any(re.search(WAY_BACK_LINE, line) for line in tl_b.lines()), "a slow reload: the other player's own connection never failed")
                agree_afterwards(watch, a, b, "after the slow reload", before_tick)
                server_says(room, "after the slow reload", state="running", paused=False, rejoins=lambda n: isinstance(n, int) and n >= 2, absent=[])
                a.shot(args.shots, "rejoin_reload_after")
                key_nowhere("reload", [a, b])
                clean_consoles("reload", [a, b])
            end_part()

        # ------------------------------------------------------------------------------------------------------------------------------------------------------------
        if wanted("restart"):
            print("[web rejoin] restart: the server is stopped and started again over the same folder: the match is still there, and both players come back by themselves")
            room = new_room()
            a, b, running = begin_match(room)
            check(running, "two players joined %s and the match runs, with no dialog open" % room)
            if running:
                watch = Watch(server, room, [a, b])
                watch.until(lambda: a.ticks and b.ticks, 40)
                status = server_says(room, "before the stop", state="running", reconnect=True)
                check((status.get("record") or {}).get("kept") is True, "the room keeps a restart record (the default holds seats: no option was given) (%s)" % (status.get("record"),))
                before_tick = max(list(a.ticks) + list(b.ticks))
                watch = Watch(server, room, [a, b])
                took = server.stop(signal.SIGTERM)
                check(took is not None and took < 10, "SIGTERM ends the server in %s s (the records are made durable)" % ("%.2f" % took if took is not None else "-"))
                down_since = time.time()
                watch.until(lambda: all(watch.tl(p).first(WAY_BACK_LINE) is not None for p in (a, b)), 20)
                check(all(watch.tl(p).first(WAY_BACK_LINE) is not None for p in (a, b)), "both pages say \"Connection lost. Reconnecting...\" while the server is down: %s | %s" % (watch.tl(a).texts()[:2], watch.tl(b).texts()[:2]))
                check(any("Esc leaves the match" in line for line in watch.tl(a).lines()), "... with the way out under it (\"Esc leaves the match\")")
                a.shot(args.shots, "rejoin_restart_down")
                watch.until(lambda: False, max(0.0, 7 - (time.time() - down_since)))                   # (the game tries every 2 s: it fails a few times before the server is back)
                server.start()
                note("the server is started again over the same folder and ports after %.1f s" % (time.time() - down_since))
                back = [None, None]

                def both_back():
                    for i, p in enumerate((a, b)):
                        if back[i] is None and p.in_match() and not any(re.search(WAY_BACK_LINE, x) for x in (p.overlay() or [])):
                            back[i] = time.time() - down_since
                    return back[0] is not None and back[1] is not None

                watch.until(both_back, 90)
                check(back[0] is not None and back[1] is not None, "both games are back in the match without anybody doing anything (%s s after the stop)" % ["%.0f" % x if x is not None else "never" for x in back])
                # settled: the server holds nothing any more (no pause, no countdown) and both screens are empty
                watch.until(lambda: bool(watch.status) and not watch.status[-1][1] and watch.status[-1][2] == 0 and watch.tl(a).last == () and watch.tl(b).last == (), 45)
                note("after the restart Ann's screen said: %s" % watch.tl(a).account())
                note("after the restart Bob's screen said: %s" % watch.tl(b).account())
                note("the room was paused for about %.1f s after it was restored (the server's clock for a pause begins at the restore: it knows nothing of the time it was down)" % watch.paused_for())
                counted = any(watch.tl(p).first(COUNTDOWN_LINE) is not None for p in (a, b))
                if watch.counted_down():
                    check(counted, "the pause after the restore lasted 3 s or more, so the server held the countdown and a screen counted the match back in (\"... the match goes on in N\"): %s" % [t for p in (a, b) for t in watch.tl(p).texts() if "goes on" in t][:2])
                else:
                    check(not counted, "the players were back within 3 s of the restore (a blip: the server resumes at once, with no countdown) and no screen counted anything down")
                check(watch.tl(a).last == () and watch.tl(b).last == (), "... and when it is over neither screen says anything: the match goes on")
                status = server_says(room, "after the restart", state="running", paused=False, absent=[])
                check(isinstance(status.get("restored"), dict), "the server says that the room came back from its record (%s)" % (status.get("restored"),))
                agree_afterwards(watch, a, b, "after the restart", before_tick)
                key_nowhere("restart", [a, b])
                clean_consoles("restart", [a, b])
            end_part()

        # ------------------------------------------------------------------------------------------------------------------------------------------------------------
        if wanted("rejoin"):
            print("[web rejoin] rejoin: a player who closed the tab finds \"Rejoin your match\" on the front page")
            room = new_room()
            a, b, running = begin_match(room)
            check(running, "two players joined %s and the match runs, with no dialog open" % room)
            if running:
                watch = Watch(server, room, [a, b])
                watch.until(lambda: a.ticks and b.ticks, 40)
                before_tick = max(list(a.ticks) + list(b.ticks))
                watch = Watch(server, room, [b])
                closed_at = time.time()
                a.close_tab()                                                                         # the window is closed: the game does not say goodbye
                watch.until(lambda: watch.tl(b).first(r"^Ann \(Green\) lost the connection, waiting \d+:\d\d$") is not None, 20)
                check(watch.tl(b).first(r"^Ann \(Green\) lost the connection, waiting \d+:\d\d$") is not None, "the match is held for the other player (\"Ann (Green) lost the connection, waiting ...\"): %s" % watch.tl(b).texts()[:2])
                watch.until(lambda: False, max(0.0, 4.5 - (time.time() - closed_at)))                 # (the player is away for a while: 3 s or more is a pause that ends with the countdown)
                a.open_tab()                                                                          # a new tab of the same browser: its storage is where the game left the key
                a.call("Page.navigate", {"url": web})
                wait_for(lambda: a.ev("!!document.getElementById('rejoin')"), 20, 0.3)
                check(a.stored_entries() == ["ants.rejoin.%s.0" % room], "the key was still in the browser's storage after the tab was closed (%s)" % a.stored_entries())
                shown = wait_for(lambda: a.ev("!document.getElementById('rejoin').hidden"), 20, 0.3)
                check(bool(shown), "the front page of the site shows the Rejoin block")
                if shown:
                    info = a.ev("""(function () { var go = document.getElementById('rejoin-go'), note = document.getElementById('rejoin-note'), r = go.getBoundingClientRect(), cards = document.getElementById('cards').getBoundingClientRect();
                        return { button: go.textContent, note: note.textContent, bottom: r.bottom, cards: cards.top, enabled: !go.disabled }; })()""")
                    check(info["button"] == "Rejoin your match (%s)" % room, "its button says \"Rejoin your match (CODE)\" with the room's code (%r)" % info["button"])
                    check(info["note"] == "Your match in room %s is still running: go back to your seat." % room, "... and the line under it says that the match is still running (%r)" % info["note"])
                    check(info["bottom"] <= info["cards"] and info["enabled"], "... above the two cards (the button ends at %.0f px, the cards begin at %.0f px)" % (info["bottom"], info["cards"]))
                    a.shot(args.shots, "rejoin_button")
                    pages = lambda: len([t for t in a.browser.devtools.call("Target.getTargets")["targetInfos"] if t["type"] == "page"])
                    before_pages = pages()
                    x, y = a.ev("(function () { var r = document.getElementById('rejoin-go').getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2]; })()")
                    a.call("Input.dispatchMouseEvent", {"type": "mouseMoved", "x": x, "y": y, "button": "none"})
                    time.sleep(0.15)
                    pressed_at = time.time()
                    a.call("Input.dispatchMouseEvent", {"type": "mousePressed", "x": x, "y": y, "button": "left", "clickCount": 1})
                    a.call("Input.dispatchMouseEvent", {"type": "mouseReleased", "x": x, "y": y, "button": "left", "clickCount": 1})
                    went = wait_for(lambda: "join=" in (a.ev("location.search") or ""), 15, 0.2)
                    check(bool(went), "pressing it takes this tab to the game page")
                    check(pages() == before_pages, "... in the same tab (no new tab or window: %d page(s) before and after)" % before_pages)
                    search = a.ev("location.search") or ""
                    given = wait_for(lambda: a.ev("typeof ANTS_ARGS !== 'undefined' ? ANTS_ARGS : null"), 10, 0.2) or []
                    check(search == "?join=/ws&room=%s&seat=0&aspect=16:9" % room, "the game page's address is the room, the seat and the shape, and no key (it drops the name parameter itself, so that a copy of the address asks its owner): %s" % search)
                    check(["--join-url", "ws://" + web.split("//", 1)[1].rstrip("/") + "/ws", "--room", room, "--seat", "0"] == given[given.index("--join-url"):given.index("--join-url") + 6] if "--join-url" in given else False,
                          "... and the game is given the site's door, the room and the seat 0 (its arguments: %s)" % [x for x in given if x != "./this.program"])
                    back = [None]
                    seen_dialog = set()
                    watch = Watch(server, room, [a, b])

                    def a_is_back():
                        s = a.state()
                        if s is not None:
                            seen_dialog.add(s[2])
                            if s[1] == 1 and s[2] == 0 and back[0] is None:
                                back[0] = time.time() - pressed_at
                        return back[0] is not None

                    watch.until(a_is_back, args.ready_timeout + 30, step=0.2)
                    check(back[0] is not None, "the player is back in the running match %s after pressing the button" % ("%.1f s" % back[0] if back[0] is not None else "never"))
                    check(1 not in seen_dialog, "... with no \"Get ready\" dialog (what the probe saw: %s)" % sorted(seen_dialog))
                    watch.until(lambda: watch.tl(b).first(r"^Ann is back: the match goes on in \d+$") is not None and watch.tl(b).last == (), 40)
                    check(watch.tl(b).first(r"^Ann is back: the match goes on in \d+$") is not None, "the other player's screen says \"Ann is back: the match goes on in N\": %s" % [t for t in watch.tl(b).texts() if "back" in t][:3])
                    note("the other player's screen said: %s" % watch.tl(b).account())
                    check(watch.tl(b).last == (), "... and says nothing at the end: the match goes on")
                    agree_afterwards(watch, a, b, "after the Rejoin button", before_tick)
                    server_says(room, "after the Rejoin button", state="running", paused=False, rejoins=lambda n: isinstance(n, int) and n >= 1, absent=[])
                    a.shot(args.shots, "rejoin_back")
                    key_nowhere("rejoin", [a, b])
                    clean_consoles("rejoin", [a, b])
            end_part()

        # ------------------------------------------------------------------------------------------------------------------------------------------------------------
        if wanted("leave"):
            print("[web rejoin] leave: a player who leaves a match on purpose (the page's Menu button, confirmed) drops the seat at once: nobody waits for it, no key is left")
            room = new_room()
            a, b, running = begin_match(room)
            check(running, "two players joined %s and the match runs, with no dialog open" % room)
            if running:
                watch = Watch(server, room, [a, b])
                watch.until(lambda: a.ticks and b.ticks, 40)
                check(b.stored_entries() == ["ants.rejoin.%s.1" % room], "the second player holds the key of its seat (%s)" % b.stored_entries())
                held = {a: a.key_hexes(), b: b.key_hexes()}                                       # (to look for them afterwards: the one who leaves holds none then)
                before = len(b.dialogs)
                watch = Watch(server, room, [a])
                b.ev("document.getElementById('menu-btn').click(); 1")                             # the page's Menu button (the check says yes to its question: Player accepts every dialog)
                went = wait_for(lambda: b.ev("location.pathname") == "/" and "join=" not in (b.ev("location.search") or ""), 20, 0.2)
                check(bool(went), "the second player's tab goes to the front page of the site")
                check(b.dialogs[before:] == ["Leave the game and go back to the menu?"], "... after the page asked first (%s)" % b.dialogs[before:])
                watch.until(lambda: False, 12)                                                      # (a seat that is held shows within 3 s: the link closes with the page, the room pauses for it)
                states = [(paused, absent) for _, paused, _, absent in watch.status]
                check(bool(states) and not any(paused or absent for paused, absent in states), "the server never paused the room for that seat: it was dropped at once, not held (%d looks, %d of them with a pause or a seat missing)" % (len(states), len([1 for paused, absent in states if paused or absent])))
                status = server.room(room) or {}
                note("the room after the second player left: state %s, paused %s, absent %s, %s turns" % (status.get("state"), status.get("paused"), status.get("absent"), status.get("turns")))
                check(not any(re.search(r"lost the connection|is coming back|Connection lost", line) for line in watch.tl(a).lines()), "the first player's screen never said that a connection was lost (it said: %s)" % watch.tl(a).account())
                wait_for(lambda: b.ev("!!document.getElementById('rejoin')"), 20, 0.3)
                time.sleep(0.5)
                check(b.stored_entries() == [], "the key is gone from the second player's storage (%s)" % b.stored_entries())
                check(bool(b.ev("document.getElementById('rejoin').hidden")), "and the front page offers no Rejoin for it")
                key_nowhere("leave", [a, b], held)
                clean_consoles("leave", [a, b])
            end_part()

        # ------------------------------------------------------------------------------------------------------------------------------------------------------------
        if wanted("none"):
            print("[web rejoin] none: the front page shows no Rejoin without a usable entry")
            p = Player(chrome, "Zed", 0, web, args.ready_timeout)
            players.append(p)
            host = re.sub(r"^https?://", "", web).rstrip("/")
            mine = ("wss://" if web.startswith("https://") else "ws://") + host + "/ws"
            hexkey = secrets.token_hex(16)

            def entry(age_ms=5000, server_text=mine, key=hexkey):
                return json.dumps({"k": key, "s": server_text, "t": int(time.time() * 1000) - age_ms}, separators=(",", ":"))

            def load_front(items, width=1280):
                """Opens the front page with exactly these items in the storage (a clean slate first); what the page shows of the Rejoin and where things stand."""
                p.resize(width, 900)
                p.call("Page.navigate", {"url": web})
                wait_for(lambda: p.ev("!!document.getElementById('rejoin')"), 20, 0.2)
                p.ev("localStorage.clear(); 1")
                for k, v in items.items():
                    p.ev("localStorage.setItem(%s, %s); 1" % (json.dumps(k), json.dumps(v)))
                p.call("Page.navigate", {"url": web})
                wait_for(lambda: p.ev("!!document.getElementById('rejoin') && !document.getElementById('cards').hidden"), 20, 0.2)
                time.sleep(0.4)
                return p.ev("""(function () { var r = document.getElementById('rejoin'), go = document.getElementById('rejoin-go'), header = document.querySelector('header.top').getBoundingClientRect(),
                    main = document.querySelector('main.page').getBoundingClientRect();
                    return { hidden: r.hidden, shown: r.getBoundingClientRect().height > 0, button: go.textContent, names: Object.keys(localStorage).filter(function (k) { return k.indexOf('ants.rejoin.') === 0; }).sort(),
                             scroll: [document.documentElement.scrollWidth, window.innerWidth], gap: main.top - header.bottom }; })()""")

            plain = load_front({})
            check(plain["hidden"] and not plain["shown"] and plain["button"] == "", "no entry: nothing of the Rejoin is shown (the block is hidden and takes no room)")
            check(plain["gap"] == 0, "... and nothing else moved: the page starts right under the header (a gap of %s px)" % plain["gap"])
            old = load_front({"ants.rejoin.old-room.0": entry(age_ms=4 * 3600 * 1000)})
            check(old["hidden"] and old["names"] == [], "an entry older than 3 hours: not offered, and removed from the storage (%s)" % old["names"])
            other = load_front({"ants.rejoin.other-room.1": entry(server_text="wss://other.example/ws")})
            check(other["hidden"] and other["names"] == ["ants.rejoin.other-room.1"], "an entry of another server: not offered, and left in the storage (%s)" % other["names"])
            scheme = load_front({"ants.rejoin.scheme-room.1": entry(server_text=("ws://" if mine.startswith("wss://") else "wss://") + host + "/ws")})
            check(scheme["hidden"] and scheme["names"] == ["ants.rejoin.scheme-room.1"], "an entry of this host with the other scheme is another server's too")
            bad = load_front({"ants.rejoin.junk-room.0": "not json", "ants.rejoin.bad-seat.7": entry(), "ants.rejoin.a.b.0": entry(), "ants.rejoin.zero-key.2": entry(key="0" * 32),
                              "ants.rejoin.extra.1": json.dumps({"k": hexkey, "s": mine, "t": int(time.time() * 1000), "v": 2}), "ants.rejoin.ahead.3": entry(age_ms=-3600 * 1000)})
            check(bad["hidden"] and len(bad["names"]) == 6, "malformed entries, a bad seat, a room with a dot, a key of zeros, an extra member and a time an hour ahead: nothing is offered and nothing is removed (%d kept)" % len(bad["names"]))
            good = load_front({"ants.rejoin.demo-small-2p-abc123.2": entry(), "ants.rejoin.older-room.0": entry(age_ms=600000), "ants.name": "Zed", "ants.aspect.v2": "4:3"})
            check(not good["hidden"] and good["shown"] and good["button"] == "Rejoin your match (demo-small-2p-abc123)", "a fresh entry of this site: the button is there, for the newest of two entries (%r)" % good["button"])
            check(good["gap"] > 40, "... it takes its own room above the page (%s px under the header) and the page moves down" % good["gap"])
            p.shot(args.shots, "rejoin_front_with_button")
            p.ev("document.getElementById('rejoin-go').click(); 1")
            wait_for(lambda: "join=" in (p.ev("location.search") or ""), 10, 0.2)
            search = p.ev("location.search") or ""
            given = wait_for(lambda: p.ev("typeof ANTS_ARGS !== 'undefined' ? ANTS_ARGS : null"), 10, 0.2) or []
            words = [x for x in given if x != "./this.program"]
            check(search == "?join=/ws&room=demo-small-2p-abc123&seat=2&aspect=4:3" and hexkey not in search, "the button's address names the room, the seat 2 and the remembered shape (4:3), and no key: %s" % search)
            check("--join-url" in given and given[given.index("--join-url") + 1] == mine and given[given.index("--room") + 1] == "demo-small-2p-abc123" and given[given.index("--seat") + 1] == "2" and given[given.index("--name") + 1] == "Zed",
                  "... and the game is given the site's door, the room, the seat and the remembered name (its arguments: %s)" % words)
            gone = load_front({})
            check(gone["hidden"], "with the entry gone the front page shows no Rejoin")
            long_code = "r" * 32
            phone = load_front({"ants.rejoin.%s.3" % long_code: entry()}, width=390)
            check(not phone["hidden"] and phone["button"] == "Rejoin your match (%s)" % long_code and phone["scroll"][0] <= phone["scroll"][1], "at 390 px with a room code of 32 characters the button is there and the page does not scroll sideways (%s)" % (phone["scroll"],))
            p.shot(args.shots, "rejoin_front_phone")
            end_part()
    except NotReachable as e:
        print("  SKIP: %s" % e)
        return 3
    except Exception as e:                                                   # noqa: BLE001  (a page or a server that breaks down is a failure, never a skip)
        print("  FAIL: the check broke down: %s: %s" % (type(e).__name__, e))
        if server.process is not None:
            print("  (the server's log: %s)" % server.tail())
        failures.append("the check broke down: %s" % e)
    finally:
        end_part()
        server.stop(signal.SIGTERM)
        if args.keep_logs:
            os.makedirs(args.keep_logs, exist_ok=True)
            try:
                shutil.copy(server.log_path, os.path.join(args.keep_logs, "rejoin_check_server.log"))
            except OSError:
                pass
        shutil.rmtree(work, ignore_errors=True)
    print("[web rejoin] %d checks, %d failed, %.0f s" % (count[0], len(failures), time.time() - began_all))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
