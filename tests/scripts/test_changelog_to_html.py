#!/usr/bin/env python3
"""Tests of tools/changelog_to_html.py (run by ./run_tests.sh --fast and by the CI):

  CHANGELOG.md   the changelog: one entry per release in a fixed template  -> changelog.html (the site's one changelog page)

The Docker image builds the page with the command below (Dockerfile); this test runs the same command on the real file and on small made-up ones. The page is in the Classic look of
the front page (it links the site's /front/classic.css, shows the logo, and carries the version and the build in the footer): what is pinned here is its structure, not its colours
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


def build(md_path, out_path, version="v1.2.0", build_id="abc1234"):
    """The Dockerfile's command, with the same options (the version and the build id) and no others."""
    return run_tool(md_path, out_path, "--version", version, "--build-id", build_id)


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
        self.assertEqual(re.sub(r"<[^>]*>", "", pill).strip(), "v1.2.0 - build abc1234")          # the text that the header used to carry
        self.assertIn('<span id="game-version">v1.2.0</span>', pill)
        self.assertIn('<span id="game-build-id">abc1234</span>', pill)
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
        self.assertEqual(page.scripts, [])
        style = re.search(r"<style>(.*?)</style>", self.text, re.S).group(1)
        self.assertNotIn("@import", style)
        self.assertNotIn("url(", style)
        self.assertNotRegex(style, r"--(?:clay|teal|gold|inset|frame|edge|shadow|cream)\s*:")      # (the tokens are the stylesheet's: the page does not make its own)
        self.assertLess(len(style), 4000)
        for needle in ('<div class="screen">', '<h1 class="banner gold">', '<footer class="bar">', '<nav class="versions panel" aria-label="Versions">'):
            self.assertIn(needle, self.text)
        self.assertEqual(self.text.count("<h1"), 1)
        self.assertEqual(self.text.count("<main>"), 1)

    def test_every_place_that_shows_a_text_escapes_it(self):
        out = os.path.join(self.tmp.name, "escape.html")
        md = os.path.join(self.tmp.name, "escape.md")
        with open(md, "w", encoding="utf-8") as f:
            f.write('# A <b>title</b> & more\n\n## v1.0.0 - 2026-01-01 - <i>x</i> & "y"\n\n- a bullet\n')
        result = run_tool(md, out, "https://example.com/a\"b/blob/main/", "--version", "<v>", "--build-id", "x&y")
        self.assertEqual(result.returncode, 0, result.stderr)
        page = read_text(out)
        for needle in ('<h1 class="banner gold">A &lt;b&gt;title&lt;/b&gt; &amp; more</h1>', '&lt;i&gt;x&lt;/i&gt; &amp; "y"', '<span id="game-version">&lt;v&gt;</span>', '<span id="game-build-id">x&amp;y</span>',
                       'href="https://example.com/a&quot;b"', "Generated from escape.md when"):
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
        nav = part(text, "nav", r' class="versions panel"')
        self.assertEqual(re.findall(r'<a href="#([^"]+)">', nav), sections, "the list of releases is the sections, in order")
        self.assertEqual(len(re.findall(r'<span class="rel">v', text)), len(versions), "every release heading has its version plate")
        header = part(text, "header")
        self.assertEqual(re.findall(r'<a class="btn sm" href="([^"]*)"[^>]*>([^<]*)</a>', header), [("/", "Play"), (GITHUB, "GitHub")])
        self.assertEqual(re.search(r"<h1 [^>]*>(.*?)</h1>", text).group(1), re.search(r"^# (.*)$", self.md, re.M).group(1))
        footer = part(text, "footer")
        self.assertIn("Generated from CHANGELOG.md when the site image is built", footer)
        self.assertIn('id="game-version">v1.2.0<', footer)
        self.assertIn('id="game-build-id">abc1234<', footer)

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


class DockerfileBuildsThePage(unittest.TestCase):
    """The Dockerfile builds the one changelog page with the command that the tests above run (the version and the build id, no other option), and the runner stage copies that one file."""

    @classmethod
    def setUpClass(cls):
        cls.lines = [line.strip() for line in read_text(os.path.join(REPO, "Dockerfile")).splitlines()]

    def test_one_command_builds_the_page_with_the_version_and_the_build_id_and_nothing_else(self):
        found = [line for line in self.lines if "python3 changelog_to_html.py" in line]
        self.assertEqual(found, ['python3 changelog_to_html.py CHANGELOG.md changelog.html --version "${GAME_VERSION}" --build-id "${BUILD_ID}"'])

    def test_the_builder_copies_the_changelog_and_the_tool_and_no_document_of_docs(self):
        self.assertEqual([l for l in self.lines if l.startswith("COPY") and "/src/changelog/" in l and "--from" not in l],
                         ["COPY CHANGELOG.md /src/changelog/CHANGELOG.md", "COPY tools/changelog_to_html.py /src/changelog/changelog_to_html.py"])
        self.assertEqual([l for l in self.lines if re.match(r"COPY\s+(--\S+\s+)*docs", l)], [])

    def test_the_runner_copies_the_one_page(self):
        self.assertEqual([l for l in self.lines if l.startswith("COPY --from=builder /src/changelog/")], ["COPY --from=builder /src/changelog/changelog.html /usr/share/nginx/html/"])


if __name__ == "__main__":
    unittest.main()
