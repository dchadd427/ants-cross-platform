#!/usr/bin/env python3
"""Tests of export/split_mutants.py, which keeps a mutant that names something private out of a public file of mutants (run by ./run_tests.sh --fast and by the CI).

On a made-up tree with made-up lists: a list that has public and private mutants is split into NAME_mutants.json and NAME_server_mutants.json (a mutant is private when the file it
changes, a path or program in its test command, a page that the command is given, or a CMake target that it builds is private), every mutant keeps its text byte for byte, the note,
the defaults and the baseline of each half are the half's own, a second run finds nothing to do, a list with no public mutant stays whole and goes to the private list, and a path on
neither list is refused. And on this repository: every list is whole or split, and on the list that its mutants say (so a new list with a private mutant in it fails here until it is split).
"""
import json
import os
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TOOL = os.path.join(REPO, "export", "split_mutants.py")

ALLOW = "src/pub.cpp\ntests/test_x/**\ntests/scripts/check.js\nweb/page.js\ntests/data/whole_public_mutants.json\n"
PRIVATE = "src/priv.cpp\ntests/test_srv/**\nweb/lobby.html\ntests/data/demo_mutants.json\ntests/data/only_private_mutants.json\ntests/data/*_server_mutants.json\n"

# one mutant per line of reasoning: the style of the file (one space of indent, the keys in this order) must come back unchanged in the halves
SPEC = """{
 "note": "Mutants of the demo (test_priv names the server).",
 "defaults": {"build": "test_pub", "test": "./build/tests/test_x/test_pub"},
 "baseline": {"build": ["test_pub", "test_priv"], "test": "./build/tests/test_x/test_pub && ./build/tests/test_srv/test_priv"},
 "mutants": [
  {"id": "a_public", "file": "src/pub.cpp", "old": "one", "new": "two"},
  {"id": "b_changes_a_private_file", "file": "src/priv.cpp", "old": "one", "new": "two", "test": "./build/tests/test_srv/test_priv", "build": ["test_priv"]},
  {"id": "c_caught_by_a_private_test", "file": "src/pub.cpp", "old": "three", "new": "four", "test": "./build/tests/test_srv/test_priv", "build": ["test_priv"]},
  {"id": "d_given_a_private_page", "file": "web/page.js", "old": "one", "new": "two", "test": "node tests/scripts/check.js web/page.js web/lobby.html", "build": []},
  {"id": "e_builds_a_private_target", "file": "src/pub.cpp", "old": "five", "new": "six", "test": "./build/tests/test_x/test_pub", "build": "test_priv"},
  {"id": "f_public_too", "file": "web/page.js", "old": "seven", "new": "eight"}
 ]
}
"""


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def read(path):
    with open(path, encoding="utf-8", newline="") as f:
        return f.read()


class Tree(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = self.tmp.name
        for rel in ("src/pub.cpp", "src/priv.cpp", "tests/test_x/test_pub.cpp", "tests/test_srv/test_priv.cpp", "tests/scripts/check.js", "web/page.js", "web/lobby.html"):
            write(os.path.join(self.root, rel), "one two three five seven\n")
        write(os.path.join(self.root, "tests", "test_srv", "CMakeLists.txt"), "add_executable(test_priv test_priv.cpp)\n")
        write(os.path.join(self.root, "tests", "test_x", "CMakeLists.txt"), "add_executable(test_pub test_pub.cpp)\n")
        write(os.path.join(self.root, "lists", "allow.txt"), ALLOW + "tests/test_x/CMakeLists.txt\n")
        write(os.path.join(self.root, "lists", "private.txt"), PRIVATE + "tests/test_srv/CMakeLists.txt\n")
        write(os.path.join(self.root, "tests", "data", "demo_mutants.json"), SPEC)

    def tearDown(self):
        self.tmp.cleanup()

    def tool(self, *args):
        return subprocess.run([sys.executable, TOOL, "--repo", self.root, "--allow", os.path.join(self.root, "lists", "allow.txt"), "--private", os.path.join(self.root, "lists", "private.txt"), *args],
                              capture_output=True, text=True, timeout=60)

    def data(self, name):
        return os.path.join(self.root, "tests", "data", name)


class TheSplit(Tree):
    def test_a_list_with_both_kinds_is_split_and_every_mutant_keeps_its_text(self):
        dry = self.tool()
        self.assertEqual(dry.returncode, 1, dry.stdout + dry.stderr)                           # something to do, and nothing written
        self.assertIn("2 public", dry.stdout)
        self.assertIn("4 private", dry.stdout)
        self.assertIn("split", dry.stdout)
        self.assertIn("list: private -> public", dry.stdout)
        self.assertEqual(read(self.data("demo_mutants.json")), SPEC)
        self.assertFalse(os.path.exists(self.data("demo_server_mutants.json")))

        done = self.tool("--write", "--public-note", "demo=Mutants of the demo.")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        first, second = read(self.data("demo_mutants.json")), read(self.data("demo_server_mutants.json"))
        public, private = json.loads(first), json.loads(second)
        self.assertEqual([m["id"] for m in public["mutants"]], ["a_public", "f_public_too"])
        self.assertEqual([m["id"] for m in private["mutants"]], ["b_changes_a_private_file", "c_caught_by_a_private_test", "d_given_a_private_page", "e_builds_a_private_target"])
        original = json.loads(SPEC)["mutants"]
        for half, text in ((public, first), (private, second)):                                 # the text of a mutant is not rewritten
            for m in half["mutants"]:
                line = [l for l in SPEC.split("\n") if '"id": "%s"' % m["id"] in l][0].strip().rstrip(",")
                self.assertIn(line, text)
        self.assertEqual(sorted(m["id"] for m in public["mutants"] + private["mutants"]), sorted(m["id"] for m in original))

    def test_each_half_has_its_own_note_defaults_and_baseline(self):
        self.assertEqual(self.tool("--write", "--public-note", "demo=Mutants of the demo.").returncode, 0)
        public, private = json.loads(read(self.data("demo_mutants.json"))), json.loads(read(self.data("demo_server_mutants.json")))
        self.assertEqual(public["note"], "Mutants of the demo.")
        self.assertTrue(private["note"].startswith("The private half of tests/data/demo_mutants.json"))
        self.assertIn("test_priv names the server", private["note"])                            # the private half keeps everything the note said
        self.assertEqual(public["defaults"], {"build": "test_pub", "test": "./build/tests/test_x/test_pub"})
        self.assertNotIn("defaults", private)                                                   # every private mutant says its own build and test: the defaults are not needed
        self.assertEqual(public["baseline"], {"build": ["test_pub"], "test": "./build/tests/test_x/test_pub"})
        self.assertEqual(private["baseline"], {"build": ["test_priv"], "test": "./build/tests/test_srv/test_priv"})
        self.assertNotIn("test_priv", read(self.data("demo_mutants.json")).replace("test_priv names", ""), "the public half names nothing private")

    def test_a_note_that_still_names_something_private_is_reported(self):
        done = self.tool("--write", "--public-note", "demo=Caught by test_server and nginx.")
        self.assertIn("the public note still names", done.stdout)

    def test_a_second_run_finds_nothing_to_do(self):
        self.tool("--write")
        before = {n: read(self.data(n)) for n in ("demo_mutants.json", "demo_server_mutants.json")}
        write(os.path.join(self.root, "lists", "allow.txt"), ALLOW + "tests/test_x/CMakeLists.txt\ntests/data/demo_mutants.json\n")
        write(os.path.join(self.root, "lists", "private.txt"), PRIVATE.replace("tests/data/demo_mutants.json\n", "") + "tests/test_srv/CMakeLists.txt\n")
        again = self.tool()
        self.assertEqual(again.returncode, 0, again.stdout + again.stderr)
        self.assertEqual(self.tool("--write").returncode, 0)
        self.assertEqual({n: read(self.data(n)) for n in before}, before)

    def test_a_list_with_no_public_mutant_stays_whole_and_is_private(self):
        spec = json.loads(SPEC)
        spec["mutants"] = [m for m in spec["mutants"] if m["id"].startswith(("b_", "c_"))]
        write(self.data("only_private_mutants.json"), json.dumps(spec, indent=1) + "\n")
        os.remove(self.data("demo_mutants.json"))
        done = self.tool("--write")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)                        # whole, and already on the private list
        self.assertFalse(os.path.exists(self.data("only_private_server_mutants.json")))
        write(self.data("demo_mutants.json"), SPEC)
        write(os.path.join(self.root, "lists", "allow.txt"), ALLOW + "tests/test_x/CMakeLists.txt\ntests/data/only_private_mutants.json\n")            # on the wrong list
        write(os.path.join(self.root, "lists", "private.txt"), PRIVATE.replace("tests/data/only_private_mutants.json\n", "") + "tests/test_srv/CMakeLists.txt\n")
        wrong = self.tool("only_private")
        self.assertEqual(wrong.returncode, 1, wrong.stdout)
        self.assertIn("list: public -> private", wrong.stdout)

    def test_a_list_of_public_mutants_only_is_left_alone(self):
        spec = json.loads(SPEC)
        spec["mutants"] = [m for m in spec["mutants"] if m["id"].startswith(("a_", "f_"))]
        write(self.data("whole_public_mutants.json"), json.dumps(spec, indent=1) + "\n")
        done = self.tool("--write", "whole_public")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertFalse(os.path.exists(self.data("whole_public_server_mutants.json")))

    def test_a_path_on_neither_list_is_refused(self):
        spec = json.loads(SPEC)
        spec["mutants"][0]["file"] = "src/unlisted.cpp"
        write(self.data("demo_mutants.json"), json.dumps(spec, indent=1) + "\n")
        done = self.tool("--write")
        self.assertEqual(done.returncode, 2, done.stdout + done.stderr)
        self.assertIn("src/unlisted.cpp is on neither list", done.stdout)
        self.assertFalse(os.path.exists(self.data("demo_server_mutants.json")))

    def test_a_list_that_is_split_and_has_a_private_mutant_again_is_not_written_over(self):
        self.tool("--write")
        write(os.path.join(self.root, "lists", "allow.txt"), ALLOW + "tests/test_x/CMakeLists.txt\ntests/data/demo_mutants.json\n")
        public = json.loads(read(self.data("demo_mutants.json")))
        public["mutants"].append({"id": "g_private_again", "file": "src/priv.cpp", "old": "one", "new": "two"})
        write(self.data("demo_mutants.json"), json.dumps(public, indent=1) + "\n")
        before = read(self.data("demo_server_mutants.json"))
        done = self.tool("--write")
        self.assertEqual(done.returncode, 2, done.stdout + done.stderr)
        self.assertEqual(read(self.data("demo_server_mutants.json")), before)


class ThisRepository(unittest.TestCase):
    def test_every_list_of_mutants_is_whole_and_on_the_list_its_mutants_say(self):
        done = subprocess.run([sys.executable, TOOL], capture_output=True, text=True, timeout=120, cwd=REPO)
        self.assertEqual(done.returncode, 0, "a list of mutants (tests/data) is mixed or on the wrong list of the export (python3 export/split_mutants.py --why; --write splits it):\n" + done.stdout + done.stderr)


if __name__ == "__main__":
    unittest.main()
