#!/usr/bin/env python3
"""The front page's Rejoin button (run by ./run_tests.sh --fast and by the CI). The owner: "if your browser crashed or your power went out, you could ... hold the game paused until you get back".

The server holds the seat of a player whose connection is lost and the game page comes back to its seat by itself (a reload, a restart of the server). The front page is the last door: a player who
closed the tab, or whose browser crashed, opens the site's front page and finds ONE button, "Rejoin your match (CODE)", with a line under it, while this browser holds a fresh key of a seat on this
site's game server. The key is a secret and is never read out of the storage into an address, a text or a log: the game page finds it itself.

  - the block REJOIN of web/lobby.html is RUN (node, when it is installed: tests/scripts/web_rejoin_block_check.js): which storage entries count (exactly the game's JSON, a room by the page's own rule,
    a seat 0 - 3), this site's server (the game page's own join, on both schemes), the age (a day to the millisecond: older is removed, as the game does; a minute ahead: not offered, not removed),
    another server's entry (kept, not offered), the newest of several, a storage that throws, the words, the address, the game page reading that address, and no key anywhere;
  - the page as a whole is run in the fake browser of tests/scripts/web_name_check.js (when the button is there, what it says, where it goes, the return from the browser's memory);
  - what needs no browser is read from the files: the markup (hidden, above the cards, no text of its own), the style (the page's teal button, a note of one line, the whole width on a phone), the
    block (its markers, no DOM and no storage of its own, the page's own room-code rule, the game's own constants), the wiring (the plain front page only, text only, this tab, no write to the storage);
  - what it is checked against is pinned too: the game page's way of building the join address, and the application's way of naming the server in a key.
The way back in a real browser (a reload, a restart of the server, the button) is tests/scripts/web_rejoin_check.py (opt-in).
"""
import os
import re
import shutil
import subprocess
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LOBBY = os.path.join(REPO, "web", "lobby.html")
SHELL = os.path.join(REPO, "web", "shell.html")
CHECK_JS = os.path.join(REPO, "tests", "scripts", "web_rejoin_block_check.js")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


class TheMarkupAndTheStyle(unittest.TestCase):
    def setUp(self):
        self.page = read("web", "lobby.html")
        self.style = self.page[:self.page.index("</style>")]

    def test_the_block_is_hidden_in_the_markup_above_the_page_with_the_cards_and_has_no_text_of_its_own(self):
        markup = '<section id="rejoin" class="rejoin" aria-label="Your running match" hidden>\n        <button id="rejoin-go" type="button" class="btn big"></button>\n        <p class="rejoin-note" id="rejoin-note"></p>\n    </section>'
        self.assertEqual(self.page.count(markup), 1)
        self.assertLess(self.page.index("</header>"), self.page.index('<section id="rejoin"'))
        self.assertLess(self.page.index('<section id="rejoin"'), self.page.index('<main class="page">'))
        self.assertEqual(len(re.findall(r'id="rejoin[-a-z]*"', self.page)), 3)

    def test_a_hidden_block_takes_no_room_and_nothing_else_moves(self):
        self.assertIn("[hidden] { display: none !important; }", self.style)                # (the block is a flex row: this rule is what hides it)
        self.assertRegex(self.style, r"\.rejoin \{ display: flex; flex-wrap: wrap;")
        page_grid = re.search(r"\.page \{ display: grid;[^}]*\}", self.style).group(0)
        self.assertNotIn("rejoin", page_grid)                                              # (it is not a cell of the page's grid, whose areas stay as they were)

    def test_it_is_the_pages_teal_button_with_its_focus_and_a_note_of_one_line(self):
        self.assertRegex(self.style, r"a:focus-visible, \.btn:focus-visible,")             # (the outline of every button of the page: the Rejoin button is one)
        self.assertIn(".rejoin-note { flex: 1 1 260px; min-width: 0; font-size: 14px; line-height: 1.35; overflow-wrap: anywhere; }", self.style)
        self.assertIn(".note { max-width: 34ch; }", self.style)                            # (the cards' note wraps at 34 characters: this one is not a .note, so that it does not, and the two notes of the cards stay two)
        self.assertEqual(len(re.findall(r'<p class="note[ "]', self.page)), 2)
        self.assertIn(".rejoin .btn { text-align: center; overflow-wrap: anywhere; }", self.style)       # (a room code of 32 characters has no place to break)

    def test_on_a_phone_the_button_takes_the_width(self):
        phone = self.style[self.style.index("@media (max-width: 700px) {"):]
        self.assertIn(".rejoin .btn { width: 100%; }", phone)


class TheBlockAndTheWiring(unittest.TestCase):
    def setUp(self):
        self.page = read("web", "lobby.html")
        self.block = self.page[self.page.index("// REJOIN_BEGIN"):self.page.index("// REJOIN_END")]
        self.wiring = self.page[self.page.index("// REJOIN_END"):self.page.index("var room = '';")]

    def test_the_markers_are_once_and_the_block_touches_neither_the_page_nor_the_storage_itself(self):
        self.assertEqual(self.page.count("// REJOIN_BEGIN"), 1)
        self.assertEqual(self.page.count("// REJOIN_END"), 1)
        self.assertNotRegex(self.block, r"innerHTML|insertAdjacentHTML|document\.|window\.|localStorage|sessionStorage|location\.|console\.|fetch\(|XMLHttpRequest|setItem")
        for name in ("rejoinServer", "rejoinParse", "rejoinOffer", "rejoinWords", "rejoinQuery"):
            self.assertEqual(len(re.findall(r"function %s\(" % name, self.page)), 1, name)

    def test_the_constants_are_the_games(self):
        self.assertIn("var REJOIN_PREFIX = 'ants.rejoin.';", self.block)
        self.assertIn("var REJOIN_MAX_AGE_MS = 24 * 60 * 60 * 1000;", self.block)
        self.assertIn("var REJOIN_FUTURE_MS = 60 * 1000;", self.block)
        store = read("include", "ants_app", "rejoin_store.hpp")
        self.assertIn('static constexpr const char* kKeyPrefix = "ants.rejoin.";', store)
        self.assertIn("inline constexpr int64_t kRejoinMaxAgeMs = int64_t{24} * 3600 * 1000;", store)

    def test_the_room_code_rule_is_the_pages_own_everywhere(self):
        rule = "[A-Za-z0-9_-]{1,32}"
        self.assertEqual(len(re.findall(re.escape("/^" + rule + "$/"), self.page.replace(self.block, ""))), 3)       # (the address's room, the Join button, the shared link)
        self.assertIn("/^ants\\.rejoin\\.(" + rule + ")\\.([0-3])$/", self.block)
        self.assertIn("/^" + rule + "$/", read("web", "shell.html"))                                                  # (and the game page's)

    def test_what_it_checks_against_is_pinned(self):
        shell = read("web", "shell.html")
        self.assertIn("out.args.push('--join-url', (secure ? 'wss://' : 'ws://') + host + join);", shell)             # (the game page's join: rejoinServer is this, with /ws)
        self.assertIn("function rejoinServer(secure, host) { return (secure ? 'wss://' : 'ws://') + host + '/ws'; }", self.block)
        app = read("src", "ants_app", "application.cpp")
        self.assertIn("const std::string server = rejoin_server_text(config_.net_address, config_.net_port, config_.net_url);", app)   # (the entry that a join looks up)
        self.assertIn("cfg.net_url = argv[++i];", app)                                                                  # (--join-url, as the page gave it)
        self.assertIn("if (!url.empty()) return url;", read("src", "ants_app", "rejoin_store.cpp"))                    # (a browser build's server is its URL)

    def test_the_key_is_not_in_what_the_block_makes(self):
        parse = self.block[self.block.index("function rejoinParse("):self.block.index("// The match to offer")]
        self.assertIn("return { room: m[1], seat: Number(m[2]), server: v.s, t: v.t };", parse)                        # (v.k is checked and let go)
        self.assertIn("return best ? { room: best.room, seat: best.seat, t: best.t } : null;", self.block)
        self.assertEqual(len(re.findall(r"\bv\.k\b", self.block)), 3)                                                   # (only the three checks of the key read it)

    def test_the_button_is_looked_for_on_the_plain_front_page_only(self):
        branch = self.page[self.page.index("function fromAddress() {"):self.page.index("// A match that an address asks for is the page of somebody who was sent a link")]
        self.assertRegex(branch, r"\} else \{\s*\$\('cards'\)\.hidden = false;\s*\$\('how'\)\.hidden = false;\s*showRejoin\(\);")
        self.assertEqual(len(re.findall(r"showRejoin\(\)", self.page)), 3)                                              # the definition, that call, and refreshRejoin
        self.assertIn("function refreshRejoin() { if (!$('cards').hidden) showRejoin(); }", self.wiring)

    def test_it_is_looked_at_again_when_the_page_returns_and_when_another_tab_writes(self):
        self.assertIn("window.addEventListener('pageshow', function (e) { if (e && e.persisted) refreshRejoin(); });", self.wiring)
        self.assertIn("window.addEventListener('storage', refreshRejoin);", self.wiring)
        self.assertEqual(len(re.findall(r"addEventListener\('visibilitychange'", self.page)), 1)                        # (the line of numbers' own: tests/scripts/web_name_check.js counts it)

    def test_the_press_takes_this_tab_to_the_game_page_with_the_remembered_name_and_shape_and_no_key(self):
        self.assertIn("window.location.assign(new URL('./' + rejoinQuery(rejoinOffered, remembered.ok ? remembered.name : '', aspect), window.location.href).href);", self.wiring)
        self.assertIn("var remembered = nameCheck(recall(NAME_KEY));", self.wiring)
        self.assertNotIn("window.open", self.wiring)                                                                    # (no new tab: the other join links of the page stay in this one)
        self.assertEqual(len(re.findall(r"window\.open\(", self.page)), 1)                                              # (the one that test_web_lobby.py counts)

    def test_the_words_are_put_in_as_text_and_nothing_is_written_to_the_storage(self):
        self.assertIn("$('rejoin-go').textContent = words.button;", self.wiring)
        self.assertIn("$('rejoin-note').textContent = words.note;", self.wiring)
        self.assertNotRegex(self.wiring, r"innerHTML|remember\(|setItem|removeItem")
        self.assertNotIn("remember('ants.rejoin", self.page)

    def test_the_words_are_the_ones_the_plan_gave(self):
        self.assertIn("button: 'Rejoin your match (' + offer.room + ')'", self.block)
        self.assertIn("note: 'Your match in room ' + offer.room + ' is still running: go back to your seat.'", self.block)
        self.assertIn("'?join=/ws&room=' + encodeURIComponent(offer.room) + '&seat=' + offer.seat + '&name=' + encodeURIComponent(name) + '&aspect=' + (shape === '4:3' ? '4:3' : '16:9')", self.block)


@unittest.skipUnless(shutil.which("node"), "node is not installed: the Rejoin block's rules were NOT run (tests/scripts/web_rejoin_block_check.js)")
class ThePagesOwnCode(unittest.TestCase):
    def test_the_entries_the_server_the_age_the_newest_the_words_the_address_and_the_key(self):
        done = subprocess.run([shutil.which("node"), CHECK_JS, LOBBY, SHELL], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn(" 0 failures", done.stdout)


if __name__ == "__main__":
    unittest.main()
