#!/usr/bin/env python3
"""The public Room and the Games list of the front page (web/home/games.js and games.css, docs/HOME_SCREEN.md, docs/PUBLIC_ROOMS.md; run by ./run_tests.sh --fast and by the CI).

The front page shows the Home bar's **Games** (a popover with the list that `GET /lobbies` gives, Join, Join by code), **Quick host**, and, in the Room's card, the host's Public | Private switch. All of it is made by a
script and a style that the page loads after its first paint, in the folder of the two screens (web/home/), with a budget of their own. Text that comes from the list is somebody else's: it is checked field by field and
drawn as text only.

  - the page: what the script reads of web/lobby.html is there (the heading, the colors, the strips, the Rejoin button), the page hands it its small bridge (the client, the look at a room, a typed code, the name), the
    page's probe leaves the old room only when the new one welcomes the page, and the page is still under its line limit;
  - the folder: games.js and games.css, under their budgets (raw and gzipped), plain ES5, no markup written anywhere (no innerHTML and no neighbors of it), no address of another site, nothing kept in the browser, one
    address that is asked for (/lobbies, no query, no cookies);
  - the style: every target at least 44 px high and wide, a layout for a phone, a short window and a wide one, nothing loaded from another site, nothing that scrolls sideways;
  - the way into the image: the Dockerfile, the local web build and the CI's list of the image's files carry the two files;
  - the code RUNS in the fake browser of tests/scripts/web_page_harness.js (node, when it is installed: tests/scripts/web_home_games_check.js, the list and its looks; web_home_games_room_check.js, the switch, Quick host
    and Join; web_home_games_quickjoin_check.js, Quick join and the question before Leave room), and against a real server in tests/scripts/web_lobby_server_check.js; the real browser is the opt-in tests/scripts/web_home_games_browser.js;
  - tests/data/home_games_mutants.json changes one thing at a time in a COPY of the files (a guest that can flip the switch, a second room made by Quick host, a list drawn as markup, a room left before the new one
    welcomes the page, a look while the tab is hidden, no backoff, a repeat from a hidden tab, a state that was only assumed, the page's own room in the list, no question with others in the room, a count that is not
    capped, a list that is off and throws, ...): the checks must then fail (the last class of this file; tools/mutate.py runs the same list).
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
CHECKS = [os.path.join(REPO, "tests", "scripts", n) for n in ("web_home_games_check.js", "web_home_games_room_check.js", "web_home_games_quickjoin_check.js")]
NODE = shutil.which("node")
FILES = ("games.js", "games.css")
GAMES_BUDGET, GAMES_GZIP_BUDGET = 71 * 1000, 20000      # bytes, raw and gzipped, both files together (about 69,950 and 19,980, with Quick join, its cards and the question before Leave room): they load after the first paint, and the list of the page's own files is another budget (web/home/home.js, home.css)
LINES = 1500                                           # tools/check_hygiene.py: the page's line limit


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def code_of(js):
    """the script without its comments (a line comment, a block comment) and without the text of its strings, so that a word in a comment or in a sentence is not a use of it"""
    js = re.sub(r"/\*.*?\*/", "", js, flags=re.S)
    js = re.sub(r"(?m)^\s*//.*$", "", js)
    js = re.sub(r"\s//\s.*$", "", js, flags=re.M)
    return re.sub(r"'(?:\\.|[^'\\\n])*'", "''", js)


class ThePage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.page = read("web", "lobby.html")
        cls.home = read("web", "home", "home.js")

    def test_what_the_script_reads_of_the_page_is_there_once(self):
        js = read("web", "home", "games.js")
        for ident in ("banner", "rejoin", "rejoin-go", "room-h", "slots"):
            self.assertIn("getElementById('%s')" % ident, js, ident)
            self.assertEqual(self.page.count('id="%s"' % ident), 1, ident)

    def test_the_page_hands_home_the_bridge_that_the_list_reads(self):
        self.assertIn("client: function () { return client; }, join: probe, typed: joinTyped, name: myName,", self.page)
        self.assertIn("function probe(code, name, done) {", self.page)
        self.assertIn("function joinTyped(text, done, before) {", self.page)

    def test_the_old_room_is_let_go_of_only_when_the_new_one_welcomes_the_page(self):
        probe = self.page[self.page.index("function probe(code, name, done) {"):]
        probe = probe[:probe.index("open(p, name);")]
        self.assertLess(probe.index("p.on('welcome'"), probe.index("if (old) old.leave();"))
        self.assertLess(probe.index("client = p;"), probe.index("if (old) old.leave();"))
        self.assertEqual(probe.count("old.leave()"), 1, "the one place where the old room is let go of")
        self.assertEqual(probe.count("client.leave()"), 0)
        self.assertIn("if (adopted) return;", probe, "a Welcome that comes after the page gave the look up takes nothing")
        self.assertIn("if (handing) { adopted = true; clearTimeout(timer); p.leave(); return; }", probe, "a Welcome that comes when the page is on its way to a game is let go of at once, and says nothing")
        join = read("web", "home", "games.js")
        join = join[join.index("function joinRow(row) {"):join.index("function joinTyped() {")]
        self.assertNotIn("page.leave()", join, "a Join from the list never leaves a room itself")

    def test_a_question_comes_before_the_name_card_and_before_anything_is_left(self):
        typed = self.page[self.page.index("function joinTyped(text, done, before) {"):self.page.index("function probe(code, name, done) {")]
        self.assertLess(typed.index("Rules.typedCode(text)"), typed.index("(before || function (go) { go(); })("))
        self.assertLess(typed.index("(before || function (go) { go(); })("), typed.index("askName({"))

    def test_home_loads_the_list_after_its_first_paint_and_tells_it_what_happens(self):
        self.assertIn("var GAMES = 'home/games';", self.home)
        self.assertIn("lazy(GAMES, function () {", self.home)
        self.assertIn("css.href = base + '.css'; css.onload = one;", self.home)
        self.assertIn("js.src = base + '.js'; js.async = true; js.onload = one;", self.home)
        self.assertIn("function (fn) { win.requestAnimationFrame(function () { setTimeout(fn, 0); }); } : function (fn) { setTimeout(fn, 0); };", self.home, "after the first paint")
        self.assertIn("afterPaint(load);", self.home)
        for call in ("gamesCall('shown', name);", "gamesCall('sync');", "gamesCall('opening');"):
            self.assertIn(call, self.home, call)
        self.assertIn("function gamesCall(name, arg) { try { if (games) games[name](arg); } catch (e) { /* the list is its own */ } }", self.home, "what goes wrong in the list stays in it")

    def test_the_map_picker_comes_before_home_so_that_it_is_there_when_the_list_runs(self):
        self.assertLess(self.page.index('<script src="front/map_picker.js"></script>'), self.page.index('<script src="home/home.js"></script>'), "the list's script (loaded after the first paint) reads AntsMapPicker from the window")

    def test_the_page_is_under_its_line_limit(self):
        self.assertLessEqual(len(self.page.split("\n")) - 1, LINES, "tools/check_hygiene.py: the page's line limit")

    def test_the_card_of_a_code_that_was_typed_is_the_one_that_the_names_of_links_use(self):
        self.assertEqual(self.page.count("askName({"), 2, "a link and a typed code are the two things that ask for a name")


class TheFolder(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.js = read("web", "home", "games.js")
        cls.code = code_of(cls.js)

    def test_the_two_files_are_there_and_nothing_of_them_is_in_the_folder_that_has_its_cap_already(self):
        for name in FILES:
            self.assertTrue(os.path.isfile(os.path.join(HOME, name)), name)
            self.assertNotIn(name, os.listdir(FRONT))

    def test_they_are_under_their_budgets(self):
        raw = zipped = 0
        for name in FILES:
            with open(os.path.join(HOME, name), "rb") as f:
                data = f.read()
            raw += len(data)
            zipped += len(gzip.compress(data, 9))
        self.assertLess(raw, GAMES_BUDGET, "games.js and games.css are %d bytes" % raw)
        self.assertLess(zipped, GAMES_GZIP_BUDGET, "and %d bytes gzipped" % zipped)

    def test_the_script_is_plain_es5_and_names_no_other_site(self):
        for token in (r"\bconst\b", r"\blet\b", r"=>", r"`", r"\bimport\b", r"\bclass\b", r"\basync\b", r"\bawait\b"):
            self.assertIsNone(re.search(token, self.code), token)
        self.assertEqual(re.findall(r"https?://[^'\" )]+", self.js), [], "no address at all: the list is the site's own")
        self.assertIn("'use strict';", self.js)

    def test_nothing_that_comes_from_the_list_is_ever_markup_a_link_or_a_script(self):
        for word in ("innerHTML", "outerHTML", "insertAdjacentHTML", "document.write", "createContextualFragment", "DOMParser", "eval(", "new Function", "setAttribute('href'", "setAttribute('src'", "setAttribute('style'", ".href =", ".onclick", "srcdoc",
                     "location.assign", "location.href", "window.open", ".style.cssText"):
            self.assertNotIn(word, self.code, word)
        self.assertEqual(len(re.findall(r"\.src = ", self.code)), 1, "one address is made, for a picture: an original's own, or the library's")
        self.assertIn("var src = row.mapKey ? 'front/preview_' + row.mapKey + '.png' : Picker ? Picker.previewPath(row.map) : '';", self.js, "an original's picture comes from a key of the page's own table, a map of the players' from the map picker's rule")
        self.assertIn("img.src = src;", self.js)
        self.assertIn("mapKey: known ? known.key : ''", self.js, "the key is the page's table's, never the row's text")
        self.assertIn("Net = win.AntsLobbyNet, Picker = win.AntsMapPicker;", self.js, "the picker is read from the page's window, and a page without it has no picture of the players' maps (never a break)")

    def test_the_picture_of_a_map_of_the_players_is_the_pickers_rule_and_not_a_second_copy_of_it(self):
        for word in ("percentEncode", "encodeURIComponent", "escape(", "/maps/previews", ".webp", "decodeURI"):
            self.assertNotIn(word, self.js, "%s: the address is made by AntsMapPicker.previewPath (web/front/map_picker.js), which checks the file name and the shape of what it makes" % word)
        picker = read("web", "front", "map_picker.js")
        self.assertIn("function previewPath(map) {", picker)
        self.assertIn("previewPath: previewPath", picker)
        self.assertIn("return ownPath('/maps/previews/' + percentEncode(base) + '.webp');", picker, "what the list's script relies on: one shape, the library's, checked")

    def test_a_picture_that_does_not_come_is_taken_away_and_the_square_stays(self):
        self.assertEqual(self.js.count("img.addEventListener('error', function () { pic.removeChild(img); });"), 1)
        self.assertIn("img.setAttribute('loading', 'lazy');", self.js, "a list of 50 rows is not 50 requests at once, and a closed popover asks for none")
        self.assertLess(self.js.index("img.setAttribute('loading', 'lazy');"), self.js.index("img.src = src;"), "set before the address, which is when a browser reads it")
        self.assertIn("pic.setAttribute('aria-hidden', 'true');", self.js)

    def test_nothing_is_kept_in_the_browser_and_the_one_address_has_no_query_and_no_cookies(self):
        for word in ("localStorage", "sessionStorage", "indexedDB", "document.cookie", "XMLHttpRequest", "sendBeacon", "WebSocket", "BroadcastChannel"):
            self.assertNotIn(word, self.code, word)
        self.assertIn("var LIST_URL = '/lobbies';", self.js)
        self.assertEqual(len(re.findall(r"win\.fetch\(", self.code)), 1)
        self.assertIn("win.fetch(LIST_URL, { cache: 'no-store', credentials: 'omit', signal: ctrl ? ctrl.signal : undefined })", self.js)

    def test_the_numbers_of_the_polling_are_the_ones_of_the_notes(self):
        for line in ("var EVERY_CLOSED = 30000, EVERY_OPEN = 5000;", "var JITTER = 0.2;", "var ROWS_MAX = 50;", "var REFRESH_MS = 600000;", "var FETCH_MS = 8000;", "var CAP_CLOSED = 300000, CAP_OPEN = 60000;"):
            self.assertIn(line, self.js, line)

    def test_every_call_into_the_page_is_guarded(self):
        self.assertIn("function guard(fn) { return function () { if (gone) return; try { return fn.apply(null, arguments); } catch (e) {", self.js)
        for event in ("'click'", "'keydown'", "'visibilitychange'", "'resize'", "'room'", "'welcome'", "'refused'"):
            for m in re.finditer(r"addEventListener\(%s, (\w+)\(" % event, self.js):
                self.assertEqual(m.group(1), "guard", event)
        self.assertIn("function client() { try { var c = page.client(); return c && !c.final() ? c : null; } catch (e) { return null; } }", self.js)


class TheStyle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.css = read("web", "home", "games.css")
        cls.rules = re.findall(r"([^{}]+)\{([^{}]*)\}", re.sub(r"/\*.*?\*/", "", cls.css, flags=re.S))

    def test_every_target_is_at_least_44_px_high_and_wide(self):
        heights = {}
        for selector, body in self.rules:
            for m in re.finditer(r"min-height: (\d+)px", body):
                heights.setdefault(selector.strip(), []).append(int(m.group(1)))
        for selector in (".gjoin", ".gcode .gcgo", ".gcode .joinbox input", ".vbtn", ".gcbtns .btn", ".gstrip .btn", ".gjoining .btn"):
            self.assertIn(selector, heights, selector)
        for selector, values in heights.items():
            for value in values:
                self.assertGreaterEqual(value, 44, "%s has a min-height of %d" % (selector, value))
        self.assertIn(".gjoining[hidden] { display: none; }", self.css, "a Join that is not on its way shows no line (and no Cancel)")
        for selector in (r"\.gjoin", r"\.gcode \.gcgo", r"\.gjoining \.btn"):
            self.assertGreaterEqual(int(re.search(selector + r" \{[^}]*min-width: (\d+)px", self.css).group(1)), 44, selector)
        self.assertNotRegex(self.css, r"(?<!min-)height: [1-3]\d px; *}", "no fixed height under 40 px on a control")

    def test_nothing_is_loaded_from_another_site_and_nothing_scrolls_sideways(self):
        self.assertNotRegex(self.css, r"https?://|@import")
        self.assertEqual(re.findall(r"url\(", self.css), [], "no picture at all: the pictures of a row are the page's own (an <img>)")
        self.assertNotRegex(self.css, r"overflow-x: *(scroll|auto)")

    def test_the_card_is_fixed_over_the_whole_window_so_that_it_stands_over_both_screens(self):
        shade = next(body for selector, body in self.rules if selector.strip() == ".gshade")
        self.assertIn("position: fixed;", shade)
        self.assertIn("inset: 0;", shade)
        self.assertNotIn("position: absolute", shade, "the card is the body's: Home's div is not shown on the Room, and Leave room asks there")

    def test_the_popover_is_over_the_page_under_the_menu_and_the_card_is_over_the_popover(self):
        z = {sel.strip(): int(re.search(r"z-index: (\d+)", body).group(1)) for sel, body in self.rules if re.search(r"z-index: (\d+)", body)}
        self.assertEqual(sorted(z), [".gdock", ".gpop", ".gshade"])          # (.gdock: the list docked in the Yard on a site with no label, under the chat panel's 6 where they meet)
        self.assertLess(z[".gdock"], 6)
        self.assertLess(z[".gpop"], 60, "the Menu (home.css) is 60")
        self.assertGreater(z[".gshade"], z[".gpop"])
        self.assertIn("position: fixed;", next(body for sel, body in self.rules if sel.strip() == ".gpop"))
        self.assertIn("overflow-y: auto;", next(body for sel, body in self.rules if sel.strip() == ".gpop"), "the list scrolls inside itself")

    def test_the_switch_has_a_layout_for_a_wide_window_and_for_a_phone(self):
        self.assertIn("@media (min-width: 721px) {", self.css)
        self.assertIn("@media (max-width: 720px) {", self.css)
        self.assertIn(".vbtn[aria-pressed=\"true\"] {", self.css)
        self.assertIn(".vwide { display: none; }", self.css)

    def test_the_switch_label_is_centered_by_the_browser_not_hung_at_the_top_by_a_baseline_row(self):
        vbtn = next(body for selector, body in self.rules if selector.strip() == ".vbtn")
        self.assertIn("display: block;", vbtn, "a flex row with align-items: baseline hangs the label at the top of the 44 px button (tests/scripts/web_home_games_browser.js, part 1j, measures it)")
        self.assertNotRegex(self.css, r"\.vbtn[^{]*\{[^}]*(baseline|inline-flex)")

    def test_the_classes_that_the_script_makes_are_the_styles_and_the_page_s_own(self):
        made = set()
        for m in re.finditer(r"el\('[a-z0-9]+', '([\w -]+)'", read("web", "home", "games.js")):
            made.update(m.group(1).split())
        for m in re.finditer(r"button\('([\w -]+)'", read("web", "home", "games.js")):
            made.update(m.group(1).split())
        theirs = {"btn", "banner", "sm", "joinbox", "hint", "ant", "ant-green", "gyes", "sr"}      # (the page's: web/lobby.html, "sr" its line that only a screen reader reads; and the answer that is the page's own teal button: only the tests look for it by its class)
        for name in sorted(made - theirs):
            self.assertRegex(self.css, r"\." + re.escape(name) + r"\b", "the class %s has no style" % name)


class TheListIsTheYardsOnAUnlabelledSite(unittest.TestCase):
    """A site with no site label (production: docs/HOME_SCREEN.md, "The Yard") shows the Games list as the page under the bar, docked in the Yard (home.js hands it #yard as `dock`), always there, the same rows and Join as the
    popover's (one code); a site with the label (the test site) keeps the popover and the stand-in. The list is centered in the window from 1280 px wide, right of the chat panel in a window of 761 to 1279 px and above its strip
    on a phone, so that no Join is covered."""

    @classmethod
    def setUpClass(cls):
        cls.js = read("web", "home", "games.js")
        cls.css = read("web", "home", "games.css")
        cls.home = read("web", "home", "home.js")
        cls.chat = read("web", "home", "chat.css")

    def test_home_docks_the_list_in_the_yard_only_where_there_is_no_label(self):
        self.assertEqual(self.home.count("dock: standIn ? null : yard,"), 1)
        self.assertEqual(self.js.count("env.dock"), 2)
        self.assertIn("(env.dock || host).appendChild(pop);", self.js)

    def test_the_docked_list_is_the_popover_s_code_with_a_few_switches(self):
        js = self.js
        self.assertEqual(js.count("function rowNode("), 1, "one place paints a row")
        self.assertEqual(js.count("function paintList("), 1)
        self.assertEqual(js.count("function joinRow("), 1)
        self.assertEqual(js.count("function look("), 1, "one GET /lobbies")
        for part in ("isOpen = docked,", "pop.hidden = !docked;", "docked ? 'region' : 'dialog'", "if (docked) return;", "if (!isOpen || docked) return;", "if (docked) focusList(); else if (isOpen) closeList(true); else openList();",
                     "if (docked) pop.className += ' gdock';"):
            self.assertEqual(js.count(part), 1, part)

    def test_the_docked_list_keeps_clear_of_the_chat_dock_and_its_strip(self):
        rules = re.sub(r"/\*.*?\*/", "", self.css, flags=re.S)
        wide = re.search(r"\.gdock \{ position: absolute; z-index: (\d+); top: 12px; left: (\d+)px; right: 12px; width: min\((\d+)px, calc\(100% - (\d+)px\)\); max-height: calc\(100% - 24px\); margin: 0 auto; \}", rules)
        self.assertIsNotNone(wide)
        z, left, cap, taken = (int(g) for g in wide.groups())
        chat = re.search(r"\.gc \{ position: absolute; left: (\d+)px; bottom: 22px; z-index: (\d+);[^}]*width: (\d+)px;", self.chat)
        self.assertLess(z, int(chat.group(2)), "the chat panel is over the list where they meet")
        self.assertGreaterEqual(left, int(chat.group(1)) + int(chat.group(3)), "on a wide window the list is right of the chat panel (24 + 380 px)")
        self.assertEqual(left + 12, taken)
        self.assertLess(z, 55, "under the Menu and the cards")
        self.assertIn("@media (max-width: 720px), (max-height: 500px) { .gdock { top: 8px; left: 8px; right: 8px; width: auto; max-height: calc(100% - 90px); } }", rules)
        self.assertIn("(max-width: 720px), (max-height: 500px) {", self.chat, "the strip's own breakpoint is the one that the list uses")
        self.assertIn("@media (min-width: 721px) and (max-width: 760px) and (min-height: 501px) { .gdock { left: 12px; right: 12px; width: auto; max-height: calc(100% - 428px); } }", rules)

    def test_the_docked_list_is_centered_in_the_window_where_the_chat_panel_leaves_room(self):
        """The Yard has the same margin on both sides of the window, so the same room on both sides of the list is a list in the window's middle. The chat panel is at the left only, and the list keeps clear of it by that room
        (so no height of the panel or of the list can make one cover the other): the default rule puts the list right of the panel, in the middle of the space there, and from the window width at which the list is still
        400 px wide the same room at the right as at the left puts its middle in the window's. A narrower window cannot have both (the panel is 404 px of it), so it keeps the list right of the panel."""
        rules = re.sub(r"/\*.*?\*/", "", self.css, flags=re.S)
        base = re.search(r"\.gdock \{ position: absolute; z-index: \d+; top: 12px; left: (\d+)px; right: 12px; width: min\((\d+)px, calc\(100% - (\d+)px\)\);", rules)
        wide = re.search(r"@media \(min-width: (\d+)px\) \{ \.gdock \{ right: (\d+)px; width: min\((\d+)px, calc\(100% - (\d+)px\)\); \} \}", rules)
        self.assertIsNotNone(base)
        self.assertIsNotNone(wide, "the rule that centers the list in the window")
        left, cap = int(base.group(1)), int(base.group(2))
        edge, right, wide_cap, taken = (int(g) for g in wide.groups())
        self.assertEqual(right, left, "the same room on both sides of the list")
        self.assertEqual(taken, left + right, "and the width is what is left between them")
        self.assertEqual(wide_cap, cap, "up to the same 560 px")
        self.assertEqual(edge, taken + 24 + 400, "from this window width the list is at least 400 px wide (24 px: the Yard's margins and borders); below it the list keeps to the right of the chat panel")
        chat = re.search(r"\.gc \{ position: absolute; left: (\d+)px; bottom: 22px; z-index: (\d+);[^}]*width: (\d+)px;", self.chat)
        self.assertGreaterEqual(right, int(chat.group(1)) + int(chat.group(3)), "the room at the right is the chat panel's too: the list's left edge is clear of the panel at every width")
        self.assertLess(rules.index(base.group(0)), rules.index(wide.group(0)), "after the rule that it changes")
        for later in ("@media (min-width: 721px) and (max-width: 760px) and (min-height: 501px) {", "@media (max-width: 720px), (max-height: 500px) { .gdock {"):
            self.assertLess(rules.index(wide.group(0)), rules.index(later), "the rules for a short window and the stacked layout come after it and win: " + later)


class TheWayIntoTheImage(unittest.TestCase):
    def test_the_dockerfile_copies_the_folder_to_where_nginx_serves_home(self):
        self.assertIn("COPY web/home/ /usr/share/nginx/html/home/\n", read("Dockerfile"))

    def test_the_local_web_build_packs_everything_of_it(self):
        self.assertIn('cp -f "${SCRIPT_DIR}/web/home"/* "${DIST_DIR}/home/"', read("build_web.sh"))

    def test_the_ci_looks_for_the_script_and_the_style_in_the_image(self):
        self.assertIn(" home/games.js home/games.css ", read(".github", "workflows", "ci.yml"))


@unittest.skipUnless(NODE, "node is not installed: the Games list and the public Room were NOT run (tests/scripts/web_home_games_check.js, web_home_games_room_check.js)")
class TheCodeRuns(unittest.TestCase):
    def test_the_list_in_the_fake_browser(self):
        done = subprocess.run([NODE, CHECKS[0], LOBBY], capture_output=True, text=True, timeout=300, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout[-4000:] + done.stderr[-2000:])
        self.assertRegex(done.stdout, r"the games list: \d+ checks, 0 failed")

    def test_the_switch_quick_host_and_join_in_the_fake_browser(self):
        done = subprocess.run([NODE, CHECKS[1], LOBBY], capture_output=True, text=True, timeout=300, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout[-4000:] + done.stderr[-2000:])
        self.assertRegex(done.stdout, r"public room, Quick host and Join: \d+ checks, 0 failed")


class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/home_games_mutants.json (the list that tools/mutate.py runs too, with `--keep-going --jobs 1`) on a COPY of the files, one at a time: each must make the checks fail, and the copy with
    nothing changed must pass"""
    FILES = ["web/lobby.html", "web/front/player_name.js", "web/home/home.js", "web/home/home.css", "web/home/chat.css", "web/home/games.js", "web/home/games.css", "web/shapes.js", "tests/scripts/test_web_home_games.py", "tests/scripts/web_home_games_check.js",
             "tests/data/home_games_thumbs.json", "Community-Maps/previews.json",
             "tests/scripts/web_home_games_room_check.js", "tests/scripts/web_home_games_quickjoin_check.js", "tests/scripts/web_home_games_rig.js", "tests/scripts/web_page_harness.js", "Dockerfile", "build_web.sh", ".github/workflows/ci.yml"] \
        + ["web/front/" + n for n in ("lobby_rules.js", "lobby_net.js", "lobby_chat.js", "rematch.js", "game_modes.js", "game_type.js", "map_picker.js", "map_maker_link.js")]

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "home_games_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_games_mutants.")
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
