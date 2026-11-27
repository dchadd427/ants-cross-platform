"""The counter's door (docs/ANALYTICS.md): the one address that the site's pages send their small events to.

  POST /hub/a    one JSON object of at most 1,024 bytes (hub/analytics_events.py says what it holds), sent by `navigator.sendBeacon` from a page of the site. The answer is 204 with no body for an event that was
                 counted AND for one that was dropped on purpose (a crawler, a visit that was counted a moment ago): the page cannot tell them apart and does not need to. Refusals: 404 (any other path, or a
                 query), 405 (not POST), 400 (no address, a body that is not one JSON object or not an event, a length that is missing or has no body), 403 (not the site's own page: the Origin and Sec-Fetch-Site),
                 413 (a body over the limit), 415 (not text/plain or application/json), 429 (over the address's allowance, or the whole hub's), 503 (the counter is off).

What the door reads of the request: the Origin, Sec-Fetch-Site, the type and the length of the body, the address that the site's nginx sets (X-Client-Address), the browser's User-Agent and the DNT and
Sec-GPC headers (a `1` in either makes the event the one minimal visit of a browser that asked not to be tracked). Nothing is written to the log: no address, no header, no text of the event.
"""
import asyncio
import json
import re

from . import http, limits as limitmod
from .analytics import Collector
from .analytics_events import MAX_BODY

PATH = "/hub/a"
ROUTE_PREFIX = "/hub/a"
DIGITS = re.compile(r"[0-9]{1,6}\Z")
TYPES = ("text/plain", "application/json")


def no_duplicates(pairs):
    keys = [k for k, _ in pairs]
    if len(set(keys)) != len(keys):
        raise ValueError("duplicate key")
    return dict(pairs)


def refuse_constant(_name):
    raise ValueError("not a JSON number")


class Door:
    def __init__(self, hub, collector):
        self.hub = hub
        self.collector = collector
        self.rate = limitmod.RateByAddress(hub.limits.analytics_burst, hub.limits.analytics_refill_seconds)
        self.next_address_log = float("-inf")

    def forget_idle(self, now):
        self.rate.forget_idle(now)

    @staticmethod
    def fail(status, code, headers=None):
        return http.response(status, {"error": code}, headers)

    def refused(self, status, code):
        self.collector.refuse()
        return self.fail(status, code)

    @staticmethod
    def untracked(headers):
        """Whether the browser asked not to be tracked: Do Not Track or Global Privacy Control, as the browser said it."""
        return headers.get("dnt", "").strip() == "1" or headers.get("sec-gpc", "").strip() == "1"

    async def serve(self, request, reader, address, ip):
        """The bytes of the whole answer to a request for /hub/a. `address` is the key of the allowance, `ip` the address as a text for the country (both are forgotten when this returns)."""
        try:
            if request.split()[0] != PATH or "?" in request.path:
                return self.fail(404, "not_found")
            if request.method != "POST":
                return self.fail(405, "method", {"Allow": "POST"})
            if limitmod.address_key(request.headers.get("x-client-address"), "") == "unknown":
                self.address_missing()
                return self.fail(400, "address")
            if not self.rate.take(address, self.hub.clock()) or not self.collector.admit():
                return self.fail(429, "rate", {"Retry-After": "5"})
            h = request.headers
            if h.get("origin") != self.hub.cfg.origin or not self.hub.cfg.origin or h.get("sec-fetch-site") != "same-origin":
                return self.refused(403, "origin")
            if h.get("content-type", "").split(";")[0].strip().lower() not in TYPES:
                return self.refused(415, "type")
            length = h.get("content-length", "")
            if "transfer-encoding" in h or not DIGITS.match(length) or int(length) < 1:
                return self.refused(400, "body")
            if int(length) > MAX_BODY:
                return self.refused(413, "body")
            body = await asyncio.wait_for(reader.readexactly(int(length)), self.hub.limits.handshake_seconds)
            try:
                data = json.loads(body.decode("utf-8"), parse_constant=refuse_constant, object_pairs_hook=no_duplicates)
            except (ValueError, UnicodeDecodeError, RecursionError):
                return self.refused(400, "body")
            answer = self.collector.accept(ip, h.get("user-agent", ""), self.untracked(h), data)
            if answer == "shape":
                return self.fail(400, "shape")
            if answer == "busy":
                return self.fail(503, "busy")
            return http.response(204)
        except (asyncio.IncompleteReadError, asyncio.TimeoutError):
            return self.refused(400, "body")
        except Exception as e:                                       # (a bug costs one request, never the hub; the class of the error is all that is logged)
            self.hub.log.event("error", kind=type(e).__name__, where="analytics")
            return self.fail(500, "error")

    def address_missing(self):
        now = self.hub.clock()
        if now >= self.next_address_log:
            self.next_address_log = now + 60.0
            self.hub.log.event("warning", what="analytics_asked_without_x_client_address")


class Off:
    """The door of a hub with the counter switched off (ANTS_HUB_ANALYTICS=off, or a file that cannot be opened): every call is a 503 `off`, and nothing is read."""

    collector = None

    def forget_idle(self, now):
        pass

    async def serve(self, request, reader, address, ip):
        return http.response(503, {"error": "off"})


def make(hub):
    """The door of a hub: the counter's own file next to the chat's (ANTS_HUB_ANALYTICS_DB), the country list (ANTS_HUB_GEO_FILE) and the Collector; Off when the counter is switched off or its file will not open."""
    cfg = hub.cfg
    if not cfg.analytics:
        return Off()
    store = None
    try:
        from . import analytics_store, geodata
        store = analytics_store.Store(cfg.analytics_db, hub.store.clock)
        collector = Collector(store, geodata.Geo.open(cfg.geo_file), cfg.origin, hub.store.clock, hub.log)
    except Exception as e:                                          # (a file that cannot be opened: the chat does not stop for the counter)
        if store is not None:
            store.close()
        hub.log.event("warning", what="analytics_off_its_file_cannot_be_opened", kind=type(e).__name__)
        return Off()
    return Door(hub, collector)
