#!/usr/bin/env python3
"""The chat's commands (hub/commands.py, hub/gamedoor.py, hub/chat.py `on_say`, hub/config.py `game_admin_url`, hub/store/ `log_action`; docs/HUB_COMMANDS.md; run by ./run_tests.sh --fast and by
the CI). Every id, token and name here is made up. The game server is a stand-in (tests/scripts/fake_game_door.py) that checks every call as the real one does.

  TheTable               who has which command: the `cmds` rows of each role, nothing above the role, none for a reader, and the one table that the powers read
  TheCmdsFrame           real sockets: `cmds` after the welcome, and again with `you` to every socket of an account whose role changed
  TheAnswerForWhatIsNotYours  a word that is no command and a command of a higher role get the same frame, the same limit; `//`; a command is never a line, never kept, never shown
  TheBotsGrammar         every bound and every word of `/bots`, and the JSON that is made of a line
  TheMapNames            the game server's list of maps: a typed name is looked up and never sent
  TheGameDoor            what the hub sends to the stand-in (the pass, the token, the exact JSON), the answers, the log of the moderators, no id
  TheDoorFails           a game server that is down, slow, silent, answers garbage or too much: `door`, nothing hangs, nothing is done or logged; one call at a time; the fresh check of the role
  TheLimits              the limits of an account across all its sockets
  TheIgnore              /ignore and /unignore, in the live lines and in the welcome
  TheStaffCommands       /nopic and /pic, and what a role may use of the old commands
  TheSetting             ANTS_HUB_GAME_ADMIN_URL
  TheLiveHub             real sockets, the real hub, the stand-in: the answer is the asking socket's alone, and no id or pass is in the hub's log

tests/data/hub_commands_mutants.json breaks the rules one at a time for tools/mutate.py (every one must be caught).
"""
import asyncio
import dataclasses
import json
import os
import sys
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
import fake_game_door as fgd  # noqa: E402
import hub_client as hc  # noqa: E402
import hub_rig  # noqa: E402
from test_hub_core import FakePeer, World  # noqa: E402
from hub import chat as chatmod, commands as cmds, config, gamedoor, limits as limitmod, session  # noqa: E402
from hub.store import base as storebase  # noqa: E402

ADMIN_ID, MOD_ID, PLAYER_ID, OTHER_ID, OWNER_ID = ("7311000099990001", "7311000099990002", "7311000099990003", "7311000099990004", "7311000099990009")
PLANTED = tuple(p for i in (ADMIN_ID, MOD_ID, PLAYER_ID, OTHER_ID, OWNER_ID) for p in (i, i[:8], i[-8:]))
ADMIN = session.Identity(ADMIN_ID, "basil", False)
MOD = session.Identity(MOD_ID, "sage", False)
PLAYER = session.Identity(PLAYER_ID, "maple.leaf", False)
OTHER = session.Identity(OTHER_ID, "pine.cone", False)
OWNER = session.Identity(OWNER_ID, "the.boss", False)
UNKNOWN = {"t": "reply", "c": "unknown", "r": "unknown"}
EVERYONE = ["/help", "/invite", "/msg", "/follow", "/unfollow", "/ignore", "/unignore"]
MODERATORS = ["/mute", "/unmute", "/del", "/kick", "/mutes"]
ADMINISTRATORS = ["/ban", "/unban", "/bans", "/nopic", "/pic", "/noimages", "/images", "/bots"]
LIMITS = limitmod.Limits()
FAST = dataclasses.replace(LIMITS, door_seconds=1.0, door_connect_seconds=0.5, roles_urgent_actor_burst=100)


def assert_no_id(test, text, where):
    for part in PLANTED:
        test.assertNotIn(part, text, "%s holds the id (%s)" % (where, part))


def replies(peer):
    return peer.kinds("reply")


def world(limits=LIMITS):
    w = World(mods=(), limits=limits)
    w.roles.roles.update({ADMIN_ID: "admin", MOD_ID: "mod", OWNER_ID: "admin"})
    return w


# ---- the table ---------------------------------------------------------------------------------------------------------------------------------------

class TheTable(unittest.TestCase):
    def test_each_role_has_its_commands_in_the_order_of_the_table_and_nothing_above(self):
        self.assertEqual([r["w"] for r in cmds.lines_for("none")], EVERYONE)
        self.assertEqual([r["w"] for r in cmds.lines_for("mod")], EVERYONE + MODERATORS)
        self.assertEqual([r["w"] for r in cmds.lines_for("admin")], EVERYONE + MODERATORS + ADMINISTRATORS)
        self.assertEqual([r["w"] for r in cmds.lines_for("anything else")], EVERYONE)               # (a role that is none of the three has the commands of everybody and no more)

    def test_a_row_is_four_short_texts_in_its_group_and_there_are_at_most_24(self):
        for role in ("none", "mod", "admin"):
            rows = cmds.lines_for(role)
            self.assertLessEqual(len(rows), 24)
            for row in rows:
                self.assertEqual(set(row), {"w", "a", "d", "g"})
                self.assertTrue(all(isinstance(v, str) for v in row.values()))
                self.assertTrue(row["w"].startswith("/") and row["d"] and len(row["d"]) <= 40, row)
        groups = {r["w"]: r["g"] for r in cmds.lines_for("admin")}
        self.assertEqual([groups[w] for w in EVERYONE], ["Everyone"] * len(EVERYONE))
        self.assertEqual([groups[w] for w in MODERATORS], ["Moderators"] * 5)
        self.assertEqual([groups[w] for w in ADMINISTRATORS], ["Administrators"] * len(ADMINISTRATORS))

    def test_the_group_of_a_row_is_the_rank_that_the_table_of_powers_reads(self):
        rank_of_group = {"Everyone": 0, "Moderators": 1, "Administrators": 2}
        for row in cmds.lines_for("admin"):
            self.assertEqual(chatmod.NEEDS[row["w"][1:]], rank_of_group[row["g"]], row)

    def test_a_reader_is_sent_no_list_and_a_person_who_is_logged_in_is_sent_hers(self):
        w = world()
        self.addCleanup(w.close)
        reader, player, mod, admin = w.join(None), w.join(PLAYER), w.join(MOD), w.join(ADMIN)
        for peer in (reader, player, mod, admin):
            w.chat.commands.send_list(peer)
        self.assertEqual(reader.sent, [])
        self.assertEqual([[r["w"] for r in p.kinds("cmds")[0]["rows"]] for p in (player, mod, admin)],
                         [EVERYONE, EVERYONE + MODERATORS, EVERYONE + MODERATORS + ADMINISTRATORS])

    def test_help_answers_that_the_list_is_the_pages_to_open(self):
        w = world()
        self.addCleanup(w.close)
        player = w.join(PLAYER)
        w.say(player, "/help")
        w.say(player, "/HELP me please")
        self.assertEqual(replies(player), [{"t": "reply", "c": "help", "r": "list"}] * 2)
        self.assertEqual(player.codes(), [])


class TheCmdsFrame(unittest.TestCase):
    def test_a_login_gets_its_list_after_the_welcome_and_a_change_of_role_sends_it_again_to_every_socket(self):
        roles = hub_rig.FakeRoles({ADMIN_ID: "admin"})
        with hub_rig.Rig(roles=roles) as rig:
            login = lambda ident: "%s:%s" % (ident.account_id, ident.username)
            first, second = hc.Client(rig.port, login=login(PLAYER)), hc.Client(rig.port, login=login(PLAYER))
            other, reader = hc.Client(rig.port, login=login(OTHER)), hc.Client(rig.port)
            for client in (first, second, other, reader):
                self.addCleanup(client.close)
                self.assertEqual(client.recv_json()["t"], "welcome")
            for client in (first, second, other):
                self.assertEqual([r["w"] for r in client.recv_json()["rows"]], EVERYONE)
            self.assertEqual(reader.drain(0.4), [])                                       # (a reader is sent no list)
            rig.call(roles.set, PLAYER_ID, "mod")
            for client in (first, second):
                frames = [m for m in client.drain(0.5) if m["t"] in ("you", "cmds")]
                self.assertEqual([m["t"] for m in frames], ["you", "cmds"])
                self.assertEqual(frames[0]["you"]["role"], "mod")
                self.assertEqual([r["w"] for r in frames[1]["rows"]], EVERYONE + MODERATORS)
            self.assertEqual([m for m in other.drain(0.3) if m["t"] in ("you", "cmds")], [])         # (another account is told nothing)
            rig.call(roles.set, PLAYER_ID, "none")
            for client in (first, second):
                frames = [m for m in client.drain(0.5) if m["t"] == "cmds"]
                self.assertEqual([[r["w"] for r in m["rows"]] for m in frames], [EVERYONE])        # (a demotion takes the rows away at once)


# ---- a word that is not theirs, `//`, and a command that is never a line ------------------------------------------------------------------------------------

class TheAnswerForWhatIsNotYours(unittest.TestCase):
    def setUp(self):
        self.w = world()
        self.addCleanup(self.w.close)
        self.player, self.mod, self.admin, self.reader = self.w.join(PLAYER), self.w.join(MOD), self.w.join(ADMIN), self.w.join(None)

    def test_a_stray_word_and_a_command_of_a_higher_role_get_the_very_same_bytes(self):
        stray = ["/frobnicate", "/", "/ ", "/x y z", "/BOTS5", "/bot", "/k\u0131ck bob", "/ban\u0131 bob"]       # (a dotless i is not an i)
        above = {"none": ["/mute bob 5", "/unmute bob", "/del 1", "/kick bob", "/mutes", "/ban bob", "/unban bob", "/bans", "/nopic bob", "/pic bob", "/bots 5", "/bots status", "/BOTS stop"],
                 "mod": ["/ban bob", "/unban bob", "/bans", "/nopic bob", "/pic bob", "/bots 5", "/bots status", "/Bots stop"]}
        for who, peer in (("none", self.player), ("mod", self.mod)):
            mine = [self.w.chat.commands.unknown_frame]
            for text in stray + above[who]:
                before = len(peer.raw)
                self.w.clock.now += 3
                self.w.say(peer, text)
                self.assertEqual(peer.raw[before:], mine, (who, text))
        self.assertEqual(self.player.codes() + self.mod.codes(), [])                              # (no `err`: nothing in the answer says which it was)
        self.assertEqual(json.loads(self.w.chat.commands.unknown_frame), UNKNOWN)

    def test_over_the_limit_the_answer_is_the_same_too_and_names_no_command(self):
        frames = {}
        for who, peer, text in (("stray", self.player, "/frobnicate"), ("not theirs", self.mod, "/bots 5")):
            for n in range(12):
                self.w.say(peer, text)
            frames[who] = [m for m in replies(peer) if m["r"] == "rate"]
        self.assertEqual(len(frames["stray"]), 2)
        self.assertEqual(frames["stray"], frames["not theirs"])
        self.assertEqual(frames["stray"][0]["c"], "unknown")
        self.assertTrue(1 <= frames["stray"][0]["wait"] <= 2, frames["stray"][0])
        self.assertEqual(set(frames["stray"][0]), {"t", "c", "r", "wait"})

    def test_a_word_that_is_theirs_names_itself_when_it_is_over_the_limit(self):
        for n in range(11):
            self.w.say(self.player, "/help")
        self.assertEqual(replies(self.player)[-1]["c"], "help")
        self.assertEqual(replies(self.player)[-1]["r"], "rate")

    def test_two_slashes_are_a_line_that_starts_with_one_and_every_limit_holds_for_it(self):
        self.w.say(self.player, "//shrug")
        self.w.clock.now += 3
        self.w.say(self.player, "///three")
        self.w.clock.now += 3
        self.w.say(self.player, "//ban bob")                                                       # (text: nothing is done, and it is said for everybody to read)
        self.assertEqual([m["text"] for m in self.reader.kinds("line")], ["/shrug", "//three", "/ban bob"])
        self.assertEqual([(l.text, l.name) for l in self.w.store.recent_lines(10)][0], ("/shrug", "maple.leaf"))
        self.assertEqual(self.w.store.bans(), [])
        flood = world()
        self.addCleanup(flood.close)
        peer = flood.join(PLAYER)
        for n in range(15):
            flood.say(peer, "//line %d" % n)
        self.assertEqual(peer.codes(), ["rate"] * 5)                                               # (a burst of ten, as for any line)
        self.assertEqual(len(flood.store.recent_lines(50)), 10)

    def test_a_command_is_never_a_line_never_kept_never_shown_and_costs_no_line(self):
        other = self.w.join(OTHER)
        for peer, text in ((self.player, "/help"), (self.player, "/ignore nobody"), (self.player, "/frobnicate"), (self.mod, "/mute pine.cone 5"), (self.mod, "/unmute pine.cone"),
                           (self.mod, "/mutes"), (self.admin, "/bans"), (self.admin, "/bots status"), (self.admin, "/bots 0"), (self.admin, "/kick nobody"), (self.player, "/"), (self.player, "/BANS")):
            self.w.clock.now += 3
            self.w.say(peer, text)
        for watcher in (self.reader, other, self.player, self.mod, self.admin):
            self.assertEqual(watcher.kinds("line"), [])
        self.assertEqual((self.w.store.recent_lines(50), self.w.store.lines(50), list(self.w.chat.ring)), ([], [], []))
        self.assertEqual(self.w.chat.welcome(self.reader)["history"], [])
        self.w.clock.now += 3
        for n in range(10):
            self.w.say(self.player, "a line %d" % n)                                               # (the commands used none of the account's ten at once)
        self.assertEqual((len(self.reader.kinds("line")), self.player.codes()), (10, []))

    def test_what_a_command_says_is_not_remembered_by_the_duplicate_rule(self):
        self.w.say(self.player, "/help")
        self.w.say(self.player, "help")
        self.assertEqual([m["text"] for m in self.reader.kinds("line")], ["help"])

    def test_a_banned_or_muted_or_young_account_is_refused_before_any_command(self):
        self.w.store.mute(PLAYER_ID, 5, "x", "x")
        self.w.say(self.player, "/help")
        self.assertEqual((self.player.codes(), replies(self.player)), (["muted"], []))
        self.w.store.ban(MOD_ID, "x", "x")
        self.w.say(self.mod, "/mutes")
        self.assertEqual((self.mod.codes(), replies(self.mod)), (["banned"], []))
        young = self.w.join(session.Identity("7311000099990005", "newbie", True))
        self.w.say(young, "/help")
        self.assertEqual((young.codes(), replies(young)), (["young"], []))


# ---- /bots: the line ---------------------------------------------------------------------------------------------------------------------------------

def parse(text):
    return cmds.parse_bots(None, text)


def error(text):
    got = parse(text)
    return (got.code, got.fields) if isinstance(got, cmds.Reply) else got


class TheBotsGrammar(unittest.TestCase):
    def test_the_count_is_one_to_ten_thousand_in_at_most_five_digits(self):
        for text, count in (("1", 1), ("10", 10), ("9999", 9999), ("10000", 10000), ("00007", 7), ("010000", None)):
            if count is None:
                self.assertEqual(error("/bots " + text), ("bad_count", {}), text)
            else:
                self.assertEqual(parse("/bots " + text).count, count, text)
        for text in ("0", "00000", "10001", "99999", "100000", "1000000", "123456789012345678901234567890", "-1", "+5", "5.0", "1e3", "abc", "five", "\u0663", "0x10", "5a", "1_0"):
            self.assertEqual(error("/bots " + text), ("bad_count", {}), text)

    def test_nothing_after_the_word_and_status_and_stop_take_nothing(self):
        self.assertEqual(error("/bots"), ("usage", {}))
        self.assertEqual(parse("/bots status").kind, "status")
        self.assertEqual(parse("/bots STOP").kind, "stop")
        for text in ("/bots status now", "/bots stop all", "/bots stop 5", "/bots status status"):
            self.assertEqual(error(text), ("usage", {}), text)

    def test_every_is_whole_minutes_or_a_half_from_0_to_120(self):
        for text, seconds in (("0", 0), ("0.5", 30), ("1", 60), ("1.5", 90), ("2.5", 150), ("119.5", 7170), ("120", 7200), ("007", 420), ("00.5", 30)):
            self.assertEqual(parse("/bots 5 every " + text).every_s, seconds, text)
        for text in ("-1", "120.5", "121", "1000", "1.25", "1.", ".5", "0.0", "1.55", "abc", "1e2", "1,5", "\u0662", "+3", "0.50", "1000000"):
            self.assertEqual(error("/bots 5 every " + text), ("bad_every", {}), text)
        self.assertEqual(error("/bots 5 every"), ("bad_every", {}))
        self.assertEqual(parse("/bots 5").every_s, 0)

    def test_the_words_of_the_options_and_the_json_that_is_made_of_a_line(self):
        full = parse("/bots 1000 every 2.5 rules 187 bots 4 levels hard teams 13-24 map Super jungle")
        self.assertEqual(cmds.request_of(full, "Super jungle.lvl"),
                         {"count": 1000, "every_s": 150, "map": "Super jungle.lvl", "rules": "187", "bots": "4", "levels": ["hard"] * 4, "teams": "split", "split": "13-24"})
        self.assertEqual(cmds.request_of(parse("/bots 5"), None), {"count": 5, "every_s": 0, "map": "random:rotation", "rules": "random", "bots": "random", "levels": ["rmh"] * 4, "teams": "random", "split": ""})
        for word, value in (("highest", "highest-score"), ("187", "187")):
            self.assertEqual(parse("/bots 5 rules " + word).rules, value)
        for word in ("2", "3", "4"):
            self.assertEqual(parse("/bots 5 bots " + word).bots, word)
        for word, value in (("easy", "easy"), ("medium", "medium"), ("hard", "hard"), ("mixed", "rmh"), ("any", "random")):
            self.assertEqual(parse("/bots 5 levels " + word).levels, value)
        self.assertEqual((parse("/bots 5 teams free").teams, parse("/bots 5 teams free").split), ("ffa", ""))
        for word, bots in (("12-34", "4"), ("13-24", "4"), ("14-23", "4"), ("1-23", "3"), ("2-13", "3"), ("3-12", "3")):
            got = parse("/bots 5 bots %s teams %s" % (bots, word))
            self.assertEqual((got.teams, got.split, got.bots), ("split", word, bots), word)
        self.assertEqual(parse("/bots 5 LEVELS HARD Rules Highest").levels, "hard")                # (the words are read in any letter case)

    def test_a_split_needs_the_number_of_bots_it_is_made_for(self):
        for text in ("/bots 5 teams 12-34", "/bots 5 teams 1-23", "/bots 5 bots 3 teams 12-34", "/bots 5 bots 4 teams 1-23", "/bots 5 bots 2 teams 12-34", "/bots 5 teams 12-34 bots 3"):
            self.assertEqual(error(text), ("bad_option", {}), text)
        self.assertEqual(parse("/bots 5 teams 12-34 bots 4").split, "12-34")                      # (in either order)

    def test_an_option_twice_an_unknown_word_or_a_value_that_is_none_of_its_words_is_bad_option(self):
        for text in ("/bots 5 rules 187 rules highest", "/bots 5 every 1 every 2", "/bots 5 bots 2 bots 3", "/bots 5 levels easy levels hard", "/bots 5 teams free teams free", "/bots 5 teams free every 1 teams free",
                     "/bots 5 speed fast", "/bots 5 rules", "/bots 5 rules fast", "/bots 5 rules 188", "/bots 5 bots 1", "/bots 5 bots 5", "/bots 5 bots two", "/bots 5 levels expert", "/bots 5 teams 1-2",
                     "/bots 5 teams split", "/bots 5 teams random", "/bots 5 levels", "/bots 5 bots", "/bots 5 teams", "/bots 5 foo", "/bots 5 5", "/bots 5 rules  187 extra"):
            self.assertEqual(error(text), ("bad_option", {}), text)

    def test_the_map_goes_last_takes_the_rest_of_the_line_and_is_one_to_96_characters(self):
        self.assertEqual(parse("/bots 5 map TINY").map_name, "TINY")
        self.assertEqual(parse("/bots 5 map every five rules").map_name, "every five rules")      # (the rest of the line, option words and all)
        self.assertEqual(parse("/bots 5 map   a   b  ").map_name, "a b")                           # (runs of blanks collapse)
        self.assertEqual(parse("/bots 5 every 2 map " + "x" * 96).map_name, "x" * 96)
        self.assertEqual(error("/bots 5 map " + "x" * 97), ("bad_map", {"close": []}))
        self.assertEqual(error("/bots 5 map"), ("bad_map", {"close": []}))
        self.assertEqual(error("/bots 5 map  "), ("bad_map", {"close": []}))
        for name in ("a\x00b", "a\x1fb", "a\x7fb", "tab\there", "../etc/passwd", "a/b", "a\\b", "/etc", "C:\\x", "x/", "..\\.."):
            self.assertEqual(error("/bots 5 map " + name), ("bad_map", {"close": []}), repr(name))
        self.assertEqual(parse("/bots 5 map Br\u00fcder.lvl").map_name, "Br\u00fcder.lvl")             # (any other character is left to the game server's list)

    def test_a_letter_of_another_script_is_no_letter_of_the_grammar(self):
        self.assertEqual(parse("/bots 5 rules highest").rules, "highest-score")
        self.assertEqual(error("/bots 5 rules h\u0131ghest"), ("bad_option", {}))                   # (a dotless i is not an i)
        self.assertEqual(error("/bots 5 \u212Aeys"), ("bad_option", {}))                            # (the Kelvin sign lower-cases to a k, and is no k of a word of the grammar)
        self.assertEqual(cmds.lower("\u212A"), "\u212A")


# ---- the setting ---------------------------------------------------------------------------------------------------------------------------------------

class TheSetting(unittest.TestCase):
    def test_the_address_is_http_a_host_a_port_and_nothing_else(self):
        for good, shown in (("http://ants-server:4030", "http://ants-server:4030"), ("http://127.0.0.1:4030", "http://127.0.0.1:4030"), ("http://a.b-c.example:1", "http://a.b-c.example:1"),
                            ("  http://ants-server:65535  ", "http://ants-server:65535"), ("http://x:0040", "http://x:40")):
            self.assertEqual(config.load({"ANTS_HUB_GAME_ADMIN_URL": good}).game_admin_url, shown)
        for bad in ("https://ants-server:4030", "http://ants-server", "http://ants-server:0", "http://ants-server:65536", "http://ants-server:4030/", "http://ants-server:4030/admin", "http://ants-server:4030?x=1",
                    "http://user@ants-server:4030", "http://user:pw@ants-server:4030", "http://[::1]:4030", "http://ant_s:4030", "http://-ants:4030", "http://ants-:4030", "http://:4030", "ants-server:4030",
                    "http://ants server:4030", "http://ants-server:40 30", "http://ants-server:4030#x", "http://ants-server:+4030", "http://ants-server:123456", "ftp://ants-server:4030", "http://ants\u00e9:4030"):
            with self.assertRaises(config.ConfigError, msg=bad) as caught:
                config.load({"ANTS_HUB_GAME_ADMIN_URL": bad})
            self.assertIn("ANTS_HUB_GAME_ADMIN_URL", str(caught.exception))
        with self.assertRaises(config.ConfigError) as caught:
            config.load({"ANTS_HUB_GAME_ADMIN_URL": "http://canary-host-9f3a.invalid:99999999"})
        self.assertNotIn("canary-host-9f3a", str(caught.exception))                                 # (the message never says the value)

    def test_none_is_none_and_the_summary_of_the_settings_never_says_it(self):
        self.assertEqual(config.load({}).game_admin_url, "")
        self.assertEqual(config.load({"ANTS_HUB_GAME_ADMIN_URL": "   "}).game_admin_url, "")
        self.assertNotIn("ants-server", repr(config.load({"ANTS_HUB_GAME_ADMIN_URL": "http://ants-server:4030"}).summary()))


# ---- the map's name -----------------------------------------------------------------------------------------------------------------------------------

class TheMapNames(unittest.TestCase):
    MAPS = [(row["file"], row["playable"]) for row in fgd.MAPS]

    def test_the_file_name_with_or_without_lvl_in_any_case_is_the_one_map(self):
        for typed in ("TINY.LVL", "tiny.lvl", "TiNy", "tiny", "SUPER JUNGLE", "super jungle.LVL", "Sand-Box"):
            kind, found = gamedoor.find_map(self.MAPS, typed)
            self.assertEqual(kind, "ok", typed)
            self.assertIn(found, [name for name, _ in self.MAPS])
        self.assertEqual(gamedoor.find_map(self.MAPS, "tiny")[1], "TINY.LVL")
        self.assertEqual(gamedoor.find_map(self.MAPS, "super JUNGLE")[1], "Super jungle.lvl")

    def test_a_map_that_cannot_be_played_is_no_map_and_one_that_is_not_there_has_close_names(self):
        self.assertEqual(gamedoor.find_map(self.MAPS, "broken one"), ("no_map", None))
        kind, close = gamedoor.find_map(self.MAPS, "jungle")
        self.assertEqual((kind, close), ("bad_map", ["Jungle2000.lvl", "Super jungle.lvl"]))        # (starts with it, or holds it; the shortest first)
        self.assertEqual(gamedoor.find_map(self.MAPS, "zzz"), ("bad_map", []))
        many = [("map%02d.lvl" % n, True) for n in range(10)]
        self.assertEqual(gamedoor.find_map(many, "map")[1], ["map00.lvl", "map01.lvl", "map02.lvl"])      # (three at the most)
        self.assertEqual(gamedoor.find_map(self.MAPS + [("broken two.lvl", False)], "broken")[1], [])          # (a map that cannot be played is not offered)

    def test_two_files_that_differ_in_letter_case_only_are_not_guessed_between(self):
        twins = [("Foo.lvl", True), ("foo.LVL", True), ("Bar.lvl", True)]
        self.assertEqual(gamedoor.find_map(twins, "FOO"), ("bad_map", ["Foo.lvl", "foo.LVL"]))

    def test_the_text_of_a_name_that_is_told_is_plain(self):
        self.assertEqual(gamedoor.plain_text("basil"), "basil")
        self.assertEqual(gamedoor.plain_text("a\x00b\nc"), "abc")
        self.assertEqual(gamedoor.plain_text("x" * 100), "x" * 40)
        self.assertEqual(gamedoor.plain_text("7311000099990001"), "")                               # (an id's length of digits is not told)
        self.assertEqual(gamedoor.plain_text("12345"), "12345")
        self.assertEqual((gamedoor.plain_text(None), gamedoor.plain_text(5), gamedoor.plain_text(["a"])), ("", "", ""))


# ---- the game door and /bots ---------------------------------------------------------------------------------------------------------------------------------

class DoorTest(unittest.IsolatedAsyncioTestCase):
    limits = FAST

    async def asyncSetUp(self):
        self.stand = fgd.FakeGameDoor(now=lambda: self.w.wall.now)
        self.addCleanup(self.stand.stop)
        self.w = world(self.limits)
        self.addCleanup(self.w.close)
        self.install(self.stand.url)
        self.admin, self.mod, self.player, self.reader = self.w.join(ADMIN), self.w.join(MOD), self.w.join(PLAYER), self.w.join(None)
        self.addCleanup(self.no_problems)

    def no_problems(self):
        self.assertEqual(self.stand.problems, [], "a call that the game server would not take")
        all_text = repr([(c.raw, c.passed) for c in self.stand.calls])
        assert_no_id(self, all_text, "what the game server was sent")

    def install(self, url, secret=fgd.SECRET, owner=None):
        door = gamedoor.GameDoor(url, secret, self.limits, self.w.clock, self.w.wall, self.w.chat.ref, lambda account: account == owner)
        self.w.chat.commands.door = door
        self.w.chat.commands.is_owner = lambda account: account == owner
        return door

    async def settle(self, seconds=10.0):
        end = time.monotonic() + seconds
        while self.w.chat.commands.flights and time.monotonic() < end:
            await asyncio.sleep(0.005)
        self.assertEqual(self.w.chat.commands.flights, {}, "a call is still on its way")

    async def say(self, peer, text, advance=11.0):
        """The line, said `advance` seconds after the last (the limit of a start is three at once and then one in ten seconds), and the wait for the call that it made."""
        self.w.clock.now += advance
        self.w.say(peer, text)
        await self.settle()

    def last(self, peer):
        return replies(peer)[-1]

    def rows(self):
        return [(r.action, r.actor, r.target, r.detail) for r in self.w.store.actions()]


class TheGameDoor(DoorTest):
    async def test_a_start_sends_the_json_that_is_made_of_the_line_and_the_page_is_told_what_started(self):
        await self.say(self.admin, "/bots 5")
        self.assertEqual([(c.method, c.tail) for c in self.stand.calls], [("POST", "bot-matches"), ("GET", "bot-matches/summary")])
        self.assertEqual(self.stand.calls[0].payload, {"count": 5, "every_s": 0, "map": "random:rotation", "rules": "random", "bots": "random", "levels": ["rmh"] * 4, "teams": "random", "split": ""})
        self.assertEqual(self.last(self.admin), {"t": "reply", "c": "bots", "r": "started", "asked": 5, "started": 5, "queued": 0, "at_once": 80})
        await self.say(self.admin, "/bots 1000 every 2.5 rules 187 bots 4 levels hard teams 13-24 map tiny")
        post = [c for c in self.stand.calls if c.method == "POST"][-1]
        self.assertEqual(post.payload, {"count": 1000, "every_s": 150, "map": "TINY.LVL", "rules": "187", "bots": "4", "levels": ["hard"] * 4, "teams": "split", "split": "13-24"})
        self.assertEqual(self.last(self.admin), {"t": "reply", "c": "bots", "r": "started", "asked": 1000, "started": 80, "queued": 920, "at_once": 80, "every_s": 150})

    async def test_what_is_typed_never_goes_to_the_game_server_but_the_name_of_a_file_from_its_own_list(self):
        typed = "tInY"
        await self.say(self.admin, "/bots 3 map " + typed)
        self.assertEqual([c.payload["map"] for c in self.stand.calls if c.method == "POST"], ["TINY.LVL"])
        for call in self.stand.calls:
            self.assertNotIn(typed.encode(), call.raw, "the typed name went on")
        await self.say(self.admin, "/bots 3 map nothing-like-it")
        self.assertEqual(self.last(self.admin), {"t": "reply", "c": "bots", "r": "bad_map", "close": []})
        await self.say(self.admin, "/bots 3 map jungle")
        self.assertEqual(self.last(self.admin), {"t": "reply", "c": "bots", "r": "bad_map", "close": ["Jungle2000.lvl", "Super jungle.lvl"]})
        await self.say(self.admin, "/bots 3 map broken one")
        self.assertEqual(self.last(self.admin), {"t": "reply", "c": "bots", "r": "no_map"})
        self.assertEqual(len([c for c in self.stand.calls if c.method == "POST"]), 1)               # (no start for a name that is not in the list)
        for call in self.stand.calls:
            self.assertNotIn(b"nothing-like-it", call.raw)
        text = "inject\", \"count\": 99999"
        await self.say(self.admin, "/bots 3 map " + text)
        self.assertEqual(len([c for c in self.stand.calls if c.method == "POST"]), 1)

    async def test_the_list_of_maps_is_kept_for_five_minutes(self):
        await self.say(self.admin, "/bots 1 map tiny", advance=0)
        await self.say(self.admin, "/bots 1 map small")
        self.assertEqual([c.tail for c in self.stand.calls].count("bot-options"), 1)
        await self.say(self.admin, "/bots 1 map tiny", advance=277)                                 # (288 seconds after the list was read)
        self.assertEqual([c.tail for c in self.stand.calls].count("bot-options"), 1)
        await self.say(self.admin, "/bots 1 map tiny", advance=11)                                  # (299)
        self.assertEqual([c.tail for c in self.stand.calls].count("bot-options"), 1)
        await self.say(self.admin, "/bots 1 map tiny", advance=2)                                   # (301: the list is read again)
        self.assertEqual([c.tail for c in self.stand.calls].count("bot-options"), 2)

    async def test_a_list_of_maps_of_the_size_of_the_real_one_is_read_and_one_of_over_half_a_megabyte_is_not(self):
        rows = [{"file": "Map number %d with a long name.lvl" % n, "name": "Map number %d with a long name" % n, "group": "library", "width": 100, "height": 81, "players": 4, "places": 1, "minutes": 10, "random": True, "playable": True, "checked": True, "reason": ""} for n in range(1900)]
        self.assertGreater(len(json.dumps(rows)), 200000)                                             # (the real list is 84 KB for 541 maps: this is more than twice that)
        self.stand.routes["bot-options"] = (200, {"maps": rows})
        await self.say(self.admin, "/bots 1 map map number 1500 with a long name")
        self.assertEqual([c.payload["map"] for c in self.stand.calls if c.method == "POST"], ["Map number 1500 with a long name.lvl"])
        self.stand.routes["bot-options"] = (200, {"maps": rows[:5], "pad": "x" * 530000})
        self.w.clock.now += 400
        await self.say(self.admin, "/bots 1 map tiny")
        self.assertEqual(self.last(self.admin), {"t": "reply", "c": "bots", "r": "door"})

    async def test_a_list_of_maps_that_is_too_long_or_not_a_list_is_not_read(self):
        self.stand.routes["bot-options"] = (200, {"maps": [{"file": "m%d.lvl" % n, "playable": True} for n in range(2001)]})
        await self.say(self.admin, "/bots 1 map m5")
        self.assertEqual(self.last(self.admin), {"t": "reply", "c": "bots", "r": "door"})
        self.stand.routes["bot-options"] = (200, {"maps": "TINY.LVL"})
        self.w.clock.now += 20
        await self.say(self.admin, "/bots 1 map tiny")
        self.assertEqual(self.last(self.admin)["r"], "door")
        self.stand.routes["bot-options"] = (200, {"maps": [5, None, {"file": 3}, {"file": "a/b.lvl"}, {"file": "x\x01y.lvl"}, {"file": "z" * 97}, {"file": "Good.lvl"}]})
        self.w.clock.now += 20
        await self.say(self.admin, "/bots 1 map good")
        self.assertEqual([c.payload["map"] for c in self.stand.calls if c.method == "POST"], ["Good.lvl"])      # (a row that is not of the shape is left out, not trusted)

    async def test_every_call_has_a_pass_of_its_own_that_lives_a_minute_and_carries_five_members(self):
        for text in ("/bots 1 map tiny", "/bots status", "/bots 2", "/bots stop", "/bots status"):
            self.w.clock.now += 20
            await self.say(self.admin, text)
        self.assertGreaterEqual(len(self.stand.calls), 7)
        payloads = [c.passed for c in self.stand.calls]
        self.assertEqual(len({p["nonce"] for p in payloads}), len(payloads), "a nonce came twice")
        for payload in payloads:
            self.assertEqual(set(payload), {"exp", "name", "nonce", "ref", "role"})
            self.assertEqual((payload["name"], payload["ref"], payload["role"]), ("basil", self.w.chat.ref(ADMIN_ID), "admin"))
            self.assertLessEqual(payload["exp"] - int(self.w.wall.now), 60)
        self.assertEqual({c.method for c in self.stand.calls if c.headers.get("x-ants-csrf")}, {"POST"})

    async def test_the_operators_account_is_the_owner_in_the_pass_and_needs_no_lookup(self):
        self.install(self.stand.url, owner=OWNER_ID)
        owner = self.w.join(OWNER)
        self.w.roles.stale.add(OWNER_ID)
        self.w.roles.outcome = False
        self.w.roles.stale.discard(OWNER_ID)
        await self.say(owner, "/bots 2")
        self.assertEqual(self.last(owner)["r"], "started")
        self.assertEqual({c.passed["role"] for c in self.stand.calls}, {"owner"})
        self.assertEqual(self.w.roles.lookups, [])

    async def test_the_game_servers_refusals_are_told_in_its_own_few_words_and_nothing_is_logged(self):
        cases = (((400, {"error": "bad_count"}), {"r": "bad_count"}), ((400, {"error": "bad_spacing"}), {"r": "bad_every"}), ((409, {"error": "no_map"}), {"r": "no_map"}),
                 ((409, {"error": "map_187"}), {"r": "map_187"}), ((503, {"error": "no_replays"}), {"r": "no_replays"}),
                 ((409, {"error": "queue_full", "waiting": 9900, "asked": 500, "max": 10000, "secret": "x"}), {"r": "queue_full", "waiting": 9900, "asked": 500, "max": 10000}))
        for answer, expected in cases:
            self.stand.routes["bot-matches"] = answer
            self.w.clock.now += 20
            await self.say(self.admin, "/bots 500")
            self.assertEqual(self.last(self.admin), dict({"t": "reply", "c": "bots"}, **expected))
        for answer in ((409, {"error": "something_new"}), (500, {"error": "no_map"}), (403, {"error": "pass"}), (200, {"started": 1}), (200, {"started": "1", "asked": 1, "queued": 0}),
                       (200, {"started": 1, "asked": 1, "queued": True}), (200, {"started": -1, "asked": 1, "queued": 0}), (200, {"started": 10 ** 9, "asked": 1, "queued": 0})):
            self.stand.routes["bot-matches"] = answer
            self.w.clock.now += 20
            await self.say(self.admin, "/bots 500")
            self.assertEqual(self.last(self.admin), {"t": "reply", "c": "bots", "r": "door"}, answer)
        self.assertEqual(self.rows(), [])                                                         # (not one start was done)

    async def test_a_start_that_is_done_leaves_one_row_of_numbers_and_the_reference_and_never_a_name_a_map_or_an_id(self):
        await self.say(self.admin, "/bots 1000 every 120 map super jungle")
        self.w.clock.now += 20
        await self.say(self.admin, "/bots 7")
        self.w.clock.now += 20
        await self.say(self.admin, "/bots stop")
        ref = self.w.chat.ref(ADMIN_ID)
        self.assertEqual(self.rows()[::-1], [("bots_start", ref, "", "1000 every 7200s"), ("bots_start", ref, "", "7 every 0s"), ("bots_stop", ref, "", "stopped 2 dropped 5")])
        for row in self.rows():
            assert_no_id(self, repr(row), "a row of the moderators' log")
            self.assertNotIn("jungle", repr(row).lower())
            self.assertNotIn("basil", repr(row))
        self.assertEqual(self.last(self.admin), {"t": "reply", "c": "bots", "r": "stopped", "stopped": 2, "dropped": 5})
        await self.say(self.admin, "/bots status")                                                 # (a read leaves no row)
        self.assertEqual(len(self.rows()), 3)

    async def test_the_start_is_told_when_the_number_that_runs_at_once_is_not_known_and_the_answer_may_say_it(self):
        self.stand.routes["bot-matches/summary"] = (500, {"error": "x"})
        await self.say(self.admin, "/bots 4")
        self.assertEqual(self.last(self.admin), {"t": "reply", "c": "bots", "r": "started", "asked": 4, "started": 4, "queued": 0})      # (it is done: the answer does not wait for what is not asked of it)
        self.assertEqual(len(self.rows()), 1)
        self.stand.routes["bot-matches"] = (200, {"started": 0, "asked": 9, "queued": 9, "held": "busy", "at_once": 12, "every_s": 60, "extra": "no"})
        self.stand.calls.clear()
        self.w.clock.now += 20
        await self.say(self.admin, "/bots 9 every 1")
        self.assertEqual(self.last(self.admin), {"t": "reply", "c": "bots", "r": "started", "asked": 9, "started": 0, "queued": 9, "at_once": 12, "every_s": 60, "held": "busy"})
        self.assertEqual([c.tail for c in self.stand.calls], ["bot-matches"])
        self.stand.routes["bot-matches"] = (200, {"started": 1, "asked": 9, "queued": 8, "held": "nonsense"})
        self.w.clock.now += 20
        await self.say(self.admin, "/bots 9")
        self.assertNotIn("held", self.last(self.admin))

    async def test_status_tells_the_numbers_and_at_most_five_queues_with_no_text_that_is_not_plain(self):
        queue = {"id": "q1", "total": 1000, "remaining": 648, "finished": 312, "running": 40, "kept": 30, "every_s": 120, "next_in": 17, "held": "cap", "by": "basil", "x": 1}
        self.stand.routes["bot-matches/summary"] = (200, {"at_once": 80, "running": 40, "waiting": 648, "busy": 31, "gate": 40,
                                                          "queues": [dict(queue, id="q%d" % n, by="ba\x00sil" if n == 2 else ADMIN_ID if n == 3 else "schedule") for n in range(7)]})
        await self.say(self.admin, "/bots status")
        got = self.last(self.admin)
        self.assertEqual({k: got[k] for k in ("t", "c", "r", "at_once", "running", "waiting", "busy")}, {"t": "reply", "c": "bots", "r": "status", "at_once": 80, "running": 40, "waiting": 648, "busy": 31})
        self.assertEqual(len(got["queues"]), 5)
        self.assertEqual(got["queues"][0], {"total": 1000, "remaining": 648, "finished": 312, "running": 40, "kept": 30, "every_s": 120, "next_in": 17, "held": "cap", "by": "schedule"})
        self.assertEqual([q["by"] for q in got["queues"]], ["schedule", "schedule", "basil", "", "schedule"])
        self.stand.routes["bot-matches/summary"] = (200, {"at_once": 80, "running": 0, "waiting": 0, "busy": 0, "queues": [{"total": 5, "remaining": 5, "next_in": -1, "held": False, "by": "x"}]})
        self.w.clock.now += 20
        await self.say(self.admin, "/bots status")
        self.assertEqual(self.last(self.admin)["queues"], [{"total": 5, "remaining": 5, "finished": 0, "running": 0, "kept": 0, "every_s": 0, "next_in": -1, "held": "", "by": "x"}])
        self.stand.routes["bot-matches/summary"] = (200, {"at_once": 80, "running": 0, "waiting": 0, "busy": 0, "queues": "none"})
        self.w.clock.now += 20
        await self.say(self.admin, "/bots status")
        self.assertEqual(self.last(self.admin)["r"], "door")

    async def test_nothing_in_the_replies_the_log_or_the_traffic_holds_an_id(self):
        for text in ("/bots 5 map small", "/bots status", "/bots stop"):
            self.w.clock.now += 20
            await self.say(self.admin, text)
        for peer in (self.admin, self.mod, self.player, self.reader):
            assert_no_id(self, b"".join(peer.raw).decode("utf-8"), "a frame")
        assert_no_id(self, repr(self.w.store.actions()), "the moderators' log")
        for call in self.stand.calls:
            assert_no_id(self, json.dumps(call.passed) + call.raw.decode("latin-1"), "a call")


class TheDoorFails(DoorTest):
    async def asyncSetUp(self):
        await super().asyncSetUp()
        self.errors = []
        self.w.chat.on_command_error = self.errors.append

    async def check_door_reply(self, text="/bots 5"):
        self.w.clock.now += 20
        started = time.monotonic()
        await self.say(self.admin, text)
        self.assertLess(time.monotonic() - started, 4.0, "it hung")
        self.assertEqual(self.last(self.admin), {"t": "reply", "c": "bots", "r": "door"}, text)
        self.assertEqual(self.rows(), [])
        self.assertEqual((self.admin.codes(), self.admin.strikes), ([], 0))

    async def test_a_door_that_does_not_answer_or_answers_what_is_not_an_answer_is_the_reply_door(self):
        for mode in ("down", "garbage", "list", "big", "redirect", "chunked_garbage", "short", "status_line", "double_length"):
            self.stand.mode = mode
            await self.check_door_reply()
            await self.check_door_reply("/bots status")
            await self.check_door_reply("/bots stop")
            await self.check_door_reply("/bots 5 map tiny")
        self.assertEqual(self.errors, [])                                                          # (no bug: every failure is the kind that the door knows)

    async def test_a_door_that_hangs_is_given_up_at_the_limit_and_the_account_may_try_again(self):
        self.stand.mode = "hang"
        await self.check_door_reply()
        self.stand.mode = "ok"
        self.w.clock.now += 20
        await self.say(self.admin, "/bots 5")
        self.assertEqual(self.last(self.admin)["r"], "started")

    async def test_a_door_that_is_not_there_at_all_is_the_reply_door(self):
        self.stand.stop()
        await self.check_door_reply()
        self.stand.server = type("Closed", (), {"shutdown": lambda self: None, "server_close": lambda self: None})()          # (the cleanup stops it again)

    async def test_a_slow_door_that_answers_in_time_is_waited_for(self):
        self.stand.delay = 0.3
        await self.say(self.admin, "/bots 5")
        self.assertEqual(self.last(self.admin)["r"], "started")

    async def test_no_setting_or_no_key_is_off_and_nothing_is_sent(self):
        self.install("")
        await self.say(self.admin, "/bots 5")
        self.assertEqual(self.last(self.admin), {"t": "reply", "c": "bots", "r": "off"})
        self.install(self.stand.url, secret="")
        self.w.clock.now += 20
        await self.say(self.admin, "/bots status")
        self.assertEqual(self.last(self.admin), {"t": "reply", "c": "bots", "r": "off"})
        self.w.chat.commands.door = None
        self.w.clock.now += 20
        await self.say(self.admin, "/bots stop")
        self.assertEqual(self.last(self.admin)["r"], "off")
        self.assertEqual(self.stand.calls, [])

    async def test_an_error_in_the_parse_is_told_before_the_door_is_looked_at(self):
        self.install("")
        await self.say(self.admin, "/bots 0")
        await self.say(self.admin, "/bots 5 every 121")
        self.assertEqual([m["r"] for m in replies(self.admin)], ["bad_count", "bad_every"])

    async def test_a_role_that_cannot_be_confirmed_is_role_and_nothing_is_sent(self):
        self.w.roles.stale.add(ADMIN_ID)
        self.w.roles.outcome = False
        await self.say(self.admin, "/bots 5")
        self.assertEqual(self.last(self.admin), {"t": "reply", "c": "bots", "r": "role"})
        self.assertEqual((self.stand.calls, self.w.roles.lookups, self.rows()), ([], [[ADMIN_ID]], []))
        self.w.roles.outcome = True                                                                # (the lookup answers: the role is confirmed, and the command goes)
        self.w.clock.now += 20
        await self.say(self.admin, "/bots 5")
        self.assertEqual(self.last(self.admin)["r"], "started")

    async def test_a_role_that_is_taken_away_is_refused_at_once_and_by_the_fresh_answer_too(self):
        self.w.roles.set(ADMIN_ID, "none")
        await self.say(self.admin, "/bots 5")
        self.assertEqual(replies(self.admin), [UNKNOWN])
        self.w.roles.set(ADMIN_ID, "mod")
        self.w.clock.now += 20
        await self.say(self.admin, "/bots status")
        self.assertEqual(replies(self.admin), [UNKNOWN, UNKNOWN])
        self.w.roles.set(ADMIN_ID, "admin")
        self.w.roles.stale.add(ADMIN_ID)

        async def lookup_says_none(accounts):
            self.w.roles.roles[ADMIN_ID] = "none"
            self.w.roles.stale.clear()
            return True
        self.w.roles.confirm = lookup_says_none                                                    # (the answer that is asked for says that the role is gone)
        self.w.clock.now += 20
        await self.say(self.admin, "/bots 5")
        self.assertEqual(self.last(self.admin), {"t": "reply", "c": "bots", "r": "role"})
        self.assertEqual((self.stand.calls, self.rows()), ([], []))

    async def test_a_moderator_a_player_and_a_reader_reach_nothing_of_the_door(self):
        for peer in (self.mod, self.player):
            await self.say(peer, "/bots 5")
            await self.say(peer, "/bots status")
        self.assertEqual(replies(self.mod) + replies(self.player), [UNKNOWN] * 4)
        self.w.say(self.reader, "/bots 5")
        self.assertEqual(self.reader.codes(), ["login"])
        self.assertEqual(self.stand.calls, [])

    async def test_one_call_at_a_time_for_an_account_and_two_for_the_hub(self):
        self.stand.delay = 0.4
        second = self.w.join(ADMIN)                                                                # (another tab of the same account)
        self.w.say(self.admin, "/bots 5")
        self.w.say(second, "/bots status")
        self.assertEqual(replies(second), [{"t": "reply", "c": "bots", "r": "rate", "wait": 1}])
        other = [self.w.join(session.Identity("7311000099990%03d" % n, "admin%d" % n, False)) for n in range(20, 22)]
        for n in range(20, 22):
            self.w.roles.roles["7311000099990%03d" % n] = "admin"
        self.w.say(other[0], "/bots status")                                                       # (the second call of the hub: the first one is the account's own)
        self.assertEqual(replies(other[0]), [])
        self.w.say(other[1], "/bots status")
        self.assertEqual(replies(other[1]), [{"t": "reply", "c": "bots", "r": "rate", "wait": 1}])      # (the hub has two on their way)
        await self.settle()
        self.assertEqual(len(replies(self.admin)), 1)
        self.w.clock.now += 20
        self.w.say(second, "/bots status")
        await self.settle()
        self.assertEqual(replies(second)[-1]["r"], "status")

    async def test_a_command_that_waits_for_a_lookup_spends_the_share_of_the_commands_that_wait(self):
        limits = dataclasses.replace(FAST, roles_urgent_actor_burst=1, roles_urgent_actor_refill_seconds=1000.0)
        self.limits = limits
        self.w.chat.wait_limits = limitmod.RateByAccount(1, 1000.0)
        self.install(self.stand.url)
        self.w.roles.stale.add(ADMIN_ID)
        await self.say(self.admin, "/bots status")
        self.assertEqual(self.last(self.admin)["r"], "status")
        self.w.roles.stale.add(ADMIN_ID)
        self.w.clock.now += 20
        self.w.say(self.admin, "/bots status")
        self.assertEqual(self.last(self.admin)["r"], "rate")
        self.assertGreaterEqual(self.last(self.admin)["wait"], 1)

    async def test_a_bug_in_the_task_is_the_reply_door_and_only_its_class_is_reported(self):
        async def breaks(who):
            raise RuntimeError("the id " + ADMIN_ID)
        self.w.chat.commands.door.summary = breaks
        await self.say(self.admin, "/bots status")
        self.assertEqual(self.last(self.admin), {"t": "reply", "c": "bots", "r": "door"})
        self.assertEqual([type(e).__name__ for e in self.errors], ["RuntimeError"])
        assert_no_id(self, b"".join(self.admin.raw).decode("utf-8"), "a frame")

    async def test_a_socket_that_went_while_the_game_server_was_asked_has_nothing_done(self):
        self.stand.delay = 0.3
        self.w.say(self.admin, "/bots 1 map tiny")
        self.admin.closed = (1001, "gone")
        await self.settle()
        self.assertEqual([c.tail for c in self.stand.calls], ["bot-options"])                    # (the start was not made)
        self.assertEqual(self.rows(), [])

    async def test_an_account_that_was_banned_while_it_waited_has_nothing_done(self):
        self.stand.delay = 0.3
        self.w.say(self.admin, "/bots 1 map tiny")
        self.w.store.ban(ADMIN_ID, "x", "x")
        await self.settle()
        self.assertEqual((self.admin.codes(), [c.tail for c in self.stand.calls], self.rows()), (["banned"], ["bot-options"], [ (a, b, c, d) for a, b, c, d in self.rows() if a == "ban"]))
        self.assertEqual([r for r in self.rows() if r[0].startswith("bots")], [])


class TheLimits(DoorTest):
    async def test_a_start_is_three_at_once_then_one_in_ten_seconds_for_the_account_across_its_sockets(self):
        second = self.w.join(ADMIN)
        peers = [self.admin, second, self.admin, second]
        for n, peer in enumerate(peers):
            self.w.say(peer, "/bots abc")                                                          # (a line that is wrong costs a token too)
        got = [m["r"] for p in (self.admin, second) for m in replies(p)]
        self.assertEqual(sorted(got), ["bad_count"] * 3 + ["rate"])
        rate = [m for p in (self.admin, second) for m in replies(p) if m["r"] == "rate"][0]
        self.assertEqual((rate["c"], 1 <= rate["wait"] <= 10), ("bots", True))
        self.w.clock.now += 9
        self.w.say(self.admin, "/bots abc")
        self.assertEqual(replies(self.admin)[-1]["r"], "rate")
        self.w.clock.now += 1.5
        self.w.say(second, "/bots abc")
        self.assertEqual(replies(second)[-1]["r"], "bad_count")

    async def test_status_and_stop_are_five_at_once_then_one_in_two_seconds_and_a_start_has_its_own_tokens(self):
        for n in range(5):
            self.w.say(self.admin, "/bots status extra")
        self.assertEqual([m["r"] for m in replies(self.admin)], ["usage"] * 5)
        self.w.say(self.admin, "/bots stop extra")
        self.assertEqual(replies(self.admin)[-1]["r"], "rate")
        self.w.say(self.admin, "/bots abc")                                                        # (the start's bucket is full)
        self.assertEqual(replies(self.admin)[-1]["r"], "bad_count")
        self.w.clock.now += 2
        self.w.say(self.admin, "/bots stop extra")
        self.assertEqual(replies(self.admin)[-1]["r"], "usage")

    async def test_the_other_commands_are_ten_at_once_then_one_in_two_seconds_and_an_account_has_its_own(self):
        for n in range(10):
            self.w.say(self.player, "/ignore nobody")
        self.assertEqual([m["r"] for m in replies(self.player)], ["nobody"] * 10)
        self.w.say(self.player, "/unignore nobody")
        self.assertEqual(replies(self.player)[-1], dict({"t": "reply", "c": "unignore", "r": "rate"}, wait=replies(self.player)[-1]["wait"]))
        self.w.say(self.mod, "/ignore nobody")                                                     # (another account: its own bucket)
        self.assertEqual(replies(self.mod)[-1]["r"], "nobody")
        self.w.clock.now += 2
        self.w.say(self.player, "/ignore nobody")
        self.assertEqual(replies(self.player)[-1]["r"], "nobody")

    async def test_a_command_that_is_over_the_limit_costs_the_socket_a_frame_and_is_no_mistake(self):
        for n in range(15):
            self.w.say(self.player, "/help")
        self.assertEqual((self.player.strikes, self.player.charges, self.player.codes()), (0, 5, []))

    async def test_the_moderators_commands_keep_the_limits_they_had(self):
        for n in range(30):
            self.w.say(self.admin, "/mutes")
        self.assertEqual((len(self.admin.kinds("mutes")), self.admin.codes(), replies(self.admin)), (30, [], []))

    async def test_forgetting_idle_buckets_drops_the_full_ones(self):
        self.w.say(self.player, "/help")
        self.w.chat.commands.forget_idle(self.w.clock.now + 100000)
        self.assertEqual({len(b.buckets) for b in self.w.chat.commands.limits.values()}, {0})


# ---- /ignore -------------------------------------------------------------------------------------------------------------------------------------------------

class TheIgnore(unittest.TestCase):
    def setUp(self):
        self.w = world()
        self.addCleanup(self.w.close)
        self.me, self.me_tab, self.other, self.tree, self.mod, self.admin, self.reader = (self.w.join(i) for i in (PLAYER, PLAYER, OTHER, session.Identity("7311000099990011", "fir.tree", False), MOD, ADMIN, None))
        self.w.chat.roles.roles["7311000099990011"] = "none"

    def say(self, peer, text):
        self.w.clock.now += 3
        self.w.say(peer, text)

    def texts(self, peer):
        return [m["text"] for m in peer.kinds("line")]

    def test_the_lines_of_an_ignored_person_are_sent_to_no_socket_of_the_account_and_to_nobody_else_is_anything_changed(self):
        self.say(self.me, "/ignore pine.cone")
        self.assertEqual(replies(self.me), [{"t": "reply", "c": "ignore", "r": "ok", "n": 1}])
        before = list(self.other.raw)
        self.say(self.other, "from pine.cone")
        self.say(self.mod, "from sage")
        self.say(self.me, "from me")
        self.assertEqual((self.texts(self.me), self.texts(self.me_tab)), (["from sage", "from me"], ["from sage", "from me"]))        # (every tab of the account)
        self.assertEqual(self.texts(self.reader), ["from pine.cone", "from sage", "from me"])
        self.assertEqual(self.texts(self.mod), ["from pine.cone", "from sage", "from me"])
        self.assertEqual(self.texts(self.other), ["from pine.cone", "from sage", "from me"])         # (the ignored person gets every line, their own too)
        self.assertEqual(replies(self.other) + replies(self.reader), [])                          # (and is told nothing)
        self.assertEqual([m for m in self.other.sent if m["t"] != "line"], [])
        self.assertEqual(len(self.other.raw), len(before) + 3)

    def test_the_welcome_leaves_out_the_lines_of_a_person_that_the_account_ignores(self):
        self.say(self.other, "early line of pine.cone")
        self.say(self.mod, "early line of sage")
        self.say(self.me, "/ignore pine.cone")
        for who, expect in ((PLAYER, ["early line of sage"]), (OTHER, ["early line of pine.cone", "early line of sage"]), (None, ["early line of pine.cone", "early line of sage"])):
            history = self.w.chat.welcome(FakePeer(who))["history"]
            self.assertEqual([h["text"] for h in history], expect, who)
        self.say(self.me, "/unignore pine.cone")
        self.assertEqual([h["text"] for h in self.w.chat.welcome(FakePeer(PLAYER))["history"]], ["early line of pine.cone", "early line of sage"])

    def test_a_deletion_still_goes_to_everybody_and_the_hubs_own_lines_are_never_left_out(self):
        self.say(self.me, "/ignore pine.cone")
        self.say(self.other, "to be deleted")
        self.say(self.mod, "/del 1")
        self.assertEqual([m["id"] for m in self.me.kinds("del")], [1])
        self.assertEqual([m["id"] for m in self.reader.kinds("del")], [1])
        self.say(self.mod, "/kick pine.cone")
        self.assertEqual([m["text"] for m in self.me.kinds("line") if m.get("sys")], ["pine.cone was kicked."])

    def test_ambiguous_nobody_self_and_staff(self):
        self.w.chat.peers.discard(self.me_tab)
        twin = self.w.join(session.Identity("7311000099990012", "pine.cone", False))               # (two accounts that show one name)
        self.say(self.me, "/ignore Pine.Cone")
        self.say(self.me, "/ignore nobody.here")
        self.say(self.me, "/ignore maple.leaf")
        self.say(self.me, "/ignore sage")
        self.say(self.me, "/ignore basil")
        self.say(self.me, "/ignore")
        self.say(self.me, "/ignore " + "x" * 33)
        self.assertEqual([m["r"] for m in replies(self.me)], ["ambiguous", "nobody", "self", "staff", "staff", "usage", "nobody"])
        self.assertEqual(self.w.chat.commands.ignores, {})
        self.assertIn(twin, self.w.chat.peers)

    def test_the_name_is_the_name_that_is_shown_and_never_a_username(self):
        shown = session.Identity("7311000099990013", "Maple Leaf", False, None, 0, "aaa.one", "display", "aaa.one")
        peer = self.w.join(shown)
        self.say(self.me, "/ignore aaa.one")
        self.assertEqual(replies(self.me)[-1]["r"], "nobody")
        self.say(self.me, "/ignore maple   LEAF")
        self.assertEqual(replies(self.me)[-1], {"t": "reply", "c": "ignore", "r": "ok", "n": 1})
        self.say(peer, "hello")
        self.assertEqual(self.texts(self.me), [])

    def test_a_person_who_left_but_wrote_a_line_that_is_still_in_the_chat_can_be_ignored(self):
        self.say(self.other, "i was here")
        self.w.chat.peers.discard(self.other)
        self.say(self.me, "/ignore pine.cone")
        self.assertEqual(replies(self.me)[-1]["r"], "ok")
        self.assertEqual([h["text"] for h in self.w.chat.welcome(FakePeer(PLAYER))["history"]], [])
        self.say(self.me, "/ignore fir.tree")                                                      # (a person who is here and has said nothing)
        self.assertEqual(replies(self.me)[-1], {"t": "reply", "c": "ignore", "r": "ok", "n": 2})

    def test_a_person_who_holds_a_role_now_or_held_one_when_they_wrote_is_never_left_out(self):
        self.say(self.other, "written as a person")
        self.say(self.me, "/ignore pine.cone")
        self.w.chat.roles.roles[OTHER_ID] = "mod"                                                  # (promoted after they were ignored)
        self.say(self.other, "written as a moderator")
        self.assertEqual(self.texts(self.me), ["written as a person", "written as a moderator"])         # (the first came before the account ignored them)
        self.assertEqual([h["text"] for h in self.w.chat.welcome(FakePeer(PLAYER))["history"]], ["written as a person", "written as a moderator"])      # (what they wrote before is shown now too)
        self.w.chat.roles.roles[OTHER_ID] = "none"                                                 # (and demoted again: the line that they wrote as a moderator stays)
        self.assertEqual([h["text"] for h in self.w.chat.welcome(FakePeer(PLAYER))["history"]], ["written as a moderator"])
        self.say(self.other, "written as a person again")
        self.assertEqual(self.texts(self.me), ["written as a person", "written as a moderator"])         # (and this one is left out again)

    def test_the_list_holds_fifty_and_the_fifty_first_is_full(self):
        for n in range(50):
            ident = session.Identity("73110000999%05d" % (100 + n), "person.%02d" % n, False)
            self.w.join(ident)
            self.w.clock.now += 3
            self.w.say(self.me, "/ignore person.%02d" % n)
        self.assertEqual([m["r"] for m in replies(self.me)], ["ok"] * 50)
        self.assertEqual(replies(self.me)[-1]["n"], 50)
        self.say(self.me, "/ignore pine.cone")
        self.assertEqual(replies(self.me)[-1], {"t": "reply", "c": "ignore", "r": "full"})
        self.say(self.me, "/ignore person.03")                                                     # (one that is on the list is no more: n stays)
        self.assertEqual(replies(self.me)[-1], {"t": "reply", "c": "ignore", "r": "ok", "n": 50})
        self.say(self.me, "/unignore person.03")
        self.say(self.me, "/ignore pine.cone")
        self.assertEqual(replies(self.me)[-1], {"t": "reply", "c": "ignore", "r": "ok", "n": 50})

    def test_unignore_by_the_name_that_was_stored_in_any_case_and_not_for_one_that_is_not_there(self):
        self.say(self.me, "/ignore pine.cone")
        self.say(self.me, "/unignore fir.tree")
        self.say(self.me, "/unignore PINE.cone")
        self.say(self.me, "/unignore pine.cone")
        self.say(self.me, "/unignore")
        self.assertEqual([(m["r"], m.get("n")) for m in replies(self.me)[1:]], [("not", None), ("ok", 0), ("not", None), ("usage", None)])
        self.assertEqual(self.w.chat.commands.ignores, {})
        self.say(self.other, "visible again")
        self.assertEqual(self.texts(self.me), ["visible again"])

    def test_a_name_that_was_changed_since_is_unignored_by_the_name_stored(self):
        self.say(self.me, "/ignore pine.cone")
        renamed = session.Identity(OTHER_ID, "Evergreen", False)
        self.other.identity = renamed
        self.say(self.other, "under a new name")
        self.assertEqual(self.texts(self.me), [])                                                  # (the account is the same account, whatever the name)
        self.say(self.me, "/unignore evergreen")
        self.assertEqual(replies(self.me)[-1]["r"], "not")
        self.say(self.me, "/unignore pine.cone")
        self.assertEqual(replies(self.me)[-1]["r"], "ok")

    def test_an_account_that_forgets_itself_loses_its_list_and_its_place_on_the_lists_of_the_others(self):
        self.say(self.me, "/ignore pine.cone")
        self.say(self.other, "/ignore maple.leaf")
        self.w.chat.commands.forget(PLAYER_ID)
        self.assertEqual(self.w.chat.commands.ignores, {self.w.chat.ref(OTHER_ID): {}} if False else {})
        self.say(self.other, "after the forget")
        self.assertEqual(self.texts(self.me), ["after the forget"])
        self.say(self.me, "from the forgotten")
        self.assertEqual(self.texts(self.other)[-1], "from the forgotten")
        self.say(self.me, "/ignore pine.cone")
        self.say(self.other, "/ignore maple.leaf")
        self.w.chat.commands.forget(OTHER_ID)                                                      # (the other way round: the one that was ignored forgets)
        self.assertEqual(list(self.w.chat.commands.ignores), [self.w.chat.ref(PLAYER_ID)] if False else [])

    def test_nothing_is_kept_by_the_id_and_nothing_about_it_is_written(self):
        self.say(self.me, "/ignore pine.cone")
        assert_no_id(self, repr(self.w.chat.commands.ignores), "the list")
        self.assertEqual(self.w.chat.commands.ignores, {self.w.chat.ref(PLAYER_ID): {self.w.chat.ref(OTHER_ID): "pine.cone"}})
        self.assertEqual(self.w.store.actions(), [])
        assert_no_id(self, b"".join(self.me.raw).decode("utf-8"), "a frame")

    def test_a_banned_person_ignored_and_a_reader_who_cannot_ignore(self):
        self.w.say(self.reader, "/ignore pine.cone")
        self.assertEqual(self.reader.codes(), ["login"])


# ---- the moderators' commands through the table -------------------------------------------------------------------------------------------------------------

class TheStaffCommands(unittest.TestCase):
    def setUp(self):
        self.w = world()
        self.addCleanup(self.w.close)
        self.admin, self.mod, self.player, self.other = (self.w.join(i) for i in (ADMIN, MOD, PLAYER, OTHER))
        for ident in (PLAYER, OTHER, MOD, ADMIN):
            self.w.store.set_profile(ident.account_id, ident.username, "", 100)

    def say(self, peer, text):
        self.w.clock.now += 3
        self.w.say(peer, text)

    def test_nopic_blocks_the_picture_and_pic_lifts_the_block_and_each_is_in_the_log(self):
        self.say(self.admin, "/nopic maple.leaf")
        self.assertEqual([m["c"] for m in self.admin.kinds("ok")], ["nopic"])
        self.assertEqual(self.w.store.picture_state(PLAYER_ID), storebase.SHOW_BLOCKED)
        self.say(self.admin, "/nopic maple.leaf")
        self.assertEqual(self.admin.codes(), ["bad"])                                              # (blocked already)
        self.say(self.admin, "/pic maple.leaf")
        self.assertEqual([m["c"] for m in self.admin.kinds("ok")], ["nopic", "pic"])
        self.assertEqual(self.w.store.picture_state(PLAYER_ID), storebase.SHOW_OFF)                # (a lift leaves the picture off until the person turns it on)
        self.say(self.admin, "/pic maple.leaf")
        self.assertEqual(self.admin.codes(), ["bad", "bad"])                                       # (not blocked)
        ref = self.w.chat.ref(ADMIN_ID)
        self.assertEqual([(r.actor, r.action, r.target) for r in self.w.store.actions()][::-1], [(ref, "nopic", self.w.chat.ref(PLAYER_ID)), (ref, "pic", self.w.chat.ref(PLAYER_ID))])
        assert_no_id(self, repr(self.w.store.actions()) + b"".join(self.admin.raw).decode("utf-8"), "the log and the frames")

    def test_a_moderator_and_a_player_have_no_nopic_and_no_pic_and_nobody_blocks_an_administrator_or_themselves(self):
        for peer in (self.mod, self.player):
            self.say(peer, "/nopic pine.cone")
            self.say(peer, "/pic pine.cone")
        self.assertEqual(replies(self.mod) + replies(self.player), [UNKNOWN] * 4)
        self.say(self.admin, "/nopic basil")
        self.say(self.admin, "/nopic nobody.here")
        self.say(self.admin, "/nopic")
        self.assertEqual(self.admin.codes(), ["power", "who", "bad"])
        self.assertEqual(self.w.store.picture_marks(), {})
        owner = self.w.join(OWNER)
        self.w.store.set_profile(OWNER_ID, "the.boss", "", 100)
        self.say(self.admin, "/nopic the.boss")
        self.assertEqual(self.admin.codes()[-1], "power")

    def test_the_old_words_are_the_moderators_in_any_case_and_a_malformed_line_is_still_bad(self):
        self.say(self.mod, "/MUTE pine.cone 5")
        self.say(self.mod, "/mute pine.cone")
        self.assertEqual(([m["c"] for m in self.mod.kinds("ok")], self.mod.codes()), (["mute"], ["bad"]))
        self.assertEqual(replies(self.mod), [])


# ---- the moderators' log of a start ---------------------------------------------------------------------------------------------------------------------------

class TheLogRows(unittest.TestCase):
    REF = "0123456789abcdef"

    def test_the_rows_of_a_start_and_a_stop_are_numbers_in_one_form_and_nothing_else_gets_in(self):
        for action, detail in (("bots_start", "1 every 0s"), ("bots_start", "10000 every 7200s"), ("bots_start", "999999 every 99999s"), ("bots_stop", "stopped 0 dropped 0"), ("bots_stop", "stopped 80 dropped 10000")):
            self.assertEqual(storebase.check_note((self.REF, action, "", detail)), (self.REF, action, "", detail))
        for action, detail in (("bots_start", "7311000099990001 every 0s"), ("bots_start", "1000000 every 0s"), ("bots_start", "5 every 100000s"), ("bots_start", "5"), ("bots_start", ""), ("bots_start", "basil"),
                               ("bots_start", "5 every 1s "), ("bots_start", "5 every 1s\n"), ("bots_start", "5 every 1s map tiny"), ("bots_start", "-5 every 1s"), ("bots_start", "stopped 1 dropped 1"),
                               ("bots_stop", "5 every 1s"), ("bots_stop", "stopped 1000000 dropped 1"), ("bots_stop", "stopped 1 dropped 7311000099990001"), ("bots_stop", "stopped 1"), ("bots_stop", "")):
            with self.assertRaises(ValueError, msg=(action, detail)):
                storebase.check_note((self.REF, action, "", detail))
        with self.assertRaises(ValueError):
            storebase.check_note(("73110000999900012", "bots_start", "", "5 every 1s"))               # (the actor is a reference, never an id)
        with self.assertRaises(ValueError):
            storebase.check_note((self.REF, "bots_start", "73110000999900012", "5 every 1s"))
        self.assertEqual(storebase.check_note((self.REF, "mute", "", "5")), (self.REF, "mute", "", "5"))        # (the old forms are as they were)
        with self.assertRaises(ValueError):
            storebase.check_note((self.REF, "mute", "", "5 every 1s"))


# ---- real sockets ------------------------------------------------------------------------------------------------------------------------------------------------

class TheLiveHub(unittest.TestCase):
    def setUp(self):
        self.stand = fgd.FakeGameDoor()
        self.addCleanup(self.stand.stop)
        self.clients = []
        self.addCleanup(lambda: [c.close() for c in self.clients])

    def connect(self, rig, ident):
        client = hc.Client(rig.port, login="%s:%s" % (ident.account_id, ident.username) if ident else None)
        self.clients.append(client)
        self.assertEqual(client.recv_json()["t"], "welcome")
        if ident:
            self.assertEqual(client.recv_json()["t"], "cmds")
        return client

    def rig(self, **kwargs):
        env = {"ANTS_HUB_GAME_ADMIN_URL": self.stand.url, "ANTS_ADMIN_GATE_SECRET": fgd.SECRET}
        return hub_rig.Rig(env=env, **kwargs)

    def answer(self, client, kind="reply", seconds=8.0):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            for m in client.drain(0.1):
                if m["t"] == kind:
                    return m
        self.fail("no %s" % kind)

    def test_the_answer_is_the_asking_sockets_alone_and_no_id_or_pass_is_in_the_log_or_the_traffic(self):
        roles = hub_rig.FakeRoles({ADMIN_ID: "admin"})
        with self.rig(roles=roles) as rig:
            first, second = self.connect(rig, ADMIN), self.connect(rig, ADMIN)
            player, reader = self.connect(rig, PLAYER), self.connect(rig, None)
            first.say("/bots 12 every 3 map small")
            got = self.answer(first)
            self.assertEqual(got, {"t": "reply", "c": "bots", "r": "started", "asked": 12, "started": 12, "queued": 0, "at_once": 80, "every_s": 180})
            for other in (second, player, reader):
                self.assertEqual([m for m in other.drain(0.4) if m["t"] in ("reply", "line", "err")], [], "an answer or a line went to somebody else")
            first.say("/bots stop")
            self.assertEqual(self.answer(first)["r"], "stopped")
            log = rig.log_text.getvalue()
            rows = rig.call(lambda: [(r.action, r.detail) for r in rig.store.actions()])
        self.assertEqual(rows[::-1], [("bots_start", "12 every 180s"), ("bots_stop", "stopped 2 dropped 5")])
        self.assertEqual(self.stand.problems, [])
        self.assertEqual(len(self.stand.calls), 4)
        assert_no_id(self, log, "the hub's log")
        assert_no_id(self, repr([(c.raw, c.passed) for c in self.stand.calls]), "what the game server was sent")
        for call in self.stand.calls:
            self.assertNotIn(call.headers["x-ants-admin-pass"], log)
            self.assertNotIn(fgd.SECRET, log)

    def test_the_operators_account_works_with_no_discord_and_a_player_reaches_nothing(self):
        with self.rig(owner=OWNER_ID) as rig:
            owner, player = self.connect(rig, OWNER), self.connect(rig, PLAYER)
            owner.say("/bots status")
            self.assertEqual(self.answer(owner)["r"], "status")
            player.say("/bots status")
            self.assertEqual(self.answer(player), UNKNOWN)
            self.assertEqual({c.passed["role"] for c in self.stand.calls}, {"owner"})
            self.assertEqual(self.stand.problems, [])

    def test_a_hub_without_the_setting_says_off_and_a_stopped_hub_leaves_no_call_behind(self):
        roles = hub_rig.FakeRoles({ADMIN_ID: "admin"})
        with hub_rig.Rig(roles=roles) as rig:
            admin = self.connect(rig, ADMIN)
            admin.say("/bots 5")
            self.assertEqual(self.answer(admin), {"t": "reply", "c": "bots", "r": "off"})
        self.assertEqual(self.stand.calls, [])
        self.stand.delay = 1.0
        roles = hub_rig.FakeRoles({ADMIN_ID: "admin"})
        with self.rig(roles=roles) as rig:
            admin = self.connect(rig, ADMIN)
            admin.say("/bots 5")
            time.sleep(0.2)
        self.assertEqual(rig.hub.chat.commands.flights, {})                                        # (the hub's stop ended the call that waited)


if __name__ == "__main__":
    unittest.main()
