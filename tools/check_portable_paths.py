#!/usr/bin/env python3
"""Portable paths: no tracked path is one that a Windows or macOS checkout cannot hold. Pure python 3 standard library.

usage: check_portable_paths.py [--root DIR]

Reads `git ls-files -z` of the repository at DIR (default: the current folder) and prints every tracked path that Windows or macOS cannot check out, with the reason, one line each, the path
quoted (JSON style, ASCII only: no line can begin with a workflow command or hide a character). Exit status: 0 every path can be held (one line says so), 1 at least one cannot, 2 a usage
error or a git that fails. Run by tests/scripts/test_portable_paths.py, so by ./run_tests.sh --fast and by the python tests of every pull request on Linux.

Why: Linux holds any name, so a name that Windows or macOS cannot hold passes the Linux jobs. A pull request of documents, pages and their tests only skips the macOS and Windows jobs
(docs/WORKFLOW.md, "Light checks"), and after a merge a name like that makes every later checkout on Windows fail (git cannot create the file) or two files into one (a case twin) until a
pull request deletes it. What is refused:
  - a name that is not valid UTF-8 (Windows and macOS names are Unicode);
  - a character that Windows does not allow in a name: < > : " | ? * and the backslash, and the control characters 0 to 31 (a `/` ends a name);
  - a name that ends with a space or a dot (Windows removes them);
  - a DOS device name as the name or before its first dot, in any case: CON PRN AUX NUL COM0 to COM9 LPT0 to LPT9 (also with the superscript digits 1 to 3 that Windows 10 reserves), CONIN$ and
    CONOUT$; `aux.md` and `NUL.tar.gz` are devices too, and so is `aux .md` (Windows ignores a space before the dot);
  - a path of more than MAX_PATH_UNITS characters, counted as UTF-16 units as Windows does: MAX_PATH is 260 with the drive and the end mark, and the checkout folder takes 45 characters on GitHub's
    runners and 60 or so on a developer's PC (the longest path of the repository is 74 characters today; no name can pass the 255 units of NTFS under this limit);
  - a name of more than 255 bytes in UTF-8 (the limit of macOS and of most Linux file systems);
  - two paths that are one on a file system that ignores case and Unicode forms (macOS and Windows): `docs/a.md` and `docs/A.md`, `cafe` with the accent as one character and as two, and
    the folders count (`Web/a.js` and `web/b.js` are in one folder there); each of the paths is named.
"""
import argparse
import json
import subprocess
import sys
import unicodedata

MAX_PATH_UNITS = 200
MAX_NAME_BYTES = 255
MAX_LINES = 100                                             # (the paths named at most; the count says how many there are)

FORBIDDEN = set('<>:"|?*\\') | {chr(c) for c in range(32)}
DEVICES = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"} | {"%s%s" % (kind, digit) for kind in ("COM", "LPT") for digit in "0123456789\u00b9\u00b2\u00b3"}


def quote(text):
    """The text as a JSON string with only ASCII in it (a character that cannot be shown is \\uXXXX, a byte that is not UTF-8 is \\udcXX)."""
    return json.dumps(text, ensure_ascii=True)


def utf16_units(text):
    return len(text.encode("utf-16-le", "surrogatepass")) // 2


def describe(char):
    return "U+%04X" % ord(char) if ord(char) < 32 else "'%s'" % char


def problems_of_name(name):
    """The reasons one name (a part of a path between two slashes) cannot be held, as a list of sentences."""
    found = []
    bad = sorted({c for c in name if c in FORBIDDEN})
    if bad:
        found.append("has %s, which Windows does not allow in a name" % ", ".join(describe(c) for c in bad))
    if name.endswith((" ", ".")):
        found.append("ends with a space or a dot, which Windows removes")
    if name.split(".", 1)[0].rstrip(" ").upper() in DEVICES:
        found.append("is a DOS device name on Windows (CON, PRN, AUX, NUL, COM0 to COM9, LPT0 to LPT9; with or without an extension)")
    size = len(name.encode("utf-8", "surrogatepass"))
    if size > MAX_NAME_BYTES:
        found.append("is %d bytes long; macOS holds %d" % (size, MAX_NAME_BYTES))
    return found


def problems_of(raw):
    """The reasons one tracked path (bytes, as git lists it) cannot be held by itself, as a list of sentences (twins are another matter: see twins)."""
    try:
        path = raw.decode("utf-8")
    except UnicodeDecodeError:
        return ["is not valid UTF-8, and Windows and macOS names are Unicode"]
    found = []
    units = utf16_units(path)
    if units > MAX_PATH_UNITS:
        found.append("is %d characters long; more than %d may not fit under Windows' limit of 260 with the folder of the checkout" % (units, MAX_PATH_UNITS))
    for name in path.split("/"):
        found += ["the name %s %s" % (quote(name), reason) for reason in problems_of_name(name)]
    return found


def fold(text):
    return unicodedata.normalize("NFC", text).casefold()


def twins(paths):
    """{path: reason} for the paths that a file system which ignores case and Unicode forms cannot hold next to another one: the same name in the same folder after folding, at any depth (so
    two folders count). `paths` are texts."""
    spellings = {}
    for path in paths:
        parts = path.split("/")
        for depth in range(1, len(parts) + 1):
            prefix = "/".join(parts[:depth])
            spellings.setdefault(fold(prefix), set()).add(prefix)
    found = {}
    for path in paths:
        parts = path.split("/")
        for depth in range(1, len(parts) + 1):
            prefix = "/".join(parts[:depth])
            others = spellings[fold(prefix)] - {prefix}
            if others:
                found[path] = "the %s %s is the same as %s on macOS and Windows, which ignore case and Unicode forms" % ("folder" if depth < len(parts) else "path", quote(prefix),
                                                                                                                         " and ".join(quote(o) for o in sorted(others)))
                break
    return found


def check(raw_paths):
    """[(path as text, reason)] of every tracked path (bytes) that cannot be held, in the order of the paths, a path once for each reason."""
    unique = sorted(set(raw_paths))
    shown = {raw: raw.decode("utf-8", "surrogateescape") for raw in unique}
    valid = [text for raw, text in shown.items() if _is_utf8(raw)]
    same = twins(valid)
    found = []
    for raw in unique:
        for reason in problems_of(raw):
            found.append((shown[raw], reason))
        if shown[raw] in same and _is_utf8(raw):
            found.append((shown[raw], same[shown[raw]]))
    return found


def _is_utf8(raw):
    try:
        raw.decode("utf-8")
    except UnicodeDecodeError:
        return False
    return True


class GitError(Exception):
    pass


def tracked_paths(root):
    """The tracked paths of the repository at root, as bytes (git ls-files -z)."""
    try:
        done = subprocess.run(["git", "-C", root, "ls-files", "-z"], capture_output=True)
    except OSError as error:
        raise GitError("cannot run git: %s" % error.strerror)
    if done.returncode != 0:
        raise GitError("git ls-files failed in %s: %s" % (root, done.stderr.decode("utf-8", "replace").strip()))
    return [p for p in done.stdout.split(b"\0") if p]


def main(argv):
    parser = argparse.ArgumentParser(description="Fails for a tracked path that a Windows or macOS checkout cannot hold (see the top of this file).")
    parser.add_argument("--root", default=".", help="the repository (default: the current folder)")
    try:
        args = parser.parse_args(argv[1:])
    except SystemExit as error:
        return 2 if error.code else 0
    try:
        paths = tracked_paths(args.root)
    except GitError as error:
        print("check_portable_paths: %s" % error, file=sys.stderr)
        return 2
    found = check(paths)
    if not found:
        print("portable paths: all %d tracked paths can be held by Windows and macOS" % len(set(paths)))
        return 0
    named = sorted({path for path, _ in found})
    print("portable paths: %d of %d tracked paths cannot be held by a Windows or macOS checkout (see the top of tools/check_portable_paths.py):" % (len(named), len(set(paths))))
    for path, reason in found[:MAX_LINES]:
        print("  %s: %s" % (quote(path), reason))
    if len(found) > MAX_LINES:
        print("  ... and %d more" % (len(found) - MAX_LINES))
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
