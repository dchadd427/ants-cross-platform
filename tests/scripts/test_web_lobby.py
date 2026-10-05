#!/usr/bin/env python3
"""The front page (run by ./run_tests.sh --fast and by the CI). The owner: "when you click the play online button it opens in a new tab can we just make that the default. you can play single on
the play online tab by setting to 1 player consolidate them". The Play online page (web/lobby.html, once web/four.html) is the front page at "/" and has single player in it; the game page
(web/shell.html) has a Menu button that goes back to it in the same tab and reads the local parameters of a game on this computer through a whitelist.

  - the pages' own rules are RUN (node, when it is installed): what the two cards mean (a game against the computer is a game on this computer: its address; the level of each of the three bases and
    the team that a browser remembered; a first visit has Medium; the Host card's players 2 - 4), the game page's whitelist (the six maps by key, the levels, the teams, a cleaned name: never the
    text of the address) and the two pages agreeing (tests/scripts/web_lobby_check.js);
  - what needs no browser is read from the files: the cards' markup (the buttons of the players and of each opponent's level, the Teams, START, Host the match), the header links and the footer of the front page, that nothing opens a new tab
    but the links that leave the game, the Menu link of the game page, one name for the catalog link on both pages, the Dockerfile that copies the lobby and the game page's second path, the CI's
    page check, and the documents.
The routes of nginx are run in tests/scripts/test_nginx_routes.py; the game page's look (the front page's) is read in tests/scripts/test_web_game.py; the whole flow in a real browser is
tests/scripts/web_home_check.py (opt-in).
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

    def test_the_host_card_offers_players_2_to_4_as_buttons_the_empty_seats_and_its_own_button(self):
        self.assertNotIn('<select id="players">', self.page)                                   # (the Players select of 1 - 4 is gone: 1 player is the first card, 2 - 4 the second)
        self.assertEqual(re.findall(r'<input type="radio" name="players" id="players-(\d)" value="(\d)"( checked)?>', self.page), [("2", "2", " checked"), ("3", "3", ""), ("4", "4", "")])
        fill = re.search(r'<select id="fill">(.*?)</select>', self.page, re.S).group(1)
        self.assertEqual(re.findall(r'<option value="([a-z]*)">([^<]*)</option>', fill), [("", "Leave empty"), ("easy", "Easy bots"), ("medium", "Medium bots"), ("hard", "Hard bots")])
        self.found(self.page, r'<label class="lab" for="fill">Empty seats at START</label>')
        self.found(self.page, r'<button id="host" type="button" class="btn">Host the match</button>')

    def test_the_first_card_has_a_group_of_four_buttons_for_each_base_the_teams_and_start(self):
        # one fieldset for each of the other three bases, in the order of the game's --bot seats (1 Red, 2 Blue, 3 Black), named by its legend, with the radio buttons None, Easy, Medium, Hard
        self.assertNotIn('id="opponents"', self.page)
        groups = re.findall(r'<fieldset class="lvlset"><legend>(\w+)</legend><div class="lvls"><span class="pair">(.*?)</span></div></fieldset>', self.page, re.S)
        self.assertEqual([name for name, _ in groups], ["Red", "Blue", "Black"])
        for seat, (name, body) in enumerate(groups, 1):
            buttons = re.findall(r'<input type="radio" name="opponent-%d" id="opponent-%d-(\w+)" value="(\w*)"( checked)?><label for="opponent-%d-\1">(\w+)</label>' % (seat, seat, seat), body)
            self.assertEqual([(word, value, text) for word, value, _, text in buttons], [("none", "", "None"), ("easy", "easy", "Easy"), ("medium", "medium", "Medium"), ("hard", "hard", "Hard")], name)
            self.assertEqual([word for word, _, checked, _ in buttons if checked], ["medium"], name + ": a first visit is Medium")
        # the Teams: a line that the script shows for two or more bots, and a select that the script fills (the choices depend on the bots)
        self.found(self.page, r'<div class="teamrow" id="teams-line" hidden>\s*<label class="lab" for="teams">Teams</label>\s*<div class="sel"><select id="teams"></select></div>\s*</div>')
        self.found(self.page, r'<button id="play" class="startbtn" type="button" aria-label="Start the game"></button>')       # (the original's own START! picture; the name is for a screen reader)

    def test_the_two_cards_have_one_line_of_help_each_and_the_rest_is_behind_how_it_works(self):
        self.found(self.page, r'<h2 class="banner" id="h-single">Play vs the computer</h2>')
        self.found(self.page, r'<h2 class="banner" id="h-online">Play online</h2>')
        self.assertEqual(len(re.findall(r'<p class="note[ "]', self.page)), 2)
        self.assertIn('<p class="note">Runs in this tab. Bots gather food, raid and fight back.</p>', self.page)
        self.assertIn('<p class="note after">Share the room code: the match starts when every seat is taken.</p>', self.page)
        self.found(self.page, r'<details class="how" id="how" hidden>\s*<summary><span class="btn">How it works</span></summary>')
        self.assertNotIn("setup-hint", self.page)                                              # (the long hints of the old form are in "How it works" and the room panel)

    def test_a_first_visit_is_treasure_two_players_on_the_host_card_medium_in_all_three_bases(self):
        self.assertIn("var DEFAULT_MAP_KEY = 'treasure';", self.page)
        self.assertIn("$('players-' + hostPlayers(recall('ants-four-players'), 2)).checked = true;", self.page)            # (the Host card's players: 2 - 4, an old stored 1 is 2)
        self.assertIn("function soloBots(stored) { return stored === null || stored === undefined ? 'medium' : validFill(stored); }", self.page)
        self.assertIn("var levels = soloSeats(recall('ants-solo-seats'), recall('ants-solo-bots'));", self.page)               # (the new key, else the old one: one level for all three)
        self.assertIn("fillSelect.value = validFill(recall('ants-four-fill'));", self.page)                  # (the rooms' choice is what it always was: leave empty until chosen)

    def test_the_old_key_of_the_opponents_is_read_and_never_written(self):
        self.assertEqual(len(re.findall(r"recall\('ants-solo-bots'\)", self.page)), 1)
        self.assertNotIn("remember('ants-solo-bots'", self.page)
        for needle in ("remember('ants-solo-seats', soloSeatsText(levels));", "if (solo) remember('ants-solo-teams', team);", "var soloTeamWanted = recall('ants-solo-teams');"):
            self.assertIn(needle, self.page, needle)

    def test_the_header_links_the_footer_the_name_the_aspect_choice_and_the_room_buttons_are_there(self):
        for needle in ('href="/asset_catalog/"', 'href="/changelog.html"', 'href="https://github.com/dchadd427/ants-cross-platform"', 'href="https://github.com/dchadd427/ants-cross-platform/issues"'):
            self.assertGreaterEqual(self.page.count(needle), 1, needle)
        for place in ('<nav class="links" aria-label="More about the game">', '<nav aria-label="Footer links">'):         # (the four links are in the header and in the footer)
            nav = self.page[self.page.index(place):]
            nav = nav[:nav.index("</nav>")]
            self.assertEqual(re.findall(r">(Sprites and sounds|Changelog|GitHub|Feedback)</a>", nav), ["Sprites and sounds", "Changelog", "GitHub", "Feedback"], place)
        self.assertIn('id="game-version"', self.page)
        self.assertIn('id="game-build-id"', self.page)
        self.assertEqual(len(re.findall(r'<input[^>]*id="player-name"', self.page)), 1)
        self.assertIn("var NAME_KEY = 'ants.name';", self.page)
        self.assertIn("var ASPECT_KEY = 'ants.aspect.v2';", self.page)
        self.found(self.page, r'<input type="radio" name="aspect" id="aspect-16-9" value="16:9" checked><label for="aspect-16-9">16:9</label>')      # (the picture's shape: a pair of buttons)
        self.found(self.page, r'<input type="radio" name="aspect" id="aspect-4-3" value="4:3"><label for="aspect-4-3">Classic 4:3</label>')
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
        self.found(self.page, r'<p id="frames-note" class="frames-note" hidden>')
        self.assertEqual(len(re.findall(r"\$\('frames-note'\)\.hidden = false;", self.page)), 3)       # the first report, a seat on the page, a seat in a window


class TheGamePage(PageCase):
    def setUp(self):
        self.page = read("web", "shell.html")

    def test_the_menu_is_the_way_back_in_the_same_tab_in_the_header_and_the_footer(self):
        header = re.search(r'<a href="/" id="menu-btn"([^>]*)>Menu</a>', self.page)
        self.assertIsNotNone(header)
        self.assertNotIn("target=", header.group(1))
        logo = re.search(r'<a href="/" id="logo-link"([^>]*)>\s*<img src="front/logo\.png"', self.page)                  # (the logo is the Menu's twin: the way back too)
        self.assertIsNotNone(logo)
        self.assertNotIn("target=", logo.group(1))
        footer = re.search(r'<a href="/" id="menu-link"([^>]*)>Menu</a>', self.page)
        self.assertIsNotNone(footer)
        self.assertNotIn("target=", footer.group(1))
        self.assertNotIn("four.html", self.page)
        self.assertNotIn(">Play online<", self.page)

    def test_it_asks_before_it_leaves_a_game_that_runs_or_a_room_that_is_joined(self):
        self.assertEqual(self.page.count("window.confirm('Leave the game and go back to the menu?')"), 1)           # (one question for the logo, the Menu button and the footer's link)
        block = self.page[self.page.index("THE WAY BACK TO THE MENU"):]
        block = block[:block.index("// The fullscreen mouse: Locked")]
        self.assertIn("['logo-link', 'menu-btn', 'menu-link'].forEach(function (id) {", block)
        self.assertIn("ANTS_ARGS.indexOf('--join-url') !== -1", block)
        self.assertIn("Module._ants_match_running() === 1", block)
        self.assertIn("e.preventDefault()", block)

    def test_a_yes_leaves_for_good_through_the_games_export_and_nothing_else_does(self):
        # (the review's M2: a player who leaves on purpose through the Menu button, the footer's link or the picture selector dropped nothing: the seat was held for the pause cap and the others waited;
        # a closed tab and a reload must stay held, so only a confirmed click calls it)
        block = self.page[self.page.index("THE WAY BACK TO THE MENU"):]
        block = block[:block.index("// The fullscreen mouse: Locked")]
        self.assertIn("else if ((joined || playing) && !(e.ctrlKey || e.metaKey || e.shiftKey || e.altKey)) antsLeaveMatch();", block)          # (a click that opens another tab leaves this one as it is)
        self.assertEqual(len(re.findall(r"antsLeaveMatch\(\)", self.page)), 3)                                  # the function, the Menu handler, the selector's yes
        selector = self.page[self.page.index("ANTS_SELECTOR_BEGIN"):self.page.index("// ANTS_SELECTOR_END")]
        self.assertIn("function antsLeaveMatch() {", selector)                                                    # (inside the block that node runs)
        self.assertIn("if (isReadyToPlay && typeof Module !== 'undefined' && Module._ants_leave_match) Module._ants_leave_match();", selector)
        self.assertIn("if (joined || playing) antsLeaveMatch();", selector)
        self.assertLess(selector.index("window.confirm('Leave the match to change the picture?')"), selector.index("antsLeaveMatch();                          //"))
        self.assertEqual(len(re.findall(r"Module\._ants_leave_match\(\)", self.page)), 1)                         # (the one place that calls it: nothing on unload, pagehide or visibilitychange)
        app = read("src", "ants_app", "application.cpp")
        self.assertIn('extern "C" EMSCRIPTEN_KEEPALIVE void ants_leave_match() {', app)
        self.assertIn("if (g_web_app != nullptr) g_web_app->leave_network_match();", app)
        self.assertIn("void Application::leave_network_match() {\n    if (net_) net_->leave();", app)

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
        for needle in ("`/play.html?map=", "**Play vs the computer**", "**Play online**", "**Host a match**", "**Join a match**", "**START!**", "**Opponents**", "/four.html", "`--play`", "**Menu**",
                       "`/stats`", "`web/front/`", "`tools/front_page_art/`"):
            self.assertIn(needle, readme, needle)
        notes = read("docs", "NETWORK_PORT.md")
        for needle in ("The front page", "localArguments", "$arg_join", "`--play`", "**Play vs the computer**", "**Host a match**", "`ants-four-players`", "the block `STATS`", "`web/front/`"):
            self.assertIn(needle, notes, needle)
        self.assertNotIn("web/four.html", readme)
        for stale in ("**Players 1 to 4**", "Players 1 to 4."):                            # (the form that the two cards replaced)
            self.assertNotIn(stale, readme + notes, stale)


@unittest.skipUnless(shutil.which("node"), "node is not installed: the front page's rules were NOT run (tests/scripts/web_lobby_check.js)")
class ThePagesOwnCode(unittest.TestCase):
    def test_the_form_s_meaning_the_whitelist_and_the_two_pages_agreeing(self):
        done = subprocess.run([shutil.which("node"), CHECK_JS, SHELL, LOBBY], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn(" 0 failures", done.stdout)


if __name__ == "__main__":
    unittest.main()
