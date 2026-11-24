"""The hub's settings come from the environment and from nothing else (a command line shows in the process list). A secret is never printed: summary() is the one place that
describes the settings for a log line, and it names the ones that are safe."""
import dataclasses
import ipaddress
import re
from urllib.parse import urlsplit

ACCOUNT_ID = re.compile(r"[1-9][0-9]{0,19}\Z")           # a Discord id: digits, no leading zero
CLIENT_ID = re.compile(r"[0-9]{5,30}\Z")                 # the number of the Discord application
CLIENT_SECRET = re.compile(r"[\x21-\x7e]{8,256}\Z")      # printable characters, no blank
BOT_TOKEN = re.compile(r"[A-Za-z0-9._-]{30,200}\Z")        # a bot's token is one plain word; one that is not is not Discord's, and is never put in a header
SNOWFLAKE = re.compile(r"[1-9][0-9]{4,19}\Z")             # the id of a server or of a role
GATE_SECRET = re.compile(r"[\x21-\x7e]{32,256}\Z")         # the key of the admin page's door (ANTS_ADMIN_GATE_SECRET): 32 to 256 printable characters, no blank
GATE_SETTING = "ANTS_ADMIN_GATE_SECRET"                    # the one setting of the key: the game server reads the same name (docs/HUB.md, "The admin page's door")
GATE_SETTING_OLD = "ANTS_HUB_ADMIN_GATE_SECRET"            # its name before the two services shared one: still read, when the new name is not set, until the release after this one
GATE_DISTINCT = 8                                        # a key of fewer different characters than this (one repeated character) is not a key
SAMPLE_GATE_KEY = "canary-gate-key-5d8f0c1e7a9b4263-q"      # the made-up key of the known answer in docs/HUB.md: published, so never a key (the game server refuses it too)
GAME_ADMIN_URL = re.compile(r"http://([A-Za-z0-9](?:[A-Za-z0-9.-]{0,251}[A-Za-z0-9])?):([0-9]{1,5})\Z")      # ANTS_HUB_GAME_ADMIN_URL: `http://` and a host of letters, digits, dots and dashes, a colon and a port, nothing else
MIN_RECHECK_SECONDS = 60
MAX_RECHECK_SECONDS = 3600
RECHECK_SECONDS = 180                                       # how often the role of everybody who is here is asked again (ANTS_HUB_ROLE_RECHECK_SECONDS)
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
    discord_bot_token: str = dataclasses.field(default="", repr=False)      # the bot's token (hub/roles.py asks Discord which roles a person holds); a secret, never printed
    discord_guild: str = dataclasses.field(default="", repr=False)          # the Discord server whose roles count
    role_admin: str = dataclasses.field(default="", repr=False)             # the role that makes an administrator
    role_mod: str = dataclasses.field(default="", repr=False)               # the role that makes a moderator
    role_images: str = dataclasses.field(default="", repr=False)            # the role that lets a person post images (docs/HUB_IMAGES.md): its id is only in the stack's environment, never printed
    role_recheck_seconds: int = RECHECK_SECONDS
    owner: str = dataclasses.field(default="", repr=False)                  # one account id that is always an administrator, whatever Discord says (or does not)
    test_login: bool = False
    login_client_id: str = dataclasses.field(default="", repr=False)
    login_client_secret: str = dataclasses.field(default="", repr=False)    # the Discord login's (hub/login.py); never printed
    min_account_days: int = MIN_ACCOUNT_DAYS                                               # a Discord account younger than this cannot write (its age is in its id)
    admin_gate_secret: str = dataclasses.field(default="", repr=False)      # the key that signs the admin page's pass and its CSRF token (hub/admindoor.py); a secret, never printed; "" when absent or not of the shape: the door is shut
    admin_gate_sample: bool = False                                         # the setting is the documents' sample key (refused: the start says so, apart from a key that is simply not good)
    admin_gate_given: bool = False                                          # whether the setting was there at all (a value that is there and is no good is said at the start; one that is absent is said as well, differently)
    discord_test_url: str = ""                                              # TESTS ONLY: where the fake Discord of the tests listens (a loopback address, refused for any other)
    game_admin_url: str = ""                                                # where the game server's admin door is, `http://host:port` (hub/gamedoor.py: `/bots`); "" is none, and `/bots` answers `off`
    counts_url: str = ""                                                    # where the game server's counts door is, `http://host:port` (hub/likedoor.py: Forget me removes the person's likes of replays there first); "" is none, and the call is skipped
    image_converter_url: str = ""                                           # where the image converter is, `http://host:port` (hub/imgdoor.py); "" is none, and images are off for everybody
    image_dir: str = "/images"                                              # the folder of the images' files (the volume that the site's web server reads, read only)
    ai_keys: bool = False                                                   # ANTS_HUB_AI_KEYS=on: the keys that let a person's own AI work in the person's open Map Maker tab (docs/MAP_AI.md); off, every door of it answers 503 `ai_off`

    @property
    def login_on(self):
        """Whether this hub offers the Discord login: both settings of the application and the site's address (the way back is <site>/hub/callback)."""
        return bool(self.login_client_id and self.login_client_secret and self.origin)

    @property
    def admin_gate_on(self):
        """Whether the admin page's door can open: the key is there and of the shape (32 or more printable characters). Otherwise the door is shut: the gate answers 503 to everybody."""
        return bool(GATE_SECRET.match(self.admin_gate_secret))

    @property
    def roles_on(self):
        """Whether the roles are asked of Discord: the bot's token, the server and at least one of the three roles."""
        return bool(self.discord_bot_token and self.discord_guild and (self.role_admin or self.role_mod or self.role_images))

    @property
    def roles_partial(self):
        """Some of the roles' settings are given, but not enough to switch them on."""
        return not self.roles_on and bool(self.discord_bot_token or self.discord_guild or self.role_admin or self.role_mod or self.role_images)

    def summary(self):
        """Safe to log: no secret, no account id, no address."""
        return {"port": self.port, "origin": self.origin or "none", "roles_configured": int(self.roles_on), "owner": int(bool(self.owner)), "recheck": self.role_recheck_seconds,
                "test_login": int(self.test_login), "login_configured": int(self.login_on), "min_account_days": self.min_account_days,
                "role_images_configured": int(bool(self.role_images)), "images_configured": int(bool(self.image_converter_url))}


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


def parse_game_admin_url(url):
    """'http://ants-server:4030' (a host of letters, digits, dots and dashes, and a port from 1 to 65535: nothing after the port, no path, no user, no query) for the game server's admin door, else a ConfigError
    that never says the value."""
    found = GAME_ADMIN_URL.match(url.strip())
    if not found or not 1 <= int(found.group(2)) <= 65535:
        raise ConfigError("ANTS_HUB_GAME_ADMIN_URL must be like http://ants-server:4030 (http, a host name or address of letters, digits, dots and dashes, and a port from 1 to 65535, nothing else)")
    return "http://%s:%d" % (found.group(1), int(found.group(2)))


def parse_counts_url(url):
    """'http://ants-server:4032' (the same shape as the game server's admin address) for the game server's counts door, else a ConfigError that never says the value."""
    found = GAME_ADMIN_URL.match(url.strip())
    if not found or not 1 <= int(found.group(2)) <= 65535:
        raise ConfigError("ANTS_HUB_COUNTS_URL must be like http://ants-server:4032 (http, a host name or address of letters, digits, dots and dashes, and a port from 1 to 65535, nothing else)")
    return "http://%s:%d" % (found.group(1), int(found.group(2)))


def parse_converter_url(url):
    """'http://ants-img:4050' (the same shape as the game server's address) for the image converter, else a ConfigError that never says the value."""
    found = GAME_ADMIN_URL.match(url.strip())
    if not found or not 1 <= int(found.group(2)) <= 65535:
        raise ConfigError("ANTS_HUB_IMAGE_CONVERTER_URL must be like http://ants-img:4050 (http, a host name or address of letters, digits, dots and dashes, and a port from 1 to 65535, nothing else)")
    return "http://%s:%d" % (found.group(1), int(found.group(2)))


def load(env):
    try:
        port = int(env.get("ANTS_HUB_PORT", "4040"))
    except ValueError:
        raise ConfigError("ANTS_HUB_PORT must be a number")
    if not 0 <= port <= 65535:
        raise ConfigError("ANTS_HUB_PORT must be 0 to 65535")
    cfg = Config(host=env.get("ANTS_HUB_HOST", "127.0.0.1"), port=port, db=env.get("ANTS_HUB_DB", "/data/hub.db"), origin=parse_origin(env.get("ANTS_HUB_PUBLIC_URL", "")),
                 test_login=env.get("ANTS_HUB_TEST_LOGIN", "") == "1",
                 login_client_id=env.get("ANTS_HUB_DISCORD_CLIENT_ID", "").strip(), login_client_secret=env.get("ANTS_HUB_DISCORD_CLIENT_SECRET", "").strip())
    if cfg.login_client_id and not CLIENT_ID.match(cfg.login_client_id):
        raise ConfigError("ANTS_HUB_DISCORD_CLIENT_ID must be the application's number (digits only)")
    if cfg.login_client_secret and not CLIENT_SECRET.match(cfg.login_client_secret):
        raise ConfigError("ANTS_HUB_DISCORD_CLIENT_SECRET must be 8 to 256 printable characters with no blank")
    gate = env.get(GATE_SETTING, "").strip() or env.get(GATE_SETTING_OLD, "").strip()        # (the shared name wins when it is set, so that one value is the key on both sides)
    cfg.admin_gate_given = bool(gate)
    cfg.admin_gate_sample = gate == SAMPLE_GATE_KEY
    cfg.admin_gate_secret = gate if GATE_SECRET.match(gate) and len(set(gate)) >= GATE_DISTINCT and not cfg.admin_gate_sample else ""        # (a short, malformed or sample key never stops the hub: the door stays shut and the start says so)
    cfg.discord_bot_token, cfg.discord_guild = env.get("ANTS_HUB_DISCORD_BOT_TOKEN", "").strip(), env.get("ANTS_HUB_DISCORD_GUILD", "").strip()
    cfg.role_admin, cfg.role_mod, cfg.role_images, cfg.owner = (env.get(name, "").strip() for name in ("ANTS_HUB_ROLE_ADMIN", "ANTS_HUB_ROLE_MOD", "ANTS_HUB_ROLE_IMAGES", "ANTS_HUB_OWNER"))
    if cfg.discord_bot_token and not BOT_TOKEN.match(cfg.discord_bot_token):
        raise ConfigError("ANTS_HUB_DISCORD_BOT_TOKEN must be one word of 30 to 200 letters, digits, dots, dashes and underscores")
    for name, value in (("ANTS_HUB_DISCORD_GUILD", cfg.discord_guild), ("ANTS_HUB_ROLE_ADMIN", cfg.role_admin), ("ANTS_HUB_ROLE_MOD", cfg.role_mod), ("ANTS_HUB_ROLE_IMAGES", cfg.role_images)):
        if value and not SNOWFLAKE.match(value):
            raise ConfigError("%s must be a Discord id (digits only)" % name)
    if (cfg.role_admin and cfg.role_admin == cfg.role_mod) or (cfg.role_images and cfg.role_images in (cfg.role_admin, cfg.role_mod)):
        raise ConfigError("ANTS_HUB_ROLE_ADMIN, ANTS_HUB_ROLE_MOD and ANTS_HUB_ROLE_IMAGES must be different roles")
    if cfg.owner and not ACCOUNT_ID.match(cfg.owner):
        raise ConfigError("ANTS_HUB_OWNER must be an account id (digits only)")
    try:
        cfg.role_recheck_seconds = int(env.get("ANTS_HUB_ROLE_RECHECK_SECONDS", "").strip() or RECHECK_SECONDS)
    except ValueError:
        raise ConfigError("ANTS_HUB_ROLE_RECHECK_SECONDS must be a number of seconds")
    if not MIN_RECHECK_SECONDS <= cfg.role_recheck_seconds <= MAX_RECHECK_SECONDS:
        raise ConfigError("ANTS_HUB_ROLE_RECHECK_SECONDS must be %d to %d" % (MIN_RECHECK_SECONDS, MAX_RECHECK_SECONDS))
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
    if env.get("ANTS_HUB_GAME_ADMIN_URL", "").strip():
        cfg.game_admin_url = parse_game_admin_url(env["ANTS_HUB_GAME_ADMIN_URL"])
    if env.get("ANTS_HUB_COUNTS_URL", "").strip():
        cfg.counts_url = parse_counts_url(env["ANTS_HUB_COUNTS_URL"])
    if env.get("ANTS_HUB_IMAGE_CONVERTER_URL", "").strip():
        cfg.image_converter_url = parse_converter_url(env["ANTS_HUB_IMAGE_CONVERTER_URL"])
    cfg.image_dir = env.get("ANTS_HUB_IMAGE_DIR", "").strip() or cfg.image_dir
    ai = env.get("ANTS_HUB_AI_KEYS", "").strip().lower() or "off"
    if ai not in ("on", "off"):
        raise ConfigError("ANTS_HUB_AI_KEYS must be on or off")
    cfg.ai_keys = ai == "on"
    return cfg
