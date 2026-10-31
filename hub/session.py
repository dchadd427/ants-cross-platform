"""Who is on the other end of a socket. The hub core asks a `Sessions` object and nothing else: the Discord login (hub/login.py) plugs in here.

An Identity is an account id (digits, 1 to 20: what a Discord id is), the display name (`text.valid_name`), `young` (the account is too new to write), and for a real login the key of
its session and when that runs out (so that the hub can end the sockets of a login that was ended). A socket that is not identified reads the chat and cannot write. Without the
Discord settings there is no login at all (NoLogin: the welcome says login:false and the hub is read-only for everyone). TestLogin exists for the tests: it believes a header that the
site's nginx never passes on, and the hub refuses to start with it unless it listens on a loopback address (hub/config.py).

A Sessions object may also have `routes` (a path -> coroutine(request, address) that answers with the bytes of a whole HTTP response: the pages of the login) and `attach(hub)`, which
the hub calls once, so that the login can end the sockets of a login that was ended.
"""
import collections

from . import config, text

Identity = collections.namedtuple("Identity", "account_id name young session expires", defaults=(None, 0))


class Sessions:
    login = False                       # whether this hub offers a login at all (the welcome's `login`)
    routes = {}                         # the HTTP addresses of the login (hub/login.py); none for the others

    def attach(self, hub):
        """Called once by the Hub that asks this object (its store, limits, log, sockets)."""

    async def identify(self, headers):
        """An Identity for the request headers of a handshake (lower-case names), or None. A coroutine, because a real login looks something up (the hub waits `identify_seconds` for it and
        takes a login that fails or is late for a reader)."""
        return None


class NoLogin(Sessions):
    pass


class TestLogin(Sessions):
    """X-Hub-Test-Login: <account id>:<name>[:young]"""

    login = True

    async def identify(self, headers):
        parts = headers.get("x-hub-test-login", "").split(":")
        if len(parts) not in (2, 3) or not config.ACCOUNT_ID.match(parts[0]) or not text.valid_name(parts[1]) or (len(parts) == 3 and parts[2] != "young"):
            return None
        return Identity(parts[0], parts[1], len(parts) == 3)


def make(cfg):
    if cfg.login_on:
        from . import login                 # (only a hub with the Discord settings loads it)
        return login.DiscordLogin(cfg)
    return TestLogin() if cfg.test_login else NoLogin()
