#!/usr/bin/env python3
"""The line of numbers in the front page's header (run by ./run_tests.sh --fast and by the CI). The owner: "Game stats would be cool on the page. How many games played / in progress etc."

"3 matches being played - 7 players online - 1,284 games played (21 today)": the numbers come from the game server through this site's own address /stats ({"now":{"matches","players"},
"online":{"day","total"},"local":{"day","total"},"since"}); a site that has no /stats is asked for /busy ({"matches","players"}) and the line then shows the live part only; when neither
answers there is no line (no text of an error). The page looks at once and every 30 seconds while it is visible, and not while it is hidden.

  - the block STATS of web/lobby.html is RUN (node, when it is installed) on tables, with a fake clock, a fake visibility and fake answers of the site (tests/scripts/web_stats_check.js), and
    the page as a whole in the fake browser of tests/scripts/web_name_check.js (what it asks, what it shows, when it looks);
  - what needs no browser is read from the files: the line is hidden in the markup until numbers come, the block has its markers and no markup or storage, the page asks only its own /stats
    and /busy with no cache and no cookies, the dot is green only while a match is played, the two parts wrap as units with no dot at the end or the start of a line, and on a phone the line
    takes the place of the slogan and shows the live part only (the header must not grow: the START! button stays on the first screen of a 390 x 844 phone).
"""
import os
import re
import shutil
import subprocess
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LOBBY = os.path.join(REPO, "web", "lobby.html")
CHECK_JS = os.path.join(REPO, "tests", "scripts", "web_stats_check.js")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


class TheMarkupAndTheStyle(unittest.TestCase):
    def setUp(self):
        self.page = read(LOBBY)
        self.style = self.page[:self.page.index("</style>")]

    def test_the_line_is_in_the_header_under_the_banner_and_hidden_until_numbers_come(self):
        line = '<p class="stats" id="stats" hidden><span id="stats-dot" class="live" aria-hidden="true"></span><span class="parts"><span class="partsrow"><span id="stats-live"></span> <span id="stats-played"></span></span></span></p>'                      # (the space is for the text of the line: "online 1,284", not "online1,284"; a flex row draws none of it)
        self.assertIn(line, self.page)
        header = self.page[self.page.index('<header class="top">'):self.page.index("</header>")]
        self.assertLess(header.index('<h1 class="banner">'), header.index('id="stats"'))
        self.assertLess(header.index('id="stats"'), header.index('<p class="lead">'))

    def test_the_block_has_its_markers_once_and_makes_no_markup_and_reads_no_storage(self):
        self.assertEqual(self.page.count("// STATS_BEGIN"), 1)
        self.assertEqual(self.page.count("// STATS_END"), 1)
        block = self.page[self.page.index("// STATS_BEGIN"):self.page.index("// STATS_END")]
        self.assertNotRegex(block, r"innerHTML|insertAdjacentHTML|document\.|localStorage|window\.")
        self.assertIn("var STATS_EVERY = 30000;", block)

    def test_the_page_asks_only_its_own_stats_and_busy_with_no_cache_and_no_cookies_and_gives_up(self):
        self.assertIn("return fetch(url, { cache: 'no-store', credentials: 'omit', signal: controller ? controller.signal : undefined })", self.page)
        self.assertIn("if (response.status !== 200) throw new Error('the site answered ' + response.status);", self.page)       # (the 200 of a page that is no answer is caught by the parse)
        self.assertIn("setTimeout(function () { if (controller) controller.abort(); }, 8000);", self.page)
        self.assertEqual(len(re.findall(r"io\.get\('(/[a-z]+)'\)", self.page)), 2)
        self.assertEqual(re.findall(r"io\.get\('(/[a-z]+)'\)", self.page), ["/stats", "/busy"])
        self.assertIn("if (typeof fetch === 'function') {", self.page)                                                    # (a browser that has none has no line)

    def test_the_page_looks_again_when_it_is_shown_and_stops_while_it_is_hidden(self):
        self.assertIn("document.addEventListener('visibilitychange', poller.visibility);", self.page)
        self.assertIn("hidden: function () { return !!document.hidden; }", self.page)

    def test_the_numbers_are_put_into_the_page_as_text(self):
        part = self.page[self.page.index("function showStats(stats) {"):self.page.index("if (typeof fetch === 'function') {")]
        self.assertIn("$('stats-live').textContent = words.live;", part)
        self.assertIn("$('stats-played').textContent = words.played;", part)
        self.assertNotIn("innerHTML", part)

    def test_the_dot_is_grey_and_green_only_while_a_match_is_being_played(self):
        self.assertRegex(self.style, r"\.live \{[^}]*background: #8b9a93;")
        self.assertRegex(self.style, r"\.live\.on \{[^}]*background: #44e08a;")
        self.assertIn("$('stats-dot').className = words.on ? 'live on' : 'live';", self.page)

    def test_on_a_phone_the_line_takes_the_place_of_the_slogan_and_shows_the_live_part_only(self):
        phone = self.style[self.style.index("@media (max-width: 700px) {"):]
        for needle in (".intro p:not(.lead):not(.stats), .links { display: none; }", "#stats-played { display: none; }", ".stats:not([hidden]) + .lead { display: none; }", ".stats .partsrow { flex-wrap: nowrap; }"):
            self.assertIn(needle, phone, needle)

    def test_the_parts_wrap_as_units_and_the_dot_between_them_is_clipped_where_it_would_begin_a_line(self):
        # the dot is a pseudo-element in the left space of the second part; the first part's space and the row's negative margin are the same size, an outer box with overflow hidden
        # cuts that space off, so a part that begins a line shows no dot (a dot at the end of the first line, or at the start of the second, looked like a mistake on a tablet)
        row = re.search(r"\.stats \.partsrow \{ display: flex; flex-wrap: wrap; gap: 2px 0; margin-left: -(\d+)px; \}", self.style)
        space = re.search(r"\.stats \.partsrow > span \{ position: relative; padding-left: (\d+)px; \}", self.style)
        self.assertTrue(row and space, "the row and its parts")
        self.assertEqual(row.group(1), space.group(1))
        self.assertIn(".stats .parts { min-width: 0; overflow: hidden; }", self.style)
        self.assertRegex(self.style, r'#stats-played::before \{ content: "\\00b7"; position: absolute; left: \d+px; \}')
        self.assertIn("#stats-played:empty { display: none; }", self.style)                  # (nothing but /busy: no totals, and no dot of theirs)
        self.assertNotIn("stats-sep", self.page)


@unittest.skipUnless(shutil.which("node"), "node is not installed: the line's rules were NOT run (tests/scripts/web_stats_check.js)")
class ThePagesOwnCode(unittest.TestCase):
    def test_the_shapes_the_fallbacks_the_plurals_the_separators_and_the_clock(self):
        done = subprocess.run([shutil.which("node"), CHECK_JS, LOBBY], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn(" 0 failures", done.stdout)


if __name__ == "__main__":
    unittest.main()
