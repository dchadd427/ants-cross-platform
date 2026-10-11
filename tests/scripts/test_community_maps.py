#!/usr/bin/env python3
"""The players' maps of the repository (Community-Maps/, tools/community_maps.py; run by ./run_tests.sh --fast and by the CI).

What is read here from the files themselves (the engine's plays of them are suite 2.18.1 of ./run_tests.sh: map_sweep over the folder, then `community_maps.py verify`):
  - the rule that decides which maps of a collection are taken in: every reason, in the order it applies, on made-up report entries and made-up bytes (each program marker, an email
    address that does not keep a map out, each of the six original names, a copy of an original under another name, a name the protocol refuses, a report without the plays that the
    sweep makes);
  - the tool as a program: `repair`, `build`, `discards` and `verify` through `main` and as a process (what they print and their exit status 0 / 1 / 2), the repaired maps (taken in
    only when the sweep of the repaired file passes, with the sentence that says what was changed), and `verify` on made-up folders: it finds a rule broken, a file the report does not
    know, one that is missing, a hash that is not the file's, a line of maps.json that differs in any field, a bad `repaired` sentence, a list out of order, a missing or unreadable
    maps.json, a copy of an original;
  - the folder: every file is a level the protocol can name (printable ASCII, no path or Windows-forbidden character, at most 64 characters, `.lvl` or `.LVL` and no other case of the
    extension: src/ants_net/protocol.cpp) and no name clashes with another on a computer that ignores the case of names, no file is one of Original-Ants/Maps under any name;
    no email address, no program and no byte of the folder's files is text to git (-text);
  - maps.json: it lists exactly the files, in the order of their names, with the size and the FNV-1a 64 hash of each, the header's minutes and description, a plausible grid and, for
    a repaired map, the sentence that says what was changed (a repair is not applied twice: repairing a repaired file finds nothing to do);
  - the README's numbers are the folder's (and the other documents that give them).
"""
import contextlib
import copy
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools"))
import community_maps  # noqa: E402
import repair_maps  # noqa: E402

FOLDER = os.path.join(REPO, "Community-Maps")
ORIGINAL_FOLDER = os.path.join(REPO, "Original-Ants", "Maps")
TOOL = os.path.join(REPO, "tools", "community_maps.py")
WINDOWS_DEVICES = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"}
# What the folder cannot say about itself: the collection it was taken from had 586 files, 7 of them the original game's maps (the six
# in Original-Ants/Maps by name, and FOOD.lvl, which has the bytes of TINY.LVL), and 21 were neither taken in nor repaired. Community-Maps/README.md gives all three; change them here and
# there together with the folder.
COLLECTION = 586
ORIGINALS = 7
LEFT_OUT = 21
SIX = ("GAUNTLET", "ISLANDS", "MEDIUM", "SMALL", "TINY", "TREASURE")


def read_bytes(*parts):
    with open(os.path.join(*parts), "rb") as f:
        return f.read()


def read_text(*parts):
    with open(os.path.join(*parts), encoding="utf-8") as f:
        return f.read()


def level_files():
    return sorted(n for n in os.listdir(FOLDER) if n.lower().endswith(".lvl"))


def play(roster, **changes):
    """One play of a sweep report's "runs": a good one unless a change says otherwise."""
    good = {"roster": roster, "status": "ok", "deterministic": True}
    good.update(changes)
    return good


def entry(**changes):
    """One object of a sweep report: a map that passes unless a change says otherwise (all four teams have places: both rosters were played)."""
    good = {"name": "Some Map.lvl", "name_ok": True, "loads": True, "playable": True, "problems": [], "width": 31, "height": 31, "start_markers": [4, 4, 4, 4],
            "hill_cells": [16, 16, 16, 16], "runs": [play(15), play(9)]}
    good.update(changes)
    return good


def protocol_accepts(name):
    """ants::net::valid_map_name (src/ants_net/protocol.cpp), written again."""
    if not 5 <= len(name) <= 64 or any(not 0x20 <= ord(c) <= 0x7E or c in '/\\:*?"<>|' for c in name) or name[0] == ".":
        return False
    return name[-4:] in (".LVL", ".lvl")


class TheRule(unittest.TestCase):
    PLAIN = b"\0\1\2\3" * 100 + b"Tiny map with no PowerUps\0"

    def reason_of(self, text, **changes):
        return community_maps.reason(entry(**changes), self.PLAIN + b"\0" + text + b"\0")

    def test_a_map_that_loads_and_plays_passes(self):
        self.assertIsNone(community_maps.reason(entry(), self.PLAIN))

    def test_every_reason_names_the_map_that_has_it(self):
        cases = [
            (entry(loads=False), self.PLAIN, "does not load"),
            (entry(playable=False), self.PLAIN, "not playable"),
            (entry(runs=[play(15), play(9, status="refused")]), self.PLAIN, "play fails"),
            (entry(runs=[play(15, status="crash")]), self.PLAIN, "play fails"),
            (entry(runs=[play(15, deterministic=False), play(9)]), self.PLAIN, "play fails"),
            (entry(problems=[{"kind": "tile_outside_dictionary", "severity": "warning"}]), self.PLAIN, "tile outside the dictionary"),
            (entry(), self.PLAIN + b"\0Reading SETVER.EXE file.\0", "program code inside"),
            (entry(), self.PLAIN + b"\0!This program cannot be run in DOS mode.\0", "program code inside"),
            (entry(name_ok=False), self.PLAIN, "name the protocol refuses"),
        ]
        for report, data, why in cases:
            self.assertEqual(community_maps.reason(report, data), why, why)

    def test_the_other_findings_of_the_sweep_do_not_keep_a_map_out(self):
        for kind in ("object_outside_grid", "mode_ignored", "egg_stock_missing", "waypoint_block_truncated", "food_outside_grid", "food_block_ended_early"):
            self.assertIsNone(community_maps.reason(entry(problems=[{"kind": kind, "severity": "warning"}]), self.PLAIN), kind)

    def test_a_map_with_no_play_on_record_does_not_pass(self):
        # (`map_sweep --skip-run` makes such a report: it loads the files and plays nothing)
        play_fails = community_maps.REASONS[2]
        self.assertEqual(community_maps.reason(entry(runs=[]), self.PLAIN), play_fails)
        self.assertEqual(community_maps.reason(entry(runs=[play(9)]), self.PLAIN), play_fails)             # (the roster of all four teams is the one that is always played)
        self.assertEqual(community_maps.reason(entry(runs=[play(1)]), self.PLAIN), play_fails)

    def test_green_and_black_alone_were_played_when_the_map_has_both(self):
        # tools/map_sweep.cpp: roster 15 always, roster 9 (teams 0 and 3) when MapInfo::has_team(0) and has_team(3): a start marker or a hill is enough
        play_fails = community_maps.REASONS[2]
        for markers, hills in (([4, 4, 4, 4], [16, 16, 16, 16]), ([4, 0, 0, 4], [0, 0, 0, 0]), ([0, 0, 0, 0], [16, 0, 0, 16]), ([4, 0, 0, 0], [0, 0, 0, 16])):
            self.assertEqual(community_maps.rosters_swept(entry(start_markers=markers, hill_cells=hills)), (15, 9))
            self.assertEqual(community_maps.reason(entry(start_markers=markers, hill_cells=hills, runs=[play(15)]), self.PLAIN), play_fails, (markers, hills))
            self.assertIsNone(community_maps.reason(entry(start_markers=markers, hill_cells=hills, runs=[play(15), play(9)]), self.PLAIN), (markers, hills))
        for markers, hills in (([0, 4, 4, 4], [0, 16, 16, 16]), ([4, 4, 4, 0], [16, 16, 16, 0]), ([4, 4, 0, 0], [16, 16, 0, 0]), ([0, 0, 0, 0], [0, 0, 0, 0])):
            self.assertEqual(community_maps.rosters_swept(entry(start_markers=markers, hill_cells=hills)), (15,))
            self.assertIsNone(community_maps.reason(entry(start_markers=markers, hill_cells=hills, runs=[play(15)]), self.PLAIN), (markers, hills))

    def test_an_email_address_does_not_keep_a_map_out(self):
        # (the owner's decision: an address in a map does not matter; only a program does)
        self.assertFalse(hasattr(community_maps, "EMAIL"))
        for text in (b"someone@example.com", b"ruth@earthlink.net", b"x@y.org", b"first.last+tag@mail.example.co.uk", b"mail me at bob@earthlink.net or call", b"!!by Bob (bob@earthlink.net)",
                     b"price @ 5 each", b"user@localhost", b"@handle"):
            self.assertIsNone(self.reason_of(text), text)

    def test_every_marker_of_a_program_keeps_a_map_out(self):
        program = community_maps.REASONS[4]
        for text in (b"This program cannot be run in DOS mode.", b"this program requires Microsoft Windows", b"THIS PROGRAM CANNOT RUN", b"Reading SETVER.EXE file.", b"setup.exe",
                     b"MSVBVM60.DLL", b"vxd.sys", b"autoexec.bat", b"call kernel32 now", b"KERNEL32", b"Kernel32.lib"):
            self.assertEqual(self.reason_of(text), program, text)

    def test_what_only_looks_like_a_program_is_left_alone(self):
        for text in (b"web addresses: antsownz.com", b"the exe file", b"a.system", b"my.batman", b"a dll of mine", b"this program is mine", b"programs and kernels", b"x.dllx"):
            self.assertIsNone(self.reason_of(text), text)

    def test_web_addresses_and_a_dotted_com_are_no_programs(self):
        self.assertIsNone(community_maps.reason(entry(), self.PLAIN + b"\0An Original Map by X   antsownz.com\0!Visit pbcguild.cjb.net ...\0"))

    def test_text_is_read_one_printable_run_at_a_time(self):
        # (a NUL or any other byte ends a string: pieces of an address or of a name do not make one)
        for text in (b"setup.e\0xe", b"kernel\x0032", b"this program\0cannot", b"set\xffup.e\xffxe"):
            self.assertIsNone(self.reason_of(text), text)

    def test_the_first_reason_wins_in_the_order_of_the_list(self):
        program = b"\0Reading SETVER.EXE file.\0"
        self.assertEqual(community_maps.reason(entry(loads=False, playable=False), self.PLAIN + program), community_maps.REASONS[0])
        self.assertEqual(community_maps.reason(entry(playable=False, runs=[]), self.PLAIN + program), community_maps.REASONS[1])
        self.assertEqual(community_maps.reason(entry(runs=[], problems=[{"kind": "tile_outside_dictionary"}]), self.PLAIN), community_maps.REASONS[2])
        self.assertEqual(community_maps.reason(entry(problems=[{"kind": "tile_outside_dictionary"}]), self.PLAIN + program), community_maps.REASONS[3])
        self.assertEqual(community_maps.REASONS, ("does not load", "not playable", "play fails", "tile outside the dictionary", "program code inside", "name the protocol refuses"))

    def test_a_program_is_named_before_a_name(self):
        self.assertEqual(community_maps.reason(entry(name_ok=False), self.PLAIN + b"\0Reading SETVER.EXE file.\0"), community_maps.REASONS[4])
        self.assertEqual(community_maps.reason(entry(name_ok=False), self.PLAIN + b"\0a@b.cd\0"), community_maps.REASONS[5])

    def test_the_original_six_are_not_taken_in_whatever_their_case(self):
        self.assertEqual(sorted(community_maps.ORIGINAL_NAMES), sorted(SIX))
        for base in SIX:
            for name in (base + ".LVL", base + ".lvl", base.lower() + ".lvl", base.lower() + ".LVL", base.title() + ".lvl", base.title() + ".LVL"):
                self.assertTrue(community_maps.is_original(name), name)
                self.assertEqual(community_maps.original_of(name, b"other bytes", {}), "its name", name)
        for name in ("Tiny Islands.lvl", "TinyIslands.lvl", "TINY2.lvl", "XTINY.lvl", "TINY .lvl", "TINY.LVL.lvl", "SMALL MAP.lvl", "MEDIUM2.lvl", "GAUNTLET 2.lvl", "Treasures.lvl", "ISLAND.lvl", ""):
            self.assertFalse(community_maps.is_original(name), name)

    def test_the_six_names_are_the_files_of_the_original_game(self):
        self.assertEqual(sorted(os.path.splitext(n)[0] for n in os.listdir(ORIGINAL_FOLDER)), sorted(SIX))

    def test_a_copy_of_an_original_under_another_name_is_an_original(self):
        tiny = b"the bytes of a made-up original" * 40
        originals = {tiny: "TINY.LVL", b"another" * 40: "SMALL.LVL"}
        self.assertEqual(community_maps.original_of("FOOD.lvl", tiny, originals), "the bytes of TINY.LVL")
        self.assertTrue(community_maps.is_original("FOOD.lvl", tiny, originals))
        self.assertIsNone(community_maps.original_of("FOOD.lvl", tiny + b"\0", originals))        # (one byte more is another map)
        self.assertIsNone(community_maps.original_of("FOOD.lvl", tiny[:-1], originals))
        self.assertFalse(community_maps.is_original("FOOD.lvl", tiny))                              # (without the originals only the names tell)
        self.assertEqual(community_maps.original_of("tiny.lvl", b"x", originals), "its name")

    def test_the_originals_are_read_from_the_folder_of_the_game_and_without_it_the_names_alone_tell(self):
        originals = community_maps.load_originals()
        self.assertEqual(sorted(originals.values()), sorted(os.listdir(ORIGINAL_FOLDER)))
        self.assertEqual(len(originals), 6)
        for data, name in originals.items():
            self.assertEqual(data, read_bytes(ORIGINAL_FOLDER, name))
        self.assertEqual(community_maps.ORIGINALS_DIR, ORIGINAL_FOLDER)
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(community_maps.load_originals(os.path.join(tmp, "nowhere")), {})
            self.assertEqual(community_maps.load_originals(tmp), {})

    def test_the_header_is_what_the_setup_screen_shows(self):
        data = (8).to_bytes(4, "little") + (1).to_bytes(4, "little") + (12).to_bytes(2, "little") + b"One person's trash...  \0" + b"\xff" * 20
        self.assertEqual(community_maps.header(data), (12, "One person's trash..."))
        self.assertEqual(community_maps.header(data[:10] + b"\x07tab\0" + b"x" * 30)[1], "tab")      # a control character is not shown
        self.assertEqual(community_maps.header(data[:8] + (300).to_bytes(2, "little") + data[10:])[0], 300)        # (two bytes, not one)

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


class TheNameRule(unittest.TestCase):
    """The name of a map must be one that a room can use: the protocol's rule, and only that."""

    def test_the_rule_written_here_is_the_protocols(self):
        with open(os.path.join(REPO, "src", "ants_net", "protocol.cpp"), encoding="utf-8") as f:
            source = f.read()
        self.assertIn('return tail == ".LVL" || tail == ".lvl";', source)             # the extension in two cases only: `.Lvl` and `.lVl` are refused
        self.assertIn("name.size() < 5 || name.size() > kMaxMapNameChars", source)
        self.assertIn("if (name[0] == '.') return false;", source)
        self.assertIn("inline constexpr size_t kMaxMapNameChars = 64;", read_text(REPO, "include", "ants_net", "protocol.hpp"))

    def test_it_accepts_and_refuses_what_the_protocol_does(self):
        for name in ("a.lvl", "A.LVL", "My Map!!.lvl", "a..b.lvl", "!!!~#&'.LVL", "x" * 60 + ".lvl", "TREASURE.LVL"):
            self.assertTrue(protocol_accepts(name), name)
        for name in (".lvl", "a.Lvl", "a.lVl", "a.LvL", "a.LVl", "MAP.Lvl", "map.lvl.txt", "map", ".hidden.lvl", "a:b.lvl", "a/b.lvl", "a\\b.lvl", 'a"b.lvl', "a|b.lvl", "a*b.lvl",
                     "a?b.lvl", "a<b.lvl", "a>b.lvl", "café.lvl", "tab\t.lvl", "x" * 61 + ".lvl", ""):
            self.assertFalse(protocol_accepts(name), name)


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
            self.assertTrue(protocol_accepts(name), name)
            self.assertTrue(re.fullmatch(r"[\x20-\x7e]{1,60}\.(lvl|LVL)", name) and not re.search(r'[/\\:*?"<>|]', name) and not name.startswith("."), name)
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

    def test_no_file_is_a_copy_of_an_original_map_under_any_name(self):
        originals = community_maps.load_originals()
        self.assertEqual(len(originals), 6)
        for name in self.names:
            self.assertNotIn(read_bytes(FOLDER, name), originals, "%s has the bytes of %s" % (name, originals.get(read_bytes(FOLDER, name))))

    def test_no_file_holds_a_program(self):
        for name in self.names:
            for text in community_maps.PRINTABLE_RUN.finditer(read_bytes(FOLDER, name)):
                text = text.group().decode("ascii")
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
            self.assertEqual(list(row), keys + (["repaired"] if "repaired" in row else []), row["file"])         # (a repaired map has one field more, the last)
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

    def test_a_repaired_line_has_a_sentence_and_the_others_have_none(self):
        repaired = [r for r in self.rows if "repaired" in r]
        self.assertGreater(len(repaired), 0)
        for row in repaired:
            self.assertRegex(row["repaired"], r"^[A-Z0-9][\x20-\x7e]{10,%d}\.$" % (community_maps.REPAIRED_FIELD_MAX - 1), row["file"])

    def test_a_repair_is_not_applied_twice(self):
        # the folder's repaired files are repaired already: the repairs find nothing in them (a file that a repair still changed would be one that the folder should hold differently)
        for row in self.rows:
            if "repaired" in row:
                fixed, notes = repair_maps.repair(read_bytes(FOLDER, row["file"]))
                self.assertEqual((fixed, notes), (None, []), row["file"])

    def test_the_readme_says_the_numbers_of_the_folder(self):
        text = read_text(FOLDER, "README.md")
        different = len({r["hash"] for r in self.rows})
        repaired = [r for r in self.rows if "repaired" in r]
        kinds = (sum(r["repaired"].startswith("Text-transfer damage") for r in repaired), sum("start marker" in r["repaired"] for r in repaired),
                 sum(r["repaired"].startswith("Tiles outside the dictionary") for r in repaired))
        self.assertEqual(sum(kinds), len(repaired))                                            # (three kinds of damage, each map one of them)
        self.assertEqual(COLLECTION - ORIGINALS - LEFT_OUT, len(self.rows))
        self.assertIn("holds the **%d** of its %d maps" % (len(self.rows), COLLECTION), text)
        self.assertIn("%d byte for byte as they were collected, and %d that were damaged and are repaired" % (len(self.rows) - len(repaired), len(repaired)), text)
        self.assertIn("%d of the %d files are different maps; the other %d are the same map" % (different, len(self.rows), len(self.rows) - different), text)
        self.assertIn("## The %d repaired maps" % len(repaired), text)
        self.assertIn("The %d that are in the folder load and play" % len(self.rows), text)
        self.assertIn("Of the collection's %d files: %d are the original game's maps" % (COLLECTION, ORIGINALS), text)
        self.assertIn("%d were not taken in" % LEFT_OUT, text)
        repaired_part, left_part = text.split("## What was left out, and why")
        table = lambda part: [int(m.group(1)) for m in re.finditer(r"(?m)^\| (\d+) \|", part)]
        self.assertEqual(table(repaired_part), list(kinds))                                    # (the rows of "the repaired maps" are the kinds of repair, in this order)
        self.assertEqual(sum(table(left_part)), LEFT_OUT)                                      # (the rows of "what was left out" add up to the 21)
        for row in repaired:
            self.assertIn("`%s`" % row["file"][:-4], repaired_part, row["file"])               # (every repaired map is named)

    def test_the_other_documents_give_the_same_numbers(self):
        n = len(self.rows)
        self.assertEqual(len(re.findall(r"Community-Maps/` holds the %d that pass" % n, read_text(REPO, "docs", "BOTS.md"))), 1)
        self.assertNotIn("email", read_text(REPO, "AGENTS.md").split("**The original game.**")[1].split("\n")[0])
        self.assertNotIn("email", read_text(REPO, "docs", "TESTING.md").split("| 2.18.1 Community maps")[1].split("\n")[0])
        self.assertIn("the repository holds only the part of the maps that is in `Community-Maps/`", read_text(REPO, "docs", "GAME_REVERSE_ENGINEERING.md"))


class Made(unittest.TestCase):
    """A made-up source folder, its sweep report and the folder built from it; `main` and the functions are tried on them."""

    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.tmp = tmp.name
        self.src = os.path.join(self.tmp, "src")
        self.folder = os.path.join(self.tmp, "folder")
        self.originals_dir = os.path.join(self.tmp, "originals")
        os.makedirs(self.src)
        os.makedirs(self.originals_dir)
        self.data = self.map_bytes("A made-up map")
        self.original = self.map_bytes("An original")
        with open(os.path.join(self.originals_dir, "ORIG.LVL"), "wb") as f:
            f.write(self.original)
        self.files = {}
        self.report = [self.add("Made Up.lvl")]
        community_maps.build(self.src, self.report, self.folder, originals={})

    @staticmethod
    def map_bytes(description):
        return (8).to_bytes(4, "little") + b"\0" * 4 + (10).to_bytes(2, "little") + description.encode("latin-1") + b"\0" * (30 - len(description)) + b"\0" * 20_000

    def add(self, name, data=None, **changes):
        """Writes a file into the source folder; returns its object of the sweep report (with the file's hash, as the sweep gives it)."""
        data = self.data if data is None else data
        self.files[name] = data
        with open(os.path.join(self.src, name), "wb") as f:
            f.write(data)
        return entry(name=name, hash=community_maps.fnv1a64(data), **changes)

    def two_maps(self):
        """A folder of two maps and the report of both."""
        report = [self.add("Alpha.lvl", self.map_bytes("Alpha")), self.add("Beta.lvl", self.map_bytes("Beta"))]
        folder = os.path.join(self.tmp, "two")
        community_maps.build(self.src, report, folder, originals={})
        return folder, report

    def report_file(self, report, name="report.json"):
        path = os.path.join(self.tmp, name)
        with open(path, "w", encoding="utf-8") as f:
            json.dump({"maps": report}, f)
        return path

    def listing(self, folder=None):
        return json.loads(read_text(folder or self.folder, "maps.json"))

    def write_listing(self, listed, folder=None):
        with open(os.path.join(folder or self.folder, "maps.json"), "w", encoding="utf-8") as f:
            json.dump(listed, f)

    def run_main(self, *args, originals_dir=None):
        """(exit status, what it printed, what it wrote to stderr) of `community_maps.py ARGS`; the originals are those of the made-up folder unless another is named."""
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err), mock.patch.object(community_maps, "ORIGINALS_DIR", originals_dir or self.originals_dir):
            code = community_maps.main(["community_maps.py"] + list(args))
        return code, out.getvalue(), err.getvalue()


class TheCheck(Made):
    """`community_maps.verify` on a made-up folder and report: it finds what it must."""

    def test_a_folder_that_was_built_passes(self):
        self.assertEqual(community_maps.verify(self.folder, self.report, originals={}), [])

    def test_a_map_that_fails_the_rule_is_named(self):
        bad = [self.add("Made Up.lvl", problems=[{"kind": "tile_outside_dictionary"}])]
        self.assertEqual(community_maps.verify(self.folder, bad, originals={}), ["Made Up.lvl: tile outside the dictionary"])

    def test_a_map_with_no_play_on_record_does_not_pass(self):
        # (a report of `map_sweep --skip-run`: the files load and nothing was played)
        self.assertEqual(community_maps.verify(self.folder, [dict(self.report[0], runs=[])], originals={}), ["Made Up.lvl: play fails"])
        self.assertEqual(community_maps.verify(self.folder, [dict(self.report[0], runs=[play(9)])], originals={}), ["Made Up.lvl: play fails"])

    def test_a_name_that_the_protocol_refuses_is_found(self):
        self.assertEqual(community_maps.verify(self.folder, [dict(self.report[0], name_ok=False)], originals={}), ["Made Up.lvl: name the protocol refuses"])

    def test_a_file_that_the_report_does_not_know_is_found(self):
        with open(os.path.join(self.folder, "Another.lvl"), "wb") as f:
            f.write(self.data)
        found = community_maps.verify(self.folder, self.report, originals={})
        self.assertTrue(any("different files" in p and "only in the folder: Another.lvl" in p for p in found), found)
        self.assertTrue(any("maps.json" in p for p in found), found)

    def test_a_file_that_is_listed_and_in_the_report_but_not_in_the_folder_is_a_problem_and_no_crash(self):
        folder, report = self.two_maps()
        os.remove(os.path.join(folder, "Beta.lvl"))
        found = community_maps.verify(folder, report, originals={})
        self.assertEqual(len(found), 2, found)
        self.assertTrue("different files" in found[0] and "only in the report: Beta.lvl" in found[0], found)
        self.assertEqual(found[1], "maps.json does not list exactly the folder's maps in order")
        # whatever else is wrong is still told
        found = community_maps.verify(folder, [report[0], dict(report[1], loads=False)], originals={})
        self.assertEqual(len(found), 2, found)
        found = community_maps.verify(folder, [dict(report[0], loads=False), report[1]], originals={})
        self.assertEqual(len(found), 3, found)
        self.assertIn("Alpha.lvl: does not load", found)

    def test_many_files_missing_are_counted_not_listed_in_full(self):
        found = community_maps.verify(self.folder, self.report + [entry(name="N%d.lvl" % i) for i in range(8)], originals={})
        self.assertIn("(8 in all)", found[0])
        self.assertNotIn("N7.lvl", found[0])

    def test_a_missing_list_is_a_problem_and_no_crash(self):
        os.remove(os.path.join(self.folder, "maps.json"))
        self.assertEqual(community_maps.verify(self.folder, self.report, originals={}), ["maps.json is missing"])
        bad = [self.add("Made Up.lvl", loads=False)]
        self.assertEqual(community_maps.verify(self.folder, bad, originals={}), ["Made Up.lvl: does not load", "maps.json is missing"])

    def test_a_list_that_cannot_be_read_is_a_problem_and_no_crash(self):
        for text in ("not json at all", "", "[]", '{"maps": 5}', '{"maps": [{"size": 1}]}', '{"maps": ["a"]}', '{"format": 1}', '{"maps": [{"file": 3}]}'):
            with open(os.path.join(self.folder, "maps.json"), "w", encoding="utf-8") as f:
                f.write(text)
            found = community_maps.verify(self.folder, self.report, originals={})
            self.assertEqual(len(found), 1, (text, found))
            self.assertTrue(found[0].startswith("maps.json cannot be read"), (text, found))

    def test_a_hash_in_the_report_that_is_not_the_files_is_found(self):
        wrong = dict(self.report[0], hash="0" * 16)
        self.assertEqual(community_maps.verify(self.folder, [wrong], originals={}), ["Made Up.lvl: the hash in the sweep report is not the file's (the report is of another file)"])
        no_hash = {k: v for k, v in self.report[0].items() if k != "hash"}
        self.assertEqual(len(community_maps.verify(self.folder, [no_hash], originals={})), 1)
        # (the report of another file with the same name: the folder's file was changed after the sweep)
        with open(os.path.join(self.folder, "Made Up.lvl"), "ab") as f:
            f.write(b"\0")
        found = community_maps.verify(self.folder, self.report, originals={})
        self.assertIn("Made Up.lvl: the hash in the sweep report is not the file's (the report is of another file)", found)
        self.assertIn("Made Up.lvl: its line in maps.json is not what the file and the sweep say", found)

    def test_a_line_that_is_not_what_the_file_says_is_found(self):
        listed = self.listing()
        listed["maps"][0]["hash"] = "0" * 16
        self.write_listing(listed)
        self.assertEqual(community_maps.verify(self.folder, self.report, originals={}), ["Made Up.lvl: its line in maps.json is not what the file and the sweep say"])

    def test_every_field_of_a_line_is_checked(self):
        pristine = self.listing()
        self.assertEqual(list(pristine["maps"][0]), ["file", "size", "hash", "width", "height", "minutes", "players", "description"])
        line = ["Made Up.lvl: its line in maps.json is not what the file and the sweep say"]
        for field, wrong in (("size", 1), ("hash", "0" * 16), ("width", 32), ("height", 32), ("minutes", 11), ("players", 3), ("description", "Another one")):
            listed = copy.deepcopy(pristine)                                              # (one field wrong at a time)
            listed["maps"][0][field] = wrong
            self.write_listing(listed)
            self.assertEqual(community_maps.verify(self.folder, self.report, originals={}), line, field)
        listed = copy.deepcopy(pristine)
        del listed["maps"][0]["players"]
        self.write_listing(listed)
        self.assertEqual(community_maps.verify(self.folder, self.report, originals={}), line, "a field that is missing")
        listed = copy.deepcopy(pristine)
        listed["maps"][0]["extra"] = 1
        self.write_listing(listed)
        self.assertEqual(community_maps.verify(self.folder, self.report, originals={}), line, "a field that is added")
        listed = copy.deepcopy(pristine)
        listed["maps"][0]["file"] = "Other.lvl"
        self.write_listing(listed)
        self.assertEqual(community_maps.verify(self.folder, self.report, originals={}), ["maps.json does not list exactly the folder's maps in order"], "the name of the file")
        self.write_listing(pristine)
        self.assertEqual(community_maps.verify(self.folder, self.report, originals={}), [])

    def test_the_players_and_the_grid_in_the_list_are_those_of_the_report(self):
        report = [dict(self.report[0], start_markers=[4, 4, 0, 0], hill_cells=[16, 16, 0, 0])]
        self.assertEqual(community_maps.verify(self.folder, report, originals={}), ["Made Up.lvl: its line in maps.json is not what the file and the sweep say"])    # (4 players listed)

    def test_a_list_out_of_order_is_found(self):
        folder, report = self.two_maps()
        self.assertEqual(community_maps.verify(folder, report, originals={}), [])
        listed = self.listing(folder)
        listed["maps"].reverse()
        self.write_listing(listed, folder)
        self.assertEqual(community_maps.verify(folder, report, originals={}), ["maps.json does not list exactly the folder's maps in order"])

    def test_a_list_that_lacks_a_map_or_has_one_twice_is_found(self):
        folder, report = self.two_maps()
        listed = self.listing(folder)
        listed["maps"] = listed["maps"][:1]
        self.write_listing(listed, folder)
        self.assertEqual(community_maps.verify(folder, report, originals={}), ["maps.json does not list exactly the folder's maps in order"])
        listed["maps"] = listed["maps"] * 2
        self.write_listing(listed, folder)
        self.assertEqual(community_maps.verify(folder, report, originals={}), ["maps.json does not list exactly the folder's maps in order"])

    def test_an_original_map_in_the_folder_is_found(self):
        with open(os.path.join(self.folder, "TINY.LVL"), "wb") as f:
            f.write(self.data)
        found = community_maps.verify(self.folder, self.report + [entry(name="TINY.LVL", hash=community_maps.fnv1a64(self.data))], originals={})
        self.assertIn("TINY.LVL: an original map belongs in Original-Ants/Maps", found)

    def test_a_copy_of_an_original_under_another_name_is_found(self):
        report = [self.add("Made Up.lvl"), self.add("FOOD.lvl", self.original)]
        community_maps.build(self.src, report, self.folder, originals={})                       # (built without the originals: the copy is in the folder)
        self.assertEqual(community_maps.verify(self.folder, report, originals={}), [])
        self.assertEqual(community_maps.verify(self.folder, report, originals={self.original: "ORIG.LVL"}), ["FOOD.lvl: an original map belongs in Original-Ants/Maps"])
        # the originals of the repository (the default), here the real Tiny
        tiny = read_bytes(ORIGINAL_FOLDER, "TINY.LVL")
        report = [self.add("Made Up.lvl"), self.add("FOOD.lvl", tiny)]
        community_maps.build(self.src, report, self.folder, originals={})
        self.assertEqual(community_maps.verify(self.folder, report), ["FOOD.lvl: an original map belongs in Original-Ants/Maps"])

    def test_a_description_with_an_accented_letter_is_written_in_ascii_and_read_back(self):
        report = [self.add("Cafe.lvl", self.data[:10] + b"Caf\xe9 des fourmis\0" + self.data[30:])]
        dest = os.path.join(self.tmp, "dest")
        community_maps.build(self.src, report, dest, originals={})
        raw = read_bytes(dest, "maps.json")
        self.assertTrue(raw.isascii())
        self.assertIn(b"Caf\\u00e9 des fourmis", raw)
        self.assertEqual(json.loads(raw)["maps"][0]["description"], "Caf\xe9 des fourmis")


class TheBuild(Made):
    def test_build_takes_in_only_what_passes_and_never_an_original(self):
        report = [self.add("Good.lvl"), self.add("Bad.lvl", loads=False), self.add("TINY.LVL"), self.add("Small.lvl"), self.add("Copy.lvl", self.original), self.add("Bad Name.Lvl", name_ok=False)]
        dest = os.path.join(self.tmp, "dest")
        self.assertEqual(community_maps.build(self.src, report, dest, originals={self.original: "ORIG.LVL"}), 1)
        self.assertEqual(sorted(os.listdir(dest)), ["Good.lvl", "maps.json"])
        self.assertEqual([r["file"] for r in self.listing(dest)["maps"]], ["Good.lvl"])

    def test_without_the_originals_only_the_names_tell(self):
        report = [self.add("Good.lvl"), self.add("TINY.LVL"), self.add("Copy.lvl", self.original)]
        dest = os.path.join(self.tmp, "dest")
        self.assertEqual(community_maps.build(self.src, report, dest, originals={}), 2)
        self.assertEqual(sorted(os.listdir(dest)), ["Copy.lvl", "Good.lvl", "maps.json"])

    def test_a_copy_of_the_real_tiny_is_not_taken_in(self):
        report = [self.add("Good.lvl"), self.add("FOOD.lvl", read_bytes(ORIGINAL_FOLDER, "TINY.LVL"))]
        dest = os.path.join(self.tmp, "dest")
        self.assertEqual(community_maps.build(self.src, report, dest), 1)
        self.assertEqual(sorted(os.listdir(dest)), ["Good.lvl", "maps.json"])

    def test_the_files_are_copied_byte_for_byte_and_the_list_is_in_the_order_of_the_names(self):
        report = [self.add("b.lvl", self.map_bytes("B")), self.add("B2.lvl", self.map_bytes("B2")), self.add("A.lvl", self.map_bytes("A"))]
        dest = os.path.join(self.tmp, "dest")
        community_maps.build(self.src, report, dest, originals={})
        self.assertEqual([r["file"] for r in self.listing(dest)["maps"]], ["A.lvl", "B2.lvl", "b.lvl"])
        for name in ("A.lvl", "B2.lvl", "b.lvl"):
            self.assertEqual(read_bytes(dest, name), self.files[name])


class TheCommandLine(Made):
    """What the three commands print and their exit status, through `main` and as a process."""

    def test_build_says_how_many_maps_it_took_in(self):
        report = [self.add("Good.lvl"), self.add("Bad.lvl", loads=False)]
        dest = os.path.join(self.tmp, "dest")
        code, out, err = self.run_main("build", self.src, self.report_file(report), dest)
        self.assertEqual((code, out, err), (0, "1 maps taken in\n", ""))
        self.assertEqual(sorted(os.listdir(dest)), ["Good.lvl", "maps.json"])

    def test_discards_names_each_map_with_its_reason_and_counts_them(self):
        report = [self.add("Good.lvl"), self.add("Bad.lvl", loads=False), self.add("Mail.lvl", self.map_bytes("by me") + b"\0write to me at someone@example.com\0"),
                  self.add("Prog.lvl", self.map_bytes("by me") + b"\0Reading SETVER.EXE file.\0"), self.add("Copy.lvl", self.original), self.add("TINY.LVL"),
                  self.add("Odd.Lvl", name_ok=False), self.add("Idle.lvl", runs=[])]
        code, out, err = self.run_main("discards", self.src, self.report_file(report))
        self.assertEqual(code, 0)
        self.assertEqual(out.splitlines(), ["does not load\tBad.lvl", "program code inside\tProg.lvl", "original map (the bytes of ORIG.LVL)\tCopy.lvl", "original map (its name)\tTINY.LVL",
                                            "name the protocol refuses\tOdd.Lvl", "play fails\tIdle.lvl"])                          # (Mail.lvl, with an email address, is taken in)
        self.assertEqual(err.splitlines(), ["1\tdoes not load", "0\tnot playable", "1\tplay fails", "0\ttile outside the dictionary", "1\tprogram code inside",
                                            "1\tname the protocol refuses", "2\toriginal map"])

    def test_an_original_is_not_counted_as_a_reason(self):
        report = [self.add("Copy.lvl", self.original), self.add("Small.lvl", loads=False)]
        code, out, err = self.run_main("discards", self.src, self.report_file(report))
        self.assertEqual(out.splitlines(), ["original map (the bytes of ORIG.LVL)\tCopy.lvl", "original map (its name)\tSmall.lvl"])
        self.assertIn("0\tdoes not load", err.splitlines())                                       # (an original that does not load is an original, not a map that fails)
        self.assertIn("2\toriginal map", err.splitlines())

    def test_verify_says_nothing_and_exits_0_for_a_folder_that_passes(self):
        self.assertEqual(self.run_main("verify", self.folder, self.report_file(self.report)), (0, "", ""))

    def test_verify_prints_each_problem_on_a_line_and_exits_1(self):
        bad = [dict(self.report[0], problems=[{"kind": "tile_outside_dictionary"}], hash="0" * 16)]
        code, out, err = self.run_main("verify", self.folder, self.report_file(bad))
        self.assertEqual(code, 1)
        self.assertEqual(out, "Made Up.lvl: tile outside the dictionary\nMade Up.lvl: the hash in the sweep report is not the file's (the report is of another file)\n")
        self.assertEqual(err, "")

    def test_verify_exits_1_for_a_missing_file_and_a_missing_list_without_a_traceback(self):
        folder, report = self.two_maps()
        os.remove(os.path.join(folder, "Beta.lvl"))
        os.remove(os.path.join(folder, "maps.json"))
        code, out, err = self.run_main("verify", folder, self.report_file(report))
        self.assertEqual(code, 1)
        self.assertEqual(len(out.splitlines()), 2, out)
        self.assertIn("Beta.lvl", out)
        self.assertIn("maps.json is missing", out)
        self.assertEqual(err, "")

    def test_a_copy_of_an_original_is_found_by_verify_with_the_originals_of_the_repository(self):
        report = [self.add("Made Up.lvl"), self.add("FOOD.lvl", read_bytes(ORIGINAL_FOLDER, "TINY.LVL"))]
        community_maps.build(self.src, report, self.folder, originals={})
        code, out, err = self.run_main("verify", self.folder, self.report_file(report), originals_dir=ORIGINAL_FOLDER)
        self.assertEqual((code, out, err), (1, "FOOD.lvl: an original map belongs in Original-Ants/Maps\n", ""))

    def test_without_the_folder_of_the_originals_only_the_names_tell_and_it_is_said(self):
        tiny = read_bytes(ORIGINAL_FOLDER, "TINY.LVL")
        report = [self.add("Made Up.lvl"), self.add("FOOD.lvl", tiny), self.add("TINY.LVL", tiny)]
        missing = os.path.join(self.tmp, "nowhere")
        dest = os.path.join(self.tmp, "dest")
        code, out, err = self.run_main("build", self.src, self.report_file(report), dest, originals_dir=missing)
        self.assertEqual((code, out), (0, "2 maps taken in\n"))                                    # (by name TINY.LVL is out, the copy is not known without the bytes)
        self.assertEqual(sorted(os.listdir(dest)), ["FOOD.lvl", "Made Up.lvl", "maps.json"])
        self.assertIn("note:", err)
        self.assertIn("only the names tell", err)
        # the same with the folder: both are out
        code, out, err = self.run_main("build", self.src, self.report_file(report), dest + "2", originals_dir=ORIGINAL_FOLDER)
        self.assertEqual((code, out, err), (0, "1 maps taken in\n", ""))
        self.assertEqual(sorted(os.listdir(dest + "2")), ["Made Up.lvl", "maps.json"])
        code, out, err = self.run_main("discards", self.src, self.report_file(report), originals_dir=missing)
        self.assertEqual(out.splitlines(), ["original map (its name)\tTINY.LVL"])
        self.assertIn("note:", err)

    def test_a_usage_mistake_prints_the_usage_and_exits_2(self):
        report = self.report_file(self.report)
        for args in ((), ("verify",), ("verify", self.folder), ("verify", self.folder, report, "more"), ("build", self.src, report), ("build", self.src, report, "d", "x"),
                     ("discards", self.src), ("discards", self.src, report, "x"), ("discards", self.src, report, "x", "y", "z"), ("repair", self.src, report), ("repair", self.src, report, "d", "x"),
                     ("bogus", self.folder, report), ("--help",)):
            code, out, err = self.run_main(*args)
            self.assertEqual(code, 2, args)
            self.assertIn("usage: community_maps.py repair SRC_DIR SWEEP_REPORT REPAIRED_DIR", out, args)
            self.assertIn("community_maps.py build SRC_DIR SWEEP_REPORT DEST_DIR [REPAIRED_DIR REPAIRED_SWEEP_REPORT]", out, args)
            self.assertEqual(err, "", args)

    def test_a_report_that_cannot_be_read_exits_1_with_a_word_not_a_traceback(self):
        broken = os.path.join(self.tmp, "broken.json")
        with open(broken, "w", encoding="utf-8") as f:
            f.write("{not json")
        for path in (os.path.join(self.tmp, "nowhere.json"), broken, self.report_file({}, "dict.json")):
            for args in (("verify", self.folder, path), ("discards", self.src, path), ("build", self.src, path, os.path.join(self.tmp, "x"))):
                code, out, err = self.run_main(*args)
                self.assertEqual((code, out), (1, ""), args)
                self.assertIn("the sweep report cannot be read", err, args)
        self.assertFalse(os.path.exists(os.path.join(self.tmp, "x")))

    def run_tool(self, *args):
        done = subprocess.run([sys.executable, TOOL] + list(args), capture_output=True, text=True, timeout=120)
        return done.returncode, done.stdout, done.stderr

    def test_the_program_exits_with_the_status_of_the_command(self):
        report = self.report_file(self.report)
        self.assertEqual(self.run_tool("verify", self.folder, report), (0, "", ""))
        bad = self.report_file([dict(self.report[0], loads=False)], "bad.json")
        code, out, err = self.run_tool("verify", self.folder, bad)
        self.assertEqual((code, out, err), (1, "Made Up.lvl: does not load\n", ""))
        code, out, err = self.run_tool("verify", self.folder, os.path.join(self.tmp, "nowhere.json"))
        self.assertEqual(code, 1)
        self.assertNotIn("Traceback", err)
        code, out, err = self.run_tool()
        self.assertEqual(code, 2)
        self.assertIn("usage:", out)
        code, out, err = self.run_tool("verify", self.folder)
        self.assertEqual(code, 2)

    def test_the_program_builds_and_discards(self):
        report = self.report_file([self.add("Good.lvl"), self.add("Bad.lvl", loads=False)])
        dest = os.path.join(self.tmp, "dest")
        self.assertEqual(self.run_tool("build", self.src, report, dest), (0, "1 maps taken in\n", ""))
        self.assertEqual(self.run_tool("verify", dest, report)[0], 1)                              # (the report is of the source folder: Bad.lvl is not in dest)
        code, out, err = self.run_tool("discards", self.src, report)
        self.assertEqual((code, out), (0, "does not load\tBad.lvl\n"))
        self.assertIn("1\tdoes not load\n", err)
        self.assertTrue(err.endswith("0\toriginal map\n"))


class TheRepairs(Made):
    """The repaired maps: `repair` writes the files and their sentences, `build` takes in the ones whose own sweep passes, `discards` counts them, `verify` checks the sentence."""

    FIXED = b"the repaired bytes" * 10

    def fake_repair(self, data):
        """Stands for repair_maps.repair: the made-up maps are no levels. A file that holds b"BROKEN" is repaired; its sentence comes from two notes."""
        self.asked.append(data)
        return (self.FIXED + data[-4:], ["a thing was done", "another thing, too"]) if b"BROKEN" in data else (None, [])

    def setUp(self):
        super().setUp()
        self.asked = []
        patcher = mock.patch.object(community_maps.repair_maps, "repair", self.fake_repair)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.broken = self.map_bytes("A broken map") + b"BROKEN"
        self.report = [self.add("Good.lvl"), self.add("Broken.lvl", self.broken, loads=False), self.add("Hopeless.lvl", self.map_bytes("Hopeless"), loads=False),
                       self.add("TINY.LVL", self.broken, loads=False)]
        self.repaired_dir = os.path.join(self.tmp, "repaired")

    def sweep_of_repaired(self, **changes):
        """The report of a sweep of the repaired folder (the file's own hash, as the sweep gives it)."""
        return [entry(name="Broken.lvl", hash=community_maps.fnv1a64(read_bytes(self.repaired_dir, "Broken.lvl")), **changes)]

    def test_repair_writes_the_files_that_have_a_repair_and_their_sentences(self):
        self.assertEqual(community_maps.repair(self.src, self.report, self.repaired_dir, originals={}), 1)
        self.assertEqual(sorted(os.listdir(self.repaired_dir)), ["Broken.lvl", "repairs.json"])
        self.assertEqual(read_bytes(self.repaired_dir, "Broken.lvl"), self.FIXED + b"OKEN")
        self.assertEqual(json.loads(read_text(self.repaired_dir, "repairs.json")), {"format": 1, "repairs": {"Broken.lvl": "A thing was done; another thing, too."}})
        self.assertEqual(sorted(self.asked), sorted([self.broken, self.map_bytes("Hopeless")]))                       # (not the map that passes, nor TINY.LVL: the name tells that it is an original)

    def test_repair_leaves_the_originals_alone(self):
        community_maps.repair(self.src, self.report, self.repaired_dir, originals={})
        self.assertNotIn("TINY.LVL", os.listdir(self.repaired_dir))
        self.asked.clear()
        community_maps.repair(self.src, [self.add("Copy.lvl", self.original, loads=False)], self.repaired_dir, originals={self.original: "ORIG.LVL"})
        self.assertEqual(self.asked, [])
        self.assertEqual(json.loads(read_text(self.repaired_dir, "repairs.json"))["repairs"], {})

    def test_the_sentence_has_a_capital_and_a_full_stop(self):
        self.assertEqual(community_maps.sentence(["one thing", "two things"]), "One thing; two things.")
        self.assertEqual(community_maps.sentence(["3 cells"]), "3 cells.")

    def test_build_takes_in_a_repaired_map_whose_sweep_passes_with_its_sentence(self):
        community_maps.repair(self.src, self.report, self.repaired_dir, originals={})
        dest = os.path.join(self.tmp, "dest")
        taken = community_maps.build(self.src, self.report, dest, originals={}, repaired=(self.repaired_dir, self.sweep_of_repaired()))
        self.assertEqual(taken, 2)
        self.assertEqual(sorted(os.listdir(dest)), ["Broken.lvl", "Good.lvl", "maps.json"])
        self.assertEqual(read_bytes(dest, "Broken.lvl"), self.FIXED + b"OKEN")                                           # (the repaired bytes, not the source's)
        self.assertEqual(read_bytes(dest, "Good.lvl"), self.files["Good.lvl"])
        rows = json.loads(read_text(dest, "maps.json"))["maps"]
        self.assertEqual([r["file"] for r in rows], ["Broken.lvl", "Good.lvl"])
        self.assertEqual(rows[0]["repaired"], "A thing was done; another thing, too.")
        self.assertNotIn("repaired", rows[1])
        self.assertEqual(rows[0]["hash"], community_maps.fnv1a64(self.FIXED + b"OKEN"))

    def test_a_repaired_map_whose_sweep_fails_stays_out_and_is_counted_as_what_it_was(self):
        community_maps.repair(self.src, self.report, self.repaired_dir, originals={})
        dest = os.path.join(self.tmp, "dest")
        for changes in ({"loads": False}, {"problems": [{"kind": "tile_outside_dictionary"}]}, {"runs": []}, {"name_ok": False}):
            repaired = (self.repaired_dir, self.sweep_of_repaired(**changes))
            self.assertEqual(community_maps.build(self.src, self.report, dest, originals={}, repaired=repaired), 1, changes)
            self.assertEqual(sorted(os.listdir(dest)), ["Good.lvl", "maps.json"], changes)
            shutil.rmtree(dest)
        with contextlib.redirect_stdout(io.StringIO()):
            counts = community_maps.discards(self.src, self.report, {}, (self.repaired_dir, self.sweep_of_repaired(loads=False)))
        self.assertEqual((counts["does not load"], counts["repaired"]), (2, 0))

    def test_a_map_is_repaired_only_if_the_repairs_list_says_so(self):
        community_maps.repair(self.src, self.report, self.repaired_dir, originals={})
        with open(os.path.join(self.repaired_dir, "repairs.json"), "w", encoding="utf-8") as f:
            json.dump({"format": 1, "repairs": {}}, f)
        dest = os.path.join(self.tmp, "dest")
        self.assertEqual(community_maps.build(self.src, self.report, dest, originals={}, repaired=(self.repaired_dir, self.sweep_of_repaired())), 1)     # (the sweep alone is not enough)
        os.remove(os.path.join(self.repaired_dir, "repairs.json"))
        with self.assertRaises(FileNotFoundError):
            community_maps.build(self.src, self.report, dest, originals={}, repaired=(self.repaired_dir, self.sweep_of_repaired()))

    def test_a_sweep_of_other_bytes_is_an_error_not_a_silent_skip(self):
        community_maps.repair(self.src, self.report, self.repaired_dir, originals={})
        wrong = [dict(self.sweep_of_repaired()[0], hash="0" * 16)]
        with self.assertRaises(ValueError):
            community_maps.build(self.src, self.report, os.path.join(self.tmp, "dest"), originals={}, repaired=(self.repaired_dir, wrong))

    def test_discards_counts_the_repaired_maps_and_does_not_name_them(self):
        community_maps.repair(self.src, self.report, self.repaired_dir, originals={})
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            counts = community_maps.discards(self.src, self.report, {}, (self.repaired_dir, self.sweep_of_repaired()))
        self.assertEqual(out.getvalue().splitlines(), ["does not load\tHopeless.lvl", "original map (its name)\tTINY.LVL"])
        self.assertEqual((counts["repaired"], counts["does not load"], counts["original map"]), (1, 1, 1))

    def test_verify_checks_the_sentence_of_a_repaired_line(self):
        folder = self.folder
        listed = self.listing()
        for sentence in ("Text-transfer damage undone: 2 zero bytes put back.", "x", "A" * community_maps.REPAIRED_FIELD_MAX):
            listed["maps"][0]["repaired"] = sentence
            self.write_listing(listed)
            self.assertEqual(community_maps.verify(folder, [entry(name="Made Up.lvl", hash=community_maps.fnv1a64(self.data))], originals={}), [], sentence)
        bad = "Made Up.lvl: its \"repaired\" in maps.json is not a sentence of printable ASCII (1 to 300 characters)"
        for sentence in ("", "caf\xe9", "tab\there", "A" * (community_maps.REPAIRED_FIELD_MAX + 1), 5, None, ["a"]):
            listed["maps"][0]["repaired"] = sentence
            self.write_listing(listed)
            found = community_maps.verify(folder, [entry(name="Made Up.lvl", hash=community_maps.fnv1a64(self.data))], originals={})
            self.assertEqual(found, [bad], repr(sentence))
        listed["maps"][0]["repaired"] = "A sentence."                                                                       # (any other field is still checked)
        listed["maps"][0]["size"] = 1
        self.write_listing(listed)
        self.assertEqual(community_maps.verify(folder, [entry(name="Made Up.lvl", hash=community_maps.fnv1a64(self.data))], originals={}),
                         ["Made Up.lvl: its line in maps.json is not what the file and the sweep say"])

    def test_the_command_line_runs_the_whole_flow(self):
        source = self.report_file(self.report)
        code, out, err = self.run_main("repair", self.src, source, self.repaired_dir)
        self.assertEqual((code, out, err), (0, "1 maps repaired\n", ""))
        swept = self.report_file(self.sweep_of_repaired(), "repaired.json")
        dest = os.path.join(self.tmp, "dest")
        code, out, err = self.run_main("build", self.src, source, dest, self.repaired_dir, swept)
        self.assertEqual((code, out, err), (0, "2 maps taken in\n", ""))
        code, out, err = self.run_main("discards", self.src, source, self.repaired_dir, swept)
        self.assertEqual(code, 0)
        self.assertEqual(out.splitlines(), ["does not load\tHopeless.lvl", "original map (its name)\tTINY.LVL"])
        self.assertEqual(err.splitlines(), ["1\tdoes not load", "0\tnot playable", "0\tplay fails", "0\ttile outside the dictionary", "0\tprogram code inside", "0\tname the protocol refuses",
                                            "1\trepaired", "1\toriginal map"])
        final = self.report_file([entry(name="Broken.lvl", hash=community_maps.fnv1a64(self.FIXED + b"OKEN")), self.report[0]], "final.json")
        self.assertEqual(self.run_main("verify", dest, final), (0, "", ""))

    def test_a_repaired_report_that_cannot_be_read_exits_1_with_a_word(self):
        source = self.report_file(self.report)
        community_maps.repair(self.src, self.report, self.repaired_dir, originals={})
        for command in ("build", "discards"):
            args = (command, self.src, source) + ((os.path.join(self.tmp, "x"),) if command == "build" else ()) + (self.repaired_dir, os.path.join(self.tmp, "nowhere.json"))
            code, out, err = self.run_main(*args)
            self.assertEqual((code, out), (1, ""), command)
            self.assertIn("nowhere.json: the sweep report cannot be read", err)
        swept = self.report_file([dict(self.sweep_of_repaired()[0], hash="0" * 16)], "wrong.json")
        code, out, err = self.run_main("build", self.src, source, os.path.join(self.tmp, "x"), self.repaired_dir, swept)
        self.assertEqual((code, out), (1, ""))
        self.assertIn("build: ValueError: Broken.lvl: the sweep report of the repaired files is not of this file", err)


if __name__ == "__main__":
    unittest.main()
