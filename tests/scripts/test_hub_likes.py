#!/usr/bin/env python3
"""The likes' gate, Forget me's call to the counts door and the login's way back to a match (hub/likedoor.py, hub/login.py, hub/config.py `ANTS_HUB_COUNTS_URL`; docs/HUB_LOGIN.md, docs/REPLAY_COUNTS.md). Real sockets and the
real hub (tests/scripts/hub_rig.py), a fake Discord for the logins (tests/scripts/fake_discord.py) and a stand-in for the game server's counts door that checks every call the way the game server does. The pass and
its keys are written here from the contract's words (HMAC-SHA-256 from the standard library), never taken from the hub's code, and the known answers are numbers that the game server's code must reproduce.

  TheGate           what the gate is and is not: 204 and a pass for any logged-in account (a banned, a muted and a young one too), 401 for nobody, GET only, no body, no query, no other path, no CORS, no
                    address (400, said once in the log), a limit for each address that is asked before the login
  TheDefaultLimit   the limit that a hub has when nothing changes it: a burst of 60 for each address, then one a second
  TheShutDoor       no key, a short key, a key of one repeated character, a key that is not printable, the published sample key: 503 for everybody and never a 401; the old name of the setting
  ThePass           `v1.<payload>.<mac>`: the mac holds with the like key and with nothing else (not the admin page's key, and not the other way round), the payload is exactly `exp`, `nonce`, `ref` in the
                    canonical text, a minute of life by the hub's clock, a nonce of its own for each pass, the reference of the account and never its id, name or cookie
  TheKnownAnswers   the keys, a pass and a token for a made-up secret
  TheRealLogin      the gate through the Discord login's cookie, and no pass after a logout
  TheForget         Forget me: the call to the stand-in door has exactly the right request (the pass of the reference, the token of the reference, no body, no cookie, nothing of the person) and an answer of
                    200 with a whole `removed` deletes as before; a door that answers anything else, nothing, too much, garbage, a redirect, or that is not there, is a 503 `busy` that deletes nothing and
                    can be tried again; no setting, no call; a setting with no good key is a 503 that deletes nothing and makes no call; a request that is refused before anything is deleted makes no call;
                    the hub keeps answering while the door is silent; nothing of the person or of the door is in the log
  TheWayBack        the login's `next` words `matches` and `match` (with an id of the shape of a match id and nothing else), the page that a failed login shows, the state that is used once
  TheSetting        ANTS_HUB_COUNTS_URL is `http://host:port` and nothing else
  TheStart          the real process: the gate with a key, the start line when the setting has no good key, a setting that is not an address stops the start without saying it

Every id here is made up.
"""
import base64
import hashlib
import hmac
import json
import os
import re
import socketserver
import sqlite3
import subprocess
import sys
import threading
import time
import unittest
import urllib.parse
from unittest import mock

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
import fake_discord as fd  # noqa: E402
import hub_client as hc  # noqa: E402
import hub_rig  # noqa: E402
from login_flow import Visitor  # noqa: E402
from test_hub_server import read_text, start_process, wait_for_port  # noqa: E402
from hub import config as configmod, likedoor  # noqa: E402
from hub.store import StoreError  # noqa: E402

SECRET = "testing-gate-key-0b8e2d71a4c6f395-z"           # a made-up key
SAMPLE_SECRET = "canary-gate-key-5d8f0c1e7a9b4263-q"     # the made-up key of the known answers of docs/HUB_ROLES.md: published, so never a key
OWNER = "9300000000000001"
PLAIN = "9300000000000002"
YOUNG = "9300000000000003"
BANNED = "9300000000000004"
MUTED = "9300000000000005"
NAMES = {OWNER: "olive.owner", PLAIN: "pat.plain", YOUNG: "yara.young", BANNED: "bo.banned", MUTED: "mo.muted"}
EVERY_ID = tuple(NAMES)
PASS_FORM = re.compile(r"v1\.([A-Za-z0-9_-]+)\.([0-9a-f]{64})\Z")
LIKE_PASS = b"ants-like-pass-v1"
LIKE_CSRF = b"ants-like-csrf-v1"
ADMIN_PASS = b"ants-admin-pass-v1"
CALLBACK = hc.SITE + "/hub/callback"
OLD = fd.snowflake(900)                                  # the id of the Discord account that the fake logs in
CLEARED_SESSION = "__Host-ants_hub=; Path=/; Max-Age=0; Secure; HttpOnly; SameSite=Strict"

# The known answers: computed from the contract's words for the made-up secret of docs/HUB_ROLES.md (the label is the MESSAGE and the secret the KEY; the keys are the raw 32 bytes of the digests).
KAT_PASS_KEY = "8c13d59045b7f259e8ea9cfa18a50178873af160e6ffa04821d9cf4601d55ae9"
KAT_CSRF_KEY = "4f749ae0b8f8bb03e531bf146ab21d7efd40d07bd58f54de599cbbfc115cdf11"
KAT_REF = "0123456789abcdef"
KAT_PASS = ("v1.eyJleHAiOjE4MDAwMDAwNjAsIm5vbmNlIjoiMDAxMTIyMzM0NDU1NjZmZiIsInJlZiI6IjAxMjM0NTY3ODlhYmNkZWYifQ."
            "0e4879c0fd6e4c2daeed36090356d812a6e47f895974279031344a764fff89c3")                       # exp 1800000060, nonce 00112233445566ff, ref KAT_REF
KAT_TOKEN = "4a9f9b31add3b8129d21a9147e4247d9"


def b64decode(text):
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def key_of(secret, label):
    return hmac.new(secret.encode("ascii"), label, hashlib.sha256).digest()


def token_of(secret, ref):
    """The token of a reference, from the contract's words."""
    return hmac.new(key_of(secret, LIKE_CSRF), b"csrf1\0" + ref.encode("ascii"), hashlib.sha256).hexdigest()[:32]


def verify(header, secret, now, label=LIKE_PASS):
    """What the game server does with a pass (written here from the contract alone): the payload, or None for a pass whose mac is wrong or that has run out."""
    m = PASS_FORM.match(header or "")
    if not m:
        return None
    good = hmac.new(key_of(secret, label), ("v1." + m.group(1)).encode("ascii"), hashlib.sha256).hexdigest()
    if not hmac.compare_digest(good, m.group(2)):
        return None
    payload = json.loads(b64decode(m.group(1)))
    return payload if payload["exp"] > now else None


def login_header(account):
    return ("X-Hub-Test-Login", "%s:%s" % (account, NAMES[account]) + (":young" if account == YOUNG else ""))


# ---- the gate -------------------------------------------------------------------------------------------------------------------------------------------

class GateTest(unittest.TestCase):
    secret = SECRET
    options = {}
    owner = ""

    def setUp(self):
        env = {"ANTS_ADMIN_GATE_SECRET": self.secret} if self.secret is not None else {}
        self.rig = hub_rig.Rig(env=env, owner=self.owner, **dict(dict(tick_seconds=0.05, sockets_per_address=50), **self.options))
        self.rig.__enter__()
        self.addCleanup(self.rig.__exit__, None, None, None)
        self.count = 0

    def call(self, method, path, account=None, address=None, extra=(), raw=b""):
        """One request as the site's nginx makes it: the visitor's login and `X-Client-Address` (a new address each time, none for address="")."""
        self.count += 1
        pairs = [] if address == "" else [("X-Client-Address", address or "198.51.100.%d" % (self.count % 200 + 1))]
        if account:
            pairs.append(login_header(account))
        return hc.request(self.rig.port, method, path, pairs + list(extra), body=raw, timeout=30.0)

    def gate(self, account=None, **kw):
        return self.call("GET", "/hub/likes/gate", account, **kw)

    def ref(self, account):
        return self.rig.call(self.rig.hub.chat.ref, account)

    def log(self):
        return self.rig.log_text.getvalue()


class TheGate(GateTest):
    options = dict(like_burst=6, like_refill_seconds=60.0)

    def test_a_good_gate_is_a_204_with_a_pass_and_no_body_no_length_no_type_and_no_cors(self):
        r = self.gate(PLAIN, address="203.0.113.1")
        self.assertEqual((r.status, r.body), (204, b""))
        self.assertRegex(r.get("x-ants-like-pass"), PASS_FORM)
        self.assertIsNone(r.get("content-length"))
        self.assertIsNone(r.get("content-type"))
        self.assertEqual(r.get("cache-control"), "no-store")
        for account in (None, PLAIN):
            r = self.gate(account, extra=[("Origin", "https://other.example")], address="203.0.113.2")
            self.assertFalse([n for n, _ in r.headers if n.startswith("access-control")], r.headers)

    def test_every_logged_in_account_gets_a_pass_whatever_the_chat_thinks_of_it(self):
        self.rig.call(self.rig.store.ban, BANNED, "command line")
        self.rig.call(self.rig.store.mute, MUTED, 60, "command line")
        for account in (PLAIN, YOUNG, BANNED, MUTED, OWNER):
            with self.subTest(NAMES[account]):
                r = self.gate(account)
                self.assertEqual(r.status, 204)
                self.assertEqual(verify(r.get("x-ants-like-pass"), SECRET, time.time())["ref"], self.ref(account))

    def test_nobody_is_a_401_with_no_pass_and_no_body(self):
        for extra in ([], [("X-Hub-Test-Login", "")], [("X-Hub-Test-Login", "not-a-login")], [("X-Hub-Test-Login", "0123:bad name")], [("Cookie", "__Host-ants_hub=" + "0" * 32)]):
            r = self.gate(extra=extra)
            self.assertEqual((r.status, r.body), (401, b""), extra)
            self.assertIsNone(r.get("x-ants-like-pass"))

    def test_only_a_get_with_no_body_and_no_query_is_answered(self):
        for method in ("POST", "PUT", "DELETE", "OPTIONS", "PATCH", "HEAD"):
            r = self.call(method, "/hub/likes/gate", PLAIN)
            self.assertEqual((r.status, r.get("allow")), (405, "GET"), method)
            self.assertIsNone(r.get("x-ants-like-pass"))
            self.assertFalse([n for n, _ in r.headers if n.startswith("access-control")])
        r = self.call("GET", "/hub/likes/gate", PLAIN, raw=b"{}")
        self.assertEqual(r.status, 400, "a body is refused")
        self.assertIsNone(r.get("x-ants-like-pass"))
        r = self.call("GET", "/hub/likes/gate", PLAIN, extra=[("Transfer-Encoding", "chunked")])
        self.assertEqual(r.status, 400)
        for path in ("/hub/likes/gate?x=1", "/hub/likes/gate?"):
            r = self.call("GET", path, PLAIN)
            self.assertEqual((r.status, r.get("x-ants-like-pass")), (404, None), path)

    def test_other_paths_under_the_door_are_not_found(self):
        for path in ("/hub/likes/", "/hub/likes/gate/", "/hub/likes/gate2", "/hub/likes/GATE", "/hub/likes/gate/x", "/hub/likes/mine", "/hub/likes/forget", "/hub/likes//gate", "/hub/likes/../likes/gate"):
            r = self.call("GET", path, PLAIN)
            self.assertEqual((r.status, r.get("x-ants-like-pass")), (404, None), path)

    def test_a_request_with_no_usable_address_is_a_400_said_once_and_before_anything_else(self):
        for method in ("GET", "POST"):
            r = self.call(method, "/hub/likes/gate", PLAIN, address="")
            self.assertEqual((r.status, r.json()), (400, {"error": "address"}), method)
        for address in ("not-an-address", "999.1.1.1", "1.2.3.4, 5.6.7.8", " "):
            r = self.gate(PLAIN, address=address)
            self.assertEqual((r.status, r.json()), (400, {"error": "address"}), address)
            self.assertIsNone(r.get("x-ants-like-pass"))
        r = self.gate(None, address="")
        self.assertEqual(r.status, 400, "nobody who is not identified is a 401 before the address is known")
        self.assertEqual(self.log().count("likes_gate_asked_without_x_client_address"), 1, self.log())

    def test_each_address_has_a_limit_that_is_asked_before_the_login_and_another_address_is_not_held_by_it(self):
        asked = []
        original = self.rig.hub.identify

        async def counting(headers):
            asked.append(1)
            return await original(headers)

        self.rig.call(setattr, self.rig.hub, "identify", counting)
        statuses = [self.gate(PLAIN, address="203.0.113.50").status for _ in range(9)]
        self.assertEqual(statuses[:6], [204] * 6)
        self.assertEqual(set(statuses[6:]), {429})
        self.assertEqual(len(asked), 6, "the login is not asked for a request beyond the limit")
        self.assertEqual(self.gate(PLAIN, address="203.0.113.51").status, 204)
        r = self.gate(PLAIN, address="203.0.113.50")
        self.assertEqual((r.status, r.json(), r.get("retry-after")), (429, {"error": "rate"}, "5"))
        self.assertIsNone(r.get("x-ants-like-pass"))

    def test_a_database_that_cannot_make_the_reference_is_a_503_and_never_a_pass(self):
        with mock.patch.object(self.rig.hub.chat, "ref", side_effect=StoreError("down")):
            r = self.gate(PLAIN)
        self.assertEqual((r.status, r.json()), (503, {"error": "busy"}))
        self.assertIsNone(r.get("x-ants-like-pass"))
        self.assertEqual(self.gate(PLAIN).status, 204)

    def test_the_answer_of_a_refusal_is_a_small_json_object_and_never_says_anything_of_the_login(self):
        for r in (self.call("POST", "/hub/likes/gate", PLAIN), self.call("GET", "/hub/likes/x", PLAIN), self.gate(PLAIN, address="")):
            self.assertEqual(r.get("cache-control"), "no-store")
            self.assertEqual(list(r.json()), ["error"])
            for text in EVERY_ID + tuple(NAMES.values()):
                self.assertNotIn(text.encode(), r.body)


class TheDefaultLimit(GateTest):
    def test_an_address_has_a_burst_of_sixty_and_then_one_a_second(self):
        statuses = [self.gate(PLAIN, address="203.0.113.80").status for _ in range(80)]
        self.assertEqual(statuses[:60], [204] * 60)
        self.assertLessEqual(statuses.count(204), 66, "one more a second, and the run takes a few seconds at the most")
        self.assertEqual(statuses[-1], 429)


class TheShutDoor(unittest.TestCase):
    """The door is shut for want of a good key: a 503 for everybody, nobody logged in too (nginx says `likes_off`; the page never says that the visitor is not logged in)."""

    def check(self, env):
        with hub_rig.Rig(env=env, tick_seconds=0.05) as rig:
            for account in (None, PLAIN, OWNER):
                r = hc.request(rig.port, "GET", "/hub/likes/gate", [("X-Client-Address", "198.51.100.7")] + ([login_header(account)] if account else []))
                self.assertEqual((r.status, r.body), (503, b""), (env, account))
                self.assertIsNone(r.get("x-ants-like-pass"))
            self.assertEqual(hc.request(rig.port, "GET", "/hub/healthz").status, 200)

    def test_no_key_a_short_key_a_key_of_one_character_a_key_that_is_not_printable_and_the_sample_key_shut_it(self):
        for key in (None, "tiny-key", "a" * 40, "\x01" * 40, SAMPLE_SECRET, "has a blank in it, and is long enough to be a key"):
            with self.subTest(key=key and len(key)):
                self.check({} if key is None else {"ANTS_ADMIN_GATE_SECRET": key})

    def test_the_old_name_of_the_setting_is_read_when_the_new_one_is_not_set_and_the_new_one_wins(self):
        with hub_rig.Rig(env={"ANTS_HUB_ADMIN_GATE_SECRET": SECRET}, tick_seconds=0.05) as rig:
            r = hc.request(rig.port, "GET", "/hub/likes/gate", [("X-Client-Address", "198.51.100.7"), login_header(PLAIN)])
            self.assertEqual(r.status, 204)
            self.assertIsNotNone(verify(r.get("x-ants-like-pass"), SECRET, time.time()))
        with hub_rig.Rig(env={"ANTS_HUB_ADMIN_GATE_SECRET": SECRET, "ANTS_ADMIN_GATE_SECRET": SECRET + "-new"}, tick_seconds=0.05) as rig:
            r = hc.request(rig.port, "GET", "/hub/likes/gate", [("X-Client-Address", "198.51.100.7"), login_header(PLAIN)])
            self.assertIsNotNone(verify(r.get("x-ants-like-pass"), SECRET + "-new", time.time()))
            self.assertIsNone(verify(r.get("x-ants-like-pass"), SECRET, time.time()))

    def test_a_secret_with_blanks_around_it_makes_the_keys_of_the_secret_without_them(self):
        with hub_rig.Rig(env={"ANTS_ADMIN_GATE_SECRET": "  " + SECRET + "\n"}, tick_seconds=0.05) as rig:
            r = hc.request(rig.port, "GET", "/hub/likes/gate", [("X-Client-Address", "198.51.100.7"), login_header(PLAIN)])
            self.assertIsNotNone(verify(r.get("x-ants-like-pass"), SECRET, time.time()))


class ThePass(GateTest):
    owner = OWNER

    def test_the_pass_holds_with_the_like_key_and_with_nothing_else(self):
        header = self.gate(PLAIN).get("x-ants-like-pass")
        self.assertRegex(header, PASS_FORM)
        now = time.time()
        self.assertIsNotNone(verify(header, SECRET, now))
        self.assertIsNone(verify(header, SECRET + "x", now))
        self.assertIsNone(verify(header, "another-key-of-the-same-sort-0123456789", now))
        version, body, mac = header.split(".")
        flipped = body[:-2] + ("AA" if body[-2:] != "AA" else "BB")
        self.assertIsNone(verify("v1.%s.%s" % (flipped, mac), SECRET, now))
        self.assertIsNone(verify("v2.%s.%s" % (body, mac), SECRET, now))
        self.assertIsNone(verify("v1.%s.%s" % (body, "0" * 64), SECRET, now))

    def test_the_key_is_not_the_admin_pages_key_and_the_admin_pages_key_is_not_this_one(self):
        now = time.time()
        like = self.gate(PLAIN).get("x-ants-like-pass")
        self.assertIsNone(verify(like, SECRET, now, label=ADMIN_PASS), "a like pass must fail the admin page's check")
        admin = self.call("GET", "/hub/admin/gate", OWNER).get("x-ants-admin-pass")
        self.assertIsNotNone(verify(admin, SECRET, now, label=ADMIN_PASS))
        self.assertIsNone(verify(admin, SECRET, now), "an admin pass must fail the like check")

    def test_the_payload_is_exp_nonce_and_ref_in_the_canonical_text_and_nothing_else(self):
        header = self.gate(PLAIN).get("x-ants-like-pass")
        body = header.split(".")[1]
        self.assertNotIn("=", body, "base64url with no padding")
        raw = b64decode(body)
        payload = json.loads(raw)
        self.assertEqual(sorted(payload), ["exp", "nonce", "ref"])
        self.assertEqual(raw, json.dumps(payload, separators=(",", ":"), sort_keys=True).encode("utf-8"), "keys sorted, no spaces")
        self.assertEqual(payload["ref"], self.ref(PLAIN))
        self.assertRegex(payload["ref"], r"[0-9a-f]{16}\Z")
        self.assertRegex(payload["nonce"], r"[0-9a-f]{16}\Z")
        self.assertIs(type(payload["exp"]), int)

    def test_the_reference_is_the_accounts_and_its_own_and_never_its_id_name_or_cookie(self):
        one, other = self.gate(PLAIN), self.gate(YOUNG)
        refs = [verify(r.get("x-ants-like-pass"), SECRET, time.time())["ref"] for r in (one, other, self.gate(PLAIN))]
        self.assertEqual(refs[0], refs[2], "one account, one reference")
        self.assertNotEqual(refs[0], refs[1])
        for r in (one, other):
            text = repr(r.headers) + b64decode(r.get("x-ants-like-pass").split(".")[1]).decode()
            for forbidden in EVERY_ID + tuple(NAMES.values()):
                self.assertNotIn(forbidden, text)
        for forbidden in EVERY_ID + tuple(NAMES.values()):
            self.assertNotIn(forbidden, self.log())

    def test_the_expiry_is_a_minute_from_the_hub_clock_and_a_verifier_honors_it(self):
        self.rig.call(setattr, self.rig.store, "clock", lambda: 5_000_000.0)
        header = self.gate(PLAIN).get("x-ants-like-pass")
        self.assertEqual(verify(header, SECRET, 5_000_000.0)["exp"], 5_000_060)
        self.assertIsNotNone(verify(header, SECRET, 5_000_059.0))
        self.assertIsNone(verify(header, SECRET, 5_000_060.0), "a pass of a minute ago is no pass")
        self.assertIsNone(verify(header, SECRET, 5_000_600.0))
        self.rig.call(setattr, self.rig.store, "clock", lambda: 5_000_100.0)
        self.assertEqual(verify(self.gate(PLAIN).get("x-ants-like-pass"), SECRET, 5_000_100.0)["exp"], 5_000_160)

    def test_every_pass_has_a_nonce_of_its_own_and_a_mac_of_its_own(self):
        passes = [self.gate(PLAIN).get("x-ants-like-pass") for _ in range(8)]
        self.assertEqual(len({verify(p, SECRET, time.time())["nonce"] for p in passes}), 8)
        self.assertEqual(len(set(passes)), 8)

    def test_no_refusal_carries_a_pass(self):
        for r in (self.gate(None), self.gate(PLAIN, address=""), self.call("POST", "/hub/likes/gate", PLAIN), self.call("GET", "/hub/likes/gate?x=1", PLAIN)):
            self.assertIsNone(r.get("x-ants-like-pass"))


class TheKnownAnswers(unittest.TestCase):
    """Numbers that the game server's code must reproduce. They come from the contract's words (the secret is the KEY, the label the MESSAGE), and the code under test must give the same."""

    def test_the_keys_are_the_raw_digests_with_the_labels_of_the_likes(self):
        pass_key, csrf_key = likedoor.keys(SAMPLE_SECRET)
        self.assertEqual((pass_key.hex(), csrf_key.hex()), (KAT_PASS_KEY, KAT_CSRF_KEY))
        self.assertEqual((pass_key, csrf_key), (key_of(SAMPLE_SECRET, LIKE_PASS), key_of(SAMPLE_SECRET, LIKE_CSRF)))
        self.assertNotIn(key_of(SAMPLE_SECRET, ADMIN_PASS), (pass_key, csrf_key))
        self.assertNotIn(key_of(SAMPLE_SECRET, b"ants-admin-csrf-v1"), (pass_key, csrf_key))
        self.assertNotIn(key_of(SAMPLE_SECRET, b"ants-admin-csrf-server-v1"), (pass_key, csrf_key))

    def test_a_pass_and_a_token_of_a_made_up_reference(self):
        pass_key, csrf_key = likedoor.keys(SAMPLE_SECRET)
        self.assertEqual(likedoor.make_pass(pass_key, KAT_REF, 1800000060, "00112233445566ff"), KAT_PASS)
        self.assertEqual(likedoor.token(csrf_key, KAT_REF), KAT_TOKEN)
        self.assertEqual(token_of(SAMPLE_SECRET, KAT_REF), KAT_TOKEN)
        self.assertIsNotNone(verify(KAT_PASS, SAMPLE_SECRET, 1800000059))
        self.assertIsNone(verify(KAT_PASS, SAMPLE_SECRET, 1800000060))


class TheRealLogin(unittest.TestCase):
    """The gate through the Discord login's cookie (the test login of the other classes is a header that only a test can send)."""

    def setUp(self):
        self.fake = fd.FakeDiscord(CALLBACK, user=(OLD, "maple.leaf"))
        self.fake.consent = False
        self.rig = hub_rig.Rig(discord=self.fake, env={"ANTS_ADMIN_GATE_SECRET": SECRET}, tick_seconds=0.05, sockets_per_address=50)
        self.rig.__enter__()
        self.addCleanup(self.finish)
        self.count = 0

    def finish(self):
        self.rig.__exit__(None, None, None)
        self.fake.close()

    def visitor(self):
        self.count += 1
        return Visitor(self.rig.port, self.fake, address="198.51.100.%d" % self.count)

    def test_a_cookie_that_the_hub_knows_gets_a_pass_for_its_account_and_nobody_else_does(self):
        v = self.visitor()
        self.assertEqual(v.call("GET", "/hub/likes/gate").status, 401)
        self.assertEqual(v.login().status, 302)
        r = v.call("GET", "/hub/likes/gate")
        self.assertEqual((r.status, r.body), (204, b""))
        payload = verify(r.get("x-ants-like-pass"), SECRET, time.time())
        self.assertEqual(payload["ref"], self.rig.call(self.rig.hub.chat.ref, OLD))
        self.assertNotIn(OLD, repr(r.headers) + self.rig.log_text.getvalue())
        stranger = self.visitor()
        self.assertEqual(stranger.call("GET", "/hub/likes/gate", headers=[("Cookie", "__Host-ants_hub=" + "ab" * 16)], cookies=False).status, 401)

    def test_no_pass_after_a_logout_or_when_the_login_ran_out(self):
        v = self.visitor()
        v.login()
        self.assertEqual(v.call("GET", "/hub/likes/gate").status, 204)
        cookie = v.jar.header()
        self.assertEqual(v.call("POST", "/hub/logout").status, 200)
        self.assertEqual(v.call("GET", "/hub/likes/gate", headers=[("Cookie", cookie)], cookies=False).status, 401, "a cookie of a login that ended")
        w = self.visitor()
        w.login()
        self.rig.call(setattr, self.rig.store, "clock", lambda: time.time() + 31 * 86400)
        self.assertEqual(w.call("GET", "/hub/likes/gate").status, 401, "a login that ran out")


# ---- Forget me ------------------------------------------------------------------------------------------------------------------------------------------

class Call:
    def __init__(self, request_line, names, headers, body, raw, problems):
        self.request_line, self.names, self.headers, self.body, self.raw, self.problems = request_line, names, headers, body, raw, problems


class FakeCountsDoor:
    """A stand-in for the game server's counts door (`POST /counts/forget`): a small HTTP server on a loopback port, in a thread of its own, that checks every call the way the game server does and keeps it.
    A call that is not what the game server accepts is named in `problems`. `mode` makes it answer: `ok` (200 and `{"removed": N}`), `status:<n>` (that status with a good-looking body), `body` (200 and the
    bytes of `body`), `close` (no answer), `short` (an answer that is cut off), `big` (an answer of more than the hub reads), `chunked_garbage`, `status_line` (not HTTP), `double_length`, `redirect`, `hang`."""

    ALLOWED = {"host", "user-agent", "accept", "accept-encoding", "connection", "x-ants-like-pass", "x-ants-csrf", "content-length"}

    def __init__(self, secret=SECRET):
        self.secret = secret
        self.calls = []
        self.nonces = set()
        self.mode = "ok"
        self.removed = 3
        self.body = b""
        self.hang_seconds = 6.0
        self.lock = threading.Lock()
        door = self

        class Handler(socketserver.StreamRequestHandler):
            def handle(self):
                door.handle(self)

        self.server = socketserver.ThreadingTCPServer(("127.0.0.1", 0), Handler)
        self.server.daemon_threads = True
        self.server.block_on_close = False
        self.port = self.server.server_address[1]
        self.url = "http://127.0.0.1:%d" % self.port
        self.thread = threading.Thread(target=self.server.serve_forever, kwargs={"poll_interval": 0.05}, daemon=True)
        self.thread.start()

    def stop(self):
        self.server.shutdown()
        self.server.server_close()

    @property
    def problems(self):
        with self.lock:
            return [p for call in self.calls for p in call.problems]

    def handle(self, handler):
        data = b""
        while b"\r\n\r\n" not in data and len(data) < 65536:
            chunk = handler.request.recv(4096)
            if not chunk:
                return
            data += chunk
        head, _, body = data.partition(b"\r\n\r\n")
        lines = head.decode("latin-1").split("\r\n")
        pairs = [(line.partition(":")[0].strip().lower(), line.partition(":")[2].strip()) for line in lines[1:]]
        call = Call(lines[0], [n for n, _ in pairs], dict(pairs), body, data, [])
        self.check(call)
        with self.lock:
            self.calls.append(call)
        self.respond(handler.request, call)

    def check(self, call):
        problems, h = call.problems, call.headers
        if call.request_line != "POST /counts/forget HTTP/1.1":
            problems.append("the request line is %r" % call.request_line)
        if sorted(call.names) != sorted(set(call.names)) or not set(call.names) <= self.ALLOWED or not {"x-ants-like-pass", "x-ants-csrf", "content-length", "host"} <= set(call.names):
            problems.append("the headers are %s" % sorted(call.names))
        if h.get("content-length") != "0" or call.body:
            problems.append("the call has a body or a length that is not 0")
        if h.get("host") != "127.0.0.1:%d" % self.port or h.get("connection") != "close":
            problems.append("the Host or the Connection is wrong")
        payload = verify(h.get("x-ants-like-pass"), self.secret, time.time())
        if payload is None:
            problems.append("the pass does not verify, or has run out")
            return
        if sorted(payload) != ["exp", "nonce", "ref"] or not re.fullmatch(r"[0-9a-f]{16}", str(payload.get("ref"))) or not re.fullmatch(r"[0-9a-f]{16}", str(payload.get("nonce"))):
            problems.append("the payload is %r" % payload)
            return
        if not time.time() < payload["exp"] <= time.time() + 61:
            problems.append("the pass's life is not a minute at the most")
        with self.lock:
            if payload["nonce"] in self.nonces:
                problems.append("the nonce is not fresh")
            self.nonces.add(payload["nonce"])
        if h.get("x-ants-csrf") != token_of(self.secret, payload["ref"]):
            problems.append("the token is not the reference's")

    def send(self, sock, status, body, extra=""):
        sock.sendall(("HTTP/1.1 %d X\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n%s\r\n" % (status, len(body), extra)).encode("ascii") + body)

    def respond(self, sock, call):
        mode = self.mode
        good = json.dumps({"removed": self.removed}).encode("ascii")
        if mode == "ok":
            return self.send(sock, 200, good)
        if mode.startswith("status:"):
            status = int(mode.split(":")[1])
            return self.send(sock, status, b"" if status == 204 else good, "Location: http://127.0.0.1:%d/counts/forget\r\n" % self.port if 300 <= status < 400 else "")
        if mode == "body":
            return self.send(sock, 200, self.body)
        if mode == "close":
            return
        if mode == "hang":
            return time.sleep(self.hang_seconds)
        if mode == "short":
            return sock.sendall(b'HTTP/1.1 200 X\r\nContent-Length: 500\r\nConnection: close\r\n\r\n{"removed"')
        if mode == "big":
            return self.send(sock, 200, json.dumps({"removed": 1, "pad": "x" * 200000}).encode("ascii"))
        if mode == "chunked_garbage":
            return sock.sendall(b"HTTP/1.1 200 X\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\nzz\r\nhello\r\n0\r\n\r\n")
        if mode == "status_line":
            return sock.sendall(b"SPDY/3 200 maybe\r\n\r\n{}")
        if mode == "double_length":
            return sock.sendall(b"HTTP/1.1 200 X\r\nContent-Length: 2\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}")
        raise AssertionError(mode)


class ForgetTest(unittest.TestCase):
    secret = SECRET
    counts = True                                         # whether the hub is given the stand-in's address
    options = {}

    def setUp(self):
        self.door = FakeCountsDoor()
        self.addCleanup(self.door.stop)
        self.fake = fd.FakeDiscord(CALLBACK, user=(OLD, "maple.leaf"))
        self.fake.consent = False
        env = {}
        if self.secret is not None:
            env["ANTS_ADMIN_GATE_SECRET"] = self.secret
        if self.counts:
            env["ANTS_HUB_COUNTS_URL"] = self.door.url
        self.rig = hub_rig.Rig(discord=self.fake, env=env, **dict(dict(sockets_per_address=100, session_burst=1000, door_seconds=0.6, door_connect_seconds=0.6, tick_seconds=0.05), **self.options))
        self.rig.__enter__()
        self.addCleanup(self.finish)
        self.count = 0

    def finish(self):
        self.rig.__exit__(None, None, None)
        self.fake.close()

    def visitor(self):
        self.count += 1
        return Visitor(self.rig.port, self.fake, address="198.51.100.%d" % self.count)

    def member(self):
        """A visitor who has logged in, who has written a line."""
        v = self.visitor()
        self.assertEqual(v.login().status, 302)
        self.rig.call(self.rig.store.add_line, OLD, "maple.leaf", "a line of mine", False)
        return v

    def rows(self, table):
        db = sqlite3.connect(self.rig.db)
        try:
            return db.execute("SELECT * FROM %s" % table).fetchall()
        finally:
            db.close()

    def log(self):
        return self.rig.log_text.getvalue()

    def assert_everything_is_still_there(self, v):
        self.assertEqual(len(self.rows("sessions")), 1)
        self.assertEqual(len(self.rows("lines")), 1)
        self.assertEqual(len(self.rows("profiles")), 1)
        me = v.call("GET", "/hub/me")
        self.assertEqual(me.status, 200)
        self.assertIsNotNone(me.json()["you"])

    def assert_it_is_all_gone(self):
        for table in ("sessions", "lines", "profiles"):
            self.assertEqual(self.rows(table), [], table)

    def forget(self, v, **kw):
        return v.call("POST", "/hub/forget", **kw)

    def my_ref(self):
        return self.rig.call(self.rig.hub.chat.ref, OLD)


class TheForget(ForgetTest):
    def test_it_calls_the_door_with_the_pass_and_the_token_of_the_reference_and_nothing_else_and_then_deletes_as_before(self):
        v = self.member()
        r = self.forget(v)
        self.assertEqual((r.status, r.json()), (200, {"ok": True}))
        self.assertEqual(r.all("set-cookie"), [CLEARED_SESSION])
        self.assertEqual(len(self.door.calls), 1)
        self.assertEqual(self.door.problems, [])
        payload = verify(self.door.calls[0].headers["x-ants-like-pass"], SECRET, time.time() - 30)
        self.assertEqual(payload["ref"], self.my_ref())
        self.assert_it_is_all_gone()

    def test_nothing_of_the_person_goes_to_the_door_or_into_the_log(self):
        v = self.member()
        cookie = v.session()
        self.assertEqual(self.forget(v).status, 200)
        sent = self.door.calls[0].raw.decode("latin-1")
        for secret in (OLD, "maple.leaf", cookie, self.fake.client_secret, self.fake.bot_token, "198.51.100.", "127.0.0.1:%d" % self.rig.port, hc.SITE):
            self.assertNotIn(secret, sent)
        log = self.log()
        for secret in (OLD, "maple.leaf", cookie, self.my_ref(), self.door.calls[0].headers["x-ants-like-pass"], self.door.calls[0].headers["x-ants-csrf"], self.door.url, "127.0.0.1"):
            self.assertNotIn(secret, log)

    def test_a_door_that_does_not_answer_with_a_200_and_a_whole_number_is_a_503_and_nothing_is_deleted_and_it_can_be_tried_again(self):
        v = self.member()
        modes = [("status:%d" % s, None) for s in (201, 202, 204, 301, 302, 400, 401, 403, 404, 429, 500, 502, 503)]
        modes += [("body", body) for body in (b"<html>not json</html>", b"", b"[1]", b'{"removed":"3"}', b'{"removed":true}', b'{"removed":-1}', b'{"removed":1.5}', b'{"removed":1e3}', b"{}", b"null",
                                              b'{"removed":NaN}', b'{"removed":Infinity}', b'{"removed":null}', b'"3"', b'{"removed":[3]}')]
        modes += [(mode, None) for mode in ("close", "short", "big", "chunked_garbage", "status_line", "double_length")]
        for mode, body in modes:
            with self.subTest(mode=mode, body=body):
                self.door.mode = mode
                self.door.body = body or b""
                before = len(self.door.calls)
                r = self.forget(v)
                self.assertEqual((r.status, r.json()), (503, {"error": "busy"}))
                self.assertEqual(r.all("set-cookie"), [], "the login stays")
                self.assertEqual(len(self.door.calls), before + 1, "one call, and a redirect is not followed")
                self.assert_everything_is_still_there(v)
        log = self.log()
        self.assertEqual(len(re.findall(r" forget_likes_failed kind=(?:down|bad)\n", log)), len(modes), log)
        self.door.mode = "ok"
        r = self.forget(v)
        self.assertEqual((r.status, r.json()), (200, {"ok": True}))
        self.assert_it_is_all_gone()
        self.assertEqual(self.door.problems, [])
        self.assertEqual(self.door.calls[-1].headers["x-ants-csrf"], token_of(SECRET, self.my_ref()))

    def test_a_door_that_is_not_there_is_a_503_that_deletes_nothing(self):
        v = self.member()
        self.door.stop()
        r = self.forget(v)
        self.assertEqual((r.status, r.json()), (503, {"error": "busy"}))
        self.assert_everything_is_still_there(v)

    def test_a_door_that_does_not_answer_in_time_is_a_503_in_time_and_the_hub_goes_on_answering_meanwhile(self):
        v = self.member()
        self.door.mode = "hang"
        done = {}

        def forget():
            started = time.monotonic()
            done["status"] = self.forget(v).status
            done["took"] = time.monotonic() - started

        thread = threading.Thread(target=forget)
        thread.start()
        deadline = time.monotonic() + 5
        while not self.door.calls and time.monotonic() < deadline:
            time.sleep(0.01)
        self.assertTrue(self.door.calls, "the call was made")
        worst = 0.0
        while thread.is_alive():
            started = time.monotonic()
            self.assertEqual(hc.request(self.rig.port, "GET", "/hub/healthz").status, 200)
            worst = max(worst, time.monotonic() - started)
            time.sleep(0.02)
        thread.join()
        self.assertEqual(done["status"], 503)
        self.assertLess(done["took"], 3.0, "the call ends when its time is up, not when the door answers")
        self.assertLess(worst, 0.5, "the hub answered another request while the door was silent")
        self.assert_everything_is_still_there(v)

    def test_each_call_has_a_pass_of_its_own_for_the_same_reference(self):
        v = self.member()
        self.door.mode = "status:500"
        self.assertEqual(self.forget(v).status, 503)
        self.door.mode = "ok"
        self.assertEqual(self.forget(v).status, 200)
        first, second = (verify(c.headers["x-ants-like-pass"], SECRET, time.time() - 30) for c in self.door.calls)
        self.assertEqual(first["ref"], second["ref"])
        self.assertNotEqual(first["nonce"], second["nonce"])
        self.assertEqual(self.door.problems, [])

    def test_a_request_that_is_refused_before_anything_is_deleted_makes_no_call(self):
        v = self.member()
        for site, origin in (("cross-site", "https://evil.example"), ("same-origin", None), (None, hc.SITE), ("same-site", hc.SITE)):
            self.assertEqual(self.forget(v, site=site, origin=origin).status, 403, (site, origin))
        self.assertEqual(v.call("GET", "/hub/forget").status, 405)
        self.assertEqual(v.call("POST", "/hub/forget?x=1").status, 404)
        self.assertEqual(self.visitor().call("POST", "/hub/forget").status, 401, "nobody is logged in")
        self.assertEqual(self.door.calls, [])
        self.assert_everything_is_still_there(v)

    def test_a_reference_that_the_store_cannot_make_is_a_503_and_not_a_call(self):
        v = self.member()
        with mock.patch.object(self.rig.hub.chat, "ref", side_effect=StoreError("down")):
            r = self.forget(v)
        self.assertEqual((r.status, r.json()), (503, {"error": "busy"}))
        self.assertEqual(self.door.calls, [])
        self.assert_everything_is_still_there(v)


class TheSettingIsEmpty(ForgetTest):
    counts = False

    def test_no_setting_no_call_and_a_forget_as_before(self):
        v = self.member()
        with mock.patch.object(likedoor.Door, "forget_likes", side_effect=AssertionError("the door was called")):
            r = self.forget(v)
        self.assertEqual((r.status, r.json()), (200, {"ok": True}))
        self.assert_it_is_all_gone()
        self.assertEqual(self.door.calls, [])
        self.assertNotIn("forget_likes_failed", self.log())


class TheSettingHasNoGoodKey(ForgetTest):
    secret = None

    def test_a_door_that_is_wanted_and_cannot_be_asked_is_a_503_that_deletes_nothing_and_makes_no_call(self):
        v = self.member()
        r = self.forget(v)
        self.assertEqual((r.status, r.json()), (503, {"error": "busy"}))
        self.assertEqual(self.door.calls, [])
        self.assert_everything_is_still_there(v)
        self.assertEqual(self.log().count(" forget_likes_failed kind=off\n"), 1, self.log())


class TheForgetOfSeveralPeople(ForgetTest):
    def test_each_account_is_asked_for_by_its_own_reference(self):
        v = self.member()
        self.assertEqual(self.forget(v).status, 200)
        self.fake.user = (fd.snowflake(800), "sam.the.other")
        w = self.visitor()
        self.assertEqual(w.login().status, 302)
        self.assertEqual(self.forget(w).status, 200)
        refs = [verify(c.headers["x-ants-like-pass"], SECRET, time.time() - 30)["ref"] for c in self.door.calls]
        self.assertEqual(len(refs), 2)
        self.assertNotEqual(refs[0], refs[1])
        self.assertEqual(refs[0], self.my_ref())
        self.assertEqual(self.door.problems, [])


# ---- the way back ---------------------------------------------------------------------------------------------------------------------------------------

GOOD_ID = "ants-maple_leaf-20261123-181530Z"
GOOD_IDS = (GOOD_ID, GOOD_ID + "-7", GOOD_ID + "-1234", "ants-a-20261123-181530Z", "ants-" + "A_z9" * 6 + "-20261123-181530Z-0")
BAD_IDS = ("", "x", "ants-x", "ants--20261123-181530Z", "ants-" + "a" * 25 + "-20261123-181530Z", "ants-x-2026112-181530Z", "ants-x-202611231-181530Z", "ants-x-20261123-18153Z", "ants-x-20261123-181530z",
           "ants-x-20261123-181530Z-12345", "ants-x-20261123-181530Z-", "ants-x-20261123-181530Z-1-2", "ANTS-x-20261123-181530Z", "ants-x-20261123-181530Z ", " ants-x-20261123-181530Z",
           "ants-x-20261123-181530Z\n", "ants-x-20261123-181530Z\r\nSet-Cookie: a=1", "ants-x-20261123-181530Z\x00", "ants-x-20261123-181530Z/", "ants-x-20261123-181530Z/../x", "ants-x-20261123-181530Z?x=1",
           "ants-x-20261123-181530Z#f", "ants-x-20261123-181530Z&next=home", "ants-x/y-20261123-181530Z", "ants-x?y-20261123-181530Z", "ants-x-20261123-181530Z\"><script>", "//evil.example", "https://evil.example/",
           "javascript:alert(1)", "ants-\u00e9-20261123-181530Z", "ants-x-\u0662\u0660\u0662\u0666\u0661\u0661\u0662\u0663-181530Z", "ants-x-20261123-181530Z\u2028", "../match", "ants-x-20261123-181530Z%0a")


class TheWayBack(unittest.TestCase):
    def setUp(self):
        self.fake = fd.FakeDiscord(CALLBACK, user=(OLD, "maple.leaf"))
        self.fake.consent = False
        self.rig = hub_rig.Rig(discord=self.fake, tick_seconds=0.05, sockets_per_address=100)
        self.rig.__enter__()
        self.addCleanup(self.finish)
        self.count = 0

    def finish(self):
        self.rig.__exit__(None, None, None)
        self.fake.close()

    def visitor(self):
        self.count += 1
        return Visitor(self.rig.port, self.fake, address="198.51.100.%d" % self.count)

    def way(self, query):
        """Where a login that was started with `next=<query>` ends (the Location of the callback)."""
        v = self.visitor()
        r = v.start(query)
        self.assertEqual(r.status, 302, query)
        r = v.callback(v.at_discord(r.get("location")))
        self.assertEqual(r.status, 302, query)
        return r.get("location")

    def test_the_words_matches_and_match_bring_the_visitor_back_to_the_list_and_to_the_match(self):
        self.assertEqual(self.way("matches"), "/matches")
        self.assertEqual(self.way("match&id=" + GOOD_ID), "/match?id=" + GOOD_ID)

    def test_an_id_of_the_shape_of_a_match_id_is_the_way_back_and_one_that_is_not_is_the_list(self):
        for ident in GOOD_IDS:
            self.assertEqual(self.way("match&id=" + urllib.parse.quote(ident, safe="")), "/match?id=" + ident, ident)
        for ident in BAD_IDS:
            location = self.way("match&id=" + urllib.parse.quote(ident, safe=""))
            self.assertEqual(location, "/matches", repr(ident))

    def test_a_missing_or_doubled_or_empty_id_is_the_list_and_the_id_is_read_for_the_word_match_only(self):
        self.assertEqual(self.way("match"), "/matches")
        self.assertEqual(self.way("match&id="), "/matches")
        self.assertEqual(self.way("match&id=%s&id=%s" % (GOOD_ID, GOOD_ID)), "/matches", "a name that comes twice is nobody's")
        self.assertEqual(self.way("match&ID=" + GOOD_ID), "/matches")
        self.assertEqual(self.way("match&id=" + GOOD_ID + "&next=room"), "/", "a word that comes twice is nobody's: Home")
        self.assertEqual(self.way("room&id=" + GOOD_ID), "/#room")
        self.assertEqual(self.way("home&id=" + GOOD_ID), "/")
        self.assertEqual(self.way("matches&id=" + GOOD_ID), "/matches")
        self.assertEqual(self.way("mapmaker&id=" + GOOD_ID), "/mapmaker")

    def test_a_letter_that_is_written_as_a_percent_code_is_the_same_letter_and_the_address_is_made_of_the_checked_id(self):
        self.assertEqual(self.way("match&id=%61nts-maple_leaf-20261123-181530Z"), "/match?id=ants-maple_leaf-20261123-181530Z")
        self.assertEqual(self.way("matc%68&id=" + GOOD_ID), "/match?id=" + GOOD_ID)
        self.assertEqual(self.way("match&id=ants-maple_leaf-20261123-181530Z%2f"), "/matches")

    def test_other_words_are_not_words(self):
        for word in ("MATCH", "Match", "match%20", "matches%20", "match/", "/match", "match?id=" + GOOD_ID, "matchs", "mat", "match%00", "http://x", "//x"):
            self.assertEqual(self.way(word + "&id=" + GOOD_ID), "/", word)

    def test_an_id_that_is_too_long_for_a_request_is_a_400_and_no_login_is_started(self):
        v = self.visitor()
        r = v.start("match&id=" + "a" * 3000)
        self.assertEqual(r.status, 400)
        self.assertIsNone(r.get("location"))
        self.assertEqual(self.rig.call(lambda: len(self.rig.hub.sessions.states)), 0)

    def test_the_page_of_a_login_that_failed_leads_to_the_same_place_and_the_state_is_used_once(self):
        for query, link in (("match&id=" + GOOD_ID, "/match?id=" + GOOD_ID), ("match&id=" + urllib.parse.quote("ants-x/y", safe=""), "/matches"), ("matches", "/matches"), ("room", "/#room"), ("home", "/")):
            v = self.visitor()
            r = v.start(query)
            back = v.at_discord(r.get("location"))
            r = v.callback(back, cookies=False)                                     # (the browser lost the cookie of the state: the callback is a calm page)
            self.assertEqual(r.status, 400, query)
            self.assertIn('<a href="%s">' % link, r.text(), query)
            r = v.callback(back)                                                     # (the state was used up by the first callback, whatever it ended in: the way back is Home)
            self.assertEqual(r.status, 400, query)
            self.assertIn('<a href="/">', r.text(), query)
            self.assertIsNone(r.get("location"))

    def test_the_state_of_a_match_is_used_once_and_a_login_that_is_not_finished_runs_out_like_any_other(self):
        v = self.visitor()
        r = v.start("match&id=" + GOOD_ID)
        back = v.at_discord(r.get("location"))
        self.assertEqual(self.rig.call(lambda: len(self.rig.hub.sessions.states)), 1)
        self.assertEqual(v.callback(back).get("location"), "/match?id=" + GOOD_ID)
        self.assertEqual(self.rig.call(lambda: len(self.rig.hub.sessions.states)), 0)
        self.assertEqual(self.visitor().start("match&id=" + GOOD_ID).status, 302)
        self.rig.call(setattr, self.rig.store, "clock", lambda: time.time() + 3600)
        self.rig.call(self.rig.hub.sessions.sweep, time.time() + 3600)
        self.assertEqual(self.rig.call(lambda: len(self.rig.hub.sessions.states)), 0)


# ---- the setting ----------------------------------------------------------------------------------------------------------------------------------------

class TheSetting(unittest.TestCase):
    def test_the_address_is_http_a_host_a_port_and_nothing_else(self):
        for good, shown in (("http://ants-server:4032", "http://ants-server:4032"), ("http://127.0.0.1:4032", "http://127.0.0.1:4032"), ("http://a.b-c.example:1", "http://a.b-c.example:1"),
                            ("  http://ants-server:65535  ", "http://ants-server:65535"), ("http://x:0040", "http://x:40")):
            self.assertEqual(configmod.load({"ANTS_HUB_COUNTS_URL": good}).counts_url, shown)
        for bad in ("https://ants-server:4032", "http://ants-server", "http://ants-server:0", "http://ants-server:65536", "http://ants-server:4032/", "http://ants-server:4032/counts/forget", "http://ants-server:4032?x=1",
                    "http://user@ants-server:4032", "http://user:pw@ants-server:4032", "http://[::1]:4032", "http://ant_s:4032", "http://-ants:4032", "http://:4032", "ants-server:4032",
                    "http://ants server:4032", "http://ants-server:4032#x", "http://ants-server:+4032", "http://ants-server:123456", "ftp://ants-server:4032", "http://ants\u00e9:4032"):
            with self.assertRaises(configmod.ConfigError, msg=bad) as caught:
                configmod.load({"ANTS_HUB_COUNTS_URL": bad})
            self.assertIn("ANTS_HUB_COUNTS_URL", str(caught.exception))
        with self.assertRaises(configmod.ConfigError) as caught:
            configmod.load({"ANTS_HUB_COUNTS_URL": "http://canary-host-9f3a.invalid:99999999"})
        self.assertNotIn("canary-host-9f3a", str(caught.exception))                                 # (the message never says the value)

    def test_none_is_none_and_the_summary_of_the_settings_never_says_it(self):
        self.assertEqual(configmod.load({}).counts_url, "")
        self.assertEqual(configmod.load({"ANTS_HUB_COUNTS_URL": "   "}).counts_url, "")
        self.assertNotIn("ants-server", repr(configmod.load({"ANTS_HUB_COUNTS_URL": "http://ants-server:4032"}).summary()))

    def test_the_setting_is_not_the_game_servers_admin_address(self):
        cfg = configmod.load({"ANTS_HUB_COUNTS_URL": "http://ants-server:4032"})
        self.assertEqual(cfg.game_admin_url, "")
        cfg = configmod.load({"ANTS_HUB_GAME_ADMIN_URL": "http://ants-server:4030"})
        self.assertEqual(cfg.counts_url, "")


# ---- the real process -----------------------------------------------------------------------------------------------------------------------------------

class TheStart(unittest.TestCase):
    def setUp(self):
        self.procs = []
        self.addCleanup(self.stop_all)

    def stop_all(self):
        for proc in self.procs:
            if proc.poll() is None:
                proc.kill()
            proc.wait(5)

    def run_hub(self, extra):
        proc, folder, log_path, env = start_process(extra)
        self.procs.append(proc)
        port = wait_for_port(proc, log_path)
        self.assertIsNotNone(port, "the hub did not start:\n" + read_text(log_path))
        return port, log_path

    def gate(self, port, account=PLAIN):
        return hc.request(port, "GET", "/hub/likes/gate", [("X-Client-Address", "198.51.100.9"), login_header(account)])

    def test_the_process_with_a_good_key_gives_a_pass_and_says_nothing_of_it(self):
        port, log_path = self.run_hub({"ANTS_HUB_TEST_LOGIN": "1", "ANTS_ADMIN_GATE_SECRET": SECRET, "ANTS_HUB_COUNTS_URL": "http://127.0.0.1:1"})
        r = self.gate(port)
        self.assertEqual(r.status, 204)
        self.assertIsNotNone(verify(r.get("x-ants-like-pass"), SECRET, time.time()))
        text = read_text(log_path)
        self.assertNotIn("no_good_admin_key", text)
        for secret in (SECRET, PLAIN, NAMES[PLAIN], r.get("x-ants-like-pass")):
            self.assertNotIn(secret, text)

    def test_a_setting_that_cannot_be_used_for_want_of_a_key_is_said_once_and_the_gate_is_shut(self):
        port, log_path = self.run_hub({"ANTS_HUB_TEST_LOGIN": "1", "ANTS_HUB_COUNTS_URL": "http://127.0.0.1:1"})
        self.assertEqual(self.gate(port).status, 503)
        text = read_text(log_path)
        self.assertEqual(text.count("forget_me_cannot_reach_the_likes_no_good_admin_key"), 1, text)
        self.assertNotIn("127.0.0.1:1", text)

    def test_a_setting_that_is_not_an_address_stops_the_start_and_does_not_say_what_it_was(self):
        proc, folder, log_path, env = start_process({"ANTS_HUB_TEST_LOGIN": "1", "ANTS_ADMIN_GATE_SECRET": SECRET, "ANTS_HUB_COUNTS_URL": "https://canary-host-7c1e.invalid:4032/x"})
        self.procs.append(proc)
        self.assertEqual(proc.wait(20), 2)
        text = read_text(log_path)
        self.assertIn("ANTS_HUB_COUNTS_URL", text)
        self.assertNotIn("canary-host-7c1e", text)


if __name__ == "__main__":
    unittest.main()
