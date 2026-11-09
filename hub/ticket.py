"""The pass that lets the game server check a logged-in person's name (docs/HUB.md, "Settings"), and the rules for the name that it carries. No I/O: the clock and the nonce are given.

A pass is "n1." and the base64url (no padding) of 36 bytes, 51 characters in all:
    bytes 0-7    the account id, 64 bits, little endian
    bytes 8-11   the end of the pass, 32 bits, little endian, Unix seconds
    bytes 12-19  a nonce: 8 bytes that belong to this pass alone
    bytes 20-35  the first 16 bytes of HMAC-SHA256(secret, b"ants-name-ticket-1\\x00" + bytes 0-19 + the name)
The name is the game name as the Hello carries it (printable ASCII, 2 to 32 characters); it is under the MAC and not in the pass. Nothing here writes a log line, and no message names a
secret, a pass, a name or an account id.
"""
import base64
import hashlib
import hmac
import math
import os
import re
import struct
import unicodedata

from . import config
from .limits import Limits

PREFIX = "n1."
DOMAIN = b"ants-name-ticket-1\x00"
ACCOUNT_MAX = 2 ** 63 - 1
CUT = 256                                       # a name is cut to this many code points before any work (as the page does)
NAME_MIN, NAME_MAX = 2, 32
BOT_LIKE = re.compile(r"^bot\(|\(bot(,[^()]+)?\)\Z")        # the game's rule: a computer player is always shown as one
GUEST_LIKE = re.compile(r"guest[0-9]*\Z")       # no login may look like a guest's own name (blanks and case ignored)
MAC_BYTES = 16


def fit_game_name(text):
    """The text made to fit the game's name rules, or '' when it cannot (the same rules as the page's fit in web/front/player_name.js, tests/data/game_name_fit.txt holds the cases of both):
    NFKD, a letter or a digit that is not ASCII and would be lost refuses the text, what is not 0x20-0x7E goes, runs of blanks become one, both ends are trimmed, 2 to 32 characters
    remain, and neither a computer player's form ("Bot (...)", "Pip (bot)") nor "Guest" with or without digits is let through."""
    if not isinstance(text, str):
        return ""
    s = unicodedata.normalize("NFKD", text[:CUT])
    for c in s:
        if not 0x20 <= ord(c) <= 0x7e and unicodedata.category(c)[0] in "LN":
            return ""
    s = re.sub(" +", " ", "".join(c for c in s if 0x20 <= ord(c) <= 0x7e)).strip(" ")
    if not NAME_MIN <= len(s) <= NAME_MAX:
        return ""
    compact = s.replace(" ", "").lower()
    if BOT_LIKE.search(compact) or GUEST_LIKE.match(compact):
        return ""
    return s


def game_name_of(identity):
    """The game name of a login: the shown name if it fits, else the other name (the order of the page's fromMe), else ''."""
    return fit_game_name(getattr(identity, "name", None)) or fit_game_name(getattr(identity, "other", None))


def ticket_end(now):
    """The end of a pass made at `now` (Unix seconds)."""
    return int(now) + int(Limits.ticket_seconds)


def make_ticket(secret, account_id, name, now, nonce=None):
    """A pass for the account to sit under `name` until ticket_seconds after `now`. ValueError (a plain message) for a secret that is not 32 to 256 printable characters, an account id that
    is not digits or is above 2**63-1, a name that fit_game_name would change, a clock that gives an end outside 32 bits, a nonce that is not 8 bytes."""
    if not isinstance(secret, str) or not config.NAME_TICKET_SECRET.match(secret):
        raise ValueError("the secret is not 32 to 256 printable characters")
    if not isinstance(account_id, str) or not config.ACCOUNT_ID.match(account_id) or int(account_id) > ACCOUNT_MAX:
        raise ValueError("the account id is not one")
    if not isinstance(name, str) or not name or fit_game_name(name) != name:
        raise ValueError("the name does not fit")
    if isinstance(now, bool) or not isinstance(now, (int, float)) or not math.isfinite(now) or not 0 <= ticket_end(now) < 2 ** 32:
        raise ValueError("the end of the pass is not a 32-bit time")
    if nonce is None:
        nonce = os.urandom(8)
    if not isinstance(nonce, (bytes, bytearray)) or len(nonce) != 8:
        raise ValueError("the nonce is not 8 bytes")
    head = struct.pack("<QI", int(account_id), ticket_end(now)) + bytes(nonce)
    mac = hmac.new(secret.encode("ascii"), DOMAIN + head + name.encode("ascii"), hashlib.sha256).digest()[:MAC_BYTES]
    return PREFIX + base64.urlsafe_b64encode(head + mac).decode("ascii").rstrip("=")
