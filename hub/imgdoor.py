"""The images' two doors (docs/HUB_IMAGES.md): the upload that the page makes, and the call to the converter that does the decoding.

UPLOAD. `POST /hub/img/up` (the site's nginx passes it, with only the login's cookie, Origin, Sec-Fetch-Site, the two headers below, the lengths and the client's address): the body is the bytes of an image, at most
4 MiB. The page's own call, and nobody else's: Origin must be the site's, `Sec-Fetch-Site` must be `same-origin`, and the request must carry `X-Ants-Image: 1` and `Content-Type: application/octet-stream`
(a page of another site cannot send that header without a preflight, which this door never answers). Everything that can be decided is decided BEFORE a body byte is read, in this order: the method, the
site, the type and the header, the address's limit, a login (401), whether the account may post at all (403 `images`: not banned, muted or too young, and hub/images.py says `on`), the length (411, 413), the
account's budget (429 `rate`), a place for the body (429 `busy`: at most `image_bodies_max` bodies at one time). Then the body is read within `image_body_seconds`, handed to the converter (one at a time, a short
queue, then `busy`), and the answer of the converter is CHECKED again here (its signatures, its lengths, its sizes) before a byte is kept. The files are written, the row follows, and the answer is
`200 {"id", "w", "h", "x", "anim", "still"}`: `id` is the 128-bit name of the image, which the page puts in its next `say` frame (hub/chat.py `on_say`) so that the account's line carries it. The id
is the account's own, once, for `image_pending_seconds`. An error is `{"error": code}` and nothing else (never a text of the file, the account or the converter's cause):
    401 login   403 origin / images   400 body   411 length   413 big   415 type   422 pixels / bad / frames   429 rate / busy   503 off / full / down
A token that is spent for an upload that failed for a reason that is not the account's (the converter busy or down, no room) is given back.

CONVERTER. `POST <ANTS_HUB_IMAGE_CONVERTER_URL>/convert` with the bytes and nothing else (no account, no name, no header of the visitor): 2 seconds to connect and 12 in all, the answer read up to 1.7 MiB.
A `200` is a line of JSON and then the image and its thumbnail; anything that is not exactly that is `down`. The converter is a program of ours in a container of its own that has no secret and no way out
(docs/HUB_IMAGES.md): this file does not trust it more than it has to.
"""
import asyncio
import json

from . import http, limits as limitmod
from .store import StoreError
from .store.base import Image

UP_PATH = "/hub/img/up"
HEADER = "x-ants-image"
CODES = ("type", "pixels", "bad", "frames", "big")        # what the converter may refuse an image with
STATUS = {"type": 415, "pixels": 422, "bad": 422, "frames": 422, "big": 413}
SIGNATURES = {"jpg": (b"\xff\xd8\xff", b"\xff\xd9"), "png": (b"\x89PNG\r\n\x1a\n", b"IEND\xaeB`\x82"), "gif": (b"GIF89a", b";")}
KEYS = {"x", "w", "h", "tw", "th", "anim", "still", "full", "thumb"}
SIDE_MAX = 2048                                              # no image that is kept is larger than this on either side
THUMB_SIDE_MAX = 400


class ConverterError(Exception):
    """The converter could not be used: `code` is one of CODES (it refused the image), or `down` (no connection, no answer in time, an answer that is not exactly its own). Nothing else is in it."""

    def __init__(self, code):
        super().__init__(code)
        self.code = code


class Converted:
    __slots__ = ("x", "full", "thumb", "w", "h", "tw", "th", "anim", "still")

    def __init__(self, x, full, thumb, w, h, tw, th, anim, still):
        self.x, self.full, self.thumb, self.w, self.h, self.tw, self.th, self.anim, self.still = x, full, thumb, w, h, tw, th, anim, still


def refuse_constant(name):
    raise ValueError(name)


def whole(value, high):
    """`value` when it is a whole number from 1 to `high` (a bool is no number), else None."""
    return value if type(value) is int and 1 <= value <= high else None


class Converter:
    def __init__(self, url, limits):
        self.limits = limits
        self.host = self.port = None
        if url:
            host, _, port = url[len("http://"):].rpartition(":")
            self.host, self.port = host, int(port)

    @property
    def on(self):
        return self.host is not None

    async def convert(self, body):
        """The Converted of an uploaded image, or a ConverterError (one of CODES, or `down`)."""
        if not self.on:
            raise ConverterError("down")
        try:
            return await asyncio.wait_for(self.exchange(body), self.limits.image_convert_seconds)
        except ConverterError:
            raise
        except (asyncio.TimeoutError, OSError, ValueError, EOFError, UnicodeError, RecursionError, asyncio.LimitOverrunError, asyncio.IncompleteReadError):
            pass                                          # (raised below, outside the handler: what the cause said can hold an address)
        raise ConverterError("down")

    async def exchange(self, body):
        head = "POST /convert HTTP/1.1\r\nHost: %s:%d\r\nUser-Agent: AntsHub/1\r\nContent-Type: application/octet-stream\r\nContent-Length: %d\r\nConnection: close\r\n\r\n" % (self.host, self.port, len(body))
        reader, writer = await asyncio.wait_for(asyncio.open_connection(self.host, self.port, limit=65536), self.limits.image_connect_seconds)
        try:
            writer.write(head.encode("ascii") + body)
            await writer.drain()
            raw = await reader.readuntil(b"\r\n\r\n")
            status, length = self.read_head(raw)
            if length > self.limits.image_reply_max:
                raise ConverterError("down")
            data = await reader.readexactly(length)
        finally:
            writer.transport.abort()                      # (at once: the call is over, and nothing waits for a polite close)
        if status == 200:
            return self.read_reply(data)
        code = self.read_error(data)
        raise ConverterError(code if status in (400, 413, 422) and code in CODES else "down")

    @staticmethod
    def read_head(raw):
        """(status, length of the body) of the head of an answer, read strictly: HTTP/1.x, a status, `Content-Length` once and no `Transfer-Encoding`, `Content-Encoding` or redirect."""
        lines = raw.decode("latin-1").split("\r\n")[:-2]
        parts = lines[0].split(" ", 2)
        if len(parts) < 2 or parts[0] not in ("HTTP/1.0", "HTTP/1.1") or not (parts[1].isdigit() and len(parts[1]) == 3):
            raise ConverterError("down")
        status, length, seen = int(parts[1]), None, set()
        for line in lines[1:]:
            name, colon, value = line.partition(":")
            name = name.strip().lower()
            if not colon or name in seen and name in ("content-length", "transfer-encoding"):
                raise ConverterError("down")
            seen.add(name)
            if name == "content-length":
                length = value.strip()
            elif name in ("transfer-encoding", "content-encoding"):
                raise ConverterError("down")
        if not (length and length.isdigit() and len(length) <= 8) or 300 <= status < 400:
            raise ConverterError("down")
        return status, int(length)

    @staticmethod
    def read_error(data):
        try:
            found = json.loads(data.decode("utf-8"), parse_constant=refuse_constant)
        except (ValueError, UnicodeError, RecursionError):
            return "down"
        code = found.get("error") if isinstance(found, dict) else None
        return code if isinstance(code, str) else "down"

    def read_reply(self, data):
        """The Converted of a 200: a line of JSON and then the image and its thumbnail, checked: the keys, the types, the sizes, the lengths that add up, the signatures and ends of the two files."""
        line, newline, rest = data.partition(b"\n")
        if not newline or len(line) > 512:
            raise ConverterError("down")
        head = json.loads(line.decode("ascii"), parse_constant=refuse_constant)
        if not isinstance(head, dict) or set(head) != KEYS:
            raise ConverterError("down")
        x, full, thumb = head["x"], whole(head["full"], self.limits.image_reply_max), whole(head["thumb"], self.limits.image_reply_max)
        sizes = [whole(head[k], SIDE_MAX if k in ("w", "h") else THUMB_SIDE_MAX) for k in ("w", "h", "tw", "th")]
        if type(x) is not str or x not in SIGNATURES or None in sizes or full is None or thumb is None or type(head["anim"]) is not bool or type(head["still"]) is not bool or full + thumb != len(rest):
            raise ConverterError("down")
        if head["anim"] != (x == "gif") or (head["anim"] and head["still"]):
            raise ConverterError("down")
        image, little = rest[:full], rest[full:]
        start, end = SIGNATURES[x]
        if not (image.startswith(start) and image.endswith(end) and little.startswith(SIGNATURES["jpg"][0]) and little.endswith(SIGNATURES["jpg"][1])):
            raise ConverterError("down")
        return Converted(x, image, little, sizes[0], sizes[1], sizes[2], sizes[3], head["anim"], head["still"])


class Door:
    """POST /hub/img/up: the page's upload (see the top of this file)."""

    def __init__(self, hub):
        self.hub = hub
        self.rate = limitmod.RateByAddress(hub.limits.image_address_burst, hub.limits.image_address_refill_seconds)
        self.bodies = 0                                       # uploads whose body is being read
        self.waiting = 0                                      # uploads that wait for the converter, and the one that is in it
        self.lock = asyncio.Lock()                            # the converter does one at a time
        self.last = None                                      # what the last call of the converter said: `ok` or `down` (the Status page); None before the first

    def forget_idle(self, now):
        self.rate.forget_idle(now)

    def converter_state(self):
        """`off` (no converter address or no folder), else `ok` or `down` as the last call said, `ok` before there was one."""
        return "off" if not self.hub.images.on else (self.last or "ok")

    @staticmethod
    def fail(status, code, headers=None):
        return http.response(status, {"error": code}, headers)

    def same_origin(self, h):
        origin = h.get("origin")
        return bool(origin) and origin == self.hub.cfg.origin and h.get("sec-fetch-site") == "same-origin"

    async def serve(self, request, reader, address):
        """The bytes of the whole answer to a request for UP_PATH."""
        try:
            return await self.upload(request, reader, address)
        except (asyncio.IncompleteReadError, asyncio.TimeoutError):
            return self.fail(400, "body")
        except StoreError as e:
            self.hub.store_trouble(e)
            return self.fail(503, "down")
        except Exception as e:                                # (a bug costs one request, never the hub; the class of the error is all that is logged)
            self.hub.log.event("error", kind=type(e).__name__, where="images")
            return self.fail(500, "error")

    async def upload(self, request, reader, address):
        hub, images = self.hub, self.hub.images
        h = request.headers
        if "?" in request.path:
            return self.fail(404, "not_found")
        if request.method != "POST":
            return self.fail(405, "method", {"Allow": "POST"})
        if not self.same_origin(h):
            return self.fail(403, "origin")
        if h.get("content-type", "").strip().lower() != "application/octet-stream" or h.get(HEADER) != "1":
            return self.fail(415, "type")
        if not images.on:
            return self.fail(503, "off")
        if not self.rate.take(address, hub.clock()):
            return self.fail(429, "rate", {"Retry-After": "5"})
        who = await hub.identify(h)
        if who is None:
            return self.fail(401, "login")
        account = who.account_id
        if who.young or hub.store.is_banned(account) or hub.store.muted_until(account) or not images.may_post(account):
            return self.fail(403, "images")
        if "transfer-encoding" in h or not h.get("content-length", "").isdigit() or len(h["content-length"]) > 15:
            return self.fail(411, "length")
        length = int(h["content-length"])
        if length < 1:
            return self.fail(411, "length")
        if length > hub.limits.image_upload_bytes:
            return self.fail(413, "big")
        key = hub.chat.key_of(account)
        if images.uploads.check(key, hub.clock()):
            return self.fail(429, "rate", {"Retry-After": "20"})

        def refused(status, code, retry=None):
            """A refusal that is not the account's doing (the hub or the converter is busy, down or full): its token is given back."""
            images.uploads.refund(key)
            return self.fail(status, code, {"Retry-After": retry} if retry else None)

        if hub.store.pending_images(account) >= hub.limits.image_pending_max:
            images.uploads.refund(key)
            return self.fail(429, "rate", {"Retry-After": "20"})
        if self.bodies >= hub.limits.image_bodies_max or self.waiting >= hub.limits.image_queue_max + 1:
            return refused(429, "busy", "5")
        self.bodies += 1
        try:
            body = await asyncio.wait_for(reader.readexactly(length), hub.limits.image_body_seconds)
        finally:
            self.bodies -= 1
        if not body.startswith((b"\x89PNG", b"\xff\xd8\xff", b"GIF8", b"RIFF")):
            return refused(415, "type")                        # (the first bytes only: the converter looks at the rest; nothing was converted, so the token is back)
        self.waiting += 1
        try:
            try:
                await asyncio.wait_for(self.lock.acquire(), hub.limits.image_queue_seconds)
            except asyncio.TimeoutError:
                return refused(429, "busy", "5")
            try:
                converted = await hub.converter.convert(body)
            except ConverterError as e:
                hub.log.event("image", kind=e.code)
                self.last = "down" if e.code == "down" else "ok"      # (a file that is refused is a converter that answered)
                if e.code == "down":
                    return refused(503, "down")
                return self.fail(STATUS[e.code], e.code)
            finally:
                self.lock.release()
        finally:
            self.waiting -= 1
        self.last = "ok"
        if not images.may_post(account):
            return self.fail(403, "images")                    # (the role went, or the flag came, while the converter worked: nothing is kept)
        if not images.room(len(converted.full) + len(converted.thumb)):
            hub.log.event("image", kind="full")
            return refused(503, "full")
        name = images.new_name()
        try:
            await asyncio.get_running_loop().run_in_executor(None, images.write, name, converted.x, converted.full, converted.thumb)
        except OSError:
            hub.log.event("image", kind="down")
            return refused(503, "down")
        image = Image(name, converted.x, converted.w, converted.h, converted.tw, converted.th, converted.anim)
        try:
            hub.store.add_image(account, name, image, len(converted.full) + len(converted.thumb))
        except BaseException:
            for path in (images.path(name, converted.x), images.path(name, converted.x, thumb=True)):
                images.unlink(path)
            raise
        hub.log.event("image", kind="ok")
        return http.response(200, {"id": name, "w": image.w, "h": image.h, "x": image.x, "anim": image.anim, "still": converted.still})
