#!/usr/bin/env python3
"""A logged-in person's Discord picture arrives and changes with no new login (hub/pictures.py `refresh`, `want`, `absent`, `lookup`; hub/roles.py, which hands over the picture code that its member
answer carries; hub/discord.py `user()`; docs/HUB_PICTURES.md "Refreshing"; run by ./run_tests.sh --fast and by the CI), against a FAKE Discord with a fake picture host
(tests/scripts/fake_discord.py): real sockets, the real hub, the real roles' check. Real Discord is never asked.

  - a login from before (no picture) gets one at the next check of the roles; a changed code replaces the picture; the same code is no fetch; no picture at Discord deletes the copy; a code that is
    not the shape of one is ignored;
  - a switched off, hidden or banned account is not fetched for, and nothing is kept;
  - a person who is not in the server is asked about with the bot's token, once, and not again within the day (and again after it); where roles are off but the bot's token is set the same;
    with no token (or with no roles and no token) nothing new happens;
  - every failure of the lookup keeps what there was; the lookups take their turns from the roles' bucket, and stop when the roles are stopped;
  - what is remembered to tell a changed code is a keyed mark in memory: the schema is where it was, and no id, code, mark, reference or token is in any log line.
"""
import hashlib
import json
import os
import sqlite3
import sys
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
import fake_discord as fd  # noqa: E402
import hub_client as hc  # noqa: E402
import hub_rig  # noqa: E402
import test_hub_pictures as T  # noqa: E402
from hub import pseudo  # noqa: E402
from hub.store import base  # noqa: E402

ACCOUNT, OTHER = T.ACCOUNT, T.OTHER
AVATAR, AVATAR2 = fd.AVATAR, "fedcba9876543210fedcba9876543210"
ROLES_ON = {"ANTS_HUB_DISCORD_BOT_TOKEN": fd.BOT_TOKEN, "ANTS_HUB_DISCORD_GUILD": fd.GUILD_ID, "ANTS_HUB_ROLE_ADMIN": fd.ROLE_ADMIN, "ANTS_HUB_ROLE_MOD": fd.ROLE_MOD}
TOKEN_ONLY = {"ANTS_HUB_DISCORD_BOT_TOKEN": fd.BOT_TOKEN}
FAST = dict(tick_seconds=0.2)
PNG1, PNG2 = fd.make_png(), fd.make_png(color=6)


class RefreshTest(T.PictureTest):
    env = ROLES_ON
    recheck = 1
    limits = FAST

    def start(self):
        self.fake = fd.FakeDiscord(T.CALLBACK, user=(ACCOUNT, "maple.leaf"))
        self.fake.consent = False
        self.fake.avatar = None                       # (a login that brings no picture: what a login from before looked like)
        self.fake.picture = PNG1
        self.rig = hub_rig.Rig(discord=self.fake, env=self.env, recheck=self.recheck, **dict(dict(sockets_per_address=100), **self.limits))
        self.rig.__enter__()
        self.count = 0
        self.sockets = []

    def finish(self):
        for c in self.sockets:
            c.close()
        super().finish()

    # ---- helpers -----------------------------------------------------------------------------------------------------------------------------

    def pictures(self):
        return self.rig.hub.sessions.pictures

    def connect(self, visitor):
        c = hc.Client(self.rig.port, headers={"Sec-Fetch-Site": "same-origin", "Cookie": visitor.jar.header()})
        self.assertTrue(c.ok)
        self.sockets.append(c)
        c.recv_json()
        return c

    def until(self, condition, what, seconds=20.0):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if condition():
                return
            time.sleep(0.05)
        self.fail("never: " + what + "\n" + self.log())

    def png(self):
        kept = self.kept()
        return kept[0][3] if kept else None

    def checks(self, count=1, since=0):
        """Wait until the roles' check (or the lookup) has asked about the account `count` more times than `since`."""
        self.until(lambda: len(self.fake.member_ids) >= since + count, "%d checks" % count)

    def member(self, avatar=AVATAR, roles=()):
        self.fake.members[ACCOUNT] = list(roles)
        self.fake.member_avatars[ACCOUNT] = avatar

    def poison(self):
        secret = self.rig.call(self.rig.store.secret)
        words = [ACCOUNT, OTHER, AVATAR, AVATAR2, fd.BOT_TOKEN, fd.GUILD_ID, pseudo.reference(secret, ACCOUNT), pseudo.reference(secret, OTHER)]
        words += [pseudo.keyed(secret, "picture-code", c) for c in (AVATAR, AVATAR2)]
        words += [h(ACCOUNT.encode()).hexdigest() for h in (hashlib.sha256, hashlib.sha1, hashlib.md5)]
        return words

    def log_is_clean(self):
        text = self.log()
        for word in self.poison():
            self.assertNotIn(word, text, "the log holds %r" % word[:6])


class TheRolesCheckRefreshes(RefreshTest):
    def test_a_login_from_before_with_no_picture_gets_one_at_the_next_check(self):
        v = self.login()
        self.assertEqual(self.kept(), [])
        self.assertEqual(self.asked(), [])
        self.member()
        self.connect(v)
        self.until(lambda: self.png() == PNG1, "the picture")
        self.assertEqual(len(self.asked()), 1)
        self.assertEqual(self.get(self.name()).status, 200)
        self.assertEqual(self.fake.user_ids, [])                    # (a member is asked about by the roles' check, and never by the profile call)

    def test_a_changed_code_replaces_the_picture_and_its_name(self):
        self.fake.avatar = AVATAR
        v = self.login()
        self.until(lambda: self.png() == PNG1, "the first picture")
        first = self.name()
        self.member(AVATAR)
        self.connect(v)
        self.checks(2)
        self.assertEqual(len(self.asked()), 1)                       # (the same code: no second fetch)
        self.fake.picture = PNG2
        self.member(AVATAR2)
        self.until(lambda: self.png() == PNG2, "the new picture")
        self.assertNotEqual(self.name(), first)
        self.assertEqual(self.get(first).status, 404)
        self.assertIn("/%s.png" % AVATAR2, self.asked()[-1][1])

    def test_the_same_code_is_no_fetch_at_any_check(self):
        self.fake.avatar = AVATAR
        v = self.login()
        self.until(lambda: self.png() == PNG1, "the picture")
        self.member(AVATAR)
        self.connect(v)
        self.checks(4)
        time.sleep(0.3)
        self.assertEqual(len(self.asked()), 1)
        self.assertEqual(T.kinds_of(self.log()), ["ok"])

    def test_no_picture_at_discord_deletes_the_copy_and_asks_for_none(self):
        self.fake.avatar = AVATAR
        v = self.login()
        self.until(lambda: self.png() == PNG1, "the picture")
        self.member(None)
        self.connect(v)
        self.until(lambda: self.kept() == [], "the copy gone")
        self.assertEqual(len(self.asked()), 1)
        self.assertEqual(T.kinds_of(self.log()), ["ok", "none"])

    def test_a_code_that_is_not_the_shape_of_one_is_ignored(self):
        self.fake.avatar = AVATAR
        v = self.login()
        self.until(lambda: self.png() == PNG1, "the picture")
        for bad in ("canary-bad-code", "../../x", AVATAR.upper(), 12345, ["a"], ""):
            self.fake.member_avatars[ACCOUNT] = bad
            self.fake.members[ACCOUNT] = []
        self.connect(v)
        self.checks(3)
        self.assertEqual((self.png(), len(self.asked())), (PNG1, 1))
        self.assertEqual(T.kinds_of(self.log()), ["ok"])

    def test_a_picture_that_is_not_what_the_host_sends_leaves_the_letter_and_is_asked_again_at_the_next_check(self):
        v = self.login()
        self.member()
        self.fake.cdn_mode = "500"
        self.connect(v)
        self.until(lambda: len(self.asked()) >= 2, "a second try")
        self.assertEqual(self.kept(), [])
        self.fake.cdn_mode = "ok"
        self.until(lambda: self.png() == PNG1, "the picture")


class TheBackOff(RefreshTest):
    def test_a_picture_that_is_not_one_is_not_asked_for_again_at_every_check_and_a_new_code_is(self):
        self.fake.picture = b"this is not a picture"
        v = self.login()
        self.member()
        self.connect(v)
        self.until(lambda: len(self.asked()) >= 1, "a fetch")
        self.checks(4, len(self.fake.member_ids))
        self.assertEqual(len(self.asked()), 1)
        self.assertEqual(T.kinds_of(self.log()), ["none", "shape"])
        self.fake.picture = PNG1
        self.member(AVATAR2)
        self.until(lambda: self.png() == PNG1, "the picture of the new code")
        self.assertEqual(len(self.asked()), 2)

    def test_a_picture_that_is_gone_at_the_image_host_is_not_asked_for_again_at_every_check(self):
        v = self.login()
        self.member()
        self.fake.cdn_mode = "404"
        self.connect(v)
        self.until(lambda: len(self.asked()) >= 1, "a fetch")
        self.checks(4, len(self.fake.member_ids))
        self.assertEqual(len(self.asked()), 1)

    def test_an_account_that_discord_does_not_know_is_not_looked_up_at_every_check(self):
        v = self.login()
        self.connect(v)
        self.until(lambda: len(self.fake.user_ids) >= 1, "a lookup")
        self.checks(4, len(self.fake.member_ids))
        self.assertEqual(self.fake.user_ids, [ACCOUNT])

    def test_a_wait_that_discord_asks_for_on_the_profiles_holds_the_roles_lookups_too(self):
        self.fake.user_mode = "rate:30"
        v = self.login()
        self.connect(v)
        self.until(lambda: len(self.fake.user_ids) >= 1, "a lookup")
        time.sleep(0.3)
        roles = self.rig.hub.chat.roles
        self.assertGreater(roles.hold_until, roles.clock() + 10)

    def test_the_marks_of_a_look_and_of_a_failure_go_with_the_copy_and_with_the_day(self):
        self.fake.avatar = AVATAR
        self.login()
        self.until(lambda: self.png() == PNG1, "the picture")
        p = self.pictures()
        self.rig.call(p.ask, ACCOUNT)
        self.assertEqual(len(self.rig.call(lambda: dict(p.looked))), 1)
        self.store("ban", ACCOUNT, "command line", "maple.leaf")
        self.until(lambda: self.rig.call(lambda: (p.looked, p.codes)) == ({}, {}), "the marks gone with the copy")

        def aged():
            p.note_look("r1")
            p.failed["r2"] = ("m", p.clock())
            p.forget_idle(p.clock() + p.limits.picture_lookup_seconds + 1)
            return p.looked, p.failed
        self.assertEqual(self.rig.call(aged), ({}, {}))

        def young():
            p.note_look("r1")
            p.failed["r2"] = ("m", p.clock())
            p.forget_idle(p.clock() + 10)
            return sorted(p.looked), sorted(p.failed)
        self.assertEqual(self.rig.call(young), (["r1"], ["r2"]))


class TheEndsStopTheFetch(RefreshTest):
    def refresh(self, avatar=AVATAR):
        self.rig.call(self.pictures().refresh, ACCOUNT, avatar)
        time.sleep(0.6)

    def test_a_switched_off_account_is_not_fetched_for_and_nothing_is_kept(self):
        self.login()
        self.assertEqual(self.store("set_picture_show", ACCOUNT, base.SHOW_OFF), base.SHOW_OFF)
        self.refresh()
        self.assertEqual((self.asked(), self.kept()), ([], []))
        self.assertEqual([r[1] for r in self.rows()], [base.SHOW_OFF])

    def test_a_hidden_account_is_not_fetched_for_and_nothing_is_kept(self):
        self.login()
        self.store("set_picture_show", ACCOUNT, base.SHOW_BLOCKED, True)
        self.refresh()
        self.assertEqual((self.asked(), self.kept()), ([], []))
        self.assertEqual([r[1] for r in self.rows()], [base.SHOW_BLOCKED])

    def test_a_banned_account_is_not_fetched_for_and_nothing_is_kept(self):
        self.login()
        self.store("ban", ACCOUNT, "command line", "maple.leaf")
        self.refresh()
        self.assertEqual((self.asked(), self.kept()), ([], []))

    def test_no_lookup_of_the_profile_is_queued_for_a_switched_off_hidden_or_banned_account(self):
        self.login()
        self.fake.profiles[ACCOUNT] = AVATAR
        for state in (base.SHOW_OFF, base.SHOW_BLOCKED):
            self.store("set_picture_show", ACCOUNT, state, True)
            self.rig.call(self.pictures().ask, ACCOUNT)
        self.store("set_picture_show", ACCOUNT, base.SHOW_ON, True)
        self.store("ban", ACCOUNT, "command line", "maple.leaf")
        self.rig.call(self.pictures().ask, ACCOUNT)
        time.sleep(0.8)
        self.assertEqual((self.fake.user_ids, self.kept()), ([], []))

    def test_an_account_with_no_login_is_not_fetched_for_and_not_looked_up_and_nothing_is_kept(self):
        self.fake.profiles[ACCOUNT] = AVATAR
        self.assertFalse(self.rig.call(self.rig.store.has_login, ACCOUNT))
        self.refresh()
        self.rig.call(self.pictures().absent, ACCOUNT)
        self.rig.call(self.pictures().want, ACCOUNT)
        time.sleep(0.6)
        self.assertEqual((self.asked(), self.fake.user_ids, self.kept()), ([], [], []))
        self.assertEqual((self.pictures().codes, self.pictures().looked), ({}, {}))
        self.login()
        self.assertTrue(self.rig.call(self.rig.store.has_login, ACCOUNT))

    def test_a_person_whose_login_is_over_is_not_fetched_for_or_looked_up(self):
        self.fake.avatar = AVATAR
        v = self.login()
        self.until(lambda: self.png() == PNG1, "the picture")
        self.fake.profiles[ACCOUNT] = AVATAR2
        self.fake.picture = PNG2
        self.assertEqual(v.call("POST", "/hub/logout").status, 200)
        self.until(lambda: self.kept() == [], "the copy gone with the login")
        self.assertFalse(self.rig.call(self.rig.store.has_login, ACCOUNT))
        self.rig.call(self.pictures().absent, ACCOUNT)
        self.refresh(AVATAR2)
        time.sleep(0.5)
        self.assertEqual((len(self.asked()), self.fake.user_ids, self.kept()), (1, [], []))

    def test_a_fetch_that_ends_after_the_switch_keeps_nothing_and_no_mark(self):
        self.login()
        self.fake.cdn_mode = "slow:0.6"
        self.rig.call(self.pictures().refresh, ACCOUNT, AVATAR)
        time.sleep(0.2)
        self.store("set_picture_show", ACCOUNT, base.SHOW_OFF)
        time.sleep(1.0)
        self.assertEqual(self.kept(), [])
        self.assertEqual(self.pictures().codes, {})

    def test_a_ban_takes_the_copy_and_the_mark(self):
        self.fake.avatar = AVATAR
        v = self.login()
        self.until(lambda: self.png() == PNG1, "the picture")
        self.assertEqual(len(self.pictures().codes), 1)
        self.store("ban", ACCOUNT, "command line", "maple.leaf")
        self.until(lambda: self.pictures().codes == {} and self.kept() == [], "the mark and the copy gone")


class TheQueueIsLookedAtAgain(RefreshTest):
    """A request that waited in the queue is not made when the picture went off, was hidden, the person was banned or logged out while it waited."""

    def hold(self):
        p = self.pictures()
        self.rig.call(lambda: setattr(p, "inflight", p.limits.picture_inflight))

    def release(self):
        p = self.pictures()

        def go():
            p.inflight = 0
            p.pump()
        self.rig.call(go)
        time.sleep(1.0)

    def stop(self, how):
        if how == "off":
            self.store("set_picture_show", ACCOUNT, base.SHOW_OFF)
        elif how == "hidden":
            self.store("set_picture_show", ACCOUNT, base.SHOW_BLOCKED, True)
        else:
            self.store("ban", ACCOUNT, "command line", "maple.leaf")

    def test_a_lookup_that_waited_is_not_made_after_the_picture_went_off_or_was_hidden_or_the_person_was_banned(self):
        for how in ("off", "hidden", "banned"):
            with self.subTest(how=how):
                self.restart(**FAST)
                self.login()
                self.fake.profiles[ACCOUNT] = AVATAR
                self.hold()
                self.rig.call(self.pictures().ask, ACCOUNT)
                self.assertEqual(self.rig.call(lambda: len(self.pictures().waiting)), 1)
                self.stop(how)
                self.release()
                self.assertEqual((self.fake.user_ids, self.kept()), ([], []))

    def test_nothing_is_queued_for_a_stopped_account_or_one_with_no_login(self):
        self.fake.profiles[ACCOUNT] = AVATAR
        self.hold()
        p = self.pictures()
        self.rig.call(p.ask, ACCOUNT)                                           # (no login yet)
        self.assertEqual(self.rig.call(lambda: len(p.waiting)), 0)
        self.login()
        for how in ("off", "hidden", "banned"):
            with self.subTest(how=how):
                self.stop(how)
                self.rig.call(p.ask, ACCOUNT)
                self.assertEqual(self.rig.call(lambda: len(p.waiting)), 0)
                self.store("set_picture_show", ACCOUNT, base.SHOW_ON, True)
                self.store("unban", ACCOUNT)
        self.rig.call(p.ask, ACCOUNT)
        self.assertEqual(self.rig.call(lambda: len(p.waiting)), 1)              # (and one that may be looked up is queued)

    def test_a_fetch_that_waited_is_not_made_after_the_picture_went_off_or_was_hidden_or_the_person_was_banned(self):
        for how in ("off", "hidden", "banned"):
            with self.subTest(how=how):
                self.restart(**FAST)
                self.login()
                self.hold()
                self.rig.call(self.pictures().refresh, ACCOUNT, AVATAR)
                self.assertEqual(self.rig.call(lambda: len(self.pictures().waiting)), 1)
                self.stop(how)
                self.release()
                self.assertEqual((self.asked(), self.kept()), ([], []))

    def test_a_fetch_of_a_login_that_waited_is_not_made_after_the_picture_went_off(self):
        self.fake.avatar = AVATAR
        self.hold()
        self.login()
        self.assertEqual(self.rig.call(lambda: len(self.pictures().waiting)), 1)
        self.stop("off")
        self.release()
        self.assertEqual((self.asked(), self.kept()), ([], []))

    def test_a_fetch_that_waited_for_its_turn_is_not_made_after_the_person_was_banned(self):
        self.restart(picture_burst=1, picture_refill_seconds=3.0, **FAST)
        self.fake.avatar = AVATAR
        self.login()
        self.until(lambda: self.png() == PNG1, "the first picture")
        other = self.login((OTHER, "other.one"))
        self.until(lambda: len(self.asked()) >= 1, "the first fetch")
        self.rig.call(self.pictures().refresh, ACCOUNT, AVATAR2)
        time.sleep(0.3)
        self.store("ban", ACCOUNT, "command line", "maple.leaf")
        time.sleep(4.0)
        self.assertNotIn("/%s.png" % AVATAR2, "".join(r[1] for r in self.asked()))


class TheProfileLookup(RefreshTest):
    def test_a_person_who_is_not_in_the_server_is_asked_about_once_and_not_again_within_the_day(self):
        v = self.login()
        self.fake.profiles[ACCOUNT] = AVATAR
        self.connect(v)
        self.until(lambda: self.png() == PNG1, "the picture")
        self.assertEqual(self.fake.user_ids, [ACCOUNT])
        before = len(self.fake.member_ids)
        self.checks(4, before)
        time.sleep(0.3)
        self.assertEqual(self.fake.user_ids, [ACCOUNT])
        self.assertEqual(len(self.asked()), 1)
        self.connect(v)                                                         # (a second socket is not a second lookup)
        time.sleep(0.5)
        self.assertEqual(self.fake.user_ids, [ACCOUNT])

    def test_the_lookup_is_made_again_after_the_day_and_a_changed_code_replaces_the_picture(self):
        self.restart(picture_lookup_seconds=1.5, **FAST)
        v = self.login()
        self.fake.profiles[ACCOUNT] = AVATAR
        self.connect(v)
        self.until(lambda: self.png() == PNG1, "the picture")
        self.fake.picture = PNG2
        self.fake.profiles[ACCOUNT] = AVATAR2
        self.until(lambda: self.png() == PNG2, "the new picture")
        self.assertEqual(len(self.fake.user_ids), 2)
        self.assertEqual(self.fake.user_ids, [ACCOUNT, ACCOUNT])

    def test_a_person_with_a_copy_from_the_login_just_made_is_not_looked_up_at_the_first_connect(self):
        self.fake.avatar = AVATAR
        v = self.login()
        self.until(lambda: self.png() == PNG1, "the picture")
        self.connect(v)
        self.checks(2)
        time.sleep(0.3)
        self.assertEqual((self.fake.user_ids, len(self.asked())), ([], 1))

    def test_a_person_who_has_no_picture_at_discord_loses_the_copy(self):
        self.restart(picture_lookup_seconds=1.5, **FAST)
        self.fake.avatar = AVATAR
        v = self.login()
        self.until(lambda: self.png() == PNG1, "the picture")
        self.fake.profiles[ACCOUNT] = None
        self.connect(v)
        self.until(lambda: self.kept() == [], "the copy gone")
        self.assertEqual(self.fake.user_ids[0], ACCOUNT)

    def test_an_account_that_discord_does_not_know_keeps_what_it_had(self):
        self.restart(picture_lookup_seconds=1.5, **FAST)
        self.fake.avatar = AVATAR
        v = self.login()
        self.until(lambda: self.png() == PNG1, "the picture")
        self.connect(v)
        self.until(lambda: len(self.fake.user_ids) >= 1, "a lookup")
        self.assertEqual(self.png(), PNG1)

    def test_an_account_is_asked_about_by_its_own_id_only_and_the_request_is_a_plain_get_with_the_bots_token(self):
        v = self.login()
        self.fake.profiles[ACCOUNT] = AVATAR
        self.connect(v)
        self.until(lambda: self.png() == PNG1, "the picture")
        asked = [r for r in self.fake.requests if r[1] == "/api/v10/users/" + ACCOUNT]
        self.assertEqual(len(asked), 1)
        method, path, query, headers, body = asked[0]
        self.assertEqual((method, query, body, headers["authorization"]), ("GET", "", "", "Bot " + fd.BOT_TOKEN))
        self.assertTrue(headers["user-agent"].startswith("DiscordBot"))


class TheSwitchComesBack(RefreshTest):
    env = TOKEN_ONLY

    def test_a_picture_switched_off_and_on_again_is_asked_for_at_once_and_not_after_a_day(self):
        v = self.login()
        self.fake.profiles[ACCOUNT] = AVATAR
        c = self.connect(v)
        self.until(lambda: self.png() == PNG1, "the picture")
        self.assertEqual(self.fake.user_ids, [ACCOUNT])
        c.send(hc.frame(1, json.dumps({"t": "pics", "on": False}).encode("utf-8")))
        self.until(lambda: self.kept() == [], "the copy gone")
        self.fake.picture = PNG2
        self.fake.profiles[ACCOUNT] = AVATAR2
        c.send(hc.frame(1, json.dumps({"t": "pics", "on": True}).encode("utf-8")))
        self.until(lambda: self.png() == PNG2, "the picture again")
        self.assertEqual(len(self.fake.user_ids), 2)

    def test_a_person_with_no_picture_at_discord_who_switches_off_and_on_is_asked_again(self):
        v = self.login()
        self.fake.profiles[ACCOUNT] = None
        c = self.connect(v)
        self.until(lambda: len(self.fake.user_ids) >= 1, "a lookup")
        time.sleep(0.4)
        self.assertEqual(self.kept(), [])
        c.send(hc.frame(1, json.dumps({"t": "pics", "on": False}).encode("utf-8")))
        time.sleep(0.4)
        self.fake.profiles[ACCOUNT] = AVATAR
        c.send(hc.frame(1, json.dumps({"t": "pics", "on": True}).encode("utf-8")))
        self.until(lambda: self.png() == PNG1, "the picture")
        self.assertEqual(len(self.fake.user_ids), 2)

    def test_a_hidden_picture_that_is_lifted_and_switched_on_comes_back(self):
        v = self.login()
        self.fake.profiles[ACCOUNT] = AVATAR
        c = self.connect(v)
        self.until(lambda: self.png() == PNG1, "the picture")
        self.store("set_picture_show", ACCOUNT, base.SHOW_BLOCKED, True)
        self.rig.call(self.pictures().sync)
        self.assertEqual(self.kept(), [])
        self.store("set_picture_show", ACCOUNT, base.SHOW_OFF, True)           # (the lift: off until the person says on)
        self.rig.call(self.pictures().sync)
        c.send(hc.frame(1, json.dumps({"t": "pics", "on": True}).encode("utf-8")))
        self.until(lambda: self.png() == PNG1, "the picture again")


class TheLookupFails(RefreshTest):
    def test_every_failure_of_the_lookup_keeps_the_copy_and_the_log_says_a_kind_only(self):
        for mode in ("status:500", "err:401", "err:403", "rate:3", "close", "notjson", "array", "redirect", "partial", "huge", "shape:{\"id\":\"1\",\"avatar\":\"%s\"}" % AVATAR2,
                     "shape:{\"id\":\"%s\"}" % ACCOUNT, "shape:{\"id\":\"%s\",\"avatar\":\"bad\"}" % ACCOUNT, "shape:{\"id\":\"%s\",\"avatar\":7}" % ACCOUNT):
            with self.subTest(mode=mode):
                self.restart(picture_lookup_seconds=1.0, **FAST)
                self.fake.avatar = AVATAR
                v = self.login()
                self.until(lambda: self.png() == PNG1, "the picture")
                self.fake.user_mode = mode
                self.fake.profiles[ACCOUNT] = AVATAR2
                self.fake.picture = PNG2
                self.connect(v)
                self.until(lambda: len(self.fake.user_ids) >= 1, "a lookup")
                time.sleep(0.5)
                self.assertEqual((self.png(), len(self.asked())), (PNG1, 1))
                self.log_is_clean()

    def test_a_refused_token_or_a_wait_stops_the_lookups_for_a_while(self):
        for mode in ("err:401", "err:403", "rate:30", "status:429"):
            with self.subTest(mode=mode):
                self.restart(picture_lookup_seconds=0.5, **FAST)
                self.fake.user_mode = mode
                self.fake.profiles[ACCOUNT] = AVATAR
                v = self.login()
                self.connect(v)
                self.until(lambda: len(self.fake.user_ids) >= 1, "a lookup")
                time.sleep(2.0)
                self.assertEqual(len(self.fake.user_ids), 1)
                self.assertEqual(self.kept(), [])

    def test_a_lookup_that_failed_is_asked_again_at_the_next_connect_not_at_every_check_of_the_roles(self):
        self.restart(picture_lookup_seconds=60.0, **FAST)
        self.fake.user_mode = "status:500"
        self.fake.profiles[ACCOUNT] = AVATAR
        v = self.login()
        self.connect(v)
        self.until(lambda: len(self.fake.user_ids) >= 1, "a lookup")
        time.sleep(0.3)
        self.fake.user_mode = "ok"
        self.connect(v)
        self.until(lambda: self.png() == PNG1, "the picture")
        self.assertEqual(len(self.fake.user_ids), 2)


class TheRolesAreOff(RefreshTest):
    env = TOKEN_ONLY

    def test_with_the_token_and_no_roles_a_person_is_asked_about_when_they_connect_and_never_by_the_roles_check(self):
        v = self.login()
        self.fake.profiles[ACCOUNT] = AVATAR
        self.connect(v)
        self.until(lambda: self.png() == PNG1, "the picture")
        self.assertEqual((self.fake.user_ids, self.fake.member_ids), ([ACCOUNT], []))
        time.sleep(1.5)
        self.assertEqual(self.fake.user_ids, [ACCOUNT])

    def test_a_refused_token_or_a_wait_stops_the_lookups_where_the_roles_are_off(self):
        for mode in ("err:401", "err:403", "status:429", "rate:30"):
            with self.subTest(mode=mode):
                self.restart(**FAST)
                self.fake.user_mode = mode
                self.fake.profiles[ACCOUNT] = AVATAR
                v = self.login()
                self.connect(v)
                self.until(lambda: len(self.fake.user_ids) >= 1, "a lookup")
                time.sleep(0.3)
                self.connect(v)
                self.connect(v)
                time.sleep(0.8)
                self.assertEqual(len(self.fake.user_ids), 1)
                self.assertEqual(self.kept(), [])

    def test_the_lookups_of_a_crowd_go_at_the_pace_of_the_pictures(self):
        self.restart(picture_burst=1, picture_refill_seconds=0.5, **FAST)
        accounts = [fd.snowflake(100 + n) for n in range(4)]
        for a in accounts:
            self.fake.profiles[a] = AVATAR
        for a in accounts:
            self.connect(self.login((a, "name%s" % a[-4:])))
        self.until(lambda: len(self.fake.user_ids) >= 4, "four lookups")
        times = [t for t in self.fake.user_times]
        gaps = [b - a for a, b in zip(times, times[1:])]
        self.assertTrue(all(g >= 0.35 for g in gaps), gaps)


class TheTokenIsNotSet(RefreshTest):
    env = {}

    def test_with_no_token_nothing_new_happens(self):
        v = self.login()
        self.fake.profiles[ACCOUNT] = AVATAR
        self.fake.members[ACCOUNT] = []
        self.fake.member_avatars[ACCOUNT] = AVATAR
        self.connect(v)
        time.sleep(1.5)
        self.assertEqual((self.fake.user_ids, self.fake.member_ids, self.asked(), self.kept()), ([], [], [], []))
        self.assertEqual(T.kinds_of(self.log()), ["none"])                 # (the login's, which brought no picture)

    def test_the_login_still_fetches_as_before(self):
        self.fake.avatar = AVATAR
        v = self.login()
        self.until(lambda: self.png() == PNG1, "the picture")
        self.connect(v)
        time.sleep(0.8)
        self.assertEqual((self.fake.user_ids, len(self.asked())), ([], 1))


class TheRolesOnlyLendTurns(RefreshTest):
    def roles(self):
        return self.rig.hub.chat.roles

    def test_a_lookup_takes_a_turn_of_the_roles_bucket_and_no_turn_is_none_when_the_roles_are_stopped(self):
        roles = self.roles()
        now = roles.clock()
        roles.bucket.tokens = 2.0
        roles.bucket.at = now
        self.assertTrue(self.rig.call(roles.spare_turn))
        self.assertLess(roles.bucket.tokens, 1.5)
        roles.bucket.tokens = 0.0
        roles.bucket.at = roles.clock()
        self.assertIs(self.rig.call(roles.spare_turn), False)
        roles.pause_until = roles.clock() + 100
        self.assertIsNone(self.rig.call(roles.spare_turn))
        roles.pause_until, roles.hold_until = float("-inf"), roles.clock() + 100
        self.assertIsNone(self.rig.call(roles.spare_turn))

    def test_a_lookup_with_the_roles_stopped_is_not_made(self):
        roles = self.roles()
        roles.pause_until = roles.clock() + 1000
        v = self.login()
        self.fake.profiles[ACCOUNT] = AVATAR
        self.rig.call(self.pictures().ask, ACCOUNT)
        time.sleep(1.0)
        self.assertEqual((self.fake.user_ids, self.kept()), ([], []))


class TheMarkAndTheSchema(RefreshTest):
    def test_the_mark_is_a_keyed_mark_in_memory_and_not_the_code_the_id_or_the_reference(self):
        self.fake.avatar = AVATAR
        self.login()
        self.until(lambda: self.png() == PNG1, "the picture")
        codes = self.pictures().codes
        secret = self.rig.call(self.rig.store.secret)
        ref = pseudo.reference(secret, ACCOUNT)
        self.assertEqual(list(codes), [ref])
        mark = codes[ref]
        self.assertEqual(mark, pseudo.keyed(secret, "picture-code", AVATAR))
        self.assertNotIn(mark, (AVATAR, ref, ACCOUNT))
        self.assertNotEqual(pseudo.keyed(secret, "picture-code", AVATAR), pseudo.keyed(secret, "other", AVATAR))
        self.assertNotEqual(pseudo.keyed(secret, "picture-code", AVATAR), pseudo.keyed(secret, "picture-code", AVATAR2))
        self.assertNotEqual(pseudo.keyed(b"a" * 32, "picture-code", AVATAR), pseudo.keyed(b"b" * 32, "picture-code", AVATAR))
        self.assertRegex(mark, r"[0-9a-f]{16}\Z")
        for word in (AVATAR, mark):
            for name in (self.rig.db, self.rig.db + "-wal"):
                if os.path.exists(name):
                    with open(name, "rb") as f:
                        self.assertNotIn(word.encode(), f.read(), "a file holds the code or its mark")

    def test_the_schema_and_the_table_are_where_they_were(self):
        self.fake.avatar = AVATAR
        self.login()
        self.until(lambda: self.png() == PNG1, "the picture")
        db = sqlite3.connect(self.rig.db)
        try:
            self.assertEqual(db.execute("PRAGMA user_version").fetchone()[0], 4)
            self.assertEqual([r[1] for r in db.execute("PRAGMA table_info(pictures)")], ["ref", "show", "name", "png", "until", "at"])
        finally:
            db.close()

    def test_the_memory_of_marks_and_looks_is_bounded(self):
        self.restart(pictures_rows_max=3, **FAST)
        p = self.pictures()

        def fill():
            for n in range(10):
                p.remember("r%d" % n, AVATAR)
                p.note_look("r%d" % n)
            return len(p.codes), len(p.looked), list(p.codes)[-1]
        self.assertEqual(self.rig.call(fill), (3, 3, "r9"))


class TheLog(RefreshTest):
    def test_no_id_code_mark_reference_or_token_is_in_the_log_in_any_path(self):
        self.restart(picture_lookup_seconds=1.0, **FAST)
        self.fake.avatar = AVATAR
        v = self.login()
        self.until(lambda: self.png() == PNG1, "the picture")
        self.member(AVATAR2)
        self.fake.picture = PNG2
        self.connect(v)
        self.until(lambda: self.png() == PNG2, "the new picture")
        self.fake.members.clear()
        self.fake.profiles[ACCOUNT] = None
        self.until(lambda: self.kept() == [], "the copy gone")
        self.fake.cdn_mode = "500"
        self.fake.profiles[ACCOUNT] = AVATAR
        self.until(lambda: len(self.fake.user_ids) >= 2 and len(self.asked()) >= 3, "failures")
        self.fake.user_mode = "status:500"
        time.sleep(1.5)
        self.log_is_clean()
        self.assertTrue(set(T.kinds_of(self.log())) <= {"ok", "none", "down"}, self.log())


if __name__ == "__main__":
    unittest.main()
