#!/usr/bin/env python3
"""Runs the python tests of tests/scripts side by side (suite 5.2 of ./run_tests.sh, and the CI).

usage: run_python_tests.py [--dir DIR] [--jobs N] [--list]

The result is that of `python3 -m unittest discover -s tests/scripts -p 'test_*.py'`: every test runs and the exit status is 1 when any failed. What differs is the time: every
test CLASS of every test_*.py file runs in a process of its own, N at a time (N = the number of cores, at most 8), so that the slowest class and not the sum of all of them
decides (the long ones, listed in COSTS, start first). The output is a line for each class, in the order of the files, the full output of the classes that failed, and a total. The classes share nothing (each makes its
folders under its own temporary folder), which is what lets them run together. A file without a class that has tests runs as a whole (an import error then shows up).
"""
import argparse
import ast
import concurrent.futures
import glob
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time


# The classes that take 20 seconds or more (seconds, alone, on a four-core machine, rounded): the longest start first, so that the last one to start is a short one and a runner with three cores does not begin
# a class of two minutes when the others are nearly done. The output stays in the order of the files. A name that is not a class here is refused by test_run_python_tests.
COSTS = {
    "test_web_mapmaker.TheCodeRuns": 110, "test_web_mapmaker.TheGroundAndTheSprites": 115, "test_web_mapmaker.ThePage": 20, "test_nginx_history.TheBlocksRun": 58, "test_web_admin.ThePagesOwnCode": 33,
    "test_bot_names.TheMutants": 30, "test_web_death_map.TheCards": 30, "test_nginx_stats.TheCacheRuns": 28, "test_web_map_files.ThePagesOwnCode": 26, "test_web_lobby_chat.TheChecksAreNotVacuous": 24,
    "test_web_mapmaker_play.ThePlayChecks": 20, "test_e2e_deadlines.Waits": 20, "test_web_home_chat.TheChecksAreNotVacuous": 85, "test_web_home_screen.TheChecksAreNotVacuous": 39, "test_web_home_games.TheChecksAreNotVacuous": 230,
}


def units(directory, package):
    """[(label, dotted name)] of what runs in one process: a class that defines tests, or the whole module when it has none."""
    found = []
    for path in sorted(glob.glob(os.path.join(directory, "test_*.py"))):
        module = os.path.splitext(os.path.basename(path))[0]
        with open(path, encoding="utf-8") as f:
            tree = ast.parse(f.read(), path)
        classes = [n.name for n in tree.body if isinstance(n, ast.ClassDef) and any(isinstance(b, ast.FunctionDef) and b.name.startswith("test") for b in n.body)]
        if classes:
            found += [("%s.%s" % (module, c), "%s.%s.%s" % (package, module, c)) for c in classes]
        else:
            found.append((module, "%s.%s" % (package, module)))
    return found


def start_order(work):
    """the indexes of work in the order in which the groups start: the longest (COSTS) first; what has no cost keeps the order of the files"""
    return sorted(range(len(work)), key=lambda i: -COSTS.get(work[i][0], 0))


def node_shim(folder):
    """Makes the `node` that the tests find first on the PATH: the node of this machine, started with --single-threaded. Returns the folder, or None (no node, or Windows, where the suites do not run).

    Why: a node 24 (V8 13.6; the one that macOS CI has) that EXITS (process.exit, or an uncaught exception) while one of V8's background threads is compiling with Maglev can hang forever: the main thread
    waits in node::Environment::Exit (DisposePlatform, uv_thread_join) for the worker threads, and a worker waits in Heap::AllocateRaw (CollectionBarrier::AwaitCollectionBackground) for a garbage collection
    that only the main thread can run. It happens to a run that fails early and quickly, which is what the checks of the broken copies do (hundreds of them), on 1 in 10 of some of them, and no
    timeout inside the process can help (the exit has stopped its JavaScript, and its watchdog thread with it). With --single-threaded V8 starts no background task, so no worker can wait for the main thread.
    The flag cannot go in NODE_OPTIONS (V8 options are refused there), so the tests get a `node` that adds it.
    """
    real = shutil.which("node")
    if not real or os.name == "nt":
        return None
    path = os.path.join(folder, "node")
    with open(path, "w", encoding="utf-8") as f:
        f.write('#!/bin/sh\nexec %s --single-threaded "$@"\n' % shlex.quote(real))
    os.chmod(path, 0o755)
    return folder


def run_unit(label, dotted, root, shim=None):
    started = time.monotonic()
    env = dict(os.environ)
    if shim:
        env["PATH"] = shim + os.pathsep + env.get("PATH", "")
    env.setdefault("PYTHONDONTWRITEBYTECODE", "1")
    done = subprocess.run([sys.executable, "-m", "unittest", dotted], capture_output=True, text=True, cwd=root, env=env)
    text = done.stdout + done.stderr
    ran = re.search(r"^Ran (\d+) tests? in", text, re.M)
    return {"label": label, "status": done.returncode, "tests": int(ran.group(1)) if ran else 0, "seconds": time.monotonic() - started, "output": text}


def main(argv):
    parser = argparse.ArgumentParser(description="Runs the python tests of tests/scripts side by side (see the top of this file).")
    parser.add_argument("--dir", default=os.path.dirname(os.path.abspath(__file__)), help="the folder of the test_*.py files (default: this script's folder)")
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 4, 8), help="how many classes at a time (default: the number of cores, at most 8)")
    parser.add_argument("--list", action="store_true", help="print what would run (one class or file per line) and exit")
    args = parser.parse_args(argv[1:])
    directory = os.path.abspath(args.dir)
    root = os.path.abspath(os.path.join(directory, "..", ".."))
    package = os.path.relpath(directory, root).replace(os.sep, ".")
    work = units(directory, package)
    if not work or args.jobs < 1:
        print("run_python_tests: no test_*.py in %s (or --jobs below 1)" % directory, file=sys.stderr)
        return 2
    if args.list:
        for label, dotted in work:
            print("%s\t%s" % (label, dotted))
        return 0
    started = time.monotonic()
    order = start_order(work)
    with tempfile.TemporaryDirectory() as shim_folder, concurrent.futures.ThreadPoolExecutor(max_workers=min(args.jobs, len(work))) as pool:
        shim = node_shim(shim_folder)
        done = list(pool.map(lambda i: run_unit(work[i][0], work[i][1], root, shim), order))
    results = [None] * len(work)
    for i, r in zip(order, done):
        results[i] = r
    failed = [r for r in results if r["status"] != 0]
    for r in results:
        print("%-62s %s  %3d tests  %5.1fs" % (r["label"], "ok    " if r["status"] == 0 else "FAILED", r["tests"], r["seconds"]))
    for r in failed:
        print("\n===== %s =====\n%s" % (r["label"], r["output"].rstrip()))
    print("\nRan %d tests in %d groups in %.1fs (side by side; one after the other they take %.1fs)" % (
        sum(r["tests"] for r in results), len(results), time.monotonic() - started, sum(r["seconds"] for r in results)))
    if failed:
        print("FAILED: %s" % ", ".join(r["label"] for r in failed))
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
