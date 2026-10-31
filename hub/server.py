"""The hub process: HTTP head, WebSocket upgrade, one task per socket, and a clock that pings, drops the silent, counts who is online and keeps the database tidy.

The door of the chat is wss://<site>/hub/ws (docker/nginx.conf passes it here, with the visitor's address in X-Client-Address, and nothing else of the request but the headers named in
docs/HUB.md). Every refusal is an HTTP answer before the upgrade: 403 for an Origin that is not the site, 429 for a ninth socket of one address, 503 for the 501st socket. The pages of the
Discord login (hub/login.py) are the other five paths that nginx passes: each is one request and one answer, and a hub without the login has none of them.
"""
import asyncio
import os
import signal
import sys
import time

from . import chat as chatmod, config as configmod, http, limits as limitmod, session as sessionmod, store as storemod, text as textrules, ws
from .log import Log

WS_PATH = "/hub/ws"
HEALTH_PATH = "/hub/healthz"


class Peer:
    """One open WebSocket (the object that hub.chat talks to)."""

    def __init__(self, hub, writer, key, tag, identity, now):
        self.hub = hub
        self.writer = writer
        self.key = key
        self.tag = tag
        self.identity = identity
        self.parser = ws.Parser(hub.limits.frame_max)
        self.frames = limitmod.TokenBucket(hub.limits.frame_burst, hub.limits.frame_refill_seconds, now)
        self.strikes = limitmod.Strikes(hub.limits.violations, hub.limits.violation_window)
        self.last_rx = now
        self.last_ping = now
        self.closed = False
        self.closed_at = None                       # when it was closed (the hub cuts a closed socket whose output is still unsent after a while)

    def send_text(self, data):
        self.write(ws.encode_frame(ws.TEXT, data))

    def strike(self):
        if self.strikes.add(self.hub.clock()):
            self.close(1008, "too many mistakes")

    def charge(self):
        """A refusal that is no mistake of the client's (the hub is busy, the database is full or locked) costs one frame of its budget more, so a client that keeps asking is no cheaper than one
        that makes mistakes: when the budget is gone, it is a mistake."""
        if not self.frames.take(self.hub.clock()):
            self.strike()

    def write(self, frame):
        if self.closed or self.writer.is_closing():
            return
        if self.writer.transport.get_write_buffer_size() > self.hub.limits.write_buffer_max:
            self.hub.log.event("drop", why="slow", tag=self.tag)
            self.closed = True
            self.writer.transport.abort()                # (cut at once: there is no grace to wait for)
            return
        self.writer.write(frame)

    def close(self, code, reason=""):
        """Send a close frame and let the socket go (what is already written is still sent)."""
        if self.closed:
            return
        self.closed = True
        self.closed_at = self.hub.clock()
        self.hub.log.event("close", code=code, tag=self.tag)
        self.writer.write(ws.encode_close(code, reason))
        self.writer.close()

    async def run(self, reader):
        while not self.closed:
            data = await reader.read(4096)
            if not data:
                return
            now = self.hub.clock()
            self.last_rx = now
            for opcode, payload in self.parser.feed(data):
                if self.closed:
                    break
                if not self.frames.take(now):
                    self.strike()
                elif opcode == ws.TEXT:
                    self.hub.chat.on_text(self, payload)
                elif opcode == ws.PING:
                    self.write(ws.encode_frame(ws.PONG, payload))
                elif opcode == ws.CLOSE:
                    self.close(payload[0])
            if self.parser.error and not self.closed:
                self.hub.log.event("protocol", code=self.parser.error.code, tag=self.tag)
                self.close(self.parser.error.code, self.parser.error.reason)


class Hub:
    def __init__(self, cfg, store, sessions, log, limits=None, clock=time.monotonic):
        self.cfg = cfg
        self.store = store
        self.sessions = sessions
        self.log = log
        self.limits = limits or limitmod.Limits()
        self.clock = clock
        self.chat = chatmod.Chat(store, cfg.mods, self.limits, clock, sessions.login)
        self.chat.on_store_error = self.store_trouble
        self.next_trouble_log = float("-inf")
        self.per_address = limitmod.PerAddress(self.limits.sockets_per_address)
        self.pending = 0
        self.server = None
        self.ticker = None
        self.port = None
        self.online_shown = 0
        self.online_at = float("-inf")
        self.next_deletions = self.next_forget = self.next_purge = self.next_checkpoint = self.next_crowded = float("-inf")
        self.routes = getattr(sessions, "routes", None) or {}
        if hasattr(sessions, "attach"):
            sessions.attach(self)

    async def start(self, host, port):
        self.server = await asyncio.start_server(self.handle, host, port, limit=8192, backlog=128)
        self.port = self.server.sockets[0].getsockname()[1]
        self.ticker = asyncio.ensure_future(self.tick())
        return self.port

    async def stop(self):
        if self.ticker:
            self.ticker.cancel()
        if self.server:
            self.server.close()
        for peer in list(self.chat.peers):
            peer.close(1001, "the hub restarts")
        await asyncio.sleep(0.05)

    # ---- one connection -------------------------------------------------------------------------------------------------------------------

    async def handle(self, reader, writer):
        if self.pending >= self.limits.pending_total:
            return writer.close()
        self.pending += 1
        pending = True
        peer = None
        try:
            try:
                head = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), self.limits.handshake_seconds)
            except asyncio.TimeoutError:
                raise http.HttpError(408, "timeout")
            except asyncio.LimitOverrunError:
                raise http.HttpError(431, "head")
            request = http.parse_head(head)
            if request.path == HEALTH_PATH and request.method == "GET":
                return writer.write(http.response(200, {"ok": True}))
            route = self.routes.get(request.split()[0])
            if route is not None:
                self.pending -= 1                                     # (the place of a connection that has not finished its handshake: this one has, and may wait for Discord)
                pending = False
                return await self.page(route, request, writer)
            if request.path != WS_PATH:
                raise http.HttpError(404, "not_found")
            peer = await self.upgrade(request, reader, writer)
            self.pending -= 1
            pending = False
            await peer.run(reader)
        except http.HttpError as e:
            self.log.event("refuse", status=e.status, why=e.code)
            writer.write(http.response(e.status, {"error": e.code}, e.headers))
        except (asyncio.IncompleteReadError, ConnectionError, asyncio.CancelledError):
            pass
        except Exception as e:                                       # (a bug costs one socket and never the process; the class of the error is all that is logged)
            self.log.event("error", kind=type(e).__name__)
            if peer is not None:
                peer.close(1011, "internal error")
        finally:
            if pending:
                self.pending -= 1
            if peer is not None:
                peer.closed = True
                self.chat.peers.discard(peer)
                self.per_address.remove(peer.key)
                self.log.event("gone", tag=peer.tag, online=len(self.chat.peers))
            writer.close()

    async def page(self, route, request, writer):
        """One of the login's pages: no body is read (one that is sent is refused), and the answer ends the connection."""
        h = request.headers
        if h.get("content-length", "0") != "0" or "transfer-encoding" in h:
            raise http.HttpError(400, "body")
        peername = writer.get_extra_info("peername")
        address = limitmod.address_key(h.get("x-client-address"), peername[0] if peername else "")
        writer.write(await route(request, address))

    async def upgrade(self, request, reader, writer):
        """Check the handshake and take the socket in: an HttpError says no; otherwise the 101 and the welcome are written and the Peer is returned."""
        h = request.headers
        if request.method != "GET":
            raise http.HttpError(405, "method", {"Allow": "GET"})
        if not self.cfg.origin or h.get("origin") != self.cfg.origin:
            raise http.HttpError(403, "origin")
        if h.get("sec-fetch-site", "same-origin") != "same-origin":          # (a browser that says that the page is of another site: its Origin was the site's, but this is not the site's page)
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
        if len(self.chat.peers) >= self.limits.sockets_total:
            raise http.HttpError(503, "full", {"Retry-After": "30"})
        peername = writer.get_extra_info("peername")
        address = limitmod.address_key(h.get("x-client-address"), peername[0] if peername else "")
        if not self.per_address.try_add(address):
            raise http.HttpError(429, "address", {"Retry-After": "10"})
        peer = None
        try:                                                         # (the place of the address is given back whatever goes wrong from here)
            identity = await self.identify(h)
            if len(self.chat.peers) >= self.limits.sockets_total:      # (others were let in while the login layer was asked)
                raise http.HttpError(503, "full", {"Retry-After": "30"})
            peer = Peer(self, writer, address, self.log.tag(address), identity, self.clock())
            self.chat.peers.add(peer)
            writer.write(http.response(101, headers={"Upgrade": "websocket", "Connection": "Upgrade", "Sec-WebSocket-Accept": ws.accept_key(key)}))
            peer.send_text(chatmod.dumps(self.chat.welcome(peer)))
        except BaseException:
            self.per_address.remove(address)
            if peer is not None:
                peer.closed = True
                self.chat.peers.discard(peer)
            raise
        self.log.event("open", tag=peer.tag, online=len(self.chat.peers), account="yes" if peer.identity else "no")
        return peer

    def store_trouble(self, error):
        """The database could not be used for a message: one line in the log every ten seconds at the most (the class of the error, which says locked or full or refused)."""
        now = self.clock()
        if now >= self.next_trouble_log:
            self.next_trouble_log = now + 10.0
            self.log.event("error", kind=type(error).__name__, where="store")

    def end_logins(self, session=None, account=None):
        """The sockets of a login that was ended (its session key), or of every login of an account, are closed: a socket never outlives the login that it was let in with. The page
        connects again, as a reader."""
        for peer in list(self.chat.peers):
            who = peer.identity
            if who is not None and not peer.closed and ((session is not None and who.session == session) or (account is not None and who.account_id == account)):
                peer.close(1000, "logged out")

    async def identify(self, headers):
        try:
            who = await asyncio.wait_for(self.sessions.identify(headers), self.limits.identify_seconds)
            if who is not None and configmod.ACCOUNT_ID.match(who.account_id) and textrules.valid_name(who.name):
                return who
        except Exception as e:                                       # (a login that fails or is late, or hands out what is not an identity: the visitor reads, and cannot write)
            self.log.event("error", kind=type(e).__name__, where="session")
        return None

    # ---- the clock ------------------------------------------------------------------------------------------------------------------------

    async def tick(self):
        while True:
            await asyncio.sleep(self.limits.tick_seconds)
            try:
                self.on_tick(self.clock())
            except Exception as e:
                self.log.event("error", kind=type(e).__name__, where="tick")

    def on_tick(self, now):
        wall = self.store.clock()
        for peer in list(self.chat.peers):
            if peer.closed:
                if peer.closed_at is not None and now - peer.closed_at >= self.limits.close_grace_seconds:
                    peer.writer.transport.abort()                     # (a closed socket whose reader never takes the rest of the output would stay for ever)
            elif peer.identity is not None and peer.identity.expires and wall >= peer.identity.expires:
                peer.close(1000, "the login ran out")                  # (a login of 30 days, and a socket that is older than that)
            elif now - peer.last_rx >= self.limits.silence_seconds:
                peer.close(1001, "silent")
            elif now - peer.last_ping >= self.limits.ping_seconds:
                peer.last_ping = now
                peer.write(ws.encode_frame(ws.PING))
        count = len(self.chat.peers)
        if count != self.online_shown and now - self.online_at >= self.limits.online_seconds:
            self.online_shown, self.online_at = count, now
            self.chat.online()
        if now >= self.next_deletions:
            self.next_deletions = now + self.limits.deletions_seconds
            self.chat.sync_deletions()
        if now >= self.next_forget:
            self.next_forget = now + 60.0
            self.chat.account_limits.forget_idle(now)
            if hasattr(self.sessions, "forget_idle"):
                self.sessions.forget_idle(now, wall)
        if now >= self.next_purge:
            self.next_purge = now + self.limits.purge_failure_seconds         # (set first: a purge that fails is tried again in a minute, not at every tick)
            gone = self.store.purge(self.limits.purge_chunk)
            self.next_purge = now + (self.limits.tick_seconds if gone >= self.limits.purge_chunk else self.limits.purge_seconds)      # (a long backlog goes a chunk at a time)
            self.next_checkpoint = now + self.limits.checkpoint_retry_seconds
            if gone:
                self.log.event("purge", lines=gone)
        elif self.store.unsettled and now >= self.next_checkpoint:
            self.next_checkpoint = now + self.limits.checkpoint_retry_seconds
            self.store.checkpoint()
        if now >= self.next_crowded:
            self.next_crowded = now + self.limits.crowded_log_seconds
            biggest = max(self.per_address.counts.values(), default=0)
            if biggest > self.limits.crowded_sockets:
                self.log.event("crowded", sockets=biggest, groups=len(self.per_address.counts))      # (no address: see docs/HUB.md, "Behind a reverse proxy")


async def serve(cfg, store, sessions, log):
    hub = Hub(cfg, store, sessions, log)
    port = await hub.start(cfg.host, cfg.port)
    log.event("listening", port=port)
    stop = asyncio.Event()
    loop = asyncio.get_running_loop()
    for sig in (signal.SIGTERM, signal.SIGINT):
        loop.add_signal_handler(sig, stop.set)
    await stop.wait()
    log.event("stopping")
    await hub.stop()


def main(env=None):
    try:
        cfg = configmod.load(os.environ if env is None else env)
    except configmod.ConfigError as e:
        print("hub: %s" % e, file=sys.stderr)
        return 2
    log = Log()
    log.event("start", **cfg.summary())
    log.event("note", what="sockets_are_counted_for_each_X-Client-Address_that_nginx_sets_see_docs/HUB.md")
    if cfg.test_login:
        log.event("warning", what="test_login_is_on")
    if cfg.discord_test_url:
        log.event("warning", what="a_fake_discord_is_on")
    if (cfg.login_client_id or cfg.login_client_secret) and not cfg.login_on:
        log.event("warning", what="discord_login_off_needs_id_secret_and_site_address")
    store = storemod.Store(cfg.db, busy_ms=300)                      # (the one thread of the hub waits at most this long for another process's write)
    try:
        store.checkpoint()                  # (what a crash left in the log file)
        asyncio.run(serve(cfg, store, sessionmod.make(cfg), log))
    finally:
        store.close()
    return 0
