#!/usr/bin/env python3
"""The front page (run by ./run_tests.sh --fast and by the CI). The owner, looking at the front page on a phone: "I don't see a way to change colors or send an invite to another person should all be
right there. We don't need separate AI and online. Only do online." The page (web/lobby.html, once web/four.html) is the front page at "/" and has ONE card, "New match", for every game: four seats
(exactly one is You, every other is a Friend, a bot of a level or Nobody), the teams, an invitation for each Friend and START!, which takes this tab into a match on the game server; the game page
(web/shell.html) has a Menu button that goes back to it in the same tab and passes the card's &seat=, &fill= and &start= to the game.

  - the pages' own rules are RUN (node, when it is installed): what the card means (who plays, the plan, the room's code and the addresses of START and of an invitation, what is remembered and what a
    first visit takes from the earlier pages' keys), the game page's whitelist (the six maps by key, the levels, the teams, a cleaned name: never the text of the address) and the two pages agreeing
    (tests/scripts/web_lobby_check.js);
  - what needs no browser is read from the files: the card's markup (the four rows with their buttons, the Teams, the invitations, START, "Have a code?"), the header links and the footer of the front
    page, that nothing opens a new tab but the links that leave the game, the Menu link of the game page, one name for the catalog link on both pages, the Dockerfile that copies the lobby and the game
    page's second path, the CI's page check, and the documents.
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

    def test_the_card_has_four_seats_with_a_group_of_five_buttons_and_sit_here_the_teams_the_invitations_and_start(self):
        self.assertEqual(len(re.findall(r'<section class="card', self.page)), 1)                  # one card: the two of the earlier pages (play against the computer, play online) are gone
        self.assertEqual(len(re.findall(r'<select ', self.page)), 2)                               # the map and the teams
        for gone in ('id="map-solo"', 'id="map-host"', 'id="host"', 'name="players"', 'name="opponent-', 'host-seat-', 'id="fill"', 'id="fill-label"', 'Play vs the computer', 'Play online</h2>', 'Host the match', 'Opponents</span>'):
            self.assertTrue(gone not in self.page, "the page still has " + gone)
        self.found(self.page, r'<h2 class="banner" id="h-match">New match</h2>')
        self.found(self.page, r'<div class="sel"><select id="map-pick"></select></div>')
        # four seats in the order of the room's (0 Green, 1 Red, 2 Blue, 3 Black), each with its ant, its colour, the marker and the name of You (hidden until the script says), Sit here and one group of five buttons
        seats = self.page[self.page.index('<ul class="roster seats4" id="seats"'):]
        seats = seats[:seats.index("</ul>")]
        rows = re.findall(r'<li id="seat-row-(\d)" data-seat="(\d)"><img src="front/ant_(\w+)\.png" alt="" width="23" height="40">\s*<span class="nm"><span class="colour">(\w+)</span><span class="you" id="seat-you-(\d)" hidden> &middot; You</span><small class="seat-name" id="seat-name-(\d)" hidden></small></span>\s*'
                          r'<fieldset class="seatset" id="seat-set-(\d)"><legend class="sr">(\w+)</legend><span class="pair">(.*?)</span></fieldset>\s*<button type="button" class="btn sm sit" id="sit-(\d)" aria-label="Sit here as (\w+)">Sit here</button></li>', seats, re.S)
        self.assertEqual([(r[0], r[2], r[3]) for r in rows], [("0", "green", "Green"), ("1", "red", "Red"), ("2", "blue", "Blue"), ("3", "black", "Black")])
        for n, same, _, name, you, nm, fieldset, legend, body, sit, sit_name in rows:
            self.assertEqual({n, same, you, nm, sit, fieldset}, {n}, name)                              # (every id of a row is the row's own seat; the buttons come before Sit here, as they stand on a wide page: Tab goes the way the eye does)
            self.assertEqual({name, sit_name, legend}, {name}, name)
            buttons = re.findall(r'<input type="radio" name="seat-%s" id="seat-%s-(\w+)" value="(\w+)"( checked)?><label for="seat-%s-\1">(\w+)</label>' % (n, n, n), body)
            self.assertEqual([(word, value, text) for word, value, _, text in buttons], [("friend", "friend", "Friend"), ("easy", "easy", "Easy"), ("medium", "medium", "Medium"), ("hard", "hard", "Hard"), ("nobody", "nobody", "Nobody")], name)
            self.assertEqual([word for word, _, checked, _ in buttons if checked], ["medium"], name + ": the markup starts as a first visit does, with a Medium bot (the script shows the seat of You instead)")
        # what the five words mean, said once under the rows
        self.found(self.page, r'<p class="hint" id="seats-hint">Friend: a seat for a person you invite\. Easy, Medium, Hard: a bot of that level\. Nobody: the seat stays out\.</p>')
        # the Teams: a line that the script shows when there is a choice, and a select that it fills (the choices depend on the seats that play)
        self.found(self.page, r'<div class="teamrow" id="teams-line" hidden>\s*<label class="lab" for="teams">Teams</label>\s*<div class="sel"><select id="teams"></select></div>\s*</div>')
        self.found(self.page, r'\.roster li\[hidden\] \{ display: none; \}')                          # (a row is a grid: hidden still hides it)
        # the invitations: a box that the script shows for the Friend seats and fills with a row for each (no markup is made of text), a note about changed links
        self.found(self.page, r'<div class="invites" id="invites" hidden>\s*<span class="lab" id="invites-label">Invite your friends: a link for each seat</span>\s*<div id="invite-list" role="group" aria-labelledby="invites-label"></div>\s*<p class="hint" id="links-note" role="status" hidden>')
        # START: the page's own teal button (its text names it for a screen reader; off with fewer than two players), the line under it
        self.found(self.page, r'<button id="play" class="btn startbtn" type="button" aria-describedby="start-note">START!</button>')
        self.found(self.page, r'<p class="note" id="start-note" aria-live="polite"></p>')
        self.found(self.page, r'\.startbtn:disabled \{[^}]*cursor: not-allowed')
        self.found(self.page, r'\.startbtn:disabled:hover \{ background-color: var\(--teal\); \}')           # (a button that is off does not answer the pointer)

    def test_start_is_the_pages_own_button_drawn_by_the_browser_at_a_modest_size(self):
        # v0.8.0 showed the original's 98 x 27 picture as the button's background at three times its size (294 x 81 pixels, 196 x 54 on a phone) with image-rendering: pixelated, so its letters and edges were
        # blocky and it was bigger than the rows above it. It is text on the page's own bevel (.btn) now: the browser draws it at the screen's resolution, and its size is a minimum that the text may outgrow.
        self.not_found(self.page, r'btn_start')
        rules = re.findall(r'(?m)^\s*\.startbtn \{([^}]*)\}', self.page)
        self.assertEqual(len(rules), 2, "one rule for a wide page and one for a phone")
        for rule in rules:
            self.not_found(rule, r'background|image-rendering|(?<![-\w])(width|height):', "the size is a minimum of the text's button, never a picture or a fixed box: " + rule)

        def px(rule, prop):
            found = re.search(r"(?<![-\w])%s: (\d+)px" % prop, rule)
            self.assertIsNotNone(found, "%s is a number of px in: %s" % (prop, rule))
            return int(found.group(1))
        wide, phone = rules
        for prop, wide_most, phone_most in (("min-width", 240, 200), ("min-height", 64, 58), ("font-size", 30, 28)):         # (one assert each: a tuple would compare its first number alone)
            self.assertLessEqual(px(wide, prop), wide_most, "wide " + prop)
            self.assertLessEqual(px(phone, prop), phone_most, "phone " + prop)
        self.assertGreaterEqual(px(phone, "min-height"), 44)                                             # (a finger's target: the base .btn is 44 px high at the least)
        self.assertEqual(self.page.count('class="btn startbtn"'), 1)
        at = self.page.index("@media (max-width: 700px)")
        phone_block = self.page[at:self.page.index("@media", at + 1)]
        self.assertEqual(len(re.findall(r"\.startbtn \{", phone_block)), 1, "the phone's rule is the one in the 700 px block")

    def test_the_card_has_a_line_to_join_a_match_that_somebody_else_made_with_the_old_ids(self):
        self.found(self.page, r'<div class="havecode">\s*<label class="lab" for="join-code">Have a code\?</label>\s*<input type="text" id="join-code" placeholder="demo-small-2p-x7k2" maxlength="32"[^>]*>\s*<button id="join-go" type="button" class="btn">Join</button>\s*<p class="hint" id="join-hint"></p>\s*</div>')
        self.assertLess(self.page.index('id="start-note"'), self.page.index('class="havecode"'))          # under the card's match, inside the card
        card = self.page[self.page.index('<section class="card match"'):]
        self.assertIn('class="havecode"', card[:card.index("</section>")])

    def test_the_card_has_one_line_of_help_and_the_rest_is_behind_how_it_works(self):
        self.assertEqual(len(re.findall(r'<p class="note[ "]', self.page)), 1)                      # the line under START (the others are hints: the seats', the invitations', the code's)
        self.found(self.page, r'<details class="how" id="how" hidden>\s*<summary><span class="btn">How it works</span></summary>')
        steps = self.page[self.page.index('<div class="steps">'):]
        steps = steps[:steps.index("</div>")]
        self.assertEqual(re.findall(r"<h4>(.*?)</h4>", steps), ["New match", "Have a code?", "On the map"])
        for word in ("<b>You</b>", "<b>Sit here</b>", "<b>Friend</b>", "<b>Easy</b>", "<b>Nobody</b>", "<b>Teams</b>", "<b>START!</b>", "<b>Invite your friends</b>", "<b>Copy link</b>", "<b>Share</b>", "<b>Join</b>"):
            self.assertIn(word, steps, word)
        self.assertNotIn("Play vs the computer", steps)
        self.assertNotIn("setup-hint", self.page)                                              # (the long hints of the old form are in "How it works" and the room panel)

    def test_a_first_visit_is_treasure_you_at_green_a_medium_bot_in_the_other_seats_and_the_choices_are_the_cards_own(self):
        self.assertIn("var DEFAULT_MAP_KEY = 'treasure';", self.page)
        self.assertIn("function cardNew(mapKey) { return cardFix({ map: mapKey, you: 0, seats: ['medium', 'medium', 'medium', 'medium'], teams: 'ffa' }); }", self.page)
        self.assertIn("var CARD_KEY = 'ants-match';", self.page)
        self.assertIn("var card = cardParse(recall(CARD_KEY)) || cardFromOld({", self.page)                  # (a state of the card's own wins; the earlier pages' keys are for a first visit)
        self.assertIn("remember(CARD_KEY, cardText(card));", self.page)
        self.assertEqual(len(re.findall(r"\bremember\(CARD_KEY", self.page)), 1)                         # (the one place that writes it: a choice was made)

    def test_the_old_keys_are_read_for_a_first_visit_and_never_written(self):
        for key in ("ants-solo-seats", "ants-solo-bots", "ants-solo-teams", "ants-four-map", "ants-four-players", "ants-four-fill", "ants-four-teams"):
            self.assertEqual(len(re.findall(r"recall\('%s'\)" % key, self.page)), 1, key)             # read once, for the first visit
        self.assertNotRegex(self.page, r"remember\('ants-(solo|four)")                                  # ... and written by nothing: the card has its own key
        self.assertNotIn("hostSeatsText", self.page)
        written = set(re.findall(r"\bremember\(([A-Za-z_']+)", self.page))
        self.assertEqual(written, {"key", "NAME_KEY", "ASPECT_KEY", "CARD_KEY"})                           # (the definition, the name, the picture's shape and the card: no other key is written)

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

    def test_start_goes_to_this_tab_and_so_do_the_old_addresses_and_the_room_panel(self):
        self.assertIn("window.location.assign(new URL('./' + query, window.location.href).href);", self.page)           # START of the card: the game page with the room, in this tab
        self.assertIn("var query = cardQuery(card, code, card.you, youName(), aspect);", self.page)
        self.assertIn("window.location.assign(new URL('./' + LOCAL_PAGE + localGameQuery(", self.page)                  # an old address that asks for a game on this computer
        self.assertIn("var LOCAL_PAGE = 'play.html';", self.page)
        self.assertIn("window.location.assign(joinUrl(room, checked.name, fill, teamsInCode ? '' : roomTeams));", self.page)       # (the teams that the room's code names travel in the code)
        self.assertIn("window.location.assign(joinUrl(code, checked.name, '', ''));", self.page)

    def test_the_old_address_and_its_state_addresses_still_work_at_the_front_page(self):
        for needle in ("params.get('room')", "params.get('map')", "params.get('players')", "validFillPlan(params.get('fill'))", "validRoomTeams(params.get('teams'))", "params.get('play') === 'here'", "new URLSearchParams(window.location.search).get('aspect')"):
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
    def test_the_browser_page_and_the_notes_say_where_the_one_card_is_and_what_it_does(self):
        page = read("docs", "PLAY_IN_BROWSER.md")
        for needle in ("`/play.html?map=", "**one card for every game, New match**", "**You**", "**Sit here**", "**Friend**", "**Nobody**", "**Teams**", "**Copy link**", "**Share**", "**START!**", "**Have a code?**", "/four.html", "`--start-when`", "**Menu**",
                       "`/stats`", "`web/front/`", "`tools/front_page_art/`"):
            self.assertIn(needle, page, needle)
        notes = read("docs", "NETWORK_PORT.md")
        for needle in ("The front page", "localArguments", "$arg_join", "`--play`", "**The card**", "**Invitations**", "**The leader's game starts the match**", "`ants-match`", "`antsStartArg`", "N5.83 - N5.85", "the block `STATS`", "`web/front/`"):
            self.assertIn(needle, notes, needle)
        readme = read("README.md")
        self.assertNotIn("web/four.html", readme + page)
        for stale in ("**Players 1 to 4**", "Players 1 to 4.", "**Play vs the computer**", "**Host a match**", "**Join a match**", "**Opponents**", "two cards", "Host card"):         # (the pages that the card replaced)
            self.assertNotIn(stale, readme + page + notes, stale)


@unittest.skipUnless(shutil.which("node"), "node is not installed: the front page's rules were NOT run (tests/scripts/web_lobby_check.js)")
class ThePagesOwnCode(unittest.TestCase):
    def test_the_form_s_meaning_the_whitelist_and_the_two_pages_agreeing(self):
        done = subprocess.run([shutil.which("node"), CHECK_JS, SHELL, LOBBY], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn(" 0 failures", done.stdout)


if __name__ == "__main__":
    unittest.main()
