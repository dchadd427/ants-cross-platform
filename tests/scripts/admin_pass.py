"""Passes of the admin page's door, for tests that stand in for the chat hub's gate (docs/HUB_ROLES.md, "The admin page's door"; docs/MAP_SUBMISSIONS.md, "The admin page").

The hub's gate answers 204 with `X-Ants-Admin-Pass: v1.<base64url payload>.<hex mac>`; the game server checks it. This file makes the same text from the shared key, with the standard
library only, so that a test can play the gate: a stand-in hub for the site's nginx, or a request made by hand to the game server's door. The keys are made as the hub makes them: the secret
(ASCII, cut of surrounding whitespace) is the KEY of an HMAC-SHA-256 whose MESSAGE is the label, and the result is the RAW 32 bytes (never its hex text).
"""
import base64
import hashlib
import hmac
import json
import os
import time

TEST_SECRET = "testing-gate-key-0b8e2d71a4c6f395-z"         # a made-up key for the tests that play the gate
SAMPLE_SECRET = "canary-gate-key-5d8f0c1e7a9b4263-q"        # the made-up key of the known answers of docs/HUB.md: published, so both services refuse it as a key (a test that sets it checks that they stay closed)
PASS_HEADER = "X-Ants-Admin-Pass"
TEST_REF = "0123456789abcdef"                              # an account's reference: 16 hex characters (a made-up one)


def secret_bytes(secret):
    return secret.strip().encode("ascii")


def derived_key(secret, label):
    return hmac.new(secret_bytes(secret), label.encode("ascii"), hashlib.sha256).digest()


def pass_key(secret):
    return derived_key(secret, "ants-admin-pass-v1")


def b64(raw):
    return base64.urlsafe_b64encode(raw).rstrip(b"=").decode("ascii")


def make_pass(secret=TEST_SECRET, ref=TEST_REF, name="dave", role="admin", now=None, lifetime=60, nonce=None, fields=None, key=None):
    """`v1.<payload>.<mac>`. `fields` replaces the whole payload (a test of a pass with a missing or an extra member), `key` replaces the pass key (a test of a pass signed by another secret)."""
    when = int(time.time() if now is None else now)
    payload = fields if fields is not None else {"ref": ref, "name": name, "role": role, "exp": when + lifetime, "nonce": nonce if nonce is not None else os.urandom(8).hex()}
    body = "v1." + b64(json.dumps(payload, separators=(",", ":"), sort_keys=True).encode("utf-8"))
    mac = hmac.new(key if key is not None else pass_key(secret), body.encode("ascii"), hashlib.sha256).hexdigest()
    return body + "." + mac
