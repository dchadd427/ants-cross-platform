"""What a page may tell the counter, and what of it is kept (docs/ANALYTICS.md, "What is counted"). Every field of an event that a page sends is checked here and cut down to a small set of
plain values: a path is one of the site's own pages, a referrer is a site's name, an error is a class, a cleaned message and a file and line of the site's own scripts. Anything else is not kept.

An event is a JSON object with a type `t`:
  view   {t, p: the page's path, r: the host that sent the visitor (may be empty), w: the width of the window in pixels, l: the browser's language}
  leave  {t, p}                                              the page was left (it ends the time of the visit)
  perf   {t, n: "load" or "game", ms: milliseconds, v: the site's version}
  error  {t, k: "Page" or "Game", n: the class of the error, m: its message, s: the file and line of the site's own script}
  visit  {t}                                                 all that a browser that asked not to be tracked sends

The checked event is a dictionary of exactly the keys of EMPTY (`name` is the timing's name or the kind of an error; `msg` the class of an error and its cleaned message); the door adds the country,
the browser and the other names, and the visitor's code.
"""
import datetime
import re
from urllib.parse import urlsplit

MAX_BODY = 1024
KINDS = frozenset(("view", "leave", "perf", "error"))
PAGES = frozenset(("/", "/play", "/matches", "/match", "/mapmaker", "/map-library", "/legal", "/changelog"))
OTHER = "other"                                   # any path that is not one of the pages above (and /map/<name>, which is one page for all the maps: "/map")
HOST = re.compile(r"(?=.{4,100}\Z)[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?(?:\.[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?)+\Z")
NUMERIC_HOST = re.compile(r"[0-9.]+\Z")
LANGUAGE = re.compile(r"([A-Za-z]{2,3})(?:[-_][A-Za-z0-9]{1,8})*\Z")
VERSION = re.compile(r"[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\Z")
ERROR_NAME = re.compile(r"[A-Za-z]{2,40}\Z")
SOURCE = re.compile(r"/?([A-Za-z0-9_][A-Za-z0-9_./-]{0,59}\.(?:js|mjs|wasm|html)):([0-9]{1,6})(?::[0-9]{1,6})?\Z")
ORIGIN_PREFIX = re.compile(r"https?://")
QUERY_BEFORE_LINE = re.compile(r"[?#][^:]*(?=:[0-9])")
WIDTH_EDGES = (400, 768, 1200, 1600)              # the buckets of the window's width: under 400, 400 to 767, 768 to 1199, 1200 to 1599, 1600 and up
MAX_PERF_MS = 600000
MESSAGE_MAX = 160
KEYS = ("t", "p", "r", "w", "l", "ms", "n", "v", "k", "m", "s")
EMPTY = {"kind": "", "path": "", "ref": "", "wb": -1, "lang": "", "ms": 0, "name": "", "msg": "", "src": "", "ver": ""}

URL_LIKE = re.compile(r"[A-Za-z][A-Za-z0-9+.-]{1,12}://\S+|www\.\S+")
EMAIL_LIKE = re.compile(r"\S+@\S+\.\S+")
ADDRESS_LIKE = re.compile(r"\b[0-9]{1,3}(?:\.[0-9]{1,3}){3}\b|\b[0-9A-Fa-f]{0,4}(?::[0-9A-Fa-f]{0,4}){3,7}\b")
LONG_NUMBER = re.compile(r"[0-9]{5,}")
LONG_WORD = re.compile(r"[A-Za-z0-9_+/=-]{24,}")
DOUBLE_QUOTED = re.compile(r'"[^"]*"')
SINGLE_QUOTED = re.compile(r"'([^']*)'")


def clean_message(text):
    """The message of an error as it is kept: plain printable ASCII, no address, no email, no long number or token, and no text that was in double quotes (an error often quotes what it could not read); a
    word in single quotes stays when it is short and has no blank (the name of a property or a function), else it goes. At most MESSAGE_MAX characters."""
    if not isinstance(text, str):
        return ""
    text = "".join(c if " " <= c <= "~" else " " for c in text[:600])
    text = URL_LIKE.sub("<url>", text)
    text = EMAIL_LIKE.sub("<email>", text)
    text = ADDRESS_LIKE.sub("<address>", text)
    text = DOUBLE_QUOTED.sub('"..."', text)
    text = SINGLE_QUOTED.sub(lambda m: m.group(0) if 0 < len(m.group(1)) <= 30 and " " not in m.group(1) and not LONG_NUMBER.search(m.group(1)) else "'...'", text)
    text = LONG_NUMBER.sub("<number>", text)
    text = LONG_WORD.sub("<token>", text)
    return " ".join(text.split())[:MESSAGE_MAX].rstrip()


def clean_source(text, origin=""):
    """`front/lobby_net.js:412` for the file and line of one of the site's own scripts (the site's address and a query are cut), else "": the address of another site is not kept."""
    if not isinstance(text, str) or len(text) > 400:
        return ""
    if ORIGIN_PREFIX.match(text):
        if not origin or not text.startswith(origin + "/"):
            return ""
        text = text[len(origin):]
    found = SOURCE.match(QUERY_BEFORE_LINE.sub("", text))
    return "%s:%s" % (found.group(1), found.group(2)) if found else ""


def page_of(path):
    """The page that a path is: one of PAGES, "/map" for any /map/<name>, else OTHER."""
    if not isinstance(path, str) or not path.startswith("/") or len(path) > 200:
        return OTHER
    path = path.split("?")[0].split("#")[0]
    if path != "/" and path.endswith("/"):
        path = path[:-1]
    if path in PAGES:
        return path
    return "/map" if re.match(r"/map/[a-z0-9-]{1,100}\Z", path) else OTHER


def host_of(text, own_host):
    """The name of the site that sent a visitor, as it is kept: lower case, `www.` cut, no address, no port; "" for nothing, for this site itself and for anything that is not a plain name."""
    if not isinstance(text, str):
        return ""
    text = text.strip().lower()
    if text.startswith("www."):
        text = text[4:]
    if not HOST.match(text) or NUMERIC_HOST.match(text) or text == own_host:
        return ""
    return text


def language_of(text):
    found = LANGUAGE.match(text) if isinstance(text, str) and len(text) <= 35 else None
    return found.group(1).lower() if found else ""


def width_bucket(value):
    """0 to 4 for a width in pixels (see WIDTH_EDGES), -1 for anything that is not a width."""
    if type(value) is not int or not 1 <= value <= 20000:
        return -1
    return sum(1 for edge in WIDTH_EDGES if value >= edge)


def check(body, origin=""):
    """The kept form of an event (a copy of EMPTY with its fields filled), or None for a body that is not an event of a known type with the right fields. Nothing that is not listed here is kept."""
    if not isinstance(body, dict) or any(key not in KEYS for key in body):
        return None
    own_host = urlsplit(origin).hostname or ""
    kind = body.get("t")
    if kind == "visit":
        return dict(EMPTY, kind="visit") if set(body) == {"t"} else None
    if kind not in KINDS:
        return None
    event = dict(EMPTY, kind=kind)
    if kind in ("view", "leave"):
        if not isinstance(body.get("p"), str):
            return None
        event["path"] = page_of(body["p"])
    if kind == "view":
        event["ref"] = host_of(body.get("r", ""), own_host)
        event["wb"] = width_bucket(body.get("w"))
        event["lang"] = language_of(body.get("l", ""))
    elif kind == "perf":
        ms = body.get("ms")
        if body.get("n") not in ("load", "game") or type(ms) is not int or not 1 <= ms <= MAX_PERF_MS:
            return None
        event.update(name=body["n"], ms=ms, ver=body["v"] if isinstance(body.get("v"), str) and VERSION.match(body["v"]) else "")
    elif kind == "error":
        name = body.get("n")
        if body.get("k") not in ("Page", "Game") or not isinstance(name, str) or not ERROR_NAME.match(name):
            return None
        message = clean_message(body.get("m", ""))
        event.update(name=body["k"], msg=name + (": " + message if message else ""), src=clean_source(body.get("s", ""), origin))
    return event


# ---- the days: Pacific time, as the admin page's are ------------------------------------------------------------------------------------------

def nth_sunday(year, month, n):
    first = datetime.date(year, month, 1)
    return first + datetime.timedelta(days=(6 - first.weekday()) % 7 + 7 * (n - 1))


def pacific_day(epoch):
    """The calendar day (`2026-11-26`) at the Pacific coast of the United States at a moment in seconds since 1970: UTC-8, and UTC-7 from the second Sunday of March at 02:00 to the first Sunday of
    November at 02:00 (the days are not asked of a time zone database: a hub in a small image has none)."""
    moment = datetime.datetime.fromtimestamp(epoch, datetime.timezone.utc)
    start = datetime.datetime.combine(nth_sunday(moment.year, 3, 2), datetime.time(10), datetime.timezone.utc)
    end = datetime.datetime.combine(nth_sunday(moment.year, 11, 1), datetime.time(9), datetime.timezone.utc)
    return (moment + datetime.timedelta(hours=-7 if start <= moment < end else -8)).date().isoformat()


def day_number(day):
    return datetime.date.fromisoformat(day).toordinal()


def add_days(day, n):
    return (datetime.date.fromisoformat(day) + datetime.timedelta(days=n)).isoformat()


def valid_day(text):
    """The text as a day when it is `YYYY-MM-DD` of a real date, else None."""
    if not isinstance(text, str) or not re.fullmatch(r"[0-9]{4}-[0-9]{2}-[0-9]{2}", text):
        return None
    try:
        datetime.date.fromisoformat(text)
    except ValueError:
        return None
    return text
