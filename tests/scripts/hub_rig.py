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
import fake_discord  # noqa: E402
from hub import config, limits as limitmod, server, session, store  # noqa: E402
from hub.log import Log  # noqa: E402

SITE = "https://chat.example.org"
WAIT = 30           # seconds that the test thread waits for the hub's thread to start, to run a function and to stop (a machine under load is slow, and a test must not fail for that)


class FakeRoles:
    """hub.roles.Roles without Discord: a dictionary of account -> `none`, `mod` or `admin` (what the chat tests need), and the same calls. `set` changes a role the way an answer of Discord would."""

    def __init__(self, roles=None):
        self.roles = dict(roles or {})
        self.connected = lambda: ()
        self.changed = lambda account: None
        self.wanted = []

    def role_of(self, account):
        return self.roles.get(account, "none")

    def want(self, account):
        self.wanted.append(account)

    def on_tick(self, now):
        pass

    def start(self):
        pass

    def stop(self):
        pass

    def set(self, account, role):
        self.roles[account] = role
        self.changed(account)


class Rig:
    def __init__(self, mods="", test_login=True, origin=SITE, db=None, discord=None, env=None, max_lines=10 ** 9, roles=None, member_fake=None, owner="", recheck=None, **limit_overrides):
        """discord: a tests/scripts/fake_discord.FakeDiscord: the hub then has the real Discord login (and no test login), with the fake as its Discord. env: more settings. mods: account ids
        (comma separated) that are moderators, with no Discord (a FakeRoles; `roles` is the same as a dictionary, or a FakeRoles). member_fake: a FakeDiscord whose members hold the roles: the hub
        then has the real hub.roles.Roles against it, with the settings that the fake knows (`recheck` seconds, for a test that cannot wait three minutes; `owner` an account that is always admin)."""
        self.folder = None if db else tempfile.TemporaryDirectory()
        self.db = db or os.path.join(self.folder.name, "hub.db")
        env = dict(env or {}, ANTS_HUB_DB=self.db, ANTS_HUB_PUBLIC_URL=origin, ANTS_HUB_HOST="127.0.0.1")
        if member_fake is not None:
            env.update(ANTS_HUB_DISCORD_BOT_TOKEN=member_fake.bot_token, ANTS_HUB_DISCORD_GUILD=member_fake.guild, ANTS_HUB_ROLE_ADMIN=fake_discord.ROLE_ADMIN, ANTS_HUB_ROLE_MOD=fake_discord.ROLE_MOD,
                       ANTS_HUB_TEST_DISCORD_URL=member_fake.url)
        if owner:
            env["ANTS_HUB_OWNER"] = owner
        if discord is not None:
            test_login = False
            env.update(ANTS_HUB_DISCORD_CLIENT_ID=discord.client_id, ANTS_HUB_DISCORD_CLIENT_SECRET=discord.client_secret, ANTS_HUB_TEST_DISCORD_URL=discord.url)
        if test_login:
            env["ANTS_HUB_TEST_LOGIN"] = "1"
        self.cfg = config.load(env)
        if recheck is not None:
            self.cfg.role_recheck_seconds = recheck
        self.roles = roles if hasattr(roles, "role_of") else FakeRoles(dict(roles or {}, **{a: "mod" for a in mods.split(",") if a})) if (roles or mods) else None
        self.max_lines = max_lines                  # (the newest lines the store keeps: out of the way by default, so that the tests that need a big backlog have one; a test of the limit passes 200)
        self.limits = dataclasses.replace(limitmod.Limits(), **limit_overrides)
        self.log_text = io.StringIO()
        self.port = None
        self.hub = None
        self.loop = asyncio.new_event_loop()
        self.thread = threading.Thread(target=self.loop.run_forever, daemon=True)

    def __enter__(self):
        self.thread.start()
        self.port = asyncio.run_coroutine_threadsafe(self.start(), self.loop).result(WAIT)
        return self

    async def start(self):
        self.store = store.Store(self.cfg.db, busy_ms=300, max_lines=self.max_lines)
        self.hub = server.Hub(self.cfg, self.store, session.make(self.cfg), Log(self.log_text), self.limits, roles=self.roles)
        return await self.hub.start("127.0.0.1", 0)

    def call(self, function, *args):
        """Run function(*args) on the hub's thread (the hub's own objects belong there) and return its result."""
        async def run():
            return function(*args)
        return asyncio.run_coroutine_threadsafe(run(), self.loop).result(WAIT)

    def __exit__(self, *exc):
        try:
            asyncio.run_coroutine_threadsafe(self.hub.stop(), self.loop).result(WAIT)
            self.call(self.store.close)
        finally:
            self.loop.call_soon_threadsafe(self.loop.stop)
            self.thread.join(5)
            self.loop.close()
            if self.folder:
                self.folder.cleanup()
        return False
