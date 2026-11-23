"""The keys of a person's AI (docs/MAP_AI.md, "Keys"): how a key looks, how one is made and how one is judged on every call. No I/O of its own: the store keeps the rows (hub/store/base.py, `ai_key_*`) and
the clock is given.

A key is `antsai_<id>_<secret>`: `id` is 8 hex digits (public: it is in the person's list and in the logs), `secret` is 43 characters of base64url (256 random bits). The prefix lets the scanners that
look for leaked keys find one. Only the SHA-256 of the secret is kept (a secret of 256 random bits needs no slow hash), compared with `hmac.compare_digest`; the whole key is shown once, in the one answer
that makes it, and is never in a URL, a log line, an error, a page or a table. A key has a scope (`read`: look and check; `edit`: also change, save a draft), an end (7, 30 or 90 days from its making: there
is no key that never ends), a name that is the person's own text, and belongs to the account's reference (hub/pseudo.py), never to its id.

`judge` is the one rule of every call: the header is `Bearer <key>` of exactly that shape, the row exists, the secret's digest is the row's, the key is not revoked and has not ended. Whatever is wrong the
answer is the same word (`bad_key`), so a key cannot be probed for which part was wrong, and the digest is compared even for an id that does not exist. A ban and Forget me delete the rows (the store does it
in the same step), so a key of such an account is simply not found. The limits of a call (a key's burst, the account's hour, the address's bad keys) are `Budgets`.
"""
import hashlib
import hmac
import re
import secrets

from . import limits as limitmod, text as textrules

PREFIX = "antsai_"
KEY = re.compile(r"antsai_([0-9a-f]{8})_([A-Za-z0-9_-]{43})\Z")
DAYS = (7, 30, 90)
DEFAULT_DAYS = 30
NO_SUCH_DIGEST = hashlib.sha256(b"no such key").hexdigest()


def digest(secret):
    return hashlib.sha256(secret.encode("ascii")).hexdigest()


def new_key():
    """(key_id, the whole key, the digest to keep): 32 random bits of public id and 256 random bits of secret."""
    key_id, secret = secrets.token_hex(4), secrets.token_urlsafe(32)
    return key_id, "%s%s_%s" % (PREFIX, key_id, secret), digest(secret)


def bearer(header):
    """(key id, secret) of an `Authorization: Bearer <key>` header, or None for anything else (another scheme, a second word, a key of another shape)."""
    if not isinstance(header, str):
        return None
    scheme, _, value = header.partition(" ")
    found = KEY.match(value) if scheme.lower() == "bearer" and value == value.strip() else None
    return (found.group(1), found.group(2)) if found else None


def judge(store, header, now):
    """The AiKey row that the header names, when every part of it is good; else None. Nothing about why."""
    given = bearer(header)
    row = store.ai_key(given[0]) if given else None
    same = good(row.digest if row is not None else NO_SUCH_DIGEST, given[1] if given else "")      # (the digest is compared even when there is no such key: the same work for every failure)
    if row is None or not same or row.revoked or row.ends <= now:
        return None
    return row


def good(kept, secret):
    return hmac.compare_digest(kept.encode("ascii"), digest(secret).encode("ascii"))


class Budgets:
    """What the calls may cost: a burst and a steady rate for each key, an hour's budget for each account (all its keys together), and a small allowance of keys that are no good for each client address
    (over it the address is refused before a key is even looked up)."""

    def __init__(self, limits, clock):
        self.clock = clock
        self.keys = limitmod.RateByAccount(limits.ai_key_burst, limits.ai_key_refill_seconds)
        self.hour = limitmod.RateByAccount(limits.ai_account_hour, 3600.0 / limits.ai_account_hour)
        self.bad = limitmod.RateByAddress(limits.ai_bad_burst, limits.ai_bad_refill_seconds)

    def address_spent(self, address):
        """Whether this address has used up its allowance of bad keys (a bucket that is empty now)."""
        bucket = self.bad.buckets.get(address)
        return bucket is not None and not bucket.ready(self.clock())

    def bad_key(self, address):
        self.bad.take(address, self.clock())

    def take(self, key_id, reference):
        """True when the call may go on: both the key's bucket and the account's hour have a token (and both are spent); False, and nothing spent, otherwise."""
        now = self.clock()
        for table, name, burst, refill in ((self.keys, key_id, self.keys.burst, self.keys.refill), (self.hour, reference, self.hour.burst, self.hour.refill)):
            if name not in table.buckets:
                if len(table.buckets) >= table.keys_max:
                    table.forget_idle(now)
                    if len(table.buckets) >= table.keys_max:
                        return False
                table.buckets[name] = limitmod.TokenBucket(burst, refill, now)
        key, hour = self.keys.buckets[key_id], self.hour.buckets[reference]
        if not (key.ready(now) and hour.ready(now)):
            return False
        key.take(now)
        hour.take(now)
        return True

    def forget_idle(self, now):
        self.keys.forget_idle(now)
        self.hour.forget_idle(now)
        self.bad.forget_idle(now)


def clean_name(text, limit):
    """(name, None) for a key's name that may be kept: the person's own text, cleaned by the hub's text rules (hub/text.py), 1 to `limit` code points; else (None, why)."""
    cleaned, why = textrules.clean_line(text)
    if cleaned is None or len(cleaned) > limit:
        return None, why or "long"
    return cleaned, None
