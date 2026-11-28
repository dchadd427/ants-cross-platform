#!/usr/bin/env python3
"""What the runners of the whole tree know of the game server, the hub, the site's pages and the stack; the rest is tested by test_run_tests.py and test_run_python_tests.py.

  - the suites of the game server in the real table of ./run_tests.sh (tests/suites_server.sh), listed without running anything: the slow ones are not in --fast, every part of
    tests/scripts/test_ants_server.sh is run by the file, and no other, and the parts are all in the full run
  - the tables of tests/scripts/costs_private.py that tests/scripts/run_python_tests.py reads: the two long mutation classes are the sharded ones, the page tests that the macOS job leaves to Linux
    are many, and what the macOS runners taught the tests of the hub, the nginx files and the site's pages stays on macOS
"""
import os
import re
import subprocess
import sys
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCRIPT = os.path.join(REPO, "run_tests.sh")
FILE = os.path.join(REPO, "tests", "suites_server.sh")
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import run_python_tests as rpt     # noqa: E402


class RealTableServerSuites(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        def listing(*args):
            env = dict(os.environ)
            env.pop("ANTS_RUN_TESTS_TABLE", None)
            out = subprocess.run(["bash", SCRIPT, *args, "--list"], capture_output=True, text=True, env=env, cwd=REPO)
            assert out.returncode == 0, out.stderr
            return [line.split("\t") for line in out.stdout.splitlines()]
        cls.full = listing()
        cls.fast = listing("--fast")

    def test_the_file_is_read_and_every_suite_of_it_is_in_the_full_run(self):
        with open(FILE, encoding="utf-8") as f:
            ids = re.findall(r'^suite "([^"]+)"', f.read(), re.M)
        self.assertGreater(len(ids), 30)
        self.assertEqual(len(ids), len(set(ids)))
        self.assertEqual(set(ids) - {row[0] for row in self.full}, set())

    def test_fast_leaves_out_the_slow_suites_of_the_server(self):
        fast_ids = {row[0] for row in self.fast}
        self.assertNotIn("3.9", fast_ids)                                  # (the server script: it starts the server and clients)
        for slow in ("2.19", "3.6"):                                       # the server, the network application
            self.assertNotIn(slow, fast_ids)

    def test_the_table_runs_every_part_of_the_server_script_and_nothing_else_of_it(self):
        with open(FILE, encoding="utf-8") as f:
            used = re.findall(r'test_ants_server\.sh" --part (\w+)', f.read())
        listed = subprocess.run(["bash", os.path.join(REPO, "tests", "scripts", "test_ants_server.sh"), "--list-parts"], capture_output=True, text=True).stdout.split()
        self.assertEqual(sorted(used), sorted(listed), "tests/suites_server.sh and tests/scripts/test_ants_server.sh --list-parts name different parts")
        self.assertEqual(len(used), len(set(used)))
        for suite_id in ("3.9", "3.9.1", "3.9.2", "3.9.3", "3.9.4", "3.9.5"):
            self.assertIn(suite_id, {row[0] for row in self.full})


class PythonRunnerTables(unittest.TestCase):
    """The tables of costs_private.py, as run_python_tests.py has them."""

    def test_the_two_long_mutation_classes_are_the_sharded_ones(self):
        self.assertEqual(sorted(rpt.SHARDS), ["test_web_home_chat.TheChecksAreNotVacuous", "test_web_home_games.TheChecksAreNotVacuous"])      # (test_run_python_tests.py checks what a sharded class must be)

    def test_many_page_tests_stay_off_the_macos_job(self):
        self.assertGreaterEqual(len(rpt.LINUX_ONLY), 10)

    def test_the_macos_job_keeps_what_the_macos_runners_taught_these_tests(self):
        left = {rpt.base_of(label) for label, _ in rpt.units(HERE, "tests.scripts")} - rpt.LINUX_ONLY
        for prefix in ("test_hub_", "test_e2e_deadlines.", "test_measure_tick_rate.", "test_nginx_", "test_web_without_docker.", "test_web_home_chat.TheCodeRuns",
                       "test_web_lobby_net.TheChecksThatNodeRuns", "test_web_lobby_chat.TheChecksThatNodeRuns", "test_web_rematch.TheChecksThatNodeRuns"):
            self.assertTrue([label for label in left if label.startswith(prefix)], "the macOS job would run nothing of " + prefix)
            self.assertFalse([label for label in rpt.LINUX_ONLY if label.startswith(prefix)], prefix + " is a lesson of the macOS runners: it stays on both")


if __name__ == "__main__":
    unittest.main()
