#!/usr/bin/env python3
"""Tests of tools/mutate.py, the mutation runner (run by ./run_tests.sh --fast and by the CI).

Each test builds a tiny scratch project in a temporary folder (a few Python files, and one small CMake project for the build step) and runs the tool on it:

  - a mutant that the test command notices is caught, one that nothing notices survived; the table and the JSON report say so, and so does the exit status
  - the file is put back, byte for byte, after every mutant, and also after an error, a timeout, a mutant that does not compile, a SIGINT and a SIGTERM
  - the old text must occur exactly once (else: error, nothing changed), the run stops at the first mutant that cannot be judged unless --keep-going
  - a backup that an interrupted (killed) run left behind is put back by the next start
  - the unmutated tree is built and tested (a "baseline") before the first mutant, after every N mutants and at the end: a baseline that fails refuses the report
    (a stale object of an earlier mutant would poison every later verdict; make 3.81 of a Mac compares file times in whole seconds), a tree that already fails
    is refused before anything is changed
  - with a build step the real code is built again at the end (the binary of the build folder is the real one)
"""
import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TOOL = os.path.join(REPO, "tools", "mutate.py")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import process_state  # noqa: E402  (a zombie, a stopped process that is not collected yet, counts as stopped)
CMAKE = shutil.which("cmake")
COMPILER = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")

CALC = "def add(a, b):\n    return a + b\n\n\ndef scale(x):\n    return x * 2  # the tests never look at this\n"
CHECK = "import sys\nfrom calc import add\nif add(2, 3) != 5:\n    print('FAIL add: expected 5, got', add(2, 3))\n    sys.exit(1)\nprint('ok')\n"


def write(path, text):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def read(path):
    with open(path, encoding="utf-8", newline="") as f:
        return f.read()


class Project(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = self.tmp.name
        write(os.path.join(self.root, "calc.py"), CALC)
        write(os.path.join(self.root, "check.py"), CHECK)
        self.original = read(os.path.join(self.root, "calc.py"))

    def tearDown(self):
        self.tmp.cleanup()

    def spec(self, mutants, **extra):
        path = os.path.join(self.root, "spec.json")
        write(path, json.dumps(dict(mutants=mutants, **extra)))
        return path

    TRUE = {"test": "true"}                                                                     # a baseline for the tests whose test command is slow on purpose

    def run_tool(self, spec, *args, timeout=60):
        env = dict(os.environ)
        env["PYTHONDONTWRITEBYTECODE"] = "1"
        return subprocess.run([sys.executable, TOOL, spec, "--root", self.root, *args], capture_output=True, text=True, env=env, timeout=timeout)

    def report(self):
        return json.loads(read(os.path.join(self.root, "spec.report.json")))

    def by_id(self):
        return {r["id"]: r for r in self.report()["results"]}

    def calc_text(self):
        return read(os.path.join(self.root, "calc.py"))

    def test_a_mutant_that_the_test_notices_is_caught_and_one_that_nothing_notices_survived(self):
        spec = self.spec([
            {"id": "plus-to-minus", "file": "calc.py", "old": "return a + b", "new": "return a - b", "test": "python3 check.py", "expect": "FAIL add"},
            {"id": "scale-ignored", "file": "calc.py", "old": "return x * 2", "new": "return x * 3", "test": "python3 check.py"},
        ])
        result = self.run_tool(spec)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)                  # one survivor: a gap in the tests
        by = self.by_id()
        self.assertEqual(by["plus-to-minus"]["status"], "caught")
        self.assertEqual(by["plus-to-minus"]["exit_code"], 1)
        self.assertIn("FAIL add", by["plus-to-minus"]["detail"])
        self.assertEqual(by["scale-ignored"]["status"], "survived")
        self.assertEqual(self.report()["summary"], {"mutants": 2, "caught": 1, "caught_elsewhere": 0, "survived": 1, "invalid": 0, "not_run": 0})
        self.assertIn("CAUGHT", result.stdout)
        self.assertIn("SURVIVED", result.stdout)
        self.assertIn("mutants: 2, caught: 1, survived: 1", result.stdout)
        self.assertEqual(self.calc_text(), self.original)                                       # restored, byte for byte
        self.assertFalse(os.path.exists(os.path.join(self.root, "calc.py.mutbak")))

    def test_all_caught_exits_zero(self):
        spec = self.spec([{"id": "m", "file": "calc.py", "old": "a + b", "new": "a * b", "test": "python3 check.py"}])
        result = self.run_tool(spec)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.calc_text(), self.original)

    def test_the_detail_is_the_assertion_not_a_test_title_with_the_word_error_in_it(self):
        noisy = "echo 'RUNNING: W1.6 Every protocol error is answered ... PASS'; echo 'RUNNING: S3.81 The count ... FAILED!'; echo '    Assertion failed: busy().matches == 1 at test.cpp:42'; exit 1"
        spec = self.spec([{"id": "m", "file": "calc.py", "old": "a + b", "new": "a - b", "test": noisy}], baseline=self.TRUE)
        self.assertEqual(self.run_tool(spec).returncode, 0)
        self.assertEqual(self.by_id()["m"]["detail"], "Assertion failed: busy().matches == 1 at test.cpp:42")
        for output, expected in (("an error here\nthen FAIL: the real one", "then FAIL: the real one"), ("all fine\nand an error word\nlast line", "and an error word"), ("one\ntwo", "two")):
            sys.path.insert(0, os.path.join(REPO, "tools"))
            import mutate
            self.assertEqual(mutate.first_failure_line(output), expected)

    def test_the_test_command_sees_the_changed_file(self):
        marker = os.path.join(self.root, "seen.txt")
        spec = self.spec([{"id": "m", "file": "calc.py", "old": "a + b", "new": "a - b", "test": "cat calc.py >> seen.txt; python3 check.py"}])
        self.run_tool(spec)
        seen = read(marker)                                                                     # one copy for the baseline before, one for the mutant, one for the baseline after
        self.assertEqual(seen.count("return a - b"), 1)
        self.assertEqual(seen.count("return a + b"), 2)
        self.assertEqual(self.calc_text(), self.original)

    def test_expect_that_is_not_in_the_output_is_caught_elsewhere(self):
        spec = self.spec([{"id": "m", "file": "calc.py", "old": "a + b", "new": "a - b", "test": "python3 check.py", "expect": "T9.9"}])
        result = self.run_tool(spec)
        self.assertEqual(result.returncode, 0)                                                  # it did fail: caught, but not where the author meant
        entry = self.by_id()["m"]
        self.assertEqual(entry["status"], "caught-elsewhere")
        self.assertIn("T9.9", entry["detail"])
        self.assertEqual(self.report()["summary"]["caught_elsewhere"], 1)

    def test_old_text_that_is_missing_or_ambiguous_is_an_error_and_nothing_is_run(self):
        spec = self.spec([
            {"id": "ok", "file": "calc.py", "old": "a + b", "new": "a - b", "test": "python3 check.py"},
            {"id": "missing", "file": "calc.py", "old": "no such text", "new": "x", "test": "true"},
            {"id": "twice", "file": "calc.py", "old": "return", "new": "return 0 +", "test": "true"},
        ])
        result = self.run_tool(spec)
        self.assertEqual(result.returncode, 2)
        self.assertIn("missing", result.stderr)
        self.assertIn("occurs 0 times", result.stderr)
        self.assertIn("occurs 2 times", result.stderr)
        self.assertIn("nothing was run", result.stderr)
        self.assertFalse(os.path.exists(os.path.join(self.root, "spec.report.json")))
        self.assertEqual(self.calc_text(), self.original)

    def test_check_validates_and_changes_nothing(self):
        spec = self.spec([{"id": "ok", "file": "calc.py", "old": "a + b", "new": "a - b", "test": "false"},
                          {"id": "bad", "file": "calc.py", "old": "nope", "new": "x", "test": "false"}])
        result = self.run_tool(spec, "--check")
        self.assertEqual(result.returncode, 2)
        self.assertIn("ERROR  bad", result.stdout)
        self.assertIn("2 mutants, 1 that cannot be applied", result.stdout)
        self.assertEqual(self.calc_text(), self.original)
        good = self.spec([{"id": "ok", "file": "calc.py", "old": "a + b", "new": "a - b", "test": "false"}])
        self.assertEqual(self.run_tool(good, "--check").returncode, 0)

    def test_keep_going_runs_the_others_after_one_that_cannot_be_judged(self):
        mutants = [
            {"id": "slow", "file": "calc.py", "old": "a + b", "new": "a - b", "test": "sleep 30", },
            {"id": "after", "file": "calc.py", "old": "a + b", "new": "a * b", "test": "python3 check.py"},
        ]
        spec = self.spec(mutants, baseline=self.TRUE)
        started = time.time()
        stopped = self.run_tool(spec, "--timeout", "1")
        self.assertLess(time.time() - started, 25, "the time limit did not end the sleeping test")
        self.assertEqual(stopped.returncode, 2)
        by = self.by_id()
        self.assertEqual(by["slow"]["status"], "timeout")
        self.assertEqual(by["after"]["status"], "not-run")                                      # stopped at the first one that could not be judged
        self.assertEqual(self.calc_text(), self.original)
        going = self.run_tool(spec, "--timeout", "1", "--keep-going")
        self.assertEqual(going.returncode, 2)
        by = self.by_id()
        self.assertEqual(by["slow"]["status"], "timeout")
        self.assertEqual(by["after"]["status"], "caught")
        self.assertEqual(self.calc_text(), self.original)

    def test_a_timeout_kills_what_the_test_command_started(self):
        pid_file = os.path.join(self.root, "child.pid")
        spec = self.spec([{"id": "loop", "file": "calc.py", "old": "a + b", "new": "a - b", "test": "sleep 300 & echo $! > child.pid; wait"}], baseline=self.TRUE)
        self.run_tool(spec, "--timeout", "1")
        pid = int(read(pid_file).strip())
        time.sleep(0.3)
        alive = process_state.running(pid)
        if alive:
            os.kill(pid, signal.SIGKILL)
        self.assertFalse(alive, "the process that the test command started is still running after the time limit")

    def test_only_runs_the_named_mutants(self):
        spec = self.spec([{"id": "a", "file": "calc.py", "old": "a + b", "new": "a - b", "test": "python3 check.py"},
                          {"id": "b", "file": "calc.py", "old": "x * 2", "new": "x * 3", "test": "python3 check.py"}])
        result = self.run_tool(spec, "--only", "a")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(list(self.by_id()), ["a"])
        self.assertEqual(self.run_tool(spec, "--only", "nope").returncode, 2)

    def test_defaults_fill_in_what_a_mutant_leaves_out(self):
        path = os.path.join(self.root, "spec.json")
        write(path, json.dumps({"defaults": {"test": "python3 check.py", "file": "calc.py"}, "mutants": [{"id": "d", "old": "a + b", "new": "a - b"}]}))
        result = self.run_tool(path)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.by_id()["d"]["status"], "caught")

    def test_a_spec_that_is_wrong_is_refused_with_a_message(self):
        for bad, text in (({"mutants": []}, "at least one mutant"),
                          ({"mutants": [{"id": "x", "file": "../outside", "old": "a", "new": "b", "test": "true"}]}, "inside the root"),
                          ({"mutants": [{"id": "x", "file": "calc.py", "old": "a", "new": "a", "test": "true"}]}, "same text"),
                          ({"mutants": [{"id": "x", "file": "calc.py", "old": "a", "test": "true"}]}, "new is missing"),
                          ({"mutants": [{"id": "x", "file": "calc.py", "old": "a", "new": "b", "test": "true"}] * 2}, "used twice")):
            path = os.path.join(self.root, "bad.json")
            write(path, json.dumps(bad))
            result = self.run_tool(path)
            self.assertEqual(result.returncode, 2, bad)
            self.assertIn(text, result.stderr)
        not_json = os.path.join(self.root, "notjson.json")
        write(not_json, "{oops")
        self.assertEqual(self.run_tool(not_json).returncode, 2)
        self.assertEqual(self.run_tool(os.path.join(self.root, "missing.json")).returncode, 2)

    # ---- the baselines ----------------------------------------------------------------------------------------------------------------------

    def test_the_unmutated_tree_is_tested_before_the_first_mutant_after_every_n_and_at_the_end(self):
        test = "echo run >> runs.log; python3 check.py"
        spec = self.spec([{"id": "m1", "file": "calc.py", "old": "a + b", "new": "a - b", "test": test},
                          {"id": "m2", "file": "calc.py", "old": "a + b", "new": "a * b", "test": test}])
        result = self.run_tool(spec)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(len(read(os.path.join(self.root, "runs.log")).split()), 4)             # start, m1, m2, end
        self.assertEqual([b["when"] for b in self.report()["baselines"]], ["start", "end"])
        self.assertTrue(self.report()["valid"])
        self.assertIn("baselines of the unmutated tree: 2, all passed", result.stdout)
        os.remove(os.path.join(self.root, "runs.log"))
        result = self.run_tool(spec, "--baseline-every", "1")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(len(read(os.path.join(self.root, "runs.log")).split()), 5)             # start, m1, after 1 mutants, m2, end (nothing twice after the last)
        self.assertEqual([b["when"] for b in self.report()["baselines"]], ["start", "after 1 mutants", "end"])

    def test_a_tree_that_fails_before_any_change_is_refused_and_nothing_is_changed(self):
        broken = self.original.replace("a + b", "a - b")
        write(os.path.join(self.root, "calc.py"), broken)                                       # check.py fails on the code as it is
        spec = self.spec([{"id": "m", "file": "calc.py", "old": "a - b", "new": "a * b", "test": "python3 check.py"}])
        result = self.run_tool(spec)
        self.assertEqual(result.returncode, 2)
        self.assertIn("THE BASELINE FAILED (start)", result.stderr)
        self.assertIn("before any change", result.stderr)
        self.assertNotIn("mutants:", result.stdout)                                             # no table, no summary
        self.assertEqual(self.report()["valid"], False)
        self.assertEqual(self.report()["results"], [])
        self.assertEqual(self.calc_text(), broken)

    def test_a_baseline_that_fails_after_a_mutant_refuses_to_report(self):
        # the mutant's test command leaves a "poison" behind: the unmutated tree then fails the baseline, as a stale object of a mutant would make it fail
        spec = self.spec([{"id": "poisons", "file": "calc.py", "old": "a + b", "new": "a - b", "test": "touch poison; python3 check.py"},
                          {"id": "next", "file": "calc.py", "old": "a + b", "new": "a * b", "test": "python3 check.py"}],
                         baseline=[{"test": "python3 check.py && test ! -f poison"}])
        result = self.run_tool(spec, "--baseline-every", "1")
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("THE BASELINE FAILED (after 1 mutants)", result.stderr)
        self.assertIn("poisons", result.stderr)                                                 # the verdicts that cannot be trusted are named
        self.assertNotIn("mutants: 2", result.stdout)
        self.assertNotIn("baselines of the unmutated tree", result.stdout)
        report = self.report()
        self.assertFalse(report["valid"])
        self.assertEqual([b["ok"] for b in report["baselines"]], [True, False])
        self.assertEqual([r["id"] for r in report["results"]], ["poisons"])                     # the second mutant was never run
        self.assertEqual(self.calc_text(), self.original)

    def test_a_baseline_that_fails_at_the_end_refuses_to_report(self):
        spec = self.spec([{"id": "poisons", "file": "calc.py", "old": "a + b", "new": "a - b", "test": "touch poison; python3 check.py"}],
                         baseline=[{"test": "python3 check.py && test ! -f poison"}])
        result = self.run_tool(spec, "--baseline-every", "0")
        self.assertEqual(result.returncode, 2)
        self.assertIn("THE BASELINE FAILED (end)", result.stderr)
        self.assertFalse(self.report()["valid"])
        self.assertEqual(self.calc_text(), self.original)

    def test_every_different_command_of_the_mutants_is_part_of_the_baseline(self):
        # command A passes on the unmutated tree and leaves poison behind only when it fails (on a mutant); command B passes only while there is no poison
        command_a = "python3 check.py; rc=$?; if [ $rc -ne 0 ]; then touch poison; fi; exit $rc"
        command_b = "test ! -f poison && python3 check.py"
        spec = self.spec([{"id": "a", "file": "calc.py", "old": "a + b", "new": "a - b", "test": command_a},
                          {"id": "b", "file": "calc.py", "old": "x * 2", "new": "x * 3", "test": command_b}])
        result = self.run_tool(spec, "--baseline-every", "0")
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("THE BASELINE FAILED (end)", result.stderr)                                # the second command found the poison that the first mutant left
        self.assertIn("test ! -f poison", result.stderr)
        self.assertEqual(self.calc_text(), self.original)

    def test_the_baseline_can_be_declared_and_is_not_the_mutants_command(self):
        spec = self.spec([{"id": "m", "file": "calc.py", "old": "a + b", "new": "a - b", "test": "echo mutant >> who.log; python3 check.py"}],
                         baseline=[{"test": "echo baseline >> who.log"}])
        self.assertEqual(self.run_tool(spec).returncode, 0)
        self.assertEqual(read(os.path.join(self.root, "who.log")).split(), ["baseline", "mutant", "baseline"])

    def test_a_declared_baseline_must_have_a_test(self):
        spec = self.spec([{"id": "m", "file": "calc.py", "old": "a + b", "new": "a - b", "test": "true"}], baseline={"build": ["x"]})
        result = self.run_tool(spec)
        self.assertEqual(result.returncode, 2)
        self.assertIn("baseline", result.stderr)

    def interrupted(self, sig):
        """Start the tool on a test that sleeps, wait until the file is changed, send the signal; returns (exit status, calc.py after, backup left?)."""
        spec = self.spec([{"id": "m", "file": "calc.py", "old": "a + b", "new": "a - b", "test": "sleep 60"}], baseline=self.TRUE)
        proc = subprocess.Popen([sys.executable, TOOL, spec, "--root", self.root], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                env=dict(os.environ, PYTHONDONTWRITEBYTECODE="1"))
        deadline = time.time() + 20
        while time.time() < deadline and "return a - b" not in self.calc_text():
            time.sleep(0.05)
        self.assertIn("return a - b", self.calc_text(), "the file was never changed")
        proc.send_signal(sig)
        proc.communicate(timeout=20)
        return proc.returncode, self.calc_text(), os.path.exists(os.path.join(self.root, "calc.py.mutbak"))

    @unittest.skipUnless(os.name == "posix", "signals")
    def test_ctrl_c_puts_the_file_back(self):
        code, text, backup = self.interrupted(signal.SIGINT)
        self.assertEqual(code, 130)
        self.assertEqual(text, self.original)
        self.assertFalse(backup)

    @unittest.skipUnless(os.name == "posix", "signals")
    def test_sigterm_puts_the_file_back(self):
        code, text, backup = self.interrupted(signal.SIGTERM)
        self.assertEqual(code, 130)
        self.assertEqual(text, self.original)
        self.assertFalse(backup)

    def test_a_backup_that_a_killed_run_left_is_put_back_at_the_next_start(self):
        calc = os.path.join(self.root, "calc.py")
        write(calc + ".mutbak", self.original)                                                  # what a run killed with SIGKILL leaves ...
        write(calc, self.original.replace("a + b", "a - b"))                                     # ... next to the changed file
        spec = self.spec([{"id": "m", "file": "calc.py", "old": "a + b", "new": "a * b", "test": "python3 check.py"}])
        result = self.run_tool(spec)
        self.assertIn("restored calc.py", result.stdout)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)                  # the old text exists again, the mutant applied and was caught
        self.assertEqual(self.calc_text(), self.original)
        self.assertFalse(os.path.exists(calc + ".mutbak"))

    def test_the_restored_file_is_newer_than_anything_built_from_the_mutant(self):
        calc = os.path.join(self.root, "calc.py")
        before = os.stat(calc).st_mtime
        spec = self.spec([{"id": "m", "file": "calc.py", "old": "a + b", "new": "a - b", "test": "python3 check.py"}])
        self.run_tool(spec)
        self.assertGreater(os.stat(calc).st_mtime, before + 1.0)

    def test_the_json_report_is_written_after_every_mutant(self):
        spec = self.spec([{"id": "first", "file": "calc.py", "old": "a + b", "new": "a - b", "test": "python3 check.py"},
                          {"id": "second", "file": "calc.py", "old": "a + b", "new": "a * b", "test": "touch started.second; sleep 60"}], baseline=self.TRUE)
        proc = subprocess.Popen([sys.executable, TOOL, spec, "--root", self.root], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                env=dict(os.environ, PYTHONDONTWRITEBYTECODE="1"))
        deadline = time.time() + 20
        while time.time() < deadline and not os.path.exists(os.path.join(self.root, "started.second")):
            time.sleep(0.05)
        partial = self.report()
        proc.send_signal(signal.SIGINT)
        proc.communicate(timeout=20)
        self.assertEqual([r["id"] for r in partial["results"]], ["first"])                      # the first mutant's verdict was already on disk
        self.assertEqual(partial["results"][0]["status"], "caught")


@unittest.skipUnless(CMAKE and COMPILER, "cmake and a C++ compiler are needed")
class WithABuildStep(unittest.TestCase):
    """The build step: a tiny CMake project (one header, one program that checks it)."""

    HEADER = "#pragma once\ninline int twice(int x) { return x * 2; }\ninline int unused(int x) { return x + 100; }\n"
    MAIN = '#include "calc.hpp"\n#include <cstdio>\nint main() { if (twice(21) != 42) { std::puts("FAIL twice"); return 1; } std::puts("ok"); return 0; }\n'
    LISTS = "cmake_minimum_required(VERSION 3.16)\nproject(tiny LANGUAGES CXX)\nadd_executable(tiny main.cpp)\n"

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = self.tmp.name
        write(os.path.join(self.root, "CMakeLists.txt"), self.LISTS)
        write(os.path.join(self.root, "calc.hpp"), self.HEADER)
        write(os.path.join(self.root, "main.cpp"), self.MAIN)
        configured = subprocess.run([CMAKE, "-S", self.root, "-B", os.path.join(self.root, "build"), "-DCMAKE_BUILD_TYPE=Release"], capture_output=True, text=True)
        self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
        built = subprocess.run([CMAKE, "--build", os.path.join(self.root, "build")], capture_output=True, text=True)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)

    def tearDown(self):
        self.tmp.cleanup()

    def binary(self):
        return subprocess.run([os.path.join(self.root, "build", "tiny")], capture_output=True, text=True)

    def test_build_and_test_decide_caught_survived_and_build_error_and_the_tree_is_built_again_at_the_end(self):
        spec = os.path.join(self.root, "spec.json")
        test = "./build/tiny"
        write(spec, json.dumps({"defaults": {"build": ["tiny"], "test": test, "file": "calc.hpp"}, "mutants": [
            {"id": "twice-is-thrice", "old": "x * 2", "new": "x * 3", "expect": "FAIL twice"},
            {"id": "unused-changed", "old": "x + 100", "new": "x + 101"},
            {"id": "does-not-compile", "old": "x * 2", "new": "x * ;"},
        ]}))
        result = subprocess.run([sys.executable, TOOL, spec, "--root", self.root, "--keep-going", "--jobs", "2"], capture_output=True, text=True, timeout=300)
        report = json.loads(read(os.path.join(self.root, "spec.report.json")))
        by = {r["id"]: r for r in report["results"]}
        self.assertEqual(by["twice-is-thrice"]["status"], "caught", result.stdout)
        self.assertEqual(by["unused-changed"]["status"], "survived", result.stdout)
        self.assertEqual(by["does-not-compile"]["status"], "build-error", result.stdout)
        self.assertEqual(result.returncode, 1)                                                  # a survivor outranks an invalid mutant
        self.assertTrue(report["valid"])
        self.assertEqual([b["ok"] for b in report["baselines"]], [True, True])               # before the first mutant and after the last, on the real code
        self.assertEqual(read(os.path.join(self.root, "calc.hpp")), self.HEADER)
        again = self.binary()
        self.assertEqual((again.returncode, again.stdout.strip()), (0, "ok"), "the build folder holds a mutant: the unmutated tree was not built again")


if __name__ == "__main__":
    unittest.main()
