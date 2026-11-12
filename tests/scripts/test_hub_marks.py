#!/usr/bin/env python3
"""The role that a line and a row of the players list carry (hub/chat.py `line_of`, hub/presence.py `row_of`, hub/store/sqlite.py `add_line`; docs/HUB.md "Moderation" and "Presence"; run by
./run_tests.sh --fast and by the CI). Every id and name here is made up.

  TheLine          a line says `role` (`mod` or `admin`, nothing for a person with no role) beside `mod`, live and in the history, and `mod` is what it always was
  TheStore         the role is kept with the line in the column that held a flag (0 none, 1 moderator, 2 administrator): no new schema step, the file of before reads as it did, and a hub of an
                   earlier version reads any of the three as a plain flag
  TheRows          a row of the players list says `role` the same way
  NeverAPower      the role on a line or a row is decoration: nothing in the hub reads it to allow an action, and it holds no account id

tests/data/hub_marks_mutants.json breaks the rules one at a time for tools/mutate.py (every one must be caught).
"""
import json
import os
import re
import sqlite3
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
import test_hub_core as core  # noqa: E402
import test_hub_presence as tp  # noqa: E402
from hub import chat as chatmod, presence, session, store as storemod  # noqa: E402

ID_MOD, ID_ADMIN, ID_NONE = "7411000000000001", "7411000000000002", "7411000000000003"       # made-up ids: like passwords, they are in no frame
MOD = session.Identity(ID_MOD, "moddy", False)
ADMIN = session.Identity(ID_ADMIN, "boss.one", False)
NONE = session.Identity(ID_NONE, "plain.person", False)


def world():
    w = core.World(mods=())
    w.roles.roles.update({ID_MOD: "mod", ID_ADMIN: "admin"})
    return w


class TheLine(unittest.TestCase):
    def setUp(self):
        self.w = world()
        self.addCleanup(self.w.close)
        self.mod, self.admin, self.none, self.reader = (self.w.join(i) for i in (MOD, ADMIN, NONE, None))

    def lines(self, peer):
        return {m["who"]: m for m in peer.kinds("line")}

    def test_a_line_says_the_role_that_its_author_has_and_mod_as_it_always_did(self):
        for peer, text in ((self.mod, "from the moderator"), (self.admin, "from the administrator"), (self.none, "from nobody")):
            self.w.say(peer, text)
        lines = self.lines(self.reader)
        self.assertEqual((lines["moddy"]["mod"], lines["moddy"]["role"]), (True, "mod"))
        self.assertEqual((lines["boss.one"]["mod"], lines["boss.one"]["role"]), (True, "admin"))
        self.assertEqual(lines["plain.person"]["mod"], False)
        self.assertNotIn("role", lines["plain.person"], "absent for a person with no role")
        self.assertEqual(set(lines["moddy"]) - {"u"}, {"t", "id", "at", "who", "text", "mod", "role", "k"}, "no other field")

    def test_everybody_gets_it_the_same_a_moderator_too_and_a_reader(self):
        self.w.say(self.admin, "hello")
        for peer in (self.reader, self.mod, self.none, self.admin):
            self.assertEqual([m["role"] for m in peer.kinds("line")], ["admin"])

    def test_the_history_of_a_welcome_has_it(self):
        self.w.say(self.mod, "one")
        self.w.say(self.admin, "two")
        self.w.say(self.none, "three")
        for who in (None, NONE, MOD, ADMIN):
            history = self.w.chat.welcome(core.FakePeer(who))["history"]
            self.assertEqual([h.get("role") for h in history], ["mod", "admin", None], who)
            self.assertEqual([h["mod"] for h in history], [True, True, False])

    def test_a_line_shows_the_role_its_author_had_when_it_wrote_it(self):
        self.w.say(self.admin, "written as an administrator")
        self.w.roles.roles[ID_ADMIN] = "none"
        self.w.clock.now += 5
        self.w.say(self.admin, "written as nobody")
        self.w.roles.roles[ID_ADMIN] = "mod"
        history = self.w.chat.welcome(core.FakePeer(None))["history"]
        self.assertEqual([h.get("role") for h in history], ["admin", None], "the first line keeps what it was, the second what it was")

    def test_the_hubs_own_line_has_no_role(self):
        self.w.say(self.admin, "/kick plain.person")
        self.w.clock.now += 1
        lines = [m for m in self.reader.kinds("line") if m.get("sys")]
        for m in lines:
            self.assertNotIn("role", m)
            self.assertEqual(m["mod"], False)


class TheStore(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.path = os.path.join(self.folder.name, "hub.db")

    def test_the_role_is_kept_in_the_column_that_held_the_flag_and_comes_back(self):
        store = storemod.Store(self.path, clock=lambda: 1_800_000_000.0)
        for mod in (0, 1, 2, False, True):
            store.add_line("acct", "n", "t", mod)
        self.assertEqual([l.mod for l in store.recent_lines(10)], [0, 1, 2, 0, 1])
        store.close()
        again = storemod.Store(self.path, clock=lambda: 1_800_000_000.0)
        self.assertEqual([l.mod for l in again.recent_lines(10)], [0, 1, 2, 0, 1], "kept in the file")
        again.close()
        raw = sqlite3.connect(self.path)
        self.assertEqual([r[0] for r in raw.execute("SELECT mod FROM lines ORDER BY id")], [0, 1, 2, 0, 1])
        raw.close()

    def test_no_schema_step_and_a_file_of_before_reads_as_it_did(self):
        store = storemod.Store(self.path, clock=lambda: 1_800_000_000.0)
        store.add_line("acct", "n", "t", 2)
        store.close()
        raw = sqlite3.connect(self.path)
        self.assertEqual(raw.execute("PRAGMA user_version").fetchone()[0], storemod.sqlite.SCHEMA)
        self.assertEqual(storemod.sqlite.SCHEMA, 4, "the roles on lines needed no step: the schema is the one of the powers")
        raw.execute("INSERT INTO lines (at, account, name, text, mod) VALUES (1800000000, 'a', 'old', 'a line of before', 1)")
        raw.commit()
        raw.close()
        again = storemod.Store(self.path, clock=lambda: 1_800_000_000.0)
        self.assertEqual([(l.text, l.mod) for l in again.recent_lines(10)], [("t", 2), ("a line of before", 1)], "a line of before is a moderator's: it is shown as that")
        again.close()

    def test_a_hub_of_an_earlier_version_reads_any_of_the_three_as_the_flag_it_knew(self):
        store = storemod.Store(self.path, clock=lambda: 1_800_000_000.0)
        for mod in (0, 1, 2):
            store.add_line("acct", "n", "t%d" % mod, mod)
        store.close()
        raw = sqlite3.connect(self.path)
        self.assertEqual([bool(r[0]) for r in raw.execute("SELECT mod FROM lines ORDER BY id")], [False, True, True], "bool(line.mod), the earlier hub's reading")
        self.assertEqual(raw.execute("SELECT COUNT(*) FROM lines WHERE (mod OR deleted) = 1").fetchone()[0], 2, "the purge's own condition: a moderator's line, whichever role")
        raw.close()

    def test_a_role_that_is_not_a_number_is_not_a_role(self):
        store = storemod.Store(self.path, clock=lambda: 1_800_000_000.0)
        self.assertEqual([store.add_line("a", "n", "t", m).mod for m in (3, 7, "admin", None, 0.0)], [1, 1, 1, 0, 0], "anything true is a moderator's mark unless it is exactly 2")
        store.close()


class TheRows(unittest.TestCase):
    def setUp(self):
        self.w = tp.World(mods=())
        self.addCleanup(self.w.close)
        self.w.chat.roles.roles.update({ID_MOD: "mod", ID_ADMIN: "admin"})

    def rows(self):
        peers = {name: self.w.join(i) for name, i in (("mod", MOD), ("admin", ADMIN), ("none", NONE))}
        watcher = self.w.join(NONE)
        for p in peers.values():
            self.w.here(p, "look")
        page = self.w.watch(watcher)
        self.w.advance(3)
        page.update()
        return {r["name"]: r for r in page.rows.values()}

    def test_a_row_says_the_role_beside_mod(self):
        rows = self.rows()
        self.assertEqual((rows["moddy"]["mod"], rows["moddy"]["role"]), (True, "mod"))
        self.assertEqual((rows["boss.one"]["mod"], rows["boss.one"]["role"]), (True, "admin"))
        self.assertNotIn("mod", rows["plain.person"])
        self.assertNotIn("role", rows["plain.person"])

    def test_row_of_takes_a_bool_or_the_word(self):
        where = presence.Where("look", None, None, None, False, None, 0)
        self.assertEqual(presence.row_of(1, "a", True, where), {"i": 1, "name": "a", "at": "look", "mod": True})
        self.assertEqual(presence.row_of(1, "a", "mod", where)["role"], "mod")
        self.assertEqual(presence.row_of(1, "a", "admin", where)["role"], "admin")
        self.assertEqual(presence.row_of(1, "a", False, where), {"i": 1, "name": "a", "at": "look"})

    def test_a_role_that_changes_changes_the_row_at_the_next_look(self):
        self.rows()
        self.w.chat.roles.roles[ID_NONE] = "admin"
        page = self.w.watch(self.w.join(NONE))
        self.w.advance(3)
        page.update()
        self.assertEqual({r["name"]: r.get("role") for r in page.rows.values()}["plain.person"], "admin")


class NeverAPower(unittest.TestCase):
    def test_the_role_of_a_line_is_read_in_one_place_and_never_to_allow_anything(self):
        with open(os.path.join(REPO, "hub", "chat.py"), encoding="utf-8") as f:
            code = re.sub(r"#.*", "", f.read())
        body = re.search(r"def line_of\(self.*?\n    def ", code, re.S).group(0)
        self.assertEqual(len(re.findall(r"\bline\.mod\b", code)), len(re.findall(r"\bline\.mod\b", body)), "only line_of reads a line's role")
        self.assertEqual(len(re.findall(r"\bline\.mod\b", body)), 3, "the flag, whether there is a role, and which")
        for name in ("roles.py", "presence.py", "session.py"):
            with open(os.path.join(REPO, "hub", name), encoding="utf-8") as f:
                self.assertNotRegex(re.sub(r"#.*", "", f.read()), r"line\.mod|entry\.mod|\.mod\b\s*(>=|==)", name)

    def test_a_line_that_says_admin_gives_its_author_nothing(self):
        w = world()
        self.addCleanup(w.close)
        admin, none = w.join(ADMIN), w.join(NONE)
        w.say(admin, "an administrator's line")
        w.roles.roles[ID_ADMIN] = "none"                   # (the role is gone: the line still says admin)
        w.clock.now += 5
        w.say(admin, "/kick plain.person")
        self.assertEqual(admin.codes()[-1:], ["bad"], "a command from a person who holds no role is refused, whatever their old lines say")
        self.assertFalse(none.kinds("bye"))

    def test_the_frames_hold_no_id(self):
        w = world()
        self.addCleanup(w.close)
        peers = [w.join(i) for i in (MOD, ADMIN, NONE)]
        for p in peers:
            w.say(p, "hello from " + p.identity.username)
        w.say(peers[1], "/kick plain.person")
        for p in peers + [w.join(None)]:
            text = json.dumps(p.sent)
            for part in (ID_MOD, ID_ADMIN, ID_NONE, ID_MOD[:8], ID_ADMIN[-8:]):
                self.assertNotIn(part, text)
        for m in w.chat.welcome(core.FakePeer(MOD))["history"]:
            for part in (ID_MOD, ID_ADMIN, ID_NONE):
                self.assertNotIn(part, json.dumps(m))


if __name__ == "__main__":
    unittest.main()
