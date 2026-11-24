#!/usr/bin/env python3
"""Private messages of the chat hub (hub/messages.py, hub/commands.py `/msg` and `/ignore`, hub/people.py `msg` alerts, hub/chat.py, hub/admin.py `reports`, hub/store/ dms and reports; docs/HUB_MESSAGES.md; run by
./run_tests.sh --fast and by the CI). Every id, token and name here is made up.

  TheSending        who may write to whom (everybody who is logged in: there is no request), where a message goes and where it does not, the bell's one row for each sender, a person who is not here
  TheRefusals       every refusal and every limit: the chat's rules, the text, the handles, the burst, the day, the new conversations, the dropped copy, the size of the tables
  TheBlock          a person who blocks another is sent nothing of theirs, and the sender sees an ordinary send; a person who holds a role is never blocked
  TheThread         opening, reading and deleting a conversation, what is kept (7 days, 300 messages, 100 shown), and what is sent after a purge
  TheReport         a report is the message and the 5 before it, copied by the hub, once, 5 a day, and the reported person is told nothing
  TheAdminsCannotRead  no frame, command or screen of the staff holds anybody's message but the copy of a reported one, which only the command line prints
  TheForgetting     an account that forgets itself takes its messages, for the other people of them as well
  TheLiveHub        real sockets: a message reaches the person alone, and a blocked sender's frames are those of any sender

tests/data/hub_messages_mutants.json breaks the rules one at a time for tools/mutate.py (every one must be caught).
"""
import dataclasses
import io
import json
import os
import sys
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
import hub_client as hc  # noqa: E402
import hub_rig  # noqa: E402
from test_hub_core import World  # noqa: E402
from hub import admin as adminmod, limits as limitmod, session  # noqa: E402

ANNA_ID, BEN_ID, CLEO_ID, DAN_ID, MOD_ID = ("8411000099990001", "8411000099990002", "8411000099990003", "8411000099990004", "8411000099990009")
ANNA, BEN, CLEO, DAN, MOD = (session.Identity(i, n, False) for i, n in ((ANNA_ID, "anna"), (BEN_ID, "ben"), (CLEO_ID, "cleo"), (DAN_ID, "dan"), (MOD_ID, "sage")))
LIMITS = limitmod.Limits()
DAY = 86400


class Base(unittest.TestCase):
    limits = LIMITS

    def setUp(self):
        self.w = World(mods=(), limits=self.limits)
        self.addCleanup(self.w.close)
        self.w.roles.roles[MOD_ID] = "mod"
        self.anna, self.anna_tab, self.ben, self.ben_tab, self.cleo, self.dan, self.guest, self.mod = (self.w.join(i) for i in (ANNA, ANNA, BEN, BEN, CLEO, DAN, None, MOD))

    def say(self, peer, text, seconds=3):
        self.w.clock.now += seconds
        self.w.say(peer, text)

    def handle(self, peer, name):
        """The handle that `peer` writes to the person by: the answer of /msg."""
        self.say(peer, "/msg " + name)
        reply = [m for m in peer.sent if m["t"] == "reply" and m["c"] == "msg"][-1]
        self.assertEqual(reply["r"], "open", reply)
        return reply["h"]

    def frame(self, peer, seconds=3, **fields):
        self.w.clock.now += seconds
        self.w.chat.on_text(peer, json.dumps(fields))

    def dm(self, peer, h, text, seconds=3):
        self.frame(peer, seconds, t="dm", h=h, text=text)

    def of(self, peer, kind):
        return [m for m in peer.sent if m["t"] == kind]

    def why(self, peer):
        return [m["why"] for m in self.of(peer, "dmx")]

    def theirs(self, peer):
        """The messages that the other person wrote to `peer`, as `peer`'s page was sent them."""
        return [m for m in self.of(peer, "msg") if m["me"] == 0]

    def thread(self, peer, h):
        before = len(self.of(peer, "thread"))
        self.frame(peer, t="dmopen", h=h)
        found = self.of(peer, "thread")
        self.assertEqual(len(found), before + 1)
        return found[-1]

    def mine(self, name):
        return self.w.chat.ref({"anna": ANNA_ID, "ben": BEN_ID, "cleo": CLEO_ID, "dan": DAN_ID}[name])


class TheSending(Base):
    def test_a_message_goes_to_every_socket_of_both_people_and_to_nobody_else(self):
        h = self.handle(self.anna, "ben")
        self.dm(self.anna, h, "hello ben")
        sent = [self.of(p, "msg") for p in (self.anna, self.anna_tab)]
        self.assertEqual(sent[0], sent[1])
        self.assertEqual({k: sent[0][0][k] for k in ("name", "text", "me", "u")}, {"name": "ben", "text": "hello ben", "me": 1, "u": 0})
        got = [self.of(p, "msg") for p in (self.ben, self.ben_tab)]
        self.assertEqual(got[0], got[1])
        self.assertEqual({k: got[0][0][k] for k in ("name", "text", "me", "u", "f")}, {"name": "anna", "text": "hello ben", "me": 0, "u": 1, "f": 0})
        for other in (self.cleo, self.dan, self.guest, self.mod):
            self.assertEqual([m for m in other.sent if m["t"] in ("msg", "msgs", "alert", "alerts", "thread")], [], "somebody else was sent it")
        self.assertEqual(self.of(self.ben, "line"), [])                                      # (never a line of the chat)
        self.assertEqual(self.w.store.recent_lines(10), [])

    def test_a_first_message_from_a_person_who_is_not_followed_arrives_as_any_other_there_is_no_request(self):
        h = self.handle(self.cleo, "dan")
        self.dm(self.cleo, h, "hi, new here")
        self.assertEqual([m["text"] for m in self.of(self.dan, "msg")], ["hi, new here"])
        self.assertEqual(self.of(self.dan, "dmx"), [])

    def test_the_receiver_gets_one_bell_row_for_each_sender_with_none_of_the_words_and_the_next_message_replaces_it(self):
        h = self.handle(self.anna, "ben")
        hc_ = self.handle(self.cleo, "ben")
        self.dm(self.anna, h, "secret one")
        self.dm(self.anna, h, "secret two")
        self.dm(self.cleo, hc_, "secret three")
        rows = [m for m in self.ben.sent if m["t"] == "alerts"][-1]["rows"]
        self.assertEqual(sorted((r["k"], r["from"], r["u"]) for r in rows), [("msg", "anna", 1), ("msg", "cleo", 1)])
        self.assertTrue(all(set(r) == {"id", "at", "k", "u", "from", "h"} for r in rows))
        blob = b"".join(raw for raw in self.ben.raw if b'"t":"alert' in raw)
        for word in (b"secret one", b"secret two", b"secret three"):
            self.assertNotIn(word, blob, "the bell holds the words of a message")
        self.assertEqual(len(self.w.store.alerts(self.mine("ben"))), 2)
        self.assertTrue(all(m["row"]["k"] == "msg" for m in self.of(self.ben, "alert")))

    def test_the_text_is_cleaned_as_a_line_of_the_chat_is(self):
        h = self.handle(self.anna, "ben")
        self.dm(self.anna, h, "  hel\u200blo \u00a0  ben  ")
        self.assertEqual(self.of(self.ben, "msg")[0]["text"], "hello ben")

    def test_a_person_who_is_not_here_has_it_in_the_list_after_the_next_welcome_and_can_read_it(self):
        self.say(self.anna, "/follow ben")
        h = [m for m in self.anna.sent if m["t"] == "follows"][-1]["rows"][0]["h"]
        self.w.chat.peers.discard(self.ben)
        self.w.chat.peers.discard(self.ben_tab)
        self.dm(self.anna, h, "when you are back")
        back = self.w.join(BEN)
        self.w.chat.messages.send_list(back)
        listing = self.of(back, "msgs")[0]
        self.assertEqual((listing["n"], [(r["name"], r["text"], r["u"], r["me"]) for r in listing["rows"]]), (1, [("anna", "when you are back", 1, 0)]))
        self.assertEqual([r["text"] for r in self.thread(back, listing["rows"][0]["h"])["rows"]], ["when you are back"])

    def test_the_list_after_the_welcome_is_there_for_a_person_with_conversations_and_not_for_one_with_none_or_a_reader(self):
        h = self.handle(self.anna, "ben")
        self.dm(self.anna, h, "one")
        for peer in (self.cleo, self.guest):
            self.w.chat.messages.send_list(peer)
            self.assertEqual(self.of(peer, "msgs"), [])
        late = self.w.join(ANNA)
        self.w.chat.messages.send_list(late)
        row = self.of(late, "msgs")[0]["rows"][0]
        self.assertEqual({k: row[k] for k in ("name", "text", "me", "u", "f")}, {"name": "ben", "text": "one", "me": 1, "u": 0, "f": 0})

    def test_a_person_that_the_account_follows_has_a_star_in_the_list_and_on_a_message(self):
        self.say(self.ben, "/follow anna")
        h = self.handle(self.anna, "ben")
        self.dm(self.anna, h, "hi")
        self.assertEqual(self.of(self.ben, "msg")[0]["f"], 1)
        self.assertEqual(self.of(self.anna, "msg")[0]["f"], 0)
        late = self.w.join(BEN)
        self.w.chat.messages.send_list(late)
        self.assertEqual(self.of(late, "msgs")[0]["rows"][0]["f"], 1)


class TheRefusals(Base):
    def test_a_guest_is_refused_with_login_and_a_malformed_frame_is_bad(self):
        self.frame(self.guest, t="dm", h="0" * 12, text="x")
        self.assertEqual(self.guest.codes(), ["login"])
        h = self.handle(self.anna, "ben")
        for fields in ({"t": "dm", "h": h}, {"t": "dm", "h": h, "text": "x", "to": "ben"}, {"t": "dm", "h": 7, "text": "x"}, {"t": "dm", "h": "XYZ", "text": "x"}, {"t": "dmopen"}, {"t": "dmreport", "h": h, "id": "1", "why": "abuse"},
                       {"t": "dmreport", "h": h, "id": 1, "why": "rude"}, {"t": "dmdel", "h": h, "x": 1}):
            before = self.anna.strikes
            self.frame(self.anna, **fields)
            self.assertEqual(self.anna.strikes, before + 1, fields)
        self.assertEqual(self.anna.codes(), ["bad"] * 8)
        self.assertEqual(self.of(self.ben, "msg"), [])

    def test_the_text_rules_of_the_chat_empty_long_and_markup(self):
        h = self.handle(self.anna, "ben")
        self.dm(self.anna, h, "   ")
        self.dm(self.anna, h, "x" * 501)
        self.dm(self.anna, h, "a <b> tag")
        self.dm(self.anna, h, 7)
        self.dm(self.anna, h, "y" * 500)
        self.assertEqual(self.why(self.anna), ["empty", "long", "bad", "bad"])
        self.assertEqual([m["text"] for m in self.of(self.ben, "msg")], ["y" * 500])

    def test_a_person_who_is_banned_muted_or_too_new_may_not_write_and_the_receiver_is_sent_nothing(self):
        h = self.handle(self.anna, "ben")
        young = self.w.join(session.Identity("8411000099990007", "newbie", True))
        hy = self.w.chat.follows.teach("8411000099990007", self.mine("ben"), "ben")                  # (an account that is too new has no commands: the handle comes from a list that it was sent)
        self.dm(young, hy, "hi")
        self.w.store.mute(ANNA_ID, 60, "by")
        self.dm(self.anna, h, "muted")
        self.w.store.mute(ANNA_ID, 0, "by")
        self.w.store.ban(ANNA_ID, "by", "anna")
        self.dm(self.anna, h, "banned")
        self.assertEqual(self.why(young), ["young"])
        self.assertEqual(self.why(self.anna), ["muted", "banned"])
        self.assertEqual(self.of(self.ben, "msg"), [])

    def test_a_handle_that_the_hub_did_not_give_the_account_and_ones_own_handle_mean_nobody(self):
        h = self.handle(self.anna, "ben")
        self.dm(self.cleo, h, "borrowed handle")
        self.dm(self.anna, "0" * 12, "nobody")
        self.say(self.anna, "/msg anna")
        self.say(self.anna, "/msg zed")
        self.say(self.anna, "/msg")
        self.assertEqual(self.why(self.cleo), ["who"])
        self.assertEqual(self.why(self.anna), ["who"])
        self.assertEqual([m["r"] for m in self.anna.sent if m["t"] == "reply" and m["c"] == "msg"][1:], ["self", "nobody", "usage"])
        self.assertEqual(self.of(self.ben, "msg"), [])
        mine = self.w.chat.ref(ANNA_ID)
        self.w.chat.follows.teach(ANNA_ID, mine, "anna")
        self.dm(self.anna, self.w.chat.handle(mine), "to myself")
        self.assertEqual(self.why(self.anna)[-1], "who")

    def test_a_refusal_is_no_mistake_of_the_socket_but_it_costs_a_frame(self):
        h = self.handle(self.anna, "ben")
        before = (self.anna.strikes, self.anna.charges)
        self.dm(self.anna, h, "   ")
        self.dm(self.anna, "0" * 12, "nobody")
        self.assertEqual((self.anna.strikes, self.anna.charges), (before[0], before[1] + 2))

    def test_the_calls_that_only_read_or_delete_are_a_burst_of_30_then_two_a_second(self):
        h = self.handle(self.anna, "ben")
        for _ in range(31):
            self.frame(self.anna, 0, t="dmread", h=h)
        self.assertEqual(self.why(self.anna), ["rate"])
        self.frame(self.anna, 1, t="dmread", h=h)
        self.assertEqual(self.why(self.anna), ["rate"])

    def test_a_burst_of_5_then_one_every_2_seconds(self):
        h = self.handle(self.anna, "ben")
        for n in range(6):
            self.dm(self.anna, h, "message %d" % n, seconds=0)
        self.assertEqual(self.why(self.anna), ["rate"])
        self.dm(self.anna, h, "later", seconds=2)
        self.assertEqual(len(self.of(self.ben, "msg")), 6)
        self.assertEqual(self.why(self.anna), ["rate"])

    def test_the_same_text_to_the_same_person_twice_in_30_seconds_is_dropped_and_to_another_person_it_is_not(self):
        h, hc_ = self.handle(self.anna, "ben"), self.handle(self.anna, "cleo")
        self.dm(self.anna, h, "same")
        self.dm(self.anna, h, "same", seconds=10)
        self.dm(self.anna, hc_, "same", seconds=3)
        self.dm(self.anna, h, "same", seconds=30)
        self.assertEqual(self.why(self.anna), ["dup"])
        self.assertEqual((len(self.of(self.ben, "msg")), len(self.of(self.cleo, "msg"))), (2, 1))

    def test_the_day_s_budget_is_spent_and_a_refused_message_costs_nothing(self):
        w = World(mods=(), limits=dataclasses.replace(LIMITS, dm_day=3))
        self.addCleanup(w.close)
        anna, ben = w.join(ANNA), w.join(BEN)
        w.clock.now += 3
        w.say(anna, "/msg ben")
        h = [m for m in anna.sent if m["t"] == "reply"][-1]["h"]
        for n in range(5):
            w.clock.now += 3
            w.chat.on_text(anna, json.dumps({"t": "dm", "h": h, "text": "m%d" % n}))
        self.assertEqual([m["why"] for m in anna.sent if m["t"] == "dmx"], ["quota", "quota"])
        self.assertEqual(len([m for m in ben.sent if m["t"] == "msg"]), 3)

    def test_new_conversations_are_limited_by_the_hour_and_a_conversation_that_exists_is_not_new(self):
        w = World(mods=(), limits=dataclasses.replace(LIMITS, dm_new_hour=2))
        self.addCleanup(w.close)
        anna = w.join(ANNA)
        others = [w.join(session.Identity("84110000777%05d" % n, "pal%d" % n, False)) for n in range(3)]
        handles = []
        for n in range(3):
            w.clock.now += 3
            w.say(anna, "/msg pal%d" % n)
            handles.append([m for m in anna.sent if m["t"] == "reply"][-1]["h"])
        for h in handles:
            w.clock.now += 3
            w.chat.on_text(anna, json.dumps({"t": "dm", "h": h, "text": "first"}))
        w.clock.now += 3
        w.chat.on_text(anna, json.dumps({"t": "dm", "h": handles[0], "text": "second"}))
        self.assertEqual([m["why"] for m in anna.sent if m["t"] == "dmx"], ["new"])
        self.assertEqual([len([m for m in o.sent if m["t"] == "msg"]) for o in others], [2, 1, 0])
        w.clock.now += 3600
        w.chat.on_text(anna, json.dumps({"t": "dm", "h": handles[2], "text": "third, an hour later"}))
        self.assertEqual(len([m for m in others[2].sent if m["t"] == "msg"]), 1)

    def test_the_tables_stop_at_a_quarter_of_the_database_and_nobody_s_message_is_deleted(self):
        w = World(mods=(), limits=dataclasses.replace(LIMITS, db_max_bytes=2000))
        self.addCleanup(w.close)
        anna, ben = w.join(ANNA), w.join(BEN)
        w.clock.now += 3
        w.say(anna, "/msg ben")
        h = [m for m in anna.sent if m["t"] == "reply"][-1]["h"]
        for n in range(8):
            w.clock.now += 3
            w.chat.on_text(anna, json.dumps({"t": "dm", "h": h, "text": "a message of some length %d" % n}))
        refused = [m["why"] for m in anna.sent if m["t"] == "dmx"]
        kept = len([m for m in ben.sent if m["t"] == "msg"])
        self.assertTrue(refused and set(refused) == {"full"}, refused)
        self.assertEqual(len(refused) + kept, 8)
        self.assertEqual(len(w.store.dm_thread(w.chat.ref(ANNA_ID), w.chat.ref(BEN_ID), 100)), kept)

    def test_a_store_that_refuses_is_busy_and_the_message_may_be_tried_again(self):
        h = self.handle(self.anna, "ben")
        real = self.w.store.dm_add

        def refuse(*args, **kwargs):
            from hub.store import StoreError
            raise StoreError("locked")
        self.w.store.dm_add = refuse
        self.dm(self.anna, h, "once")
        self.w.store.dm_add = real
        self.dm(self.anna, h, "once", seconds=0)
        self.assertEqual(self.why(self.anna), ["busy"])
        self.assertEqual([m["text"] for m in self.of(self.ben, "msg")], ["once"])


class TheBlock(Base):
    def test_a_blocked_sender_gets_the_answer_of_any_sender_and_the_receiver_is_sent_nothing_at_all(self):
        hb, hc_ = self.handle(self.anna, "ben"), self.handle(self.anna, "cleo")
        self.say(self.ben, "/ignore anna")
        self.assertEqual([m["r"] for m in self.ben.sent if m["t"] == "reply" and m["c"] == "ignore"], ["ok"])
        mark = {p: len(p.sent) for p in (self.ben, self.ben_tab)}
        self.dm(self.anna, hb, "to the person who blocked me")
        self.dm(self.anna, hc_, "to the person who did not")
        a = [m for m in self.of(self.anna, "msg")]
        self.assertEqual([sorted(m) for m in a], [sorted(a[0])] * 2)
        self.assertEqual([m["me"] for m in a], [1, 1])
        self.assertEqual(self.of(self.anna, "dmx"), [])
        for p in (self.ben, self.ben_tab):
            self.assertEqual(p.sent[mark[p]:], [], "the blocker was sent something")
        self.assertEqual(len(self.w.store.alerts(self.mine("ben"))), 0)
        self.assertEqual(self.thread(self.anna, hb)["rows"][-1]["text"], "to the person who blocked me")         # (the sender keeps its own copy)
        self.assertEqual(len(self.of(self.cleo, "msg")), 1)

    def test_what_a_blocked_sender_wrote_is_never_in_the_receivers_thread_or_list_even_after_the_block_is_lifted(self):
        hb = self.handle(self.anna, "ben")
        self.dm(self.anna, hb, "before the block")
        self.say(self.ben, "/ignore anna")
        self.dm(self.anna, hb, "while blocked")
        hab = self.handle(self.ben, "anna")
        self.say(self.ben, "/unignore anna")
        self.dm(self.anna, hb, "after the block")
        self.assertEqual([r["text"] for r in self.thread(self.ben, hab)["rows"]], ["before the block", "after the block"])
        self.assertEqual([r["text"] for r in self.thread(self.anna, hb)["rows"]], ["before the block", "while blocked", "after the block"])
        self.assertEqual([m["text"] for m in self.of(self.ben, "msg")], ["before the block", "after the block"])

    def test_the_list_says_which_people_are_blocked_and_block_by_handle_works_for_a_person_who_is_not_here(self):
        hb = self.handle(self.anna, "ben")
        self.dm(self.anna, hb, "hello")
        hab = self.handle(self.ben, "anna")
        self.say(self.ben, "/ignore h:%s" % hab)
        self.assertEqual([m["r"] for m in self.ben.sent if m["t"] == "reply" and m["c"] == "ignore"], ["ok"])
        listing = self.of(self.ben, "msgs")[-1]
        self.assertEqual([(r["name"], r.get("b")) for r in listing["rows"]], [("anna", 1)])
        self.assertEqual(self.thread(self.ben, hab).get("b"), 1)
        self.say(self.ben, "/unignore h:%s" % hab)
        self.assertNotIn("b", self.of(self.ben, "msgs")[-1]["rows"][0])

    def test_a_person_who_holds_a_role_is_never_blocked(self):
        hm = self.handle(self.mod, "ben")
        self.say(self.ben, "/ignore sage")
        self.assertEqual([m["r"] for m in self.ben.sent if m["t"] == "reply" and m["c"] == "ignore"], ["staff"])
        self.w.roles.roles[CLEO_ID] = "none"
        self.say(self.ben, "/ignore cleo")
        self.w.roles.roles[CLEO_ID] = "mod"                                                   # (a person who held no role when they were blocked holds one now)
        hc_ = self.handle(self.cleo, "ben")
        self.dm(self.cleo, hc_, "from a moderator")
        self.dm(self.mod, hm, "from sage")
        self.assertEqual([m["text"] for m in self.of(self.ben, "msg")], ["from a moderator", "from sage"])


class TheThread(Base):
    def talk(self):
        h = self.handle(self.anna, "ben")
        self.dm(self.anna, h, "one")
        hb = self.handle(self.ben, "anna")
        self.dm(self.ben, hb, "two")
        self.dm(self.anna, h, "three")
        return h, hb

    def test_opening_a_conversation_sends_it_to_the_asking_socket_alone_and_reads_it(self):
        h, hb = self.talk()
        self.assertEqual(self.w.store.dm_conversations(self.mine("ben"))[0][2], 2)
        self.w.chat.messages.send_lists(BEN_ID)
        self.assertEqual(self.of(self.ben, "msgs")[-1]["n"], 2)
        mark = len(self.ben_tab.sent)
        got = self.thread(self.ben, hb)
        self.assertEqual([(r["text"], r.get("me", 0)) for r in got["rows"]], [("one", 0), ("two", 1), ("three", 0)])
        self.assertEqual((got["name"], got["f"]), ("anna", 0))
        self.assertEqual(self.of(self.ben_tab, "thread"), [])
        self.assertEqual(self.of(self.ben, "msgs")[-1]["n"], 0)                                 # (read: the count is gone on every page)
        self.assertEqual(self.of(self.ben_tab, "msgs")[-1]["n"], 0)
        self.assertGreater(len(self.ben_tab.sent), mark)
        self.assertEqual(self.w.store.alerts(self.mine("ben")), [])                              # (and so is the bell's row)
        self.assertEqual(self.w.store.dm_unread(self.mine("ben"), self.mine("anna")), 0)

    def test_dmread_reads_what_came_in_while_the_conversation_was_open(self):
        h, hb = self.talk()
        self.frame(self.ben, t="dmread", h=hb)
        self.assertEqual(self.w.store.dm_unread(self.mine("ben"), self.mine("anna")), 0)
        self.assertEqual(self.of(self.ben, "thread"), [])
        self.dm(self.anna, h, "four")
        self.assertEqual(self.w.store.dm_unread(self.mine("ben"), self.mine("anna")), 1)
        self.assertEqual(self.of(self.ben, "msg")[-1]["u"], 1)

    def test_a_conversation_that_one_person_deletes_is_gone_for_them_alone_and_for_both_it_is_gone_from_the_file(self):
        h, hb = self.talk()
        self.frame(self.anna, t="dmdel", h=h)
        self.assertEqual(self.thread(self.anna, h)["rows"], [])
        self.assertEqual([r["text"] for r in self.thread(self.ben, hb)["rows"]], ["one", "two", "three"])
        self.assertEqual(self.of(self.anna, "msgs")[-1]["rows"], [])
        self.dm(self.ben, hb, "four")
        self.assertEqual([r["text"] for r in self.thread(self.anna, h)["rows"]], ["four"])      # (a new message starts it again)
        self.frame(self.ben, t="dmdel", h=hb)
        self.frame(self.anna, t="dmdel", h=h)
        self.assertEqual(self.w.store.db.execute("SELECT COUNT(*) FROM dms").fetchone()[0], 0)

    def test_300_messages_are_kept_and_100_are_shown(self):
        w = World(mods=(), limits=dataclasses.replace(LIMITS, dm_burst=1000, dm_day=100000, dm_thread_max=300, dm_refill_seconds=0.001))
        self.addCleanup(w.close)
        anna, ben = w.join(ANNA), w.join(BEN)
        w.clock.now += 3
        w.say(anna, "/msg ben")
        h = [m for m in anna.sent if m["t"] == "reply"][-1]["h"]
        for n in range(310):
            w.clock.now += 1
            w.chat.on_text(anna, json.dumps({"t": "dm", "h": h, "text": "m%d" % n}))
        w.chat.on_text(anna, json.dumps({"t": "dmopen", "h": h}))
        rows = [m for m in anna.sent if m["t"] == "thread"][-1]["rows"]
        self.assertEqual((len(rows), rows[0]["text"], rows[-1]["text"]), (100, "m210", "m309"))
        self.assertEqual(w.store.db.execute("SELECT COUNT(*) FROM dms").fetchone()[0], 300)

    def test_a_message_is_kept_for_7_days_then_the_purge_deletes_it_and_the_open_pages_are_sent_their_lists(self):
        h, hb = self.talk()
        self.w.wall.now += 6 * DAY
        self.assertEqual(len(self.thread(self.ben, hb)["rows"]), 3)
        self.assertEqual(self.of(self.ben, "msgs")[-1]["n"], 0)
        self.w.wall.now += DAY + 60
        self.assertEqual(self.w.store.dm_thread(self.mine("ben"), self.mine("anna"), 100), [])                   # (past its days, whatever the file still holds)
        self.assertEqual(self.w.store.db.execute("SELECT COUNT(*) FROM dms").fetchone()[0], 3)
        self.w.store.purge()
        self.assertEqual(self.w.store.db.execute("SELECT COUNT(*) FROM dms").fetchone()[0], 0)
        mark = len(self.ben.sent)
        self.w.chat.messages.after_purge()
        self.assertEqual(self.of(self.ben, "msgs")[-1]["rows"], [])
        self.assertGreater(len(self.ben.sent), mark)

    def test_the_table_is_not_made_by_reading_purging_or_forgetting(self):
        self.w.say(self.anna, "hello")
        self.w.chat.messages.send_list(self.anna)
        self.w.wall.now += 40 * DAY
        self.w.store.purge()
        self.w.store.forget(ANNA_ID)
        tables = {r[0] for r in self.w.store.db.execute("SELECT name FROM sqlite_master WHERE type = 'table'")}
        self.assertTrue({"dms", "reports", "follows"}.isdisjoint(tables))


class TheReport(Base):
    def conversation(self, n=8):
        h, hb = self.handle(self.anna, "ben"), self.handle(self.ben, "anna")
        for k in range(n):
            if k % 2:
                self.dm(self.anna, h, "anna %d" % k)
            else:
                self.dm(self.ben, hb, "ben %d" % k)
        return h, hb

    def test_a_report_is_the_message_and_the_5_before_it_copied_by_the_hub_and_the_reported_person_is_told_nothing(self):
        h, hb = self.conversation(8)
        reported = self.theirs(self.anna)[-1]["id"]
        mark = len(self.ben.sent)
        self.frame(self.anna, t="dmreport", h=h, id=reported, why="abuse")
        self.assertEqual(self.of(self.anna, "dmreported"), [{"t": "dmreported", "h": h, "id": reported}])
        self.assertEqual(self.ben.sent[mark:], [], "the reported person was told")
        rows = self.w.store.reports()
        self.assertEqual(len(rows), 1)
        report = rows[0]
        self.assertEqual((report.reporter_name, report.reported_name, report.why, report.closed), ("anna", "ben", "abuse", 0))
        self.assertEqual([(who, text) for _, who, text in report.messages], [("r", "anna 1"), ("d", "ben 2"), ("r", "anna 3"), ("d", "ben 4"), ("r", "anna 5"), ("d", "ben 6")])

    def test_the_copy_is_what_the_hub_holds_and_a_message_that_the_reporter_cannot_see_cannot_be_reported(self):
        h, hb = self.conversation(2)
        own = self.of(self.anna, "msg")[-1]["id"]
        self.assertEqual(self.of(self.anna, "msg")[-1]["me"], 1)
        self.frame(self.anna, t="dmreport", h=h, id=own, why="spam")                           # (a message of their own: it is not from the person)
        self.frame(self.anna, t="dmreport", h=h, id=99999, why="spam")
        self.assertEqual(self.why(self.anna), ["gone", "gone"])
        self.frame(self.anna, t="dmreport", h="0" * 12, id=1, why="spam")
        self.assertEqual(self.why(self.anna)[-1], "who")
        self.assertEqual(self.w.store.reports(), [])

    def test_a_message_of_another_person_cannot_be_reported_as_this_person_s(self):
        h = self.handle(self.anna, "ben")
        hc_ = self.handle(self.cleo, "anna")
        self.dm(self.cleo, hc_, "from cleo")
        cleos = self.theirs(self.anna)[-1]["id"]
        self.frame(self.anna, t="dmreport", h=h, id=cleos, why="abuse")
        self.assertEqual((self.why(self.anna), self.w.store.reports()), (["gone"], []))

    def test_a_message_that_was_hidden_from_the_reporter_is_not_in_the_copy_and_cannot_be_reported(self):
        h, hb = self.handle(self.anna, "ben"), self.handle(self.ben, "anna")
        self.dm(self.ben, hb, "first")
        self.say(self.anna, "/ignore ben")
        self.dm(self.ben, hb, "hidden from anna")
        hidden = self.of(self.ben, "msg")[-1]["id"]
        self.frame(self.anna, t="dmreport", h=h, id=hidden, why="abuse")
        self.assertEqual(self.why(self.anna), ["gone"])
        self.say(self.anna, "/unignore ben")
        self.dm(self.ben, hb, "last")
        self.frame(self.anna, t="dmreport", h=h, id=self.theirs(self.anna)[-1]["id"], why="abuse")
        self.assertEqual([text for _, _, text in self.w.store.reports()[0].messages], ["first", "last"])

    def test_the_same_message_reported_twice_is_one_report_and_5_reports_are_the_most_in_a_day(self):
        h, hb = self.handle(self.anna, "ben"), self.handle(self.ben, "anna")
        for n in range(7):
            self.dm(self.ben, hb, "message %d" % n)
        ids = [m["id"] for m in self.theirs(self.anna)]
        self.frame(self.anna, t="dmreport", h=h, id=ids[0], why="abuse")
        self.frame(self.anna, t="dmreport", h=h, id=ids[0], why="spam")
        self.assertEqual(len(self.w.store.reports()), 1)
        for i in ids[1:4]:
            self.frame(self.anna, t="dmreport", h=h, id=i, why="other")
        self.assertEqual((len(self.w.store.reports()), self.why(self.anna)), (4, []))
        self.frame(self.anna, t="dmreport", h=h, id=ids[4], why="other")                          # (the sixth call of the day: the repeat counted as one)
        self.assertEqual((len(self.w.store.reports()), self.why(self.anna)), (4, ["rate"]))
        self.w.clock.now += DAY
        self.frame(self.anna, t="dmreport", h=h, id=ids[4], why="other")
        self.assertEqual(len(self.w.store.reports()), 5)

    def test_the_command_line_prints_the_copy_and_nothing_else_and_closes_a_report(self):
        h, hb = self.conversation(4)
        self.dm(self.ben, hb, "never reported, and a secret", seconds=3)
        self.frame(self.anna, t="dmreport", h=h, id=self.theirs(self.anna)[1]["id"], why="abuse")          # (the second message that ben wrote: ben 2; the one after it is not in the copy)
        out = io.StringIO()
        adminmod.show_reports(self.w.store, False, out)
        text = out.getvalue()
        self.assertIn("report 1", text)
        self.assertIn("reported ben (%s)" % self.mine("ben"), text)
        self.assertIn("by anna (%s)" % self.mine("anna"), text)
        self.assertIn("ben 0", text)
        self.assertIn("ben 2", text)
        self.assertNotIn("a secret", text)
        for account_id in (ANNA_ID, BEN_ID):
            self.assertNotIn(account_id, text)
        self.assertEqual(adminmod.run(["--db", self.w.db, "report-close", "1"], io.StringIO(), io.StringIO()), 0)
        self.assertEqual(adminmod.run(["--db", self.w.db, "report-close", "1"], io.StringIO(), io.StringIO()), 1)
        out = io.StringIO()
        adminmod.show_reports(self.w.store, False, out)
        self.assertNotIn("report 1", out.getvalue())
        out = io.StringIO()
        adminmod.show_reports(self.w.store, True, out)
        self.assertIn("closed", out.getvalue())

    def test_a_report_is_kept_30_days_after_it_was_closed_and_90_if_nobody_closes_it(self):
        h, hb = self.conversation(2)
        self.frame(self.anna, t="dmreport", h=h, id=self.theirs(self.anna)[0]["id"], why="abuse")
        self.w.wall.now += 89 * DAY
        self.w.store.purge()
        self.assertEqual(len(self.w.store.reports()), 1)
        self.w.wall.now += 2 * DAY
        self.w.store.purge()
        self.assertEqual(self.w.store.reports(True), [])
        self.dm(self.ben, self.w.chat.follows.teach(BEN_ID, self.mine("anna"), "anna"), "again")
        self.frame(self.anna, t="dmreport", h=h, id=self.theirs(self.anna)[-1]["id"], why="abuse")
        rid = self.w.store.reports()[0].id
        self.assertTrue(self.w.store.report_close(rid))
        self.w.wall.now += 29 * DAY
        self.w.store.purge()
        self.assertEqual(len(self.w.store.reports(True)), 1)
        self.w.wall.now += 2 * DAY
        self.w.store.purge()
        self.assertEqual(self.w.store.reports(True), [])


class TheAdminsCannotRead(Base):
    def test_no_frame_command_or_screen_of_the_staff_holds_a_message_that_was_not_reported(self):
        h = self.handle(self.anna, "ben")
        self.dm(self.anna, h, "the words nobody else may read")
        for peer in (self.mod, self.cleo, self.guest):
            self.assertEqual([m for m in peer.sent if m["t"] in ("msg", "msgs", "thread", "alert", "alerts", "follows")], [])
            self.assertNotIn(b"nobody else may read", b"".join(peer.raw))
        self.frame(self.mod, t="dmopen", h=h)                                                   # (a moderator who has a handle of somebody else's gets nothing: it is not theirs)
        self.assertEqual(self.of(self.mod, "thread"), [])
        self.assertEqual(self.why(self.mod), ["who"])
        for line in ("/help", "/mutes", "/bans", "/ignore ben"):
            self.say(self.mod, line)
        self.assertNotIn(b"nobody else may read", b"".join(self.mod.raw))
        for show in (lambda out: adminmod.show(self.w.store, 500, out), lambda out: adminmod.show_log(self.w.store, 30, 1000, out), lambda out: adminmod.show_usage(self.w.store, 10 ** 8, out),
                     lambda out: adminmod.show_reports(self.w.store, True, out)):
            out = io.StringIO()
            show(out)
            self.assertNotIn("nobody else may read", out.getvalue())
        self.assertNotIn("nobody else may read", " ".join(l.text for l in self.w.store.recent_lines(100)))
        self.assertNotIn("nobody else may read", repr(self.w.store.actions()))


class TheForgetting(Base):
    def test_an_account_that_forgets_itself_takes_its_messages_for_the_other_people_of_them_as_well(self):
        h = self.handle(self.anna, "ben")
        self.dm(self.anna, h, "one")
        hb = self.handle(self.ben, "anna")
        self.dm(self.ben, hb, "two")
        hc_ = self.handle(self.cleo, "ben")
        self.dm(self.cleo, hc_, "from cleo")
        self.frame(self.ben, t="dmreport", h=hb, id=self.theirs(self.ben)[0]["id"], why="spam")
        self.w.store.forget(ANNA_ID)
        self.w.chat.messages.forget(ANNA_ID)
        self.assertEqual(self.w.store.dm_partners(self.mine("anna")), [])
        listing = self.of(self.ben, "msgs")[-1]
        self.assertEqual([r["name"] for r in listing["rows"]], ["cleo"])
        self.assertEqual(self.w.store.db.execute("SELECT COUNT(*) FROM dms").fetchone()[0], 1)
        report = self.w.store.reports()[0]
        self.assertEqual((report.reported, report.reported_name, report.reporter_name), (self.mine("anna"), "", "ben"))      # (a report stays, as a ban does, without the name of the person who left)

    def test_the_reporter_who_forgets_itself_is_not_in_the_report_any_more(self):
        h, hb = self.handle(self.anna, "ben"), self.handle(self.ben, "anna")
        self.dm(self.ben, hb, "rude")
        self.frame(self.anna, t="dmreport", h=h, id=self.theirs(self.anna)[0]["id"], why="abuse")
        self.w.store.forget(ANNA_ID)
        report = self.w.store.reports()[0]
        self.assertEqual((report.reporter, report.reporter_name, report.reported_name), ("", "", "ben"))
        self.assertEqual([text for _, _, text in report.messages], ["rude"])


def send_json(client, **fields):
    client.send(hc.frame(1, json.dumps(fields).encode("utf-8")))


class TheLiveHub(unittest.TestCase):
    def setUp(self):
        self.clients = []
        self.kept = {}
        self.addCleanup(lambda: [c.close() for c in self.clients])

    def connect(self, rig, ident):
        client = hc.Client(rig.port, login="%s:%s" % (ident.account_id, ident.username) if ident else None)
        self.clients.append(client)
        self.assertEqual(client.recv_json()["t"], "welcome")
        if ident:
            self.assertEqual(client.recv_json()["t"], "cmds")
        return client

    def next_of(self, client, kind, seconds=8.0):
        """The next frame of this kind; the frames of other kinds that came with it wait in `kept` for the next call (a frame is never thrown away unseen)."""
        kept = self.kept.setdefault(id(client), [])
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            for m in kept:
                if m["t"] == kind:
                    kept.remove(m)
                    return m
            kept.extend(client.drain(0.1))
        self.fail("no %s" % kind)

    def rest(self, client, seconds=0.5):
        """Every frame that comes in the next moment, with the ones that waited."""
        kept = self.kept.setdefault(id(client), [])
        kept.extend(client.drain(seconds))
        out, kept[:] = list(kept), []
        return out

    def test_a_message_reaches_the_person_alone_a_blocked_sender_is_answered_as_any_sender_and_the_log_holds_none_of_it(self):
        with hub_rig.Rig(roles=hub_rig.FakeRoles({})) as rig:
            anna, ben, cleo = (self.connect(rig, i) for i in (ANNA, BEN, CLEO))
            anna.say("/msg ben")
            h = self.next_of(anna, "reply")["h"]
            time.sleep(1.1)
            send_json(anna, t="dm", h=h, text="a live secret")
            echo = self.next_of(anna, "msg")
            got = self.next_of(ben, "msg")
            self.assertEqual((echo["text"], echo["me"], got["text"], got["me"], got["name"]), ("a live secret", 1, "a live secret", 0, "anna"))
            self.assertEqual(self.next_of(ben, "alert")["row"]["k"], "msg")
            self.assertEqual([m for m in self.rest(cleo, 0.4) if m["t"] in ("msg", "alert", "alerts", "msgs")], [])
            ben.say("/ignore anna")
            self.assertEqual(self.next_of(ben, "reply")["r"], "ok")
            self.rest(ben, 0.3)
            time.sleep(1.1)
            send_json(anna, t="dm", h=h, text="after the block")
            echo = self.next_of(anna, "msg")
            self.assertEqual((echo["text"], echo["me"]), ("after the block", 1))
            self.assertEqual([m for m in self.rest(ben, 0.6) if m["t"] in ("msg", "alert", "alerts")], [])
            log = rig.log_text.getvalue()
            for part in (ANNA_ID, BEN_ID, "a live secret", "after the block", h):
                self.assertNotIn(part, log, "the log holds %r" % part)


if __name__ == "__main__":
    unittest.main()
