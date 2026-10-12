#!/usr/bin/env python3
"""The players' maps in a REAL browser (opt-in; see tests/scripts/test_web_map_fetch.sh and docs/PLAY_IN_BROWSER.md "Players' maps in the browser").

The game's file system holds the six originals only. A room that plays a map of the community must therefore have the file fetched into every browser before the match loads. Only a browser shows that
the whole chain works: the game (WebAssembly) asks the page, the page fetches /maps/files/<name> through the site's nginx, checks the FNV-1a 64 against the hash of the room's Start, writes the file where
the game looks, and the game loads it and plays the same match as the other browser.

Needs the web image of this tree served by nginx (tools/web_without_docker.py builds and serves it without Docker: `tools/web_without_docker.py -- tests/scripts/test_web_map_fetch.sh`), the native
`ants_server` of this tree (--server), a Chromium-based browser and Python 3 (the DevTools protocol is spoken with the client of web_hidden_check.py, standard library only). The check starts the
server itself, with the options of docker-compose.stack.yml (and a folder of maps of its own), and puts a small front door of its own before the site: /ws and the server's public routes (/live,
/replays, /maps, /busy, /stats) go to the server, everything else to the site's nginx, which is what the site's nginx does in the image (here it has no server to ask: no Docker network). Parts (--only NAME):

  * match     a room of the server on a community map that the site's /maps/files has; two browsers (Ann leads and presses START by itself with &start=2) join: the match runs in both, both file systems hold
              the file under the maps folder, each page made exactly ONE download of it, the two games' state hashes agree, and a third browser that watches the match LIVE (a recording of a community
              map) shows it: its game has the file too and plays the recording;
  * missing   the room's map is on the server but not on the site: both pages say that the map is not on the server, the match does not start for anybody (and the games stay in the room); the file is then put
              on the site, the leader's next START (the page presses it every second) works and the match runs with equal hashes;
  * differs   the server's copy of the map is another file than the site's under the same name: both pages say that it is not the host's copy, the match does not start, and no browser wrote the file.

Exit status 0: every check passed; 1: a check failed, or the browser, a page or the server broke down; 3: the check could not be made because the environment is not there; 2: a bad command line.
"""
import argparse
import asyncio
import json
import os
import re
import secrets
import shutil
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from web_hidden_check import find_browser, free_port                          # noqa: E402
from web_rejoin_check import Player, Server, wait_for, REPO                    # noqa: E402

PARTS = ("match", "missing", "differs")
MAPS_FOLDER = "Original-Ants/Maps"
SERVER_DOOR = (b"/live", b"/replays", b"/maps", b"/busy", b"/stats", b"/history")


def community_map(name):
    return os.path.join(REPO, "Community-Maps", name)


class Front:
    """The front door of the check: one TCP port that sends a request to the site's nginx or to the game server by its path (the image's nginx does the same with its proxy rules)."""

    def __init__(self, site_port, ws_port, door_port):
        self.site, self.ws, self.door = site_port, ws_port, door_port
        self.port = free_port()
        self.loop = asyncio.new_event_loop()
        self.ready = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.requests = []                                                   # the paths that went to the site
        self.maps_down = False                                               # True: the list of maps (GET /maps) is not offered (502), as when the game server is away
        self.thread.start()
        self.ready.wait(10)

    def run(self):
        asyncio.set_event_loop(self.loop)
        server = self.loop.run_until_complete(asyncio.start_server(self.handle, "127.0.0.1", self.port))
        self.ready.set()
        try:
            self.loop.run_forever()
        finally:
            server.close()

    def stop(self):
        self.loop.call_soon_threadsafe(self.loop.stop)

    async def handle(self, reader, writer):
        try:
            head = await reader.readuntil(b"\r\n\r\n")
        except (asyncio.IncompleteReadError, asyncio.LimitOverrunError, ConnectionError):
            writer.close()
            return
        first = head.split(b"\r\n", 1)[0].split(b" ")
        path = first[1] if len(first) > 1 else b"/"
        bare = path.split(b"?")[0]
        upgrade = b"upgrade: websocket" in head.lower()
        if self.maps_down and bare == b"/maps":
            writer.write(b"HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
            writer.close()
            return
        if bare == b"/ws" or bare.startswith(b"/ws/"):
            port = self.ws
        elif any(bare == p or bare.startswith(p + b"/") for p in SERVER_DOOR) and not bare.startswith((b"/maps/files", b"/maps/previews")):
            port = self.door
        else:
            port = self.site
            self.requests.append(urllib.parse.unquote(path.decode("latin-1")))
        if not upgrade:                                                      # one request on each connection: the next one may go elsewhere
            lines = [l for l in head.split(b"\r\n")[:-2] if not l.lower().startswith(b"connection:")]
            head = b"\r\n".join(lines + [b"Connection: close", b"", b""])
        try:
            up_reader, up_writer = await asyncio.open_connection("127.0.0.1", port)
        except OSError:
            writer.write(b"HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
            writer.close()
            return
        up_writer.write(head)

        async def pipe(src, dst):
            try:
                while True:
                    data = await src.read(65536)
                    if not data:
                        break
                    dst.write(data)
                    await dst.drain()
            except (ConnectionError, asyncio.CancelledError):
                pass
            finally:
                try:
                    dst.close()
                except Exception:                                            # noqa: BLE001
                    pass
        await asyncio.gather(pipe(reader, up_writer), pipe(up_reader, writer))


class MapPlayer(Player):
    """A player whose room is made by the server's control interface (the room's map is a community map, which no address can name): the game page joins by the code."""

    def game_url(self, room):
        return "%s?join=/ws&room=%s&seat=%d&name=%s&aspect=16:9&start=2" % (self.web, room, self.seat, self.name)

    def watch_url(self, live_id):
        return "%splay.html?live=%s&aspect=16:9" % (self.web, live_id)


def toast_of(player):
    return player.ev("(function () { var t = document.getElementById('toast'); return t && !t.hidden ? t.textContent : ''; })()") or ""


def file_in_game(player, name):
    return bool(player.ev("(function () { try { return Module.FS.analyzePath(%s).exists; } catch (e) { return false; } })()" % json.dumps(MAPS_FOLDER + "/" + name)))


def downloads(player, name):
    """How many times this document asked the site for the map file (the browser's resource timing: a cache hit counts too, a request that never left the page does not)."""
    suffix = "/maps/files/" + urllib.parse.quote(name, safe="-._~")
    return player.ev("performance.getEntriesByType('resource').filter(function (e) { return e.name.indexOf(%s) >= 0; }).length" % json.dumps(suffix))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--web", required=True, help="the site's nginx, e.g. http://127.0.0.1:19980/ (the web image of this tree; it needs no game server)")
    ap.add_argument("--store", required=True, help="the folder that the site's /maps/files serves (tools/web_without_docker.py: WORK/fs/srv/community-maps/files)")
    ap.add_argument("--server", default=os.environ.get("ANTS_SERVER_BIN", os.path.join(REPO, "build", "src", "ants_server", "ants_server")), help="the ants_server program of this tree")
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--only", default="", help="run only the parts whose name contains this text (%s)" % ", ".join(PARTS))
    ap.add_argument("--ready-timeout", type=float, default=120.0, metavar="SECONDS", help="how long a page may take to get its game ready (default 120)")
    ap.add_argument("--keep-logs", default="", help="a folder to copy the server's log to when the check ends")
    args = ap.parse_args()
    site = args.web if args.web.endswith("/") else args.web + "/"
    failures = []
    count = [0]

    def check(ok, what):
        count[0] += 1
        print("  %s: %s" % ("ok  " if ok else "FAIL", what), flush=True)
        if not ok:
            failures.append(what)

    def wanted(name):
        return not args.only or args.only in name

    chrome = find_browser(args.browser)
    if not chrome:
        print("  SKIP: no Chromium-based browser found (give one with --browser or CHROME)")
        return 3
    if not os.path.isfile(args.server) or not os.access(args.server, os.X_OK):
        print("  SKIP: the game server program is not there (build the target ants_server, or give --server)")
        return 3
    if not os.path.isdir(args.store):
        print("  SKIP: the site's folder of map files is not there (%s)" % args.store)
        return 3
    try:
        urllib.request.urlopen(site, timeout=10).read(64)
    except (OSError, urllib.error.URLError) as e:
        print("  SKIP: nothing answers at %s (%s)" % (site, e))
        return 3

    work = tempfile.mkdtemp(prefix="ants_map_fetch_check.")
    maps = os.path.join(work, "maps")
    os.makedirs(maps)
    for f in os.listdir(os.path.join(REPO, "Original-Ants", "Maps")):
        shutil.copy(os.path.join(REPO, "Original-Ants", "Maps", f), maps)
    MATCH, MISSING, DIFFERS = "Bomb Island.lvl", "Only on the server.lvl", "thief craker2.0.lvl"
    shutil.copy(community_map(MATCH), maps)                                  # the site has these two (the image copies Community-Maps)
    shutil.copy(community_map("Bomb Island.lvl"), os.path.join(maps, MISSING))      # the site does not have this one: it is put there in the middle of "missing"
    with open(community_map(DIFFERS), "rb") as f:
        data = bytearray(f.read())
    with open(os.path.join(maps, DIFFERS), "wb") as f:                       # the server's copy is not the site's: a text of the file that the game does not use is another
        data.extend(b"\r\n")
        f.write(bytes(data))
    for name in (MATCH, DIFFERS):
        if not os.path.isfile(os.path.join(args.store, name)):
            print("  SKIP: the site does not serve %s (%s)" % (name, args.store))
            shutil.rmtree(work, ignore_errors=True)
            return 3
    if os.path.exists(os.path.join(args.store, MISSING)):
        os.remove(os.path.join(args.store, MISSING))

    ws_port, door_port = free_port(), free_port()
    server = Server(args.server, maps, ws_port, work)
    server.options = server.options + ["--replay-port", str(door_port)]
    front = None
    players = []
    try:
        server.start()
        site_port = int(re.search(r":(\d+)", site.split("//", 1)[1]).group(1))
        front = Front(site_port, ws_port, door_port)
        web = "http://127.0.0.1:%d/" % front.port

        def make_room(code, name):
            body = json.dumps({"code": code, "map": name, "players": 2, "early_start": True, "reconnect": True}).encode()
            request = urllib.request.Request("http://127.0.0.1:%d/rooms" % server.ctl_port, data=body, method="POST",
                                             headers={"Authorization": "Bearer " + server.secret, "Content-Type": "application/json"})
            with urllib.request.urlopen(request, timeout=10) as r:
                return json.load(r)

        def two_players():
            a = MapPlayer(chrome, "Ann", 0, web, args.ready_timeout)
            b = MapPlayer(chrome, "Bob", 1, web, args.ready_timeout)
            players.extend([a, b])
            return a, b

        def hashes_agree(a, b, need=2, limit=60):
            deadline = time.time() + limit
            common = []
            while time.time() < deadline:
                a.syncs()
                b.syncs()
                common = [(a.ticks[t], b.ticks[t]) for t in sorted(a.ticks) if t in b.ticks]
                if len(common) >= need:
                    break
                time.sleep(0.5)
            return len(common) >= need and all(x == y for x, y in common) and not a.conflicts and not b.conflicts

        a, b = two_players()

        if wanted("match"):
            print("part match", flush=True)
            room = "mapmatch"
            make_room(room, MATCH)
            a.open_game(room)
            b.open_game(room)
            ran = wait_for(lambda: a.in_match() and b.in_match(), 90, 0.5)
            check(bool(ran), "the match on %s runs in both browsers" % MATCH)
            check(file_in_game(a, MATCH) and file_in_game(b, MATCH), "both games' file systems hold %s/%s" % (MAPS_FOLDER, MATCH))
            check(downloads(a, MATCH) == 1 and downloads(b, MATCH) == 1, "each page made one download of the map (%s, %s)" % (downloads(a, MATCH), downloads(b, MATCH)))
            check(hashes_agree(a, b), "the two games' state hashes agree at every tick they both reported")
            status = server.room(room) or {}
            check(status.get("state") == "running", "the server's room is running (%s)" % status.get("state"))
            # a third browser watches the match live: a recording of a community map
            live_id = wait_for(lambda: next((m["id"] for m in json.load(urllib.request.urlopen("http://127.0.0.1:%d/live" % door_port, timeout=5)).get("live", []) if m.get("map") == MATCH), None), 120, 2.0)
            if not live_id:
                print("  note: the match has not been on the live board for 120 s (a match is listed after it has run 30 s)")
            check(bool(live_id), "the match is on the server's live board")
            if live_id:
                w = MapPlayer(chrome, "Watcher", 0, web, args.ready_timeout)
                players.append(w)
                w.call("Page.navigate", {"url": w.watch_url(live_id)})
                ready = wait_for(lambda: w.ev("!!window.isReadyToPlay"), args.ready_timeout, 0.5)
                check(bool(ready), "the watcher's game is ready")
                check(bool(wait_for(lambda: file_in_game(w, MATCH), 20, 0.5)), "the watcher's file system holds the map of the recording")
                state = wait_for(lambda: w.ev("Module._ants_replay_get ? Module._ants_replay_get(0) : -1") in (2, 3), 30, 0.5)       # ReplayState Playing or Paused (7 is a recording that cannot be shown)
                check(bool(state), "the watcher's game plays the recording (replay state %s)" % w.ev("Module._ants_replay_get(0)"))
                check(downloads(w, MATCH) == 1, "the watcher's page made one download of the map")
                w.close()
                players.remove(w)
            for p in (a, b):
                p.call("Page.navigate", {"url": "about:blank"})

        if wanted("missing"):
            print("part missing", flush=True)
            room = "mapmissing"
            make_room(room, MISSING)
            a.open_game(room)
            b.open_game(room)
            said = wait_for(lambda: "not on the server" in toast_of(a) and "not on the server" in toast_of(b), 90, 0.5)
            check(bool(said), "both pages say that the map is not on the server (%r, %r)" % (toast_of(a)[:80], toast_of(b)[:80]))
            check(not a.in_match() and not b.in_match(), "the match did not start for anybody")
            check(not file_in_game(a, MISSING) and not file_in_game(b, MISSING), "no game holds a file it could not have")
            status = server.room(room) or {}
            check(status.get("state") in ("waiting", "loading"), "the server's room has not started a match (%s)" % status.get("state"))
            shutil.copy(os.path.join(maps, MISSING), os.path.join(args.store, MISSING))            # the file is on the site now: the leader's next START (every second) works
            ran = wait_for(lambda: a.in_match() and b.in_match(), 90, 0.5)
            check(bool(ran), "after the file is on the site the next START runs the match in both browsers")
            check(file_in_game(a, MISSING) and file_in_game(b, MISSING), "both games' file systems hold it now")
            check(hashes_agree(a, b), "the two games' state hashes agree")
            for p in (a, b):
                p.call("Page.navigate", {"url": "about:blank"})
            os.remove(os.path.join(args.store, MISSING))

        if wanted("differs"):
            print("part differs", flush=True)
            room = "mapdiffers"
            make_room(room, DIFFERS)
            a.open_game(room)
            b.open_game(room)
            said = wait_for(lambda: "not the host's copy" in toast_of(a) and "not the host's copy" in toast_of(b), 90, 0.5)
            check(bool(said), "both pages say that the map is not the host's copy (%r, %r)" % (toast_of(a)[:80], toast_of(b)[:80]))
            time.sleep(3)
            check(not a.in_match() and not b.in_match(), "the match did not start for anybody")
            check(not file_in_game(a, DIFFERS) and not file_in_game(b, DIFFERS), "no browser wrote the other file")
            status = server.room(room) or {}
            check(status.get("state") in ("waiting", "loading"), "the server's room has not started a match (%s)" % status.get("state"))

        bad = [(p.name, t) for p in players for _, level, t in p.console if level == "exception"]
        check(not bad, "no page threw an exception (%s)" % bad[:3])
    except Exception as e:                                                   # noqa: BLE001
        failures.append("the check broke: %r" % (e,))
        print("  FAIL: the check broke: %r (server: %s)" % (e, server.tail()), flush=True)
    finally:
        for p in players:
            try:
                p.close()
            except Exception:                                                # noqa: BLE001
                pass
        if front:
            front.stop()
        server.stop()
        if args.keep_logs:
            os.makedirs(args.keep_logs, exist_ok=True)
            shutil.copy(server.log_path, args.keep_logs)
        shutil.rmtree(work, ignore_errors=True)
        for name in ("Only on the server.lvl",):
            if os.path.exists(os.path.join(args.store, name)):
                os.remove(os.path.join(args.store, name))
    print("%d checks, %d failed" % (count[0], len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
