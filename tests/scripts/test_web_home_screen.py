#!/usr/bin/env python3
"""The two screens of the front page, Home and the Room (web/home/, docs/HOME_SCREEN.md; run by ./run_tests.sh --fast and by the CI).

The front page (web/lobby.html) is one page with one WebSocket: <body data-screen="home|room"> says whether Home (a top bar over the games, the players and the chat) or the Room (today's room card under a slim
bar) shows, and going from one to the other is a class and one render of the page, never a request. Everything new is in web/home/ (home.js, home.css), whose size has a cap of its own because web/front/ is
at its cap and web/lobby.html at its line limit.

  - the page: the strips (the notices and the Rejoin button) are above both screens, Home's mount is before the Room, the stylesheet and the script are linked, the page opens on Home, a plain visit makes no
    room, the page calls Home from render(), from the hand-off to the game and from the dialog's inert;
  - the folder: exactly home.js and home.css, under their budgets (raw and gzipped), nothing of it in web/front/;
  - the style: which screen shows what, every target at least 44 px, nothing loaded from another site, the pictures are data;
  - the body of Home (the grid that the games and the players list mount into) and the phone's strip: no Yard, no stand-in, no site label read;
  - the way into the image: the Dockerfile, the local web build and the CI's list of the image's files carry the folder;
  - the code RUNS in the fake browser of tests/scripts/web_page_harness.js (node, when it is installed: tests/scripts/web_home_screen_check.js: a plain visit, My room, Home, Back and Forward, a link, a reload,
    Leave room, START while a guest sits on Home, the strips, the Menu);
  - tests/data/home_screen_mutants.json changes one thing at a time in a COPY of the files (the room left on Home, the hand-off ignored while Home is up, Back, a reload on Home, ...): the checks must then
    fail (the last class of this file; tools/mutate.py runs the same list).
The pictures that were approved are compared by hand with the page in a browser (docs/HOME_SCREEN.md); the real server and the hand-off to a real game are tests/scripts/web_lobby_server_check.js.
"""
import gzip
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HOME = os.path.join(REPO, "web", "home")
FRONT = os.path.join(REPO, "web", "front")
LOBBY = os.path.join(REPO, "web", "lobby.html")
CHECK = os.path.join(REPO, "tests", "scripts", "web_home_screen_check.js")
NODE = shutil.which("node")
FILES = ("home.js", "home.css")
LATER = ("games.js", "games.css")                    # the files that load after the first paint (tests/scripts/test_web_home_games.py has their budgets)
CHAT_FILES = ("chat.js", "chat.css")                  # the chat dock, loaded after the first paint: its own budgets and tests are tests/scripts/test_web_home_chat.py
PLAYERS_FILES = ("players.js", "players.css")         # the players list, loaded after the dock: its own budgets and tests are tests/scripts/test_web_home_players.py
TALK_FILES = ("talk.js", "talk.css")                  # the emoji and microphone buttons of the chat, loaded after the dock: its own budgets and tests are tests/scripts/test_web_home_talk.py
# (raised on purpose from 39,000 and 11,600 to 40,000 and 12,000 for the top bar's three rules: the account's name beside the badge, My room only while the page holds a room, and their comments; no comment was trimmed)
HOME_BUDGET, HOME_GZIP_BUDGET = 40 * 1000, 12000     # bytes, raw and gzipped, both files together (about 38,464 and 11,691 now, 37,247 and 11,349 before the top bar's rules; it was about 38,810 and 11,590, with the loaders of the dock and of the Games list, the timers of the hand-off, the narrow logo, the question before Leave room, the steps of the bar for narrow windows and the two compact rows of a phone held sideways; the caps went from 38,000 to 39,000 raw and from 11,300 to 11,600 gzipped because those two rules take about 2,000 bytes raw and 420 gzipped together): web/front/ has no room left, and the Games list and the chat dock load later, with caps of their own


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def run_function(page, name):
    """the text of function `name` of the page's script, up to its closing brace at the same indent"""
    at = page.index("        function %s(" % name)
    end = page.index("\n        }\n", at) + len("\n        }\n")
    return page[at:end]


class ThePage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.page = read("web", "lobby.html")

    def test_the_page_opens_on_home_and_has_the_strips_above_both_screens(self):
        page = self.page
        self.assertIn('<body data-screen="home">', page)
        strips = page.index('<div class="wrap strips">')
        home = page.index('<div id="home"></div>')
        room = page.index('<main class="wrap">')
        banner, rejoin = page.index('id="banner"'), page.index('id="rejoin"')
        self.assertLess(page.index('<div class="screen" id="screen">'), strips)
        self.assertTrue(strips < banner < rejoin < home < room, "the notices and the Rejoin button, then Home's mount, then the Room")
        self.assertEqual(page[rejoin:home].count("</div>"), 2, "the strips' own box closes before Home")
        self.assertEqual(page.count('id="home"'), 1)

    def test_the_script_and_the_style_are_linked_once_and_the_script_comes_before_the_page_s_own(self):
        page = self.page
        self.assertEqual(page.count('<link rel="stylesheet" href="home/home.css">'), 1)
        self.assertEqual(page.count('<script src="home/home.js"></script>'), 1)
        self.assertLess(page.index('<script src="home/home.js"></script>'), page.index("<script>\n"), "the page's own script finds AntsHome")
        self.assertLess(page.index('<link rel="stylesheet" href="front/game_type.css">'), page.index('<link rel="stylesheet" href="home/home.css">'), "Home's style comes after the page's others")
        self.assertIn("|| !window.AntsHome) {", page, "a page whose Home script did not come says that it could not load all of its parts, as for its other scripts")

    def test_a_plain_visit_makes_no_room(self):
        run = run_function(self.page, "runLobby")
        self.assertNotRegex(run, r"else \{\s*connect\(\{ code: newCode\(\)", "the old last branch (no seat, no link: a room of its own) is gone: My room (the page's make()) makes it")
        self.assertEqual(run.count("newCode()"), 1, "the only room that runLobby makes is the one of a test room that is ready")
        self.assertIn("Home.start(test || wantedCode, stored);", run)
        self.assertIn("connect({ code: stored.code, key: stored.key, name: stored.name || myName(), own: stored.own });", run, "a tab that holds a seat takes it back, also on Home")
        self.assertIn("make: function () { connect({ code: newCode(), name: myName(), own: true }); }", self.page, "My room makes the room, a private one with a new code")

    def test_the_page_calls_home_where_the_room_changes_the_hand_off_starts_and_a_dialog_opens(self):
        page = self.page
        self.assertIn("            Home.sync();\n        }\n        function paintCard", page, "render() ends with Home's look at the room (the count of My room)")
        self.assertIn("handing = true; Home.opening();", page, "the hand-off to the game says it on Home, in the same breath as it starts")
        self.assertIn("Home.inert(on); }", page, "the dialog of the map list makes Home's bars inert too")
        self.assertIn("window.history.replaceState(window.history.state, '', window.location.pathname.replace(/^\\/+/, '/') + (Home.screen() === 'room' ? '#room' : ''))", page, "stripping the room's code from the address keeps the screen's entry and names the screen that is up")
        self.assertEqual(page.count("AntsHome.mount("), 1)

    def test_the_hand_off_does_not_look_at_the_screen(self):
        starting = run_function(self.page, "onStarting")
        for word in ("data-screen", "Home.screen", "screen()"):
            self.assertNotIn(word, starting, "the hand-off to the game is the same on Home and on the Room (START must reach a guest on Home)")
        self.assertIn("if (handing || !client) return;", starting)

    def test_leaving_the_room_forgets_it_and_the_page_asks_for_nothing_else_of_home(self):
        mount = self.page[self.page.index("AntsHome.mount("):]
        mount = mount[:mount.index("// ---- go ----")]
        self.assertIn("leave: function () { if (handing) return; var old = client;", mount, "a page that is on its way to the game gives its seat up to nobody")
        self.assertIn("if (old) old.leave(); forgetSession(); leaveBack(); hideBanner(); render();", mount)
        self.assertEqual(sorted(set(re.findall(r"\b(info|make|leave|how|stalled):", mount))), ["how", "info", "leave", "make", "stalled"])
        self.assertIn("client: function () { return client; }, join: probe, typed: joinTyped, name: myName, handing: function () { return handing; }, cancel: function () { if (lastProbe && lastProbe !== client) lastProbe.leave(); },", mount, "what the Games list reads of the page (docs/HOME_SCREEN.md): its client, the look at a room, a typed code, the name, whether the page is on its way to a game, and the way to give a look up")

    def test_the_words_that_said_that_a_page_makes_a_room_when_it_opens_are_corrected(self):
        page = self.page
        self.assertNotIn("Open the page and your room is ready", page)
        self.assertNotIn("where a new room is ready", page)
        self.assertNotIn("Back finds a room again", page)
        self.assertNotIn("A plain visit makes a room", page)
        self.assertNotIn("ready when the page opens", read("docker", "nginx.conf"))
        self.assertNotIn("ready when the page opens", read("docs", "PLAY_IN_BROWSER.md"))

    def test_how_it_works_says_where_the_room_comes_from_because_the_room_shows_it_too(self):
        self.assertIn("<h3>The room</h3><p>From Home, press Quick host, choose Public room or Private room, and your room is ready, with the first color yours.", self.page)
        self.assertIn("A public room is listed under Games, where players join it from the list or by its code; a private one is for the people who have its link.</p>", self.page, "How it works says where the public rooms are and how to be in one")
        self.assertNotIn("<p>Press My room and your room", self.page, "the list opens from the Room too, where there is no Quick host to press")
        check = read("tests", "scripts", "web_home_check.py")
        self.assertNotIn("the front page makes a room again", check, "a plain visit of the front page makes no room: My room does")
        self.assertIn("the front page (Home), after My room, makes a room again", check)

    def test_the_coffee_picture_of_the_footer_that_is_not_shown_on_home_is_lazy(self):
        self.assertRegex(self.page, r'<img src="https://cdn\.buymeacoffee\.com/[^"]*" alt="Buy Me a Coffee" width="217" height="61" crossorigin="anonymous" loading="lazy">')


class TheFolder(unittest.TestCase):
    def test_it_holds_the_two_screens_the_games_list_the_chat_dock_and_the_players_list_and_nothing_else(self):
        self.assertEqual(sorted(n for n in os.listdir(HOME) if not n.endswith(".mutbak")), sorted(FILES + LATER + CHAT_FILES + PLAYERS_FILES + TALK_FILES))      # (tools/mutate.py keeps FILE.mutbak while it changes a file)

    def test_it_is_under_its_budgets(self):
        raw = zipped = 0
        for name in FILES:
            with open(os.path.join(HOME, name), "rb") as f:
                data = f.read()
            raw += len(data)
            zipped += len(gzip.compress(data, 9))
        self.assertLess(raw, HOME_BUDGET, "home.js and home.css are %d bytes" % raw)
        self.assertLess(zipped, HOME_GZIP_BUDGET, "and %d bytes gzipped" % zipped)

    def test_a_hand_off_that_does_not_come_is_given_up_after_15_seconds(self):
        js = read("web", "home", "home.js")
        self.assertIn("var OPENING_MS = 15000, GIVE_UP_MS = 60000;", js)
        self.assertIn("goingTimer = null; going = false; stalled = true; opening.hidden = true; leave.disabled = true; leave.setAttribute('aria-disabled', 'true'); inert(held);", js, "the card goes, Home is free, Leave room stays off (the seat may still be the game's), and what a dialog holds stays held")
        self.assertIn("try { if (page.stalled) page.stalled(words); } catch (e) { /* what the page says is its own */ }", js, "the page says that the game is slow to open, and a minute after START that it did not")
        self.assertIn("failTimer = setTimeout(function () { failTimer = null; tell(TEXT.failed); }, GIVE_UP_MS - OPENING_MS);", js, "the wording of a failure comes a minute after START, not at 15 s")
        self.assertIn("if (failTimer !== null) clearTimeout(failTimer);", js, "a second hand-off starts both times again")
        self.assertIn("tell(TEXT.slow);", js)
        self.assertIn("slow: 'The game is slow to open. Reload this page to get your seat back.', failed: 'The game did not open. Reload this page to get your seat back.',", js)
        self.assertIn("function () { if (stalled) return; var go = function () { page.leave(); show('home', 'push'); }; if (!asking(go)) go(); }", js, "a Leave room that reaches the page all the same does nothing; one that may go asks the Games list first (a room with other people in it is asked about)")
        self.assertIn("if (goingTimer !== null) clearTimeout(goingTimer);", js, "a second hand-off starts the time again")

    def test_nothing_of_it_is_in_the_folder_that_has_its_cap_already(self):
        for name in FILES + LATER + CHAT_FILES + PLAYERS_FILES + TALK_FILES + ("home",):
            self.assertNotIn(name, os.listdir(FRONT))

    def test_the_script_is_plain_es5_and_names_no_other_site(self):
        js = read("web", "home", "home.js")
        code = re.sub(r"//.*", "", js)
        for token in (r"\bconst\b", r"\blet\b", r"=>", r"`", r"\bimport\b", r"\bclass\b"):
            self.assertIsNone(re.search(token, code), token)
        self.assertEqual(sorted(set(re.findall(r"https?://[^'\" )]+", js))), ["https://discord.gg/WaWrXQnKPj", "https://github.com/dchadd427/ants-cross-platform", "https://github.com/dchadd427/ants-cross-platform/issues"], "the Menu's links that leave the site, and no other address")
        self.assertIn("'use strict';", js)


class TheStyle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.css = read("web", "home", "home.css")
        cls.rules = re.findall(r"([^{}]+)\{([^{}]*)\}", re.sub(r"/\*.*?\*/", "", cls.css, flags=re.S))

    def test_each_screen_shows_its_own_things(self):
        css = self.css
        self.assertIn('body[data-screen="home"] main.wrap, body[data-screen="home"] .bar { display: none; }', css)
        self.assertIn('body[data-screen="room"] #home { display: none; }', css)
        self.assertRegex(css, r"\.mast \{ display: none; \}")
        self.assertRegex(css, r'body\[data-screen="home"\] \{ display: flex; flex-direction: column; height: 100vh; height: 100dvh; padding-bottom: env\(safe-area-inset-bottom\); overflow: hidden; \}')
        self.assertRegex(css, r'body\[data-screen="home"\] \.screen \{ flex: 1 1 auto; min-height: 0; overflow: hidden; \}')

    def test_leave_room_is_red_like_leave_game_with_the_stamped_look(self):
        self.assertIn(".leave { color: var(--cream); background: var(--sheen) #9d0d17; border-color: var(--edge); box-shadow: var(--bevel), 3px 4px 0 #3b0d10; }", self.css, "the red face, the weave, the double frame and a darker red shadow")
        self.assertIn(".leave:hover { background-color: #b5222c; }", self.css, "a lighter red under the cream words (5.6 to 1)")
        self.assertNotRegex(self.css, r"\.leave[^{]*\{[^}]*(transform|text-shadow|#1b0f0d)", "the press, the words' shadow and forced colors are .btn's")

    def test_the_rooms_bar_gives_the_logo_what_is_left_on_a_narrow_phone(self):
        match = re.search(r"@media \(max-width: 389px\) \{(.*?)\n\}", self.css, re.S)
        self.assertIsNotNone(match, "a rule for 360 and 320 px (the bar does not fit at its size for a wider phone)")
        self.assertIn(".rbar .hbrand { flex: 1 1 0; min-width: 40px; max-width: 76px; } .rbar .hlogo { width: 100%; }", match.group(1))

    def test_the_rooms_bar_leaves_the_logo_picture_out_below_340_px_and_the_h1_keeps_its_name(self):
        match = re.search(r"@media \(max-width: 339px\) \{ \.rbar \.hbrand \{([^}]*)\} \}", self.css)
        self.assertIsNotNone(match, "a rule for 320 px (the logo is 40 px wide at 360 and 320: too small to read)")
        body = match.group(1)
        self.assertIn("position: absolute;", body)
        self.assertIn("clip: rect(0 0 0 0);", body)
        self.assertNotRegex(body, r"display: *none|visibility: *hidden", "the heading and its name (the picture's alt) stay for a screen reader")
        self.assertLess(self.css.index("max-width: 389px"), self.css.index("max-width: 339px"), "the narrower rule comes after the wider one")

    def test_the_menu_scrolls_inside_itself_when_the_window_is_short(self):
        box = next(body for selector, body in self.rules if selector.strip() == ".hmenubox")
        self.assertIn("overflow-y: auto;", box)
        self.assertIn("overscroll-behavior: contain;", box)
        self.assertRegex(box, r"max-height: calc\(100vh - \d+px\); max-height: calc\(100dvh - \d+px\);")
        self.assertIn("position: fixed;", box)

    def test_every_target_is_at_least_44_px_high_and_wide(self):
        heights = {}
        for selector, body in self.rules:
            for m in re.finditer(r"min-height: (\d+)px", body):
                heights.setdefault(selector.strip(), []).append(int(m.group(1)))
        for selector in (".hbar .btn", ".hbar .quick", ".rbar .btn", ".hmenubox a, .hmenubox button.mitem", ".hmenubox .mlink"):
            self.assertIn(selector, heights, selector)
        for selector, values in heights.items():
            for value in values:
                self.assertGreaterEqual(value, 44, "%s has a min-height of %d" % (selector, value))
        for selector in (r"\.hmenu, \.hlogin", r"\.hmenubox \.mlink"):
            widths = re.search(selector + r" \{[^}]*min-width: (\d+)px", self.css)
            self.assertGreaterEqual(int(widths.group(1)), 44, selector)
        self.assertNotRegex(self.css, r"(?<!min-)height: [1-3]\d px; *}", "no fixed height under 40 px on a control")

    def test_nothing_is_loaded_from_another_site_and_the_pictures_are_data(self):
        self.assertNotRegex(self.css, r"https?://|@import")
        for target in re.findall(r"url\(\s*[\"']?([^\"')]+)", self.css):
            self.assertTrue(target.startswith("data:image/svg+xml,") or target.startswith("front/"), target)
        self.assertNotRegex(self.css, r"url\(\s*[\"']?front/(?!clay)")

    def test_every_style_sheet_and_inline_style_closes_every_brace_it_opens(self):
        # a rule that lost its closing brace makes the browser swallow the rules after it (the sideways phone's rules of the Games list once went that way)
        def open_and_close(text):
            text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
            return text.count("{"), text.count("}")
        web = os.path.join(REPO, "web")
        seen = 0
        for folder, _, names in os.walk(web):
            for name in sorted(names):
                path = os.path.join(folder, name)
                if name.endswith(".css"):
                    blocks = [read(os.path.relpath(path, REPO))]
                elif name.endswith(".html"):
                    blocks = re.findall(r"<style[^>]*>(.*?)</style>", read(os.path.relpath(path, REPO)), flags=re.S)
                else:
                    continue
                for block in blocks:
                    seen += 1
                    opened, closed = open_and_close(block)
                    self.assertEqual(opened, closed, "%s: %d '{' and %d '}'" % (os.path.relpath(path, REPO), opened, closed))
        self.assertGreaterEqual(seen, 3)                # (the copy that the mutant runs holds only a few of them)

    def test_the_bars_have_a_layout_for_a_phone_upright_and_for_a_short_window(self):
        self.assertIn("@media (max-width: 720px) and (orientation: portrait), (max-width: 639px) {", self.css)
        self.assertIn("@media (max-height: 500px) and (orientation: landscape) {", self.css)
        for step in ("@media (min-width: 721px) and (max-width: 1076px) {", "(min-width: 721px) and (max-width: 891px), ", "@media (min-width: 721px) and (max-width: 774px), ", "@media (min-width: 360px) and (max-width: 639px) and (max-height: 500px) and (orientation: landscape) {"):
            self.assertIn(step, self.css, "the bar's steps for a window that is not wide (docs/HOME_SCREEN.md, \"The top bar\"): the real-browser check is tests/scripts/web_home_games_browser.js")
        self.assertNotRegex(self.css, r"overflow-x: *(scroll|auto)", "nothing scrolls sideways")


    def test_a_phone_upright_shows_the_visitor_s_name_beside_the_badge_and_the_guest_chip_stays_the_mark(self):
        """docs/HOME_SCREEN.md, "The top bar": the first row is the logo, then what is left of it is the chip's and the Menu's. Where the layout is in the real browser: tests/scripts/web_home_topbar_browser.js"""
        phone = self.css[self.css.index("@media (max-width: 720px) and (orientation: portrait), (max-width: 639px) {"):]
        phone = phone[:phone.index("\n}\n")]
        self.assertIn(".hbar .hlogin .gc-av:not([hidden]) ~ .gc-cn { display: block;", phone, "the name only where the badge is drawn: a guest's chip says Log in with the mark alone")
        self.assertIn(".hbar .hright { grid-area: 1 / 1 / 2 / 3; min-width: 0; margin-left: 102px;", phone, "the chip and the Menu use the row's free place, to the right of the logo (92 px and the gap)")
        self.assertRegex(phone, r"\.hbar button\.hlogin \{ flex: 0 1 auto;", "the chip shrinks, so the name is cut and the Menu stays in the row")
        side = self.css[self.css.index("@media (min-width: 360px) and (max-width: 639px) and (max-height: 500px) and (orientation: landscape) {"):]
        side = side[:side.index("\n}\n")]
        self.assertIn(".hbar .hlogin .gc-av:not([hidden]) ~ .gc-cn { display: none; }", side, "held sideways there is no free place: the badge alone")


class TheBodyOfHome(unittest.TestCase):
    """Home under its bar is the games (left), the players (right) and the chat (along the bottom); a phone (up to 700 px wide) has one of the three at a time and a strip to pick it. There is no Yard on Home yet:
    no stand-in, no site label read, nothing that prints a size. The strip is the page's own small piece in home.js; the three parts keep their own files."""

    @classmethod
    def setUpClass(cls):
        cls.js = read("web", "home", "home.js")
        cls.css = read("web", "home", "home.css")
        cls.code = re.sub(r"//.*", "", cls.js)

    def test_there_is_no_yard_and_no_label_is_read(self):
        for name in ("'yard", "Yard", "standIn", "labelled", "site-label", "YARD_ANTS", "reservedText", "ylabel", "yant"):
            self.assertFalse(name in self.code, name)
        for name in (".yard", ".standin", ".ylabel", ".yant", ".ytag", ".yres"):
            self.assertFalse(name in self.css, name)

    def test_the_body_is_one_main_that_the_list_of_games_and_the_list_of_players_mount_into(self):
        code = self.code
        self.assertIn("var main = el('main', 'hmain'); main.id = 'hmain';", code)
        self.assertIn("host.appendChild(bar); host.appendChild(main); host.appendChild(opening); host.appendChild(tabs);", code)
        self.assertEqual(code.count("dock: main, page: page"), 1, "the Games list docks in the body")
        self.assertEqual(code.count("yard: main, quickHost"), 1, "the players list mounts in the body")

    def test_the_strip_has_three_buttons_and_the_chat_is_open_only_on_its_own_view_on_a_phone(self):
        code = self.code
        self.assertIn("var TABS = [['games', 'Games'], ['players', 'Players'], ['chat', 'Chat']];", code)
        self.assertIn("var PHONE = '(max-width: 700px)';", code)
        self.assertIn("if (dock && phone()) dock.open(name === 'chat');", code)
        self.assertIn("host.setAttribute('data-tab', name);", code)
        self.assertIn("if (dock && ph !== was) dock.open(ph ? tab === 'chat' : (win.innerHeight || 0) >= 601);", code, "a window that changes from a phone's width to a wide one (or back) opens or shuts the chat for its view")
        self.assertIn("[bar, rbar, tabs, dock && dock.root, players && players.root]", code, "the strip takes no press while the page is on its way to the game")
        self.assertIn("mirror(gamesPill, tabBtns.games.pill);", code)
        self.assertIn("mirror(c, tabBtns.players.pill);", code)
        self.assertIn("to.textContent = /^\\d{3}/.test(t) ? '99+' : t;", code, "a count of three digits or more (500+ too) is 99+ in the strip, as on the old bar's badge")
        self.assertNotIn("login.addEventListener('click'", code, "the chip opens the account's dropdown (chat.js): it does not change the view of a phone")

    def test_the_layout_is_a_grid_for_a_wide_window_and_one_view_for_a_phone(self):
        css = self.css
        self.assertIn(".hmain { flex: 1 1 auto; min-height: 0; display: grid;", css)
        self.assertIn(".pl-on .hmain { grid-template-columns: minmax(0, 1fr) clamp(220px, 24vw, 280px); }", css, "the players list is a sidebar of 220 to 280 px, and only once there is a list")
        self.assertIn(".htabs { display: none; }", css)
        phone = css[css.index("@media (max-width: 700px) {\n    .hmain"):]
        phone = phone[:phone.index("\n}\n")]
        self.assertIn(".htabs { order: 9; flex: none; display: flex;", phone)
        self.assertIn('#home[data-tab="chat"] .hmain, #home:not([data-tab="chat"]) .gc, #home[data-tab="games"] .pl, #home[data-tab="players"] .gdock { display: none; }', phone)
        self.assertIn("#home:not(.pl-on) .htab.t-players { display: none; }", phone, "a page with no players list has no Players view")
        self.assertIn(".pl-on .hmain { grid-template-columns: minmax(0, 1fr); }", phone, "a phone has one column, with the list there or not")
        self.assertIn("@media (max-width: 380px) { .htabs { gap: 6px; padding-left: 8px; padding-right: 8px; } .htab { gap: 0; font-size: 15px; padding: 6px 2px; } .htab .pill { margin-left: 4px; padding: 0 5px; font-size: 13px; } }", css, "a third of a 320 px strip holds Players and \"99+\"")
        self.assertIn("min-height: 48px", phone)


class TheWayIntoTheImage(unittest.TestCase):
    def test_the_dockerfile_copies_the_folder_to_where_nginx_serves_home(self):
        self.assertIn("COPY web/home/ /usr/share/nginx/html/home/\n", read("Dockerfile"))

    def test_the_local_web_build_packs_it_too(self):
        script = read("build_web.sh")
        self.assertIn('mkdir -p "${DIST_DIR}/front" "${DIST_DIR}/home"', script)
        self.assertIn('cp -f "${SCRIPT_DIR}/web/home"/* "${DIST_DIR}/home/"', script)

    def test_the_ci_looks_for_the_script_and_the_style_in_the_image(self):
        self.assertIn(" home/home.js home/home.css ", read(".github", "workflows", "ci.yml"))


@unittest.skipUnless(NODE, "node is not installed: the page's two screens were NOT run (tests/scripts/web_home_screen_check.js)")
class TheCodeRuns(unittest.TestCase):
    def test_the_two_screens_in_the_fake_browser(self):
        done = subprocess.run([NODE, CHECK, LOBBY], capture_output=True, text=True, timeout=300, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout[-4000:] + done.stderr[-2000:])
        self.assertRegex(done.stdout, r"home screens: \d+ checks, 0 failed")


class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/home_screen_mutants.json (the list that tools/mutate.py runs too, with `--keep-going --jobs 1`) on a COPY of the files, one at a time: each must make the checks fail, and the
    copy with nothing changed must pass"""
    FILES = ["web/lobby.html", "web/front/player_name.js", "web/home/home.js", "web/home/home.css", "web/home/chat.js", "web/home/chat.css", "web/home/games.js", "web/home/games.css", "web/home/players.js", "web/home/players.css", "web/home/talk.js", "web/home/talk.css", "web/shapes.js", "tests/scripts/test_web_home_screen.py", "tests/scripts/web_home_screen_check.js", "tests/scripts/web_page_harness.js",
             "tests/scripts/web_home_check.py", "Dockerfile", "build_web.sh", ".github/workflows/ci.yml", "docker/nginx.conf", "docs/PLAY_IN_BROWSER.md"] + ["web/front/" + n for n in ("lobby_rules.js", "lobby_net.js", "lobby_chat.js", "rematch.js", "game_modes.js", "game_type.js", "map_picker.js", "map_maker_link.js")]

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "home_screen_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_home_mutants.")
        for name in cls.FILES:
            os.makedirs(os.path.dirname(os.path.join(cls.folder, *name.split("/"))), exist_ok=True)
            shutil.copy(os.path.join(REPO, *name.split("/")), os.path.join(cls.folder, *name.split("/")))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.folder, ignore_errors=True)

    def run_command(self, command):
        done = subprocess.run(command, shell=True, cwd=self.folder, capture_output=True, text=True, timeout=300)
        return done.returncode, done.stdout + done.stderr

    def test_the_copy_with_nothing_changed_passes(self):
        status, out = self.run_command(self.spec["mutants"][0]["test"])
        self.assertEqual(status, 0, out[-3000:])

    def test_every_mutant_is_seen(self):
        ids = [m["id"] for m in self.spec["mutants"]]
        self.assertEqual(len(ids), len(set(ids)), "an id twice")
        missed = []
        for m in self.spec["mutants"]:
            path = os.path.join(self.folder, *m["file"].split("/"))
            with open(path, encoding="utf-8", newline="") as f:
                text = f.read()
            self.assertEqual(text.count(m["old"]), 1, "%s: %r is not in %s exactly once" % (m["id"], m["old"][:60], m["file"]))
            try:
                with open(path, "w", encoding="utf-8", newline="") as f:
                    f.write(text.replace(m["old"], m["new"]))
                status, _ = self.run_command(m["test"])
            finally:
                with open(path, "w", encoding="utf-8", newline="") as f:
                    f.write(text)
            if status == 0:
                missed.append(m["id"])
        self.assertEqual(missed, [], "the checks did not see these changes")


if __name__ == "__main__":
    unittest.main()
