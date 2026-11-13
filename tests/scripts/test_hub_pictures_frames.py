#!/usr/bin/env python3
"""What the sockets are told of the Discord pictures (hub/chat.py `you_of`, `on_pics`, `picture_changed`, hub/presence.py `row_of`, hub/pictures.py `sync`, hub/admin.py `nopic`, `pic`, `status`; docs/HUB_PICTURES.md),
on real sockets against the real hub, a fake Discord and its fake picture host (tests/scripts/test_hub_pictures.py is the fetch and the store):

  - `you` (the welcome, the frame, /hub/me) carries `pics` (on, off, blocked) and, where the account has a picture that may be shown, `pic`; a guest and a hub without the Discord login get neither;
  - a row of the players list carries `pic`; a change of it is a changed row;
  - the picture arriving, going (the switch, the last logout, a kick, a ban, the operator's block, the end of the login's time) is told to every socket of the account, and only to them;
  - the switch `{"t":"pics","on":bool}`: a guest is `login`, anything else that is wrong `pics`, three at once and then one in 30 seconds, a block stays; off deletes the copy before any frame says so, on shows nothing
    until a login and asks for no fetch;
  - the operator's commands (hub.admin nopic, pic, status);
  - no frame of any socket holds an id, a hash of one, the reference or the picture's code; a field that a client forges in any frame changes nothing.
There is no picture in a chat line and no frame of its own for a picture that changed: the lines are as they were.
"""
import hashlib
import io
import json
import os
import re
import sqlite3
import sys
import time
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
import fake_discord as fd  # noqa: E402
import hub_client as hc  # noqa: E402
import hub_rig  # noqa: E402
import test_hub_pictures as T  # noqa: E402
import test_hub_roles as R  # noqa: E402
from hub import admin, pseudo  # noqa: E402
from hub.store import base  # noqa: E402

ACCOUNT, OTHER, THIRD = T.ACCOUNT, T.OTHER, T.THIRD
MOD, ADMIN = fd.snowflake(650), fd.snowflake(640)
NAME = re.compile(r"[0-9a-f]{32}\Z")
FAST = dict(tick_seconds=0.2, who_seconds=0.3, who_resend_seconds=0.0)


def pics(on):
    return hc.frame(1, json.dumps({"t": "pics", "on": on}).encode("utf-8"))


def raw(obj):
    return hc.frame(1, json.dumps(obj).encode("utf-8"))


class Seeing(hc.Client):
    """A socket that keeps every text frame that it was sent, as the bytes that were sent."""

    def __init__(self, *args, **kwargs):
        self.seen = []
        super().__init__(*args, **kwargs)

    def recv_frame(self, timeout=None):
        opcode, payload = super().recv_frame(timeout)
        if opcode == 1:
            self.seen.append(payload)
        return opcode, payload


class FrameTest(T.PictureTest):
    limits = FAST
    roles = {MOD: "mod", ADMIN: "admin"}

    def setUp(self):
        super().setUp()
        self.sockets = []
        self.addCleanup(self.check_every_frame)

    def socket(self, visitor=None, welcome=True):
        c = Seeing(self.rig.port, headers=dict({"Sec-Fetch-Site": "same-origin"}, **({"Cookie": visitor.jar.header()} if visitor else {})))
        self.assertTrue(c.ok)
        self.sockets.append(c)
        c.welcome = c.recv_json() if welcome else None
        return c

    def member(self, user=(ACCOUNT, "maple.leaf")):
        """(the visitor, a socket of it): a login with the fetch done."""
        before = len(T.kinds_of(self.log()))
        v = self.login(user)
        if user[0] not in (MOD, ADMIN):
            self.settle(1, before)
        return v, self.socket(v)

    def check_every_frame(self):
        """Every frame that any socket of the test was sent: none holds an id of the test, a hash of one, a reference, or the picture's code."""
        secret = self.rig.call(self.rig.store.secret) if self.rig.thread.is_alive() else None
        poison = [fd.AVATAR]
        for account in (ACCOUNT, OTHER, THIRD, MOD, ADMIN):
            poison += [account] + [h(account.encode()).hexdigest() for h in (hashlib.sha256, hashlib.sha1, hashlib.md5)]
            if secret:
                poison.append(pseudo.reference(secret, account))
        for c in self.sockets:
            c.close()
            for frame in c.seen:
                text = frame.decode("utf-8")
                for word in poison:
                    self.assertNotIn(word, text, "a frame holds %r" % word[:6])

    def you_frames(self, c, seconds=10.0, settle=0.6):
        return [m for m in R.wait_frames(c, "you", settle, seconds) if m["t"] == "you"]

    def row_of(self, c, name="maple.leaf", seconds=10.0):
        """The row of the list that says `name`, from the first `who` frames on."""
        c.send(raw({"t": "who"}))
        rows = {}
        for m in R.wait_frames(c, "who", 0.5, seconds):
            if m["t"] == "who":
                for row in m["rows"]:
                    rows[row["i"]] = row
        return next((r for r in rows.values() if r["name"] == name), None)

    def kept_for(self, *accounts):
        """The pictures that are kept for these accounts (the moderator and the administrator of a test have pictures of their own)."""
        mine = {self.ref(a) for a in accounts}
        return [r[0] for r in self.kept() if r[0] in mine]

    def change(self, function, *args):
        return self.rig.call(getattr(self.rig.store, function), *args)


class TheYou(FrameTest):
    def test_a_member_is_told_of_its_picture_and_its_switch_and_a_member_without_a_picture_only_of_the_switch(self):
        v, c = self.member()
        name = self.name()
        self.assertRegex(name, NAME)
        you = c.welcome["you"]
        self.assertEqual((you["pic"], you["pics"]), (name, "on"))
        other, d = self.member(user=(OTHER, "other.one"))
        d2 = self.socket(other)
        self.fake.avatar = None
        third = self.login((THIRD, "third.one"))
        e = self.socket(third)
        self.assertEqual({k: v for k, v in e.welcome["you"].items() if k in ("pic", "pics")}, {"pics": "on"})
        self.assertEqual(d2.welcome["you"]["pic"], self.name(OTHER))

    def test_me_has_the_same_two_fields_for_a_member_and_none_for_a_guest(self):
        v, c = self.member()
        r = v.call("GET", "/hub/me")
        self.assertEqual((r.json()["you"]["pic"], r.json()["you"]["pics"]), (self.name(), "on"))
        self.assertIsNone(hc.request(self.rig.port, "GET", "/hub/me", [("Sec-Fetch-Site", "same-origin"), ("X-Client-Address", "203.0.113.4")]).json()["you"])

    def test_a_guest_is_told_of_no_picture_field_and_a_hub_without_the_login_of_none(self):
        self.member()
        guest = self.socket()
        self.assertIsNone(guest.welcome["you"])
        with hub_rig.Rig(test_login=True) as rig:
            c = hc.Client(rig.port, login="%s:alice" % ACCOUNT)
            self.addCleanup(c.close)
            welcome = c.recv_json()
            self.assertEqual(sorted(welcome["you"]), ["k", "mod", "name", "role"])
            c.send(pics(False))
            self.assertEqual(c.recv_reply(), {"t": "err", "code": "bad"})                  # (a frame that this hub does not know)
            c.send(raw({"t": "who"}))
            rows = c.recv_reply()["rows"]
            self.assertTrue(rows and all("pic" not in row for row in rows))

    def test_the_picture_arriving_is_told_to_every_socket_of_the_account_and_to_nobody_else(self):
        self.fake.cdn_mode = "slow:0.8"
        v = self.login()
        a, b = self.socket(v), self.socket(v)
        self.fake.cdn_mode = "ok"
        other_visitor = self.login((OTHER, "other.one"))
        self.settle(1)                                                                # (the other account's own fetch is done: it is the first to end)
        c = self.socket(other_visitor)
        self.assertNotIn("pic", a.welcome["you"])
        for sock in (a, b):
            frames = self.you_frames(sock)
            self.assertEqual(len(frames), 1)
            self.assertEqual((frames[0]["you"]["pic"], frames[0]["you"]["pics"]), (self.name(), "on"))
            self.assertEqual(frames[0]["you"]["name"], "maple.leaf")
        self.settle(2)
        self.assertEqual([m for m in c.drain(0.6) if m["t"] == "you"], [])

    def test_the_whole_you_comes_again_when_the_picture_ends_with_its_logins_time(self):
        v, c = self.member()
        self.change("add_session", "k-extra", ACCOUNT, "maple.leaf", 3, 10, 100)         # (a login that ends in three seconds: the picture ends with the latest login, which is still the long one)
        self.rig.call(lambda: self.rig.store.db.execute("UPDATE pictures SET until = ?", (int(time.time()) + 2,)))
        self.rig.call(lambda: self.rig.store.db.execute("UPDATE meta SET v = v + 1 WHERE k = 'pic_seq'"))
        frames = self.you_frames(c)
        self.assertEqual("pic" in frames[-1]["you"], False)
        self.assertEqual(frames[-1]["you"]["pics"], "on")


class TheRows(FrameTest):
    def test_a_row_carries_the_name_of_the_picture_and_a_change_of_it_is_a_changed_row(self):
        v, c = self.member()
        watcher = self.socket()
        row = self.row_of(watcher)
        self.assertEqual(row["pic"], self.name())
        c.send(pics(False))
        self.you_frames(c)
        later = [m for m in R.wait_frames(watcher, "who", 0.5, 10.0) if m["t"] == "who"]
        changed = [r for m in later for r in m["rows"] if r["name"] == "maple.leaf"]
        self.assertTrue(changed and all("pic" not in r for r in changed))
        self.assertTrue(all(not m["full"] for m in later))

    def test_a_person_with_no_picture_has_a_row_without_the_field_and_a_banned_account_is_not_in_the_list(self):
        self.member()
        self.fake.avatar = None
        self.member(user=(OTHER, "other.one"))
        watcher = self.socket()
        self.assertNotIn("pic", self.row_of(watcher, "other.one"))
        self.change("ban", ACCOUNT, "command line", "maple.leaf")
        time.sleep(0.5)
        self.assertIsNone(self.row_of(watcher, "maple.leaf"))


class TheSwitch(FrameTest):
    def test_off_deletes_the_copy_before_any_frame_says_so_and_every_socket_of_the_account_is_told(self):
        v, c = self.member()
        d = self.socket(v)
        name = self.name()
        c.send(pics(False))
        for sock in (c, d):
            frame = self.you_frames(sock)[-1]["you"]
            self.assertEqual((frame["pics"], "pic" in frame), ("off", False))
            self.assertEqual(self.kept(), [])                                           # (gone when the first frame is read)
        self.assertEqual(self.get(name).status, 404)
        self.assertEqual(self.store("picture_state", ACCOUNT), base.SHOW_OFF)

    def test_on_again_shows_nothing_until_a_login_and_asks_for_no_fetch_and_a_login_then_fetches(self):
        v, c = self.member()
        c.send(pics(False))
        self.you_frames(c)
        asked = len(self.asked())
        c.send(pics(True))
        frame = self.you_frames(c)[-1]["you"]
        self.assertEqual((frame["pics"], "pic" in frame), ("on", False))
        time.sleep(0.5)
        self.assertEqual((len(self.asked()), self.kept()), (asked, []))
        before = len(T.kinds_of(self.log()))
        v2 = self.login()
        self.settle(1, before)
        self.assertEqual(self.socket(v2).welcome["you"]["pic"], self.name())

    def test_a_guest_is_told_login_and_anything_but_a_true_or_a_false_is_pics(self):
        v, c = self.member()
        guest = self.socket()
        guest.send(pics(False))
        self.assertEqual(guest.recv_reply(), {"t": "err", "code": "login"})
        for bad in (None, 0, 1, "true", "off", [True], {"on": True}, 1.0):
            c.send(raw({"t": "pics", "on": bad}))
            self.assertEqual(c.recv_reply(), {"t": "err", "code": "pics"}, repr(bad))
        c.send(raw({"t": "pics"}))
        self.assertEqual(c.recv_reply(), {"t": "err", "code": "pics"})
        self.assertEqual((self.store("picture_state", ACCOUNT), len(self.kept())), (base.SHOW_ON, 1))

    def test_three_switches_at_once_and_then_one_every_half_minute(self):
        v, c = self.member()
        for on in (False, True, False):
            c.send(pics(on))
            self.you_frames(c, settle=0.3)
        c.send(pics(True))
        self.assertEqual(c.recv_reply(), {"t": "err", "code": "pics"})
        self.assertEqual(self.store("picture_state", ACCOUNT), base.SHOW_OFF)                     # (the fourth did nothing)
        other, d = self.member(user=(OTHER, "other.one"))
        d.send(pics(False))                                                             # (another account has its own allowance)
        self.assertEqual(self.you_frames(d)[-1]["you"]["pics"], "off")

    def test_a_block_stays_and_a_person_cannot_lift_it_and_it_wins_over_a_login_and_forget_me(self):
        v, c = self.member()
        name = self.name()
        self.change("set_picture_show", ACCOUNT, base.SHOW_BLOCKED, True)
        frame = self.you_frames(c)[-1]["you"]                                           # (the hub's clock reads the change that another process made)
        self.assertEqual((frame["pics"], "pic" in frame), ("blocked", False))
        self.assertEqual(self.get(name).status, 404)
        for on in (True, False):
            c.send(pics(on))
            self.assertEqual(c.recv_reply(), {"t": "err", "code": "pics"})
        asked = len(self.asked())
        v2 = self.login()
        time.sleep(0.5)
        self.assertEqual((len(self.asked()), self.kept(), self.store("picture_state", ACCOUNT)), (asked, [], base.SHOW_BLOCKED))
        self.assertEqual(self.socket(v2).welcome["you"]["pics"], "blocked")
        self.assertEqual(v2.call("POST", "/hub/forget").status, 200)
        self.assertEqual(self.store("picture_state", ACCOUNT), base.SHOW_BLOCKED)

    def test_a_field_that_a_client_forges_changes_nothing(self):
        v, c = self.member()
        name = self.name()
        forged = {"pic": "f" * 32, "pics": "off", "on": False, "p": "f" * 32}
        c.send(raw(dict({"t": "say", "text": "hello"}, **forged)))
        c.send(raw(dict({"t": "here", "at": "look"}, **forged)))
        c.send(raw(dict({"t": "who"}, **forged)))
        c.send(raw(dict({"t": "ping"}, **forged)))
        c.send(raw(dict({"t": "pick", "pick": "username"}, **forged)))
        frames = c.drain(1.0)
        line = next(m for m in frames if m["t"] == "line")
        self.assertEqual(sorted(line), ["at", "id", "k", "mod", "t", "text", "who"])
        self.assertNotIn("you", [m["t"] for m in frames])
        self.assertEqual((self.store("picture_state", ACCOUNT), [r[2] for r in self.kept()]), (base.SHOW_ON, [name]))
        row = self.row_of(self.socket(), "maple.leaf")
        self.assertEqual(row["pic"], name)


class TheEnds(FrameTest):
    def test_the_last_log_out_a_kick_and_a_ban_take_the_picture_and_the_list_shows_it(self):
        v, c = self.member()
        other, d = self.member(user=(OTHER, "other.one"))
        third, e = self.member(user=(THIRD, "third.one"))
        watcher = self.socket()
        self.assertTrue(self.row_of(watcher, "other.one")["pic"])
        self.assertEqual(v.call("POST", "/hub/logout").status, 200)
        mod = self.socket(self.login((MOD, "mod.one")))
        mod.send(hc.say("/kick other.one"))
        frames = R.wait_frames(mod, "ok", 0.5, 10.0)
        self.assertIn({"t": "ok", "c": "kick"}, frames)
        boss = self.socket(self.login((ADMIN, "admin.one")))                              # (a ban is an administrator's)
        boss.send(hc.say("/ban third.one"))
        self.assertIn({"t": "ok", "c": "ban"}, R.wait_frames(boss, "ok", 0.5, 10.0))
        self.assertEqual(self.kept_for(ACCOUNT, OTHER, THIRD), [])
        for account in (ACCOUNT, OTHER, THIRD):
            self.assertIsNone(self.store("picture_names").get(self.ref(account)))
        for name in ("maple.leaf", "other.one", "third.one"):
            self.assertIsNone(self.row_of(watcher, name))
        later = self.login((OTHER, "other.one"))                                         # (a kicked account that logs in again gets its picture again; a banned one does not)
        self.assertEqual(later.call("GET", "/hub/me").json()["you"]["pics"], "on")

    def test_a_banned_account_logs_in_again_without_a_fetch(self):
        self.member()
        boss = self.socket(self.login((ADMIN, "admin.one")))                              # (a ban is an administrator's)
        boss.send(hc.say("/ban maple.leaf"))
        R.wait_frames(boss, "ok", 0.3, 10.0)
        time.sleep(0.6)                                                                  # (the administrator's own fetch is done)
        asked = len(self.asked())
        self.login((ACCOUNT, "maple.leaf"))
        time.sleep(0.6)
        self.assertEqual((len(self.asked()), self.kept_for(ACCOUNT)), (asked, []))

    def test_every_kind_of_socket_gets_no_id_hash_reference_or_code_in_any_frame(self):
        v, c = self.member()
        second = self.socket(v)
        mod = self.socket(self.login((MOD, "mod.one")))
        admin_socket = self.socket(self.login((ADMIN, "admin.one")))
        guest = self.socket()
        for sock in (mod, admin_socket, guest):
            sock.send(raw({"t": "who"}))
        c.send(pics(False))
        c.send(hc.say("a line"))
        c.send(pics(True))
        for sock in (c, second, mod, admin_socket, guest):
            sock.drain(1.0)
        self.change("set_picture_show", ACCOUNT, base.SHOW_BLOCKED, True)
        self.assertEqual(self.you_frames(second)[-1]["you"]["pics"], "blocked")
        for sock in (c, second, mod, admin_socket, guest):
            self.assertTrue(sock.seen)


class TheTick(FrameTest):
    def test_the_names_are_read_by_the_clock_and_not_for_every_frame(self):
        self.member()
        time.sleep(0.5)
        calls = []
        store = self.rig.store
        real = store.picture_names
        self.rig.call(lambda: setattr(store, "picture_names", lambda *a, **k: calls.append(1) or real(*a, **k)))
        try:
            sockets = [self.socket() for _ in range(20)]
            for sock in sockets:
                sock.send(raw({"t": "who"}))
                sock.recv_reply()
            self.assertEqual(calls, [])                                                    # (a welcome and a list for twenty sockets: no read of the names)
        finally:
            self.rig.call(lambda: delattr(store, "picture_names"))


class TheOperator(FrameTest):
    def run_admin(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        code = admin.run(["--db", self.rig.db] + list(argv), out=out, err=err)
        return code, out.getvalue(), err.getvalue()

    def test_nopic_blocks_the_picture_at_once_and_pic_lifts_it_and_the_log_says_so_by_reference_only(self):
        v, c = self.member()
        name = self.name()
        code, out, err = self.run_admin("nopic", "@maple.leaf")
        ref = self.ref()
        self.assertEqual((code, out, err), (0, "%s picture blocked\n" % ref, ""))
        self.assertNotIn(ACCOUNT, out + err)
        frame = self.you_frames(c)[-1]["you"]                                          # (the running hub reads it at its next tick)
        self.assertEqual((frame["pics"], "pic" in frame), ("blocked", False))
        self.assertEqual((self.get(name).status, self.kept()), (404, []))
        self.assertEqual(self.run_admin("nopic", "@maple.leaf")[0], 1)
        code, out, err = self.run_admin("log")
        self.assertIn(" nopic  by terminal to %s" % ref, out)
        self.assertNotIn(ACCOUNT, out)
        code, out, err = self.run_admin("pic", ref)
        self.assertEqual((code, out), (0, "%s picture is no longer blocked\n" % ref))
        self.assertEqual(self.you_frames(c)[-1]["you"]["pics"], "on")
        self.assertEqual(self.run_admin("pic", ref)[0], 1)
        self.assertIn(" pic    by terminal to %s" % ref, self.run_admin("log")[1])

    def test_a_block_survives_the_switch_a_login_and_forget_me_and_only_the_operator_lifts_it(self):
        self.member()
        self.run_admin("nopic", "@maple.leaf")
        self.assertEqual(self.store("set_picture_show", ACCOUNT, base.SHOW_ON), base.SHOW_BLOCKED)
        self.login()
        self.assertEqual(self.store("picture_state", ACCOUNT), base.SHOW_BLOCKED)
        self.assertEqual(self.run_admin("pic", "@maple.leaf")[0], 0)
        self.assertEqual(self.store("picture_state", ACCOUNT), base.SHOW_ON)

    def test_status_says_how_many_pictures_are_kept_switched_off_and_blocked(self):
        self.member()
        self.member(user=(OTHER, "other.one"))
        self.member(user=(THIRD, "third.one"))
        self.run_admin("nopic", "@third.one")
        self.change("set_picture_show", OTHER, base.SHOW_OFF)
        self.change("set_picture_show", ADMIN, base.SHOW_OFF)                            # (two are switched off and one is blocked: the two counts differ)
        code, out, err = self.run_admin("status")
        self.assertEqual(code, 0)
        line = [l for l in out.splitlines() if l.startswith("pictures:")]
        self.assertEqual(len(line), 1)
        self.assertRegex(line[0], r"^pictures: 1 of 2,000 kept, 0\.0 MB of 8\.0 MB; 2 switched off, 1 blocked$")
        self.assertNotIn(ACCOUNT, out)


if __name__ == "__main__":
    unittest.main()
