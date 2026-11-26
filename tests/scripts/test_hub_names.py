#!/usr/bin/env python3
"""Nicknames, tags, guests' names and the pass for the game server (hub/nickrules.py, hub/nicknames.py, hub/namepass.py, hub/store/ nicknames, hub/chat.py, hub/login.py `/hub/me`; docs/HUB_NICKNAMES.md; run by
./run_tests.sh --fast and by the CI). Every id and name here is made up.

  TheRules        what a nickname, a tag and a guest's name may be (one alphabet, the letters of the font, length, signs, reserved words, staff badges, rude words, labels of computer players) and the word that says why not
  TheBotLabels    the Python twin of the rule that keeps a person from being called like a computer player answers tests/data/bot_label_cases.tsv, the table of the C++ and the JavaScript twins
  TheStore        the table of nicknames: one owner for a lookalike key, a name that is given up is held, a drop, Forget me
  TheFrames       the `name` frame: a check, a save (both fields or none), every refusal with its word and its time, the rates, a guest's check, the shape of a frame, the `you` that every socket of the account gets
  TheLimits       a nickname changes once in 30 days (the old one is held for 30 days), a tag once a day, and what a change that is refused leaves
  TheShownName    the nickname is the name of the chat, with the tag in the line, the list of people and the history; a login's name that is a lookalike of a nickname is not shown
  ThePass         the pass of a name: its words (the same as the game server's: a golden pass is in tests/test_server/test_admin_pass.cpp, ADM1.8), the game name of an account, `/hub/me`
  TheSecrets      no id, reference or username in a frame of a name

tests/data/hub_names_mutants.json breaks the rules one at a time for tools/mutate.py (every one must be caught).
"""
import json
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
from test_hub_core import World  # noqa: E402
from hub import commands as commandsmod, namepass, name_letters, nickrules, session, text  # noqa: E402

ANNA_ID, BEN_ID, CLEO_ID = "8421000099990001", "8421000099990002", "8421000099990003"
ANNA, BEN, CLEO = (session.Identity(i, n, False) for i, n in ((ANNA_ID, "anna"), (BEN_ID, "ben"), (CLEO_ID, "cleo")))
SECRET = "name-pass-test-secret-7f3a9c1e5b2d4086-q"
GOLDEN = {"[\u00c4NT] Zo\u00eb": "1700000000.58ab4380a2403bd62cf97b71020684ad", "Wren": "1700000000.a5d465711484ee0192b99f6c66d3f172", "\u0401\u0436\u0438\u043a": "1700000000.a847a6f3c846156fcbe82c2943c6c912"}
DAY = 86400


def label_cases():
    """The `reserved` rows of tests/data/bot_label_cases.tsv: (text, whether a person may not be called so)."""
    rows = []
    with open(os.path.join(REPO, "tests", "data", "bot_label_cases.tsv"), encoding="utf-8") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            fields = line.rstrip("\n").split("\t")
            if fields[0] != "reserved":
                continue

            def unescape(cell):
                if cell == "<empty>":
                    return ""
                out, i = [], 0
                while i < len(cell):
                    if cell[i:i + 2] == "\\x":
                        out.append(chr(int(cell[i + 2:i + 4], 16)))
                        i += 4
                    else:
                        out.append(cell[i])
                        i += 1
                return "".join(out)
            rows.append((unescape(fields[1]), fields[2] == "1"))
    return rows


class TheRules(unittest.TestCase):
    def nick(self, raw):
        return nickrules.check_nickname(raw)

    def test_a_nickname_is_what_the_font_draws_in_one_alphabet(self):
        for good in ("Zo\u00eb", "\u0401\u0436\u0438\u043a", "Jean-Luc", "O'Brien", "a1b", "Wolfgang Amadeus", "Nguy\u1ec5n \u0110\u1eb7ng", "\u0218tefan", "Mary Jane 2", "x.y", "\u0426\u0430\u0440\u044c_77"):
            self.assertEqual(self.nick(good), (good, None), good)
        self.assertEqual(self.nick("Zoe\u0308"), ("Zo\u00eb", None))                                  # (NFC: the letter and its mark are the one letter)
        self.assertEqual(self.nick("  Mary \t  Jane  "), ("Mary Jane", None))                      # (every blank is one space, the ends go)
        self.assertEqual(self.nick("O\u2019Brien"), ("O'Brien", None))                              # (a phone's curly apostrophe is the plain one)
        self.assertEqual(self.nick("Jean\u2013Luc"), ("Jean-Luc", None))

    def test_every_refusal_has_its_word(self):
        cases = {"": "empty", "   ": "empty", "ab": "short", "x" * 17: "long", "x" * 65: "long", "Wolfgang Amadeus!": "chars", "a@b": "chars", "\U0001f600smile": "chars", "name\u0301\u0301s": "chars",
                 "\u0391\u03bb\u03ad\u03be\u03b7\u03c2": "script", "\u9f8d\u9f8d\u9f8d": "script", "\u05e9\u05dc\u05d5\u05dd": "script", "Zo\u0435": "alphabets", "\u0401\u0436ik": "alphabets",
                 "_ab": "chars", "ab-": "chars", "a..b": "chars", "a - b": "chars", "123": "chars", "...": "chars", "Admin": "reserved", "adm1n": "reserved", "A d m i n": "reserved", "Moderator": "reserved",
                 "System": "reserved", "Guest 123456": "reserved", "guest": "reserved", "G u e s t 1": "reserved", "Owner": "reserved", "Everyone": "reserved", "fuck": "rude", "Sh1t": "rude", "f.u.c.k": "rude",
                 "F u c k": "rude", "\u0425\u0443\u0439": "rude", "\u041f\u0438\u0437\u0434\u0430": "rude", "bastard1": "rude", None: "long", 5: "long"}
        for raw, why in cases.items():
            self.assertEqual(self.nick(raw), (None, why), repr(raw))

    def test_a_word_that_sits_inside_a_harmless_name_is_not_rude(self):
        for good in ("Hancock", "Dickson", "Cassie", "Assange", "Drape", "Scunthorpe".replace("cunt", "cun"), "Michelle", "Computation", "Cumberland", "Analyst", "Shiitake", "Class"):
            self.assertEqual(self.nick(good), (good, None), good)
        for bad in ("Dick", "Cock", "Ass", "a55", "Anal", "Slut", "Whore", "BigSh1tHead", "xxfuckxx", "Hitler99"):
            self.assertEqual(self.nick(bad)[1], "rude", bad)

    def test_the_cyrillic_words_are_read_as_written_and_the_latin_ones_as_a_person_reads_them(self):
        self.assertEqual(self.nick("\u0412\u0430\u0441\u044f")[1], None)                              # a name that holds no word of the list
        self.assertEqual(self.nick("\u041c\u0443\u0434\u0430\u043a")[1], "rude")
        self.assertEqual(self.nick("\u0421\u0443\u043a\u0430")[1], "rude")                           # (a whole-name word)
        self.assertEqual(self.nick("\u0421\u0443\u043a\u043d\u043e")[1], None)                        # ... that is no word inside a name
        self.assertTrue(nickrules.is_rude("sh1t") and nickrules.is_rude("S H I T") and nickrules.is_rude("sh.i.t") and not nickrules.is_rude("shine"))

    def test_a_tag_is_two_to_five_letters_and_digits_of_one_alphabet_in_capitals(self):
        for good, shown in (("ant", "ANT"), ("\u00c4NT", "\u00c4NT"), ("\u00e4nt", "\u00c4NT"), ("ANTS1", "ANTS1"), ("42", "42"), ("\u0440\u0443\u0441", "\u0420\u0423\u0421"), ("w\u00fc", "W\u00dc"), ("\u0131i", "II")):
            self.assertEqual(nickrules.check_tag(good, "Zo\u00eb"), (shown, None), good)
        for raw in (None, "", "   "):
            self.assertEqual(nickrules.check_tag(raw), ("", None), repr(raw))                      # (no tag is fine)
        cases = {"a": "short", "abcdef": "long", "a b": "chars", "a-b": "chars", "[ab]": "chars", "\u00df\u00df": "chars", "\u0391\u039b": "script", "\u9f8d\u9f8d": "script", "a\u0430": "alphabets",
                 "MOD": "reserved", "mod": "reserved", "ADMIN": "reserved", "STAFF": "reserved", "OWNER": "reserved", "ANTS": "reserved", "M0D": "staff", "4DM1N": "staff", "M\u041eD": "alphabets",
                 "FUCK": "rude", "BOT": "bot", "B0T": "bot", "B\u00d8T": "bot", "\u0412\u041eT": "alphabets", 5: "long"}
        for raw, why in cases.items():
            self.assertEqual(nickrules.check_tag(raw, "Zo\u00eb")[1], why, repr(raw))
        self.assertEqual(nickrules.check_tag("BOTS", "Zo\u00eb")[1], None)                           # ("[BOTS] Zoe" is no label of a computer player)

    def test_how_a_name_is_shown(self):
        self.assertEqual(nickrules.shown("Zo\u00eb", "ANT"), "[ANT] Zo\u00eb")
        self.assertEqual(nickrules.shown("Zo\u00eb", ""), "Zo\u00eb")
        self.assertEqual(nickrules.guest_shown("Wren"), "Wren (Guest)")

    def test_the_letters_and_the_limits_are_the_documents(self):
        self.assertEqual((nickrules.NICK_MIN, nickrules.NICK_MAX, nickrules.TAG_MIN, nickrules.TAG_MAX), (3, 16, 2, 5))
        self.assertTrue(all(name_letters.is_letter(c) for c in "A\u00e4\u0436\u1ec5"))
        self.assertFalse(name_letters.is_letter("\u03b1"))


class TheBotLabels(unittest.TestCase):
    def test_the_python_twin_answers_the_table_that_the_other_two_answer(self):
        rows = label_cases()
        self.assertGreater(len(rows), 40)
        for name, expected in rows:
            self.assertEqual(nickrules.reads_as_bot(name), expected, repr(name))

    def test_a_tag_that_would_make_the_whole_name_a_label_is_refused_and_one_that_does_not_is_not(self):
        self.assertTrue(nickrules.reads_as_bot("[BOT] Zoe") and nickrules.reads_as_bot("[b0t] Zoe") and nickrules.reads_as_bot("Pip (bot, Hard)"))
        self.assertFalse(nickrules.reads_as_bot("[BOTS] Zoe") or nickrules.reads_as_bot("Bot") or nickrules.reads_as_bot("Robot") or nickrules.reads_as_bot("[ANT] Zoe") or nickrules.reads_as_bot(None))


class Base(unittest.TestCase):
    def setUp(self):
        self.w = World(mods=())
        self.addCleanup(self.w.close)
        self.anna, self.ben, self.cleo, self.guest = (self.w.join(i) for i in (ANNA, BEN, CLEO, None))

    def frame(self, peer, **fields):
        self.w.clock.now += 30                                           # (far past the rates: they are the subject of one test)
        self.w.chat.on_text(peer, json.dumps(fields))

    def names(self, peer):
        return [m for m in peer.sent if m["t"] == "name"]

    def ask(self, peer, nick, tag="", save=False, guest=False, r=1):
        before = len(self.names(peer))
        self.frame(peer, t="name", r=r, nick=nick, tag=tag, save=save, guest=guest)
        got = self.names(peer)
        self.assertEqual(len(got), before + 1, "one answer for one frame")
        return got[-1]

    def choose(self, peer, nick, tag=""):
        answer = self.ask(peer, nick, tag, save=True)
        self.assertTrue(answer["ok"] and answer["saved"], answer)
        return answer

    def later(self, seconds):
        self.w.wall.now += seconds

    def you(self, peer):
        return self.w.chat.you_of(peer.identity)


class TheStore(Base):
    def test_a_lookalike_key_has_one_owner_and_the_answer_is_the_other_accounts_folded_name(self):
        s = self.w.store
        self.assertIsNone(s.nick_row(ANNA_ID))
        self.assertIsNone(s.nick_owner("zoe"))
        self.assertIsNone(s.nick_put(ANNA_ID, "Zo\u00eb", "zo\u00eb", "zoe", 100, "ANT", 100, 10 ** 12))
        self.assertEqual(s.nick_row(ANNA_ID), s.nick_row(ANNA_ID))
        self.assertEqual(tuple(s.nick_row(ANNA_ID)), ("Zo\u00eb", "zoe", 100, "ANT", 100))
        self.assertEqual(s.nick_owner("zoe"), (ANNA_ID, "zo\u00eb"))
        self.assertEqual(s.nick_put(BEN_ID, "Zoe", "zoe", "zoe", 200, "", 0, 10 ** 12), "zo\u00eb")      # (the other account's folded name: the caller tells taken from lookalike by it)
        self.assertIsNone(s.nick_row(BEN_ID))
        self.assertIsNone(s.nick_put(ANNA_ID, "Zo\u00eb", "zo\u00eb", "zoe", 100, "", 0, 10 ** 12))      # (the account itself may put its own name again: nothing is in its way)

    def test_a_name_that_is_given_up_is_held_for_its_account_until_the_hold_ends(self):
        s = self.w.store
        now = int(s.clock())
        self.assertIsNone(s.nick_put(ANNA_ID, "Zo\u00eb", "zo\u00eb", "zoe", now, "", 0, now + 100))
        self.assertIsNone(s.nick_put(ANNA_ID, "Wren", "wren", "wren", now, "", 0, now + 100))
        self.assertEqual(s.nick_owner("zoe"), (ANNA_ID, "zo\u00eb"))                                  # (held)
        self.assertEqual(s.nick_put(BEN_ID, "Zoe", "zoe", "zoe", now, "", 0, now + 100), "zo\u00eb")
        self.assertIsNone(s.nick_put(ANNA_ID, "Zo\u00eb", "zo\u00eb", "zoe", now, "", 0, now + 100))     # (the account takes its old name back, and the name that it gives up is held in its place)
        self.assertEqual(s.nick_owner("wren"), (ANNA_ID, "wren"))
        self.w.wall.now += 101
        self.assertIsNone(s.nick_owner("wren"))                                                       # (the hold ended)
        self.assertIsNone(s.nick_put(BEN_ID, "Wren", "wren", "wren", now, "", 0, now + 100))

    def test_a_drop_takes_the_nickname_the_tag_and_what_the_account_holds(self):
        s = self.w.store
        now = int(s.clock())
        s.nick_put(ANNA_ID, "Zo\u00eb", "zo\u00eb", "zoe", now, "ANT", now, now + 100)
        s.nick_put(ANNA_ID, "Wren", "wren", "wren", now, "ANT", now, now + 100)
        self.assertTrue(s.nick_drop(ANNA_ID))
        self.assertFalse(s.nick_drop(ANNA_ID))
        self.assertTrue(s.nick_owner("zoe") is None and s.nick_owner("wren") is None and s.nick_row(ANNA_ID) is None)

    def test_forget_me_takes_the_nickname_and_the_holds(self):
        s = self.w.store
        now = int(s.clock())
        s.nick_put(ANNA_ID, "Zo\u00eb", "zo\u00eb", "zoe", now, "", 0, now + 100)
        s.nick_put(ANNA_ID, "Wren", "wren", "wren", now, "", 0, now + 100)
        s.nick_put(BEN_ID, "Pip", "pip", "pip", now, "", 0, now + 100)
        s.forget(ANNA_ID)
        self.assertTrue(s.nick_row(ANNA_ID) is None and s.nick_owner("zoe") is None and s.nick_owner("wren") is None)
        self.assertEqual(s.nick_owner("pip"), (BEN_ID, "pip"))

    def test_a_login_name_is_claimed_by_one_account_on_its_lookalike_key_for_a_time(self):
        s = self.w.store
        now = int(s.clock())
        self.assertTrue(s.login_claim(ANNA_ID, "maple", now + 1000))
        self.assertFalse(s.login_claim(BEN_ID, "maple", now + 1000))                                  # (the key is Anna's)
        self.assertTrue(s.login_claim(ANNA_ID, "maple", now + 1000))                                  # (and she keeps it)
        self.assertTrue(s.login_claim(BEN_ID, "oak", now + 1000))
        self.assertTrue(s.login_claim(ANNA_ID, "birch", now + 1000))                                  # (one claim for an account: the old key goes)
        self.assertTrue(s.login_claim(BEN_ID, "maple", now + 1000))
        self.assertFalse(s.login_claim(ANNA_ID, "oak", now + 1000) and s.login_claim(ANNA_ID, "maple", now + 1000))
        self.w.wall.now += 1001
        self.assertTrue(s.login_claim(CLEO_ID, "maple", now + 5000))                                  # (a claim that nobody renewed has ended)

    def test_a_claim_that_is_renewed_within_the_hour_writes_nothing_and_one_that_is_older_is_renewed(self):
        s = self.w.store
        now = int(s.clock())
        self.assertTrue(s.login_claim(ANNA_ID, "maple", now + 10 * 86400))
        before = s.db.execute("SELECT until FROM login_claim WHERE account = ?", (ANNA_ID,)).fetchone()[0]
        self.w.wall.now += 60
        self.assertTrue(s.login_claim(ANNA_ID, "maple", now + 10 * 86400 + 60))
        self.assertEqual(s.db.execute("SELECT until FROM login_claim WHERE account = ?", (ANNA_ID,)).fetchone()[0], before)
        self.w.wall.now += 7200
        self.assertTrue(s.login_claim(ANNA_ID, "maple", now + 10 * 86400 + 7260))
        self.assertGreater(s.db.execute("SELECT until FROM login_claim WHERE account = ?", (ANNA_ID,)).fetchone()[0], before)

    def test_forget_me_takes_the_claim_of_a_login_name_too(self):
        s = self.w.store
        now = int(s.clock())
        s.login_claim(ANNA_ID, "maple", now + 1000)
        s.forget(ANNA_ID)
        self.assertTrue(s.login_claim(BEN_ID, "maple", now + 1000))

    def test_a_store_that_never_had_a_nickname_has_no_table_and_answers_for_nothing(self):
        s = self.w.store
        self.assertFalse(s.has_nicknames())
        self.assertEqual((s.nick_row(ANNA_ID), s.nick_owner("zoe"), s.nick_drop(ANNA_ID)), (None, None, False))
        self.assertFalse(s.has_nicknames())
        for bad in ((1, "a", "a", "a", 0, "", 0, 0), (ANNA_ID, None, "a", "a", 0, "", 0, 0)):
            with self.assertRaises(ValueError):
                s.nick_put(*bad)


class TheFrames(Base):
    def test_a_check_says_each_field_and_how_the_others_will_see_the_person_and_saves_nothing(self):
        answer = self.ask(self.anna, "Zo\u00eb", "ant")
        self.assertEqual(answer, {"t": "name", "r": 1, "ok": True, "nick": {"ok": True}, "tag": {"ok": True}, "shown": "[ANT] Zo\u00eb", "saved": False})
        self.assertIsNone(self.w.store.nick_row(ANNA_ID))
        self.assertEqual(self.ask(self.anna, "Zo\u00eb", "")["shown"], "Zo\u00eb")
        self.assertEqual(self.ask(self.anna, "Zo\u00eb", "", r=77)["r"], 77)                              # (the page's own number comes back: an answer that is late is told from a newer one)

    def test_a_tag_that_has_no_nickname_behind_it_is_refused_in_its_own_line(self):
        answer = self.ask(self.anna, "", "ant")
        self.assertEqual((answer["ok"], answer["nick"]["why"], answer["tag"]), (False, "empty", {"ok": False, "why": "nick"}))
        self.assertIsNone(self.w.store.nick_row(ANNA_ID))

    def test_a_name_that_somebody_took_between_the_check_and_the_save_is_not_saved(self):
        n = self.w.chat.nicknames
        self.choose(self.ben, "Wren Q")
        saved, field = n.save(ANNA_ID, "Wren Q", "", self.w.wall.now, {"ok": True})
        self.assertEqual((saved, field), (False, {"ok": False, "why": "taken"}))
        saved, field = n.save(ANNA_ID, "Wren Qx", "", self.w.wall.now, {"ok": True})
        self.assertTrue(saved)
        saved, field = n.save(CLEO_ID, "Wren  Q", "", self.w.wall.now, {"ok": True})
        self.assertEqual((saved, field["why"]), (False, "lookalike"))
        self.assertIsNone(self.w.store.nick_row(CLEO_ID))

    def test_a_save_stores_the_nickname_and_the_tag_and_every_socket_of_the_account_gets_the_whole_you(self):
        tab = self.w.join(ANNA)
        before = len([m for m in tab.sent if m["t"] == "you"])
        answer = self.choose(self.anna, "Zo\u00eb", "ant")
        self.assertEqual((answer["shown"], answer["nick"], answer["tag"]), ("[ANT] Zo\u00eb", {"ok": True}, {"ok": True}))
        row = self.w.store.nick_row(ANNA_ID)
        self.assertEqual((row.nick, row.tag), ("Zo\u00eb", "ANT"))
        for peer in (self.anna, tab):
            you = [m for m in peer.sent if m["t"] == "you"][-1]["you"]
            self.assertEqual((you["name"], you["nick"], you["tag"]), ("Zo\u00eb", "Zo\u00eb", "ANT"))
            self.assertEqual(peer.identity.name, "Zo\u00eb")
            self.assertEqual((peer.identity.nick, peer.identity.tag), ("Zo\u00eb", "ANT"))
        self.assertEqual(len([m for m in tab.sent if m["t"] == "you"]), before + 1)
        self.assertNotIn("you", [m["t"] for m in self.ben.sent])                                         # (nobody else is sent anything)
        self.assertEqual(self.w.chat.welcome(self.anna)["you"]["nick"], "Zo\u00eb")

    def test_every_refusal_of_a_nickname_has_its_word(self):
        self.choose(self.anna, "Zo\u00eb")
        cases = {"Zo\u00eb": "taken", "ZO\u00cb": "taken", "zoe": "lookalike", "Z0e": "lookalike", "z.o.e": "lookalike", "Admin": "reserved", "ab": "short", "x" * 17: "long", "Zoe!": "chars",
                 "\u0391\u03bb\u03ad": "script", "Zo\u0435": "alphabets", "fuck": "rude"}
        for raw, why in cases.items():
            answer = self.ask(self.ben, raw)
            self.assertEqual((answer["ok"], answer["nick"]["ok"], answer["nick"].get("why")), (False, False, why), raw)
            self.assertIsNone(answer["shown"])
            self.assertIsNone(self.w.store.nick_row(BEN_ID))

    def test_the_answer_never_names_the_person_whose_name_it_is(self):
        self.choose(self.anna, "Zo\u00eb", "ant")
        answers = [self.ask(self.ben, raw) for raw in ("Zo\u00eb", "zoe", "Z0e")]
        blob = json.dumps(answers)
        for secret in (ANNA_ID, "anna", self.w.chat.key_of(ANNA_ID), self.w.chat.ref(ANNA_ID)):
            self.assertNotIn(secret, blob)

    def test_a_tag_is_refused_with_its_word_and_the_nickname_is_not_saved_with_it(self):
        for raw, why in (("MOD", "reserved"), ("M0D", "staff"), ("FUCK", "rude"), ("BOT", "bot"), ("A", "short"), ("ABCDEF", "long"), ("A B", "chars"), ("\u0391\u039b", "script")):
            answer = self.ask(self.anna, "Zo\u00eb", raw, save=True)
            self.assertEqual((answer["ok"], answer["nick"], answer["tag"]), (False, {"ok": True}, {"ok": False, "why": why}), raw)
            self.assertIsNone(self.w.store.nick_row(ANNA_ID), raw)                                       # (both fields or none)
            self.assertFalse(answer["saved"])

    def test_a_tag_needs_a_nickname_to_stand_before(self):
        answer = self.ask(self.anna, "", "ANT", save=True)
        self.assertEqual((answer["ok"], answer["nick"]["why"]), (False, "empty"))
        self.assertIsNone(self.w.store.nick_row(ANNA_ID))

    def test_the_tag_alone_changes_when_the_nickname_is_the_same_and_no_limit_is_asked_of_what_does_not_change(self):
        self.choose(self.anna, "Zo\u00eb", "")
        self.later(2 * DAY)
        answer = self.ask(self.anna, "Zo\u00eb", "ant", save=True)                                       # (inside the nickname's 30 days: the nickname does not change, so its wait does not matter)
        self.assertTrue(answer["ok"] and answer["saved"], answer)
        self.assertEqual(self.w.store.nick_row(ANNA_ID).tag, "ANT")
        self.assertEqual(self.ask(self.anna, "Zo\u00eb", "ANT")["nick"], {"ok": True})

    def test_the_frames_of_the_page_are_read_strictly(self):
        for bad in ({"t": "name"}, {"t": "name", "r": "1", "nick": "Zoe"}, {"t": "name", "r": 1, "nick": 5}, {"t": "name", "r": 1, "nick": "Zoe", "tag": 1}, {"t": "name", "r": -1, "nick": "Zoe"},
                    {"t": "name", "r": 1.5, "nick": "Zoe"}, {"t": "name", "r": True, "nick": "Zoe"}, {"t": "name", "r": 1, "nick": "Zoe", "save": "yes"}, {"t": "name", "r": 1, "nick": "Zoe", "guest": 1},
                    {"t": "name", "r": 1 << 40, "nick": "Zoe"}, {"t": "name", "r": 1, "nick": "Zoe", "guest": True, "save": True}):
            before = len(self.anna.sent)
            self.frame(self.anna, **bad)
            self.assertEqual([m for m in self.anna.sent[before:] if m["t"] == "name"], [], bad)
            self.assertEqual(self.anna.sent[before:][-1]["t"], "err", bad)
        self.assertIsNone(self.w.store.nick_row(ANNA_ID))

    def test_a_reader_may_check_a_guests_name_and_nothing_else(self):
        self.frame(self.guest, t="name", r=1, nick="Zo\u00eb", tag="")
        self.assertEqual(self.guest.sent[-1], {"t": "err", "code": "login"})
        self.frame(self.guest, t="name", r=2, nick="Wren", save=True)
        self.assertEqual(self.guest.sent[-1], {"t": "err", "code": "login"})
        self.assertEqual(self.ask(self.guest, "Wren", guest=True, r=3), {"t": "name", "r": 3, "ok": True, "nick": {"ok": True}, "tag": None, "shown": "Wren (Guest)"})

    def test_a_guests_name_has_the_rules_of_a_nickname_and_may_not_copy_a_person_who_is_logged_in(self):
        self.choose(self.anna, "Zo\u00eb", "ant")
        for raw, why in (("Zo\u00eb", "lookalike"), ("zoe", "lookalike"), ("Z0e", "lookalike"), ("Admin", "reserved"), ("fuck", "rude"), ("\u0391\u03bb\u03ad", "script"), ("Zo\u0435", "alphabets"), ("ab", "short"), ("Guest 123456", "reserved")):
            answer = self.ask(self.guest, raw, guest=True)
            self.assertEqual((answer["ok"], answer["nick"].get("why"), answer["shown"]), (False, why, None), raw)
        self.assertEqual(self.ask(self.guest, "\u0401\u0436\u0438\u043a", guest=True)["shown"], "\u0401\u0436\u0438\u043a (Guest)")
        self.assertIsNone(self.w.store.nick_row(ANNA_ID) and None)

    def test_a_guests_name_may_not_copy_a_name_that_a_person_gave_up_and_the_hold_ends(self):
        self.choose(self.anna, "Zo\u00eb")
        self.later(31 * DAY)
        self.choose(self.anna, "Wren")
        self.assertEqual(self.ask(self.guest, "Zoe", guest=True)["nick"], {"ok": False, "why": "lookalike"})
        self.later(31 * DAY)
        self.assertEqual(self.ask(self.guest, "Zoe", guest=True)["nick"], {"ok": True})

    def test_the_checks_have_a_rate_for_each_socket_and_a_save_for_each_account(self):
        got = []
        for _ in range(40):
            self.w.chat.on_text(self.guest, json.dumps({"t": "name", "r": 1, "nick": "Wren", "guest": True}))
            got.append(self.guest.sent[-1])
        self.assertTrue(any(m.get("why") == "rate" for m in got) and any(m.get("ok") is True for m in got))
        self.assertTrue(all(m["t"] in ("name", "err") for m in got))
        self.w.clock.now += 10
        self.w.chat.on_text(self.anna, json.dumps({"t": "name", "r": 1, "nick": "Zo\u00eb", "tag": "", "save": True}))
        self.assertTrue(self.names(self.anna)[-1]["saved"])
        for n in range(6):
            self.w.clock.now += 3
            self.w.chat.on_text(self.anna, json.dumps({"t": "name", "r": 2 + n, "nick": "Zo\u00eb", "tag": "", "save": True}))
        self.assertTrue(any(m.get("why") == "rate" for m in self.names(self.anna)))

    def test_a_name_that_somebody_took_between_the_check_and_the_save_is_refused_at_the_save(self):
        self.assertTrue(self.ask(self.ben, "Zo\u00eb")["ok"])
        self.choose(self.anna, "Zo\u00eb")
        answer = self.ask(self.ben, "Zo\u00eb", save=True)
        self.assertEqual((answer["ok"], answer["saved"], answer["nick"]), (False, False, {"ok": False, "why": "taken"}))
        self.assertIsNone(self.w.store.nick_row(BEN_ID))

    def test_a_store_that_cannot_be_used_is_an_answer_and_not_a_crash(self):
        class Broken(Exception):
            pass
        from hub.store import StoreError

        def refuse(*args, **kwargs):
            raise StoreError("locked")
        self.w.store.nick_owner = refuse
        answer = self.ask(self.ben, "Pip")
        self.assertEqual((answer["ok"], answer["nick"]), (False, {"ok": False, "why": "busy"}))
        self.assertEqual(self.ask(self.guest, "Pip", guest=True)["nick"], {"ok": False, "why": "busy"})


class TheLimits(Base):
    def test_a_nickname_changes_once_in_30_days_and_says_when(self):
        self.choose(self.anna, "Zo\u00eb")
        start = int(self.w.wall.now)
        answer = self.ask(self.anna, "Wren", save=True)
        self.assertEqual((answer["ok"], answer["nick"]), (False, {"ok": False, "why": "soon", "until": start + 30 * DAY}))
        self.assertEqual(self.w.store.nick_row(ANNA_ID).nick, "Zo\u00eb")
        self.later(30 * DAY - 1)
        self.assertEqual(self.ask(self.anna, "Wren")["nick"]["why"], "soon")
        self.later(1)
        self.assertEqual(self.ask(self.anna, "Wren")["nick"], {"ok": True})
        self.choose(self.anna, "Wren")
        self.assertEqual(self.ask(self.anna, "Pip", save=True)["nick"]["why"], "soon")

    def test_the_old_nickname_is_held_for_30_days_and_its_person_may_take_it_back(self):
        self.choose(self.anna, "Zo\u00eb")
        self.later(31 * DAY)
        self.choose(self.anna, "Wren")
        for raw in ("Zo\u00eb", "zoe"):
            self.assertIn(self.ask(self.ben, raw)["nick"]["why"], ("taken", "lookalike"))
        self.later(29 * DAY)
        self.assertEqual(self.ask(self.ben, "Zo\u00eb")["nick"]["why"], "taken")                          # (held: 29 days since it was given up)
        self.later(31 * DAY)
        self.assertEqual(self.ask(self.ben, "Zo\u00eb")["nick"], {"ok": True})
        self.choose(self.anna, "Zo\u00eb")                                                                 # (its person takes it back, 60 days on, when nobody has: and it is theirs again)
        self.assertEqual(self.w.store.nick_row(ANNA_ID).nick, "Zo\u00eb")

    def test_a_person_who_changes_back_inside_the_hold_has_the_old_name_and_waits_again(self):
        self.choose(self.anna, "Zo\u00eb")
        self.later(31 * DAY)
        self.choose(self.anna, "Wren")
        self.later(31 * DAY)
        answer = self.choose(self.anna, "Zo\u00eb")
        self.assertEqual(answer["shown"], "Zo\u00eb")
        self.assertEqual(self.ask(self.anna, "Wren", save=True)["nick"]["why"], "soon")

    def test_a_tag_changes_once_a_day_and_says_when(self):
        self.choose(self.anna, "Zo\u00eb", "ant")
        start = int(self.w.wall.now)
        answer = self.ask(self.anna, "Zo\u00eb", "pet", save=True)
        self.assertEqual((answer["ok"], answer["tag"]), (False, {"ok": False, "why": "soon", "until": start + DAY}))
        self.assertEqual(self.w.store.nick_row(ANNA_ID).tag, "ANT")
        self.later(DAY)
        self.assertTrue(self.ask(self.anna, "Zo\u00eb", "pet", save=True)["saved"])
        self.later(DAY)
        answer = self.ask(self.anna, "Zo\u00eb", "", save=True)                                           # (taking the tag away is a change too)
        self.assertTrue(answer["saved"])
        self.assertEqual(self.w.store.nick_row(ANNA_ID).tag, "")
        self.assertEqual(self.ask(self.anna, "Zo\u00eb", "ant", save=True)["tag"]["why"], "soon")

    def test_the_you_says_when_each_may_change_again_and_stops_saying_it(self):
        self.choose(self.anna, "Zo\u00eb", "ant")
        now = int(self.w.wall.now)
        you = self.you(self.anna)
        self.assertEqual((you["nickat"], you["nickfree"], you["tagfree"]), (now, now + 30 * DAY, now + DAY))
        self.later(DAY + 1)
        you = self.you(self.anna)
        self.assertEqual((you["nickfree"], "tagfree" in you), (now + 30 * DAY, False))
        self.later(30 * DAY)
        self.assertNotIn("nickfree", self.you(self.anna))

    def test_a_refused_change_leaves_the_names_and_costs_no_wait(self):
        self.choose(self.anna, "Zo\u00eb", "ant")
        row = self.w.store.nick_row(ANNA_ID)
        self.later(2 * DAY)
        self.ask(self.anna, "Wren", "pet", save=True)
        self.assertEqual(self.w.store.nick_row(ANNA_ID), row)


class TheShownName(Base):
    def test_a_line_carries_the_tag_of_its_author_and_the_history_keeps_it(self):
        self.choose(self.anna, "Zo\u00eb", "ant")
        self.w.clock.now += 3
        self.w.say(self.anna, "hello")
        self.w.clock.now += 3
        self.w.say(self.ben, "hi")
        lines = self.w.chat.welcome(self.guest)["history"]
        self.assertEqual([(l["who"], l.get("tag")) for l in lines], [("Zo\u00eb", "ANT"), ("ben", None)])
        self.assertEqual(lines[0]["text"], "hello")
        later = self.w.store.recent_lines(10)
        self.assertEqual([l.tag for l in later], ["ANT", ""])
        self.later(DAY)
        self.choose(self.anna, "Zo\u00eb", "")
        self.assertEqual([(l["who"], l.get("tag")) for l in self.w.chat.welcome(self.guest)["history"]][0], ("Zo\u00eb", "ANT"))       # (a line keeps the name and tag that it was written with)

    def test_the_list_of_people_has_the_nickname_and_the_tag(self):
        self.choose(self.anna, "Zo\u00eb", "ant")
        rows = list(self.w.chat.presence.view()["rows"].values())
        by_name = {r["name"]: r for r in rows}
        self.assertEqual(by_name["Zo\u00eb"].get("tag"), "ANT")
        self.assertNotIn("tag", by_name["ben"])
        self.assertNotIn("anna", by_name)

    def test_a_login_name_that_is_a_lookalike_of_a_nickname_is_not_shown_the_username_is(self):
        self.choose(self.anna, "Mary Jane")
        twin = session.Identity("8421000099990009", "Mary Jane", False, None, 0, "mary.jane2", "display", "mary.jane2")
        shown = self.w.chat.nicknames.dress(twin)
        self.assertEqual((shown.name, shown.pick, shown.other), ("mary.jane2", "username", ""))
        fine = session.Identity("8421000099990010", "Mary Smith", False, None, 0, "mary.s", "display", "mary.s")
        self.assertEqual(self.w.chat.nicknames.dress(fine), fine)
        own = self.w.chat.nicknames.dress(session.Identity(ANNA_ID, "Mary Jane", False, None, 0, "anna", "display", "anna"))
        self.assertEqual((own.name, own.nick, own.other), ("Mary Jane", "Mary Jane", ""))

    def test_the_name_that_is_shown_is_the_one_that_is_checked_the_display_name_or_the_username_and_a_capital_i_is_an_l(self):
        self.choose(self.anna, "Alice")
        n = self.w.chat.nicknames

        def who(account, name, username, pick="display"):
            return session.Identity(account, name, False, None, 0, username, pick, username if pick == "display" else "")
        shown = n.dress(who("8421000099990011", "AIice", "aiice.q"))
        self.assertEqual((shown.name, shown.pick, shown.other), ("aiice.q", "username", ""))                       # (a capital I for the l of Alice: the display name is a lookalike)
        shown = n.dress(who("8421000099990012", "A1ice", "a1ice.q"))
        self.assertEqual(shown.name, "a1ice.q")
        # no display name: the shown name is the username, and it is the one that is checked
        shown = n.dress(who("8421000099990013", "alice.", "alice.", pick=""))
        self.assertEqual((shown.name, shown.pick), ("alice. (Discord)", "username"))
        # a username that the person picked, and the display name that it would be switched to: the shown one is checked
        shown = n.dress(who("8421000099990014", "A.lice", "a.lice", pick="username"))
        self.assertEqual(shown.name, "a.lice (Discord)")
        # the display name and the username both look like the nickname: neither is shown as it is
        shown = n.dress(who("8421000099990015", "AIice", "a.l.i.c.e"))
        self.assertEqual((shown.name, shown.pick, shown.other), ("a.l.i.c.e (Discord)", "username", ""))
        # a name that is nobody's lookalike is shown as it is, and the account that holds the nickname is never a lookalike of itself
        fine = who("8421000099990016", "Alicia", "alicia.q")
        self.assertEqual(n.dress(fine), fine)
        own = n.dress(who(ANNA_ID, "AIice", "anna"))
        self.assertEqual((own.name, own.nick), ("Alice", "Alice"))

    def test_an_account_that_has_a_nickname_is_not_offered_the_switch_of_its_two_login_names(self):
        a = self.w.join(session.Identity(ANNA_ID, "Anna Smith", False, None, 0, "anna", "display", "anna"))
        self.assertEqual(self.you(a)["other"], "anna")
        self.choose(a, "Zo\u00eb")
        you = self.you(a)
        self.assertEqual((you["name"], "other" in you, "pick" in you), ("Zo\u00eb", False, False))

    def test_a_reset_takes_the_nickname_and_the_tag_and_the_account_is_shown_by_its_login_again(self):
        a = self.w.join(session.Identity(ANNA_ID, "Anna Smith", False, None, 0, "anna", "display", "anna"))
        self.choose(a, "Zo\u00eb", "ant")
        self.assertTrue(self.w.chat.nicknames.reset(ANNA_ID))
        self.assertFalse(self.w.chat.nicknames.reset(ANNA_ID))
        you = [m for m in a.sent if m["t"] == "you"][-1]["you"]
        self.assertEqual((you["name"], "nick" in you, "tag" in you), ("anna", False, False))
        self.assertEqual(a.identity.nick, "")
        self.assertTrue(self.ask(a, "Wren", save=True)["saved"])                                          # (and may choose again at once)


class ThePass(Base):
    def test_the_passes_are_the_words_of_the_contract(self):
        maker = namepass.Maker(SECRET)
        self.assertEqual(maker.key.hex(), "f5228536a05a361af0ebf40b199c7ed718846d787662cf3bb472e228c11c1525")
        for name, golden in GOLDEN.items():
            self.assertEqual(maker.make(name, 1700000000 - namepass.SECONDS), golden, name)
            self.assertTrue(maker.check(name, golden, 1700000000 - namepass.SECONDS), name)

    def test_a_pass_is_good_for_exactly_its_name_and_until_it_ends(self):
        maker = namepass.Maker(SECRET)
        good = maker.make("Wren", 1000)
        self.assertTrue(maker.check("Wren", good, 1000))
        self.assertTrue(maker.check("Wren", good, 1000 + namepass.SECONDS - 1))
        self.assertFalse(maker.check("Wren", good, 1000 + namepass.SECONDS))                           # (ended)
        self.assertFalse(maker.check("wren", good, 1000))
        self.assertFalse(maker.check("Wren ", good, 1000))
        self.assertFalse(maker.check("", good, 1000))
        self.assertFalse(namepass.Maker(SECRET + "x").check("Wren", good, 1000))
        exp, _, mac = good.partition(".")
        far = str(int(exp) + 10 * DAY)
        self.assertFalse(maker.check("Wren", far + "." + mac, 1000))                                   # (a time that the hub does not make is no pass)
        for bad in (None, 5, "", ".", good + "0", good[:-1], exp, exp + ".", "." + mac, exp + "." + mac.upper(), "x" + good, good.replace(".", "..")):
            self.assertFalse(maker.check("Wren", bad, 1000), repr(bad))

    def test_a_pass_lasts_two_minutes_and_a_half_a_minute_more_is_all_that_the_server_forgives(self):
        maker = namepass.Maker(SECRET)
        self.assertEqual(namepass.SECONDS, 120)
        self.assertEqual(int(maker.make("Wren", 1000).partition(".")[0]), 1120)
        for exp, want in ((1000 + 120 + 30, True), (1000 + 120 + 31, False), (1000 + 120 + 10 * DAY, False)):
            self.assertEqual(maker.check("Wren", "%d.%s" % (exp, maker.mac(str(exp), "Wren")), 1000), want, exp)
        self.assertFalse(maker.check("", maker.make("", 1000), 1000))                               # (a pass for no name is no pass, whoever made it)

    def test_a_login_name_that_reads_as_a_computer_player_or_is_too_short_is_not_played_under(self):
        n = self.w.chat.nicknames
        for name in ("Bot (Hard)", "Pip (bot)", "x", "Guest 123456", "Admin"):
            who = session.Identity(CLEO_ID, name, False, None, 0, "cleo", "display", "cleo")
            self.assertEqual(n.play_name(who), "cleo", name)

    def test_the_game_name_of_an_account_is_its_nickname_with_the_tag(self):
        n = self.w.chat.nicknames
        self.assertEqual(n.play_name(ANNA), "anna")
        self.choose(self.anna, "Zo\u00eb", "ant")
        self.assertEqual(n.play_name(self.anna.identity), "[ANT] Zo\u00eb")
        self.choose(self.ben, "Pip")
        self.assertEqual(n.play_name(self.ben.identity), "Pip")

    def test_an_account_with_no_nickname_plays_under_the_first_name_that_fits_the_game(self):
        n = self.w.chat.nicknames

        def who(name, other="", username="maple.leaf"):
            return session.Identity("8421000099990020", name, False, None, 0, username, "display" if other else "", other)
        self.assertEqual(n.play_name(who("Maple Leaf", "maple.leaf")), "Maple Leaf")
        self.assertEqual(n.play_name(who("\u03a3\u03bf\u03c6\u03af\u03b1", "maple.leaf")), "maple.leaf")                  # (Greek: the font draws none of it)
        self.assertEqual(n.play_name(who("\U0001f600\U0001f600", "maple.leaf")), "maple.leaf")
        self.assertEqual(n.play_name(who("Admin", "maple.leaf")), "maple.leaf")                                       # (a name that could pass for the hub's voice)
        self.assertEqual(n.play_name(who("Bot (Hard)", "maple.leaf")), "maple.leaf")                                  # (a label of a computer player)
        self.assertEqual(n.play_name(who("Guest 123456", "maple.leaf")), "maple.leaf")
        self.assertEqual(n.play_name(who("x", "maple.leaf")), "maple.leaf")                                          # (too short)
        self.choose(self.ben, "Zo\u00eb")
        self.assertEqual(n.play_name(who("Zo\u00eb", "maple.leaf")), "maple.leaf")                                    # (a lookalike of somebody's nickname)
        self.assertEqual(n.play_name(who("Zoe", "zo.e", "zo.e")), None)                                              # (and when the username is one too, no name fits: the page plays as a guest)

    def test_a_login_name_is_played_under_only_by_the_rules_of_a_nickname(self):
        n = self.w.chat.nicknames

        def who(name, account="8421000099990030"):
            return session.Identity(account, name, False, None, 0, "maple.leaf", "display", "maple.leaf")
        for name in ("Owner", "Developer", "Host", "Nobody", "Player", "Bot", "Computer", "butt", "A" * 17, "Maple Leaf Maple Leaf", "Alex\u0438s", "Maple ", " Maple", "Ma  ple", "Maple!", "Zo\u00eb\u0301"):
            self.assertEqual(n.play_name(who(name)), "maple.leaf", repr(name))
        for name in ("Maple", "Maple Leaf", "Zo\u00eb", "\u0401\u0436\u0438\u043a", "A" * 16, "abc"):
            self.assertEqual(n.play_name(who(name)), name, repr(name))
        self.assertIsNone(n.play_name(session.Identity("8421000099990031", "Owner", False, None, 0, "admin", "display", "admin")))      # (no name fits: the page plays as a guest)

    def test_two_accounts_with_one_display_name_are_not_both_passed_for_it(self):
        n = self.w.chat.nicknames
        n.pass_maker = namepass.Maker(SECRET)

        def who(account, name, username):
            return session.Identity(account, name, False, None, 0, username, "display", username)
        first, second, third = who("8421000099990041", "Maple Leaf", "maple.a"), who("8421000099990042", "Maple Leaf", "maple.b"), who("8421000099990043", "Maple.Leaf", "maple.c")
        self.assertEqual(n.play(first)["name"], "Maple Leaf")
        self.assertEqual(n.play(second)["name"], "maple.b")                                              # (the first account has the name)
        self.assertEqual(n.play(third)["name"], "maple.c")                                               # (and a lookalike of it)
        self.assertEqual(n.play(first)["name"], "Maple Leaf")                                            # (it keeps it as long as it asks)
        self.later(86400)
        self.assertEqual(n.play(first)["name"], "Maple Leaf")
        self.later(29 * 86400)
        self.assertEqual(n.play(second)["name"], "maple.b")                                              # (29 days since the first asked: the claim is still hers)
        self.later(2 * 86400)
        self.assertEqual(n.play(second)["name"], "Maple Leaf")                                           # (nobody asked for it for 30 days: it is free)
        self.assertEqual(n.play(first)["name"], "maple.a")
        # a person who shows another name gives the old one up at once
        self.assertEqual(n.play(who("8421000099990042", "Oak Leaf", "maple.b"))["name"], "Oak Leaf")
        self.assertEqual(n.play(first)["name"], "Maple Leaf")

    def test_a_store_that_cannot_claim_leaves_the_person_to_play_as_a_guest_and_the_hub_is_told(self):
        from hub.store import StoreError
        told = []
        self.w.chat.on_store_error = told.append

        def refuse(*args, **kwargs):
            raise StoreError("locked")
        self.w.store.login_claim = refuse
        n = self.w.chat.nicknames
        self.assertIsNone(n.play_name(session.Identity("8421000099990061", "Maple Leaf", False, None, 0, "maple.a", "display", "maple.a")))
        self.assertEqual(len(told), 3)                                                                   # (the three names were tried, and each failure was said)

    def test_a_nickname_does_not_give_way_to_a_login_name_that_somebody_is_passed_for(self):
        n = self.w.chat.nicknames

        def who(account, name, username):
            return session.Identity(account, name, False, None, 0, username, "display", username)
        first = who("8421000099990051", "Maple Leaf", "maple.a")
        self.assertEqual(n.play_name(first), "Maple Leaf")
        self.choose(self.ben, "Maple Leaf")                                                              # (a nickname is the person's own: the claim does not stand in its way)
        self.assertEqual(n.play_name(first), "maple.a")                                                  # (and the login name is a lookalike of a nickname now: the username)

    def test_play_gives_the_pass_only_where_the_hub_has_the_key(self):
        n = self.w.chat.nicknames
        self.choose(self.anna, "Zo\u00eb", "ant")
        self.assertEqual(n.play(self.anna.identity), {"name": "[ANT] Zo\u00eb"})
        n.pass_maker = namepass.Maker(SECRET)
        play = n.play(self.anna.identity)
        self.assertEqual(play["name"], "[ANT] Zo\u00eb")
        self.assertTrue(n.pass_maker.check(play["name"], play["pass"], self.w.store.clock()))
        self.assertFalse(n.pass_maker.check("Zo\u00eb", play["pass"], self.w.store.clock()))
        nothing = n.play(session.Identity("8421000099990021", "Zoe", False, None, 0, "zo.e", "display", "zo.e"))
        self.assertEqual(nothing, {"name": None})                                                          # (no name, no pass)


class TheReset(unittest.TestCase):
    """A moderator's /rename: the nickname and the tag of a person go, the person may choose again at once, the log says who did it to whom, and only a moderator, only to somebody below them, only to a person who has one."""
    MOD_ID, MOD2_ID = "8421000099990020", "8421000099990021"

    def setUp(self):
        self.w = World(mods=(self.MOD_ID, self.MOD2_ID))
        self.addCleanup(self.w.close)
        self.mod = self.w.join(session.Identity(self.MOD_ID, "moddy", False, None, 0, "moddy"))
        self.mod2 = self.w.join(session.Identity(self.MOD2_ID, "modette", False, None, 0, "modette"))
        self.anna = self.w.join(session.Identity(ANNA_ID, "anna", False, None, 0, "anna"))
        self.ben = self.w.join(session.Identity(BEN_ID, "ben", False, None, 0, "ben"))

    def step(self):
        self.w.clock.now += 30
        self.w.wall.now += 30

    def say(self, peer, line):
        self.step()
        before = len(peer.sent)
        self.w.say(peer, line)
        return peer.sent[before:]

    def save(self, peer, nick, tag):
        self.step()
        self.w.chat.on_text(peer, json.dumps({"t": "name", "r": 1, "nick": nick, "tag": tag, "save": True}))
        self.assertTrue([m for m in peer.sent if m["t"] == "name"][-1]["saved"])

    def test_a_moderator_takes_the_nickname_and_the_tag_away_and_the_person_may_choose_again_at_once(self):
        self.save(self.anna, "Zoe Q", "ANT")
        self.assertEqual(self.w.chat.you_of(self.anna.identity).get("nick"), "Zoe Q")
        self.assertEqual(self.say(self.mod, "/rename anna"), [{"t": "ok", "c": "rename"}])
        you = [m for m in self.anna.sent if m["t"] == "you"][-1]["you"]
        self.assertEqual((you["name"], "nick" in you, "tag" in you), ("anna", False, False))
        self.assertEqual(self.anna.identity.nick, "")
        self.step()
        self.w.chat.on_text(self.anna, json.dumps({"t": "name", "r": 2, "nick": "Wren Q", "tag": "", "save": True}))
        self.assertTrue([m for m in self.anna.sent if m["t"] == "name"][-1]["saved"])                    # (no 30 days to wait, and the old nickname is not held against the person)

    def test_the_log_says_who_reset_whom_and_nothing_else(self):
        self.save(self.anna, "Zoe Q", "")
        self.say(self.mod, "/rename anna")
        rows = self.w.store.actions()
        self.assertEqual([(r.actor, r.action, r.target, r.detail) for r in rows], [(self.w.chat.ref(self.MOD_ID), "rename", self.w.chat.ref(ANNA_ID), "")])

    def test_a_person_who_has_no_nickname_or_is_not_here_is_refused_and_nothing_is_logged(self):
        self.assertEqual(self.say(self.mod, "/rename ben"), [{"t": "err", "code": "bad"}])
        self.assertEqual(self.say(self.mod, "/rename nobody"), [{"t": "err", "code": "who"}])
        self.assertEqual(self.w.store.actions(), [])

    def test_a_person_who_is_not_a_moderator_gets_the_answer_of_a_word_that_is_no_command(self):
        self.save(self.anna, "Zoe Q", "")
        unknown = self.say(self.ben, "/nonsense anna")
        self.assertEqual(self.say(self.ben, "/rename anna"), unknown)
        self.assertEqual(self.w.chat.you_of(self.anna.identity).get("nick"), "Zoe Q")

    def test_a_moderator_has_no_power_over_the_nickname_of_a_moderator(self):
        self.save(self.mod2, "Boss Q", "")
        self.assertEqual(self.say(self.mod, "/rename modette"), [{"t": "err", "code": "power"}])
        self.assertEqual(self.w.chat.you_of(self.mod2.identity).get("nick"), "Boss Q")

    def test_a_moderator_does_not_reset_their_own_name(self):
        self.save(self.mod, "Boss Q", "")
        self.assertEqual(self.say(self.mod, "/rename moddy"), [{"t": "err", "code": "power"}])

    def test_the_command_is_in_the_list_of_a_moderator_and_not_in_a_persons(self):
        self.assertIn("/rename", [row["w"] for row in commandsmod.lines_for("mod")])
        self.assertIn("/rename", [row["w"] for row in commandsmod.lines_for("admin")])
        self.assertNotIn("/rename", [row["w"] for row in commandsmod.lines_for("none")])


class TheSecrets(Base):
    def test_no_id_and_no_reference_is_in_a_frame_of_a_name(self):
        self.choose(self.anna, "Zo\u00eb", "ant")
        self.ask(self.ben, "zoe")
        self.ask(self.guest, "Zoe", guest=True)
        blob = json.dumps([m for p in (self.anna, self.ben, self.guest) for m in p.sent if m["t"] == "name"])
        for secret in (ANNA_ID, BEN_ID, self.w.chat.ref(ANNA_ID), self.w.chat.ref(BEN_ID), "anna", "ben"):
            self.assertNotIn(secret, blob)


if __name__ == "__main__":
    unittest.main()
