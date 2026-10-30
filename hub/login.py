"""The Discord login of the chat hub (docs/HUB.md, "Login"): who a visitor is, from one cookie and the one scope `identify`.

The pages (nginx passes these five and /hub/ws to the hub, docker/nginx.conf):
  GET  /hub/login?next=home|room   a random state (128 bits, one use, 10 minutes) is kept here and in a short cookie, and the visitor is sent to Discord
  GET  /hub/callback               Discord sends the visitor back with a code: the state of the cookie, of the address and of this hub must be one, the code is exchanged (hub/discord.py),
                                   the id and the username are kept (a login: the SHA-256 of a random cookie value, the id, the name, 30 days) and the visitor goes back to the front page
  GET  /hub/me                     who the cookie says the visitor is (same origin only)
  POST /hub/logout                 ends this login (same origin only: the Origin and Sec-Fetch-Site of a page of the site)
  POST /hub/forget                 ends every login of the account and deletes its chat lines (same origin only); a ban or a mute stays, by id, whatever the account does
and, for a WebSocket, identify(): the login's cookie in the handshake is who the socket is for its whole life.

Rules that the tests hold (tests/scripts/test_hub_login.py): the return address is one of two fixed words and never what the visitor wrote; a state is good for one callback whatever it
ends in; the cookie of the state is `__Host-` (a sibling subdomain cannot set it), so is the login's; a Discord that fails costs the visitor a calm page and the readers nothing; no
token, no code, no state, no cookie value, no id and no name is ever logged; a name is untrusted text (it is checked as a name and only ever written into a JSON body).
"""
import collections
import hashlib
import hmac
import re
import secrets
import sqlite3
import urllib.parse

from . import config, discord as discordmod, http, limits as limitmod, session, text

COOKIE = "__Host-ants_hub"                  # the login: a random 128-bit value, Path=/, Secure, HttpOnly, SameSite=Strict (only its SHA-256 is kept)
STATE_COOKIE = "__Host-ants_oauth"          # the state of a login on its way: SameSite=Lax, because Discord's way back is a navigation from another site
VALUE = re.compile(r"[0-9a-f]{32}\Z")
CODE = re.compile(r"[\x21-\x7e]{1,512}\Z")
NEXT = {"home": "/", "room": "/#room"}      # where the visitor goes back to: a fixed list, chosen by a word
DISCORD_EPOCH_MS = 1420070400000            # the time that the first bits of a Discord id count from
RESERVED = frozenset(("mod", "admin", "moderator", "administrator", "system", "server", "staff", "ants", "ants.bot", "hub", "everyone", "here"))      # (names that could pass for the hub's own voice)
LOOKALIKE = str.maketrans("013457", "oieast")           # (0 o, 1 i, 3 e, 4 a, 5 s, 7 t: a digit for the letter that it looks like)


def squash(name):
    """What a name looks like at a glance: no dots or underscores, a digit for the letter that it looks like read as that letter ("adm1n", "admin.", "a.d.m.i.n", "system_" are all "admin" and
    "system" to a reader). Digits at the end are not dropped: "system32", "hub99" and "ants1998" are names of real people, and a lookalike gains nothing (a moderator is the hub's `mod` flag, by
    Discord id, never a name)."""
    return re.sub(r"[._]", "", name.lower()).translate(LOOKALIKE)


RESERVED_SQUASHED = frozenset(squash(n) for n in RESERVED)


def reserved(name):
    return name in RESERVED or squash(name) in RESERVED_SQUASHED
MAX_QUERY = 1500

PAGES = {                                   # kind -> (status, heading, line)
    "unavailable": (503, "Login unavailable, try again later", "The chat can still be read without logging in."),
    "rejected": (400, "That login did not work", "Go back to the front page and press Log in again."),
    "expired": (400, "That login has run out", "It was used already, or it is older than ten minutes. Go back to the front page and press Log in again."),
    "cookie": (400, "Your browser did not keep the login cookie", "Allow cookies for this site and press Log in again."),
    "denied": (400, "Login cancelled", "Nothing was kept. The chat can still be read without logging in."),
    "name": (400, "This Discord name cannot be used here", "The chat takes names of lower-case letters, digits, underscores and dots."),
    "reserved": (400, "This Discord name cannot be used here", "It looks too much like a name that the chat keeps for itself. Change your Discord username and log in again."),
    "busy": (429, "Too many tries", "Wait a minute and press Log in again."),
    "site": (403, "Log in from the front page", "Open the front page and press Log in there."),
}
TEMPLATE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Ants! - Log in</title>
<style>
body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;font:18px/1.5 system-ui,sans-serif;color:#f4ece2;background:#0c1a15}
main{box-sizing:border-box;width:min(460px,calc(100% - 32px));margin:16px;padding:20px 22px;background:#070b0f;border:3px solid #2b6b4f;border-radius:4px;box-shadow:0 0 0 2px #050a08,3px 4px 0 2px #9d0d17}
h1{margin:0 0 8px;font-size:22px;font-weight:500;line-height:1.3;color:#f6d058}
p{margin:0 0 16px}
a{display:inline-flex;align-items:center;min-height:44px;padding:0 18px;color:#f4ece2;background:#2b6357;border:2px solid #050a08;border-radius:8px;text-decoration:none;box-shadow:3px 4px 0 #9d0d17}
a:focus-visible{outline:3px solid #f6d058;outline-offset:3px}
</style></head><body><main><h1>@HEADING@</h1><p>@LINE@</p><a href="@BACK@">Back to the front page</a></main></body></html>
"""
PAGE_HEADERS = (("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'"), ("Referrer-Policy", "no-referrer"),
                ("X-Frame-Options", "DENY"))


def digest(value):
    """What is kept of a cookie's value: its SHA-256 in hex (a copy of the database does not log anybody in)."""
    return hashlib.sha256(value.encode("ascii")).hexdigest()


def cookies_of(header):
    """name -> value of a Cookie header. A name that comes twice maps to None: a visitor's own cookie and one that a page of another site tossed cannot be told apart, so neither is believed."""
    found = {}
    for part in header.split(";"):
        name, equals, value = part.strip().partition("=")
        if equals and name:
            found[name] = None if name in found else value.strip()
    return found


def params_of(query):
    """name -> value of a query; a name that comes twice maps to None. A query that is too long, or has too many fields, is empty."""
    if len(query) > MAX_QUERY:
        return {}
    try:
        pairs = urllib.parse.parse_qsl(query, keep_blank_values=True, max_num_fields=30, errors="replace")
    except ValueError:
        return {}
    found = {}
    for name, value in pairs:
        found[name] = None if name in found else value
    return found


def cookie_line(name, value, seconds, same_site):
    return "%s=%s; Path=/; Max-Age=%d; Secure; HttpOnly; SameSite=%s" % (name, value, seconds, same_site)


class DiscordLogin(session.Sessions):
    login = True

    def __init__(self, cfg):
        self.cfg = cfg
        self.hub = self.store = self.limits = self.log = self.discord = None        # (attach() has them)
        self.states = collections.OrderedDict()                     # SHA-256 of a state -> (when it runs out, the word for where the visitor goes back to); in order of time
        self.routes = {"/hub/login": self.start, "/hub/callback": self.callback, "/hub/me": self.me, "/hub/logout": self.logout, "/hub/forget": self.forget}

    def attach(self, hub):
        self.hub, self.store, self.limits, self.log = hub, hub.store, hub.limits, hub.log
        self.discord = discordmod.Discord(self.cfg, self.limits)
        self.rate_login = limitmod.RateByAddress(self.limits.login_burst, self.limits.login_refill_seconds)
        self.rate_callback = limitmod.RateByAddress(self.limits.login_burst, self.limits.login_refill_seconds)
        self.rate_session = limitmod.RateByAddress(self.limits.session_burst, self.limits.session_refill_seconds)

    def forget_idle(self, now, wall):
        """Called by the hub's clock once a minute: the buckets that are full again, and the logins that were started and never finished."""
        for rate in (self.rate_login, self.rate_callback, self.rate_session):
            rate.forget_idle(now)
        self.sweep(wall)

    def sweep(self, wall):
        while self.states and next(iter(self.states.values()))[0] <= wall:
            self.states.popitem(last=False)

    # ---- who a socket is -------------------------------------------------------------------------------------------------------------------

    def young(self, account):
        """Whether the account is younger than ANTS_HUB_MIN_ACCOUNT_DAYS: its age is in its id (a Discord id counts milliseconds from 2015 in its first 42 bits)."""
        days = self.cfg.min_account_days
        if days <= 0:
            return False
        made = (int(account) >> 22) + DISCORD_EPOCH_MS
        return self.store.clock() * 1000 - made < days * 86400 * 1000

    def who_of(self, headers):
        """The Identity that the login cookie of a request names, or None (no cookie, one that is not ours, a login that ended or ran out)."""
        value = cookies_of(headers.get("cookie", "")).get(COOKIE)
        if not value or not VALUE.match(value):
            return None
        key = digest(value)
        row = self.store.session(key)
        if row is None:
            return None
        return session.Identity(row[0], row[1], self.young(row[0]), key, row[2])

    async def identify(self, headers):
        return self.who_of(headers)

    # ---- the answers -----------------------------------------------------------------------------------------------------------------------

    @staticmethod
    def fail(status, code, headers=None):
        return http.response(status, {"error": code}, headers)

    def page(self, kind, back="home", cookies=()):
        status, heading, line = PAGES[kind]
        html = TEMPLATE.replace("@HEADING@", heading).replace("@LINE@", line).replace("@BACK@", NEXT[back])
        return http.response(status, headers=list(PAGE_HEADERS) + [("Set-Cookie", c) for c in cookies], page=html)

    def clear(self, name, same_site):
        return cookie_line(name, "", 0, same_site)

    def same_origin(self, request, post):
        """A request that the front page itself made: the Origin of the site (always sent with a POST; a same-origin GET may have none) and Sec-Fetch-Site same-origin, both of them."""
        origin, site = request.headers.get("origin"), request.headers.get("sec-fetch-site")
        if post:
            return origin == self.cfg.origin and site == "same-origin"
        return (origin is None or origin == self.cfg.origin) and site == "same-origin"

    # ---- /hub/login ------------------------------------------------------------------------------------------------------------------------

    async def start(self, request, address):
        if request.method != "GET":
            return self.fail(405, "method", {"Allow": "GET"})
        site = request.headers.get("sec-fetch-site")
        if site is not None and site not in ("same-origin", "none"):        # (a link of another site, or of a sibling subdomain, cannot start a login: only the front page's button, or the address typed;
            return self.page("site")                                         # and it is turned away before the limit is asked, so that a hostile page cannot spend a visitor's tries)
        if not self.rate_login.take(address, self.hub.clock()):
            return self.page("busy")
        wanted = params_of(request.split()[1]).get("next")
        back = wanted if wanted in NEXT else "home"                          # (a word of the list, else Home: the address is never made from what was sent)
        wall = self.store.clock()
        self.sweep(wall)
        state = secrets.token_hex(16)
        self.states[digest(state)] = (wall + self.limits.state_seconds, back)
        while len(self.states) > self.limits.login_states_max:
            self.states.popitem(last=False)
        return http.response(302, headers=[("Location", self.discord.authorize_url(state)), ("Referrer-Policy", "no-referrer"),
                                           ("Set-Cookie", cookie_line(STATE_COOKIE, state, int(self.limits.state_seconds), "Lax"))])

    # ---- /hub/callback ---------------------------------------------------------------------------------------------------------------------

    async def callback(self, request, address):
        if request.method != "GET":
            return self.fail(405, "method", {"Allow": "GET"})
        if not self.rate_callback.take(address, self.hub.clock()):
            return self.page("busy")
        try:
            return await self.finish(request, address)
        except Exception as e:                                               # (a bug costs one visitor a calm page, never the hub)
            self.log.event("login_failed", kind="bug", error=type(e).__name__)
            return self.page("unavailable", cookies=[self.clear(STATE_COOKIE, "Lax")])

    async def finish(self, request, address):
        params = params_of(request.split()[1])
        cookies = cookies_of(request.headers.get("cookie", ""))
        gone = [self.clear(STATE_COOKIE, "Lax")]
        state, mine = params.get("state"), cookies.get(STATE_COOKIE)
        kept = None
        if isinstance(state, str) and VALUE.match(state):
            kept = self.states.pop(digest(state), None)                      # (one use: the state is gone whatever the rest of this call ends in; nothing waits between here and the pop)
        back = kept[1] if kept else "home"
        if not mine or not VALUE.match(mine):
            return self.page("cookie", back, gone)
        if not isinstance(state, str) or not VALUE.match(state) or not hmac.compare_digest(mine.encode("ascii"), state.encode("ascii")):
            return self.page("expired", back, gone)
        if kept is None or kept[0] <= self.store.clock():
            return self.page("expired", back, gone)
        if params.get("error") is not None:                                  # (the visitor said no at Discord, or Discord refused: `error` is not shown and not kept)
            return self.page("denied", back, gone)
        code = params.get("code")
        if not isinstance(code, str) or not CODE.match(code):
            return self.page("expired", back, gone)
        try:
            account, name = await self.discord.who(code)
        except discordmod.DiscordError as e:
            self.log.event("login_failed", kind=e.kind, status=e.status, tag=self.log.tag(address))
            return self.page("rejected" if e.kind == "rejected" else "unavailable", back, gone)
        if not config.ACCOUNT_ID.match(account) or not text.valid_name(name):
            self.log.event("login_failed", kind="name", tag=self.log.tag(address))
            return self.page("name", back, gone)
        if reserved(name):
            self.log.event("login_failed", kind="reserved", tag=self.log.tag(address))
            return self.page("reserved", back, gone)
        value = secrets.token_hex(16)
        try:
            self.store.add_session(digest(value), account, name, self.limits.session_seconds, self.limits.sessions_per_account, self.limits.sessions_total)
            old = cookies.get(COOKIE)
            if old and VALUE.match(old):                                      # (a login that this browser had is over: a new one never takes over the value of an old one)
                self.store.delete_session(digest(old))
                self.hub.end_logins(session=digest(old))
        except sqlite3.OperationalError as e:
            self.log.event("login_failed", kind="database", error=type(e).__name__)
            return self.page("unavailable", back, gone)
        self.log.event("login", tag=self.log.tag(address))
        return http.response(302, headers=[("Location", NEXT[back]), ("Referrer-Policy", "no-referrer"), ("Set-Cookie", cookie_line(COOKIE, value, int(self.limits.session_seconds), "Strict")),
                                           ("Set-Cookie", gone[0])])

    # ---- /hub/me, /hub/logout, /hub/forget -------------------------------------------------------------------------------------------------

    def guard(self, request, address, method):
        """None when the request may go on, else the answer that refuses it."""
        if request.method != method:
            return self.fail(405, "method", {"Allow": method})
        if request.split()[1]:
            return self.fail(404, "not_found")
        if not self.rate_session.take(address, self.hub.clock()):
            return self.fail(429, "rate", {"Retry-After": "5"})
        if not self.same_origin(request, method == "POST"):
            return self.fail(403, "origin")
        return None

    async def me(self, request, address):
        refused = self.guard(request, address, "GET")
        if refused:
            return refused
        try:
            who = self.who_of(request.headers)
        except sqlite3.OperationalError:
            return self.fail(503, "busy")
        return http.response(200, {"login": True, "you": {"name": who.name, "mod": who.account_id in self.cfg.mods} if who else None})

    async def logout(self, request, address):
        refused = self.guard(request, address, "POST")
        if refused:
            return refused
        value = cookies_of(request.headers.get("cookie", "")).get(COOKIE)
        try:
            if value and VALUE.match(value):
                self.store.delete_session(digest(value))
                self.hub.end_logins(session=digest(value))
        except sqlite3.OperationalError:
            return self.fail(503, "busy")
        self.log.event("logout", tag=self.log.tag(address))
        return http.response(200, {"ok": True}, [("Set-Cookie", cookie_line(COOKIE, "", 0, "Strict"))])

    async def forget(self, request, address):
        refused = self.guard(request, address, "POST")
        if refused:
            return refused
        try:
            who = self.who_of(request.headers)
            if who is None:
                return self.fail(401, "login", [("Set-Cookie", cookie_line(COOKIE, "", 0, "Strict"))])
            lines = self.store.forget(who.account_id, self.hub.chat.deleted_seq)
        except sqlite3.OperationalError:
            return self.fail(503, "busy")
        self.hub.end_logins(account=who.account_id)
        self.hub.chat.drop_lines(lines)
        self.log.event("forget", tag=self.log.tag(address), lines=len(lines))
        return http.response(200, {"ok": True}, [("Set-Cookie", cookie_line(COOKIE, "", 0, "Strict"))])
