#!/usr/bin/env python3
"""The name that the chat shows is the person's nickname in the Discord server when there is one (hub/roles.py `tell_names`, hub/chat.py `names_changed`, hub/session.py `renamed`;
docs/HUB.md "Names"; run by ./run_tests.sh --fast and by the CI), against a FAKE Discord: real sockets, the real hub, the real roles' check. Real Discord is never asked.

  - the nickname comes first, then the Discord display name, then the username; a nickname that is null, empty, unusable or the username itself is no nickname;
  - a nickname that is set, changed or removed reaches every open socket as a whole `you` frame at the next check (no new call to Discord: the member answer that the roles' check reads is the
    one source), the lines that were written keep their name, and a person who chose the username keeps it (only the other name changes);
  - a person who is not in the server, a hub whose roles are off and a member answer that says nothing of names have no nickname and change nothing;
  - `you.next` is the display name that follows a shown nickname (the page tries it before the username when its game cannot take the nickname);
  - no account id, token or code is in a frame or a log line.
"""
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
import test_hub_login as L  # noqa: E402
import test_hub_pictures as T  # noqa: E402
import test_hub_pictures_refresh as R  # noqa: E402

ACCOUNT = T.ACCOUNT
NICK, GLOBAL, USER = "Maple Nick", "Maple Leaf", "maple.leaf"


class NickTest(R.RefreshTest):
    limits = dict(tick_seconds=0.2)

    def start(self):
        super().start()
        self.fake.user = (ACCOUNT, USER, GLOBAL)

    def member(self, nick=NICK, glob=GLOBAL):
        self.fake.members[ACCOUNT] = []
        self.fake.member_avatars[ACCOUNT] = None
        self.fake.member_nicks[ACCOUNT] = nick
        self.fake.member_globals[ACCOUNT] = glob

    def you_until(self, client, wanted, seconds=20.0):
        """The first `you` that `wanted` says yes to (the welcome's, or a live frame), else a failure that says what came."""
        seen = []
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            try:
                frame = client.recv_json(timeout=0.5)
            except (TimeoutError, OSError):
                continue
            seen.append(frame)
            you = frame.get("you") if frame.get("t") in ("you", "welcome") else None
            if isinstance(you, dict) and wanted(you):
                return you
        self.fail("no such `you`: %r" % [f.get("you") for f in seen])

    def open(self, visitor):
        c = hc.Client(self.rig.port, headers={"Sec-Fetch-Site": "same-origin", "Cookie": visitor.jar.header()})
        self.assertTrue(c.ok)
        self.sockets.append(c)
        return c

    def profile(self):
        db = sqlite3.connect(self.rig.db)
        try:
            return db.execute("SELECT username, display, pick FROM profiles").fetchall()
        finally:
            db.close()

    def shown(self, name):
        return lambda you: you.get("name") == name


class TheNicknameIsShown(NickTest):
    def test_a_nickname_is_shown_first_and_the_display_name_follows_it(self):
        self.member()
        c = self.open(self.login())
        you = self.you_until(c, self.shown(NICK))
        self.assertEqual((you["pick"], you["other"], you["next"]), ("display", USER, GLOBAL))
        self.assertEqual(self.profile(), [(USER, NICK, "display")])

    def test_no_nickname_shows_the_display_name_as_it_did(self):
        self.member(nick=None)
        c = self.open(self.login())
        welcome = c.recv_json()
        self.assertEqual((welcome["you"]["name"], welcome["you"]["other"]), (GLOBAL, USER))
        self.checks(2)
        self.assertNotIn("next", welcome["you"])
        self.assertEqual(self.profile(), [(USER, GLOBAL, "display")])

    def test_a_nickname_that_is_not_text_or_is_unusable_is_none(self):
        for nick in ("", "   ", "x", 7, ["a"], "a" * 300, "\u202e\u200b"):
            with self.subTest(nick=nick):
                self.member(nick=nick)
                before = len(self.fake.member_ids)
                c = self.open(self.login())
                c.recv_json()
                self.checks(2, since=before)
                self.assertEqual(self.profile(), [(USER, GLOBAL, "display")])

    def test_a_nickname_that_is_the_username_leaves_one_name(self):
        self.member(nick=USER)
        c = self.open(self.login())
        you = self.you_until(c, self.shown(USER))
        self.assertNotIn("other", you)
        self.assertEqual(self.profile()[0][1], "")

    def test_a_nickname_for_a_person_with_no_display_name_is_shown_and_the_username_is_the_other(self):
        self.fake.user = (ACCOUNT, USER)
        self.member(glob=None)
        c = self.open(self.login())
        you = self.you_until(c, self.shown(NICK))
        self.assertEqual((you["pick"], you["other"]), ("display", USER))
        self.assertNotIn("next", you)


class TheNicknameChanges(NickTest):
    def test_a_changed_nickname_is_a_live_frame_and_a_removed_one_is_the_display_name_again(self):
        self.member()
        c = self.open(self.login())
        self.you_until(c, self.shown(NICK))
        self.member(nick="Second Nick")
        self.assertEqual(self.you_until(c, self.shown("Second Nick"))["next"], GLOBAL)
        self.member(nick=None)
        you = self.you_until(c, self.shown(GLOBAL))
        self.assertNotIn("next", you)
        self.assertEqual(self.profile(), [(USER, GLOBAL, "display")])

    def test_a_change_reaches_every_socket_and_the_lines_written_keep_their_name(self):
        self.member()
        v = self.login()
        a, b = self.open(v), self.open(v)
        self.you_until(a, self.shown(NICK))
        self.you_until(b, self.shown(NICK))
        a.say("hello there")
        line = self.line_of(b)
        self.assertEqual(line["who"], NICK)
        self.member(nick="Second Nick")
        self.you_until(a, self.shown("Second Nick"))
        self.you_until(b, self.shown("Second Nick"))
        a.say("again")
        self.assertEqual(self.line_of(b)["who"], "Second Nick")
        c = self.open(v)
        self.assertEqual(c.recv_json()["history"][0]["who"], NICK)

    def line_of(self, client):
        for _ in range(50):
            frame = client.recv_json(timeout=10)
            if frame.get("t") == "line":
                return frame
        self.fail("no line")

    def test_a_check_that_says_the_same_sends_nothing_new(self):
        self.member()
        c = self.open(self.login())
        self.you_until(c, self.shown(NICK))
        before = len(self.fake.member_ids)
        self.checks(3, since=before)
        frames = []
        while True:
            try:
                frames.append(c.recv_json(timeout=0.3))
            except (TimeoutError, OSError):
                break
        self.assertEqual([f for f in frames if f.get("t") == "you"], [])

    def test_the_username_that_a_person_chose_wins_and_a_nickname_changes_only_the_other_name(self):
        self.member()
        v = self.login()
        c = self.open(v)
        self.you_until(c, self.shown(NICK))
        c.send(L.pick_frame("username"))
        c.close()
        d = self.open(v)
        you = self.you_until(d, lambda y: y.get("name") == USER)
        self.assertEqual((you["pick"], you["other"]), ("username", NICK))
        self.assertNotIn("next", you)
        self.member(nick="Second Nick")
        you = self.you_until(d, lambda y: y.get("other") == "Second Nick")
        self.assertEqual((you["name"], you["pick"]), (USER, "username"))
        d.close()
        e = self.open(v)
        self.assertEqual(e.recv_json()["you"]["name"], USER)


class TheNicknameEnds(NickTest):
    def test_a_person_who_leaves_the_server_has_no_nickname_at_the_next_check(self):
        self.member()
        c = self.open(self.login())
        self.you_until(c, self.shown(NICK))
        del self.fake.members[ACCOUNT]
        you = self.you_until(c, self.shown(GLOBAL))
        self.assertNotIn("next", you)
        self.assertEqual(self.profile(), [(USER, GLOBAL, "display")])

    def test_a_person_who_is_not_a_member_has_the_display_name(self):
        c = self.open(self.login())
        self.assertEqual(c.recv_json()["you"]["name"], GLOBAL)
        self.checks(2)
        self.assertEqual(self.profile(), [(USER, GLOBAL, "display")])

    def test_an_answer_with_no_display_name_and_no_nickname_changes_nothing(self):
        v = self.login()
        self.fake.members[ACCOUNT] = []
        self.fake.member_nicks[ACCOUNT] = None
        c = self.open(v)
        self.assertEqual(c.recv_json()["you"]["name"], GLOBAL)
        self.checks(2)
        self.assertEqual(self.profile(), [(USER, GLOBAL, "display")])

    def test_a_nickname_with_no_display_name_in_the_answer_is_shown_and_removed_again_only_at_the_next_login(self):
        v = self.login()
        self.fake.members[ACCOUNT] = []
        self.fake.member_nicks[ACCOUNT] = NICK
        c = self.open(v)
        self.you_until(c, self.shown(NICK))
        self.fake.member_nicks[ACCOUNT] = None
        self.checks(2, since=len(self.fake.member_ids))
        self.assertEqual(self.profile(), [(USER, NICK, "display")])

    def test_a_failed_check_changes_nothing(self):
        self.member()
        c = self.open(self.login())
        self.you_until(c, self.shown(NICK))
        self.fake.member_mode = "err:500"
        self.member(nick="Second Nick")
        self.checks(2, since=len(self.fake.member_ids))
        self.assertEqual(self.profile(), [(USER, NICK, "display")])


class RolesOff(NickTest):
    env = None

    def test_a_hub_whose_roles_are_off_reads_no_nickname(self):
        self.member()
        c = self.open(self.login())
        self.assertEqual(c.recv_json()["you"]["name"], GLOBAL)
        time.sleep(2.5)
        self.assertEqual(self.fake.member_ids, [])
        self.assertEqual(self.profile(), [(USER, GLOBAL, "display")])


class NoIdAnywhere(NickTest):
    def test_no_id_token_or_code_is_in_a_frame_or_a_log_line(self):
        self.member()
        c = self.open(self.login())
        frames = []
        self.you_until(c, lambda you: frames.append(you) or you.get("name") == NICK)
        self.member(nick="Second Nick")
        frames.append(self.you_until(c, self.shown("Second Nick")))
        text = json.dumps(frames)
        for word in (ACCOUNT, fd.BOT_TOKEN, fd.GUILD_ID):
            self.assertNotIn(word, text)
        self.log_is_clean()
        log = self.log()
        for word in (NICK, "Second Nick", GLOBAL):
            self.assertNotIn(word, log)


if __name__ == "__main__":
    unittest.main()
