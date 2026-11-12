#!/usr/bin/env python3
"""Tests of tests/scripts/run_python_tests.py, which runs the python tests side by side (suite 5.2 of ./run_tests.sh and the CI).

It runs on made-up test folders (tmp/tests/scripts/test_*.py) and checks: the result is that of unittest (a failing test makes the exit status 1 and its output is shown, the
others still run), the classes run at the same time (two tests that wait for each other pass only when they overlap), a file that cannot be imported fails and says why, and
on the REAL tests/scripts the classes that it runs are exactly the classes that unittest's own loader finds (so that no test is left out). The two ways in which the CI splits the work are
checked here too: --shard K/N (the slices are every group once and cost about the same) and --skip-linux-only (the macOS job's list names page tests only and keeps the groups that the macOS runners
taught the tests).
"""
import contextlib
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "run_python_tests.py")
sys.path.insert(0, HERE)
import run_python_tests as rpt     # noqa: E402
import mutant_shard                # noqa: E402

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

    def test_the_long_ones_start_first_and_the_output_keeps_the_order_of_the_files(self):
        mark = "import os, time, unittest\nclass %s(unittest.TestCase):\n    def test_x(self):\n        with open(os.path.join(os.environ['MARK_DIR'], '%s'), 'w') as f:\n            f.write(str(time.monotonic_ns()))\n"
        for name, cls in (("test_a.py", "First"), ("test_b.py", "Second"), ("test_c.py", "Long")):
            self.add(name, mark % (cls, name))
        saved = dict(rpt.COSTS)
        os.environ["MARK_DIR"] = self.marks
        out = io.StringIO()
        try:
            rpt.COSTS.clear()
            rpt.COSTS["test_c.Long"] = 50
            with contextlib.redirect_stdout(out):
                status = rpt.main(["run_python_tests.py", "--dir", self.scripts, "--jobs", "1"])
        finally:
            rpt.COSTS.clear()
            rpt.COSTS.update(saved)
            del os.environ["MARK_DIR"]
        self.assertEqual(status, 0, out.getvalue())
        began = {}
        for name in ("test_a.py", "test_b.py", "test_c.py"):
            with open(os.path.join(self.marks, name)) as f:
                began[name] = int(f.read())
        self.assertLess(began["test_c.py"], began["test_a.py"], "the group with a cost starts first")
        self.assertLess(began["test_a.py"], began["test_b.py"], "the others keep the order of the files")
        self.assertEqual([line.split()[0] for line in out.getvalue().splitlines() if line.startswith("test_")], ["test_a.First", "test_b.Second", "test_c.Long"])

    def many(self, count=7):
        for i in range(count):
            self.add("test_f%d.py" % i, PASSING.replace("Good", "Class%d" % i))

    def listing(self, *args):
        result = self.run_tool("--list", *args)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return [line.split("\t")[0] for line in result.stdout.splitlines()]

    def test_the_slices_are_every_group_once_in_the_order_of_the_files(self):
        self.many(7)
        everything = self.listing()
        for n in (1, 2, 3, 7):
            slices = [self.listing("--shard", "%d/%d" % (k, n)) for k in range(1, n + 1)]
            self.assertEqual(sorted(sum(slices, [])), sorted(everything), n)             # every group, none twice
            for one in slices:
                self.assertTrue(one, "a slice without a group would pass without running anything")
                self.assertEqual(one, [g for g in everything if g in one], "the order of the files")

    def test_a_slice_runs_its_groups_only_and_says_which_slice_it_is(self):
        self.many(4)
        seen = []
        for k in (1, 2):
            result = self.run_tool("--shard", "%d/2" % k, "--jobs", "2")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Ran 4 tests in 2 groups of slice %d/2" % k, result.stdout)
            seen += [line.split()[0] for line in result.stdout.splitlines() if line.startswith("test_f")]
        self.assertEqual(sorted(seen), ["test_f0.Class0", "test_f1.Class1", "test_f2.Class2", "test_f3.Class3"])

    def test_a_failing_group_fails_its_slice_and_only_its_slice(self):
        self.add("test_bad.py", FAILING)
        self.many(3)
        codes = [self.run_tool("--shard", "%d/2" % k).returncode for k in (1, 2)]
        self.assertEqual(sorted(codes), [0, 1])

    def test_the_slices_share_the_groups_by_their_costs(self):
        work = [("test_%s.C" % name, "tests.scripts.test_%s.C" % name) for name in "abcde"]
        saved = dict(rpt.COSTS)
        try:
            rpt.COSTS.clear()
            rpt.COSTS.update({"test_a.C": 100, "test_b.C": 60, "test_c.C": 50, "test_d.C": 40, "test_e.C": 10})
            self.assertEqual([w[0] for w in rpt.shard_of(work, 1, 2)], ["test_a.C", "test_d.C"])           # (100, then 140 against 60 + 50 + 10)
            self.assertEqual([w[0] for w in rpt.shard_of(work, 2, 2)], ["test_b.C", "test_c.C", "test_e.C"])
            rpt.COSTS.clear()
            self.assertEqual([w[0] for w in rpt.shard_of(work, 1, 2)], ["test_a.C", "test_c.C", "test_e.C"])      # (no cost: a group counts 1, so the slices have the same number)
            self.assertEqual([w[0] for w in rpt.shard_of(work, 2, 2)], ["test_b.C", "test_d.C"])
            rpt.COSTS.update({"test_c.C": 50, "test_d.C": 40, "test_e.C": 30})
            self.assertEqual([w[0] for w in rpt.shard_of(work, 1, 2)], ["test_a.C", "test_b.C", "test_c.C"])      # (c is placed first, a and b after it: the slice keeps the order of the files)
            self.assertEqual([w[0] for w in rpt.shard_of(work, 2, 2)], ["test_d.C", "test_e.C"])
        finally:
            rpt.COSTS.clear()
            rpt.COSTS.update(saved)

    def test_a_slice_that_is_not_one_is_refused(self):
        self.many(3)
        for bad in ("0/2", "3/2", "1/0", "1/4", "x", "1", "1/2/3", "-1/2", ""):
            result = self.run_tool("--shard", bad)
            self.assertEqual(result.returncode, 2, bad)
            self.assertIn("--shard", result.stderr, bad)

    def test_skip_linux_only_leaves_out_the_named_groups_and_nothing_else(self):
        self.many(5)
        saved = set(rpt.LINUX_ONLY)
        try:
            rpt.LINUX_ONLY.clear()
            rpt.LINUX_ONLY.update({"test_f1.Class1", "test_f3.Class3"})
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(rpt.main(["run_python_tests.py", "--dir", self.scripts, "--list", "--skip-linux-only"]), 0)
            self.assertEqual([line.split("\t")[0] for line in out.getvalue().splitlines()], ["test_f0.Class0", "test_f2.Class2", "test_f4.Class4"])
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(rpt.main(["run_python_tests.py", "--dir", self.scripts, "--list", "--skip-linux-only", "--shard", "2/2"]), 0)
            self.assertEqual([line.split("\t")[0] for line in out.getvalue().splitlines()], ["test_f2.Class2"])       # (the slices are of what is left)
        finally:
            rpt.LINUX_ONLY.clear()
            rpt.LINUX_ONLY.update(saved)

    def test_a_sharded_class_is_units_that_each_get_their_number_and_the_others_get_none(self):
        self.add("test_big.py", "import os, unittest\nclass Big(unittest.TestCase):\n    def test_it(self):\n        print('SHARD=' + os.environ.get('%s', 'none'))\n" % rpt.SHARD_ENV)
        self.add("test_small.py", "import os, unittest\nclass Small(unittest.TestCase):\n    def test_it(self):\n        print('SHARD=' + os.environ.get('%s', 'none'))\n" % rpt.SHARD_ENV)
        saved, saved_costs, saved_linux = dict(rpt.SHARDS), dict(rpt.COSTS), set(rpt.LINUX_ONLY)
        os.environ[rpt.SHARD_ENV] = "9/9"                                            # (a caller's variable must not reach a group that is not sharded)
        try:
            rpt.SHARDS.clear()
            rpt.SHARDS["test_big.Big"] = 3
            rpt.COSTS.clear()
            rpt.COSTS["test_big.Big"] = 100
            def here(*args):                    # (in this process: SHARDS and COSTS are patched here)
                out = io.StringIO()
                with contextlib.redirect_stdout(out):
                    self.assertEqual(rpt.main(["run_python_tests.py", "--dir", self.scripts, "--list", *args]), 0)
                return [line.split("\t")[0] for line in out.getvalue().splitlines()]
            listing = here()
            self.assertEqual(listing, ["test_big.Big[1/3]", "test_big.Big[2/3]", "test_big.Big[3/3]", "test_small.Small"])
            self.assertEqual([rpt.cost_of(label) for label in listing], [34, 34, 34, 1])        # (the class's cost shared, rounded up)
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(rpt.main(["run_python_tests.py", "--dir", self.scripts, "--jobs", "2"]), 0)
            self.assertIn("Ran 4 tests in 4 groups", out.getvalue())
            # what each unit saw, by running them as the runner does
            seen = {}
            for label, dotted in rpt.units(self.scripts, "tests.scripts"):
                result = rpt.run_unit(label, dotted, self.tmp.name)
                seen[label] = [line for line in result["output"].splitlines() if line.startswith("SHARD=")]
            self.assertEqual(seen, {"test_big.Big[1/3]": ["SHARD=1/3"], "test_big.Big[2/3]": ["SHARD=2/3"], "test_big.Big[3/3]": ["SHARD=3/3"], "test_small.Small": ["SHARD=none"]})
            # the units are slices' work like any group; --skip-linux-only goes by the class
            rpt.LINUX_ONLY.add("test_big.Big")
            self.assertEqual(here("--skip-linux-only"), ["test_small.Small"])
            rpt.LINUX_ONLY.clear()
            every = [here("--shard", "%d/2" % k) for k in (1, 2)]
            self.assertEqual(sorted(sum(every, [])), sorted(listing))
        finally:
            del os.environ[rpt.SHARD_ENV]
            rpt.SHARDS.clear()
            rpt.SHARDS.update(saved)
            rpt.COSTS.clear()
            rpt.COSTS.update(saved_costs)
            rpt.LINUX_ONLY.clear()
            rpt.LINUX_ONLY.update(saved_linux)

    @unittest.skipUnless(shutil.which("node") and os.name != "nt", "node is not installed (or this is Windows, where the suites do not run)")
    def test_the_node_of_the_tests_is_started_single_threaded(self):
        """a node 24 that exits while a background thread compiles with Maglev can hang for ever (see node_shim in run_python_tests.py): the tests get a node that starts with --single-threaded, so that V8 has no background thread to wait for the main one"""
        self.add("test_node.py", "import json, shutil, subprocess, unittest\nclass Node(unittest.TestCase):\n    def test_flag(self):\n"
                 "        out = subprocess.run([shutil.which('node'), '-p', 'JSON.stringify(process.execArgv)'], capture_output=True, text=True).stdout\n"
                 "        self.assertIn('--single-threaded', json.loads(out))\n")
        result = self.run_tool()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipUnless(shutil.which("node") and os.name != "nt", "node is not installed (or this is Windows)")
    def test_the_shim_runs_the_real_node_with_every_argument(self):
        with tempfile.TemporaryDirectory() as folder:
            self.assertEqual(rpt.node_shim(folder), folder)
            done = subprocess.run([os.path.join(folder, "node"), "-p", "process.execArgv.includes('--single-threaded') + '|' + process.argv.slice(1).join()", "x y", "z"], capture_output=True, text=True)
            self.assertEqual(done.stdout.strip(), "true|x y,z", done.stderr)


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
        grouped = {rpt.base_of(label) for label, _ in rpt.units(HERE, "tests.scripts")}
        self.assertEqual(grouped - found, set(), "the runner would run a group that unittest does not find")
        # a class with tests that is only in the loader's list would be left out by the runner
        self.assertEqual(found - grouped, set(), "the runner would leave these classes out: " + ", ".join(sorted(found - grouped)))

    def test_the_costs_name_classes_that_exist_and_the_long_ones_start_first(self):
        grouped = {rpt.base_of(label) for label, _ in rpt.units(HERE, "tests.scripts")}
        self.assertEqual(set(rpt.COSTS) - grouped, set(), "a cost for a class that is not there")
        self.assertTrue(all(isinstance(v, int) and v >= 1 for v in rpt.COSTS.values()))

    def test_the_slices_of_the_real_tests_are_every_group_once_and_cost_about_the_same(self):
        work = rpt.units(HERE, "tests.scripts")
        every = [label for label, _ in work]
        for n in (2, 3, 4):
            slices = [[label for label, _ in rpt.shard_of(work, k, n)] for k in range(1, n + 1)]
            self.assertEqual(sorted(sum(slices, [])), sorted(every), n)
            costs = [sum(rpt.cost_of(label) for label in one) for one in slices]
            self.assertLessEqual(max(costs) - min(costs), max(map(rpt.cost_of, every)), "slices of %d: %s" % (n, costs))
            self.assertLessEqual(max(costs), 1.05 * sum(costs) / n, "slices of %d: %s" % (n, costs))
        # the biggest groups are in different slices (one of them alone takes more than a fair share of the time of a fourth)
        biggest = sorted((label for label in every if label in rpt.COSTS or rpt.unit_of(label)), key=lambda label: -rpt.cost_of(label))[:2]
        slices = [{label for label, _ in rpt.shard_of(work, k, 2)} for k in (1, 2)]
        self.assertNotEqual(biggest[0] in slices[0], biggest[1] in slices[0])

    def test_the_two_long_mutation_classes_are_units_that_together_try_every_mutant_once(self):
        """The checks stay as strong as they were: the units of a sharded class run the same class, and the mutants of the units are the whole list, each once, whatever the number of units."""
        self.assertEqual(sorted(rpt.SHARDS), ["test_web_home_chat.TheChecksAreNotVacuous", "test_web_home_games.TheChecksAreNotVacuous"])
        work = rpt.units(HERE, "tests.scripts")
        for label, n in rpt.SHARDS.items():
            self.assertGreaterEqual(n, 2)
            self.assertEqual([w for w in work if rpt.base_of(w[0]) == label], [("%s[%d/%d]" % (label, k, n), "tests.scripts." + label) for k in range(1, n + 1)])
            self.assertIn(label, rpt.COSTS)
            self.assertIn(label, rpt.LINUX_ONLY)
        for spec in ("home_games", "home_chat"):
            with open(os.path.join(HERE, "..", "data", spec + "_mutants.json"), encoding="utf-8") as f:
                ids = [m["id"] for m in json.load(f)["mutants"]]
            self.assertGreater(len(ids), 300)
            for n in range(1, 13):
                parts = [mutant_shard.pick(ids, (k, n)) for k in range(1, n + 1)]
                self.assertEqual(sorted(sum(parts, [])), sorted(ids), "%s in %d units" % (spec, n))      # the union is the list, no mutant twice
                self.assertEqual(len(set(sum(parts, []))), len(ids))
                for part in parts:
                    self.assertTrue(part)
                    self.assertEqual(part, [i for i in ids if i in set(part)], "the order of the list")
                self.assertLessEqual(max(map(len, parts)) - min(map(len, parts)), 1, "the units are of the same size")
        for name in ("test_web_home_games.py", "test_web_home_chat.py"):
            with open(os.path.join(HERE, name), encoding="utf-8") as f:
                text = f.read()
            self.assertIn('cls.mutants = mutant_shard.from_environment(cls.spec["mutants"])', text, name)
            self.assertIn("for m in self.mutants:", text, name)
            self.assertNotIn('\n        for m in self.spec["mutants"]', text, name + ": a loop over the whole list would run it in every unit")

    def test_the_variable_of_a_unit_is_read_strictly(self):
        self.assertEqual(mutant_shard.parse(None), (1, 1))
        self.assertEqual(mutant_shard.parse(""), (1, 1))
        self.assertEqual(mutant_shard.parse("3/4"), (3, 4))
        for bad in ("0/4", "5/4", "1/0", "x", "1", "1/2/3", "-1/2", " 1/2", "1/2 "):
            with self.assertRaises(ValueError, msg=bad):
                mutant_shard.parse(bad)
        self.assertEqual(mutant_shard.from_environment(list("abcdefg"), {}), list("abcdefg"))
        self.assertEqual(mutant_shard.from_environment(list("abcdefg"), {mutant_shard.ENV: "2/3"}), ["b", "e"])
        with self.assertRaises(ValueError):                                          # a unit with nothing to try would pass without looking
            mutant_shard.from_environment(["a"], {mutant_shard.ENV: "2/3"})

    def test_the_linux_only_groups_exist_and_are_page_tests(self):
        grouped = {rpt.base_of(label) for label, _ in rpt.units(HERE, "tests.scripts")}
        self.assertEqual(rpt.LINUX_ONLY - grouped, set(), "a Linux-only name that is not a class")
        for label in rpt.LINUX_ONLY:
            self.assertIn(".", label, "a class, not a whole file: " + label)
            self.assertTrue(label.startswith(("test_web_", "test_bot_names.")), "only page tests may stay off the macOS job: " + label)
        self.assertGreaterEqual(len(rpt.LINUX_ONLY), 10)

    def test_the_macos_job_keeps_what_the_macos_runners_taught_the_tests(self):
        left = {rpt.base_of(label) for label, _ in rpt.units(HERE, "tests.scripts")} - rpt.LINUX_ONLY
        for prefix in ("test_hub_", "test_e2e_deadlines.", "test_measure_tick_rate.", "test_quick_http.", "test_run_tests.", "test_run_python_tests.",
                       "test_library_guard.", "test_community_maps.", "test_repair_maps.", "test_cmake_", "test_mutate_tool.", "test_release_tool.", "test_version_tools.", "test_source_portability.",
                       "test_hygiene.", "test_ci_workflow.", "test_nginx_", "test_pbkdf2_vectors.", "test_web_without_docker.",
                       "test_web_mapmaker.TheCodeRuns", "test_web_mapmaker.TheTimeAndTheBail", "test_web_mapmaker_play.ThePlayChecks", "test_web_home_chat.TheCodeRuns",
                       "test_web_lobby_net.TheChecksThatNodeRuns", "test_web_lobby_chat.TheChecksThatNodeRuns", "test_web_rematch.TheChecksThatNodeRuns"):
            self.assertTrue([label for label in left if label.startswith(prefix)], "the macOS job would run nothing of " + prefix)
            self.assertFalse([label for label in rpt.LINUX_ONLY if label.startswith(prefix)], prefix + " is a lesson of the macOS runners: it stays on both")

    def test_the_real_files_are_named_as_the_runner_expects(self):
        names = [f for f in os.listdir(HERE) if f.startswith("test_") and f.endswith(".py")]
        self.assertGreater(len(names), 8)
        self.assertNotIn("run_python_tests.py", names)                                 # (it is not itself a test file: unittest discover must not import it)


if __name__ == "__main__":
    unittest.main()
