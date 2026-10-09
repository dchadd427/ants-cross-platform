#!/usr/bin/env python3
"""The players' maps of the repository (Community-Maps/, tools/community_maps.py; run by ./run_tests.sh --fast and by the CI).

What is read here from the files themselves (the engine's plays of them are suite 2.18.1 of ./run_tests.sh: map_sweep over the folder, then `community_maps.py verify`):
  - the rule that decides which maps of a collection are taken in: every reason, in the order it applies, on made-up report entries and made-up bytes;
  - the folder: every file is a level the protocol can name (printable ASCII, no path or Windows-forbidden character, at most 64 characters, `.lvl` or `.LVL`) and no name clashes with
    another on a computer that ignores the case of names or with a file of Original-Ants/Maps; no email address, no program and no byte of the folder's files is text to git (-text);
  - maps.json: it lists exactly the files, in the order of their names, with the size and the FNV-1a 64 hash of each, the header's minutes and description, and a plausible grid;
  - the README's numbers are the folder's.
"""
import json
import os
import re
import sys
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools"))
import community_maps  # noqa: E402

FOLDER = os.path.join(REPO, "Community-Maps")
WINDOWS_DEVICES = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"}


def read_bytes(*parts):
    with open(os.path.join(*parts), "rb") as f:
        return f.read()


def level_files():
    return sorted(n for n in os.listdir(FOLDER) if n.lower().endswith(".lvl"))


def entry(**changes):
    """One object of a sweep report: a map that passes unless a change says otherwise."""
    good = {"name": "Some Map.lvl", "loads": True, "playable": True, "problems": [], "width": 31, "height": 31, "start_markers": [4, 4, 4, 4], "hill_cells": [16, 16, 16, 16],
            "runs": [{"roster": 15, "status": "ok", "deterministic": True}, {"roster": 9, "status": "ok", "deterministic": True}]}
    good.update(changes)
    return good


class TheRule(unittest.TestCase):
    PLAIN = b"\0\1\2\3" * 100 + b"Tiny map with no PowerUps\0"

    def test_a_map_that_loads_and_plays_passes(self):
        self.assertIsNone(community_maps.reason(entry(), self.PLAIN))

    def test_every_reason_names_the_map_that_has_it(self):
        cases = [
            (entry(loads=False), self.PLAIN, "does not load"),
            (entry(playable=False), self.PLAIN, "not playable"),
            (entry(runs=[{"roster": 15, "status": "ok", "deterministic": True}, {"roster": 9, "status": "refused", "deterministic": True}]), self.PLAIN, "play fails"),
            (entry(runs=[{"roster": 15, "status": "crash", "deterministic": True}]), self.PLAIN, "play fails"),
            (entry(runs=[{"roster": 15, "status": "ok", "deterministic": False}]), self.PLAIN, "play fails"),
            (entry(problems=[{"kind": "tile_outside_dictionary", "severity": "warning"}]), self.PLAIN, "tile outside the dictionary"),
            (entry(), self.PLAIN + b"\0write to me at someone@example.com\0", "email address inside"),
            (entry(), self.PLAIN + b"\0Reading SETVER.EXE file.\0", "program code inside"),
            (entry(), self.PLAIN + b"\0!This program cannot be run in DOS mode.\0", "program code inside"),
        ]
        for report, data, why in cases:
            self.assertEqual(community_maps.reason(report, data), why, why)

    def test_the_other_findings_of_the_sweep_do_not_keep_a_map_out(self):
        for kind in ("object_outside_grid", "mode_ignored", "egg_stock_missing", "waypoint_block_truncated", "food_outside_grid", "food_block_ended_early"):
            self.assertIsNone(community_maps.reason(entry(problems=[{"kind": kind, "severity": "warning"}]), self.PLAIN), kind)

    def test_web_addresses_and_a_dotted_com_are_no_programs(self):
        self.assertIsNone(community_maps.reason(entry(), self.PLAIN + b"\0An Original Map by X   antsownz.com\0!Visit pbcguild.cjb.net ...\0"))

    def test_the_first_reason_wins_in_the_order_of_the_list(self):
        self.assertEqual(community_maps.reason(entry(loads=False, playable=False), self.PLAIN + b"a@b.cd"), community_maps.REASONS[0])
        self.assertEqual(community_maps.reason(entry(problems=[{"kind": "tile_outside_dictionary"}]), self.PLAIN + b"a@b.cd"), community_maps.REASONS[3])
        self.assertEqual(community_maps.REASONS, ("does not load", "not playable", "play fails", "tile outside the dictionary", "email address inside", "program code inside"))

    def test_the_original_six_are_not_taken_in_whatever_their_case(self):
        for name in ("TINY.LVL", "tiny.lvl", "Treasure.lvl", "ISLANDS.LVL", "GAUNTLET.lvl"):
            self.assertTrue(community_maps.is_original(name), name)
        self.assertFalse(community_maps.is_original("Tiny Islands.lvl"))
        self.assertFalse(community_maps.is_original("TinyIslands.lvl"))

    def test_the_header_is_what_the_setup_screen_shows(self):
        data = (8).to_bytes(4, "little") + (1).to_bytes(4, "little") + (12).to_bytes(2, "little") + b"One person's trash...  \0" + b"\xff" * 20
        self.assertEqual(community_maps.header(data), (12, "One person's trash..."))
        self.assertEqual(community_maps.header(data[:10] + b"\x07tab\0" + b"x" * 30)[1], "tab")      # a control character is not shown

    def test_players_are_the_teams_that_have_a_hill_or_a_start_marker(self):
        data = b"\0" * 60
        self.assertEqual(community_maps.manifest_entry(entry(), data)["players"], 4)
        self.assertEqual(community_maps.manifest_entry(entry(start_markers=[2, 2, 0, 0], hill_cells=[16, 16, 0, 0]), data)["players"], 2)
        self.assertEqual(community_maps.manifest_entry(entry(start_markers=[0, 0, 0, 0], hill_cells=[16, 16, 16, 0]), data)["players"], 3)
        self.assertEqual(community_maps.manifest_entry(entry(start_markers=[0, 0, 0, 0], hill_cells=[0, 0, 0, 0]), data)["players"], 4)

    def test_the_hash_is_the_one_of_the_protocol(self):
        self.assertEqual(community_maps.fnv1a64(b""), "cbf29ce484222325")
        self.assertEqual(community_maps.fnv1a64(b"a"), "af63dc4c8601ec8c")
        self.assertEqual(community_maps.fnv1a64(b"foobar"), "85944171f73967e8")


class TheFolder(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.names = level_files()
        with open(os.path.join(FOLDER, "maps.json"), encoding="utf-8") as f:
            cls.manifest = json.load(f)

    def test_the_folder_holds_maps_and_nothing_but_their_list_and_its_readme(self):
        self.assertEqual(sorted(os.listdir(FOLDER)), sorted(self.names + ["README.md", "maps.json"]))
        self.assertGreater(len(self.names), 400)

    def test_every_name_is_one_that_a_room_can_use(self):
        for name in self.names:
            self.assertTrue(re.fullmatch(r"[\x20-\x7e]{1,60}\.[lL][vV][lL]", name) and not re.search(r'[/\\:*?"<>|]', name) and not name.startswith("."), name)
            self.assertLessEqual(len(name), 64, name)

    def test_no_name_clashes_on_a_computer_that_ignores_case_or_with_an_original_map(self):
        folded = [n.lower() for n in self.names]
        self.assertEqual(len(folded), len(set(folded)))
        originals = {n.lower() for n in os.listdir(os.path.join(REPO, "Original-Ants", "Maps"))}
        self.assertFalse(originals & set(folded))
        for name in self.names:
            self.assertFalse(community_maps.is_original(name), name)
            base = os.path.splitext(name)[0]
            self.assertNotIn(base.rstrip(". ").upper(), WINDOWS_DEVICES, name)         # Windows cannot make CON.lvl or "NUL ..lvl"

    def test_no_file_holds_an_email_address_or_a_program(self):
        for name in self.names:
            for text in community_maps.PRINTABLE_RUN.finditer(read_bytes(FOLDER, name)):
                text = text.group().decode("ascii")
                self.assertIsNone(community_maps.EMAIL.search(text), "%s: %r" % (name, text))
                self.assertIsNone(community_maps.PROGRAM.search(text), "%s: %r" % (name, text))

    def test_every_file_is_a_version_8_level_the_size_of_a_map(self):
        for name in self.names:
            data = read_bytes(FOLDER, name)
            self.assertEqual(int.from_bytes(data[:4], "little"), 8, name)
            self.assertTrue(10_000 < len(data) < 1_000_000, name)

    def test_git_leaves_the_bytes_alone(self):
        with open(os.path.join(REPO, ".gitattributes"), encoding="utf-8") as f:
            text = f.read()
        self.assertRegex(text, r"(?m)^Community-Maps/\*\*\s+-text$")
        self.assertRegex(text, r"(?m)^\*\.lvl\s+binary$")
        self.assertRegex(text, r"(?m)^\*\.LVL\s+binary$")


class TheList(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.names = level_files()
        with open(os.path.join(FOLDER, "maps.json"), encoding="utf-8") as f:
            cls.manifest = json.load(f)
        cls.rows = cls.manifest["maps"]

    def test_it_lists_exactly_the_files_in_the_order_of_their_names(self):
        self.assertEqual(self.manifest["format"], 1)
        self.assertEqual([r["file"] for r in self.rows], self.names)
        self.assertEqual(list(self.manifest), ["format", "maps"])

    def test_every_line_says_what_the_file_says(self):
        keys = ["file", "size", "hash", "width", "height", "minutes", "players", "description"]
        for row in self.rows:
            data = read_bytes(FOLDER, row["file"])
            self.assertEqual(list(row), keys, row["file"])
            self.assertEqual(row["size"], len(data), row["file"])
            self.assertEqual(row["hash"], community_maps.fnv1a64(data), row["file"])
            minutes, description = community_maps.header(data)
            self.assertEqual((row["minutes"], row["description"]), (minutes, description), row["file"])

    def test_the_file_is_plain_ascii_with_one_final_newline(self):
        raw = read_bytes(FOLDER, "maps.json")
        self.assertTrue(raw.isascii())                  # (a description's accented letter is written \\u00e9: the file reads the same on every computer)
        self.assertTrue(raw.endswith(b"}\n") and not raw.endswith(b"\n\n") and b"\r" not in raw)

    def test_the_numbers_are_those_of_a_map(self):
        for row in self.rows:
            self.assertTrue(10 <= row["width"] <= 200 and 10 <= row["height"] <= 200, row["file"])
            self.assertTrue(1 <= row["minutes"] <= 120, row["file"])
            self.assertIn(row["players"], (2, 3, 4), row["file"])
            self.assertRegex(row["hash"], r"^[0-9a-f]{16}$")

    def test_the_readme_says_the_numbers_of_the_folder(self):
        with open(os.path.join(FOLDER, "README.md"), encoding="utf-8") as f:
            text = f.read()
        different = len({r["hash"] for r in self.rows})
        self.assertIn("holds the **%d** of its 586 maps" % len(self.rows), text)
        self.assertIn("%d of the %d files are different maps; the other %d are the same map" % (different, len(self.rows), len(self.rows) - different), text)
        self.assertIn("The %d that remain" % len(self.rows), text)
        self.assertIn("%d were not taken in" % (586 - 6 - len(self.rows)), text)


class TheCheck(unittest.TestCase):
    """`community_maps.py verify` on a made-up folder and report: it finds what it must."""

    def setUp(self):
        import tempfile
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.src = os.path.join(tmp.name, "src")
        self.folder = os.path.join(tmp.name, "folder")
        os.makedirs(self.src)
        self.data = (8).to_bytes(4, "little") + b"\0" * 4 + (10).to_bytes(2, "little") + b"A made-up map\0" + b"\0" * 20_000
        self.report = [entry(name="Made Up.lvl")]
        with open(os.path.join(self.src, "Made Up.lvl"), "wb") as f:
            f.write(self.data)
        community_maps.build(self.src, self.report, self.folder)

    def test_a_folder_that_was_built_passes(self):
        self.assertEqual(community_maps.verify(self.folder, self.report), [])

    def test_a_map_that_fails_the_rule_is_named(self):
        bad = [entry(name="Made Up.lvl", problems=[{"kind": "tile_outside_dictionary"}])]
        self.assertEqual(community_maps.verify(self.folder, bad), ["Made Up.lvl: tile outside the dictionary"])

    def test_a_file_that_the_report_does_not_know_is_found(self):
        with open(os.path.join(self.folder, "Another.lvl"), "wb") as f:
            f.write(self.data)
        found = community_maps.verify(self.folder, self.report)
        self.assertTrue(any("different files" in p for p in found), found)
        self.assertTrue(any("maps.json" in p for p in found), found)

    def test_a_line_that_is_not_what_the_file_says_is_found(self):
        path = os.path.join(self.folder, "maps.json")
        with open(path, encoding="utf-8") as f:
            listed = json.load(f)
        listed["maps"][0]["hash"] = "0" * 16
        with open(path, "w", encoding="utf-8") as f:
            json.dump(listed, f)
        self.assertEqual(community_maps.verify(self.folder, self.report), ["Made Up.lvl: its line in maps.json is not what the file and the sweep say"])

    def test_an_original_map_in_the_folder_is_found(self):
        with open(os.path.join(self.folder, "TINY.LVL"), "wb") as f:
            f.write(self.data)
        found = community_maps.verify(self.folder, self.report + [entry(name="TINY.LVL")])
        self.assertTrue(any("TINY.LVL: an original map" in p for p in found), found)

    def test_a_description_with_an_accented_letter_is_written_in_ascii_and_read_back(self):
        data = self.data[:10] + b"Caf\xe9 des fourmis\0" + self.data[30:]
        with open(os.path.join(self.src, "Cafe.lvl"), "wb") as f:
            f.write(data)
        dest = os.path.join(os.path.dirname(self.src), "dest")
        community_maps.build(self.src, [entry(name="Cafe.lvl")], dest)
        raw = read_bytes(dest, "maps.json")
        self.assertTrue(raw.isascii())
        self.assertIn(b"Caf\\u00e9 des fourmis", raw)
        self.assertEqual(json.loads(raw)["maps"][0]["description"], "Caf\xe9 des fourmis")

    def test_build_takes_in_only_what_passes_and_never_an_original(self):
        for name in ("Good.lvl", "Bad.lvl", "TINY.LVL"):
            with open(os.path.join(self.src, name), "wb") as f:
                f.write(self.data)
        dest = os.path.join(os.path.dirname(self.src), "dest")
        report = [entry(name="Good.lvl"), entry(name="Bad.lvl", loads=False), entry(name="TINY.LVL")]
        self.assertEqual(community_maps.build(self.src, report, dest), 1)
        self.assertEqual(sorted(os.listdir(dest)), ["Good.lvl", "maps.json"])


if __name__ == "__main__":
    unittest.main()
