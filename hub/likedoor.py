"""The likes' door (docs/REPLAY_COUNTS.md; the pass is the admin page's, docs/HUB_ROLES.md "The admin page's door"): who may like a replay, decided by the hub's login and nowhere else, and the hub's one call to
the game server's counts door.

  GET  /hub/likes/gate         asked only by the site's nginx (auth_request) before it passes a /counts/mine, /counts/like or /counts/unlike request on, with the visitor's own headers: 204 and a signed pass in
                               `X-Ants-Like-Pass` for ANY account that is logged in (a ban or a mute of the chat does not stop a like), 401 when nobody is, 503 when the door is shut (no good key: for
                               everybody, nobody logged in too; a shut door is never a 401), 400 when the request has no usable `X-Client-Address`, 429 beyond the address's limit. A GET with no body and no
                               query; any other method is a 405, anything else under /hub/likes/ a 404. No CORS.
  POST <ANTS_HUB_COUNTS_URL>/counts/forget      the hub's call (`forget_likes`): every like of an account's reference is removed, before Forget me deletes anything (hub/login.py `forget`)

The keys: the secret is the admin page's one (`ANTS_ADMIN_GATE_SECRET`, hub/config.py `admin_gate_on` is the one rule of whether it is good), and the two keys here are not the admin page's: the pass key is the
RAW 32-byte digest of HMAC-SHA-256 with the secret (ASCII) as the KEY and the label `ants-like-pass-v1` as the MESSAGE, the token key the same with `ants-like-csrf-v1`. The pass is `v1.<payload>.<mac>` (the admin
page's form, hub/admindoor.py `sign`): the payload is base64url, no padding, of the JSON `{"exp","nonce","ref"}` (exp: seconds since 1970, 60 seconds after it was made; nonce: 16 hex characters, new for each
pass; ref: the account's reference, hub/pseudo.py), the mac the hex HMAC-SHA-256, under the pass key, of the ASCII text `v1.<payload>`. The token of a reference is the first 32 characters of the lower hex of
HMAC-SHA-256 under the token key of `csrf1\\0` and the reference. The game server holds the same secret and makes the same keys. A like pass has no name and no role, and never an id: whoever sees one learns a
pseudonym and nothing more.

The call is made with a small HTTP/1.1 client of the same kind and strictness as hub/gamedoor.py (the same reader, the same limits: connected in 3 seconds, the whole call over in 10, at most 128 KB read, a redirect
is a failure, the answer must be status 200 and a JSON object with a whole number `removed`). Every failure is a `DoorError` that has a kind and nothing else: no address, no header, no pass and no text of any
cause is in it or in a log line.
"""
import asyncio
import hashlib
import hmac
import json
import secrets

from . import admindoor, discord as discordmod, gamedoor, http, limits as limitmod
from .gamedoor import DoorError
from .store import StoreError

GATE_PATH = "/hub/likes/gate"
ROUTE_PREFIX = "/hub/likes/"
PASS_HEADER = "X-Ants-Like-Pass"
CSRF_HEADER = "X-Ants-Csrf"
FORGET_PATH = "/counts/forget"
PASS_LABEL = b"ants-like-pass-v1"
CSRF_LABEL = b"ants-like-csrf-v1"
PASS_SECONDS = 60


def keys(secret):
    """(the pass key, the token key) of a secret (text): two raw 32-byte digests that are not the admin page's."""
    raw = secret.encode("ascii")
    return hmac.new(raw, PASS_LABEL, hashlib.sha256).digest(), hmac.new(raw, CSRF_LABEL, hashlib.sha256).digest()


def make_pass(key, ref, exp, nonce):
    """`v1.<payload>.<mac>` of a reference (see the top of this file)."""
    return admindoor.sign(key, {"exp": exp, "nonce": nonce, "ref": ref})


def token(csrf_key, ref):
    """The token of a reference: what the game server hands the page, and what the hub sends with its call."""
    return hmac.new(csrf_key, b"csrf1\0" + ref.encode("ascii"), hashlib.sha256).hexdigest()[:32]


class Door:
    def __init__(self, hub):
        self.hub = hub
        cfg = hub.cfg
        self.key = self.csrf_key = None
        if cfg.admin_gate_on:
            self.key, self.csrf_key = keys(cfg.admin_gate_secret)
        self.host = self.port = None
        if cfg.counts_url:
            host, _, port = cfg.counts_url[len("http://"):].rpartition(":")
            self.host, self.port = host, int(port)
        self.rate = limitmod.RateByAddress(hub.limits.like_burst, hub.limits.like_refill_seconds)
        self.next_address_log = float("-inf")

    @property
    def wanted(self):
        """Whether Forget me has to remove the person's likes first: the counts door's address is set (it may still be shut for want of a good key: `forget_likes` then says `off`)."""
        return self.host is not None

    def forget_idle(self, now):
        self.rate.forget_idle(now)

    @staticmethod
    def fail(status, code, headers=None):
        return http.response(status, {"error": code}, headers)

    # ---- the gate ----------------------------------------------------------------------------------------------------------------------------

    async def serve(self, request, address):
        """The bytes of the whole answer to a request for a path under /hub/likes/. Nothing is asked of the login before the request is known to be well formed and the address is within its limit."""
        try:
            if request.split()[0] != GATE_PATH or "?" in request.path:                  # (no query at all: not even an empty one)
                return self.fail(404, "not_found")
            if limitmod.address_key(request.headers.get("x-client-address"), "") == "unknown":
                self.address_missing()                                                  # (the gate is asked once for every call of the page: with no address of the visitor all of them would share one bucket)
                return self.fail(400, "address")
            if not self.rate.take(address, self.hub.clock()):
                return self.fail(429, "rate", {"Retry-After": "5"})
            return await self.gate(request)
        except StoreError as e:
            self.hub.store_trouble(e)
            return self.fail(503, "busy")
        except Exception as e:                                                          # (a bug costs one request, never the hub; the class of the error is all that is logged)
            self.hub.log.event("error", kind=type(e).__name__, where="likes")
            return self.fail(500, "error")

    def address_missing(self):
        """The gate was asked with no address of the visitor (X-Client-Address): one line in the log a minute at the most."""
        now = self.hub.clock()
        if now >= self.next_address_log:
            self.next_address_log = now + 60.0
            self.hub.log.event("warning", what="likes_gate_asked_without_x_client_address")

    async def gate(self, request):
        if request.method != "GET":
            return self.fail(405, "method", {"Allow": "GET"})
        h = request.headers
        if "transfer-encoding" in h or h.get("content-length", "0") != "0":
            return http.response(400)
        if self.key is None:
            return http.response(503)                                # (no good key: the door is shut, which is not a "no": nginx says likes_off and the page says that likes are not on)
        who = await self.hub.identify(h)
        if who is None:
            return http.response(401)
        return http.response(204, headers={PASS_HEADER: self.made_pass(self.hub.chat.ref(who.account_id))})

    def made_pass(self, ref):
        """A pass for one call: a minute from now by the clock of the hub's store, and a nonce of its own."""
        return make_pass(self.key, ref, int(self.hub.store.clock()) + PASS_SECONDS, secrets.token_hex(8))

    # ---- Forget me's call ----------------------------------------------------------------------------------------------------------------------

    async def forget_likes(self, ref):
        """The number of likes that the counts door removed for the reference, or a DoorError (`off`: no address or no good key, `down`: no connection, no answer in time, not an HTTP answer, too big, a
        redirect or an answer that is not JSON, `bad`: a status that is not 200 or an answer that is not what was asked for)."""
        if self.host is None or self.key is None:
            raise DoorError("off")
        try:
            return await asyncio.wait_for(self.exchange(ref), self.hub.limits.door_seconds)
        except DoorError:
            raise
        except (asyncio.TimeoutError, OSError, ValueError, EOFError, UnicodeError, RecursionError, discordmod.DiscordError):
            pass                                                  # (raised below, outside the handler: what the cause said can hold the address)
        raise DoorError("down")

    async def exchange(self, ref):
        lines = ["POST %s HTTP/1.1" % FORGET_PATH, "Host: %s:%d" % (self.host, self.port), "User-Agent: " + gamedoor.USER_AGENT, "Accept: application/json", "Accept-Encoding: identity", "Connection: close",
                 "%s: %s" % (PASS_HEADER, self.made_pass(ref)), "%s: %s" % (CSRF_HEADER, token(self.csrf_key, ref)), "Content-Length: 0"]
        reader, writer = await asyncio.wait_for(asyncio.open_connection(self.host, self.port, limit=65536), self.hub.limits.door_connect_seconds)
        try:
            writer.write(("\r\n".join(lines) + "\r\n\r\n").encode("ascii"))
            await writer.drain()
            status, answer, _ = await gamedoor.read_answer(None, reader, self.hub.limits.door_body_max)
        finally:
            writer.transport.abort()                              # (at once: the call is over, and nothing waits for a polite close)
        if 300 <= status < 400:
            raise DoorError("down")                               # (a redirect is never followed)
        if status != 200:
            raise DoorError("bad")
        data = json.loads(answer.decode("utf-8"), parse_constant=gamedoor.refuse_constant) if answer else None
        removed = data.get("removed") if isinstance(data, dict) else None
        if not isinstance(removed, int) or isinstance(removed, bool) or removed < 0:
            raise DoorError("bad")
        return removed
