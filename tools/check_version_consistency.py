#!/usr/bin/env python3
"""Checks that the places that NAME the current release agree with the one source of the version, the file VERSION.

Usage: check_version_consistency.py [--root DIR]

  VERSION        one line, MAJOR.MINOR.PATCH (the only place the version is written by hand; CMake generates the C++ header from it)
  CHANGELOG.md   the top release heading ("## vX.Y.Z - YYYY-MM-DD - title", newest first) is the release in VERSION
  STATUS.md      "current release **vX.Y.Z**" in the line under the title
  README.md      "Current version: vX.Y.Z" in the first paragraph

Exit code 0: all agree. Exit code 1: at least one disagrees or is missing; every problem is printed with the file that is wrong (and the line), so
the fix is obvious: change that file (or VERSION, when the version is what is wrong). Run by `./run_tests.sh --fast`, by the CI and by the release
procedure (docs/WORKFLOW.md). No dependencies beyond the standard library.
"""
import argparse
import os
import re
import sys

SEMVER = r"[0-9]+\.[0-9]+\.[0-9]+"


def read_lines(path):
    with open(path, encoding="utf-8") as f:
        return f.read().splitlines()


def find_first(lines, pattern):
    """The first (line number, captured version, text) whose line matches pattern (one group: the version), else None."""
    rx = re.compile(pattern)
    for number, line in enumerate(lines, 1):
        m = rx.search(line)
        if m:
            return number, m.group(1), line.strip()
    return None


def check(root):
    """Returns the list of problems (empty: all agree)."""
    problems = []
    version_path = os.path.join(root, "VERSION")
    if not os.path.isfile(version_path):
        return ["VERSION: the file is missing (one line, MAJOR.MINOR.PATCH, for example 0.1.0)"]
    version_lines = read_lines(version_path)
    version = version_lines[0].strip() if version_lines else ""
    if not re.fullmatch(SEMVER, version):
        return ["VERSION: the first line must be MAJOR.MINOR.PATCH (digits only, for example 0.1.0), found %r" % version]
    if len([l for l in version_lines if l.strip()]) != 1:
        problems.append("VERSION: must hold exactly one line, found %d non-empty lines" % len([l for l in version_lines if l.strip()]))

    sources = [
        ("CHANGELOG.md", r"^##\s+v(%s)\b" % SEMVER, "a release heading '## vX.Y.Z - YYYY-MM-DD - title' (the top one is the newest release)"),
        ("STATUS.md", r"current release\s+\*\*v(%s)\*\*" % SEMVER, "the text 'current release **vX.Y.Z**' under the title"),
        ("README.md", r"Current version:\s*\*{0,2}\s*v(%s)\b" % SEMVER, "the text 'Current version: vX.Y.Z' in the first paragraph"),
    ]
    for name, pattern, what in sources:
        path = os.path.join(root, name)
        if not os.path.isfile(path):
            problems.append("%s: the file is missing" % name)
            continue
        found = find_first(read_lines(path), pattern)
        if found is None:
            problems.append("%s: no %s was found" % (name, what))
            continue
        number, found_version, text = found
        if found_version != version:
            problems.append("%s: line %d says v%s but the file VERSION says %s (%s)" % (name, number, found_version, version, text[:100]))
    return problems


def main(argv):
    parser = argparse.ArgumentParser(description="Checks that CHANGELOG.md, STATUS.md and README.md name the release that the file VERSION names.")
    parser.add_argument("--root", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."), help="the repository's root folder (default: this script's parent)")
    args = parser.parse_args(argv[1:])
    root = os.path.abspath(args.root)
    problems = check(root)
    if problems:
        sys.stderr.write("VERSION CONSISTENCY CHECK FAILED: the release is named differently in these files:\n")
        for problem in problems:
            sys.stderr.write("  - %s\n" % problem)
        sys.stderr.write("The version is the line of the file VERSION; the heading at the top of CHANGELOG.md, 'current release' in STATUS.md and 'Current version' in README.md must name the same release.\n")
        return 1
    with open(os.path.join(root, "VERSION"), encoding="utf-8") as f:
        version = f.readline().strip()
    print("version consistency: VERSION, CHANGELOG.md, STATUS.md and README.md all name v%s" % version)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
