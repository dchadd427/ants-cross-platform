#!/usr/bin/env python3
"""Tests of tests/scripts/measure_tick_rate.py, the measure of the referee's tick rate in the server end-to-end script (run by ./run_tests.sh --fast and by the CI).

The script used to read a room's ticks twice, sleep 3 s between the readings and divide by 3.0; on a loaded macOS runner it measured 25.0 ticks a second of a referee that sealed 20, and
the band 17 - 23 failed a good server. Two groups of tests:

  - the arithmetic, on a clock that only moves when the measure sleeps or reads (a long window takes no time here): the steady server, slow answers, a server that stalls and catches up
    (a burst of turns, as HostSession does), and a rate that is wrong (30 and 10 a second, and also the nearer 26 and 14) must be seen as wrong by the band of the script
  - the program as the script runs it: against a real HTTP server on this machine, its line, a room that never ticks, and a server that does not answer
"""
import http.server
import os
import socketserver
import subprocess
import sys
import threading
import time
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "measure_tick_rate.py")
sys.path.insert(0, HERE)
import measure_tick_rate as mt     # noqa: E402

BAND = (17.0, 23.0)     # the band that tests/scripts/test_ants_server.sh checks the rate against (the band of the check before the measure was made precise: it is not widened)


class Sim:
    """A server whose tick count is a function of the time, and a clock that moves by the delay of every reading."""

    def __init__(self, ticks_at, delay=0.02, delays=None):
        self.now = 0.0
        self.ticks_at = ticks_at
        self.delay = delay
        self.delays = list(delays or [])

    def clock(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds

    def read(self):
        wait = self.delays.pop(0) if self.delays else self.delay
        self.now += wait / 2.0                      # (the server reads its count in the middle of the request)
        ticks = self.ticks_at(self.now)
        self.now += wait / 2.0
        return ticks

    def measure(self, window):
        return mt.measure(self.read, self.clock, self.sleep, window)


def steady(rate, first=5.0):
    return lambda t: 0 if t < first else int((t - first) * rate)


def stalls(rate, first, stall_from, stall_to):
    """`rate` a second, but nothing is sealed from `stall_from` to `stall_to` (a starved server); after it the turns that were due are sealed at once."""
    def ticks(t):
        if t < first:
            return 0
        if stall_from <= t < stall_to:
            return int((stall_from - first) * rate)
        return int((t - first) * rate)
    return ticks


class Arithmetic(unittest.TestCase):
    def rate(self, sim, window=8.0):
        rate, ticks, seconds, readings = sim.measure(window)
        self.assertGreaterEqual(readings, mt.MIN_READINGS)
        return rate

    def test_steady_server_measures_its_rate(self):
        self.assertAlmostEqual(self.rate(Sim(steady(20.0))), 20.0, delta=0.5)

    def test_window_starts_at_the_first_tick_not_at_the_start_of_the_match(self):
        sim = Sim(steady(20.0, first=11.0))
        rate, ticks, seconds, readings = sim.measure(8.0)
        self.assertAlmostEqual(rate, 20.0, delta=0.5)
        self.assertGreaterEqual(seconds, 8.0)
        self.assertLess(seconds, 8.5)
        self.assertGreater(sim.now, 19.0)                   # (it waited for the dialog and then measured for the whole window)

    def test_slow_answers_do_not_change_the_rate(self):
        # the first reading took 0.75 s: the old script, which divided the ticks between two readings by a nominal 3 s, measured a quarter more than the server did
        slow = Sim(steady(20.0), delays=[0.75] + [0.05] * 400)
        self.assertAlmostEqual(self.rate(slow), 20.0, delta=1.0)
        self.assertTrue(BAND[0] <= self.rate(Sim(steady(20.0), delay=0.6)) <= BAND[1])

    def test_the_old_arithmetic_failed_where_the_measure_holds(self):
        sim = Sim(steady(20.0), delays=[0.02, 0.02])
        sim.sleep(10.0)                                    # (the match is past its dialog)
        first = sim.read()                                  # (what wait_ticks printed)
        sim.sleep(3.0 + 0.73)                              # `sleep 3`, then a python and a curl that start late on a busy machine
        last = sim.read()
        self.assertGreater((last - first) / 3.0, 24.0)      # the line of the CI log: "(measured: 25.0)", over the 23 of the band
        self.assertTrue(BAND[0] <= self.rate(Sim(steady(20.0), delay=0.4)) <= BAND[1])

    def test_a_stall_and_its_catch_up_stay_inside_the_band(self):
        for length in (0.75, 1.5, 2.0):
            for start in (5.5, 8.0, 11.0, 12.5):
                sim = Sim(stalls(20.0, 5.0, start, start + length))
                self.assertTrue(BAND[0] <= self.rate(sim) <= BAND[1], (length, start))

    def test_a_wrong_rate_is_outside_the_band(self):
        for wrong in (30.0, 10.0, 26.0, 14.0, 24.0, 16.0, 40.0, 60.0, 5.0):
            rate = self.rate(Sim(steady(wrong)))
            self.assertFalse(BAND[0] <= rate <= BAND[1], (wrong, rate))
            self.assertAlmostEqual(rate, wrong, delta=0.06 * wrong)

    def test_a_wrong_rate_is_outside_the_band_on_a_loaded_machine_too(self):
        for wrong in (30.0, 10.0):
            rate = self.rate(Sim(steady(wrong), delays=[0.75, 0.4] + [0.3] * 400))
            self.assertFalse(BAND[0] <= rate <= BAND[1], (wrong, rate))

    def test_a_room_that_never_ticks_has_rate_zero(self):
        sim = Sim(steady(20.0, first=1.0e9))
        rate, ticks, seconds, readings = sim.measure(8.0)
        self.assertEqual((rate, ticks, readings), (0.0, 0, 0))
        self.assertGreater(sim.now, mt.START_WAIT)          # (it waited the 30 s, and no longer than 30 s + the window)
        self.assertLess(sim.now, mt.START_WAIT + 8.0 + 1.0)

    def test_a_server_that_does_not_answer_has_rate_zero(self):
        sim = Sim(lambda t: None)
        rate, ticks, seconds, readings = sim.measure(8.0)
        self.assertEqual((rate, readings), (0.0, 0))

    def test_a_server_that_answers_a_few_times_only_is_no_measure(self):
        answers = [3, 9, 12]
        sim = Sim(lambda t: answers.pop(0) if answers else None)
        rate, ticks, seconds, readings = sim.measure(8.0)
        self.assertEqual(rate, 0.0)

    def test_the_slope_of_a_line_and_of_a_single_point(self):
        self.assertAlmostEqual(mt.slope([(0.0, 0), (1.0, 20), (2.0, 40), (3.0, 60)]), 20.0)
        self.assertEqual(mt.slope([(1.0, 7)]), 0.0)


class QuickServer(http.server.ThreadingHTTPServer):
    """A server that is up at once. HTTPServer.server_bind asks socket.getfqdn() for the name of the machine, a reverse lookup that took seconds a start on the macOS runner of the CI (the five tests
    of Program took 45 s there and 10 s on Linux), for a name that nobody reads."""

    def server_bind(self):
        socketserver.TCPServer.server_bind(self)
        self.server_name = "localhost"
        self.server_port = self.server_address[1]


class FakeRoom(http.server.BaseHTTPRequestHandler):
    started = time.monotonic()
    rate = 20.0
    first = 0.3
    secret = "s3cret"

    def do_GET(self):
        if self.headers.get("Authorization") != "Bearer " + self.secret or self.path != "/rooms/R1":
            self.send_response(401)
            self.end_headers()
            return
        elapsed = time.monotonic() - FakeRoom.started
        ticks = 0 if elapsed < FakeRoom.first else int((elapsed - FakeRoom.first) * FakeRoom.rate)
        body = ('{"state":"running","ticks":%d}' % ticks).encode()
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


class Program(unittest.TestCase):
    def setUp(self):
        FakeRoom.started = time.monotonic()
        FakeRoom.rate = 20.0
        FakeRoom.first = 0.3
        with mock.patch("socket.getfqdn", side_effect=AssertionError("the fake server asked for the name of the machine: a reverse lookup that takes seconds on a macOS runner")):
            self.httpd = QuickServer(("127.0.0.1", 0), FakeRoom)
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()
        self.base = "http://127.0.0.1:%d" % self.httpd.server_address[1]

    def tearDown(self):
        self.httpd.shutdown()
        self.httpd.server_close()

    def run_tool(self, room="R1", secret="s3cret", window="1.5", base=None, start_wait=None):
        extra = [start_wait] if start_wait is not None else []
        return subprocess.run([sys.executable, TOOL, base or self.base, room, secret, window] + extra, capture_output=True, text=True, timeout=60)

    def test_prints_the_rate_ticks_seconds_and_readings_in_one_line(self):
        done = self.run_tool()
        self.assertEqual(done.returncode, 0, done.stderr)
        fields = done.stdout.split()
        self.assertEqual(len(fields), 4, done.stdout)
        self.assertAlmostEqual(float(fields[0]), 20.0, delta=2.0)
        self.assertGreater(int(fields[1]), 20)
        self.assertGreaterEqual(float(fields[2]), 1.5)
        self.assertGreaterEqual(int(fields[3]), mt.MIN_READINGS)

    def test_a_wrong_rate_is_seen(self):
        FakeRoom.rate = 40.0
        rate = float(self.run_tool().stdout.split()[0])
        self.assertAlmostEqual(rate, 40.0, delta=4.0)
        self.assertFalse(BAND[0] <= rate <= BAND[1])

    def test_a_wrong_secret_is_no_measure(self):
        done = self.run_tool(secret="wrong", window="0.5", start_wait="0.5")
        self.assertEqual(done.stdout.split(), ["0.00", "0", "0.00", "0"])

    def test_a_room_that_does_not_exist_and_a_port_that_is_closed_are_no_measure(self):
        self.assertEqual(self.run_tool(room="nowhere", window="0.5", start_wait="0.5").stdout.split(), ["0.00", "0", "0.00", "0"])
        self.assertEqual(self.run_tool(base="http://127.0.0.1:9", window="0.5", start_wait="0.5").stdout.split(), ["0.00", "0", "0.00", "0"])

    def test_usage(self):
        done = subprocess.run([sys.executable, TOOL], capture_output=True, text=True, timeout=60)
        self.assertEqual(done.returncode, 2)
        self.assertIn("usage:", done.stdout)


if __name__ == "__main__":
    unittest.main()
