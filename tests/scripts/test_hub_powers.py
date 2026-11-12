#!/usr/bin/env python3
"""The moderators' powers (hub/chat.py `allowed`, `command`; hub/roles.py `fresh`, `confirm`; hub/pseudo.py; the moderators' log in hub/store/; hub/admin.py `log`; docs/HUB.md "Moderation" and
"The moderators' log"; run by ./run_tests.sh --fast and by the CI). Every id, token and name here is made up.

  ThePowerTable          who may do what to whom: the function against a model written the other way round, and the chat's answer for every command, every role of the actor, every kind of target
  TheCommands            kick, ban, unban, bans, mute, unmute, del one by one: the answers, the closed sockets, the ended logins, the named line (and never for a refused command), case
  TheNamedLine           what the line says, how it is kept, what it never says
  TheFreshCheck          an answer older than a minute is asked again before an action, for both people; Discord slow, failing, refusing, or the socket gone: the action is refused and the chat goes on
  TheOfflineTarget       a person who is not here: by the profile's username, with the lookup made from the id that the database holds, and never logged
  TheLog                 the moderators' log: rows, references and no id, the command line that reads it, forgetting, the purge
  TheReference           the pseudonymous reference: the same for an account, different per hub, never the id
  TheIdIsLikeAPassword   a fake id is planted and looked for in every log line, frame, error, answer, row, representation and traceback, in every failure of Discord, the real process included
  TheLiveHub             real sockets and the real hub against the fake Discord: kick, ban, unban, bans, a slow or failing Discord, the log

tests/data/hub_powers_mutants.json breaks the rules one at a time for tools/mutate.py (every one must be caught).
"""
import asyncio
import dataclasses
import gc
import io
import itertools
import json
import logging
import os
import re
import sqlite3
import subprocess
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
import test_hub_server as ths  # noqa: E402
from test_hub_core import ALICE, BOB, MAPLE_A, FakePeer, World  # noqa: E402
from test_hub_roles import ADMIN, ENV, MOD, NOT_A_MEMBER, TOKEN, LiveTest, Log, Stub, good  # noqa: E402
from hub import admin, chat as chatmod, config, discord as discordmod, limits as limitmod, pseudo, roles as rolesmod, session, store as storemod  # noqa: E402

ID = "7311000099990001"                       # the made-up Discord id of a person who acts: like a password, it is nowhere that it need not be
TARGET_ID = "7311000099990002"                # ... and of one who is acted on
PLANTED = (ID, ID[:8], ID[-8:], TARGET_ID, TARGET_ID[:8], TARGET_ID[-8:])
OWNER = "1900000000000001"
ROLES = ("none", "mod", "admin")
COMMANDS = ("mute", "unmute", "del", "kick", "ban", "unban", "bans")

MODERATOR = session.Identity("9001", "moddy", False)
MODERATOR2 = session.Identity("9002", "other.mod", False)
ADMIN1 = session.Identity("9101", "boss.one", False)
ADMIN2 = session.Identity("9102", "boss.two", False)
OWNER_IDENTITY = session.Identity(OWNER, "the.owner", False)


def assert_no_id(test, text, where):
    for part in PLANTED:
        test.assertNotIn(part, text, "%s holds the id (%s)" % (where, part))


def allowed_by_the_rules(command, actor, target, same):
    """The table of the powers as the owner stated it, written the other way round from hub/chat.py: a list of what each role may do, and nothing else."""
    may = {"none": [], "mod": ["mute", "unmute", "del", "kick"], "admin": ["mute", "unmute", "del", "kick", "ban", "unban", "bans"]}
    if command not in may[actor]:
        return False
    if target is None:
        return True
    if same:
        return command == "del"                                           # (one's own line: no power over a person)
    if target == "admin":
        return False                                                      # (nobody acts on an administrator: not a moderator, and not another administrator)
    if target == "mod":
        return actor == "admin"
    return True


class ThePowerTable(unittest.TestCase):
    def test_the_function_is_the_table_for_every_command_actor_and_target(self):
        for command, actor, same in itertools.product(COMMANDS, ROLES, (False, True)):
            for target in (None,) + ROLES:
                if command == "bans" and target is not None:
                    continue
                if command != "bans" and target is None:
                    continue
                self.assertEqual(chatmod.allowed(command, actor, target, same), allowed_by_the_rules(command, actor, target, same), (command, actor, target, same))

    def test_the_table_says_what_the_owner_decided(self):
        a = chatmod.allowed
        self.assertTrue(a("kick", "mod", "none", False) and a("mute", "mod", "none", False) and a("unmute", "mod", "none", False) and a("del", "mod", "none", False))
        self.assertFalse(a("ban", "mod", "none", False) or a("unban", "mod", "none", False) or a("bans", "mod", None, False))          # (a moderator cannot ban or list bans)
        self.assertTrue(a("ban", "admin", "none", False) and a("ban", "admin", "mod", False) and a("unban", "admin", "none", False) and a("bans", "admin", None, False))
        self.assertFalse(a("kick", "mod", "mod", False) or a("kick", "mod", "admin", False) or a("kick", "admin", "admin", False))     # (no power over a moderator for a moderator, none over an administrator)
        self.assertFalse(a("ban", "admin", "admin", False))                                                                       # (admin against admin: refused)
        self.assertFalse(a("kick", "admin", "none", True) or a("ban", "admin", "admin", True))                                    # (nobody acts on themselves)
        self.assertTrue(a("del", "mod", "mod", True) and a("del", "admin", "admin", True))                                        # (but a line of one's own may go)
        self.assertEqual(sorted(chatmod.NEEDS), sorted(COMMANDS))
        self.assertEqual(chatmod.COMMANDS, frozenset("/" + c for c in COMMANDS))


class Powers(unittest.TestCase):
    """A chat with sockets that are plain fakes and roles that are a dictionary (hub_rig.FakeRoles): the answers of the chat."""

    def setUp(self):
        self.w = World(mods=())
        self.addCleanup(self.w.close)
        self.w.roles.roles.update({"9001": "mod", "9002": "mod", "9101": "admin", "9102": "admin", OWNER: "admin"})
        self.mod, self.mod2, self.admin, self.admin2 = (self.w.join(i) for i in (MODERATOR, MODERATOR2, ADMIN1, ADMIN2))
        self.alice, self.bob, self.reader = self.w.join(ALICE), self.w.join(BOB), self.w.join(None)

    def say(self, peer, text):
        self.w.say(peer, text)

    def rows(self):
        return self.w.store.actions()

    def system_lines(self, peer):
        return [m for m in peer.kinds("line") if m.get("sys")]


class ThePowersOfEveryRole(Powers):
    TARGETS = {"none": (BOB, "bob"), "mod": (MODERATOR2, "other.mod"), "admin": (ADMIN2, "boss.two")}

    def prepare(self, w, command, account, online):
        """The state that makes the command possible when the table lets it: a muted person to unmute, a banned one to unban, a line to delete."""
        if command == "unmute":
            w.store.mute(account, 60, "x", "x")
        elif command == "unban":
            w.store.ban(account, "x", "x")
        elif command == "del":
            w.store.add_line(account, "x", "a line of the target", False)

    def test_every_command_by_every_role_on_every_kind_of_person(self):
        for command, actor_role, target_role, kind in itertools.product(COMMANDS, ROLES, ROLES + ("self",), ("here", "away")):
            if command == "bans" and (target_role != "none" or kind != "here"):
                continue
            with self.subTest(command=command, actor=actor_role, target=target_role, kind=kind):
                w = World(mods=())
                self.addCleanup(w.close)
                w.roles.roles.update({"9001": "mod", "9101": "admin", "9002": "mod", "9102": "admin"})
                actor_id = {"none": ALICE, "mod": MODERATOR, "admin": ADMIN1}[actor_role]
                actor = w.join(actor_id)
                bystander = w.join(None)
                same = target_role == "self"
                ident = actor_id if same else self.TARGETS.get(target_role, self.TARGETS["none"])[0]
                shown_role = actor_role if same else target_role
                if command != "bans":
                    if not same:
                        w.store.set_profile(ident.account_id, ident.username, "", 100)
                        if kind == "here":
                            peer = w.join(ident)
                    if not same or command == "del":
                        self.prepare(w, command, ident.account_id, kind == "here")
                    text = "/%s %s" % (command, ident.username) if command not in ("del", "mute") else "/del 1" if command == "del" else "/mute %s 5" % ident.username
                else:
                    text = "/bans"
                before = len(w.store.actions())
                w.say(actor, text)
                codes, kinds = actor.codes(), [m["t"] for m in actor.sent if m["t"] in ("ok", "bans")]
                if actor_role == "none":
                    self.assertEqual(codes, ["bad"])                                              # (a command from an account with no role is refused and never posted)
                    self.assertEqual(bystander.kinds("line"), [])
                    self.assertEqual(len(w.store.actions()), before)
                    continue
                if command == "kick" and kind == "away" and not same:
                    self.assertEqual(codes, ["who"])                                              # (a person who is not here cannot be kicked)
                    continue
                should = allowed_by_the_rules(command, actor_role, None if command == "bans" else shown_role, same)
                if command == "unban" and same or command == "unmute" and same:
                    should = False
                if should:
                    self.assertEqual(codes, [], text)
                    self.assertTrue(kinds in (["ok"], ["bans"]), kinds)
                else:
                    self.assertEqual((codes, kinds), (["power"], []), text)
                    self.assertEqual(system(bystander), [])
                    self.assertEqual(len(w.store.actions()), before)
                if command in ("kick", "ban") and should:
                    self.assertEqual(len(system(bystander)), 1)
                else:
                    self.assertEqual(system(bystander), [])


def system(peer):
    return [m for m in peer.kinds("line") if m.get("sys")]


class TheCommands(Powers):
    def test_a_moderator_may_kick_mute_unmute_delete_and_may_not_ban_unban_or_list(self):
        self.say(self.bob, "hello")
        self.say(self.mod, "/mute bob 5")
        self.say(self.mod, "/unmute bob")
        self.say(self.mod, "/del 1")
        self.assertEqual(self.mod.codes(), [])
        self.assertEqual([m["c"] for m in self.mod.kinds("ok")], ["mute", "unmute", "del"])
        for command in ("/ban bob", "/unban bob", "/bans"):
            self.say(self.mod, command)
        self.assertEqual(self.mod.codes(), ["power"] * 3)
        self.assertFalse(self.w.store.is_banned("1002"))
        self.say(self.mod, "/kick bob")
        self.assertEqual(self.bob.closed, (1000, "kicked"))
        self.assertEqual([m["c"] for m in self.mod.kinds("ok")][-1], "kick")

    def test_an_administrator_may_ban_unban_list_and_act_on_a_moderator(self):
        self.say(self.admin, "/ban other.mod")
        self.assertEqual(self.mod2.closed, (1000, "banned"))
        self.assertTrue(self.w.store.is_banned("9002"))
        self.say(self.admin, "/bans")
        (frame,) = self.admin.kinds("bans")
        self.assertEqual(frame["rows"], [{"u": "other.mod", "at": int(self.w.wall())}])
        self.say(self.admin, "/unban other.mod")
        self.assertFalse(self.w.store.is_banned("9002"))
        self.say(self.admin, "/mute other.mod 7")
        self.assertEqual(self.admin.codes(), [])

    def test_nobody_acts_on_an_administrator_on_the_owner_or_on_themselves_and_admin_against_admin_is_refused(self):
        for actor, text in ((self.mod, "/kick boss.one"), (self.mod, "/mute boss.one 5"), (self.mod, "/kick other.mod"), (self.mod, "/mute moddy 5"), (self.admin, "/kick boss.two"),
                            (self.admin, "/ban boss.two"), (self.admin, "/ban boss.one"), (self.admin, "/mute boss.two 5"), (self.admin, "/kick boss.one")):
            before = len(actor.codes())
            self.say(actor, text)
            self.assertEqual(actor.codes()[before:], ["power"], text)
        owner = self.w.join(OWNER_IDENTITY)
        self.w.store.set_profile(OWNER, "the.owner", "", 100)
        for actor, text in ((self.admin, "/ban the.owner"), (self.admin, "/kick the.owner"), (self.mod, "/mute the.owner 5")):
            before = len(actor.codes())
            self.say(actor, text)
            self.assertEqual(actor.codes()[before:], ["power"], text)
        self.say(owner, "/ban boss.one")                                                       # (even the owner account, an administrator, has no power over another)
        self.assertEqual(owner.codes(), ["power"])
        self.assertEqual((self.w.store.bans(), self.w.store.mutes(), self.rows()), ([], [], []))
        self.assertTrue(all(p.closed is None for p in (self.mod, self.mod2, self.admin, self.admin2, owner)))

    def test_the_words_are_read_in_any_case_with_single_spaces(self):
        self.say(self.admin, "/BAN bob")
        self.say(self.admin, "/Bans")
        self.say(self.admin, "/UnBan bob")
        self.say(self.admin, "/Mute bob 5")
        self.say(self.admin, "/UNMUTE bob")
        self.say(self.admin, "/KICK alice")
        self.assertEqual(self.admin.codes(), [])
        self.assertEqual([m["c"] for m in self.admin.kinds("ok")], ["ban", "unban", "mute", "unmute", "kick"])
        for text in ("/kick", "/kick a b", "/ban", "/bans x", "/unban", "/unmute", "/unmute a b", "/mute bob", "/del", "/del 1 2", "/", "/frobnicate bob"):
            before = len(self.admin.codes())
            self.say(self.admin, text)
            self.assertEqual(self.admin.codes()[before:], ["bad"], text)

    def test_an_account_with_no_role_gets_bad_for_every_command_word_in_any_case_and_nothing_is_posted(self):
        for word in ("/kick", "/KICK", "/Ban", "/unban", "/bans", "/Unmute", "/mute", "/del", "/Del"):
            self.w.clock.now += 3
            self.say(self.alice, word + " bob")
        self.assertEqual(self.alice.codes(), ["bad"] * 9)
        self.assertEqual(self.reader.kinds("line"), [])
        self.assertEqual(self.w.store.recent_lines(10), [])

    def test_an_unknown_name_and_a_name_of_two_are_who_and_a_name_that_is_shown_is_none(self):
        for text in ("/kick nobody", "/ban nobody", "/unban nobody", "/mute nobody 5", "/unmute nobody", "/kick Bob", "/kick bob.smith", "/kick 1002"):
            before = len(self.admin.codes())
            self.say(self.admin, text)
            self.assertEqual(self.admin.codes()[before:], ["who"], text)
        first, second = self.w.join(session.Identity("1010", "sam", False)), self.w.join(session.Identity("1011", "sam", False))
        self.say(self.admin, "/ban sam")
        self.assertEqual(self.admin.codes()[-1], "who")
        self.assertEqual(self.w.store.bans(), [])
        self.assertEqual(self.rows(), [])

    def test_a_ban_that_is_there_and_an_unban_that_is_not_are_bad_and_add_no_line_and_no_row(self):
        self.say(self.admin, "/ban bob")
        lines, rows = len(self.system_lines(self.reader)), len(self.rows())
        self.w.join(BOB)
        self.say(self.admin, "/ban bob")
        self.say(self.admin, "/unban alice")
        self.say(self.admin, "/unmute bob")
        self.assertEqual(self.admin.codes(), ["bad", "bad", "bad"])
        self.assertEqual((len(self.system_lines(self.reader)), len(self.rows())), (lines, rows))

    def test_kick_closes_every_socket_of_the_person_tells_them_and_ends_every_login(self):
        second = self.w.join(BOB)
        self.w.store.add_session("k1", "1002", "bob", 3600, 10, 100)
        self.w.store.add_session("k2", "1002", "bob", 3600, 10, 100)
        self.w.store.add_session("k3", "1001", "alice", 3600, 10, 100)
        self.say(self.mod, "/kick bob")
        for peer in (self.bob, second):
            self.assertEqual(peer.closed, (1000, "kicked"))
            self.assertEqual(peer.kinds("bye"), [{"t": "bye", "why": "kicked"}])
            self.assertEqual(peer.sent[-2:-1][0]["t"], "bye")                                   # (the notice goes before the close)
        self.assertEqual((self.w.store.session("k1"), self.w.store.session("k2")), (None, None))
        self.assertIsNotNone(self.w.store.session("k3"))
        self.assertEqual([p.closed for p in (self.alice, self.reader, self.admin)], [None] * 3)
        self.assertFalse(self.w.store.is_banned("1002"))
        self.assertEqual(self.alice.kinds("bye"), [])                                           # (nobody else is told)

    def test_a_kicked_person_may_log_in_again_at_once_and_write_at_once(self):
        self.say(self.mod, "/kick bob")
        self.assertEqual(self.w.store.muted_until("1002"), 0)
        self.w.store.add_session("k9", "1002", "bob", 3600, 10, 100)                            # (a new login: nothing refuses it)
        again = self.w.join(BOB)
        self.w.clock.now += 3
        self.say(again, "i am back")
        self.assertEqual((again.codes(), [m["text"] for m in self.reader.kinds("line") if not m.get("sys")]), ([], ["i am back"]))

    def test_ban_closes_the_sockets_keeps_the_login_and_the_person_can_still_read(self):
        second = self.w.join(BOB)
        self.w.store.add_session("k1", "1002", "bob", 3600, 10, 100)
        self.say(self.admin, "/ban bob")
        for peer in (self.bob, second):
            self.assertEqual((peer.closed, peer.kinds("bye")), ((1000, "banned"), [{"t": "bye", "why": "banned"}]))
        self.assertTrue(self.w.store.is_banned("1002"))
        self.assertIsNotNone(self.w.store.session("k1"))                                        # (a ban stops writing, not reading: the login stays)
        again = self.w.join(BOB)
        self.say(again, "hello")
        self.assertEqual(again.codes(), ["banned"])
        self.assertEqual(self.w.store.bans()[0][1], "bob")

    def test_bans_lists_the_newest_fifty_by_username_and_time_and_nothing_else_and_to_nobody_else(self):
        for n in range(60):
            self.w.wall.now += 1
            self.w.store.ban("2%03d" % n, "x", "user%d" % n)
        self.say(self.admin, "/bans")
        (frame,) = self.admin.kinds("bans")
        self.assertEqual(len(frame["rows"]), 50)
        self.assertEqual(frame["rows"][0]["u"], "user59")
        self.assertEqual(frame["rows"][-1]["u"], "user10")
        self.assertEqual(set(frame), {"t", "rows"})
        self.assertTrue(all(set(r) == {"u", "at"} for r in frame["rows"]))
        for peer in (self.alice, self.mod, self.reader, self.admin2):
            self.assertEqual(peer.kinds("bans"), [])
        self.assertNotIn(b"2059", b"".join(self.admin.raw))                                     # (no account id)

    def test_an_unban_finds_the_person_by_the_name_the_ban_was_made_with_when_the_profile_is_gone(self):
        self.say(self.admin, "/ban bob")
        self.assertIsNone(self.w.store.profile("1002"))
        self.w.chat.peers.discard(self.bob)
        self.say(self.admin, "/unban bob")
        self.assertEqual((self.admin.codes(), self.w.store.bans()), ([], []))

    def test_mute_and_unmute(self):
        self.say(self.mod, "/mute bob 5")
        self.assertGreater(self.w.store.muted_until("1002"), 0)
        self.assertEqual(self.w.store.mutes()[0][1], "bob")
        self.say(self.bob, "hi")
        self.assertEqual(self.bob.codes(), ["muted"])
        self.say(self.mod, "/unmute bob")
        self.assertEqual(self.w.store.muted_until("1002"), 0)
        self.say(self.mod, "/unmute bob")
        self.assertEqual(self.mod.codes(), ["bad"])                                             # (not muted: nothing done, and no row)
        self.assertEqual([r.action for r in self.rows()], ["unmute", "mute"])

    def test_del_takes_a_line_off_and_not_a_line_of_a_person_with_a_role_or_of_the_hub(self):
        self.say(self.bob, "from bob")
        self.w.clock.now += 3
        self.say(self.mod2, "from the other moderator")
        self.say(self.admin, "/kick alice")
        self.say(self.mod, "/del 1")
        self.assertEqual(self.reader.kinds("del"), [{"t": "del", "id": 1}])
        self.say(self.mod, "/del 2")
        self.say(self.mod, "/del 3")                                                            # (the hub's own line of the kick)
        self.assertEqual(self.mod.codes(), ["power", "power"])
        self.say(self.admin, "/del 2")
        self.assertEqual(self.admin.codes(), [])                                                # (an administrator may delete a moderator's line)
        self.say(self.mod, "/del 99")
        self.assertEqual(self.mod.codes()[-1], "bad")
        self.assertEqual([r.action for r in self.rows()], ["del", "del", "kick"])

    def test_a_moderator_may_delete_a_line_of_their_own_and_it_is_no_action_of_the_log(self):
        self.say(self.mod, "hello all")
        self.say(self.mod, "/del 1")
        self.assertEqual((self.mod.codes(), self.rows()), ([], []))
        self.assertEqual(self.w.store.account_of_line(1), None)
        self.say(self.admin, "/kick alice")
        self.say(self.alice, "x")
        self.say(self.mod, "/del 1")
        self.assertEqual(self.mod.codes(), ["bad"])                                             # (it is gone already)

    def test_a_command_of_a_muted_or_banned_moderator_is_refused_before_anything(self):
        self.w.store.mute("9001", 5, "x", "x")
        self.say(self.mod, "/kick bob")
        self.assertEqual(self.mod.codes(), ["muted"])
        self.assertIsNone(self.bob.closed)
        self.w.store.ban("9002", "x", "x")
        self.say(self.mod2, "/kick bob")
        self.assertEqual(self.mod2.codes(), ["banned"])
        self.assertIsNone(self.bob.closed)

    def test_a_refused_command_counts_as_a_mistake_of_the_socket(self):
        self.say(self.mod, "/ban bob")
        self.say(self.mod, "/kick nobody")
        self.assertEqual((self.mod.strikes, self.mod.charges), (2, 0))

    def test_a_demoted_account_whose_cached_role_ran_out_is_told_role_and_nothing_is_done(self):
        original = self.w.chat.is_mod
        self.w.chat.is_mod = lambda account: True                                               # (the cached role said mod when the command came in: the answer that is fresh says none)
        self.w.roles.roles["9001"] = "none"
        self.say(self.mod, "/mute bob 5")
        self.assertEqual((self.mod.codes(), self.w.store.mutes()), (["role"], []))
        self.w.chat.is_mod = original

    def test_no_command_is_a_line_of_chat_and_a_good_one_answers_ok_to_the_sender_only(self):
        self.say(self.admin, "/mute bob 5")
        self.assertEqual([m["t"] for m in self.admin.sent if m["t"] in ("ok", "line")], ["ok"])
        for peer in (self.bob, self.alice, self.reader, self.mod):
            self.assertEqual(peer.kinds("ok"), [])
            self.assertEqual(peer.kinds("line"), [])


class TheNamedLine(Powers):
    def test_a_kick_and_a_ban_say_the_name_to_everybody_and_not_the_moderator(self):
        self.say(self.mod, "/kick bob")
        self.say(self.admin, "/ban alice")
        for peer in (self.reader, self.alice, self.admin2, self.mod, self.admin):
            lines = system(peer)
            self.assertEqual([(m["who"], m["text"], m["mod"], m["k"], m["sys"]) for m in lines], [("system", "bob was kicked.", False, "", True), ("system", "alice was banned.", False, "", True)])
            self.assertEqual(set(lines[0]), {"t", "id", "at", "who", "text", "mod", "k", "sys"})
            for name in ("moddy", "boss.one"):
                self.assertNotIn(name.encode(), b"".join(peer.raw))                              # (the moderator is not named)

    def test_it_is_the_name_that_is_shown_and_never_the_username_of_an_account_that_shows_another(self):
        maple = self.w.join(MAPLE_A)
        self.say(self.mod, "/kick aaa.one")
        self.assertEqual(system(self.reader)[0]["text"], "Maple Leaf was kicked.")
        self.assertNotIn(b"aaa.one", b"".join(self.reader.raw))

    def test_for_an_account_that_is_away_it_is_its_profile_s_name(self):
        self.w.store.set_profile("1201", "carl.c", "Carl C", 100)
        self.w.store.set_profile("1202", "dora.d", "Dora D", 100)
        self.w.store.set_pick("1202", "username")
        self.say(self.admin, "/ban carl.c")
        self.say(self.admin, "/ban dora.d")
        self.assertEqual([m["text"] for m in system(self.reader)], ["Carl C was banned.", "dora.d was banned."])

    def test_it_is_kept_like_any_other_line_in_the_history_the_store_and_the_ring(self):
        self.say(self.mod, "/kick bob")
        newcomer = self.w.join(None)
        history = self.w.chat.welcome(newcomer)["history"]
        self.assertEqual([(h["who"], h["text"], h.get("sys"), h["k"]) for h in history], [("system", "bob was kicked.", True, "")])
        self.assertEqual([(l.account, l.name, l.text, l.mod) for l in self.w.store.recent_lines(10)], [("", "system", "bob was kicked.", 0)])
        self.assertEqual(len(self.w.chat.ring), 1)
        self.w.wall.now += 7 * 86400 + 5
        self.assertEqual(self.w.chat.welcome(newcomer)["history"], [])                          # (the same seven days as every line)
        self.assertEqual(self.w.store.purge(), 1)

    def test_it_counts_among_the_newest_lines_of_the_store(self):
        self.w.store.max_lines = 3
        for n in range(3):
            self.w.clock.now += 3
            self.say(self.alice, "line %d" % n)
        self.say(self.mod, "/kick bob")
        self.assertEqual([l.text for l in self.w.store.recent_lines(10)], ["line 1", "line 2", "bob was kicked."])

    def test_a_moderator_who_kicks_and_bans_over_and_over_does_all_of_it_and_has_only_a_few_lines(self):
        limits = limitmod.Limits()                                                                   # (the default numbers, read from where they are set: the chat of this class has them)
        self.assertEqual((limits.named_lines_burst, limits.named_lines_refill_seconds), (5, 10.0))
        for n in range(30):
            self.w.join(BOB)
            self.say(self.admin, "/ban bob")
            self.say(self.admin, "/unban bob")
        self.assertEqual(self.admin.codes(), [])                                                     # (every ban and every unban was done)
        self.assertEqual([m["c"] for m in self.admin.kinds("ok")].count("ban"), 30)
        self.assertEqual(len(self.rows()), 60)                                                       # (and has its row in the log)
        self.assertEqual(len(system(self.reader)), limits.named_lines_burst)                         # (the lines for everybody are the budget)
        self.assertEqual(len(self.w.store.recent_lines(200)), limits.named_lines_burst)
        self.say(self.mod, "/kick alice")                                                            # (another moderator has its own budget)
        self.assertEqual(len(system(self.reader)), limits.named_lines_burst + 1)
        self.w.wall.now += 100
        self.w.clock.now += 100
        self.w.join(BOB)
        self.say(self.admin, "/ban bob")                                                             # (and the budget comes back with time)
        self.assertEqual(len(system(self.reader)), limits.named_lines_burst + 2)

    def test_a_ban_that_is_refused_or_a_duplicate_spends_nothing_of_the_budget(self):
        self.say(self.admin, "/ban bob")
        for n in range(20):
            self.say(self.admin, "/ban bob")                                                         # (banned already: `bad`)
            self.say(self.admin, "/ban boss.two")                                                    # (an administrator: `power`)
            self.say(self.admin, "/ban nobody")                                                      # (nobody: `who`)
        for name in ("alice", "moddy", "other.mod"):
            self.say(self.admin, "/ban " + name)
        self.assertEqual(len(system(self.reader)), 4)                                                # (all four bans have their line: nothing was spent by the refused ones)

    def test_a_refused_command_makes_no_line_at_all(self):
        for text in ("/kick nobody", "/ban boss.two", "/kick boss.one", "/ban bob extra", "/kick boss.two"):
            self.say(self.admin, text)
        self.say(self.mod, "/ban bob")
        self.say(self.mod, "/kick other.mod")
        self.say(self.alice, "/kick bob")
        self.assertEqual((system(self.reader), self.w.store.recent_lines(10)), ([], []))

    def test_the_line_of_the_hub_is_not_a_line_of_an_account_that_a_forget_or_a_mute_could_reach(self):
        self.say(self.mod, "/kick bob")
        self.assertEqual(self.w.store.forget("1002"), [])
        self.assertEqual(len(self.w.store.recent_lines(10)), 1)
        self.assertEqual(self.w.store.last_name(""), "system")
        self.assertFalse(config.ACCOUNT_ID.match(""))                                           # (no account has the empty id)

    def test_the_longest_name_is_not_cut_and_the_line_is_far_under_the_limit(self):
        long = session.Identity("1301", "u" * 32, False)
        mod, victim = self.mod, self.w.join(long)
        self.say(mod, "/kick " + "u" * 32)
        self.assertEqual([l.text for l in self.w.store.recent_lines(10)], ["u" * 32 + " was kicked."])
        self.assertLess(len(self.w.store.recent_lines(10)[0].text), chatmod.textrules.MAX_CODE_POINTS)
        self.assertNotRegex(self.w.store.recent_lines(10)[0].text, "[<>]")


class FreshBase(unittest.IsolatedAsyncioTestCase):
    """The real Roles against a stub of Discord that the test controls, on a clock that the test moves, with real tasks: the worker, the urgent lookups, the commands that wait."""

    OPTIONS = dict(roles_refill_seconds=0.001, roles_jitter=0.0, roles_urgent_seconds=0.6, discord_seconds=1.0, roles_urgent_actor_burst=10 ** 6, named_lines_burst=10 ** 6)       # (the budgets of one moderator are out of the way, but in the tests of them)

    async def asyncSetUp(self):
        self.skew = 0.0
        self.members = {"9001": [MOD], "9101": [ADMIN], "1002": [], "1001": []}
        self.delay = 0.0
        self.mode = None
        self.log = Log()
        self.w = World(mods=())
        self.addCleanup(self.w.close)
        self.limits = dataclasses.replace(limitmod.Limits(), **self.OPTIONS)
        self.clock = lambda: time.monotonic() + self.skew
        self.stub = Slow(self)
        cfg = config.load(dict(ENV, ANTS_HUB_OWNER=OWNER))
        self.roles = rolesmod.Roles(cfg, self.limits, self.clock, self.log, client=self.stub, rng=lambda: 0.5)
        self.w.chat.roles = self.roles
        self.roles.connected, self.roles.changed = self.w.chat.accounts, self.w.chat.role_changed
        self.w.chat.wait_limits = limitmod.RateByAccount(self.limits.roles_urgent_actor_burst, self.limits.roles_urgent_actor_refill_seconds)      # (the budgets of one moderator, as this test's limits have them)
        self.w.chat.named_limits = limitmod.RateByAccount(self.limits.named_lines_burst, self.limits.named_lines_refill_seconds)
        self.roles.start()
        self.addCleanup(self.roles.stop)
        self.mod, self.admin, self.alice, self.bob = (self.w.join(i) for i in (MODERATOR, ADMIN1, ALICE, BOB))
        for peer in (self.mod, self.admin, self.alice, self.bob):
            self.w.chat.welcome(peer)
        await self.until(lambda: self.roles.fresh(["9001", "9101", "1001", "1002"]))
        self.stub.calls.clear()

    async def until(self, what, seconds=5.0):
        end = time.monotonic() + seconds
        while not what() and time.monotonic() < end:
            await asyncio.sleep(0.005)
        self.assertTrue(what())

    async def settle(self):
        while self.w.chat.busy:
            await asyncio.gather(*list(self.w.chat.busy.values()), return_exceptions=True)
            await asyncio.sleep(0)

    def age(self, seconds=61.0):
        self.skew += seconds

    def grant(self, account, role):
        """Discord has just said that `account` holds `role` (as after a check), without a call: the state that a test of a failure starts from."""
        entry = self.roles.entries[account]
        entry.role, entry.ok_at, entry.fails = role, self.clock(), 0
        self.roles.settle(account, entry, self.clock())

    def asked(self):
        return sorted(a for _, a in self.stub.calls)


class TheFreshCheck(FreshBase):
    async def test_answers_that_are_young_need_no_lookup_and_the_command_is_done_at_once(self):
        self.age(59.0)
        self.w.say(self.mod, "/mute bob 5")
        self.assertEqual((self.stub.calls, self.mod.codes(), self.w.chat.busy), ([], [], {}))      # (done before this line: no task, no call)
        self.assertGreater(self.w.store.muted_until("1002"), 0)

    async def test_an_answer_older_than_a_minute_about_the_actor_makes_a_lookup_of_both_before_the_action(self):
        self.age()
        self.w.say(self.mod, "/mute bob 5")
        self.assertEqual(self.w.store.muted_until("1002"), 0)                                      # (not yet: it waits for the lookups)
        await self.settle()
        self.assertEqual(self.asked(), ["1002", "9001"])
        self.assertGreater(self.w.store.muted_until("1002"), 0)
        self.assertEqual([m["c"] for m in self.mod.kinds("ok")], ["mute"])

    async def test_an_answer_older_than_a_minute_about_the_target_alone_does_the_same(self):
        self.age(40.0)
        self.w.say(self.alice, "hi")                                                               # (a line is no action: nothing is asked)
        await self.until(lambda: True)
        self.roles.entries["1002"].ok_at -= 30                                                     # (only the target's answer is old: 70 s, the actor's 40 s)
        self.w.say(self.mod, "/kick bob")
        await self.settle()
        self.assertEqual(self.asked(), ["1002", "9001"])
        self.assertEqual(self.bob.closed, (1000, "kicked"))

    async def test_the_owner_account_needs_no_lookup_and_its_target_gets_one_alone(self):
        owner = self.w.join(OWNER_IDENTITY)
        self.w.chat.welcome(owner)
        self.age()
        self.w.say(owner, "/mute bob 5")
        await self.settle()
        self.assertEqual(self.asked(), ["1002"])
        self.assertGreater(self.w.store.muted_until("1002"), 0)

    async def test_the_lookup_says_what_the_person_is_now(self):
        self.age()
        self.members["1002"] = [ADMIN]                                                             # (promoted since the answer that the pages know)
        self.w.say(self.mod, "/mute bob 5")
        await self.settle()
        self.assertEqual((self.mod.codes(), self.w.store.mutes()), (["power"], []))
        self.age()
        self.members["9001"] = []                                                                  # (the moderator was demoted)
        self.members["1002"] = []
        self.w.say(self.mod, "/mute bob 5")
        await self.settle()
        self.assertEqual((self.mod.codes()[-1], self.w.store.mutes(), self.mod.charges), ("role", [], 1))
        self.assertEqual(self.roles.role_of("9001"), "none")

    async def test_a_discord_that_answers_slowly_but_in_time_is_waited_for(self):
        self.age()
        self.delay = 0.2
        self.w.say(self.admin, "/ban bob")
        self.assertEqual((self.w.store.bans(), self.admin.codes(), self.admin.kinds("ok")), ([], [], []))        # (it waits)
        await self.settle()
        self.assertEqual((self.admin.codes(), self.w.store.is_banned("1002"), [m["c"] for m in self.admin.kinds("ok")]), ([], True, ["ban"]))
        self.age()
        self.w.say(self.mod, "/ban alice")
        await self.settle()
        self.assertEqual((self.mod.codes(), self.w.store.is_banned("1001")), (["power"], False))           # (a moderator may not ban: the lookups were made first, and the answer is power)

    async def test_a_discord_that_is_too_slow_refuses_the_action_with_role_and_the_chat_goes_on(self):
        self.age()
        self.delay = 3.0
        self.w.say(self.mod, "/kick bob")
        await self.settle()
        self.assertEqual((self.mod.codes(), self.bob.closed, self.w.store.recent_lines(10)), (["role"], None, []))
        self.delay = 0.0
        self.w.clock.now += 3
        self.w.say(self.alice, "the chat goes on")
        self.assertEqual([m["text"] for m in self.bob.kinds("line")], ["the chat goes on"])

    async def test_every_failure_of_discord_refuses_the_action_and_none_is_taken_for_an_answer(self):
        failures = {"500": (500, {"message": "x"}, {}), "garbage": (200, {"roles": "x"}, {}), "401": (401, {"code": 0}, {}), "403": (403, {"code": 50001}, {}), "10004": (404, {"code": 10004}, {}),
                    "429": (429, {"retry_after": 1.0}, {"retry_after": 1.0}), "wrong user": (200, {"user": {"id": "1"}, "roles": []}, {}),
                    "down": discordmod.DiscordError("down", None), "bug": RuntimeError("canary " + ID), "too many roles": (200, {"roles": [MOD] * 300}, {})}
        for name, outcome in failures.items():
            with self.subTest(name):
                self.roles.pause_until = self.roles.hold_until = float("-inf")
                self.mode = None
                self.grant("9001", "mod")
                self.grant("1002", "none")
                self.age()
                self.mode = lambda account, o=outcome: o
                before = len(self.mod.codes())
                self.w.say(self.mod, "/kick bob")
                await self.settle()
                self.assertEqual(self.mod.codes()[before:], ["role"], name)
                self.assertIsNone(self.bob.closed)
                self.assertEqual(self.w.store.actions(), [])
        self.mode = None

    async def test_a_refusal_of_the_token_is_told_at_once_and_the_owner_still_has_the_power(self):
        self.mode = lambda account: (401, {"code": 0}, {})
        self.age()
        self.w.say(self.mod, "/kick bob")
        await self.settle()
        self.assertEqual(self.mod.codes(), ["role"])
        self.mode = None
        self.grant("9001", "mod")                                                                  # (the moderator holds the role again, with an old answer, and the roles are still paused)
        self.roles.entries["9001"].ok_at -= 100
        started = time.monotonic()
        self.w.say(self.mod, "/kick bob")
        await self.settle()
        self.assertLess(time.monotonic() - started, 0.3)                                           # (the roles are paused: no waiting for an answer that cannot come)
        owner = self.w.join(OWNER_IDENTITY)
        self.w.say(owner, "/mute bob 5")
        await self.settle()
        self.assertEqual((owner.codes(), self.w.store.muted_until("1002")), (["role"], 0))         # (the owner needs no answer about itself, but the person acted on must have one: fail closed; the command line is the fallback)
        self.grant("1002", "none")                                                                 # (an answer of a moment ago about the person is enough)
        self.w.say(owner, "/mute bob 5")
        self.assertEqual(owner.codes(), ["role"])
        self.assertGreater(self.w.store.muted_until("1002"), 0)

    async def test_a_second_command_of_a_socket_that_waits_is_rate_and_other_sockets_are_not_held(self):
        self.age()
        self.delay = 0.1
        self.w.say(self.mod, "/mute bob 5")
        self.w.say(self.mod, "/mute alice 5")
        self.assertEqual((self.mod.codes(), self.mod.charges), (["rate"], 1))
        self.w.say(self.admin, "/mute alice 7")
        await self.settle()
        self.assertEqual(sorted(m[0] for m in self.w.store.mutes()), ["1001", "1002"])

    async def test_a_socket_that_went_while_its_command_waited_gets_nothing_and_nothing_is_done(self):
        self.age()
        self.delay = 0.2
        self.w.say(self.mod, "/kick bob")
        self.mod.closed = (1001, "gone")
        await self.settle()
        self.assertEqual((self.bob.closed, self.mod.kinds("ok"), self.mod.codes(), self.w.store.actions()), (None, [], [], []))

    async def test_a_moderator_who_was_muted_or_banned_while_the_command_waited_is_refused(self):
        self.age()
        self.delay = 0.2
        self.w.say(self.mod, "/kick bob")
        self.w.store.mute("9001", 5, "x", "x")
        await self.settle()
        self.assertEqual((self.mod.codes(), self.bob.closed), (["muted"], None))

    async def test_a_moderator_who_was_banned_while_the_command_waited_is_refused(self):
        self.age()
        self.delay = 0.2
        self.w.say(self.mod, "/kick bob")
        self.w.store.ban("9001", "x", "x")
        await self.settle()
        self.assertEqual((self.mod.codes(), self.bob.closed), (["banned"], None))

    async def test_a_refusal_that_comes_while_actions_wait_ends_their_wait_at_once(self):
        self.age()
        self.delay = 0.1
        self.mode = lambda account: (401, {"code": 0}, {})
        started = time.monotonic()
        self.w.say(self.mod, "/kick bob")
        self.w.say(self.admin, "/kick alice")
        await self.settle()
        self.assertLess(time.monotonic() - started, 0.45)                                           # (the wait of an action is 0.6 s: it did not run out)
        self.assertEqual(self.mod.codes(), ["role"])
        self.assertEqual(self.admin.codes(), ["role"])

    async def test_a_target_that_left_while_the_command_waited_is_who(self):
        self.age()
        self.delay = 0.2
        self.w.say(self.mod, "/kick bob")
        self.w.chat.peers.discard(self.bob)
        await self.settle()
        self.assertEqual((self.mod.codes(), self.w.store.actions()), (["who"], []))

    async def test_the_lookups_of_commands_are_one_at_a_time_through_the_bucket_and_shared_between_commands(self):
        self.age()
        self.delay = 0.1
        for peer, text in ((self.mod, "/mute bob 5"), (self.admin, "/mute bob 6")):
            self.w.say(peer, text)
        await self.settle()
        calls = self.asked()
        self.assertEqual(sorted(set(calls)), ["1002", "9001", "9101"])
        self.assertLessEqual(calls.count("1002"), 2)

    async def test_the_numbers_are_the_ones_the_document_gives(self):
        d = limitmod.Limits()
        self.assertEqual((d.roles_action_fresh_seconds, d.roles_urgent_seconds, d.roles_urgent_burst, d.roles_urgent_refill_seconds), (60.0, 3.0, 10, 1.0))
        self.assertEqual((d.roles_urgent_actor_burst, d.roles_urgent_actor_refill_seconds, d.named_lines_burst, d.named_lines_refill_seconds), (3, 5.0, 5, 10.0))

    async def test_a_moderator_who_asks_for_many_lookups_gets_their_share_and_is_then_refused_and_the_people_s_own_checks_go_on(self):
        for n in range(40):
            self.w.store.set_profile("20%02d" % n, "user.%d" % n, "", 100)
            self.members["20%02d" % n] = []
        self.age()                                                                                   # (the answers about the moderator and the others are old: every command needs lookups)
        for n in range(40):
            self.w.say(self.mod, "/mute user.%d 5" % n)
            await self.settle()
        asked = len(self.stub.calls)
        refused = self.mod.codes().count("role")
        self.assertLessEqual(asked, self.limits.roles_urgent_burst + 4)                              # (a burst of ten, and what the second or two of this test earned)
        self.assertGreater(refused, 20)
        self.assertEqual(len(self.w.store.mutes()) + refused, 40)
        self.stub.calls.clear()                                                                      # (and the lookups of the people's own roles are not held back by it)
        self.roles.entries["1001"].due = self.clock() - 1
        self.roles.entries["1001"].new = True
        self.roles.wake.set()
        await self.until(lambda: any(a == "1001" for _, a in self.stub.calls))

    async def test_one_moderator_on_many_sockets_has_a_share_of_their_own_and_the_others_keep_theirs(self):
        real = limitmod.Limits()                                                                     # (the default numbers, read from where they are set)
        self.w.chat.wait_limits = limitmod.RateByAccount(real.roles_urgent_actor_burst, real.roles_urgent_actor_refill_seconds)
        for n in range(12):
            self.w.store.set_profile("20%02d" % n, "user.%d" % n, "", 100)
            self.members["20%02d" % n] = []
        sockets = [self.w.join(MODERATOR) for _ in range(9)]
        self.age()
        for n, peer in enumerate(sockets[:10]):
            self.w.say(peer, "/mute user.%d 5" % n)
            await self.settle()
        codes = [c for p in sockets for c in p.codes()]
        self.assertEqual(codes.count("rate"), len(sockets) - 3)                                      # (`rate`: too fast; `role` is the answer of a role that could not be confirmed)
        self.assertEqual(len(self.w.store.mutes()), 3)                                           # (three commands waited for lookups; the others were refused at the door)
        self.assertEqual(sum(p.strikes for p in sockets), len(sockets) - 3)                          # (and a refusal of the share is a mistake of the socket)
        asked_before = len(self.stub.calls)
        self.w.say(self.admin, "/mute user.11 5")                                                    # (another moderator: its own share is whole, and so is the hub's)
        await self.settle()
        self.assertEqual((self.admin.codes(), len(self.stub.calls) > asked_before), ([], True))
        self.assertGreater(self.w.store.muted_until("2011"), 0)

    async def test_a_command_that_the_share_refuses_makes_no_entry_and_evicts_nobody(self):
        self.members["1010"] = []
        self.w.store.set_profile("1010", "dora.d", "", 100)
        self.roles.limits = dataclasses.replace(self.roles.limits, roles_entries_max=len(self.roles.entries) + 3)
        for n in range(3):
            self.roles.entries["3%03d" % n] = rolesmod.Entry(self.clock())                           # (people who left, and whose entries fill the table to its cap)
            self.roles.entries["3%03d" % n].seen = self.clock() - 5 - n
        before = set(self.roles.entries)
        self.roles.urgent_bucket.tokens = 0.0
        self.roles.urgent_bucket.at = self.clock()
        for account in ("9101", "1002"):
            self.roles.entries[account].ok_at = self.clock() - 100.0
        self.w.say(self.admin, "/mute dora.d 5")
        await self.settle()
        self.assertEqual(self.admin.codes(), ["role"])
        self.assertEqual(set(self.roles.entries), before)                                            # (no entry for the person, and none of those that left was evicted for it)

    async def test_the_share_of_the_actions_comes_back_with_time(self):
        def stale():
            for account in ("9101", "1002"):
                self.roles.entries[account].ok_at = self.clock() - 100.0                              # (old answers, without moving the clock that the share is counted by)
        self.roles.urgent_bucket.tokens = 0.0
        self.roles.urgent_bucket.at = self.clock()
        stale()
        self.w.say(self.admin, "/mute bob 5")
        await self.settle()
        self.assertEqual((self.admin.codes(), self.w.store.mutes()), (["role"], []))
        self.skew += 1.0                                                                             # (one lookup has come back: two are needed for this action)
        stale()
        self.w.say(self.admin, "/mute bob 5")
        await self.settle()
        self.assertEqual(self.admin.codes(), ["role", "role"])
        self.skew += 1.05
        stale()
        self.w.say(self.admin, "/mute bob 5")
        await self.settle()
        self.assertEqual((self.admin.codes(), len(self.w.store.mutes())), (["role", "role"], 1))

    async def test_a_hub_with_no_roles_set_up_needs_no_lookup_and_only_the_owner_acts(self):
        cfg = config.load({"ANTS_HUB_OWNER": OWNER})
        roles = rolesmod.Roles(cfg, self.limits, self.clock, self.log)
        self.assertTrue(roles.fresh(["1001", "1002"]))
        self.assertTrue(await roles.confirm(["1001", "1002"]))
        self.w.chat.roles = roles
        roles.connected, roles.changed = self.w.chat.accounts, self.w.chat.role_changed
        owner = self.w.join(OWNER_IDENTITY)
        self.w.say(owner, "/mute bob 5")
        self.assertEqual(owner.codes(), [])
        self.assertGreater(self.w.store.muted_until("1002"), 0)
        self.w.say(self.mod, "/mute alice 5")                                                      # (the moderator's role is not asked of anybody: none)
        self.assertEqual(self.mod.codes(), ["bad"])

    async def test_a_bug_in_a_waiting_command_is_reported_by_its_class_and_never_prints_a_text(self):
        records, errors = [], []

        class Catch(logging.Handler):
            def emit(self, record):
                records.append(self.format(record))
        handler = Catch()
        logging.getLogger("asyncio").addHandler(handler)
        self.addCleanup(logging.getLogger("asyncio").removeHandler, handler)
        self.w.chat.on_command_error = errors.append

        async def breaks(accounts):
            raise RuntimeError("the id " + ID)
        self.age()
        with mock.patch.object(self.roles, "confirm", breaks):
            self.w.say(self.mod, "/kick bob")
            await self.settle()
        gc.collect()
        await asyncio.sleep(0.05)
        gc.collect()
        self.assertEqual(records, [])
        self.assertEqual([type(e).__name__ for e in errors], ["RuntimeError"])
        self.assertEqual(self.w.chat.busy, {})                                                     # (the socket may send another command)
        self.w.say(self.mod, "/mute bob 5")
        self.assertEqual(self.mod.codes()[-1:], [])


class Slow(Stub):
    """The stub of Discord whose answers wait `test.delay` seconds and are `test.mode` (a function of the account) or the members of the test."""

    def __init__(self, test):
        super().__init__(test.__class__, None)
        self.test = test

    async def member(self, guild, user, token):
        assert token == TOKEN
        self.calls.append((time.monotonic(), user))
        if self.test.delay:
            await asyncio.sleep(self.test.delay)
        result = self.test.mode(user) if self.test.mode else (good(user, self.test.members[user]) if user in self.test.members else NOT_A_MEMBER)
        if isinstance(result, Exception):
            raise result
        return result


class TheOfflineTarget(FreshBase):
    """A person who is not here is found by the username of its profile, and acted on only after Discord has said what it is, now: the lookup is made from the id that the database holds, and it is
    not written anywhere."""

    async def asyncSetUp(self):
        await super().asyncSetUp()
        self.members["1010"] = []
        self.w.store.set_profile("1010", "dora.d", "Dora D", 100)

    async def run_command(self, peer, text):
        self.w.say(peer, text)
        await self.settle()

    async def test_a_person_who_is_not_here_and_holds_no_role_can_be_muted_and_banned_after_a_lookup_of_both(self):
        await self.run_command(self.mod, "/mute dora.d 5")
        self.assertEqual(self.asked(), ["1010", "9001"])
        self.assertGreater(self.w.store.muted_until("1010"), 0)
        self.stub.calls.clear()
        await self.run_command(self.admin, "/ban dora.d")
        self.assertEqual(self.stub.calls, [])                                                          # (both answers are of a moment ago: nothing is asked)
        self.assertTrue(self.w.store.is_banned("1010"))
        self.assertEqual([m["text"] for m in self.reader_lines()], ["Dora D was banned."])

    def reader_lines(self):
        return [m for m in self.alice.kinds("line") if m.get("sys")]

    async def test_a_person_who_is_not_in_the_server_at_all_holds_no_role_and_that_is_an_answer(self):
        self.w.store.set_profile("1011", "eve.e", "", 100)                                            # (the fake knows nobody of that id: 404, code 10007)
        await self.run_command(self.mod, "/mute eve.e 5")
        self.assertEqual((self.mod.codes(), self.asked()), ([], ["1011", "9001"]))
        self.assertGreater(self.w.store.muted_until("1011"), 0)

    async def test_an_offline_administrator_or_moderator_is_power_and_a_moderator_can_do_nothing_to_them(self):
        self.members["1010"] = [ADMIN]
        await self.run_command(self.mod, "/mute dora.d 5")
        await self.run_command(self.admin, "/ban dora.d")
        self.members["1010"] = [MOD]
        self.age()
        await self.run_command(self.mod, "/mute dora.d 5")
        await self.run_command(self.admin, "/mute dora.d 5")
        self.assertEqual(self.mod.codes(), ["power", "power"])
        self.assertEqual(self.admin.codes(), ["power"])
        self.assertEqual(self.w.store.muted_until("1010") > 0, True)                                  # (the administrator, a moderator now, may be muted by an administrator)
        self.assertFalse(self.w.store.is_banned("1010"))

    async def test_a_lookup_that_fails_refuses_an_action_on_a_person_who_is_not_here(self):
        for mode in (lambda a: (500, {}, {}), lambda a: discordmod.DiscordError("down", None), lambda a: (200, {"roles": "x"}, {}), lambda a: (401, {"code": 0}, {})):
            self.roles.pause_until = self.roles.hold_until = float("-inf")
            self.mode = None
            self.grant("9101", "admin")
            self.age()
            self.mode = mode
            await self.run_command(self.admin, "/ban dora.d")
        self.assertEqual(self.admin.codes(), ["role"] * 4)
        self.assertEqual((self.w.store.bans(), self.w.store.actions()), ([], []))

    async def test_a_username_that_nobody_has_is_who_and_no_lookup_is_made(self):
        await self.run_command(self.admin, "/ban nobody.here")
        await self.run_command(self.admin, "/mute nobody.here 5")
        self.assertEqual((self.admin.codes(), self.stub.calls), (["who", "who"], []))

    async def test_a_kick_of_a_person_who_is_not_here_is_who_before_any_lookup(self):
        await self.run_command(self.admin, "/kick dora.d")
        self.assertEqual((self.admin.codes(), self.stub.calls), (["who"], []))

    async def test_an_answer_of_a_moment_ago_about_the_person_is_used_and_asked_again_after_a_minute(self):
        await self.run_command(self.mod, "/mute dora.d 5")
        self.stub.calls.clear()
        self.w.clock.now += 3
        await self.run_command(self.mod, "/mute dora.d 6")
        self.assertEqual(self.stub.calls, [])
        self.age()
        await self.run_command(self.mod, "/mute dora.d 7")
        self.assertEqual(self.asked(), ["1010", "9001"])

    async def test_the_line_of_a_person_who_is_not_here_is_deleted_after_a_lookup_of_its_author(self):
        self.w.store.add_line("1010", "Dora D", "a line of dora", False)
        await self.run_command(self.mod, "/del 1")
        self.assertEqual((self.mod.codes(), self.asked(), self.w.store.account_of_line(1)), ([], ["1010", "9001"], None))
        self.members["1010"] = [ADMIN]
        self.w.store.add_line("1010", "Dora D", "another", False)
        self.age()
        await self.run_command(self.mod, "/del 2")
        self.assertEqual((self.mod.codes(), self.w.store.account_of_line(2)), (["power"], "1010"))

    async def test_what_is_kept_of_a_person_who_is_not_here_is_forgotten_after_the_grace(self):
        await self.run_command(self.mod, "/mute dora.d 5")
        self.assertIn("1010", self.roles.entries)
        self.skew += self.roles.grace + 1
        self.roles.on_tick(self.clock())
        self.assertNotIn("1010", self.roles.entries)

    async def test_the_lookups_are_not_one_for_each_try_of_a_moderator_who_asks_over_and_over(self):
        for n in range(20):
            self.w.clock.now += 3
            await self.run_command(self.mod, "/mute dora.d %d" % (n + 1))
        self.assertLessEqual(len(self.stub.calls), 2)


class TheReference(unittest.TestCase):
    def test_it_is_16_hex_characters_the_same_for_an_account_and_not_the_same_for_two_or_under_two_secrets(self):
        a = pseudo.reference(b"s" * 32, "1001")
        self.assertRegex(a, r"^[0-9a-f]{16}\Z")
        self.assertEqual(a, pseudo.reference(b"s" * 32, "1001"))
        self.assertNotEqual(a, pseudo.reference(b"s" * 32, "1002"))
        self.assertNotEqual(a, pseudo.reference(b"t" * 32, "1001"))
        self.assertEqual(pseudo.LENGTH, 16)

    def test_it_is_hmac_sha256_of_the_id_truncated(self):
        import hashlib
        import hmac
        self.assertEqual(pseudo.reference(b"k" * 32, ID), hmac.new(b"k" * 32, ID.encode(), hashlib.sha256).hexdigest()[:16])
        self.assertNotIn(ID[:8], pseudo.reference(b"k" * 32, ID))

    def test_the_hub_s_secret_is_made_by_the_store_and_the_chat_and_the_command_line_agree_on_it(self):
        w = World(mods=())
        self.addCleanup(w.close)
        w.roles.roles["9001"] = "mod"
        mod, bob = w.join(MODERATOR), w.join(BOB)
        w.say(mod, "/mute bob 5")
        (row,) = w.store.actions()
        self.assertEqual((row.actor, row.target), (pseudo.reference(w.store.secret(), "9001"), pseudo.reference(w.store.secret(), "1002")))
        self.assertEqual(w.chat.ref("9001"), row.actor)


class TheLog(Powers):
    def test_every_action_that_is_done_leaves_one_row_with_references_and_no_id(self):
        self.say(self.bob, "from bob")
        self.say(self.mod, "/mute bob 5")
        self.say(self.mod, "/unmute bob")
        self.say(self.admin, "/kick alice")
        self.say(self.admin, "/ban other.mod")
        self.say(self.admin, "/unban other.mod")
        self.say(self.admin, "/del 1")
        rows = self.rows()
        self.assertEqual([r.action for r in rows], ["del", "unban", "ban", "kick", "unmute", "mute"])
        ref = self.w.chat.ref
        self.assertEqual([(r.actor, r.target, r.detail) for r in rows], [(ref("9101"), ref("1002"), "1"), (ref("9101"), ref("9002"), ""), (ref("9101"), ref("9002"), ""), (ref("9101"), ref("1001"), ""),
                                                                           (ref("9001"), ref("1002"), ""), (ref("9001"), ref("1002"), "5")])
        for r in rows:
            self.assertRegex(r.actor + r.target, r"^[0-9a-f]{32}\Z")
            self.assertNotIn("1002", r.target)
        self.assertEqual(len({r.at for r in rows}) >= 1, True)

    def test_a_refused_command_leaves_no_row(self):
        for text in ("/kick boss.one", "/ban bob", "/kick nobody", "/mute bob", "/kick moddy"):
            self.say(self.mod, text)
        self.assertEqual(self.rows(), [])

    def test_the_by_of_a_ban_a_mute_and_a_deletion_is_the_reference_and_not_the_id(self):
        self.say(self.bob, "hello")
        self.say(self.admin, "/ban alice")
        self.say(self.admin, "/mute bob 5")
        self.say(self.admin, "/del 1")
        by = self.w.chat.ref("9101")
        self.assertEqual((self.w.store.bans()[0][3], self.w.store.mutes()[0][3]), (by, by))
        self.assertEqual(self.w.store.db.execute("SELECT del_by FROM lines WHERE id = 1").fetchone()[0], by)
        self.assertNotIn("9101", [by])

    def test_the_log_is_in_no_frame_and_no_page_can_ask_for_it(self):
        self.say(self.admin, "/kick alice")
        for peer in (self.reader, self.alice, self.bob, self.mod, self.admin):
            self.assertNotIn(b"modlog", b"".join(peer.raw))
        for frame in ({"t": "log"}, {"t": "modlog"}, {"t": "actions"}):
            self.w.chat.on_text(self.admin, json.dumps(frame))
        self.assertEqual(self.admin.codes()[-3:], ["bad"] * 3)

    def test_a_moderator_cannot_make_the_log_grow_without_end(self):
        self.w.store.max_modlog = 30
        for n in range(100):
            self.say(self.mod, "/mute bob 5")
        self.assertLessEqual(len(self.rows()), 30)

    def test_forgetting_does_not_remove_the_rows_and_the_log_says_nothing_of_the_person_afterwards(self):
        self.w.store.set_profile("1002", "bob", "", 100)
        self.say(self.mod, "/mute bob 5")
        self.w.store.forget("1002")
        (row,) = self.rows()
        self.assertEqual(row.target, self.w.chat.ref("1002"))
        out = io.StringIO()
        admin.show_log(self.w.store, 30, 100, out)
        self.assertNotIn("bob", out.getvalue())                                                     # (no profile: only the reference)
        self.assertIn(row.target, out.getvalue())


class TheCommandLine(unittest.TestCase):
    def setUp(self):
        self.w = World(mods=())
        self.addCleanup(self.w.close)
        self.w.roles.roles.update({"9001": "mod", "9101": "admin"})
        self.mod, self.bob = self.w.join(MODERATOR), self.w.join(BOB)
        self.w.store.set_profile("9001", "moddy", "", 100)
        self.w.store.set_profile("1002", "bob", "", 100)
        self.w.say(self.bob, "hello there")
        self.w.say(self.mod, "/mute bob 5")

    def run_admin(self, *args):
        out, err = io.StringIO(), io.StringIO()
        status = admin.run(["--db", self.w.db] + list(args), out, err)
        return status, out.getvalue(), err.getvalue()

    def test_log_prints_the_rows_with_references_and_the_usernames_that_are_known(self):
        status, out, _ = self.run_admin("log")
        self.assertEqual(status, 0)
        ref = self.w.chat.ref
        self.assertRegex(out, r"mute +by %s \(moddy\) to %s \(bob\) 5\n" % (ref("9001"), ref("1002")))
        self.assertIn("moderators' log", out)
        self.assertNotIn("9001", out.replace(ref("9001"), "").replace(ref("1002"), ""))

    def test_what_the_command_line_does_is_written_to_the_log_by_terminal(self):
        self.assertEqual(self.run_admin("ban", "bob")[0], 0)
        self.assertEqual(self.run_admin("unban", "bob")[0], 0)
        self.assertEqual(self.run_admin("mute", "bob", "9")[0], 0)
        self.assertEqual(self.run_admin("mute", "bob", "0")[0], 0)
        self.assertEqual(self.run_admin("del", "1")[0], 0)
        rows = self.w.store.actions()
        self.assertEqual([(r.actor, r.action, r.detail) for r in rows][:5], [("terminal", "del", "1"), ("terminal", "unmute", ""), ("terminal", "mute", "9"), ("terminal", "unban", ""), ("terminal", "ban", "")])
        self.assertEqual(rows[0].target, self.w.chat.ref("1002"))
        self.assertEqual(self.run_admin("unban", "bob")[0], 1)                                      # (nothing done: no row)
        self.assertEqual(len(self.w.store.actions()), 6)
        out = self.run_admin("log")[1]
        self.assertIn("terminal", out)

    def test_log_takes_days_and_lines_and_keeps_to_the_30_days_that_the_hub_keeps(self):
        status, out, _ = self.run_admin("log", "--lines", "0")
        self.assertEqual((status, out.count(" mute ")), (0, 0))
        self.assertIn("nothing", out)
        self.assertEqual(self.run_admin("log", "--days", "999")[1].splitlines()[0], "moderators' log (newest first; 30 days at the most, 10,000 rows at the most):")
        self.w.store.db.execute("UPDATE modlog SET at = ?", (int(time.time()) - 31 * 86400,))             # (the command line reads the real clock)
        self.assertIn("nothing", self.run_admin("log")[1])
        self.w.store.db.execute("UPDATE modlog SET at = ?", (int(time.time()) - 29 * 86400,))
        self.assertIn(" mute ", self.run_admin("log")[1])
        self.assertIn("nothing", self.run_admin("log", "--days", "10")[1])

    def test_list_and_every_command_print_the_reference_and_never_an_id_and_an_id_by_a_mute_of_before_is_printed_as_its_reference(self):
        self.w.store.mute("1003", 5, "9001", "old.one")                                             # (a mute that a moderator made before the references: its `by` is an id)
        self.w.store.ban("1004", "9101", "old.two")
        for command in (("list",), ("ban", "bob"), ("unban", "bob"), ("mute", "bob", "5"), ("mute", "1002", "0"), ("ban", "1002"), ("unban", "1002"), ("log",), ("status",), ("del", "1")):
            status, out, err = self.run_admin(*command)
            text = out + err
            for account in ("1001", "1002", "1003", "1004", "9001", "9101"):
                self.assertNotRegex(text, r"(?<![0-9a-f])%s(?![0-9a-f])" % account, "%s printed %s" % (command, account))
        out = self.run_admin("list")[1]
        self.assertIn("by %s" % self.w.chat.ref("9001"), out)

    def test_an_account_is_addressed_by_the_reference_that_list_prints(self):
        ghost = self.w.store.add_line("5005", "ghost", "from before profiles were kept", False)
        ref = self.w.chat.ref("5005")
        self.assertIn(ref, self.run_admin("list")[1])
        self.assertEqual(self.run_admin("ban", ref)[0], 0)
        self.assertTrue(self.w.store.is_banned("5005"))
        self.assertEqual(self.run_admin("unban", ref)[0], 0)
        self.assertEqual(self.run_admin("ban", "0123456789abcdef")[0], 1)                           # (nobody has it)

    def test_a_wrong_use_does_not_echo_what_was_typed(self):
        err = io.StringIO()
        with mock.patch.object(sys, "stderr", err):
            for args in (("mute", "bob", ID), ("frobnicate", ID), ("del", ID + "x"), ("log", "--days", "x" + ID)):
                self.assertEqual(self.run_admin(*args)[0], 2, args)
        self.assertNotIn(ID, err.getvalue())
        self.assertIn("wrong use", err.getvalue())

    def test_the_command_line_has_full_power_over_anybody(self):
        self.w.store.set_profile("9101", "boss.one", "", 100)
        self.assertEqual(self.run_admin("ban", "boss.one")[0], 0)
        self.assertEqual(self.run_admin("mute", "moddy", "5")[0], 0)


class TheIdIsLikeAPassword(unittest.IsolatedAsyncioTestCase):
    """A fake Discord id is planted for the moderator and for the person acted on, and looked for everywhere that something is written, sent or printed."""

    async def asyncSetUp(self):
        self.skew = 0.0
        self.members = {ID: [MOD], TARGET_ID: []}
        self.delay = 0.0
        self.mode = None
        self.log = Log()
        self.logs = io.StringIO()
        from hub.log import Log as RealLog
        self.real = RealLog(self.logs)
        self.w = World(mods=())
        self.addCleanup(self.w.close)
        self.limits = dataclasses.replace(limitmod.Limits(), roles_refill_seconds=0.001, roles_jitter=0.0, roles_urgent_seconds=0.4, discord_seconds=1.0, roles_urgent_actor_burst=10 ** 6, named_lines_burst=10 ** 6)
        self.clock = lambda: time.monotonic() + self.skew
        self.stub = Slow(self)
        cfg = config.load(dict(ENV))
        self.roles = rolesmod.Roles(cfg, self.limits, self.clock, self.real, client=self.stub, rng=lambda: 0.5)
        self.w.chat.roles = self.roles
        self.roles.connected, self.roles.changed = self.w.chat.accounts, self.w.chat.role_changed
        self.w.chat.wait_limits = limitmod.RateByAccount(self.limits.roles_urgent_actor_burst, self.limits.roles_urgent_actor_refill_seconds)      # (the budgets of one moderator, as this test's limits have them)
        self.w.chat.named_limits = limitmod.RateByAccount(self.limits.named_lines_burst, self.limits.named_lines_refill_seconds)
        self.w.chat.on_store_error = lambda e: self.real.event("error", kind=type(e).__name__, where="store")
        self.w.chat.on_command_error = lambda e: self.real.event("error", kind=type(e).__name__, where="command")
        self.roles.start()
        self.addCleanup(self.roles.stop)
        self.w.store.set_profile(TARGET_ID, "target.t", "", 100)
        self.w.store.set_profile(ID, "actor.a", "", 100)
        self.actor = self.w.join(session.Identity(ID, "actor.a", False))
        self.target = self.w.join(session.Identity(TARGET_ID, "target.t", False))
        self.w.chat.welcome(self.actor)
        self.w.chat.welcome(self.target)
        await self.until(lambda: self.roles.fresh([ID, TARGET_ID]))

    async def until(self, what, seconds=5.0):
        end = time.monotonic() + seconds
        while not what() and time.monotonic() < end:
            await asyncio.sleep(0.005)
        self.assertTrue(what())

    async def settle(self):
        while self.w.chat.busy:
            await asyncio.gather(*list(self.w.chat.busy.values()), return_exceptions=True)

    def everything(self):
        """Every text that the run produced: the log, the frames that went out, the rows, the representations."""
        frames = b"".join(b"".join(p.raw) for p in self.w.chat.peers).decode()
        rows = repr(self.w.store.actions()) + repr(self.w.store.bans()) + repr(self.w.store.mutes()) + repr(self.w.store.db.execute("SELECT del_by FROM lines").fetchall())
        reprs = "".join(repr(x) + str(x) for x in (self.actor.identity, self.target.identity, self.roles, self.roles.entries, *self.roles.entries.values(), *self.w.store.recent_lines(50), *self.w.store.actions()))
        return {"log": self.logs.getvalue() + self.log.text(), "frames": frames, "rows": rows, "reprs": reprs}

    async def test_no_log_line_frame_answer_row_or_representation_holds_the_id_after_any_command_in_any_failure_of_discord(self):
        modes = {"ok": None, "500": lambda a: (500, {}, {}), "401": lambda a: (401, {"code": 0}, {}), "403": lambda a: (403, {"code": 50001}, {}), "429": lambda a: (429, {"retry_after": 1.0}, {"retry_after": 1.0}),
                 "garbage": lambda a: (200, {"roles": 5}, {}), "bug with the id": lambda a: RuntimeError("GET /members/" + a), "down": lambda a: discordmod.DiscordError("down", None),
                 "not a member": lambda a: NOT_A_MEMBER, "deleted": lambda a: (404, {"code": 10013}, {})}
        for name, mode in modes.items():
            self.roles.pause_until = self.roles.hold_until = float("-inf")
            self.skew += 61
            self.mode = mode
            for text in ("/mute target.t 5", "/unmute target.t", "/kick target.t", "/ban target.t", "/unban target.t", "/bans", "/del 1", "/kick nobody", "/ban actor.a", "/mute target.t x"):
                self.target = self.w.join(session.Identity(TARGET_ID, "target.t", False)) if self.target.closed else self.target
                self.w.clock.now += 3
                self.w.say(self.actor, text)
                await self.settle()
                if self.actor.closed:
                    self.actor = self.w.join(session.Identity(ID, "actor.a", False))
            self.w.say(self.target, "a line")
            await self.settle()
            for what, text in self.everything().items():
                assert_no_id(self, text, "%s after the mode %s" % (what, name))
            self.assertNotIn("#", self.logs.getvalue())                                              # (the log's scrubber never had a number to replace)
        self.assertGreater(len(self.w.store.actions()), 0)

    async def test_the_names_of_the_actors_are_not_in_the_log_either(self):
        self.w.say(self.actor, "/mute target.t 5")
        await self.settle()
        log = self.logs.getvalue()
        for text in ("actor.a", "target.t"):
            self.assertNotIn(text, log)

    async def test_an_error_that_holds_the_id_in_a_waiting_command_is_logged_as_its_class_only(self):
        self.skew += 61
        self.mode = lambda a: RuntimeError("GET /members/" + a)

        async def breaks(accounts):
            raise KeyError(ID)
        with mock.patch.object(self.roles, "confirm", breaks):
            self.w.say(self.actor, "/kick target.t")
            await self.settle()
        text = self.logs.getvalue()
        assert_no_id(self, text, "the log")
        self.assertIn("error kind=KeyError where=command", text)

    def test_the_default_representation_of_an_identity_and_a_line_never_shows_the_id(self):
        who = session.Identity(ID, "actor.a", False, "sessionkey" + ID, 0, "actor.a", "display", "x.y")
        line = storemod.base.Line(1, 2, ID, "actor.a", "text", 0)
        for text in (repr(who), str(who), "%r" % (who,), repr([who]), repr({"k": who}), repr(line), str(line), repr([line]), repr(storemod.base.Line._make([1, 2, TARGET_ID, "x", "y", 0]))):
            assert_no_id(self, text, "a representation")
            self.assertNotIn("sessionkey", text)
        self.assertEqual((who.account_id, line.account), (ID, ID))
        self.assertEqual((who[0], line[2]), (ID, ID))


class TheLiveHub(LiveTest):
    """Real sockets, the real hub with its real Roles against the fake Discord, the intervals short."""

    options = dict(LiveTest.options, roles_action_fresh_seconds=0.8, roles_urgent_seconds=2.0, roles_urgent_actor_burst=10 ** 6, named_lines_burst=10 ** 6)
    recheck = 60                                                                                   # (only the lookups at the handshake and the urgent ones of the actions: no check on a timer)

    def setUp(self):
        super().setUp()
        self.fake.members = {"1001": [MOD], "1002": [ADMIN], "1003": [], "1004": []}

    def drain_until(self, client, kind, seconds=5.0):
        end, seen = time.monotonic() + seconds, []
        while time.monotonic() < end:
            for m in client.drain(0.1):
                seen.append(m)
                if m.get("t") == kind:
                    return m, seen
        self.fail("no %s: %s" % (kind, seen))

    def grant(self, account, role):
        """The state that a test of a failure starts from: Discord has just said that the account holds `role`, and nothing is paused or held."""
        def go():
            roles = self.rig.hub.chat.roles
            roles.pause_until = roles.hold_until = float("-inf")
            entry = roles.entries[account]
            entry.role, entry.ok_at, entry.fails = role, roles.clock(), 0
            roles.settle(account, entry, roles.clock())
        self.rig.call(go)

    def moderator(self):
        mod, _ = self.connect("1001:moddy")
        self.wait_role(mod, "mod")
        return mod

    def test_kick_over_real_sockets_with_a_lookup_that_is_slow_and_the_login_ends(self):
        mod = self.moderator()
        v1, _ = self.connect("1003:victim")
        v2, _ = self.connect("1003:victim")
        watcher, _ = self.connect("1004:watcher")
        self.rig.call(lambda: self.rig.store.add_session("kickedlogin", "1003", "victim", 3600, 10, 100))
        self.rig.call(lambda: self.rig.store.add_session("otherlogin", "1004", "watcher", 3600, 10, 100))
        time.sleep(0.9)                                                                            # (the answers are older than the action's freshness: both are asked again)
        mod.drain(0.1)
        self.fake.member_mode = "slow:0.25"
        before = len(self.fake.member_ids)
        mod.say("/kick victim")
        self.drain_until(mod, "ok")
        self.assertEqual(sorted(set(self.fake.member_ids[before:])), ["1001", "1003"])
        for victim in (v1, v2):
            seen = victim.drain(1.0)
            self.assertIn({"t": "bye", "why": "kicked"}, seen)
        line, _ = self.drain_until(watcher, "line")
        self.assertEqual((line["who"], line["text"], line.get("sys"), line["mod"]), ("system", "victim was kicked.", True, False))
        self.assertNotIn("moddy", json.dumps(line))
        self.assertIsNone(self.rig.call(lambda: self.rig.store.session("kickedlogin")))
        self.assertIsNotNone(self.rig.call(lambda: self.rig.store.session("otherlogin")))
        again, welcome = self.connect("1003:victim")                                               # (at once: no timer)
        self.assertEqual(welcome["you"]["name"], "victim")
        again.say("back again")
        self.assertIn("back again", [m["text"] for m in watcher.drain(0.5) if m["t"] == "line"])
        newcomer, welcome = self.connect("1004:watcher")
        self.assertIn(("system", "victim was kicked."), [(h["who"], h["text"]) for h in welcome["history"]])

    def test_ban_unban_and_bans_over_real_sockets(self):
        admin1, _ = self.connect("1002:boss")
        self.wait_role(admin1, "admin")
        victim, _ = self.connect("1003:victim")
        other, _ = self.connect("1004:other")
        admin1.say("/ban victim")
        self.drain_until(admin1, "ok")
        self.assertIn({"t": "bye", "why": "banned"}, victim.drain(1.0))
        seen = other.drain(0.5)
        self.assertIn("victim was banned.", [m.get("text") for m in seen])
        admin1.say("/bans")
        frame, _ = self.drain_until(admin1, "bans")
        self.assertEqual([r["u"] for r in frame["rows"]], ["victim"])
        self.assertEqual(set(frame["rows"][0]), {"u", "at"})
        self.assertEqual(other.drain(0.3), [])
        again, _ = self.connect("1003:victim")
        again.say("i am here")
        self.assertIn({"t": "err", "code": "banned"}, again.drain(0.5))
        admin1.say("/unban victim")
        self.drain_until(admin1, "ok")
        again.say("a line after the unban")
        time.sleep(0.2)
        self.assertNotIn({"t": "err", "code": "banned"}, again.drain(0.5)[1:])
        actions = self.rig.call(lambda: [r.action for r in self.rig.store.actions()])
        self.assertEqual(actions, ["unban", "ban"])

    def test_a_moderator_cannot_ban_or_list_and_an_admin_cannot_ban_an_admin(self):
        mod = self.moderator()
        victim, _ = self.connect("1003:victim")
        mod.say("/ban victim")
        self.assertEqual(self.drain_until(mod, "err")[0]["code"], "power")
        mod.say("/bans")
        self.assertEqual(self.drain_until(mod, "err")[0]["code"], "power")
        boss, _ = self.connect("1002:boss")
        self.wait_role(boss, "admin")
        other_boss, _ = self.connect("1002:boss")
        mod.say("/kick boss")
        self.assertEqual(self.drain_until(mod, "err")[0]["code"], "power")
        boss.say("/ban boss")
        self.assertEqual(self.drain_until(boss, "err")[0]["code"], "power")

    def test_a_discord_that_fails_or_is_too_slow_refuses_the_action_and_the_chat_goes_on(self):
        mod = self.moderator()
        victim, _ = self.connect("1003:victim")
        reader, _ = self.connect("1004:reader")
        for n, mode in enumerate(("slow:2.5", "err:401", "status:500", "close", "notjson", "rate:1", "err:403", "err:404:10004")):
            self.fake.member_mode = "ok"
            self.grant("1001", "mod")                                                              # (what Discord said, a moment ago: the pause of a refused token is over, and the roles are as they were)
            self.grant("1003", "none")
            self.grant("1004", "none")
            time.sleep(0.9)                                                                        # (the answers are old again)
            self.fake.member_mode = mode
            mod.drain(0.1)
            mod.say("/kick victim")
            frame, _ = self.drain_until(mod, "err", 6.0)
            self.assertEqual(frame["code"], "role", mode)
            reader.say("the chat goes on, again %d" % n)
            self.assertIn(True, [m.get("t") == "line" for m in victim.drain(0.4)])
            time.sleep(0.2)
        self.assertEqual(self.rig.call(lambda: self.rig.store.actions()), [])
        self.assertFalse(self.rig.call(lambda: self.rig.hub.chat.sockets_of("1003")[0].closed))

    def test_the_owner_account_acts_on_a_person_with_a_young_answer_when_the_roles_are_refused_and_not_on_one_without(self):
        self.rig.__exit__(None, None, None)
        self.rig = hub_rig.Rig(member_fake=self.fake, recheck=60, owner="1009", **self.options)
        self.rig.__enter__()
        owner, welcome = self.connect("1009:the.owner")
        self.assertEqual(welcome["you"]["role"], "admin")
        victim, _ = self.connect("1003:victim")
        time.sleep(0.3)
        self.grant("1003", "none")
        self.fake.member_mode = "err:401"                                                          # (the token is refused from now on)
        owner.say("/mute victim 5")                                                                # (within the minute of the answer about the person: no lookup is needed)
        self.drain_until(owner, "ok")
        time.sleep(0.9)
        owner.say("/unmute victim")
        self.assertEqual(self.drain_until(owner, "err")[0]["code"], "role")                       # (the answer is old: a lookup is needed, and it cannot be had)
        victim.say("hi")
        self.assertIn({"t": "err", "code": "muted"}, victim.drain(0.5))


    def test_a_command_that_waits_is_stopped_with_the_hub(self):
        mod = self.moderator()
        victim, _ = self.connect("1003:victim")
        time.sleep(0.9)
        self.fake.member_mode = "slow:1.5"
        mod.say("/kick victim")
        time.sleep(0.3)
        tasks = self.rig.call(lambda: list(self.rig.hub.chat.busy.values()))
        self.assertEqual(len(tasks), 1)
        import asyncio as aio
        aio.run_coroutine_threadsafe(self.rig.hub.stop(), self.rig.loop).result(10)
        time.sleep(0.2)
        self.assertTrue(self.rig.call(lambda: all(t.done() for t in tasks)))

    def test_a_bug_in_a_command_that_waits_is_one_line_in_the_log_with_its_class_and_no_text(self):
        mod = self.moderator()
        victim, _ = self.connect("1003:victim")

        async def breaks(self, accounts):
            raise RuntimeError("the id " + ID)
        time.sleep(0.9)
        with mock.patch.object(rolesmod.Roles, "confirm", breaks):
            mod.say("/kick victim")
            time.sleep(0.5)
            mod.say("/mute victim 5")                                                              # (the socket may send another command: nothing is stuck)
            time.sleep(0.5)
        log = self.rig.log_text.getvalue()
        self.assertIn(" error kind=RuntimeError where=command", log)
        assert_no_id(self, log, "the log")
        self.assertNotIn("the id", log)
        self.assertEqual(self.rig.call(lambda: self.rig.hub.chat.busy), {})

    def test_the_log_is_read_with_the_command_line_and_the_hub_purges_it_by_its_clock(self):
        mod = self.moderator()
        victim, _ = self.connect("1003:victim")
        mod.say("/mute victim 5")
        self.drain_until(mod, "ok")
        rows = self.rig.call(lambda: self.rig.store.actions())
        self.assertEqual(len(rows), 1)
        out, err = io.StringIO(), io.StringIO()
        self.assertEqual(admin.run(["--db", self.rig.db, "log"], out, err), 0)
        self.assertIn(rows[0].actor, out.getvalue())
        self.rig.call(lambda: self.rig.store.db.execute("UPDATE modlog SET at = at - ?", (31 * 86400,)))
        self.rig.call(lambda: setattr(self.rig.hub, "next_purge", float("-inf")))
        end = time.monotonic() + 5
        while time.monotonic() < end and self.rig.call(lambda: self.rig.store.actions()):
            time.sleep(0.05)
        self.assertEqual(self.rig.call(lambda: self.rig.store.actions()), [])


class TheRealProcess(unittest.TestCase):
    """`python -m hub` as the image runs it (the one change: the minute that an answer stays young is half a second and the lookups come faster than two a second, so that a test need not wait), a fake Discord, and the id of the moderator and of the
    person acted on planted: in the log, the frames, the answers, the rows and the command line's output there is none."""

    RUN = ("import sys\nsys.path.insert(0, %r)\nfrom hub import limits, server\ninit = limits.Limits.__init__\n"
           "def patched(self, *a, **k):\n    k.setdefault('roles_action_fresh_seconds', 0.5)\n    k.setdefault('roles_urgent_seconds', 1.5)\n    k.setdefault('roles_refill_seconds', 0.01)\n    k.setdefault('roles_urgent_actor_burst', 10 ** 6)\n    k.setdefault('named_lines_burst', 10 ** 6)\n    init(self, *a, **k)\n"
           "limits.Limits.__init__ = patched\nraise SystemExit(server.main())\n") % REPO

    def test_no_id_anywhere_after_kick_ban_mute_and_a_failing_discord(self):
        fake = fd.FakeDiscord()
        self.addCleanup(fake.close)
        fake.members = {ID: [ADMIN], TARGET_ID: []}
        extra = dict(ENV, ANTS_HUB_TEST_LOGIN="1", ANTS_HUB_TEST_DISCORD_URL=fake.url, ANTS_HUB_ROLE_RECHECK_SECONDS="60")
        proc, folder, log_path, env = ths.start_process(extra)
        proc.kill()
        proc.wait(5)
        with open(log_path, "wb") as out:
            proc = subprocess.Popen([sys.executable, "-c", self.RUN], cwd=REPO, env=env, stdout=out, stderr=subprocess.STDOUT)
        clients, frames = [], []
        db = env["ANTS_HUB_DB"]
        try:
            port = ths.wait_for_port(proc, log_path)
            self.assertIsNotNone(port, ths.read_text(log_path))
            for mode in ("ok", "slow:0.2", "status:500", "close", "notjson", "ok", "err:404:10004", "err:403", "err:401"):
                fake.member_mode = "ok"
                actor = hc.Client(port, login=ID + ":actor.a")
                clients.append(actor)
                frames.append(actor.recv_json())
                end, admin_now = time.monotonic() + 3, (frames[-1]["you"] or {}).get("role") == "admin"            # (the welcome says what is known: an answer of the last run is kept for a few minutes)
                while time.monotonic() < end and not admin_now:
                    seen = actor.drain(0.05)
                    frames += seen
                    admin_now = any(m.get("t") == "you" and m["you"]["role"] == "admin" for m in seen)
                time.sleep(0.7)                                                      # (the answers are older than the half second that they stay young)
                fake.member_mode = mode
                for command in ("/mute target.t 5", "/unmute target.t", "/kick target.t", "/ban target.t", "/bans", "/unban target.t", "/del 1", "/kick actor.a", "/ban nobody"):
                    victim = hc.Client(port, login=TARGET_ID + ":target.t")
                    clients.append(victim)
                    frames.append(victim.recv_json())
                    actor.say(command)
                    frames += actor.drain(0.15) + victim.drain(0.05)
                    victim.close()
                actor.close()
                time.sleep(0.1)
            actor = hc.Client(port, login=ID + ":actor.a")
            clients.append(actor)
            frames.append(actor.recv_json())
            actor.say("hello everybody")
            frames += actor.drain(0.3)
            status, health = hc.http_get(port, "/hub/healthz")
            proc.send_signal(__import__("signal").SIGTERM)
            proc.wait(10)
        finally:
            for c in clients:
                c.close()
            if proc.poll() is None:
                proc.kill()
            proc.wait(5)
        text = ths.read_text(log_path)
        wire = json.dumps(frames)
        self.assertEqual({"ok", "bans", "bye"} - {m["t"] for m in frames}, set())
        self.assertIn("role", [m.get("code") for m in frames if m["t"] == "err"])
        for what, body in (("the log and stderr of the process", text), ("the frames", wire), ("healthz", health.decode())):
            assert_no_id(self, body, what)
        self.assertNotIn("#", text)
        for line in text.splitlines():
            self.assertRegex(line, r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ [a-z_]+( [a-z_]+=[A-Za-z0-9_.:/-]*)*$", line)
        self.assertNotIn("Traceback", text)
        conn = sqlite3.connect(db)
        try:
            rows = repr(conn.execute("SELECT * FROM modlog").fetchall())
            by = repr(conn.execute("SELECT by FROM bans").fetchall() + conn.execute("SELECT by FROM mutes").fetchall() + conn.execute("SELECT del_by FROM lines").fetchall())
            self.assertGreater(conn.execute("SELECT COUNT(*) FROM modlog").fetchone()[0], 0)
        finally:
            conn.close()
        assert_no_id(self, rows, "the moderators' log")
        assert_no_id(self, by, "the `by` of a ban, a mute and a deletion")
        for command in (("list",), ("log",), ("status",), ("unban", "target.t"), ("ban", "target.t"), ("mute", "target.t", "5"), ("log", "--lines", "5")):
            done = subprocess.run([sys.executable, "-m", "hub.admin", "--db", db] + list(command), cwd=REPO, capture_output=True, text=True, env=dict(os.environ, PYTHONDONTWRITEBYTECODE="1"))
            assert_no_id(self, done.stdout + done.stderr, "hub.admin %s" % " ".join(command))


class TheDocumentsSayWhatTheCodeDoes(unittest.TestCase):
    def setUp(self):
        with open(os.path.join(REPO, "docs", "HUB.md"), encoding="utf-8") as f:
            self.doc = f.read()

    def test_the_protocol_table_and_the_codes_list_what_the_chat_sends(self):
        for needle in ("`{\"t\":\"ok\",\"c\":c}`", "`{\"t\":\"bye\",\"why\":\"kicked\"\\|\"banned\"}`", "`{\"t\":\"bans\",\"rows\":[{\"u\",\"at\"},...]}`", "`power`", "`role`", "`who`",
                       "/kick username", "/ban username", "/unban username", "/bans", "/unmute username", "was kicked.", "was banned.", "The moderators' log", "hub.admin log",
                       "HMAC-SHA256", "16 hex", "30 days", "10,000 rows", "Forget me", "tests/scripts/test_hub_powers.py", "tests/data/hub_powers_mutants.json", "60 seconds"):
            self.assertIn(needle, self.doc, needle)
        with open(os.path.join(REPO, "hub", "chat.py"), encoding="utf-8") as f:
            sent = set(re.findall(r'mistake\(peer, "([a-z]+)"', f.read())) | set(re.findall(r'return "([a-z]+)"', f.read()))
        codes = self.doc[self.doc.index("Error codes:"):self.doc.index("\n", self.doc.index("Error codes:"))]
        for code in ("power", "role", "who"):
            self.assertIn("`%s`" % code, codes)
        self.assertNotIn("`kicked`", codes)                                                          # (there is no timer after a kick, so no code that refuses a kicked person)

    def test_the_protocol_version_is_still_one(self):
        from hub import PROTOCOL
        self.assertEqual(PROTOCOL, 1)
        self.assertIn("The protocol stays at version 1", self.doc)


if __name__ == "__main__":
    unittest.main()
