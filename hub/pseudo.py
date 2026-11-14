"""The pseudonymous reference of an account (docs/HUB.md, "The moderators' log"): what the hub writes down about a person where it must not write the Discord id, which is like a password.

A reference is the first 16 hex characters of HMAC-SHA256 of the account id, keyed with a secret of the hub's own (32 random bytes, made at the first start and kept in hub.db: `store.secret()`).
It is the same for an account every time (so a log can say that two actions were done to one person) and it says nothing of the id to anybody who does not hold the secret. The function is
the one place that makes one, so that every table that will hold a reference instead of an id (the moderators' log now) uses the same. Nothing here is ever logged or sent to a page.
"""
import hashlib
import hmac
import re

LENGTH = 16
REFERENCE = re.compile(r"[0-9a-f]{%d}\Z" % LENGTH)


def reference(secret, account):
    """The reference of `account` (a Discord id as text) under `secret` (bytes): 16 lower-case hex characters."""
    return hmac.new(secret, account.encode("utf-8"), hashlib.sha256).hexdigest()[:LENGTH]
