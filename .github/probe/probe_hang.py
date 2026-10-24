#!/usr/bin/env python3
"""Throwaway probe (branch probe/macos-hang only): loops the broken-copy node checks of test_web_mapmaker_play and test_web_death_map side by side and, on a hang, dumps what the macOS tools say."""
import json, os, random, shutil, subprocess, sys, tempfile, threading, time, collections
REPO = os.getcwd()
sys.path.insert(0, REPO)
sys.path.insert(0, os.path.join(REPO, "tests", "scripts"))
if os.environ.get("PROBE_SHIM") == "1":
    import run_python_tests as rpt
    sys.path.insert(0, os.path.join(REPO, "tests", "scripts"))
    SHIM = tempfile.mkdtemp(prefix="shim-")
    rpt.node_shim(SHIM)
    os.environ["PATH"] = SHIM + os.pathsep + os.environ["PATH"]
NODE = shutil.which("node")
FRONT = os.path.join(REPO, "web", "front")
PLAY_CHECK = os.path.join(REPO, "tests", "scripts", "web_mapmaker_play_check.js")
BAIL = os.path.join(REPO, "tests", "scripts", "web_mapmaker_bail.js")
DM_CHECK = os.path.join(REPO, "tests", "scripts", "web_death_map_check.js")
HIST = os.path.join(REPO, "web", "history_page.js")
LIMIT = float(os.environ.get("PROBE_LIMIT", "60"))
WORKERS = int(os.environ.get("PROBE_WORKERS", "4"))
DEADLINE = time.monotonic() + float(os.environ.get("PROBE_MINUTES", "20")) * 60
HANG_NAMES = ("play_name_as_html", "play_rows_columns_swapped", "game_strip_not_first")
lock = threading.Lock()
stats = collections.Counter()
durations = []
hangs = []

def sh(cmd, t=40):
    try:
        r = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=t)
        return (r.stdout + r.stderr)
    except Exception as e:
        return "ERR %r" % (e,)

def say(*a):
    with lock:
        print(*a, flush=True)

def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()

def jobs():
    out = []
    spec = json.load(open(os.path.join(REPO, "tests", "data", "mapmaker_play_mutants.json"), encoding="utf-8"))
    for m in spec["mutants"]:
        if m["test"].startswith("node tests/scripts/web_mapmaker_play_check.js"):
            out.append(("play", m["id"], m))
    import tests.scripts.test_web_death_map as dm
    for why, (old, new) in dm.TheCards.MUTANTS.items():
        out.append(("dm", why, (old, new, "cards")))
    for why, (old, new) in dm.TheStatsPanel.MUTANTS.items():
        out.append(("dm", why, (old, new, "stats")))
    return out

def diagnose(p, label, started):
    info = ["==== HANG %s after %.1fs pid=%d poll=%r" % (label, time.monotonic() - started, p.pid, p.poll())]
    info.append(sh("ps -axo pid,ppid,pgid,stat,etime,time,command | egrep -i 'node|python|PID' | grep -v egrep"))
    info.append("children: " + sh("pgrep -lP %d" % p.pid))
    info.append(sh("lsof -p %d 2>&1 | head -60" % p.pid))
    info.append(sh("sample %d 3 2>&1 | head -150" % p.pid, 30))
    info.append(sh("uptime; vm_stat | head -4"))
    say("\n".join(info))

def run_one(kind, name, payload, tmp):
    if kind == "play":
        m = payload
        fname = os.path.basename(m["file"])
        text = read(*m["file"].split("/"))
        with open(os.path.join(tmp, fname), "w", encoding="utf-8", newline="") as f:
            f.write(text.replace(m["old"], m["new"]))
        args = [NODE, "--require", BAIL, PLAY_CHECK, "--front", tmp]
        restore = lambda: shutil.copy(os.path.join(FRONT, fname), os.path.join(tmp, fname))
    else:
        old, new, which = payload
        if which == "cards":
            text = read("web", "death_map.js")
            path = os.path.join(tmp, "death_map.js")
            shutil.copy(HIST, os.path.join(tmp, "history_page.js"))
            args = [NODE, DM_CHECK, path, HIST]
        else:
            text = read("web", "history_page.js")
            path = os.path.join(tmp, "history_page.js")
            args = [NODE, DM_CHECK, os.path.join(REPO, "web", "death_map.js"), path]
        with open(path, "w", encoding="utf-8") as f:
            f.write(text.replace(old, new))
        restore = lambda: None
    started = time.monotonic()
    p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    label = "%s:%s" % (kind, name)
    try:
        out, err = p.communicate(timeout=LIMIT)
        dt = time.monotonic() - started
        with lock:
            stats["runs"] += 1
            stats["rc%d" % (p.returncode if p.returncode in (0, 1) else 99)] += 1
            durations.append((dt, label))
            if p.returncode == 0:
                stats["accepted"] += 1
        if p.returncode == 0:
            say("ACCEPTED (not a hang): " + label)
        if dt > 10:
            say("slow %.1fs %s out=%d err=%d" % (dt, label, len(out), len(err)))
    except subprocess.TimeoutExpired as e:
        with lock:
            stats["runs"] += 1
            stats["HANGS"] += 1
            hangs.append(label)
        diagnose(p, label, started)
        say("collected so far: stdout=%r stderr=%r" % (len(e.stdout or b""), len(e.stderr or b"")))
        say("tail stdout: %r" % ((e.stdout or b"")[-300:],))
        p.kill()
        try:
            p.communicate(timeout=10)
        except Exception as e2:
            say("after kill: %r" % (e2,))
    finally:
        restore()

def worker(i, work):
    tmp = tempfile.mkdtemp(prefix="probe%d-" % i)
    for n in os.listdir(FRONT):
        if n.endswith(".js"):
            shutil.copy(os.path.join(FRONT, n), os.path.join(tmp, n))
    rnd = random.Random(i)
    while time.monotonic() < DEADLINE:
        # favour the three that hung, so that the tail is sampled often
        pool = [w for w in work if w[1] in HANG_NAMES] * 3 + work
        kind, name, payload = rnd.choice(pool)
        run_one(kind, name, payload, tmp)

def main():
    say(sh("sw_vers; uname -a; sysctl -n hw.ncpu machdep.cpu.brand_string; node --version; python3 --version; ulimit -a | head -12"))
    work = jobs()
    say("jobs: %d, workers %d, limit %.0fs" % (len(work), WORKERS, LIMIT))
    ts = [threading.Thread(target=worker, args=(i, work)) for i in range(WORKERS)]
    for t in ts: t.start()
    for t in ts: t.join()
    durations.sort(reverse=True)
    say("SUMMARY %s" % dict(stats))
    say("slowest: " + "; ".join("%.1f %s" % d for d in durations[:10]))
    say("hangs: %r" % hangs)
    return 1 if hangs else 0

if __name__ == "__main__":
    sys.exit(main())
