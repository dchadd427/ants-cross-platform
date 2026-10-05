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
        self.assertIn(".rejoin { display: flex; flex-direction: column; align-items: flex-start; gap: 8px; margin: 18px 0 0; }", self.style)       # (the button, and the line UNDER it)
        page_grid = re.search(r"\.page \{ display: grid;[^}]*\}", self.style).group(0)
        self.assertNotIn("rejoin", page_grid)                                              # (it is not a cell of the page's grid, whose areas stay as they were)

    def test_it_is_the_pages_teal_button_with_its_focus_and_a_note_of_one_line(self):
        self.assertRegex(self.style, r"a:focus-visible, \.btn:focus-visible,")             # (the outline of every button of the page: the Rejoin button is one)
        self.assertIn(".rejoin-note { max-width: 100%; font-size: 14px; line-height: 1.35; overflow-wrap: anywhere; }", self.style)
        self.assertIn(".note { max-width: 34ch; }", self.style)                            # (the cards' note wraps at 34 characters: this one is not a .note, so that it does not, and the two notes of the cards stay two)
        self.assertEqual(len(re.findall(r'<p class="note[ "]', self.page)), 2)
        self.assertIn(".rejoin .btn { max-width: 100%; text-align: center; overflow-wrap: anywhere; }", self.style)       # (a room code of 32 characters has no place to break)

    def test_on_a_phone_the_button_takes_the_width(self):
        phone = self.style[self.style.index("@media (max-width: 700px) {"):]
        self.assertIn(".rejoin { margin: 10px 0 0; align-items: stretch; }", phone)
        self.assertIn(".rejoin .btn { width: 100%; }", phone)


class TheBlockAndTheWiring(unittest.TestCase):
    def setUp(self):
        self.page = read("web", "lobby.html")
        self.keys = self.page[self.page.index("// REJOINKEY_BEGIN"):self.page.index("// REJOINKEY_END")]       # (the keys that this browser holds: the same text in the game page)
        self.block = self.page[self.page.index("// REJOIN_BEGIN"):self.page.index("// REJOIN_END")]            # (the front page's own: where the button goes)
        self.wiring = self.page[self.page.index("// REJOIN_END"):self.page.index("var room = '';")]

    def test_the_markers_are_once_and_the_blocks_touch_neither_the_page_nor_the_storage_themselves(self):
        for marker in ("// REJOINKEY_BEGIN", "// REJOINKEY_END", "// REJOIN_BEGIN", "// REJOIN_END"):
            self.assertEqual(self.page.count(marker), 1, marker)
        for text in (self.keys, self.block):
            self.assertNotRegex(text, r"innerHTML|insertAdjacentHTML|document\.|window\.|localStorage|sessionStorage|location\.|console\.|fetch\(|XMLHttpRequest|setItem")
        for name in ("rejoinParse", "rejoinOffer"):
            self.assertEqual(len(re.findall(r"function %s\(" % name, self.keys)), 1, name)
        for name in ("rejoinServer", "rejoinWords", "rejoinQuery"):
            self.assertEqual(len(re.findall(r"function %s\(" % name, self.block)), 1, name)

    def test_the_constants_are_the_games(self):
        self.assertIn("var REJOIN_PREFIX = 'ants.rejoin.';", self.keys)
        self.assertIn("var REJOIN_MAX_AGE_MS = 24 * 60 * 60 * 1000;", self.keys)
        self.assertIn("var REJOIN_FUTURE_MS = 60 * 1000;", self.keys)
        store = read("include", "ants_app", "rejoin_store.hpp")
        self.assertIn('static constexpr const char* kKeyPrefix = "ants.rejoin.";', store)
        self.assertIn("inline constexpr int64_t kRejoinMaxAgeMs = int64_t{24} * 3600 * 1000;", store)

    def test_the_room_code_rule_is_the_pages_own_everywhere(self):
        rule = "[A-Za-z0-9_-]{1,32}"
        self.assertEqual(len(re.findall(re.escape("/^" + rule + "$/"), self.page.replace(self.keys, "").replace(self.block, ""))), 3)       # (the address's room, the Join button, the shared link)
        self.assertIn("/^ants\\.rejoin\\.(" + rule + ")\\.([0-3])$/", self.keys)
        self.assertIn("/^" + rule + "$/", read("web", "shell.html"))                                                  # (and the game page's)

    def test_what_it_checks_against_is_pinned(self):
        shell = read("web", "shell.html")
        self.assertIn("out.args.push('--join-url', (secure ? 'wss://' : 'ws://') + host + join);", shell)             # (the game page's join: rejoinServer is this, with /ws)
        self.assertIn("function rejoinServer(secure, host) { return (secure ? 'wss://' : 'ws://') + host + '/ws'; }", self.block)
        app = read("src", "ants_app", "application.cpp")
        self.assertIn("const std::string server = rejoin_server_text(config_.net_address, config_.net_port, config_.net_url);", app)   # (the entry that a join looks up)
        self.assertIn("cfg.net_url = argv[++i];", app)                                                                  # (--join-url, as the page gave it)
        self.assertIn("if (!url.empty()) return url;", read("src", "ants_app", "rejoin_store.cpp"))                    # (a browser build's server is its URL)

    def test_the_key_is_not_in_what_the_blocks_make(self):
        parse = self.keys[self.keys.index("function rejoinParse("):self.keys.index("// The seat to offer")]
        self.assertIn("return { room: m[1], seat: Number(m[2]), server: v.s, t: v.t };", parse)                        # (v.k is checked and let go)
        self.assertIn("return best ? { room: best.room, seat: best.seat, t: best.t } : null;", self.keys)
        self.assertEqual(len(re.findall(r"\bv\.k\b", self.keys)), 3)                                                   # (only the three checks of the key read it)

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


class TheGamePageDoesNotAskARejoinerForAName(unittest.TestCase):
    """The browser check found that a reload of a game page waited for a name for ever: the page drops the name from its address bar (so that a copy of the address asks its owner), the reload's address
    looked like a shared link, and the name step held the game back before it could connect with its key. A browser that holds the key of that very seat is not asked."""

    def setUp(self):
        self.shell = read("web", "shell.html")
        self.lobby = read("web", "lobby.html")

    def test_the_game_page_carries_the_same_keys_block_as_the_front_page(self):
        def block(text):
            return text[text.index("// REJOINKEY_BEGIN"):text.index("// REJOINKEY_END")]
        self.assertEqual(self.shell.count("// REJOINKEY_BEGIN"), 1)
        self.assertEqual(block(self.shell), block(self.lobby))

    def test_the_step_asks_unless_the_browser_holds_this_seat_and_a_frame_is_never_asked(self):
        self.assertIn("var asks = !ANTS_EMBED && ANTS_PAGE.asksForName(window.location.search);", self.shell)
        self.assertIn("try { asks = !holdsThisSeat(window.location.search, ANTS_ARGS, window.localStorage, Date.now()); } catch (e) { /* no storage: the step asks, as for any shared link */ }", self.shell)
        self.assertIn("return at !== -1 && rejoinOffer(storage, now, args[at + 1], q.get('room') || '', /^[0-3]$/.test(seat || '') ? Number(seat) : -1) !== null;", self.shell)

    def test_the_address_still_loses_its_name_so_that_a_copy_of_it_asks_its_owner(self):
        self.assertIn("var stripped = ANTS_PAGE.withoutName(window.location.search);", self.shell)
        self.assertIn("window.history.replaceState(null, '', window.location.pathname + stripped + window.location.hash);", self.shell)


class TheBrowserCheckAndTheGame(unittest.TestCase):
    """The opt-in check (tests/scripts/web_rejoin_check.py) is not run here, but what it stands on is pinned: what the game exports for it, what it asks of the site and the server, its parts and its exit statuses."""

    def setUp(self):
        self.check = read("tests", "scripts", "web_rejoin_check.py")

    def test_the_game_exports_what_the_check_reads(self):
        app = read("src", "ants_app", "application.cpp")
        self.assertIn('extern "C" EMSCRIPTEN_KEEPALIVE int ants_probe(int what) {', app)
        self.assertIn("default: return net_overlay_probe(g_web_app->net_overlay_now(), what);", app)           # (16 and 10000 and up: the overlay's lines; the model function is tested natively)
        self.assertIn("int net_overlay_probe(const NetOverlayLine& overlay, int what);", read("include", "ants_app", "net_overlay.hpp"))
        for needle in ("Module._ants_probe(16)", "Module._ants_probe(10000 + 1000 * l + i)", "Module._ants_probe(5)", "Module._ants_match_running()"):
            self.assertIn(needle, self.check, needle)
        self.assertIn("window.parent.postMessage({ants: 'sync'", app)                                           # (a top-level game posts its hashes on the channel that the check listens to)
        self.assertIn('new BroadcastChannel("ants-sync")', app)
        self.assertIn("new BroadcastChannel('ants-sync')", self.check)

    def test_the_probes_touch_the_game_only_once_the_page_says_that_it_is_ready(self):
        # (index.js binds each export to the program at its first call: a call while the program is still being compiled leaves it undefined for the life of the page; a slow connection showed it)
        for name in ("STATE_JS", "OVERLAY_JS"):
            body = self.check.split(name + ' = """', 1)[1].split('"""', 1)[0]
            self.assertIn("if (!window.isReadyToPlay || typeof Module === 'undefined'", body, name)
        self.assertEqual(len(re.findall(r"Module\._ants_\w+\(", self.check)), 4)                                   # (the four calls of the two scripts: nothing else reads the game)

    def test_it_runs_the_server_as_the_stack_does_and_asks_the_site_for_the_servers_busy_answer(self):
        self.assertIn('"docker-compose.stack.yml"', self.check)                                                   # (the site's own options: the demo rooms)
        self.assertIn("stack_command.py", self.check)
        self.assertNotIn("--reconnect", self.check.replace("NO reconnect option", ""))                           # (what holds the seats is the default: no option is given)
        self.assertNotIn("--no-reconnect", self.check)
        self.assertIn('urllib.request.urlopen(web + "busy", timeout=10)', self.check)
        self.assertIn('set(busy) == {"matches", "players"}', self.check)
        self.assertIn("signal.SIGTERM", self.check)

    def test_it_has_the_four_parts_and_the_statuses_of_the_other_browser_checks(self):
        self.assertIn('PARTS = ("reload", "restart", "rejoin", "none")', self.check)
        for part in ("reload", "restart", "rejoin", "none"):
            self.assertIn('if wanted("%s"):' % part, self.check)
        self.assertEqual(len(re.findall(r"return 3\b", self.check)), 6)                                         # (a skip: no browser, no server program, no page, the site's /busy does not answer, it is not the server's, a browser that did not start)
        self.assertIn("return 1 if failures else 0", self.check)
        for text in ("Exit status 0:", "3: the check could not be made"):
            self.assertIn(text, self.check)

    def test_a_player_is_a_browser_of_its_own_and_the_key_is_looked_for_but_never_printed(self):
        self.assertIn("self.browser = Browser(path)", self.check)                                                 # (its own profile: its own storage)
        self.assertEqual(len(re.findall(r"print\([^)]*key", self.check)), 0)
        self.assertIn("def key_hexes(self):", self.check)
        self.assertIn("def key_nowhere(label, ps):", self.check)
        self.assertNotRegex(self.check, r"/home/|/Users/|/tmp/")                           # (nothing of this machine in the file)

    def test_the_wrapper_is_opt_in_and_skips_without_its_environment(self):
        wrapper = read("tests", "scripts", "test_web_rejoin.sh")
        self.assertIn('if [ -z "$ANTS_WEB_URL" ] || [ -z "$ANTS_WS_PORT" ]; then', wrapper)
        self.assertIn('python3 "$ROOT/tests/scripts/web_rejoin_check.py" --web "$ANTS_WEB_URL" --ws-port "$ANTS_WS_PORT" "$@"', wrapper)
        self.assertIn('if [ "$status" -eq 3 ]; then', wrapper)
        self.assertNotIn("test_web_rejoin.sh", read("run_tests.sh"))                                              # (opt-in, like the other browser checks)
        self.assertTrue(os.access(os.path.join(REPO, "tests", "scripts", "test_web_rejoin.sh"), os.X_OK))


@unittest.skipUnless(shutil.which("node"), "node is not installed: the Rejoin block's rules were NOT run (tests/scripts/web_rejoin_block_check.js)")
class ThePagesOwnCode(unittest.TestCase):
    def test_the_entries_the_server_the_age_the_newest_the_words_the_address_and_the_key(self):
        done = subprocess.run([shutil.which("node"), CHECK_JS, LOBBY, SHELL], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn(" 0 failures", done.stdout)


if __name__ == "__main__":
    unittest.main()
