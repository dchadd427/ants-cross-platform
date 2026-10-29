"""Who is on the other end of a socket. The hub core asks a `Sessions` object and nothing else, so the real login (a later change) plugs in here.

An Identity is an account id (digits, 1 to 20: what a Discord id is), the display name (`text.valid_name`), and `young` (the account is too new to write). A socket that is not
identified reads the chat and cannot write. By default there is no login at all (NoLogin: the welcome says login:false and the hub is read-only for everyone). TestLogin exists for
the tests: it believes a header that the site's nginx never passes on, and the hub refuses to start with it unless it listens on a loopback address (hub/config.py).
"""
import collections

from . import config, text

Identity = collections.namedtuple("Identity", "account_id name young")


class Sessions:
    login = False                       # whether this hub offers a login at all (the welcome's `login`)

    def identify(self, headers):
        """An Identity for the request headers of a handshake (lower-case names), or None."""
        return None


class NoLogin(Sessions):
    pass


class TestLogin(Sessions):
    """X-Hub-Test-Login: <account id>:<name>[:young]"""

    login = True

    def identify(self, headers):
        parts = headers.get("x-hub-test-login", "").split(":")
        if len(parts) not in (2, 3) or not config.ACCOUNT_ID.match(parts[0]) or not text.valid_name(parts[1]) or (len(parts) == 3 and parts[2] != "young"):
            return None
        return Identity(parts[0], parts[1], len(parts) == 3)


def make(cfg):
    return TestLogin() if cfg.test_login else NoLogin()
