"""The admin page's door (docs/HUB.md, "The admin page's door"): who may use the admin page, decided here and nowhere else, and the hub's side of what the page does.

  GET  /hub/admin/gate         asked only by the site's nginx (auth_request) before it passes an /admin/api request on, with the visitor's own headers: 204 and a signed pass in
                               `X-Ants-Admin-Pass` when the visitor may, 401 when nobody is logged in, 403 when somebody is but may not (or when the door is shut). No body either way, no CORS.
  GET  /hub/admin/api/me, bans, mutes, hidden, modlog, status
  POST /hub/admin/api/ban, unban, mute, unmute, nopic, pic, del      one JSON object, the same rules as the hub's other POSTs and a token (`csrf` of /me)

THE rule (`Door.decide`, used by every one of the above): the visitor's hub login names an account; the account may when it is `ANTS_HUB_OWNER` (no call to Discord) or when its role is
`admin` on the hub's own freshness rule (hub/roles.py `role_of`: an answer of Discord that is not past its interval and its grace; a role that is not known or not that young is looked up
once, urgently, and a lookup that does not answer means no). A moderator may not. Unknown, stale, failed: not allowed. The door is shut for everybody when `ANTS_HUB_ADMIN_GATE_SECRET` is
absent, shorter than 32 characters or not printable (the hub still starts; the start line says so).

The pass: `v1.<payload>.<mac>`; payload = base64url (no padding) of the JSON `{"exp", "name", "nonce", "ref", "role"}` (exp: seconds since 1970, 60 seconds after it was made; name: the Discord
username; nonce: 16 hex characters; ref: the account's reference, hub/pseudo.py; role: `admin` or `owner`); mac = hex HMAC-SHA-256, under the secret, of the ASCII text `v1.<payload>`. The game
server holds the same secret as ANTS_ADMIN_GATE_SECRET and checks the mac, then exp, then role. The token of the page's POSTs is the hex HMAC-SHA-256, under the same secret, of
`csrf1`, the login's key and the account's reference: it changes with every login and is never in a URL.

An account is addressed by username or reference only (never an id: `admin.resolve(..., ids=False)`), every change is written to the moderators' log with the actor's reference, an action needs the fresh
check of hub/chat.py (the roles of the person who acts and of the person acted on, answered within a minute) and the same table of the powers (`chat.allowed`; the account of the
operator is above every other: it may act on an administrator, and nobody may act on it). No id is in a pass, an answer, a log line or an error. Names in an answer are typed text:
every answer that has some says `untrusted` and names the fields in `untrusted_fields`, as the maps API does.
"""
import asyncio
import base64
import hashlib
import hmac
import io
import json
import re
import secrets

from . import admin as adminmod, chat as chatmod, http, limits as limitmod, pseudo, roles as rolesmod
from .store import StoreError
from .store.base import SHOW_BLOCKED

GATE_PATH = "/hub/admin/gate"
API_PREFIX = "/hub/admin/api/"
ROUTE_PREFIX = "/hub/admin/"
PASS_HEADER = "X-Ants-Admin-Pass"
CSRF_HEADER = "x-ants-csrf"
PASS_SECONDS = 60
MAX_BODY = 512                                   # a body of the page's POSTs is a few dozen bytes
MAX_MUTE_MINUTES = 10080
MODLOG_DAYS = 30
MODLOG_SHOWN = 500
READS = frozenset(("me", "bans", "mutes", "hidden", "modlog", "status"))
WRITES = frozenset(("ban", "unban", "mute", "unmute", "nopic", "pic", "del"))
TABLE_WORD = {"nopic": "ban", "pic": "unban"}      # (the table of the chat's powers has the commands of the chat: a picture's block and its lift need what a ban and its lift need)
ACCOUNT_ARGUMENT = re.compile(r"[!-~]{1,40}\Z")
DIGITS = re.compile(r"[0-9]{1,18}\Z")


def b64(raw):
    return base64.urlsafe_b64encode(raw).rstrip(b"=").decode("ascii")


def sign(key, payload):
    """`v1.<payload>.<mac>` for a dictionary (see the top of this file)."""
    body = "v1." + b64(json.dumps(payload, separators=(",", ":"), sort_keys=True).encode("utf-8"))
    return "%s.%s" % (body, hmac.new(key, body.encode("ascii"), hashlib.sha256).hexdigest())


def warn(cfg, log):
    """One line at the start when the door is shut for want of a good key (never the key, nor how long it is)."""
    if not cfg.admin_gate_on:
        log.event("warning", what="admin_page_door_shut_" + ("secret_too_short_or_not_printable" if cfg.admin_gate_given else "no_secret_set"))


class Refused(Exception):
    """A request that is turned away before anything is asked of Discord: `answer` is the bytes of the response."""

    def __init__(self, answer):
        super().__init__("refused")
        self.answer = answer


class Door:
    def __init__(self, hub):
        self.hub = hub
        self.key = hub.cfg.admin_gate_secret.encode("ascii") if hub.cfg.admin_gate_on else None
        self.rate = limitmod.RateByAddress(hub.limits.admin_burst, hub.limits.admin_refill_seconds)
        self.lookups = limitmod.RateByAccount(hub.limits.admin_lookups_burst, hub.limits.admin_lookups_refill_seconds)

    def forget_idle(self, now):
        self.rate.forget_idle(now)
        self.lookups.forget_idle(now)

    # ---- the rule ----------------------------------------------------------------------------------------------------------------------------

    def is_owner(self, account):
        owner = self.hub.cfg.owner
        return bool(owner) and account == owner

    async def decide(self, headers):
        """(verdict, who, role): `closed` (no good key), `login` (nobody is logged in), `no` (somebody is, and may not) or `ok` with the Identity and the role that counts (`owner` or `admin`)."""
        if self.key is None:
            return "closed", None, None
        who = await self.hub.identify(headers)
        if who is None:
            return "login", None, None
        if self.is_owner(who.account_id):
            return "ok", who, "owner"
        roles = self.hub.chat.roles
        role = roles.role_of(who.account_id)
        if role != rolesmod.ADMIN and self.lookups.take(self.hub.chat.key_of(who.account_id), self.hub.clock()):
            try:
                await roles.confirm([who.account_id])                # (one urgent lookup, for a role that is not known or ran out; what it says is read again below, from the answer itself)
            except Exception as e:
                self.hub.log.event("error", kind=type(e).__name__, where="admin")
            role = roles.role_of(who.account_id)
        return ("ok", who, "admin") if role == rolesmod.ADMIN else ("no", who, None)

    def token(self, who):
        """The token of the page's POSTs for this login."""
        message = b"csrf1\0" + (who.session or "").encode("ascii") + b"\0" + self.hub.chat.ref(who.account_id).encode("ascii")
        return hmac.new(self.key, message, hashlib.sha256).hexdigest()

    # ---- one request -------------------------------------------------------------------------------------------------------------------------

    @staticmethod
    def fail(status, code, headers=None):
        return http.response(status, {"error": code}, headers)

    async def serve(self, request, reader, address):
        """The bytes of the whole answer to a request for a path under /hub/admin/. The body of a POST is read here (the hub's other pages take none); nothing is asked of Discord before the
        request is known to be well formed and the address is within its limit."""
        try:
            path = request.split()[0]
            if path != GATE_PATH and not (path.startswith(API_PREFIX) and path[len(API_PREFIX):] in READS | WRITES):
                return self.fail(404, "not_found")
            if request.split()[1]:
                return self.fail(404, "not_found")
            if not self.rate.take(address, self.hub.clock()):
                return self.fail(429, "rate", {"Retry-After": "5"})
            if path == GATE_PATH:
                return await self.gate(request)
            name = path[len(API_PREFIX):]
            post = name in WRITES
            if request.method != ("POST" if post else "GET"):
                return self.fail(405, "method", {"Allow": "POST" if post else "GET"})
            body = await self.body(request, reader, post)
            return await self.api(request, name, body)
        except Refused as e:
            return e.answer
        except (asyncio.IncompleteReadError, asyncio.TimeoutError):
            return self.fail(400, "body")
        except StoreError as e:
            self.hub.store_trouble(e)
            return self.fail(503, "busy")
        except Exception as e:                                       # (a bug costs one request, never the hub; the class of the error is all that is logged)
            self.hub.log.event("error", kind=type(e).__name__, where="admin")
            return self.fail(500, "error")

    async def body(self, request, reader, post):
        """The body of a POST (JSON text), None for a GET; Refused when the framing is wrong: a GET has no body, a POST has a Content-Length of 1 to MAX_BODY and no Transfer-Encoding."""
        h = request.headers
        if "transfer-encoding" in h:
            raise Refused(self.fail(400, "body"))
        length = h.get("content-length", "0")
        if not post:
            if length != "0":
                raise Refused(self.fail(400, "body"))
            return None
        if not DIGITS.match(length) or int(length) < 1:
            raise Refused(self.fail(400, "body"))
        if int(length) > MAX_BODY:
            raise Refused(self.fail(413, "body"))
        return await asyncio.wait_for(reader.readexactly(int(length)), self.hub.limits.handshake_seconds)

    async def gate(self, request):
        if request.method != "GET":
            return self.fail(405, "method", {"Allow": "GET"})
        h = request.headers
        if "transfer-encoding" in h or h.get("content-length", "0") != "0":
            return http.response(400)
        verdict, who, role = await self.decide(h)
        if verdict == "login":
            return http.response(401)
        if verdict != "ok":
            return http.response(403)
        store = self.hub.store
        payload = {"ref": self.hub.chat.ref(who.account_id), "name": who.username, "role": role, "exp": int(store.clock()) + PASS_SECONDS, "nonce": secrets.token_hex(8)}
        return http.response(204, headers={PASS_HEADER: sign(self.key, payload)})

    # ---- the page's API ----------------------------------------------------------------------------------------------------------------------

    def same_origin(self, request, post):
        """A request that the admin page itself made: Sec-Fetch-Site same-origin and the site's Origin (always sent with a POST; a same-origin GET may have none)."""
        origin, site = request.headers.get("origin"), request.headers.get("sec-fetch-site")
        if post:
            return origin == self.hub.cfg.origin and bool(origin) and site == "same-origin"
        return (origin is None or (origin == self.hub.cfg.origin and bool(origin))) and site == "same-origin"

    async def api(self, request, name, body):
        post = name in WRITES
        h = request.headers
        if not self.same_origin(request, post):
            return self.fail(403, "origin")
        if post and h.get("content-type", "").split(";")[0].strip().lower() != "application/json":
            return self.fail(415, "type")
        verdict, who, role = await self.decide(h)
        if verdict == "login":
            return self.fail(401, "login")
        if verdict != "ok":
            return self.fail(403, "admin")
        if post and not (h.get(CSRF_HEADER) and hmac.compare_digest(h[CSRF_HEADER].encode("latin-1"), self.token(who).encode("ascii"))):
            return self.fail(403, "csrf")
        if post:
            return await self.act(who, role, name, body)
        return self.read(who, role, name)

    def read(self, who, role, name):
        store, chat = self.hub.store, self.hub.chat
        if name == "me":
            return http.response(200, {"name": who.username, "role": role, "csrf": self.token(who), "untrusted": True, "untrusted_fields": ["name"]})
        if name == "status":
            out = io.StringIO()
            adminmod.show_usage(store, self.hub.limits.db_max_bytes, out)                       # (what `hub.admin status` prints, line by line)
            adminmod.show_pictures(store, self.hub.limits, out)
            return http.response(200, {"status": out.getvalue().splitlines(), "health": self.health()})
        if name == "bans":
            rows = list(reversed(store.bans()))
            names = store.usernames_by_reference({chat.ref(a) for a, _, _, _ in rows})
            out = [{"ref": chat.ref(a), "name": names.get(chat.ref(a), n), "at": at, "by": adminmod.by_of(store, by)} for a, n, at, by in rows]
            return http.response(200, {"bans": out, "untrusted": True, "untrusted_fields": ["name"]})
        if name == "mutes":
            rows = store.mutes()
            names = store.usernames_by_reference({chat.ref(a) for a, _, _, _ in rows})
            out = [{"ref": chat.ref(a), "name": names.get(chat.ref(a), n), "until": until, "by": adminmod.by_of(store, by)} for a, n, until, by in rows]
            return http.response(200, {"mutes": out, "untrusted": True, "untrusted_fields": ["name"]})
        if name == "hidden":
            blocked = sorted(r for r, state in store.picture_marks().items() if state == SHOW_BLOCKED)
            names = store.usernames_by_reference(blocked)
            return http.response(200, {"hidden": [{"ref": r, "name": names.get(r, "")} for r in blocked], "untrusted": True, "untrusted_fields": ["name"]})
        rows = store.actions(store.clock() - MODLOG_DAYS * 86400, MODLOG_SHOWN)
        names = store.usernames_by_reference({r for row in rows for r in (row.actor, row.target) if r})
        out = [{"at": r.at, "action": r.action, "actor": r.actor, "actor_name": names.get(r.actor, ""), "target": r.target, "target_name": names.get(r.target, ""), "detail": r.detail} for r in rows]
        return http.response(200, {"modlog": out, "untrusted": True, "untrusted_fields": ["actor_name", "target_name"]})

    def health(self):
        """What the roles' lookups are doing, in words (no account, no id): `off` when no role is asked of Discord, `refused` while Discord refuses the token, the server or the access, `down` while
        a lookup fails, else `ok`; and how many accounts the hub holds an answer for."""
        roles = self.hub.chat.roles
        if not getattr(roles, "on", False):
            state = "off"
        elif self.hub.clock() < getattr(roles, "pause_until", float("-inf")):
            state = "refused"
        else:
            state = "down" if getattr(roles, "down", False) else "ok"
        return {"roles": state, "roles_known": len(getattr(roles, "entries", ()))}

    # ---- the page's changes ------------------------------------------------------------------------------------------------------------------

    def arguments(self, name, body):
        """The checked arguments of a change: (account text, minutes, line id), or None for a body that is not what the change takes (an object with exactly the right fields of the right types)."""
        try:
            data = json.loads(body.decode("utf-8"), parse_constant=chatmod.Chat.refuse_constant)
        except (ValueError, UnicodeDecodeError, RecursionError):
            return None
        if not isinstance(data, dict):
            return None
        if name == "del":
            line = data.get("line")
            return (None, None, line) if set(data) == {"line"} and type(line) is int and 0 < line < 2 ** 62 else None
        account = data.get("account")
        if not isinstance(account, str) or not ACCOUNT_ARGUMENT.match(account):
            return None
        if name == "mute":
            minutes = data.get("minutes")
            return (account, minutes, None) if set(data) == {"account", "minutes"} and type(minutes) is int and 1 <= minutes <= MAX_MUTE_MINUTES else None
        return (account, None, None) if set(data) == {"account"} else None

    def powers(self, command, me, role, target, target_role):
        """May the actor (`me`, whose role is `owner` or `admin`) do `command` to `target` (None: to nobody)? The operator's account is above every other and nobody is above it."""
        if target is None:
            return True
        same = target == me
        if self.is_owner(target):
            return same and command == "del"
        if role == "owner" and not same:
            return True
        return chatmod.allowed(TABLE_WORD.get(command, command), rolesmod.ADMIN, target_role, same)

    async def act(self, who, role, name, body):
        args = self.arguments(name, body)
        if args is None:
            return self.fail(400, "bad")
        argument, minutes, line = args
        store, chat, roles = self.hub.store, self.hub.chat, self.hub.chat.roles
        me = who.account_id
        by = chat.ref(me)
        target, shown, setter, state, whom = None, "", None, None, ""
        if name == "del":
            target = store.account_of_line(line)
            if target is None:
                return self.fail(404, "bad")
        elif name in ("nopic", "pic"):
            if pseudo.REFERENCE.match(argument) and store.picture_state_by_reference(argument) is not None:      # (a block that outlived its account is found by its reference, as `hub.admin` does)
                target = adminmod.resolve(store, argument, ids=False)[0]
                state = store.picture_state_by_reference(argument)
                setter = lambda show, note: store.set_picture_show_by_reference(argument, show, operator=True, note=note)
                whom = argument
            else:
                target, shown = adminmod.resolve(store, argument, ids=False)
                if target is None:
                    return self.fail(404, "who")
                state = store.picture_state(target)
                setter = lambda show, note: store.set_picture_show(target, show, operator=True, note=note)
        else:
            target, shown = adminmod.resolve(store, argument, ids=False)
            if target is None:
                return self.fail(404, "who")
        if target == "":
            target = None                                            # (a line of the hub's own: no person)
        accounts = [me] + ([target] if target is not None else [])
        if not await roles.confirm(accounts) or not roles.fresh(accounts):
            return self.fail(503, "role")                            # (the roles of the two could not be confirmed with Discord just now: nothing is done)
        if not self.is_owner(me) and roles.role_of(me) != rolesmod.ADMIN:
            return self.fail(403, "admin")                           # (no longer an administrator: the answer is fresh, and it says so)
        if not self.powers(name, me, role, target, roles.role_of(target) if target is not None else None):
            return self.fail(403, "power")
        if target is not None:
            whom = chat.ref(target)
        if name == "mute":
            until = store.mute(target, minutes, by, shown, note=(by, "mute", whom, str(minutes)))
            return http.response(200, {"ok": True, "until": until})
        if name == "unmute":
            if not store.muted_until(target):
                return self.fail(409, "bad")
            store.mute(target, 0, by, "", note=(by, "unmute", whom, ""))
        elif name == "ban":
            if store.is_banned(target):
                return self.fail(409, "bad")
            store.ban(target, by, shown, note=(by, "ban", whom, ""))
            chat.remove(chat.sockets_of(target), "banned")
            chat.sync_pictures()
        elif name == "unban":
            if not store.unban(target, note=(by, "unban", whom, "")):
                return self.fail(409, "bad")
        elif name == "del":
            if not store.delete_line(line, by, note=(by, "del", whom, str(line))):
                return self.fail(409, "bad")
            chat.sync_deletions()
        else:
            if adminmod.change_picture(store, name, whom, state, lambda show, note: setter(show, (by,) + note[1:]), io.StringIO(), io.StringIO()):         # (the command line's own change, with this actor in the log's row)
                return self.fail(409, "bad")
            chat.sync_pictures()
        return http.response(200, {"ok": True})

