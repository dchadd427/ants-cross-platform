#!/usr/bin/env python3
"""Self-check of ./run_tests.sh and its tiers (run by ./run_tests.sh --fast and by the CI).

The runner takes its suites from a table (`suite ID TIER QUICK TARGETS LABEL TITLE COMMAND`). A test can give its own table (ANTS_RUN_TESTS_TABLE=<file>, which
builds nothing), so these tests run the real script on made-up suites and check what the tiers promise:

  - --fast runs the quick suites only, the full run runs all of them (a marker file proves which commands ran)
  - a failing suite makes the script exit 1 and says which, and the suites after it still run
  - the summary prints the time of every suite, the slowest ones and the totals; the final marker lines are unchanged (agents wait for them)
  - the tier options select as before (--sim, --app, --assets, --e2e) and --fast filters them
  - the real table: what --fast contains (and does not), listed without running anything
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCRIPT = os.path.join(REPO, "run_tests.sh")

STUB_TABLE = r'''
suite "T1" sim   1 "-" "Quick simulation suite"      "QUICK SIM"        'touch "$MARK_DIR/t1"'
suite "T2" sim   1 "-" "Second quick suite"          "QUICK SIM TWO"    'touch "$MARK_DIR/t2"'
suite "T3" sim   0 "-" "Slow simulation suite"       "SLOW SIM"         'touch "$MARK_DIR/t3"'
suite "T4" app   1 "-" "Quick application suite"     "QUICK APP"        'touch "$MARK_DIR/t4"'
suite "T5" app   0 "-" "Slow application suite"      "SLOW APP"         'touch "$MARK_DIR/t5"'
suite "T6" e2e   0 "-" "End to end"                  "E2E"              'touch "$MARK_DIR/t6"'
suite "T7" tools 1 "-" "Repository check"            "TOOLS"            'touch "$MARK_DIR/t7"'
'''

SLEEPY_STUB_TABLE = r'''
suite "S1" sim   1 "-" "Quick suite"                 "QUICK"            'touch "$MARK_DIR/s1"'
suite "S2" sim   1 "-" "Suite that takes time"       "SLEEPER"          'sleep 1.1; touch "$MARK_DIR/s2"'
suite "S3" sim   1 "-" "Another quick suite"         "QUICK TWO"        'touch "$MARK_DIR/s3"'
'''

FAILING_STUB_TABLE = r'''
suite "F1" sim   1 "-" "Quick passing suite"         "PASS ONE"         'touch "$MARK_DIR/f1"'
suite "F2" sim   1 "-" "Quick failing suite"         "FAIL"             'touch "$MARK_DIR/f2"; exit 3'
suite "F3" app   1 "-" "Quick suite after the failure" "PASS TWO"       'touch "$MARK_DIR/f3"'
suite "F4" app   0 "-" "Slow suite"                  "SLOW"             'touch "$MARK_DIR/f4"'
'''


class RunnerTiers(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.marks = os.path.join(self.tmp.name, "marks")
        os.makedirs(self.marks)

    def tearDown(self):
        self.tmp.cleanup()

    def table(self, text):
        path = os.path.join(self.tmp.name, "table.sh")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        return path

    def run_script(self, args, table_text=None):
        env = dict(os.environ)
        env["NO_COLOR"] = "1"
        env["MARK_DIR"] = self.marks
        env.pop("ANTS_RUN_TESTS_TABLE", None)
        if table_text is not None:
            env["ANTS_RUN_TESTS_TABLE"] = self.table(table_text)
        return subprocess.run(["bash", SCRIPT, *args], capture_output=True, text=True, env=env, cwd=REPO)

    def ran(self):
        return sorted(os.listdir(self.marks))

    # ---- the fast tier -------------------------------------------------------------------------------------------------------------------------------

    def test_fast_runs_the_quick_suites_only(self):
        result = self.run_script(["--fast"], STUB_TABLE)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.ran(), ["t1", "t2", "t4", "t7"])             # not the slow simulation / application suites, not the E2E
        self.assertIn("RESULT: ALL EXECUTED TEST SUITES PASSED CLEANLY!", result.stdout)
        self.assertIn("Suites run: 4, failed: 0", result.stdout)

    def test_the_default_runs_everything(self):
        result = self.run_script([], STUB_TABLE)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.ran(), ["t1", "t2", "t3", "t4", "t5", "t6", "t7"])
        self.assertIn("Suites run: 7, failed: 0", result.stdout)

    def test_all_is_the_same_as_the_default(self):
        result = self.run_script(["--all"], STUB_TABLE)
        self.assertEqual(self.ran(), ["t1", "t2", "t3", "t4", "t5", "t6", "t7"], result.stdout)

    def test_a_failing_suite_exits_non_zero_and_is_named_and_the_others_still_run(self):
        result = self.run_script(["--fast"], FAILING_STUB_TABLE)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertEqual(self.ran(), ["f1", "f2", "f3"])                    # f3 ran after f2 failed; the slow f4 is not part of --fast
        self.assertRegex(result.stdout, r"F2\s+Quick failing suite:\s+FAILED \(exit code 3\)")
        self.assertRegex(result.stdout, r"F1\s+Quick passing suite:\s+PASSED")
        self.assertRegex(result.stdout, r"F3\s+Quick suite after the failure:\s+PASSED")
        self.assertIn("RESULT: 1 TEST SUITE(S) FAILED!", result.stdout)
        self.assertNotIn("ALL EXECUTED TEST SUITES PASSED CLEANLY", result.stdout)

    def test_a_failure_in_the_full_run_is_found_too(self):
        result = self.run_script([], FAILING_STUB_TABLE)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(self.ran(), ["f1", "f2", "f3", "f4"])

    def test_a_command_that_does_not_exist_fails_the_suite(self):
        result = self.run_script(["--fast"], 'suite "M1" sim 1 "-" "Missing program" "MISSING" "./no-such-program-here"\n')
        self.assertEqual(result.returncode, 1)
        self.assertRegex(result.stdout, r"FAILED \(exit code 127\)")

    # ---- the summary ---------------------------------------------------------------------------------------------------------------------------------

    def test_the_summary_prints_the_time_of_every_suite_the_slowest_and_the_total(self):
        result = self.run_script(["--fast"], SLEEPY_STUB_TABLE)
        summary = result.stdout[result.stdout.index("OVERALL TEST RUN SUMMARY"):]
        for suite_id in ("S1", "S2", "S3"):
            self.assertRegex(summary, r" %s\s+.*:\s+PASSED\s+\d+\.\ds" % suite_id)
        self.assertRegex(summary, r"S2\s+Suite that takes time:\s+PASSED\s+1\.\ds")        # the sleeper: 1.1 s and a little more, measured
        self.assertRegex(summary, r"S1\s+Quick suite:\s+PASSED\s+0\.\ds")
        self.assertIn("Slowest suites:", summary)
        slowest = summary[summary.index("Slowest suites:"):]
        self.assertRegex(slowest, r"Slowest suites:\n\s+1\.\ds\s+S2 Suite that takes time\n")   # the slowest one comes first
        self.assertRegex(summary, r"Total Test Execution Time: \d+s")

    def test_each_suite_prints_its_header_before_it_runs(self):
        result = self.run_script(["--fast"], STUB_TABLE)
        self.assertIn(">>> T1. RUNNING QUICK SIM...", result.stdout)
        self.assertLess(result.stdout.index(">>> T1."), result.stdout.index(">>> T2."))

    # ---- the tier options ----------------------------------------------------------------------------------------------------------------------------

    def test_the_tier_options_select_as_before(self):
        expected = {"--sim": ["t1", "t2", "t3"], "--app": ["t4", "t5"], "--e2e": ["t6"], "--tools": ["t7"]}
        for option, marks in expected.items():
            for name in os.listdir(self.marks):
                os.remove(os.path.join(self.marks, name))
            result = self.run_script([option], STUB_TABLE)
            self.assertEqual(result.returncode, 0, option + result.stdout)
            self.assertEqual(self.ran(), marks, option)

    def test_fast_filters_the_tier_options(self):
        result = self.run_script(["--sim", "--fast"], STUB_TABLE)
        self.assertEqual(self.ran(), ["t1", "t2"], result.stdout)

    def test_a_selection_of_no_suite_is_a_failure(self):
        result = self.run_script(["--assets"], STUB_TABLE)                  # the stub table has no asset suite
        self.assertEqual(result.returncode, 1)
        self.assertIn("NO TEST SUITE WAS SELECTED", result.stdout)

    def test_list_shows_the_selection_and_runs_nothing(self):
        result = self.run_script(["--fast", "--list"], STUB_TABLE)
        self.assertEqual(result.returncode, 0)
        self.assertEqual([line.split("\t")[0] for line in result.stdout.splitlines()], ["T1", "T2", "T4", "T7"])
        self.assertEqual(self.ran(), [])
        result = self.run_script(["--list"], STUB_TABLE)
        self.assertEqual(len(result.stdout.splitlines()), 7)

    def test_an_unknown_option_is_refused(self):
        result = self.run_script(["--no-such-option"], STUB_TABLE)
        self.assertEqual(result.returncode, 1)
        self.assertIn("Unknown option", result.stdout)

    def test_help_names_the_fast_tier(self):
        result = self.run_script(["--help"])
        self.assertEqual(result.returncode, 0)
        self.assertIn("--fast", result.stdout)
        self.assertIn("docs/WORKFLOW.md", result.stdout)


class RealTable(unittest.TestCase):
    """The real table, listed (nothing is built or run)."""

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

    def test_ids_are_unique_and_the_full_run_has_every_tier(self):
        ids = [row[0] for row in self.full]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertEqual({row[1] for row in self.full}, {"assets", "sim", "app", "e2e", "tools"})

    def test_fast_is_a_proper_subset_and_has_the_repository_checks(self):
        fast_ids = {row[0] for row in self.fast}
        full_ids = {row[0] for row in self.full}
        self.assertTrue(fast_ids < full_ids)
        self.assertIn("5.1", fast_ids)                                      # the version / changelog consistency check
        self.assertIn("5.2", fast_ids)                                      # the python tests of tests/scripts
        for suite_id in ("1", "2", "2.10", "3", "3.1", "3.10", "3.19"):
            self.assertIn(suite_id, fast_ids)                              # asset decoders, simulation rules, the command layer, the application model suites

    def test_fast_leaves_out_what_the_tier_promises_to_leave_out(self):
        fast_ids = {row[0] for row in self.fast}
        self.assertNotIn("4", fast_ids)                                    # no E2E
        self.assertNotIn("3.8", fast_ids)                                  # no script suites (they start the game and the server)
        self.assertNotIn("3.9", fast_ids)
        for slow in ("2.11", "2.19", "2.22", "3.6"):                       # the soak, the server, the worker bot, the network application
            self.assertNotIn(slow, fast_ids)
        self.assertEqual({row[1] for row in self.fast}, {"assets", "sim", "app", "tools"})

    def test_every_suite_that_the_ci_script_steps_run_is_in_the_full_run(self):
        full_ids = {row[0] for row in self.full}
        for suite_id in ("2.18", "2.21", "3.8", "3.9", "4"):                # map_sweep / bot_arena self-tests, the two script suites, the E2E runner
            self.assertIn(suite_id, full_ids)


if __name__ == "__main__":
    unittest.main()
