#!/usr/bin/env python3
"""Tests of tools/changelog_to_html.py (run by ./run_tests.sh --fast and by the CI):

  CHANGELOG.md   the changelog: one entry per release in a fixed template  -> changelog.html (the site's one changelog page)

The Docker image builds the page with the command below (Dockerfile); this test runs the same command on the real file and on small made-up ones. The page is in the Classic look of
the front page (it links the site's /front/classic.css, shows the logo, and carries the version and the build in the footer): what is pinned here is its structure, not its colours
(web/front/classic.css and tests/scripts/test_web_front.py own those).

The page is the whole list of releases, which is all that a browser without JavaScript gets, and one small inline script shows it in pages (ten releases to a page, five on a phone, named by the
versions they hold; the address of a release opens its page). The script is RUN here, with node and a small fake of the DOM (tests/scripts/web_changelog_pages_check.js; without node these tests
are skipped and say so), and the check is shown able to fail: broken copies of the page's script are refused by it. The real browsers' check is opt-in: tests/scripts/test_web_changelog.sh.
"""
import html.parser
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TOOL = os.path.join(REPO, "tools", "changelog_to_html.py")

CHANGELOG_MD = """# Changelog

Intro with a [readme link](README.md) and a [link to a document](docs/BOTS.md).

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

**Details:** [commits](https://github.com/example/ants/compare/aaaaaaa...bbbbbbb), [workflow](docs/WORKFLOW.md)

## v1.1.0 - 2026-01-01 - First release

**For players:**
- only bullet, which continues
  on a second line.

**Details:** [commits](https://github.com/example/ants/compare/ccccccc...aaaaaaa), [the changelog](docs/WORKFLOW.md#the-changelog)

## Older versions (v1.0.0 and before)

Every release before v1.1.0 is only in the git history.
"""


def read_text(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


GITHUB = "https://github.com/dchadd427/ants-cross-platform"
BLOB = GITHUB + "/blob/main/"
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


def build(md_path, out_path, version="v1.2.0", build_id="abc1234", build_date="2026-01-02 09:30 PT"):
    """The Dockerfile's command, with the same options (the version, the build id and the build date) and no others."""
    return run_tool(md_path, out_path, "--version", version, "--build-id", build_id, "--build-date", build_date)


class MadeUpFiles(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.md = os.path.join(cls.tmp.name, "CHANGELOG.md")
        with open(cls.md, "w", encoding="utf-8") as f:
            f.write(CHANGELOG_MD)
        cls.html_path = os.path.join(cls.tmp.name, "changelog.html")
        cls.result = build(cls.md, cls.html_path)
        cls.text = read_text(cls.html_path) if os.path.exists(cls.html_path) else ""

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_the_page_is_written(self):
        self.assertEqual(self.result.returncode, 0, self.result.stderr)
        self.assertTrue(self.text.startswith("<!DOCTYPE html>"))

    def test_the_header_has_play_and_github_buttons_and_the_logo_goes_to_the_front_page(self):
        header = part(self.text, "header")
        self.assertRegex(header, r'<a class="btn sm" href="/">Play</a>')                         # back to the game: the front page
        self.assertRegex(header, r'<a class="btn sm" href="%s" target="_blank" rel="noopener noreferrer">GitHub</a>' % re.escape(GITHUB))
        self.assertEqual(re.findall(r'class="btn sm" href="([^"]*)"', header), ["/", GITHUB])         # the two buttons, in this order, and no button for another changelog page
        self.assertIn('<a class="logo" href="/"', header)                                          # and the logo goes to the front page too

    def test_version_and_build_id_are_in_the_footer_with_the_front_pages_ids(self):
        footer = part(self.text, "footer")
        pill = re.search(r'<span class="ver" id="game-version-line">(.*?)</span></span>\n', footer, re.S).group(0)
        self.assertEqual(re.sub(r"<[^>]*>", "", pill).strip(), "v1.2.0 - build abc1234 (2026-01-02 09:30 PT)")          # the text that the header used to carry, with the build date
        self.assertIn('<span id="game-version">v1.2.0</span>', pill)
        self.assertIn('<span id="game-build-id">abc1234</span>', pill)
        self.assertIn('<span id="game-build-date">(2026-01-02 09:30 PT)</span>', pill)
        self.assertNotIn("build abc1234", part(self.text, "header"))                              # (one place: the footer)

    def test_without_version_and_build_the_footer_has_no_version_line(self):
        out = os.path.join(self.tmp.name, "plain.html")
        result = run_tool(self.md, out)
        self.assertEqual(result.returncode, 0, result.stderr)
        page = read_text(out)
        for needle in ("game-version-line", "game-version", "game-build", "build "):
            self.assertNotIn(needle, page, needle)

    def test_the_version_and_the_build_may_be_given_alone(self):
        only = {}
        for flag, value in (("--version", "v3.4.5"), ("--build-id", "f00ba12")):
            out = os.path.join(self.tmp.name, "only%s.html" % flag.strip("-"))
            self.assertEqual(run_tool(self.md, out, flag, value).returncode, 0)
            only[flag] = re.sub(r"<[^>]*>", "", re.search(r'<span class="ver" id="game-version-line">.*?</span></span>', read_text(out), re.S).group(0))
        self.assertEqual(only, {"--version": "v3.4.5", "--build-id": "build f00ba12"})

    def test_every_version_heading_is_a_section_with_a_nav_entry(self):
        for anchor in ("v1-2-0", "v1-1-0"):
            self.assertIn('<section id="%s" class="panel">' % anchor, self.text)
            self.assertIn('<a href="#%s">' % anchor, self.text)
        self.assertEqual(self.text.count("<section "), 4)             # Unreleased, v1.2.0, v1.1.0, Older versions

    def test_the_template_labels_and_bullets_render(self):
        section = re.search(r'<section id="v1-2-0" class="panel">.*?</section>', self.text, re.S).group(0)
        for label in ("For players:", "Rules / network:", "Fixes:", "Details:"):
            self.assertIn("<strong>%s</strong>" % label, section)
        self.assertEqual(section.count("<li>"), 3)                          # two player bullets and one fix
        self.assertIn("network protocol 12", section)

    def test_a_relative_link_goes_to_the_file_in_the_repository_and_an_absolute_one_stays(self):
        self.assertIn('<a href="%sREADME.md">readme link</a>' % BLOB, self.text)
        self.assertIn('<a href="%sdocs/BOTS.md">link to a document</a>' % BLOB, self.text)          # a path of docs/ too
        self.assertIn('<a href="%sdocs/WORKFLOW.md">workflow</a>' % BLOB, self.text)
        self.assertIn('<a href="%sdocs/WORKFLOW.md#the-changelog">the changelog</a>' % BLOB, self.text)          # with its anchor
        self.assertIn('href="https://github.com/example/ants/compare/aaaaaaa...bbbbbbb"', self.text)

    def test_links_are_resolved_from_the_root_of_the_repository_and_those_that_are_not_paths_stay(self):
        md = os.path.join(self.tmp.name, "links.md")
        out = os.path.join(self.tmp.name, "links.html")
        links = (("a", "README.md", BLOB + "README.md"), ("b", "./docs/BOTS.md", BLOB + "docs/BOTS.md"), ("c", "/AGENTS.md", BLOB + "AGENTS.md"), ("d", "docs/", BLOB + "docs/"),
                 ("e", "docs/WORKFLOW.md#version-policy", BLOB + "docs/WORKFLOW.md#version-policy"),
                 ("f", "#v1-0-0", "#v1-0-0"), ("g", "mailto:someone@example.org", "mailto:someone@example.org"), ("h", "https://example.org/x?a=1", "https://example.org/x?a=1"),
                 ("i", "http://example.org/y", "http://example.org/y"))
        with open(md, "w", encoding="utf-8") as f:
            f.write("# Changelog\n\n" + " ".join("[%s](%s)" % (label, url) for label, url, _ in links) + "\n\n## v1.0.0 - 2026-01-01 - Links\n\n- a bullet\n")
        self.assertEqual(run_tool(md, out).returncode, 0)
        page = read_text(out)
        for label, url, want in links:
            self.assertIn('<a href="%s">%s</a>' % (want, label), page, url)

    def test_an_html_comment_is_dropped_and_text_is_escaped(self):
        self.assertNotIn("never shown", self.text)
        self.assertNotIn("v9-9-9", self.text)
        self.assertNotIn("must not appear", self.text)
        self.assertIn("&lt;angle&gt; brackets &amp; an ampersand", self.text)
        self.assertIn("<code>code</code>", self.text)

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
        self.assertIn("<li>only bullet, which continues on a second line.</li>", self.text)

    def test_the_footer_names_the_source_file(self):
        self.assertIn("Generated from CHANGELOG.md when the site image is built", self.text)

    def test_the_page_is_in_the_classic_look_and_loads_only_the_logo_the_font_and_the_one_stylesheet(self):
        page = walk(self.text)
        self.assertEqual(page.errors, [])
        self.assertEqual(page.stack, [])
        self.assertEqual([(l.get("rel"), l.get("href")) for l in page.links],
                         [("icon", "/favicon.png"), ("preload", "/front/LibreFranklin-Medium.ttf"), ("stylesheet", "/front/classic.css")])
        self.assertEqual([i.get("src") for i in page.imgs], ["/front/logo.png"])
        self.assertEqual(page.scripts, ["script"])                                                 # (the one inline script that makes the pages: TheScript, below)
        style = re.search(r"<style>(.*?)</style>", self.text, re.S).group(1)
        self.assertNotIn("@import", style)
        self.assertNotIn("url(", style)
        self.assertNotRegex(style, r"--(?:clay|teal|gold|inset|frame|edge|shadow|cream)\s*:")      # (the tokens are the stylesheet's: the page does not make its own)
        self.assertLess(len(style), 8000)
        for needle in ('<div class="screen">', '<h1 class="banner gold">', '<footer class="bar">', '<details class="versions panel" id="versions" open><summary>Jump to a version</summary><nav aria-label="Versions">'):
            self.assertIn(needle, self.text)
        self.assertEqual(self.text.count("<h1"), 1)
        self.assertEqual(self.text.count("<main>"), 1)

    def test_every_place_that_shows_a_text_escapes_it(self):
        out = os.path.join(self.tmp.name, "escape.html")
        md = os.path.join(self.tmp.name, "escape.md")
        with open(md, "w", encoding="utf-8") as f:
            f.write('# A <b>title</b> & more\n\n## v1.0.0 - 2026-01-01 - <i>x</i> & "y"\n\n- a bullet\n')
        result = run_tool(md, out, "https://example.com/a\"b/blob/main/", "--version", "<v>", "--build-id", "x&y", "--build-date", "d<t")
        self.assertEqual(result.returncode, 0, result.stderr)
        page = read_text(out)
        for needle in ('<h1 class="banner gold">A &lt;b&gt;title&lt;/b&gt; &amp; more</h1>', '&lt;i&gt;x&lt;/i&gt; &amp; "y"', '<span id="game-version">&lt;v&gt;</span>', '<span id="game-build-id">x&amp;y</span>',
                       '<span id="game-build-date">(d&lt;t)</span>', 'href="https://example.com/a&quot;b"', "Generated from escape.md when"):
            self.assertIn(needle, page, needle)
        self.assertEqual(walk(page).errors, [])
        self.assertNotIn("<b>", page)
        self.assertNotIn("<i>", page)

    def test_the_github_buttons_go_to_the_repository_and_its_issues(self):
        self.assertEqual(self.text.count('href="%s"' % GITHUB), 2)                                       # the header button and the footer link
        self.assertIn('href="%s/issues"' % GITHUB, part(self.text, "footer"))

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

    def test_the_options_of_the_two_page_layout_are_gone_and_refused(self):
        out = os.path.join(self.tmp.name, "refused.html")
        for option in (("--other-page", "other.html"), ("--other-label", "Other"), ("--page-link", "README.md=readme.html"), ("--link-base", "docs")):
            result = run_tool(self.md, out, *option)
            self.assertEqual(result.returncode, 2, option)
            self.assertIn("unrecognized arguments", result.stderr, option)


class RealFile(unittest.TestCase):
    """The repository's own changelog, built the way the Dockerfile builds it."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.md = read_text(os.path.join(REPO, "CHANGELOG.md"))
        cls.html_path = os.path.join(cls.tmp.name, "changelog.html")
        cls.result = build(os.path.join(REPO, "CHANGELOG.md"), cls.html_path)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_the_real_page_builds_and_every_heading_is_a_section(self):
        self.assertEqual(self.result.returncode, 0, self.result.stderr)
        body = re.sub(r"<!--.*?-->", "", self.md, flags=re.S)
        headings = len(re.findall(r"^## ", body, re.M))
        page = read_text(self.html_path)
        self.assertEqual(page.count("<section "), headings)
        self.assertGreaterEqual(headings, 2)

    def test_the_structure_of_the_real_page_header_nav_sections_and_footer(self):
        text = read_text(self.html_path)
        page = walk(text)
        self.assertEqual((page.errors, page.stack), ([], []))
        body = re.sub(r"<!--.*?-->", "", self.md, flags=re.S)
        versions = re.findall(r"^## (v\d+(?:\.\d+)*)", body, re.M)
        sections = re.findall(r'<section id="([^"]+)" class="panel">', text)
        self.assertEqual(len(sections), len(re.findall(r"^## ", body, re.M)))
        self.assertEqual(len(set(sections)), len(sections), "every section has an id of its own")
        self.assertEqual(len(set(page.ids)), len(page.ids), "no id twice on the page")
        nav = part(text, "nav", r' aria-label="Versions"')
        self.assertEqual(re.findall(r'<a href="#([^"]+)">', nav), sections, "the list of releases is the sections, in order")
        self.assertEqual(len(re.findall(r'<span class="rel">v', text)), len(versions), "every release heading has its version plate")
        header = part(text, "header")
        self.assertEqual(re.findall(r'<a class="btn sm" href="([^"]*)"[^>]*>([^<]*)</a>', header), [("/", "Play"), (GITHUB, "GitHub")])
        self.assertEqual(re.search(r"<h1 [^>]*>(.*?)</h1>", text).group(1), re.search(r"^# (.*)$", self.md, re.M).group(1))
        footer = part(text, "footer")
        self.assertIn("Generated from CHANGELOG.md when the site image is built", footer)
        self.assertIn('id="game-version">v1.2.0<', footer)
        self.assertIn('id="game-build-id">abc1234<', footer)
        self.assertIn('id="game-build-date">(2026-01-02 09:30 PT)<', footer)

    def test_every_link_of_the_real_page_leads_somewhere_that_exists(self):
        text = read_text(self.html_path)
        page = walk(text)
        anchors = set(re.findall(r'<section id="([^"]+)"', text))
        for a in page.anchors:
            href = a["href"]
            if href.startswith("#"):
                self.assertIn(href[1:], anchors, href)
            elif href.startswith(BLOB):
                path = href[len(BLOB):].split("#", 1)[0]
                self.assertTrue(os.path.exists(os.path.join(REPO, path)), "a link of the changelog names %s, which the repository does not have" % path)
            elif href.startswith("https://"):
                self.assertNotIn(" ", href)
            else:
                self.assertEqual(href, "/", "a link that is neither the front page of the site nor an address: %s" % href)     # (a path of the repository is an address, not a file of the site)

    def test_the_oldest_release_that_the_file_holds_is_the_one_the_readme_the_workflow_and_the_footer_name(self):
        oldest = re.findall(r"^## v(\d+\.\d+\.\d+)", self.md, re.M)[-1]
        major, minor, number = (int(n) for n in oldest.split("."))
        first = "v" + oldest
        last_of_the_line = {(0, 1, 0): "v0.0.99", (0, 2, 0): "v0.1.3", (0, 3, 0): "v0.2.0", (0, 4, 0): "v0.3.0", (0, 5, 0): "v0.4.0", (0, 6, 0): "v0.5.1", (0, 7, 0): "v0.6.0"}                               # (v0.1.0 followed v0.0.99, v0.2.0 followed v0.1.3, v0.3.0 followed v0.2.0, v0.4.0 followed v0.3.0, v0.5.0 followed v0.4.0, v0.6.0 followed v0.5.1, v0.7.0 followed v0.6.0)
        before = last_of_the_line.get((major, minor, number), "v%d.%d.%d" % (major, minor, number - 1))
        self.assertIn("## Older versions (%s and before)" % before, self.md)
        self.assertIn("Every release before " + first + ",", self.md)
        for path in ("README.md", os.path.join("docs", "WORKFLOW.md")):
            text = read_text(os.path.join(os.path.dirname(__file__), "..", "..", path))
            self.assertIn("before " + first + " are only in git history", text, path)
        self.assertIn("releases since " + first + " are in the short", read_text(os.path.join(REPO, "README.md")))

    def test_the_changelog_is_short(self):
        self.assertLess(len(self.md), 60_000)
        releases = re.findall(r"^## v\d", self.md, re.M)
        self.assertGreaterEqual(len(releases), 1)

    def test_every_release_entry_follows_the_template(self):
        body = re.sub(r"<!--.*?-->", "", self.md, flags=re.S)
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
            self.assertNotRegex(entry, r"\b[0-9,]+ (assertions|test cases)\b", head)             # no test counts in the changelog


NODE = shutil.which("node")
PAGES_CHECK = os.path.join(REPO, "tests", "scripts", "web_changelog_pages_check.js")


def script_of(page):
    return re.search(r"<script>\n(.*?)\n</script>", page, re.S).group(1)


def releases_md(n, older=True):
    """A changelog of n releases (v1.0.n newest first), with the Older versions section at the end when asked."""
    out = ["# Changelog\n\nIntro.\n"]
    for k in range(n, 0, -1):
        out.append("## v1.0.%d - 2026-01-%02d - Release %d\n\n**For players:**\n- bullet %d\n\n**Details:** [commits](https://github.com/example/ants/compare/aaaaaaa...bbbbbbb)\n" % (k, 1 + k % 28, k, k))
    if older:
        out.append("## Older versions (v0.9.0 and before)\n\nEvery release before v1.0.1 is only in the git history.\n")
    return "\n".join(out)


def page_of(tmp, name, md, *extra):
    src, out = os.path.join(tmp, name + ".md"), os.path.join(tmp, name + ".html")
    with open(src, "w", encoding="utf-8") as f:
        f.write(md)
    result = run_tool(src, out, *extra)
    assert result.returncode == 0, result.stderr
    return read_text(out)


def run_pages_check(page_path):
    return subprocess.run([NODE, PAGES_CHECK, page_path], capture_output=True, text=True, timeout=120)


class ThePagesOfThePage(unittest.TestCase):
    """The page is the whole list, and an inline script makes the pages: what the page holds for the script, and what it must never do."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.real = page_of(cls.tmp.name, "real", read_text(os.path.join(REPO, "CHANGELOG.md")), "--version", "v1.2.0", "--build-id", "abc1234")
        cls.long = page_of(cls.tmp.name, "long", releases_md(55))
        cls.short = page_of(cls.tmp.name, "short", releases_md(3, older=False))

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_without_the_script_the_page_is_the_whole_list(self):
        for page in (self.real, self.long, self.short):
            bare = re.sub(r"<script>.*?</script>", "", re.sub(r"<style>.*?</style>", "", page, flags=re.S), flags=re.S)
            self.assertNotRegex(bare, r"<[^>]*\shidden[\s>=]")                                         # (no release is hidden by the page: only the script hides, and only when it runs)
            self.assertNotRegex(bare, r"pager|pages-top|class=\"tab|class=\"foot")             # (the tabs and the Newer / Older bar are made by the script)
            self.assertIn('<details class="versions panel" id="versions" open>', bare)         # (the list of versions is open: it is all that a visitor without JavaScript has to jump with)
            sections = re.findall(r'<section id="([^"]+)" class="panel">', bare)
            nav = re.search(r'<nav aria-label="Versions">(.*?)</nav>', bare, re.S).group(1)
            self.assertEqual(re.findall(r'<a href="#([^"]+)">', nav), sections)

    def test_one_inline_script_after_the_releases_and_before_the_footer(self):
        for page in (self.real, self.long, self.short):
            self.assertEqual(page.count("<script"), 1)
            self.assertEqual(page.count("</script>"), 1)
            self.assertIn("</main>\n<script>\n(function () {", page)
            self.assertLess(page.index("</script>"), page.index("<footer"))
            self.assertNotRegex(page, r"<script[^>]*\b(src|type|defer|async)=")
            self.assertNotIn("@", script_of(page))                                           # (every placeholder of the template was filled)

    def test_the_script_is_the_same_for_every_changelog(self):
        self.assertEqual(script_of(self.real), script_of(self.long))
        self.assertEqual(script_of(self.real), script_of(self.short))

    def test_ten_to_a_page_five_on_a_phone_up_to_the_stylesheets_phone_width(self):
        script = script_of(self.real)
        self.assertIn("var SIZE = 10, PHONE_SIZE = 5, PHONE_QUERY = '(max-width: 700px)';", script)
        self.assertIn("@media (max-width: 700px) {", self.real)
        self.assertIn("Math.round(secs.length / size)", script)                                 # (the count of pages is the rounded quotient: the last page has the rest, never less than half a page)

    def test_the_script_makes_nothing_from_text_and_reaches_for_nothing(self):
        script = script_of(self.real)
        for word in ("innerHTML", "outerHTML", "insertAdjacentHTML", "document.write", "eval", "Function(", "setTimeout", "setInterval", "fetch", "XMLHttpRequest", "localStorage", "sessionStorage",
                     "http", "cookie", "import(", "</script", "<!--"):
            self.assertNotIn(word, script, word)
        for word in ("document.createElement", "textContent", "location.hash", "hashchange"):
            self.assertIn(word, script, word)

    def test_the_pager_looks_like_the_pages_buttons_and_every_deep_link_is_framed_in_gold(self):
        style = re.search(r"<style>(.*?)</style>", self.real, re.S).group(1)
        for needle in (".pager .tab {", ".pager .tab.on {", ".pager .off {", ".versions a.here {", "details.versions {"):
            self.assertIn(needle, style, needle)
        # a release that a link names is framed in gold: by :target when the page is the plain list, by the class "deep" (the script sets it for the dotted address too, and not for a click in the bars) when it is paged
        self.assertIn("main:not(.paged) section:target { border-color: var(--gold); }", style)
        self.assertIn("section.deep { border-color: var(--gold); }", style)
        self.assertNotIn("section:target {", style.replace("main:not(.paged) section:target {", ""))                              # (no :target rule that the paged page could not switch off)
        self.assertNotIn("section.first", style)                                                                                  # (the first release of a page is framed like any other that a link names)
        self.assertNotIn("nav.versions", style)

    def test_the_bars_wrap_and_the_tabs_keep_one_width_in_a_grid(self):
        style = re.search(r"<style>(.*?)</style>", self.real, re.S).group(1)
        self.assertRegex(style, r"\.pager \{ display: flex; flex-wrap: wrap;")
        self.assertRegex(style, r"\.versions nav \{ display: flex; flex-wrap: wrap;")
        self.assertIn(".pager.tabs { display: grid; grid-template-columns: repeat(auto-fill, minmax(140px, 1fr)); }", style)           # (a last row with fewer tabs leaves its cells empty: the tabs do not stretch)
        phone = style[style.index("@media (max-width: 700px)"):]
        self.assertIn(".pager.tabs { grid-template-columns: repeat(3, minmax(0, 1fr)); }", phone)
        self.assertNotRegex(style, r"\.pager \.tab \{[^}]*\bflex:")

    def test_the_current_tab_and_a_button_that_leads_nowhere_look_different_from_the_others(self):
        style = re.search(r"<style>(.*?)</style>", self.real, re.S).group(1)
        rule = lambda selector: re.search(re.escape(selector) + r" \{([^}]*)\}", style).group(1)
        face = lambda text: re.search(r"background: ([^;]+);", text).group(1)
        self.assertNotEqual(face(rule(".pager .tab")), face(rule(".pager .tab.on")))
        classic = re.sub(r"/\*.*?\*/", "", read_text(os.path.join(REPO, "web", "front", "classic.css")), flags=re.S)
        pressed = re.search(r"\.btn\.on \{([^}]*)\}", classic).group(1)
        for prop in ("background", "color", "transform", "box-shadow"):                          # (the chosen page is a pressed button: what .btn.on of the stylesheet is)
            self.assertEqual(re.search(prop + r": ([^;]+);", rule(".pager .tab.on")).group(1), re.search(r"(?<![-\w])" + prop + r": ([^;]+);", pressed).group(1), prop)
        self.assertNotEqual(re.search(r"box-shadow: ([^;]+);", rule(".pager .tab")).group(1), re.search(r"box-shadow: ([^;]+);", rule(".pager .tab.on")).group(1))
        self.assertLess(float(re.search(r"opacity: (\.?\d+(?:\.\d+)?);", rule(".pager .off")).group(1)), 1.0)           # (Newer on the first page and Older on the last are dim, and take no click)
        self.assertIn("pointer-events: none", rule(".pager .off"))

    def test_a_tab_is_at_least_a_finger_wide_and_tall(self):
        style = re.search(r"<style>(.*?)</style>", self.real, re.S).group(1)
        self.assertRegex(style, r"\.pager \.tab \{[^}]*min-height: 52px")
        phone = style[style.index("@media (max-width: 700px)"):]
        self.assertRegex(phone, r"\.pager \.prev, \.pager \.next \{[^}]*min-height: 44px")


@unittest.skipUnless(NODE, "node is not installed: the changelog page's script was NOT run (tests/scripts/web_changelog_pages_check.js)")
class TheScriptRunByNode(unittest.TestCase):
    """The script of the generated page, run against the page with a small fake of the DOM (tests/scripts/web_changelog_pages_check.js), and the check shown able to fail."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.real = page_of(cls.tmp.name, "real", read_text(os.path.join(REPO, "CHANGELOG.md")))

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def write(self, name, page):
        path = os.path.join(self.tmp.name, name + ".html")
        with open(path, "w", encoding="utf-8") as f:
            f.write(page)
        return path

    def test_every_check_holds_on_the_real_page(self):
        done = run_pages_check(self.write("real", self.real))
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r"web changelog pages: \d+ checks, 0 failed")

    def test_and_on_changelogs_of_other_lengths_on_both_sides_of_every_rounding(self):
        for n in (15, 16, 20, 24, 25, 34, 35, 52, 55, 100):
            for older in (True, False):
                page = page_of(self.tmp.name, "n%d%s" % (n, older), releases_md(n, older))
                done = run_pages_check(self.write("n%d%s" % (n, older), page))
                self.assertEqual(done.returncode, 0, "%d releases, older=%s: %s%s" % (n, older, done.stdout, done.stderr))

    def test_a_page_without_the_script_is_refused(self):
        bare = re.sub(r"<script>.*?</script>", "", self.real, flags=re.S)
        done = run_pages_check(self.write("bare", bare))
        self.assertEqual(done.returncode, 1)
        self.assertIn("no inline script", done.stdout)

    # (what to break in the page's script, and the words for it). Every copy has to be refused: a check that none of these fails proves nothing.
    BROKEN = [
        ("the count of pages is cut, not rounded", "Math.round(secs.length / size)", "Math.floor(secs.length / size)"),
        ("a short list is paged too", "if (pages < 2) {", "if (pages < 1) {"),
        ("the address is read in its own case", ".toLowerCase()", ""),
        ("a dot in the address is not a dash", ".replace(/\\./g, '-')", ""),
        ("the wrong releases are hidden", "secs[k].hidden = pageOf(k) !== p;", "secs[k].hidden = pageOf(k) === p;"),
        ("the list of versions is not folded", "versions.removeAttribute('open')", "versions.setAttribute('open', '')"),
        ("Back and Forward do nothing", "window.addEventListener('hashchange', route);", ""),
        ("ten to a page is twelve", "var SIZE = 10,", "var SIZE = 12,"),
        ("five on a phone is six", "PHONE_SIZE = 5,", "PHONE_SIZE = 6,"),
        ("the first release of a page is not what the tab scrolls to", "(k === firstOf(cur) ? top : secs[k]).scrollIntoView()", "secs[k].scrollIntoView()"),
        ("the focus is lost after a click on the bar", "tabs[cur].focus({ preventScroll: true })", "tabs[cur].focus({})"),
        ("a click in the bars is not noted", "clicked = a ? a.getAttribute('href') : null;", "clicked = null;"),
        ("every change of the address takes the focus", "keep = clicked !== null && clicked === location.hash", "keep = true"),
        ("a click on the words inside a tab is not a click on the tab", "while (a && a.tagName !== 'A') a = a.parentNode;", ""),
        ("the bar at the top is not told of clicks", "    top.addEventListener('click', noted);\n", ""),
        ("the bar at the bottom is not told of clicks", "    foot.addEventListener('click', noted);\n", ""),
        ("a release has no label but its id", "labels[secs[k].id] || secs[k].id", "secs[k].id"),
        ("a page that ends in Older versions is named by it", "/^v\\d/.test(label(k)) ? label(k) : 'older'", "label(k)"),
        ("an older browser is not told of the resize", " else if (mq.addListener) mq.addListener(resized);", ";"),
        ("an address that names nothing resets the page", "if (!top || k === -1) return;", "if (!top) return;"),
        ("the last page ends early", "p === pages - 1 ? secs.length - 1 : (p + 1) * size - 1", "(p + 1) * size - 1"),
        ("a release after the last full page has no page", "Math.min(Math.floor(k / size), pages - 1)", "Math.floor(k / size)"),
        ("a deep-linked release is not framed", "secs[j].classList.toggle('deep', j === k);", "secs[j].classList.toggle('deep', false);"),
        ("a click in the bars frames the first release of the page", "frame(keep ? -1 : k);", "frame(k);"),
        ("the page that opens on an address has no frame", "if (top) { frame(start); reveal(start); }", "if (top) { reveal(start); }"),
        ("a paged list is not marked as paged", "main.classList.add('paged');", ""),
        ("a list of one page stays marked as paged", "main.classList.remove('paged');", ""),
        ("aria-disabled stays on a button after the first page", "e.removeAttribute('aria-disabled'); ", ""),
        ("the dim class stays on a button after the first page", "e.classList.remove('off'); }", "}"),
        ("a name that every object has (constructor) is a release", "return index.hasOwnProperty(id) ? index[id] : -1;", "return index[id] !== undefined ? index[id] : -1;"),
        ("Newer stays a link on the first page", "if (p > 0) link(prev, p - 1); else {", "if (p > -1) link(prev, p - 1); else {"),
        ("the bar names the page before", "where.textContent = 'Page ' + (p + 1) + ' of ' + pages;", "where.textContent = 'Page ' + p + ' of ' + pages;"),
        ("a resize leaves the old bars", "    drop(top);\n    drop(foot);\n", ""),
        ("text becomes markup", "if (text) e.textContent = text;", "if (text) e.innerHTML = text;"),
        ("the list of versions does not say which are on the page", "chips[k].classList.toggle('here', index.hasOwnProperty(id) && pageOf(index[id]) === p);", ""),
        ("the page that opens on an address is not scrolled", "if (top) { frame(start); reveal(start); }", "if (top) { frame(start); }"),
        ("a resize loses the release in sight", "apply(top ? firstOf(cur) : 0)", "apply(0)"),
        ("an invalid address is read as no address", "catch (e) { return -1; }", "catch (e) { return -2; }"),
        ("the tab of the page is not marked as the current one", "tabs[k].setAttribute('aria-current', 'page')", "tabs[k].setAttribute('aria-current', 'true')"),
    ]

    def test_the_check_is_shown_able_to_fail_on_broken_copies_of_the_script(self):
        script = script_of(self.real)
        survivors = []
        for number, (what, old, new) in enumerate(self.BROKEN):
            self.assertEqual(script.count(old), 1, "%s: the text to break occurs %d times in the script" % (what, script.count(old)))
            page = self.real.replace(script, script.replace(old, new))
            self.assertNotEqual(page, self.real, what)
            done = run_pages_check(self.write("broken%d" % number, page))
            if done.returncode == 0:
                survivors.append(what)
        self.assertEqual(survivors, [], "the check did not notice these broken copies of the script")


class DockerfileBuildsThePage(unittest.TestCase):
    """The Dockerfile builds the one changelog page with the command that the tests above run (the version, the build id and the build date, no other option), and the runner stage copies that one file."""

    @classmethod
    def setUpClass(cls):
        cls.lines = [line.strip() for line in read_text(os.path.join(REPO, "Dockerfile")).splitlines()]

    def test_one_command_builds_the_page_with_the_version_and_the_build_id_and_nothing_else(self):
        found = [line for line in self.lines if "python3 changelog_to_html.py" in line]
        self.assertEqual(found, ['python3 changelog_to_html.py CHANGELOG.md changelog.html --version "${GAME_VERSION}" --build-id "${BUILD_ID}" --build-date "${BUILD_DATE}"'])

    def test_the_builder_copies_the_changelog_and_the_tool_and_no_document_of_docs(self):
        self.assertEqual([l for l in self.lines if l.startswith("COPY") and "/src/changelog/" in l and "--from" not in l],
                         ["COPY CHANGELOG.md /src/changelog/CHANGELOG.md", "COPY tools/changelog_to_html.py /src/changelog/changelog_to_html.py"])
        self.assertEqual([l for l in self.lines if re.match(r"COPY\s+(--\S+\s+)*docs", l)], [])

    def test_the_runner_copies_the_one_page(self):
        self.assertEqual([l for l in self.lines if l.startswith("COPY --from=builder /src/changelog/")], ["COPY --from=builder /src/changelog/changelog.html /usr/share/nginx/html/"])


if __name__ == "__main__":
    unittest.main()
