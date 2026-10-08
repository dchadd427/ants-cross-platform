#!/usr/bin/env python3
"""Makes a release in the files: VERSION, the changelog heading and the README's version line (docs/WORKFLOW.md, "Releasing").

usage: release.py X.Y.Z "title" [--date YYYY-MM-DD] [--dry-run] [--root DIR] [--open-pr [--base BRANCH] [--draft] [--try TEXT ...]]

  - VERSION becomes X.Y.Z (it must be higher than the version now in the file)
  - the top draft section of CHANGELOG.md, the one headed "## Next" (above the newest release; the template at the top of that file), becomes
    "## vX.Y.Z - DATE - title"; the tool refuses, with a message, when there is no such section or it is empty
  - the README's "Current version: vX.Y.Z" names the new version (the date of the heading is today's Pacific date unless --date says otherwise)
  - tools/check_version_consistency.py must be happy before the files are changed (else nothing is touched) and is run again when they are written

Everything is worked out first and written only if all of it works. --dry-run prints what would change (a diff) and writes nothing. --root names another
folder (the tests use scratch copies). --open-pr then runs `gh pr create --base BRANCH (default main) --title "vX.Y.Z - title"` with the changelog entry (and a
"What to try" list from every --try) as the body; the branch must be committed and pushed first. --now is for tests.
Exit status: 0 done, 1 the written files disagree (should not happen), 2 refused or a wrong argument.
"""
import argparse
import datetime
import difflib
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_version_consistency as consistency     # noqa: E402  (the same folder)

SEMVER = r"[0-9]+\.[0-9]+\.[0-9]+"
README_RE = re.compile(r"(Current version:\s*\*{0,2}\s*v)(%s)\b" % SEMVER)
HEADING_RE = re.compile(r"^##\s+(.*?)\s*$")


class Refused(Exception):
    """A clear message for the person who runs the tool; nothing was changed."""


# ---- Pacific time ----------------------------------------------------------------------------------------------------------------------------

def nth_sunday(year, month, n):
    day = datetime.date(year, month, 1)
    first = day + datetime.timedelta(days=(6 - day.weekday()) % 7)
    return first + datetime.timedelta(weeks=n - 1)


def pacific(utc):
    """(date text, time text, zone name) in Pacific time at the UTC moment: PDT from 2:00 on the second Sunday of March to 2:00 on the first Sunday of November
    (the US rule since 2007), PST otherwise. Worked out here, so that it needs no time zone database (a Windows machine has none)."""
    start = datetime.datetime.combine(nth_sunday(utc.year, 3, 2), datetime.time(10, 0), tzinfo=datetime.timezone.utc)     # 2:00 PST
    end = datetime.datetime.combine(nth_sunday(utc.year, 11, 1), datetime.time(9, 0), tzinfo=datetime.timezone.utc)       # 2:00 PDT
    daylight = start <= utc < end
    local = utc + datetime.timedelta(hours=-7 if daylight else -8)
    return local.strftime("%Y-%m-%d"), local.strftime("%H:%M"), "PDT" if daylight else "PST"


def utc_now(override):
    if not override:
        return datetime.datetime.now(datetime.timezone.utc)
    try:
        moment = datetime.datetime.fromisoformat(override.replace("Z", "+00:00"))
    except ValueError:
        raise Refused("--now must be an ISO 8601 time, found %r" % override)
    return moment.replace(tzinfo=datetime.timezone.utc) if moment.tzinfo is None else moment.astimezone(datetime.timezone.utc)


# ---- the files -------------------------------------------------------------------------------------------------------------------------------

def read(root, name):
    path = os.path.join(root, name)
    try:
        with open(path, encoding="utf-8", newline="") as f:
            return f.read()
    except OSError as e:
        raise Refused("%s: cannot be read (%s)" % (name, e.strerror))


def sections(lines):
    """[(line number, heading text)] of the `## ` headings that are real: not inside an HTML comment (the template) and not inside a code fence."""
    found = []
    in_comment = False
    in_fence = False
    for number, line in enumerate(lines):
        stripped = line.strip()
        if in_comment:
            if "-->" in line:
                in_comment = False
            continue
        if stripped.startswith("```"):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        if stripped.startswith("<!--") and "-->" not in stripped:
            in_comment = True
            continue
        m = HEADING_RE.match(line) if not line.startswith("###") else None
        if m:
            found.append((number, m.group(1)))
    return found


def convert_changelog(text, version, date, title):
    """(new text, the entry's body) with the draft section turned into the release heading."""
    newline = "\r\n" if "\r\n" in text else "\n"
    lines = text.split(newline)
    heads = sections(lines)
    if not heads:
        raise Refused('CHANGELOG.md has no "## " sections at all: add a draft section headed "## Next" above the newest release')
    first_number, first_text = heads[0]
    if first_text != "Next":
        later = [h for h in heads if h[1] == "Next"]
        if later:
            raise Refused('CHANGELOG.md: the section "## Next" must be the top section (above the newest release "## %s"), it is below it (line %d)' % (first_text, later[0][0] + 1))
        raise Refused('CHANGELOG.md has no draft: write the entry of this release under a section headed exactly "## Next" above the newest release "## %s" '
                      "(the template is at the top of the file, docs/WORKFLOW.md says how)" % first_text)
    end = heads[1][0] if len(heads) > 1 else len(lines)
    body = newline.join(lines[first_number + 1:end]).strip()
    if not body:
        raise Refused('CHANGELOG.md: the draft section "## Next" is empty: write the entry (the template at the top of the file) before the release')
    lines[first_number] = "## v%s - %s - %s" % (version, date, title)
    return newline.join(lines), body


def replace_once(regex, text, repl, where, what):
    if not regex.search(text):
        raise Refused("%s: no %s was found (the consistency check looks for it too)" % (where, what))
    return regex.sub(repl, text, count=1)


def diff_text(name, old, new):
    return "".join(difflib.unified_diff(old.splitlines(True), new.splitlines(True), "a/" + name, "b/" + name, n=1))


def main(argv):
    parser = argparse.ArgumentParser(description="Makes a release in the files (see the top of this file).")
    parser.add_argument("version", help="the new version, MAJOR.MINOR.PATCH")
    parser.add_argument("title", help="the title of the release (the heading and the pull request)")
    parser.add_argument("--date", help="the date of the heading, YYYY-MM-DD (default: today, Pacific time)")
    parser.add_argument("--dry-run", action="store_true", help="show what would change; write nothing")
    parser.add_argument("--root", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."), help="the repository's root folder (default: this script's parent)")
    parser.add_argument("--open-pr", action="store_true", help="then open the pull request with `gh pr create`")
    parser.add_argument("--base", default="main", help="the branch of the pull request (default: main)")
    parser.add_argument("--draft", action="store_true", help="open the pull request as a draft")
    parser.add_argument("--try", dest="try_items", action="append", default=[], metavar="TEXT", help='a line of the pull request\'s "What to try" list (repeatable)')
    parser.add_argument("--now", help="the time now as an ISO 8601 UTC time (for tests)")
    args = parser.parse_args(argv[1:])
    root = os.path.abspath(args.root)
    try:
        return run(args, root)
    except Refused as e:
        print("release: %s" % e, file=sys.stderr)
        print("release: nothing was changed", file=sys.stderr)
        return 2


def run(args, root):
    version, title = args.version, args.title.strip()
    if not re.fullmatch(SEMVER, version):
        raise Refused("the version must be MAJOR.MINOR.PATCH (digits only, for example 0.1.3), found %r" % version)
    if not title or "\n" in title or "\r" in title:
        raise Refused("the title must be one non-empty line")
    now = utc_now(args.now)
    today = pacific(now)[0]
    date = args.date or today
    try:
        datetime.date.fromisoformat(date)
    except ValueError:
        raise Refused("--date must be YYYY-MM-DD, found %r" % date)
    if args.open_pr and not args.dry_run and not shutil.which("gh"):
        raise Refused("--open-pr needs the GitHub CLI (gh) on the PATH")

    problems = consistency.check(root)
    if problems:
        raise Refused("the files disagree before the release, fix that first:\n  - " + "\n  - ".join(problems))
    old = {name: read(root, name) for name in ("VERSION", "CHANGELOG.md", "README.md")}
    current = old["VERSION"].strip()
    if tuple(int(n) for n in version.split(".")) <= tuple(int(n) for n in current.split(".")):
        raise Refused("the version must be higher than the one in the file VERSION (%s), found %s" % (current, version))

    new = dict(old)
    new["VERSION"] = version + "\n"
    new["CHANGELOG.md"], entry = convert_changelog(old["CHANGELOG.md"], version, date, title)
    new["README.md"] = replace_once(README_RE, old["README.md"], lambda m: m.group(1) + version, "README.md", "'Current version: vX.Y.Z' line")
    if "**For players:**" not in entry:
        print('release: warning: the draft has no "**For players:**" paragraph (the template at the top of CHANGELOG.md)', file=sys.stderr)

    with tempfile.TemporaryDirectory() as scratch:                  # the result must pass the consistency check before anything is written
        for name, text in new.items():
            with open(os.path.join(scratch, name), "w", encoding="utf-8", newline="") as f:
                f.write(text)
        problems = consistency.check(scratch)
    if problems:
        raise Refused("the result would not be consistent:\n  - " + "\n  - ".join(problems))

    changed = [name for name in new if new[name] != old[name]]
    if args.dry_run:
        for name in changed:
            sys.stdout.write(diff_text(name, old[name], new[name]))
        print("dry run: v%s - %s - %s: %s would change; nothing was written" % (version, date, title, ", ".join(changed)))
    else:
        for name in changed:
            path = os.path.join(root, name)
            with open(path + ".release.tmp", "w", encoding="utf-8", newline="") as f:
                f.write(new[name])
            os.replace(path + ".release.tmp", path)
        print("released v%s (%s): %s written" % (version, date, ", ".join(changed)))
        print("  VERSION %s -> %s; CHANGELOG.md: '## Next' -> '## v%s - %s - %s'" % (current, version, version, date, title))
        checked = subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "check_version_consistency.py"), "--root", root],
                                 capture_output=True, text=True)
        sys.stdout.write(checked.stdout)
        if checked.returncode != 0:
            sys.stderr.write(checked.stderr)
            return 1
        print("next: commit these files, push the branch, open the pull request titled 'v%s - %s'" % (version, title))

    if args.open_pr:
        body = entry + ("\n\n**What to try:**\n" + "\n".join("- " + t for t in args.try_items) if args.try_items else "") + "\n"
        command = ["gh", "pr", "create", "--base", args.base, "--title", "v%s - %s" % (version, title), "--body-file", "-"] + (["--draft"] if args.draft else [])
        if args.dry_run:
            print("dry run: would run: %s  (the body is the changelog entry%s)" % (" ".join(command[:6]) + " ...", " and the What to try list" if args.try_items else ""))
        else:
            opened = subprocess.run(command, input=body, text=True, cwd=root)
            if opened.returncode != 0:
                print("release: the files are released, but `gh pr create` failed (is the branch pushed?)", file=sys.stderr)
                return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
