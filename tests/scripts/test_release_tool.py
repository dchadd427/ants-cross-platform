#!/usr/bin/env python3
"""Tests of tools/release.py, the release tool (run by ./run_tests.sh --fast and by the CI).

The tool runs on SCRATCH copies (--root): a small repository of the four files that a release touches (VERSION, CHANGELOG.md, README.md, STATUS.md), and a copy of the
real files with a "## Next" draft put in, so that the tool is held to the real formats. What is checked:

  - the draft "## Next" (the template's HTML comment is not a draft) becomes "## vX.Y.Z - date - title"; VERSION, the README's version line and STATUS.md's
    "current release" and Pacific time stamp follow; tools/check_version_consistency.py is happy afterwards
  - it refuses, with a message and with nothing written: no draft, an empty draft, a draft below a release, a version that is not higher, a bad version, date or
    title, files that already disagree, a README or STATUS.md without the line it has to change (nothing is written half way)
  - --dry-run writes nothing and prints the diff; the Pacific time is right in summer and in winter, also at the changes of the clock; --open-pr runs gh pr create
"""
import datetime
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TOOL = os.path.join(REPO, "tools", "release.py")
CHECK = os.path.join(REPO, "tools", "check_version_consistency.py")
sys.path.insert(0, os.path.join(REPO, "tools"))
import release as release_module     # noqa: E402

CHANGELOG = """# Changelog

Intro text.

<!--
Template of an entry:

## vX.Y.Z - YYYY-MM-DD - title

**For players:**
- one bullet

## Next
-->

%(draft)s## v1.2.3 - 2026-01-01 - Old title

**For players:**
- old

## v1.2.2 - 2025-12-01 - Older

**For players:**
- older
"""
DRAFT = "## Next\n\n**For players:**\n- new thing\n- another thing\n\n**Fixes:**\n- a fix\n\n"
README = "# Title\n\n**Current version: v1.2.3** (shown on screen)\n\nMore text mentions v1.2.3 elsewhere.\n"
STATUS = "# Status\n\n_Updated 2026-01-02 09:30 PST · current release **v1.2.3** · details: [CHANGELOG](CHANGELOG.md)_\n\n## In progress\n- x\n"
NOW_SUMMER = "2026-10-03T21:05:00Z"          # 14:05 PDT
NOW_WINTER = "2026-12-03T21:05:00Z"          # 13:05 PST


def write(path, text):
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text)


def read(path):
    with open(path, encoding="utf-8", newline="") as f:
        return f.read()


class Scratch(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = self.tmp.name
        self.make(CHANGELOG % {"draft": DRAFT})

    def tearDown(self):
        self.tmp.cleanup()

    def make(self, changelog, readme=README, status=STATUS, version="1.2.3\n"):
        for name, text in (("VERSION", version), ("CHANGELOG.md", changelog), ("README.md", readme), ("STATUS.md", status)):
            write(os.path.join(self.root, name), text)

    def snapshot(self):
        return {n: read(os.path.join(self.root, n)) for n in ("VERSION", "CHANGELOG.md", "README.md", "STATUS.md")}

    def release(self, *args, now=NOW_SUMMER):
        return subprocess.run([sys.executable, TOOL, *args, "--root", self.root, "--now", now], capture_output=True, text=True)

    def consistent(self, root=None):
        return subprocess.run([sys.executable, CHECK, "--root", root or self.root], capture_output=True, text=True)


class Release(Scratch):
    def test_the_draft_becomes_the_release_and_the_four_files_agree(self):
        result = self.release("1.3.0", "The new thing")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        files = self.snapshot()
        self.assertEqual(files["VERSION"], "1.3.0\n")
        self.assertIn("\n## v1.3.0 - 2026-10-03 - The new thing\n\n**For players:**\n- new thing\n", files["CHANGELOG.md"])
        self.assertNotIn("\n## Next\n\n", files["CHANGELOG.md"].split("-->")[1])        # the draft heading is gone (the template's comment keeps its own)
        self.assertIn("## v1.2.3 - 2026-01-01 - Old title", files["CHANGELOG.md"])      # the older entries stay
        self.assertEqual(files["README.md"], README.replace("**Current version: v1.2.3**", "**Current version: v1.3.0**"))   # only the version line: v1.2.3 elsewhere stays
        self.assertEqual(files["STATUS.md"], STATUS.replace("_Updated 2026-01-02 09:30 PST", "_Updated 2026-10-03 14:05 PDT").replace("**v1.2.3**", "**v1.3.0**"))
        self.assertEqual(self.consistent().returncode, 0, self.consistent().stderr)
        self.assertIn("released v1.3.0", result.stdout)
        self.assertIn("version consistency", result.stdout)                             # the tool ran the check itself

    def test_the_template_comment_is_not_a_draft_and_keeps_its_example(self):
        self.make(CHANGELOG % {"draft": ""})                                            # the comment mentions "## Next" and a "## vX.Y.Z" heading, there is no real draft
        before = self.snapshot()
        result = self.release("1.3.0", "x")
        self.assertEqual(result.returncode, 2)
        self.assertIn('no draft', result.stderr)
        self.assertIn('"## Next"', result.stderr)
        self.assertEqual(self.snapshot(), before)

    def test_an_empty_draft_is_refused(self):
        self.make(CHANGELOG % {"draft": "## Next\n\n"})
        before = self.snapshot()
        result = self.release("1.3.0", "x")
        self.assertEqual(result.returncode, 2)
        self.assertIn("empty", result.stderr)
        self.assertEqual(self.snapshot(), before)

    def test_a_draft_below_the_newest_release_is_refused(self):
        text = CHANGELOG % {"draft": ""}
        text = text.replace("## v1.2.2 - 2025-12-01 - Older", "## Next\n\n- late\n\n## v1.2.2 - 2025-12-01 - Older")
        self.make(text)
        before = self.snapshot()
        result = self.release("1.3.0", "x")
        self.assertEqual(result.returncode, 2)
        self.assertIn("top section", result.stderr)
        self.assertEqual(self.snapshot(), before)

    def test_the_version_must_be_higher(self):
        before = self.snapshot()
        for version in ("1.2.3", "1.2.2", "0.9.9", "1.1.99"):
            result = self.release(version, "x")
            self.assertEqual(result.returncode, 2, version)
            self.assertIn("higher", result.stderr)
        self.assertEqual(self.snapshot(), before)
        self.assertEqual(self.release("1.2.4", "x").returncode, 0)                      # a patch release is fine
        self.assertEqual(self.snapshot()["VERSION"], "1.2.4\n")

    def test_a_bad_version_date_or_title_is_refused(self):
        before = self.snapshot()
        for args in (("1.3", "x"), ("v1.3.0", "x"), ("1.3.0.1", "x"), ("1.3.0", ""), ("1.3.0", "  "), ("1.3.0", "two\nlines"), ("1.3.0", "x", "--date", "2026-13-40"),
                     ("1.3.0", "x", "--date", "yesterday")):
            result = self.release(*args)
            self.assertEqual(result.returncode, 2, args)
            self.assertIn("release:", result.stderr)
        self.assertEqual(self.snapshot(), before)

    def test_a_date_can_be_given(self):
        self.assertEqual(self.release("1.3.0", "x", "--date", "2026-11-11").returncode, 0)
        self.assertIn("## v1.3.0 - 2026-11-11 - x", self.snapshot()["CHANGELOG.md"])
        self.assertIn("_Updated 2026-10-03 14:05 PDT", self.snapshot()["STATUS.md"])    # the stamp is the time now, whatever the heading says

    def test_files_that_disagree_before_the_release_are_refused(self):
        self.make(CHANGELOG % {"draft": DRAFT}, readme=README.replace("v1.2.3**", "v1.2.1**"))
        before = self.snapshot()
        result = self.release("1.3.0", "x")
        self.assertEqual(result.returncode, 2)
        self.assertIn("disagree", result.stderr)
        self.assertIn("README.md", result.stderr)
        self.assertEqual(self.snapshot(), before)

    def test_a_file_without_the_line_to_change_stops_everything_and_nothing_is_written(self):
        for readme, status, text in ((README.replace("Current version:", "Version:"), STATUS, "README.md"),
                                     (README, STATUS.replace("_Updated 2026-01-02 09:30 PST · ", "_"), "STATUS.md")):
            self.make(CHANGELOG % {"draft": DRAFT}, readme=readme, status=status)
            before = self.snapshot()
            result = self.release("1.3.0", "x")
            self.assertEqual(result.returncode, 2, text)
            self.assertIn(text, result.stderr)
            self.assertEqual(self.snapshot(), before, "a file was written although the release could not be finished")

    def test_dry_run_writes_nothing_and_shows_the_diff(self):
        before = self.snapshot()
        result = self.release("1.3.0", "The new thing", "--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.snapshot(), before)
        self.assertIn("-## Next", result.stdout)
        self.assertIn("+## v1.3.0 - 2026-10-03 - The new thing", result.stdout)
        self.assertIn("+1.3.0", result.stdout)
        self.assertIn("-**Current version: v1.2.3**", result.stdout)
        self.assertIn("dry run:", result.stdout)
        self.assertFalse([n for n in os.listdir(self.root) if n.endswith(".tmp")])

    def test_dry_run_still_refuses(self):
        self.make(CHANGELOG % {"draft": ""})
        self.assertEqual(self.release("1.3.0", "x", "--dry-run").returncode, 2)

    def test_pacific_time_in_summer_and_in_winter(self):
        self.assertEqual(self.release("1.3.0", "x", now=NOW_WINTER).returncode, 0)
        self.assertIn("_Updated 2026-12-03 13:05 PST", self.snapshot()["STATUS.md"])
        self.assertIn("## v1.3.0 - 2026-12-03 - x", self.snapshot()["CHANGELOG.md"])

    def test_the_date_is_the_pacific_date_not_the_utc_date(self):
        self.assertEqual(self.release("1.3.0", "x", now="2026-10-04T03:30:00Z").returncode, 0)       # 20:30 PDT on the 3rd
        self.assertIn("## v1.3.0 - 2026-10-03 - x", self.snapshot()["CHANGELOG.md"])
        self.assertIn("_Updated 2026-10-03 20:30 PDT", self.snapshot()["STATUS.md"])

    def test_windows_line_endings_are_kept(self):
        self.make((CHANGELOG % {"draft": DRAFT}).replace("\n", "\r\n"), readme=README.replace("\n", "\r\n"), status=STATUS.replace("\n", "\r\n"))
        self.assertEqual(self.release("1.3.0", "x").returncode, 0)
        changelog = self.snapshot()["CHANGELOG.md"]
        self.assertNotIn("\n", changelog.replace("\r\n", ""))
        self.assertIn("## v1.3.0 - 2026-10-03 - x\r\n", changelog)
        self.assertEqual(self.consistent().returncode, 0)


class PacificTime(unittest.TestCase):
    def at(self, text):
        moment = datetime.datetime.fromisoformat(text.replace("Z", "+00:00"))
        return release_module.pacific(moment)

    def test_the_clock_changes(self):
        self.assertEqual(self.at("2026-03-08T09:59:00Z"), ("2026-03-08", "01:59", "PST"))    # 2:00 PST of the second Sunday of March: the clock goes to 3:00 PDT
        self.assertEqual(self.at("2026-03-08T10:00:00Z"), ("2026-03-08", "03:00", "PDT"))
        self.assertEqual(self.at("2026-11-01T08:59:00Z"), ("2026-11-01", "01:59", "PDT"))    # the first Sunday of November: 2:00 PDT goes back to 1:00 PST
        self.assertEqual(self.at("2026-11-01T09:00:00Z"), ("2026-11-01", "01:00", "PST"))
        self.assertEqual(self.at("2026-01-01T07:59:00Z"), ("2025-12-31", "23:59", "PST"))    # the date is the Pacific date
        self.assertEqual(self.at("2027-07-04T06:30:00Z"), ("2027-07-03", "23:30", "PDT"))

    def test_it_agrees_with_the_time_zone_database_for_every_day_of_four_years(self):
        try:
            from zoneinfo import ZoneInfo
            zone = ZoneInfo("America/Los_Angeles")
        except Exception:
            self.skipTest("no time zone database on this machine")
        start = datetime.datetime(2024, 1, 1, 0, 0, tzinfo=datetime.timezone.utc)
        for hours in range(0, 4 * 366 * 24, 7):
            utc = start + datetime.timedelta(hours=hours)
            local = utc.astimezone(zone)
            self.assertEqual(release_module.pacific(utc), (local.strftime("%Y-%m-%d"), local.strftime("%H:%M"), local.strftime("%Z")), str(utc))


class TheRealFormats(unittest.TestCase):
    """A scratch copy of the repository's own files with a draft put in: the tool must handle the formats that are really used."""

    def test_the_real_files_with_a_draft_can_be_released_and_stay_consistent(self):
        with tempfile.TemporaryDirectory() as root:
            for name in ("VERSION", "CHANGELOG.md", "README.md", "STATUS.md"):
                shutil.copyfile(os.path.join(REPO, name), os.path.join(root, name))
            changelog = read(os.path.join(root, "CHANGELOG.md"))
            at = changelog.index("\n## v", changelog.index("-->")) + 1                # the newest release, below the template's comment
            write(os.path.join(root, "CHANGELOG.md"), changelog[:at] + DRAFT + changelog[at:])
            major, minor, patch = (int(n) for n in read(os.path.join(root, "VERSION")).split("."))
            new_version = "%d.%d.%d" % (major, minor + 1, 0)
            result = subprocess.run([sys.executable, TOOL, new_version, "A test release", "--root", root, "--now", NOW_SUMMER], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            checked = subprocess.run([sys.executable, CHECK, "--root", root], capture_output=True, text=True)
            self.assertEqual(checked.returncode, 0, checked.stderr)
            self.assertIn("v" + new_version, checked.stdout)
            self.assertIn("## v%s - 2026-10-03 - A test release" % new_version, read(os.path.join(root, "CHANGELOG.md")))
            self.assertIn("_Updated 2026-10-03 14:05 PDT", read(os.path.join(root, "STATUS.md")))
        self.assertEqual(subprocess.run([sys.executable, CHECK], capture_output=True, text=True, cwd=REPO).returncode, 0, "the real repository was changed")


@unittest.skipUnless(os.name == "posix", "a fake gh program is a shell script")
class OpenPullRequest(Scratch):
    def fake_gh(self, exit_status=0):
        folder = os.path.join(self.root, "fakebin")
        os.makedirs(folder, exist_ok=True)
        log = os.path.join(self.root, "gh.log")
        path = os.path.join(folder, "gh")
        write(path, '#!/bin/sh\nprintf "%%s\\n" "$@" > "%s"\ncat > "%s.body"\nexit %d\n' % (log, log, exit_status))
        os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR)
        return folder, log

    def run_with_gh(self, folder, *args):
        env = dict(os.environ, PATH=folder + os.pathsep + os.environ.get("PATH", ""))
        return subprocess.run([sys.executable, TOOL, *args, "--root", self.root, "--now", NOW_SUMMER], capture_output=True, text=True, env=env)

    def test_open_pr_runs_gh_with_the_title_the_entry_and_the_list(self):
        folder, log = self.fake_gh()
        result = self.run_with_gh(folder, "1.3.0", "The new thing", "--open-pr", "--try", "open the page", "--try", "press START", "--draft")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        arguments = read(log).split("\n")
        self.assertEqual(arguments[:3], ["pr", "create", "--base"])
        self.assertEqual(arguments[3], "main")
        self.assertIn("v1.3.0 - The new thing", arguments)
        self.assertIn("--draft", arguments)
        body = read(log + ".body")
        self.assertIn("**For players:**\n- new thing", body)
        self.assertIn("**What to try:**\n- open the page\n- press START", body)
        self.assertNotIn("## Next", body)

    def test_open_pr_with_dry_run_runs_nothing(self):
        folder, log = self.fake_gh()
        result = self.run_with_gh(folder, "1.3.0", "x", "--open-pr", "--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(os.path.exists(log))
        self.assertIn("would run: gh pr create", result.stdout)

    def test_a_failing_gh_is_reported_and_the_files_stay_released(self):
        folder, _ = self.fake_gh(exit_status=1)
        result = self.run_with_gh(folder, "1.3.0", "x", "--open-pr")
        self.assertEqual(result.returncode, 2)
        self.assertIn("gh pr create", result.stderr)
        self.assertEqual(self.snapshot()["VERSION"], "1.3.0\n")

    def test_without_gh_nothing_is_changed(self):
        before = self.snapshot()
        env = dict(os.environ, PATH=os.path.dirname(sys.executable))                    # a PATH with python and nothing else
        result = subprocess.run([sys.executable, TOOL, "1.3.0", "x", "--open-pr", "--root", self.root, "--now", NOW_SUMMER], capture_output=True, text=True, env=env)
        if shutil.which("gh", path=env["PATH"]):
            self.skipTest("gh lives next to python on this machine")
        self.assertEqual(result.returncode, 2)
        self.assertIn("gh", result.stderr)
        self.assertEqual(self.snapshot(), before)


if __name__ == "__main__":
    unittest.main()
