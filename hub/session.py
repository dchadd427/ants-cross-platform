"""Who is on the other end of a socket. The hub core asks a `Sessions` object and nothing else: the Discord login (hub/login.py) plugs in here.

An Identity is an account id (digits, 1 to 20: what a Discord id is), the name that is shown (`name`: the Discord display name that the person picked, else the username), `young` (the account
is too new to write), and for a real login the key of its session and when that runs out (so that the hub can end the sockets of a login that was ended), the `username` (`text.valid_name`:
the one name that is unique, the key of the account for a moderator; it is the name when there is no other), and, where the account has a display name as well, `pick` (which of the two is
shown: "display" or "username") and `other` (the other one: the page offers the switch only where there is one). A name is not unique, a username is.

A socket that is not identified reads the chat and cannot write. Without the Discord settings there is no login at all (NoLogin: the welcome says login:false and the hub is read-only for
everyone). TestLogin exists for the tests: it believes a header that the site's nginx never passes on, and the hub refuses to start with it unless it listens on a loopback address
(hub/config.py).

A Sessions object may also have `routes` (a path -> coroutine(request, address) that answers with the bytes of a whole HTTP response: the pages of the login) and `attach(hub)`, which
the hub calls once, so that the login can end the sockets of a login that was ended.
"""
import collections

from . import config, text


class Identity(collections.namedtuple("Identity", "account_id name young session expires username pick other")):
    __slots__ = ()

    def __new__(cls, account_id, name, young, session=None, expires=0, username=None, pick="", other=""):
        return super().__new__(cls, account_id, name, young, session, expires, name if username is None else username, pick, other)


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
