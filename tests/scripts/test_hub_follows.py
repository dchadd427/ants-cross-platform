#!/usr/bin/env python3
"""Follows of the chat hub (hub/follows.py, hub/presence.py `here`, hub/commands.py `/follow` and `/unfollow`, hub/chat.py, hub/store/ follows; docs/HUB_PEOPLE.md, "Follows"; run by ./run_tests.sh --fast and by the CI).
Every id, token and name here is made up.

  TheCommands       /follow and /unfollow: who is meant (a name, a row, a line, a handle), every answer, the limit of 100, a person who is banned
  TheList           the list that only its owner gets: the frame, the number of the row of a person who is here, the pages that are open, a hub that starts again
  TheRoomAlert      "<person> opened a room": who is told and who never is (private rooms, a person in a match, a person who ignores the host, the limits), and how the alert ends
  TheSecret         nobody learns who follows them or whom somebody follows: no frame, no log, no alert of anybody else holds it
  TheForgetting     an account that forgets itself takes its follows and every follow of it

tests/data/hub_follows_mutants.json breaks the rules one at a time for tools/mutate.py (every one must be caught).
"""
import dataclasses
import json
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
from test_hub_core import World  # noqa: E402
from hub import limits as limitmod, session  # noqa: E402

ANNA_ID, BEN_ID, CLEO_ID, DAN_ID = ("8411000099990001", "8411000099990002", "8411000099990003", "8411000099990004")
ANNA, BEN, CLEO, DAN = (session.Identity(i, n, False) for i, n in ((ANNA_ID, "anna"), (BEN_ID, "ben"), (CLEO_ID, "cleo"), (DAN_ID, "dan")))
LIMITS = limitmod.Limits()
ROOM = {"code": "abc234", "map": "Treasure.LVL", "n": 1, "of": 4}


class Base(unittest.TestCase):
    limits = LIMITS

    def setUp(self):
        self.w = World(mods=(), limits=self.limits)
        self.addCleanup(self.w.close)
        self.anna, self.anna_tab, self.ben, self.cleo, self.dan, self.guest = (self.w.join(i) for i in (ANNA, ANNA, BEN, CLEO, DAN, None))

    def say(self, peer, text, seconds=3):
        self.w.clock.now += seconds
        self.w.say(peer, text)

    def frame(self, peer, **fields):
        self.w.clock.now += 3
        self.w.chat.on_text(peer, json.dumps(fields))

    def host(self, peer, code="abc234", **room):
        """The page of `peer` says that it waits in a public room (the host's page says its code)."""
        fields = dict(ROOM, code=code, **room)
        self.frame(peer, t="here", at="wait", pub=True, map=fields["map"], n=fields["n"], of=fields["of"], code=code, inv=fields)

    def replies(self, peer, word="follow"):
        return [m for m in peer.sent if m["t"] == "reply" and m["c"] == word]

    def lists(self, peer):
        return [m["rows"] for m in peer.sent if m["t"] == "follows"]

    def alerts_of(self, peer):
        return [m["row"] for m in peer.sent if m["t"] == "alert"]

    def tick(self, seconds):
        for _ in range(int(seconds)):
            self.w.clock.now += 1
            self.w.chat.people.sweep(self.w.clock.now)


class TheCommands(Base):
    def test_a_person_is_followed_by_a_name_in_any_letter_case_and_the_answer_says_who_and_how_many(self):
        self.say(self.anna, "/follow BEN")
        self.say(self.anna, "/follow cleo")
        self.assertEqual([(m["r"], m["to"], m["n"]) for m in self.replies(self.anna)], [("ok", "ben", 1), ("ok", "cleo", 2)])
        store = self.w.store
        self.assertEqual({name for _, name in store.follows(self.w.chat.ref(ANNA_ID))}, {"ben", "cleo"})

    def test_a_person_may_be_named_by_a_row_of_the_list_a_line_or_a_handle_that_the_hub_gave(self):
        row = self.w.chat.presence.row_id(BEN_ID)
        self.say(self.anna, "/follow row:%d" % row)
        self.say(self.cleo, "a line of cleo")
        self.say(self.anna, "/follow #1")
        self.say(self.anna, "/follow #99")
        self.say(self.anna, "/follow row:%d" % (row + 40))
        self.assertEqual([m["r"] for m in self.replies(self.anna)], ["ok", "ok", "nobody", "nobody"])
        handle = self.lists(self.anna)[-1][0]["h"]
        self.say(self.anna, "/unfollow h:%s" % handle)
        self.assertEqual(self.replies(self.anna, "unfollow")[-1]["r"], "ok")

    def test_every_answer(self):
        twin = self.w.join(session.Identity("8411000099990005", "ben", False))
        out = []
        for line in ("/follow", "/follow anna", "/follow zed", "/follow ben", "/follow " + "x" * 40):
            self.say(self.anna, line)
        self.assertEqual([m["r"] for m in self.replies(self.anna)], ["usage", "self", "nobody", "ambiguous", "nobody"])
        self.w.chat.peers.discard(twin)
        self.say(self.anna, "/follow ben")
        self.say(self.anna, "/follow ben")
        self.assertEqual([m["r"] for m in self.replies(self.anna)][-2:], ["ok", "ok"])
        self.assertEqual(len(self.w.store.follows(self.w.chat.ref(ANNA_ID))), 1)             # (a person that is followed already is not a second follow)
        self.say(self.anna, "/unfollow")
        self.say(self.anna, "/unfollow zed")
        self.assertEqual([m["r"] for m in self.replies(self.anna, "unfollow")], ["usage", "not"])
        del out

    def test_the_limit_of_follows_is_the_stores_and_a_person_who_is_followed_is_still_let_in_again(self):
        w = World(mods=(), limits=dataclasses.replace(LIMITS, follow_max=2))
        self.addCleanup(w.close)
        anna = w.join(ANNA)
        people = [w.join(i) for i in (BEN, CLEO, DAN)]
        for line in ("/follow ben", "/follow cleo", "/follow dan", "/follow ben"):
            w.clock.now += 3
            w.say(anna, line)
        self.assertEqual([m["r"] for m in anna.sent if m["t"] == "reply"], ["ok", "ok", "full", "ok"])
        del people

    def test_a_person_who_is_banned_from_the_chat_is_nobody(self):
        self.w.store.ban(BEN_ID, "by", "ben")
        self.say(self.anna, "/follow ben")
        self.assertEqual([m["r"] for m in self.replies(self.anna)], ["nobody"])

    def test_unfollow_by_the_name_that_was_shown_when_the_follow_was_made_and_by_the_name_shown_now(self):
        self.say(self.anna, "/follow ben")
        self.w.chat.peers.discard(self.ben)
        self.say(self.anna, "/unfollow BEN")
        self.assertEqual(self.replies(self.anna, "unfollow")[-1]["r"], "ok")
        self.assertEqual(self.w.store.follows(self.w.chat.ref(ANNA_ID)), [])
        self.assertEqual(self.lists(self.anna)[-1], [])

    def test_a_guest_has_no_commands(self):
        self.w.chat.on_text(self.guest, json.dumps({"t": "say", "text": "/follow ben"}))
        self.assertEqual(self.guest.codes(), ["login"])


class TheList(Base):
    def test_the_list_goes_to_every_socket_of_the_account_alone(self):
        self.say(self.anna, "/follow ben")
        a, b = self.lists(self.anna), self.lists(self.anna_tab)
        self.assertEqual(a, b)
        row = a[-1][0]
        self.assertEqual((row["name"], row["k"]), ("ben", self.w.chat.key_of(BEN_ID)))
        self.assertEqual(set(row), {"h", "name", "k"})                                       # (no row of the list yet: the number is there once there is one)
        for other in (self.ben, self.cleo, self.dan, self.guest):
            self.assertEqual([m for m in other.sent if m["t"] == "follows"], [])

    def test_the_number_of_the_row_is_there_for_a_person_who_is_here_and_sent_again_when_one_comes_or_goes(self):
        self.say(self.anna, "/follow ben")
        self.say(self.anna, "/follow dan")
        presence = self.w.chat.presence
        presence.refresh(self.w.clock.now, force=True)
        rows = {r["name"]: r for r in self.lists(self.anna)[-1]}
        self.assertEqual({n: r["i"] for n, r in rows.items()}, {"ben": presence.ids[BEN_ID], "dan": presence.ids[DAN_ID]})
        before = len(self.lists(self.anna))
        presence.refresh(self.w.clock.now + 5, force=True)
        self.assertEqual(len(self.lists(self.anna)), before)                                 # (nothing changed: nothing is sent again)
        self.w.chat.peers.discard(self.dan)
        presence.ids.pop(DAN_ID)
        presence.refresh(self.w.clock.now + 10, force=True)
        later = {r["name"]: r for r in self.lists(self.anna)[-1]}
        self.assertEqual(set(later["dan"]), {"h", "name"})                                   # (no number for a person who is gone, and no key: the hub knows no line of theirs)
        self.assertEqual(later["ben"]["i"], presence.ids[BEN_ID])

    def test_a_person_who_is_banned_from_the_chat_is_not_here_to_the_list_either(self):
        self.say(self.anna, "/follow dan")
        self.w.chat.presence.refresh(self.w.clock.now, force=True)
        self.assertIn("i", self.lists(self.anna)[-1][0])
        self.w.store.ban(DAN_ID, "by", "dan")
        self.w.chat.presence.refresh(self.w.clock.now + 5, force=True)
        self.assertNotIn("i", self.lists(self.anna)[-1][0])

    def test_the_rows_are_by_name_and_a_person_who_is_not_here_keeps_the_name_that_was_shown(self):
        for n in ("dan", "ben", "cleo"):
            self.say(self.anna, "/follow " + n)
        self.w.chat.peers.discard(self.cleo)
        presence = self.w.chat.presence
        presence.refresh(self.w.clock.now, force=True)
        self.assertEqual([r["name"] for r in self.lists(self.anna)[-1]], ["ben", "cleo", "dan"])

    def test_the_welcome_sends_the_list_when_there_is_one_and_nothing_when_there_is_not(self):
        late = self.w.join(ANNA)
        self.w.chat.follows.send_list(late)
        self.assertEqual(late.sent, [])
        self.say(self.anna, "/follow ben")
        later = self.w.join(ANNA)
        self.w.chat.follows.send_list(later)
        self.assertEqual([m["t"] for m in later.sent], ["follows"])
        reader = self.guest
        self.w.chat.follows.send_list(reader)
        self.assertEqual(reader.sent, [])

    def test_a_handle_that_the_hub_did_not_give_this_account_means_nobody(self):
        self.say(self.anna, "/follow ben")
        handle = self.lists(self.anna)[-1][0]["h"]
        self.say(self.cleo, "/follow h:%s" % handle)
        self.assertEqual(self.replies(self.cleo)[-1]["r"], "nobody")
        self.say(self.anna, "/follow h:%s" % ("0" * 12))
        self.assertEqual(self.replies(self.anna)[-1]["r"], "nobody")

    def test_the_follows_are_there_after_a_restart(self):
        self.say(self.anna, "/follow ben")
        w = World(mods=(), limits=LIMITS, db=self.w.db)
        self.addCleanup(lambda: w.store.close())
        again = w.join(ANNA)
        w.chat.follows.send_list(again)
        self.assertEqual([r["name"] for r in again.sent[0]["rows"]], ["ben"])


class TheRoomAlert(Base):
    def follow(self, peer, name="anna"):
        self.say(peer, "/follow " + name)

    def test_the_people_who_follow_the_host_are_told_and_nobody_else_is_not_even_the_host(self):
        self.follow(self.ben)
        self.follow(self.cleo)
        self.host(self.anna)
        for follower in (self.ben, self.cleo):
            rows = self.alerts_of(follower)
            self.assertEqual(len(rows), 1)
            row = rows[0]
            self.assertEqual({k: row[k] for k in ("k", "u", "from", "map", "n", "of", "o", "code")}, {"k": "room", "u": 1, "from": "anna", "map": "Treasure.LVL", "n": 1, "of": 4, "o": 1, "code": "abc234"})
        for other in (self.dan, self.guest, self.anna, self.anna_tab):
            self.assertEqual([m for m in other.sent if m["t"] in ("alert", "alerts")], [], "somebody else was told")
        self.assertEqual([m for m in self.anna.sent if m["t"] not in ("follows", "reply")], [])

    def test_the_alert_is_kept_in_the_list_of_the_account_and_goes_to_every_socket(self):
        tab = self.w.join(BEN)
        self.follow(self.ben)
        self.host(self.anna)
        self.assertEqual(self.alerts_of(self.ben), self.alerts_of(tab))
        late = self.w.join(BEN)
        self.w.chat.people.send_list(late)
        self.assertEqual([(r["k"], r["from"], r["code"]) for r in late.sent[0]["rows"]], [("room", "anna", "abc234")])

    def test_only_a_public_room_with_a_code_is_told_never_a_private_one_or_a_page_that_says_none(self):
        self.follow(self.ben)
        self.frame(self.anna, t="here", at="wait", pub=False, inv=ROOM)                       # (private)
        self.frame(self.anna, t="here", at="wait", pub=True, map="Treasure.LVL", n=1, of=4, inv=ROOM)       # (a guest's page of a public room: no code)
        self.frame(self.anna, t="here", at="play", pub=True, map="Treasure.LVL", n=2)
        self.frame(self.anna, t="here", at="look")
        self.assertEqual(self.alerts_of(self.ben), [])
        self.host(self.anna)
        self.assertEqual(len(self.alerts_of(self.ben)), 1)

    def test_a_second_tab_of_the_host_that_says_the_room_without_its_code_does_not_make_the_room_a_new_one(self):
        self.follow(self.ben)
        self.host(self.anna)
        self.frame(self.anna_tab, t="here", at="wait", pub=True, map="Treasure.LVL", n=1, of=4, inv=ROOM)            # (a tab that holds the same room as a guest: no code)
        self.w.clock.now += 3600
        self.host(self.anna)
        self.assertEqual(len(self.alerts_of(self.ben)), 1)

    def test_a_room_that_is_said_again_is_one_alert_and_a_new_room_within_the_hour_is_none(self):
        self.follow(self.ben)
        self.host(self.anna)
        self.host(self.anna)
        self.frame(self.anna, t="here", at="look")
        self.host(self.anna)
        self.assertEqual(len(self.alerts_of(self.ben)), 1)
        self.host(self.anna, code="zzz999")
        self.assertEqual(len(self.alerts_of(self.ben)), 1)                                    # (one wave for a host in an hour)
        self.w.clock.now += 3600
        self.host(self.anna, code="yyy888")
        self.assertEqual([r["code"] for r in self.alerts_of(self.ben)], ["abc234", "yyy888"])

    def test_a_person_has_six_room_alerts_in_a_day_from_everybody_they_follow(self):
        hosts = [self.w.join(session.Identity("84110000888%05d" % n, "host%d" % n, False)) for n in range(8)]
        for h in hosts:
            self.follow(self.ben, h.identity.username)
        for n, h in enumerate(hosts):
            self.host(h, code="room%d" % n)
        self.assertEqual(len(self.alerts_of(self.ben)), 6)
        self.w.clock.now += 86400 / 6 + 1
        extra = self.w.join(session.Identity("84110000888%05d" % 9, "host9", False))
        self.follow(self.ben, "host9")
        self.host(extra, code="room9")
        self.assertEqual(len(self.alerts_of(self.ben)), 7)

    def test_a_person_in_a_match_or_in_that_room_already_is_not_told_and_a_person_who_ignores_the_host_is_told_nothing(self):
        for p in (self.ben, self.cleo, self.dan):
            self.follow(p)
        self.frame(self.ben, t="here", at="play", pub=False)
        self.frame(self.cleo, t="here", at="wait", pub=False, inv=ROOM)
        self.say(self.dan, "/ignore anna")
        self.host(self.anna)
        for p in (self.ben, self.cleo, self.dan):
            self.assertEqual(self.alerts_of(p), [], p.identity.username)

    def test_a_person_who_does_not_follow_the_host_any_more_is_told_nothing(self):
        self.follow(self.ben)
        self.say(self.ben, "/unfollow anna")
        self.host(self.anna)
        self.assertEqual(self.alerts_of(self.ben), [])

    def test_a_host_who_may_not_chat_has_no_alerts_made_for_the_room(self):
        self.follow(self.ben)
        self.w.store.mute(ANNA_ID, 60, "by")
        self.host(self.anna)
        self.assertEqual(self.alerts_of(self.ben), [])
        young = self.w.join(session.Identity("8411000099990007", "newbie", True))
        self.follow(self.cleo, "newbie")
        self.host(young)
        self.assertEqual(self.alerts_of(self.cleo), [])

    def test_a_room_with_no_free_seat_is_no_alert(self):
        self.follow(self.ben)
        self.host(self.anna, n=4, of=4)
        self.assertEqual(self.alerts_of(self.ben), [])

    def test_the_alert_ends_when_the_room_closes_like_an_invitation_does(self):
        self.follow(self.ben)
        self.host(self.anna)
        self.frame(self.anna, t="here", at="look")
        self.tick(30)
        rows = [m for m in self.ben.sent if m["t"] == "alerts"][-1]["rows"]
        self.assertEqual([("o" in r, "code" in r) for r in rows], [(False, False)])
        stored = self.w.store.alerts(self.w.chat.ref(BEN_ID))[0]
        self.assertNotIn("abc234", stored.body)                                              # (the code is out of the database as well)

    def test_a_person_who_is_not_here_is_not_alerted_and_nothing_is_kept_for_them(self):
        self.follow(self.ben)
        self.w.chat.peers.discard(self.ben)
        self.host(self.anna)
        self.assertEqual(self.w.store.alerts(self.w.chat.ref(BEN_ID)), [])


class TheSecret(Base):
    def test_nothing_that_anybody_else_is_sent_holds_who_follows_whom(self):
        self.say(self.ben, "/follow anna")
        self.say(self.cleo, "/follow anna")
        self.host(self.anna)
        refs = [self.w.chat.ref(i) for i in (ANNA_ID, BEN_ID, CLEO_ID)]
        for peer in (self.anna, self.anna_tab, self.dan, self.guest):
            blob = b"".join(peer.raw)
            for part in [ANNA_ID, BEN_ID, CLEO_ID] + refs:
                self.assertNotIn(part.encode(), blob, "a frame holds %r" % part)
            self.assertEqual([m for m in peer.sent if m["t"] in ("follows", "alert", "alerts")], [])
        self.assertEqual([m for m in self.anna.sent if m["t"] == "reply"], [])

    def test_no_frame_of_the_follower_holds_a_reference_an_id_or_a_count_of_followers(self):
        self.say(self.ben, "/follow anna")
        self.say(self.cleo, "/follow anna")
        blob = b"".join(self.ben.raw)
        for part in (ANNA_ID, BEN_ID, CLEO_ID, self.w.chat.ref(ANNA_ID), self.w.chat.ref(CLEO_ID)):
            self.assertNotIn(part.encode(), blob)
        for m in self.ben.sent:
            self.assertFalse({"followers", "count"} & set(m))

    def test_following_a_person_is_not_followed_back_and_the_person_gets_nothing(self):
        self.say(self.anna, "/follow ben")
        self.assertEqual([m for m in self.ben.sent if m["t"] not in ("welcome",)], [])
        self.assertEqual(self.w.store.follows(self.w.chat.ref(BEN_ID)), [])


class TheForgetting(Base):
    def test_an_account_that_forgets_itself_loses_its_follows_and_every_follow_of_it(self):
        self.say(self.anna, "/follow ben")
        self.say(self.ben, "/follow anna")
        self.say(self.cleo, "/follow anna")
        ref = self.w.chat.ref(ANNA_ID)
        self.w.store.forget(ANNA_ID)
        self.w.chat.follows.forget(ANNA_ID)
        self.assertEqual(self.w.store.follows(ref), [])
        self.assertEqual(self.w.store.followers(ref), [])
        self.assertEqual(self.w.store.follows(self.w.chat.ref(BEN_ID)), [])
        self.assertEqual(self.lists(self.ben)[-1], [])                                       # (the pages of the others that are open are sent their lists again)
        self.assertEqual(self.lists(self.cleo)[-1], [])


if __name__ == "__main__":
    unittest.main()
