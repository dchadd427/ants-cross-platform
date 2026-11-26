"""The pass of a name (docs/HUB_NICKNAMES.md, "The pass"): what lets a person go by a nickname on the game server. The hub signs the name that a logged-in person plays under for two minutes; the page tells the pass in
its Hello and in each rename, and the game server (ants_server/name_pass.hpp, which checks it) takes the name as it is only when the pass is good for exactly that name; every other name there is a guest's.

The key is the one setting of the hub and the game server, ANTS_ADMIN_GATE_SECRET (hub/config.py `admin_gate_on` says whether it is a good one), with a label of its own so that a pass of another kind is never good for a
name: key = HMAC-SHA-256 with the secret as the KEY and `ants-name-pass-v1` as the MESSAGE (the raw 32 bytes). A pass is `<exp>.<mac>`: exp is seconds since 1970 in digits (now + 120), mac the first 32 hex digits of
HMAC-SHA-256 under that key of `name1`, a NUL, the digits of exp, a NUL and the UTF-8 bytes of the name. `/hub/me` hands it out (hub/login.py); it holds no account, no id and no reference: it is the name, and it ends.
"""
import hashlib
import hmac

LABEL = b"ants-name-pass-v1"
SECONDS = 120                                  # how long a pass lasts (the game server refuses one that claims more than this and a little slack)
MAC_CHARS = 32


class Maker:
    def __init__(self, secret):
        """`secret`: the text of the shared setting (a str, printable ASCII, no blank); the caller has checked that it is a good key."""
        self.key = hmac.new(secret.encode("ascii"), LABEL, hashlib.sha256).digest()

    def mac(self, exp_text, name):
        message = b"name1\0" + exp_text.encode("ascii") + b"\0" + name.encode("utf-8")
        return hmac.new(self.key, message, hashlib.sha256).hexdigest()[:MAC_CHARS]

    def make(self, name, now):
        """The pass of `name` that lasts SECONDS from `now` (seconds since 1970)."""
        exp = str(int(now) + SECONDS)
        return exp + "." + self.mac(exp, name)

    def check(self, name, text, now):
        """Whether `text` is a good pass for exactly `name` at `now` (the game server's check, written again: the tests hold both to the same words)."""
        if not isinstance(text, str) or not isinstance(name, str) or not name or text.count(".") != 1:
            return False
        exp, _, mac = text.partition(".")
        if not exp.isascii() or not exp.isdigit() or not 1 <= len(exp) <= 12 or len(mac) != MAC_CHARS:
            return False
        if not hmac.compare_digest(mac, self.mac(exp, name)):
            return False
        return now < int(exp) <= now + SECONDS + 30
