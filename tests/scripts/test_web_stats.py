#!/usr/bin/env python3
"""The line of numbers in the front page's footer (run by ./run_tests.sh --fast and by the CI). The owner: "Game stats would be cool on the page. How many games played / in progress etc."

"3 matches being played - 7 players online - 1,284 games played (21 today)": the numbers come from the game server through this site's own address /stats ({"now":{"matches","players"},
"online":{"day","total"},"local":{"day","total"},"since"}); a site that has no /stats is asked for /busy ({"matches","players"}) and the line then shows the live part only; when neither
answers there is no line (no text of an error). The page looks at once and every 30 seconds while it is visible, and not while it is hidden.

  - the block STATS of web/lobby.html is RUN (node, when it is installed) on tables, with a fake clock, a fake visibility and fake answers of the site (tests/scripts/web_stats_check.js), and
    the page as a whole in the fake browser of tests/scripts/web_name_check.js (what it asks, what it shows, when it looks);
  - what needs no browser is read from the files: the line stands in the footer and is hidden in the markup until numbers come, the block has its markers and no markup or storage, the page
    asks only its own /stats and /busy with no cache and no cookies, the dot is green only while a match is played, and the separator between the two parts is drawn by the style (it is
    not there when only /busy answered).
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

    def test_the_line_is_in_the_footer_before_the_version_row_and_hidden_until_numbers_come(self):
        line = '<p class="stats" id="stats" hidden><i class="live" id="stats-dot" aria-hidden="true"></i><span id="stats-live"></span><span id="stats-played"></span></p>'       # (the separator between the two parts is the style's)
        self.assertEqual(self.page.count(line), 1)
        footer = self.page[self.page.index('<footer class="bar">'):self.page.index("</footer>")]
        self.assertIn(line, footer)
        self.assertLess(footer.index('id="stats"'), footer.index('<div class="bar-row">'))                        # (above the version line, the footer links and the Screen buttons)
        # hidden until numbers come: the attribute is in the markup, and the style's own display for the line must not undo it
        self.assertIn("[hidden] { display: none !important; }", self.style)
        self.assertNotRegex(self.style, r"\.bar \.stats \{[^}]*display:")                                         # (the line is one text that flows and wraps, as the phone picture draws it: no flex row of parts)

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

    def test_the_separator_between_the_two_parts_is_the_styles_and_is_not_there_without_the_second_part(self):
        self.assertIn('#stats-played::before { content: "\\00a0\\00b7\\0020"; }', self.style)                   # (the dot between "... online" and "... played": a no-break space before it, so that a line never begins with it, and a space after it)
        self.assertIn("#stats-played:empty { display: none; }", self.style)                  # (nothing but /busy: no totals, and no separator of theirs)
        self.assertNotIn("stats-sep", self.page)


@unittest.skipUnless(shutil.which("node"), "node is not installed: the line's rules were NOT run (tests/scripts/web_stats_check.js)")
class ThePagesOwnCode(unittest.TestCase):
    def test_the_shapes_the_fallbacks_the_plurals_the_separators_and_the_clock(self):
        done = subprocess.run([shutil.which("node"), CHECK_JS, LOBBY], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn(" 0 failures", done.stdout)


if __name__ == "__main__":
    unittest.main()
