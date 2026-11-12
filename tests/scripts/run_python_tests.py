#!/usr/bin/env python3
"""Runs the python tests of tests/scripts side by side (suite 5.2 of ./run_tests.sh, and the CI).

usage: run_python_tests.py [--dir DIR] [--jobs N] [--list] [--shard K/N] [--skip-linux-only]

The result is that of `python3 -m unittest discover -s tests/scripts -p 'test_*.py'`: every test runs and the exit status is 1 when any failed. What differs is the time: every
test CLASS of every test_*.py file runs in a process of its own, N at a time (N = the number of cores, at most 8), so that the slowest class and not the sum of all of them
decides (the long ones, listed in COSTS, start first). The output is a line for each class, in the order of the files, the full output of the classes that failed, and a total. The classes share nothing (each makes its
folders under its own temporary folder), which is what lets them run together. A file without a class that has tests runs as a whole (an import error then shows up).

The CI splits the work two ways, and a plain run (no option) is still every test:
  --shard K/N          only the groups of slice K of N (K from 1): the groups go, the most expensive first, each to the slice that has the least so far (COSTS), so the N slices take about the same
                       time, and every group is in exactly one slice. The Linux job runs all the slices (its python legs); together they are every test.
  --skip-linux-only    leave out the groups of LINUX_ONLY (see there): the macOS job runs the rest, the Linux job runs them with everything else.
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


# What each group costs, in seconds of a run on the CI's Linux machine (four groups side by side), rounded; the groups of 5 seconds or more, the longest first (a group that is not here counts 1). The
# order of the run starts the longest first, so that the last one to start is a short one and a runner with three cores does not begin a class of two minutes when the others are nearly done
# (the output stays in the order of the files); --shard shares the groups between its slices by the same numbers. A name that is not a class here is refused by test_run_python_tests.
COSTS = {
    "test_web_home_games.TheChecksAreNotVacuous": 537, "test_web_home_chat.TheChecksAreNotVacuous": 462, "test_web_mapmaker.TheGroundAndTheSprites": 387,
    "test_web_mapmaker.TheCodeRuns": 157, "test_nginx_history.TheBlocksRun": 60, "test_web_home_screen.TheChecksAreNotVacuous": 58, "test_web_map_files.ThePagesOwnCode": 47,
    "test_web_replay_open.TheChecksAreNotVacuous": 45, "test_web_admin.ThePagesOwnCode": 44, "test_web_mapmaker_play.ThePlayChecks": 42, "test_bot_names.TheMutants": 35, "test_web_mapmaker.ThePage": 30,
    "test_nginx_stats.TheCacheRuns": 29, "test_web_death_map.TheCards": 28, "test_web_game_menu.TheMutants": 23, "test_web_lobby_chat.TheChecksAreNotVacuous": 23,
    "test_web_lobby_net.TheBudgetChecksAreNotVacuous": 22, "test_web_rematch.TheChecksAreNotVacuous": 20, "test_e2e_deadlines.Waits": 18,
    "test_hub_login.TheCallbackWhenDiscordGoesWrong": 18, "test_run_tests.ParallelResources": 17, "test_web_shapes.TheMutantsOfTheTable": 16,
    "test_web_lobby_rules.TheCheckIsNotVacuous": 16, "test_web_lobby.TheCheckIsNotVacuous": 15, "test_nginx_live.TheBlocksRun": 15, "test_e2e_deadlines.Watchdog": 14,
    "test_map_atlas.TheAtlas": 13, "test_web_without_docker.TheRealDockerfileReplayed": 13, "test_changelog_to_html.TheScriptRunByNode": 13,
    "test_web_shapes.TheMutantsOfThePicker": 12, "test_web_lobby_net.ThePublicRoomChecksAreNotVacuous": 12, "test_nginx_replays.TheBlocksRun": 11, "test_hub_ws.TheFuzz": 11,
    "test_nginx_lobbies.TheCacheRuns": 10, "test_e2e_deadlines.TheScriptCounts": 10, "test_hub_login.TheCallback": 10,
    "test_hub_login.TheSocket": 10, "test_nginx_lobbies.TheServerHangsRuns": 10, "test_measure_tick_rate.Program": 10, "test_nginx_maps.TheBlocksRun": 8,
    "test_nginx_hub_login.TheBlocksRun": 8, "test_hub_login.TheForget": 8, "test_web_map_picker.ThePickersOwnCode": 8, "test_hub_login.TheProcess": 7,
    "test_web_home_chat.TheCodeRuns": 7, "test_nginx_lobbies.TheServerFailsRuns": 7, "test_hub_login.TheDiscordClient": 7, "test_hub_server.TheProcess": 7,
    "test_nginx_stats.TheRealServerBehindTheFile": 7, "test_hub_server.TheTimers": 6, "test_mutate_tool.Project": 6, "test_mutate_tool.WithABuildStep": 6,
    "test_community_maps.TheList": 6, "test_run_tests.ParallelRunner": 6, "test_web_map_maker_link.TheChecksAreNotVacuous": 6, "test_nginx_stats.TheBlocksRun": 6,
    "test_ci_light.Decision": 6, "test_cmake_version.CcacheSupport": 5,
}

# The groups that the macOS job leaves to Linux (--skip-linux-only): page tests only. They run the JavaScript of the site's pages with node against the files of the checkout (or build the map
# maker's atlas from them), start no server and no socket of their own, and assert no time, so nothing in them depends on the operating system. Most are the "checks are not vacuous" groups
# (a check on broken copies of the scripts must fail, which it does on every system), and the real runs of the same checks stay on macOS wherever they are classes of their own (TheCodeRuns,
# TheChecksThatNodeRuns: among them the home chat against a fake hub on a real socket). Every other group runs on both: what the macOS runners taught the tests (servers, sockets, HTTP, clocks and
# timers, the watchdog of a node that does not end in test_web_mapmaker_play, the long output of a node through a pipe in test_web_mapmaker.TheCodeRuns, file names and paths, bash 3.2, compilers).
# test_run_python_tests allows page tests only in this list, and a group that is new runs on macOS until it is named here.
LINUX_ONLY = {
    "test_web_home_games.TheChecksAreNotVacuous", "test_web_home_chat.TheChecksAreNotVacuous", "test_web_home_screen.TheChecksAreNotVacuous", "test_web_lobby.TheCheckIsNotVacuous",
    "test_web_lobby_chat.TheChecksAreNotVacuous", "test_web_lobby_net.TheBudgetChecksAreNotVacuous", "test_web_lobby_net.ThePublicRoomChecksAreNotVacuous",
    "test_web_lobby_rules.TheCheckIsNotVacuous", "test_web_rematch.TheChecksAreNotVacuous", "test_web_map_maker_link.TheChecksAreNotVacuous", "test_web_game_menu.TheMutants",
    "test_web_shapes.TheMutantsOfTheTable", "test_web_shapes.TheMutantsOfThePicker", "test_bot_names.TheMutants", "test_web_mapmaker.TheGroundAndTheSprites", "test_web_mapmaker.ThePage",
    "test_web_map_files.ThePagesOwnCode", "test_web_admin.ThePagesOwnCode", "test_web_death_map.TheCards",
    "test_web_player_name.TheChecksAreNotVacuous",
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


def shard_of(work, k, n):
    """The groups of work that slice k of n (k from 1) runs, in the order of the files. The groups go in the order of the start (the longest first), each to the slice that has the least so far
    (the lower number when equal; a group without a cost counts 1), so that every group is in exactly one slice and the slices cost about the same. It depends on the groups and COSTS only, so
    the jobs of a run that each take their slice agree without talking."""
    load = [0] * n
    mine = []
    for i in start_order(work):
        s = min(range(n), key=lambda j: (load[j], j))
        load[s] += COSTS.get(work[i][0], 1)
        if s == k - 1:
            mine.append(work[i])
    return [w for w in work if w in mine]


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
    parser.add_argument("--shard", metavar="K/N", help="run only slice K of N (K from 1) of the groups: the slices take about the same time and together they are every group")
    parser.add_argument("--skip-linux-only", action="store_true", help="leave out the groups of LINUX_ONLY (the page tests that the macOS job leaves to Linux)")
    args = parser.parse_args(argv[1:])
    directory = os.path.abspath(args.dir)
    root = os.path.abspath(os.path.join(directory, "..", ".."))
    package = os.path.relpath(directory, root).replace(os.sep, ".")
    work = units(directory, package)
    if not work or args.jobs < 1:
        print("run_python_tests: no test_*.py in %s (or --jobs below 1)" % directory, file=sys.stderr)
        return 2
    if args.skip_linux_only:
        work = [w for w in work if w[0] not in LINUX_ONLY]
    shard = None
    if args.shard is not None:
        picked = re.fullmatch(r"(\d+)/(\d+)", args.shard)
        shard = (int(picked.group(1)), int(picked.group(2))) if picked else None
        if not shard or not 1 <= shard[0] <= shard[1] or shard[1] > len(work):
            print("run_python_tests: --shard %s is not K/N with 1 <= K <= N <= the number of groups (%d)" % (args.shard, len(work)), file=sys.stderr)
            return 2
        work = shard_of(work, *shard)
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
    which = (" of slice %d/%d" % shard) if shard else ""
    print("\nRan %d tests in %d groups%s in %.1fs (side by side; one after the other they take %.1fs)" % (
        sum(r["tests"] for r in results), len(results), which, time.monotonic() - started, sum(r["seconds"] for r in results)))
    if failed:
        print("FAILED: %s" % ", ".join(r["label"] for r in failed))
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
