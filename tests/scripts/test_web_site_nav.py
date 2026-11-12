#!/usr/bin/env python3
"""The bar at the top of every page of the site (web/site_nav.css, web/site_nav.js; docs/SITE_NAV.md): run by ./run_tests.sh --fast and by the CI.

What this holds the bar to, without a browser (tests/scripts/web_site_nav_browser.js, opt-in, runs it in Chromium at 320 to 1280 px):
  - the same five links, in the same order, on every page (Home, Matches, Live now, Map maker, Discord), the page that is open marked aria-current, and the same Menu on Home (web/home/home.js) as in
    web/site_nav.js (one list: the two files name the same links);
  - the files are in the image (the Dockerfile copies each once), are plain ES5 and CSS, carry no copy of the colors, and the pages put no inline style or script into the bar (the CSP of /admin
    has none, and the other pages keep to it);
  - the byte budgets of the two files, and the pages that are meant to stay without the bar (the admin page).
"""
import gzip
import os
import re
import sys
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DISCORD = "https://discord.gg/WaWrXQnKPj"
ROW = ["/", "/matches", "/live-now", "/mapmaker", DISCORD]
SMALL = [("Sprites and sounds", "/asset_catalog/"), ("Changelog", "/changelog"), ("GitHub", "https://github.com/dchadd427/ants-cross-platform"),
         ("Feedback", "https://github.com/dchadd427/ants-cross-platform/issues"), ("Legal", "/legal")]
CSS_BUDGET, CSS_GZIP_BUDGET = 5600, 1800            # bytes, raw and gzipped (about 5,350 and 1,730: the button row, the phone bar, the Menu and the title block, with their comments; the plan said 1,700 and 730 and the comments and the forced-colors rules are the difference)
JS_BUDGET, JS_GZIP_BUDGET = 4300, 1800               # (about 3,960 and 1,590)


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def changelog_page():
    sys.path.insert(0, os.path.join(REPO, "tools"))
    try:
        import changelog_to_html
    finally:
        sys.path.pop(0)
    return changelog_to_html.convert(read("CHANGELOG.md"), changelog_to_html.DEFAULT_REPO_BLOB, version="v0.0.0", build_id="0000000")


# name -> (the page's text, what the row marks (None: the Menu does), data-live)
PAGES = {
    "matches": (read("web", "matches.html"), "/matches", "now"),
    "match": (read("web", "match.html"), "/matches", "now"),
    "live": (read("web", "live.html"), "/live-now", ""),
    "legal": (read("web", "legal.html"), None, "menu"),
    "changelog": (changelog_page(), None, "menu"),
    "mapmaker": (read("web", "mapmaker.html"), "/mapmaker", "menu"),
    "catalog": (read("asset_catalog", "index.html"), None, "menu"),
}
CSS = read("web", "site_nav.css")
JS = read("web", "site_nav.js")


def bar_of(page):
    return re.search(r'<header class="sb"([^>]*)>(.*?)</header>', page, re.S)


class ThePages(unittest.TestCase):
    def test_every_page_has_one_bar_with_the_same_five_links_in_the_same_order(self):
        for name, (page, marked, live) in PAGES.items():
            self.assertEqual(len(re.findall(r'<header class="sb"', page)), 1, name)
            bar = bar_of(page)
            nav = re.search(r'<nav aria-label="Pages">(.*?)</nav>', bar.group(2), re.S).group(1)
            self.assertEqual(re.findall(r'<a class="btn[a-z ]*" href="([^"]*)"', nav), ROW, name)
            self.assertEqual(re.findall(r'<a class="btn[a-z ]*" href="([^"]*)"', bar.group(2))[:2], ["/", "/"], name + ": Home, then the logo, are the phone's two links to the front page")
            self.assertEqual(re.findall(r'data-live="([^"]*)"', bar.group(1)), [live] if live else [], name)

    def test_the_page_that_is_open_is_marked_in_the_row_and_no_other(self):
        for name, (page, marked, live) in PAGES.items():
            nav = re.search(r'<nav aria-label="Pages">(.*?)</nav>', bar_of(page).group(2), re.S).group(1)
            found = re.findall(r'<a class="btn[a-z ]*" href="([^"]*)" aria-current="page"', nav)
            self.assertEqual(found, [marked] if marked else [], name)

    def test_the_bar_has_the_phone_pieces_and_the_menu_button_is_named_and_controls_the_menu(self):
        for name, (page, marked, live) in PAGES.items():
            bar = bar_of(page).group(2)
            self.assertIn('<a class="btn sbhome" href="/"><span class="sbchev" aria-hidden="true">&lsaquo;</span>Home</a>', bar, name)
            self.assertRegex(bar, r'<a class="sblogo" href="/"><img src="/?front/logo\.png" alt="ants! - Home" width="581" height="218"></a>', name)
            self.assertIn('<a class="btn sblive" href="/live-now" hidden></a>', bar, name)                  # (hidden until the count is known and above none)
            self.assertIn('<button class="btn sbtn" type="button" aria-label="Menu" aria-expanded="false" aria-controls="sbm"><i class="sbbars"></i></button>', bar, name)
            self.assertIn('<span>Live now</span><span class="sbn"></span>', bar, name)                       # (the count's place is kept, so that the row does not move when it arrives)
            self.assertEqual(len(re.findall(r"<img", bar)), 1, name)
            self.assertNotIn("style=", bar, name)                                                             # (no inline style: the CSP of the admin page has none, the rest keeps to it)
            self.assertNotIn("onclick", bar, name)
            self.assertNotIn("<script", bar, name)

    def test_the_discord_link_opens_in_a_new_tab_without_handing_over_the_page(self):
        for name, (page, marked, live) in PAGES.items():
            link = re.search(r'<a class="btn sbdc" href="%s"[^>]*>' % re.escape(DISCORD), bar_of(page).group(2)).group(0)
            self.assertIn('target="_blank" rel="noopener noreferrer" aria-label="Discord server"', link, name)

    def test_the_bar_files_are_linked_once_each_and_the_script_comes_after_the_bar(self):
        for name, (page, marked, live) in PAGES.items():
            self.assertEqual(len(re.findall(r'href="/?site_nav\.css"', page)), 1, name)
            self.assertEqual(len(re.findall(r'src="/?site_nav\.js"', page)), 1, name)
            self.assertLess(page.index('<header class="sb"'), page.index("site_nav.js"), name)

    def test_the_title_and_its_line_are_one_block_under_the_bar_with_the_only_h1(self):
        for name, (page, marked, live) in PAGES.items():
            if name in ("mapmaker", "catalog"):
                continue                                                                                      # (the editor and the catalog keep their own title rows)
            self.assertEqual(len(re.findall(r"<h1[ >]", page)), 1, name)
            self.assertRegex(page, r'</header>\n(    )?<div class="pt">(<a class="crumb"[^>]*>[^<]*</a>)?<h1>[^<]+</h1><p>[^<]+</p></div>', name)

    def test_match_keeps_its_crumb_to_all_matches_and_the_old_ones_are_gone(self):
        self.assertIn('<a class="crumb" id="all-btn" href="/matches">&lsaquo; All matches</a>', PAGES["match"][0])
        for name, (page, marked, live) in PAGES.items():
            self.assertNotIn('class="mast"', page, name)
            if name != "catalog":                                                                              # (the catalog keeps its own header, its tabs, under the bar)
                self.assertNotIn('class="site-head"', page, name)
            self.assertNotIn("discord-top", page, name)
        self.assertIn('data-act="menu" disabled>File</button>', PAGES["mapmaker"][0])                        # (the editor's own Menu is the File button: two Menus are one too many)
        self.assertNotIn(">Menu</button>", PAGES["mapmaker"][0].replace('aria-label="Menu"', ""))
        self.assertIn("label: 'File'", read("web", "front", "mapmaker_dialog.js"))

    def test_the_admin_page_stays_without_the_bar_and_without_a_link_to_it(self):
        for name in ("admin.html", "admin_page.js", "admin.css"):
            self.assertNotIn("site_nav", read("web", name), name)
        for name, (page, marked, live) in PAGES.items():
            self.assertNotIn("/admin", re.sub(r"[a-z]+-[a-z]+/admin", "", page), name)                       # (nothing on the site links to it: docker/nginx.conf)
        self.assertNotIn("/admin", JS)


class TheHomeMenu(unittest.TestCase):
    """The Menu of the front page (web/home/home.js) and the bar's (web/site_nav.js) name the same links, in the same order."""

    def home_lists(self):
        text = read("web", "home", "home.js")
        main = re.search(r"var MENU_MAIN = \[(.*?)\];", text).group(1)
        small = re.search(r"var MENU_SMALL = \[(.*?)\];", text).group(1)
        pair = lambda s: re.findall(r"\['([^']*)', '([^']*)'\]", s)
        return pair(main), pair(small)

    def test_the_main_links_are_the_row_and_the_small_ones_are_the_same_list(self):
        main, small = self.home_lists()
        self.assertEqual([m for m in main if m[1]], [("Home", "/"), ("Matches", "/matches"), ("Live now", "/live-now"), ("Map maker", "/mapmaker"), ("Discord server", DISCORD)])
        self.assertEqual([m[1] for m in main if m[1]], ROW)
        self.assertEqual(small, SMALL)
        js_small = re.search(r"var SMALL = \[(.*?)\];", JS).group(1)
        self.assertEqual(re.findall(r"\['([^']*)', '([^']*)'\]", js_small), SMALL)
        for name, (page, marked, live) in PAGES.items():
            nav = re.search(r'<nav aria-label="Pages">(.*?)</nav>', bar_of(page).group(2), re.S).group(1)
            labels = [re.sub(r"<[^>]*>", "", x).strip() or "Discord server" for x in re.findall(r"<a [^>]*>(.*?)</a>", nav, re.S)]
            self.assertEqual(labels, ["Home", "Matches", "Live now", "Map maker", "Discord server"], name)
        self.assertEqual([m[0] for m in main if m[1]][:1], ["Home"], "Home is the first item")

    def test_home_marks_itself_and_shows_the_live_count_on_live_now_from_the_footers_line(self):
        text = read("web", "home", "home.js")
        self.assertIn("item.setAttribute('aria-current', 'page')", text)
        self.assertIn("doc.getElementById('stats-live')", text)
        self.assertIn("/^[1-9]\\d*/.exec(sl.textContent)", text)
        css = read("web", "home", "home.css")
        self.assertIn(".hmenubox a[aria-current] { color: var(--gold); }", css)
        self.assertIn(".hmenubox .mn:empty { display: none; }", css)


class TheFiles(unittest.TestCase):
    def test_the_dockerfile_copies_each_file_once(self):
        docker = read("Dockerfile")
        for name in ("site_nav.css", "site_nav.js"):
            self.assertEqual(docker.count("COPY web/%s /usr/share/nginx/html/%s\n" % (name, name)), 1, name)
        self.assertEqual(len(re.findall(r"site_nav", docker)), 4)                                              # (a source and an address, for each)

    def test_they_are_ascii_and_within_their_budgets(self):
        for name, text, budget, gzip_budget in (("site_nav.css", CSS, CSS_BUDGET, CSS_GZIP_BUDGET), ("site_nav.js", JS, JS_BUDGET, JS_GZIP_BUDGET)):
            raw = text.encode("utf-8")
            self.assertTrue(all(b < 128 for b in raw), name + " is ASCII")
            self.assertLess(len(raw), budget, "%s is %d bytes" % (name, len(raw)))
            self.assertLess(len(gzip.compress(raw, 9)), gzip_budget, "%s is %d bytes gzipped" % (name, len(gzip.compress(raw, 9))))

    def test_the_script_is_plain_es5_and_asks_the_site_for_one_thing(self):
        for bad in ("=>", "let ", "const ", "`", "class ", "async ", "await ", "innerHTML", "document.write", "eval(", "new Function", "XMLHttpRequest", "localStorage", "cookie"):
            self.assertNotIn(bad, JS, bad)
        self.assertEqual(re.findall(r"fetch\('([^']*)'", JS), ["/live"])
        self.assertIn("{ cache: 'no-store', credentials: 'omit' }", JS)
        self.assertEqual(len(re.findall(r"\.style\.", JS)), 2)                                                  # (the Menu's place, by CSSOM: no style string)
        self.assertIn("if (e.key === 'Escape') close(true);", JS)                                              # (Escape closes it and the focus goes back to its button)
        self.assertIn("btn.setAttribute('aria-expanded', 'true');", JS)
        self.assertIn("btn.setAttribute('aria-expanded', 'false');", JS)
        self.assertIn("if (back) btn.focus();", JS)

    def test_the_style_carries_the_phone_bar_the_row_and_the_rules_for_every_kind_of_user(self):
        self.assertNotRegex(CSS, r"--(?:clay|ink|teal|teal-hi|teal-lo|edge|cream|inset|frame|frame-hi|frame-lo|shadow|gold|muted|weave|sheen|bevel)\s*:")      # (no copy of the tokens: the page's own)
        self.assertNotIn("@import", CSS)
        self.assertNotRegex(CSS, r"url\(")
        self.assertIn("@media (min-width: 720px)", CSS)
        self.assertIn(".sb nav { display: none; }", CSS)
        self.assertIn(".sb .btn { min-height: 44px;", CSS)                                                      # (a 44 px target)
        self.assertIn(".sb .sblogo { display: flex; align-items: center; min-height: 44px;", CSS)
        self.assertIn("@media (forced-colors: active)", CSS)
        self.assertIn("outline: 3px solid Highlight", CSS)
        self.assertNotIn("transition", CSS)                                                                     # (no motion)
        self.assertNotIn("animation", CSS)
        self.assertIn(".sb nav [aria-current]", CSS)
        self.assertIn("@media (max-height: 480px) and (max-width: 900px)", CSS)                                 # (a phone held sideways: the title and its line share one row)

    def test_the_discord_rules_name_the_class_of_the_markup(self):
        self.assertNotIn(".dcb", CSS)                                                                           # (a class that no page has)
        self.assertIn(".sb nav .btn.sbdc { min-width: 44px; }", CSS)                                            # (44 px wide from 720 px)
        self.assertIn(".sb nav .btn.sbdc { padding: 6px 12px; }", CSS)

    def test_the_live_count_is_asked_for_at_the_first_opening_only_where_the_bar_says_menu(self):
        self.assertIn("if (bar.getAttribute('data-live') === 'menu') look();", JS)                              # (Live now has no attribute: its own list is the count)

    def test_the_old_headers_rules_are_gone_from_the_stylesheets(self):
        matches = read("web", "matches.css")
        for gone in (".mast", ".discord", ".btn.live", "hide-s"):
            self.assertNotIn(gone, matches, gone)
        classic = read("web", "front", "classic.css")
        for gone in (".site-head", ".links {", ".logo"):
            self.assertNotIn(gone, classic, gone)


if __name__ == "__main__":
    unittest.main()
