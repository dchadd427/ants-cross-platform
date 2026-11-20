"""A stand-in for the game server's admin door, for the tests of the hub's `/bots` (hub/gamedoor.py; docs/HUB_COMMANDS.md, "The game door"): a small HTTP server on a loopback port, in a thread of its own,
that checks every call the way the game server does (the pass, with the secret, written here from tests/scripts/admin_pass.py and not from the hub's code; the token of a POST; the shape of the
request) and keeps what it was sent. A call that is not what the game server accepts is answered 403 and is named in `problems`, which a test asserts to be empty.

`routes` maps a tail (`bot-options`, `bot-matches`, `bot-matches/summary`, `bot-matches/stop-all`) to a (status, object) or to a function of the Call that returns one; `mode` makes it misbehave:
`down` (the connection is closed with no answer), `hang` (it never answers), `garbage`, `list`, `big` (an answer of 130 KiB, more than the 128 KiB that the hub reads), `redirect`, `chunked_garbage`, `short` (an answer that ends before its length),
`status_line` (not HTTP). `delay` waits before an answer. Nothing here is a copy of the hub's code: a test of the hub must not share the code it checks.
"""
import base64
import collections
import hashlib
import hmac
import json
import re
import socketserver
import threading
import time

import admin_pass

SECRET = admin_pass.TEST_SECRET
Call = collections.namedtuple("Call", "method tail headers body payload passed raw")
MAPS = [{"file": "TINY.LVL", "name": "TINY", "playable": True}, {"file": "SMALL.LVL", "name": "SMALL", "playable": True}, {"file": "ISLANDS.LVL", "name": "ISLANDS", "playable": True},
        {"file": "Super jungle.lvl", "name": "Super jungle", "playable": True}, {"file": "Jungle2000.lvl", "name": "Jungle2000", "playable": True},
        {"file": "Broken One.lvl", "name": "Broken One", "playable": False}, {"file": "sand-box.lvl", "name": "sand-box", "playable": True}]


def unb64(text):
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        door = self.server.door
        data = b""
        while b"\r\n\r\n" not in data and len(data) < 65536:
            chunk = self.request.recv(4096)
            if not chunk:
                return
            data += chunk
        head, _, rest = data.partition(b"\r\n\r\n")
        lines = head.decode("latin-1").split("\r\n")
        method, path, _ = lines[0].split(" ", 2)
        headers = {}
        for line in lines[1:]:
            name, _, value = line.partition(":")
            headers[name.strip().lower()] = value.strip()
        length = int(headers.get("content-length", "0") or 0)
        while len(rest) < length:
            chunk = self.request.recv(4096)
            if not chunk:
                break
            rest += chunk
        tail = path[len("/admin/api/"):] if path.startswith("/admin/api/") else path
        try:
            payload = json.loads(rest.decode("utf-8")) if rest else None
        except ValueError:
            payload = None
        call = Call(method, tail, headers, rest, payload, None, data)
        passed = door.check(call)
        call = call._replace(passed=passed)
        with door.lock:
            door.calls.append(call)
        self.respond(door, call, passed)

    def send(self, status, body, extra=""):
        self.request.sendall(("HTTP/1.1 %d X\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n%s\r\n" % (status, len(body), extra)).encode("ascii") + body)

    def respond(self, door, call, passed):
        mode = door.mode
        if door.delay:
            time.sleep(door.delay)
        if mode == "down":
            return
        if mode == "hang":
            time.sleep(30)
            return
        if mode == "garbage":
            return self.send(200, b"<html>not json</html>")
        if mode == "list":
            return self.send(200, b"[1, 2, 3]")
        if mode == "big":
            return self.send(200, json.dumps({"maps": [], "pad": "x" * 600000}).encode("ascii"))
        if mode == "redirect":
            return self.send(302, b"{}", "Location: http://127.0.0.1:1/x\r\n")
        if mode == "chunked_garbage":
            return self.request.sendall(b"HTTP/1.1 200 X\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\nzz\r\nhello\r\n0\r\n\r\n")
        if mode == "short":
            return self.request.sendall(b"HTTP/1.1 200 X\r\nContent-Length: 500\r\nConnection: close\r\n\r\n{\"started\"")
        if mode == "status_line":
            return self.request.sendall(b"SPDY/3 200 maybe\r\n\r\n{}")
        if mode == "double_length":
            return self.request.sendall(b"HTTP/1.1 200 X\r\nContent-Length: 2\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}")
        if not passed:
            return self.send(403, b'{"error":"pass"}')
        route = door.routes.get(call.tail)
        status, obj = route(call) if callable(route) else route if route is not None else door.default(call)
        self.send(status, json.dumps(obj).encode("utf-8"))


class FakeGameDoor:
    def __init__(self, secret=SECRET, now=time.time):
        self.secret = secret
        self.now = now
        self.calls = []
        self.problems = []
        self.nonces = set()
        self.routes = {}
        self.mode = "ok"
        self.delay = 0.0
        self.lock = threading.Lock()
        self.server = socketserver.ThreadingTCPServer(("127.0.0.1", 0), Handler)
        self.server.daemon_threads = True
        self.server.block_on_close = False
        self.server.door = self
        self.port = self.server.server_address[1]
        self.url = "http://127.0.0.1:%d" % self.port
        self.thread = threading.Thread(target=self.server.serve_forever, kwargs={"poll_interval": 0.05}, daemon=True)
        self.thread.start()

    def stop(self):
        self.server.shutdown()
        self.server.server_close()

    def default(self, call):
        if call.tail == "bot-options":
            return 200, {"maps": MAPS, "checking": 0, "random_max_minutes": 30, "untrusted": True, "untrusted_fields": ["name"]}
        if call.tail == "bot-matches" and call.method == "POST" and isinstance(call.payload, dict):
            count = call.payload.get("count", 1)
            started = min(count, 80)
            answer = {"started": started, "asked": count, "queued": count - started}
            if call.payload.get("every_s"):
                answer["every_s"] = call.payload["every_s"]
            return 200, answer
        if call.tail == "bot-matches/summary":
            return 200, {"at_once": 80, "running": 3, "waiting": 7, "busy": 31, "gate": 40, "queues": []}
        if call.tail == "bot-matches/stop-all":
            return 200, {"stopped": 2, "dropped": 5}
        return 404, {"error": "not_found"}

    def problem(self, text):
        with self.lock:
            self.problems.append(text)

    def check(self, call):
        """The payload of the pass when the call is one that the game server would take, else None (and a problem is kept)."""
        h = call.headers
        header = h.get("x-ants-admin-pass", "")
        parts = header.split(".")
        if len(parts) != 3 or parts[0] != "v1":
            self.problem("the pass is not v1.<payload>.<mac>")
            return None
        mac = hmac.new(admin_pass.pass_key(self.secret), ("v1." + parts[1]).encode("ascii"), hashlib.sha256).hexdigest()
        if not hmac.compare_digest(mac, parts[2]):
            self.problem("the mac of the pass is wrong")
            return None
        payload = json.loads(unb64(parts[1]).decode("utf-8"))
        now = int(self.now())
        if set(payload) != {"exp", "name", "nonce", "ref", "role"}:
            self.problem("the pass has members %s" % sorted(payload))
        if not (type(payload.get("exp")) is int and now < payload["exp"] <= now + 60):
            self.problem("the pass's life is not 60 seconds at the most: exp %r, now %d" % (payload.get("exp"), now))
        if not re.fullmatch(r"[0-9a-f]{16}", str(payload.get("nonce"))) or payload.get("nonce") in self.nonces:
            self.problem("the nonce is not fresh and of 16 hex characters")
        self.nonces.add(payload.get("nonce"))
        if payload.get("role") not in ("admin", "owner") or not re.fullmatch(r"[0-9a-f]{16}", str(payload.get("ref"))) or not isinstance(payload.get("name"), str):
            self.problem("the role, the ref or the name of the pass is not of the shape")
        post = call.method == "POST"
        token = h.get("x-ants-csrf")
        want = hmac.new(admin_pass.derived_key(self.secret, "ants-admin-csrf-server-v1"), b"csrf1\0" + str(payload.get("ref")).encode("ascii"), hashlib.sha256).hexdigest()[:32]
        if post and token != want:
            self.problem("the token of the POST is wrong or missing")
        if not post and token is not None:
            self.problem("a GET carries a token")
        if post and (h.get("content-type") != "application/json" or int(h.get("content-length", "-1")) != len(call.body) or call.payload is None):
            self.problem("a POST needs a JSON body and its length")
        if not post and call.body:
            self.problem("a GET has a body")
        for name in ("sec-fetch-site", "cookie", "origin", "referer"):
            if name in h:
                self.problem("the call has %s" % name)
        if not call.raw.startswith((b"GET /admin/api/", b"POST /admin/api/")) or "host" not in h:
            self.problem("the request line or the Host is wrong")
        return payload
