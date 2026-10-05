#!/usr/bin/env python3
"""Tests of tools/changelog_to_html.py on the two-file changelog layout (run by ./run_tests.sh --fast and by the CI):

  CHANGELOG.md               the short changelog: one entry per release in a fixed template  -> changelog.html (the default page)
  docs/CHANGELOG_ARCHIVE.md  the detailed history up to v0.1.0, frozen                       -> changelog_archive.html

The Docker image builds both pages with the commands below (Dockerfile); this test runs the same commands on the real files and on small made-up ones. The pages are in the Classic look of
the front page (they link the site's /front/classic.css, show the logo, and carry the version and the build in the footer): what is pinned here is their structure, not their colours
(web/front/classic.css and tests/scripts/test_web_front.py own those).
"""
import html.parser
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


GITHUB = "https://github.com/dchadd427/ants-cross-platform"
VOID = {"meta", "link", "img", "br", "hr", "input"}


class Structure(html.parser.HTMLParser):
    """Walks a page: the elements are balanced, and the head's links, the images, the scripts and the anchors are collected."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.stack, self.errors = [], []
        self.links, self.imgs, self.scripts, self.anchors, self.ids, self.text = [], [], [], [], [], []

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if "id" in a:
            self.ids.append(a["id"])
        if tag == "link":
            self.links.append(a)
        elif tag == "img":
            self.imgs.append(a)
        elif tag in ("script", "iframe", "object", "embed"):
            self.scripts.append(tag)
        elif tag == "a":
            self.anchors.append(a)
        if tag not in VOID:
            self.stack.append(tag)

    def handle_endtag(self, tag):
        if tag in VOID:
            return
        if not self.stack or self.stack[-1] != tag:
            self.errors.append("</%s> closes %s" % (tag, self.stack[-1] if self.stack else "nothing"))
            if tag in self.stack:
                while self.stack and self.stack.pop() != tag:
                    pass
        else:
            self.stack.pop()

    def handle_data(self, data):
        self.text.append(data)


def walk(page):
    parser = Structure()
    parser.feed(page)
    parser.close()
    return parser


def part(page, tag, attrs=""):
    """The first <tag ...> ... </tag> of a page (the markup between the tags, the tags included)."""
    return re.search(r"<%s%s[^>]*>.*?</%s>" % (tag, attrs, tag), page, re.S).group(0)


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

    def test_the_two_pages_link_to_each_other_from_the_header_with_play_and_github_beside_them(self):
        header = part(self.short_text, "header")
        self.assertRegex(header, r'<a class="btn sm" href="changelog_archive\.html">Detailed history</a>')
        self.assertRegex(header, r'<a class="btn sm" href="/">Play</a>')                         # back to the game: the front page
        self.assertRegex(header, r'<a class="btn sm" href="%s" target="_blank" rel="noopener noreferrer">GitHub</a>' % re.escape(GITHUB))
        self.assertEqual(re.findall(r'class="btn sm" href="([^"]*)"', header), ["/", "changelog_archive.html", GITHUB])         # the three buttons, in this order
        self.assertIn('<a class="logo" href="/"', header)                                          # and the logo goes to the front page too
        header = part(self.archive_text, "header")
        self.assertRegex(header, r'<a class="btn sm" href="changelog\.html">Short changelog</a>')

    def test_version_and_build_id_are_in_the_footer_with_the_front_pages_ids(self):
        for text in (self.short_text, self.archive_text):
            footer = part(text, "footer")
            pill = re.search(r'<span class="ver" id="game-version-line">(.*?)</span></span>\n', footer, re.S).group(0)
            self.assertEqual(re.sub(r"<[^>]*>", "", pill).strip(), "v1.2.0 - build abc1234")          # the text that the header used to carry
            self.assertIn('<span id="game-version">v1.2.0</span>', pill)
            self.assertIn('<span id="game-build-id">abc1234</span>', pill)
            self.assertNotIn("build abc1234", part(text, "header"))                                   # (one place: the footer)

    def test_without_version_and_build_the_footer_has_no_version_line(self):
        out = os.path.join(self.tmp.name, "plain.html")
        result = run_tool(self.short_md, out)
        self.assertEqual(result.returncode, 0, result.stderr)
        page = read_text(out)
        for needle in ("game-version-line", "game-version", "game-build", "build "):
            self.assertNotIn(needle, page, needle)

    def test_the_version_and_the_build_may_be_given_alone(self):
        only = {}
        for flag, value in (("--version", "v3.4.5"), ("--build-id", "f00ba12")):
            out = os.path.join(self.tmp.name, "only%s.html" % flag.strip("-"))
            self.assertEqual(run_tool(self.short_md, out, flag, value).returncode, 0)
            only[flag] = re.sub(r"<[^>]*>", "", re.search(r'<span class="ver" id="game-version-line">.*?</span></span>', read_text(out), re.S).group(0))
        self.assertEqual(only, {"--version": "v3.4.5", "--build-id": "build f00ba12"})

    def test_every_version_heading_is_a_section_with_a_nav_entry(self):
        for anchor in ("v1-2-0", "v1-1-0"):
            self.assertIn('<section id="%s" class="panel">' % anchor, self.short_text)
            self.assertIn('<a href="#%s">' % anchor, self.short_text)
        self.assertEqual(self.short_text.count("<section "), 4)             # Unreleased, v1.2.0, v1.1.0, Older versions
        self.assertEqual(self.archive_text.count("<section "), 1)

    def test_the_template_labels_and_bullets_render(self):
        section = re.search(r'<section id="v1-2-0" class="panel">.*?</section>', self.short_text, re.S).group(0)
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

    def test_bullets_that_a_blank_line_separates_are_one_list_and_the_markup_is_balanced(self):
        out = os.path.join(self.tmp.name, "loose.html")
        md = os.path.join(self.tmp.name, "loose.md")
        with open(md, "w", encoding="utf-8") as f:
            f.write("# Changelog\n\n## v1.0.0 - 2026-01-01 - Loose\n\n- one\n\n- two\n\n\n- three\n\nA paragraph.\n\n- four\n")
        self.assertEqual(run_tool(md, out).returncode, 0)
        page = read_text(out)
        lists = re.findall(r"<ul>.*?</ul>", page, re.S)
        self.assertEqual([l.count("<li>") for l in lists], [3, 1])                              # one, two and three are a list; a paragraph ends it
        self.assertNotIn("</ul><li>", page)
        self.assertNotIn("</ul></ul>", page)
        result = walk(page)
        self.assertEqual((result.errors, result.stack), ([], []))

    def test_a_continuation_line_belongs_to_its_bullet(self):
        self.assertIn("<li>A long detailed bullet that continues on a second line.</li>", self.archive_text)

    def test_the_footer_names_the_source_file(self):
        self.assertIn("Generated from CHANGELOG.md when the site image is built", self.short_text)
        self.assertIn("Generated from CHANGELOG_ARCHIVE.md when the site image is built", self.archive_text)

    def test_the_pages_are_in_the_classic_look_and_load_only_the_logo_the_font_and_the_one_stylesheet(self):
        for text in (self.short_text, self.archive_text):
            page = walk(text)
            self.assertEqual(page.errors, [])
            self.assertEqual(page.stack, [])
            self.assertEqual([(l.get("rel"), l.get("href")) for l in page.links],
                             [("icon", "/favicon.png"), ("preload", "/front/LibreFranklin-Medium.ttf"), ("stylesheet", "/front/classic.css")])
            self.assertEqual([i.get("src") for i in page.imgs], ["/front/logo.png"])
            self.assertEqual(page.scripts, [])
            style = re.search(r"<style>(.*?)</style>", text, re.S).group(1)
            self.assertNotIn("@import", style)
            self.assertNotIn("url(", style)
            self.assertNotRegex(style, r"--(?:clay|teal|gold|inset|frame|edge|shadow|cream)\s*:")      # (the tokens are the stylesheet's: the page does not make its own)
            self.assertLess(len(style), 4000)
            for needle in ('<div class="screen">', '<h1 class="banner gold">', '<footer class="bar">', '<nav class="versions panel" aria-label="Versions">'):
                self.assertIn(needle, text)
            self.assertEqual(text.count("<h1"), 1)
            self.assertEqual(text.count("<main>"), 1)

    def test_every_place_that_shows_a_text_escapes_it(self):
        out = os.path.join(self.tmp.name, "escape.html")
        md = os.path.join(self.tmp.name, "escape.md")
        with open(md, "w", encoding="utf-8") as f:
            f.write('# A <b>title</b> & more\n\n## v1.0.0 - 2026-01-01 - <i>x</i> & "y"\n\n- a bullet\n')
        result = run_tool(md, out, "https://example.com/a\"b/blob/main/", "--version", "<v>", "--build-id", "x&y", "--other-page", 'o"ther.html', "--other-label", "<o> & co")
        self.assertEqual(result.returncode, 0, result.stderr)
        page = read_text(out)
        for needle in ('<h1 class="banner gold">A &lt;b&gt;title&lt;/b&gt; &amp; more</h1>', '&lt;i&gt;x&lt;/i&gt; &amp; "y"', '<span id="game-version">&lt;v&gt;</span>', '<span id="game-build-id">x&amp;y</span>',
                       '>&lt;o&gt; &amp; co</a>', 'href="o&quot;ther.html"', 'href="https://example.com/a&quot;b"', "Generated from escape.md when"):
            self.assertIn(needle, page, needle)
        self.assertEqual(walk(page).errors, [])
        self.assertNotIn("<b>", page)
        self.assertNotIn("<i>", page)

    def test_the_github_buttons_go_to_the_repository_and_its_issues(self):
        for text in (self.short_text, self.archive_text):
            self.assertEqual(text.count('href="%s"' % GITHUB), 2)                                       # the header button and the footer link
            self.assertIn('href="%s/issues"' % GITHUB, part(text, "footer"))

    def test_a_long_word_cannot_widen_the_page(self):
        out = os.path.join(self.tmp.name, "long.html")
        md = os.path.join(self.tmp.name, "long.md")
        with open(md, "w", encoding="utf-8") as f:
            f.write("# Changelog\n\n## v1.0.0 - 2026-01-01 - Long\n\n- `" + "a" * 400 + "` and " + "b" * 400 + "\n")
        self.assertEqual(run_tool(md, out).returncode, 0)
        style = re.search(r"<style>(.*?)</style>", read_text(out), re.S).group(1)
        panel = re.search(r"\.panel \{([^}]*)\}", style).group(1)
        self.assertIn("overflow-x: auto", panel)                                                        # a line that cannot break scrolls inside its panel
        self.assertIn("overflow-wrap: anywhere", panel)                                                 # and one that can, wraps

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

    def pages(self):
        return ((self.short_md, self.short_html, "changelog_archive.html", "Detailed history", "CHANGELOG.md"),
                (self.archive_md, self.archive_html, "changelog.html", "Short changelog", "CHANGELOG_ARCHIVE.md"))

    def test_the_structure_of_the_real_pages_header_nav_sections_and_footer(self):
        for md, path, other, label, source in self.pages():
            text = read_text(path)
            page = walk(text)
            self.assertEqual((page.errors, page.stack), ([], []), source)
            body = re.sub(r"<!--.*?-->", "", md, flags=re.S)
            versions = re.findall(r"^## (v\d+(?:\.\d+)*)", body, re.M)
            sections = re.findall(r'<section id="([^"]+)" class="panel">', text)
            self.assertEqual(len(sections), len(re.findall(r"^## ", body, re.M)), source)
            self.assertEqual(len(set(sections)), len(sections), source + ": every section has an id of its own")
            self.assertEqual(len(set(page.ids)), len(page.ids), source + ": no id twice on the page")
            nav = part(text, "nav", r' class="versions panel"')
            self.assertEqual(re.findall(r'<a href="#([^"]+)">', nav), sections, source + ": the list of releases is the sections, in order")
            self.assertEqual(len(re.findall(r'<span class="rel">v', text)), len(versions), source + ": every release heading has its version plate")
            header = part(text, "header")
            self.assertEqual(re.findall(r'<a class="btn sm" href="([^"]*)"[^>]*>([^<]*)</a>', header), [("/", "Play"), (other, label), (GITHUB, "GitHub")], source)
            self.assertEqual(re.search(r"<h1 [^>]*>(.*?)</h1>", text).group(1), re.search(r"^# (.*)$", md, re.M).group(1), source)
            footer = part(text, "footer")
            self.assertIn("Generated from %s when the site image is built" % source, footer)
            self.assertIn('id="game-version">v1.2.0<', footer)
            self.assertIn('id="game-build-id">abc1234<', footer)

    def test_every_link_of_the_real_pages_leads_somewhere_that_exists(self):
        for md, path, other, label, source in self.pages():
            text = read_text(path)
            page = walk(text)
            anchors = set(re.findall(r'<section id="([^"]+)"', text))
            for a in page.anchors:
                href = a["href"]
                if href.startswith("#"):
                    self.assertIn(href[1:], anchors, "%s: %s" % (source, href))
                elif href.startswith("https://"):
                    self.assertNotIn(" ", href, source)
                else:
                    self.assertIn(href, ("/", other), "%s: a link that is neither the site, nor the other page, nor an address: %s" % (source, href))     # (a path of the repository is an address, not a file of the site)

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


class DockerfileBuildsThePages(unittest.TestCase):
    """The Dockerfile must build the two pages with the cross links that the commands above carry (without them the short page's 13 links to the detailed history and the archive's link
    back went to GitHub's file view instead of the site's other page)."""

    @classmethod
    def setUpClass(cls):
        cls.dockerfile = read_text(os.path.join(REPO, "Dockerfile"))

    def command(self, input_name):
        found = [line for line in self.dockerfile.splitlines() if "changelog_to_html.py %s " % input_name in line]
        self.assertEqual(len(found), 1, "one command builds %s" % input_name)
        return found[0]

    def test_the_short_page_points_its_links_at_the_detailed_history(self):
        line = self.command("CHANGELOG.md")
        self.assertIn("--page-link docs/CHANGELOG_ARCHIVE.md=changelog_archive.html", line)
        self.assertNotIn("--link-base", line)

    def test_the_detailed_history_points_its_links_at_the_short_page_and_resolves_from_docs(self):
        line = self.command("CHANGELOG_ARCHIVE.md")
        self.assertIn("--page-link CHANGELOG.md=changelog.html", line)
        self.assertIn("--link-base docs", line)


if __name__ == "__main__":
    unittest.main()
