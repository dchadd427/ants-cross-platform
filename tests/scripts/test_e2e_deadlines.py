#!/usr/bin/env python3
"""Tests of tests/scripts/e2e_deadlines.sh, the deadlines of the waits in tests/scripts/test_ants_server.sh (run by ./run_tests.sh --fast and by the CI).

A "Script suites" step of the macOS job once hung for half an hour (a normal run takes three minutes) until the job was cancelled, with no log of where: some wait of the server end-to-end
script had no deadline. Every wait for a process has one now, and a hang is a failure that names what was waited for. Three groups of tests, each running the helpers in a bash of their own:

  - the waits: a child that ends is waited for as before (its status comes back); one that ignores SIGTERM, one that was stopped with SIGSTOP and one that never ends are each reported with
    a line that names them, are killed, and the wait returns after its deadline and not before it
  - the watchdog of a part: a script that blocks in the foreground, or in `wait`, is ended with status 1 and a line that names the programs that were still running
  - the script itself: no bare `wait` is left in it (every wait goes through the helpers), the servers that it runs in a pipeline have an alarm, and the checks of the tick rate use the
    measure of tests/scripts/measure_tick_rate.py and not two readings over a nominal 3 s
"""
import os
import re
import subprocess
import sys
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
HELPERS = os.path.join(HERE, "e2e_deadlines.sh")
SCRIPT = os.path.join(HERE, "test_ants_server.sh")


def run_bash(body, timeout=60):
    """Runs `body` in a bash that has sourced the helpers (PART_LABEL ' [t]', the counters at 0); returns (status, stdout, stderr, seconds)."""
    program = 'PART_LABEL=" [t]"; FAILS=0; CHECKS=7; STOP_WAIT=1; WATCHDOG_GRACE=1\n. "%s"\n%s\n' % (HELPERS, body)
    started = time.monotonic()
    done = subprocess.run(["bash", "-c", program], capture_output=True, text=True, timeout=timeout)
    return done.returncode, done.stdout, done.stderr, time.monotonic() - started


class Waits(unittest.TestCase):
    def test_a_child_that_ends_gives_its_status_and_is_no_hang(self):
        status, out, err, seconds = run_bash('(exit 3) & wait_end $! 5 "a child that ends"; echo "status=$? hangs=$HANGS fails=$FAILS"')
        self.assertEqual(out.strip(), "status=3 hangs=0 fails=0")
        self.assertEqual(err, "")
        self.assertLess(seconds, 3)

    def test_a_child_that_ends_after_a_while_is_waited_for(self):
        status, out, err, seconds = run_bash('(sleep 1; exit 0) & wait_end $! 10 "a child"; echo "status=$? hangs=$HANGS"')
        self.assertEqual(out.strip(), "status=0 hangs=0")
        self.assertGreaterEqual(seconds, 0.9)
        self.assertLess(seconds, 5)

    def test_a_child_that_ignores_sigterm_is_a_hang_that_names_it_and_is_killed(self):
        status, out, err, seconds = run_bash('(trap "" TERM; while :; do sleep 0.2; done) & pid=$!; wait_end $pid 1 "ants_server (stopped with SIGTERM)"; '
                                             'echo "status=$? hangs=$HANGS fails=$FAILS"; kill -0 $pid 2>/dev/null && echo ALIVE || echo DEAD')
        self.assertEqual(out.split(), ["status=124", "hangs=1", "fails=1", "DEAD"])
        self.assertIn("FAIL: [t] HUNG: ants_server (stopped with SIGTERM) was still running 1 s after it was told to end (it was killed)", err)
        self.assertNotIn("Killed", err)                     # (the shell's own notice of the death would be a second line for the same hang)
        self.assertGreaterEqual(seconds, 1.0)
        self.assertLess(seconds, 12)

    def test_the_line_names_the_lines_of_the_script_that_waited(self):
        import tempfile
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "caller.sh")
            with open(path, "w", encoding="utf-8") as f:
                f.write('PART_LABEL=" [t]"; FAILS=0; STOP_WAIT=1\n'
                        '. "%s"\n'
                        'stop_here() { sleep 100 & wait_end $! 1 "a program"; }\n'       # line 3: where it is defined and waits
                        'echo before\n'
                        'stop_here\n' % HELPERS)                                           # line 5: where it is called
            done = subprocess.run(["bash", path], capture_output=True, text=True, timeout=60)
        self.assertIn("HUNG: a program was still running 1 s after it was told to end (it was killed) [called from caller.sh:3 caller.sh:5]", done.stderr)

    def test_a_child_that_was_stopped_is_continued_and_killed(self):
        status, out, err, seconds = run_bash('sleep 100 & pid=$!; kill -STOP $pid; wait_end $pid 1 "a stopped game"; echo "status=$?"; kill -0 $pid 2>/dev/null && echo ALIVE || echo DEAD')
        self.assertEqual(out.split(), ["status=124", "DEAD"])
        self.assertIn("HUNG: a stopped game", err)

    def test_the_hang_of_a_function_in_a_substitution_is_heard_on_stderr_and_its_status_is_not_zero(self):
        status, out, err, seconds = run_bash('f() { sleep 100 & wait_end $! 1 "a program in a substitution"; echo $?; }; r="$(f)"; echo "got=$r"')
        self.assertEqual(out.strip(), "got=124")
        self.assertIn("HUNG: a program in a substitution", err)

    def test_stop_pids_ends_the_ones_that_go_and_names_the_ones_that_do_not(self):
        body = ('sleep 100 & good=$!\n'
                'ready="$(mktemp -u)"\n'
                '(trap "" TERM; : > "$ready"; while :; do sleep 0.2; done) & stubborn=$!\n'
                'while [ ! -e "$ready" ]; do sleep 0.05; done; rm -f "$ready"\n'          # (SIGTERM must find the trap in place: a child that has not set it yet would die of it)
                'sleep 100 & frozen=$!; kill -STOP $frozen\n'
                'stop_pids "the clients of the room" $good $stubborn $frozen\n'
                'echo "hangs=$HANGS"\n'
                'for p in $good $stubborn $frozen; do kill -0 $p 2>/dev/null && echo ALIVE || echo DEAD; done')
        status, out, err, seconds = run_bash(body)
        # the one that was stopped is continued first, so it ends at SIGTERM like the good one: only the one that ignores the signal is a hang
        self.assertEqual(out.split(), ["hangs=1", "DEAD", "DEAD", "DEAD"])
        self.assertEqual(err.count("HUNG:"), 1)
        self.assertNotIn("Killed", err)
        self.assertIn("the clients of the room (process ", err)
        self.assertLess(seconds, 15)

    def test_stop_pids_with_nothing_running_returns_at_once(self):
        status, out, err, seconds = run_bash('(exit 0) & a=$!; (exit 0) & b=$!; sleep 0.3; stop_pids "gone" $a $b; echo "hangs=$HANGS"')
        self.assertEqual(out.strip(), "hangs=0")
        self.assertLess(seconds, 3)

    def test_run_limited_gives_the_status_and_the_output_of_a_program_that_ends(self):
        status, out, err, seconds = run_bash('f="$(mktemp)"; run_limited 10 "a checker" "$f" bash -c "echo hello; echo oops >&2; exit 4"; echo "status=$?"; cat "$f"; rm -f "$f"')
        self.assertEqual(out.split(), ["status=4", "hello", "oops"])

    def test_run_limited_ends_a_program_that_never_does(self):
        status, out, err, seconds = run_bash('f="$(mktemp)"; run_limited 1 "a checker that never answers" "$f" bash -c "echo started; sleep 100"; echo "status=$?"; cat "$f"; rm -f "$f"')
        self.assertEqual(out.split(), ["status=124", "started"])
        self.assertIn("HUNG: a checker that never answers was still running 1 s", err)
        self.assertLess(seconds, 12)


class Watchdog(unittest.TestCase):
    def test_a_part_that_blocks_in_the_foreground_is_ended_with_status_1_and_a_line_that_names_what_ran(self):
        status, out, err, seconds = run_bash('start_watchdog 1; sleep 101 & sleep 102', timeout=30)
        self.assertEqual(status, 1)
        self.assertIn("HUNG: the part has not ended after 1 s", err)
        self.assertIn("sleep 101", err)
        self.assertIn("server e2e [t]: 7 checks, 1 failures", out)
        self.assertGreaterEqual(seconds, 1.0)
        self.assertLess(seconds, 15)

    def test_a_part_that_blocks_in_wait_is_ended_at_once(self):
        status, out, err, seconds = run_bash('start_watchdog 1; sleep 103 & wait', timeout=30)
        self.assertEqual(status, 1)
        self.assertIn("HUNG: the part has not ended after 1 s", err)
        self.assertLess(seconds, 6)

    def test_a_part_that_ends_in_time_is_left_alone_and_the_watchdog_goes_with_it(self):
        # (the watchdog has a limit of 1 s; the part stops it at once, and nothing of it is left to fire later, nor its sleep)
        body = ('start_watchdog 1; wd=$WATCHDOG_PID; echo done; stop_watchdog; sleep 3; echo still-here; '
                'kill -0 $wd 2>/dev/null && echo WATCHDOG-ALIVE || echo watchdog-gone; echo "pid=[$WATCHDOG_PID]"; '
                'jobs -p | wc -l')
        status, out, err, seconds = run_bash(body, timeout=30)
        self.assertEqual(status, 0)
        self.assertEqual(out.split(), ["done", "still-here", "watchdog-gone", "pid=[]", "0"])
        self.assertEqual(err, "")

    def test_a_part_that_ends_in_time_by_exit_leaves_no_sleep_behind(self):
        # (a script that ends with its watchdog's sleep alive would leave a process for minutes; on exit the part stops the watchdog in its EXIT trap, as test_ants_server.sh does)
        token = "917%d" % os.getpid()
        body = 'trap stop_watchdog EXIT; PART_LABEL=" [t]"; start_watchdog %s; exit 0' % token
        status, out, err, seconds = run_bash(body, timeout=30)
        time.sleep(0.5)
        left = subprocess.run(["ps", "-eo", "args"], capture_output=True, text=True).stdout
        self.assertEqual(status, 0)
        self.assertNotIn("sleep %s" % token, left)


class TheScript(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(SCRIPT, encoding="utf-8") as f:
            cls.text = f.read()
        cls.code = [line for line in cls.text.split("\n") if not line.lstrip().startswith("#")]

    def test_it_loads_the_helpers_and_starts_its_watchdog(self):
        self.assertIn('. "$ROOT/tests/scripts/e2e_deadlines.sh"', self.text)
        self.assertRegex(self.text, r"start_watchdog \$\(\(\d+ \* TIME_SCALE\)\)")

    def test_no_wait_is_left_that_has_no_deadline(self):
        bare = [line.strip() for line in self.code if re.search(r'(^|[;&|]\s*|\bdo\s+|\bthen\s+)wait(\s|$)', line)]
        self.assertEqual(bare, [], "a bare `wait` hangs for ever when its program does: use wait_end, stop_pids or run_limited (tests/scripts/e2e_deadlines.sh)")

    def test_a_server_that_runs_in_a_pipeline_has_an_alarm(self):
        piped = [line.strip()[:100] for line in self.code if re.search(r'"\$SERVER" [^|]*\|\s*grep', line) and "--help" not in line and "alarm" not in line]
        self.assertEqual(piped, [], "a server that does not stop by itself would hang the pipeline: perl -e 'alarm 5; exec @ARGV'")

    def test_the_rate_checks_use_the_measure_and_not_two_readings_over_a_nominal_time(self):
        self.assertGreaterEqual(len(re.findall(r'tick_rate "\$\w+"', self.text)), 3)
        self.assertNotRegex(self.text, r"\) / 3\.0\)")
        self.assertNotRegex(self.text, r"rate_of_victim")
        self.assertNotRegex(self.text, r"17\.0 <= r <= 23\.0")


if __name__ == "__main__":
    unittest.main()
