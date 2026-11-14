"""A FAKE Discord for the tests of the hub's login (tests/scripts/test_hub_login.py, test_nginx_hub_login.py, web_home_login_browser.js's Python twin): the three addresses that the hub uses, on a
loopback port, over plain http (the hub allows that to a fake Discord on a loopback address and nowhere else).

    GET  /oauth2/authorize   a consent page (or, with consent=False, the answer of a visitor who said yes before: at once) that sends the visitor back to the redirect with a fresh code
    POST /api/v10/oauth2/token   the exchange: the client id, the secret, the redirect and an unused code are checked; a code is good once (a second use is 400 invalid_grant)
    GET  /api/v10/users/@me      who the token belongs to
    GET  /api/v10/guilds/<server>/members/<account>   the roles of a member of the server, for the bot's token (the hub's roles: hub/roles.py, tests/scripts/test_hub_roles.py)

and every way that a real service goes wrong: a status (400, 401, 429, 500, ...), no answer at all, an answer that drips, one that is far too big (by its length, in chunks, or in its head), JSON
that is cut off or is not JSON, a message with both chunks and a length, an answer of the wrong shape (a token that is a number, an id that is not digits, a name with a line end in it), a redirect, a connection that is cut. `token_mode` and
`me_mode` pick one; `requests` keeps what the hub sent (the secret is in the body of the exchange and nowhere else).

The member lookup knows `members` (account id -> role ids; an account that is not there is not in the server: 404 with the code 10007), checks the bot's token and the server's id (401; 404 with the
code 10004) and goes wrong by `member_mode` like the others, and more: `err:401`, `err:403` (code 50001), `err:404:<code>`, `rate:<seconds>` (429 with `retry_after` and the headers),
`rate_global:<seconds>`, `remaining0:<seconds>` (a good answer that says the bucket is empty), `roles:<json>` (a member object whose `roles` is that), `user:<id>` (a member of another user).
`member_times` has the time of every lookup, `member_ids` the account each one asked about. All ids and the token here are made up.
"""
import json
import secrets
import socketserver
import struct
import threading
import time
import urllib.parse
import zlib

CLIENT_ID = "4242424242420001"
CLIENT_SECRET = "canary-client-secret-9d41c7e2b8"
BOT_TOKEN = "canary-bot-token-0a1b2c3d4e5f6a7b8c9d.XyZ-12345"
GUILD_ID = "700000000000000001"
ROLE_ADMIN = "700000000000000002"
ROLE_MOD = "700000000000000003"
ROLE_OTHER = "700000000000000004"
EPOCH_MS = 1420070400000
AVATAR = "0123456789abcdef0123456789abcdef"      # a made-up picture code of the shape that Discord's are (the fake `me` says it when `avatar` is set to it)


def chunk(kind, body):
    """One PNG chunk: the length, the four letters, the bytes and the CRC."""
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xffffffff)


def make_png(width=64, height=64, before=(), after=(), color=2, depth=8, interlace=0):
    """A real PNG (8 bit RGB by default) of the size asked for, with the chunks of `before` between the header and the data and the ones of `after` between the data and the end: (letters, bytes) pairs."""
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color]
    row = b"\x00" + bytes((x * 7 + 3) % 256 for x in range(-(-width * channels * depth // 8)))
    raw = row * height
    if interlace:                                    # (the rows of the seven passes: each has a filter byte and its pixels to a whole byte)
        raw = b""
        for x0, y0, dx, dy in ((0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)):
            columns, rows = -(-(width - x0) // dx), -(-(height - y0) // dy)
            if columns > 0 and rows > 0:
                raw += (b"\x00" + b"\x55" * -(-columns * channels * depth // 8)) * rows
    head = chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, depth, color, 0, 0, interlace))
    mid = b"".join(chunk(k, b) for k, b in before)
    if color == 3:
        mid = chunk(b"PLTE", bytes(range(256))[:3 * 16]) + mid
    return b"\x89PNG\r\n\x1a\n" + head + mid + chunk(b"IDAT", zlib.compress(raw)) + b"".join(chunk(k, b) for k, b in after) + chunk(b"IEND", b"")


def snowflake(age_days, now=None, worker=7):
    """A Discord id of an account that was made `age_days` ago."""
    now = time.time() if now is None else now
    return str(((int(now * 1000) - EPOCH_MS - int(age_days * 86400 * 1000)) << 22) | (worker << 12) | 5)


class FakeDiscord:
    def __init__(self, redirect="", user=("261842000123456789", "maple.leaf"), client_id=CLIENT_ID, client_secret=CLIENT_SECRET):
        self.redirect = redirect                         # the address that Discord sends the visitor back to (the hub's /hub/callback, as the application has it)
        self.user = user                                 # who the visitor is when they authorize: (id, username) or (id, username, global_name): the display name that Discord says (null when a person set none: the default, and
                                                         # whatever else a test puts there, of any kind and size, as Discord's answer carries it)
        self.client_id, self.client_secret = client_id, client_secret
        self.consent = True                              # a first visit shows a consent page; False is a visitor who said yes before (no page: the way back at once)
        self.token_mode = "ok"
        self.me_mode = "ok"
        self.codes = {}                                  # code -> user, until it is used
        self.used = set()
        self.tokens = {}                                 # access token -> user
        self.requests = []                               # (method, path, query, headers, body text)
        self.guild, self.bot_token = GUILD_ID, BOT_TOKEN
        self.members = {}                                # account id -> [role ids] of the server's members
        self.member_mode = "ok"
        self.member_times = []                           # time.monotonic() of every member lookup
        self.member_ids = []                             # the account that each one asked about
        self.sent = 0                                    # the bytes that the last "huge" answer got out before the hub hung up on it
        self.avatar = "canary-avatar-hash"               # what `me` says is the picture's code (not of the shape of one: no picture is fetched); set to AVATAR (or None for none) by the tests of the pictures
        self.cdn_mode = "ok"                             # the picture host (/cdn/avatars/<id>/<code>.png): ok, 404, 500, status:<n>, slow:<s>, hang, close, partial, drip, huge, huge_stream, huge_chunked, redirect, type:<content type>, notype
        self.picture = make_png()                        # the bytes that it sends
        self.cdn_inflight = self.cdn_peak = 0            # requests that the picture host is answering now, and the most there were at one time
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
        if path.startswith("/cdn/"):
            with self.lock:
                self.cdn_inflight += 1
                self.cdn_peak = max(self.cdn_peak, self.cdn_inflight)
            try:
                return self.serve_cdn(h, method, path)
            finally:
                with self.lock:
                    self.cdn_inflight -= 1
        if path == "/oauth2/authorize":
            return self.authorize(h, query)
        if path == "/api/v10/oauth2/token" and method == "POST":
            return self.answer(h, self.token_mode, lambda: self.token(body))
        if path == "/api/v10/users/@me" and method == "GET":
            return self.answer(h, self.me_mode, lambda: self.me(headers.get("authorization", "")))
        parts = path.split("/")
        if method == "GET" and len(parts) == 7 and parts[:4] == ["", "api", "v10", "guilds"] and parts[5] == "members":
            with self.lock:
                self.member_times.append(time.monotonic())
                self.member_ids.append(parts[6])
            return self.answer(h, self.member_mode, lambda: self.member(headers.get("authorization", ""), parts[4], parts[6]))
        h.wfile.write(raw(404, {"error": "not found"}))

    def serve_cdn(self, h, method, path):
        """The picture host: GET /cdn/avatars/<id>/<code>.png (the query is ignored); `cdn_mode` makes it go wrong in the ways a real host can."""
        mode, body = self.cdn_mode, self.picture
        parts = path.split("/")
        if method != "GET" or len(parts) != 5 or parts[:3] != ["", "cdn", "avatars"] or not parts[4].endswith(".png"):
            return h.wfile.write(raw(404, {"error": "not found"}))
        png = ("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: %d\r\nConnection: close\r\n\r\n" % len(body)).encode() + body
        if mode == "ok":
            return h.wfile.write(png)
        if mode == "404":
            return h.wfile.write(raw(404, {"message": "404: Not Found", "code": 0}))
        if mode == "500":
            return h.wfile.write(raw(500, {"message": "canary-error-body"}))
        if mode.startswith("status:"):
            return h.wfile.write(raw(int(mode[7:]), {"message": "canary-error-body"}))
        if mode.startswith("slow:"):
            time.sleep(float(mode[5:]))
            return h.wfile.write(png)
        if mode == "hang":
            time.sleep(30)
            return None
        if mode == "close":
            return None
        if mode == "partial":
            return h.wfile.write(png[:len(png) // 2])
        if mode == "drip":
            h.wfile.write(png[:png.index(b"\r\n\r\n") + 4])
            for b in body:
                h.wfile.write(bytes([b]))
                h.wfile.flush()
                time.sleep(0.2)
            return None
        if mode in ("huge", "huge_stream", "huge_chunked"):
            self.sent = 0
            heads = {"huge": b"HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: 100000000\r\n\r\n", "huge_stream": b"HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nConnection: close\r\n\r\n",
                     "huge_chunked": b"HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nTransfer-Encoding: chunked\r\n\r\n"}
            piece = b"1000\r\n" + b"x" * 4096 + b"\r\n" if mode == "huge_chunked" else b"x" * 65536
            try:
                h.wfile.write(heads[mode])
                for _ in range(1500):
                    h.wfile.write(piece)
                    self.sent += len(piece)
            except OSError:
                pass
            return None
        if mode == "redirect":
            return h.wfile.write(raw(302, None, {"Location": self.url + "/cdn/followed.png"}))        # (an address of this fake: a hub that followed it would be seen asking for it)
        if mode.startswith("type:"):
            return h.wfile.write(png.replace(b"Content-Type: image/png", b"Content-Type: " + mode[5:].encode(), 1))
        if mode == "notype":
            return h.wfile.write(png.replace(b"Content-Type: image/png\r\n", b"", 1))
        raise AssertionError("no such mode: " + mode)

    def member(self, authorization, guild, account):
        if authorization != "Bot " + self.bot_token:
            return 401, {"message": "401: Unauthorized", "code": 0}
        if guild != self.guild:
            return 404, {"message": "Unknown Guild", "code": 10004}
        roles = self.members.get(account)
        if roles is None:
            return 404, {"message": "Unknown Member", "code": 10007}
        return 200, {"user": {"id": account, "username": "canary.member", "avatar": None}, "roles": list(roles), "nick": None, "joined_at": "2020-01-01T00:00:00+00:00", "flags": 0}

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
        return 200, {"id": user[0], "username": user[1], "discriminator": "0", "global_name": user[2] if len(user) > 2 else None, "avatar": self.avatar, "email": "canary-email@example.org", "verified": True}

    def answer(self, h, mode, make):
        status, data = make()
        if mode == "ok":
            return h.wfile.write(raw(status, data))
        if mode.startswith("err:"):                          # the status of a real refusal, with its Discord code in the body
            _, code, *more = mode.split(":")
            number = int(more[0]) if more else {"401": 0, "403": 50001, "404": 10004}.get(code, 0)
            return h.wfile.write(raw(int(code), {"message": "canary-error-body", "code": number}))
        if mode.startswith("rate"):                          # 429: rate:<seconds> or rate_global:<seconds>
            kind, _, seconds = mode.partition(":")
            heads = {"Retry-After": seconds, "X-RateLimit-Scope": "global" if kind == "rate_global" else "user", "X-RateLimit-Remaining": "0", "X-RateLimit-Reset-After": seconds}
            return h.wfile.write(raw(429, {"message": "You are being rate limited.", "retry_after": float(seconds), "global": kind == "rate_global"}, heads))
        if mode.startswith("remaining0:"):
            return h.wfile.write(raw(status, data, {"X-RateLimit-Remaining": "0", "X-RateLimit-Reset-After": mode[11:], "X-RateLimit-Bucket": "canary-bucket"}))
        if mode.startswith("roles:"):
            return h.wfile.write(raw(200, dict(data, roles=json.loads(mode[6:]))))
        if mode.startswith("user:"):
            return h.wfile.write(raw(200, dict(data, user={"id": mode[5:], "username": "canary.other"})))
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
    head = {200: "OK", 302: "Found", 307: "Temporary Redirect", 308: "Permanent Redirect", 400: "Bad Request", 401: "Unauthorized", 403: "Forbidden", 404: "Not Found", 429: "Too Many Requests",
            500: "Internal Server Error"}.get(status, "Status")
    lines = ["HTTP/1.1 %d %s" % (status, head), "Content-Length: %d" % len(data), "Connection: close"]
    if body is not None:
        lines.append("Content-Type: application/json")
    lines += ["%s: %s" % kv for kv in (headers or {}).items()]
    return ("\r\n".join(lines) + "\r\n\r\n").encode("latin-1") + data


def raw_text(status, text):
    return raw(status, None, {"Content-Type": "application/json"}, text.encode())
