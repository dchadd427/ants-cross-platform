#!/usr/bin/env python3
"""Every mutant spec of tests/data still applies to the code (run by ./run_tests.sh --fast and by the CI).

A mutant (tests/data/*_mutants.json, run by tools/mutate.py) is the proof that a test would notice one particular bug. Its `old` text must occur exactly once in its
file; when the code is rewritten and the text is gone, the mutant cannot be applied, the proof has silently gone, and nothing used to say so (30 of 2,947 had gone this way).
This test runs `tools/mutate.py SPEC --check` (it only reads: nothing is built or run) on every spec and fails for one that reports an `old` text that is not there exactly once.
A spec may have a second half, NAME_server_mutants.json beside NAME_mutants.json (the mutants that need files which are not in every copy of the tree): `--check` reads the two as one
list, so a copy of the tree that has only the first half checks that, and the second half is a spec of its own only for the rule that it needs its first half.

A spec may be left out only in STALE_ALLOWED, with the reason and when to look at it again; the test also fails when a listed spec is whole again (take it off the list)
and when the list names a file that is not there. Run the mutants themselves with `python3 tools/mutate.py tests/data/NAME_mutants.json --keep-going --jobs 1` (each spec's
`baseline` says what is built and run first).
"""
import glob
import json
import os
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TOOL = os.path.join(REPO, "tools", "mutate.py")
DATA = os.path.join(REPO, "tests", "data")

# Specs that are not checked yet: file name -> why, and when to look again (listed 20xx-xx-xx, "after those changes ...").
STALE_ALLOWED = {}
SECOND_HALF = "_server_mutants.json"


def specs():
    """every list, by its first half (a second half is read with it)"""
    return sorted(s for s in glob.glob(os.path.join(DATA, "*_mutants.json")) if not s.endswith(SECOND_HALF))


def second_halves():
    return sorted(glob.glob(os.path.join(DATA, "*" + SECOND_HALF)))


def check(spec, root=REPO):
    """(exit status, output) of `mutate.py SPEC --check --root ROOT`: 0 when every old text occurs exactly once, 2 when one does not (or the file is wrong)."""
    done = subprocess.run([sys.executable, TOOL, spec, "--check", "--root", root], capture_output=True, text=True, timeout=120)
    return done.returncode, done.stdout + done.stderr


class EverySpecStillApplies(unittest.TestCase):
    def test_there_are_specs(self):
        self.assertGreaterEqual(len(specs()), 40, "tests/data/*_mutants.json: where are the specs?")

    def test_every_old_text_occurs_once(self):
        for spec in specs():
            name = os.path.basename(spec)
            if name in STALE_ALLOWED:
                continue
            with self.subTest(name):
                status, output = check(spec)
                self.assertEqual(status, 0, "%s: a mutant no longer applies to the code (re-aim its `old` text at the current code, do not delete the mutant; "
                                            "`python3 tools/mutate.py tests/data/%s --check` lists them):\n%s" % (name, name, output))

    def test_every_second_half_has_its_first_half(self):
        for half in second_halves():
            first = half[:-len(SECOND_HALF)] + "_mutants.json"
            with self.subTest(os.path.basename(half)):
                self.assertTrue(os.path.isfile(first), "%s is read with %s, which is not there: tools/mutate.py would never read it" % (os.path.basename(half), os.path.basename(first)))

    def test_the_list_of_exceptions_is_true(self):
        for name, why in STALE_ALLOWED.items():
            with self.subTest(name):
                spec = os.path.join(DATA, name)
                self.assertTrue(os.path.isfile(spec), "STALE_ALLOWED names %s, which is not in tests/data" % name)
                self.assertIn("listed 20", why, "say when it was listed")
                self.assertIn("after those changes", why, "say when to look again")
                status, output = check(spec)
                self.assertNotEqual(status, 0, "%s applies again: take it off STALE_ALLOWED so that it is checked" % name)


class TheCheckCanFail(unittest.TestCase):
    """The test above is not vacuous: one `old` text edited in a copy of a real spec, or in a small spec of its own, is found."""

    def spec_with(self, tmp, spec, name):
        path = os.path.join(tmp, name)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(spec, f)
        return path

    def test_a_good_spec_passes_and_the_same_with_one_edited_old_text_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            with open(os.path.join(tmp, "calc.py"), "w", encoding="utf-8") as f:
                f.write("def add(a, b):\n    return a + b\n")
            mutant = {"id": "add_subtracts", "file": "calc.py", "old": "return a + b", "new": "return a - b", "test": "true"}
            status, output = check(self.spec_with(tmp, {"mutants": [mutant]}, "good_mutants.json"), tmp)
            self.assertEqual(status, 0, output)
            mutant["old"] = "return a + c"                       # the code was rewritten and the text is gone
            status, output = check(self.spec_with(tmp, {"mutants": [mutant]}, "edited_mutants.json"), tmp)
            self.assertEqual(status, 2, output)
            self.assertIn("ERROR  add_subtracts", output)
            self.assertIn("occurs 0 times", output)
            mutant["old"] = "a"                                   # a text that occurs more than once
            status, output = check(self.spec_with(tmp, {"mutants": [mutant]}, "twice_mutants.json"), tmp)
            self.assertEqual(status, 2, output)
            self.assertIn("ERROR  add_subtracts", output)
            self.assertIn("occurs 3 times", output)

    def test_one_edited_old_text_of_a_real_spec_fails(self):
        name = [os.path.basename(s) for s in specs() if os.path.basename(s) not in STALE_ALLOWED][0]
        with open(os.path.join(DATA, name), encoding="utf-8") as f:
            spec = json.load(f)
        victim = spec["mutants"][-1]
        victim["old"] = victim["old"] + " /* edited */"
        with tempfile.TemporaryDirectory() as tmp:
            status, output = check(self.spec_with(tmp, spec, name))
        self.assertEqual(status, 2, output)
        self.assertIn("ERROR  " + victim["id"], output)
        self.assertIn("1 that cannot be applied", output)

    def test_the_second_half_of_a_list_is_read_with_it_and_its_ids_are_the_lists_too(self):
        with tempfile.TemporaryDirectory() as tmp:
            with open(os.path.join(tmp, "calc.py"), "w", encoding="utf-8") as f:
                f.write("def add(a, b):\n    return a + b\n")
            first = {"id": "add_subtracts", "file": "calc.py", "old": "return a + b", "new": "return a - b", "test": "true"}
            second = {"id": "add_multiplies", "file": "calc.py", "old": "return a + b", "new": "return a * b", "test": "true"}
            self.spec_with(tmp, {"mutants": [second]}, "calc_server_mutants.json")
            status, output = check(self.spec_with(tmp, {"mutants": [first]}, "calc_mutants.json"), tmp)
            self.assertEqual(status, 0, output)
            self.assertIn("2 mutants, 0 that cannot be applied", output)                      # the two halves are one list
            second["old"] = "return a + c"                                                     # a mutant of the second half that no longer applies is found
            self.spec_with(tmp, {"mutants": [second]}, "calc_server_mutants.json")
            status, output = check(os.path.join(tmp, "calc_mutants.json"), tmp)
            self.assertEqual(status, 2, output)
            self.assertIn("ERROR  add_multiplies", output)
            second.update(id="add_subtracts", old="return a + b")                              # an id that is in both halves is refused
            self.spec_with(tmp, {"mutants": [second]}, "calc_server_mutants.json")
            status, output = check(os.path.join(tmp, "calc_mutants.json"), tmp)
            self.assertEqual(status, 2, output)
            self.assertIn("the id is used twice", output)
            os.remove(os.path.join(tmp, "calc_server_mutants.json"))                          # a tree without the second half reads the first alone
            status, output = check(os.path.join(tmp, "calc_mutants.json"), tmp)
            self.assertEqual(status, 0, output)
            self.assertIn("1 mutants, 0 that cannot be applied", output)

    def test_a_file_that_is_gone_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            mutant = {"id": "gone", "file": "no_such_file.cpp", "old": "x", "new": "y", "test": "true"}
            status, output = check(self.spec_with(tmp, {"mutants": [mutant]}, "gone_mutants.json"), tmp)
        self.assertEqual(status, 2, output)
        self.assertIn("ERROR  gone", output)


if __name__ == "__main__":
    unittest.main()
