#!/usr/bin/env python3
"""The public Room and the Games list of the front page (web/home/games.js and games.css, docs/HOME_SCREEN.md, docs/PUBLIC_ROOMS.md; run by ./run_tests.sh --fast and by the CI).

The front page shows the Home bar's **Games** (a popover with the list that `GET /lobbies` gives, Join, Join by code), **Quick host**, and, in the Room's card, the host's Public | Private switch. All of it is made by a
script and a style that the page loads after its first paint, in the folder of the two screens (web/home/), with a budget of their own. Text that comes from the list is somebody else's: it is checked field by field and
drawn as text only.

  - the page: what the script reads of web/lobby.html is there (the heading, the colors, the strips, the Rejoin button), the page hands it its small bridge (the client, the look at a room, a typed code, the name), the
    page's probe leaves the old room only when the new one welcomes the page;
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
import sys
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HOME = os.path.join(REPO, "web", "home")
FRONT = os.path.join(REPO, "web", "front")
LOBBY = os.path.join(REPO, "web", "lobby.html")
CHECKS = [os.path.join(REPO, "tests", "scripts", n) for n in ("web_home_games_check.js", "web_home_games_room_check.js", "web_home_games_quickjoin_check.js", "web_home_games_live_check.js")]
NODE = shutil.which("node")
FILES = ("games.js", "games.css")
sys.path.insert(0, os.path.join(REPO, "tools"))
from size_limits import GAMES_BUDGET, GAMES_GZIP_BUDGET  # noqa: E402  (every byte budget: tools/size_limits.py)


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
        for ident in ("rejoin", "rejoin-go", "room-h", "slots"):
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
        for word in ("percentEncode", "escape(", "/maps/previews", ".webp", "decodeURI"):
            self.assertNotIn(word, self.js, "%s: the address is made by AntsMapPicker.previewPath (web/front/map_picker.js), which checks the file name and the shape of what it makes" % word)
        self.assertEqual(self.js.count("encodeURIComponent"), 1, "the one use: the id in the address of Watch (a second guard, the id is checked by its shape first)")
        self.assertIn("'/play?live=' + encodeURIComponent(e.id)", self.js)
        picker = read("web", "front", "map_picker.js")
        self.assertIn("function previewPath(map) {", picker)
        self.assertIn("previewPath: previewPath", picker)
        self.assertIn("return ownPath('/maps/previews/' + percentEncode(base) + '.webp');", picker, "what the list's script relies on: one shape, the library's, checked")

    def test_a_picture_that_does_not_come_is_taken_away_and_the_square_stays(self):
        self.assertEqual(self.js.count("img.addEventListener('error', function () { pic.removeChild(img); });"), 1)
        self.assertIn("img.setAttribute('loading', 'lazy');", self.js, "a list of 50 rows is not 50 requests at once, and a closed popover asks for none")
        self.assertLess(self.js.index("img.setAttribute('loading', 'lazy');"), self.js.index("img.src = src;"), "set before the address, which is when a browser reads it")
        self.assertIn("pic.setAttribute('aria-hidden', 'true');", self.js)

    def test_nothing_is_kept_in_the_browser_and_the_two_addresses_have_no_query_and_no_cookies(self):
        for word in ("sessionStorage", "indexedDB", "document.cookie", "XMLHttpRequest", "sendBeacon", "WebSocket", "BroadcastChannel"):
            self.assertNotIn(word, self.code, word)
        self.assertNotIn("localStorage", self.code, "nothing is kept in the browser: the matches in progress always show")
        self.assertNotIn("ants.home.games", self.code, "no stored word (an old one from an earlier version is never read)")
        self.assertNotRegex(read("web", "home", "games.css"), r"\.gfilter|\.gfrow", "no switch in the list's head")
        self.assertIn("var LIST_URL = '/lobbies';", self.js)
        self.assertIn("var LIVE_URL = '/live';", self.js)
        self.assertEqual(len(re.findall(r"win\.fetch\(", self.code)), 1, "one fetch, in look(url), for both addresses")
        self.assertIn("win.fetch(url, { cache: 'no-store', credentials: 'omit', signal: ctrl ? ctrl.signal : undefined })", self.js)
        self.assertEqual(sorted(set(re.findall(r"look\((\w+)\)", self.code)) - {"url"}), ["LIST_URL", "LIVE_URL"], "the two addresses and no other")

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

    def test_watch_has_the_grid_place_of_join(self):
        self.assertIn(".gdock .gjoin, .gdock .gwatch { grid-area: j; }", read("web", "home", "home.css"), "without its area Watch is put by the grid's own order, not at the row's right edge")

    def test_a_dot_of_an_unknown_color_is_neutral_and_not_the_color_of_a_seat(self):
        color = {selector.strip(): body.strip() for selector, body in self.rules if selector.strip().startswith(".gdot.c")}
        self.assertEqual(color[".gdot.cn"], "--c: #8a8693;", "a color that the door does not know has its own neutral dot")
        self.assertEqual(len({color[".gdot.c%s" % k] for k in ("0", "1", "2", "3", "n")}), 5, "no seat's color is used for the neutral dot (nor twice)")

    def test_the_names_of_a_match_can_shrink_so_they_stay_out_of_watch(self):
        who = next(body for selector, body in self.rules if selector.strip() == ".gwho")
        self.assertIn("flex: 1 1 9em;", who, "a names box that cannot shrink (flex-shrink 0) runs under Watch in a narrow row")

    def test_the_toast_is_over_the_page_and_takes_no_room(self):
        toast = next(body for selector, body in self.rules if selector.strip() == ".gtoast")
        self.assertIn("position: fixed;", toast, "a notice is out of the flow: nothing under it moves")
        for word in ("position: relative", "position: static", "position: sticky", "float:"):
            self.assertNotIn(word, toast)
        self.assertIn("animation: gtoastin", toast)
        self.assertRegex(self.css, r"@media \(prefers-reduced-motion: reduce\) \{[^}]*\.gtoast \{ animation: none; \}", "no motion for a reader who asks for none")
        self.assertIn(".gtoast[hidden]", self.css, "a hidden toast is not drawn")

    def test_the_toast_is_placed_above_the_chat_and_its_field_and_goes_by_itself(self):
        js = read("web", "home", "games.js")
        self.assertIn("rectOf('#gchat') || rectOf('.htabs')", js, "above the chat, else above the strip of views")
        self.assertIn("(rectOf('.gc-form') || { top: r.bottom - 6 }).top", js, "a phone's Chat view: above the field, not over it")
        self.assertIn("rectOf('#hmain') ? r.top :", js, "the chat fills the view (no body above it): over its field, not over the bar")
        self.assertIn("(r = rectOf('.go'))) y = r.top;", js, "the Room on a phone: above the sticky START bar")
        self.assertIn("< bar.bottom + 2) up = h - bar.bottom - 2 - toast.offsetHeight;", js, "never into the bar at the top")
        self.assertIn("bar = rectOf('.hbar'), r = rectOf('#gchat')", js, "Home's bar is never covered; on the Room START! comes first")
        self.assertIn("if (lastFocus && lastFocus.focus) lastFocus.focus();", js, "the focus goes back when Escape closes the toast")
        self.assertIn("toastWatch.disconnect();", js, "no look at the page after the hand-off")
        self.assertIn("toast.style.bottom = Math.round(up) + 'px';", js)
        self.assertIn("TOAST_MS = 6000;", js)
        self.assertIn("toast.hidden || toastAsks ? null : later(", js, "a toast with a question stays; the others have a timer")
        self.assertIn("toast.matches(':hover, :focus-within')", js, "a toast under the pointer or the focus waits")
        self.assertIn("doc.body.appendChild(toast)", js, "the toast is the body's, not part of Home's flow")
        self.assertIn("placeVis(); placeToast(); }));", js, "placed again when the window changes")
        self.assertIn("if (!toast.hidden) placeToast(); pollLive(); }))", js, "placed again when a phone's view or the chat's fold changes (and the looks at /live follow whether the list is drawn)")
        self.assertIn("attributeFilter: ['data-tab', 'data-open', 'data-screen', 'hidden']", js)
        self.assertIn("if (!dialog && toast.contains(doc.activeElement)) { clearNotice();", js, "Escape closes a toast that has the focus, and only then")

    def test_have_a_code_is_a_disclosure_button_in_the_heading(self):
        js = read("web", "home", "games.js")
        self.assertIn("popHead.appendChild(codeLink);", js.replace("popHead.appendChild(popTitle); popHead.appendChild(popCount); ", ""), "the button is in the heading")
        self.assertIn("codeLink.setAttribute('aria-expanded', 'false'); codeLink.setAttribute('aria-controls', 'gc-box');", js)
        self.assertIn("codeLink.setAttribute('aria-expanded', on ? 'true' : 'false');", js, "the state is told every time it changes")
        self.assertIn("else if (codeOpen && pop.contains(doc.activeElement)) setCode(false, true);", js, "Escape shuts the box first and puts the focus back on the button")
        self.assertIn(".gcode[hidden] { display: none; }", self.css, "a shut box takes no room")

    def test_every_target_is_at_least_44_px_high_and_wide(self):
        heights = {}
        for selector, body in self.rules:
            for m in re.finditer(r"min-height: (\d+)px", body):
                heights.setdefault(selector.strip(), []).append(int(m.group(1)))
        for selector in (".gjoin", ".gcodelink", ".gcode .gcgo", ".gcode .gcx", ".gcode .joinbox input", ".vbtn", ".gcbtns .btn", ".gtoast .btn", ".gjoining .btn"):
            self.assertIn(selector, heights, selector)
        for selector, values in heights.items():
            for value in values:
                self.assertGreaterEqual(value, 44, "%s has a min-height of %d" % (selector, value))
        self.assertIn(".gjoining[hidden] { display: none; }", self.css, "a Join that is not on its way shows no line (and no Cancel)")
        for selector in (r"\.gjoin", r"\.gcodelink", r"\.gcode \.gcgo", r"\.gcode \.gcx", r"\.gjoining \.btn"):
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
        self.assertEqual(sorted(z), [".gpop", ".gshade", ".gtoast"])          # (the list docked in Home's body has no layer of its own: home.css, ".hmain .gdock", is a box of the page's grid)
        self.assertLess(z[".gpop"], 60, "the Menu (home.css) is 60")
        self.assertGreater(z[".gshade"], z[".gpop"])
        self.assertGreater(z[".gtoast"], z[".gpop"], "a toast is over the list")
        self.assertLess(z[".gtoast"], z[".gshade"], "the question card is over a toast")
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


class TheListIsDockedInHomesBody(unittest.TestCase):
    """Home docks the Games list in its body (home.js hands it #hmain as `dock`): always there, as the games panel at the left of the players list, with the same rows and Join as the popover's (one code). Each room is a
    card (home.css); the popover (a list that is given no dock) is the same code with the switches below. The docked box has no layer of its own and no place of its own: it is a box of Home's grid, so nothing can cover it."""

    @classmethod
    def setUpClass(cls):
        cls.js = read("web", "home", "games.js")
        cls.css = read("web", "home", "games.css")
        cls.home = read("web", "home", "home.js")
        cls.homecss = read("web", "home", "home.css")

    def test_home_docks_the_list_in_its_body(self):
        self.assertEqual(self.home.count("dock: main, page: page"), 1)
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

    def test_the_docked_list_is_a_box_of_the_grid_and_the_cards_are_a_grid_of_their_own(self):
        self.assertNotIn(".gdock", self.css, "games.css has the popover's rows; the docked list's place and cards are Home's (home.css)")
        rules = re.sub(r"/\*.*?\*/", "", self.homecss, flags=re.S)
        box = re.search(r"\.hmain \.gdock \{([^}]*)\}", rules).group(1)
        self.assertIn("position: relative; z-index: auto;", box, "no layer: the popover's fixed place and layer are put away")
        self.assertIn("min-height: 0; overflow: hidden;", box, "the panel does not grow past its cell: the cards scroll inside it")
        self.assertIn(".gdock .gpbody { flex: 1 1 auto; min-height: 0; overflow-y: auto;", rules, "the heading and the foot (Join by code) stay, the cards scroll")
        self.assertIn(".gdock .glist { display: grid; grid-template-columns: repeat(auto-fill, minmax(290px, 1fr));", rules)
        self.assertIn('grid-template-areas: "m t t" "m l j";', rules, "the map at the left, the host and the map's name, then the seats and Join")
        self.assertIn(".gdock .gitem:has(.gyours) { border-color: var(--gold); }", rules)
        self.assertIn(".gdock .glist { grid-template-columns: minmax(0, 1fr); }", rules, "one column on a phone")

    def test_the_empty_state_is_text_only_the_bar_has_quick_host_and_join(self):
        self.assertNotIn("gerow", self.js)
        self.assertNotIn("gerow", self.homecss)
        self.assertNotIn("emptyRow", self.js)
        m = re.search(r"popEmpty[.]appendChild[(]el[(]'span', 'ant ant-green'.*\n.*\n", self.js)
        self.assertIsNotNone(m)
        self.assertNotIn("button(", m.group(0), "the card says it in words; the buttons are the bar's")


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
    FILES = ["tools/size_limits.py", "web/lobby.html", "web/front/player_name.js", "web/home/home.js", "web/home/home.css", "web/home/chat.css", "web/home/games.js", "web/home/games.css", "web/shapes.js", "tests/scripts/test_web_home_games.py", "tests/scripts/web_home_games_check.js",
             "tests/data/home_games_thumbs.json", "Community-Maps/previews.json",
             "tests/scripts/web_home_games_room_check.js", "tests/scripts/web_home_games_quickjoin_check.js", "tests/scripts/web_home_games_rig.js", "tests/scripts/web_page_harness.js", "Dockerfile", "build_web.sh", ".github/workflows/ci.yml"] \
        + ["web/front/" + n for n in ("lobby_rules.js", "lobby_net.js", "lobby_chat.js", "rematch.js", "game_modes.js", "game_type.js", "map_picker.js", "map_maker_link.js")]

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "home_games_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import mutant_shard                     # (here and not at the top: the copies in which the mutants run have this file but not that one, and run the other classes)
        cls.mutants = mutant_shard.from_environment(cls.spec["mutants"])     # (all of them, or the units of tests/scripts/run_python_tests.py: ANTS_MUTANT_SHARD=K/N)
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
        status, out = self.run_command(self.mutants[0]["test"])
        self.assertEqual(status, 0, out[-3000:])

    def test_every_mutant_is_seen(self):
        ids = [m["id"] for m in self.spec["mutants"]]
        self.assertEqual(len(ids), len(set(ids)), "an id twice")
        missed = []
        for m in self.mutants:
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
