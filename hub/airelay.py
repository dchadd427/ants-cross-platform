"""The relay between a person's AI and the person's open Map Maker tab (docs/MAP_AI.md, "The relay"): the tab opens a WebSocket of its own, `wss://<site>/hub/ai/ws`, when the person turns on "Let an AI work
in this tab", and everything the AI asks for goes through that socket to that tab and nowhere else. The hub keeps nothing of the map: a call and its answer are in memory for as long as they are in flight.

  - ARMED. A socket is an armed tab. It needs the person's login (the same cookie as the chat; none is a 401 before the upgrade), the site's own Origin and a banned account is let in no more than it was
    before. One tab is armed for an account: a second socket takes the first one's place (the first is told `replaced` and closed). Closing the socket (the Stop button, Turn off, closing or reloading the
    tab) disarms at once: the call in flight is answered `stopped` and later calls get `not_armed`. A tab that has had no call for an hour is disarmed, a login that ends takes its tab with it.
  - ONE CALL AT A TIME for an account, with a short queue behind it (`busy` past that): the call's own check of the key is made again when its turn comes. A call is answered by the tab within ten seconds,
    else `timeout`; a late answer of the tab is thrown away.
  - WHAT GOES TO THE TAB is `{"t":"call","id","tool","args","key","scope"}` (`key` is the key's own name, the person's text) and only for a tool of hub/aitools.py whose arguments fit its schema. What comes back is `{"t":"result","id","ok",...}`:
    checked again here (a result is an object, a picture is base64 and within its size, the code of an error is one the tab may use, a sentence is cut to one printable line), and what the AI is given is
    that and nothing else of the frame. The tab's `cells` of an edit are counted: 100,000 in all while the tab stays armed, then `budget`.
  - NO MAP IS KEPT, no text of the map is written to a log: a call is one line (`ai_call key=<id> ref=<8 hex> tool=<name> cells=<n> ok=<0|1> ms=<n>`), never an argument, a result, a name or a key.
"""
import asyncio
import json
import re

from . import aitools, http, limits as limitmod, ws
from .store import StoreError

PATH = "/hub/ai/ws"
BASE64 = re.compile(r"[A-Za-z0-9+/]+={0,2}\Z")
FRAME_KEYS = {"result": {"t", "id", "ok", "result", "cells", "error", "message"}}


class Refusal(Exception):
    """A call that is answered with a code of hub/aitools.py ERRORS (and, for the codes that are the tab's own, with the tab's sentence)."""

    def __init__(self, code, message="", tab=False):
        super().__init__(code)
        self.code = code
        self.message = message or aitools.ERRORS[code][1]
        self.tab = tab                              # the sentence is the page's own (text that a person could have changed), not the hub's


class Tab:
    """One armed tab: a WebSocket, the account it belongs to, and the one call that may be in flight."""

    def __init__(self, relay, writer, tag, identity, ref, address, now):
        self.relay = relay
        self.writer = writer
        self.tag = tag
        self.account = identity.account_id
        self.session = identity.session
        self.expires = identity.expires
        self.ref = ref
        self.address = address
        self.parser = ws.Parser(relay.hub.limits.ai_frame_max)
        self.frames = limitmod.TokenBucket(relay.hub.limits.ai_frames_burst, relay.hub.limits.ai_frames_refill_seconds, now)
        self.strikes = limitmod.Strikes(relay.hub.limits.violations, relay.hub.limits.violation_window)
        self.last_rx = self.last_ping = self.last_active = now
        self.closed = False
        self.lock = asyncio.Lock()
        self.waiting = 0
        self.pending = None                         # (id, future, key id) of the call in flight
        self.next_id = 1
        self.cells = 0                              # the cells that the AI changed while this tab stayed armed
        self.why = "stopped"                        # what a call in flight is answered with when the socket goes

    def send(self, value):
        self.write(ws.encode_frame(ws.TEXT, json.dumps(value, separators=(",", ":")).encode("utf-8")))

    def write(self, frame):
        if self.closed or self.writer.is_closing():
            return
        if self.writer.transport.get_write_buffer_size() > self.relay.hub.limits.write_buffer_max:
            self.finish()
            self.writer.transport.abort()
            return
        self.writer.write(frame)

    def strike(self):
        if self.strikes.add(self.relay.hub.clock()):
            self.close(1008, "too many mistakes")

    def finish(self):
        """The tab is armed no more: the call in flight is answered, the registry forgets it (once)."""
        if self.closed:
            return
        self.closed = True
        if self.pending is not None and not self.pending[1].done():
            self.pending[1].set_exception(Refusal("stopped"))
        self.relay.forget(self)

    def close(self, code, reason=""):
        """Tell the page why (`bye`), send a close frame and let the socket go."""
        if self.closed:
            return
        self.send({"t": "bye", "why": reason})
        self.finish()
        self.relay.hub.log.event("ai_close", code=code, tag=self.tag)
        self.writer.write(ws.encode_close(code, reason))
        self.writer.close()
        try:
            asyncio.get_running_loop().call_later(self.relay.hub.limits.close_grace_seconds, self.writer.transport.abort)         # (a socket whose reader never takes the rest of the output would stay for ever)
        except RuntimeError:
            pass

    async def run(self, reader):
        try:
            while not self.closed:
                data = await reader.read(4096)
                if not data:
                    return
                now = self.relay.hub.clock()
                self.last_rx = now
                for opcode, payload in self.parser.feed(data):
                    if self.closed:
                        break
                    if not self.frames.take(now):
                        self.strike()
                    elif opcode == ws.TEXT:
                        self.on_text(payload)
                    elif opcode == ws.PING:
                        self.write(ws.encode_frame(ws.PONG, payload))
                    elif opcode == ws.CLOSE:
                        self.close(payload[0])
                if self.parser.error and not self.closed:
                    self.relay.hub.log.event("protocol", code=self.parser.error.code, tag=self.tag)
                    self.close(self.parser.error.code, self.parser.error.reason)
        finally:
            self.finish()

    def on_text(self, text):
        frame = aitools.parse(text)
        if not isinstance(frame, dict) or frame.get("t") != "result" or not set(frame) <= FRAME_KEYS["result"]:
            return self.strike()
        pending = self.pending
        if pending is None or frame.get("id") != pending[0] or type(frame.get("id")) is not int:
            return                                                  # (a late answer, or one for a call that was never made: thrown away, and it costs nothing)
        if not pending[1].done():
            pending[1].set_result(frame)


class Relay:
    def __init__(self, hub):
        self.hub = hub
        self.tabs = {}                              # the account's reference (hub/pseudo.py) -> its armed Tab
        self.per_address = limitmod.PerAddress(hub.limits.ai_sockets_address)

    # ---- the socket ---------------------------------------------------------------------------------------------------------------------------

    async def upgrade(self, request, writer, address):
        """Check the handshake and take the socket in (an HttpError says no): the 101 and the `ready` frame are written and the Tab is returned for its caller to run."""
        hub, h = self.hub, request.headers
        if not hub.cfg.ai_keys:
            raise http.HttpError(503, "ai_off")
        if request.method != "GET":
            raise http.HttpError(405, "method", {"Allow": "GET"})
        if "?" in request.path:
            raise http.HttpError(404, "not_found")
        if not hub.cfg.origin or h.get("origin") != hub.cfg.origin:
            raise http.HttpError(403, "origin")
        if h.get("sec-fetch-site", "same-origin") != "same-origin":
            raise http.HttpError(403, "fetch_site")
        if h.get("content-length", "0") != "0" or "transfer-encoding" in h:
            raise http.HttpError(400, "body")
        if h.get("upgrade", "").lower() != "websocket" or not request.has_token("connection", "upgrade"):
            raise http.HttpError(426, "upgrade", {"Upgrade": "websocket"})
        if h.get("sec-websocket-version") != "13":
            raise http.HttpError(426, "version", {"Sec-WebSocket-Version": "13"})
        key = h.get("sec-websocket-key", "")
        if not ws.valid_key(key):
            raise http.HttpError(400, "key")
        if len(self.tabs) >= hub.limits.ai_sockets_total:
            raise http.HttpError(503, "full", {"Retry-After": "30"})
        if not self.per_address.try_add(address):
            raise http.HttpError(429, "address", {"Retry-After": "10"})
        tab = None
        try:                                                        # (the place of the address is given back whatever goes wrong from here)
            who = await hub.identify(h)
            if who is None:
                raise http.HttpError(401, "login")
            try:
                banned = hub.store.is_banned(who.account_id)
            except StoreError as e:
                hub.store_trouble(e)
                raise http.HttpError(503, "busy")
            if banned:
                raise http.HttpError(403, "banned")
            if len(self.tabs) >= hub.limits.ai_sockets_total:
                raise http.HttpError(503, "full", {"Retry-After": "30"})
            tab = Tab(self, writer, hub.log.tag(address), who, hub.chat.ref(who.account_id), address, hub.clock())
            old = self.tabs.get(tab.ref)
            if old is not None:
                old.close(1000, "replaced")                         # (one armed tab for an account: the new one takes its place)
            self.tabs[tab.ref] = tab
            writer.write(http.response(101, headers={"Upgrade": "websocket", "Connection": "Upgrade", "Sec-WebSocket-Accept": ws.accept_key(key)}))
            tab.send({"t": "ready", "v": 1, "tools": sorted(aitools.TOOLS), "idle": int(hub.limits.ai_idle_seconds), "budget": hub.limits.ai_cells_armed})
        except BaseException:
            if tab is not None and self.tabs.get(tab.ref) is tab:
                tab.closed = True
                self.forget(tab)                                    # (which gives the address its place back)
            else:
                self.per_address.remove(address)
            raise
        hub.log.event("ai_armed", tag=tab.tag, ref=tab.ref[:8], tabs=len(self.tabs))
        return tab

    def forget(self, tab):
        if self.tabs.get(tab.ref) is tab:
            del self.tabs[tab.ref]
        self.per_address.remove(tab.address)

    def end(self, session=None, account=None):
        """The tabs of a login that was ended (its session key), or of every login of an account, are disarmed: a socket never outlives the login that it was let in with."""
        for tab in list(self.tabs.values()):
            if (session is not None and tab.session == session) or (account is not None and tab.account == account):
                tab.close(1000, "logged out")

    def stop(self):
        for tab in list(self.tabs.values()):
            tab.close(1001, "the hub restarts")

    def on_tick(self, now):
        limits, wall = self.hub.limits, self.hub.store.clock()
        for tab in list(self.tabs.values()):
            if tab.expires and wall >= tab.expires:
                tab.close(1000, "the login ran out")
            elif now - tab.last_active >= limits.ai_idle_seconds:
                tab.close(1000, "idle")
            elif now - tab.last_rx >= limits.silence_seconds:
                tab.close(1001, "silent")
            elif now - tab.last_ping >= limits.ping_seconds:
                tab.last_ping = now
                tab.write(ws.encode_frame(ws.PING))

    # ---- a call -------------------------------------------------------------------------------------------------------------------------------

    def armed(self, ref):
        tab = self.tabs.get(ref)
        return tab is not None and not tab.closed

    async def call(self, key, tool, args, valid):
        """(result, cells) of `tool` on the armed tab of the key's account, or a Refusal. `valid()` is asked again when the call's turn comes: whether the key still works."""
        hub, limits = self.hub, self.hub.limits
        tab = self.tabs.get(key.ref)
        if tab is None or tab.closed:
            raise Refusal("not_armed")
        edit = aitools.TOOLS[tool].scope == aitools.EDIT
        if tab.waiting >= limits.ai_queue_max:
            raise Refusal("busy")
        tab.waiting += 1
        try:
            await asyncio.wait_for(tab.lock.acquire(), limits.ai_queue_seconds)
        except asyncio.TimeoutError:
            raise Refusal("busy")
        finally:
            tab.waiting -= 1
        try:
            if tab.closed:
                raise Refusal("not_armed")
            if not valid():
                raise Refusal("bad_key")
            if edit and tab.cells >= limits.ai_cells_armed:
                raise Refusal("budget")
            now = hub.clock()
            tab.last_active = now
            call_id, tab.next_id = tab.next_id, tab.next_id + 1
            future = asyncio.get_running_loop().create_future()
            tab.pending = (call_id, future, key.id)
            tab.send({"t": "call", "id": call_id, "tool": tool, "args": args, "key": key.name, "scope": key.scope})
            try:
                frame = await asyncio.wait_for(future, limits.ai_call_seconds)
            except asyncio.TimeoutError:
                tab.pending = None
                tab.send({"t": "cancel", "id": call_id})            # (the tab drops the work that has not started: a late answer is thrown away, and a retry must not apply the change twice)
                raise Refusal("timeout")
            finally:
                tab.pending = None
            tab.last_active = hub.clock()
            return self.accept(tab, tool, frame, edit)
        finally:
            tab.lock.release()

    def cut(self, ref, key_id):
        """A key was revoked: the call of that key that is in flight on the account's tab is answered `bad_key` at once and the tab is told to drop it (the calls that wait are asked again at their turn)."""
        tab = self.tabs.get(ref)
        pending = tab.pending if tab is not None else None
        if pending is not None and pending[2] == key_id and not pending[1].done():
            pending[1].set_exception(Refusal("bad_key"))
            tab.send({"t": "cancel", "id": pending[0]})

    def accept(self, tab, tool, frame, edit):
        """What the tab answered, checked: (result, cells) or a Refusal."""
        limits = self.hub.limits
        if frame.get("ok") is not True:
            code = frame.get("error")
            code = code if code in aitools.TAB_CODES else "tab_error"
            raise Refusal(code, aitools.sentence(frame.get("message")) if code in aitools.TAB_SENTENCE else "", tab=code in aitools.TAB_SENTENCE)
        result = frame.get("result")
        if not isinstance(result, dict):
            raise Refusal("tab_error")
        png = result.get("png")
        if png is not None:
            if not (isinstance(png, str) and BASE64.match(png)):
                raise Refusal("tab_error")
            if len(png) > limits.ai_picture_max:
                raise Refusal("too_big", "The picture is too big: ask for a smaller window or a smaller max_px.")
        cells = frame.get("cells", 0)
        cells = min(max(cells, 0), aitools.MAX_CELLS) if type(cells) is int and edit else 0
        tab.cells += cells
        return result, cells
