#!/usr/bin/env python3
"""The legal notice (run by ./run_tests.sh --fast and by the CI): the Legal page (web/legal.html, served at /legal.html), the same notice in the README's License section, and the footer link to it.

  - the page is the changelog page's look (it links /front/classic.css), loads nothing from another site, runs no script, is short, links the LICENSE and THIRD_PARTY_NOTICES.md of the repository
    (files that exist) and says nothing about what the site stores or for how long (that wording has its own home);
  - the five sentences of the notice are the same words on the page and in the README's License section (the owner approved this wording; a change to one is a change to the other), and what both say about
    the original game's data is the README's old wording;
  - every page of the site that has a footer has the link "Legal" at the end of its footer links, opening in a new tab (a match that runs is not left), the page's own footer marks it as the current page;
  - the Dockerfile copies the page and nginx.conf sends /legal to it with a plain redirect, as it does /changelog.
"""
import glob
import html
import os
import re
import sys
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
GITHUB = "https://github.com/dchadd427/ants-cross-platform"
LEGAL_URL = "https://beta.playants.org/legal.html"

# The notice, as the owner approved it (the page's panels in order, the first sentence of the fourth; the README's first paragraph of "License")
NOTICE = (
    "This game is free to play and free to use.",
    'It is provided "as is", with no warranty of any kind, express or implied.',
    "Its authors are not liable for any damage, data loss or other problem from using it, including online play, the bots, the website and the servers.",
    "There is no promise that the online servers will be up: they may be slow, down, restarted or shut down at any time, with no notice.",
    "It is an unofficial fan remake of the 1998 game Ants, not affiliated with or endorsed by the authors or owners of the original game.",
)


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def plain(text):
    """The words of a piece of HTML or Markdown: no tags, no links' addresses, entities and curly quotes as plain characters, one space between words."""
    text = re.sub(r"\[([^\]]+)\]\([^)\s]+\)", r"\1", text)
    text = re.sub(r"<[^>]+>", "", text)
    text = html.unescape(text).replace("“", '"').replace("”", '"').replace("’", "'").replace("**", "")
    return re.sub(r"\s+", " ", text).strip()


def section(markdown, heading):
    """The text under a ## heading of a Markdown file, up to the next one (the heading line itself is not in it)."""
    start = markdown.index("\n## %s\n" % heading) + len("\n## %s\n" % heading)
    end = markdown.find("\n## ", start)
    return markdown[start:end if end != -1 else len(markdown)]


def footer_links(page):
    """[(href, attributes after it, text)] of the links in the nav of the page's footer."""
    footer = page[page.index('<footer class="bar"'):]
    nav = footer[footer.index('<nav aria-label="Footer links">'):footer.index("</nav>")]
    return [(m.group(1), m.group(2), plain(m.group(3))) for m in re.finditer(r'<(?:a|button)\b[^>]*?\bhref="([^"]*)"([^>]*)>(.*?)</(?:a|button)>', nav, re.S)]


def addresses_that_leave_the_site(page):
    """What in a page would load something from, or lead to, another site than this one and the project's own repository: an address in href, src, srcset, action, poster or data that
    is not a path of this site ("/x", never "//host") or an anchor ("#x") or a link into the repository (the repository's address and then "/" or the end), and any @import or url() in a style."""
    found = []
    for name, value in re.findall(r'\b(href|src|srcset|action|poster|data)="([^"]*)"', page):
        for address in ([part.split()[0] for part in value.split(",") if part.strip()] if name == "srcset" else [value]):
            if address.startswith("/") and not address.startswith("//") or address.startswith("#"):
                continue
            if address == GITHUB or address.startswith(GITHUB + "/"):
                continue
            found.append(address)
    for style in re.findall(r"<style[^>]*>(.*?)</style>", page, re.S):
        found += ["@import" for _ in re.findall(r"@import\b", style)]
        found += ["url(" for _ in re.findall(r"\burl\(", style)]
    return found


def changelog_page():
    sys.path.insert(0, os.path.join(REPO, "tools"))
    try:
        import changelog_to_html
    finally:
        sys.path.pop(0)
    return changelog_to_html.convert(read("CHANGELOG.md"), changelog_to_html.DEFAULT_REPO_BLOB, version="v0.0.0", build_id="0000000")


def pages_with_a_footer():
    """{name: text} of every page of the site that has a footer bar: the pages of web/ (the legal page apart) and the generated changelog page."""
    pages = {}
    for path in sorted(glob.glob(os.path.join(REPO, "web", "*.html"))):
        name = os.path.basename(path)
        text = read("web", name)
        if name != "legal.html" and '<footer class="bar"' in text:
            pages[name] = text
    pages["changelog.html (generated)"] = changelog_page()
    return pages


class TheLegalPage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.page = read("web", "legal.html")
        cls.main = cls.page[cls.page.index("<main>"):cls.page.index("</main>")]
        cls.text = plain(cls.main)

    def test_it_is_a_page_in_the_changelog_pages_look(self):
        self.assertTrue(self.page.startswith("<!DOCTYPE html>"))
        self.assertIn("<title>Ants (1998) - Legal</title>", self.page)
        self.assertIn('<link rel="stylesheet" href="/front/classic.css">', self.page)
        self.assertIn('<h1 class="banner gold">Legal</h1>', self.page)
        self.assertEqual(re.findall(r'class="btn sm" href="([^"]*)"', self.page), ["/", GITHUB])                 # the header's two buttons, as the changelog page's
        self.assertNotIn("<script", self.page)
        self.assertNotIn("@@", self.page)                                                                         # (the page is copied as it is: nothing in it is filled in by the build)
        for blank in re.findall(r"<a [^>]*target=\"_blank\"[^>]*>", self.page):
            self.assertIn('rel="noopener noreferrer"', blank)

    def test_it_loads_nothing_from_another_site(self):
        self.assertEqual(addresses_that_leave_the_site(self.page), [])

    def test_the_check_of_the_addresses_is_shown_able_to_fail(self):
        """Copies of the page with one address that leaves the site: each must be named by the check (a protocol-relative one and an @import included)."""
        mutants = {
            "a protocol-relative style sheet": ('<link rel="stylesheet" href="/front/classic.css">', '<link rel="stylesheet" href="/front/classic.css"><link rel="stylesheet" href="//cdn.example/x.css">', "//cdn.example/x.css"),
            "a protocol-relative image": ('<img src="/front/logo.png"', '<img src="//cdn.example/logo.png"', "//cdn.example/logo.png"),
            "an image of another site": ('<img src="/front/logo.png"', '<img src="https://cdn.example/logo.png"', "https://cdn.example/logo.png"),
            "a plain http address": ('<link rel="icon" type="image/png" href="/favicon.png">', '<link rel="icon" type="image/png" href="http://cdn.example/f.png">', "http://cdn.example/f.png"),
            "an address that only starts like the repository's": ('href="https://github.com/dchadd427/ants-cross-platform/issues"', 'href="https://github.com/dchadd427/ants-cross-platform.evil.example/issues"', "ants-cross-platform.evil.example"),
            "an @import of a quoted address": ("main { margin-top: 26px; }", '@import "https://cdn.example/x.css";\nmain { margin-top: 26px; }', "@import"),
            "an @import url()": ("main { margin-top: 26px; }", "@import url(//cdn.example/x.css);\nmain { margin-top: 26px; }", "@import"),
            "a url() in the page's own rules": ("main { margin-top: 26px; }", 'main { margin-top: 26px; background: url("/front/clay.png"); }', "url("),
            "a srcset": ('<img src="/front/logo.png"', '<img src="/front/logo.png" srcset="//cdn.example/l.png 2x"', "//cdn.example/l.png"),
        }
        for name, (old, new, needle) in sorted(mutants.items()):
            self.assertGreaterEqual(self.page.count(old), 1, "%s: the mutation's anchor is in the page" % name)
            found = addresses_that_leave_the_site(self.page.replace(old, new, 1))
            self.assertTrue(found and any(needle in f for f in found), "%s: the check did not name it (%s)" % (name, found))

    def test_it_says_the_notice_in_the_words_that_the_owner_approved(self):
        for sentence in NOTICE:
            self.assertIn(sentence, self.text)
        self.assertIn("not legal advice", self.text)
        self.assertEqual(re.findall(r"<strong>(.*?)</strong>", self.main), ["no warranty of any kind", "not liable"])                  # the two phrases that matter are in bold

    def test_it_says_what_the_readme_says_of_the_original_game(self):
        for words in ("belong to their copyright holders", "does not own them and cannot license them", "open an issue", "it will be taken out"):
            self.assertIn(words.lower(), self.text.lower(), words)
            self.assertIn(words.lower(), plain(section(read("README.md"), "License")).lower(), words)
        for words in ("artwork", "animations", "sounds", "music", "maps"):
            self.assertIn(words, self.text)
        self.assertIn("the maps that players made", self.text)

    def test_it_is_short_and_says_nothing_of_what_the_site_stores_or_for_how_long(self):
        self.assertLessEqual(len(self.text.split()), 250)
        for word in ("record", "recorded", "recordings", "stored", "kept", "retain", "retention", "cookie", "cookies", "privacy", "personal", "days", "names"):
            self.assertNotRegex(self.text.lower(), r"\b%s\b" % word, word)

    def test_it_links_the_licence_the_third_party_notices_and_the_issues_page_of_the_repository(self):
        hrefs = re.findall(r'<a href="([^"]*)"', self.main)
        self.assertEqual(hrefs, [GITHUB + "/issues", GITHUB + "/blob/main/LICENSE", GITHUB + "/blob/main/THIRD_PARTY_NOTICES.md", GITHUB + "/blob/main/LICENSE"])
        for name in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
            self.assertTrue(os.path.exists(os.path.join(REPO, name)), name)

    def test_its_footer_is_the_changelog_pages_and_marks_the_page_it_is(self):
        self.assertEqual([(href, text) for href, _, text in footer_links(self.page)],
                         [("/", "Play"), (GITHUB, "GitHub"), (GITHUB + "/issues", "Feedback"), ("/legal.html", "Legal")])
        self.assertIn('<a href="/legal.html" aria-current="page">Legal</a>', self.page)
        self.assertIn(".bar a[aria-current]", self.page)


class TheReadme(unittest.TestCase):
    def test_the_license_section_opens_with_the_notice_and_links_the_page(self):
        text = plain(section(read("README.md"), "License"))
        first = text[:text.index("The source code and the documents written for this project")]
        self.assertTrue(first.startswith(" ".join(NOTICE)), first)
        self.assertIn("The same notice, in plain words, is the site's Legal page; it is not legal advice.", first)
        self.assertIn("(%s)" % LEGAL_URL, section(read("README.md"), "License"))

    def test_the_mit_and_the_original_games_paragraphs_are_still_there(self):
        text = section(read("README.md"), "License")
        for words in ("released under the **MIT License** ([`LICENSE`](LICENSE))", "**The original game is not part of that licence.**", "**The players' maps are not part of that licence either.**",
                      "[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)"):
            self.assertIn(words, text, words)


class TheFooterLinks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pages = pages_with_a_footer()

    def test_the_pages_with_a_footer_are_the_ones_this_test_knows(self):
        names = sorted(self.pages)
        self.assertIn("lobby.html", names)
        self.assertIn("shell.html", names)
        self.assertIn("changelog.html (generated)", names)

    def test_every_footer_ends_with_the_legal_link_in_a_new_tab(self):
        for name, page in sorted(self.pages.items()):
            links = footer_links(page)
            self.assertEqual(links[-1][0], "/legal.html", name)
            self.assertEqual(links[-1][2], "Legal", name)
            self.assertIn('target="_blank" rel="noopener noreferrer"', links[-1][1], name)
            self.assertEqual([l for l in links if l[0] == "/legal.html"], [links[-1]], "%s: one link to the page" % name)
            self.assertEqual(len([l for l in links if l[2] == "Feedback"]), 1, name)
            self.assertEqual([l[2] for l in links][-2:], ["Feedback", "Legal"], name)                                # (after Feedback, the link that the other pages end with)

    def test_the_link_is_the_same_markup_in_every_footer(self):
        for name, page in sorted(self.pages.items()):
            self.assertEqual(page.count('<a href="/legal.html" target="_blank" rel="noopener noreferrer">Legal</a>'), 1, name)

    def test_the_header_buttons_of_the_game_page_do_not_have_it(self):
        game = read("web", "shell.html")
        header = game[game.index("<header>"):game.index("</header>")]
        self.assertNotIn("legal", header.lower())


class TheRouteAndTheImage(unittest.TestCase):
    def test_the_dockerfile_copies_the_page_to_the_site(self):
        self.assertIn("\nCOPY web/legal.html /usr/share/nginx/html/legal.html\n", read("Dockerfile"))

    def test_nginx_sends_legal_to_the_page_with_a_plain_redirect_like_changelog(self):
        conf = read("docker", "nginx.conf")
        block = re.search(r"\n    location = /legal \{([^}]*)\}", conf)
        self.assertIsNotNone(block, "location = /legal")
        self.assertEqual(block.group(1).strip(), "return 301 /legal.html;")                                          # no add_header: a header here would hide the server's own
        changelog = re.search(r"\n    location = /changelog \{([^}]*)\}", conf).group(1).strip()
        self.assertEqual(changelog, "return 301 /changelog.html;")


if __name__ == "__main__":
    unittest.main()
