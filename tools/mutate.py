#!/usr/bin/env python3
"""Mutation runner: breaks the code on purpose, one change at a time, and shows which tests notice (the deep tier of docs/WORKFLOW.md).

usage: mutate.py SPEC.json [--root DIR] [--build-dir DIR] [--jobs N] [--timeout SECONDS] [--keep-going] [--only ID ...] [--report FILE] [--check]
                          [--baseline-every N]

SPEC.json is {"mutants": [...]} (or the list itself); an optional "defaults" object gives every mutant the keys that it does not set. A mutant is
  id      a name, unique in the file
  file    the source to change, relative to the root
  old     the text to replace: it must occur in the file exactly once, else the mutant is an error (and nothing is changed)
  new     what takes its place
  build   the CMake targets to build after the change (a name or a list; leave it out for a project that needs no build)
  test    the command that must notice (run by the shell in the root; a non-zero exit means: caught)
  expect  optional: a text that the output of the failing run must contain (the id of the test that should catch it); without it the mutant is caught
          anywhere ("caught-elsewhere" in the report)
The optional "baseline" (an object or a list of objects with "build" and "test") says what proves the UNMUTATED tree; without it, every different build and test
command of the mutants is used.

For each mutant, one at a time: the file is changed, the targets are built (cmake --build BUILD_DIR --target ...), the test command runs, and the file is
restored, ALWAYS: after an error, a timeout, Ctrl-C or SIGTERM too (a backup FILE.mutbak is kept while a file is changed, and put back by the next start if this
program was killed outright). Results: caught, caught-elsewhere, survived (the tests did not notice: a gap), build-error (the mutant does not compile: not a
valid mutant), timeout, error (a mutant that cannot be applied). A table is printed and a JSON report is written (default SPEC.report.json, after every mutant).

BASELINES. A stale object of an earlier mutant (make compares file times in whole seconds on a Mac: a file restored within the second in which its mutant was
built is not built again) makes every later mutant "fail" or "crash" and nothing says so. So the unmutated tree is built and tested before the first mutant, after
every N mutants (--baseline-every, default 10; 0: only at the start and the end) and at the end, and the files are made newer than anything built from a mutant
before each build. A baseline that fails means that the results cannot be trusted: nothing is reported (no table, "valid": false in the report) and the exit
status is 2. There is no way to switch the baselines off.

By default the run stops at the first mutant that cannot be judged (error, build-error, timeout) and the rest is "not-run"; --keep-going goes on.
--timeout is the limit of one mutant or one baseline command (build and test together, default 1800 s). --check only validates the file. Exit status: 0 every
mutant caught, 1 at least one survived, 2 at least one could not be judged, a baseline failed, or the file or the arguments are wrong.
"""
import argparse
import json
import os
import re
import signal
import subprocess
import sys
import time

BACKUP_SUFFIX = ".mutbak"
OUTPUT_TAIL = 6000                      # characters of output kept in the report
INVALID = ("error", "build-error", "timeout")


class Terminated(BaseException):
    """Raised by the signal handlers so that the `finally` blocks that restore the files run."""


def on_signal(signum, _frame):
    raise Terminated(signum)


class SpecError(Exception):
    pass


# ---- the spec --------------------------------------------------------------------------------------------------------------------------------

def as_targets(value, where):
    if value in (None, "", []):
        return []
    if isinstance(value, str):
        return value.split()
    if isinstance(value, list) and all(isinstance(t, str) and t for t in value):
        return list(value)
    raise SpecError("%s: build must be a target name or a list of target names" % where)


def load_spec(path):
    """(mutants, baseline commands or None)"""
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    except OSError as e:
        raise SpecError("cannot read %s: %s" % (path, e.strerror))
    except ValueError as e:
        raise SpecError("%s is not valid JSON: %s" % (path, e))
    if isinstance(data, list):
        data = {"mutants": data}
    if not isinstance(data, dict) or not isinstance(data.get("mutants"), list) or not data["mutants"]:
        raise SpecError('%s: expected {"mutants": [ ... ]} with at least one mutant' % path)
    defaults = data.get("defaults", {})
    if not isinstance(defaults, dict):
        raise SpecError("%s: defaults must be an object" % path)
    mutants = []
    seen = set()
    for index, raw in enumerate(data["mutants"], 1):
        if not isinstance(raw, dict):
            raise SpecError("mutant %d: must be an object" % index)
        m = dict(defaults)
        m.update(raw)
        where = "mutant %d (%s)" % (index, m.get("id", "no id"))
        for key in ("id", "file", "old", "test"):
            if not isinstance(m.get(key), str) or not m[key]:
                raise SpecError("%s: %s is missing (a non-empty text)" % (where, key))
        if not isinstance(m.get("new"), str):
            raise SpecError("%s: new is missing (a text; empty deletes the old text)" % where)
        if m["id"] in seen:
            raise SpecError("%s: the id is used twice" % where)
        seen.add(m["id"])
        if os.path.isabs(m["file"]) or m["file"].replace("\\", "/").split("/")[0] == ".." or "/../" in m["file"].replace("\\", "/"):
            raise SpecError("%s: file must be a path inside the root, found %r" % (where, m["file"]))
        if m["old"] == m["new"]:
            raise SpecError("%s: old and new are the same text" % where)
        if "expect" in m and m["expect"] is not None and not isinstance(m["expect"], str):
            raise SpecError("%s: expect must be a text" % where)
        m["build"] = as_targets(m.get("build"), where)
        mutants.append(m)
    declared = None
    if "baseline" in data:
        items = data["baseline"] if isinstance(data["baseline"], list) else [data["baseline"]]
        declared = []
        for item in items:
            if not isinstance(item, dict) or not isinstance(item.get("test"), str) or not item["test"]:
                raise SpecError('%s: every "baseline" needs a "test" command (and may have a "build")' % path)
            declared.append({"build": as_targets(item.get("build"), "baseline"), "test": item["test"]})
        if not declared:
            raise SpecError('%s: "baseline" is empty' % path)
    return mutants, declared


def baseline_commands(mutants, declared):
    """What proves the unmutated tree: the declared baseline, else every different (build targets, test command) of the mutants, in the order they first appear."""
    if declared:
        return declared
    found = []
    for m in mutants:
        item = {"build": m["build"], "test": m["test"]}
        if item not in found:
            found.append(item)
    return found


# ---- the files -------------------------------------------------------------------------------------------------------------------------------

ACTIVE = {}                             # path -> original bytes, for every file that is changed right now
SIGNALS = (signal.SIGINT, signal.SIGTERM) + ((signal.SIGHUP,) if hasattr(signal, "SIGHUP") else ())


def bump_mtime(path):
    """A modification time later than any object that was built from the file before: whole-second file systems and old make programs would
    otherwise take the object of the mutant for current after the file was restored."""
    t = max(time.time(), os.stat(path).st_mtime) + 2.0
    os.utime(path, (t, t))


def write_bytes(path, data):
    with open(path, "wb") as f:
        f.write(data)
    bump_mtime(path)


def restore_all():
    """Put every changed file back. A signal that arrives meanwhile waits (it would stop the restore half way)."""
    previous = {sig: signal.signal(sig, signal.SIG_IGN) for sig in SIGNALS}
    try:
        for path, original in list(ACTIVE.items()):
            write_bytes(path, original)
            try:
                os.remove(path + BACKUP_SUFFIX)
            except OSError:
                pass
            del ACTIVE[path]
    finally:
        for sig, handler in previous.items():
            signal.signal(sig, handler)


def recover(root, files):
    """A previous run that was killed outright leaves FILE.mutbak next to a changed file: put the original back before anything else."""
    for rel in sorted(set(files)):
        path = os.path.join(root, rel)
        backup = path + BACKUP_SUFFIX
        if os.path.isfile(backup):
            with open(backup, "rb") as f:
                original = f.read()
            write_bytes(path, original)
            os.remove(backup)
            print("restored %s from %s (an earlier run was interrupted while it was changed)" % (rel, os.path.basename(backup)))


def mutate_text(root, m):
    """(path, (original bytes, mutated bytes), None) for the mutant's file, or (None, None, a problem text)."""
    path = os.path.join(root, m["file"])
    try:
        with open(path, "rb") as f:
            original = f.read()
    except OSError as e:
        return None, None, "%s: %s" % (m["file"], e.strerror)
    try:
        text = original.decode("utf-8")
    except UnicodeDecodeError:
        return None, None, "%s is not UTF-8 text" % m["file"]
    count = text.count(m["old"])
    if count != 1:
        return None, None, "the old text occurs %d times in %s (it must occur exactly once)" % (count, m["file"])
    return path, (original, text.replace(m["old"], m["new"], 1).encode("utf-8")), None


# ---- running commands ------------------------------------------------------------------------------------------------------------------------

def kill_group(proc):
    try:
        if os.name == "posix":
            os.killpg(proc.pid, signal.SIGKILL)
        else:
            proc.kill()
    except (OSError, ProcessLookupError):
        pass


def run(command, cwd, limit):
    """(exit status or None when the time ran out, output text). Everything the command started is killed when the time is out or this program is interrupted."""
    proc = subprocess.Popen(command, shell=isinstance(command, str), cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                            start_new_session=(os.name == "posix"))
    try:
        out, _ = proc.communicate(timeout=max(limit, 0.1))
        return proc.returncode, out.decode("utf-8", "replace")
    except subprocess.TimeoutExpired:
        kill_group(proc)
        out, _ = proc.communicate()
        return None, out.decode("utf-8", "replace")
    except BaseException:
        kill_group(proc)
        proc.wait()
        raise


def first_failure_line(output):
    """The line that says what failed: an assertion first, then a FAIL / FAILED marker, then any line with a word like error or expected (a test's own title may hold that word), else the last line."""
    lines = output.splitlines()
    for pattern in (r"Assertion failed|\bAssertionError\b", r"\bFAIL(ED|URE)?\b[:!]?", r"(?i)\b(assert\w*|fail\w*|error|mismatch|expected)\b"):
        for line in lines:
            if re.search(pattern, line):
                return line.strip()[:140]
    tail_lines = [l.strip() for l in lines if l.strip()]
    return tail_lines[-1][:140] if tail_lines else ""


def tail(text):
    return text if len(text) <= OUTPUT_TAIL else "..." + text[-OUTPUT_TAIL:]


BUILD_GAP = 1.1                         # seconds between two builds: make 3.81 (macOS) compares modification times in whole seconds, so an object that is
LAST_BUILD_END = time.monotonic()       # rebuilt within the second in which its program was linked would not be linked again (the first build counts from the start)


def build(root, build_dir, targets, jobs, limit):
    """Runs the build; returns what run() returns. The gap is kept first, so that every file the build writes is newer, in whole seconds, than the last build's."""
    global LAST_BUILD_END
    wait = BUILD_GAP - (time.monotonic() - LAST_BUILD_END)
    if wait > 0:
        time.sleep(wait)
        limit -= wait
    try:
        return run(["cmake", "--build", build_dir, "--target", *targets, "-j", str(jobs)], root, limit)
    finally:
        LAST_BUILD_END = time.monotonic()


# ---- baselines -------------------------------------------------------------------------------------------------------------------------------

def failure_of(step, status, output):
    return {"step": step, "exit_code": status, "detail": "timeout" if status is None else first_failure_line(output), "output_tail": tail(output)}


def baseline(root, args, commands, when):
    """Builds and tests the unmutated tree: {"when", "ok", "seconds", "failure"}."""
    started = time.monotonic()
    entry = {"when": when, "ok": True, "seconds": 0.0, "failure": None}
    for item in commands:
        if item["build"]:
            status, output = build(root, args.build_dir, item["build"], args.jobs, args.timeout)
            if status != 0:
                entry.update(ok=False, failure=failure_of("build " + " ".join(item["build"]), status, output))
                break
        status, output = run(item["test"], root, args.timeout)
        if status != 0:
            entry.update(ok=False, failure=failure_of("test " + item["test"], status, output))
            break
    entry["seconds"] = round(time.monotonic() - started, 1)
    return entry


# ---- one mutant ------------------------------------------------------------------------------------------------------------------------------

def judge(root, m, args):
    result = {"id": m["id"], "file": m["file"], "build": m["build"], "test": m["test"], "expect": m.get("expect"), "status": "error", "exit_code": None,
              "seconds": 0.0, "detail": "", "output_tail": ""}
    started = time.monotonic()
    path, pair, problem = mutate_text(root, m)
    if problem:
        result["detail"] = problem
        return result
    original, mutated = pair
    deadline = started + args.timeout
    try:
        with open(path + BACKUP_SUFFIX, "wb") as f:                # crash insurance: the original, next to the file, until it is restored
            f.write(original)
        ACTIVE[path] = original
        write_bytes(path, mutated)
        if m["build"]:
            status, output = build(root, args.build_dir, m["build"], args.jobs, deadline - time.monotonic())
            if status is None:
                result.update(status="timeout", detail="the build took longer than %d s" % args.timeout, output_tail=tail(output))
                return result
            if status != 0:
                result.update(status="build-error", exit_code=status, detail=first_failure_line(output), output_tail=tail(output))
                return result
        status, output = run(m["test"], root, deadline - time.monotonic())
        if status is None:
            result.update(status="timeout", detail="no verdict within %d s (the mutant may loop; the command was killed)" % args.timeout, output_tail=tail(output))
        elif status == 0:
            result.update(status="survived", exit_code=0, detail="the test command passed: nothing noticed the change", output_tail=tail(output))
        else:
            matched = (not m.get("expect")) or (m["expect"] in output)
            result.update(status="caught" if matched else "caught-elsewhere", exit_code=status, output_tail=tail(output),
                          detail=first_failure_line(output) if matched else "failed, but the output has no %r: %s" % (m["expect"], first_failure_line(output)))
        return result
    finally:
        result["seconds"] = round(time.monotonic() - started, 1)
        restore_all()


def not_run(m):
    return {"id": m["id"], "file": m["file"], "build": m["build"], "test": m["test"], "expect": m.get("expect"), "status": "not-run", "exit_code": None, "seconds": 0.0,
            "detail": "an earlier mutant could not be judged (use --keep-going)", "output_tail": ""}


# ---- report ----------------------------------------------------------------------------------------------------------------------------------

def summary_of(results, total):
    counts = {k: 0 for k in ("caught", "caught-elsewhere", "survived", "build-error", "timeout", "error", "not-run")}
    for r in results:
        counts[r["status"]] += 1
    return {"mutants": total, "caught": counts["caught"] + counts["caught-elsewhere"], "caught_elsewhere": counts["caught-elsewhere"], "survived": counts["survived"],
            "invalid": counts["build-error"] + counts["timeout"] + counts["error"], "not_run": counts["not-run"]}


def write_report(path, args, state):
    report = {"spec": os.path.basename(args.spec), "root": args.root, "started": state["started"], "valid": state["valid"], "summary": summary_of(state["results"], state["total"]),
              "baselines": state["baselines"], "results": state["results"]}
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        json.dump(report, f, indent=1)
        f.write("\n")
    os.replace(tmp, path)


def print_table(results):
    rows = [("id", "file", "result", "time", "detail")]
    for r in results:
        rows.append((r["id"], r["file"], r["status"].upper(), ("%.1fs" % r["seconds"]) if r["status"] != "not-run" else "-", r["detail"][:90]))
    widths = [min(max(len(row[i]) for row in rows), cap) for i, cap in enumerate((40, 48, 16, 8, 90))]
    for n, row in enumerate(rows):
        line = "  ".join(cell[:widths[i]].ljust(widths[i]) for i, cell in enumerate(row[:4])) + "  " + row[4]
        print(line.rstrip())
        if n == 0:
            print("  ".join("-" * w for w in widths))


def refuse(entry, untrusted):
    f = entry["failure"]
    print("mutate: THE BASELINE FAILED (%s): %s: %s" % (entry["when"], f["step"], f["detail"] or "no output"), file=sys.stderr)
    if f["output_tail"]:
        print("\n".join("    " + l for l in f["output_tail"].splitlines()[-12:]), file=sys.stderr)
    if untrusted:
        print("mutate: the unmutated tree fails, so the verdicts since the last good baseline cannot be trusted (%s); nothing is reported (the JSON report says valid: false)"
              % ", ".join(untrusted), file=sys.stderr)
    else:
        print("mutate: the unmutated tree fails before any change: nothing was changed; fix that first", file=sys.stderr)


# ---- main ------------------------------------------------------------------------------------------------------------------------------------

def main(argv):
    parser = argparse.ArgumentParser(description="Mutation runner: one change at a time, which tests notice? (see the top of this file)")
    parser.add_argument("spec", help="the JSON file that lists the mutants")
    parser.add_argument("--root", default=".", help="the project's root folder (default: the current folder)")
    parser.add_argument("--build-dir", default="build", help="the CMake build folder, relative to the root (default: build)")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4, help="parallel build jobs (default: the number of cores)")
    parser.add_argument("--timeout", type=float, default=1800.0, help="seconds for one mutant or one baseline command (default: 1800)")
    parser.add_argument("--keep-going", action="store_true", help="go on after a mutant that cannot be judged (error, build-error, timeout)")
    parser.add_argument("--only", nargs="+", metavar="ID", help="run only these mutants")
    parser.add_argument("--report", help="where the JSON report goes (default: SPEC with .report.json instead of .json)")
    parser.add_argument("--check", action="store_true", help="only validate the file: every old text must occur exactly once; nothing is built or run")
    parser.add_argument("--baseline-every", type=int, default=10, metavar="N", help="build and test the unmutated tree after every N mutants (default 10; 0: only at the start and the end)")
    args = parser.parse_args(argv[1:])
    if args.jobs < 1 or args.timeout <= 0 or args.baseline_every < 0:
        print("--jobs and --timeout must be positive, --baseline-every not negative", file=sys.stderr)
        return 2

    try:
        mutants, declared = load_spec(args.spec)
    except SpecError as e:
        print("mutate: %s" % e, file=sys.stderr)
        return 2
    if args.only:
        known = {m["id"] for m in mutants}
        missing = [i for i in args.only if i not in known]
        if missing:
            print("mutate: no mutant with the id %s in %s" % (", ".join(missing), args.spec), file=sys.stderr)
            return 2
        mutants = [m for m in mutants if m["id"] in set(args.only)]
    if not os.path.isdir(args.root):
        print("mutate: the root %s is not a folder" % args.root, file=sys.stderr)
        return 2
    report_path = args.report or (re.sub(r"\.json$", "", args.spec) + ".report.json")
    commands = baseline_commands(mutants, declared)

    for sig in SIGNALS:
        signal.signal(sig, on_signal)

    recover(args.root, [m["file"] for m in mutants])
    problems = []
    for m in mutants:                                               # every change must apply before anything is built
        path, pair, problem = mutate_text(args.root, m)
        if problem:
            problems.append("%s: %s" % (m["id"], problem))
    if args.check:
        for p in problems:
            print("ERROR  " + p)
        print("%d mutants, %d that cannot be applied; the baseline is %d command(s)" % (len(mutants), len(problems), len(commands)))
        return 2 if problems else 0
    if problems and not args.keep_going:
        for p in problems:
            print("ERROR  " + p, file=sys.stderr)
        print("mutate: nothing was run: fix the file, or use --keep-going to run the others", file=sys.stderr)
        return 2

    state = {"started": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "valid": True, "results": [], "baselines": [], "total": len(mutants)}
    results = state["results"]
    t0 = time.monotonic()
    stopped = False
    trusted_until = 0                                               # the results before this index come before a good baseline
    try:
        print("baseline (before any change) ...", flush=True)
        first = baseline(args.root, args, commands, "start")
        state["baselines"].append(first)
        if not first["ok"]:
            state["valid"] = False
            write_report(report_path, args, state)
            refuse(first, [])
            return 2
        print("        ok  %.1fs" % first["seconds"], flush=True)
        for number, m in enumerate(mutants, 1):
            if stopped:
                results.append(not_run(m))
                continue
            print("[%d/%d] %s ..." % (number, len(mutants), m["id"]), flush=True)
            r = judge(args.root, m, args)
            results.append(r)
            print("        %s  %.1fs  %s" % (r["status"].upper(), r["seconds"], r["detail"][:100]), flush=True)
            write_report(report_path, args, state)
            if r["status"] in INVALID and not args.keep_going:
                stopped = True
            if args.baseline_every and number % args.baseline_every == 0 and number < len(mutants) and not stopped:
                print("baseline (after %d mutants) ..." % number, flush=True)
                entry = baseline(args.root, args, commands, "after %d mutants" % number)
                state["baselines"].append(entry)
                if not entry["ok"]:
                    state["valid"] = False
                    write_report(report_path, args, state)
                    refuse(entry, [x["id"] for x in results[trusted_until:]])
                    return 2
                trusted_until = len(results)
                print("        ok  %.1fs" % entry["seconds"], flush=True)
        print("baseline (the end) ...", flush=True)
        last = baseline(args.root, args, commands, "end")                                   # (the files are restored by judge(), so this builds the real code)
        state["baselines"].append(last)
        if not last["ok"]:
            state["valid"] = False
            write_report(report_path, args, state)
            refuse(last, [x["id"] for x in results[trusted_until:]])
            return 2
        print("        ok  %.1fs" % last["seconds"], flush=True)
    except Terminated as e:
        print("\nmutate: interrupted (signal %s): the changed file is restored" % e.args[0], file=sys.stderr)
        restore_all()
        state["valid"] = False
        write_report(report_path, args, state)
        return 130
    finally:
        restore_all()
    write_report(report_path, args, state)

    print()
    print_table(results)
    s = summary_of(results, len(mutants))
    print()
    print("mutants: %d, caught: %d%s, survived: %d, could not be judged: %d, not run: %d  (%.0f s)" % (
        s["mutants"], s["caught"], (" (%d elsewhere)" % s["caught_elsewhere"]) if s["caught_elsewhere"] else "", s["survived"], s["invalid"], s["not_run"], time.monotonic() - t0))
    print("baselines of the unmutated tree: %d, all passed" % len(state["baselines"]))
    print("report: %s" % report_path)
    if s["survived"]:
        return 1
    if s["invalid"] or s["not_run"]:
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
