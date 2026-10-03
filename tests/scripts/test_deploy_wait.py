#!/usr/bin/env python3
"""Tests of tools/deploy_wait.py, the wait of the deploy job for an idle game server (run by ./run_tests.sh --fast and by the CI).

A deploy recreates the server's container and ends every match that is running, so the deploy job polls the site's public GET /busy ({"matches": N, "players": M}) and goes on
when no match runs, after DEPLOY_MAX_WAIT_MINUTES whatever runs, or when the address does not answer for five minutes. Three groups of tests:

  - the decisions, with a scripted answer and a clock that only moves when the script sleeps (a three-hour wait takes no time here): idle at once, busy and then idle, busy for
    ever, an address that never answers, one that answers badly and then well, a grace that starts again after a good answer, a wait of zero, an interval longer than the wait
  - the reading of the answer, against a real HTTP server on this machine: only exactly two non-negative whole counts are an answer; a missing page, a web page, an extra key, a
    float, a boolean, a text, a long body, a slow server, a closed port and a redirect are all "does not answer" and never "idle"
  - the program as the workflow runs it (a subprocess, short times): the lines it writes, the exit status, a longest wait that is no number, a cap, a cancellation by SIGINT or SIGTERM,
    and that the address is never printed
  - the step of .github/workflows/ci.yml that calls it, run as the runner would run it (bash, with the environment that the step declares): the production branch polls the
    production address, the staging branch the staging address and, when that is not set, does not wait at all
"""
import http.server
import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import threading
import time
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TOOL = os.path.join(REPO, "tools", "deploy_wait.py")
sys.path.insert(0, os.path.join(REPO, "tools"))
import deploy_wait as dw     # noqa: E402


class FakeTime:
    """A clock that moves when the script sleeps (and by `poll_seconds` for every poll: a request takes time)."""

    def __init__(self, poll_seconds=0.0):
        self.now = 0.0
        self.slept = []
        self.poll_seconds = poll_seconds

    def clock(self):
        return self.now

    def sleep(self, seconds):
        self.slept.append(seconds)
        self.now += seconds


def idle(players=0):
    return (0, players), None


def busy(matches=1, players=2):
    return (matches, players), None


def down(why="connection refused"):
    return None, why


def run_wait(answers, interval=60.0, max_minutes=180.0, grace_minutes=5.0, poll_seconds=0.0):
    """Runs wait() against a list of answers (the last one repeats); returns (why, lines, fake time, polls)."""
    fake = FakeTime(poll_seconds)
    lines = []
    polls = []

    def poll():
        if len(polls) >= 1000:                                                              # (the longest legal run is 181 polls: a wait that has lost its way out must fail, not spin)
            raise AssertionError("the wait does not end")
        fake.now += fake.poll_seconds
        polls.append(fake.now)
        return answers[min(len(polls) - 1, len(answers) - 1)]

    why = dw.wait(poll, interval, max_minutes * 60.0, grace_minutes * 60.0, clock=fake.clock, sleep=fake.sleep, out=lines.append)
    return why, lines, fake, polls


class Decisions(unittest.TestCase):
    def test_an_idle_server_is_deployed_at_once_without_a_sleep(self):
        why, lines, fake, polls = run_wait([idle()])
        self.assertEqual(why, "idle")
        self.assertEqual(fake.slept, [])
        self.assertEqual(len(polls), 1)
        self.assertIn("the server is idle, deploying", lines[-1])

    def test_people_in_rooms_that_have_not_started_do_not_hold_the_deploy_back(self):
        why, lines, _, _ = run_wait([idle(players=3)])
        self.assertEqual(why, "idle")
        self.assertIn("3 people in rooms that have not started", lines[-1])
        self.assertIn("1 person in rooms", run_wait([idle(players=1)])[1][-1])

    def test_it_waits_while_matches_run_and_goes_on_the_moment_the_server_is_idle(self):
        why, lines, fake, polls = run_wait([busy(2, 6), busy(2, 6), busy(1, 2), idle()])
        self.assertEqual(why, "idle")
        self.assertEqual(fake.slept, [60.0, 60.0, 60.0])                                   # one poll a minute
        self.assertEqual(len(polls), 4)
        self.assertEqual(len(lines), 4)                                                    # one line for every poll
        self.assertIn("2 matches running with 6 people: waiting", lines[0])
        self.assertIn("1 match running with 2 people: waiting", lines[2])
        self.assertIn("the server is idle, deploying", lines[3])

    def test_a_server_that_never_gets_quiet_is_deployed_after_the_longest_wait_with_a_clear_line(self):
        why, lines, fake, polls = run_wait([busy(3, 9)], max_minutes=180)
        self.assertEqual(why, "maximum")
        self.assertEqual(len(polls), 181)                                                  # t = 0, 60, ... 10800
        self.assertAlmostEqual(fake.now, 180 * 60, delta=60)
        self.assertLessEqual(fake.now, 180 * 60)
        self.assertIn("3 matches still running with 9 people", lines[-1])
        self.assertIn("the longest wait of 180 minutes is over, DEPLOYING ANYWAY", lines[-1])
        self.assertEqual(sum("DEPLOYING ANYWAY" in l for l in lines), 1)

    def test_the_longest_wait_is_a_parameter(self):
        why, lines, fake, _ = run_wait([busy()], max_minutes=10)
        self.assertEqual(why, "maximum")
        self.assertAlmostEqual(fake.now, 600, delta=60)
        self.assertIn("longest wait of 10 minutes", lines[-1])

    def test_a_wait_of_zero_looks_once_and_never_sleeps(self):
        why, lines, fake, polls = run_wait([busy(2, 4)], max_minutes=0)
        self.assertEqual((why, fake.slept, len(polls)), ("maximum", [], 1))
        self.assertIn("2 matches still running with 4 people", lines[-1])
        self.assertEqual(run_wait([idle()], max_minutes=0)[0], "idle")                      # an idle server is idle whatever the wait

    def test_an_interval_longer_than_the_wait_looks_once(self):
        why, _, fake, polls = run_wait([busy()], interval=600, max_minutes=5)
        self.assertEqual((why, fake.slept, len(polls)), ("maximum", [], 1))

    def test_an_address_that_never_answers_is_deployed_after_the_grace_not_at_once(self):
        why, lines, fake, polls = run_wait([down("connection refused")], grace_minutes=5)
        self.assertEqual(why, "unreachable")
        self.assertEqual(len(polls), 6)                                                    # t = 0, 60, ... 300
        self.assertEqual(fake.now, 300)
        self.assertIn("has not answered properly for 5 min 00 s (connection refused)", lines[-1])
        self.assertIn("connection refused", lines[0])
        self.assertIn("0 min 00 s of 5 min 00 s", lines[0])

    def test_a_bad_answer_is_no_idle_server(self):
        for why_text in ("HTTP 404", "the answer is not JSON", "the answer is not the two counts", "the counts are not numbers", "timed out", "HTTP 503"):
            why, lines, _, polls = run_wait([down(why_text)], grace_minutes=2)
            self.assertEqual(why, "unreachable", why_text)
            self.assertEqual(len(polls), 3, why_text)
            self.assertIn(why_text, lines[-1])

    def test_the_grace_starts_again_after_a_good_answer(self):
        # down for 4 minutes, one busy answer, down for 4 minutes, idle: no point of the run is five minutes without an answer
        answers = [down()] * 5 + [busy()] + [down()] * 5 + [idle()]
        why, _, fake, polls = run_wait(answers, grace_minutes=5)
        self.assertEqual(why, "idle")
        self.assertEqual(len(polls), len(answers))

    def test_a_flickering_address_is_still_bounded_by_the_longest_wait(self):
        answers = ([down()] * 4 + [busy()]) * 100
        why, lines, fake, _ = run_wait(answers, max_minutes=30, grace_minutes=5)
        self.assertEqual(why, "maximum")
        self.assertLessEqual(fake.now, 30 * 60)

    def test_the_time_of_a_slow_poll_counts_toward_the_longest_wait(self):
        why, _, fake, polls = run_wait([busy()], interval=60, max_minutes=10, poll_seconds=10)
        self.assertEqual(why, "maximum")
        self.assertLessEqual(fake.now, 10 * 60 + 10)
        self.assertLess(len(polls), 11)                                                    # fewer polls than at 60 s a poll: each took 70 s

    def test_a_maximum_that_falls_on_a_bad_answer_says_so(self):
        why, lines, _, _ = run_wait([down("HTTP 404")], max_minutes=2, grace_minutes=60)
        self.assertEqual(why, "maximum")
        self.assertIn("does not answer properly (HTTP 404) and the longest wait of 2 minutes is over, DEPLOYING ANYWAY", lines[-1])

    def test_clock_text(self):
        self.assertEqual(dw.clock_text(0), "0 min 00 s")
        self.assertEqual(dw.clock_text(125.9), "2 min 05 s")
        self.assertEqual(dw.clock_text(10800), "180 min 00 s")


class Scripted(http.server.BaseHTTPRequestHandler):
    """Answers from `server.script` (a list of (status, content_type, body, extra headers); the last one repeats), counts the requests and records the paths."""

    def do_GET(self):
        server = self.server
        with server.lock:
            index = min(server.hits, len(server.script) - 1)
            server.hits += 1
            server.paths.append(self.path)
            status, ctype, body, headers = server.script[index]
        if ctype == "SLOW":
            time.sleep(1.5)
            status, ctype = 200, "application/json"
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        for key, value in headers.items():
            self.send_header(key, value)
        self.end_headers()
        try:
            self.wfile.write(body)
        except OSError:
            pass

    def log_message(self, *args):
        pass


def answer(matches, players):
    return (200, "application/json", json.dumps({"matches": matches, "players": players}, separators=(",", ":")).encode(), {})


class Serving:
    """A real HTTP server on 127.0.0.1 with a scripted answer, for the length of a `with`."""

    def __init__(self, *script):
        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Scripted)
        self.server.daemon_threads = True
        self.server.script = list(script)
        self.server.hits = 0
        self.server.paths = []
        self.server.lock = threading.Lock()
        self.thread = threading.Thread(target=self.server.serve_forever, kwargs={"poll_interval": 0.01}, daemon=True)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *exc):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(5)

    @property
    def url(self):
        return "http://127.0.0.1:%d/busy" % self.server.server_address[1]

    @property
    def hits(self):
        return self.server.hits


def closed_port_url():
    """An address on this machine that nothing listens on."""
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]
    return "http://127.0.0.1:%d/busy" % port


class ReadingTheAnswer(unittest.TestCase):
    def read(self, *script, timeout=3.0):
        with Serving(*script) as server:
            return dw.read_counts(server.url, timeout), server

    def test_the_two_counts_are_an_answer(self):
        (counts, problem), server = self.read(answer(2, 7))
        self.assertEqual((counts, problem), ((2, 7), None))
        self.assertEqual(server.server.paths, ["/busy"])
        self.assertEqual(self.read(answer(0, 0))[0], ((0, 0), None))
        spaced = (200, "application/json", b'{ "players": 5 ,\n "matches": 1 }\n', {})            # (any order and any spacing: it is JSON)
        self.assertEqual(self.read(spaced)[0], ((1, 5), None))

    def test_only_exactly_two_non_negative_whole_counts_are_an_answer(self):
        bad = {
            "a missing page": (404, "text/plain", b"not found", {}),
            "a server error": (500, "text/plain", b"oops", {}),
            "a bad gateway": (502, "text/html", b"<html>bad gateway</html>", {}),
            "a rate limit": (503, "text/html", b"<html>slow down</html>", {}),
            "an empty answer": (200, "application/json", b"", {}),
            "a web page (the site's fallback page)": (200, "text/html", b"<!doctype html><title>Ants</title>", {}),
            "an extra key": (200, "application/json", b'{"matches":0,"players":0,"x":1}', {}),
            "a missing key": (200, "application/json", b'{"matches":0}', {}),
            "another key": (200, "application/json", b'{"matches":0,"people":0}', {}),
            "a list": (200, "application/json", b"[0,0]", {}),
            "a number": (200, "application/json", b"0", {}),
            "a float": (200, "application/json", b'{"matches":0.0,"players":0}', {}),
            "a boolean": (200, "application/json", b'{"matches":false,"players":0}', {}),
            "a text": (200, "application/json", b'{"matches":"0","players":0}', {}),
            "a null": (200, "application/json", b'{"matches":null,"players":0}', {}),
            "a negative count": (200, "application/json", b'{"matches":-1,"players":0}', {}),
            "not-a-number": (200, "application/json", b'{"matches":NaN,"players":0}', {}),
            "no content": (204, "application/json", b"", {}),
            "another success status with the counts": (203, "application/json", b'{"matches":0,"players":0}', {}),
            "a long body": (200, "application/json", b'{"matches":0,"players":0,"pad":"' + b"x" * 4000 + b'"}', {}),
            "bytes that are no text": (200, "application/json", b"\xff\xfe\x00{", {}),
        }
        for name, response in bad.items():
            (counts, problem), _ = self.read(response)
            self.assertIsNone(counts, name)
            self.assertTrue(problem, name)

    def test_the_reasons_are_short_and_say_what_went_wrong(self):
        self.assertEqual(self.read((404, "text/plain", b"x", {}))[0][1], "HTTP 404")
        self.assertEqual(self.read((203, "application/json", b'{"matches":0,"players":0}', {}))[0][1], "HTTP 203")
        self.assertEqual(self.read((200, "text/html", b"<html>", {}))[0][1], "the answer is not JSON")
        self.assertEqual(self.read((200, "application/json", b"[]", {}))[0][1], "the answer is not the two counts")
        self.assertEqual(self.read((200, "application/json", b'{"matches":-1,"players":0}', {}))[0][1], "the counts are not numbers")
        self.assertEqual(self.read((200, "application/json", b"x" * 5000, {}))[0][1], "the answer is too long")

    def test_a_redirect_to_the_answer_is_followed(self):
        with Serving((301, "text/plain", b"", {"Location": "/elsewhere"}), answer(0, 1)) as server:
            self.assertEqual(dw.read_counts(server.url, 3.0), ((0, 1), None))
            self.assertEqual(server.server.paths, ["/busy", "/elsewhere"])

    def test_a_closed_port_and_a_slow_server_are_no_answer(self):
        counts, problem = dw.read_counts(closed_port_url(), 2.0)
        self.assertIsNone(counts)
        self.assertEqual(problem, "connection refused")
        (counts, problem), _ = self.read(("SLOW", "SLOW", b'{"matches":0,"players":0}', {}), timeout=0.3)
        self.assertIsNone(counts)
        self.assertEqual(problem, "timed out")

    def test_a_name_that_does_not_exist_and_an_address_that_is_none_are_no_answer(self):
        counts, problem = dw.read_counts("http://nonexistent.invalid/busy", 3.0)
        self.assertIsNone(counts)
        self.assertTrue(problem)
        self.assertNotIn("nonexistent", problem)                                           # (a reason never holds the address)
        counts, problem = dw.read_counts("http://127.0.0.1:1/ spaced", 1.0)
        self.assertIsNone(counts)
        self.assertNotIn("spaced", problem)

    def test_a_server_that_hangs_up_in_the_middle_of_its_answer_is_no_answer(self):
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        port = listener.getsockname()[1]

        def serve():
            connection, _ = listener.accept()
            connection.recv(4096)
            connection.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n{\"matches\":")
            connection.close()

        thread = threading.Thread(target=serve, daemon=True)
        thread.start()
        try:
            counts, problem = dw.read_counts("http://127.0.0.1:%d/busy" % port, 3.0)
        finally:
            thread.join(5)
            listener.close()
        self.assertIsNone(counts)
        self.assertTrue(problem)


def run_tool(*args, timeout=15):
    """Runs the program to its end (a program that does not end in `timeout` seconds is an error: a mutant that waits for ever is caught by it)."""
    return subprocess.run([sys.executable, TOOL, *args], capture_output=True, text=True, timeout=timeout)


def first_lines(*args, count=2):
    """Starts the program, reads its first `count` lines of output and cancels it: what it says before it polls, whatever it does after that."""
    process = subprocess.Popen([sys.executable, TOOL, *args], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    lines = []
    try:
        for _ in range(count):
            line = process.stdout.readline()
            if not line:
                break
            lines.append(line.rstrip("\n"))
    finally:
        process.terminate()
        process.communicate(timeout=15)
    return lines


class TheProgram(unittest.TestCase):
    def test_an_idle_server_ends_the_wait_at_once(self):
        with Serving(answer(0, 2)) as server:
            started = time.monotonic()
            result = run_tool("--url", server.url, "--label", "production", timeout=10)
            took = time.monotonic() - started
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("polling the busy address of production every 60 s", result.stdout)
            self.assertIn("the server is idle, deploying", result.stdout)
            self.assertEqual(server.hits, 1)
            self.assertLess(took, 20)

    def test_it_waits_for_the_matches_and_goes_on_when_they_are_over(self):
        with Serving(answer(2, 5), answer(2, 5), answer(1, 2), answer(0, 0)) as server:
            result = run_tool("--url", server.url, "--interval", "0.2", "--max-wait-minutes", "0.2")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(server.hits, 4)
            lines = result.stdout.splitlines()
            self.assertEqual(sum("running with" in l for l in lines), 3)
            self.assertIn("2 matches running with 5 people: waiting", result.stdout)
            self.assertIn("the server is idle, deploying", lines[-1])

    def test_a_server_that_stays_busy_is_deployed_anyway_after_the_longest_wait(self):
        with Serving(answer(1, 2)) as server:
            started = time.monotonic()
            result = run_tool("--url", server.url, "--interval", "0.2", "--max-wait-minutes", "0.03")        # 1.8 s
            took = time.monotonic() - started
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("DEPLOYING ANYWAY", result.stdout)
            self.assertIn("1 match still running with 2 people", result.stdout)
            self.assertGreaterEqual(took, 1.2)
            self.assertLess(took, 20)
            self.assertGreater(server.hits, 3)

    def test_a_wait_of_zero_looks_once_and_deploys_anyway(self):
        with Serving(answer(1, 2)) as server:
            result = run_tool("--url", server.url, "--max-wait-minutes", "0", timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("DEPLOYING ANYWAY", result.stdout)
            self.assertEqual(server.hits, 1)

    def test_an_address_that_does_not_answer_is_deployed_after_the_grace(self):
        started = time.monotonic()
        result = run_tool("--url", closed_port_url(), "--interval", "0.2", "--grace-minutes", "0.02")      # 1.2 s
        took = time.monotonic() - started
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("has not answered properly for", result.stdout)
        self.assertIn("connection refused", result.stdout)
        self.assertIn("deploying", result.stdout.splitlines()[-1])
        self.assertGreaterEqual(took, 1.0)
        self.assertLess(took, 20)

    def test_a_missing_page_is_not_taken_for_an_idle_server(self):
        # the first deploy of a build that has /busy runs against a site that has none yet: the answer is a 404 or the page of the site, and the wait must run its grace out
        for response in ((404, "text/plain", b"nope", {}), (200, "text/html", b"<html>the game</html>", {})):
            with Serving(response) as server:
                result = run_tool("--url", server.url, "--interval", "0.2", "--grace-minutes", "0.02")
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertNotIn("the server is idle", result.stdout)
                self.assertIn("has not answered properly for", result.stdout)
                self.assertGreater(server.hits, 3)

    def test_without_an_address_it_deploys_without_a_wait(self):
        for args in ([], ["--url", "", "--label", "staging"]):
            result = run_tool(*args, timeout=8)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("no busy address is set for", result.stdout)
            self.assertIn("deploying without a wait", result.stdout)
        self.assertIn("for staging", run_tool("--url", "", "--label", "staging", timeout=8).stdout)

    def test_a_wrong_argument_is_status_2_and_writes_to_stderr(self):
        for args in (["--url", "ftp://example.org/busy"], ["--url", "example.org/busy"], ["--interval", "0"], ["--interval", "-3"], ["--interval", "x"], ["--nonsense"]):
            result = run_tool(*args)
            self.assertEqual(result.returncode, 2, args)
            self.assertEqual(result.stdout.count("deploying"), 0, args)
            self.assertTrue(result.stderr, args)

    def test_a_longest_wait_that_is_no_number_is_replaced_by_the_default_and_says_so(self):
        with Serving(answer(1, 1)) as server:                                              # (busy: the lines before the first poll are what is looked at, then the program is cancelled)
            for text in ("abc", "-5", "nan", "1,5"):
                lines = first_lines("--url", server.url, "--max-wait-minutes", text, count=2)
                self.assertEqual(len(lines), 2, text)
                self.assertEqual(lines[0], "deploy wait: the longest wait '%s' is not a number of minutes: using 180" % text)
                self.assertIn("after 180 minutes at the most", lines[1])
            lines = first_lines("--url", server.url, "--grace-minutes", "soon", count=2)
            self.assertEqual(lines[0], "deploy wait: the grace 'soon' is not a number of minutes: using 5")
            self.assertIn("has not answered for 5 minutes", lines[1])
            self.assertIn("every 60 s", lines[1])                                          # (the defaults: a poll a minute, 180 minutes, 5 minutes of grace)
            self.assertIn("after 180 minutes at the most", lines[1])

    def test_the_longest_wait_has_a_cap_below_the_time_limit_of_the_job(self):
        with Serving(answer(1, 1)) as server:
            lines = first_lines("--url", server.url, "--max-wait-minutes", "100000", count=2)
            self.assertEqual(lines[0], "deploy wait: 100000 minutes is more than a job may take: waiting at most 300")
            self.assertIn("after 300 minutes at the most", lines[1])
        self.assertEqual(dw.MAX_WAIT_CAP_MINUTES, 300)

    def test_the_address_is_never_printed(self):
        token = "tokenpartofthestagingaddress"
        scripts = ([answer(1, 1)] * 3, [(404, "text/plain", b"x", {})], [answer(0, 0)])
        for script in scripts:
            with Serving(*script) as server:
                url = server.url.replace("/busy", "/" + token)
                result = run_tool("--url", url, "--interval", "0.2", "--max-wait-minutes", "0.02", "--grace-minutes", "0.01")
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertNotIn(token, result.stdout + result.stderr)
                self.assertNotIn("127.0.0.1", result.stdout + result.stderr)
        result = run_tool("--url", closed_port_url().replace("/busy", "/" + token), "--interval", "0.2", "--grace-minutes", "0.01")
        self.assertNotIn(token, result.stdout + result.stderr)
        self.assertNotIn("127.0.0.1", result.stdout + result.stderr)

    @unittest.skipIf(os.name == "nt", "signals as the runner sends them are POSIX")
    def test_a_cancelled_wait_says_that_nothing_is_deployed_and_is_not_status_0(self):
        # a newer push cancels the job of an older one: the runner sends SIGINT, later SIGTERM (a closed terminal sends SIGHUP). The test itself may run in the background of a shell
        # (./run_tests.sh starts its suites that way), where SIGINT is inherited as ignored: the program must not depend on the default.
        for sig, ignored_at_start in ((signal.SIGINT, False), (signal.SIGINT, True), (signal.SIGTERM, False), (signal.SIGHUP, False)):
            with Serving(answer(1, 2)) as server:
                command = [sys.executable, TOOL, "--url", server.url, "--interval", "30"]
                if ignored_at_start:                                                        # (what a shell does for a program that it starts in the background)
                    command = ["sh", "-c", "trap '' INT; exec \"$@\"", "sh"] + command
                process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                try:
                    deadline = time.monotonic() + 15
                    while server.hits < 1 and time.monotonic() < deadline:
                        time.sleep(0.05)
                    self.assertGreaterEqual(server.hits, 1)
                    time.sleep(0.2)
                    process.send_signal(sig)
                    out, err = process.communicate(timeout=15)
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.communicate()
                self.assertEqual(process.returncode, 130, (sig, out, err))
                self.assertIn("cancelled", out)
                self.assertIn("nothing is deployed from this run", out)
                self.assertNotIn("Traceback", err)
                self.assertNotIn("deploying", out.replace("nothing is deployed", ""))

    def test_the_script_has_no_way_to_print_the_address(self):
        with open(TOOL, encoding="utf-8") as f:
            source = f.read()
        for line in source.splitlines():
            code = line.split("#", 1)[0]
            if "say(" in code or "out(" in code or "print(" in code:
                self.assertNotIn("args.url", code, line)
                self.assertNotIn("(url", code.replace("read_counts(url", ""), line)


CI = os.path.join(REPO, ".github", "workflows", "ci.yml")


def deploy_step(name):
    """(the `env:` entries, the `run:` script) of one step of the deploy job, read from the workflow's text."""
    with open(CI, encoding="utf-8") as f:
        lines = f.read().splitlines()
    job = lines.index("  deploy:")
    start = next(i for i in range(job, len(lines)) if lines[i] == "      - name: " + name)
    end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith("      - ")), len(lines))
    step = lines[start:end]
    env = {}
    in_env = False
    script = []
    in_run = False
    for line in step:
        if line == "        env:":
            in_env, in_run = True, False
        elif line == "        run: |":
            in_env, in_run = False, True
        elif in_env and line.startswith("          "):
            key, _, value = line.strip().partition(": ")
            env[key] = value
        elif in_run and (line.startswith("          ") or not line.strip()):
            script.append(line[10:])
        elif not line.startswith("          ") and line.strip() and not line.lstrip().startswith("#"):
            in_env = in_run = False
    return env, "\n".join(script).rstrip() + "\n"


@unittest.skipUnless(shutil.which("bash"), "bash is needed")
class TheWorkflowStep(unittest.TestCase):
    NAME = "Wait for an idle game server"

    def run_step(self, branch, production, staging, max_wait="0", timeout=15):
        env, script = deploy_step(self.NAME)
        self.assertEqual(sorted(env), ["MAX_WAIT_MINUTES", "PRODUCTION_BUSY_URL", "STAGING_BUSY_URL"])
        environment = dict(os.environ, BRANCH=branch, PRODUCTION_BUSY_URL=production, STAGING_BUSY_URL=staging, MAX_WAIT_MINUTES=max_wait)
        return subprocess.run(["bash", "--noprofile", "--norc", "-eo", "pipefail", "-c", script], cwd=REPO, env=environment, capture_output=True, text=True, timeout=timeout)

    def test_the_step_is_read_from_the_workflow(self):
        env, script = deploy_step(self.NAME)
        self.assertIn("python3 tools/deploy_wait.py", script)
        self.assertEqual(env["MAX_WAIT_MINUTES"], "${{ vars.DEPLOY_MAX_WAIT_MINUTES || '180' }}")
        self.assertEqual(env["PRODUCTION_BUSY_URL"], "${{ vars.DEPLOY_BUSY_URL || 'https://beta.playants.org/busy' }}")
        self.assertEqual(env["STAGING_BUSY_URL"], "${{ vars.STAGING_BUSY_URL }}")

    def test_the_production_branch_polls_the_production_address_and_never_the_staging_one(self):
        with Serving(answer(0, 3)) as production, Serving(answer(5, 9)) as staging:
            result = self.run_step("main", production.url, staging.url)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("polling the busy address of production every 60 s", result.stdout)
            self.assertIn("the server is idle, deploying", result.stdout)
            self.assertEqual((production.hits, staging.hits), (1, 0))
            self.assertNotIn("127.0.0.1", result.stdout + result.stderr)

    def test_the_staging_branch_polls_the_staging_address_and_deploys_anyway_when_the_wait_is_zero(self):
        with Serving(answer(0, 0)) as production, Serving(answer(5, 9)) as staging:
            result = self.run_step("staging", production.url, staging.url)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("polling the busy address of staging", result.stdout)
            self.assertIn("5 matches still running with 9 people", result.stdout)
            self.assertIn("DEPLOYING ANYWAY", result.stdout)
            self.assertEqual((production.hits, staging.hits), (0, 1))

    def test_the_staging_branch_does_not_wait_while_it_has_no_address(self):
        with Serving(answer(5, 9)) as production:
            result = self.run_step("staging", production.url, "", max_wait="180", timeout=8)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("no busy address is set for staging: deploying without a wait", result.stdout)
            self.assertEqual(production.hits, 0)                                           # (the production address is never the fallback of staging)

    def test_a_mistyped_wait_is_replaced_by_the_default_and_never_stops_the_deploy(self):
        with Serving(answer(0, 0)) as production:
            result = self.run_step("main", production.url, "", max_wait="three hours", timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("the longest wait 'three hours' is not a number of minutes: using 180", result.stdout)
            self.assertIn("the server is idle, deploying", result.stdout)

    def test_a_branch_other_than_staging_takes_the_production_address(self):
        with Serving(answer(0, 0)) as production, Serving(answer(0, 0)) as staging:
            self.assertEqual(self.run_step("main", production.url, staging.url).returncode, 0)
            self.assertEqual((production.hits, staging.hits), (1, 0))


if __name__ == "__main__":
    unittest.main()
