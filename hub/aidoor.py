"""The doors of the AI keys (docs/MAP_AI.md): the pages where a person makes, lists and revokes keys, and the doors that an AI uses with a key. The socket of the armed tab is hub/airelay.py.

  GET  /hub/ai/keys              the person's keys (name, scope, made, ends, last used, calls: never a secret or a digest), the limits and the page's token (`csrf`)
  POST /hub/ai/keys              {"name", "scope": read|edit, "days": 7|30|90}: makes a key; the whole key is in this one answer and nowhere else (409 `live`: three work already; 429 `day`: five made today)
  POST /hub/ai/keys/revoke       {"id"}: the person's own key stops working at once
  POST /hub/ai/v1/call           {"tool", "args"} with `Authorization: Bearer <key>`: one tool, answered {"ok": true, "result"} or {"ok": false, "error", "message"}
  POST /hub/ai/mcp               the MCP door (hub/aimcp.py), the same tools and the same checks
  GET  /hub/ai/v1/openapi.json   the plain HTTP door as an OpenAPI file (no key: it says only what the tools are)
  GET  /hub/ai/ws                the armed tab's socket (hub/airelay.py)

THE KEY PAGES are the page's own calls and nobody else's: the site's Origin and `Sec-Fetch-Site: same-origin` (both for a POST), a JSON body of 512 bytes at the most, the person's login (401 `login`), an account
that is not banned (403 `banned`) and, for a POST, the token of this login (`X-Ants-Csrf`, an HMAC under a key made from the hub's own secret: it changes with every login and is never in a URL). Everything that
can be decided is decided before a body byte is read.

THE KEY DOORS take no cookie and no CORS: the key is the right to ask (hub/aikeys.py judges it on every call, in the same order for every failure), the armed tab is the consent to receive. In this order, and
nothing is read from the body before the key is good: the address's allowance of bad keys (429 `rate`, the key is not looked at), the Origin (a browser's page of another site is 403 `origin`; there is none from
an AI app), the type and the length of the body, the key (401 `bad_key`, one answer for every wrong), the key's and the account's budgets (429 `rate`), the body, the tool and its arguments (hub/aitools.py), the
scope (403 `read_only`), then the relay. A call is one line in the log and one row in the calls' log, never a key, an argument, a result or a name.
"""
import asyncio
import hashlib
import hmac
import re
import time

from . import aikeys, aimcp, airelay, aitools, http, limits as limitmod
from .store import StoreError

PREFIX = "/hub/ai/"
WS_PATH = "/hub/ai/ws"
ROUTES = {"/hub/ai/keys": ("GET", "POST"), "/hub/ai/keys/revoke": ("POST",), "/hub/ai/v1/call": ("POST",), "/hub/ai/mcp": ("POST",), "/hub/ai/v1/openapi.json": ("GET",)}
PAGES = frozenset(("/hub/ai/keys", "/hub/ai/keys/revoke"))
CSRF_HEADER = "x-ants-csrf"
DIGITS = re.compile(r"[0-9]{1,9}\Z")


class Refused(Exception):
    """A request turned away before anything else is done: `answer` is the bytes of the response."""

    def __init__(self, answer):
        super().__init__("refused")
        self.answer = answer


class Door:
    def __init__(self, hub):
        self.hub = hub
        self.relay = airelay.Relay(hub)
        self.budgets = aikeys.Budgets(hub.limits, hub.clock)
        self.pages = limitmod.RateByAddress(hub.limits.ai_pages_burst, hub.limits.ai_pages_refill_seconds)
        self.csrf_key = None
        self.next_log = float("-inf")

    def forget_idle(self, now):
        self.pages.forget_idle(now)
        self.budgets.forget_idle(now)

    # ---- answers -----------------------------------------------------------------------------------------------------------------------------

    @staticmethod
    def fail(status, code, headers=None):
        return http.response(status, {"error": code}, headers)

    @staticmethod
    def refusal(code, message="", headers=None):
        """The answer of the plain HTTP door to a call that did not work: the status of the code, and the code and the sentence."""
        status, own = aitools.ERRORS[code]
        extra = dict(headers or {})
        if status == 429:
            extra.setdefault("Retry-After", "5")
        if status == 401:
            extra.setdefault("WWW-Authenticate", "Bearer")
        return http.response(status, {"ok": False, "error": code, "message": message or own}, extra)

    def noted(self, why, address):
        """One line in the log a second at the most for a refused call: the word and the address's short tag, never what was sent."""
        now = self.hub.clock()
        if now >= self.next_log:
            self.next_log = now + 1.0
            self.hub.log.event("ai_refuse", why=why, tag=self.hub.log.tag(address))

    # ---- one request -------------------------------------------------------------------------------------------------------------------------

    async def serve(self, request, reader, address):
        """The bytes of the whole answer to a request for a path under /hub/ai/ (the socket's path is not here). The body of a POST is read here, after the cheap checks."""
        try:
            path = request.split()[0]
            methods = ROUTES.get(path)
            if methods is None or "?" in request.path:
                return self.fail(404, "not_found")
            if not self.hub.cfg.ai_keys:
                return self.fail(503, "ai_off")
            if request.method not in methods:
                return self.fail(405, "method", {"Allow": ", ".join(methods)})
            length = self.framing(request, request.method == "POST", self.hub.limits.ai_page_body_max if path in PAGES else self.hub.limits.ai_body_max)
            if path in PAGES:
                return await self.page(request, reader, address, path, length)
            if path == "/hub/ai/v1/openapi.json":
                return http.response(200, aimcp.openapi(self.hub.cfg.origin))
            return await self.key_door(request, reader, address, path, length)
        except Refused as e:
            return e.answer
        except (asyncio.IncompleteReadError, asyncio.TimeoutError):
            return self.fail(400, "body")
        except StoreError as e:
            self.hub.store_trouble(e)
            return self.fail(503, "busy")
        except Exception as e:                                       # (a bug costs one request, never the hub; the class of the error is all that is logged)
            self.hub.log.event("error", kind=type(e).__name__, where="ai")
            return self.fail(500, "error")

    def framing(self, request, post, limit):
        """The length of the body that a request promises: 0 for a GET, 1 to `limit` for a POST, no Transfer-Encoding. Refused otherwise. Nothing is read."""
        h = request.headers
        if "transfer-encoding" in h:
            raise Refused(self.fail(400, "body"))
        length = h.get("content-length", "0")
        if not post:
            if length != "0":
                raise Refused(self.fail(400, "body"))
            return 0
        if not DIGITS.match(length) or int(length) < 1:
            raise Refused(self.fail(400, "body"))
        if int(length) > limit:
            raise Refused(self.fail(413, "body"))
        return int(length)

    async def body(self, reader, length):
        return await asyncio.wait_for(reader.readexactly(length), self.hub.limits.handshake_seconds)

    # ---- the key pages -----------------------------------------------------------------------------------------------------------------------

    def same_origin(self, request, post):
        origin, site = request.headers.get("origin"), request.headers.get("sec-fetch-site")
        if post:
            return bool(origin) and origin == self.hub.cfg.origin and site == "same-origin"
        return (origin is None or (bool(origin) and origin == self.hub.cfg.origin)) and site == "same-origin"

    def token(self, who):
        """The token of the page's POSTs for this login: it changes with every login, and with the account."""
        if self.csrf_key is None:
            self.csrf_key = hmac.new(self.hub.store.secret(), b"ants-ai-csrf-v1", hashlib.sha256).digest()
        message = b"csrf1\0" + (who.session or "").encode("ascii") + b"\0" + self.hub.chat.ref(who.account_id).encode("ascii")
        return hmac.new(self.csrf_key, message, hashlib.sha256).hexdigest()

    def shown(self, row):
        return {"id": row.id, "name": row.name, "scope": row.scope, "made": row.made, "ends": row.ends, "used": row.used, "calls": row.calls}

    async def page(self, request, reader, address, path, length):
        hub, h = self.hub, request.headers
        post = request.method == "POST"
        if not self.pages.take(address, hub.clock()):
            return self.fail(429, "rate", {"Retry-After": "5"})
        if not self.same_origin(request, post):
            return self.fail(403, "origin")
        if post and h.get("content-type", "").split(";")[0].strip().lower() != "application/json":
            return self.fail(415, "type")
        who = await hub.identify(h)
        if who is None:
            return self.fail(401, "login")
        if hub.store.is_banned(who.account_id):
            return self.fail(403, "banned")
        if post and not (h.get(CSRF_HEADER) and hmac.compare_digest(h[CSRF_HEADER].encode("latin-1"), self.token(who).encode("ascii"))):
            return self.fail(403, "csrf")
        if not post:
            keys = [self.shown(r) for r in hub.store.ai_keys(who.account_id)]
            return http.response(200, {"ok": True, "keys": keys, "max": hub.limits.ai_keys_live, "days": list(aikeys.DAYS), "csrf": self.token(who), "armed": self.relay.armed(hub.chat.ref(who.account_id)),
                                       "untrusted": True, "untrusted_fields": ["keys[].name"]})
        data = aitools.parse(await self.body(reader, length))
        if not isinstance(data, dict):
            return self.fail(400, "bad")
        if path == "/hub/ai/keys/revoke":
            key_id = data.get("id") if set(data) == {"id"} else None
            if not isinstance(key_id, str):
                return self.fail(400, "bad")
            if not hub.store.ai_key_revoke(who.account_id, key_id):
                return self.fail(404, "no_key")
            hub.log.event("ai_revoke", key=key_id, ref=hub.chat.ref(who.account_id)[:8])
            return http.response(200, {"ok": True})
        return self.make(who, data)

    def make(self, who, data):
        hub = self.hub
        if not set(data) == {"name", "scope", "days"}:
            return self.fail(400, "bad")
        name, why = aikeys.clean_name(data["name"], hub.limits.ai_name_chars)
        scope, days = data["scope"], data["days"]
        if name is None or scope not in ("read", "edit") or type(days) is not int or days not in aikeys.DAYS:
            return self.fail(400, "bad")
        for _ in range(5):                                          # (8 hex digits of public id: a clash is rare, and is tried again with another)
            key_id, key, kept = aikeys.new_key()
            row = hub.store.ai_key_add(who.account_id, key_id, name, scope, kept, days * 86400, hub.limits.ai_keys_live, hub.limits.ai_keys_day)
            if row != "id":
                break
        if row == "live":
            return self.fail(409, "live")
        if row == "day":
            return self.fail(429, "day", {"Retry-After": "60"})
        if row == "id":
            return self.fail(503, "busy", {"Retry-After": "5"})
        hub.log.event("ai_key_made", key=key_id, ref=row.ref[:8], scope=scope, days=days)
        return http.response(200, {"ok": True, "key": key, **self.shown(row)})

    # ---- the doors of an AI ------------------------------------------------------------------------------------------------------------------

    async def key_door(self, request, reader, address, path, length):
        hub, h = self.hub, request.headers
        mcp = path == "/hub/ai/mcp"
        if self.budgets.address_spent(address):
            self.noted("rate", address)
            return self.refusal("rate", headers={"Retry-After": "10"})
        if h.get("origin") not in (None, hub.cfg.origin):
            return self.fail(403, "origin")
        if h.get("content-type", "").split(";")[0].strip().lower() != "application/json":
            return self.fail(415, "type")
        key = aikeys.judge(hub.store, h.get("authorization"), hub.store.clock())
        if key is None:
            self.budgets.bad_key(address)
            self.noted("bad_key", address)
            return self.refusal("bad_key")
        if not self.budgets.take(key.id, key.ref):
            self.noted("rate", address)
            return self.refusal("rate")
        data = aitools.parse(await self.body(reader, length))
        hub.store.ai_key_use(key.id)
        return await (self.mcp(key, data) if mcp else self.plain(key, data))

    async def plain(self, key, data):
        if not isinstance(data, dict) or not set(data) <= {"tool", "args"} or not isinstance(data.get("tool"), str):
            return self.refusal("bad_request")
        args = data.get("args", {})
        try:
            result = await self.run(key, data["tool"], args)
        except airelay.Refusal as e:
            return self.refusal(e.code, e.message)
        return http.response(200, {"ok": True, "result": result})

    async def run(self, key, name, args):
        """The marked result of a tool for a key, or a Refusal: the tool is in the table, the scope covers it, the arguments fit, the relay asks the tab. One row of the calls' log either way."""
        tool = aitools.TOOLS.get(name)
        if tool is None:
            raise airelay.Refusal("unknown_tool")
        if tool.scope == aitools.EDIT and key.scope != aitools.EDIT:
            raise airelay.Refusal("read_only")
        bad = aitools.arguments(name, args)
        if bad:
            raise airelay.Refusal(bad[0], bad[1])
        started, cells, ok = time.monotonic(), 0, 0
        try:
            result, cells = await self.relay.call(key, name, args, lambda: self.still_good(key))
            ok = 1
            return aitools.mark(name, result)
        finally:
            self.log_call(key, name, cells, ok, started)

    def still_good(self, key):
        """Whether the key works now, asked again when a call's turn comes: the same row, not revoked, not ended."""
        row = self.hub.store.ai_key(key.id)
        return row is not None and row.digest == key.digest and not row.revoked and row.ends > self.hub.store.clock()

    def log_call(self, key, tool, cells, ok, started):
        millis = int((time.monotonic() - started) * 1000)
        self.hub.log.event("ai_call", key=key.id, ref=key.ref[:8], tool=tool, cells=cells, ok=ok, ms=millis)
        try:
            self.hub.store.ai_log_add(key.id, key.ref, tool, cells, ok, millis)
        except StoreError as e:
            self.hub.store_trouble(e)

    async def mcp(self, key, message):
        if not isinstance(message, dict) or message.get("jsonrpc") != "2.0" or not isinstance(message.get("method"), str) or ("id" in message and type(message["id"]) not in aimcp.ID_TYPES):
            return http.response(400, aimcp.rpc_error(None, -32600, "Invalid request: one JSON-RPC 2.0 message at a time"))
        if "id" not in message:
            return http.response(202)                               # (a notification: nothing to say)
        ident, method, params = message["id"], message["method"], message.get("params", {})
        if not isinstance(params, dict):
            return http.response(200, aimcp.rpc_error(ident, -32602, "Invalid params"))
        if method == "initialize":
            name = aimcp.client_name(params)
            if name:
                self.relay.clients[key.id] = name
            return http.response(200, aimcp.rpc_result(ident, aimcp.initialize(params)))
        if method == "ping":
            return http.response(200, aimcp.rpc_result(ident, {}))
        if method == "tools/list":
            return http.response(200, aimcp.rpc_result(ident, aimcp.tools_list(key.scope)))
        if method != "tools/call":
            return http.response(200, aimcp.rpc_error(ident, -32601, "Method not found"))
        name, args = params.get("name"), params.get("arguments", {})
        if not isinstance(name, str) or name not in aitools.TOOLS:
            return http.response(200, aimcp.rpc_error(ident, -32602, "Unknown tool"))
        try:
            result = await self.run(key, name, args)
        except airelay.Refusal as e:
            return http.response(200, aimcp.rpc_result(ident, aimcp.error_result(e.code, e.message)))
        return http.response(200, aimcp.rpc_result(ident, aimcp.tool_result(name, result)))
