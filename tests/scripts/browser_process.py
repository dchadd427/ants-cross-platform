"""The browser that a check starts (the checks that drive a real browser through its DevTools port: web_aspect_check.py, web_hidden_check.py, web_prediction_check.py and every check built on them).

One home for how such a browser is started and stopped, so that none is left running when a check ends:

  * the browser is a process of its own and the leader of a group of its own (`start_new_session`), so that it is stopped by its PID and the group of that PID: the browser and the helpers that it started,
    nothing else, and never by a name (another check's browser, or the person's own, has the same name);
  * it is stopped on every way out of a check: the end, a failure, a deadline (a TimeoutError of the check), a Ctrl-C and the SIGTERM or SIGHUP of a runner that gives up on the check (the first such signal ends the
    check with an exit, so that its `finally` blocks run too), and, last, when the program ends in any way that runs `atexit`;
  * a browser that does not leave when asked is killed after `grace` seconds, with its group.

SIGKILL of the check itself cannot be answered by any program: a runner that must give up on a check for good sends SIGTERM first.
"""
import atexit
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import threading

_running = []                    # [(process, profile)] of the browsers that this program started and has not stopped
_lock = threading.Lock()
_installed = []                  # the signals whose handler this module put in


def _send(process, signum):
    """The signal to the browser's group (the group of its PID: it is the leader), or to the process alone where there are no groups; a process that is gone is no error"""
    try:
        if os.name == "posix":
            os.killpg(process.pid, signum)
        elif signum == signal.SIGTERM:
            process.terminate()
        else:
            process.kill()
    except (ProcessLookupError, PermissionError, OSError):
        pass


def stop(process, profile=None, grace=10):
    """Stop this browser and what it started, and remove its profile folder: asked to leave (SIGTERM), killed when it has not gone after `grace` seconds; afterwards no process of its group is left either."""
    with _lock:
        _running[:] = [entry for entry in _running if entry[0] is not process]
    if process.poll() is None:
        _send(process, signal.SIGTERM)
        try:
            process.wait(timeout=grace)
        except subprocess.TimeoutExpired:
            _send(process, signal.SIGKILL)
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                pass
    _send(process, signal.SIGKILL)                          # (the helpers of a browser that left at once or was killed: its group, and only its group; a number is not given to another process while a process of its group lives)
    if profile:
        shutil.rmtree(profile, ignore_errors=True)


def stop_all():
    """Stop every browser that this program started and has not stopped (at exit, and in the handler of a signal)"""
    with _lock:
        entries = list(_running)
    for process, profile in entries:
        stop(process, profile, grace=3)


def _on_signal(signum, frame):
    stop_all()
    sys.exit(128 + signum)                                  # (SystemExit: the `finally` blocks of the check run, then atexit)


def _install():
    if _installed or threading.current_thread() is not threading.main_thread():
        return
    atexit.register(stop_all)
    for name in ("SIGTERM", "SIGHUP"):
        signum = getattr(signal, name, None)
        if signum is not None and signal.getsignal(signum) == signal.SIG_DFL:
            signal.signal(signum, _on_signal)
            _installed.append(signum)
    _installed.append(0)


def start(command, profile_prefix="ants_browser_profile."):
    """Start the browser `command` (the program and its arguments, each with `{profile}` where the folder of its profile goes) with a new, empty profile folder of its own; (process, profile).
    The caller stops it with stop(process, profile) when it is done, in a `finally`."""
    profile = tempfile.mkdtemp(prefix=profile_prefix)
    _install()
    try:
        process = subprocess.Popen([part.replace("{profile}", profile) for part in command], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                   start_new_session=(os.name == "posix"))
    except BaseException:
        shutil.rmtree(profile, ignore_errors=True)
        raise
    with _lock:
        _running.append((process, profile))
    return process, profile


def run_check(argv, timeout, **kwargs):
    """subprocess.run(argv, capture_output=True, text=True, timeout=timeout) for a check that starts a browser: at the deadline the check is asked to leave (SIGTERM: it stops its browser, see above) and is
    killed only 15 seconds later; the deadline is a subprocess.TimeoutExpired, as with subprocess.run, and the browser is not left behind by the kill that subprocess.run would have made at once."""
    with subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, **kwargs) as process:
        try:
            out, err = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.terminate()
            try:
                out, err = process.communicate(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                out, err = process.communicate()
            raise subprocess.TimeoutExpired(argv, timeout, out, err)
    return subprocess.CompletedProcess(argv, process.returncode, out, err)
