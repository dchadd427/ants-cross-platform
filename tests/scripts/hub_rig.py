"""A hub that runs in a thread of the test process, on a loopback port, for the tests that talk to it with tests/scripts/hub_client.py. The real code and real sockets, with the limits
and timers the test asks for (a second of silence instead of a minute)."""
import asyncio
import dataclasses
import io
import os
import sys
import tempfile
import threading

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from hub import config, limits as limitmod, server, session, store  # noqa: E402
from hub.log import Log  # noqa: E402

SITE = "https://chat.example.org"


class Rig:
    def __init__(self, mods="", test_login=True, origin=SITE, db=None, **limit_overrides):
        self.folder = None if db else tempfile.TemporaryDirectory()
        self.db = db or os.path.join(self.folder.name, "hub.db")
        env = {"ANTS_HUB_DB": self.db, "ANTS_HUB_PUBLIC_URL": origin, "ANTS_HUB_HOST": "127.0.0.1", "ANTS_HUB_MODS": mods}
        if test_login:
            env["ANTS_HUB_TEST_LOGIN"] = "1"
        self.cfg = config.load(env)
        self.limits = dataclasses.replace(limitmod.Limits(), **limit_overrides)
        self.log_text = io.StringIO()
        self.port = None
        self.hub = None
        self.loop = asyncio.new_event_loop()
        self.thread = threading.Thread(target=self.loop.run_forever, daemon=True)

    def __enter__(self):
        self.thread.start()
        self.port = asyncio.run_coroutine_threadsafe(self.start(), self.loop).result(10)
        return self

    async def start(self):
        self.store = store.Store(self.cfg.db, busy_ms=300)
        self.hub = server.Hub(self.cfg, self.store, session.make(self.cfg), Log(self.log_text), self.limits)
        return await self.hub.start("127.0.0.1", 0)

    def call(self, function, *args):
        """Run function(*args) on the hub's thread (the hub's own objects belong there) and return its result."""
        async def run():
            return function(*args)
        return asyncio.run_coroutine_threadsafe(run(), self.loop).result(10)

    def __exit__(self, *exc):
        try:
            asyncio.run_coroutine_threadsafe(self.hub.stop(), self.loop).result(10)
            self.call(self.store.close)
        finally:
            self.loop.call_soon_threadsafe(self.loop.stop)
            self.thread.join(5)
            self.loop.close()
            if self.folder:
                self.folder.cleanup()
        return False
