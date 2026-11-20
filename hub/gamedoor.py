"""The hub's call to the game server's admin door (docs/HUB_COMMANDS.md, "The game door"): what `/bots` does is done there, with the pass and the token that the admin page's calls carry.

A call is `GET` or `POST` to `<ANTS_HUB_GAME_ADMIN_URL>/admin/api/<tail>` (the tail is one of the four of `Tails`, never text from a person). Its headers are `X-Ants-Admin-Pass`, a pass that is
made for this one call (`v1.<payload>.<mac>`, hub/admindoor.py `sign`, the key and the label of the admin page's door, the payload `exp`, `name` (the Discord username), `nonce` (a fresh one every
time), `ref` (the account's reference) and `role` (`admin`, or `owner` for the operator's account), and nothing else: never an id), and, for a POST, `X-Ants-Csrf`, the game server's own token
for that reference, `Content-Type: application/json` and `Content-Length`. No cookie and no `Sec-Fetch-Site`.

A small HTTP/1.1 client of its own, as hub/discord.py is: the answer of another program is data (the connection is made in 3 seconds and the whole call ends in 10; the head and the body together are
read up to 128 KB and not a byte more; a redirect is a failure; the status line, the headers and the lengths are read strictly with the reader that hub/discord.py has; and the answer must be a
JSON object). Every failure is a `DoorError` that has a kind and nothing else: no address, no header, no pass and no text of any cause is in it or in what is told to a page.

The list of maps (`GET bot-options`) is kept for five minutes. A map that is typed is looked up in it (`find_map`): the only text that ever goes to the game server about a map is the file name that
the list itself has.
"""
import asyncio
import hashlib
import hmac
import json
import secrets

from . import discord as discordmod

PATH = "/admin/api/"
PASS_HEADER = "X-Ants-Admin-Pass"
CSRF_HEADER = "X-Ants-Csrf"
PASS_LABEL = b"ants-admin-pass-v1"                  # the key of the pass is the same as the admin page's door makes (hub/admindoor.py Door.key)
CSRF_LABEL = b"ants-admin-csrf-server-v1"            # the game server's own token's label (src/ants_server/admin_pass.cpp): not the label of the page's token
USER_AGENT = "AntsHub/1"
PASS_SECONDS = 60
MAP_MAX = 96
CLOSE_MAX = 3
TEXT_MAX = 40
Tails = ("bot-options", "bot-matches", "bot-matches/summary", "bot-matches/stop-all")
read_answer = discordmod.Discord.read_answer         # (it uses nothing of a client: the one strict reader of an answer's head, lengths and chunks)


class DoorError(Exception):
    """The game server could not be used: `kind` is `off` (no door), `down` (no connection, no answer in time, not an HTTP answer, too big, a redirect) or `bad` (an answer that is not what was asked for).
    Nothing else: no address, no pass, no cause."""

    def __init__(self, kind):
        super().__init__(kind)
        self.kind = kind


def refuse_constant(name):
    raise ValueError(name)


def plain_text(value):
    """What may be told of a name that the game server says (the one who started a batch): text of at most 40 characters without a control character, and nothing that is all digits and as long as an id."""
    if not isinstance(value, str):
        return ""
    value = "".join(c for c in value if c.isprintable())[:TEXT_MAX]
    return "" if value.isdigit() and len(value) >= 15 else value


class GameDoor:
    def __init__(self, url, secret, limits, clock, wall, ref, is_owner):
        """`url` is `http://host:port` or '' (off), `secret` the admin page's one key as text or '' (off), `clock` the monotonic clock (the age of the list of maps), `wall` the clock of seconds since 1970
        (a pass's life), `ref(account)` the account's reference and `is_owner(account)` whether it is the operator's."""
        self.limits = limits
        self.clock = clock
        self.wall = wall
        self.ref = ref
        self.is_owner = is_owner
        self.host = self.port = self.key = self.csrf_key = None
        if url and secret:
            host, _, port = url[len("http://"):].rpartition(":")
            self.host, self.port = host, int(port)
            raw = secret.encode("ascii")
            self.key = hmac.new(raw, PASS_LABEL, hashlib.sha256).digest()
            self.csrf_key = hmac.new(raw, CSRF_LABEL, hashlib.sha256).digest()
        self.maps = None                             # [(file name, playable)] as the game server listed them, and until when (the monotonic clock)
        self.maps_until = float("-inf")

    @property
    def on(self):
        """Whether `/bots` can be done at all: the address and the key are both set."""
        return self.key is not None

    # ---- one call ----------------------------------------------------------------------------------------------------------------------------------

    async def call(self, who, method, tail, payload=None, body_max=None):
        """(status, JSON object) of one call for the account of `who` (an Identity), or a DoorError. `payload` is the JSON of a POST."""
        if not self.on:
            raise DoorError("off")
        try:
            return await asyncio.wait_for(self.exchange(who, method, tail, payload, body_max or self.limits.door_body_max), self.limits.door_seconds)
        except DoorError:
            raise
        except (asyncio.TimeoutError, OSError, ValueError, EOFError, UnicodeError, RecursionError, discordmod.DiscordError):
            pass                                      # (raised below, outside the handler: what the cause said can hold the address)
        raise DoorError("down")

    def headers_of(self, who, post):
        from . import admindoor                       # (here and not at the top: hub/admindoor.py reads hub/chat.py, which reads the commands, which read this file)
        ref = self.ref(who.account_id)
        pass_text = admindoor.sign(self.key, {"exp": int(self.wall()) + PASS_SECONDS, "name": who.username, "nonce": secrets.token_hex(8), "ref": ref,
                                              "role": "owner" if self.is_owner(who.account_id) else "admin"})
        headers = [(PASS_HEADER, pass_text)]
        if post:
            headers += [(CSRF_HEADER, hmac.new(self.csrf_key, b"csrf1\0" + ref.encode("ascii"), hashlib.sha256).hexdigest()[:32]), ("Content-Type", "application/json")]
        return headers

    async def exchange(self, who, method, tail, payload, body_max):
        assert tail in Tails
        post = method == "POST"
        body = json.dumps(payload, separators=(",", ":")).encode("ascii") if post else b""
        lines = ["%s %s%s HTTP/1.1" % (method, PATH, tail), "Host: %s:%d" % (self.host, self.port), "User-Agent: " + USER_AGENT, "Accept: application/json", "Accept-Encoding: identity", "Connection: close"]
        lines += ["%s: %s" % kv for kv in self.headers_of(who, post)]
        if post:
            lines.append("Content-Length: %d" % len(body))
        reader, writer = await asyncio.wait_for(asyncio.open_connection(self.host, self.port, limit=65536), self.limits.door_connect_seconds)
        try:
            writer.write(("\r\n".join(lines) + "\r\n\r\n").encode("ascii") + body)
            await writer.drain()
            status, answer, _ = await read_answer(None, reader, body_max)
        finally:
            writer.transport.abort()                  # (at once: the call is over, and nothing waits for a polite close)
        if 300 <= status < 400:
            raise DoorError("down")                   # (a redirect is never followed)
        data = json.loads(answer.decode("utf-8"), parse_constant=refuse_constant) if answer else None
        if not isinstance(data, dict):
            raise DoorError("bad")
        return status, data

    # ---- what `/bots` asks ---------------------------------------------------------------------------------------------------------------------------

    async def start(self, who, request):
        return await self.call(who, "POST", "bot-matches", request)

    async def stop_all(self, who):
        return await self.call(who, "POST", "bot-matches/stop-all", {})

    async def summary(self, who):
        return await self.call(who, "GET", "bot-matches/summary")

    async def catalog(self, who):
        """[(file name, playable)] of the maps that the game server has, as it listed them within the last five minutes (a list that is longer than 2,000, or is not of the shape, is not read at all; a map
        that is not of the shape of a file name is left out of it)."""
        now = self.clock()
        if self.maps is not None and now < self.maps_until:
            return self.maps
        status, data = await self.call(who, "GET", "bot-options", body_max=self.limits.catalog_body_max)
        rows = data.get("maps")
        if status != 200 or not isinstance(rows, list) or len(rows) > self.limits.catalog_entries_max:
            raise DoorError("bad")
        found = []
        for row in rows:
            name = row.get("file") if isinstance(row, dict) else None
            if isinstance(name, str) and 1 <= len(name) <= MAP_MAX and "/" not in name and "\\" not in name and all(c.isprintable() for c in name):
                found.append((name, row.get("playable") is not False))
        self.maps, self.maps_until = found, now + self.limits.catalog_seconds
        return found


def find_map(maps, typed):
    """(`ok`, the file name) for the one map of the list that the typed name means (the file name, or the file name without `.lvl`, in any letter case); (`no_map`, None) when that map is one that the
    server cannot play; (`bad_map`, up to three names that start with the typed text or hold it, the shortest first, playable ones only) when no map or several (two files that differ in letter case only)
    are meant. The text that was typed is data: it is compared and never sent."""
    want = typed.casefold()
    exact = [(name, ok) for name, ok in maps if name.casefold() == want or (name.casefold().endswith(".lvl") and name.casefold()[:-4] == want)]
    if len(exact) == 1:
        return ("ok", exact[0][0]) if exact[0][1] else ("no_map", None)
    pool = exact if exact else [(name, ok) for name, ok in maps if ok and (name.casefold().startswith(want) or want in name.casefold())]
    return "bad_map", [name for name, _ in sorted(pool, key=lambda item: (len(item[0]), item[0]))[:CLOSE_MAX]]
