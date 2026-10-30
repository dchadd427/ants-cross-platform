#!/usr/bin/env python3
"""The chat dock of Home (web/home/chat.js and chat.css, docs/HOME_SCREEN.md "The chat dock"; run by ./run_tests.sh --fast and by the CI).

Home carries the site's global chat in a panel in its lower-left corner (a strip along the bottom on a phone). It reads and writes the chat hub, one WebSocket at /hub/ws (docs/HUB.md), and every word it shows
is text from strangers. It is loaded by web/home/home.js, after the first paint and only when Home is shown, so that it can never hold up Home, the Room or START. What this file checks:

  - the page and the other pages: web/lobby.html does not link the dock, no other page names it or the hub's door, and the only script that knows the address /hub/ws is chat.js;
  - the loader in home.js: the two files, once, after the first paint, only when Home shows, mounted when BOTH have come, and whatever goes wrong with them stays with them;
  - the folder: its own budgets (raw and gzipped), plain ES5, no markup made from text, no link, no storage, no other site, nothing that opens a socket by itself;
  - the style: the sizes of the approved pictures (380 x 380 in the corner, folded to its header, the strip of 62 px, the panel of a phone and of a phone held sideways), every target at least 44 px (the
    field and Send 48), nothing sideways, reduced motion, nothing that the Menu's layer does not stay above, the login chip's place hidden, nothing loaded from another site;
  - the way into the image: the CI's list of the image's files carries the two files, the Dockerfile and the local web build copy the folder;
  - the code RUNS in node: tests/scripts/web_home_chat_check.js (a fake hub and a fake clock: the pure parts, what the dock shows, hostile text, the link and its backoff, the visitor, the page) and
    tests/scripts/web_home_chat_hub_check.js (tests/scripts/fake_hub.js on a real socket, the real clock);
  - tests/data/home_chat_mutants.json changes one thing at a time in a COPY of the files (innerHTML, no cap on the lines, del ignored, a send while offline, no backoff, a reconnect storm, unread never reset,
    the words of the errors, garbage that throws, a socket on another page, no ping, a login button with login:false, the field without `you`, ...): the checks must then fail (the last class of this
    file; tools/mutate.py runs the same list).
The pictures that were approved are compared by hand with the page in a real browser (tests/scripts/web_home_chat_browser.js, opt-in, `--shots DIR`).
"""
import gzip
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unicodedata
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HOME = os.path.join(REPO, "web", "home")
WEB = os.path.join(REPO, "web")
LOBBY = os.path.join(REPO, "web", "lobby.html")
CHECK = os.path.join(REPO, "tests", "scripts", "web_home_chat_check.js")
HUB_CHECK = os.path.join(REPO, "tests", "scripts", "web_home_chat_hub_check.js")
NODE = shutil.which("node")
FILES = ("chat.js", "chat.css")
CHAT_BUDGET, CHAT_GZIP_BUDGET = 51000, 15900           # bytes, raw and gzipped, both files together (about 48,600 and 15,200, with the login: the button, the chip, the account line, the mark of Discord; and with the dock's fit to the room under Home's bar and its queue of lines that wait for an answer): they load after the first paint, so they cost the first look nothing


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def code_of(js):
    return re.sub(r"//.*", "", js)


class ThePageAndTheOtherPages(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.page = read("web", "lobby.html")

    def test_the_front_page_does_not_link_the_dock_home_loads_it_later(self):
        for word in ("home/chat", "gchat", "AntsHomeChat", "/hub", "hub/ws"):
            self.assertNotIn(word, self.page, word)
        self.assertEqual(self.page.count('<script src="home/home.js"></script>'), 1)
        self.assertFalse([src for src in re.findall(r'<script src="([^"]*)"', self.page) if "chat" in src and "lobby_chat" not in src], "the page's scripts are the ones that it had: the dock is not one of them")

    def test_no_other_file_of_the_site_names_the_dock_or_the_hubs_door(self):
        naming_the_dock, naming_the_door = set(), set()
        for base, _dirs, names in os.walk(WEB):
            for name in names:
                if os.path.splitext(name)[1] not in (".html", ".js", ".css"):
                    continue
                path = os.path.join(base, name)
                rel = os.path.relpath(path, REPO).replace(os.sep, "/")
                with open(path, encoding="utf-8", errors="replace") as f:
                    text = f.read()
                if re.search(r"home/chat\.(js|css)", text):
                    naming_the_dock.add(rel)
                if re.search(r"/hub/ws|/hub\b", text):
                    naming_the_door.add(rel)
        self.assertEqual(sorted(naming_the_dock), ["web/home/chat.css", "web/home/chat.js", "web/home/home.js"], "the front page's own script asks for the dock; no other page does")
        self.assertEqual(sorted(naming_the_door), ["web/home/chat.js"], "only the dock's script knows the address of the hub: a page that opens a socket to it is a page that has a chat")

    def test_the_game_server_and_the_game_never_open_the_hub(self):
        for rel in (("web", "shell.html"), ("web", "front", "lobby_net.js"), ("web", "front", "lobby_chat.js")):
            self.assertNotRegex(read(*rel), r"/hub\b", rel[-1])


class TheLoaderInHome(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.js = read("web", "home", "home.js")

    def test_it_asks_for_the_two_files_by_their_addresses_and_nothing_else(self):
        self.assertIn("var CHAT = { script: 'home/chat.js', style: 'home/chat.css' };", self.js)
        self.assertEqual(sorted(set(re.findall(r"home/chat\.\w+", code_of(self.js)))), ["home/chat.css", "home/chat.js"])

    def test_it_waits_for_the_first_paint_and_only_for_home(self):
        self.assertIn("if (typeof win.requestAnimationFrame === 'function') win.requestAnimationFrame(function () { setTimeout(load, 0); }); else setTimeout(load, 0);", self.js, "after the frame that paints, not before")
        self.assertIn("if (name === 'home') wantDock();", self.js, "a link to a room shows the Room: no chat until Home shows")
        self.assertEqual(self.js.count("wantDock();"), 1, "one call, where a screen is shown")
        self.assertIn("if (asked) return;\n            asked = true;", self.js, "once")

    def test_the_dock_is_mounted_when_both_files_are_there_and_whatever_goes_wrong_stays_in_it(self):
        self.assertIn("var parts = 2;", self.js)
        self.assertIn("function mountDock() { if (--parts) return; try { if (win.AntsHomeChat) { dock = win.AntsHomeChat.mount({ doc: doc, win: win, host: host, chip: login, screen: function () { return screen; } }); inert(held); dockShown(); } } catch (e) { dock = null; } }", self.js)
        self.assertIn("[bar, rbar, dock && dock.root].forEach(function (n) { if (n) {", self.js, "a dialog over the page, or a hand-off, takes the dock's keys and presses too: the focus does not go behind the modal")
        self.assertIn("css.onload = mountDock;", self.js)
        self.assertIn("js.src = CHAT.script; js.async = true; js.onload = mountDock;", self.js, "async: the script never holds the page")
        self.assertIn("} catch (e) { /* no chat */ }", self.js)
        self.assertIn("function dockShown() { try { if (dock) dock.shown(screen === 'home'); } catch (e) { /* the dock is its own */ } }", self.js)
        self.assertEqual(self.js.count("dockShown();"), 2, "after the mount, and where the screen changes")

    def test_it_makes_a_link_and_a_script_and_never_writes_markup(self):
        self.assertIn("css = el('link'), js = el('script');", self.js)
        self.assertNotRegex(code_of(self.js), r"innerHTML|outerHTML|insertAdjacentHTML|document\.write")


class TheFolderAndTheScript(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.js = read("web", "home", "chat.js")
        cls.code = code_of(cls.js)

    def test_it_is_under_its_budgets(self):
        raw = zipped = 0
        for name in FILES:
            with open(os.path.join(HOME, name), "rb") as f:
                data = f.read()
            raw += len(data)
            zipped += len(gzip.compress(data, 9))
        self.assertLess(raw, CHAT_BUDGET, "chat.js and chat.css are %d bytes" % raw)
        self.assertLess(zipped, CHAT_GZIP_BUDGET, "and %d bytes gzipped" % zipped)

    def test_the_script_is_plain_es5(self):
        for token in (r"\bconst\b", r"\blet\b", r"=>", r"`", r"\bimport\b", r"\bclass\b", r"\basync\b", r"\bawait\b"):
            self.assertIsNone(re.search(token, self.code), token)
        self.assertIn("'use strict';", self.js)
        self.assertNotRegex(self.code, r"\b(var|function)\s+(near|far)\b|\b(near|far)\s*=", "Windows has macros of these names")

    def test_nothing_that_the_hub_says_becomes_markup_a_link_or_code(self):
        self.assertNotRegex(self.code, r"innerHTML|outerHTML|insertAdjacentHTML|document\.write|\beval\b|new Function|srcdoc|createContextualFragment|DOMParser|setTimeout\(\s*['\"]")
        self.assertNotRegex(self.code, r"\.href\b|\.src\b|setAttribute\(\s*['\"](href|src|style|on\w+|srcdoc)")
        made = sorted(set(re.findall(r"\bel\('(\w+)'", self.code)))
        self.assertEqual(made, ["bdi", "button", "div", "form", "h2", "i", "input", "p", "section", "span"], "no a, img, script, iframe, style or link: the dock makes text and boxes")
        self.assertEqual(self.code.count("createElement"), 1, "one place makes elements")
        self.assertIn("if (text !== undefined) e.textContent = text;", self.js)

    def test_it_keeps_nothing_in_the_browser_and_asks_for_nothing_else(self):
        self.assertNotRegex(self.code, r"localStorage|sessionStorage|indexedDB|document\.cookie|XMLHttpRequest|navigator\.|importScripts|\bimport\b|postMessage|BroadcastChannel")
        self.assertNotRegex(self.code, r"https?://", "no other site, no address but the page's own host")
        self.assertEqual(len(re.findall(r"\bfetch\b", self.code)), 2, "the one call is `var f = env.fetch || win.fetch;`: a POST of the page to the hub's logout and forget, and nothing else")
        self.assertIn("var f = env.fetch || win.fetch;", self.js)
        self.assertIn("f.call(win, path, { method: 'POST', credentials: 'same-origin', cache: 'no-store', redirect: 'error' })", self.js)
        self.assertEqual(sorted(re.findall(r"leave\((LOGOUT|FORGET)\)", self.code)), ["FORGET", "LOGOUT"])
        self.assertIn("var LOGIN = '/hub/login?next=', LOGOUT = '/hub/logout', FORGET = '/hub/forget';", self.js)
        self.assertNotRegex(self.code, r"discord\.com|Authorization|access_token|location\.(search|hash|href)|\.href\b", "no token, no address of Discord, nothing read from the address")
        self.assertEqual(len(re.findall(r"location\.assign\(", self.code)), 1, "the one navigation: to the hub's login page")
        self.assertEqual(self.code.count("/hub/ws"), 1)
        self.assertIn("var PATH = '/hub/ws', VERSION = 1;", self.js)
        self.assertIn("(loc.protocol === 'https:' ? 'wss://' : 'ws://') + loc.host + PATH", self.js, "the page's own site, secure where the page is")

    def test_it_opens_no_socket_when_it_is_loaded_only_when_it_is_mounted(self):
        self.assertNotRegex(self.code, r"new\s+WebSocket", "the socket class is the page's (win.WebSocket), made in go() only")
        self.assertEqual(len(re.findall(r"new Socket\(", self.code)), 1)
        self.assertEqual(self.code.count("        paint();\n        go();\n        win.addEventListener('resize'"), 1, "go() runs when the dock is mounted, never when the file is loaded")
        self.assertNotRegex(self.code, r"\n    go\(|\n\}\)\(.*go\(")
        self.assertIn("root.AntsHomeChat = api;", self.js)

    def test_the_limits_are_the_hubs_and_the_pace_is_below_it(self):
        self.assertIn("var LIMIT = { name: 32, line: 200, feed: 100, history: 50, frame: 120000, bad: 30, field: 400, count: 1e9, here: 500, said: 2 };", self.js)
        self.assertIn("var MS = { ping: 25000, answer: 30000, connect: 15000, stable: 30000, notice: 6000, regain: 1000, flood: 3000, pending: 8000 };", self.js)
        self.assertIn("var BACKOFF = { min: 1000, max: 60000, low: 0.5, high: 1.5 };", self.js, "the wait is spread from half a step to one and a half, within 1 to 60 s")
        self.assertIn("var PACE = { burst: 2, every: 2500, same: 31000 };", self.js, "below the hub's: 3 at once, one every 2 s, the same text twice within 30 s")

    def test_the_words_are_the_agreed_ones(self):
        for words in ("Chat is offline", "reconnecting…", "Chat is read-only for now", "Slow down a little", "Log in with Discord to chat", "Global chat"):
            self.assertIn(words, self.js)

    def test_the_login_place_is_made_hidden_and_shown_only_for_a_named_visitor_where_the_hub_has_a_login(self):
        self.assertIn("slot.id = 'gchat-login-slot'; slot.setAttribute('data-slot', 'login'); slot.hidden = true;", self.js)
        self.assertIn("slot.hidden = !(live && you && login);", self.js, "the account line: the welcome names the visitor and the hub has a login; nothing else shows it")
        self.assertIn("chip.hidden = !(live && login);", self.js, "the bar's chip: only where the hub offers a login, and not while the hub is not there")
        self.assertIn("var ask = live && !you && login;", self.js, "the field is off and asks for the login only where one is offered")
        self.assertIn("send.hidden = !you; enter.hidden = !!you;", self.js, "Send for a named visitor, Log in for the others, never both")
        self.assertEqual(self.code.count("login = m.login === true;"), 1, "whether there is a login is what the hub says, nothing else")

    def test_the_log_and_the_controls_have_their_names_and_the_dock_its_focus_handling(self):
        for needle in ("log.setAttribute('role', 'log'); log.setAttribute('aria-live', 'polite');", "log.setAttribute('tabindex', '0');", "note.setAttribute('role', 'status');",
                       "field.setAttribute('aria-label', TEXT.field);", "fold.setAttribute('aria-controls', 'gchat-body');", "fold.setAttribute('aria-expanded', out ? 'true' : 'false');",
                       "box.setAttribute('aria-labelledby', 'gchat-title');", "e.key === 'Escape' && open", "e.preventDefault()"):
            self.assertIn(needle, self.js, needle)


class TheStyle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.css = read("web", "home", "chat.css")
        cls.home_css = read("web", "home", "home.css")
        cls.rules = re.findall(r"([^{}]+)\{([^{}]*)\}", re.sub(r"/\*.*?\*/", "", cls.css, flags=re.S))

    def rule(self, selector):
        return next(body for sel, body in self.rules if " ".join(sel.split()) == selector)

    def test_the_panel_is_380_by_380_in_the_lower_left_corner_and_folds_to_its_header(self):
        body = self.rule(".gc")
        for declaration in ("position: absolute;", "left: 24px;", "bottom: 22px;", "width: 380px;", "height: 380px;", "z-index: 6;"):
            self.assertIn(declaration, body)
        self.assertLess(6, 60, "the Menu (home.css: z-index 60) stays above the dock")
        self.assertIn("z-index: 60;", self.home_css)
        self.assertIn("height: auto;", self.rule('.gc[data-open="false"]'))
        self.assertIn("min-height: 58px;", self.rule(".gc-head"))

    def test_a_phone_and_a_short_window_have_a_strip_of_62_px_that_opens_upward(self):
        self.assertIn("@media (max-width: 720px), (max-height: 500px) {", self.css)
        self.assertIn("(min-width: 721px) and (min-height: 501px)", self.home_css, "the desktop's breakpoints are Home's own, and the script's: 721 x 501")
        self.assertIn("(win.innerWidth || 0) >= 721 && (win.innerHeight || 0) >= 501", read("web", "home", "chat.js"))
        compact = re.search(r"@media \(max-width: 720px\), \(max-height: 500px\) \{(.*?)\n\}", self.css, re.S).group(1)
        self.assertIn('.gc[data-open="false"] { height: 62px; }', compact)
        self.assertIn("left: 16px; right: 16px; bottom: 12px;", compact)
        self.assertIn("height: min(490px, calc(100% - 200px));", compact, "under the bar of a phone (184 px)")
        self.assertIn(".gc { --gc-min: 200px; left: 16px;", compact, "the least height of the open panel (chat.js reads it: with the strips above Home there is less room, and then the strip shows instead)")
        self.assertIn('.gc[data-open="true"] { min-height: var(--gc-min); }', compact, "with the strips above Home there is less room: the open panel is never a sliver")
        self.assertIn(".gc[hidden] { display: none; }", self.css, "a window that has no room even for the strip shows nothing")
        self.assertIn('.gc[data-open="true"] { left: 10px; right: 10px; }', compact)
        side = re.search(r"@media \(max-height: 500px\) and \(orientation: landscape\) \{(.*?)\n\}", self.css, re.S).group(1)
        self.assertIn("width: 420px; height: min(286px, calc(100% - 76px)); }", side, "its top clears the bar of 62 px")
        self.assertIn(".gc, .gc[data-open=\"true\"] { --gc-min: 150px; left: 14px;", side, "the least height of the open panel sideways (chat.js reads it)")
        self.assertIn('.gc[data-open="true"] { min-height: var(--gc-min); }', side, "an open panel is never a sliver; a folded strip has no minimum")
        self.assertNotIn("min-height: 150px", self.css.replace("--gc-min: 150px", ""), "the least height is the property's: one number for the style and the script")
        self.assertIn('left: 14px; right: auto;', side)

    def test_the_title_of_a_folded_strip_is_the_short_one_and_the_long_one_stays_for_a_screen_reader(self):
        compact = re.search(r"@media \(max-width: 720px\), \(max-height: 500px\) \{(.*?)\n\}", self.css, re.S).group(1)
        long_rule = re.search(r'\.gc\[data-open="false"\] \.gc-long \{([^}]*)\}', compact).group(1)
        self.assertIn("clip: rect(0 0 0 0);", long_rule)
        self.assertNotRegex(long_rule, r"display: *none|visibility: *hidden")
        self.assertIn(".gc-short, .gc-peek { display: none; }", self.css)

    def test_every_target_is_at_least_44_px_and_the_field_and_send_48(self):
        heights = {}
        for selector, body in self.rules:
            for m in re.finditer(r"min-height: (\d+)px", body):
                heights.setdefault(" ".join(selector.split()), []).append(int(m.group(1)))
        for selector in (".gc-fold", ".gc-input", ".gc-send", ".gc-head"):
            self.assertIn(selector, heights, selector)
        for selector, values in heights.items():
            for value in values:
                self.assertGreaterEqual(value, 44, "%s has a min-height of %d" % (selector, value))
        self.assertIn("min-width: 46px;", self.rule(".gc-fold"))
        self.assertGreaterEqual(heights[".gc-input"][0], 48)
        self.assertGreaterEqual(heights[".gc-send"][0], 48)
        self.assertNotRegex(self.css, r"(?<!min-)height: [1-3]\d px; *}", "no fixed height under 40 px on a control")

    def test_nothing_scrolls_sideways_and_the_log_scrolls_inside_itself(self):
        self.assertNotRegex(self.css, r"overflow-x: *(scroll|auto)")
        log = self.rule(".gc-log")
        self.assertIn("overflow-y: auto;", log)
        self.assertIn("overscroll-behavior: contain;", log)
        self.assertIn("overflow-wrap: anywhere;", self.rule(".gc-line"))
        self.assertIn("min-width: 0;", self.rule(".gc-input"))

    def test_a_narrow_or_a_low_window_gives_the_log_its_room(self):
        self.assertIn("@media (max-width: 359px) { .gc-head { gap: 6px; } .gc-title { font-size: 19px; white-space: nowrap; } .gc-online { gap: 6px; font-size: 13px; } }", self.css, "the heading and the count on one line at 320 px")
        self.assertIn("@media (max-width: 720px) and (max-height: 640px), (max-height: 500px) { .gc-guest { display: none; } }", self.css, "with notices above Home, or sideways, the guest line gives its place to the log")

    def test_the_input_is_16_px_so_that_a_phone_does_not_zoom_into_it(self):
        self.assertIn("font-size: 16px;", self.rule(".gc-input"))

    def test_motion_stops_for_a_person_who_asks(self):
        self.assertIn("@media (prefers-reduced-motion: reduce) { .gc-dot { animation: none; } }", self.css)
        self.assertIn("animation: gc-pulse", self.css)

    def test_the_card_that_says_that_the_game_is_opening_is_above_the_dock_and_the_summary_place_is_not_seen(self):
        card = re.search(r"\.hopen \{[^}]*z-index: (\d+);", self.home_css)
        self.assertGreater(int(card.group(1)), int(re.search(r"z-index: (\d+);", self.rule(".gc")).group(1)), "an open panel does not hide it")
        news = self.rule(".gc-news")
        self.assertIn("clip: rect(0 0 0 0);", news)
        self.assertNotRegex(news, r"display: *none|visibility: *hidden", "a screen reader reads it")

    def test_the_login_in_the_bar_and_in_the_dock(self):
        self.assertIn("@media (max-width: 720px), (max-height: 500px) { .hbar .hlogin { padding: 0; } .hlogin .gc-cn { display: none; } }", self.css, "on a phone, held either way, the chip is the mark or the circle only (46 x 46, as drawn): the breakpoints are the dock's")
        name = self.rule(".gc-an")
        self.assertIn("overflow-wrap: anywhere;", name)
        self.assertIn("min-width: 0;", name)
        cut = self.rule(".gc-cn")
        for declaration in ("overflow: hidden;", "text-overflow: ellipsis;", "white-space: nowrap;", "max-width: 11em;"):
            self.assertIn(declaration, cut, "a long name is cut in the bar and does not push the Menu button off the bar")
        for selector in (".gc-link", ".gc-forget"):
            self.assertRegex(self.rule(selector), r"min-height: (4[4-9]|[5-9]\d)px;", selector)
        self.assertIn("min-width: 44px;", self.rule(".gc-link"))
        self.assertIn("-webkit-mask: var(--dc) no-repeat center / contain; mask: var(--dc) no-repeat center / contain;", self.css, "the mark takes the color of the text around it")

    def test_the_hidden_parts_are_hidden_even_where_the_style_gives_them_a_display(self):
        self.assertIn(".gc-form[hidden], .gc-guest[hidden], .gc-slot[hidden] { display: none; }", self.css)
        self.assertIn(".gc-unread[hidden], .gc-online[hidden] { display: none; }", self.css)

    def test_nothing_is_loaded_from_another_site_and_there_is_no_picture(self):
        self.assertNotRegex(self.css, r"https?://|@import")
        self.assertEqual(re.findall(r"url\(([^)]*)\)", self.css), [re.search(r"url\((\"data:image/svg\+xml,[^\"]*\")\)", self.css).group(1)], "the mark of Discord, drawn as data: no other picture, no file")
        self.assertNotRegex(self.css, r"url\(\"(?!data:image/svg\+xml,)")

    def test_a_link_that_is_down_dims_the_log(self):
        self.assertIn('.gc[data-state="offline"] .gc-log { filter: saturate(.5) brightness(.94); }', self.css)

    def test_the_mod_mark_and_the_visitor_s_own_name_are_seen(self):
        self.assertIn(".gc-me .gc-who { color: var(--gold); }", self.css)
        self.assertRegex(self.rule(".gc-badge"), r"background: var\(--gold\);")


class TheWayIntoTheImage(unittest.TestCase):
    def test_the_dockerfile_copies_the_whole_folder(self):
        self.assertIn("COPY web/home/ /usr/share/nginx/html/home/\n", read("Dockerfile"))

    def test_the_local_web_build_packs_the_whole_folder(self):
        self.assertIn('cp -f "${SCRIPT_DIR}/web/home"/* "${DIST_DIR}/home/"', read("build_web.sh"))

    def test_the_ci_looks_for_the_two_files_in_the_image(self):
        self.assertIn(" home/home.js home/home.css home/chat.js home/chat.css ", read(".github", "workflows", "ci.yml"))

    def test_the_documentation_is_where_the_screens_are(self):
        doc = read("docs", "HOME_SCREEN.md")
        self.assertIn("## The chat dock", doc)
        for needle in ("`chat.js` and `chat.css` load once, after the first paint", "no other page loads them or opens a hub socket", "#gchat-login-slot", "Chat is read-only for now", "Chat is offline", "1 s, doubling to 60 s", "100 lines at most",
                       "two at once, then one every 2.5 s", "nothing the chat does can block Home, the Room or START", "tests/scripts/test_web_home_chat.py"):
            self.assertIn(needle, doc, needle)


@unittest.skipUnless(NODE, "node is not installed: the invisible characters were NOT compared with the hub's (tests/scripts/test_web_home_chat.py)")
class TheInvisibleCharactersAreTheHubs(unittest.TestCase):
    """the page removes what the hub removes (hub/text.py: the control and format characters, the fillers), as a second wall: a name of nothing that shows is "?"; the two lists are kept in step here"""
    SCRIPT = (
        "const Chat = require(process.argv[1]); const list = JSON.parse(require('fs').readFileSync(process.argv[2], 'utf8'));"
        "const out = list.filter((x) => { const c = String.fromCodePoint(x.cp); const t = Chat.plain('a' + c + 'b', x.strict); return !x.keep ? (t !== 'ab' && t !== 'a b') : t !== 'a' + c + 'b'; });"
        "console.log(JSON.stringify(out.map((x) => x.cp.toString(16))));")

    def run_list(self, items):
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "list.json")
            with open(path, "w", encoding="utf-8") as f:
                json.dump(items, f)
            done = subprocess.run([NODE, "-e", self.SCRIPT, os.path.join(HOME, "chat.js"), path], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stderr[-2000:])
        return json.loads(done.stdout)

    def test_every_control_format_and_filler_character_that_the_hub_removes_is_removed_by_the_page(self):
        sys.path.insert(0, REPO)
        try:
            from hub import text as hub_text
        finally:
            sys.path.remove(REPO)
        every = [cp for cp in range(0x110000) if not 0xd800 <= cp <= 0xdfff and (unicodedata.category(chr(cp)) in ("Cc", "Cf") or chr(cp) in hub_text.FILLERS)]
        self.assertGreater(len(every), 200)
        self.assertEqual(self.run_list([{"cp": cp} for cp in every]), [], "code points (hex) that hub/text.py removes and web/home/chat.js does not: add them to HIDDEN")

    def test_the_variation_selectors_leave_a_name_and_stay_in_a_line(self):
        selectors = [cp for cp in range(0x110000) if not 0xd800 <= cp <= 0xdfff and "VARIATION SELECTOR" in unicodedata.name(chr(cp), "")]
        self.assertGreater(len(selectors), 250)
        self.assertEqual(self.run_list([{"cp": cp, "strict": True} for cp in selectors]), [], "a name made of these shows nothing: SELECTORS")
        self.assertEqual(self.run_list([{"cp": cp, "keep": True} for cp in selectors]), [], "a line keeps them: the heart that is a picture is a heart and a variation selector")


@unittest.skipUnless(NODE, "node is not installed: the chat dock was NOT run (tests/scripts/web_home_chat_check.js)")
class TheCodeRuns(unittest.TestCase):
    def test_the_dock_against_a_fake_hub_and_a_fake_clock(self):
        done = subprocess.run([NODE, CHECK, LOBBY], capture_output=True, text=True, timeout=300, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout[-4000:] + done.stderr[-2000:])
        self.assertRegex(done.stdout, r"home chat: \d+ checks, 0 failed")

    def test_the_dock_against_the_fake_hub_on_a_real_socket(self):
        done = subprocess.run([NODE, HUB_CHECK, LOBBY], capture_output=True, text=True, timeout=300, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout[-4000:] + done.stderr[-2000:])
        self.assertRegex(done.stdout, r"home chat on a real socket: \d+ checks, 0 failed")


class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/home_chat_mutants.json (the list that tools/mutate.py runs too, with `--keep-going --jobs 1`) on a COPY of the files, one at a time: each must make the checks fail, and the
    copy with nothing changed must pass"""
    FILES = (["web/lobby.html", "web/shapes.js", "web/home/home.js", "web/home/home.css", "web/home/chat.js", "web/home/chat.css", "tests/scripts/test_web_home_chat.py", "tests/scripts/web_home_chat_check.js",
              "tests/scripts/web_home_chat_hub_check.js", "tests/scripts/web_page_harness.js", "tests/scripts/fake_hub.js", "tests/scripts/mini_websocket.js", "Dockerfile", "build_web.sh", ".github/workflows/ci.yml",
              "docs/HOME_SCREEN.md", "hub/__init__.py", "hub/text.py", "web/matches.html", "web/live.html", "web/match.html", "web/legal.html", "web/mapmaker.html", "web/shell.html", "web/admin.html"]
             + ["web/front/" + n for n in ("lobby_rules.js", "lobby_net.js", "lobby_chat.js", "lobby_chat.css", "rematch.js", "game_modes.js", "game_type.js", "map_picker.js", "map_maker_link.js")])

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "home_chat_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_home_chat_mutants.")
        for name in cls.FILES:
            os.makedirs(os.path.dirname(os.path.join(cls.folder, *name.split("/"))), exist_ok=True)
            shutil.copy(os.path.join(REPO, *name.split("/")), os.path.join(cls.folder, *name.split("/")))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.folder, ignore_errors=True)

    def run_command(self, command):
        done = subprocess.run(command, shell=True, cwd=self.folder, capture_output=True, text=True, timeout=600)
        return done.returncode, done.stdout + done.stderr

    def test_the_copy_with_nothing_changed_passes(self):
        for command in sorted({m["test"] for m in self.spec["mutants"]}):
            status, out = self.run_command(command)
            self.assertEqual(status, 0, command + "\n" + out[-3000:])

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
