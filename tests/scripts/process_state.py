"""Whether a process still runs, for the tests that check that a tool stopped what it started.

`os.kill(pid, 0)` succeeds for a zombie too (a killed process not yet collected). A child whose parent is gone is adopted by
process 1, and process 1 of some containers collects late, so there a child that was stopped looked alive: a zombie runs nothing.
"""
import os
import subprocess


def running(pid):
    """True while the process exists and is not a zombie."""
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    try:
        with open("/proc/%d/stat" % pid) as f:                   # Linux: the state is the first field after the command's ")"
            return f.read().rsplit(")", 1)[1].split()[0] not in ("Z", "X")
    except OSError:
        pass
    state = subprocess.run(["ps", "-o", "stat=", "-p", str(pid)], capture_output=True, text=True).stdout.strip()
    return state != "" and not state.startswith("Z")             # macOS and other systems without /proc
