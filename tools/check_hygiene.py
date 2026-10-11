#!/usr/bin/env python3
"""Repository hygiene check: fails (exit 1) when the repository gets worse. Pure python 3 standard library.

usage: check_hygiene.py [--root DIR] [--list]

Exit 0 when everything is in order, 1 when there are findings (one line each), 2 for a usage error. --list prints the current numbers that the ratchet tables below pin.
Run by the tools tier (tests/scripts/test_hygiene.py, so by ./run_tests.sh --tools and by CI). The checks:
  1. a document (docs/**/*.md, root *.md) is at most DOC_LIMIT bytes, except the pages of DOC_CEILINGS (ceilings that may only shrink);
  2. every docs/*.md and docs/<folder>/README.md is reachable from README.md by relative markdown links (a link to a folder reaches its README.md);
  3. no tracked text file names a private planning document, a commit trailer or a session link (FORBIDDEN), except the files of FORBIDDEN_ALLOWED and the vendored files of VENDORED_CODE;
  4. a test file has at most TEST_LINES lines and a production file at most PROD_LINES, except the files of LINE_CEILINGS (ceilings that may only shrink);
  5. at most ASSERT_TRUE_DEFINITIONS definitions of the macro ASSERT_TRUE;
  6. every .cpp under src/, tests/ and tools/ is named in a CMakeLists.txt, except CMAKE_EXCEPTIONS.
Ratchets: a number may never grow. When a page or file shrinks by more than the tolerance, the table must be lowered, so that the gain is kept.
"""
import argparse
import os
import re
import subprocess
import sys

DOC_LIMIT = 100 * 1024
DOC_SLACK = 0.05  # a page more than 5% below its ceiling: lower the ceiling
DOC_CEILINGS = {
    "docs/NETWORK_PORT.md": 318396,
    "docs/BOTS.md": 177480,
    "docs/GAME_REVERSE_ENGINEERING.md": 429658,
}

TEST_LINES = 2500
PROD_LINES = 1500
LINE_SLACK = 0.10  # a file more than 10% below its ceiling: lower the ceiling
LINE_CEILINGS = {
    "tests/test_server/test_server.cpp": 12276,
    "tests/test_net/test_lockstep.cpp": 11857,
    "tests/test_app/test_app_integration.cpp": 11827,
    "tests/test_app/test_network_app.cpp": 7123,
    "tests/test_net/test_lobby.cpp": 5508,
    "tests/test_app/test_view_fingerprint.cpp": 4347,
    "tests/test_app/test_hud_layout.cpp": 3457,
    "tests/test_app/test_start_menu_app.cpp": 3202,
    "tests/test_app/test_zoom_view.cpp": 3157,
    "tests/test_app/test_start_menu.cpp": 2943,
    "tests/test_app/test_wide_hud.cpp": 2710,
    "tests/test_ctl/test_ctl.cpp": 2625,
    "tests/test_server/test_rejoin.cpp": 2560,
    "tests/test_net/test_netgame.cpp": 2521,
    "src/ants_ai/standard_tasks.cpp": 3911,
    "web/shell.html": 3812,
    "src/ants_app/application.cpp": 3580,
    "tools/bot_arena.cpp": 2773,
    "tools/map_sweep.cpp": 2626,
    "src/ants_app/renderer.cpp": 2368,
    "src/ants_app/hud.cpp": 2009,
    "src/ants_sim/movement_system.cpp": 1787,
    "src/ants_net/session.cpp": 1571,
    "src/ants_net/netgame.cpp": 1565,
    "src/ants_sim/sim_engine.cpp": 1533,
}

ASSERT_TRUE_DEFINITIONS = 2  # tests/common/test_assert.hpp and the throwing ASSERT_TRUE of tests/e2e/e2e_framework.hpp: a third one is a new copy to remove

FORBIDDEN = re.compile(r"implementation_plan|ants-plans|DECISIONS\.md|claude\.ai/code|Co-Authored-By|Claude-Session|project thread|the other thread", re.I)
VENDORED_CODE = {"third_party/sqlite/sqlite3.c", "third_party/sqlite/sqlite3.h"}       # (the files of other people's code that are copied unchanged; our own files beside them are checked)
FORBIDDEN_ALLOWED = {"AGENTS.md", "docs/WORKFLOW.md", ".gitignore", "tools/check_hygiene.py", "tests/scripts/test_hygiene.py"}

CMAKE_EXCEPTIONS = set()

TEXT_SUFFIXES_SKIPPED = {".lvl"}


def tracked_files(root):
    """Relative paths (with /) of the tracked files: git's list when root is a checkout, else every file below root."""
    if os.path.exists(os.path.join(root, ".git")):
        done = subprocess.run(["git", "-C", root, "ls-files", "-z"], capture_output=True)
        if done.returncode == 0:
            return sorted(p for p in done.stdout.decode("utf-8", "replace").split("\0") if p and os.path.isfile(os.path.join(root, p)))
    found = []
    for base, dirs, names in os.walk(root):
        dirs[:] = [d for d in dirs if d != ".git"]
        for n in names:
            found.append(os.path.relpath(os.path.join(base, n), root).replace(os.sep, "/"))
    return sorted(found)


def read_bytes(root, rel):
    with open(os.path.join(root, rel), "rb") as f:
        return f.read()


def line_count(data):
    return data.count(b"\n") + (1 if data and not data.endswith(b"\n") else 0)


def is_doc(rel):
    return rel.endswith(".md") and (rel.startswith("docs/") or "/" not in rel)


def check_doc_sizes(root, files, limit=DOC_LIMIT, ceilings=None, slack=DOC_SLACK):
    ceilings = DOC_CEILINGS if ceilings is None else ceilings
    out = []
    for rel in files:
        if not is_doc(rel):
            continue
        size = os.path.getsize(os.path.join(root, rel))
        if rel in ceilings:
            if size > ceilings[rel]:
                out.append("%s: %d bytes is above its ceiling %d (the page may only shrink; split it)" % (rel, size, ceilings[rel]))
            elif size < ceilings[rel] * (1 - slack):
                out.append("%s: %d bytes is more than %d%% below its ceiling %d: lower the ceiling to %d" % (rel, size, round(slack * 100), ceilings[rel], size))
            if ceilings[rel] <= limit:
                out.append("%s: its ceiling %d is within the limit %d: remove it from the table" % (rel, ceilings[rel], limit))
        elif size > limit:
            out.append("%s: %d bytes is above the document limit %d (split it)" % (rel, size, limit))
    for rel in ceilings:
        if rel not in files:
            out.append("%s: in the ceiling table but not a tracked file: remove it from the table" % rel)
    return out


LINK = re.compile(r"\]\(\s*<?([^)\s>]+)")
REFDEF = re.compile(r"^\s{0,3}\[[^\]]+\]:\s*<?(\S+?)>?(?:\s|$)", re.M)


def links_of(root, rel, filesset):
    text = read_bytes(root, rel).decode("utf-8", "replace")
    text = re.sub(r"```.*?```", "", text, flags=re.S)
    here = os.path.dirname(rel)
    found = set()
    for target in LINK.findall(text) + REFDEF.findall(text):
        if re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", target) or target.startswith("#"):
            continue
        target = target.split("#")[0].split("?")[0]
        if not target:
            continue
        path = os.path.normpath(os.path.join(here, target)).replace(os.sep, "/")
        if path in filesset:
            found.add(path)
        elif path + "/README.md" in filesset:
            found.add(path + "/README.md")
    return found


def check_reachable(root, files, start="README.md"):
    filesset = set(files)
    if start not in filesset:
        return ["%s: missing (the start of the link check)" % start]
    seen = {start}
    todo = [start]
    while todo:
        cur = todo.pop()
        if not cur.endswith(".md"):
            continue
        for nxt in sorted(links_of(root, cur, filesset)):
            if nxt not in seen:
                seen.add(nxt)
                todo.append(nxt)
    out = []
    for rel in files:
        if not rel.endswith(".md") or not rel.startswith("docs/"):
            continue
        parts = rel.split("/")
        if len(parts) == 2 or parts[-1] == "README.md":
            if rel not in seen:
                out.append("%s: not reachable from README.md by relative links (link it from README.md or from a page README.md links)" % rel)
    return out


def looks_binary(data):
    return b"\0" in data[:8192]


def check_forbidden(root, files, allowed=FORBIDDEN_ALLOWED):
    out = []
    for rel in files:
        if rel in allowed or rel.startswith("previews/") or "/previews/" in rel or rel in VENDORED_CODE:       # (vendored code is not ours: SQLite's own comments say "the other thread")
            continue
        if os.path.splitext(rel)[1].lower() in TEXT_SUFFIXES_SKIPPED:
            continue
        data = read_bytes(root, rel)
        if looks_binary(data):
            continue
        for n, line in enumerate(data.decode("utf-8", "replace").splitlines(), 1):
            m = FORBIDDEN.search(line)
            if m:
                out.append("%s:%d: names '%s' (private planning, trailer or session text does not belong in a public file)" % (rel, n, m.group(0)))
    return out


def kind_of(rel):
    ext = os.path.splitext(rel)[1]
    if rel.startswith("tests/") and ext in (".cpp", ".hpp", ".js", ".py"):
        return "test", TEST_LINES
    if rel.startswith(("src/", "include/", "web/", "tools/")) and ext in (".cpp", ".hpp", ".js", ".html", ".py"):
        return "production", PROD_LINES
    return None, None


def check_lines(root, files, ceilings=None, slack=LINE_SLACK):
    ceilings = LINE_CEILINGS if ceilings is None else ceilings
    out = []
    for rel in files:
        kind, limit = kind_of(rel)
        if not kind:
            continue
        n = line_count(read_bytes(root, rel))
        if rel in ceilings:
            if n > ceilings[rel]:
                out.append("%s: %d lines is above its ceiling %d (a %s file over %d lines is split, never grown)" % (rel, n, ceilings[rel], kind, limit))
            elif n <= limit:
                out.append("%s: %d lines no longer exceeds %d: remove it from the table" % (rel, n, limit))
            elif n < ceilings[rel] * (1 - slack):
                out.append("%s: %d lines is more than %d%% below its ceiling %d: lower the ceiling to %d" % (rel, n, round(slack * 100), ceilings[rel], n))
        elif n > limit:
            out.append("%s: %d lines is above the %s limit %d (split it)" % (rel, n, kind, limit))
    for rel in ceilings:
        if rel not in files:
            out.append("%s: in the line ceiling table but not a tracked file: remove it from the table" % rel)
    return out


ASSERT_DEF = re.compile(r"^\s*#\s*define\s+ASSERT_TRUE\b", re.M)


def assert_true_count(root, files):
    total = 0
    for rel in files:
        if rel.startswith(("tests/", "src/", "include/", "tools/")) and os.path.splitext(rel)[1] in (".cpp", ".hpp", ".h", ".inc", ".js", ".py"):
            data = read_bytes(root, rel)
            if not looks_binary(data):
                total += len(ASSERT_DEF.findall(data.decode("utf-8", "replace")))
    return total


def check_assert_true(root, files, allowed=ASSERT_TRUE_DEFINITIONS):
    n = assert_true_count(root, files)
    if n > allowed:
        return ["ASSERT_TRUE: %d definitions, at most %d (include tests/common/test_assert.hpp, define no new copy)" % (n, allowed)]
    if n < allowed:
        return ["ASSERT_TRUE: %d definitions, fewer than the constant %d: lower the constant" % (n, allowed)]
    return []


def check_cmake(root, files, exceptions=CMAKE_EXCEPTIONS):
    cmake = "".join(read_bytes(root, r).decode("utf-8", "replace") for r in files if os.path.basename(r) == "CMakeLists.txt" or r.endswith(".cmake"))
    out = []
    for rel in files:
        if rel.endswith(".cpp") and rel.startswith(("src/", "tests/", "tools/")) and rel not in exceptions:
            if not re.search(r"(?<![\w.-])" + re.escape(os.path.basename(rel)) + r"(?![\w.-])", cmake):
                out.append("%s: not named in any CMakeLists.txt (add it to the build or to CMAKE_EXCEPTIONS)" % rel)
    return out


def run_checks(root):
    files = tracked_files(root)
    return (check_doc_sizes(root, files) + check_reachable(root, files) + check_forbidden(root, files)
            + check_lines(root, files) + check_assert_true(root, files) + check_cmake(root, files))


def list_numbers(root):
    files = tracked_files(root)
    lines = ["documents (bytes, largest first; limit %d):" % DOC_LIMIT]
    docs = sorted(((os.path.getsize(os.path.join(root, r)), r) for r in files if is_doc(r)), reverse=True)
    lines += ["  %8d  %s%s" % (s, r, "  (ceiling %d)" % DOC_CEILINGS[r] if r in DOC_CEILINGS else "") for s, r in docs[:8]]
    lines.append("files over the line limits (tests %d, production %d):" % (TEST_LINES, PROD_LINES))
    over = []
    for r in files:
        kind, limit = kind_of(r)
        if kind:
            n = line_count(read_bytes(root, r))
            if n > limit:
                over.append((n, r))
    lines += ["  %6d  %s%s" % (n, r, "  (ceiling %d)" % LINE_CEILINGS[r] if r in LINE_CEILINGS else "") for n, r in sorted(over, reverse=True)]
    lines.append("ASSERT_TRUE definitions: %d (constant %d)" % (assert_true_count(root, files), ASSERT_TRUE_DEFINITIONS))
    return lines


def main(argv):
    parser = argparse.ArgumentParser(description="Repository hygiene check (see the top of this file).")
    parser.add_argument("--root", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."), help="the repository (default: this tool's)")
    parser.add_argument("--list", action="store_true", help="print the current numbers that the ratchet tables pin, and exit")
    try:
        args = parser.parse_args(argv[1:])
    except SystemExit as e:
        return 2 if e.code else 0
    root = os.path.abspath(args.root)
    if not os.path.isdir(root):
        print("check_hygiene: %s is not a folder" % root, file=sys.stderr)
        return 2
    if args.list:
        print("\n".join(list_numbers(root)))
        return 0
    findings = run_checks(root)
    for f in findings:
        print(f)
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
