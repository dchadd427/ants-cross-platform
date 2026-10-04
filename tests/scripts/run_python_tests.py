#!/usr/bin/env python3
"""Runs the python tests of tests/scripts side by side (suite 5.2 of ./run_tests.sh, and the CI).

usage: run_python_tests.py [--dir DIR] [--jobs N] [--list]

The result is that of `python3 -m unittest discover -s tests/scripts -p 'test_*.py'`: every test runs and the exit status is 1 when any failed. What differs is the time: every
test CLASS of every test_*.py file runs in a process of its own, N at a time (N = the number of cores, at most 8), so that the slowest class and not the sum of all of them
decides. The output is a line for each class, in the order of the files, the full output of the classes that failed, and a total. The classes share nothing (each makes its
folders under its own temporary folder), which is what lets them run together. A file without a class that has tests runs as a whole (an import error then shows up).
"""
import argparse
import ast
import concurrent.futures
import glob
import os
import re
import subprocess
import sys
import time


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


def run_unit(label, dotted, root):
    started = time.monotonic()
    env = dict(os.environ)
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
    with concurrent.futures.ThreadPoolExecutor(max_workers=min(args.jobs, len(work))) as pool:
        results = list(pool.map(lambda unit: run_unit(unit[0], unit[1], root), work))
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
