#!/usr/bin/env python3
"""The front page (run by ./run_tests.sh --fast and by the CI). The front page (web/lobby.html, at "/") is a LOBBY: a room that lives on the game server (network protocol 16). A plain visit makes a room and
seats you in its first colour; the page shows the room's code and a link to send (Copy link, Share), the four colours (Black top left, Green top right, Red bottom left, Blue bottom right) where the host drags a
player onto another colour, sets a colour to Open, a bot of a level or Nobody, and presses Team 1 / Team 2, the map and START!; a link to somebody's room asks for a name first, "Have a code?" takes a typed code,
and START sends every page to the game page (web/shell.html) in the same tab with the key of its seat (a match for one is the game page's own single player). The game page has a Menu button back.

  - what needs no browser and no markup is RUN (node, when it is installed): the rules of the addresses that the page still makes (a game on this computer),
    the game page's whitelist (the six maps by key, the levels, the teams, the room's block, a cleaned name: never the text of the address), the two pages agreeing, and that everything the page reads from
    its two scripts is there (tests/scripts/web_lobby_check.js). The rules of the lobby itself (front/lobby_rules.js) are run by tests/scripts/web_lobby_rules_check.js, its client of the game server
    (front/lobby_net.js) by tests/scripts/web_lobby_net_check.js and web_lobby_client_check.js;
  - what the PAGE is is read from the file: the markup (the room, the four colours, the invitation, "Have a code?", the map and START, How it works, the name card, the footer), the wiring of the script to it (the
    ids, the attributes the cards are made with and the listeners find, which element shows the code and the link), the looks that carry a rule (START, the team buttons: 4.5 : 1 in every state), the room-code and
    create-block wiring of the links, that START and the old addresses go to this tab, that nothing opens a new tab but the links that leave the game, the Menu link of the game page, one name for the catalog link
    on both pages, the Dockerfile that copies the lobby and the game page's second path, the CI's page check, and the documents.
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
        self.markup = self.page[:self.page.index("<script")]                                             # the style and the body: what the browser has before any script runs
        self.style = re.sub(r"/\*.*?\*/", "", self.page[:self.page.index("</style>")], flags=re.S)       # (the rules, with their comments taken out)
        self.wide = self.style[:self.style.index("@media (min-width: 1041px)")]                               # (the rules of a wide page: the media queries of the narrower windows come after them)
        self.script = self.page[self.page.index('<script src="front/lobby_rules.js">'):]
        self.rules = read("web", "front", "lobby_rules.js")

    def rule(self, selector, text=None):
        """the body of the one rule `selector { ... }` of the text (the wide page's rules by default), failing when there is not exactly one"""
        found = re.findall(r"(?:^|[};])\s*" + re.escape(selector) + r"\s*\{([^}]*)\}", text if text is not None else self.wide)
        self.assertEqual(len(found), 1, "%d rules for %s" % (len(found), selector))
        return found[0]

    def test_it_is_titled_ants_1998_and_has_the_placeholders_once(self):
        self.assertIn("<title>Ants (1998)@@SITE_TITLE@@</title>", self.page)
        for placeholder in ("@@SITE_TITLE@@", "@@SITE_FOOTER@@", "@@GAME_VERSION@@", "@@BUILD_ID@@"):
            self.assertEqual(self.page.count(placeholder), 1, placeholder)

    def test_the_page_is_one_lobby_the_room_with_its_four_colours_and_the_match_with_its_map_and_start(self):
        self.assertEqual(len(re.findall(r'<section class="card', self.page)), 0)                  # the one card of the earlier front page (New match: Friend seats, Sit here, START) is gone: the page is a room
        self.assertEqual(self.markup.count('<div class="grid" id="lobby">'), 1)
        self.assertEqual(len(re.findall(r'<select ', self.page)), 1)                               # the map's (the drop-down of a colour is made by the script, for the colours that the host can set)
        for gone in ('id="map-solo"', 'id="map-host"', 'id="host"', 'name="players"', 'name="opponent-', 'host-seat-', 'id="fill"', 'id="fill-label"', 'Play vs the computer', 'Play online</h2>', 'Host the match',
                     'Opponents</span>', 'Sit here', 'seat-row-', 'id="h-match"', 'id="player-name"', 'btn_start', 'setup-hint', 'four.html'):
            self.assertTrue(gone not in self.page, "the page still has " + gone)
        self.not_found(self.page, r"(?i)\bfriends?\b", "on-screen text says player, never friend (AGENTS.md rule 7)")
        # the room: its heading and code, the invitation, the colours; the match: the map, the line about it and START
        self.assertIn('<section class="panel" aria-labelledby="room-h">\n            <div class="head"><h2 id="room-h">Your room</h2><span class="code">Room code <b id="code"></b></span></div>', self.markup)
        self.assertIn('<ul class="slots" id="slots" aria-label="The four colours"></ul>', self.markup)
        self.assertIn('<p class="slots-note" id="slots-note" role="status"></p>\n            <p class="slots-note teams-note" id="teams-note" role="status"></p>', self.markup)
        self.assertIn('<aside class="side" aria-label="The match">', self.markup)
        self.assertIn('<div class="pick"><button class="btn square" type="button" id="prev" aria-label="Previous map">&#8249;</button><select id="map" aria-label="Map"></select>'
                      '<button class="btn square" type="button" id="next" aria-label="Next map">&#8250;</button></div>', self.markup)
        self.assertIn('<p class="info" id="info" aria-live="polite"></p>', self.markup)
        self.assertIn('<button class="btn big" type="button" id="start">START!</button>', self.markup)
        self.assertEqual(self.markup.count('id="start"'), 1)
        self.assertIn('<p class="plan"><span class="plan-long" id="plan"></span><span class="plan-short" id="plan-short"></span></p>', self.markup)
        # the map: the page shows the default map until the room says another (the picture of the first paint), and has a picture for each map that the script lists
        keys = re.findall(r"\{ key: '(\w+)', name: '\w+', file: '\w+\.LVL'", self.rules)
        default = re.search(r"var DEFAULT_MAP_KEY = '(\w+)';", self.rules).group(1)
        self.assertEqual(len(keys), 6)
        self.assertIn('<img id="preview" src="front/preview_%s.png" alt="The map: %s" width="300" height="300">' % (default, default.capitalize()), self.markup)
        self.assertIn("$('preview').src = 'front/preview_' + side.mapKey + '.png';", self.script)
        for key in keys:
            self.assertTrue(os.path.isfile(os.path.join(REPO, "web", "front", "preview_%s.png" % key)), key)
        self.assertIn("Rules.MAPS.forEach(function (m) { var o = el('option', '', m.name); o.value = m.key; $('map').appendChild(o); });", self.script)       # (the list of the drop-down is the rules' list, in its order)

    def test_the_four_colours_are_built_in_reading_order_by_the_script_and_every_control_of_a_colour_is_named(self):
        script = self.script
        # the cards lie Black, Green, Red, Blue (the four hills of the maps) and are BUILT in that order from the rules' GRID, with no CSS order, so that Tab and a screen reader follow what is on screen
        self.assertIn("var GRID = Rules.GRID;", script)
        self.assertIn("GRID.forEach(function (seat) { var li = el('li'); li.setAttribute('data-i', String(seat)); lis[seat] = li; $('slots').appendChild(li); });", script)
        self.assertIn("GRID.forEach(function (seat) { paintCard(v.cards[seat], m); });", script)
        for selector, body in re.findall(r"([^{};]+)\{([^{}]*)\}", self.style):
            if ".slot" in selector:
                self.not_found(body, r"(?<![-\w])order\s*:", "a colour card is placed by the order of the markup, never by a CSS order: " + selector.strip())
        self.found(self.style, r"\.slots \{[^}]*grid-template-columns: minmax\(0, 1fr\) minmax\(0, 1fr\);")             # two columns on a wide page ...
        phone = self.style[self.style.index("@media (max-width: 720px)"):self.style.index("@media (max-width: 374px)")]
        self.found(phone, r"\.slots \{ grid-template-columns: minmax\(0, 1fr\);")                                         # ... one column on a phone, in the same order
        # the page keeps no table of the seats' names of its own (the colours' names, seats and cards come from front/lobby_rules.js: Green 0, Red 1, Blue 2, Black 3)
        self.assertNotIn("var SEATS", script)
        # what a card is made of: text and attributes, never markup (a name is whatever a person typed), each control named for a screen reader and found again by the listeners through the same attributes
        for banned in ("innerHTML", "outerHTML", "insertAdjacentHTML", "document.write"):
            self.assertNotIn(banned, self.page)
        for needle in ("li.setAttribute('role', 'group');", "li.setAttribute('aria-label', card.label);", "grip.setAttribute('aria-label', 'Move ' + Rules.nameOf(m, seat) + ' to another colour');",
                       "select.setAttribute('aria-label', c.name + ' is');", "pencil.setAttribute('aria-label', 'Change your name');", "rm.setAttribute('aria-label', 'Remove ' + card.who[0].text + ' from the room');",
                       "ask.setAttribute('aria-label', 'Remove ' + card.who[0].text + ' from the room?');", "set.setAttribute('aria-label', 'Team of ' + c.name);", "b.setAttribute('aria-pressed', String(t.pressed));",
                       "if (t.disabled) b.setAttribute('aria-disabled', 'true');", "b.setAttribute('aria-label', 'Team ' + t.side + ' for ' + c.name);"):
            self.assertIn(needle, script, needle)
        for attribute in ("data-grip", "data-mode", "data-edit", "data-remove", "data-remove-yes", "data-remove-no", "data-side", "data-seat"):
            self.assertIn("setAttribute('%s'" % attribute, script, attribute + " is made on the card")
            self.assertTrue(("[%s]" % attribute) in script or ("getAttribute('%s')" % attribute) in script, attribute + " is looked for by a listener")
        # the drop-down of a colour offers the plan's five words, the ones that the rules' planWith takes
        kinds = re.findall(r"'(\w+)'", re.search(r"var KINDS = \[(.*?)\];", self.rules).group(1))
        options = re.search(r"\[\['open', 'Open'\].*?\]\]\.forEach", script).group(0)
        self.assertEqual(re.findall(r"\['(\w+)', '[A-Za-z ]+'\]", options), kinds)
        self.assertEqual(re.findall(r"\['\w+', '([A-Za-z ]+)'\]", options), ["Open", "Easy bot", "Medium bot", "Hard bot", "Nobody"])
        self.assertIn("if (s) setMode(+s.getAttribute('data-mode'), s.value);", script)

    def test_start_is_the_pages_own_button_drawn_by_the_browser_at_a_modest_size(self):
        # v0.8.0 showed the original's 98 x 27 picture as the button's background at three times its size with image-rendering: pixelated, so its letters and edges were blocky and it was bigger than the rows
        # above it. START is text on the page's own bevel (.btn) now: the browser draws it at the screen's resolution, and its size is a minimum that the text may outgrow.
        self.not_found(self.page, r"btn_start")
        rules = re.findall(r"(?m)^\s*\.btn\.big \{([^}]*)\}", self.style)
        self.assertEqual(len(rules), 2, "one rule for a wide page and one for a phone")
        for rule in rules:
            self.not_found(rule, r"background|image-rendering|(?<![-\w])(width|height):", "the size is a minimum of the text's button, never a picture or a fixed box: " + rule)

        def px(rule, prop):
            found = re.search(r"(?<![-\w])%s: (\d+)px" % prop, rule)
            self.assertIsNotNone(found, "%s is a number of px in: %s" % (prop, rule))
            return int(found.group(1))
        wide, phone = rules
        for prop, wide_most, phone_most in (("min-height", 64, 58), ("font-size", 30, 28)):                # (one assert each: a tuple would compare its first number alone)
            self.assertLessEqual(px(wide, prop), wide_most, "wide " + prop)
            self.assertLessEqual(px(phone, prop), phone_most, "phone " + prop)
        self.assertGreaterEqual(px(phone, "min-height"), 44)                                             # (a finger's target: the base .btn is 44 px high at the least)
        self.assertGreaterEqual(px(self.rule(".btn"), "min-height"), 44)
        self.assertEqual(self.markup.count('class="btn big"'), 1)
        at = self.style.index("@media (max-width: 720px)")
        phone_block = self.style[at:self.style.index("@media", at + 1)]
        self.assertEqual(len(re.findall(r"\.btn\.big \{", phone_block)), 1, "the phone's rule is the one in the 720 px block")
        self.assertEqual(len(re.findall(r"\.btn\.big \{", self.style[:at])), 1)
        self.found(phone_block, r"\.go \{ position: sticky; bottom: 0;")                                 # (on a phone START stays in view at the bottom of the screen)

    def test_the_team_buttons_have_their_look_a_hidden_rule_and_a_place_on_every_width(self):
        # Team 1 and Team 2 are two buttons on each colour that plays, when three or four play (the script builds them: Rules.viewOf says which colours have them): the page's one `[hidden]` rule has `!important`,
        # a pressed button must look different from a loose one in more than hue, and a button that a third colour may not press is dimmed but stays a button (it says why when pressed): drawn flat with a dashed
        # edge and softer letters, never faded with `opacity` (a faded face and letters would fall under 4.5 : 1), and every look of a button keeps its letters at 4.5 : 1 on its face
        self.found(self.style, r"\[hidden\] \{ display: none !important; \}")
        self.found(self.style, r"\.teamset \{[^}]*display: flex;")
        base = self.rule(".teamset button")
        hover = self.rule(".teamset button:hover")
        pressed = self.rule('.teamset button[aria-pressed="true"]')
        dimmed = self.rule('.teamset button[aria-disabled="true"]')
        self.not_found(dimmed, r"opacity", "a dimmed button is not faded: " + dimmed)
        self.assertIn("dashed", dimmed)
        self.assertNotIn("dashed", base + pressed)
        self.found(self.style, r"(?m)^\s*:focus-visible \{ outline: 3px solid var\(--gold\); outline-offset: 3px; \}")      # (every button, link and field shows where it is on the keyboard)
        self.not_found(self.style, r"outline:\s*(none|0)\b", "no rule takes the outline away")
        tokens = dict(re.findall(r"(--[\w-]+): (#[0-9a-fA-F]{6});", self.style[:self.style.index("* { box-sizing")]))

        def colour(text):
            found = re.search(r"#[0-9a-fA-F]{6}|var\((--[\w-]+)\)", text)
            return tokens[found.group(1)] if found.group(1) else found.group(0)

        def luminance(hexed):
            def part(v):
                v = int(v, 16) / 255
                return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
            return 0.2126 * part(hexed[1:3]) + 0.7152 * part(hexed[3:5]) + 0.0722 * part(hexed[5:7])

        def ratio(a, b):
            high, low = sorted((luminance(a), luminance(b)), reverse=True)
            return (high + 0.05) / (low + 0.05)

        def letters(rule):
            return re.search(r"(?<![-\w])color: ([^;]+);", rule).group(1)

        def face(rule):
            return re.search(r"(?<![-\w])background: (var\(--[\w-]+\)|#[0-9a-fA-F]{6})", rule).group(1)

        def edge(rule):
            return re.search(r"(?<![-\w])border(?:-color)?: (?:\d+px solid )?(var\(--[\w-]+\)|#[0-9a-fA-F]{6})", rule).group(1)
        looks = {"loose": (letters(base), face(base)), "pressed": (letters(pressed), face(pressed)), "dimmed": (letters(dimmed), face(dimmed)), "hovered": (letters(hover), face(base))}
        for look, (ink, ground) in looks.items():
            self.assertGreaterEqual(ratio(colour(ink), colour(ground)), 4.5, "a %s button: %s on %s" % (look, ink, ground))
        # a pressed button is told from a loose one by more than its hue: its edge is far lighter than the loose edge (3 : 1 is the least that a state needs against what it is told from)
        self.assertGreaterEqual(ratio(colour(edge(pressed)), colour(edge(base))), 3.0, "the edge of a pressed button against the edge of a loose one")
        # a place for them on every width: 36 px high on a wide page, 40 on a phone, and a narrower phone only makes the letters and the room around them smaller
        self.assertGreaterEqual(int(re.search(r"min-height: (\d+)px", base).group(1)), 36)
        phone = self.style[self.style.index("@media (max-width: 720px)"):self.style.index("@media (max-width: 374px)")]
        self.found(phone, r"\.teamset button \{ flex: 1 1 0; max-width: 112px; min-height: 40px; \}")
        narrow = self.style[self.style.index("@media (max-width: 374px)"):]
        self.found(narrow, r"\.teamset button \{ padding: 3px 4px; font-size: 14px; \}")
        # the script puts the buttons in a group of their own, named for the colour, inside the text of the card (so they follow the name and the status in the reading order)
        self.assertIn("var set = el('div', 'teamset');", self.script)
        self.assertIn("txt.appendChild(set);", self.script)
        self.assertLess(self.script.index("txt.appendChild(set);"), self.script.index("li.appendChild(txt);"))

    def test_the_room_has_its_code_its_link_with_copy_link_and_share_and_the_link_carries_the_plain_code_only(self):
        # which element shows what: the code in two groups of three beside the heading, the link in a read-only field (a phone hides the field and Copy link takes the width), Copy link, Share where the browser has it
        self.assertIn('<input id="link" type="text" readonly value="" aria-label="Invitation link">', self.markup)
        self.assertIn('<button class="btn" type="button" id="copy">Copy link</button>', self.markup)
        self.assertIn('<button class="btn share" type="button" id="share" hidden>Share</button>', self.markup)
        self.assertIn('<span id="invite-line">Players who open this link take the next free colour.</span>', self.markup)
        script = self.script
        self.assertIn("setText($('code'), client ? Rules.codeText(client.code) : '');", script)
        self.assertIn("if ($('link').value !== roomLink()) $('link').value = roomLink();", script)
        self.assertIn(r"function roomLink() { return window.location.origin + window.location.pathname.replace(/^\/+/, '/') + '?room=' + encodeURIComponent(client ? client.code : ''); }", script)
        self.assertNotIn("roomBlockQuery", re.search(r"function roomLink\(\)[^\n]*", script).group(0))            # (the lobby's link is the room's code and nothing else: the server made the room from the Hello; a create block is for the test room's links)
        self.assertIn("copyButton($('copy'), roomLink);", script)
        self.assertIn("btn.textContent = ok ? 'Copied!' : 'Press Ctrl+C';", script)
        self.assertIn("$('link').addEventListener('focus', function () { $('link').select(); });", script)
        self.assertIn("if (typeof navigator.share === 'function') {", script)
        self.assertIn("$('share').hidden = false;", script)
        self.assertIn("navigator.share({ url: roomLink() })", script)
        self.found(self.style, r"@media \(min-width: 721px\) \{ \.share \{ display: none; \} \}")                 # (Share is a phone's button; the field and Copy link are a wide page's)
        phone = self.style[self.style.index("@media (max-width: 720px)"):self.style.index("@media (max-width: 374px)")]
        self.found(phone, r"\.invite input \{ display: none; \}")
        self.found(phone, r"\.share \{ display: inline-flex; \}")
        self.found(phone, r"\.invite #copy \{ width: 100%; \}")

    def test_have_a_code_opens_a_line_and_a_typed_code_asks_for_a_name_then_looks_at_the_room_before_it_leaves_this_one(self):
        self.assertIn('<span class="rt"><button type="button" id="havecode">Have a code?</button></span>', self.markup)
        self.assertIn('<div class="joinbox" id="joinbox" hidden><input type="text" id="joincode" placeholder="6 letters and numbers, like k7m 2xq" aria-label="Room code" aria-describedby="joinhint" '
                      'autocomplete="off" autocapitalize="off" spellcheck="false"><button class="btn sm" type="button" id="joingo">Join</button></div>', self.markup)
        self.assertIn('<p class="hint" id="joinhint" role="status"></p>', self.markup)
        self.assertLess(self.markup.index('id="joinbox"'), self.markup.index('id="joinhint"'))
        self.assertLess(self.markup.index('id="invite-line"'), self.markup.index('id="havecode"'))                  # (under the invitation, inside the room's panel)
        panel = self.markup[self.markup.index('<section class="panel" aria-labelledby="room-h">'):]
        self.assertIn('id="havecode"', panel[:panel.index("</section>")])
        script = self.script
        # the line: a press opens it or closes it, clears an old hint and puts the cursor in the field; typing clears the hint; Enter and Join do the same
        self.assertIn("$('joinbox').hidden = !$('joinbox').hidden;", script)
        self.assertIn("if (!$('joinbox').hidden) $('joincode').focus();", script)
        self.assertIn("$('joincode').addEventListener('input', function () { $('joinhint').textContent = ''; });", script)
        self.assertIn("$('joincode').addEventListener('keydown', function (e) { if (e.key === 'Enter') joinTyped(); });", script)
        self.assertIn("$('joingo').addEventListener('click', joinTyped);", script)
        # the code: blanks and capitals do not matter (Rules.typedCode), a code that is none is explained under the field, this page's own room is not left for itself, and the name card comes first
        self.assertIn("var typed = Rules.typedCode($('joincode').value), hint = $('joinhint');", script)
        self.assertIn("if (!typed.ok) { hint.textContent = typed.why; return; }", script)
        self.assertIn("if (client && typed.code === client.code) { hint.textContent = ownRoom ? Rules.HINT.own : Rules.HINT.same; return; }", script)
        self.assertIn("title: 'Join the room ' + Rules.codeText(typed.code), button: 'Join',", script)
        self.assertIn("done: function (name) { probe(typed.code, name); },", script)
        self.assertIn("back: function () { /* the page's own room stays */ }", script)
        # the look at the room: a client of its own that must find the room (join: true makes none), and this page's room is let go of only when that room has welcomed it
        probe = script[script.index("function probe(code, name) {"):]
        probe = probe[:probe.index("p.connect();")]
        self.assertIn("p = makeClient({ code: code, name: name, join: true })", probe)
        self.assertIn("join: !!p.join", script)
        self.assertLess(probe.index("p.on('welcome'"), probe.index("if (old) old.leave();"))
        self.assertLess(probe.index("client = p;"), probe.index("if (old) old.leave();"))
        self.assertIn("if (e.reason === R.Full) failed(Rules.HINT.full);", probe)
        self.assertIn("else if (e.reason === R.MatchRunning) failed(Rules.HINT.running);", probe)
        self.assertIn("else if (e.reason === R.VersionMismatch) showBanner('version');", probe)
        self.assertIn("else failed(Rules.TEXT.noroom());", probe)
        self.assertNotIn("joinUrl(", script)                                          # a typed code never makes an address of the game page
        # there is no "More ways to play": the map selection screen is the only way to play
        for gone in ("More ways to play", "morebox", "more-here", "more-windows", "testRoom", "Play every colour"):
            self.assertNotIn(gone, self.page, gone)

    def test_how_it_works_is_a_dialog_behind_a_footer_button_with_its_steps_and_the_two_sheets(self):
        self.assertIn('<button class="lnk" type="button" id="how-open">How it works</button>', self.markup)
        self.found(self.markup, r'<div class="how" id="how" hidden><div class="panel" role="dialog" aria-modal="true" aria-label="How it works">\s*<div class="head"><h2>How it works</h2><button class="btn sm" type="button" id="how-close">Close</button></div>')
        how = self.markup[self.markup.index('<div class="how" id="how" hidden>'):]
        how = how[:how.index("<div class=\"name-modal\"")]
        self.assertEqual(re.findall(r"<h3>(.*?)</h3>", how), ["The room", "The other colours", "Removing a player", "Teams", "START!", "When the host leaves"])
        for words in ("Black top left, Green top right, Red bottom left, Blue bottom right", "drags a player onto another colour", "tap a player's dots and then the colour", "Easy, Medium or Hard", "set to Nobody",
                      "the Remove button on their colour", "It asks first", "press Team 1 or Team 2 under a player", "Press a lit button again to take it off", "An open colour stays empty",
                      "the player who has been in the room longest becomes the host", "closes about a minute later"):
            self.assertIn(words, how, words)
        self.assertNotIn("Play vs the computer", how)
        for sheet in ("front/qh_quickhelp.png", "front/qh_power.png"):
            self.assertIn('<img src="%s"' % sheet, how)
            self.assertTrue(os.path.isfile(os.path.join(REPO, "web", sheet)), sheet)
        script = self.script
        self.assertIn("function openHow() { howOpener = document.activeElement; $('how').hidden = false; $('how-close').focus(); }", script)       # (the focus goes into the dialog and comes back to what opened it)
        self.assertIn("function closeHow() { $('how').hidden = true; if (howOpener && howOpener.focus) howOpener.focus(); }", script)
        self.assertIn("$('how').addEventListener('click', function (e) { if (e.target === $('how')) closeHow(); });", script)
        self.assertIn("if (!$('how').hidden) { closeHow(); return; }", script)                                      # (Escape)
        self.assertIn("$('how-open').addEventListener('click', openHow);", script)
        self.assertIn("$('how-close').addEventListener('click', closeHow);", script)

    def test_a_plain_visit_makes_a_room_on_treasure_with_a_new_code_a_reload_takes_the_seat_back_and_a_link_asks_for_a_name_first(self):
        script = self.script
        self.assertIn("var DEFAULT_MAP_KEY = Rules.DEFAULT_MAP_KEY;", script)
        self.assertIn("url: SERVER, code: p.code, map: Rules.mapByKey(Rules.DEFAULT_MAP_KEY).file, name: p.name, key: p.key || null, join: !!p.join, joinFirst: !!p.joinFirst, platform: platform()", script)      # (the Hello of a new room asks for the default map)
        run = script[script.index("function runLobby() {"):script.index("// SELECTOR_BEGIN")]
        self.assertIn("connect({ code: newCode(), name: myName(), own: true });", run)                            # a plain visit: a room of a new code
        self.assertIn("} else if (stored) {", run)
        self.assertIn("connect({ code: stored.code, key: stored.key, name: stored.name || myName(), own: stored.own });", run)       # a reload: the same room and the key of the seat
        self.assertIn("if (wantedCode && !(stored && stored.code === wantedCode)) {", run)                        # a link: the name card first, unless this tab holds that room already
        self.assertIn("title: 'Join the room ' + Rules.codeText(wantedCode), button: 'Join',", run)
        self.assertIn("done: function (name) { forgetSession(); connect({ code: wantedCode, name: name, own: false, joinFirst: true }); },", run)      # (a link only joins until the room has taken the page: a code with no room is not made)
        self.assertIn("offline = 'join';", run)                                                                   # (the page behind the card waits with no names and no map)
        self.assertIn("var wantedCode = wanted && /^[A-Za-z0-9_-]{1,32}$/.test(wanted) ? wanted.toLowerCase() : '';", run)      # (a room code is lower case: a link that came in capitals leads to the same room)
        # this tab's room and the seat's key live in the tab's session storage, a seat that a game holds in the browser's local storage; a stored room is believed only when it is shaped as the page writes it
        self.assertIn("var LOBBY_KEY = 'ants.lobby';", script)
        self.assertEqual(sorted(set(re.findall(r"window\.sessionStorage\.\w+\(LOBBY_KEY", script))), ["window.sessionStorage.getItem(LOBBY_KEY", "window.sessionStorage.removeItem(LOBBY_KEY", "window.sessionStorage.setItem(LOBBY_KEY"])
        self.assertIn("if (!v || typeof v !== 'object' || !Net.publicRoomCode(v.c) || typeof v.k !== 'string' || !/^[0-9a-f]{32}$/.test(v.k) || /^0+$/.test(v.k)) return null;", script)
        self.assertIn("window.addEventListener('pageshow', function (e) { if (e && e.persisted) window.location.reload(); });", script)       # (Back from a match: the room is the match's now, this page makes another)
        self.assertIn("forgetSession();                           // (Back from the match makes a room again: this one is the match's now)", script)
        # which page this is: the lobby, whatever the address (there is no test room to ask for)
        self.assertIn("runLobby();", script)
        for gone in ("runTestRoom", "asksMap", "asksRoom", "wantedBlock", "in-room", "room-panel"):
            self.assertNotIn(gone, script, gone)

    def test_the_keys_in_this_browser_are_the_name_the_shape_and_the_seat_entry_and_none_of_the_old_cards(self):
        for key in ("ants-match", "ants-solo-seats", "ants-solo-bots", "ants-solo-teams", "ants-four-map", "ants-four-players", "ants-four-fill", "ants-four-teams"):
            self.assertNotRegex(self.page, r"(recall|remember|getItem|setItem|removeItem)\(\s*'%s" % key, key + " is neither read nor written by the lobby")
        for gone in ("CARD_KEY", "CARD_ROOM_KEY", "hostSeatsText", "cardParse", "cardFromOld"):
            self.assertNotIn(gone, self.page, gone)
        written = set(re.findall(r"\bremember\(([A-Za-z_']+)", self.page))
        self.assertEqual(written, {"key", "NAME_KEY", "ASPECT_KEY"})                                  # (the definition, the name, the picture's shape: no other key is written to the local storage by name)
        self.assertEqual(sorted(re.findall(r"localStorage\.setItem\(([^,]+),", self.page)), ["entry.name", "key"])       # (the one definition of remember, and the entry of a seat that the game page reads: ants.rejoin.<room>.<seat>)
        self.assertIn("var NAME_KEY = 'ants.name';", self.page)
        self.assertIn("var ASPECT_KEY = 'ants.aspect.v2';", self.page)
        self.assertEqual(len(re.findall(r"localStorage\.removeItem|localStorage\.clear|sessionStorage\.clear", self.page)), 0)

    def test_the_footer_links_the_name_card_the_aspect_choice_and_the_test_room_buttons_are_there(self):
        self.assertEqual(self.page.count("<nav "), 1, "the header is the logo, the tagline and the one button of Watch matches: the other links are in the footer")
        self.assertIn('<header class="mast"><h1><img src="front/logo.png" alt="Ants!" width="581" height="218"></h1>', self.markup)
        self.assertIn('<a class="btn sm" href="/watch.html" id="watch-top"', self.markup)                  # (the one button of the header, the right of the row: test_web_replay.py)
        nav = self.page[self.page.index('<nav aria-label="Footer links">'):]
        nav = nav[:nav.index("</nav>")]
        self.assertEqual(re.findall(r">(How it works|Watch matches|Sprites and sounds|Changelog|GitHub|Feedback)</(?:a|button)>", nav),
                         ["How it works", "Watch matches", "Sprites and sounds", "Changelog", "GitHub", "Feedback"])
        for needle in ('href="/watch.html" id="watch-link"', 'href="/asset_catalog/"', 'href="/changelog.html"', 'href="https://github.com/dchadd427/ants-cross-platform"',
                       'href="https://github.com/dchadd427/ants-cross-platform/issues"'):
            self.assertIn(needle, nav, needle)
        self.assertIn('id="game-version"', self.page)
        self.assertIn('id="game-build-id"', self.page)
        self.assertIn('<p class="notice" id="footer-notice">Online matches are recorded and kept for 30 days. The recordings are public and show the players&rsquo; names.</p>', self.markup)
        self.found(self.markup, r'<p class="stats" id="stats" hidden><i class="live" id="stats-dot" aria-hidden="true"></i><span id="stats-live"></span><span id="stats-played"></span></p>')
        # the name: asked on a card (a link, a code that was typed, an old address) and changed with the pencil on the player's own colour; the field of the card takes the name rules of the game page
        self.assertEqual(len(re.findall(r'<input[^>]*id="name-step-input"', self.markup)), 1)
        self.found(self.markup, r'<div class="name-modal" id="name-step" role="dialog" aria-modal="true" aria-labelledby="name-step-title" hidden>')
        self.found(self.markup, r'<input type="text" id="name-step-input" placeholder="Player" maxlength="32" autocomplete="off" autocapitalize="off" spellcheck="false" aria-describedby="name-step-msg name-step-hint">')
        self.found(self.markup, r'<button type="button" id="name-step-go" class="btn">Join</button>')
        self.found(self.markup, r'<button type="button" class="btn sm name-step-back" id="name-step-own">Start a room of my own instead</button>')
        self.found(self.markup, r'<div class="name-step-msg" id="name-step-msg" role="alert"></div>')
        self.assertIn("runNameStep(ui, { recall: recall, remember: remember }, function (name) { o.done(name || suggestion); });", self.script)       # (an empty name is the suggestion that the field's placeholder shows)
        self.assertIn("ui.input.placeholder = suggestion;", self.script)
        self.assertEqual(len(re.findall(r"\baskName\(\{", self.script)), 2, "the name card is asked for by a typed code and a link to a room")
        self.assertIn("pencil.setAttribute('aria-label', 'Change your name');", self.script)
        self.assertIn("if (client && client.rename(checked.name)) remember(NAME_KEY, checked.name);", self.script)
        # the picture's shape: a pair of buttons of the footer, 16:9 first and checked until the page reads another
        self.found(self.markup, r'<input type="radio" name="aspect" id="aspect-16-9" value="16:9" checked><label for="aspect-16-9" title="16:9: the wide picture \(960 x 540\)">16:9</label>')
        for shape, name in (("4-3", "Classic 4:3"), ("16-10", "16:10"), ("21-9", "21:9")):
            self.found(self.markup, r'<input type="radio" name="aspect" id="aspect-%s" value="[0-9:]+"><label for="aspect-%s" title="[^"]+">%s</label>' % (shape, shape, name))
        # the test room (a second page inside this file, with the games in frames) is gone
        for ident in ("room-panel", "play-tab", "all-here", "all-windows", "new-room", "any-link", "copy-any", "seat-rows", "room-code", "room-map", "popup-hint", "fill-hint", "sync", "grid", "frames-note"):
            self.assertEqual(self.markup.count('id="%s"' % ident), 0, ident)

    def test_nothing_opens_a_new_tab_but_the_links_that_leave_the_game_and_the_explicit_windows(self):
        anchors = re.findall(r"<a [^>]*>", self.page)
        blank = [a for a in anchors if 'target="_blank"' in a]
        hrefs = sorted(set(re.search(r'href="([^"]*)"', a).group(1) for a in blank))
        self.assertEqual(hrefs, ["/asset_catalog/", "/changelog.html", "https://github.com/dchadd427/ants-cross-platform", "https://github.com/dchadd427/ants-cross-platform/issues"])
        self.assertEqual(len(re.findall(r"window\.open\(", self.page)), 0, "the front page opens no window of its own")

    def test_start_goes_to_this_tab(self):
        script = self.script
        # START of the lobby: when the room heard that it starts, this page puts the entry of its seat where the game page looks, then goes to the game page in THIS tab (the game page asks no name, the seat's key is there)
        start = script[script.index("function onStarting() {"):script.index("function shownTeam(seat) {")]
        self.assertIn("try { window.localStorage.setItem(entry.name, entry.text); }", start)
        self.assertIn("window.location.assign(new URL('./' + rejoinQuery({ room: code, seat: seat }, name, aspect), window.location.href).href);", start)
        self.assertLess(start.index("window.localStorage.setItem(entry.name, entry.text)"), start.index("window.location.assign("))
        self.assertLess(start.index("forgetSession();"), start.index("window.location.assign("))                 # (Back from the match makes another room)
        self.assertIn("c.on('starting', mine(onStarting));", script)
        # the Rejoin button of the strip at the top does the same for a match that still runs
        self.assertIn("window.location.assign(new URL('./' + rejoinQuery(rejoinOffered, remembered.ok ? remembered.name : '', aspect), window.location.href).href);", script)
        # START with nobody else in the match: the game page's own single player, on this computer, in this tab (a room of the game server needs two); the room is let go of first
        self.assertIn("if (Rules.startsAlone(model)) { startAlone(); return; }", script)
        alone = script[script.index("function startAlone() {"):script.index("function pressTeam(seat, side) {")]
        self.assertIn("window.location.assign(new URL('./' + LOCAL_PAGE + localGameQuery(key, [], name, aspect), window.location.href).href);", alone)
        self.assertLess(alone.index("client.leave();"), alone.index("window.location.assign("))
        self.assertIn("var LOCAL_PAGE = 'play.html';", script)
        self.assertIn("client.start();", script)
        # the selector of the picture's shape changes the lobby's next game at once and asks nothing (the lobby has no game open)
        block = script[script.index("// SELECTOR_BEGIN"):script.index("// SELECTOR_END")]
        self.assertIn("aspect = value;", block)
        self.assertNotIn("confirm", block)
        self.assertNotIn("location.assign", block)

    def test_only_a_room_code_and_the_shape_are_read_from_the_address(self):
        for needle in ("new URLSearchParams(window.location.search).get('room')", "new URLSearchParams(window.location.search).get('aspect')"):
            self.assertIn(needle, self.page, needle)
        for gone in ("get('map')", "get('players')", "get('fill')", "get('teams')", "get('play')", "roomBlockOf", "validFillPlan", "validRoomTeams"):      # (the old addresses of the test room mean nothing now: they open the lobby)
            self.assertNotIn(gone, self.page, gone)
        self.assertNotIn("four.html", self.page)

    def test_a_code_is_only_a_name_and_no_link_of_the_page_carries_a_create_block(self):
        # protocol 15: the code is six random characters of an alphabet without look-alikes (front/lobby_rules.js makeCode; tests/scripts/web_lobby_rules_check.js holds the alphabet). The lobby's
        # own rooms are made by the game server from the Hello, so their link is the code alone.
        script = self.script
        newcode = script[script.index("function newCode() {"):]
        newcode = newcode[:newcode.index("function platform()")]
        self.assertIn("var buf = new Uint32Array(6);", newcode)                                           # six random numbers from the browser's generator ...
        self.assertIn("(window.crypto || window.msCrypto).getRandomValues(buf);", newcode)
        self.assertIn("return Rules.makeCode(buf);", newcode)                                              # ... made into the code by the rules
        self.assertNotIn("Math.random", newcode)
        for gone in ("roomBlockQuery", "roomBlockOf", "roommap", "roomseats", "roomteams", "roomleaderstart"):                    # (the create block of the test room's links: the lobby's links are the code alone)
            self.assertNotIn(gone, script, gone)
        for gone in ("roomTeamWord", "codeTeams", "describeCode", "cardCode", "teamsInCode", "'demo-'", "demo-small", "demo-treasure", "randomCode", "cardBlock", "cardRoom"):                 # (a code names nothing, and no code is converted)
            self.assertNotIn(gone, self.page, gone)

    def test_a_code_is_shown_in_two_groups_of_three_and_typed_as_it_is_shown(self):
        script = self.script
        self.assertIn("var codeText = Rules.codeText;", script)
        self.assertIn("setText($('code'), client ? Rules.codeText(client.code) : '');", script)                 # (the room's heading: the lobby)
        self.assertIn("title: 'Join the room ' + Rules.codeText(wantedCode), button: 'Join',", script)           # (the name card of a link)
        self.assertIn("title: 'Join the room ' + Rules.codeText(typed.code), button: 'Join',", script)           # (the name card of a typed code)
        self.assertIn("var code = codeText(offer.room);", script)                                                 # (the Rejoin button's words)
        # typed: Rules.typedCode takes blanks and capitals; a link's code must be 1 - 32 letters, digits, - and _ (the older eight-character codes work), in the three places that read one
        self.assertIn("var typed = Rules.typedCode($('joincode').value), hint = $('joinhint');", script)
        self.assertEqual(script.count("/^[A-Za-z0-9_-]{1,32}$/.test(wanted)"), 1)
        self.assertIn('placeholder="6 letters and numbers, like k7m 2xq"', self.markup)
        self.assertIn('autocapitalize="off" spellcheck="false"><button class="btn sm" type="button" id="joingo">', self.markup)

    def test_every_element_that_the_script_asks_for_is_in_the_markup_once_and_the_two_scripts_are_loaded_before_it(self):
        ids = re.findall(r'\sid="([^"]+)"', self.markup)
        self.assertEqual(sorted(i for i in set(ids) if ids.count(i) > 1), [], "an id that is used twice")
        asked = set(re.findall(r"\$\('([\w-]+)'\)", self.script)) | set(re.findall(r"getElementById\('([\w-]+)'\)", self.script)) | set(re.findall(r"freshNode\('([\w-]+)'\)", self.script))
        asked |= {"aspect-%s" % shape for shape in ("4-3", "16-10", "16-9", "21-9")} | {"fit-%s" % shape for shape in ("4-3", "16-10", "16-9", "21-9")}      # (asked for through the table of shapes)
        self.assertEqual(sorted(asked - set(ids)), [], "the script asks for an element that is not in the page")
        for ref in re.findall(r'(?:\sfor|aria-labelledby|aria-describedby)="([^"]+)"', self.markup):
            for target in ref.split():
                self.assertIn(target, ids, "an id that a label or a description names is not in the page: " + target)
        self.assertIn('<script src="front/lobby_rules.js"></script>\n<script src="front/lobby_net.js"></script>\n<script>', self.page)         # (the page's own script comes last and reads their globals)
        for name in ("lobby_rules.js", "lobby_net.js"):
            self.assertTrue(os.path.isfile(os.path.join(REPO, "web", "front", name)), name)
        self.assertIn("root.AntsLobbyRules = api", self.rules)
        self.assertIn("root.AntsLobbyNet = api", read("web", "front", "lobby_net.js"))
        self.assertIn("var Rules = window.AntsLobbyRules;\n        var Net = window.AntsLobbyNet;\n        if (!Rules || !Net) {", self.script)
        self.assertIn("'This page could not load all of its parts. Reload it to try again.'", self.script)     # (a script that did not come: the page says so and does nothing else)


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
        # (the review's M2: a player who leaves on purpose through the Menu button or the footer's link dropped nothing: the seat was held for the pause cap and the others waited;
        # a closed tab and a reload must stay held, so only a confirmed click calls it. The picture selector used to be a third place: it changes the picture inside the running game now,
        # asks nothing and never leaves, so the seat and the room stay)
        block = self.page[self.page.index("THE WAY BACK TO THE MENU"):]
        block = block[:block.index("// The fullscreen mouse: Locked")]
        self.assertIn("else if ((joined || playing) && !(e.ctrlKey || e.metaKey || e.shiftKey || e.altKey)) antsLeaveMatch();", block)          # (a click that opens another tab leaves this one as it is)
        self.assertEqual(len(re.findall(r"antsLeaveMatch\(\)", self.page)), 2)                                  # the function, the Menu handler: nothing else leaves
        selector = self.page[self.page.index("ANTS_SELECTOR_BEGIN"):self.page.index("// ANTS_SELECTOR_END")]
        self.assertIn("function antsLeaveMatch() {", selector)                                                    # (inside the block that node runs)
        self.assertIn("if (isReadyToPlay && typeof Module !== 'undefined' && Module._ants_leave_match) Module._ants_leave_match();", selector)
        # (the picture selector changes the picture in place: it neither asks nor leaves nor reloads, in a joined match or any other)
        switch = selector[selector.index("function antsSetAspect(value) {"):]
        self.assertNotIn("antsLeaveMatch", switch)
        self.assertNotIn("confirm", switch)
        self.assertNotIn("location.assign", switch)
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

    def test_the_rooms_create_block_and_the_platform_reach_the_game_through_their_own_whitelists_in_one_order(self):
        # protocol 15: the address's &roommap= &roomseats= &roomteams= &roomleaderstart= &platform= are the game's --room-map, --room-seats, --room-teams, --room-leader-start and --platform, each read through
        # its own test (tests/scripts/web_fill_check.js runs them), only with a valid join, and in this order (tests/scripts/web_aspect_key_check.js runs the whole of it on a table of addresses)
        fill = self.page[self.page.index("ANTS_FILL_BEGIN"):self.page.index("// ANTS_FILL_END")]
        for name in ("antsRoomMapArg", "antsRoomSeatsArg", "antsRoomLeaderStartArg", "antsPlatformArg", "antsTeamsArg"):
            self.assertEqual(len(re.findall(r"function %s\(value\) \{" % name, fill)), 1, name)                 # (inside the block that node runs)
        self.assertIn("/^(tiny|small|medium|gauntlet|treasure|islands)$/.test(word)", fill)
        self.assertIn("/^[2-4]$/.test(value)", fill)
        self.assertIn("return value === '1';", fill)
        self.assertIn("/^(browser-)?(windows|macos|linux|android|ios|other)$/.test(word)", fill)
        join = self.page[self.page.index("function joinArguments(search, secure, host) {"):]
        join = join[:join.index("return out;", join.index("if (join && "))]                                     # (the first "return out;" is the one of an address that cannot be read)
        self.assertEqual(re.findall(r"out\.args\.push\('(--[a-z-]+)'", join),
                         ["--join-url", "--room", "--room-map", "--room-seats", "--room-teams", "--room-leader-start", "--platform", "--seat", "--fill-bots", "--teams", "--start-when", "--name"])
        self.assertIn("var roomTeams = antsTeamsArg(q.get('roomteams'));", join)
        self.assertIn("var platform = antsPlatformArg(q.get('platform'));", join)
        self.assertNotIn("navigator", join)                                                                    # (the page does not look for the platform: it is the address's word or none)
        for name in ("--room-map", "--room-seats", "--room-teams", "--room-leader-start", "--platform"):
            self.assertGreater(join.index("'%s'" % name), join.index("if (join && "), name + " only after a valid join is checked")

    def test_the_name_step_names_the_room_by_its_code_in_two_groups_of_four_as_the_front_page_does(self):
        # (the same function on both pages: tests/scripts/web_lobby_check.js runs the game page's and the rules script's (front/lobby_rules.js) on a table of texts and they must agree)
        self.assertIn("function codeText(code) {", self.page[self.page.index("ANTS_PAGE_BEGIN"):self.page.index("// ANTS_PAGE_END")])
        self.assertIn("return text.length === 6 ? text.slice(0, 3) + ' ' + text.slice(3) : text;", self.page)
        self.assertIn("asksForName: asksForName, codeText: codeText, withoutName: withoutName", self.page)
        self.assertIn("document.getElementById('name-step-title').textContent = 'Join the match ' + ANTS_PAGE.codeText(room);", self.page)

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
        self.assertIn('for page in index.html play.html lobby.html watch.html; do grep -q "(staging)</title>" $page', ci)
        self.assertIn("! grep -qi staging index.html play.html lobby.html", ci)
        self.assertIn("python3 tests/scripts/web_routes_check.py --web http://127.0.0.1:18080/", ci)

    def test_the_local_build_packs_the_lobby_and_the_game_page_s_own_path(self):
        script = read("build_web.sh")
        self.assertIn('"${DIST_DIR}/play.html"', script)
        self.assertIn('"${SCRIPT_DIR}/web/lobby.html"', script)
        self.assertIn('> "${DIST_DIR}/lobby.html"', script)


class TheDocuments(unittest.TestCase):
    def test_the_browser_page_and_the_notes_say_where_the_lobby_is_and_what_it_does(self):
        page = read("docs", "PLAY_IN_BROWSER.md")
        for needle in ("`/play.html?map=", "## The front page: a room that is ready when the page opens", "### Your room", "### START!", "### The notices", "### The old addresses", "**Host**", "**You**", "**Open**", "**Nobody**", "**Teams**", "**Team 1**", "**Team 2**",
                       "**Remove**", "**Keep**", "**Copy link**", "**Share**", "**START!**", "**Have a code?**", "**Rejoin it**", "**Start a room of my own instead**", "/four.html", "`--start-when`", "**Menu**",
                       "`/stats`", "`web/front/`", "`tools/front_page_art/`", "`ants.lobby`", "`k7m 2xq`"):
            self.assertIn(needle, page, needle)
        notes = read("docs", "NETWORK_PORT.md")
        for needle in ("The front page", "localArguments", "$arg_join", "`--play`", "**The lobby**", "**START!** (`StartRequest`)", "`ants.lobby`", "**The leader's game starts the match**", "`antsStartArg`", "N5.83 - N5.85", "the block `STATS`", "`web/front/`"):
            self.assertIn(needle, notes, needle)
        readme = read("README.md")
        self.assertNotIn("**More ways to play**", page)                                     # (the button and the test room are gone: the map selection screen is the only way to play)
        self.assertNotIn("### The old test room", page)
        self.assertNotIn("web/four.html", readme + page)
        for stale in ("**Players 1 to 4**", "Players 1 to 4.", "**Play vs the computer**", "**Host a match**", "**Join a match**", "**Opponents**", "two cards", "Host card",                       # (the pages that the earlier card replaced)
                      "one card for every game", "**Sit here**", "**Friend**", "**The card", "New match**", "Invitations**", "`ants-match`"):                                                              # (the earlier front page's card: the lobby replaced it)
            self.assertNotIn(stale, readme + page, stale)
        for stale in ("**Players 1 to 4**", "Players 1 to 4.", "**Play vs the computer**", "**Host a match**", "**Join a match**", "**Opponents**", "two cards", "Host card"):
            self.assertNotIn(stale, notes, stale)

    def test_the_browser_page_says_what_a_code_is_and_what_the_create_block_and_its_parameters_are(self):
        # protocol 15: a code is only a name (six characters, shown in two groups of three) and the room's choices are the create block that every link carries
        page = read("docs", "PLAY_IN_BROWSER.md")
        for needle in ("### The room's code and its create block", "(#the-rooms-code-and-its-create-block)", "is **only a name**", "`abcdefghjkmnpqrstuvwxyz23456789`", "`k7m 2xq`", "`k7m2xq`",
                       "There is no such room on this server.", "a code that was made some other way", "A typed or pasted code ignores blanks and capital letters",
                       "`&roomteams=A%2BB`", "`&roomleaderstart=1`", "`&platform=`", "`0%2B1`"):
            self.assertIn(needle, page, needle)
        table = page[page.index("| Parameter | Value | The game's argument |"):]
        table = table[:table.index("\n\n")]
        rows = [[cell.strip() for cell in line.strip("|").split("|")] for line in table.splitlines()[2:]]
        self.assertEqual([(row[0], row[2]) for row in rows], [("`roommap`", "`--room-map <key>`"), ("`roomseats`", "`--room-seats <n>`"), ("`roomteams`", "`--room-teams <A+B>`"),
                                                              ("`roomleaderstart`", "`--room-leader-start`"), ("`platform`", "`--platform <word>`")])
        self.assertIn("`--join-url`, `--room`, `--room-map`, `--room-seats`, `--room-teams`, `--room-leader-start`, `--platform`, `--seat`, `--fill-bots`, `--teams`, `--start-when`, `--name`, then the page's own `--aspect`", page)
        for stale in ("`demo-<map>-4p-", "a word of the room's code", "demo-treasure-4p-", "is a word of the code itself", "a word of the code itself"):                 # (the old codes named the map, the seats and the teams)
            self.assertNotIn(stale, page, stale)


@unittest.skipUnless(shutil.which("node"), "node is not installed: the front page's rules were NOT run (tests/scripts/web_lobby_check.js)")
class ThePagesOwnCode(unittest.TestCase):
    def test_the_form_s_meaning_the_whitelist_and_the_two_pages_agreeing(self):
        done = subprocess.run([shutil.which("node"), CHECK_JS, SHELL, LOBBY], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn(" 0 failures", done.stdout)


if __name__ == "__main__":
    unittest.main()
