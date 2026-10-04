#!/usr/bin/env python3
"""The front page (run by ./run_tests.sh --fast and by the CI). The owner: "when you click the play online button it opens in a new tab can we just make that the default. you can play single on
the play online tab by setting to 1 player consolidate them". The Play online page (web/lobby.html, once web/four.html) is the front page at "/" and has single player in it; the game page
(web/shell.html) has a Menu button that goes back to it in the same tab and reads the local parameters of a game on this computer through a whitelist.

  - the pages' own rules are RUN (node, when it is installed): what the form means (players 1 is a game on this computer: its address; the opponents that a browser remembered; a first visit
    has Medium), the game page's whitelist (the six maps by key, the three levels, a cleaned name: never the text of the address) and the two pages agreeing (tests/scripts/web_lobby_check.js);
  - what needs no browser is read from the files: the form's markup (players 1 - 4, the two rows of bots, the button), the header links and the footer of the front page, that nothing opens a new tab
    but the links that leave the game, the Menu link of the game page, one name for the catalog link on both pages, the Dockerfile that copies the lobby and the game page's second path, the CI's
    page check, and the documents.
The routes of nginx are run in tests/scripts/test_nginx_routes.py; the whole flow in a real browser is tests/scripts/web_home_check.py (opt-in).
"""
import os
import re
import shutil
import subprocess
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SHELL = os.path.join(REPO, "web", "shell.html")
LOBBY = os.path.join(REPO, "web", "lobby.html")
CHECK_JS = os.path.join(REPO, "tests", "scripts", "web_lobby_check.js")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


class PageCase(unittest.TestCase):
    """assertRegex / assertNotRegex without printing the whole page when they fail"""

    def found(self, text, pattern, msg=None):
        self.assertIsNotNone(re.search(pattern, text), msg or ("lacks: " + pattern))

    def not_found(self, text, pattern, msg=None):
        self.assertIsNone(re.search(pattern, text), msg or ("has: " + pattern))


class TheFrontPageMarkup(PageCase):
    def setUp(self):
        self.page = read("web", "lobby.html")

    def test_it_is_titled_ants_1998_and_has_the_placeholders_once(self):
        self.assertIn("<title>Ants (1998)@@SITE_TITLE@@</title>", self.page)
        for placeholder in ("@@SITE_TITLE@@", "@@SITE_FOOTER@@", "@@GAME_VERSION@@", "@@BUILD_ID@@"):
            self.assertEqual(self.page.count(placeholder), 1, placeholder)

    def test_the_form_offers_players_1_to_4_and_a_row_of_bots_for_each_mode(self):
        select = re.search(r'<select id="players">(.*?)</select>', self.page, re.S)
        self.assertEqual(re.findall(r'<option value="(\d)">', select.group(1)), ["1", "2", "3", "4"])
        for select_id, first in (("fill", ("", "Leave empty")), ("opponents", ("", "None"))):
            body = re.search(r'<select id="%s">(.*?)</select>' % select_id, self.page, re.S).group(1)
            self.assertEqual(re.findall(r'<option value="([a-z]*)">([^<]*)</option>', body), [first, ("easy", "Easy bots"), ("medium", "Medium bots"), ("hard", "Hard bots")], select_id)
        self.found(self.page, r'<label for="opponents">Opponents</label>')
        self.found(self.page, r'<label for="fill">Empty seats at START</label>')
        self.found(self.page, r'<div class="fillline" id="solo-line" hidden>')
        self.found(self.page, r'<button id="create" type="button" class="btn go">Create the match</button>')           # (the script makes it Play for 1 player)
        self.assertIn("$('create').textContent = solo ? 'Play' : 'Create the match';", self.page)

    def test_a_first_visit_is_treasure_one_player_medium(self):
        self.assertIn("var DEFAULT_MAP_KEY = 'treasure';", self.page)
        self.assertIn("playersSelect.value = String(playersChoice(recall('ants-four-players'), 1));", self.page)
        self.assertIn("function soloBots(stored) { return stored === null || stored === undefined ? 'medium' : validFill(stored); }", self.page)
        self.assertIn("opponentsSelect.value = soloBots(recall('ants-solo-bots'));", self.page)
        self.assertIn("fillSelect.value = validFill(recall('ants-four-fill'));", self.page)                  # (the rooms' choice is what it always was: leave empty until chosen)

    def test_the_header_links_the_footer_the_name_the_aspect_choice_and_the_room_buttons_are_there(self):
        for needle in ('href="/asset_catalog/"', 'href="/changelog.html"', 'href="https://github.com/dchadd427/ants-cross-platform"', 'href="https://github.com/dchadd427/ants-cross-platform/issues"'):
            self.assertGreaterEqual(self.page.count(needle), 1, needle)
        self.assertIn('<span class="long">Sprites and sounds</span>', self.page)
        self.assertIn('id="game-version"', self.page)
        self.assertIn('id="game-build-id"', self.page)
        self.assertEqual(len(re.findall(r'<input[^>]*id="player-name"', self.page)), 1)
        self.assertIn("var NAME_KEY = 'ants.name';", self.page)
        self.assertIn("var ASPECT_KEY = 'ants.aspect.v2';", self.page)
        self.found(self.page, r'<select id="aspect-select"[^>]*>\s*<option value="16:9">16:9</option>')
        for ident in ("play-tab", "all-here", "all-windows", "new-room", "any-link", "seat-rows"):
            self.assertIn('id="%s"' % ident, self.page)

    def test_nothing_opens_a_new_tab_but_the_links_that_leave_the_game_and_the_explicit_windows(self):
        anchors = re.findall(r"<a [^>]*>", self.page)
        blank = [a for a in anchors if 'target="_blank"' in a]
        hrefs = sorted(set(re.search(r'href="([^"]*)"', a).group(1) for a in blank))
        self.assertEqual(hrefs, ["/asset_catalog/", "/changelog.html", "https://github.com/dchadd427/ants-cross-platform", "https://github.com/dchadd427/ants-cross-platform/issues"])
        self.assertEqual(len(re.findall(r"window\.open\(", self.page)), 1, "the one window.open is the explicit 'Open a window' of a seat")
        self.assertIn("function openWindow(seat)", self.page)

    def test_play_goes_to_this_tab_and_the_host_s_own_seat_too(self):
        self.assertIn("window.location.assign(new URL('./' + LOCAL_PAGE + localGameQuery(", self.page)
        self.assertIn("var LOCAL_PAGE = 'play.html';", self.page)
        self.assertIn("window.location.assign(joinUrl(room, checked.name, fill));", self.page)
        self.assertIn("window.location.assign(joinUrl(code, checked.name, ''));", self.page)

    def test_the_old_address_and_its_state_addresses_still_work_at_the_front_page(self):
        for needle in ("params.get('room')", "params.get('map')", "params.get('players')", "validFill(params.get('fill'))", "params.get('play') === 'here'", "new URLSearchParams(window.location.search).get('aspect')"):
            self.assertIn(needle, self.page, needle)
        self.assertIn("var wantedPlayers = playersChoice(wantedPlayersText, 4);", self.page)         # (an address that names no players hosts 4, as before; players=1 plays on this computer)
        self.assertNotIn("four.html", self.page)

    def test_the_frames_note_is_shown_only_where_games_run(self):
        self.found(self.page, r'<p id="frames-note" hidden>')
        self.assertEqual(len(re.findall(r"\$\('frames-note'\)\.hidden = false;", self.page)), 3)       # the first report, a seat on the page, a seat in a window


class TheGamePage(PageCase):
    def setUp(self):
        self.page = read("web", "shell.html")

    def test_the_menu_is_the_way_back_in_the_same_tab_in_the_header_and_the_footer(self):
        header = re.search(r'<a href="/" id="menu-btn"([^>]*)>', self.page)
        self.assertIsNotNone(header)
        self.assertNotIn("target=", header.group(1))
        self.assertIn('<span class="btn-text">Menu</span><span class="btn-text-short">Menu</span>', self.page)
        footer = re.search(r'<a href="/" id="menu-link"([^>]*)>Menu</a>', self.page)
        self.assertIsNotNone(footer)
        self.assertNotIn("target=", footer.group(1))
        self.assertNotIn("four.html", self.page)
        self.assertNotIn(">Play online<", self.page)

    def test_it_asks_before_it_leaves_a_game_that_runs_or_a_room_that_is_joined(self):
        self.assertIn("window.confirm('Leave the game and go back to the menu?')", self.page)
        block = self.page[self.page.index("THE WAY BACK TO THE MENU"):]
        block = block[:block.index("// The fullscreen mouse: Locked")]
        self.assertIn("ANTS_ARGS.indexOf('--join-url') !== -1", block)
        self.assertIn("Module._ants_match_running() === 1", block)
        self.assertIn("e.preventDefault()", block)

    def test_the_catalog_link_has_one_name_on_both_pages(self):
        for name in ("shell.html", "lobby.html"):
            page = read("web", name)
            self.assertIn("Sprites and sounds", page, name)
            self.assertNotIn("Asset Viewer", page, name)
            self.assertNotIn("Asset Catalog Viewer", page, name)
            self.assertNotIn(">Assets<", page, name)

    def test_the_local_parameters_are_read_only_for_a_game_that_joins_no_server(self):
        self.assertIn("if (joined.args.indexOf('--join-url') === -1) {", self.page)
        self.assertIn("ANTS_PAGE.localArguments(window.location.search, remembered).args.forEach", self.page)
        self.assertIn("localStorage.getItem('ants.name')", self.page)
        self.assertEqual(len(re.findall(r"LOCAL_MAPS\s*=\s*\{", self.page)), 1)
        self.assertIn("localArguments: localArguments", self.page)

    def test_the_guide_no_longer_says_that_there_are_no_computer_opponents(self):
        self.assertNotIn("there are no computer opponents", self.page)
        self.assertIn("<strong>Opponents:</strong>", self.page)


class TheImageAndTheCi(PageCase):
    def test_the_dockerfile_builds_the_lobby_and_the_game_page_s_own_path(self):
        dockerfile = read("Dockerfile")
        self.assertIn("COPY --from=builder /src/lobby.html /usr/share/nginx/html/lobby.html", dockerfile)
        self.assertIn("RUN cp /usr/share/nginx/html/index.html /usr/share/nginx/html/play.html", dockerfile)
        self.assertFalse("/src/four.html" in dockerfile or "web/four.html" in dockerfile, "the Dockerfile still builds the old page")
        self.assertIn("web/shell.html web/lobby.html", dockerfile)                           # the version goes into both pages
        self.assertIn('"$PAGE" /src/web/lobby.html', dockerfile)                             # and the build id
        self.assertIn("cp /src/web/lobby.html /src/lobby.html", dockerfile)                  # the site label step runs on the lobby

    def test_the_ci_checks_the_pages_of_the_image_the_label_and_the_routes(self):
        ci = read(".github", "workflows", "ci.yml")
        self.assertNotIn("four.html page", ci)
        self.assertIn("ls -l index.html play.html lobby.html", ci)
        self.assertIn('for page in index.html play.html lobby.html; do grep -q "(staging)</title>" $page', ci)
        self.assertIn("! grep -qi staging index.html play.html lobby.html", ci)
        self.assertIn("python3 tests/scripts/web_routes_check.py --web http://127.0.0.1:18080/", ci)

    def test_the_local_build_packs_the_lobby_and_the_game_page_s_own_path(self):
        script = read("build_web.sh")
        self.assertIn('"${DIST_DIR}/play.html"', script)
        self.assertIn('"${SCRIPT_DIR}/web/lobby.html"', script)
        self.assertIn('> "${DIST_DIR}/lobby.html"', script)


class TheDocuments(unittest.TestCase):
    def test_the_readme_and_the_notes_say_where_single_player_and_online_play_are(self):
        readme = read("README.md")
        for needle in ("`/play.html?map=", "**Players 1 to 4**", "**Opponents**", "/four.html", "`--play`", "**Menu**"):
            self.assertIn(needle, readme, needle)
        notes = read("docs", "NETWORK_PORT.md")
        for needle in ("The front page", "localArguments", "$arg_join", "`--play`"):
            self.assertIn(needle, notes, needle)
        self.assertNotIn("web/four.html", readme)


@unittest.skipUnless(shutil.which("node"), "node is not installed: the front page's rules were NOT run (tests/scripts/web_lobby_check.js)")
class ThePagesOwnCode(unittest.TestCase):
    def test_the_form_s_meaning_the_whitelist_and_the_two_pages_agreeing(self):
        done = subprocess.run([shutil.which("node"), CHECK_JS, SHELL, LOBBY], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn(" 0 failures", done.stdout)


if __name__ == "__main__":
    unittest.main()
