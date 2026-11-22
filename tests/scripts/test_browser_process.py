#!/usr/bin/env python3
"""The browser that a check starts is stopped on every way out of the check, by its PID and the group of that PID and never by a name (tests/scripts/browser_process.py; run by ./run_tests.sh --tools).

The "browsers" here are small programs of the test's own that start a helper of their own and wait, so that what the helper does is seen too:

  TheStop        a browser and its helper are gone after stop(); one that ignores the request is killed; another browser that runs beside it is not touched
  TheWayOut      the check ends, fails, is asked to leave by SIGTERM (a runner's deadline) or is interrupted: the browser and its helper are gone each time
  TheStartOfOne  the browser of web_aspect_check.Browser that does not come up is stopped before the check says so
  TheDeadline    a check that is still running at its deadline is asked to leave first, and it takes its browser with it
"""
import os
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import browser_process  # noqa: E402
from process_state import running  # noqa: E402

POSIX = os.name == "posix"

# a "browser": it writes its own PID and its helper's PID to the files that it is given (the first two arguments) and waits; with the third argument `ignore` it does not leave when it is asked to, and with
# `helper_ignores` its helper does not (the browser itself does: a browser that has gone and left a helper behind)
FAKE = r"""
import os, signal, subprocess, sys, time
mine, helper = sys.argv[1], sys.argv[2]
if sys.argv[3:] == ["ignore"]:
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
stubborn = "signal.signal(signal.SIGTERM, signal.SIG_IGN); " if sys.argv[3:] == ["helper_ignores"] else ""
child = subprocess.Popen([sys.executable, "-c", "import signal, sys, time; " + stubborn + "open(sys.argv[1], 'w').write('up'); time.sleep(120)", helper + ".up"])
while not os.path.exists(helper + ".up"):                 # (the helper is up, and has set what it does with a request to leave, before anybody is told of it)
    time.sleep(0.02)
open(mine, "w").write(str(os.getpid()))
open(helper, "w").write(str(child.pid))
time.sleep(120)
"""


def read_file(path):
    try:
        with open(path) as f:
            return f.read()
    except OSError:
        return ""


def wait_for_file(path, seconds=30):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        try:
            with open(path) as f:
                text = f.read()
            if text:
                return int(text)
        except (OSError, ValueError):
            pass
        time.sleep(0.05)
    raise AssertionError("no %s within %s s" % (path, seconds))


def wait_until_gone(pid, seconds=30):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if not running(pid):
            return True
        time.sleep(0.05)
    return False


@unittest.skipUnless(POSIX, "the groups of processes are those of POSIX")
class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="ants_browser_process_test.")
        self.addCleanup(lambda: __import__("shutil").rmtree(self.tmp, ignore_errors=True))
        self.pids = []
        self.addCleanup(self.last_resort)

    def last_resort(self):
        for pid in self.pids:
            try:
                os.kill(pid, signal.SIGKILL)                         # (only the PIDs that this test saw its own programs make)
            except OSError:
                pass

    def path(self, name):
        return os.path.join(self.tmp, name)

    def start(self, tag, ignore=False, helper_ignores=False):
        """a fake browser by the module under test: (process, profile, the helper's PID)"""
        command = [sys.executable, "-c", FAKE, self.path(tag + ".pid"), self.path(tag + ".helper")] + (["ignore"] if ignore else ["helper_ignores"] if helper_ignores else [])
        process, profile = browser_process.start(command, "ants_browser_process_test_profile.")
        self.addCleanup(browser_process.stop, process, profile, 1)
        helper = wait_for_file(self.path(tag + ".helper"))
        self.pids += [process.pid, helper]
        return process, profile, helper


class TheStop(Base):
    def test_a_stopped_browser_its_helper_and_its_profile_are_gone(self):
        process, profile, helper = self.start("a")
        self.assertTrue(os.path.isdir(profile) and running(process.pid) and running(helper))
        browser_process.stop(process, profile)
        self.assertTrue(wait_until_gone(process.pid) and wait_until_gone(helper), "the browser and what it started are gone")
        self.assertFalse(os.path.exists(profile))

    def test_a_browser_that_does_not_leave_when_asked_is_killed_with_its_helper(self):
        process, profile, helper = self.start("b", ignore=True)
        began = time.monotonic()
        browser_process.stop(process, profile, grace=1)
        self.assertTrue(wait_until_gone(process.pid) and wait_until_gone(helper))
        self.assertGreaterEqual(time.monotonic() - began, 0.9, "it was asked first, and given its second")

    def test_a_helper_that_is_left_behind_by_a_browser_that_has_gone_is_killed_too(self):
        process, profile, helper = self.start("h", helper_ignores=True)
        browser_process.stop(process, profile)
        self.assertTrue(wait_until_gone(process.pid) and wait_until_gone(helper), "the helper that did not leave when asked is killed with the group")

    def test_only_its_own_browser_is_stopped(self):
        one, profile_one, helper_one = self.start("one")
        two, profile_two, helper_two = self.start("two")
        browser_process.stop(one, profile_one)
        self.assertTrue(wait_until_gone(one.pid) and wait_until_gone(helper_one))
        self.assertTrue(running(two.pid) and running(helper_two) and os.path.isdir(profile_two), "the other browser runs on, and so does its helper")
        browser_process.stop(two, profile_two)
        self.assertTrue(wait_until_gone(two.pid))

    def test_stopping_twice_and_stopping_one_that_has_gone_are_no_error(self):
        process, profile, helper = self.start("c")
        browser_process.stop(process, profile)
        browser_process.stop(process, profile)
        self.assertTrue(wait_until_gone(helper))


# the check of a test: it starts a browser by the module under test, says where its PIDs are, and then does what the case wants
CHECK = r"""
import os, signal, sys, time
for name in ("SIGINT", "SIGTERM", "SIGHUP"):               # (a check that was started by a shell in the background, or by nohup, has some of these ignored: this one is the check of a person at a terminal)
    signal.signal(getattr(signal, name), signal.default_int_handler if name == "SIGINT" else signal.SIG_DFL)
sys.path.insert(0, %(here)r)
import browser_process
mine, helper, how = sys.argv[1], sys.argv[2], sys.argv[3]
process, profile = browser_process.start([sys.executable, "-c", open(%(fake)r).read(), mine, helper], "ants_browser_process_test_profile.")
try:
    while not (os.path.exists(helper) and open(helper).read()):
        time.sleep(0.05)
    open(sys.argv[4], "w").write(profile)
    if how == "end":
        pass
    elif how == "fail":
        raise RuntimeError("the check failed")
    elif how == "wait":
        time.sleep(120)
    elif how == "wait_and_clean_up":
        try:
            time.sleep(120)
        finally:
            browser_process.stop(process, profile)
    elif how == "forget":
        os._exit(0)
finally:
    if how == "end_with_a_finally":
        browser_process.stop(process, profile)
"""


class TheWayOut(Base):
    def run_check(self, how, signum=None, expect_exit=None):
        fake = self.path("fake.py")
        with open(fake, "w") as f:
            f.write(FAKE)
        script = self.path("check.py")
        with open(script, "w") as f:
            f.write(CHECK % {"here": HERE, "fake": fake})
        mine, helper, where = self.path("check_browser.pid"), self.path("check_helper.pid"), self.path("check_profile")
        process = subprocess.Popen([sys.executable, script, mine, helper, how, where], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.addCleanup(lambda: process.poll() is None and process.kill())
        browser_pid, helper_pid = wait_for_file(mine), wait_for_file(helper)
        self.pids += [browser_pid, helper_pid]
        profile = None
        if how != "end" and how != "fail":
            end = time.monotonic() + 30
            while time.monotonic() < end and not read_file(where):
                time.sleep(0.05)
            profile = read_file(where) or None
        if signum is not None:
            os.kill(process.pid, signum)
        try:
            process.communicate(timeout=60)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
            self.fail("the check did not end")
        if expect_exit is not None:
            self.assertEqual(process.returncode, expect_exit)
        return browser_pid, helper_pid, profile

    def gone(self, browser_pid, helper_pid, profile):
        self.assertTrue(wait_until_gone(browser_pid), "the browser is gone")
        self.assertTrue(wait_until_gone(helper_pid), "its helper is gone")
        if profile:
            self.assertFalse(os.path.exists(profile), "its profile folder is gone")

    def test_a_check_that_ends_with_no_cleanup_of_its_own_leaves_no_browser(self):
        self.gone(*self.run_check("end", expect_exit=0))

    def test_a_check_that_fails_leaves_no_browser(self):
        self.gone(*self.run_check("fail", expect_exit=1))

    def test_a_check_that_is_asked_to_leave_by_sigterm_leaves_no_browser_and_exits_by_it(self):
        self.gone(*self.run_check("wait", signal.SIGTERM, expect_exit=128 + signal.SIGTERM))

    def test_a_check_that_hangs_up_leaves_no_browser(self):
        self.gone(*self.run_check("wait", signal.SIGHUP, expect_exit=128 + signal.SIGHUP))

    def test_a_check_that_is_interrupted_leaves_no_browser(self):
        self.gone(*self.run_check("wait", signal.SIGINT))

    def test_a_check_that_cleans_up_by_itself_and_is_asked_to_leave_does_it_once_and_leaves_no_browser(self):
        self.gone(*self.run_check("wait_and_clean_up", signal.SIGTERM, expect_exit=128 + signal.SIGTERM))


class TheStartOfOne(Base):
    def test_a_browser_that_does_not_come_up_is_stopped_before_the_check_says_so(self):
        import web_aspect_check as aspect
        script = self.path("browser.sh")
        with open(script, "w") as f:
            f.write("#!/bin/sh\nexec sleep 120\n")
        os.chmod(script, 0o755)
        started = []
        real_start = browser_process.start

        def spy(*args, **kwargs):
            process, profile = real_start(*args, **kwargs)
            started.append((process, profile))
            self.pids.append(process.pid)
            return process, profile
        with mock.patch.object(browser_process, "start", spy), mock.patch.object(aspect, "EventDevTools", side_effect=OSError("no DevTools port")), mock.patch.object(aspect.time, "sleep", lambda s: None):
            with self.assertRaises(aspect.NotReachable):
                aspect.Browser(script)
        self.assertEqual(len(started), 1)
        process, profile = started[0]
        self.assertIsNotNone(process.poll(), "the browser that never answered was stopped and collected")
        self.assertFalse(running(process.pid))
        self.assertFalse(os.path.exists(profile), "and its profile folder is gone")


class TheDeadline(Base):
    def test_a_check_at_its_deadline_is_asked_to_leave_first_and_takes_its_browser_with_it(self):
        fake = self.path("fake.py")
        with open(fake, "w") as f:
            f.write(FAKE)
        script = self.path("check.py")
        with open(script, "w") as f:
            f.write(CHECK % {"here": HERE, "fake": fake})
        mine, helper, where = self.path("d_browser.pid"), self.path("d_helper.pid"), self.path("d_profile")
        started = []

        def watch():
            started.append((wait_for_file(mine), wait_for_file(helper)))
        import threading
        watcher = threading.Thread(target=watch)
        watcher.start()
        with self.assertRaises(subprocess.TimeoutExpired):
            browser_process.run_check([sys.executable, script, mine, helper, "wait", where], 5)
        watcher.join()
        browser_pid, helper_pid = started[0]
        self.pids += [browser_pid, helper_pid]
        self.assertTrue(wait_until_gone(browser_pid) and wait_until_gone(helper_pid), "the check's browser is not left running after its deadline")

    def test_a_check_that_ends_in_time_is_a_completed_process_with_its_output(self):
        done = browser_process.run_check([sys.executable, "-c", "import sys; print('out'); print('err', file=sys.stderr); sys.exit(3)"], 30)
        self.assertEqual((done.returncode, done.stdout.strip(), done.stderr.strip()), (3, "out", "err"))


if __name__ == "__main__":
    unittest.main()
