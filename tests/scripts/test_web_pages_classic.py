#!/usr/bin/env python3
"""Every page of the site is in the Classic look of the front page (run by ./run_tests.sh --fast and by the CI).

The site's pages are found, not listed: the html files of web/ (the front page and the game page: the Dockerfile copies the folder, the image serves them at "/", /lobby.html, /index.html and
/play.html), every html file of asset_catalog/ (Sprites and sounds, copied whole), and the pages that the Dockerfile generates from the changelogs with tools/changelog_to_html.py (built
here the way the image builds them). A page is in the Classic look when it links the shared stylesheet, /front/classic.css, or, for the two pages that keep an inline copy of it
(lobby.html and shell.html), defines the same colours as that stylesheet. A page that is not fails the test by name, so that a new page, or an old one that is redone, cannot be
forgotten when the look moves on.

  - PENDING names the pages that are known not to be there yet. The test needs them to stay that way: when one is restyled the test fails and says to remove it from the list.
  - the colours of the inline copies are the stylesheet's (the three places cannot drift apart)
  - everything a Classic page loads from /front/ is in web/front/ (and so in the image)
  - the checks themselves are tried on made-up pages, one in the old look and one in the new
"""
import os
import re
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FRONT = os.path.join(REPO, "web", "front")

# Pages that are not in the Classic look yet. The coordinator removes a line when the branch that restyles that page is merged (this test then says so).
PENDING = {
}

# The colours that a page must share with the stylesheet to be in the look without linking it (the names of the front page's own :root).
TOKENS = ("clay", "ink", "teal", "teal-hi", "teal-lo", "edge", "cream", "inset", "frame", "frame-hi", "frame-lo", "shadow", "gold", "bad-bg", "bad-ink", "bad-edge")
LINK = re.compile(r'<link\b(?=[^>]*\brel="stylesheet")(?=[^>]*\bhref="/front/classic\.css")[^>]*>')


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def tokens_of(css):
    """{name: value} of the colours that a stylesheet or a page's inline style defines ("--clay: #db4b13;")."""
    return {m.group(1): m.group(2).lower() for m in re.finditer(r"--([a-z-]+):\s*(#[0-9a-fA-F]{6})\s*;", css)}


def is_classic(page):
    """(True, how) when the page links the shared stylesheet or carries all of its colours, else (False, why)."""
    page = re.sub(r"<!--.*?-->", "", page, flags=re.S)                    # (a link in a comment is no link)
    if LINK.search(page):
        return True, "it links /front/classic.css"
    own = tokens_of("\n".join(re.findall(r"<style\b[^>]*>(.*?)</style>", page, re.S)))
    shared = tokens_of(read("web", "front", "classic.css"))
    missing = [t for t in TOKENS if own.get(t) != shared[t]]
    if not missing:
        return True, "it carries the colours of the stylesheet"
    return False, "it neither links /front/classic.css nor carries its colours (missing or different: %s)" % ", ".join(missing[:6])


def generated_pages():
    """{page name: text}: the pages that the Dockerfile builds from the changelogs, made here with the same tool and the same arguments."""
    dockerfile = read("Dockerfile")
    calls = re.findall(r"python3 changelog_to_html\.py (\S+) (\S+\.html)([^&\n]*)", dockerfile)
    pages = {}
    with tempfile.TemporaryDirectory() as tmp:
        for source, target, rest in calls:
            md = {"CHANGELOG.md": ("CHANGELOG.md",), "CHANGELOG_ARCHIVE.md": ("docs", "CHANGELOG_ARCHIVE.md")}.get(source)
            if md is None:
                raise AssertionError("the Dockerfile builds a page from %s: this test does not know where that file is" % source)
            args = [sys.executable, os.path.join(REPO, "tools", "changelog_to_html.py"), os.path.join(REPO, *md), os.path.join(tmp, target), "--version", "v0.0.0", "--build-id", "0000000"]
            other = re.search(r"--other-page (\S+) --other-label \"([^\"]+)\"", rest)
            if other:
                args += ["--other-page", other.group(1), "--other-label", other.group(2)]
            done = subprocess.run(args, capture_output=True, text=True)
            if done.returncode != 0:
                raise AssertionError("tools/changelog_to_html.py failed for %s: %s" % (source, done.stderr))
            with open(os.path.join(tmp, target), encoding="utf-8") as f:
                pages[target + " (generated from " + source + ")"] = f.read()
    return pages


def site_pages():
    pages = {}
    for folder in ("web", "asset_catalog"):
        for root, _, files in os.walk(os.path.join(REPO, folder)):
            for name in files:
                if name.endswith((".html", ".htm")):
                    path = os.path.join(root, name)
                    with open(path, encoding="utf-8") as f:
                        pages[os.path.relpath(path, REPO).replace(os.sep, "/")] = f.read()
    pages.update(generated_pages())
    return pages


PAGES = None


def pages():
    global PAGES
    if PAGES is None:
        PAGES = site_pages()
    return PAGES


class TheSitesPages(unittest.TestCase):
    def test_the_pages_are_the_ones_that_the_image_serves(self):
        found = sorted(pages())
        self.assertEqual(found, ["asset_catalog/index.html", "changelog.html (generated from CHANGELOG.md)", "changelog_archive.html (generated from CHANGELOG_ARCHIVE.md)", "web/lobby.html", "web/shell.html", "web/watch.html"],
                         "the site has a page that this list does not name (or lost one): a new page needs the Classic look too (link /front/classic.css), then name it here")

    def test_every_page_is_in_the_classic_look_but_the_pending_ones(self):
        for name, page in sorted(pages().items()):
            if name in PENDING:
                continue
            ok, why = is_classic(page)
            self.assertTrue(ok, "%s is not in the Classic look: %s. Link /front/classic.css (see web/front/classic.css), or add the page to PENDING with the reason." % (name, why))

    def test_a_pending_page_is_still_pending(self):
        for name, reason in PENDING.items():
            self.assertIn(name, pages(), "%s is in PENDING but is not a page of the site any more: remove it" % name)
            ok, why = is_classic(pages()[name])
            self.assertFalse(ok, "%s is in the Classic look now (%s): remove it from PENDING in tests/scripts/test_web_pages_classic.py (%s)" % (name, why, reason))


class TheReleasePlates(unittest.TestCase):
    def test_a_release_plate_has_the_corners_and_the_sheen_of_the_buttons(self):
        # "v0.8.1" on a changelog page is a teal plate with the buttons' bevel: it is cut and lit like them (the owner, 2026-10-05: "round a little more", "less flat")
        classic = re.sub(r"/\*.*?\*/", "", read("web", "front", "classic.css"), flags=re.S)
        button = re.search(r"\.btn, \.banner \{([^}]*)\}", classic).group(1)
        radius = re.search(r"border-radius: (\d+px);", button).group(1)
        face = re.search(r"background: ([^;]+);", button).group(1)
        self.assertEqual(face, "var(--teal) var(--sheen)")
        found = 0
        for name, page in sorted(pages().items()):
            if "generated" not in name:
                continue
            plate = re.search(r"\.rel \{([^}]*)\}", page)
            self.assertIsNotNone(plate, name)
            self.assertIn("border-radius: " + radius + ";", plate.group(1), name)
            self.assertIn("background: " + face.replace("var(--sheen)", "var(--sheen, none)") + ";", plate.group(1), name)       # (the plate takes the token from the style sheet: flat teal, not transparent, with an older one)
            found += 1
        self.assertEqual(found, 2)


class TheInlineCopies(unittest.TestCase):
    def test_the_front_pages_colours_are_the_stylesheets(self):
        shared = tokens_of(read("web", "front", "classic.css"))
        lobby = read("web", "lobby.html")
        own = tokens_of(lobby[:lobby.index("</style>")])
        for name in TOKENS:
            self.assertEqual(own.get(name), shared[name], "--%s of web/lobby.html is not the stylesheet's" % name)

    def test_a_page_that_links_the_stylesheet_does_not_make_colours_of_its_own(self):
        shared = tokens_of(read("web", "front", "classic.css"))
        for name, page in sorted(pages().items()):
            if not LINK.search(page):
                continue
            own = tokens_of("\n".join(re.findall(r"<style\b[^>]*>(.*?)</style>", page, re.S)))
            for token, value in own.items():
                if token in shared:
                    self.assertEqual(value, shared[token], "%s redefines --%s: the colours are the stylesheet's" % (name, token))


class TheFilesOfTheLook(unittest.TestCase):
    def test_everything_a_classic_page_loads_from_front_is_in_the_folder(self):
        in_folder = set(os.listdir(FRONT))
        for name, page in sorted(pages().items()):
            if name in PENDING or not LINK.search(page):
                continue
            wanted = set(re.findall(r'(?:href|src)="/front/([^"?#]+)"', page))
            self.assertTrue({"classic.css"} <= wanted, name)
            self.assertEqual(wanted - in_folder, set(), "%s loads files that web/front/ does not have" % name)


class TheCheckItself(unittest.TestCase):
    OLD = '<!DOCTYPE html><html><head><title>x</title><style>:root { --bg:#121513; --panel:#1c231e; --green:#48b870; --gold:#e6b830; }</style></head><body><h1>x</h1></body></html>'
    NEW = '<!DOCTYPE html><html><head><title>x</title><link rel="stylesheet" href="/front/classic.css"></head><body></body></html>'
    NEW_REVERSED = '<!DOCTYPE html><html><head><link href="/front/classic.css" rel="stylesheet"></head><body></body></html>'

    def test_a_page_in_the_old_look_fails_and_says_why(self):
        ok, why = is_classic(self.OLD)
        self.assertFalse(ok)
        self.assertIn("clay", why)

    def test_a_page_that_links_the_stylesheet_passes_in_either_order_of_the_attributes(self):
        self.assertTrue(is_classic(self.NEW)[0])
        self.assertTrue(is_classic(self.NEW_REVERSED)[0])

    def test_a_page_that_names_the_stylesheet_in_a_comment_or_with_another_address_fails(self):
        self.assertFalse(is_classic('<!-- <link rel="stylesheet" href="/front/classic.css"> --><link rel="preload" href="/front/classic.css" as="style">')[0])
        self.assertFalse(is_classic('<link rel="stylesheet" href="/front/classic2.css">')[0])
        self.assertFalse(is_classic('<link rel="stylesheet" href="https://example.com/front/classic.css">')[0])

    def test_a_page_with_the_colours_but_one_changed_fails(self):
        shared = tokens_of(read("web", "front", "classic.css"))
        root = " ".join("--%s: %s;" % (t, shared[t]) for t in TOKENS)
        self.assertTrue(is_classic("<style>:root { %s }</style>" % root)[0])
        self.assertFalse(is_classic("<style>:root { %s }</style>" % root.replace(shared["gold"], "#e6b830"))[0])


if __name__ == "__main__":
    unittest.main()
