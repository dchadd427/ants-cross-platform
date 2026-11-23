#!/usr/bin/env python3
"""Alerts, invitations and mentions of the chat hub (hub/people.py, hub/presence.py `inv`, hub/commands.py `/invite`, hub/chat.py, hub/store/ alerts; docs/HUB_PEOPLE.md; run by ./run_tests.sh --fast and by the CI).
Every id, token and name here is made up.

  TheInvitation        /invite: who is meant, every refusal, the limits, a person who ignores the inviter, the room's code while the room is open and not after, a hub that starts again
  TheRoomItSays        what a page may say of its room (`inv`): checked, kept for invitations alone, never in a list or a frame of anybody
  TheMentions          the names that a line marks and who is alerted by it: every rule of the writing, the limits, the people who may not be told, a line that is deleted, an account that forgets itself
  TheList              the alerts of an account: the welcome, a new one, read, cleared, the join notes of its own page, the same on every socket of the account
  TheLiveHub           real sockets: an invitation and a mention reach the person, nothing else does, and the list is there again after a new connection

tests/data/hub_people_mutants.json breaks the rules one at a time for tools/mutate.py (every one must be caught).
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
import hub_client as hc  # noqa: E402
import hub_rig  # noqa: E402
from test_hub_core import FakePeer, World  # noqa: E402
from hub import limits as limitmod, session  # noqa: E402

ANNA_ID, BEN_ID, CLEO_ID, DAN_ID, MOD_ID = ("8411000099990001", "8411000099990002", "8411000099990003", "8411000099990004", "8411000099990009")
ANNA = session.Identity(ANNA_ID, "anna", False)
BEN = session.Identity(BEN_ID, "ben", False)
CLEO = session.Identity(CLEO_ID, "cleo", False)
DAN = session.Identity(DAN_ID, "dan", False)
MOD = session.Identity(MOD_ID, "sage", False)
LIMITS = limitmod.Limits()
ROOM = {"code": "abc234", "map": "Treasure.LVL", "n": 1, "of": 4}


def world(limits=LIMITS, **kwargs):
    w = World(mods=(), limits=limits, **kwargs)
    w.roles.roles[MOD_ID] = "mod"
    return w


def text_of(peer, kind):
    return [m for m in peer.sent if m["t"] == kind]


class Base(unittest.TestCase):
    def setUp(self):
        self.w = world()
        self.addCleanup(self.w.close)
        self.anna, self.anna_tab, self.ben, self.cleo, self.dan, self.guest = (self.w.join(i) for i in (ANNA, ANNA, BEN, CLEO, DAN, None))

    def say(self, peer, text, seconds=3):
        self.w.clock.now += seconds
        self.w.say(peer, text)

    def frame(self, peer, **fields):
        self.w.clock.now += 3
        self.w.chat.on_text(peer, json.dumps(fields))

    def here(self, peer, at="wait", **room):
        fields = dict(ROOM, **room) if at == "wait" else {}
        extra = {"inv": fields} if at == "wait" else {}
        self.frame(peer, t="here", at=at, pub=False, **extra)

    def replies(self, peer):
        return [m for m in peer.sent if m["t"] == "reply"]

    def alerts_of(self, peer):
        return [m["row"] for m in peer.sent if m["t"] == "alert"]

    def tick(self, seconds):
        """The hub's clock moves on a second at a time, and every second it looks at the invitations."""
        for _ in range(int(seconds)):
            self.w.clock.now += 1
            self.w.chat.people.sweep(self.w.clock.now)


# ---- invitations ---------------------------------------------------------------------------------------------------------------------------------

class TheInvitation(Base):
    def test_an_invitation_goes_to_every_socket_of_the_named_person_alone_and_the_inviter_is_told_ok(self):
        tab = self.w.join(BEN)
        self.here(self.anna)
        self.say(self.anna, "/invite Ben")
        self.assertEqual(self.replies(self.anna), [{"t": "reply", "c": "invite", "r": "ok", "to": "ben"}])
        rows = [self.alerts_of(p) for p in (self.ben, tab)]
        self.assertEqual(rows[0], rows[1])
        row = rows[0][0]
        self.assertEqual({k: row[k] for k in ("k", "u", "from", "map", "n", "of", "o", "code")}, {"k": "invite", "u": 1, "from": "anna", "map": "Treasure.LVL", "n": 1, "of": 4, "o": 1, "code": "abc234"})
        for other in (self.cleo, self.dan, self.guest, self.anna_tab):
            self.assertEqual([m for m in other.sent if m["t"] in ("alert", "alerts", "reply")], [], "an invitation went to somebody else")
        self.assertEqual([m for m in self.ben.sent if m["t"] == "line"], [])           # (an invitation is never a line of the chat)

    def test_the_invitation_is_kept_in_the_account_s_list_for_a_page_that_comes_later(self):
        self.here(self.anna)
        self.say(self.anna, "/invite ben")
        late = self.w.join(BEN)
        self.w.chat.people.send_list(late)
        rows = text_of(late, "alerts")[0]["rows"]
        self.assertEqual([(r["k"], r["from"], r["code"], r["u"]) for r in rows], [("invite", "anna", "abc234", 1)])

    def test_the_person_may_be_named_by_the_number_of_a_row_of_the_list_or_of_a_line(self):
        self.here(self.anna)
        row = self.w.chat.presence.row_id(BEN_ID)
        self.say(self.anna, "/invite row:%d" % row)
        self.say(self.ben, "a line of ben")
        self.say(self.anna, "/invite #1", seconds=61)
        self.say(self.anna, "/invite row:%d" % (row + 40), seconds=61)
        self.say(self.anna, "/invite #99", seconds=61)
        self.assertEqual([(r["r"], r.get("to")) for r in self.replies(self.anna)], [("ok", "ben"), ("ok", "ben"), ("nobody", None), ("nobody", None)])
        self.assertEqual(len(self.alerts_of(self.ben)), 2)

    def test_the_name_is_matched_in_any_letter_case_and_a_name_that_two_people_show_is_ambiguous(self):
        twin = self.w.join(session.Identity("8411000099990005", "ben", False))
        self.here(self.anna)
        self.say(self.anna, "/invite BEN")
        self.assertEqual([m["r"] for m in self.replies(self.anna)], ["ambiguous"])
        self.assertEqual([m for m in self.ben.sent + twin.sent if m["t"] == "alert"], [])
        self.w.chat.peers.discard(twin)
        self.say(self.anna, "/invite BEN")
        self.assertEqual([m["r"] for m in self.replies(self.anna)], ["ambiguous", "ok"])

    def test_what_cannot_be_invited_is_told_in_one_word_and_costs_nothing(self):
        self.say(self.anna, "/invite ben")                                       # (no room yet)
        self.here(self.anna, n=4, of=4)
        self.say(self.anna, "/invite ben")                                       # (a full room)
        self.here(self.anna)
        self.say(self.anna, "/invite")
        self.say(self.anna, "/invite zed")
        self.say(self.anna, "/invite anna")
        self.frame(self.ben, t="here", at="play", pub=False)
        self.say(self.anna, "/invite ben")                                       # (playing a match)
        self.here(self.cleo, code="abc234", n=2)
        self.say(self.anna, "/invite cleo")                                      # (in the room already)
        self.say(self.anna, "/invite " + "x" * 40)
        out = [(m["r"], m.get("to"), m.get("n"), m.get("of")) for m in self.replies(self.anna)]
        self.assertEqual(out, [("noroom", None, None, None), ("full", None, 4, 4), ("usage", None, None, None), ("nobody", None, None, None), ("self", None, None, None),
                               ("busy", "ben", None, None), ("inroom", "cleo", None, None), ("nobody", None, None, None)])
        for n in range(5):                                                       # (none of those counted against the five a minute: all five of these go)
            self.w.join(session.Identity("8411000088%06d" % n, "pal%d" % n, False))
            self.say(self.anna, "/invite pal%d" % n, seconds=0.1)
        self.assertEqual([m["r"] for m in self.replies(self.anna)[-5:]], ["ok"] * 5)

    def test_a_person_who_is_not_here_any_more_is_nobody_even_when_a_line_of_theirs_is_in_the_chat(self):
        self.here(self.anna)
        self.say(self.dan, "I am leaving")
        self.w.chat.peers.discard(self.dan)
        self.say(self.anna, "/invite dan")
        self.assertEqual([m["r"] for m in self.replies(self.anna)], ["nobody"])
        self.assertEqual(self.alerts_of(self.dan), [])

    def test_a_guest_has_no_name_to_invite_and_a_banned_account_is_nobody(self):
        self.here(self.anna)
        self.w.store.ban(DAN_ID, "0000000000000000", "dan")
        self.say(self.anna, "/invite dan")
        self.assertEqual([m["r"] for m in self.replies(self.anna)], ["nobody"])
        self.assertEqual(self.alerts_of(self.dan), [])
        self.say(self.guest, "/invite anna")
        self.assertEqual(self.guest.codes(), ["login"])                          # (a visitor who is not logged in cannot use a command at all)

    def test_one_invitation_to_a_person_a_minute_and_five_in_all(self):
        self.here(self.anna)
        self.say(self.anna, "/invite ben")
        self.say(self.anna, "/invite ben", seconds=14)
        self.say(self.anna, "/invite ben", seconds=45)
        self.say(self.anna, "/invite ben", seconds=1)
        self.assertEqual([(m["r"], m.get("wait")) for m in self.replies(self.anna)], [("ok", None), ("person", 46), ("person", 1), ("ok", None)])
        self.assertEqual(len(self.alerts_of(self.ben)), 2)

    def test_five_in_all_then_one_more_every_twelve_seconds(self):
        self.here(self.anna)
        people = [self.w.join(session.Identity("8411000088%06d" % n, "pal%d" % n, False)) for n in range(8)]
        for n in range(8):
            self.say(self.anna, "/invite pal%d" % n, seconds=0.5)
        got = [(m["r"], m.get("wait")) for m in self.replies(self.anna)]
        self.assertEqual([r for r, _ in got], ["ok"] * 5 + ["total"] * 3)
        self.assertTrue(all(isinstance(w, int) and 1 <= w <= 12 for r, w in got if r == "total"))
        self.assertEqual(sum(len(self.alerts_of(p)) for p in people), 5)
        self.say(self.anna, "/invite pal7", seconds=13)
        self.assertEqual(self.replies(self.anna)[-1]["r"], "ok")

    def test_the_limit_of_five_is_for_the_account_across_all_its_tabs(self):
        self.here(self.anna)
        people = [self.w.join(session.Identity("8411000088%06d" % n, "pal%d" % n, False)) for n in range(6)]
        for n in range(6):
            self.say(self.anna if n % 2 else self.anna_tab, "/invite pal%d" % n, seconds=0.1)
        sent = [m["r"] for p in (self.anna, self.anna_tab) for m in self.replies(p)]
        self.assertEqual(sorted(sent), ["ok"] * 5 + ["total"])
        self.assertEqual(sum(len(self.alerts_of(p)) for p in people), 5)

    def test_a_person_who_ignores_the_inviter_is_sent_nothing_and_the_inviter_is_told_ok_and_the_limits_count_all_the_same(self):
        self.say(self.ben, "/ignore anna")
        self.here(self.anna)
        self.say(self.anna, "/invite ben")
        self.say(self.anna, "/invite ben", seconds=5)
        self.assertEqual([(m["r"], m.get("wait")) for m in self.replies(self.anna)], [("ok", None), ("person", 55)])
        self.assertEqual([m for m in self.ben.sent if m["t"] in ("alert", "alerts")], [])
        self.w.chat.people.send_list(self.ben)
        self.assertEqual(text_of(self.ben, "alerts")[-1]["rows"], [])

    def test_a_person_who_holds_a_role_now_is_not_hidden_by_an_ignore_that_was_made_before(self):
        self.say(self.ben, "/ignore dan")
        self.w.roles.roles[DAN_ID] = "mod"                                         # (a person who held none when they were ignored may hold one now: the line, the invitation and the mention all come)
        self.here(self.dan)
        self.say(self.dan, "/invite ben")
        self.say(self.dan, "@ben hello", seconds=61)
        self.assertEqual([(r["k"], r["from"]) for r in self.alerts_of(self.ben)], [("invite", "dan"), ("mention", "dan")])

    def test_the_code_is_in_the_invitation_while_the_room_is_open_and_goes_when_it_is_not(self):
        self.here(self.anna)
        self.say(self.anna, "/invite ben")
        self.tick(60)
        self.assertEqual(text_of(self.ben, "alerts"), [])                          # (the page that keeps saying it is in the room keeps the invitation open: nothing to send)
        self.here(self.anna)
        self.tick(1)
        self.here(self.anna, at="look")                                           # (the room is left)
        self.tick(14)
        self.assertEqual(text_of(self.ben, "alerts"), [])                          # (not at once: a page that connects again must not end it)
        self.tick(2)
        rows = text_of(self.ben, "alerts")[-1]["rows"]
        self.assertEqual([("o" in r, "code" in r, r["from"]) for r in rows], [(False, False, "anna")])
        stored = self.w.store.alerts(self.w.chat.ref(BEN_ID))
        self.assertNotIn("abc234", stored[0].body)                                # (nothing of the room stays in the database)

    def test_a_room_that_comes_back_within_the_grace_keeps_the_invitation_open(self):
        self.here(self.anna)
        self.say(self.anna, "/invite ben")
        self.here(self.anna, at="look")
        self.tick(10)
        self.here(self.anna)
        self.tick(30)
        self.assertEqual(text_of(self.ben, "alerts"), [])
        self.w.chat.people.send_list(self.ben)
        self.assertEqual(text_of(self.ben, "alerts")[-1]["rows"][0]["code"], "abc234")

    def test_a_room_that_fills_a_room_that_changes_and_an_hour_end_it(self):
        for how in ("full", "another", "hour"):
            w = world()
            self.addCleanup(w.close)
            anna, ben = w.join(ANNA), w.join(BEN)
            w.chat.on_text(anna, json.dumps({"t": "here", "at": "wait", "pub": False, "inv": ROOM}))
            w.clock.now += 3
            w.say(anna, "/invite ben")
            ending = {"full": dict(ROOM, n=4), "another": dict(ROOM, code="zzz999")}.get(how)
            for n in range(1, 400):
                w.clock.now += 5 if how != "hour" else 10
                if ending:
                    w.chat.on_text(anna, json.dumps({"t": "here", "at": "wait", "pub": False, "inv": ending}))
                elif how == "hour":
                    w.chat.on_text(anna, json.dumps({"t": "here", "at": "wait", "pub": False, "inv": ROOM}))
                w.chat.people.sweep(w.clock.now)
                if text_of(ben, "alerts"):
                    break
            rows = text_of(ben, "alerts")[-1]["rows"]
            self.assertNotIn("code", rows[0], how)
            self.assertLess(w.clock.now - 1000, 3700 if how == "hour" else 40, how)
            if how == "hour":
                self.assertGreaterEqual(w.clock.now - 1000, 3600, how)

    def test_a_hub_that_starts_again_closes_the_invitations_that_were_open(self):
        self.here(self.anna)
        self.say(self.anna, "/invite ben")
        again = world(db=self.w.db)
        self.addCleanup(again.close)
        ben = again.join(BEN)
        again.chat.people.send_list(ben)
        row = text_of(ben, "alerts")[0]["rows"][0]
        self.assertEqual(("code" in row, "o" in row), (False, False))
        self.assertNotIn("abc234", again.store.alerts(again.chat.ref(BEN_ID))[0].body)

    def test_a_muted_account_cannot_invite(self):
        self.here(self.anna)
        self.w.store.mute(ANNA_ID, 10, "0000000000000000", "anna")
        self.say(self.anna, "/invite ben")
        self.assertEqual(self.anna.codes(), ["muted"])
        self.assertEqual(self.alerts_of(self.ben), [])


# ---- the room that a page says it is in ----------------------------------------------------------------------------------------------------------------

class TheRoomItSays(Base):
    def test_a_room_that_is_not_what_a_page_says_is_refused_and_changes_nothing(self):
        bad = [{"code": "ABC 234", "n": 1, "of": 4}, {"code": "abc234", "n": 5, "of": 4}, {"code": "abc234", "n": 3, "of": 2}, {"code": "abc234", "n": 1, "of": 0}, {"code": "abc234", "n": True, "of": 4},
               {"code": "abc234", "n": 1, "of": 4, "map": "a/b.lvl"}, {"code": "abc234", "n": 1, "of": 4, "extra": 1}, {"code": 7, "n": 1, "of": 4}, "abc234", ["abc234"], {"n": 1, "of": 4}]
        for inv in bad:
            self.frame(self.anna, t="here", at="wait", pub=False, inv=inv)
        self.assertEqual(self.anna.codes(), ["here"] * len(bad))
        self.say(self.anna, "/invite ben")
        self.assertEqual([m["r"] for m in self.replies(self.anna)], ["noroom"])

    def test_a_room_with_no_map_is_fine_and_a_page_that_is_not_waiting_has_none(self):
        self.frame(self.anna, t="here", at="wait", pub=False, inv={"code": "abc234", "n": 1, "of": 4})
        self.frame(self.anna_tab, t="here", at="look", inv=dict(ROOM))
        self.frame(self.anna_tab, t="here", at="play", pub=False, inv=dict(ROOM))
        self.assertEqual(self.anna.codes() + self.anna_tab.codes(), ["here", "here"])
        self.say(self.anna, "/invite ben")
        self.assertEqual([r["r"] for r in self.replies(self.anna)], ["ok"])
        self.assertNotIn("map", self.alerts_of(self.ben)[0])

    def test_a_page_that_says_the_room_again_with_no_code_has_left_it(self):
        self.here(self.anna)
        self.frame(self.anna, t="here", at="wait", pub=False)
        self.say(self.anna, "/invite ben")
        self.assertEqual([r["r"] for r in self.replies(self.anna)], ["noroom"])

    def test_the_newest_room_of_the_account_is_the_one_that_it_invites_to(self):
        self.here(self.anna, code="aaa111")
        self.here(self.anna_tab, code="bbb222")
        self.say(self.anna, "/invite ben")
        self.assertEqual(self.alerts_of(self.ben)[0]["code"], "bbb222")

    def test_the_code_of_a_room_is_in_no_list_and_in_no_frame_of_anybody_else(self):
        self.here(self.anna)
        self.frame(self.anna, t="who")
        self.w.chat.presence.refresh(self.w.clock.now + 10, force=True)
        for peer in (self.ben, self.guest):
            self.frame(peer, t="who")
        self.say(self.anna, "/invite cleo")
        seen = b"".join(self.ben.raw + self.guest.raw + self.dan.raw + self.anna_tab.raw)
        self.assertNotIn(b"abc234", seen)
        self.assertNotIn(b"Treasure", seen)


# ---- mentions ------------------------------------------------------------------------------------------------------------------------------------------

class TheMentions(Base):
    def marks(self, peer):
        return [m.get("m") for m in text_of(peer, "line")]

    def test_a_line_marks_the_names_in_the_units_of_the_page_and_every_screen_gets_the_same_marks(self):
        self.say(self.anna, "hey \U0001f600 @ben and @Cleo, look")
        key = self.w.chat.key_of
        want = [[7, 4, key(BEN_ID)], [16, 5, key(CLEO_ID)]]
        for peer in (self.ben, self.cleo, self.dan, self.guest, self.anna):
            self.assertEqual(self.marks(peer), [want])
        line = text_of(self.ben, "line")[0]["text"]
        self.assertEqual([line.encode("utf-16-le")[2 * s:2 * (s + n)].decode("utf-16-le") for s, n, _ in want], ["@ben", "@Cleo"])

    def test_the_history_of_a_page_that_comes_later_has_the_same_marks(self):
        self.say(self.anna, "@ben are you there")
        late = self.w.chat.welcome(FakePeer(DAN))["history"]
        self.assertEqual(late[0]["m"], [[0, 4, self.w.chat.key_of(BEN_ID)]])

    def test_the_rules_of_a_name(self):
        self.w.join(session.Identity("8411000099990006", "maple leaf", False))
        self.w.join(session.Identity("8411000099990007", "maple", False))
        cases = [("a@ben.com wrote", 0), ("mail me a@ben", 0), ("@bens are many", 0), ("@ben, @ben!", 2), ("(@ben)", 0), ("x@ben", 0), ("@BEN", 1), ("@maple leaf is here", 1), ("@maple is here", 1),
                 ("@maple leafs", 1), ("@everyone and @here", 0), ("@nobody.here", 0), ("@ben_", 0), ("@ben's", 1), ("@", 0), ("@ @ben", 1), ("\u200f@ben", 1)]
        for n, (line, count) in enumerate(cases):
            self.say(self.anna, line + " ." * n)                                                # (a line that differs: no duplicate)
        got = [len(m.get("m", [])) for m in text_of(self.dan, "line")]
        self.assertEqual(got, [c for _, c in cases])
        maple = text_of(self.dan, "line")[8]["m"]
        self.assertEqual(maple[0][1], len("@maple"))                                            # (the longest known name wins where two fit)
        leaf = text_of(self.dan, "line")[7]["m"]
        self.assertEqual(leaf[0][1], len("@maple leaf"))

    def test_a_name_that_two_people_show_is_marked_for_nobody(self):
        self.w.join(session.Identity("8411000099990005", "Ben", False))
        self.say(self.anna, "@ben hello")
        self.assertEqual(self.marks(self.dan), [None])
        self.assertEqual(self.alerts_of(self.ben), [])

    def test_a_person_who_is_not_here_can_be_named_when_a_line_of_theirs_is_in_the_chat_and_waits_for_the_list(self):
        self.say(self.dan, "I am leaving")
        self.w.chat.peers.discard(self.dan)
        self.say(self.anna, "@dan see you")
        self.assertEqual(self.marks(self.ben), [None, [[0, 4, self.w.chat.key_of(DAN_ID)]]])
        late = self.w.join(DAN)
        self.w.chat.people.send_list(late)
        self.assertEqual([(r["k"], r["from"], r["text"]) for r in text_of(late, "alerts")[0]["rows"]], [("mention", "anna", "@dan see you")])

    def test_a_person_who_never_wrote_and_is_not_here_is_nobody(self):
        self.say(self.anna, "@erin see you")
        self.assertEqual(self.marks(self.ben), [None])

    def test_the_named_person_alone_is_alerted_never_the_writer_and_the_alert_is_on_every_socket(self):
        tab = self.w.join(BEN)
        self.say(self.anna, "@ben and @anna and @cleo")
        for peer in (self.ben, tab):
            self.assertEqual([(r["k"], r["from"], r["text"]) for r in self.alerts_of(peer)], [("mention", "anna", "@ben and @anna and @cleo")])
        self.assertEqual(len(self.alerts_of(self.cleo)), 1)
        for peer in (self.anna, self.anna_tab, self.dan, self.guest):
            self.assertEqual(self.alerts_of(peer), [])

    def test_a_line_alerts_three_people_at_the_most_and_marks_them_all(self):
        people = [self.w.join(session.Identity("8411000088%06d" % n, "pal%d" % n, False)) for n in range(11, 24)]
        self.say(self.anna, " ".join("@pal%d" % n for n in range(11, 24)))
        self.assertEqual(len(self.marks(self.dan)[0]), 10)                                      # (ten names are marked in a line)
        self.assertEqual([len(self.alerts_of(p)) for p in people], [1, 1, 1] + [0] * 10)

    def test_the_same_writer_alerts_the_same_person_once_in_two_minutes_but_the_line_is_marked(self):
        self.say(self.anna, "@ben one")
        self.say(self.anna, "@ben two", seconds=100)
        self.assertEqual(len(self.marks(self.cleo)[1]), 1)
        self.assertEqual(len(self.alerts_of(self.ben)), 1)
        self.say(self.anna, "@ben three", seconds=25)
        self.assertEqual([r["text"] for r in self.alerts_of(self.ben)], ["@ben one", "@ben three"])
        self.say(self.cleo, "@ben from somebody else", seconds=1)
        self.assertEqual(len(self.alerts_of(self.ben)), 3)

    def test_a_person_gets_ten_mention_alerts_an_hour_from_everybody(self):
        writers = [self.w.join(session.Identity("8411000088%06d" % n, "w%d" % n, False)) for n in range(30, 42)]
        for n, writer in enumerate(writers):
            self.say(writer, "@ben number %d" % n, seconds=1)
        self.assertEqual(len(self.alerts_of(self.ben)), 10)
        self.say(writers[0], "@ben later", seconds=3600 / 10 + 125)
        self.assertEqual(len(self.alerts_of(self.ben)), 11)                                     # (a token comes back every six minutes)

    def test_a_person_who_ignores_the_writer_sees_no_line_and_gets_no_alert_and_the_others_see_it(self):
        self.say(self.ben, "/ignore anna")
        self.say(self.anna, "@ben are you there?")
        self.assertEqual(text_of(self.ben, "line"), [])
        self.assertEqual(self.alerts_of(self.ben), [])
        self.assertEqual(len(self.marks(self.cleo)), 1)
        self.assertEqual(self.replies(self.anna), [])                                           # (the writer is told nothing)

    def test_a_person_who_is_muted_is_still_alerted_and_a_banned_person_is_not(self):
        self.w.store.mute(BEN_ID, 10, "0000000000000000", "ben")
        self.w.store.ban(DAN_ID, "0000000000000000", "dan")
        self.say(self.anna, "@ben and @dan")
        self.assertEqual(len(self.alerts_of(self.ben)), 1)
        self.assertEqual(self.alerts_of(self.dan), [])
        self.assertEqual(len(self.marks(self.cleo)[0]), 1)

    def test_a_line_that_is_deleted_takes_its_alerts_out_of_the_list_and_tells_the_page(self):
        sage = self.w.join(MOD)
        self.say(self.anna, "@ben come")
        self.say(sage, "/del 1")
        rows = text_of(self.ben, "alerts")[-1]["rows"]
        self.assertEqual(rows, [])
        self.assertEqual(self.w.store.alerts(self.w.chat.ref(BEN_ID)), [])
        self.assertNotIn(1, self.w.chat.people.marks)

    def test_an_account_that_forgets_itself_leaves_nothing_of_its_name_in_the_lists_of_the_others(self):
        self.here(self.anna)
        self.say(self.anna, "/invite ben")
        self.say(self.anna, "@ben hi", seconds=61)
        self.assertEqual(len(self.w.store.alerts(self.w.chat.ref(BEN_ID))), 2)
        lines = self.w.store.forget(ANNA_ID, self.w.chat.deleted_seq)
        self.w.chat.people.forget(ANNA_ID)
        self.w.chat.drop_lines(lines)
        self.assertEqual(self.w.store.alerts(self.w.chat.ref(BEN_ID)), [])
        self.assertEqual(text_of(self.ben, "alerts")[-1]["rows"], [])
        self.here(self.anna, at="look")
        sent = len(text_of(self.ben, "alerts"))
        self.tick(40)
        self.assertEqual(len(text_of(self.ben, "alerts")), sent)                    # (its invitation is not looked at any more: nothing is sent to anybody when its room is gone)

    def test_the_text_of_the_alert_is_the_start_of_the_line(self):
        long = "@ben " + "word " * 40
        self.say(self.anna, long)
        quote = self.alerts_of(self.ben)[0]["text"]
        self.assertEqual(len(quote), 80)
        self.assertTrue(quote.endswith("\u2026") and long.startswith(quote[:-1].rstrip()))


# ---- the list ------------------------------------------------------------------------------------------------------------------------------------------

class TheList(Base):
    def test_a_page_is_sent_the_list_after_its_welcome_only_when_it_has_one_and_a_guest_has_none(self):
        self.say(self.anna, "@ben hello")
        for who, count in ((BEN, 1), (CLEO, 0), (None, 0)):
            peer = FakePeer(who)
            self.w.chat.peers.add(peer)
            self.w.chat.people.send_list(peer, force=False)
            self.assertEqual(len(text_of(peer, "alerts")), count, who)
        guest = FakePeer(None)
        self.w.chat.people.send_list(guest)
        self.assertEqual(guest.sent, [], "a visitor who is not logged in has no list, whatever asks")

    def test_the_list_is_newest_first_at_most_thirty_of_the_last_seven_days(self):
        for n in range(35):
            self.w.chat.people.add(BEN_ID, "join", {"name": "p%d" % n, "seat": "Red"})
        rows = self.w.chat.people.rows(BEN_ID)
        self.assertEqual([r["name"] for r in rows], ["p%d" % n for n in range(34, 4, -1)])
        self.w.wall.now += 7 * 86400 + 1
        self.assertEqual(self.w.chat.people.rows(BEN_ID), [])

    def test_seen_reads_up_to_the_number_and_every_socket_of_the_account_is_sent_the_list_again(self):
        tab = self.w.join(BEN)
        self.say(self.anna, "@ben one")
        self.say(self.cleo, "@ben two", seconds=1)
        first, second = [r["id"] for r in self.alerts_of(self.ben)]
        self.frame(self.ben, t="seen", upto=first)
        for peer in (self.ben, tab):
            rows = text_of(peer, "alerts")[-1]["rows"]
            self.assertEqual([(r["id"], r.get("u", 0)) for r in rows], [(second, 1), (first, 0)])
        before = len(text_of(self.ben, "alerts"))
        self.frame(self.ben, t="seen", upto=first)
        self.assertEqual(len(text_of(self.ben, "alerts")), before)                              # (nothing changed: nothing is sent)
        self.assertEqual(text_of(self.cleo, "alerts"), [])

    def test_a_person_cannot_read_or_clear_the_list_of_another(self):
        self.say(self.anna, "@ben one")
        self.frame(self.cleo, t="seen", upto=10 ** 9)
        self.frame(self.cleo, t="clearalerts")
        self.assertEqual(len(self.w.store.alerts(self.w.chat.ref(BEN_ID))), 1)
        self.assertEqual(self.w.store.alerts(self.w.chat.ref(BEN_ID))[0].unread, 1)

    def test_clear_empties_the_list_on_every_socket(self):
        tab = self.w.join(BEN)
        self.say(self.anna, "@ben one")
        self.frame(self.ben, t="clearalerts")
        for peer in (self.ben, tab):
            self.assertEqual(text_of(peer, "alerts")[-1]["rows"], [])

    def test_frames_that_are_not_what_a_page_says_are_mistakes_and_a_guest_is_told_to_log_in(self):
        for fields in ({"t": "seen"}, {"t": "seen", "upto": "7"}, {"t": "seen", "upto": -1}, {"t": "seen", "upto": True}, {"t": "seen", "upto": 1, "x": 1}, {"t": "clearalerts", "x": 1},
                       {"t": "note", "k": "invite", "name": "pat", "seat": "Red"}, {"t": "note", "k": "join", "name": "pat", "seat": "Purple"}, {"t": "note", "k": "join", "name": "<b>", "seat": "Red"},
                       {"t": "note", "k": "join", "name": "x" * 33, "seat": "Red"}, {"t": "note", "k": "join", "name": "pat"}):
            self.frame(self.ben, **fields)
        self.assertEqual(self.ben.codes(), ["bad"] * 11)
        for fields in ({"t": "seen", "upto": 1}, {"t": "clearalerts"}, {"t": "note", "k": "join", "name": "pat", "seat": "Red"}):
            self.frame(self.guest, **fields)
        self.assertEqual(self.guest.codes(), ["login"] * 3)

    def test_a_join_note_of_the_accounts_own_page_is_an_alert_of_its_own_list_only_and_is_limited(self):
        tab = self.w.join(BEN)
        self.frame(self.ben, t="note", k="join", name="juniper", seat="Red")
        for peer in (self.ben, tab):
            self.assertEqual([(r["k"], r["name"], r["seat"]) for r in self.alerts_of(peer)], [("join", "juniper", "Red")])
        for peer in (self.anna, self.cleo, self.guest):
            self.assertEqual(self.alerts_of(peer), [])
        for n in range(12):
            self.w.clock.now -= 2.9                                                         # (nearly no time passes)
            self.frame(self.ben, t="note", k="join", name="p%d" % n, seat="Blue")
        self.assertTrue(len(self.alerts_of(self.ben)) < 13 and "rate" in self.ben.codes())

    def test_the_row_of_an_alert_holds_nothing_that_the_kind_does_not_have(self):
        self.here(self.anna)
        self.say(self.anna, "/invite ben")
        self.say(self.anna, "@ben x", seconds=70)
        self.frame(self.ben, t="note", k="join", name="juniper", seat="Red")
        keys = {r["k"]: sorted(r) for r in self.w.chat.people.rows(BEN_ID)}
        self.assertEqual(keys, {"invite": ["at", "code", "from", "id", "k", "map", "n", "o", "of", "u"], "mention": ["at", "from", "id", "k", "text", "u"], "join": ["at", "id", "k", "name", "seat", "u"]})

    def test_the_text_of_an_alert_is_data_and_never_markup(self):
        name = "a&b'c"
        who = self.w.join(session.Identity("8411000099990008", name, False))
        self.say(who, "@ben x & y")
        self.assertEqual(self.alerts_of(self.ben)[0]["from"], name)
        self.assertNotIn(b"<", b"".join(self.ben.raw))


# ---- real sockets --------------------------------------------------------------------------------------------------------------------------------------

def send_json(client, **fields):
    client.send(hc.frame(1, json.dumps(fields).encode("utf-8")))


class TheLiveHub(unittest.TestCase):
    def setUp(self):
        self.clients = []
        self.addCleanup(lambda: [c.close() for c in self.clients])

    def connect(self, rig, ident):
        client = hc.Client(rig.port, login="%s:%s" % (ident.account_id, ident.username) if ident else None)
        self.clients.append(client)
        self.assertEqual(client.recv_json()["t"], "welcome")
        if ident:
            self.assertEqual(client.recv_json()["t"], "cmds")
        return client

    def next_of(self, client, kind, seconds=8.0):
        import time
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            for m in client.drain(0.1):
                if m["t"] == kind:
                    return m
        self.fail("no %s" % kind)

    def test_an_invitation_and_a_mention_reach_the_person_alone_and_the_list_is_there_again_after_a_new_connection(self):
        with hub_rig.Rig(roles=hub_rig.FakeRoles({})) as rig:
            anna, ben, cleo, guest = (self.connect(rig, i) for i in (ANNA, BEN, CLEO, None))
            send_json(anna, t="here", at="wait", pub=False, inv=ROOM)
            import time
            time.sleep(0.3)
            anna.say("/invite ben")
            self.assertEqual(self.next_of(anna, "reply"), {"t": "reply", "c": "invite", "r": "ok", "to": "ben"})
            row = self.next_of(ben, "alert")["row"]
            self.assertEqual((row["k"], row["from"], row["code"]), ("invite", "anna", "abc234"))
            for other in (cleo, guest):
                self.assertEqual([m for m in other.drain(0.4) if m["t"] in ("alert", "alerts", "reply")], [])
            time.sleep(1.1)
            anna.say("@ben still there?")
            line = self.next_of(cleo, "line")
            self.assertEqual(line["m"][0][:2], [0, 4])
            self.assertEqual(self.next_of(ben, "alert")["row"]["k"], "mention")
            ben.close()
            again = hc.Client(rig.port, login="%s:%s" % (BEN_ID, "ben"))
            self.clients.append(again)
            self.assertEqual([again.recv_json()["t"], again.recv_json()["t"]], ["welcome", "cmds"])
            rows = self.next_of(again, "alerts")["rows"]
            self.assertEqual([r["k"] for r in rows], ["mention", "invite"])
            send_json(again, t="seen", upto=rows[0]["id"])
            rows = self.next_of(again, "alerts")["rows"]
            self.assertEqual([r.get("u", 0) for r in rows], [0, 0])
            log = rig.log_text.getvalue()
            for part in (ANNA_ID, BEN_ID, "abc234", "still there"):
                self.assertNotIn(part, log, "the log holds %r" % part)


if __name__ == "__main__":
    unittest.main()
