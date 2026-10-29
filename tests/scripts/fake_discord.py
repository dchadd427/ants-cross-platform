"""A FAKE Discord for the tests of the hub's login (tests/scripts/test_hub_login.py, test_nginx_hub_login.py, web_home_login_browser.js's Python twin): the three addresses that the hub uses, on a
loopback port, over plain http (the hub allows that to a fake Discord on a loopback address and nowhere else).

    GET  /oauth2/authorize   a consent page (or, with consent=False, the answer of a visitor who said yes before: at once) that sends the visitor back to the redirect with a fresh code
    POST /api/v10/oauth2/token   the exchange: the client id, the secret, the redirect and an unused code are checked; a code is good once (a second use is 400 invalid_grant)
    GET  /api/v10/users/@me      who the token belongs to

and every way that a real service goes wrong: a status (400, 401, 429, 500, ...), no answer at all, an answer that drips, one that is far too big (by its length, in chunks, or in its head), JSON
that is cut off or is not JSON, a message with both chunks and a length, an answer of the wrong shape (a token that is a number, an id that is not digits, a name with a line end in it), a redirect, a connection that is cut. `token_mode` and
`me_mode` pick one; `requests` keeps what the hub sent (the secret is in the body of the exchange and nowhere else).
"""
import json
import secrets
import socketserver
import threading
import time
import urllib.parse

CLIENT_ID = "4242424242420001"
CLIENT_SECRET = "canary-client-secret-9d41c7e2b8"
EPOCH_MS = 1420070400000


def snowflake(age_days, now=None, worker=7):
    """A Discord id of an account that was made `age_days` ago."""
    now = time.time() if now is None else now
    return str(((int(now * 1000) - EPOCH_MS - int(age_days * 86400 * 1000)) << 22) | (worker << 12) | 5)


class FakeDiscord:
    def __init__(self, redirect, user=("261842000123456789", "maple.leaf"), client_id=CLIENT_ID, client_secret=CLIENT_SECRET):
        self.redirect = redirect                         # the address that Discord sends the visitor back to (the hub's /hub/callback, as the application has it)
        self.user = user                                 # who the visitor is when they authorize: (id, username)
        self.client_id, self.client_secret = client_id, client_secret
        self.consent = True                              # a first visit shows a consent page; False is a visitor who said yes before (no page: the way back at once)
        self.token_mode = "ok"
        self.me_mode = "ok"
        self.codes = {}                                  # code -> user, until it is used
        self.used = set()
        self.tokens = {}                                 # access token -> user
        self.requests = []                               # (method, path, query, headers, body text)
        self.sent = 0                                    # the bytes that the last "huge" answer got out before the hub hung up on it
        self.lock = threading.Lock()
        fake = self

        class Handler(socketserver.StreamRequestHandler):
            def handle(self):
                try:
                    fake.serve(self)
                except (OSError, ValueError):
                    pass

        class Server(socketserver.ThreadingTCPServer):
            allow_reuse_address = True
            daemon_threads = True

        self.server = Server(("127.0.0.1", 0), Handler)
        self.port = self.server.server_address[1]
        self.url = "http://127.0.0.1:%d" % self.port
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def close(self):
        self.server.shutdown()
        self.server.server_close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
        return False

    # ---- a request ---------------------------------------------------------------------------------------------------------------------------

    def serve(self, h):
        line = h.rfile.readline(65536).decode("latin-1").rstrip("\r\n")
        method, target, _ = line.split(" ", 2)
        headers = {}
        while True:
            row = h.rfile.readline(65536).decode("latin-1").rstrip("\r\n")
            if not row:
                break
            name, _, value = row.partition(":")
            headers[name.strip().lower()] = value.strip()
        body = h.rfile.read(int(headers.get("content-length", "0"))).decode("latin-1") if headers.get("content-length", "0").isdigit() else ""
        path, _, query = target.partition("?")
        with self.lock:
            self.requests.append((method, path, query, headers, body))
        if path == "/oauth2/authorize":
            return self.authorize(h, query)
        if path == "/api/v10/oauth2/token" and method == "POST":
            return self.answer(h, self.token_mode, lambda: self.token(body))
        if path == "/api/v10/users/@me" and method == "GET":
            return self.answer(h, self.me_mode, lambda: self.me(headers.get("authorization", "")))
        h.wfile.write(raw(404, {"error": "not found"}))

    def authorize(self, h, query):
        q = dict(urllib.parse.parse_qsl(query))
        problem = None
        if q.get("client_id") != self.client_id or q.get("response_type") != "code" or q.get("scope") != "identify" or q.get("prompt") != "none" or q.get("redirect_uri") != self.redirect:
            problem = "bad request"
        elif not q.get("state"):
            problem = "no state"
        if problem:
            return h.wfile.write(raw(400, {"error": problem}))
        code = secrets.token_hex(15)
        with self.lock:
            self.codes[code] = self.user
        back = self.redirect + "?" + urllib.parse.urlencode({"code": code, "state": q["state"]})
        if not self.consent:
            return h.wfile.write(raw(302, None, {"Location": back}))
        page = ("<!doctype html><title>Authorize</title><h1>Authorize Ants?</h1><p>This will let it see your username.</p><a id=\"go\" href=\"%s\">Authorize</a>" % back.replace("&", "&amp;")).encode()
        h.wfile.write(raw(200, None, {"Content-Type": "text/html; charset=utf-8"}, page))

    def token(self, body):
        form = dict(urllib.parse.parse_qsl(body))
        with self.lock:
            user = self.codes.get(form.get("code", ""))
            ok = (form.get("grant_type") == "authorization_code" and form.get("client_id") == self.client_id and form.get("redirect_uri") == self.redirect and user is not None
                  and form.get("code") not in self.used)
            if form.get("client_id") != self.client_id or form.get("client_secret") != self.client_secret:
                return 401, {"error": "invalid_client"}
            if not ok:
                return 400, {"error": "invalid_grant"}
            self.used.add(form["code"])
            token = secrets.token_hex(16)
            self.tokens[token] = user
        return 200, {"access_token": token, "token_type": "Bearer", "expires_in": 604800, "refresh_token": "canary-refresh-" + secrets.token_hex(8), "scope": "identify"}

    def me(self, authorization):
        user = self.tokens.get(authorization[len("Bearer "):]) if authorization.startswith("Bearer ") else None
        if user is None:
            return 401, {"message": "401: Unauthorized", "code": 0}
        return 200, {"id": user[0], "username": user[1], "discriminator": "0", "global_name": "Maple", "avatar": None, "email": "canary-email@example.org", "verified": True}

    def answer(self, h, mode, make):
        status, data = make()
        if mode == "ok":
            return h.wfile.write(raw(status, data))
        if mode.startswith("status:"):
            return h.wfile.write(raw(int(mode[7:]), {"error": "x", "message": "canary-error-body"}))
        if mode.startswith("slow:"):                         # an answer that comes, after this many seconds
            time.sleep(float(mode[5:]))
            return h.wfile.write(raw(status, data))
        if mode == "hang":
            time.sleep(30)
            return None
        if mode == "close":
            return None
        if mode == "partial":
            h.wfile.write(b"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 500\r\n\r\n{\"id\": ")
            return None
        if mode == "drip":
            body = json.dumps(data).encode()
            h.wfile.write(("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n" % len(body)).encode())
            for b in body:
                h.wfile.write(bytes([b]))
                h.wfile.flush()
                time.sleep(0.4)
            return None
        if mode in ("huge", "huge_stream", "huge_chunked"):
            self.sent = 0
            heads = {"huge": b"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 100000000\r\n\r\n",           # a length that is far past any answer of Discord, and the bytes of it
                     "huge_stream": b"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"x\": \"",     # no length at all, and no end
                     "huge_chunked": b"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n"}
            piece = b"1000\r\n" + b"x" * 4096 + b"\r\n" if mode == "huge_chunked" else b"x" * 65536
            try:
                h.wfile.write(heads[mode])
                for _ in range(1500):                       # (about 98 MB, if the hub keeps reading)
                    h.wfile.write(piece)
                    self.sent += len(piece)
            except OSError:
                pass
            return None
        if mode == "huge_head":
            h.wfile.write(b"HTTP/1.1 200 OK\r\n" + b"".join(b"X-Pad-%d: %s\r\n" % (i, b"y" * 200) for i in range(400)) + b"\r\n{}")
            return None
        if mode == "chunked":
            body = json.dumps(data).encode()
            mid = len(body) // 2
            parts = [body[:mid], body[mid:]]
            wire = b"".join(b"%x\r\n%s\r\n" % (len(p), p) for p in parts) + b"0\r\n\r\n"
            return h.wfile.write(b"HTTP/1.1 %d OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n" % status + wire)
        if mode == "chunked_length":                         # both a length and chunks: a message that two readers read in two ways
            body = json.dumps(data).encode()
            wire = b"%x\r\n%s\r\n0\r\n\r\n" % (len(body), body)
            return h.wfile.write(b"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\nContent-Length: %d\r\nConnection: close\r\n\r\n" % len(wire) + wire)
        if mode == "nolength":
            return h.wfile.write(b"HTTP/1.1 %d OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n" % status + json.dumps(data).encode())
        if mode == "notjson":
            return h.wfile.write(raw_text(200, "<html>Cloudflare says hello</html>"))
        if mode == "cutoff":
            return h.wfile.write(raw_text(200, '{"access_token": "abc", "token_type": "Bear'))
        if mode == "array":
            return h.wfile.write(raw_text(200, "[1, 2, 3]"))
        if mode == "nan":
            return h.wfile.write(raw_text(200, '{"id": NaN}'))
        if mode == "deep":
            return h.wfile.write(raw_text(200, "[" * 100000 + "]" * 100000))
        if mode.startswith("redirect"):
            code = int(mode[8:] or 302)
            return h.wfile.write(raw(code, None, {"Location": self.url + "/followed"}))             # (an address of this fake: a hub that followed the redirect would be seen asking for it)
        if mode.startswith("shape:"):
            return h.wfile.write(raw(200, json.loads(mode[6:])))
        if mode.startswith("merge:"):                        # the real answer with fields replaced
            data = dict(data, **json.loads(mode[6:]))
            return h.wfile.write(raw(200, data))
        raise AssertionError("no such mode: " + mode)


def raw(status, body=None, headers=None, payload=None):
    data = payload if payload is not None else b"" if body is None else json.dumps(body).encode()
    head = {200: "OK", 302: "Found", 307: "Temporary Redirect", 308: "Permanent Redirect", 400: "Bad Request", 401: "Unauthorized", 404: "Not Found", 429: "Too Many Requests"}.get(status, "Status")
    lines = ["HTTP/1.1 %d %s" % (status, head), "Content-Length: %d" % len(data), "Connection: close"]
    if body is not None:
        lines.append("Content-Type: application/json")
    lines += ["%s: %s" % kv for kv in (headers or {}).items()]
    return ("\r\n".join(lines) + "\r\n\r\n").encode("latin-1") + data


def raw_text(status, text):
    return raw(status, None, {"Content-Type": "application/json"}, text.encode())
