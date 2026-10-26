#!/usr/bin/env python3
"""Tests of tests/scripts/e2e_deadlines.sh, the deadlines of the waits in tests/scripts/test_ants_server.sh (run by ./run_tests.sh --fast and by the CI).

A "Script suites" step of the macOS job once hung for half an hour (a normal run takes three minutes) until the job was cancelled, with no log of where: some wait of the server end-to-end
script had no deadline. Every wait for a process has one now, and a hang is a failure that names what was waited for. Three groups of tests, each running the helpers in a bash of their own:

  - the waits: a child that ends is waited for as before (its status comes back); one that ignores SIGTERM, one that was stopped with SIGSTOP and one that never ends are each reported with
    a line that names them, are killed, and the wait returns after its deadline and not before it
  - the watchdog of a part: a script that blocks in the foreground, or in `wait`, is ended with status 1 and a line that names the programs that were still running
  - the count: a hang that is reported by a function that runs inside a `$( )` (stop_server in a check's substitution, db_start) is counted, and so changes the exit status of the part: the real script, run
    with a fake server that ignores SIGTERM, ends with as many failures as it printed FAIL lines
  - the script itself: no bare `wait` is left in it (every wait goes through the helpers), the servers that it runs in a pipeline have an alarm, and the checks of the tick rate use the
    measure of tests/scripts/measure_tick_rate.py and not two readings over a nominal 3 s
"""
import collections
import os
import re
import shutil
import signal
import subprocess
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
HELPERS = os.path.join(HERE, "e2e_deadlines.sh")
SCRIPT = os.path.join(HERE, "test_ants_server.sh")


def run_bash(body, timeout=60):
    """Runs `body` in a bash that has sourced the helpers (PART_LABEL ' [t]', the counters at 0); returns (status, stdout, stderr, seconds)."""
    program = 'PART_LABEL=" [t]"; FAILS=0; CHECKS=7; STOP_WAIT=1; WATCHDOG_GRACE=1\n. "%s"\n%s\n' % (HELPERS, body)
    started = time.monotonic()
    with tempfile.TemporaryDirectory() as folder:       # (the watchdog's flag file goes here: a part that is ended by the watchdog does not clean up as the script does)
        done = subprocess.run(["bash", "-c", program], capture_output=True, text=True, timeout=timeout, env=dict(os.environ, TMPDIR=folder))
    return done.returncode, done.stdout, done.stderr, time.monotonic() - started


class Waits(unittest.TestCase):
    def test_a_child_that_ends_gives_its_status_and_is_no_hang(self):
        status, out, err, seconds = run_bash('(exit 3) & wait_end $! 5 "a child that ends"; echo "status=$? hangs=$(hang_count) fails=$FAILS"')
        self.assertEqual(out.strip(), "status=3 hangs=0 fails=0")
        self.assertEqual(err, "")
        self.assertLess(seconds, 3)

    def test_a_child_that_ends_after_a_while_is_waited_for(self):
        status, out, err, seconds = run_bash('(sleep 1; exit 0) & wait_end $! 10 "a child"; echo "status=$? hangs=$(hang_count)"')
        self.assertEqual(out.strip(), "status=0 hangs=0")
        self.assertGreaterEqual(seconds, 0.9)
        self.assertLess(seconds, 5)

    def test_a_child_that_ignores_sigterm_is_a_hang_that_names_it_and_is_killed(self):
        status, out, err, seconds = run_bash('(trap "" TERM; while :; do sleep 0.2; done) & pid=$!; wait_end $pid 1 "ants_server (stopped with SIGTERM)"; '
                                             'echo "status=$? hangs=$(hang_count) fails=$FAILS"; kill -0 $pid 2>/dev/null && echo ALIVE || echo DEAD')
        self.assertEqual(out.split(), ["status=124", "hangs=1", "fails=0", "DEAD"])      # (a hang is counted in hang_count, the part adds it to its failures when it ends)
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

    def test_the_hang_of_a_function_in_a_substitution_is_heard_on_stderr_and_counted(self):
        # (a function inside $( ) is a subshell: a counter that it changes is lost with it, which is why the count is a file)
        status, out, err, seconds = run_bash('HANG_LOG="$(mktemp)"; f() { sleep 100 & wait_end $! 1 "a program in a substitution"; echo $?; }; r="$(f)"; '
                                             'echo "got=$r hangs=$(hang_count) fails=$FAILS"; rm -f "$HANG_LOG"')
        self.assertEqual(out.strip(), "got=124 hangs=1 fails=0")
        self.assertIn("HUNG: a program in a substitution", err)

    def test_without_the_file_the_count_of_a_substitution_is_lost_which_is_the_bug_that_the_file_mends(self):
        status, out, err, seconds = run_bash('HANG_LOG=""; f() { sleep 100 & wait_end $! 1 "a program in a substitution"; echo $?; }; r="$(f)"; echo "got=$r hangs=$(hang_count)"')
        self.assertEqual(out.strip(), "got=124 hangs=0")

    def test_a_failure_that_is_no_check_is_counted_too(self):
        status, out, err, seconds = run_bash('HANG_LOG="$(mktemp)"; r="$(fail_note "db_start: a server did not log its game port"; echo 1)"; echo "got=$r hangs=$(hang_count)"; rm -f "$HANG_LOG"')
        self.assertEqual(out.strip(), "got=1 hangs=1")
        self.assertIn("FAIL: [t] db_start: a server did not log its game port", err)

    def test_a_part_whose_check_holds_while_a_program_in_its_substitution_hangs_ends_with_a_failure(self):
        # the shape of `check "..." "$(start_nosecret ...; r=$?; stop_server; ...; echo $?)"` in the secret part: the condition holds, the stop hangs
        check = ('check() { CHECKS=$((CHECKS + 1)); if [ "$2" -ne 0 ]; then FAILS=$((FAILS + 1)); echo "  FAIL: $1"; fi; }\n'
                 'check "a check whose condition holds" "$(sleep 100 & wait_end $! 1 "a program in a substitution"; echo 0)"\n'
                 'FAILS=$((FAILS + $(hang_count)))\n'
                 'echo "$CHECKS checks, $FAILS failures"\n'
                 '[ "$FAILS" -eq 0 ]')
        status, out, err, seconds = run_bash('HANG_LOG="$(mktemp)"\n' + check)
        self.assertEqual((status, out.strip()), (1, "8 checks, 1 failures"))      # (run_bash starts the counters at 7 checks and 0 failures)
        status, out, err, seconds = run_bash('HANG_LOG=""\n' + check)         # (the count that was a variable: the hang is printed and the part ends with status 0)
        self.assertEqual((status, out.strip()), (0, "8 checks, 0 failures"))

    def test_stop_pids_ends_the_ones_that_go_and_names_the_ones_that_do_not(self):
        body = ('sleep 100 & good=$!\n'
                'ready="$(mktemp -u)"\n'
                '(trap "" TERM; : > "$ready"; while :; do sleep 0.2; done) & stubborn=$!\n'
                'while [ ! -e "$ready" ]; do sleep 0.05; done; rm -f "$ready"\n'          # (SIGTERM must find the trap in place: a child that has not set it yet would die of it)
                'sleep 100 & frozen=$!; kill -STOP $frozen\n'
                'stop_pids "the clients of the room" $good $stubborn $frozen\n'
                'echo "hangs=$(hang_count)"\n'
                'for p in $good $stubborn $frozen; do kill -0 $p 2>/dev/null && echo ALIVE || echo DEAD; done')
        status, out, err, seconds = run_bash(body)
        # the one that was stopped is continued first, so it ends at SIGTERM like the good one: only the one that ignores the signal is a hang
        self.assertEqual(out.split(), ["hangs=1", "DEAD", "DEAD", "DEAD"])
        self.assertEqual(err.count("HUNG:"), 1)
        self.assertNotIn("Killed", err)
        self.assertIn("the clients of the room (process ", err)
        self.assertLess(seconds, 15)

    def test_stop_pids_with_nothing_running_returns_at_once(self):
        status, out, err, seconds = run_bash('(exit 0) & a=$!; (exit 0) & b=$!; sleep 0.3; stop_pids "gone" $a $b; echo "hangs=$(hang_count)"')
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


# The watchdog as it was when macOS CI showed "Terminated: 15 ( s=\"\"; trap ... )" on the script's stderr: a subshell that stop_watchdog KILLED (it had not set up its own handler yet when the part ended at once)
OLD_WATCHDOG = r'''
start_watchdog() { WATCHDOG_LIMIT="$1"; trap on_watchdog USR2; ( s=""; trap 'kill $s 2> /dev/null; exit 0' TERM; sleep "$1" & s=$!; wait $s; kill -USR2 $$; sleep "$WATCHDOG_GRACE" & s=$!; wait $s; pkill -KILL -P $$ ) > /dev/null 2>&1 & WATCHDOG_PID=$!; }
stop_watchdog() { [ -n "$WATCHDOG_PID" ] && kill "$WATCHDOG_PID" 2> /dev/null; WATCHDOG_PID=""; }
'''


def in_time_problems(helpers, rounds=40):
    """Runs the part that ends in time `rounds` times at once (the race of the macOS failure: the watchdog is stopped before it has done anything) with the helpers in `helpers` (after them `extra`
    may redefine functions); returns what is wrong: anything on stderr, a kill sent to a watchdog (the job of a bash that is killed prints a notice on the Mac's bash 3.2, which Linux's bash 5 does not for
    SIGTERM, so the signal itself is what is looked for), a watchdog that is a job of the shell (a job that is in its table can be reported), one that is still there 3 s after, a flag file left behind."""
    with tempfile.TemporaryDirectory() as folder:
        log = os.path.join(folder, "kills")
        pids = os.path.join(folder, "pids")
        script = os.path.join(folder, "part.sh")
        with open(script, "w", encoding="utf-8") as f:
            f.write('PART_LABEL=" [t]"; FAILS=0; CHECKS=0; WATCHDOG_GRACE=1; export TMPDIR="%s"\n'
                    'kill() { echo "$*" >> "%s"; builtin kill "$@"; }\n'
                    '. "%s"\n'
                    '%s\n'
                    'for i in $(seq 1 %d); do start_watchdog 30; echo "$WATCHDOG_PID" >> "%s"; stop_watchdog; done\n'
                    'echo "jobs=[$(jobs -p | tr "\\n" " ")]"\n'
                    'sleep 3\n'
                    'n=0; for p in $(cat "%s"); do kill -0 $p 2>/dev/null && n=$((n + 1)); done; echo "alive=$n"\n' % (folder, log, helpers, OLD_WATCHDOG if helpers.endswith("old") else "", rounds, pids, pids))
        done = subprocess.run(["bash", script], capture_output=True, text=True, timeout=120)
        with open(pids, encoding="utf-8") as f:
            watchdogs = set(f.read().split())
        killed = []
        if os.path.exists(log):
            with open(log, encoding="utf-8") as f:
                killed = [line for line in f.read().split("\n") if line.split()[-1:] and line.split()[-1] in watchdogs and not line.startswith("-0 ")]       # (a `kill -0` only looks)
        leftovers = [name for name in os.listdir(folder) if name.startswith("ants_watchdog")]
    problems = []
    if done.stderr.strip():
        problems.append("stderr: %s" % done.stderr.strip()[:200])
    if killed:
        problems.append("%d kills sent to a watchdog" % len(killed))
    if "jobs=[]" not in done.stdout:
        problems.append("a watchdog is a job of the shell: %s" % re.search(r"jobs=\[[^\]]*\]", done.stdout).group(0))
    if "alive=0" not in done.stdout:
        problems.append("a watchdog is still there 3 s after: %s" % re.search(r"alive=\d+", done.stdout).group(0))
    if leftovers:
        problems.append("flag files left: %s" % leftovers)
    return problems


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
        # (the watchdog has a limit of 1 s; the part stops it at once; it fires nothing later and is gone by itself)
        body = ('start_watchdog 1; wd=$WATCHDOG_PID; echo done; stop_watchdog; sleep 3; echo still-here; '
                'kill -0 $wd 2>/dev/null && echo WATCHDOG-ALIVE || echo watchdog-gone; echo "pid=[$WATCHDOG_PID]"; '
                'jobs -p | wc -l')
        status, out, err, seconds = run_bash(body, timeout=30)
        self.assertEqual(status, 0)
        self.assertEqual(out.split(), ["done", "still-here", "watchdog-gone", "pid=[]", "0"])
        self.assertEqual(err, "")

    def test_a_part_that_ends_in_time_forty_times_at_once_leaves_nothing_and_says_nothing(self):
        self.assertEqual(in_time_problems(HELPERS), [])

    def test_the_old_watchdog_that_was_killed_is_seen_by_that_test(self):
        # (the same test on the helpers with the old start and stop: a kill is sent to every one of the 40 watchdogs. Linux's bash 5 says nothing of a job that a SIGTERM killed, so this is what shows on Linux what the
        # Mac's bash 3.2 printed)
        with tempfile.TemporaryDirectory() as folder:
            old = os.path.join(folder, "e2e_deadlines.sh.old")
            with open(HELPERS, encoding="utf-8") as f:
                text = f.read()
            with open(old, "w", encoding="utf-8") as f:
                f.write(text)
            problems = in_time_problems(old)
        self.assertIn("40 kills sent to a watchdog", problems)

    def test_a_part_that_exits_stops_its_watchdog_and_no_flag_file_stays(self):
        with tempfile.TemporaryDirectory() as folder:
            marker = "marker%d" % os.getpid()
            body = 'export TMPDIR="%s"; trap stop_watchdog EXIT; PART_LABEL=" [t]"; start_watchdog 30; : %s; exit 0' % (folder, marker)
            status, out, err, seconds = run_bash(body, timeout=30)
            time.sleep(1.5)
            left = subprocess.run(["ps", "-eo", "args"], capture_output=True, text=True).stdout
            files = os.listdir(folder)
        self.assertEqual(status, 0)
        self.assertEqual(err, "")
        self.assertNotIn(marker, left)
        self.assertEqual(files, [])


# A fake ants_server: the few things that the secret part of test_ants_server.sh asks of one. It answers /healthz on --ctl-port, makes the secret file that it is told to, refuses (status 2) what the part asks it to refuse,
# and the two servers of the part that are stopped inside a `$( )` (their folder is called "elsewhere") ignore SIGTERM: those stops hang. It never asks for the name of the machine (HTTPServer.server_bind does, with
# socket.getfqdn(): a reverse lookup that took seconds a start on the macOS runner of the CI), and it writes a line to the file that FAKE_SERVER_LOG names for each start, each answer and each end, so that the report
# of a run that failed or took long shows when every server of the part was up.
FAKE_SERVER = r"""#!/usr/bin/env python3
import time
started = time.time()
import http.server, os, signal, socket, sys
args = sys.argv[1:]
text = " ".join(args)
def note(what):
    if os.environ.get("FAKE_SERVER_LOG"):
        with open(os.environ["FAKE_SERVER_LOG"], "a") as f:
            f.write("%.3f %d %s\n" % (time.time(), os.getpid(), what))
def leave(code, why):
    note("exit %d: %s" % (code, why))
    sys.exit(code)
def getfqdn(*unused):
    note("getfqdn was called")
    raise RuntimeError("the fake server must not ask for the name of the machine")
socket.getfqdn = getfqdn
note("start " + text[:100])
if "--help" in args:
    print("fake ants_server")
    sys.exit(0)
if "--version" in args:
    print("fake ants_server 0 (network protocol 21)")
    sys.exit(0)
def value(name):
    return args[args.index(name) + 1] if name in args else ""
secret_file, results = value("--secret-file"), value("--results-dir")
if "dangling" in text or "NO-SUCH-MAP" in text:
    leave(2, "a secret file that is a link to nothing, or a map that is not there")
if secret_file:
    os.makedirs(os.path.dirname(secret_file), exist_ok=True)
    if not os.path.exists(secret_file):
        open(secret_file, "w").write("a" * 64 + "\n")
elif results:
    path = os.path.join(results, "control-secret")
    os.makedirs(results, exist_ok=True)
    if not os.path.exists(path):
        open(path, "w").write("a" * 64 + "\n")
    elif open(path).read().strip() != "a" * 64:
        leave(2, "a secret file that is no secret")
elif not os.environ.get("ANTS_SERVER_SECRET"):
    leave(2, "no secret and nowhere to keep one")
if "--ctl-port" not in args:
    leave(2, "no control port")
if "elsewhere" in text:
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    note("ignores SIGTERM")
class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        note("GET " + self.path)
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b'{"status":"ok"}')
    def log_message(self, *args):
        pass
sys.path.insert(0, "@QUICK_HTTP_DIR@")
import quick_http
server = quick_http.QuickThreadingHTTPServer(("127.0.0.1", int(value("--ctl-port"))), Handler)
note("up after %.2f s" % (time.time() - started))
server.serve_forever()
"""


def the_variable_only_count(helpers):
    """The helpers as they were before the count was a file: hang_note and hang_count keep it in a variable, which a function inside a `$( )` changes for itself only."""
    helpers = re.sub(r"hang_note\(\) \{.*?\n\}", 'hang_note() {\n    HANGS=$((HANGS + 1))\n}', helpers, count=1, flags=re.S)
    return re.sub(r"hang_count\(\) \{.*?\n\}", 'hang_count() {\n    echo "$HANGS"\n}', helpers, count=1, flags=re.S)


Run = collections.namedtuple("Run", "status failures fail_lines hung_lines report")


@unittest.skipUnless(shutil.which("curl") and shutil.which("perl"), "the script needs curl and perl")
class TheScriptCounts(unittest.TestCase):
    """The real script, its secret part, against a fake server that does not stop where the part stops it inside a `$( )`: every FAIL line is a failure of the part's count.

    A run takes 5 s on Linux. LIMIT is how long one run may take, 35 times that: a run that is longer is killed, with everything it started, and fails with a report, the output of the part and
    when each fake server was up. On the macOS runner of the CI a run took 255 s (limit: 300 s) when the fake server asked for the name of the machine before it listened (HTTPServer.server_bind);
    the polls of a start that wait for a port that is bound and does not listen yet may take their whole second there, and the part polls up to 50 times for each of its servers."""
    LIMIT = 180

    def report(self, text, fake_log, started, seconds, killed):
        servers = []
        for line in fake_log.split("\n"):
            if line:
                when, pid, what = line.split(" ", 2)
                servers.append((float(when), "%7.2f s  pid %s  %s" % (float(when) - started, pid, what)))
        servers.sort(key=lambda entry: entry[0])
        return "the part %s after %.1f s\n--- its output ---\n%s\n--- the fake servers (seconds after the start of the part) ---\n%s" % (
            "was killed" if killed else "ended", seconds, text.rstrip(), "\n".join(line for _, line in servers) or "(none started)")

    def run_part(self, edit=None):
        with tempfile.TemporaryDirectory() as root:
            scripts = os.path.join(root, "tests", "scripts")
            os.makedirs(scripts)
            shutil.copy(SCRIPT, scripts)
            with open(HELPERS, encoding="utf-8") as f:
                helpers = f.read()
            with open(os.path.join(scripts, "e2e_deadlines.sh"), "w", encoding="utf-8") as f:
                f.write(edit(helpers) if edit else helpers)
            for name, body in (("build/src/ants_server/ants_server", FAKE_SERVER), ("build/src/ants_app/ants", "#!/bin/sh\nexit 0\n")):
                path = os.path.join(root, name)
                os.makedirs(os.path.dirname(path))
                with open(path, "w", encoding="utf-8") as f:
                    f.write(body.replace("@QUICK_HTTP_DIR@", os.path.dirname(os.path.abspath(__file__))))
                os.chmod(path, 0o755)
            tmp = os.path.join(root, "tmp")
            os.makedirs(tmp)
            log = os.path.join(tmp, "fake_server.log")
            env = dict(os.environ, BUILD_DIR="build", SERVER_END_WAIT="1", KILL_GRACE="3", TMPDIR=tmp, FAKE_SERVER_LOG=log)
            started = time.time()
            # (a group of its own: at the limit the script and every fake server that it started are killed together; a fake server does not end by itself)
            part = subprocess.Popen(["bash", os.path.join(scripts, "test_ants_server.sh"), "--part", "secret"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env, start_new_session=True)
            killed = False
            try:
                text = part.communicate(timeout=self.LIMIT)[0]
            except subprocess.TimeoutExpired:
                killed = True
                os.killpg(part.pid, signal.SIGKILL)
                text = part.communicate()[0]
            seconds = time.time() - started
            fake_log = ""
            if os.path.exists(log):
                with open(log, encoding="utf-8") as f:
                    fake_log = f.read()
        report = self.report(text, fake_log, started, seconds, killed)
        self.assertFalse(killed, report)
        self.assertNotIn("getfqdn", fake_log, report)
        summary = re.search(r"server e2e \[secret\]: \d+ checks, (\d+) failures", text)
        self.assertIsNotNone(summary, report)
        fail_lines = [line for line in text.split("\n") if line.startswith("  FAIL:")]
        hung_lines = [line for line in fail_lines if "HUNG:" in line]
        return Run(part.returncode, int(summary.group(1)), len(fail_lines), len(hung_lines), report)

    def test_the_failures_of_the_part_are_its_fail_lines_also_those_of_a_stop_inside_a_substitution(self):
        run = self.run_part()
        self.assertGreaterEqual(run.hung_lines, 1, run.report)               # (two: the servers whose folder is "elsewhere", stopped inside the substitution of a check; on a machine so loaded that such a server is not up in 5 s, fewer)
        self.assertEqual(run.failures, run.fail_lines, run.report)
        self.assertEqual(run.status, 1, run.report)

    def test_with_the_count_in_a_variable_the_hangs_are_printed_and_lost(self):
        run = self.run_part(the_variable_only_count)
        self.assertGreaterEqual(run.hung_lines, 1, run.report)
        self.assertEqual(run.failures, run.fail_lines - run.hung_lines, run.report)  # (the old count: what the substitutions changed was lost with them; every hang of this part is in one)


class TheScript(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(SCRIPT, encoding="utf-8") as f:
            cls.text = f.read()
        cls.code = [line for line in cls.text.split("\n") if not line.lstrip().startswith("#")]

    def test_it_loads_the_helpers_and_starts_its_watchdog(self):
        self.assertIn('. "$ROOT/tests/scripts/e2e_deadlines.sh"', self.text)
        self.assertRegex(self.text, r"start_watchdog \$\(\(\d+ \* TIME_SCALE \* PARTS_RUN\)\)")      # (a limit for each part that the call runs)
        self.assertRegex(self.text, r'PARTS_RUN="\$\(echo \$\{PARTS:-\$PART_NAMES\} \| wc -w')

    def test_it_counts_the_hangs_of_functions_that_ran_inside_a_substitution(self):
        self.assertIn('HANG_LOG="$WORK/hangs"', self.text)
        self.assertLess(self.text.index('HANG_LOG="$WORK/hangs"'), self.text.index('. "$ROOT/tests/scripts/e2e_deadlines.sh"'))
        end = self.text.rindex('echo "server e2e${PART_LABEL}:')
        self.assertIn("FAILS=$((FAILS + $(hang_count)))", self.text[end - 200:end])

    def test_no_wait_is_left_that_has_no_deadline(self):
        bare = [line.strip() for line in self.code if re.search(r'(^|[;&|]\s*|\bdo\s+|\bthen\s+)wait(\s|$)', line)]
        self.assertEqual(bare, [], "a bare `wait` hangs for ever when its program does: use wait_end, stop_pids or run_limited (tests/scripts/e2e_deadlines.sh)")

    def test_a_server_that_runs_in_a_pipeline_has_an_alarm(self):
        piped = [line.strip()[:100] for line in self.code if re.search(r'"\$SERVER" [^|]*\|\s*grep', line) and "--help" not in line and "alarm" not in line]
        self.assertEqual(piped, [], "a server that does not stop by itself would hang the pipeline: perl -e 'alarm 5; exec @ARGV'")

    def test_the_ping_flood_asks_for_an_honest_burst_to_be_answered_whole_and_has_a_ceiling(self):
        self.assertIn("FLOOD_BURST=1000", self.text)                       # (the documented budget, written here)
        self.assertIn("FLOOD_RATE=1000", self.text)
        self.assertIn("HONEST=$((FLOOD_BURST * 9 / 10))", self.text)
        self.assertRegex(self.text, r'\[ "\$\{HONEST_PONGS:-0\}" -eq "\$HONEST" \]')                          # every pong of the honest burst, not a share of them
        self.assertRegex(self.text, r"PONG_CEILING=\$\(\(FLOOD_BURST \+ FLOOD_RATE \* \(\$\{FLOOD_SPAN_MS:-0\} \+ FLOOD_LAG_MS\) / 1000 \+ FLOOD_SLACK\)\)")       # the ceiling: the burst plus the refill of the MEASURED time (a fixed one fails on a slow runner)
        self.assertRegex(self.text, r'-le "\$PONG_CEILING"')                                                 # on both phases together
        self.assertNotRegex(self.text, r"FLOOD_RATE / 2")                                                   # the fixed half second is gone
        self.assertRegex(self.text, r"last_pong = time\.time\(\)")                                             # the span ends at the last Pong taken off the socket ...
        self.assertRegex(self.text, r"began = time\.time\(\)\s.*\n\s*sock\.sendall\(frame\(hello\)\)")       # ... and begins before the first byte, so it is never shorter than the server's refill
        self.assertRegex(self.text, r"FLOOD_LAG_MS=[1-3]\d\d\b")                                                 # (a few hundred milliseconds of stamping lag: not a margin that hides a raised budget)
        self.assertRegex(self.text, r"FLOOD_SLACK=\d\b")
        self.assertNotRegex(self.text, r"kMessageBurst = ")                                              # (not read from the source that the check is about)
        self.assertNotRegex(self.text, r"FLOOD_BURST \* 3 / 10")                                         # the lowered floor is gone

    def test_the_rate_checks_use_the_measure_and_not_two_readings_over_a_nominal_time(self):
        self.assertGreaterEqual(len(re.findall(r'tick_rate "\$\w+"', self.text)), 3)
        self.assertNotRegex(self.text, r"\) / 3\.0\)")
        self.assertNotRegex(self.text, r"rate_of_victim")


if __name__ == "__main__":
    unittest.main()
