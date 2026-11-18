#!/usr/bin/env python3
"""The admin page's door (hub/admindoor.py, hub/admin.py `resolve`, docs/HUB.md "The admin page's door"): who may use it, the pass that the site's nginx hands on, and the hub's side of what the page does.

  TheRule          the one rule: nobody / no role / moderator / administrator / the operator's account, each with a fresh answer, an old one and a Discord that is down; no key, a short key, a key with a blank
                   (a shut door is a 503 `door_off` at the gate and on the page's calls, for everybody, and never the 403 that says "not an administrator")
  TheFreshCheck    the door wants an answer a minute old for an administrator; an action asks for both
  TheLookupInFlight  requests of one account that come together wait for one lookup and share its answer
  TheDoorsOwnShare the door's lookups come from its own share, not the moderators' actions'
  TheStaffShare    accounts never seen with a role have a small share of their own
  TheRequestsThatAreNotWhatTheDoorIsFor   no address at the gate, a body promised by somebody who is not let in, an empty query, a key twice, an id-shaped name in an old row
  ThePass          `v1.<payload>.<mac>`: the mac holds with the key and with nothing else, the expiry is a minute from now, the payload says a reference, a name and a role and never an id
  TheGate          what the gate is and is not: GET only, no body in or out, no CORS, a limit for each address
  TheFences        the page's calls: the same-origin rules, the JSON type, the token (none, wrong, another login's), the shape of the body, the size of the body
  TheChanges       every change and its row of the moderators' log, the powers, the fresh check, the lists and the status
  TheIdIsLikeAPassword   an id that is planted is in no answer, pass, log line or frame
  TheRealLogin     the same through the Discord login's cookie, and `next=admin`
  TheOneSecret     the key is the one setting ANTS_ADMIN_GATE_SECRET (the game server's name too); the hub's old name is read only when it is not set
  TheStart         a missing, short or malformed key does not stop the hub and is said once at the start

The fake Discord holds the roles (tests/scripts/fake_discord.py); the intervals are short (a check every second, a grace of a third of a second more). Every id here is made up.
"""
import base64
import hashlib
import hmac
import json
import os
import re
import socket
import sys
import tempfile
import threading
import time
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
import fake_discord as fd  # noqa: E402
import hub_client as hc  # noqa: E402
import hub_rig  # noqa: E402
from login_flow import Visitor  # noqa: E402
from test_hub_server import read_text, start_process, wait_for_port  # noqa: E402
from hub import admindoor, config as configmod, log as logmod  # noqa: E402

SECRET = "testing-gate-key-0b8e2d71a4c6f395-z"           # 35 characters (a made-up key)
SAMPLE_SECRET = "canary-gate-key-5d8f0c1e7a9b4263-q"     # the made-up key of the known answer in docs/HUB.md: published, so refused as a key
OWNER = "9100000000000001"
ADMIN = "9100000000000002"
ADMIN2 = "9100000000000007"
MOD = "9100000000000003"
PLAIN = "9100000000000004"
TARGET = "9100000000000005"
AWAY = "9100000000000006"                              # an account that has no login and no line: nobody the hub has seen
NAMES = {OWNER: "olive.owner", ADMIN: "adam.admin", ADMIN2: "ada.admin", MOD: "molly.mod", PLAIN: "pat.plain", TARGET: "terry.target", AWAY: "away.person"}
EVERY_ID = tuple(NAMES)
OPTIONS = dict(roles_grace_seconds=0.3, roles_refill_seconds=0.01, roles_jitter=0.0, tick_seconds=0.05, sockets_per_address=50, frame_burst=1000, frame_refill_seconds=0.001,
               discord_seconds=1.0, discord_connect_seconds=1.0, roles_pause_seconds=60.0, admin_lookups_burst=50, admin_lookups_refill_seconds=0.01)
BOSS = fd.snowflake(900)                               # (the ids that a login by Discord has are old enough to write: one number for the whole file, a snowflake is new at every call)
OTHER = fd.snowflake(901)
OLDOWNER = fd.snowflake(777)
PASS_FORM = re.compile(r"v1\.([A-Za-z0-9_-]+)\.([0-9a-f]{64})\Z")


def b64decode(text):
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def verify(header, key, now):
    """What the game server does with a pass (written here from the description alone): the payload, or None for a pass whose mac is wrong or that has run out."""
    m = PASS_FORM.match(header or "")
    if not m:
        return None
    pass_key = hmac.new(key.encode(), b"ants-admin-pass-v1", hashlib.sha256).digest()        # (the key of the pass is derived from the secret, as docs/HUB.md says)
    good = hmac.new(pass_key, ("v1." + m.group(1)).encode(), hashlib.sha256).hexdigest()
    if not hmac.compare_digest(good, m.group(2)):
        return None
    payload = json.loads(b64decode(m.group(1)))
    return payload if payload["exp"] > now else None


class DoorTest(unittest.TestCase):
    secret = SECRET
    options = {}
    recheck = 1

    def setUp(self):
        self.fake = fd.FakeDiscord()
        self.addCleanup(self.fake.close)
        self.fake.members = {OWNER: [], ADMIN: [fd.ROLE_ADMIN], ADMIN2: [fd.ROLE_ADMIN], MOD: [fd.ROLE_MOD], PLAIN: [], TARGET: [], AWAY: []}
        env = {"ANTS_ADMIN_GATE_SECRET": self.secret} if self.secret is not None else {}
        self.rig = hub_rig.Rig(member_fake=self.fake, owner=OWNER, recheck=self.recheck, env=env, **dict(OPTIONS, **self.options))
        self.rig.__enter__()
        self.addCleanup(self.rig.__exit__, None, None, None)
        self.tokens = {}
        self.count = 0
        self.sockets = []
        self.addCleanup(self.close_sockets)
        for account in (OWNER, ADMIN, ADMIN2, MOD, PLAIN, TARGET):
            self.rig.call(self.rig.store.set_profile, account, NAMES[account], "", 1000)

    def close_sockets(self):
        for c in self.sockets:
            c.close()

    # ---- the calls of a page ---------------------------------------------------------------------------------------------------------------

    def login_header(self, account):
        return ("X-Hub-Test-Login", "%s:%s" % (account, NAMES[account]))

    def call(self, method, path, account=None, body=None, site="same-origin", origin="default", ctype="default", csrf="default", extra=(), address=None, raw=None):
        """One request as the admin page makes it (or not): the test login of `account`, Sec-Fetch-Site, the Origin of a POST, a JSON type and the token of /me."""
        self.count += 1
        pairs = [] if address == "" else [("X-Client-Address", address or "198.51.100.%d" % (self.count % 200 + 1))]       # (address="" leaves the header out)
        if account:
            pairs.append(self.login_header(account))
        if site is not None:
            pairs.append(("Sec-Fetch-Site", site))
        post = method == "POST"
        if (origin == "default" and post) or origin not in ("default", None):
            pairs.append(("Origin", hc.SITE if origin == "default" else origin))
        data = raw if raw is not None else json.dumps(body).encode() if body is not None else b""
        if post and ctype is not None:
            pairs.append(("Content-Type", "application/json" if ctype == "default" else ctype))
        if post and csrf is not None:
            pairs.append(("X-Ants-Csrf", self.token(account) if csrf == "default" else csrf))
        pairs += list(extra)
        return hc.request(self.rig.port, method, path, pairs, body=data)

    def token(self, account):
        """The token of /me for this login (a made-up one for somebody who is not let in: the call is then refused for that, and the test says so)."""
        if account not in self.tokens:
            r = self.call("GET", "/hub/admin/api/me", account)
            if r.status != 200:
                return "0" * 64
            self.tokens[account] = r.json()["csrf"]
        return self.tokens[account]

    def get(self, name, account=ADMIN, **kw):
        return self.call("GET", "/hub/admin/api/" + name, account, **kw)

    def post(self, name, body, account=ADMIN, **kw):
        return self.call("POST", "/hub/admin/api/" + name, account, body=body, **kw)

    def gate(self, account=None, **kw):
        return self.call("GET", "/hub/admin/gate", account, **kw)

    def modlog(self):
        return self.rig.call(self.rig.store.actions, 0, 100)

    def ref(self, account):
        return self.rig.call(self.rig.hub.chat.ref, account)

    def socket(self, account):
        c = hc.Client(self.rig.port, login="%s:%s" % (account, NAMES[account]))
        self.sockets.append(c)
        self.assertTrue(c.ok, c.status)
        c.recv_json()
        return c


class TheRule(DoorTest):
    def test_nobody_logged_in_is_a_401_and_the_door_never_asks_discord(self):
        r = self.gate()
        self.assertEqual((r.status, r.body), (401, b""))
        self.assertEqual(self.get("me", None).status, 401)
        self.assertEqual(self.fake.member_ids, [])

    def test_the_matrix_of_who_gets_in_with_a_fresh_answer(self):
        want = {OWNER: 204, ADMIN: 204, MOD: 403, PLAIN: 403, AWAY: 403}
        for account, status in want.items():
            with self.subTest(NAMES[account]):
                self.assertEqual(self.gate(account).status, status)
        self.assertEqual(self.fake.member_ids.count(OWNER), 0, "the operator's account needs no call to Discord")

    def test_a_moderator_is_not_let_in_by_the_page_or_the_gate(self):
        self.assertEqual(self.gate(MOD).status, 403)
        r = self.get("bans", MOD)
        self.assertEqual((r.status, r.json()), (403, {"error": "admin"}))
        r = self.post("ban", {"account": NAMES[TARGET]}, MOD, csrf="0" * 64)
        self.assertEqual(r.status, 403)
        self.assertFalse(self.rig.call(self.rig.store.is_banned, TARGET))

    def test_a_fresh_answer_is_believed_without_another_call(self):
        self.assertEqual(self.gate(ADMIN).status, 204)
        asked = len(self.fake.member_ids)
        self.assertEqual(asked, 1)
        for _ in range(5):
            self.assertEqual(self.gate(ADMIN).status, 204)
        self.assertEqual(len(self.fake.member_ids), asked, "a role that is fresh costs no call to Discord")

    def test_an_answer_that_is_old_and_a_discord_that_is_down_close_the_door(self):
        self.assertEqual(self.gate(ADMIN).status, 204)
        self.fake.member_mode = "status:500"
        time.sleep(1.8)                                              # (the interval of a second, the tenth more and the grace: the answer is no longer believed)
        self.assertEqual(self.gate(ADMIN).status, 403)
        self.assertEqual(self.get("bans", ADMIN).status, 403)
        self.assertEqual(self.gate(OWNER).status, 204, "the operator's account works with Discord down")
        self.fake.member_mode = "ok"
        self.assertEqual(self.gate(ADMIN).status, 204, "and the door opens again with the first answer")

    def test_a_discord_that_is_down_from_the_start_lets_in_nobody_but_the_operator(self):
        self.fake.member_mode = "status:500"
        self.assertEqual([self.gate(a).status for a in (ADMIN, MOD, PLAIN)], [403, 403, 403])
        self.assertEqual(self.gate(OWNER).status, 204)

    def test_a_token_that_discord_refuses_closes_the_door_for_everybody_but_the_operator(self):
        self.assertEqual(self.gate(ADMIN).status, 204)
        self.fake.member_mode = "err:401"
        time.sleep(1.8)
        self.assertEqual(self.gate(ADMIN).status, 403)
        self.assertEqual(self.gate(OWNER).status, 204)

    def test_a_role_that_is_taken_away_closes_the_door_within_the_interval(self):
        self.assertEqual(self.gate(ADMIN).status, 204)
        self.fake.members[ADMIN] = []
        time.sleep(1.8)
        self.assertEqual(self.gate(ADMIN).status, 403)

    def test_a_person_who_asks_again_and_again_costs_discord_one_call_and_a_discord_that_is_down_a_burst(self):
        def ask(rig, account, times):
            for _ in range(times):
                r = hc.request(rig.port, "GET", "/hub/admin/gate", [("X-Client-Address", "198.51.100.77"), ("X-Hub-Test-Login", "%s:%s" % (account, NAMES[account]))])
                self.assertEqual(r.status, 403)
        with hub_rig.Rig(member_fake=self.fake, owner=OWNER, recheck=1, env={"ANTS_ADMIN_GATE_SECRET": SECRET},
                         **dict(OPTIONS, admin_lookups_burst=2, admin_lookups_refill_seconds=60.0, roles_grace_seconds=30.0, admin_door_new_burst=10)) as rig:
            ask(rig, PLAIN, 10)
            self.assertEqual(self.fake.member_ids.count(PLAIN), 1, "an answer of `none` is an answer, and a fresh one is believed")
            self.fake.member_mode = "status:500"
            ask(rig, MOD, 10)
            self.assertEqual(self.fake.member_ids.count(MOD), 2, "a person that no answer is known of makes the hub ask a burst and then waits")

    def test_the_door_is_shut_for_everybody_without_a_good_key(self):
        for secret in (None, "", "x" * 31, "a b" * 20, "caf\u00e9-" * 8, SAMPLE_SECRET):
            with self.subTest(secret=len(secret or "")):
                env = {"ANTS_ADMIN_GATE_SECRET": secret} if secret is not None else {}
                with hub_rig.Rig(member_fake=self.fake, owner=OWNER, recheck=1, env=env, **OPTIONS) as rig:
                    for account in (OWNER, ADMIN, MOD, PLAIN):
                        pairs = [("X-Client-Address", "198.51.100.5"), ("X-Hub-Test-Login", "%s:%s" % (account, NAMES[account])), ("Sec-Fetch-Site", "same-origin")]
                        r = hc.request(rig.port, "GET", "/hub/admin/gate", pairs)
                        self.assertEqual((r.status, r.get("x-ants-admin-pass"), r.body), (503, None, b""), "a shut door is not a 'no': nginx says door_off and the page says it is not available")
                        r = hc.request(rig.port, "GET", "/hub/admin/api/me", pairs)
                        self.assertEqual((r.status, r.json()), (503, {"error": "door_off"}), "the page's own calls say the same, never 'admin'")
                    r = hc.request(rig.port, "GET", "/hub/admin/gate", [("X-Client-Address", "198.51.100.5")])
                    self.assertEqual(r.status, 503, "a door that is shut is shut for everybody, nobody logged in too")
                    pairs = [("X-Client-Address", "198.51.100.5"), ("Sec-Fetch-Site", "same-origin")]
                    for name in sorted(admindoor.READS):
                        r = hc.request(rig.port, "GET", "/hub/admin/api/" + name, pairs)
                        self.assertEqual((r.status, r.json()), (503, {"error": "door_off"}), name)
                        self.assertNotIn(b"csrf", r.body)
                    for name in sorted(admindoor.WRITES):
                        r = hc.request(rig.port, "POST", "/hub/admin/api/" + name, pairs + [("Origin", hc.SITE), ("Content-Type", "application/json")], body=b"{}")
                        self.assertEqual((r.status, r.json()), (503, {"error": "door_off"}), name)

    def test_a_door_that_is_open_still_says_who_is_not_allowed(self):
        """The other side of the rule: with a good key, nobody logged in is 401 and a person who may not is 403 (a shut door is not mistaken for either)."""
        self.assertEqual(self.gate().status, 401)
        self.assertEqual(self.gate(PLAIN).status, 403)
        self.assertEqual(self.get("me", None).status, 401)
        r = self.get("me", PLAIN)
        self.assertEqual((r.status, r.json()), (403, {"error": "admin"}))


class ThePass(DoorTest):
    def test_the_pass_holds_with_the_key_and_with_nothing_else(self):
        r = self.gate(ADMIN)
        header = r.get("x-ants-admin-pass")
        self.assertRegex(header, PASS_FORM)
        now = time.time()
        payload = verify(header, SECRET, now)
        self.assertIsNotNone(payload)
        self.assertIsNone(verify(header, SECRET + "x", now))
        self.assertIsNone(verify(header, "another-key-of-the-same-sort-0123456789", now))
        version, body, mac = header.split(".")
        flipped = body[:-2] + ("AA" if body[-2:] != "AA" else "BB")
        self.assertIsNone(verify("v1.%s.%s" % (flipped, mac), SECRET, now))
        self.assertIsNone(verify("v2.%s.%s" % (body, mac), SECRET, now))
        self.assertIsNone(verify("v1.%s.%s" % (body, "0" * 64), SECRET, now))

    def test_the_payload_has_a_reference_a_name_a_role_an_expiry_and_a_nonce_and_nothing_else(self):
        header = self.gate(ADMIN).get("x-ants-admin-pass")
        payload = verify(header, SECRET, time.time())
        self.assertEqual(sorted(payload), ["exp", "name", "nonce", "ref", "role"])
        self.assertEqual((payload["name"], payload["role"], payload["ref"]), (NAMES[ADMIN], "admin", self.ref(ADMIN)))
        self.assertRegex(payload["ref"], r"[0-9a-f]{16}\Z")
        self.assertRegex(payload["nonce"], r"[0-9a-f]{16}\Z")
        owner = verify(self.gate(OWNER).get("x-ants-admin-pass"), SECRET, time.time())
        self.assertEqual((owner["name"], owner["role"], owner["ref"]), (NAMES[OWNER], "owner", self.ref(OWNER)))

    def test_the_expiry_is_a_minute_from_the_hub_clock_and_a_verifier_honours_it(self):
        self.rig.call(setattr, self.rig.store, "clock", lambda: 5_000_000.0)
        header = self.gate(OWNER).get("x-ants-admin-pass")
        self.assertEqual(verify(header, SECRET, 5_000_000.0)["exp"], 5_000_060)
        self.assertIsNotNone(verify(header, SECRET, 5_000_059.0))
        self.assertIsNone(verify(header, SECRET, 5_000_060.0), "a pass of a minute ago is no pass")
        self.assertIsNone(verify(header, SECRET, 5_000_600.0))
        self.rig.call(setattr, self.rig.store, "clock", lambda: 5_000_100.0)
        self.assertEqual(verify(self.gate(OWNER).get("x-ants-admin-pass"), SECRET, 5_000_100.0)["exp"], 5_000_160)

    def test_every_pass_has_a_nonce_of_its_own(self):
        seen = {verify(self.gate(OWNER).get("x-ants-admin-pass"), SECRET, time.time())["nonce"] for _ in range(8)}
        self.assertEqual(len(seen), 8)

    def test_no_refusal_carries_a_pass(self):
        for account in (None, MOD, PLAIN):
            r = self.gate(account)
            self.assertIsNone(r.get("x-ants-admin-pass"), account)
            self.assertEqual(r.body, b"")

    def test_the_pass_is_the_answer_of_the_gate_only(self):
        for name in ("me", "bans", "status"):
            r = self.get(name)
            self.assertEqual(r.status, 200)
            self.assertIsNone(r.get("x-ants-admin-pass"), name)


class TheFreshCheck(DoorTest):
    """The door wants an answer of Discord that is a minute old at the most for an administrator (a pass is believed by the game server for a minute), as an action does. Here the minute is a fifth of a
    second and the hub's usual belief in a role (its grace) is long, so that only the door's own rule decides."""

    options = dict(roles_action_fresh_seconds=0.2, roles_grace_seconds=30.0)

    def test_an_answer_that_is_young_costs_no_call_and_an_old_one_is_asked_for_again(self):
        self.assertEqual(self.gate(ADMIN).status, 204)
        self.assertEqual(self.fake.member_ids.count(ADMIN), 1)
        self.assertEqual(self.gate(ADMIN).status, 204)
        self.assertEqual(self.fake.member_ids.count(ADMIN), 1, "a fresh answer is believed")
        time.sleep(0.5)
        self.assertEqual(self.gate(ADMIN).status, 204)
        self.assertEqual(self.fake.member_ids.count(ADMIN), 2, "an answer that is old is asked for again")

    def test_a_role_that_went_closes_the_gate_and_the_reads_at_once_not_after_the_hubs_grace(self):
        self.token(ADMIN)
        self.fake.members[ADMIN] = []
        time.sleep(0.5)
        for r in (self.gate(ADMIN), self.get("bans"), self.get("modlog"), self.get("status")):
            self.assertEqual(r.status, 403)
        self.assertIsNone(self.gate(ADMIN).get("x-ants-admin-pass"))

    def test_an_old_answer_and_a_discord_that_does_not_answer_close_the_door_even_inside_the_grace(self):
        self.token(ADMIN)
        self.fake.member_mode = "status:500"
        time.sleep(0.5)
        self.assertEqual(self.gate(ADMIN).status, 403, "believed by the roles, not young enough for the door")
        self.assertEqual(self.gate(OWNER).status, 204, "the operator's account needs no answer")
        self.fake.member_mode = "ok"
        time.sleep(0.2)
        self.assertEqual(self.gate(ADMIN).status, 204)

    def test_an_action_needs_the_fresh_check_of_both_and_a_discord_that_is_down_refuses_it(self):
        self.token(ADMIN)                                            # (the actor is young enough for the door now)
        self.fake.member_mode = "status:500"
        r = self.post("ban", {"account": NAMES[TARGET]})            # (the person acted on has no answer at all: the action must ask, and Discord does not answer)
        self.assertEqual((r.status, r.json()), (503, {"error": "role"}))
        self.assertFalse(self.rig.call(self.rig.store.is_banned, TARGET))
        self.assertEqual(self.modlog(), [])

    def test_an_actor_whose_answer_is_old_while_discord_is_down_is_refused_by_the_door_before_any_action(self):
        self.token(ADMIN)
        self.fake.member_mode = "status:500"
        time.sleep(0.5)
        r = self.post("ban", {"account": NAMES[TARGET]})
        self.assertEqual((r.status, r.json()), (403, {"error": "admin"}))
        self.assertFalse(self.rig.call(self.rig.store.is_banned, TARGET))

    def test_an_action_on_a_person_whose_answer_is_old_asks_for_it(self):
        self.token(ADMIN)
        self.assertEqual(self.post("mute", {"account": NAMES[TARGET], "minutes": 5}).status, 200)
        asked = self.fake.member_ids.count(TARGET)
        self.assertGreaterEqual(asked, 1)
        time.sleep(0.5)
        self.assertEqual(self.post("unmute", {"account": NAMES[TARGET]}).status, 200)
        self.assertEqual(self.fake.member_ids.count(TARGET), asked + 1)


class TheLookupInFlight(DoorTest):
    """Requests of one account that come while its answer is being asked for wait for that lookup and share the result: the page makes several calls at once. The share of each account (3, no refill)
    is smaller than the crowd, so a request that took a share of its own would be refused."""

    options = dict(roles_action_fresh_seconds=0.2, roles_grace_seconds=30.0, admin_lookups_burst=3, admin_lookups_refill_seconds=600.0, admin_burst=1000)

    def together(self, account, times=8):
        found = []

        def one(n):
            r = hc.request(self.rig.port, "GET", "/hub/admin/gate", [("X-Client-Address", "198.51.100.%d" % (n + 10)), self.login_header(account)])
            found.append(r.status)
        threads = [threading.Thread(target=one, args=(n,)) for n in range(times)]
        for t in threads:
            t.start()
        for t in threads:
            t.join(20)
        return found

    def test_parallel_calls_for_an_administrator_whose_answer_is_old_are_all_let_in_on_one_lookup(self):
        self.assertEqual(self.gate(ADMIN).status, 204)
        self.assertEqual(self.fake.member_ids.count(ADMIN), 1)
        time.sleep(0.5)
        self.fake.member_mode = "slow:0.4"
        self.assertEqual(self.together(ADMIN), [204] * 8)
        self.assertEqual(self.fake.member_ids.count(ADMIN), 2, "one lookup for all eight")

    def test_parallel_calls_while_the_lookup_fails_are_all_refused_on_one_lookup(self):
        self.assertEqual(self.gate(ADMIN).status, 204)
        time.sleep(0.5)
        self.fake.member_mode = "hang"
        self.assertEqual(self.together(ADMIN), [403] * 8)
        self.assertEqual(self.fake.member_ids.count(ADMIN), 2, "one lookup for all eight")


class TheDoorsOwnShare(DoorTest):
    """The door's lookups are paid from its own small share: the moderators' commands live on the actions' share (hub/roles.py `afford`), and a crowd of people with no role at the door does not touch it."""

    options = dict(admin_burst=1000, roles_urgent_burst=3, roles_urgent_refill_seconds=600.0, roles_grace_seconds=30.0, admin_door_burst=100, admin_door_new_burst=100, admin_lookups_burst=100)

    def test_many_logged_in_people_with_no_role_at_the_gate_do_not_use_up_what_a_mute_needs(self):
        extras = ["92%014d" % n for n in range(1, 31)]
        for account in extras:
            self.fake.members[account] = []
            NAMES[account] = "visitor.%d" % int(account[-5:])
        try:
            for account in extras:
                self.assertEqual(self.gate(account).status, 403)
            self.assertGreaterEqual(len(self.fake.member_ids), 30, "each one was looked up (by the door's own share)")
            before = len(self.fake.member_ids)
            self.token(ADMIN)
            r = self.post("mute", {"account": NAMES[TARGET], "minutes": 5})
            self.assertEqual((r.status, r.json().get("error")), (200, None), "the actions' share is whole")
            self.assertGreater(len(self.fake.member_ids), before)
        finally:
            for account in extras:
                del NAMES[account]

    def test_the_door_has_a_share_of_its_own_and_it_runs_out(self):
        with hub_rig.Rig(member_fake=self.fake, owner=OWNER, recheck=1, env={"ANTS_ADMIN_GATE_SECRET": SECRET},
                         **dict(OPTIONS, admin_burst=1000, admin_door_burst=2, admin_door_refill_seconds=600.0, admin_door_new_burst=2, admin_door_new_refill_seconds=600.0, admin_lookups_burst=100)) as rig:
            seen = 0
            for account in (ADMIN, ADMIN2, MOD, PLAIN):
                r = hc.request(rig.port, "GET", "/hub/admin/gate", [("X-Client-Address", "198.51.100.40"), ("X-Hub-Test-Login", "%s:%s" % (account, NAMES[account]))])
                seen += r.status == 204
            self.assertEqual(seen, 2, "the door's share is two lookups: the third and the fourth person are not let in, whoever they are")
            self.assertEqual(len(self.fake.member_ids), 2)


class TheStaffShare(DoorTest):
    """An account that was never seen with a role has a small share of lookups of its own, so that plain accounts cannot use up the share of the staff."""

    options = dict(roles_action_fresh_seconds=0.2, roles_grace_seconds=30.0, admin_burst=1000, admin_lookups_burst=100, admin_door_burst=5, admin_door_refill_seconds=600.0,
                   admin_door_new_burst=2, admin_door_new_refill_seconds=600.0)

    def test_plain_accounts_that_spend_their_share_do_not_stop_an_administrators_pass(self):
        self.assertEqual(self.gate(ADMIN).status, 204)                  # (first seen with a role)
        extras = ["93%014d" % n for n in range(1, 11)]
        for account in extras:
            self.fake.members[account] = []
            NAMES[account] = "visitor.%d" % int(account[-5:])
        try:
            for account in extras:
                self.assertEqual(self.gate(account).status, 403)
            looked = [a for a in self.fake.member_ids if a in extras]
            self.assertEqual(len(looked), 1, "the small share held two lookups, one of which the administrator's first visit used")
            time.sleep(0.5)                                              # (the administrator's answer is old again)
            self.assertEqual(self.gate(ADMIN).status, 204)
            self.assertEqual(self.fake.member_ids.count(ADMIN), 2)
        finally:
            for account in extras:
                del NAMES[account]

    def test_a_share_that_is_used_up_is_one_line_in_the_log_and_names_no_account(self):
        for account in (PLAIN, TARGET, AWAY, MOD):
            self.gate(account)
        text = self.rig.log_text.getvalue()
        self.assertEqual(text.count("admin_door_lookup_budget_empty"), 1)
        for account in EVERY_ID:
            self.assertNotIn(account, text)


class TheRequestsThatAreNotWhatTheDoorIsFor(DoorTest):
    options = dict(handshake_seconds=1.5)

    def head(self, account=None, length=100, extra=()):
        lines = ["POST /hub/admin/api/ban HTTP/1.1", "Host: chat.example.org", "X-Client-Address: 198.51.100.61", "Sec-Fetch-Site: same-origin", "Origin: " + hc.SITE,
                 "Content-Type: application/json", "Content-Length: %d" % length, "X-Ants-Csrf: " + (self.token(account) if account else "0" * 64)]
        if account:
            lines.append("%s: %s" % self.login_header(account))
        lines += ["%s: %s" % kv for kv in extra]
        return ("\r\n".join(lines) + "\r\n\r\n").encode("latin-1")

    def answer_at(self, sock, seconds):
        sock.settimeout(seconds)
        try:
            return sock.recv(4096)
        except socket.timeout:
            return None

    def test_a_gate_asked_with_no_address_of_the_visitor_is_a_400_and_one_line_of_the_log(self):
        for _ in range(3):
            r = self.call("GET", "/hub/admin/gate", OWNER, address="")
            self.assertEqual((r.status, r.get("x-ants-admin-pass")), (400, None))
        r = hc.request(self.rig.port, "GET", "/hub/admin/gate", [("X-Client-Address", "not an address"), self.login_header(OWNER)])
        self.assertEqual(r.status, 400)
        self.assertEqual(self.rig.log_text.getvalue().count("admin_gate_asked_without_x_client_address"), 1)
        for _ in range(70):
            self.call("GET", "/hub/admin/gate", None, address="")                      # (a crowd with no address cannot use up a bucket that the operator needs)
        self.assertEqual(self.gate(OWNER, address="203.0.113.5").status, 204)
        self.assertEqual(self.get("me", OWNER).status, 200)

    def test_a_client_that_is_not_logged_in_and_promises_a_body_is_told_no_at_once_and_never_waited_for(self):
        with socket.create_connection(("127.0.0.1", self.rig.port), timeout=5) as sock:
            sock.sendall(self.head(None))
            data = self.answer_at(sock, 1.0)
            self.assertIsNotNone(data, "no answer within a second: the hub waited for the body")
            self.assertTrue(data.startswith(b"HTTP/1.1 401 "), data[:40])

    def test_a_client_that_is_let_in_and_promises_a_body_holds_its_place_until_the_body_is_read_or_given_up(self):
        base = self.rig.call(lambda: self.rig.hub.pending)
        with socket.create_connection(("127.0.0.1", self.rig.port), timeout=5) as sock:
            sock.sendall(self.head(ADMIN))
            time.sleep(0.5)
            self.assertEqual(self.rig.call(lambda: self.rig.hub.pending), base + 1, "the place of the connection is kept while it waits for the body")
            data = self.answer_at(sock, 5.0)
            self.assertTrue(data and data.startswith(b"HTTP/1.1 400 "), data)
        time.sleep(0.2)
        self.assertEqual(self.rig.call(lambda: self.rig.hub.pending), base)

    def test_an_empty_query_is_a_query_too(self):
        for path in ("/hub/admin/gate?", "/hub/admin/api/me?", "/hub/admin/api/ban?"):
            r = self.call("GET", path, OWNER)
            self.assertEqual(r.status, 404, path)

    def test_a_key_that_comes_twice_in_the_body_is_refused(self):
        r = self.call("POST", "/hub/admin/api/ban", ADMIN, raw=b'{"account":"%s","account":"%s"}' % (NAMES[TARGET].encode(), NAMES[PLAIN].encode()))
        self.assertEqual(r.status, 400)
        self.assertEqual(self.modlog(), [])

    def test_the_pass_and_the_token_have_keys_of_their_own(self):
        raw = SECRET.encode()
        message = b"csrf1\0\0" + self.ref(ADMIN).encode()
        token = self.token(ADMIN)                                    # (the test login has no session: the key of the login is empty)
        self.assertNotEqual(token, hmac.new(raw, message, hashlib.sha256).hexdigest(), "not the secret itself")
        self.assertNotEqual(token, hmac.new(hmac.new(raw, b"ants-admin-pass-v1", hashlib.sha256).digest(), message, hashlib.sha256).hexdigest(), "not the pass's key")
        self.assertEqual(token, hmac.new(hmac.new(raw, b"ants-admin-csrf-v1", hashlib.sha256).digest(), message, hashlib.sha256).hexdigest())
        header = self.gate(ADMIN).get("x-ants-admin-pass")
        body = header.rsplit(".", 1)[0]
        self.assertNotEqual(header.rsplit(".", 1)[1], hmac.new(raw, body.encode(), hashlib.sha256).hexdigest(), "the pass is not signed with the secret itself")

    def test_the_keys_are_the_raw_digests_of_the_secret_as_the_key_and_the_label_as_the_message_known_answer(self):
        sample_pass_key = hmac.new(SAMPLE_SECRET.encode("ascii"), b"ants-admin-pass-v1", hashlib.sha256).digest()       # (the document's known answer, for the document's made-up key)
        sample_csrf_key = hmac.new(SAMPLE_SECRET.encode("ascii"), b"ants-admin-csrf-v1", hashlib.sha256).digest()
        self.assertEqual(sample_pass_key.hex(), "13be5dab7dcbeb06e979f0fa5e5f817d178fa6b40da7c92bc02588e80ae3dd25")
        self.assertEqual(sample_csrf_key.hex(), "03a2b6b94fabcf8ae018852e8e6c0b1ec691375286a0263168238796556e3018")
        pass_key = hmac.new(SECRET.encode("ascii"), b"ants-admin-pass-v1", hashlib.sha256).digest()                 # (the same rule for the key that this hub runs with)
        csrf_key = hmac.new(SECRET.encode("ascii"), b"ants-admin-csrf-v1", hashlib.sha256).digest()
        self.assertEqual(len(pass_key), 32)
        header = self.gate(ADMIN).get("x-ants-admin-pass")
        body, mac = header.rsplit(".", 1)
        self.assertEqual(mac, hmac.new(pass_key, body.encode("ascii"), hashlib.sha256).hexdigest(), "the hub signs with exactly this key")
        self.assertEqual(self.token(ADMIN), hmac.new(csrf_key, b"csrf1\0\0" + self.ref(ADMIN).encode(), hashlib.sha256).hexdigest())

    def test_a_name_that_is_the_shape_of_an_id_in_an_old_row_is_not_told(self):
        planted = "12345678901234567"
        self.rig.call(self.rig.store.ban, AWAY, "terminal", planted)
        self.rig.call(self.rig.store.mute, AWAY, 5, "terminal", planted)
        for name, field in (("bans", "bans"), ("mutes", "mutes")):
            r = self.get(name)
            self.assertNotIn(planted, r.text())
            self.assertEqual([row["name"] for row in r.json()[field]], [""])
        self.rig.call(self.rig.store.ban, PLAIN, "terminal", "plain.name")
        self.assertIn("pat.plain", self.get("bans").text(), "a name that is a name is told (the profile's)")


class TheGate(DoorTest):
    options = dict(admin_burst=6, admin_refill_seconds=60.0)

    def test_a_good_gate_is_a_204_with_no_body_and_no_length_and_no_cors(self):
        r = self.gate(OWNER, address="203.0.113.1")
        self.assertEqual((r.status, r.body), (204, b""))
        self.assertIsNone(r.get("content-length"))
        self.assertIsNone(r.get("content-type"))
        self.assertEqual(r.get("cache-control"), "no-store")
        for status_account in (None, MOD, OWNER):
            r = self.gate(status_account, extra=[("Origin", "https://other.example")], address="203.0.113.2")
            self.assertFalse([n for n, _ in r.headers if n.startswith("access-control")], r.headers)

    def test_only_a_get_with_no_body_and_no_query_is_answered(self):
        for method in ("POST", "PUT", "DELETE", "OPTIONS", "PATCH"):
            r = self.call(method, "/hub/admin/gate", OWNER, address="203.0.113.3")
            self.assertEqual(r.status, 405, method)
            self.assertIsNone(r.get("x-ants-admin-pass"))
            self.assertFalse([n for n, _ in r.headers if n.startswith("access-control")])
        r = self.call("GET", "/hub/admin/gate", OWNER, raw=b"{}", address="203.0.113.4", site=None)
        self.assertEqual(r.status, 400, "a body is refused")
        r = self.call("GET", "/hub/admin/gate", OWNER, extra=[("Transfer-Encoding", "chunked")], address="203.0.113.5")
        self.assertEqual(r.status, 400)
        r = self.call("GET", "/hub/admin/gate?x=1", OWNER, address="203.0.113.6")
        self.assertEqual(r.status, 404)
        r = self.call("GET", "/hub/admin/gate/", OWNER, address="203.0.113.7")
        self.assertEqual(r.status, 404)

    def test_each_address_has_a_limit_and_another_address_is_not_held_by_it(self):
        statuses = [self.gate(OWNER, address="203.0.113.50").status for _ in range(9)]
        self.assertEqual(statuses[:6], [204] * 6)
        self.assertEqual(set(statuses[6:]), {429})
        self.assertEqual(self.gate(OWNER, address="203.0.113.51").status, 204)
        r = self.gate(OWNER, address="203.0.113.50")
        self.assertEqual((r.status, r.get("retry-after")), (429, "5"))

    def test_the_limit_is_asked_before_discord(self):
        before = len(self.fake.member_ids)
        for _ in range(12):
            self.gate(PLAIN, address="203.0.113.60")
        self.assertLessEqual(len(self.fake.member_ids) - before, 6)

    def test_other_paths_under_the_door_are_not_found(self):
        for path in ("/hub/admin", "/hub/admin/", "/hub/admin/api", "/hub/admin/api/", "/hub/admin/api/nothing", "/hub/admin/api/me/x", "/hub/admin/gate2", "/hub/admin/API/me"):
            r = self.call("GET", path, OWNER, address="203.0.113.70")
            self.assertEqual(r.status, 404, path)


class TheFences(DoorTest):
    options = dict(handshake_seconds=1.0)
    BODY = {"account": NAMES[TARGET]}

    def untouched(self):
        store = self.rig.store
        self.assertFalse(self.rig.call(store.is_banned, TARGET))
        self.assertEqual(self.modlog(), [])

    def test_a_good_change_goes_through_and_is_the_baseline_of_the_refusals_below(self):
        self.assertEqual(self.post("ban", self.BODY).status, 200)
        self.assertTrue(self.rig.call(self.rig.store.is_banned, TARGET))

    def test_a_change_without_the_token_or_with_a_wrong_one_is_refused(self):
        self.get("me")
        for csrf in (None, "", "0" * 64, "x", self.token(ADMIN)[:-1], self.token(ADMIN).upper(), self.token(ADMIN) + "0"):
            r = self.post("ban", self.BODY, csrf=csrf)
            self.assertEqual((r.status, r.json()), (403, {"error": "csrf"}), csrf)
        self.untouched()

    def test_the_token_of_another_login_is_no_token(self):
        self.assertEqual(self.post("ban", self.BODY, csrf=self.token(ADMIN2)).status, 403)
        self.assertEqual(self.post("ban", self.BODY, csrf=self.token(OWNER)).status, 403)
        self.untouched()

    def test_a_token_is_not_an_answer_to_a_page_of_another_site(self):
        for site in ("cross-site", "same-site", "none", None, "", "Same-Origin"):
            r = self.post("ban", self.BODY, site=site)
            self.assertEqual((r.status, r.json()), (403, {"error": "origin"}), site)
            r = self.get("me", site=site)
            self.assertEqual(r.status, 403, site)
        self.untouched()

    def test_the_origin_of_a_post_must_be_the_sites_and_a_get_may_have_none(self):
        for origin in (None, "", "https://other.example", "https://chat.example.org.evil.example", "http://chat.example.org", "null", "https://chat.example.org/"):
            r = self.post("ban", self.BODY, origin=origin)
            self.assertEqual(r.status, 403, origin)
        self.assertEqual(self.get("me", origin=None).status, 200)
        self.assertEqual(self.get("me", origin="https://other.example").status, 403)
        self.untouched()

    def test_a_change_must_say_that_it_is_json(self):
        for ctype in (None, "", "text/plain", "application/x-www-form-urlencoded", "multipart/form-data; boundary=x", "text/json", "application/jsonp", "application/json2"):
            r = self.post("ban", self.BODY, ctype=ctype)
            self.assertEqual((r.status, r.json()), (415, {"error": "type"}), ctype)
        self.assertEqual(self.post("unban", self.BODY, ctype="Application/JSON; charset=utf-8").status, 409, "the type is read as a type is (and the change that follows was not asked for: nobody is banned)")
        self.untouched()

    def test_a_header_that_comes_twice_is_refused(self):
        for name, value in (("Content-Type", "application/json"), ("X-Ants-Csrf", self.token(ADMIN)), ("Content-Length", "20")):
            r = self.post("ban", self.BODY, extra=[(name, value)])
            self.assertEqual(r.status, 400, name)
        self.untouched()

    def test_the_wrong_method_is_a_405(self):
        self.assertEqual(self.call("POST", "/hub/admin/api/me", ADMIN, body={}).status, 405)
        self.assertEqual(self.call("GET", "/hub/admin/api/ban", ADMIN).status, 405)
        self.assertEqual(self.call("PUT", "/hub/admin/api/ban", ADMIN, body=self.BODY).status, 405)
        self.untouched()

    def test_a_body_that_is_not_what_the_change_takes_is_a_400(self):
        bad = [b"", b"{", b"[]", b"null", b'"x"', b"{}", b'{"account":1}', b'{"account":""}', b'{"account":"a b"}', b'{"account":"%s","extra":1}' % NAMES[TARGET].encode(), b'{"account":null}',
               b'{"account":"' + b"a" * 41 + b'"}', b'{"account":"%s","account":"%s"}' % (NAMES[TARGET].encode(), b"x" * 50), b"\xff\xfe", b'{"account":NaN}', b"[" * 400]
        for raw in bad:
            r = self.call("POST", "/hub/admin/api/ban", ADMIN, raw=raw)
            self.assertIn(r.status, (400,), raw[:30])
        self.untouched()

    def test_the_size_of_a_body_is_bounded_and_a_missing_length_is_refused(self):
        r = self.call("POST", "/hub/admin/api/ban", ADMIN, raw=b'{"account":"' + b"a" * 600 + b'"}')
        self.assertEqual(r.status, 413)
        r = self.call("POST", "/hub/admin/api/ban", ADMIN, raw=b'{"account":"x"}', extra=[("Transfer-Encoding", "chunked")])
        self.assertEqual(r.status, 400)
        r = hc.request(self.rig.port, "POST", "/hub/admin/api/ban", [("Sec-Fetch-Site", "same-origin"), ("Origin", hc.SITE), ("Content-Type", "application/json"), ("X-Ants-Csrf", self.token(ADMIN)),
                                                                     self.login_header(ADMIN)], body=b'{"account":"x"}', send_length=False)
        self.assertEqual(r.status, 400)
        r = self.call("GET", "/hub/admin/api/me", ADMIN, raw=b"{}")
        self.assertEqual(r.status, 400, "a GET with a body")
        r = self.call("GET", "/hub/admin/api/me", ADMIN, extra=[("Content-Length", "0")], raw=None)
        self.assertEqual(r.status, 200, "and one that says 0 is a GET")
        self.untouched()

    def test_a_body_that_is_shorter_than_it_says_is_a_400_and_costs_nothing_else(self):
        r = hc.request(self.rig.port, "POST", "/hub/admin/api/ban", [("Sec-Fetch-Site", "same-origin"), ("Origin", hc.SITE), ("Content-Type", "application/json"), ("X-Ants-Csrf", self.token(ADMIN)),
                                                                     self.login_header(ADMIN), ("Content-Length", "400")], body=b'{"account":"x"}', send_length=False, timeout=15.0)
        self.assertEqual(r.status, 400)
        self.assertEqual(self.get("me").status, 200)

    def test_a_body_that_is_not_the_answer_is_never_written_back(self):
        r = self.call("POST", "/hub/admin/api/ban", ADMIN, raw=b"HTTP/1.1 200 OK\r\nSet-Cookie: x=1\r\n\r\n")
        self.assertEqual(r.status, 400)
        self.assertIsNone(r.get("set-cookie"))
        self.assertNotIn(b"Set-Cookie", r.body)

    def test_the_checks_come_before_the_login_and_the_login_before_the_work(self):
        r = self.post("ban", self.BODY, None, csrf=None)
        self.assertEqual(r.status, 401)
        r = self.post("ban", self.BODY, PLAIN, csrf="x")
        self.assertEqual(r.status, 403)
        self.assertEqual(r.json(), {"error": "admin"})
        self.untouched()

    def test_every_answer_of_the_door_says_that_it_is_not_to_be_kept_or_sniffed_and_none_is_cors(self):
        for r in (self.get("me"), self.get("bans"), self.post("ban", self.BODY), self.get("me", None), self.post("ban", self.BODY, csrf=None)):
            self.assertEqual(r.get("cache-control"), "no-store")
            self.assertEqual(r.get("x-content-type-options"), "nosniff")
            self.assertFalse([n for n, _ in r.headers if n.startswith("access-control")])


class TheChanges(DoorTest):
    def row(self, action):
        return [r for r in self.modlog() if r.action == action]

    def line(self, account=TARGET, text="canary line"):
        return self.rig.call(self.rig.store.add_line, account, NAMES[account], text, 0).id

    def test_me_says_the_name_the_role_and_the_token_and_marks_the_name_as_typed(self):
        r = self.get("me")
        body = r.json()
        self.assertEqual((body["name"], body["role"]), (NAMES[ADMIN], "admin"))
        self.assertRegex(body["csrf"], r"[0-9a-f]{64}\Z")
        self.assertEqual((body["untrusted"], body["untrusted_fields"]), (True, ["name"]))
        self.assertEqual(self.get("me", OWNER).json()["role"], "owner")
        self.assertNotEqual(self.token(ADMIN), self.token(OWNER))

    def test_ban_closes_the_sockets_lists_and_writes_the_log_and_unban_undoes_it(self):
        target = self.socket(TARGET)
        r = self.post("ban", {"account": NAMES[TARGET]})
        self.assertEqual((r.status, r.json()), (200, {"ok": True}))
        self.assertTrue(self.rig.call(self.rig.store.is_banned, TARGET))
        self.assertEqual(target.recv_json()["t"], "bye")
        rows = self.get("bans").json()
        self.assertEqual([(b["ref"], b["name"], b["by"], b["by_name"]) for b in rows["bans"]], [(self.ref(TARGET), NAMES[TARGET], self.ref(ADMIN), NAMES[ADMIN])])
        self.assertEqual((rows["untrusted"], rows["untrusted_fields"]), (True, ["name", "by_name"]))
        log = self.row("ban")
        self.assertEqual([(a.actor, a.target, a.detail) for a in log], [(self.ref(ADMIN), self.ref(TARGET), "")])
        self.assertEqual(self.post("ban", {"account": NAMES[TARGET]}).status, 409, "banned already")
        r = self.post("unban", {"account": self.ref(TARGET)})
        self.assertEqual(r.status, 200)
        self.assertFalse(self.rig.call(self.rig.store.is_banned, TARGET))
        self.assertEqual([(a.actor, a.target) for a in self.row("unban")], [(self.ref(ADMIN), self.ref(TARGET))])
        self.assertEqual(self.post("unban", {"account": self.ref(TARGET)}).status, 409, "not banned")
        self.assertEqual(self.get("bans").json()["bans"], [])

    def test_mute_takes_minutes_one_to_a_week_and_unmute_lifts_it(self):
        r = self.post("mute", {"account": NAMES[TARGET], "minutes": 30})
        self.assertEqual(r.status, 200)
        until = r.json()["until"]
        self.assertAlmostEqual(until, time.time() + 1800, delta=5)
        rows = self.get("mutes").json()
        self.assertEqual([(m["ref"], m["name"], m["until"], m["by"], m["by_name"]) for m in rows["mutes"]], [(self.ref(TARGET), NAMES[TARGET], until, self.ref(ADMIN), NAMES[ADMIN])])
        self.assertEqual(rows["untrusted_fields"], ["name", "by_name"])
        self.assertEqual([(a.actor, a.target, a.detail) for a in self.row("mute")], [(self.ref(ADMIN), self.ref(TARGET), "30")])
        self.assertEqual(self.post("mute", {"account": NAMES[TARGET], "minutes": 10080}).status, 200)
        self.assertEqual(self.post("mute", {"account": NAMES[TARGET], "minutes": 1}).status, 200)
        for minutes in (0, -1, 10081, 525600, 1.5, "30", True, None, [30]):
            r = self.post("mute", {"account": NAMES[TARGET], "minutes": minutes})
            self.assertEqual(r.status, 400, minutes)
        self.assertEqual(self.post("mute", {"account": NAMES[TARGET]}).status, 400)
        self.assertEqual(self.post("unmute", {"account": self.ref(TARGET)}).status, 200)
        self.assertEqual(self.rig.call(self.rig.store.muted_until, TARGET), 0)
        self.assertEqual([a.actor for a in self.row("unmute")], [self.ref(ADMIN)])
        self.assertEqual(self.post("unmute", {"account": self.ref(TARGET)}).status, 409, "not muted")
        self.assertEqual(self.get("mutes").json()["mutes"], [])

    def test_a_muted_account_cannot_write_and_a_banned_one_cannot_either(self):
        self.post("mute", {"account": NAMES[TARGET], "minutes": 5})
        target = self.socket(TARGET)
        target.say("hello")
        self.assertEqual(target.recv_reply(), {"t": "err", "code": "muted"})

    def test_a_reference_of_digits_only_is_still_a_reference_in_the_lists(self):
        digits = "1234567890123456"                                  # (a reference that happens to be all digits: one in about two thousand is)
        self.rig.call(self.rig.store.ban, TARGET, digits, NAMES[TARGET])
        self.rig.call(self.rig.store.mute, ADMIN2, 5, digits, NAMES[ADMIN2])
        self.assertEqual([(b["by"], b["by_name"]) for b in self.get("bans").json()["bans"]], [(digits, "")])
        self.assertEqual([(b["by"], b["by_name"]) for b in self.get("mutes").json()["mutes"]], [(digits, "")])
        self.rig.call(self.rig.store.ban, ADMIN2, "terminal", NAMES[ADMIN2])
        self.assertEqual(sorted((b["by"], b["by_name"]) for b in self.get("bans").json()["bans"]), [(digits, ""), ("terminal", "")], "the command line is no one's username")

    def test_nopic_and_pic_block_and_lift_a_picture_and_the_list_shows_the_block(self):
        self.assertEqual(self.post("nopic", {"account": NAMES[TARGET]}).status, 200)
        rows = self.get("hidden").json()
        self.assertEqual(rows["hidden"], [{"ref": self.ref(TARGET), "name": NAMES[TARGET]}])
        self.assertEqual(rows["untrusted_fields"], ["name"])
        self.assertEqual([(a.actor, a.target) for a in self.row("nopic")], [(self.ref(ADMIN), self.ref(TARGET))])
        self.assertEqual(self.post("nopic", {"account": NAMES[TARGET]}).status, 409, "blocked already")
        self.assertEqual(self.post("pic", {"account": self.ref(TARGET)}).status, 200)
        self.assertEqual(self.get("hidden").json()["hidden"], [])
        self.assertEqual([(a.actor, a.target) for a in self.row("pic")], [(self.ref(ADMIN), self.ref(TARGET))])
        self.assertEqual(self.post("pic", {"account": self.ref(TARGET)}).status, 409, "not blocked")

    def test_a_block_that_outlived_its_account_is_lifted_by_its_reference(self):
        self.post("nopic", {"account": NAMES[TARGET]})
        self.rig.call(self.rig.store.forget, TARGET)
        self.assertEqual([h["ref"] for h in self.get("hidden").json()["hidden"]], [self.ref(TARGET)])
        self.assertEqual(self.post("pic", {"account": self.ref(TARGET)}).status, 200)
        self.assertEqual(self.get("hidden").json()["hidden"], [])

    def test_del_deletes_a_line_tells_the_screens_and_writes_the_log(self):
        watcher = self.socket(PLAIN)
        line_id = self.line()
        self.rig.call(setattr, self.rig.hub, "next_deletions", float("inf"))        # (the clock's own look at the deletions never comes: the screens are told by the change itself)
        r = self.post("del", {"line": line_id})
        self.assertEqual((r.status, r.json()), (200, {"ok": True}))
        deadline = time.monotonic() + 5
        seen = []
        while time.monotonic() < deadline and {"t": "del", "id": line_id} not in seen:
            seen += watcher.drain(0.3)
        self.assertIn({"t": "del", "id": line_id}, seen)
        self.assertEqual([(a.actor, a.target, a.detail) for a in self.row("del")], [(self.ref(ADMIN), self.ref(TARGET), str(line_id))])
        self.assertEqual(self.post("del", {"line": line_id}).status, 404, "deleted already: there is no such line")
        self.assertEqual(self.post("del", {"line": 99999}).status, 404, "no such line")
        for line in (0, -1, 2 ** 62, "1", 1.0, True, None):
            self.assertEqual(self.post("del", {"line": line}).status, 400, line)
        self.assertEqual(self.post("del", {"line": line_id, "account": "x"}).status, 400)

    def test_an_account_is_a_username_or_a_reference_and_never_an_id_or_a_display_name(self):
        for who in (TARGET, "@" + TARGET, "9" * 17, "1", "0123", "x", "UPPER.CASE", "a" * 40, self.ref(AWAY)):
            r = self.post("ban", {"account": who})
            self.assertEqual((r.status, r.json()), (404, {"error": "who"}), who)
        self.assertEqual(self.post("ban", {"account": "not a name"}).status, 400)
        self.assertEqual(self.modlog(), [])
        self.assertEqual(self.post("ban", {"account": "@" + NAMES[TARGET]}).status, 200, "@name is a username too")

    def test_no_command_takes_an_id(self):
        for who in (TARGET, "@" + TARGET):
            for name, body in (("ban", {}), ("unban", {}), ("mute", {"minutes": 5}), ("unmute", {}), ("nopic", {}), ("pic", {})):
                r = self.post(name, dict(body, account=who))
                self.assertEqual((r.status, r.json()), (404, {"error": "who"}), (name, who))
        self.assertEqual(self.modlog(), [])

    def test_a_username_of_digits_is_written_with_an_at_sign_and_is_not_an_id(self):
        self.rig.call(self.rig.store.set_profile, AWAY, "123456789012", "", 1000)
        self.assertEqual(self.post("ban", {"account": "123456789012"}).status, 404)
        self.assertEqual(self.post("ban", {"account": "@123456789012"}).status, 200)
        self.assertTrue(self.rig.call(self.rig.store.is_banned, AWAY))

    def test_an_account_that_the_hub_has_not_seen_is_nobody(self):
        self.assertEqual(self.post("mute", {"account": "nobody.here", "minutes": 5}).status, 404)
        self.assertEqual(self.post("nopic", {"account": "nobody.here"}).status, 404)
        self.assertEqual(self.modlog(), [])

    def test_the_powers_the_table_of_the_chat_and_the_operator_above_it(self):
        s = self.rig.store
        self.assertEqual(self.post("ban", {"account": NAMES[ADMIN2]}).status, 403, "an administrator has no power over an administrator")
        self.assertEqual(self.post("ban", {"account": NAMES[ADMIN]}).status, 403, "nor over themselves")
        self.assertEqual(self.post("mute", {"account": NAMES[OWNER], "minutes": 5}).status, 403, "nor over the operator's account")
        self.assertEqual(self.post("nopic", {"account": NAMES[OWNER]}).status, 403)
        self.assertEqual(self.post("ban", {"account": NAMES[OWNER]}, OWNER).status, 403, "and the operator's account does not act on itself")
        self.assertEqual(self.post("mute", {"account": NAMES[ADMIN], "minutes": 5}, OWNER).status, 200, "the operator's account is above an administrator")
        self.assertEqual(self.post("ban", {"account": NAMES[ADMIN2]}, OWNER).status, 200)
        self.assertEqual(self.post("ban", {"account": NAMES[MOD]}).status, 200, "an administrator may act on a moderator")
        self.assertEqual(self.post("ban", {"account": NAMES[PLAIN]}).status, 200)
        self.assertFalse(self.rig.call(s.is_banned, OWNER))
        self.assertTrue(self.rig.call(s.is_banned, ADMIN2))

    def test_nobody_acts_on_the_operators_account_whatever_the_roles_say_of_it(self):
        self.token(ADMIN)
        roles = self.rig.hub.chat.roles
        self.rig.call(setattr, roles, "owner", "")                    # (the roles do not know the account as the operator's now: only the setting does, and Discord says it holds no role)
        for name, body in (("ban", {}), ("mute", {"minutes": 5}), ("nopic", {})):
            r = self.post(name, dict(body, account=NAMES[OWNER]))
            self.assertEqual((r.status, r.json()), (403, {"error": "power"}), name)
        self.assertEqual(self.modlog(), [])

    def test_a_line_of_an_administrator_is_theirs_to_delete_and_not_another_administrators(self):
        line_id = self.line(ADMIN2)
        self.assertEqual(self.post("del", {"line": line_id}).status, 403)
        self.assertEqual(self.post("del", {"line": line_id}, OWNER).status, 200)
        mine = self.line(ADMIN)
        self.assertEqual(self.post("del", {"line": mine}).status, 200, "one's own line is one's own to delete")
        owners = self.line(OWNER)
        self.assertEqual(self.post("del", {"line": owners}).status, 403)
        theirs = self.line(OWNER)
        self.assertEqual(self.post("del", {"line": theirs}, OWNER).status, 200)

    def test_a_line_of_the_hubs_own_may_be_deleted(self):
        line_id = self.rig.call(self.rig.store.add_line, "", "system", "canary notice", False).id
        self.assertEqual(self.post("del", {"line": line_id}).status, 200)
        self.assertEqual([(a.target, a.detail) for a in self.row("del")], [("", str(line_id))])

    def test_the_operators_account_acts_on_a_person_whose_role_is_confirmed_and_on_nobody_else(self):
        self.fake.member_mode = "status:500"
        r = self.post("ban", {"account": NAMES[TARGET]}, OWNER)
        self.assertEqual(r.status, 503, "the person acted on needs a fresh answer, whoever acts")
        self.fake.member_mode = "ok"
        self.assertEqual(self.post("ban", {"account": NAMES[TARGET]}, OWNER).status, 200)

    def test_an_administrator_whose_role_went_is_told_so_by_the_fresh_answer(self):
        self.token(ADMIN)
        self.fake.members[ADMIN] = []
        time.sleep(1.8)
        r = self.post("ban", {"account": NAMES[TARGET]})
        self.assertEqual(r.status, 403)
        self.assertFalse(self.rig.call(self.rig.store.is_banned, TARGET))

    def test_the_log_is_thirty_days_newest_first_with_usernames_where_there_is_a_profile_and_marked_as_typed(self):
        self.post("mute", {"account": NAMES[TARGET], "minutes": 5})
        self.post("ban", {"account": NAMES[TARGET]})
        self.rig.call(self.rig.store.mute, AWAY, 5, "terminal", "away.person", (("terminal", "mute", self.ref(AWAY), "5")))
        rows = self.get("modlog").json()
        self.assertEqual((rows["untrusted"], rows["untrusted_fields"]), (True, ["actor_name", "target_name"]))
        got = [(r["action"], r["actor"], r["actor_name"], r["target"], r["target_name"], r["detail"]) for r in rows["modlog"]]
        self.assertEqual(got, [("mute", "terminal", "", self.ref(AWAY), "", "5"),
                               ("ban", self.ref(ADMIN), NAMES[ADMIN], self.ref(TARGET), NAMES[TARGET], ""),
                               ("mute", self.ref(ADMIN), NAMES[ADMIN], self.ref(TARGET), NAMES[TARGET], "5")])
        self.assertEqual(sorted(rows["modlog"][0]), ["action", "actor", "actor_name", "at", "detail", "target", "target_name"])
        old = self.rig.call(self.rig.store.actions, 0, 5)[-1]
        self.assertEqual(old.action, "mute")

    def test_the_log_leaves_out_what_is_older_than_thirty_days(self):
        self.rig.call(setattr, self.rig.store, "clock", lambda: time.time() - 40 * 86400)
        self.rig.call(self.rig.store.mute, TARGET, 5, "terminal", "x", ("terminal", "mute", self.ref(TARGET), "5"))
        self.rig.call(setattr, self.rig.store, "clock", time.time)
        self.rig.call(self.rig.store.mute, TARGET, 6, "terminal", "x", ("terminal", "mute", self.ref(TARGET), "6"))
        details = [r["detail"] for r in self.get("modlog").json()["modlog"]]
        self.assertEqual(details, ["6"])

    def test_the_status_is_numbers_for_the_store_the_health_of_the_roles_the_last_picture_and_the_program(self):
        self.line()
        body = self.get("status").json()
        self.assertEqual(sorted(body), ["health", "last_picture", "store", "version"])
        store = body["store"]
        self.assertEqual(sorted(store), ["blocked", "bytes", "free", "limit", "lines", "log", "off", "oldest", "picture_bytes", "picture_bytes_max", "pictures", "pictures_max"])
        self.assertEqual((store["limit"], store["lines"], store["pictures"], store["off"], store["blocked"]), (self.rig.limits.db_max_bytes, 1, 0, 0, 0))
        self.assertTrue(all(type(store[k]) is int and store[k] >= 0 for k in store if k != "oldest"))
        self.assertEqual(store["pictures_max"], self.rig.limits.pictures_rows_max)
        self.assertEqual(store["picture_bytes_max"], self.rig.limits.pictures_bytes_max)
        self.assertAlmostEqual(store["oldest"], time.time(), delta=60)
        self.assertEqual(body["health"], {"roles": "ok", "roles_known": body["health"]["roles_known"], "kind": None, "status": None, "retry": None})
        self.assertEqual(body["last_picture"], None, "no picture was fetched since the hub started")
        self.assertRegex(body["version"]["code"], r"^[0-9a-f]{8}\Z")
        self.assertAlmostEqual(body["version"]["started"], time.time(), delta=300)
        self.assertEqual(sorted(body["version"]), ["code", "started"])
        self.fake.member_mode = "status:500"
        self.gate(PLAIN)
        time.sleep(0.4)
        self.assertEqual(self.get("status", OWNER).json()["health"]["roles"], "down")

    def test_the_status_says_which_refusal_and_when_the_hub_asks_again_while_discord_refuses(self):
        self.fake.member_mode = "err:401"
        self.gate(PLAIN)
        time.sleep(0.4)
        health = self.get("status", OWNER).json()["health"]
        self.assertEqual((health["roles"], health["kind"], health["status"]), ("refused", "token", 401))
        self.assertTrue(0 < health["retry"] <= 61, health)
        self.assertEqual(sorted(health), ["kind", "retry", "roles", "roles_known", "status"])
        roles = self.rig.hub.chat.roles
        for kind, status in (("access", 403), ("guild", 404)):          # (the other two refusals of Discord, said the way the check says them)
            self.rig.call(lambda: roles.refused(kind, status, roles.clock()))
            health = self.get("status", OWNER).json()["health"]
            self.assertEqual((health["roles"], health["kind"], health["status"]), ("refused", kind, status))
        self.rig.call(setattr, roles, "pause_until", float("-inf"))
        self.assertEqual(self.get("status", OWNER).json()["health"]["kind"], None, "no refusal is told once the hub asks again")

    def test_the_name_of_who_did_it_is_not_told_when_it_is_as_long_as_an_id_and_the_program_is_named_by_its_files(self):
        self.rig.call(self.rig.store.set_profile, ADMIN2, "1234567890123456789", "", 1000)           # (a username of digits, as long as an id: such a name is never told, in any list)
        self.rig.call(self.rig.store.ban, TARGET, self.ref(ADMIN2), NAMES[TARGET])
        self.rig.call(self.rig.store.mute, PLAIN, 5, self.ref(ADMIN2), NAMES[PLAIN])
        self.assertEqual([b["by_name"] for b in self.get("bans").json()["bans"]], [""])
        self.assertEqual([m["by_name"] for m in self.get("mutes").json()["mutes"]], [""])
        import tempfile
        from unittest import mock
        with tempfile.TemporaryDirectory() as one, tempfile.TemporaryDirectory() as two:
            for folder, text in ((one, "a = 1\n"), (two, "a = 2\n")):
                with open(os.path.join(folder, "x.py"), "w") as f:
                    f.write(text)
            stamps = []
            for folder in (one, two, one):
                with mock.patch.object(admindoor, "__file__", os.path.join(folder, "admindoor.py")):
                    stamps.append(admindoor.code_stamp())
            self.assertEqual(stamps[0], stamps[2], "the same files, the same eight characters")
            self.assertNotEqual(stamps[0], stamps[1], "another file, another eight")
            self.assertTrue(all(re.match(r"[0-9a-f]{8}\Z", x) for x in stamps), stamps)

    def test_the_status_says_the_last_picture_fetch_by_its_kind_and_its_age_and_nothing_else(self):
        from hub import pictures as picmod
        hub = self.rig.hub
        pictures = picmod.Pictures(hub.store, None, hub.limits, hub.log, hub.clock)                # (this rig has no login with Discord, which is what makes the pictures: the same object, by hand)
        self.rig.call(setattr, hub.chat, "pictures", pictures)
        self.rig.call(pictures.tell, "down")
        self.rig.call(pictures.tell, "ok")
        last = self.get("status").json()["last_picture"]
        self.assertEqual(sorted(last), ["age", "kind"])
        self.assertEqual(last["kind"], "ok")
        self.assertTrue(0 <= last["age"] < 5, last)
        self.rig.call(setattr, pictures, "last", ("down", pictures.clock() - 90))                # (a fetch that ended a minute and a half ago)
        last = self.get("status").json()["last_picture"]
        self.assertEqual(last["kind"], "down")
        self.assertTrue(90 <= last["age"] < 95, last)
        self.assertNotIn(self.ref(ADMIN), json.dumps(self.get("status").json()))


class TheIdIsLikeAPassword(DoorTest):
    """Every id that is planted (the accounts' ids are the ones above) is looked for in every answer, pass, log line, row and frame."""

    def test_no_id_is_in_an_answer_a_pass_a_row_a_frame_or_the_log(self):
        sock = self.socket(PLAIN)
        target = self.socket(TARGET)
        owners = []
        for body in ({"account": NAMES[TARGET], "minutes": 5},):
            owners.append(self.post("mute", body))
        owners += [self.post("unmute", {"account": NAMES[TARGET]}), self.post("ban", {"account": NAMES[TARGET]}), self.post("unban", {"account": NAMES[TARGET]}),
                   self.post("nopic", {"account": NAMES[TARGET]}), self.post("pic", {"account": NAMES[TARGET]}), self.post("ban", {"account": TARGET}), self.post("ban", {"account": "@" + TARGET}),
                   self.post("mute", {"account": "x", "minutes": 5}), self.post("del", {"line": 5}), self.post("ban", {"account": NAMES[ADMIN2]}), self.post("ban", {"account": TARGET}, csrf="x"),
                   self.post("ban", {"account": TARGET}, PLAIN), self.post("ban", {"account": TARGET}, None), self.post("ban", {"account": TARGET}, site="cross-site"),
                   self.post("ban", {"account": TARGET}, ctype="text/plain"), self.post("ban", {"account": TARGET}, csrf=None), self.post("ban", {"account": TARGET}, origin="https://x.example"),
                   self.post("ban", {"account": TARGET, "x": TARGET})]
        owners += [self.gate(a) for a in (None, OWNER, ADMIN, MOD, PLAIN, AWAY)]
        texts = [r.headers.__repr__().encode() + r.body for r in owners]
        for name in ("me", "bans", "mutes", "hidden", "modlog", "status"):
            for account in (ADMIN, OWNER, MOD, None):
                r = self.get(name, account)
                texts.append(r.headers.__repr__().encode() + r.body)
        frames = [json.dumps(f).encode() for f in sock.drain(0.5) + target.drain(0.5)]
        texts += frames
        texts.append(self.rig.log_text.getvalue().encode())
        rows = self.rig.call(self.rig.store.actions, 0, 1000)
        texts.append(repr(rows).encode())
        header = self.gate(OWNER).get("x-ants-admin-pass")
        texts.append(b64decode(header.split(".")[1]))
        blob = b"\n".join(texts).decode("utf-8", "replace")
        for account in EVERY_ID:
            self.assertNotIn(account, blob, "an id is somewhere that an id must never be")
        self.assertNotRegex(blob, r"(?<![0-9a-f])[0-9]{15,}(?![0-9a-f])")

    def test_the_stores_modlog_and_picture_rows_hold_references_and_no_id(self):
        self.post("ban", {"account": NAMES[TARGET]})
        self.post("mute", {"account": NAMES[TARGET], "minutes": 3}, OWNER)
        self.post("nopic", {"account": NAMES[TARGET]})
        db = os.path.join(os.path.dirname(self.rig.db), "hub.db")
        import sqlite3
        c = sqlite3.connect(db)
        try:
            rows = c.execute("SELECT * FROM modlog").fetchall() + c.execute("SELECT * FROM pictures").fetchall()
        finally:
            c.close()
        text = repr(rows)
        for account in EVERY_ID:
            self.assertNotIn(account, text)

    def test_an_error_that_is_a_bug_logs_its_class_only(self):
        from unittest import mock
        with mock.patch.object(self.rig.store, "bans", side_effect=RuntimeError("canary %s" % TARGET)):
            r = self.get("bans")
        self.assertEqual(r.status, 500)
        text = self.rig.log_text.getvalue()
        self.assertIn("error kind=RuntimeError where=admin", text)
        self.assertNotIn(TARGET, text + r.text())
        self.assertNotIn("canary", text + r.text())

    def test_a_store_that_refuses_is_a_503_that_says_busy_and_nothing_else(self):
        from unittest import mock
        from hub.store import StoreError
        with mock.patch.object(self.rig.store, "bans", side_effect=StoreError("canary %s" % TARGET)):
            r = self.get("bans")
        self.assertEqual((r.status, r.json()), (503, {"error": "busy"}))
        self.assertNotIn(TARGET, self.rig.log_text.getvalue())


class TheRealLogin(unittest.TestCase):
    """The door with the Discord login's cookie: the account is the one that the cookie's login names, and a login that ends ends the door."""

    def setUp(self):
        self.fake = fd.FakeDiscord(hc.SITE + "/hub/callback", user=(BOSS, "maple.leaf"))
        self.fake.consent = False
        self.addCleanup(self.fake.close)
        self.rig = hub_rig.Rig(discord=self.fake, member_fake=self.fake, env={"ANTS_ADMIN_GATE_SECRET": SECRET}, **dict(OPTIONS, sockets_per_address=100))
        self.rig.__enter__()
        self.addCleanup(self.rig.__exit__, None, None, None)
        self.count = 0

    def visitor(self, user):
        self.fake.user = user
        self.count += 1
        v = Visitor(self.rig.port, self.fake, address="198.51.100.%d" % self.count)
        r = v.login("admin")
        self.assertEqual((r.status, r.get("location")), (302, "/admin"), "the way back from Discord is the admin page")
        return v

    def test_the_cookie_of_an_administrator_opens_the_gate_and_a_plain_member_does_not(self):
        self.fake.members = {BOSS: [fd.ROLE_ADMIN], OTHER: []}
        boss = self.visitor((BOSS, "boss.person"))
        plain = self.visitor((OTHER, "plain.person"))
        nobody = Visitor(self.rig.port, self.fake, address="198.51.100.200")
        self.assertEqual(boss.call("GET", "/hub/admin/gate").status, 204)
        self.assertEqual(plain.call("GET", "/hub/admin/gate").status, 403)
        self.assertEqual(nobody.call("GET", "/hub/admin/gate").status, 401)
        header = boss.call("GET", "/hub/admin/gate").get("x-ants-admin-pass")
        self.assertEqual(verify(header, SECRET, time.time())["name"], "boss.person")

    def test_a_login_that_ends_ends_the_door_and_the_token_is_bound_to_the_login(self):
        self.fake.members = {BOSS: [fd.ROLE_ADMIN]}
        a = self.visitor((BOSS, "boss.person"))
        b = self.visitor((BOSS, "boss.person"))
        token_a = a.call("GET", "/hub/admin/api/me").json()["csrf"]
        token_b = b.call("GET", "/hub/admin/api/me").json()["csrf"]
        self.assertNotEqual(token_a, token_b, "a token belongs to one login")
        body = json.dumps({"account": "nobody.here", "minutes": 5}).encode()
        r = hc.request(self.rig.port, "POST", "/hub/admin/api/mute", [("Cookie", a.jar.header()), ("Origin", hc.SITE), ("Sec-Fetch-Site", "same-origin"), ("Content-Type", "application/json"),
                                                                      ("X-Ants-Csrf", token_b), ("X-Client-Address", "198.51.100.150")], body=body)
        self.assertEqual((r.status, r.json()), (403, {"error": "csrf"}), "the token of another login")
        r = hc.request(self.rig.port, "POST", "/hub/admin/api/mute", [("Cookie", a.jar.header()), ("Origin", hc.SITE), ("Sec-Fetch-Site", "same-origin"), ("Content-Type", "application/json"),
                                                                      ("X-Ants-Csrf", token_a), ("X-Client-Address", "198.51.100.150")], body=body)
        self.assertEqual((r.status, r.json()), (404, {"error": "who"}), "its own token is accepted (and nobody is called that)")
        self.assertEqual(a.call("POST", "/hub/logout").status, 200)
        self.assertEqual(a.call("GET", "/hub/admin/gate", cookies=False).status, 401)
        old = [("Cookie", "__Host-ants_hub=" + "0" * 32)]
        self.assertEqual(hc.request(self.rig.port, "GET", "/hub/admin/gate", old + [("X-Client-Address", "198.51.100.151")]).status, 401)
        self.assertEqual(b.call("GET", "/hub/admin/gate").status, 204, "the other login is still good")

    def test_the_owner_of_the_hub_needs_no_role_through_the_cookie_either(self):
        owner_id = OLDOWNER
        with hub_rig.Rig(discord=self.fake, member_fake=self.fake, owner=owner_id, env={"ANTS_ADMIN_GATE_SECRET": SECRET}, **dict(OPTIONS, sockets_per_address=100)) as rig:
            self.fake.redirect = hc.SITE + "/hub/callback"
            self.fake.user = (owner_id, "olive.owner")
            v = Visitor(rig.port, self.fake, address="198.51.100.180")
            self.assertEqual(v.login("admin").status, 302)
            self.fake.member_mode = "status:500"
            r = v.call("GET", "/hub/admin/gate")
            self.assertEqual(r.status, 204)
            self.assertEqual(verify(r.get("x-ants-admin-pass"), SECRET, time.time())["role"], "owner")


class TheLoginReturnsToTheAdminPage(unittest.TestCase):
    def test_next_admin_is_one_of_the_fixed_words_and_nothing_else_is(self):
        from hub import login
        self.assertEqual(login.NEXT["admin"], "/admin")
        self.assertEqual(sorted(login.NEXT), ["admin", "home", "room"])
        self.assertTrue(all(v.startswith("/") and not v.startswith("//") for v in login.NEXT.values()))


class TheKey(unittest.TestCase):
    def load(self, **env):
        return configmod.load(dict(ANTS_HUB_PORT="0", **env))

    def test_a_key_of_thirty_two_printable_characters_or_more_opens_the_door(self):
        for key in ("abcdefgh" * 4, "abcdefgh" * 32, "a1!~xyzw" * 5, "kjhgfedc" + "k" * 24):
            self.assertTrue(self.load(ANTS_ADMIN_GATE_SECRET=key).admin_gate_on, len(key))
        for key in ("", "k" * 40, "ab" * 20, "abcdefg" * 6, "abcdefgh" * 3 + "a" * 7, "abcdefgh" * 32 + "x", "k" * 31, "k" * 257, "abcdefgh" * 2 + " " + "abcdefgh" * 2, "caf\u00e9" * 10, "abcdefgh" * 2 + "\t" + "abcdefgh" * 2):
            cfg = self.load(ANTS_ADMIN_GATE_SECRET=key)
            self.assertFalse(cfg.admin_gate_on, repr(len(key)))
            self.assertEqual(cfg.admin_gate_secret, "")
        self.assertFalse(self.load().admin_gate_on)
        cfg = self.load(ANTS_ADMIN_GATE_SECRET="  " + "abcdefgh" * 4 + "\n")
        self.assertEqual(cfg.admin_gate_secret, "abcdefgh" * 4, "surrounding whitespace is cut: the game server must cut it too (docs/HUB.md)")

    def test_the_published_sample_key_is_no_key_however_it_is_padded(self):
        self.assertTrue(configmod.GATE_SECRET.match(SAMPLE_SECRET) and len(set(SAMPLE_SECRET)) >= configmod.GATE_DISTINCT, "it has the shape of a key: only its being published refuses it")
        for text in (SAMPLE_SECRET, "  " + SAMPLE_SECRET + "\n", "\t" + SAMPLE_SECRET):
            cfg = self.load(ANTS_ADMIN_GATE_SECRET=text)
            self.assertFalse(cfg.admin_gate_on)
            self.assertEqual(cfg.admin_gate_secret, "")
            self.assertTrue(cfg.admin_gate_given and cfg.admin_gate_sample)
        for text in (SAMPLE_SECRET + "x", "x" + SAMPLE_SECRET, SECRET):
            cfg = self.load(ANTS_ADMIN_GATE_SECRET=text)
            self.assertTrue(cfg.admin_gate_on and not cfg.admin_gate_sample, "a key that only contains or extends it is a key")
        for text in ("", "short", "k" * 40):
            self.assertFalse(self.load(ANTS_ADMIN_GATE_SECRET=text).admin_gate_sample)

    def test_a_malformed_key_never_stops_the_load_and_the_key_is_in_no_text(self):
        cfg = self.load(ANTS_ADMIN_GATE_SECRET=SECRET)
        for text in (repr(cfg), str(cfg), json.dumps(cfg.summary())):
            self.assertNotIn(SECRET, text)
        self.load(ANTS_ADMIN_GATE_SECRET="\x01\x02")
        self.load(ANTS_ADMIN_GATE_SECRET="x" * 100000)

    def test_the_warning_is_one_line_and_never_holds_the_key(self):
        import io
        for key, word in (("", "no_secret_set"), ("tiny-" * 2, "secret_too_short_or_not_printable"), ("a b " * 20, "secret_too_short_or_not_printable"), (SAMPLE_SECRET, "secret_is_the_sample_key")):
            stream = io.StringIO()
            admindoor.warn(self.load(ANTS_ADMIN_GATE_SECRET=key), logmod.Log(stream))
            lines = stream.getvalue().splitlines()
            self.assertEqual(len(lines), 1, key)
            self.assertIn("warning what=admin_page_door_shut_" + word, lines[0])
            self.assertNotIn(key.strip() or "\0", lines[0])
        stream = io.StringIO()
        admindoor.warn(self.load(ANTS_ADMIN_GATE_SECRET=SECRET), logmod.Log(stream))
        self.assertEqual(stream.getvalue(), "")


class TheOneSecret(unittest.TestCase):
    """The key is ONE setting, ANTS_ADMIN_GATE_SECRET, which the stack hands to the chat hub and to the game server alike; the hub's old name for it is read only when the new one is not set, so that a stack that
    still has the old name keeps its door. The values here are made up."""
    OLD = "ANTS_HUB_ADMIN_GATE_SECRET"
    NEW = "ANTS_ADMIN_GATE_SECRET"
    OTHER_KEY = "another-gate-key-6c3a9e1f0b7d4825-w"

    def load(self, **env):
        return configmod.load(dict(ANTS_HUB_PORT="0", **env))

    def test_the_shared_name_alone_is_the_key(self):
        cfg = self.load(**{self.NEW: SECRET})
        self.assertTrue(cfg.admin_gate_on)
        self.assertEqual(cfg.admin_gate_secret, SECRET)

    def test_the_old_name_alone_still_is_the_key(self):
        cfg = self.load(**{self.OLD: SECRET})
        self.assertTrue(cfg.admin_gate_on)
        self.assertEqual(cfg.admin_gate_secret, SECRET)

    def test_a_stack_with_both_names_and_one_value_has_one_key(self):
        cfg = self.load(**{self.OLD: SECRET, self.NEW: SECRET})
        self.assertEqual((cfg.admin_gate_on, cfg.admin_gate_secret), (True, SECRET))

    def test_the_old_name_is_read_when_the_new_one_is_empty_or_blank(self):
        for blank in ("", "   ", "\n", "\t \n"):
            cfg = self.load(**{self.NEW: blank, self.OLD: SECRET})
            self.assertEqual(cfg.admin_gate_secret, SECRET, repr(blank))

    def test_the_shared_name_wins_when_both_are_set_so_that_both_services_hold_the_same_key(self):
        """The game server reads only the shared name: a hub that took the old one beside it would hold another key and the page would stay closed."""
        cfg = self.load(**{self.OLD: self.OTHER_KEY, self.NEW: SECRET})
        self.assertEqual(cfg.admin_gate_secret, SECRET)
        cfg = self.load(**{self.OLD: SECRET, self.NEW: self.OTHER_KEY})
        self.assertEqual(cfg.admin_gate_secret, self.OTHER_KEY)

    def test_a_shared_name_that_is_no_good_does_not_fall_back_to_the_old_one(self):
        """The two services must agree: the game server, which reads only the shared name, would have no key, so the hub keeps the door shut as well."""
        for bad in ("tiny-key", "k" * 40, SAMPLE_SECRET):
            cfg = self.load(**{self.NEW: bad, self.OLD: SECRET})
            self.assertEqual((cfg.admin_gate_on, cfg.admin_gate_secret), (False, ""), bad)

    def test_a_key_under_the_old_name_is_held_to_the_same_shape_when_the_new_name_is_unset_empty_or_blank(self):
        """The old name is another place to read the key from, not another rule: a short key, a key with a blank inside, one that is not printable and one of one repeated character leave the door shut."""
        bad_keys = ("tiny-key", "k" * 40, "a b " * 20, "abcdefghijkl", "abcdefgh abcdefgh abcdefgh abcdefgh", "\x01\x02\x03\x04\x05\x06\x07\x08" * 5)
        for bad in bad_keys:
            for new in (None, "", "   "):
                env = {self.OLD: bad}
                if new is not None:
                    env[self.NEW] = new
                cfg = self.load(**env)
                self.assertEqual((cfg.admin_gate_on, cfg.admin_gate_secret), (False, ""), (len(bad), new))
                self.assertTrue(cfg.admin_gate_given, "a key that is there and is no good is said at the start (and one that is not there is said differently)")

    def test_the_sample_key_is_refused_under_either_name(self):
        for name in (self.NEW, self.OLD):
            cfg = self.load(**{name: SAMPLE_SECRET})
            self.assertEqual((cfg.admin_gate_on, cfg.admin_gate_secret, cfg.admin_gate_sample), (False, "", True), name)

    def test_neither_name_is_no_key_and_is_said_so(self):
        cfg = self.load()
        self.assertEqual((cfg.admin_gate_on, cfg.admin_gate_given), (False, False))


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

    def test_a_hub_with_a_missing_or_short_or_malformed_key_starts_answers_503_and_says_so_once(self):
        for key, word in ((None, "no_secret_set"), ("tiny-key", "secret_too_short_or_not_printable"), ("\x01" * 40, "secret_too_short_or_not_printable")):
            with self.subTest(key=key and len(key)):
                extra = {"ANTS_HUB_TEST_LOGIN": "1", "ANTS_HUB_OWNER": OWNER}
                if key is not None:
                    extra["ANTS_ADMIN_GATE_SECRET"] = key
                port, log_path = self.run_hub(extra)
                r = hc.request(port, "GET", "/hub/admin/gate", [("X-Client-Address", "198.51.100.9"), ("X-Hub-Test-Login", "%s:%s" % (OWNER, NAMES[OWNER]))])
                self.assertEqual(r.status, 503)
                r = hc.request(port, "GET", "/hub/healthz")
                self.assertEqual(r.status, 200)
                text = read_text(log_path)
                self.assertEqual(text.count("admin_page_door_shut"), 1, text)
                self.assertIn("what=admin_page_door_shut_" + word, text)
                self.assertNotIn(key or "\0", text)

    def test_a_hub_with_a_good_key_opens_the_door_for_the_operator_and_says_nothing(self):
        for name in ("ANTS_ADMIN_GATE_SECRET", "ANTS_HUB_ADMIN_GATE_SECRET"):          # (the shared name, and the old one that a stack may still carry)
            with self.subTest(setting=name):
                port, log_path = self.run_hub({"ANTS_HUB_TEST_LOGIN": "1", "ANTS_HUB_OWNER": OWNER, name: SECRET})
                r = hc.request(port, "GET", "/hub/admin/gate", [("X-Client-Address", "198.51.100.9"), ("X-Hub-Test-Login", "%s:%s" % (OWNER, NAMES[OWNER]))])
                self.assertEqual(r.status, 204)
                self.assertEqual(verify(r.get("x-ants-admin-pass"), SECRET, time.time())["role"], "owner")
                text = read_text(log_path)
                self.assertNotIn("admin_page_door_shut", text)
                self.assertNotIn(SECRET, text)
                for account in EVERY_ID:
                    self.assertNotIn(account, text)


class TheCommandLineStillWorks(unittest.TestCase):
    """The command line's own functions (hub/admin.py) are what the page's API uses: `resolve` takes an id for the command line and never for the page."""

    def test_the_command_line_and_the_page_resolve_an_account_the_same_way(self):
        from hub import admin, store as storemod
        with tempfile.TemporaryDirectory() as folder:
            s = storemod.Store(os.path.join(folder, "hub.db"))
            try:
                s.set_profile(TARGET, "terry.target", "", 1000)
                self.assertEqual(admin.resolve(s, "terry.target"), (TARGET, "terry.target"))
                self.assertEqual(admin.resolve(s, "@terry.target"), (TARGET, "terry.target"))
                self.assertEqual(admin.resolve(s, admin.ref(s, TARGET)), (TARGET, "terry.target"))
                self.assertEqual(admin.resolve(s, TARGET), (TARGET, "terry.target"), "the command line takes an id")
                self.assertEqual(admin.resolve(s, TARGET, ids=False)[0], None, "the page's API never does")
                self.assertEqual(admin.resolve(s, "@" + TARGET, ids=False)[0], None)
                for why in (admin.resolve(s, TARGET, ids=False)[1], admin.resolve(s, "BAD NAME", ids=False)[1], admin.resolve(s, "nobody.at.all", ids=False)[1]):
                    self.assertNotIn(TARGET, why)
                    self.assertNotIn("BAD", why)
            finally:
                s.close()


if __name__ == "__main__":
    unittest.main()
