#!/usr/bin/env python3
"""Who is a moderator or an administrator (hub/roles.py, the bot's lookup in hub/discord.py; docs/HUB.md "Roles"; run by ./run_tests.sh --fast and by the CI), against a FAKE Discord
(tests/scripts/fake_discord.py: every id and the token in these tests are made up) and, for the scheduler, against a stub of the client and a clock that the test moves.

  TheMapping                       admin, mod, both roles (admin), none, not in the server, one role configured, the owner account, a role that a client says is never read
  TheLookup                        the call itself against the fake: the header, the address, the user agent, every failure that a real service has, the rate-limit fields
  TheFailures                      every failure is a failed check and never `none` or a role: the person keeps what they had until the grace, then loses it; one line in the log
  TheRefusals                      a token, an access or a server that Discord refuses: no call for ten minutes, one neutral line, nobody holds a role but the owner account
  TheGrace                         a role is believed for the interval and a minute more, then it is gone and the pages are told
  TheScheduler                     the front of the queue for a new socket, the stagger of a crowd, two calls a second, role holders first, the jitter, promotion and demotion, memory that is bounded
  TheRateLimits                    `remaining` 0 and 429 stop the lookups for as long as Discord says (and no longer than a minute)
  TheLiveRoles                     real sockets and the real hub: the welcome, the live `you` frame, a promotion and a removal without a new login, /mute for a role holder, an outage that never stops the chat
  TheSecrets                       the token, the server's id, the roles' ids and the accounts are in no log line, frame, answer or error text, the real process included
  TheIdIsLikeAPassword             a fake Discord id is planted and looked for in every log line, frame, error, repr and traceback, in every failure mode, the real process included
  TheSettings                      the variables, their limits, what a wrong value says (its name and never the value), the start line

tests/data/hub_roles_mutants.json breaks the rules one at a time for tools/mutate.py (every one must be caught).
"""
import asyncio
import dataclasses
import gc
import io
import json
import logging
import os
import re
import signal
import sys
import time
import traceback
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
import fake_discord as fd  # noqa: E402
import hub_client as hc  # noqa: E402
import hub_rig  # noqa: E402
import test_hub_server as ths  # noqa: E402  (its helpers start the real process and read its log)
from test_hub_core import ALICE, BOB, World  # noqa: E402  (a chat without sockets, and its identities)
from hub import session as sessionmod  # noqa: E402
from hub import config, discord as discordmod, limits as limitmod, roles as rolesmod  # noqa: E402

TOKEN, GUILD, ADMIN, MOD, OTHER = fd.BOT_TOKEN, fd.GUILD_ID, fd.ROLE_ADMIN, fd.ROLE_MOD, fd.ROLE_OTHER
OWNER = "1900000000000001"
ENV = {"ANTS_HUB_DISCORD_BOT_TOKEN": TOKEN, "ANTS_HUB_DISCORD_GUILD": GUILD, "ANTS_HUB_ROLE_ADMIN": ADMIN, "ANTS_HUB_ROLE_MOD": MOD}
SECRETS = (TOKEN, GUILD, ADMIN, MOD, OTHER)


class Clock:
    def __init__(self):
        self.t = 1000.0

    def __call__(self):
        return self.t


class Log:
    def __init__(self):
        self.events = []

    def event(self, name, **fields):
        self.events.append((name, fields))

    def named(self, name):
        return [f for n, f in self.events if n == name]

    def text(self):
        return repr(self.events)


def good(account, roles):
    return 200, {"user": {"id": account, "username": "canary.member"}, "roles": list(roles)}, {}


NOT_A_MEMBER = (404, {"message": "Unknown Member", "code": 10007}, {})


class Stub:
    """The Discord client, as hub.roles.Roles uses it: `answer(account)` is a (status, data, rate limits) or an exception to raise."""

    def __init__(self, clock, answer):
        self.clock = clock
        self.answer = answer
        self.calls = []                          # (time, account) of every lookup

    async def member(self, guild, user, token):
        assert (guild, token) == (GUILD, TOKEN)
        self.calls.append((self.clock.t, user))
        result = self.answer(user)
        if isinstance(result, Exception):
            raise result
        return result


class Setup:
    """A Roles with a clock, a log, a stub for Discord, and the accounts that are `here`. `members`: account -> role ids (an account that is not there is not in the server)."""

    def __init__(self, recheck=180, owner="", members=None, rng=lambda: 0.5, env=ENV, **limits):
        self.clock, self.log = Clock(), Log()
        self.members = dict(members or {})
        self.mode = None                         # None: the members dictionary answers; or a function(account) that does
        cfg = config.load(dict(env, ANTS_HUB_OWNER=owner))
        cfg.role_recheck_seconds = recheck
        self.limits = dataclasses.replace(limitmod.Limits(), **limits)
        self.stub = Stub(self.clock, self.answer)
        self.roles = rolesmod.Roles(cfg, self.limits, self.clock, self.log, client=self.stub, rng=rng)
        self.here = set()
        self.changes = []
        self.roles.connected = lambda: set(self.here)
        self.roles.changed = self.changes.append
        self.loop = asyncio.new_event_loop()

    def answer(self, account):
        if self.mode is not None:
            return self.mode(account)
        return good(account, self.members[account]) if account in self.members else NOT_A_MEMBER

    def connect(self, *accounts):
        for account in accounts:
            self.here.add(account)
            self.roles.want(account)

    def run(self, seconds):
        """The worker and the clock's work, for `seconds` of the test's time."""
        end, next_tick = self.clock.t + seconds, self.clock.t
        while self.clock.t < end:
            if self.clock.t >= next_tick:
                self.roles.on_tick(self.clock.t)
                next_tick += 1.0
            delay = self.loop.run_until_complete(self.roles.step())
            if delay > 0:
                self.clock.t += min(delay, max(0.001, next_tick - self.clock.t), end - self.clock.t) if self.clock.t < end else 0
        self.roles.on_tick(self.clock.t)

    def role(self, account):
        return self.roles.role_of(account)

    def close(self):
        self.loop.close()


class SetupTest(unittest.TestCase):
    def setup(self, **kw):
        s = Setup(**kw)
        self.addCleanup(s.close)
        return s


class TheMapping(SetupTest):
    def test_each_role_is_what_discord_says_and_both_roles_are_admin(self):
        s = self.setup(members={"1001": [ADMIN], "1002": [MOD], "1003": [MOD, ADMIN], "1004": [OTHER], "1005": [OTHER, MOD, "700000000000000009"]})
        s.connect("1001", "1002", "1003", "1004", "1005", "1006")                # (1006 is not in the server)
        s.run(30)
        self.assertEqual([s.role(a) for a in ("1001", "1002", "1003", "1004", "1005", "1006")], ["admin", "mod", "admin", "none", "mod", "none"])
        self.assertEqual({a for _, a in s.stub.calls}, {"1001", "1002", "1003", "1004", "1005", "1006"})

    def test_a_deleted_discord_account_is_an_answer_and_holds_no_role(self):
        s = self.setup()
        s.mode = lambda account: (404, {"message": "Unknown User", "code": 10013}, {})
        s.connect("1006")
        s.run(5)
        self.assertEqual((s.roles.entries["1006"].fails, s.log.events, s.role("1006"), s.roles.fresh(["1006"])), (0, [], "none", True))

    def test_a_person_who_is_not_in_the_server_is_an_answer_and_not_a_failure(self):
        s = self.setup()
        s.connect("1006")
        s.run(5)
        self.assertEqual(s.roles.entries["1006"].fails, 0)
        self.assertIsNotNone(s.roles.entries["1006"].ok_at)
        self.assertEqual(s.log.events, [])

    def test_the_owner_account_is_always_admin_with_no_call_and_nobody_else_is_for_it(self):
        s = self.setup(owner=OWNER, members={})
        self.assertEqual(s.role(OWNER), "admin")                                   # (before anything: no lookup is needed, and none is made)
        s.connect(OWNER, "1001")
        s.run(30)
        self.assertEqual({a for _, a in s.stub.calls}, {"1001"})
        self.assertEqual((s.role(OWNER), s.role("1001")), ("admin", "none"))
        s.mode = lambda account: (401, {"code": 0}, {})
        s.run(700)
        self.assertEqual(s.role(OWNER), "admin")                                   # (Discord refuses the token: the owner account stays)

    def test_the_owner_account_is_admin_when_the_roles_are_not_configured_at_all(self):
        cfg = config.load({"ANTS_HUB_OWNER": OWNER})
        roles = rolesmod.Roles(cfg, limitmod.Limits(), Clock(), Log())
        self.assertFalse(roles.on)
        roles.want("1001")
        self.assertEqual((roles.role_of(OWNER), roles.role_of("1001"), roles.entries), ("admin", "none", {}))
        roles.start()                                                              # (no worker, no client)
        self.assertIsNone(roles.task)

    def test_one_role_configured_the_other_is_never_given(self):
        s = self.setup(env=dict(ENV, ANTS_HUB_ROLE_ADMIN=""), members={"1001": [ADMIN], "1002": [MOD]})
        s.connect("1001", "1002")
        s.run(10)
        self.assertEqual((s.role("1001"), s.role("1002")), ("none", "mod"))

    def test_a_role_that_a_client_says_is_never_read(self):
        with hub_rig.Rig() as rig:
            c = hc.Client(rig.port, login="1001:alice")
            self.addCleanup(c.close)
            self.assertEqual(c.recv_json()["you"]["role"], "none")
            for frame in ({"t": "you", "you": {"role": "admin", "mod": True}}, {"t": "say", "text": "/mute bob 5", "role": "admin", "mod": True}, {"t": "here", "at": "look", "role": "admin"}):
                c.send(hc.frame(1, json.dumps(frame).encode()))
            seen = c.drain(0.5)
            self.assertEqual([m for m in seen if m["t"] == "err"], [{"t": "err", "code": "bad"}] * 2)       # (`you` is not a message that a client may send; and a command from somebody with no role is refused)
            self.assertEqual([m for m in seen if m["t"] == "line"], [])                                    # (and is not posted for everybody to read)


def session_identity(account, name):
    return sessionmod.Identity(account, name, False)


class TheChatAsksTheRoles(unittest.TestCase):
    """The chat without sockets, with the roles as a dictionary (hub_rig.FakeRoles): what it does with the answer."""

    def setUp(self):
        self.w = World(mods=())
        self.addCleanup(self.w.close)
        self.alice, self.alice2, self.bob = self.w.join(ALICE), self.w.join(ALICE), self.w.join(BOB)

    def test_a_change_of_role_is_told_to_every_socket_of_the_account_and_to_nobody_else(self):
        self.w.roles.set("1001", "mod")
        for peer in (self.alice, self.alice2):
            (frame,) = peer.kinds("you")
            self.assertEqual((frame["you"]["role"], frame["you"]["mod"], frame["you"]["name"]), ("mod", True, "alice"))
            self.assertEqual(set(frame), {"t", "you"})
        self.assertEqual(self.bob.kinds("you"), [])
        self.alice.sent.clear()
        self.w.roles.set("1001", "admin")
        self.assertEqual(self.alice.kinds("you")[0]["you"]["role"], "admin")
        self.w.roles.set("1001", "none")
        self.assertEqual([m["you"]["mod"] for m in self.alice.kinds("you")], [True, False])

    def test_a_closed_socket_is_not_told(self):
        self.alice2.closed = True
        self.w.roles.set("1001", "mod")
        self.assertEqual((len(self.alice.kinds("you")), len(self.alice2.kinds("you"))), (1, 0))

    def test_the_welcome_asks_for_the_role_of_a_login_and_of_nobody_else(self):
        self.w.chat.welcome(self.alice)
        self.w.chat.welcome(self.w.join())
        self.assertEqual(self.w.roles.wanted, ["1001"])

    def test_the_commands_follow_the_role_as_it_is_now(self):
        self.w.say(self.bob, "hello")
        self.w.roles.set("1001", "mod")
        self.w.say(self.alice, "/mute bob 5")
        self.assertEqual(self.w.store.muted_until("1002") > 0, True)
        self.w.roles.set("1001", "none")
        self.w.say(self.alice, "/mute alice 5")
        self.assertEqual(self.w.store.muted_until("1001"), 0)
        self.assertEqual(self.alice.codes(), ["bad"])                            # (refused, privately)
        self.assertEqual(self.w.chat.ring[-1].text, "hello")                     # (and not posted for everybody to read)

    def test_the_command_words_are_read_in_any_case(self):
        self.w.say(self.bob, "hello")
        for n, text in enumerate(("/Mute alice 5", "/MUTE alice 5", "/Del 1", "/dEl 1")):
            self.w.clock.now += 3
            self.w.say(self.alice, text)                                         # (a role-less account: a phone's capital letter does not make it a line for everybody)
        self.assertEqual(self.alice.codes(), ["bad"] * 4)
        self.assertEqual([l.text for l in self.w.chat.ring], ["hello"])
        self.w.roles.set("1002", "mod")
        self.w.roles.set("1001", "none")
        self.w.say(self.alice, "hi")
        self.w.clock.now += 3
        self.w.say(self.bob, "/Mute alice 5")                                    # (and a moderator's own capital letter is a command)
        self.assertGreater(self.w.store.muted_until("1001"), 0)
        self.assertEqual(self.bob.codes(), [])

    def test_both_roles_are_a_moderator_for_the_lines_the_list_and_the_username(self):
        for role in ("mod", "admin"):
            self.w.roles.set("1001", role)
            self.assertTrue(self.w.chat.is_mod("1001"), role)
            self.assertEqual(self.w.chat.welcome(self.alice)["you"]["role"], role)
        self.assertFalse(self.w.chat.is_mod("1002"))

    def test_an_account_that_holds_a_role_is_not_muted_by_a_moderator(self):
        roles = hub_rig.FakeRoles({"1001": "mod", "1002": "admin"})
        w = World(mods=())
        self.addCleanup(w.close)
        w.chat.roles = roles
        roles.connected, roles.changed = w.chat.accounts, w.chat.role_changed
        a, b = w.join(ALICE), w.join(BOB)
        w.say(b, "hello")
        w.say(a, "/mute bob 5")
        self.assertEqual(w.store.muted_until("1002"), 0)                         # (a moderator's command on an account with a role is refused)
        self.assertEqual(a.codes(), ["power"])


class TheLookup(unittest.TestCase):
    """The call against the fake Discord."""

    def setUp(self):
        self.fake = fd.FakeDiscord()
        self.addCleanup(self.fake.close)
        self.fake.members = {"1001": [MOD], "1002": []}
        self.limits = dataclasses.replace(limitmod.Limits(), discord_seconds=1.0, discord_connect_seconds=1.0, discord_body_max=4096)
        cfg = config.load(dict(ENV, ANTS_HUB_TEST_DISCORD_URL=self.fake.url))
        self.client = discordmod.Discord(cfg, self.limits)

    def ask(self, account="1001", token=TOKEN, guild=GUILD):
        return asyncio.run(self.client.member(guild, account, token))

    def test_a_member_answers_with_its_roles_and_the_request_is_the_one_discord_documents(self):
        status, data, rl = self.ask()
        self.assertEqual((status, data["roles"], data["user"]["id"]), (200, [MOD], "1001"))
        method, path, query, headers, body = self.fake.requests[-1]
        self.assertEqual((method, path, query, body), ("GET", "/api/v10/guilds/%s/members/1001" % GUILD, "", ""))
        self.assertEqual(headers["authorization"], "Bot " + TOKEN)
        self.assertTrue(headers["user-agent"].startswith("DiscordBot ("), headers["user-agent"])
        self.assertEqual(self.ask("1002")[1]["roles"], [])

    def test_not_in_the_server_the_wrong_token_and_the_wrong_server_are_told_apart(self):
        self.assertEqual(self.ask("1009")[:2], (404, {"message": "Unknown Member", "code": 10007}))
        self.assertEqual(self.ask(token=TOKEN + "x")[0], 401)
        self.assertEqual(self.ask(guild="700000000000000099")[:2], (404, {"message": "Unknown Guild", "code": 10004}))

    def test_a_status_with_its_code(self):
        for mode, status, code in (("err:401", 401, 0), ("err:403", 403, 50001), ("err:404:10004", 404, 10004), ("err:404:10007", 404, 10007), ("status:500", 500, None), ("status:502", 502, None)):
            self.fake.member_mode = mode
            got = self.ask()
            self.assertEqual((got[0], got[1].get("code")), (status, code), mode)

    def test_every_way_that_a_real_service_goes_wrong_is_an_error_and_not_a_member(self):
        for mode in ("hang", "close", "partial", "notjson", "cutoff", "nan", "deep", "redirect", "redirect307", "huge", "huge_stream", "huge_chunked", "huge_head", "chunked_length"):
            self.fake.member_mode = mode
            started = time.monotonic()
            with self.assertRaises(discordmod.DiscordError, msg=mode) as caught:
                self.ask()
            self.assertLess(time.monotonic() - started, 6.0, mode)                               # (a timeout on every request)
            self.assertEqual(set(vars(caught.exception)), {"kind", "status"}, mode)
            for canary in (TOKEN, GUILD, "1001"):
                self.assertNotIn(canary, str(caught.exception) + repr(caught.exception) + repr(vars(caught.exception)), mode)
        self.fake.member_mode = "redirect"
        self.assertEqual([r for r in self.fake.requests if r[1] == "/followed"], [])             # (a redirect is never followed)
        self.assertLess(self.fake.sent, 30 * 10 ** 6)                                            # (a flood is cut at the size cap: the fake would send 98 MB to a client that kept reading)

    def test_a_drip_ends_at_the_time_limit(self):
        self.fake.member_mode = "drip"
        started = time.monotonic()
        with self.assertRaises(discordmod.DiscordError):
            self.ask()
        self.assertLess(time.monotonic() - started, 3.0)

    def test_a_list_or_text_that_is_not_an_object_is_no_member(self):
        for mode in ("shape:[1,2]", "shape:\"text\"", "shape:5"):
            self.fake.member_mode = mode
            with self.assertRaises(discordmod.DiscordError, msg=mode):
                self.ask()

    def test_the_rate_limit_fields_come_back_as_small_numbers(self):
        self.fake.member_mode = "rate:7"
        status, data, rl = self.ask()
        self.assertEqual((status, rl), (429, {"remaining": 0.0, "reset_after": 7.0, "retry_after": 7.0, "global": False}))
        self.fake.member_mode = "rate_global:3"
        self.assertEqual(self.ask()[2], {"remaining": 0.0, "reset_after": 3.0, "retry_after": 3.0, "global": True})
        self.fake.member_mode = "remaining0:2"
        status, data, rl = self.ask()
        self.assertEqual((status, rl["remaining"], rl["reset_after"], rl["global"]), (200, 0.0, 2.0, False))
        self.fake.member_mode = "ok"
        self.assertEqual(self.ask()[2], {"global": False})

    def test_a_rate_limit_value_that_is_not_a_small_plain_number_is_left_out(self):
        rl = discordmod.Discord.rate_limits
        junk = {"x-ratelimit-remaining": "nan", "x-ratelimit-reset-after": "1e9"}
        self.assertEqual(rl(junk, {}), {"global": False})
        self.assertEqual(rl({"x-ratelimit-reset-after": "99999"}, {}), {"global": False})
        self.assertEqual(rl({}, {"retry_after": True}), {"global": False})
        longest = {"retry_after": 3600.0, "global": False}                       # (a wait that is asked for and cannot be read, or is huge, is the longest wait and never a short one)
        for fields, body in (({"retry-after": "abc"}, {}), ({"retry-after": "-1"}, {}), ({"retry-after": "99999"}, {}), ({"retry-after": "nan"}, {}), ({}, {"retry_after": 10 ** 9}),
                             ({}, {"retry_after": "soon"}), ({}, {"retry_after": [1]}), ({}, {"retry_after": -1})):
            self.assertEqual(rl(fields, body), longest, (fields, body))
        self.assertEqual(rl({"retry-after": "7"}, {}), {"retry_after": 7.0, "global": False})
        self.assertEqual(rl({}, {"retry_after": 2.5, "global": True}), {"retry_after": 2.5, "global": True})
        self.assertEqual(rl({"x-ratelimit-scope": "shared"}, {}), {"global": False})

    def test_a_token_or_an_id_that_is_not_the_shape_of_one_is_never_put_in_a_request(self):
        before = len(self.fake.requests)
        for args in (("1001", TOKEN + "\r\nX: y", GUILD), ("1001", "short", GUILD), ("10/../01", TOKEN, GUILD), ("1001", TOKEN, GUILD + "/x"), ("1001", TOKEN, ""), ("", TOKEN, GUILD)):
            with self.assertRaises(discordmod.DiscordError, msg=repr(args)):
                self.ask(*args)
        self.assertEqual(len(self.fake.requests), before)


FAILURES = {
    "down": lambda a: discordmod.DiscordError("down"),
    "a bug": lambda a: RuntimeError("canary-text-of-an-error " + TOKEN),
    "500": lambda a: (500, {"message": "x"}, {}),
    "502": lambda a: (502, {}, {}),
    "400": lambda a: (400, {"message": "x"}, {}),
    "404 of another kind": lambda a: (404, {"message": "Unknown Channel", "code": 10003}, {}),
    "404 with no code": lambda a: (404, {}, {}),
    "404 with a code that is text": lambda a: (404, {"code": "10007"}, {}),
    "404 with a code that is true": lambda a: (404, {"code": True}, {}),
    "roles is not a list": lambda a: (200, {"user": {"id": a}, "roles": "700000000000000003"}, {}),
    "roles is missing": lambda a: (200, {"user": {"id": a}}, {}),
    "roles are numbers": lambda a: (200, {"user": {"id": a}, "roles": [700000000000000003]}, {}),
    "a role that is not an id": lambda a: (200, {"user": {"id": a}, "roles": [MOD, "admin"]}, {}),
    "too many roles": lambda a: (200, {"user": {"id": a}, "roles": [MOD] * 251}, {}),
    "another user": lambda a: (200, {"user": {"id": "1999"}, "roles": [MOD]}, {}),
    "a user that is not an object": lambda a: (200, {"user": "x", "roles": [MOD]}, {}),
    "a user with no id": lambda a: (200, {"user": {}, "roles": [MOD]}, {}),
}


class TheFailures(SetupTest):
    def test_a_failed_check_is_never_none_and_never_a_role(self):
        for name, failure in FAILURES.items():
            s = self.setup(members={"1001": [MOD]})
            s.connect("1001", "1002")
            s.run(5)
            self.assertEqual(s.role("1001"), "mod", name)
            s.mode = failure
            s.run(200)                                                          # (the next check is due at 180, and fails; the person is still believed: one grace is forgiven)
            self.assertEqual((s.role("1001"), s.role("1002")), ("mod", "none"), name)
            self.assertGreaterEqual(s.roles.entries["1001"].fails, 1, name)
            self.assertEqual(s.roles.entries["1002"].ok_at is not None, True, name)             # (1002 had an answer too: "not in the server", and kept)
            s.run(100)                                                          # (past the grace: 180 + 60 after the last answer)
            self.assertEqual(s.role("1001"), "none", name)
            self.assertIn(("1001",), [(a,) for a in s.changes], name)
            self.assertEqual(len(s.log.named("roles_down")), 1, name)           # (one line for the whole outage)
            self.assertEqual(s.log.named("roles_up"), [], name)

    def test_a_new_person_whose_first_check_fails_is_none_and_is_asked_again_soon_then_at_the_pace(self):
        s = self.setup()
        s.mode = lambda a: (500, {}, {})
        s.connect("1001")
        s.run(700)
        times = [t - 1000.0 for t, _ in s.stub.calls]
        self.assertEqual(s.role("1001"), "none")
        gaps = [round(b - a) for a, b in zip(times, times[1:])]
        self.assertEqual(gaps[:5], [15, 30, 60, 180, 180])                       # (15, 30, 60, and then the normal pace)

    def test_the_log_says_it_once_in_ten_minutes_and_says_when_it_is_over(self):
        s = self.setup(members={"1001": [MOD]})
        s.mode = lambda a: (500, {}, {})
        s.connect("1001")
        s.run(500)
        self.assertEqual(s.log.named("roles_down"), [{"kind": "down"}])
        s.run(200)
        self.assertEqual(len(s.log.named("roles_down")), 2)                      # (ten minutes later: once more)
        s.mode = None
        s.run(200)
        self.assertEqual(s.log.named("roles_up"), [{}])
        self.assertEqual(s.role("1001"), "mod")
        self.assertIn("1001", s.changes)

    def test_nothing_in_the_log_or_an_error_is_the_text_of_an_exception_or_an_id(self):
        s = self.setup(members={"1001": [MOD]})
        s.mode = FAILURES["a bug"]
        s.connect("1001")
        s.run(200)
        self.assertEqual(s.log.named("error"), [{"kind": "RuntimeError", "where": "roles"}])
        for secret in SECRETS + ("1001", "canary-text-of-an-error"):
            self.assertNotIn(secret, s.log.text())

    def test_a_check_that_fails_leaves_the_others_waiting_no_longer_than_the_pace(self):
        s = self.setup(members={"1001": [MOD], "1002": [ADMIN]})
        s.mode = lambda a: discordmod.DiscordError("down") if a == "1001" else good(a, [ADMIN])
        s.connect("1001", "1002")
        s.run(5)
        self.assertEqual(s.role("1002"), "admin")                                # (one account's failure does not hold the queue)


class TheRefusals(SetupTest):
    CASES = (("token", 401, {"code": 0}), ("access", 403, {"code": 50001}), ("guild", 404, {"code": 10004}))

    def test_a_refused_token_access_or_server_stops_every_call_for_ten_minutes_and_takes_the_roles_away(self):
        for kind, status, body in self.CASES:
            s = self.setup(owner=OWNER, members={"1001": [ADMIN], "1002": [MOD]})
            s.connect("1001", "1002")
            s.run(10)
            self.assertEqual((s.role("1001"), s.role("1002")), ("admin", "mod"), kind)
            s.changes.clear()
            s.mode = lambda a: (status, body, {})
            s.connect("1003")
            s.run(1)                                                             # (the new person is asked at once, and refused)
            calls = len(s.stub.calls)
            self.assertEqual((s.role("1001"), s.role("1002"), s.role(OWNER)), ("none", "none", "admin"), kind)
            self.assertEqual(sorted(set(s.changes)), ["1001", "1002"], kind)     # (the pages were told)
            self.assertEqual(s.log.named("warning"), [{"what": "discord_roles_refused", "kind": kind, "status": status}], kind)
            s.run(580)                                                           # (nothing is asked for the rest of the ten minutes, whoever connects)
            s.connect("1004")
            s.run(1)
            self.assertEqual(len(s.stub.calls), calls, kind)
            s.run(30)                                                            # (then one call, which is refused again: ten minutes more, one more line)
            self.assertEqual(len(s.stub.calls), calls + 1, kind)
            self.assertEqual(len(s.log.named("warning")), 2, kind)
            s.run(550)
            self.assertEqual(len(s.stub.calls), calls + 1, kind)
            s.mode = None                                                        # (and when Discord takes the token again, the roles come back)
            s.run(120)
            self.assertEqual((s.role("1001"), s.role("1002")), ("admin", "mod"), kind)

    def test_not_in_the_server_is_no_refusal(self):
        s = self.setup(members={})
        s.connect("1001", "1002", "1003")
        s.run(600)
        self.assertEqual([f for f in s.log.named("warning")], [])
        self.assertGreater(len(s.stub.calls), 3)

    def test_the_log_line_of_a_refusal_holds_no_secret(self):
        s = self.setup(members={"1001": [ADMIN]})
        s.mode = lambda a: (401, {"message": "canary-body-text 401: Unauthorized", "code": 0}, {})
        s.connect("1001")
        s.run(30)
        for secret in SECRETS + ("1001", "canary-body-text"):
            self.assertNotIn(secret, s.log.text())


class TheGrace(SetupTest):
    def test_a_role_is_believed_for_the_interval_and_a_minute_and_then_it_is_gone(self):
        s = self.setup(members={"1001": [MOD]})
        s.connect("1001")
        s.run(1)
        asked = s.roles.entries["1001"].ok_at
        s.mode = lambda a: discordmod.DiscordError("down")
        s.changes.clear()
        s.clock.t = asked + 258.0
        self.assertEqual(s.role("1001"), "mod")                                  # (180 + a tenth + 60: still believed)
        s.clock.t = asked + 258.5
        self.assertEqual(s.role("1001"), "none")
        s.roles.on_tick(s.clock.t)
        self.assertEqual(s.changes, ["1001"])                                    # (the pages are told, once)
        s.roles.on_tick(s.clock.t + 1)
        self.assertEqual(s.changes, ["1001"])

    def test_a_check_that_is_due_as_late_as_it_can_be_never_comes_after_the_role_ran_out(self):
        for recheck in (180, 3600):
            s = self.setup(recheck=recheck, rng=lambda: 1.0, members={"1001": [MOD]})        # (the latest: the interval and a tenth)
            s.connect("1001")
            s.run(1)
            s.changes.clear()
            step = recheck / 20.0
            for _ in range(int(recheck * 1.1 / step) + 2):
                s.run(step)
                self.assertEqual(s.role("1001"), "mod", (recheck, s.clock.t))
            self.assertEqual(s.changes, [], recheck)
            self.assertGreaterEqual(s.roles.grace, recheck * 1.1 + 60.0)

    def test_the_grace_follows_the_setting(self):
        s = self.setup(recheck=60, members={"1001": [MOD]})
        s.connect("1001")
        s.run(1)
        asked = s.roles.entries["1001"].ok_at
        s.mode = lambda a: discordmod.DiscordError("down")
        s.clock.t = asked + 125.0
        self.assertEqual(s.role("1001"), "mod")
        s.clock.t = asked + 127.0
        self.assertEqual(s.role("1001"), "none")

    def test_a_role_that_comes_back_is_told_again(self):
        s = self.setup(members={"1001": [ADMIN]})
        s.connect("1001")
        s.run(1)
        s.mode = lambda a: discordmod.DiscordError("down")
        s.run(300)
        self.assertEqual(s.role("1001"), "none")
        s.changes.clear()
        s.mode = None
        s.run(200)
        self.assertEqual((s.role("1001"), s.changes), ("admin", ["1001"]))


class TheScheduler(SetupTest):
    def test_a_new_socket_is_at_the_front_of_the_queue_before_the_rechecks_that_are_due(self):
        accounts = ["%d" % n for n in range(2000, 2060)]
        s = self.setup(members={a: [] for a in accounts})
        s.connect(*accounts)
        s.run(400)
        s.stub.calls.clear()
        s.clock.t += 175.0                                                       # (everybody is due within a few seconds; a person arrives)
        s.connect("1001")
        s.run(3)
        self.assertEqual(s.stub.calls[0][1], "1001")

    def test_role_holders_are_asked_before_the_rest_when_they_are_due_together(self):
        accounts = ["%d" % n for n in range(2000, 2040)]
        holders = {"2037", "2038", "2039"}                                       # (last in the order of the dictionary: only their role puts them first)
        s = self.setup(members={a: [MOD] if a in holders else [] for a in accounts})
        s.connect(*accounts)
        s.run(400)
        s.stub.calls.clear()
        for entry in s.roles.entries.values():
            entry.due = s.clock.t                                                # (everybody is due at this moment, and nobody is overdue)
        s.run(2)
        self.assertEqual({a for _, a in s.stub.calls[:3]}, holders)

    def test_a_crowd_that_arrives_together_is_asked_at_two_a_second_and_spread_out(self):
        accounts = ["%d" % n for n in range(3000, 3300)]
        s = self.setup(members={a: [MOD] if n % 10 == 0 else [] for n, a in enumerate(accounts)})
        s.connect(*accounts)
        s.run(700)
        times = sorted(t - 1000.0 for t, _ in s.stub.calls)
        for start in range(0, 650, 7):                                          # in any window of w seconds: the burst and two a second, and not one more
            for width in (1.0, 5.0, 30.0):
                in_window = [t for t in times if start <= t < start + width]
                self.assertLessEqual(len(in_window), 5 + 2 * width + 1, (start, width))
        self.assertGreater(times[299], 140.0)                                    # (300 first checks at two a second: about 150 seconds)
        by = {}
        for t, a in s.stub.calls:
            by.setdefault(a, []).append(t)
        for a in accounts[::10]:                                                 # (the role holders are asked again after the interval and a tenth, however many people there are)
            self.assertLessEqual(max(b - c for c, b in zip(by[a], by[a][1:])), 180 * 1.1 + 5, a)
        self.assertTrue(all(s.roles.entries[a].ok_at is not None for a in accounts))
        dues = sorted(s.roles.entries[a].due for a in accounts)
        self.assertGreater(dues[-1] - dues[0], 100.0)                           # (the next checks are not at one moment)
        for account in accounts[::10]:
            self.assertEqual(s.role(account), "mod")

    def test_when_there_are_too_many_the_role_holders_are_never_held_back_and_the_rest_wait_at_most_three_intervals(self):
        accounts = ["%d" % n for n in range(4000, 4500)]                         # (500 people need 2.8 calls a second: more than two)
        holders = set(accounts[::25])                                           # 20 holders
        s = self.setup(members={a: [MOD] if a in holders else [] for a in accounts})
        s.connect(*accounts)
        s.run(1500)
        by = {}
        for t, a in s.stub.calls:
            by.setdefault(a, []).append(t)
        for a in holders:
            gaps = [b - c for c, b in zip(by[a], by[a][1:])]
            self.assertGreaterEqual(len(by[a]), 5, a)
            self.assertLessEqual(max(gaps), 180 * 1.1 + 30 + 5, (a, max(gaps)))                 # (even while the crowd's first checks are made, which come before everything else)
        others = [a for a in accounts if a not in holders]
        worst = max(b - c for a in others for c, b in zip(by[a], by[a][1:]))
        self.assertLessEqual(worst, 3 * 180, worst)
        self.assertTrue(all(len(by[a]) >= 2 for a in others))

    def test_the_next_check_is_the_interval_plus_or_minus_a_tenth(self):
        for rng, factor in ((lambda: 0.0, 0.9), (lambda: 1.0, 1.1), (lambda: 0.5, 1.0)):
            s = self.setup(rng=rng, members={"1001": [MOD]})
            s.connect("1001")
            s.run(1)
            entry = s.roles.entries["1001"]
            self.assertAlmostEqual(entry.due - entry.ok_at, 180 * factor, 6)
        s = self.setup(rng=__import__("random").random, members={"1001": [MOD]})
        spread = set()
        for _ in range(30):
            s.roles.entries.clear()
            s.connect("1001")
            s.run(1)
            spread.add(round(s.roles.entries["1001"].due - s.roles.entries["1001"].ok_at, 3))
        self.assertGreater(len(spread), 20)
        self.assertTrue(all(162.0 <= x <= 198.0 for x in spread))

    def test_a_promotion_and_a_removal_arrive_within_the_interval_without_a_new_login(self):
        s = self.setup(members={"1001": [], "1002": [MOD]})
        s.connect("1001", "1002")
        s.run(20)
        self.assertEqual((s.role("1001"), s.role("1002")), ("none", "mod"))
        s.changes.clear()
        s.members["1001"], s.members["1002"] = [ADMIN], [OTHER]
        started = s.clock.t
        s.run(180 * 1.1 + 5)
        self.assertEqual((s.role("1001"), s.role("1002")), ("admin", "none"))
        self.assertEqual(sorted(s.changes), ["1001", "1002"])
        s.members.pop("1001")                                                   # (and leaving the server)
        s.run(180 * 1.1 + 5)
        self.assertEqual(s.role("1001"), "none")
        self.assertLess(s.clock.t - started, 450)

    def test_nobody_who_is_gone_is_asked_about_and_the_memory_is_bounded(self):
        s = self.setup(roles_entries_max=50, members={})
        s.connect("1001")
        for n in range(500):                                                     # (500 people come and go, one at a time)
            account = "%d" % (5000 + n)
            s.connect(account)
            s.run(3)
            s.here.discard(account)
            s.run(1)
            self.assertLessEqual(len(s.roles.entries), 50)
        self.assertIn("1001", s.roles.entries)                                   # (the one that stayed is never evicted)
        asked = [a for _, a in s.stub.calls]
        s.stub.calls.clear()
        s.run(400)
        self.assertEqual({a for _, a in s.stub.calls}, {"1001"})
        self.assertLessEqual(len(s.roles.entries), 50)
        self.assertGreater(len(asked), 100)
        s.run(300)                                                               # (and those that left are forgotten after the grace)
        self.assertEqual(set(s.roles.entries), {"1001"})

    def test_a_page_that_reconnects_does_not_skip_the_delay_of_a_failed_check(self):
        s = self.setup(members={"1001": [MOD]})
        s.mode = lambda a: (500, {}, {})
        s.connect("1001")
        s.run(1)
        due = s.roles.entries["1001"].due
        self.assertGreater(due, s.clock.t + 10.0)
        for _ in range(5):
            s.connect("1001")                                                    # (the page connects again and again)
        self.assertEqual(s.roles.entries["1001"].due, due)
        s.run(10)
        self.assertEqual(len(s.stub.calls), 1)
        s.run(10)
        self.assertEqual(len(s.stub.calls), 2)

    def test_a_worker_that_ended_is_said_once_and_started_again(self):
        s = self.setup(members={"1001": [MOD]})

        async def scenario():
            s.roles.start()
            first = s.roles.task
            first.cancel()
            try:
                await first
            except asyncio.CancelledError:
                pass
            s.roles.on_tick(s.clock.t)
            second = s.roles.task
            assert second is not first and not second.done()
            first_log = list(s.log.events)
            second.cancel()
            try:
                await second
            except asyncio.CancelledError:
                pass
            s.roles.on_tick(s.clock.t + 1)                                       # (it ends again at once: started again, and not said again)
            s.roles.stop()
            return first_log
        said = s.loop.run_until_complete(scenario())
        self.assertEqual(said, [("error", {"kind": "worker_ended", "where": "roles"})])
        self.assertEqual(s.log.events, said)
        s.roles.stop()

    def test_a_person_who_reloads_keeps_the_role_that_was_known(self):
        s = self.setup(members={"1001": [MOD]})
        s.connect("1001")
        s.run(5)
        s.here.discard("1001")
        s.run(30)
        self.assertEqual(s.role("1001"), "mod")                                  # (the page that reloads is told `mod` in its welcome)
        s.connect("1001")
        self.assertEqual(s.role("1001"), "mod")
        self.assertFalse(s.roles.entries["1001"].new)                            # (and is not put in front of the queue again: the answer is young)

    def test_an_entry_that_goes_while_it_is_asked_about_is_no_trouble(self):
        s = self.setup(members={"1001": [MOD]})
        s.connect("1001")

        def forget(account):
            s.roles.entries.clear()
            return good(account, [MOD])
        s.mode = forget
        s.run(5)
        self.assertEqual(s.log.named("error"), [])
        self.assertEqual(s.roles.entries, {})

    def test_a_bug_in_the_worker_is_one_look_and_one_line(self):
        s = self.setup(members={"1001": [MOD]})
        s.roles.pick = lambda now: 1 / 0
        s.connect("1001")
        for _ in range(3):
            with self.assertRaises(ZeroDivisionError):
                s.loop.run_until_complete(s.roles.step())
        async def run_once():
            task = asyncio.ensure_future(s.roles.run())
            s.roles.wake = asyncio.Event()
            await asyncio.sleep(0.05)
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass
        s.loop.run_until_complete(run_once())
        self.assertEqual(s.log.named("error"), [{"kind": "ZeroDivisionError", "where": "roles"}])


class TheRateLimits(SetupTest):
    def test_an_empty_bucket_stops_the_lookups_until_it_is_full_again(self):
        s = self.setup(members={"1001": [], "1002": []})
        s.mode = lambda a: (200, {"user": {"id": a}, "roles": []}, {"remaining": 0.0, "reset_after": 4.0, "global": False})
        s.connect("1001", "1002")
        s.run(0.5)
        self.assertEqual(len(s.stub.calls), 1)
        first = s.stub.calls[0][0]
        s.run(10)
        self.assertGreaterEqual(s.stub.calls[1][0] - first, 4.0)

    def test_a_429_waits_for_retry_after_for_every_account_and_is_no_failure(self):
        for is_global in (False, True):
            s = self.setup(members={"1001": [MOD], "1002": [MOD]})
            s.mode = lambda a: (429, {"retry_after": 7.0, "global": is_global}, {"retry_after": 7.0, "global": is_global})
            s.connect("1001", "1002")
            s.run(1)
            self.assertEqual(len(s.stub.calls), 1)
            first = s.stub.calls[0][0]
            s.run(5)
            self.assertEqual(len(s.stub.calls), 1)                               # (nothing for anybody for seven seconds)
            s.mode = None
            s.run(5)
            self.assertGreaterEqual(s.stub.calls[1][0] - first, 7.0)
            self.assertEqual([e.fails for e in s.roles.entries.values()], [0, 0])
            self.assertEqual(s.log.named("roles_down"), [{"kind": "busy"}])
            s.run(30)
            self.assertEqual((s.role("1001"), s.role("1002")), ("mod", "mod"))
            self.assertEqual(s.log.named("roles_up"), [{}])

    def test_the_wait_that_discord_asks_for_is_at_most_a_minute(self):
        s = self.setup(members={"1001": [MOD]})
        s.mode = lambda a: (429, {"retry_after": 3000.0}, {"retry_after": 3000.0, "global": True})
        s.connect("1001")
        s.run(1)
        s.mode = None
        s.run(59)
        self.assertEqual(len(s.stub.calls), 1)
        s.run(3)
        self.assertEqual(len(s.stub.calls), 2)

    def test_a_429_that_says_nothing_of_the_time_is_a_few_seconds(self):
        s = self.setup(members={"1001": [MOD]})
        s.mode = lambda a: (429, {}, {"global": False})
        s.connect("1001")
        s.run(1)
        s.mode = None
        s.run(4)
        self.assertEqual(len(s.stub.calls), 1)
        s.run(3)
        self.assertEqual(len(s.stub.calls), 2)


class LiveTest(unittest.TestCase):
    """Real sockets, the real hub and its Roles, against the fake Discord, with the intervals made short: a check every second, a grace of three seconds more."""

    recheck = 1
    options = dict(roles_grace_seconds=3.0, roles_refill_seconds=0.01, roles_jitter=0.0, tick_seconds=0.05, sockets_per_address=50, frame_burst=1000, frame_refill_seconds=0.001,
                   discord_seconds=1.0, discord_connect_seconds=1.0, roles_pause_seconds=5.0)

    def setUp(self):
        self.fake = fd.FakeDiscord()
        self.addCleanup(self.fake.close)
        self.rig = hub_rig.Rig(member_fake=self.fake, recheck=self.recheck, owner=getattr(self, "owner", ""), **self.options)
        self.rig.__enter__()
        self.clients = []
        self.addCleanup(self.finish)

    def finish(self):
        for c in self.clients:
            c.close()
        self.rig.__exit__(None, None, None)

    def connect(self, login):
        c = hc.Client(self.rig.port, login=login)
        self.clients.append(c)
        self.assertTrue(c.ok, c.status)
        welcome = c.recv_json()
        self.assertEqual(welcome["t"], "welcome")
        return c, welcome

    def you_frames(self, client, seconds):
        return [m["you"] for m in client.drain(seconds) if m["t"] == "you"]

    def wait_role(self, client, role, seconds=8.0):
        """The roles in the `you` frames that come until one says `role` (or the time is out)."""
        end, seen = time.monotonic() + seconds, []
        while time.monotonic() < end:
            for you in self.you_frames(client, 0.2):
                seen.append(you["role"])
                if you["role"] == role:
                    return seen
        self.fail("no `you` with the role %s: %s" % (role, seen))


class TheLiveRoles(LiveTest):
    def test_the_welcome_says_none_and_a_live_you_follows_with_the_role_and_mod_stays_true_for_both(self):
        self.fake.members = {"1001": [MOD], "1002": [ADMIN, MOD]}
        a, welcome = self.connect("1001:alice")
        self.assertEqual((welcome["you"]["role"], welcome["you"]["mod"]), ("none", False))
        b, welcome = self.connect("1002:bob")
        self.assertEqual(welcome["you"]["role"], "none")
        c, welcome = self.connect("1003:carol")
        self.wait_role(a, "mod")
        self.assertEqual(self.wait_role(b, "admin"), ["admin"])
        self.assertEqual(self.you_frames(c, 1.5), [])                            # (a person with no role is told nothing: it was `none` and still is, and what happens to the others is not its business)
        d, welcome = self.connect("1001:alice")                                    # (a second tab: the welcome has the answer that is known)
        self.assertEqual((welcome["you"]["role"], welcome["you"]["mod"]), ("mod", True))
        self.assertEqual(set(welcome["you"]), {"name", "mod", "role", "k"})

    def test_a_promotion_and_a_removal_reach_every_tab_without_a_new_login(self):
        a1, _ = self.connect("1001:alice")
        a2, _ = self.connect("1001:alice")
        self.assertEqual(self.you_frames(a1, 1.5), [])
        self.fake.members["1001"] = [MOD]
        self.wait_role(a1, "mod")
        self.wait_role(a2, "mod")
        self.fake.members["1001"] = [ADMIN]
        self.wait_role(a1, "admin")
        self.fake.members["1001"] = [OTHER]
        self.wait_role(a1, "none")
        self.wait_role(a2, "none")
        full = [m for m in a1.drain(0.3) if m["t"] == "you"]
        self.assertEqual(full, [])

    def test_a_you_frame_is_the_whole_you_and_has_no_id(self):
        self.fake.members = {"1001": [ADMIN]}
        a, _ = self.connect("1001:alice")
        end = time.monotonic() + 5
        raw = None
        while raw is None and time.monotonic() < end:
            opcode, data = a.recv_frame(timeout=1.0)
            if opcode == 1 and json.loads(data)["t"] == "you":
                raw = data
        self.assertIsNotNone(raw)
        frame = json.loads(raw)
        self.assertEqual(set(frame), {"t", "you"})
        self.assertEqual((frame["you"]["name"], frame["you"]["role"], frame["you"]["mod"], len(frame["you"]["k"])), ("alice", "admin", True, 12))
        for secret in SECRETS:                                                     # (the account's own digits are not looked for: its key is random hex, which can hold four digits by chance)
            self.assertNotIn(secret.encode(), raw)

    def test_the_commands_follow_the_role_while_it_lasts(self):
        self.fake.members = {"1001": [MOD]}
        mod, _ = self.connect("1001:moddy")
        victim, _ = self.connect("1003:victim")
        self.wait_role(mod, "mod")
        victim.say("hello")
        self.assertTrue(self.wait_for(lambda: self.rig.call(lambda: self.rig.hub.chat.roles.fresh(["1003"])), 8.0))          # (the victim's role is known: it was asked)
        mod.say("/mute victim 5")
        time.sleep(0.4)
        victim.drain(0.2)
        victim.say("hello again")
        self.assertIn({"t": "err", "code": "muted"}, victim.drain(0.5))
        self.fake.members["1001"] = []                                           # (demoted: the next check, a second at most, and the command is text)
        self.wait_role(mod, "none")
        mod.drain(0.2)
        mod.say("/mute nobody 5")
        seen = mod.drain(0.5)
        self.assertEqual(([m for m in seen if m["t"] == "line"], [m["code"] for m in seen if m["t"] == "err"]), ([], ["bad"]))     # (refused, and not posted for everybody to read)

    def test_the_players_list_marks_both_roles(self):
        self.fake.members = {"1001": [MOD], "1002": [ADMIN]}
        a, _ = self.connect("1001:alice")
        b, _ = self.connect("1002:bob")
        c, _ = self.connect("1003:carol")
        self.wait_role(a, "mod")
        self.wait_role(b, "admin")
        c.send(hc.frame(1, json.dumps({"t": "who"}).encode()))
        end = time.monotonic() + 5
        rows = {}
        while time.monotonic() < end and len(rows) < 3:
            for m in c.drain(0.3):
                if m["t"] == "who":
                    rows.update({r["name"]: r.get("mod", False) for r in m["rows"]})
        self.assertEqual(rows, {"alice": True, "bob": True, "carol": False})

    def test_the_worker_runs_with_the_hub_and_ends_with_it(self):
        task = self.rig.call(lambda: self.rig.hub.chat.roles.task)
        self.assertIsNotNone(task)
        self.assertFalse(task.done())
        asyncio.run_coroutine_threadsafe(self.rig.hub.stop(), self.rig.loop).result(30)
        self.assertIsNone(self.rig.call(lambda: self.rig.hub.chat.roles.task))
        self.assertTrue(task.cancelled() or task.done())

    def test_a_discord_that_is_down_never_stops_the_chat_and_never_gives_a_role(self):
        for n, mode in enumerate(("close", "hang", "status:500", "notjson", "huge", "err:429", "rate:2", "redirect")):
            self.fake.member_mode = mode
            a, _ = self.connect("%d:alice%d" % (2000 + 2 * n, n))                    # (new people for each: an account has its own limit on lines)
            b, _ = self.connect("%d:bob%d" % (2001 + 2 * n, n))
            a.say("a word %s" % mode.replace(":", " "))
            self.assertEqual([m["text"] for m in b.drain(0.6) if m["t"] == "line"], ["a word %s" % mode.replace(":", " ")], mode)
            self.assertEqual(self.you_frames(a, 0.3), [], mode)
            a.close()
            b.close()
        self.assertGreater(len(self.fake.member_times), 3)

    def test_a_refused_token_is_one_call_and_one_line_for_the_pause_whoever_connects(self):
        self.fake.bot_token = "another-token-0a1b2c3d4e5f6a7b8c9d00"
        self.fake.members = {"1001": [MOD]}
        for n in range(5):
            c, _ = self.connect("%d:person%d" % (1001 + n, n))
        time.sleep(1.5)
        self.assertEqual(len(self.fake.member_times), 1)
        self.assertEqual(self.rig.log_text.getvalue().count("discord_roles_refused"), 1)
        self.assertIn(" warning what=discord_roles_refused kind=token status=401", self.rig.log_text.getvalue())
        c.say("chat goes on")
        self.assertEqual([m["text"] for m in c.drain(0.5) if m["t"] == "line"], ["chat goes on"])

    def test_the_memory_of_the_roles_is_the_people_who_are_here(self):
        self.fake.members = {"%d" % n: [MOD] for n in range(1001, 1013)}
        clients = [self.connect("%d:person%d" % (1001 + n, n))[0] for n in range(12)]
        time.sleep(0.5)
        self.assertEqual(self.rig.call(lambda: len(self.rig.hub.chat.roles.entries)), 12)
        for c in clients:
            c.close()
        self.assertTrue(self.wait_for(lambda: self.rig.call(lambda: len(self.rig.hub.chat.roles.entries)) == 0, 8.0))
        calls = len(self.fake.member_times)
        time.sleep(1.5)
        self.assertEqual(len(self.fake.member_times), calls)                     # (nobody is here: nobody is asked about)

    @staticmethod
    def wait_for(what, seconds):
        end = time.monotonic() + seconds
        while not what() and time.monotonic() < end:
            time.sleep(0.05)
        return what()

    def test_the_event_loop_is_not_held_by_a_slow_discord(self):
        self.fake.members = {"1001": [MOD]}
        self.fake.member_mode = "slow:0.9"
        a, _ = self.connect("1001:alice")
        b, _ = self.connect("1002:bob")
        started = time.monotonic()
        for n in range(5):
            a.say("line %d" % n)
            time.sleep(0.05)
        got = [m for m in b.drain(0.6) if m["t"] == "line"]
        self.assertGreaterEqual(len(got), 2)                                     # (lines go through while a lookup waits)
        self.assertLess(time.monotonic() - started, 1.5)


class TheLiveOwner(LiveTest):
    owner = OWNER

    def test_the_owner_account_is_admin_at_once_and_a_refused_token_changes_nothing_for_the_owner(self):
        self.fake.members = {"1001": [MOD]}
        self.fake.bot_token = "another-token-0a1b2c3d4e5f6a7b8c9d00"
        o, welcome = self.connect(OWNER + ":boss")
        self.assertEqual((welcome["you"]["role"], welcome["you"]["mod"]), ("admin", True))
        a, welcome = self.connect("1001:alice")
        self.assertEqual(self.you_frames(a, 2.0), [])
        self.assertEqual(self.you_frames(o, 0.1), [])
        a.say("hello")
        self.assertEqual([m["text"] for m in o.drain(0.5) if m["t"] == "line"], ["hello"])
        self.assertEqual(len(self.fake.member_times), 1)                          # (the owner account was never asked about: only alice, once, and refused)


class TheSecrets(unittest.TestCase):
    def test_no_secret_is_in_the_text_of_the_settings_the_roles_or_the_errors(self):
        cfg = config.load(dict(ENV, ANTS_HUB_OWNER=OWNER))
        roles = rolesmod.Roles(cfg, limitmod.Limits(), Clock(), Log())
        for text in (repr(cfg), str(cfg), json.dumps(cfg.summary()), repr(roles), str(roles), repr(roles.entries), repr(vars(roles.client).keys())):
            for secret in SECRETS + (OWNER,):
                self.assertNotIn(secret, text)
        self.assertEqual(cfg.summary(), {"port": 4040, "origin": "none", "roles_configured": 1, "owner": 1, "recheck": 180, "test_login": 0, "login_configured": 0, "min_account_days": 7})
        for name, value in (("ANTS_HUB_DISCORD_BOT_TOKEN", "canary token with blanks " + TOKEN), ("ANTS_HUB_DISCORD_BOT_TOKEN", "short"), ("ANTS_HUB_DISCORD_GUILD", "canary-guild"),
                            ("ANTS_HUB_ROLE_ADMIN", "canary-role"), ("ANTS_HUB_ROLE_MOD", "0123456"), ("ANTS_HUB_OWNER", "canary-owner"), ("ANTS_HUB_ROLE_RECHECK_SECONDS", "canary")):
            with self.assertRaises(config.ConfigError, msg=name) as caught:
                config.load(dict(ENV, **{name: value}))
            self.assertIn(name, str(caught.exception))
            self.assertNotIn(value, str(caught.exception))
            self.assertNotIn("canary", str(caught.exception))

    def test_a_ban_a_mute_or_a_line_never_holds_a_role_in_the_database(self):
        with hub_rig.Rig() as rig:
            names = [t[0] for t in rig.call(lambda: rig.store.db.execute("select name from sqlite_master where type='table'").fetchall())]
        self.assertFalse([n for n in names if "role" in n], names)

    def test_the_whole_run_of_the_real_process_with_every_failure_of_discord_leaves_no_secret_in_its_log(self):
        fake = fd.FakeDiscord()
        self.addCleanup(fake.close)
        first, second = "4100000000000101", "4100000000000102"
        fake.members = {first: [MOD], second: [ADMIN]}
        extra = dict(ENV, ANTS_HUB_TEST_LOGIN="1", ANTS_HUB_TEST_DISCORD_URL=fake.url, ANTS_HUB_ROLE_RECHECK_SECONDS="60", ANTS_HUB_OWNER=OWNER)
        proc, folder, log_path, env = ths.start_process(extra)
        clients, frames = [], []
        try:
            port = ths.wait_for_port(proc, log_path)
            self.assertIsNotNone(port, ths.read_text(log_path))
            for mode in ("ok", "err:403", "ok", "rate:1", "ok", "status:500", "ok", "err:404:10004", "notjson", "err:401"):
                fake.member_mode = mode
                c = hc.Client(port, login=first + ":alice.canary")
                clients.append(c)
                frames.append(c.recv_json())
                frames += c.drain(0.4)
                c.close()                                                            # (eight sockets for each address)
            fake.member_mode = "ok"
            o = hc.Client(port, login=OWNER + ":boss.canary")
            clients.append(o)
            frames.append(o.recv_json())
            self.assertEqual(frames[-1]["you"]["role"], "admin")
            assert_no_id(self, json.dumps(frames).replace(first, "").replace(OWNER, ""), "the frames")        # (the ends of the ids too: none of them is in a frame)
            for account in (first, second, OWNER):
                self.assertNotIn(account, json.dumps(frames))
            status, health = hc.http_get(port, "/hub/healthz")
            self.assertEqual((status, health), (200, b'{"ok":true}'))
            for secret in SECRETS:
                self.assertNotIn(secret.encode(), health)
            proc.send_signal(signal.SIGTERM)
            proc.wait(10)
            text = ths.read_text(log_path)
            self.assertIn(" start ", text)
            self.assertRegex(text, r" start port=0 origin=\S+ roles_configured=1 owner=1 recheck=60 ")
            self.assertGreaterEqual(len(fake.member_times), 1)
            for secret in SECRETS + (OWNER, first, second, "alice.canary", "boss.canary", "canary-error-body", "Traceback", folder):
                self.assertNotIn(secret, text, "the log shows " + secret)
            for line in text.splitlines():
                self.assertRegex(line, r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ [a-z_]+( [a-z_]+=[A-Za-z0-9_.:/-]*)*$", line)
            self.assertNotIn("#", text)                                          # (the log's scrubber replaces a long number with #: it never had anything to replace)
        finally:
            for c in clients:
                c.close()
            if proc.poll() is None:
                proc.kill()
            proc.wait(5)


ID = "7311000099990001"                          # a made-up Discord id of a person: like a password, it is in no log line, frame, error, repr or traceback (what is looked for: all of it, and its two ends)
PARTS = (ID, ID[:8], ID[-8:])


def assert_no_id(test, text, where):
    for part in PARTS:
        test.assertNotIn(part, text, "%s holds the id (%s)" % (where, part))


class TheIdIsLikeAPassword(SetupTest):
    def test_no_state_of_the_roles_prints_the_id_and_no_failure_leaves_it_in_the_log(self):
        cases = dict(FAILURES, **{"ok": lambda a: good(a, [MOD]), "refused token": lambda a: (401, {"code": 0}, {}), "refused access": lambda a: (403, {"code": 50001}, {}),
                                  "refused guild": lambda a: (404, {"code": 10004}, {}), "429": lambda a: (429, {"retry_after": 3.0}, {"retry_after": 3.0}),
                                  "an error with the id in it": lambda a: RuntimeError("GET /guilds/x/members/" + a), "deleted": lambda a: (404, {"code": 10013}, {}),
                                  "not a member": lambda a: NOT_A_MEMBER})
        for name, answer in cases.items():
            s = self.setup(members={ID: [MOD]})
            s.mode = answer
            s.connect(ID, "1002")
            s.run(700)
            for what, text in (("log", s.log.text()), ("roles", repr(s.roles) + str(s.roles)), ("entries", repr(s.roles.entries) + str(s.roles.entries)),
                               ("entry", "".join(repr(e) + str(e) for e in s.roles.entries.values()))):
                assert_no_id(self, text, "%s of %s" % (what, name))

    def test_an_error_of_the_call_has_a_kind_and_a_status_and_no_cause_that_could_hold_the_address(self):
        cfg = config.load(dict(ENV, ANTS_HUB_TEST_DISCORD_URL="http://127.0.0.1:4999"))
        client = discordmod.Discord(cfg, limitmod.Limits())
        for error in (RuntimeError("GET /guilds/%s/members/%s" % (GUILD, ID)), ValueError(ID), OSError("connect to /members/" + ID), KeyError(ID), AssertionError(ID), UnicodeError(ID)):
            async def fails(*args, _error=error, **kw):
                raise _error
            with mock.patch.object(discordmod.Discord, "exchange", fails):
                with self.assertRaises(discordmod.DiscordError) as caught:
                    asyncio.run(client.member(GUILD, ID, TOKEN))
            e = caught.exception
            self.assertEqual((e.kind, e.__cause__, e.__context__), ("down", None, None), repr(error))
            text = "".join(traceback.format_exception(e)) + str(e) + repr(e)
            assert_no_id(self, text, "the traceback of " + repr(error))
            self.assertNotIn(TOKEN, text)

    def test_the_log_replaces_any_long_number_a_caller_puts_in_it(self):
        from hub.log import Log
        out = io.StringIO()
        Log(out).event("note", a=ID, b="x" + ID + "y", c="id=" + ID, d=[ID], e=ID[:14])
        text = out.getvalue()
        self.assertNotIn(ID, text)
        self.assertNotIn(ID[:15], text)
        self.assertIn("a=# b=x#y c=id_# d=__#__ e=" + ID[:14], text)             # (the number is replaced; a shorter one is a number)
        self.assertEqual(len(text.splitlines()), 1)

    def test_the_report_of_a_worker_that_ended_has_no_text_in_it(self):
        s = self.setup(members={"1001": [MOD]})
        records = []

        class Catch(logging.Handler):
            def emit(self, record):
                records.append(self.format(record) + "".join(traceback.format_exception(*record.exc_info)) if record.exc_info else self.format(record))
        handler = Catch()
        logging.getLogger("asyncio").addHandler(handler)
        self.addCleanup(logging.getLogger("asyncio").removeHandler, handler)

        async def dies(self):
            raise RuntimeError("worker with the id " + ID)

        async def scenario():
            with mock.patch.object(rolesmod.Roles, "run", dies):
                s.roles.start()
                await asyncio.sleep(0.05)
                s.roles.on_tick(s.clock.t)                                       # (ended: its error is read, and dropped with the task, and a new one starts and ends at once)
                await asyncio.sleep(0.05)
                s.roles.on_tick(s.clock.t + 1)
                s.roles.stop()
        s.loop.run_until_complete(scenario())
        gc.collect()
        s.loop.run_until_complete(asyncio.sleep(0.05))
        gc.collect()
        self.assertEqual(records, [], "asyncio reported: %r" % records)
        self.assertEqual(s.log.named("error"), [{"kind": "worker_ended", "where": "roles"}])

    def test_the_report_of_a_task_nobody_read_is_its_class_and_nothing_else(self):
        from hub import server
        log = Log()
        server.scrubbed_error(log, {"message": "Task exception was never retrieved " + ID, "exception": RuntimeError("the id " + ID), "future": object()})
        server.scrubbed_error(log, {"message": "something " + ID})
        self.assertEqual(log.events, [("error", {"kind": "RuntimeError", "where": "loop"}), ("error", {"kind": "none", "where": "loop"})])
        assert_no_id(self, log.text(), "the report")

    def test_the_stderr_of_the_real_process_has_no_id_when_a_worker_and_a_task_die(self):
        code = ("import asyncio, os, sys\nsys.path.insert(0, %r)\nfrom hub import roles, server\nplant = os.environ['PLANT']\n"
                "async def dies(self):\n    raise RuntimeError('worker with the id ' + plant)\nroles.Roles.run = dies\n"
                "start = server.Hub.start\nasync def started(self, host, port):\n    result = await start(self, host, port)\n"
                "    async def stray():\n        raise RuntimeError('task with the id ' + plant)\n    asyncio.ensure_future(stray())\n    return result\n"
                "server.Hub.start = started\nraise SystemExit(server.main())\n") % REPO
        extra = dict(ENV, PLANT=ID, ANTS_HUB_TEST_LOGIN="1")
        proc, folder, log_path, env = ths.start_process(extra)
        proc.kill()
        proc.wait(5)
        env["PYTHONUNBUFFERED"] = "1"
        with open(log_path, "wb") as out:
            proc = __import__("subprocess").Popen([sys.executable, "-c", code], cwd=REPO, env=env, stdout=out, stderr=__import__("subprocess").STDOUT)
        try:
            port = ths.wait_for_port(proc, log_path)
            self.assertIsNotNone(port, ths.read_text(log_path))
            time.sleep(3.5)                                                      # (the clock sees the ended task, starts it again, and the stray task is collected)
            proc.send_signal(signal.SIGTERM)
            proc.wait(10)
        finally:
            if proc.poll() is None:
                proc.kill()
            proc.wait(5)
        text = ths.read_text(log_path)
        assert_no_id(self, text, "stdout and stderr of the process")
        self.assertNotIn("never retrieved", text)
        self.assertIn(" error kind=worker_ended where=roles", text)
        self.assertIn(" error kind=RuntimeError where=loop", text)

    def test_no_frame_no_log_and_no_answer_of_the_hub_holds_the_id_in_any_failure_of_discord(self):
        fake = fd.FakeDiscord()
        self.addCleanup(fake.close)
        fake.members = {ID: [ADMIN]}
        with hub_rig.Rig(member_fake=fake, recheck=1, **LiveTest.options) as rig:
            seen = []
            for n, mode in enumerate(("ok", "err:403", "ok", "rate:1", "ok", "status:500", "notjson", "close", "err:404:10004", "err:401", "ok")):
                fake.member_mode = mode
                c = hc.Client(rig.port, login=ID + ":idowner")
                try:
                    seen += [c.recv_json()] + c.drain(1.3)
                    c.say("a line %d" % n)
                    seen += c.drain(0.2)
                finally:
                    c.close()
                time.sleep(0.05)
            status, health = hc.http_get(rig.port, "/hub/healthz")
            log = rig.log_text.getvalue()
        self.assertIn("you", {m["t"] for m in seen})
        assert_no_id(self, json.dumps(seen), "the frames")
        assert_no_id(self, health.decode(), "healthz")
        assert_no_id(self, log, "the log")
        self.assertNotIn("#", log)                                               # (the log's scrubber replaces a long number with #: it must never have had anything to replace)
        self.assertNotIn("#", json.dumps(seen))


class TheSettings(unittest.TestCase):
    def test_the_defaults_and_the_limits(self):
        cfg = config.load(ENV)
        self.assertEqual((cfg.roles_on, cfg.role_recheck_seconds, cfg.owner, cfg.roles_partial), (True, 180, "", False))
        self.assertEqual(config.load({"ANTS_HUB_ROLE_RECHECK_SECONDS": ""}).role_recheck_seconds, 180)
        for good_value in ("60", "180", "3600", " 90 "):
            self.assertEqual(config.load({"ANTS_HUB_ROLE_RECHECK_SECONDS": good_value}).role_recheck_seconds, int(good_value))
        for bad in ("59", "3601", "0", "-1", "x", "1.5"):
            with self.assertRaises(config.ConfigError, msg=bad):
                config.load({"ANTS_HUB_ROLE_RECHECK_SECONDS": bad})

    def test_the_roles_are_on_with_a_token_a_server_and_one_role_and_partial_otherwise(self):
        self.assertFalse(config.load({}).roles_on)
        self.assertFalse(config.load({}).roles_partial)
        for drop in ("ANTS_HUB_DISCORD_BOT_TOKEN", "ANTS_HUB_DISCORD_GUILD"):
            cfg = config.load({k: v for k, v in ENV.items() if k != drop})
            self.assertEqual((cfg.roles_on, cfg.roles_partial), (False, True), drop)
        cfg = config.load({k: v for k, v in ENV.items() if k not in ("ANTS_HUB_ROLE_ADMIN", "ANTS_HUB_ROLE_MOD")})
        self.assertEqual((cfg.roles_on, cfg.roles_partial), (False, True))
        self.assertEqual(config.load(dict(ENV, ANTS_HUB_ROLE_MOD="")).roles_on, True)
        self.assertEqual(config.load({"ANTS_HUB_OWNER": OWNER}).roles_partial, False)           # (the owner account alone is no half of the roles)

    def test_the_two_roles_are_two(self):
        with self.assertRaises(config.ConfigError):
            config.load(dict(ENV, ANTS_HUB_ROLE_MOD=ADMIN))

    def test_the_roles_need_no_login_and_the_login_needs_no_roles(self):
        cfg = config.load(dict(ENV, ANTS_HUB_TEST_LOGIN="1", ANTS_HUB_TEST_DISCORD_URL="http://127.0.0.1:4999"))
        self.assertEqual((cfg.roles_on, cfg.test_login, cfg.login_on), (True, True, False))

    def test_the_process_says_when_the_roles_are_half_set_and_a_leftover_hand_list_does_nothing(self):
        proc, folder, log_path, env = ths.start_process({"ANTS_HUB_DISCORD_BOT_TOKEN": TOKEN, "ANTS_HUB_DISCORD_GUILD": GUILD, "ANTS_HUB_MODS": "5559876543", "ANTS_HUB_TEST_LOGIN": "1"})
        try:
            port = ths.wait_for_port(proc, log_path)
            self.assertIsNotNone(port, ths.read_text(log_path))
            c = hc.Client(port, login="5559876543:oldmod")
            try:
                self.assertEqual(c.recv_json()["you"]["role"], "none")
            finally:
                c.close()
            proc.send_signal(signal.SIGTERM)
            proc.wait(10)
            text = ths.read_text(log_path)
            self.assertEqual(text.count(" warning what=discord_roles_off_needs_token_server_and_a_role"), 1)
            self.assertEqual(text.count(" warning what=ANTS_HUB_MODS_is_no_longer_read_roles_come_from_discord"), 1)
            self.assertIn(" roles_configured=0 ", text)
            for secret in SECRETS + ("5559876543",):
                self.assertNotIn(secret, text)
        finally:
            if proc.poll() is None:
                proc.kill()
            proc.wait(5)

    def test_the_limits_of_the_roles_are_in_the_documents(self):
        with open(os.path.join(REPO, "docs", "HUB.md"), encoding="utf-8") as f:
            doc = f.read()
        limits = limitmod.Limits()
        for needle in ("two a second", "a burst of 5", "10 minutes", "a tenth", "ANTS_HUB_ROLE_RECHECK_SECONDS", "ANTS_HUB_DISCORD_BOT_TOKEN", "ANTS_HUB_OWNER", "60 to 3600"):
            self.assertIn(needle, doc, needle)
        self.assertEqual((limits.roles_burst, limits.roles_refill_seconds, limits.roles_grace_seconds, limits.roles_pause_seconds, limits.roles_jitter, limits.roles_hold_max_seconds),
                         (5, 0.5, 60.0, 600.0, 0.1, 60.0))
        self.assertEqual(doc.count("ANTS_HUB_MODS"), 1)                          # (the one line that says that a leftover value is not read)


if __name__ == "__main__":
    unittest.main()
