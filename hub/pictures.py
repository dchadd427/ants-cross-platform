"""The small copy of an account's Discord profile picture (docs/HUB.md, "Pictures"): fetched after a login and again while the person is logged in when Discord says the picture changed, checked hard, rewritten to a bare PNG, kept in hub.db and served at a name of its own.

After a login is answered, in the background, the hub asks Discord's image host for the one 64 pixel PNG of the picture whose short code the login saw (hub/discord.py `picture()`: the address
is made there from constants and the two checked parts, no redirect is followed, nothing is read past a cap). The login never waits for it and never fails because of it: every failure is a
letter in the page's circle (no picture kept), and one line in the log that says which kind (`picture kind=ok|none|down|type|big|shape|busy|bug`, with the count of dropped fetches or the class of an error, nothing else: no id, no code, no name, no reference).

What Discord sends is data, not a thing to trust. It is kept only when it is a PNG whose every chunk is whole (a length that fits, a CRC that matches), whose header is first and says 8 to 128 pixels
and a legal depth and color, that ends with IEND and has nothing after it, and whose data is a whole zlib stream that inflates to exactly the rows that the header says (never inflated past that). The hub then writes its own copy, which holds only the header, the palette, the transparency, the picture data and
the end: every other chunk (text, EXIF, color profiles, the animation chunks of an APNG) is dropped. The copy is what is kept and served; its pixels are not decoded here.

The copy is kept under a random 128-bit name that is new at every fetch (`secrets.token_hex(16)`; nothing in it comes from the id, the reference, the code or the bytes), by the account's keyed
reference (hub/pseudo.py) and never by its id. It is served at /hub/pic/<name>.png with fixed headers (hub/http.py `picture()`). It is deleted with the login's end, a kick, Forget me, the person's
switch, a ban, a block, or when the login runs out (the store's rule: no login, no picture); a fetch that ends after any of these keeps nothing; a name that is not known, is not shown or is past its time answers 404.

Refreshing (no new login needed). The roles' check (hub/roles.py), which already asks Discord about every person who is here, hands over the picture's short code that the member answer carries
(`refresh`); a person who is not in the server, or a hub whose roles are off, is asked with the bot's token for the person's public profile (`GET /users/<id>`, `lookup`) when they connect and no
look at Discord has been made yet this day, and then at most once a day. What is remembered to tell a changed picture is a keyed mark of the code (hub/pseudo.py `keyed`: not the code, not the id), in memory
only, for at most `pictures_rows_max` accounts: no schema, no file; a hub that was restarted fetches each picture once more, at the pictures' own pace. The same code as the copy on hand was made from
is no fetch; a different one, or none kept, is a fetch (the same checks, caps, pace and ends as after a login); no picture at Discord deletes the copy. The lookups take their turns from the roles' bucket
(two a second for all the calls to Discord's API together) or, where roles are off, from a bucket of their own at the pictures' pace; Discord's own "wait" and a refused token stop them. Nothing is
looked at for an account that switched its picture off, that the operator hid or that is banned. A failed lookup or fetch leaves what there was.

A person's id is used in one place here: the path of a fetch or of a lookup. It is not kept, not logged, not in an error.
"""
import asyncio
import collections
import re
import secrets
import zlib

from . import discord as discordmod, http, limits as limitmod, pseudo
from .store import StoreError
from .store.base import SHOW_BLOCKED, SHOW_OFF, SHOW_ON

PATH = re.compile(r"/hub/pic/([0-9a-f]{32})\.png\Z")
SIGNATURE = b"\x89PNG\r\n\x1a\n"
DEPTHS = {0: (1, 2, 4, 8, 16), 2: (8, 16), 3: (1, 2, 4, 8), 4: (8, 16), 6: (8, 16)}       # color type -> the bit depths that the format allows with it
LOOKUP = object()                                           # (in the queue in place of a picture code: the person's profile is asked for first)
SIDE_MIN, SIDE_MAX = 8, 128                                 # pixels, each side
CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}
PASSES = ((0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2))      # (first column, first row, column step, row step) of the seven passes of an interlaced file


class ShapeError(Exception):
    """What was fetched is not a PNG that is kept."""


def raw_size(width, height, color, depth, interlace):
    """How many bytes the picture data inflates to: each row has a filter byte and its pixels, to a whole byte (an interlaced file: the rows of its seven passes)."""
    bits = CHANNELS[color] * depth
    if not interlace:
        return height * (1 + (width * bits + 7) // 8)
    total = 0
    for x0, y0, dx, dy in PASSES:
        columns, rows = (width - x0 + dx - 1) // dx, (height - y0 + dy - 1) // dy
        if columns > 0 and rows > 0:
            total += rows * (1 + (columns * bits + 7) // 8)
    return total


def rewrite(data):
    """The bare PNG made from `data`, or a ShapeError. Every length and every CRC is checked, the header comes first, IEND is last and nothing follows it; only the header, PLTE, tRNS, the IDATs and IEND are
    written (in the order they came); a critical chunk that is not one of those is a ShapeError."""
    if not isinstance(data, (bytes, bytearray)) or data[:8] != SIGNATURE:
        raise ShapeError
    data = bytes(data)
    at, out, header, ended, seen, phase, color, palette, total, pieces = 8, [SIGNATURE], False, False, set(), 0, 0, 0, 0, []       # phase: 0 before the picture data, 1 in its run of IDATs, 2 after it
    while at < len(data):
        if ended or len(data) - at < 12:
            raise ShapeError                                 # (a byte after IEND, or a chunk with no room for its head and its CRC)
        length = int.from_bytes(data[at:at + 4], "big")
        if length > 0x7fffffff or length > len(data) - at - 12:
            raise ShapeError
        kind, body = data[at + 4:at + 8], data[at + 8:at + 8 + length]
        if not re.fullmatch(rb"[A-Za-z]{4}", kind) or zlib.crc32(kind + body) != int.from_bytes(data[at + 8 + length:at + 12 + length], "big"):
            raise ShapeError
        chunk = data[at:at + 12 + length]
        at += 12 + length
        if not header:
            if kind != b"IHDR" or length != 13:
                raise ShapeError
            width, height = int.from_bytes(body[0:4], "big"), int.from_bytes(body[4:8], "big")
            depth, color, compression, filtering, interlace = body[8:13]
            if not (SIDE_MIN <= width <= SIDE_MAX and SIDE_MIN <= height <= SIDE_MAX) or depth not in DEPTHS.get(color, ()) or compression or filtering or interlace not in (0, 1):
                raise ShapeError
            header = True
            out.append(chunk)
            continue
        if kind == b"IHDR" or (kind in (b"PLTE", b"tRNS") and (kind in seen or phase)):
            raise ShapeError                                 # (a second header, a second palette, a palette after the picture data)
        seen.add(kind)
        if kind == b"IDAT":
            if phase == 2:
                raise ShapeError                             # (picture data that is not in one run)
            phase, total = 1, total + length
            pieces.append(body)
            out.append(chunk)
            continue
        phase = 2 if phase else 0
        if kind == b"IEND":
            if length:
                raise ShapeError
            ended = True
            out.append(chunk)
        elif kind == b"PLTE":
            if color in (0, 4) or length % 3 or not 3 <= length <= 768:
                raise ShapeError
            palette = length // 3
            out.append(chunk)
        elif kind == b"tRNS":
            if color in (4, 6) or (color == 0 and length != 2) or (color == 2 and length != 6) or (color == 3 and (not palette or length > palette)):
                raise ShapeError
            out.append(chunk)
        elif kind[0:1].isupper():
            raise ShapeError                                 # (a critical chunk that this reader does not know)
    if not ended or not total or (color == 3 and not palette):
        raise ShapeError
    expected = raw_size(width, height, color, depth, interlace)        # (the data must be a whole zlib stream that inflates to exactly the rows that the header says: at most 128 x 128 x 8 bytes and a bit more, and it is never inflated past that)
    try:
        inflater = zlib.decompressobj()
        inflated = inflater.decompress(b"".join(pieces), expected + 1)
    except zlib.error:
        raise ShapeError
    if len(inflated) != expected or not inflater.eof or inflater.unused_data:
        raise ShapeError
    return b"".join(out)


class Pictures:
    """The fetch, the pacing and the serving. `offer` is what the login calls; `serve` is the route of /hub/pic/<name>.png."""

    def __init__(self, store, discord, limits, log, clock, token="", roles=None):
        self.store, self.discord, self.limits, self.log, self.clock = store, discord, limits, log, clock
        self.token = token                                   # the bot's token (None or empty: no lookup of a person's profile; the login and the roles' check still hand codes over)
        self.roles = roles                                   # hub/roles.py Roles (its answers carry codes; its bucket lends the lookups their turns) or None
        self.codes = {}                                      # {reference: keyed mark of the code} of the copies made, in memory only (see the top of this file)
        self.looked = {}                                     # {reference: when Discord was last looked at for the account's picture (a member answer or a profile)}
        self.users = limitmod.TokenBucket(limits.picture_burst, limits.picture_refill_seconds, clock())      # the turns of the lookups where the roles' bucket is not there to lend them
        self.failed = {}                                     # {reference: (mark of a code, when)}: a copy of that code that could not be made for good reason (a 404, not a PNG, too big): not tried again within `picture_lookup_seconds`
        self.hold_until = float("-inf")                      # no lookup before this (Discord said wait, or refused the token)
        self.waiting = collections.deque()                   # (account id, picture code) of the fetches that wait for their turn: in memory only, at most `picture_waiting`
        self.inflight = 0
        self.tasks = set()
        self.queued = set()                                  # the references of the accounts whose fetch waits or runs: one each, so a burst of logins of one account is one fetch
        self.dropped = 0                                     # fetches dropped since the last line of the log
        self.stopping = False                                # the hub is stopping: nothing new is started
        self.dropped_at = float("-inf")
        self.bucket = limitmod.TokenBucket(limits.picture_burst, limits.picture_refill_seconds, clock())
        self.rate = limitmod.RateByAddress(limits.picture_serve_burst, limits.picture_serve_refill_seconds)
        self.seen = None                                     # the number of the last change of the pictures that sync() read
        self.cache = {}                                      # {reference: name} of the pictures that may be shown, as sync() last read them
        self.marks = {}                                      # {reference: SHOW_OFF or SHOW_BLOCKED} of the accounts that are not in the default state
        self.ends = None                                     # the soonest end of a picture in `cache`: past it, the list is read again for time alone
        self.guessed = False                                 # a page was answered before the first look could be made (the database refused it)
        self.on_change = None                                # called with the set of references whose name or state changed, each time sync() finds a change (the chat tells their sockets)

    # ---- the fetch ---------------------------------------------------------------------------------------------------------------------------

    def offer(self, account, avatar):
        """What a finished login hands over: the account and the short code of its picture (None for none). It decides at once, never waits and never raises: an account whose picture is off, hidden or banned or that has no login (`allowed`), one
        whose fetch is waiting or running already, a full queue: nothing is fetched (and the drop is counted and told). A new login always gets its fetch: the next login is the retry. No picture at
        Discord deletes the older copy of ours."""
        try:
            if self.stopping:
                return
            self.store.picture_state(account, touch=True)          # (a login renews the time of an `off` that the person set)
            if not self.allowed(account):
                return
            if avatar is None:
                self.store.clear_picture(account)
                self.log.event("picture", kind="none")
                return self.sync()
            self.enqueue(account, avatar, pseudo.reference(self.store.secret(), account))
        except Exception as e:                                # (a bug or a refusing database costs one picture, never the login)
            self.log.event("picture", kind="bug", error=type(e).__name__)

    def enqueue(self, account, avatar, key, quiet=False):
        """One fetch (or lookup, `avatar` is LOOKUP) waits for its turn: one for an account at a time, at most `picture_waiting` in all (the rest is dropped, and counted unless `quiet`)."""
        if key in self.queued or len(self.waiting) >= self.limits.picture_waiting:
            return None if quiet else self.drop(self.clock())
        self.queued.add(key)
        self.waiting.append((account, avatar, key))
        self.pump()

    # ---- refreshing: no new login ------------------------------------------------------------------------------------------------------------

    def refresh(self, account, avatar):
        """What the roles' check hands over for a person who is here: the short code of the picture that Discord's member answer carries (None: no picture; the shape is checked by the caller). The same
        code as the copy was made from is nothing; another one, or none kept, is a fetch. Never waits, never raises."""
        try:
            if self.stopping:
                return
            key = pseudo.reference(self.store.secret(), account)
            if self.settle(account, avatar, key):
                self.enqueue(account, avatar, key, quiet=True)
        except Exception as e:
            self.log.event("picture", kind="bug", error=type(e).__name__)

    def want(self, account):
        """A logged-in person connected. Where the roles' check will say what the picture is (roles on, and the person is not the owner account that is never checked) nothing is done here; else the person's
        profile is asked for, if that is due (`ask`)."""
        try:
            roles = self.roles
            if getattr(roles, "on", False) is True and not (roles.owner and account == roles.owner):
                return
            self.ask(account)
        except Exception as e:
            self.log.event("picture", kind="bug", error=type(e).__name__)

    def absent(self, account):
        """The roles' check says that the person is not in the server: their profile is asked for, if that is due (`ask`)."""
        try:
            self.ask(account)
        except Exception as e:
            self.log.event("picture", kind="bug", error=type(e).__name__)

    def ask(self, account):
        """Queue a lookup of the person's profile when it is due: no look at Discord was made for the account within `picture_lookup_seconds` (one a day), there is a bot token, nothing is stopped (the picture
        switched off, hidden, banned) and none is queued. An account whose copy was made just now, from a login, counts as looked at."""
        if not self.token or self.stopping or not self.allowed(account):
            return
        key, now = pseudo.reference(self.store.secret(), account), self.clock()
        last = self.looked.get(key)
        if last is None and key in self.codes and self.store.has_picture(account):
            return self.note_look(key)
        if last is not None and now - last < self.limits.picture_lookup_seconds:
            return
        self.enqueue(account, LOOKUP, key, quiet=True)

    def allowed(self, account):
        """Whether Discord may be looked at, or a copy made, for the account now: its picture is on (not switched off, not hidden by the operator), it is not banned and it has a login (no login, no picture:
        a person who is not logged in is never looked up, whatever asked). Asked again right before every call to Discord, because the request may have waited."""
        return self.store.picture_state(account) == SHOW_ON and not self.store.is_banned(account) and self.store.has_login(account)

    def settle(self, account, avatar, key):
        """Whether a fetch is needed for the code that Discord says (None: none), and what is done at once when none is wanted: nothing for a person whose picture is off, hidden or banned; the copy deleted
        for a person who has no picture now."""
        if not self.allowed(account):
            return False
        self.note_look(key)
        have = self.store.has_picture(account)
        if avatar is None:
            self.codes.pop(key, None)
            if have:
                self.store.clear_picture(account)
                self.log.event("picture", kind="none")
                self.sync()
            return False
        mark, failed = self.mark(avatar), self.failed.get(key)
        if failed is not None and failed[0] == mark and self.clock() - failed[1] < self.limits.picture_lookup_seconds:
            return False                                      # (this very picture could not be made a short while ago: it is asked for again after a day, or when the code changes)
        return not (have and self.codes.get(key) == mark)

    def give_up(self, key, avatar):
        """A copy of this code could not be made, for a reason that will not change by asking again at once (not a picture, too big, gone): not tried again for a day."""
        if key is not None:
            self.failed.pop(key, None)
            self.failed[key] = (self.mark(avatar), self.clock())
            while len(self.failed) > self.limits.pictures_rows_max:
                del self.failed[next(iter(self.failed))]

    def mark(self, avatar):
        return pseudo.keyed(self.store.secret(), "picture-code", avatar)

    def note_look(self, key):
        self.looked.pop(key, None)
        self.looked[key] = self.clock()
        while len(self.looked) > self.limits.pictures_rows_max:
            del self.looked[next(iter(self.looked))]

    def remember(self, key, avatar):
        self.failed.pop(key, None)
        self.codes.pop(key, None)
        self.codes[key] = self.mark(avatar)
        while len(self.codes) > self.limits.pictures_rows_max:
            del self.codes[next(iter(self.codes))]

    async def turn(self):
        """A turn for a lookup: from the roles' bucket where roles are on, else from this one. False when there is none within about ten seconds or the lookups are stopped (the person is asked at a later connect)."""
        for _ in range(40):
            if self.clock() < self.hold_until:
                return False
            taken = self.roles.spare_turn() if getattr(self.roles, "on", False) is True else self.users.take(self.clock())
            if taken is None:
                return False
            if taken:
                return True
            await asyncio.sleep(0.25)
        return False

    async def lookup(self, account, key):
        """(True, code or None) of the person's profile at Discord, or (False, None) when there is no answer to believe; one line of the log when there is not, and the token's refusal or a "wait" stops lookups."""
        if not await self.turn() or not self.allowed(account):
            return False, None                                   # (no turn; or the picture was switched off, hidden, the person banned or logged out while the request waited: no call)
        try:
            status, data, rl = await self.discord.user(account, self.token)
        except discordmod.DiscordError:
            self.log.event("picture", kind="down")
            return False, None
        now = self.clock()
        remaining, reset = rl.get("remaining"), rl.get("reset_after")
        if remaining is not None and remaining < 1 and reset is not None:
            self.hold_until = max(self.hold_until, now + min(reset, 60.0))
        if status == 429:
            wait = min(max(rl.get("retry_after", 5.0), 0.5), 60.0)
            self.hold_until = max(self.hold_until, now + wait)
            if getattr(self.roles, "on", False) is True:
                self.roles.hold_until = max(self.roles.hold_until, now + wait)          # (the roles' lookups wait too: a limit of the whole bot is meant for all of them)
        elif status == 404 and data.get("code") == 10013:
            self.note_look(key)                                  # (Discord does not know the account: nothing to change, and not asked again for a day)
        elif status in (401, 403):
            self.hold_until = max(self.hold_until, now + 600.0)
        valid, code = discordmod.Discord.avatar_code(data.get("avatar")) if status == 200 and "avatar" in data and data.get("id") == account else (False, None)
        if not valid:
            self.log.event("picture", kind="down")
            return False, None
        return True, code

    def drop(self, now):
        """One more fetch that was not made: counted, and told in one line at most every `picture_dropped_log_seconds` (the count only: no account)."""
        self.dropped += 1
        if now - self.dropped_at >= self.limits.picture_dropped_log_seconds:
            self.tell_drops(now)

    def tell_drops(self, now):
        if self.dropped:
            self.log.event("picture", kind="busy", dropped=self.dropped)
            self.dropped, self.dropped_at = 0, now

    def pump(self):
        while self.waiting and self.inflight < self.limits.picture_inflight:
            account, avatar, key = self.waiting.popleft()
            self.inflight += 1
            task = asyncio.ensure_future(self.work(account, avatar, key))
            self.tasks.add(task)
            task.add_done_callback(self.tasks.discard)

    async def work(self, account, avatar, key):
        try:
            if avatar is LOOKUP:
                ok, avatar = await self.lookup(account, key)
                if not ok:
                    return
                if not self.settle(account, avatar, key):
                    return
            await self.pace()
            if not self.allowed(account):
                return                                           # (the turn was long: nothing is asked of Discord's image host for a picture that went off, a banned or a logged out person)
            await self.fetch(account, avatar, key)
        except asyncio.CancelledError:
            raise
        except Exception as e:
            self.log.event("picture", kind="bug", error=type(e).__name__)
        finally:
            self.inflight -= 1
            self.queued.discard(key)
            self.pump()

    async def pace(self):
        """The whole hub's turn: a burst of `picture_burst`, then one every `picture_refill_seconds`."""
        while not self.bucket.take(self.clock()):
            await asyncio.sleep(self.limits.picture_refill_seconds / 4)

    async def fetch(self, account, avatar, key=None):
        """One fetch, check and keeping, ending in one line of the log. Nothing changes when it fails, but for a 404 (the picture went away at Discord), which deletes the older copy."""
        try:
            status, content_type, body = await self.discord.picture(account, avatar)
        except discordmod.DiscordError as e:
            if e.kind == "big":
                self.give_up(key, avatar)
            return self.log.event("picture", kind="big" if e.kind == "big" else "down")
        if status == 404:
            self.codes.pop(key, None)
            self.store.clear_picture(account)
            self.sync()
            self.give_up(key, avatar)                                # (after the look at the pictures: it forgets what it held for an account whose copy went)
            return self.log.event("picture", kind="none")
        if status != 200:
            return self.log.event("picture", kind="down")
        if content_type != "image/png":
            self.give_up(key, avatar)
            return self.log.event("picture", kind="type")
        if len(body) > self.limits.picture_bytes_max:
            self.give_up(key, avatar)
            return self.log.event("picture", kind="big")
        try:
            png = rewrite(body)
        except ShapeError:
            self.give_up(key, avatar)
            return self.log.event("picture", kind="shape")
        kept = self.store.set_picture(account, png, secrets.token_hex(16), self.store.clock() + self.limits.session_seconds, self.limits.pictures_rows_max, self.limits.pictures_bytes_max)
        if kept and key is not None:
            self.remember(key, avatar)
        self.log.event("picture", kind="ok" if kept else "none")       # (not kept: the person switched it off, the login ended or the account was banned while it was on its way)
        self.sync()

    def forget_idle(self, now):
        for table in (self.looked, self.failed):
            for ref in [ref for ref, entry in table.items() if now - (entry[1] if isinstance(entry, tuple) else entry) >= self.limits.picture_lookup_seconds]:
                del table[ref]                                    # (a day old: it no longer holds anything back)
        self.rate.forget_idle(now)
        self.tell_drops(now)

    def stop(self):
        """The hub stops: the queue is emptied first and nothing new starts (a task that ends gives its place back and would start the next one), then the running fetches are cancelled."""
        self.stopping = True
        self.waiting.clear()
        self.queued.clear()
        for task in list(self.tasks):
            task.cancel()

    # ---- the names ---------------------------------------------------------------------------------------------------------------------------

    def sync(self):
        """Read the pictures again when the store's counter says that something changed (here, or in another process: the command line) or when the soonest picture has run out, and tell the chat which
        references changed (their name or their state): the hub's clock calls it once a tick, and whatever changes a picture here calls it at once. One small read when nothing changed."""
        number = self.store.last_picture_change()
        if self.seen == number and (self.ends is None or self.ends > self.store.clock()):
            return
        number, names, marks, ends = self.store.picture_look()                # (one moment of the data: the names and the marks of a look agree)
        changed = {ref for ref in set(self.cache) | set(names) if self.cache.get(ref) != names.get(ref)} | {ref for ref in set(self.marks) | set(marks) if self.marks.get(ref) != marks.get(ref)}
        first = self.seen is None and not self.guessed            # (a page was answered "no pictures, on" before the first look: this look tells the sockets what is true)
        self.guessed = False
        for ref in [ref for ref in self.cache if ref not in names] + [ref for ref in set(self.marks) | set(marks) if self.marks.get(ref) != marks.get(ref)]:
            self.looked.pop(ref, None)                            # (a copy that went, or a switch or a hide that changed: the account is asked again at the next check)
            self.failed.pop(ref, None)
        self.seen, self.cache, self.marks, self.ends = number, names, marks, ends
        for ref in [ref for ref in self.codes if ref not in names]:
            del self.codes[ref]                                   # (no copy to tell a change of: Forget me, a log out, a switch, a block and the like leave no mark)
        if changed and not first and self.on_change is not None:
            self.on_change(changed)

    def first_look(self):
        """Before the hub's first look at the pictures a page that asks gets it made now; a database that refuses it leaves "no pictures, on" for this once (the clock's look tries again)."""
        if self.seen is None:
            try:
                self.sync()
            except StoreError:
                self.guessed = True

    def name_of(self, ref):
        """The public name of the account's picture (the account by its reference) when it may be shown now, else None: the list that sync() last read, no database read. Never a name for an account whose
        picture is off or blocked, whatever the two lists say."""
        self.first_look()
        return None if ref in self.marks else self.cache.get(ref)

    def state_of(self, ref):
        """`on`, `off` (the person switched it off) or `blocked` (the operator did) for the account, by the list that sync() last read."""
        self.first_look()
        return {SHOW_OFF: "off", SHOW_BLOCKED: "blocked"}.get(self.marks.get(ref), "on")

    # ---- the door ----------------------------------------------------------------------------------------------------------------------------

    async def serve(self, request, address):
        """GET /hub/pic/<name>.png: the picture kept under the name, or 404 (no name of this shape, no such name, a picture that is not shown or whose login has run out). Anyone who has the address may
        fetch it: the name cannot be guessed and is new at every fetch."""
        if request.method != "GET":
            return http.response(405, {"error": "method"}, {"Allow": "GET"})
        path = request.split()[0]
        if "?" in request.path:
            return http.response(404, {"error": "not_found"})
        if not self.rate.take(address, self.clock()):
            return http.response(429, {"error": "rate"}, {"Retry-After": "1"})
        found = PATH.match(path)
        try:
            png = self.store.picture(found.group(1)) if found else None
        except StoreError:
            return http.response(503, {"error": "busy"})
        if png is None:
            return http.response(404, {"error": "not_found"})
        return http.picture(png)
