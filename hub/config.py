"""The hub's settings come from the environment and from nothing else (a command line shows in the process list). A secret is never printed: summary() is the one place that
describes the settings for a log line, and it names the ones that are safe."""
import dataclasses
import ipaddress
import re
from urllib.parse import urlsplit

ACCOUNT_ID = re.compile(r"[1-9][0-9]{0,19}\Z")           # a Discord id: digits, no leading zero
CLIENT_ID = re.compile(r"[0-9]{5,30}\Z")                 # the number of the Discord application
CLIENT_SECRET = re.compile(r"[\x21-\x7e]{8,256}\Z")      # printable characters, no blank
MAX_MIN_ACCOUNT_DAYS = 3650
MIN_ACCOUNT_DAYS = 7                                                         # the default age that a Discord account needs to write (ANTS_HUB_MIN_ACCOUNT_DAYS)


class ConfigError(Exception):
    pass


@dataclasses.dataclass
class Config:
    host: str = "127.0.0.1"                                         # the image sets 0.0.0.0 (a published port is not on the container's loopback address)
    port: int = 4040
    db: str = "/data/hub.db"
    origin: str = ""                                                # the site's address (scheme and host): the only Origin a socket may come from; empty: no socket is let in
    mods: frozenset = dataclasses.field(default=frozenset(), repr=False)    # account ids that may type /mute and /del
    test_login: bool = False
    login_client_id: str = dataclasses.field(default="", repr=False)
    login_client_secret: str = dataclasses.field(default="", repr=False)    # the Discord login's (hub/login.py); never printed
    min_account_days: int = MIN_ACCOUNT_DAYS                                               # a Discord account younger than this cannot write (its age is in its id)
    discord_test_url: str = ""                                              # TESTS ONLY: where the fake Discord of the tests listens (a loopback address, refused for any other)

    @property
    def login_on(self):
        """Whether this hub offers the Discord login: both settings of the application and the site's address (the way back is <site>/hub/callback)."""
        return bool(self.login_client_id and self.login_client_secret and self.origin)

    def summary(self):
        """Safe to log: no secret, no account id, no address."""
        return {"port": self.port, "origin": self.origin or "none", "mods": len(self.mods), "test_login": int(self.test_login), "login_configured": int(self.login_on),
                "min_account_days": self.min_account_days}


def parse_origin(url):
    """'https://beta.example.org/' -> 'https://beta.example.org' (the form a browser sends in Origin)."""
    if not url:
        return ""
    problem = ConfigError("ANTS_HUB_PUBLIC_URL must be like https://example.org (a scheme and a host, nothing else)")
    try:
        parts = urlsplit(url.strip())
        port = parts.port
        host = parts.hostname
    except ValueError:                                              # (a port that is not a number, an unclosed bracket)
        raise problem
    if parts.scheme not in ("http", "https") or not host or parts.username or parts.password or parts.path not in ("", "/") or parts.query or parts.fragment:
        raise problem
    host = host.lower()
    if ":" in host:
        host = "[%s]" % host
    default = {"http": 80, "https": 443}[parts.scheme]
    return "%s://%s%s" % (parts.scheme, host, "" if port in (None, default) else ":%d" % port)


def parse_mods(value):
    mods = []
    for n, item in enumerate(re.split(r"[,\s]+", value.strip()) if value.strip() else [], 1):
        if not ACCOUNT_ID.match(item):
            raise ConfigError("ANTS_HUB_MODS: entry %d is not an account id (digits only)" % n)
        mods.append(item)
    return frozenset(mods)


def is_loopback(host):
    try:
        return ipaddress.ip_address(host).is_loopback
    except ValueError:
        return host == "localhost"


def parse_test_url(url):
    """'http://127.0.0.1:4999' (a loopback address, nothing after the port) for the fake Discord of the tests, else a ConfigError."""
    problem = ConfigError("ANTS_HUB_TEST_DISCORD_URL must be like http://127.0.0.1:4999 (a loopback address and a port, nothing else)")
    try:
        parts = urlsplit(url.strip())
        port, host = parts.port, parts.hostname
    except ValueError:
        raise problem
    if parts.scheme not in ("http", "https") or not host or port is None or not is_loopback(host) or parts.username or parts.password or parts.path not in ("", "/") or parts.query or parts.fragment:
        raise problem
    return "%s://%s:%d" % (parts.scheme, "[%s]" % host if ":" in host else host, port)


def load(env):
    try:
        port = int(env.get("ANTS_HUB_PORT", "4040"))
    except ValueError:
        raise ConfigError("ANTS_HUB_PORT must be a number")
    if not 0 <= port <= 65535:
        raise ConfigError("ANTS_HUB_PORT must be 0 to 65535")
    cfg = Config(host=env.get("ANTS_HUB_HOST", "127.0.0.1"), port=port, db=env.get("ANTS_HUB_DB", "/data/hub.db"), origin=parse_origin(env.get("ANTS_HUB_PUBLIC_URL", "")),
                 mods=parse_mods(env.get("ANTS_HUB_MODS", "")), test_login=env.get("ANTS_HUB_TEST_LOGIN", "") == "1",
                 login_client_id=env.get("ANTS_HUB_DISCORD_CLIENT_ID", "").strip(), login_client_secret=env.get("ANTS_HUB_DISCORD_CLIENT_SECRET", "").strip())
    if cfg.login_client_id and not CLIENT_ID.match(cfg.login_client_id):
        raise ConfigError("ANTS_HUB_DISCORD_CLIENT_ID must be the application's number (digits only)")
    if cfg.login_client_secret and not CLIENT_SECRET.match(cfg.login_client_secret):
        raise ConfigError("ANTS_HUB_DISCORD_CLIENT_SECRET must be 8 to 256 printable characters with no blank")
    try:
        cfg.min_account_days = int(env.get("ANTS_HUB_MIN_ACCOUNT_DAYS", str(MIN_ACCOUNT_DAYS)).strip())
    except ValueError:
        raise ConfigError("ANTS_HUB_MIN_ACCOUNT_DAYS must be a number of days")
    if not 0 <= cfg.min_account_days <= MAX_MIN_ACCOUNT_DAYS:
        raise ConfigError("ANTS_HUB_MIN_ACCOUNT_DAYS must be 0 to %d" % MAX_MIN_ACCOUNT_DAYS)
    if cfg.test_login and not is_loopback(cfg.host):
        raise ConfigError("ANTS_HUB_TEST_LOGIN=1 is for tests only: it needs ANTS_HUB_HOST to be a loopback address")
    if env.get("ANTS_HUB_TEST_DISCORD_URL", "").strip():
        if not is_loopback(cfg.host):
            raise ConfigError("ANTS_HUB_TEST_DISCORD_URL is for tests only: it needs ANTS_HUB_HOST to be a loopback address")
        cfg.discord_test_url = parse_test_url(env["ANTS_HUB_TEST_DISCORD_URL"])
    if cfg.test_login and cfg.login_on:
        raise ConfigError("ANTS_HUB_TEST_LOGIN=1 and the Discord login cannot both be on")
    return cfg
