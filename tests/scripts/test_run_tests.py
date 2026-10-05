#!/usr/bin/env python3
"""Self-check of ./run_tests.sh and its tiers (run by ./run_tests.sh --fast and by the CI).

The runner takes its suites from a table (`suite ID TIER QUICK TARGETS LABEL TITLE COMMAND`). A test can give its own table (ANTS_RUN_TESTS_TABLE=<file>, which
builds nothing), so these tests run the real script on made-up suites and check what the tiers promise:

  - --fast runs the quick suites only, the full run runs all of them (a marker file proves which commands ran)
  - a failing suite makes the script exit 1 and says which, and the suites after it still run
  - the summary prints the time of every suite, the slowest ones and the totals; the final marker lines are unchanged (agents wait for them)
  - the tier options select as before (--sim, --app, --assets, --e2e) and --fast filters them
  - --asan gives the suites the time scale of a sanitized build (ANTS_E2E_TIME_SCALE=4, a value that is set stays); a run without it sets none
  - the real table: what --fast contains (and does not), listed without running anything
  - the parallel run (the default; --jobs N, --serial): suites that are independent run at the same time and only then, each suite's output stays together and is printed
    in the order of the table, the result of every suite is the same as in a serial run, exclusive / lock / weight in the table are kept, every suite has a temporary
    folder of its own, the longest suites start first (the cost of the table, then the time that the last run measured), and a signal stops what was started
"""
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCRIPT = os.path.join(REPO, "run_tests.sh")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import process_state  # noqa: E402  (a zombie, a stopped process that is not collected yet, counts as stopped)

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

SCALE_STUB_TABLE = r'''
suite "A1" sim   1 "-" "What a suite sees of the time scale" "SEES" 'sh -c "echo time scale: \${ANTS_E2E_TIME_SCALE:-unset}" > "$MARK_DIR/scale.log"'
'''


class StubRunner(unittest.TestCase):
    """Runs the real script on a table of made-up suites (ANTS_RUN_TESTS_TABLE); MARK_DIR is a folder in which the suites leave their marks."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.marks = os.path.join(self.tmp.name, "marks")
        os.makedirs(self.marks)
        self.base_tmp = os.path.join(self.tmp.name, "tmp")                     # the TMPDIR of the script: what it leaves behind in it is visible
        os.makedirs(self.base_tmp)

    def tearDown(self):
        self.tmp.cleanup()

    def table(self, text):
        path = os.path.join(self.tmp.name, "table.sh")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        return path

    def environment(self, table_text=None, **extra):
        env = dict(os.environ)
        env["NO_COLOR"] = "1"
        env["MARK_DIR"] = self.marks
        env["TMPDIR"] = self.base_tmp
        env.pop("ANTS_RUN_TESTS_TABLE", None)
        env.pop("ANTS_RUN_TESTS_HISTORY", None)
        env.pop("ANTS_E2E_TIME_SCALE", None)
        if table_text is not None:
            env["ANTS_RUN_TESTS_TABLE"] = self.table(table_text)
        env.update(extra)
        return env

    def run_script(self, args, table_text=None, **extra):
        return subprocess.run(["bash", SCRIPT, *args], capture_output=True, text=True, env=self.environment(table_text, **extra), cwd=REPO)

    def ran(self):
        return sorted(n for n in os.listdir(self.marks) if not n.endswith(".log"))

    def read_marks(self, name):
        with open(os.path.join(self.marks, name), encoding="utf-8") as f:
            return f.read()

    def suites(self, *lines):
        return "\n".join(lines) + "\n"


class RunnerTiers(StubRunner):

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


class SanitizedRun(StubRunner):
    """--asan runs the same suites, with the time scale that a sanitized build needs (the restore section of tests/scripts/test_ants_server.sh reads it)."""

    def scale_seen_by_the_suite(self, args, **extra):
        mark = os.path.join(self.marks, "scale.log")
        if os.path.exists(mark):
            os.remove(mark)                                                    # (the mark of an earlier call would prove nothing)
        result = self.run_script(args, SCALE_STUB_TABLE, **extra)
        self.assertEqual(result.returncode, 0, result.stdout)
        return self.read_marks("scale.log").strip()

    def test_a_sanitized_run_gives_the_suites_a_time_scale_of_four(self):
        self.assertEqual(self.scale_seen_by_the_suite(["--asan"]), "time scale: 4")

    def test_a_scale_that_is_set_stays(self):
        self.assertEqual(self.scale_seen_by_the_suite(["--asan"], ANTS_E2E_TIME_SCALE="7"), "time scale: 7")

    def test_a_run_without_the_sanitizers_sets_none(self):
        self.assertEqual(self.scale_seen_by_the_suite([]), "time scale: unset")
        self.assertEqual(self.scale_seen_by_the_suite(["--fast"]), "time scale: unset")


# Each of these suites waits (up to two seconds) for the mark that the other one leaves: they can only pass when they run at the same time
def meet(me, other, loops=20):
    return ("""touch "$MARK_DIR/%s.up"; for n in $(seq 1 %d); do [ -e "$MARK_DIR/%s.up" ] && { touch "$MARK_DIR/%s.saw"; exit 0; }; sleep 0.1; done; exit 1""" % (me, loops, other, me))


class ParallelRunner(StubRunner):
    """The parallel run: suites overlap, the output stays in the order of the table and together, the results are those of a serial run."""
    def test_independent_suites_run_at_the_same_time_and_a_serial_run_does_not(self):
        table = self.suites('suite "A" sim 1 "-" "Suite A" "A" \'%s\'' % meet("a", "b", 10), 'suite "B" sim 1 "-" "Suite B" "B" \'%s\'' % meet("b", "a", 10))
        result = self.run_script(["--jobs", "2"], table)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.ran(), ["a.saw", "a.up", "b.saw", "b.up"])                    # each saw the other: they overlapped
        self.assertIn("[PARALLEL] 2 suites, up to 2 at a time", result.stdout)
        for name in os.listdir(self.marks):
            os.remove(os.path.join(self.marks, name))
        serial = self.run_script(["--serial"], table)
        self.assertEqual(serial.returncode, 1)                                              # the first one waited for a partner that never came
        self.assertNotIn("a.saw", self.ran())
        self.assertNotIn("[PARALLEL]", serial.stdout)
        self.assertNotIn("Parallel run:", serial.stdout)

    def test_the_default_runs_in_parallel_and_jobs_one_is_serial(self):
        table = self.suites('suite "A" sim 1 "-" "Suite A" "A" \'%s\'' % meet("a", "b", 10), 'suite "B" sim 1 "-" "Suite B" "B" \'%s\'' % meet("b", "a", 10))
        self.assertEqual(self.run_script([], table).returncode, 0)                          # no option: as many at a time as there are cores (two suites here)
        for name in os.listdir(self.marks):
            os.remove(os.path.join(self.marks, name))
        self.assertEqual(self.run_script(["--jobs", "1"], table).returncode, 1)
        self.assertEqual(self.run_script(["--jobs=2"], table).returncode, 0)

    def test_the_output_is_printed_in_the_order_of_the_table_not_of_the_finish(self):
        table = self.suites('suite "S1" sim 1 "-" "Slow first" "SLOW" \'sleep 1; echo first-output\'', 'suite "S2" sim 1 "-" "Quick second" "QUICK" \'echo second-output\'')
        result = self.run_script(["--jobs", "2"], table)
        out = result.stdout
        self.assertEqual(result.returncode, 0)
        self.assertLess(out.index(">>> S1."), out.index("first-output"))
        self.assertLess(out.index("first-output"), out.index(">>> S2."))
        self.assertLess(out.index(">>> S2."), out.index("second-output"))
        self.assertLess(out.index("second-output"), out.index("OVERALL TEST RUN SUMMARY"))
        summary = out[out.index("OVERALL TEST RUN SUMMARY"):]
        self.assertLess(summary.index(" S1 "), summary.index(" S2 "))                        # the summary too is in the order of the table

    def test_the_output_of_every_suite_stays_together(self):
        body = 'for n in $(seq 1 15); do echo %s-line-$n; sleep 0.02; done'
        table = self.suites('suite "A" sim 1 "-" "Suite A" "A" \'%s\'' % (body % "A"), 'suite "B" sim 1 "-" "Suite B" "B" \'%s\'' % (body % "B"))
        out = self.run_script(["--jobs", "2"], table).stdout
        lines = [l for l in out.splitlines() if "-line-" in l]
        self.assertEqual(lines, ["A-line-%d" % n for n in range(1, 16)] + ["B-line-%d" % n for n in range(1, 16)])

    def test_the_results_are_the_same_as_in_a_serial_run(self):
        table = self.suites('suite "P1" sim 1 "-" "Passes" "ONE" \'sleep 0.3; echo hello\'', 'suite "F1" sim 1 "-" "Fails" "TWO" \'sleep 0.1; exit 3\'',
                            'suite "P2" app 1 "-" "Passes too" "THREE" \'true\'', 'suite "F2" app 1 "-" "Not found" "FOUR" \'./no-such-program-here\'')

        def verdicts(result):
            return re.findall(r"^ (\S+)\s+.*?:\s+(PASSED|FAILED \(exit code \d+\))", result.stdout, re.M)
        serial = self.run_script(["--serial"], table)
        parallel = self.run_script(["--jobs", "4"], table)
        self.assertEqual(serial.returncode, 1)
        self.assertEqual(parallel.returncode, 1)
        self.assertEqual(verdicts(parallel), verdicts(serial))
        self.assertEqual(verdicts(parallel), [("P1", "PASSED"), ("F1", "FAILED (exit code 3)"), ("P2", "PASSED"), ("F2", "FAILED (exit code 127)")])
        for result in (serial, parallel):
            self.assertIn("Suites run: 4, failed: 2", result.stdout)
            self.assertIn("RESULT: 2 TEST SUITE(S) FAILED!", result.stdout)
        self.assertIn("hello", parallel.stdout)


class ParallelResources(StubRunner):
    """What the table says about resources: jobs, exclusive, lock, weight, cost, and the times that the last run measured."""
    def test_jobs_limits_how_many_suites_run_at_once(self):
        body = ("""d=$(mktemp -d "$MARK_DIR/run.XXXXXX"); n=$(ls -d "$MARK_DIR"/run.* | wc -l | tr -d "[:space:]"); echo $n >> "$MARK_DIR/count.log"; sleep 0.4; rmdir "$d" """)
        table = self.suites(*['suite "R%d" sim 1 "-" "Run %d" "R%d" \'%s\'' % (n, n, n, body) for n in range(1, 7)])
        for jobs, expected in (("3", 3), ("1", 1)):
            counts = os.path.join(self.marks, "count.log")
            if os.path.exists(counts):
                os.remove(counts)
            result = self.run_script(["--jobs", jobs], table)
            self.assertEqual(result.returncode, 0, result.stdout)
            seen = [int(x) for x in self.read_marks("count.log").split()]
            self.assertEqual(max(seen), expected, "--jobs %s: the most that ran at once was %d" % (jobs, max(seen)))

    def test_an_exclusive_suite_runs_alone(self):
        x = 'touch "$MARK_DIR/x.up"; sleep 0.6; touch "$MARK_DIR/x.done"'
        e = 'if [ -e "$MARK_DIR/x.up" ] && [ ! -e "$MARK_DIR/x.done" ]; then exit 1; fi; touch "$MARK_DIR/e.running"; sleep 0.6; rm "$MARK_DIR/e.running"'
        y = '[ ! -e "$MARK_DIR/e.running" ] || exit 1; touch "$MARK_DIR/y.ok"'
        table = self.suites('suite "X" sim 1 "-" "Normal first" "X" \'%s\'' % x, 'suite "E" sim 1 "-" "Exclusive" "E" \'%s\' "exclusive"' % e, 'suite "Y" sim 1 "-" "Normal after" "Y" \'%s\'' % y)
        result = self.run_script(["--jobs", "3"], table)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("y.ok", self.ran())
        # the control: without `exclusive` the same suites overlap and E fails (the test can tell the difference)
        for name in os.listdir(self.marks):
            os.remove(os.path.join(self.marks, name))
        control = table.replace(' "exclusive"', "")
        self.assertEqual(self.run_script(["--jobs", "3"], control).returncode, 1)

    def test_suites_with_the_same_lock_never_run_together(self):
        inside = 'mkdir "$MARK_DIR/inside" || exit 1; sleep 0.5; rmdir "$MARK_DIR/inside"'
        table = self.suites('suite "A" sim 1 "-" "Has the lock" "A" \'%s\' "lock=port"' % inside, 'suite "B" sim 1 "-" "Has it too" "B" \'%s\' "lock=port"' % inside,
                            'suite "C" sim 1 "-" "Has none" "C" \'touch "$MARK_DIR/c.ran"; sleep 0.3\'')
        result = self.run_script(["--jobs", "3"], table)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("c.ran", self.ran())
        control = table.replace(' "lock=port"', "")
        self.assertEqual(self.run_script(["--jobs", "3"], control).returncode, 1)               # without the lock they collide in the folder

    def test_a_suite_takes_as_many_job_slots_as_its_weight(self):
        w = 'touch "$MARK_DIR/w.up"; sleep 0.6; touch "$MARK_DIR/w.done"'
        z = '[ ! -e "$MARK_DIR/w.up" ] || [ -e "$MARK_DIR/w.done" ] || exit 1'
        table = self.suites('suite "W" sim 1 "-" "Heavy" "W" \'%s\' "weight=2"' % w, 'suite "Z" sim 1 "-" "Light" "Z" \'%s\'' % z)
        self.assertEqual(self.run_script(["--jobs", "2"], table).returncode, 0)
        for name in os.listdir(self.marks):
            os.remove(os.path.join(self.marks, name))
        self.assertEqual(self.run_script(["--jobs", "2"], table.replace(' "weight=2"', "")).returncode, 1)     # with one slot each they overlap

    def test_a_weight_above_the_jobs_still_runs(self):
        table = self.suites('suite "W" sim 1 "-" "Heavy" "W" \'touch "$MARK_DIR/w.ran"\' "weight=9"')
        self.assertEqual(self.run_script(["--jobs", "2"], table).returncode, 0)
        self.assertIn("w.ran", self.ran())

    def test_the_longest_suites_start_first(self):
        note = 'echo %s >> "$MARK_DIR/order.log"; sleep 0.5'
        table = self.suites('suite "A" sim 1 "-" "A" "A" \'%s\'' % (note % "A"), 'suite "B" sim 1 "-" "B" "B" \'%s\'' % (note % "B"),
                            'suite "C" sim 1 "-" "C" "C" \'%s\' "cost=5"' % (note % "C"))
        self.assertEqual(self.run_script(["--jobs", "2"], table).returncode, 0)
        order = self.read_marks("order.log").split()
        self.assertEqual(sorted(order[:2]), ["A", "C"], order)                                  # C (the costly one) is among the first two, B waits for a free slot
        self.assertEqual(order[2], "B")

    def test_the_times_of_the_last_run_decide_the_order_of_the_next(self):
        history = os.path.join(self.tmp.name, "times.tsv")
        note = 'echo %s >> "$MARK_DIR/order.log"; %s'
        table = self.suites('suite "A" sim 1 "-" "A" "A" \'%s\'' % (note % ("A", "true")), 'suite "B" sim 1 "-" "B" "B" \'%s\'' % (note % ("B", "sleep 1")),
                            'suite "C" sim 1 "-" "C" "C" \'%s\'' % (note % ("C", "true")), 'suite "F" sim 1 "-" "F" "F" \'%s\'' % (note % ("F", "exit 1")))
        self.run_script(["--serial"], table, ANTS_RUN_TESTS_HISTORY=history)
        with open(history, encoding="utf-8") as f:
            recorded = dict(line.split("\t") for line in f.read().splitlines())
        self.assertEqual(sorted(recorded), ["A", "B", "C"])                                     # the failed suite F is not recorded
        self.assertGreaterEqual(int(recorded["B"]), 900)
        os.remove(os.path.join(self.marks, "order.log"))
        self.run_script(["--jobs", "2"], table, ANTS_RUN_TESTS_HISTORY=history)
        order = self.read_marks("order.log").split()
        self.assertIn("B", order[:2], order)                                                   # the slow one started in the first round although it is second in the table
        with open(history, encoding="utf-8") as f:
            self.assertIn("B\t", f.read())


class ParallelHousekeeping(StubRunner):
    """The parts around it: a temporary folder for every suite, the option, the optional column, a signal."""
    def test_every_suite_has_a_temporary_folder_of_its_own(self):
        body = 'test -d "$TMPDIR" && echo "folder:$TMPDIR" >> "$MARK_DIR/folders.log"; touch "$TMPDIR/leftover"'
        table = self.suites('suite "A" sim 1 "-" "A" "A" \'%s\'' % body, 'suite "B" sim 1 "-" "B" "B" \'%s\'' % body)
        result = self.run_script(["--jobs", "2"], table)
        self.assertEqual(result.returncode, 0, result.stdout)
        folders = self.read_marks("folders.log").split()
        self.assertEqual(len(folders), 2)
        self.assertEqual(len(set(folders)), 2, "the two suites shared a temporary folder")
        self.assertTrue(all(f.startswith("folder:" + self.base_tmp) for f in folders))
        self.assertEqual(os.listdir(self.base_tmp), [], "the run left its folders behind")
        os.remove(os.path.join(self.marks, "folders.log"))
        self.assertEqual(self.run_script(["--serial"], table).returncode, 0)                  # a serial run does not change the folder: as it always was
        self.assertEqual(set(self.read_marks("folders.log").split()), {"folder:" + self.base_tmp})

    def test_the_jobs_option_is_checked(self):
        for args in (["--jobs", "0"], ["--jobs", "many"], ["--jobs"], ["--jobs=0"], ["-j", "-3"]):
            result = self.run_script(args, STUB_TABLE)
            self.assertEqual(result.returncode, 1, args)
            self.assertIn("--jobs needs a number", result.stdout + result.stderr)
        self.assertEqual(self.run_script(["-j", "2"], STUB_TABLE).returncode, 0)

    def test_the_resource_column_is_optional_and_other_words_do_no_harm(self):
        table = self.suites('suite "A" sim 1 "-" "No resources" "A" \'touch "$MARK_DIR/a"\'', 'suite "B" sim 1 "-" "Resources" "B" \'touch "$MARK_DIR/b"\' "cost=3 something-else"',
                            'suite "C" sim 1 "-" "Empty resources" "C" \'touch "$MARK_DIR/c"\' ""')
        self.assertEqual(self.run_script(["--jobs", "3"], table).returncode, 0)
        self.assertEqual(self.ran(), ["a", "b", "c"])

    def test_a_signal_stops_the_suites_that_were_started_and_cleans_up(self):
        started = os.path.join(self.marks, "started")
        child_pid = os.path.join(self.marks, "child.pid")
        table = self.suites('suite "SLEEPER" sim 1 "-" "Sleeps" "SLEEPER" \'sleep 300 & echo $! > "%s"; touch "%s"; wait\'' % (child_pid, started),
                            'suite "OTHER" sim 1 "-" "Sleeps too" "OTHER" \'sleep 300\'')
        proc = subprocess.Popen(["bash", SCRIPT, "--jobs", "2"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=self.environment(table), cwd=REPO)
        deadline = time.time() + 20
        while time.time() < deadline and not os.path.exists(started):
            time.sleep(0.05)
        self.assertTrue(os.path.exists(started), "the suite never started")
        time.sleep(0.3)
        pid = int(self.read_marks("child.pid").strip())
        proc.send_signal(signal.SIGTERM)
        proc.communicate(timeout=20)
        self.assertEqual(proc.returncode, 130)
        time.sleep(0.3)
        alive = process_state.running(pid)
        if alive:
            os.kill(pid, signal.SIGKILL)
        self.assertFalse(alive, "a process that a suite started was left running")
        self.assertEqual(os.listdir(self.base_tmp), [], "the interrupted run left its folder behind")

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

    def test_the_table_runs_every_part_of_the_server_script_and_nothing_else_of_it(self):
        with open(SCRIPT, encoding="utf-8") as f:
            used = re.findall(r'test_ants_server\.sh" --part (\w+)', f.read())
        listed = subprocess.run(["bash", os.path.join(REPO, "tests", "scripts", "test_ants_server.sh"), "--list-parts"], capture_output=True, text=True).stdout.split()
        self.assertEqual(sorted(used), sorted(listed), "run_tests.sh and tests/scripts/test_ants_server.sh --list-parts name different parts")
        self.assertEqual(len(used), len(set(used)))
        for suite_id in ("3.9", "3.9.1", "3.9.2", "3.9.3", "3.9.4"):
            self.assertIn(suite_id, {row[0] for row in self.full})

    def test_every_suite_that_the_ci_script_steps_run_is_in_the_full_run(self):
        full_ids = {row[0] for row in self.full}
        for suite_id in ("2.18", "2.21", "3.8", "3.9", "4"):                # map_sweep / bot_arena self-tests, the two script suites, the E2E runner
            self.assertIn(suite_id, full_ids)


if __name__ == "__main__":
    unittest.main()
