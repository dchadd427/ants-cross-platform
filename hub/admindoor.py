"""The admin page's door (docs/HUB.md, "The admin page's door"): who may use the admin page, decided here and nowhere else, and the hub's side of what the page does.

  GET  /hub/admin/gate         asked only by the site's nginx (auth_request) before it passes an /admin/api request on, with the visitor's own headers: 204 and a signed pass in
                               `X-Ants-Admin-Pass` when the visitor may, 401 when nobody is logged in, 403 when somebody is but may not, 503 when the door is shut (no good key: for everybody, nobody
                               logged in too; nginx turns that into its `door_off` answer, the page says it is not available, and never that the visitor is not an admin). No body, no CORS.
  GET  /hub/admin/api/me, bans, mutes, hidden, noimages, modlog, status
  POST /hub/admin/api/ban, unban, mute, unmute, nopic, pic, noimages, images, del      one JSON object, the same rules as the hub's other POSTs and a token (`csrf` of /me)

THE rule (`Door.decide`, used by every one of the above): the visitor's hub login names an account; the account may when it is `ANTS_HUB_OWNER` (no call to Discord) or when its role is
`admin` on the hub's own freshness rule (hub/roles.py `role_of`: an answer of Discord that is not past its interval and its grace; a role that is not known or not that young is looked up
once, urgently, and a lookup that does not answer means no). A moderator may not. Unknown, stale, failed: not allowed. The door is shut for everybody when `ANTS_ADMIN_GATE_SECRET` (the
one setting of the key; the game server reads the same name; `ANTS_HUB_ADMIN_GATE_SECRET`, its old name here, is read when the new one is not set) is absent, shorter than 32 characters,
not printable or the made-up key of the known answer below (the hub still starts; the start line says so). A shut door is not a "no": it answers 503 `door_off` on the page's calls
too, so that nobody is told that they are not an administrator when the page is simply not on.

The keys: the secret is the setting's text with the characters that Python's str.strip() cuts removed from both ends (in practice ASCII space, tab, CR and LF; the secret is printable ASCII), taken as ASCII bytes. The key of the pass is
the RAW 32-byte digest (not its hex text) of HMAC-SHA-256 with the secret as the KEY and the label `ants-admin-pass-v1` as the MESSAGE; the key of the page's token is the same with the label `ants-admin-csrf-v1`, so the two never share a key. (Known answer: the secret
`canary-gate-key-5d8f0c1e7a9b4263-q` gives the pass key 13be5dab7dcbeb06e979f0fa5e5f817d178fa6b40da7c92bc02588e80ae3dd25 and the token key 03a2b6b94fabcf8ae018852e8e6c0b1ec691375286a0263168238796556e3018, as hex of the 32 bytes. That key is published, so it is refused as a key, here and by the game server.)

The pass: `v1.<payload>.<mac>`; payload = base64url (no padding) of the JSON `{"exp", "name", "nonce", "ref", "role"}` (exp: seconds since 1970, 60 seconds after it was made; name: the Discord
username; nonce: 16 hex characters; ref: the account's reference, hub/pseudo.py; role: `admin` or `owner`); mac = hex (64 characters) HMAC-SHA-256, under the raw pass key, of the ASCII text `v1.<payload>`. The game
server holds the same secret (the same setting, ANTS_ADMIN_GATE_SECRET) and checks the mac, then exp, then role. The token of the page's POSTs is the hex HMAC-SHA-256, under the token's key, of
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
import os
import re
import secrets

from . import admin as adminmod, chat as chatmod, http, limits as limitmod, pseudo, roles as rolesmod
from .store import StoreError
from .store.base import SHOW_BLOCKED, SHOW_OFF, TERMINAL

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
STAFF_MAX = 1000                                 # (accounts that the door remembers as having held a role: a handful in practice)
READS = frozenset(("me", "bans", "mutes", "hidden", "noimages", "modlog", "status"))
WRITES = frozenset(("ban", "unban", "mute", "unmute", "nopic", "pic", "noimages", "images", "del"))
TABLE_WORD = {"nopic": "ban", "pic": "unban", "noimages": "ban", "images": "unban"}      # (the table of the chat's powers has the commands of the chat: a picture's block, taking posting of images away and their lifts need what a ban and its lift need)
ACCOUNT_ARGUMENT = re.compile(r"[!-~]{1,40}\Z")
DIGITS = re.compile(r"[0-9]{1,18}\Z")
ID_SHAPED = re.compile(r"[0-9]{15,20}\Z")


def b64(raw):
    return base64.urlsafe_b64encode(raw).rstrip(b"=").decode("ascii")


def sign(key, payload):
    """`v1.<payload>.<mac>` for a dictionary (see the top of this file)."""
    body = "v1." + b64(json.dumps(payload, separators=(",", ":"), sort_keys=True).encode("utf-8"))
    return "%s.%s" % (body, hmac.new(key, body.encode("ascii"), hashlib.sha256).hexdigest())


def warn(cfg, log):
    """One line at the start when the door is shut for want of a good key (never the key, nor how long it is)."""
    if not cfg.admin_gate_on:
        log.event("warning", what="admin_page_door_shut_" + ("secret_is_the_sample_key" if cfg.admin_gate_sample else "secret_too_short_or_not_printable" if cfg.admin_gate_given else "no_secret_set"))


def no_duplicates(pairs):
    """A JSON object whose key comes twice is not one that the hub reads (another reader of the same text may take the first)."""
    keys = [k for k, _ in pairs]
    if len(set(keys)) != len(keys):
        raise ValueError("duplicate key")
    return dict(pairs)


def untyped_id(name):
    """The name of a row as an answer says it: a row of an older version or of the terminal may hold any text, and a text that is all digits and as long as an id is not told."""
    return "" if ID_SHAPED.match(name or "") else name


def code_stamp():
    """Eight hex characters that name the program that this hub runs: the SHA-256 of the bytes of its own source files (the image carries no version of its own, so that a deploy that changes nothing here
    does not restart the chat). The admin page's Status shows it beside the time the hub started. Nothing else is read; a file that cannot be read gives an empty text."""
    digest, here = hashlib.sha256(), os.path.dirname(os.path.abspath(__file__))
    try:
        for base, folders, names in os.walk(here):
            folders.sort()
            for name in sorted(names):
                if name.endswith(".py"):
                    with open(os.path.join(base, name), "rb") as f:
                        digest.update(os.path.relpath(os.path.join(base, name), here).encode("utf-8") + b"\0" + f.read() + b"\0")
    except OSError:
        return ""
    return digest.hexdigest()[:8]


def by_name(store, by):
    """Who made a ban or a mute, as the page is told: the reference that the row holds (16 hex characters, which may happen to be all digits, and are not then an id to be hashed again), else what
    the command line prints (`command line`, or a reference made of an id that an older row holds)."""
    return by if pseudo.REFERENCE.match(by) else adminmod.by_of(store, by)


class Refused(Exception):
    """A request that is turned away before anything is asked of Discord: `answer` is the bytes of the response."""

    def __init__(self, answer):
        super().__init__("refused")
        self.answer = answer


class Door:
    def __init__(self, hub):
        self.hub = hub
        secret = hub.cfg.admin_gate_secret.encode("ascii") if hub.cfg.admin_gate_on else None
        self.key = hmac.new(secret, b"ants-admin-pass-v1", hashlib.sha256).digest() if secret else None
        self.csrf_key = hmac.new(secret, b"ants-admin-csrf-v1", hashlib.sha256).digest() if secret else None
        self.rate = limitmod.RateByAddress(hub.limits.admin_burst, hub.limits.admin_refill_seconds)
        self.lookups = limitmod.RateByAccount(hub.limits.admin_lookups_burst, hub.limits.admin_lookups_refill_seconds)
        self.budget = limitmod.TokenBucket(hub.limits.admin_door_burst, hub.limits.admin_door_refill_seconds, hub.clock())      # (the door's own share of the lookups: the actions' share is not touched)
        self.new_budget = limitmod.TokenBucket(hub.limits.admin_door_new_burst, hub.limits.admin_door_new_refill_seconds, hub.clock())      # (the small share of the accounts never known to hold a role)
        self.staff = {}                                  # the hub's key of every account that was seen with a role (at most STAFF_MAX; the oldest is dropped first): these use the door's whole share
        self.flights = {}                                # the hub's key of an account -> the lookup in flight for it, which every parallel request waits for
        self.next_address_log = float("-inf")
        self.next_budget_log = float("-inf")
        self.version = {"code": code_stamp(), "started": int(hub.store.clock())}          # (what this hub is, and since when: the Status of the page)

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
        roles, account = self.hub.chat.roles, who.account_id
        key = self.hub.chat.key_of(account)
        if not (roles.role_of(account) == rolesmod.ADMIN and roles.fresh([account])):
            await self.look(roles, account, key)                     # (the usual belief of the roles is not enough here: a pass is believed by the game server for a minute, so the answer must be a minute old at the most)
        if roles.role_of(account) != rolesmod.NONE:
            self.remember_staff(key)
        if roles.role_of(account) == rolesmod.ADMIN and roles.fresh([account]):
            return "ok", who, "admin"
        return "no", who, None

    def remember_staff(self, key):
        self.staff.pop(key, None)
        self.staff[key] = True
        while len(self.staff) > STAFF_MAX:
            del self.staff[next(iter(self.staff))]

    def is_staff(self, roles, account, key):
        """Whether the account holds a role now or was seen with one (the entry of the roles, or the door's own short list)."""
        entry = roles.entries.get(account)
        return key in self.staff or (entry is not None and entry.role != rolesmod.NONE)

    def budget_empty(self):
        """One line in the log a minute at the most when a share of the door's lookups is used up (no account in it)."""
        now = self.hub.clock()
        if now >= self.next_budget_log:
            self.next_budget_log = now + 60.0
            self.hub.log.event("warning", what="admin_door_lookup_budget_empty")

    async def look(self, roles, account, key):
        """One urgent lookup of the account's role, for all the requests of that account that ask while it is in flight: the first one makes it (from the door's own share, never the actions' `afford`,
        which the moderators' commands live on, once in a while for each account and for the door as a whole) and the others wait for the same answer and spend nothing. An answer that is a minute old
        at the most is not asked for again (it says what it says). A lookup that does not answer in time says nothing: the account is then not let in."""
        if roles.fresh([account]) and getattr(roles, "on", False):
            return
        flight = self.flights.get(key)
        if flight is not None:
            await asyncio.wait([flight], timeout=self.hub.limits.roles_urgent_seconds + 1.0)
            return
        flight = asyncio.get_running_loop().create_future()
        self.flights[key] = flight
        try:
            await self.lookup(roles, account, key)
        finally:
            del self.flights[key]
            flight.set_result(None)

    async def lookup(self, roles, account, key):
        budget = self.budget if self.is_staff(roles, account, key) else self.new_budget       # (an account never known to hold a role has a small share of its own)
        if not self.lookups.take(key, self.hub.clock()):
            return
        if not budget.take(self.hub.clock()):
            self.budget_empty()
            return
        try:
            if getattr(roles, "task", None) is None or roles.clock() < roles.pause_until:
                return
            if account not in roles.entries:
                if len(roles.entries) >= roles.limits.roles_entries_max and not roles.evict():
                    return
                roles.entries[account] = rolesmod.Entry(roles.clock())
            future = asyncio.get_running_loop().create_future()
            roles.urgent.setdefault(account, []).append(future)
            if roles.wake is not None:
                roles.wake.set()
            done, pending = await asyncio.wait([future], timeout=self.hub.limits.roles_urgent_seconds)
            for f in pending:
                f.cancel()
        except Exception as e:
            self.hub.log.event("error", kind=type(e).__name__, where="admin")

    def token(self, who):
        """The token of the page's POSTs for this login."""
        message = b"csrf1\0" + (who.session or "").encode("ascii") + b"\0" + self.hub.chat.ref(who.account_id).encode("ascii")
        return hmac.new(self.csrf_key, message, hashlib.sha256).hexdigest()

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
            if "?" in request.path:                                  # (no query at all: not even an empty one)
                return self.fail(404, "not_found")
            if path == GATE_PATH and limitmod.address_key(request.headers.get("x-client-address"), "") == "unknown":
                self.address_missing()                              # (the gate is asked once for every request of the page: with no address of the visitor all of them would share one bucket)
                return self.fail(400, "address")
            if not self.rate.take(address, self.hub.clock()):
                return self.fail(429, "rate", {"Retry-After": "5"})
            if path == GATE_PATH:
                return await self.gate(request)
            name = path[len(API_PREFIX):]
            post = name in WRITES
            if request.method != ("POST" if post else "GET"):
                return self.fail(405, "method", {"Allow": "POST" if post else "GET"})
            length = self.framing(request, post)
            return await self.api(request, name, reader, length)
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

    def address_missing(self):
        """The gate was asked with no address of the visitor (X-Client-Address): one line in the log a minute at the most."""
        now = self.hub.clock()
        if now >= self.next_address_log:
            self.next_address_log = now + 60.0
            self.hub.log.event("warning", what="admin_gate_asked_without_x_client_address")

    def framing(self, request, post):
        """The length of the body that a request promises: 0 for a GET, which has none; for a POST 1 to MAX_BODY, with no Transfer-Encoding. Refused (Refused) otherwise. Nothing is read."""
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
        if int(length) > MAX_BODY:
            raise Refused(self.fail(413, "body"))
        return int(length)

    async def gate(self, request):
        if request.method != "GET":
            return self.fail(405, "method", {"Allow": "GET"})
        h = request.headers
        if "transfer-encoding" in h or h.get("content-length", "0") != "0":
            return http.response(400)
        verdict, who, role = await self.decide(h)
        if verdict == "closed":
            return http.response(503)                                # (no good key: the door is shut, which is not a "no": nginx says door_off and the page says that it is not available)
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

    async def api(self, request, name, reader, length):
        """The cheap checks first (the site, the type, the login, the token), and the body of a POST is read only then, so that nobody who is not let in can make the hub wait for one."""
        post = name in WRITES
        h = request.headers
        if not self.same_origin(request, post):
            return self.fail(403, "origin")
        if post and h.get("content-type", "").split(";")[0].strip().lower() != "application/json":
            return self.fail(415, "type")
        verdict, who, role = await self.decide(h)
        if verdict == "closed":
            return self.fail(503, "door_off")
        if verdict == "login":
            return self.fail(401, "login")
        if verdict != "ok":
            return self.fail(403, "admin")
        if post and not (h.get(CSRF_HEADER) and hmac.compare_digest(h[CSRF_HEADER].encode("latin-1"), self.token(who).encode("ascii"))):
            return self.fail(403, "csrf")
        if post:
            body = await asyncio.wait_for(reader.readexactly(length), self.hub.limits.handshake_seconds)
            return await self.act(who, role, name, body)
        return self.read(who, role, name)

    def read(self, who, role, name):
        store, chat = self.hub.store, self.hub.chat
        if name == "me":
            return http.response(200, {"name": who.username, "role": role, "csrf": self.token(who), "untrusted": True, "untrusted_fields": ["name"]})
        if name == "status":
            return http.response(200, self.status())
        if name == "bans":
            rows = list(reversed(store.bans()))
            names = store.usernames_by_reference({chat.ref(a) for a, _, _, _ in rows} | {by_name(store, by) for _, _, _, by in rows})
            out = [{"ref": chat.ref(a), "name": untyped_id(names.get(chat.ref(a), n)), "at": at, "by": by_name(store, by), "by_name": untyped_id(names.get(by_name(store, by), ""))} for a, n, at, by in rows]
            return http.response(200, {"bans": out, "untrusted": True, "untrusted_fields": ["name", "by_name"]})
        if name == "mutes":
            rows = store.mutes()
            names = store.usernames_by_reference({chat.ref(a) for a, _, _, _ in rows} | {by_name(store, by) for _, _, _, by in rows})
            out = [{"ref": chat.ref(a), "name": untyped_id(names.get(chat.ref(a), n)), "until": until, "by": by_name(store, by), "by_name": untyped_id(names.get(by_name(store, by), ""))} for a, n, until, by in rows]
            return http.response(200, {"mutes": out, "untrusted": True, "untrusted_fields": ["name", "by_name"]})
        if name == "hidden":
            blocked = sorted(r for r, state in store.picture_marks().items() if state == SHOW_BLOCKED)
            names = store.usernames_by_reference(blocked)
            return http.response(200, {"hidden": [{"ref": r, "name": names.get(r, "")} for r in blocked], "untrusted": True, "untrusted_fields": ["name"]})
        if name == "noimages":
            marks = sorted(store.noimages_marks().items(), key=lambda item: -item[1][0])
            names = store.usernames_by_reference({r for r, _ in marks} | {by for _, (_, by) in marks if pseudo.REFERENCE.match(by)})
            return http.response(200, {"noimages": [{"ref": r, "name": untyped_id(names.get(r, "")), "at": at, "by": by if pseudo.REFERENCE.match(by) else "command line" if by == TERMINAL else "", "by_name": untyped_id(names.get(by, ""))} for r, (at, by) in marks],
                                       "untrusted": True, "untrusted_fields": ["name", "by_name"]})
        rows = store.actions(store.clock() - MODLOG_DAYS * 86400, MODLOG_SHOWN)
        names = store.usernames_by_reference({r for row in rows for r in (row.actor, row.target) if r})
        out = [{"at": r.at, "action": r.action, "actor": r.actor, "actor_name": names.get(r.actor, ""), "target": r.target, "target_name": names.get(r.target, ""), "detail": r.detail} for r in rows]
        return http.response(200, {"modlog": out, "untrusted": True, "untrusted_fields": ["actor_name", "target_name"]})

    def status(self):
        """What the page's Status shows, as numbers (the page draws the meter and the words from them): the store (`hub.admin status` says the same in lines), the roles' health, the last fetch of a
        picture and which program this is. No account, no id, no name."""
        store, limits = self.hub.store, self.hub.limits
        used, (count, size), marks = store.usage(), store.picture_usage(), list(store.picture_marks().values())
        pictures = getattr(self.hub.chat, "pictures", None)
        last = pictures.last_fetch() if pictures is not None else None
        return {"store": {"bytes": used.bytes, "limit": limits.db_max_bytes, "log": used.log, "free": used.free, "lines": used.lines, "oldest": used.oldest if used.lines else None,
                          "pictures": count, "pictures_max": limits.pictures_rows_max, "picture_bytes": size, "picture_bytes_max": limits.pictures_bytes_max, "off": marks.count(SHOW_OFF), "blocked": marks.count(SHOW_BLOCKED)},
                "images": self.images_status(),
                "health": self.health(), "last_picture": {"kind": last[0], "age": last[1]} if last else None, "version": dict(self.version)}

    def images_status(self):
        """The Status page's numbers of the images: the files kept and their bytes, of the two limits, the accounts whose posting is taken away, and whether the converter can be asked (`off`: the hub has none, `ok`: the last
        upload did not find it down, `down`: it did). No account, no name."""
        images, limits = self.hub.images, self.hub.limits
        rows, size = self.hub.store.image_usage()
        return {"on": images.on, "files": rows, "files_max": limits.image_files_max, "bytes": size, "bytes_max": limits.image_bytes_max, "off": len(self.hub.store.noimages_marks()), "converter": self.hub.image_door.converter_state()}

    def health(self):
        """What the roles' lookups are doing, in words (no account, no id): `off` when no role is asked of Discord, `refused` while Discord refuses the token, the server or the access, `down` while
        a lookup fails, else `ok`; how many accounts the hub holds an answer for; and, while refused, which refusal (`guild`, `token` or `access`), the HTTP status that Discord gave and the seconds until the
        hub asks again (`kind`, `status` and `retry` are null otherwise)."""
        roles = self.hub.chat.roles
        kind = status = retry = None
        if not getattr(roles, "on", False):
            state = "off"
        elif self.hub.clock() < getattr(roles, "pause_until", float("-inf")):
            state = "refused"
            kind, status = getattr(roles, "problem", (None, None))
            retry = max(0, int(roles.pause_until - self.hub.clock()) + 1)
        else:
            state = "down" if getattr(roles, "down", False) else "ok"
        return {"roles": state, "roles_known": len(getattr(roles, "entries", ())), "kind": kind, "status": status, "retry": retry}

    # ---- the page's changes ------------------------------------------------------------------------------------------------------------------

    def arguments(self, name, body):
        """The checked arguments of a change: (account text, minutes, line id), or None for a body that is not what the change takes (an object with exactly the right fields of the right types)."""
        try:
            data = json.loads(body.decode("utf-8"), parse_constant=chatmod.Chat.refuse_constant, object_pairs_hook=no_duplicates)
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
        elif name in ("noimages", "images"):
            if name == "images" and pseudo.REFERENCE.match(argument) and store.noimages_state_by_reference(argument):          # (a flag that outlived its account's other rows is lifted by its reference, as the command line does)
                found = adminmod.resolve(store, argument, ids=False)[0]
                target = found if found is not None else None
                whom = argument
            else:
                target, shown = adminmod.resolve(store, argument, ids=False)
                if target is None:
                    return self.fail(404, "who")
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
            kind = "del_img" if store.image_of_line(line) is not None else "del"
            if not store.delete_line(line, by, note=(by, kind, whom, str(line))):
                return self.fail(409, "bad")
            chat.sync_deletions()
        elif name in ("noimages", "images"):
            on = name == "noimages"
            changed = store.set_noimages(target, on, by, note=(by, name, whom, "")) if target is not None else store.set_noimages_by_reference(argument, on, by, note=(by, name, argument, ""))
            if not changed:
                return self.fail(409, "bad")
            chat.sync_images()
        else:
            if adminmod.change_picture(store, name, whom, state, lambda show, note: setter(show, (by,) + note[1:]), io.StringIO(), io.StringIO()):         # (the command line's own change, with this actor in the log's row)
                return self.fail(409, "bad")
            chat.sync_pictures()
        return http.response(200, {"ok": True})

