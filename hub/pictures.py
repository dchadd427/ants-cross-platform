"""The small copy of an account's Discord profile picture (docs/HUB.md, "Pictures"): fetched once after a login, checked hard, rewritten to a bare PNG, kept in hub.db and served at a name of its own.

After a login is answered, in the background, the hub asks Discord's image host for the one 64 pixel PNG of the picture whose short code the login saw (hub/discord.py `picture()`: the address
is made there from constants and the two checked parts, no redirect is followed, nothing is read past a cap). The login never waits for it and never fails because of it: every failure is a
letter in the page's circle (no picture kept), and one line in the log that says which kind (`picture kind=ok|none|down|type|big|shape|busy|bug`, with the count of dropped fetches or the class of an error, nothing else: no id, no code, no name, no reference).

What Discord sends is data, not a thing to trust. It is kept only when it is a PNG whose every chunk is whole (a length that fits, a CRC that matches), whose header is first and says 8 to 128 pixels
and a legal depth and color, that ends with IEND and has nothing after it, and whose data is a whole zlib stream that inflates to exactly the rows that the header says (never inflated past that). The hub then writes its own copy, which holds only the header, the palette, the transparency, the picture data and
the end: every other chunk (text, EXIF, color profiles, the animation chunks of an APNG) is dropped. The copy is what is kept and served; its pixels are not decoded here.

The copy is kept under a random 128-bit name that is new at every login (`secrets.token_hex(16)`; nothing in it comes from the id, the reference, the code or the bytes), by the account's keyed
reference (hub/pseudo.py) and never by its id. It is served at /hub/pic/<name>.png with fixed headers (hub/http.py `picture()`). It is deleted with the login's end, a kick, Forget me, the person's
switch, a ban, a block, or when the login runs out (the store's rule: no login, no picture); a fetch that ends after any of these keeps nothing; a name that is not known, is not shown or is past its time answers 404.

A person's id is used in one place here: the path of the one fetch. It is not kept, not logged, not in an error.
"""
import asyncio
import collections
import re
import secrets
import zlib

from . import discord as discordmod, http, limits as limitmod, pseudo
from .store import StoreError
from .store.base import SHOW_ON

PATH = re.compile(r"/hub/pic/([0-9a-f]{32})\.png\Z")
SIGNATURE = b"\x89PNG\r\n\x1a\n"
DEPTHS = {0: (1, 2, 4, 8, 16), 2: (8, 16), 3: (1, 2, 4, 8), 4: (8, 16), 6: (8, 16)}       # color type -> the bit depths that the format allows with it
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

    def __init__(self, store, discord, limits, log, clock):
        self.store, self.discord, self.limits, self.log, self.clock = store, discord, limits, log, clock
        self.waiting = collections.deque()                   # (account id, picture code) of the fetches that wait for their turn: in memory only, at most `picture_waiting`
        self.inflight = 0
        self.tasks = set()
        self.queued = set()                                  # the references of the accounts whose fetch waits or runs: one each, so a burst of logins of one account is one fetch
        self.dropped = 0                                     # fetches dropped since the last line of the log
        self.dropped_at = float("-inf")
        self.bucket = limitmod.TokenBucket(limits.picture_burst, limits.picture_refill_seconds, clock())
        self.rate = limitmod.RateByAddress(limits.picture_serve_burst, limits.picture_serve_refill_seconds)
        self.seen = None                                     # (the number of the last change, {reference: name}) for names()

    # ---- the fetch ---------------------------------------------------------------------------------------------------------------------------

    def offer(self, account, avatar):
        """What a finished login hands over: the account and the short code of its picture (None for none). It decides at once, never waits and never raises: a switched off or blocked account, a banned one, one
        whose fetch is waiting or running already, a full queue: nothing is fetched (and the drop is counted and told). A new login always gets its fetch: the next login is the retry. No picture at
        Discord deletes the older copy of ours."""
        try:
            if self.store.picture_state(account, touch=True) != SHOW_ON:
                return
            if self.store.is_banned(account):
                self.store.clear_picture(account)
                return
            if avatar is None:
                self.store.clear_picture(account)
                self.log.event("picture", kind="none")
                return
            key, now = pseudo.reference(self.store.secret(), account), self.clock()
            if key in self.queued or len(self.waiting) >= self.limits.picture_waiting:
                return self.drop(now)
            self.queued.add(key)
            self.waiting.append((account, avatar, key))
            self.pump()
        except Exception as e:                                # (a bug or a refusing database costs one picture, never the login)
            self.log.event("picture", kind="bug", error=type(e).__name__)

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
            await self.pace()
            await self.fetch(account, avatar)
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

    async def fetch(self, account, avatar):
        """One fetch, check and keeping, ending in one line of the log. Nothing changes when it fails, but for a 404 (the picture went away at Discord), which deletes the older copy."""
        try:
            status, content_type, body = await self.discord.picture(account, avatar)
        except discordmod.DiscordError as e:
            return self.log.event("picture", kind="big" if e.kind == "big" else "down")
        if status == 404:
            self.store.clear_picture(account)
            return self.log.event("picture", kind="none")
        if status != 200:
            return self.log.event("picture", kind="down")
        if content_type != "image/png":
            return self.log.event("picture", kind="type")
        if len(body) > self.limits.picture_bytes_max:
            return self.log.event("picture", kind="big")
        try:
            png = rewrite(body)
        except ShapeError:
            return self.log.event("picture", kind="shape")
        kept = self.store.set_picture(account, png, secrets.token_hex(16), self.store.clock() + self.limits.session_seconds, self.limits.pictures_rows_max, self.limits.pictures_bytes_max)
        self.log.event("picture", kind="ok" if kept else "none")       # (not kept: the person switched it off, the login ended or the account was banned while it was on its way)

    def forget_idle(self, now):
        self.rate.forget_idle(now)
        self.tell_drops(now)

    def stop(self):
        for task in list(self.tasks):
            task.cancel()

    # ---- the names ---------------------------------------------------------------------------------------------------------------------------

    def names(self):
        """{reference: name} of the pictures that may be shown: read again when the store's counter says that something changed (here or in another process, the command line)."""
        number = self.store.last_picture_change()
        if self.seen is None or self.seen[0] != number:
            self.seen = (number, self.store.picture_names())
        return self.seen[1]

    # ---- the door ----------------------------------------------------------------------------------------------------------------------------

    async def serve(self, request, address):
        """GET /hub/pic/<name>.png: the picture kept under the name, or 404 (no name of this shape, no such name, a picture that is not shown or whose login has run out). Anyone who has the address may
        fetch it: the name cannot be guessed and is new at every login."""
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
