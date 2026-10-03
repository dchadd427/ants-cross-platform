#!/usr/bin/env python3
"""Tests of tests/scripts/run_python_tests.py, which runs the python tests side by side (suite 5.2 of ./run_tests.sh and the CI).

It runs on made-up test folders (tmp/tests/scripts/test_*.py) and checks: the result is that of unittest (a failing test makes the exit status 1 and its output is shown, the
others still run), the classes run at the same time (two tests that wait for each other pass only when they overlap), a file that cannot be imported fails and says why, and
on the REAL tests/scripts the classes that it runs are exactly the classes that unittest's own loader finds (so that no test is left out).
"""
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "run_python_tests.py")
sys.path.insert(0, HERE)
import run_python_tests as rpt     # noqa: E402

PASSING = "import unittest\nclass Good(unittest.TestCase):\n    def test_one(self):\n        pass\n    def test_two(self):\n        self.assertEqual(1, 1)\n"
FAILING = "import unittest\nclass Bad(unittest.TestCase):\n    def test_it_fails(self):\n        self.fail('this is the message of the failure')\n"
MEET = """import os, time, unittest
MARKS = os.environ["MARK_DIR"]
class Meet(unittest.TestCase):
    def test_meet(self):
        open(os.path.join(MARKS, "%(me)s.up"), "w").close()
        deadline = time.time() + %(wait)s
        while time.time() < deadline:
            if os.path.exists(os.path.join(MARKS, "%(other)s.up")):
                return
            time.sleep(0.05)
        self.fail("the other group never started")
"""


class Wrapper(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.scripts = os.path.join(self.tmp.name, "tests", "scripts")
        self.marks = os.path.join(self.tmp.name, "marks")
        os.makedirs(self.scripts)
        os.makedirs(self.marks)

    def tearDown(self):
        self.tmp.cleanup()

    def add(self, name, text):
        with open(os.path.join(self.scripts, name), "w", encoding="utf-8") as f:
            f.write(text)

    def run_tool(self, *args):
        env = dict(os.environ, MARK_DIR=self.marks)
        return subprocess.run([sys.executable, TOOL, "--dir", self.scripts, *args], capture_output=True, text=True, env=env)

    def test_everything_passes(self):
        self.add("test_a.py", PASSING)
        self.add("test_b.py", PASSING.replace("Good", "AlsoGood"))
        result = self.run_tool()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("test_a.Good", result.stdout)
        self.assertIn("test_b.AlsoGood", result.stdout)
        self.assertIn("Ran 4 tests in 2 groups", result.stdout)
        self.assertTrue(result.stdout.rstrip().endswith("OK"))

    def test_a_failing_test_fails_the_run_shows_its_output_and_the_others_still_run(self):
        self.add("test_a.py", PASSING)
        self.add("test_bad.py", FAILING)
        self.add("test_c.py", PASSING.replace("Good", "Third"))
        result = self.run_tool()
        self.assertEqual(result.returncode, 1)
        self.assertIn("this is the message of the failure", result.stdout)
        self.assertIn("FAILED: test_bad.Bad", result.stdout)
        self.assertRegex(result.stdout, r"test_a\.Good\s+ok")
        self.assertRegex(result.stdout, r"test_c\.Third\s+ok")
        self.assertRegex(result.stdout, r"test_bad\.Bad\s+FAILED")
        self.assertIn("Ran 5 tests", result.stdout)

    def test_every_class_of_a_file_is_a_group_of_its_own_in_the_order_of_the_files(self):
        self.add("test_b.py", PASSING + "class Second(unittest.TestCase):\n    def test_x(self):\n        pass\n")
        self.add("test_a.py", PASSING)
        listing = self.run_tool("--list").stdout.splitlines()
        self.assertEqual([line.split("\t")[0] for line in listing], ["test_a.Good", "test_b.Good", "test_b.Second"])
        self.assertEqual(listing[0].split("\t")[1], "tests.scripts.test_a.Good")

    def test_groups_run_at_the_same_time(self):
        self.add("test_a.py", MEET % {"me": "a", "other": "b", "wait": 5})
        self.add("test_b.py", MEET % {"me": "b", "other": "a", "wait": 5})
        self.assertEqual(self.run_tool("--jobs", "2").returncode, 0)                     # each saw the other
        for name in os.listdir(self.marks):
            os.remove(os.path.join(self.marks, name))
        self.add("test_a.py", MEET % {"me": "a", "other": "b", "wait": 1})
        self.add("test_b.py", MEET % {"me": "b", "other": "a", "wait": 1})
        self.assertEqual(self.run_tool("--jobs", "1").returncode, 1)                     # one after the other: the first waits for a partner that comes later

    def test_a_file_that_cannot_be_imported_fails_and_says_why(self):
        self.add("test_broken.py", "import module_that_does_not_exist\n")
        self.add("test_a.py", PASSING)
        result = self.run_tool()
        self.assertEqual(result.returncode, 1)
        self.assertIn("module_that_does_not_exist", result.stdout)
        self.assertIn("FAILED: test_broken", result.stdout)

    def test_a_folder_without_tests_is_refused(self):
        self.assertEqual(self.run_tool().returncode, 2)
        self.assertEqual(self.run_tool("--jobs", "0").returncode, 2)


class TheRealTests(unittest.TestCase):
    def test_the_groups_are_the_classes_that_the_loader_finds(self):
        loader = unittest.TestLoader()
        suite = loader.discover(HERE, pattern="test_*.py", top_level_dir=HERE)
        found = set()

        def walk(s):
            for item in s:
                if isinstance(item, unittest.TestSuite):
                    walk(item)
                else:
                    module = type(item).__module__.split(".")[-1]
                    if module.startswith("test_") and os.path.exists(os.path.join(HERE, module + ".py")) and type(item).__module__ == module:
                        found.add("%s.%s" % (module, type(item).__name__))
        walk(suite)
        found.discard("unittest.loader._FailedTest")
        grouped = {label for label, _ in rpt.units(HERE, "tests.scripts")}
        self.assertEqual(grouped - found, set(), "the runner would run a group that unittest does not find")
        # a class with tests that is only in the loader's list would be left out by the runner
        self.assertEqual(found - grouped, set(), "the runner would leave these classes out: " + ", ".join(sorted(found - grouped)))

    def test_the_real_files_are_named_as_the_runner_expects(self):
        names = [f for f in os.listdir(HERE) if f.startswith("test_") and f.endswith(".py")]
        self.assertGreater(len(names), 8)
        self.assertNotIn("run_python_tests.py", names)                                 # (it is not itself a test file: unittest discover must not import it)


if __name__ == "__main__":
    unittest.main()
