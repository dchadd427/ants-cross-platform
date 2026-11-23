#!/usr/bin/env python3
"""The bar at the top of every page of the site (web/site_nav.css, web/site_nav.js; docs/SITE_NAV.md): run by ./run_tests.sh --fast and by the CI.

What this holds the bar to, without a browser (tests/scripts/web_site_nav_browser.js, opt-in, runs it in Chromium at 320 to 1280 px):
  - the same four links, in the same order, on every page (Home, Matches, Map maker, Discord), no link to the page of the matches that run now anywhere on the site, the page that is open marked aria-current, and the same Menu on Home (web/home/home.js) as in
    web/site_nav.js (one list: the two files name the same links);
  - the files are in the image (the Dockerfile copies each once), are plain ES5 and CSS, carry no copy of the colors, and the pages put no inline style or script into the bar (the CSP of /admin
    has none, and the other pages keep to it);
  - the byte budgets of the two files, and that nothing but the bar links to the admin page (the page has the bar, as every page of the site but the editor's and the catalog's own screens);
  - the Menu run in node on the real markup of the pages (web/site_nav.js with a small fake of the DOM: tests/scripts/web_site_nav_menu_check.js): its last line is the stamped version and build as
    text, a page that nobody stamped shows no line (never "@@GAME_VERSION@@"), every link that the old footers had is in it, and the pages with the bar have no footer (the front page keeps its own; the game page, with the bar, has none and a hidden stamp).
"""
import gzip
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DISCORD = "https://discord.gg/WaWrXQnKPj"
ROW = ["/", "/matches", "/mapmaker", DISCORD]
SMALL = [("Sprites and sounds", "/asset_catalog/"), ("What’s new", "/changelog"), ("GitHub", "https://github.com/dchadd427/ants-cross-platform"),
         ("Feedback", "https://github.com/dchadd427/ants-cross-platform/issues"), ("Legal", "/legal")]
sys.path.insert(0, os.path.join(REPO, "tools"))
from size_limits import NAV_CSS_BUDGET as CSS_BUDGET, NAV_CSS_GZIP_BUDGET as CSS_GZIP_BUDGET, NAV_JS_BUDGET as JS_BUDGET, NAV_JS_GZIP_BUDGET as JS_GZIP_BUDGET  # noqa: E402  (every byte budget: tools/size_limits.py)


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
    "legal": (read("web", "legal.html"), None, "menu"),
    "changelog": (changelog_page(), None, "menu"),
    "mapmaker": (read("web", "mapmaker.html"), "/mapmaker", "menu"),
    "admin": (read("web", "admin.html"), None, "menu"),                                                       # (its title and its tabs are drawn by its script, for a visitor who is let in: tests/scripts/test_web_admin.py)
    "catalog": (read("asset_catalog", "index.html"), None, "menu"),
    "game": (read("web", "shell.html"), None, "menu"),                                                        # (the game page: a bot game, a joined match, a recording, a live view; its tools and its stamp are tests/scripts/test_web_game.py's)
}
CSS = read("web", "site_nav.css")
JS = read("web", "site_nav.js")


def bar_of(page):
    return re.search(r'<header class="sb"([^>]*)>(.*?)</header>', page, re.S)



def js_pairs(source):
    """The ['name', 'address'] pairs of a list in a script, its \\uXXXX escapes read as the browser reads them (the scripts are ASCII)."""
    return [tuple(re.sub(r"\\u([0-9a-fA-F]{4})", lambda m: chr(int(m.group(1), 16)), x) for x in pair) for pair in re.findall(r"\['([^']*)', '([^']*)'\]", source)]

class ThePages(unittest.TestCase):
    def test_every_page_has_one_bar_with_the_same_four_links_in_the_same_order(self):
        for name, (page, marked, live) in PAGES.items():
            self.assertEqual(len(re.findall(r'<header class="sb"', page)), 1, name)
            bar = bar_of(page)
            nav = re.search(r'<nav aria-label="Pages">(.*?)</nav>', bar.group(2), re.S).group(1)
            self.assertEqual(re.findall(r'<a class="btn[a-z ]*" href="([^"]*)"', nav), ROW, name)
            self.assertEqual(re.findall(r'<a class="btn[a-z ]*" href="([^"]*)"', bar.group(2))[:2], ["/", "/"], name + ": the phone's Home button and the row's Home are the two links to the front page")
            self.assertEqual(re.findall(r'data-live="([^"]*)"', bar.group(1)), [live] if live else [], name)

    def test_the_page_that_is_open_is_marked_in_the_row_and_no_other(self):
        for name, (page, marked, live) in PAGES.items():
            nav = re.search(r'<nav aria-label="Pages">(.*?)</nav>', bar_of(page).group(2), re.S).group(1)
            found = re.findall(r'<a class="btn[a-z ]*" href="([^"]*)" aria-current="page"', nav)
            self.assertEqual(found, [marked] if marked else [], name)

    def test_the_bar_has_the_phone_pieces_and_the_menu_button_is_named_and_controls_the_menu(self):
        for name, (page, marked, live) in PAGES.items():
            bar = bar_of(page).group(2)
            self.assertRegex(bar, r'<a class="btn sbhome" href="/"[^>]*><span class="sbchev" aria-hidden="true">&lsaquo;</span>Home</a>', name)
            self.assertIn('<a class="btn sblive" href="/" hidden></a>', bar, name)                          # (the live mark: it goes to the front page, and is hidden until the count is known and above none)
            self.assertIn('<button class="btn sbtn" type="button" aria-label="Menu" aria-expanded="false" aria-controls="sbm"><i class="sbbars"></i></button>', bar, name)
            self.assertEqual(bar.count("sblive"), 1, name)                                                    # (one mark: not a second one in the row)
            self.assertEqual(len(re.findall(r"<img", bar)), 0, name + ": the bar has no picture, so no page downloads one for it")
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
            if name in ("mapmaker", "catalog", "game", "admin"):
                continue                                                                                      # (the editor and the catalog keep their own title rows, and the admin page draws its title for an admin only)
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

    def test_the_admin_page_has_the_bar_and_no_other_page_links_to_it(self):
        for name in ("admin.html", "admin_page.js", "admin_bots.js", "admin_map_pick.js", "admin.css"):
            self.assertTrue(read("web", name))
        self.assertIn("site_nav", read("web", "admin.html"))
        for name, (page, marked, live) in PAGES.items():
            if name != "admin":
                self.assertNotIn("/admin", re.sub(r"[a-z]+-[a-z]+/admin", "", page), name)                   # (nothing on the site links to it: docker/nginx.conf)
        self.assertNotIn("/admin", JS)


class TheHomeMenu(unittest.TestCase):
    """The Menu of the front page (web/home/home.js) and the bar's (web/site_nav.js) name the same links, in the same order."""

    def home_lists(self):
        text = read("web", "home", "home.js")
        main = re.search(r"var MENU_MAIN = \[(.*?)\];", text).group(1)
        small = re.search(r"var MENU_SMALL = \[(.*?)\];", text).group(1)
        return js_pairs(main), js_pairs(small)

    def test_the_main_links_are_the_row_and_the_small_ones_are_the_same_list(self):
        main, small = self.home_lists()
        self.assertEqual([m for m in main if m[1]], [("Home", "/"), ("Matches", "/matches"), ("Map maker", "/mapmaker"), ("Discord server", DISCORD)])
        self.assertEqual([m[1] for m in main if m[1]], ROW)
        self.assertEqual(small, SMALL)
        js_small = re.search(r"var SMALL = \[(.*?)\];", JS).group(1)
        self.assertEqual(js_pairs(js_small), SMALL)
        for name, (page, marked, live) in PAGES.items():
            nav = re.search(r'<nav aria-label="Pages">(.*?)</nav>', bar_of(page).group(2), re.S).group(1)
            labels = [re.sub(r"<[^>]*>", "", x).strip() or "Discord server" for x in re.findall(r"<a [^>]*>(.*?)</a>", nav, re.S)]
            self.assertEqual(labels, ["Home", "Matches", "Map maker", "Discord server"], name)
        self.assertEqual([m[0] for m in main if m[1]][:1], ["Home"], "Home is the first item")

    def test_home_marks_itself_and_its_menu_has_no_live_entry(self):
        text = read("web", "home", "home.js")
        self.assertIn("item.setAttribute('aria-current', 'page')", text)
        css = read("web", "home", "home.css")
        self.assertIn(".hmenubox a[aria-current] { color: var(--gold); }", css)
        self.assertNotIn("live-now", text)                                                                    # (the front page lists the matches that run now itself: its Menu has no entry for them, and no mark)


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
        self.assertIn("@media (forced-colors: active)", CSS)
        self.assertIn("outline: 3px solid Highlight", CSS)
        self.assertIn(".sb .sblive::before { background: ButtonText; box-shadow: none; }", CSS[CSS.index("@media (forced-colors: active)"):])      # (the mark's dot: the page's colors are not used there, so it takes the button's text color and stays seen)
        self.assertNotIn("transition", CSS)                                                                     # (no motion)
        self.assertNotIn("animation", CSS)
        self.assertIn(".sb nav [aria-current]", CSS)
        self.assertIn("@media (max-height: 480px) and (max-width: 900px)", CSS)                                 # (a phone held sideways: the title and its line share one row)

    def test_the_discord_rules_name_the_class_of_the_markup(self):
        self.assertNotIn(".dcb", CSS)                                                                           # (a class that no page has)
        self.assertIn(".sb nav .btn.sbdc { min-width: 44px; }", CSS)                                            # (44 px wide from 720 px)
        self.assertIn(".sb nav .btn.sbdc { padding: 6px 12px; }", CSS)

    def test_the_live_count_is_asked_for_at_the_first_opening_only_where_the_bar_says_menu(self):
        self.assertIn("if (bar.getAttribute('data-live') === 'menu') look();", JS)                              # (a page with no attribute does not ask)

    def test_the_old_headers_rules_are_gone_from_the_stylesheets(self):
        matches = read("web", "matches.css")
        for gone in (".mast", ".discord", ".btn.live", "hide-s"):
            self.assertNotIn(gone, matches, gone)
        classic = read("web", "front", "classic.css")
        for gone in (".site-head", ".links {", ".logo"):
            self.assertNotIn(gone, classic, gone)


STAMPS = {"@@GAME_VERSION@@": "v9.8.7", "@@BUILD_ID@@": "abc1234", "@@BUILD_DATE@@": "2026-01-02 09:30 PT"}
LINKS_OF_THE_OLD_FOOTERS = ["/", "/matches", "/asset_catalog/", "/changelog", "https://github.com/dchadd427/ants-cross-platform", "https://github.com/dchadd427/ants-cross-platform/issues", "/legal"]
BAR_PAGES = ("matches", "match", "legal", "mapmaker", "shell", "admin")      # (shell: the game page)


@unittest.skipUnless(shutil.which("node"), "node is not installed: the Menu is not run here")
class TheMenusLastLine(unittest.TestCase):
    """The Menu opened on the real markup of every page that has the bar (node, a fake DOM): what a visitor of a stamped build reads at its foot, and what a page that was never stamped shows."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="site_nav_menu_")
        cls.maker = "v" + read("web", "MAPMAKER_VERSION").strip()
        manifest = []

        def add(name, text):
            path = os.path.join(cls.tmp, name + ".html")
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
            manifest.append({"name": name, "path": path})

        for page in BAR_PAGES:
            raw = read("web", page + ".html")
            stamped = raw
            for placeholder, value in STAMPS.items():
                stamped = stamped.replace(placeholder, value)
            add(page + ":stamped", stamped)
            add(page + ":raw", raw)                                                                       # (a page that was never stamped: the repository's own file)
            add(page + ":version", raw.replace("@@GAME_VERSION@@", "v9.8.7"))                             # (a stamp that has the version and not the build)
            add(page + ":nodate", raw.replace("@@GAME_VERSION@@", "v9.8.7").replace("@@BUILD_ID@@", "abc1234"))      # (the version and the build, no date)
            add(page + ":buildonly", raw.replace("@@BUILD_ID@@", "abc1234"))                              # (a build with no version)
        sys.path.insert(0, os.path.join(REPO, "tools"))
        try:
            import changelog_to_html
        finally:
            sys.path.pop(0)
        md = read("CHANGELOG.md")
        add("changelog:stamped", changelog_to_html.convert(md, changelog_to_html.DEFAULT_REPO_BLOB, version="v9.8.7", build_id="abc1234", build_date="2026-01-02 09:30 PT"))
        add("changelog:raw", changelog_to_html.convert(md, changelog_to_html.DEFAULT_REPO_BLOB))
        add("changelog:version", changelog_to_html.convert(md, changelog_to_html.DEFAULT_REPO_BLOB, version="v9.8.7"))
        add("changelog:nodate", changelog_to_html.convert(md, changelog_to_html.DEFAULT_REPO_BLOB, version="v9.8.7", build_id="abc1234"))
        add("changelog:buildonly", changelog_to_html.convert(md, changelog_to_html.DEFAULT_REPO_BLOB, build_id="abc1234"))
        with open(os.path.join(cls.tmp, "manifest.json"), "w", encoding="utf-8") as f:
            json.dump(manifest, f)
        done = subprocess.run(["node", os.path.join(REPO, "tests", "scripts", "web_site_nav_menu_check.js"), os.path.join(cls.tmp, "manifest.json")], capture_output=True, text=True, timeout=120)
        if done.returncode != 0:
            raise AssertionError("the Menu check did not run: " + done.stderr[-1500:])
        cls.found = json.loads(done.stdout)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def last_line(self, name):
        items = self.found[name]["items"]
        return items[-1] if items and items[-1]["cls"] == "sbver" else None

    def test_a_stamped_page_ends_its_menu_with_the_version_and_the_build_as_text(self):
        for page in BAR_PAGES + ("changelog",):
            line = self.last_line(page + ":stamped")
            self.assertIsNotNone(line, page)
            expected = "Version v9.8.7 - build abc1234 (2026-01-02 09:30 PT)" + (" - Map maker " + self.maker if page == "mapmaker" else "")
            self.assertEqual(line["text"], expected, page)
            self.assertEqual(line["links"], [], page + ": the line is text, not a link")
            self.assertFalse(self.found[page + ":stamped"]["hidden"], page)
            self.assertEqual([i["cls"] for i in self.found[page + ":stamped"]["items"]].count("sbver"), 1, page)

    def test_a_page_that_nobody_stamped_shows_no_line_and_never_a_placeholder(self):
        for page in BAR_PAGES + ("changelog",):
            items = self.found[page + ":raw"]["items"]
            for item in items:
                self.assertNotIn("@", item["text"], page)
                self.assertNotIn("Version", item["text"], page)
                self.assertNotIn("build", item["text"], page)
            line = self.last_line(page + ":raw")
            if page == "mapmaker":
                self.assertEqual(line["text"], "Map maker " + self.maker)                               # (the editor's own version is no stamp: the file web/MAPMAKER_VERSION)
            else:
                self.assertIsNone(line, page)
                self.assertEqual(items[-1]["cls"], "sbsmall", page + ": the small links are the last thing")

    def test_a_stamp_that_has_only_the_version_shows_the_version_alone(self):
        for page in BAR_PAGES + ("changelog",):
            line = self.last_line(page + ":version")
            self.assertEqual(line["text"], "Version v9.8.7" + (" - Map maker " + self.maker if page == "mapmaker" else ""), page)

    def test_a_build_with_no_date_has_no_brackets_and_a_build_with_no_version_shows_nothing(self):
        for page in BAR_PAGES + ("changelog",):
            maker = " - Map maker " + self.maker if page == "mapmaker" else ""
            self.assertEqual(self.last_line(page + ":nodate")["text"], "Version v9.8.7 - build abc1234" + maker, page)
            only = self.last_line(page + ":buildonly")
            self.assertEqual(only["text"] if only else None, "Map maker " + self.maker if page == "mapmaker" else None, page)

    def test_every_link_that_the_old_footers_had_is_in_the_menu_of_every_page(self):
        for name, result in sorted(self.found.items()):
            for href in LINKS_OF_THE_OLD_FOOTERS:
                self.assertIn(href, result["hrefs"], "%s: %s" % (name, href))

    def test_the_pages_with_the_bar_have_no_footer_and_none_of_the_footers_version_markup(self):
        for name, result in sorted(self.found.items()):
            self.assertEqual(result["footers"], 0, name)
            if not name.startswith("shell"):
                self.assertEqual(result["versionIds"], [], name)                                              # (the game page keeps the hidden stamp that the image build reads)

    def test_the_front_page_keeps_its_footer_with_the_version_line_and_the_game_page_has_none(self):
        self.assertEqual(len(re.findall(r"<footer\b", read("web", "lobby.html"))), 1)
        self.assertEqual(len(re.findall(r"<footer\b", read("web", "shell.html"))), 0)


@unittest.skipUnless(shutil.which("node"), "node is not installed: the live mark is not run here")
class TheLiveMark(unittest.TestCase):
    """The small red mark of the bar (docs/SITE_NAV.md "The live mark"), run with the real web/site_nav.js on the real markup of the pages, against a fake /live: shown only while at least one match
    runs, named for a screen reader, linked to the front page; not there for none, for an answer that is no list, a failure, an error status or an answer that never comes."""

    PAGE_NAMES = ("matches", "legal", "mapmaker", "changelog", "catalog", "game")        # (data-live "now" on the first, "menu" on the others: both ask for the count)
    CASES = [("one", {"count": 1}), ("twelve", {"count": 12}), ("none", None), ("fail", "fail"), ("status", "status"), ("never", "never"), ("notlist", "notlist"), ("many", {"count": 120})]
    # the tab comes back to the front: (case, the first answer, the answer from then on, the states fired, whether the Menu is opened)
    AGAIN = [("more", {"count": 1}, {"count": 4}, None, True), ("ended", {"count": 3}, None, None, True), ("appears", None, {"count": 2}, None, True), ("fails", {"count": 3}, "fail", None, True),
             ("errors", {"count": 3}, "status", None, True), ("not_list", {"count": 3}, "notlist", None, True), ("hidden_only", {"count": 2}, {"count": 5}, ["hidden"], True),
             ("twice", {"count": 2}, {"count": 6}, ["hidden", "visible", "hidden", "visible"], True), ("unopened", {"count": 2}, {"count": 5}, None, False)]

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="site_nav_mark_")
        manifest = []
        for name, (page, marked, live) in PAGES.items():
            if name not in cls.PAGE_NAMES:
                continue
            path = os.path.join(cls.tmp, name + ".html")
            with open(path, "w", encoding="utf-8") as f:
                f.write(page)
            for case, behavior in cls.CASES:
                manifest.append({"name": "%s:%s" % (name, case), "path": path, "live": behavior})
            for case, first, later, states, click in cls.AGAIN:
                entry = {"name": "%s:again_%s" % (name, case), "path": path, "live": first, "later": later, "click": click}
                if states:
                    entry["back"] = states
                manifest.append(entry)
        with open(os.path.join(cls.tmp, "manifest.json"), "w", encoding="utf-8") as f:
            json.dump(manifest, f)
        done = subprocess.run(["node", os.path.join(REPO, "tests", "scripts", "web_site_nav_menu_check.js"), os.path.join(cls.tmp, "manifest.json")], capture_output=True, text=True, timeout=120)
        if done.returncode != 0:
            raise AssertionError("the live mark check did not run: " + done.stderr[-1500:])
        cls.found = json.loads(done.stdout)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def mark(self, page, case):
        return self.found["%s:%s" % (page, case)]["mark"]

    def test_with_one_match_the_mark_is_shown_says_1_live_and_names_the_front_page(self):
        for page in self.PAGE_NAMES:
            mark = self.mark(page, "one")
            self.assertEqual((mark["there"], mark["hidden"]), (True, False), page)
            self.assertEqual(mark["text"], "1 live", page)
            self.assertEqual(mark["label"], "1 live: see them on the front page", page)           # (the name starts with the words on the button)
            self.assertEqual(mark["title"], "1 match is being played - see them on the front page", page)
            self.assertEqual(mark["href"], "/", page)

    def test_with_twelve_matches_the_mark_says_12_live_and_names_the_count(self):
        for page in self.PAGE_NAMES:
            mark = self.mark(page, "twelve")
            self.assertEqual((mark["hidden"], mark["text"], mark["href"]), (False, "12 live", "/"), page)
            self.assertEqual(mark["label"], "12 live: see them on the front page", page)
            self.assertEqual(mark["title"], "12 matches are being played - see them on the front page", page)

    def test_the_count_stops_at_99(self):
        for page in self.PAGE_NAMES:
            mark = self.mark(page, "many")
            self.assertEqual((mark["hidden"], mark["text"], mark["label"]), (False, "99 live", "99 live: see them on the front page"), page)

    def after(self, page, case):
        return self.found["%s:again_%s" % (page, case)]

    def test_when_the_tab_comes_back_the_count_is_asked_again_and_the_mark_follows_it(self):
        for page in self.PAGE_NAMES:
            more = self.after(page, "more")
            self.assertEqual((more["mark"]["text"], more["after"]["text"], more["after"]["hidden"]), ("1 live", "4 live", False), page)
            self.assertEqual(more["after"]["label"], "4 live: see them on the front page", page)
            self.assertEqual(more["fetched"], ["/live", "/live"], page)
            self.assertEqual(self.after(page, "twice")["after"]["text"], "6 live", page)
            self.assertEqual(self.after(page, "twice")["fetched"], ["/live"] * 3, page)             # (once for the opening, once for each return)

    def test_when_the_matches_have_ended_the_mark_goes_and_when_one_starts_it_comes(self):
        for page in self.PAGE_NAMES:
            self.assertEqual((self.after(page, "ended")["mark"]["hidden"], self.after(page, "ended")["after"]["hidden"]), (False, True), page)
            self.assertEqual((self.after(page, "appears")["mark"]["hidden"], self.after(page, "appears")["after"]["hidden"], self.after(page, "appears")["after"]["text"]), (True, False, "2 live"), page)

    def test_when_the_count_cannot_be_read_on_return_the_mark_goes(self):
        for case in ("fails", "errors", "not_list"):
            for page in self.PAGE_NAMES:
                self.assertEqual((self.after(page, case)["mark"]["hidden"], self.after(page, case)["after"]["hidden"]), (False, True), "%s %s" % (page, case))

    def test_a_tab_that_goes_to_the_back_does_not_ask_and_a_page_that_never_asked_does_not_either(self):
        for page in self.PAGE_NAMES:
            hidden = self.after(page, "hidden_only")
            self.assertEqual((hidden["fetched"], hidden["after"]["text"]), (["/live"], "2 live"), page)
        for page in self.PAGE_NAMES:
            if page != "matches":                                                                    # (the Menu is not opened: a page that waits for it has not asked, and does not on a return)
                unopened = self.after(page, "unopened")
                self.assertEqual((unopened["fetched"], unopened["after"]["hidden"]), ([], True), page)

    def test_with_no_match_the_mark_is_not_shown(self):
        for page in self.PAGE_NAMES:
            self.assertTrue(self.mark(page, "none")["hidden"], page)

    def test_when_the_count_is_unknown_the_mark_is_not_shown(self):
        for case in ("fail", "status", "never", "notlist"):                       # (the request fails, answers an error, does not answer, or answers something that is no list)
            for page in self.PAGE_NAMES:
                self.assertTrue(self.mark(page, case)["hidden"], "%s %s" % (page, case))

    def test_the_bar_asks_for_the_count_once_and_only_at_the_address_of_the_servers_list(self):
        for page in self.PAGE_NAMES:
            self.assertEqual(self.found["%s:one" % page]["fetched"], ["/live"], page)

    def test_the_menu_has_no_live_entry_and_no_count(self):
        for page in self.PAGE_NAMES:
            result = self.found["%s:twelve" % page]
            self.assertNotIn("/live-now", result["hrefs"], page)
            self.assertEqual([i["cls"] for i in result["items"] if "12" in i["text"]], [], page)

    def test_the_front_page_has_no_mark_and_no_bar_script(self):
        page = read("web", "lobby.html")
        self.assertNotIn("sblive", page)
        self.assertNotIn("site_nav.js", page)


class TheOldPageIsNotLinked(unittest.TestCase):
    """No page, script or tool of the site names the address of the page of the matches that run now: a link left behind would end at a redirect. (A text search, because the places that build links are
    markup, scripts and a tool's template that no single run reaches; the redirect itself is run in tests/scripts/test_nginx_clean_urls.py.)"""

    def test_no_file_of_the_site_names_live_now_or_live_html(self):
        roots = [os.path.join(REPO, "web"), os.path.join(REPO, "asset_catalog"), os.path.join(REPO, "src", "ants_app")]
        bad = []
        for root in roots:
            for base, _dirs, files in os.walk(root):
                for name in files:
                    if not name.endswith((".html", ".js", ".css", ".webmanifest", ".cpp", ".hpp", ".json")):
                        continue
                    with open(os.path.join(base, name), encoding="utf-8", errors="replace") as f:
                        text = f.read()
                    if re.search(r"live-now|/live\.html", text):
                        bad.append(os.path.relpath(os.path.join(base, name), REPO))
        self.assertEqual(bad, [])
        self.assertNotIn("live-now", changelog_page())
        self.assertNotIn("live-now", read("tools", "changelog_to_html.py"))


if __name__ == "__main__":
    unittest.main()
