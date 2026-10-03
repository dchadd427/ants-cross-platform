#!/usr/bin/env python3
"""Tests of tools/changelog_to_html.py on the two-file changelog layout (run by ./run_tests.sh --fast and by the CI):

  CHANGELOG.md               the short changelog: one entry per release in a fixed template  -> changelog.html (the default page)
  docs/CHANGELOG_ARCHIVE.md  the detailed history up to v0.1.0, frozen                       -> changelog_archive.html

The Docker image builds both pages with the commands below (Dockerfile); this test runs the same commands on the real files and on small made-up ones.
"""
import os
import re
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TOOL = os.path.join(REPO, "tools", "changelog_to_html.py")

SHORT_MD = """# Changelog

Intro with a [link to the archive](docs/CHANGELOG_ARCHIVE.md) and a [readme link](README.md).

<!-- A comment with ## v9.9.9 - 2000-01-01 - never shown
- and a bullet that must not appear -->

## Unreleased - what is next

**For players:**
- a bullet with `code` and <angle> brackets & an ampersand

## v1.2.0 - 2026-01-02 - Second release

**For players:**
- first bullet
- second bullet

**Rules / network:** network protocol 12: a v1.1.0 game cannot join.

**Fixes:**
- one fix

**Details:** [commits](https://github.com/example/ants/compare/aaaaaaa...bbbbbbb), [detailed notes](docs/CHANGELOG_ARCHIVE.md)

## v1.1.0 - 2026-01-01 - First release

**For players:**
- only bullet

**Details:** [commits](https://github.com/example/ants/compare/ccccccc...aaaaaaa), [detailed notes](docs/CHANGELOG_ARCHIVE.md)

## Older versions (v1.0.0 and before)

See the [detailed history](docs/CHANGELOG_ARCHIVE.md).
"""

ARCHIVE_MD = """# Detailed history up to v1.2.0

Intro: back to the [short changelog](../CHANGELOG.md), and a link to [the audit notes](audit/B2_notes.md) and [the readme](../README.md).

## v1.0.0 - 2025-12-01 - Zero

- A long detailed bullet that continues
  on a second line.
"""


def read_text(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def run_tool(md_path, out_path, *extra):
    return subprocess.run([sys.executable, TOOL, md_path, out_path, *extra], capture_output=True, text=True)


def build_short(md_path, out_path, version="v1.2.0", build_id="abc1234"):
    return run_tool(md_path, out_path, "--version", version, "--build-id", build_id, "--other-page", "changelog_archive.html", "--other-label", "Detailed history",
                    "--page-link", "docs/CHANGELOG_ARCHIVE.md=changelog_archive.html")


def build_archive(md_path, out_path, version="v1.2.0", build_id="abc1234"):
    return run_tool(md_path, out_path, "--version", version, "--build-id", build_id, "--other-page", "changelog.html", "--other-label", "Short changelog",
                    "--page-link", "CHANGELOG.md=changelog.html", "--link-base", "docs")


class MadeUpFiles(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        t = cls.tmp.name
        cls.short_md = os.path.join(t, "CHANGELOG.md")
        cls.archive_md = os.path.join(t, "CHANGELOG_ARCHIVE.md")
        with open(cls.short_md, "w", encoding="utf-8") as f:
            f.write(SHORT_MD)
        with open(cls.archive_md, "w", encoding="utf-8") as f:
            f.write(ARCHIVE_MD)
        cls.short_html = os.path.join(t, "changelog.html")
        cls.archive_html = os.path.join(t, "changelog_archive.html")
        cls.short_result = build_short(cls.short_md, cls.short_html)
        cls.archive_result = build_archive(cls.archive_md, cls.archive_html)
        cls.short_text = read_text(cls.short_html) if os.path.exists(cls.short_html) else ""
        cls.archive_text = read_text(cls.archive_html) if os.path.exists(cls.archive_html) else ""

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_both_pages_are_written(self):
        self.assertEqual(self.short_result.returncode, 0, self.short_result.stderr)
        self.assertEqual(self.archive_result.returncode, 0, self.archive_result.stderr)
        self.assertTrue(self.short_text.startswith("<!DOCTYPE html>"))
        self.assertTrue(self.archive_text.startswith("<!DOCTYPE html>"))

    def test_the_two_pages_link_to_each_other_from_the_header(self):
        header = re.search(r"<header>.*?</header>", self.short_text, re.S).group(0)
        self.assertIn('<a href="changelog_archive.html">Detailed history</a>', header)
        self.assertIn('href="/"', header)                                  # back to the game, as before
        header = re.search(r"<header>.*?</header>", self.archive_text, re.S).group(0)
        self.assertIn('<a href="changelog.html">Short changelog</a>', header)

    def test_version_and_build_id_are_in_the_header(self):
        for text in (self.short_text, self.archive_text):
            header = re.search(r"<header>.*?</header>", text, re.S).group(0)
            self.assertIn("v1.2.0 - build abc1234", header)

    def test_without_version_and_build_the_header_has_no_build_line(self):
        out = os.path.join(self.tmp.name, "plain.html")
        result = run_tool(self.short_md, out)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("buildline\">", read_text(out))

    def test_every_version_heading_is_a_section_with_a_nav_entry(self):
        for anchor in ("v1-2-0", "v1-1-0"):
            self.assertIn('<section id="%s">' % anchor, self.short_text)
            self.assertIn('<a href="#%s">' % anchor, self.short_text)
        self.assertEqual(self.short_text.count("<section "), 4)             # Unreleased, v1.2.0, v1.1.0, Older versions
        self.assertEqual(self.archive_text.count("<section "), 1)

    def test_the_template_labels_and_bullets_render(self):
        section = re.search(r'<section id="v1-2-0">.*?</section>', self.short_text, re.S).group(0)
        for label in ("For players:", "Rules / network:", "Fixes:", "Details:"):
            self.assertIn("<strong>%s</strong>" % label, section)
        self.assertEqual(section.count("<li>"), 3)                          # two player bullets and one fix
        self.assertIn("network protocol 12", section)

    def test_links_go_to_the_site_or_to_the_repository(self):
        # the short page: the archive's path becomes the page of the site, README.md goes to the repository, an absolute link stays
        self.assertIn('<a href="changelog_archive.html">link to the archive</a>', self.short_text)
        self.assertIn('href="https://github.com/dchadd427/ants-cross-platform/blob/main/README.md"', self.short_text)
        self.assertIn('href="https://github.com/example/ants/compare/aaaaaaa...bbbbbbb"', self.short_text)
        # the archive page: ../CHANGELOG.md is the short page, ../README.md and audit/B2_notes.md are files of the repository (relative to docs/)
        self.assertIn('<a href="changelog.html">short changelog</a>', self.archive_text)
        self.assertIn('href="https://github.com/dchadd427/ants-cross-platform/blob/main/README.md"', self.archive_text)
        self.assertIn('href="https://github.com/dchadd427/ants-cross-platform/blob/main/docs/audit/B2_notes.md"', self.archive_text)

    def test_an_html_comment_is_dropped_and_text_is_escaped(self):
        self.assertNotIn("never shown", self.short_text)
        self.assertNotIn("v9-9-9", self.short_text)
        self.assertNotIn("must not appear", self.short_text)
        self.assertIn("&lt;angle&gt; brackets &amp; an ampersand", self.short_text)
        self.assertIn("<code>code</code>", self.short_text)

    def test_a_continuation_line_belongs_to_its_bullet(self):
        self.assertIn("<li>A long detailed bullet that continues on a second line.</li>", self.archive_text)

    def test_the_footer_names_the_source_file(self):
        self.assertIn("Generated from CHANGELOG.md when the site image is built", self.short_text)
        self.assertIn("Generated from CHANGELOG_ARCHIVE.md when the site image is built", self.archive_text)

    def test_a_bad_page_link_is_refused(self):
        out = os.path.join(self.tmp.name, "bad.html")
        result = run_tool(self.short_md, out, "--page-link", "no-equals-sign")
        self.assertEqual(result.returncode, 2)


class RealFiles(unittest.TestCase):
    """The repository's own changelog and archive, built the way the Dockerfile builds them."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.short_md = read_text(os.path.join(REPO, "CHANGELOG.md"))
        cls.archive_md = read_text(os.path.join(REPO, "docs", "CHANGELOG_ARCHIVE.md"))
        cls.short_html = os.path.join(cls.tmp.name, "changelog.html")
        cls.archive_html = os.path.join(cls.tmp.name, "changelog_archive.html")
        cls.short_result = build_short(os.path.join(REPO, "CHANGELOG.md"), cls.short_html)
        cls.archive_result = build_archive(os.path.join(REPO, "docs", "CHANGELOG_ARCHIVE.md"), cls.archive_html)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_both_real_pages_build_and_every_heading_is_a_section(self):
        self.assertEqual(self.short_result.returncode, 0, self.short_result.stderr)
        self.assertEqual(self.archive_result.returncode, 0, self.archive_result.stderr)
        for md, html_path in ((self.short_md, self.short_html), (self.archive_md, self.archive_html)):
            body = re.sub(r"<!--.*?-->", "", md, flags=re.S)
            headings = len(re.findall(r"^## ", body, re.M))
            page = read_text(html_path)
            self.assertEqual(page.count("<section "), headings)
            self.assertGreaterEqual(headings, 2)

    def test_the_archive_is_the_whole_old_changelog(self):
        self.assertTrue(self.archive_md.startswith("# Detailed history up to v0.1.0\n"))
        self.assertGreaterEqual(len(re.findall(r"^## v\d", self.archive_md, re.M)), 99)      # the old file had 99 release entries
        self.assertGreater(len(self.archive_md), 400_000)

    def test_the_short_changelog_is_short(self):
        self.assertLess(len(self.short_md), 60_000)
        releases = re.findall(r"^## v\d", self.short_md, re.M)
        self.assertGreaterEqual(len(releases), 1)

    def test_every_release_entry_follows_the_template(self):
        body = re.sub(r"<!--.*?-->", "", self.short_md, flags=re.S)
        parts = re.split(r"^(?=## )", body, flags=re.M)
        entries = [p for p in parts if re.match(r"## v\d+\.\d+\.\d+ - \d{4}-\d{2}-\d{2} - \S", p)]
        self.assertGreaterEqual(len(entries), 1)
        for entry in entries:
            head = entry.splitlines()[0]
            lines = [l for l in entry.splitlines() if l.strip()]
            self.assertGreaterEqual(len(lines), 4, head)
            self.assertLessEqual(len(entry.rstrip("\n").splitlines()), 17, "%s: an entry is 5 - 15 lines (blank ones count)" % head)
            self.assertIn("**For players:**\n", entry, head)
            players = re.search(r"\*\*For players:\*\*\n((?:- .*\n)+)", entry)
            self.assertIsNotNone(players, head)
            self.assertTrue(1 <= len(players.group(1).splitlines()) <= 6, "%s: 1 - 6 bullets under For players" % head)
            self.assertIn("**Details:** [commits](https://github.com/", entry, head)
            # fixed order of the labels
            positions = [entry.find(label) for label in ("**For players:**", "**Rules / network:**", "**Fixes:**", "**Details:**") if label in entry]
            self.assertEqual(positions, sorted(positions), "%s: the labels are in the order For players, Rules / network, Fixes, Details" % head)
            self.assertNotRegex(entry, r"\b[0-9,]+ (assertions|test cases)\b", head)             # no test counts in the short changelog


if __name__ == "__main__":
    unittest.main()
