"""Discord, the two calls that the login needs (docs/HUB.md, "Login"): the code that Discord handed the browser is exchanged for an access token (`POST /oauth2/token`), and the token is
used once, to ask who it belongs to (`GET /users/@me`). The id and the username are all that is kept; the token is a local variable of one function and is gone when it returns (no
refresh token is asked for or looked at, no email, no avatar).

A small HTTP client of its own, because the answer of another service is data and not a thing to trust: https only (the one exception is the fake Discord of the tests, on a loopback
address, which hub/config.py allows only to a hub that listens on one), the certificate and the name checked, short timeouts for the connection and for the whole call, no redirect
is followed (a 3xx is a failure), the answer is read up to a size cap and not a byte more (the head, the body, a chunked body: all counted), and what comes back must be JSON of the
shape that was asked for. The failures are told apart for the log and for the page: a code that Discord refused (the visitor tries again), a refusal of the application's own
settings (the operator looks), a busy Discord, and everything else (down, slow, garbage, too big).
"""
import asyncio
import json
import re
import ssl
import urllib.parse

from . import config

AUTHORIZE = "https://discord.com/oauth2/authorize"
API = "https://discord.com/api/v10"
USER_AGENT = "AntsHub/1 (+https://github.com/dchadd427/ants-cross-platform)"       # (Discord's front door turns away a client that names none)
TOKEN = re.compile(r"[A-Za-z0-9._~+/=-]{1,512}\Z")           # an access token is a plain word: one that is not is not Discord's, and is never put in a header
STATUS_LINE = re.compile(r"HTTP/1\.[01] ([0-9]{3})(?: [^\r\n]*)?\Z")
HEADER = re.compile(r"([!#$%&'*+.^_`|~0-9A-Za-z-]+):[ \t]*([\t\x20-\x7e]*?)[ \t]*\Z")
CHUNK_SIZE = re.compile(r"([0-9a-fA-F]{1,8})(?:;[^\r\n]*)?\Z")


class DiscordError(Exception):
    """`kind` is rejected (the code was refused), config (the application's id or secret was refused), busy (429) or down (anything else); `status` is the HTTP status when there was one."""

    def __init__(self, kind, status=0):
        super().__init__(kind)
        self.kind = kind
        self.status = status


class Discord:
    def __init__(self, cfg, limits, ssl_context=None):
        self.client_id = cfg.login_client_id
        self.client_secret = cfg.login_client_secret
        self.redirect = cfg.origin + "/hub/callback"
        base = cfg.discord_test_url
        self.test = bool(base)                                   # (a fake Discord on a loopback address: plain http is allowed there and nowhere else)
        self.authorize_base = base + "/oauth2/authorize" if base else AUTHORIZE
        self.api = base + "/api/v10" if base else API
        self.limits = limits
        self.ssl = ssl_context or ssl.create_default_context()
        self.inflight = 0

    def authorize_url(self, state):
        """Where the visitor is sent: the application, the one scope (`identify`), the way back, the state, and no second consent for a visitor who said yes before."""
        pairs = (("client_id", self.client_id), ("response_type", "code"), ("scope", "identify"), ("redirect_uri", self.redirect), ("state", state), ("prompt", "none"))
        return self.authorize_base + "?" + "&".join("%s=%s" % (k, urllib.parse.quote(v, safe="")) for k, v in pairs)

    async def who(self, code):
        """(id, username) of the account that `code` was handed to, or a DiscordError. At most `login_inflight` of these run at a time: the next one is `busy` at once."""
        if self.inflight >= self.limits.login_inflight:
            raise DiscordError("busy")
        self.inflight += 1
        try:
            token = await self.token(code)
            return await self.me(token)
        finally:
            self.inflight -= 1

    async def token(self, code):
        form = urllib.parse.urlencode({"client_id": self.client_id, "client_secret": self.client_secret, "grant_type": "authorization_code", "code": code, "redirect_uri": self.redirect})
        status, data = await self.call("POST", "/oauth2/token", {"Content-Type": "application/x-www-form-urlencoded"}, form.encode("ascii"))
        if status == 400:
            raise DiscordError("config" if data.get("error") in ("invalid_client", "unauthorized_client") else "rejected", status)
        self.check_status(status)
        token = data.get("access_token")
        scopes = data.get("scope", "identify")
        if not isinstance(token, str) or not TOKEN.match(token) or str(data.get("token_type", "")).lower() != "bearer" or not isinstance(scopes, str) or "identify" not in scopes.split(" "):
            raise DiscordError("down", status)
        return token

    async def me(self, token):
        status, data = await self.call("GET", "/users/@me", {"Authorization": "Bearer " + token}, b"")
        self.check_status(status)
        account, name = data.get("id"), data.get("username")
        if not isinstance(account, str) or not config.ACCOUNT_ID.match(account) or not isinstance(name, str):
            raise DiscordError("down", status)
        return account, name

    @staticmethod
    def check_status(status):
        if status == 200:
            return
        raise DiscordError("config" if status in (401, 403) else "busy" if status == 429 else "down", status)

    # ---- one call -------------------------------------------------------------------------------------------------------------------------

    async def call(self, method, path, headers, body):
        """(status, JSON object) of one call; a DiscordError for a call that did not finish in time, was not answered, was answered with a redirect, or with anything but a small JSON object
        (a 4xx or 5xx answer with such a body is returned too: its status is what the caller looks at)."""
        try:
            return await asyncio.wait_for(self.exchange(method, self.api + path, headers, body), self.limits.discord_seconds)
        except DiscordError:
            raise
        except (asyncio.TimeoutError, OSError, ssl.SSLError, ValueError, EOFError, UnicodeError, RecursionError):
            raise DiscordError("down")

    async def exchange(self, method, url, headers, body):
        parts = urllib.parse.urlsplit(url)
        secure = parts.scheme == "https"
        if not secure and not (self.test and parts.scheme == "http"):
            raise DiscordError("down")
        host, port = parts.hostname, parts.port or (443 if secure else 80)
        reader, writer = await asyncio.wait_for(asyncio.open_connection(host, port, ssl=self.ssl if secure else None, server_hostname=host if secure else None, limit=65536),
                                                self.limits.discord_connect_seconds)
        try:
            lines = ["%s %s HTTP/1.1" % (method, parts.path), "Host: " + parts.netloc, "User-Agent: " + USER_AGENT, "Accept: application/json", "Accept-Encoding: identity", "Connection: close"]
            lines += ["%s: %s" % kv for kv in headers.items()]
            if method == "POST":
                lines.append("Content-Length: %d" % len(body))
            writer.write(("\r\n".join(lines) + "\r\n\r\n").encode("ascii") + body)
            await writer.drain()
            status, answer = await self.read_answer(reader)
        finally:
            writer.transport.abort()                              # (at once: a polite TLS shutdown of a server that does not answer would wait)
        if 300 <= status < 400:
            raise DiscordError("down", status)                    # (a redirect is never followed)
        try:
            data = json.loads(answer.decode("utf-8"), parse_constant=self.refuse_constant) if answer else None
        except ValueError:
            data = None
        if not isinstance(data, dict):
            if status == 200:
                raise DiscordError("down", status)
            data = {}                                             # (an error that is not JSON still has its status)
        return status, data

    @staticmethod
    def refuse_constant(name):
        raise ValueError(name)

    async def read_answer(self, reader):
        """(status, body bytes) of the answer on `reader`, reading no more than `discord_body_max` bytes in all."""
        room = self.limits.discord_body_max
        buffer = bytearray()

        async def more():
            nonlocal room
            data = await reader.read(min(4096, room + 1))
            if not data:
                raise EOFError("the answer ended")
            room -= len(data)
            if room < 0:
                raise DiscordError("down")                         # (too much: not what Discord sends)
            buffer.extend(data)

        async def until(marker):
            while marker not in buffer:
                await more()
            at = buffer.index(marker)
            piece = bytes(buffer[:at])
            del buffer[:at + len(marker)]
            return piece

        async def exactly(n):
            while len(buffer) < n:
                await more()
            piece = bytes(buffer[:n])
            del buffer[:n]
            return piece

        head = (await until(b"\r\n\r\n")).decode("latin-1").split("\r\n")
        m = STATUS_LINE.match(head[0])
        if not m:
            raise DiscordError("down")
        status = int(m.group(1))
        fields = {}
        for line in head[1:]:
            h = HEADER.match(line)
            if not h:
                raise DiscordError("down", status)
            name = h.group(1).lower()
            if name in fields and name in ("content-length", "transfer-encoding"):
                raise DiscordError("down", status)
            fields[name] = h.group(2)
        if 300 <= status < 400 or status == 204 or 100 <= status < 200:
            return status, b""
        if "transfer-encoding" in fields:
            if fields["transfer-encoding"].lower() != "chunked" or "content-length" in fields:
                raise DiscordError("down", status)
            body = bytearray()
            while True:
                size = CHUNK_SIZE.match((await until(b"\r\n")).decode("latin-1"))
                if not size:
                    raise DiscordError("down", status)
                n = int(size.group(1), 16)
                if n == 0:
                    return status, bytes(body)
                body += await exactly(n)
                if await exactly(2) != b"\r\n":
                    raise DiscordError("down", status)
        if "content-length" in fields:
            if not re.match(r"[0-9]{1,9}\Z", fields["content-length"]) or int(fields["content-length"]) > room + len(buffer):
                raise DiscordError("down", status)
            return status, await exactly(int(fields["content-length"]))
        while True:                                               # (no length: the answer ends when the connection does)
            try:
                await more()
            except EOFError:
                return status, bytes(buffer)
