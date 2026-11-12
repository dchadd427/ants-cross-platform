#!/usr/bin/env python3
"""The chat dock of Home (web/home/chat.js and chat.css, docs/HOME_SCREEN.md "The chat dock"; run by ./run_tests.sh --fast and by the CI).

Home carries the site's global chat in a panel in its lower-left corner (a strip along the bottom on a phone). It reads and writes the chat hub, one WebSocket at /hub/ws (docs/HUB.md), and every word it shows
is text from strangers. It is loaded by web/home/home.js, after the first paint and only when Home is shown, so that it can never hold up Home, the Room or START. What this file checks:

  - the page and the other pages: web/lobby.html does not link the dock, no other page names it or the hub's door, and the only script that knows the address /hub/ws is chat.js;
  - the loader in home.js: the two files, once, after the first paint, only when Home shows, mounted when BOTH have come, and whatever goes wrong with them stays with them;
  - the folder: its own budgets (raw and gzipped), plain ES5, no markup made from text, no link, no storage, no other site, nothing that opens a socket by itself;
  - the style: the panel along the bottom of Home (folded to its header), the Chat view of a phone and of a phone held sideways, every target at least 44 px (the
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
LOGIN_LINK = 'href="/hub/login?next=home"'      # (the one thing that a page may say of the hub in its markup: the plain link of the name card that logs a guest in; tests/scripts/test_web_name.py)
sys.path.insert(0, os.path.join(REPO, "tools"))
from size_limits import CHAT_BUDGET, CHAT_GZIP_BUDGET  # noqa: E402  (every byte budget: tools/size_limits.py)


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
            self.assertNotIn(word, self.page.replace(LOGIN_LINK, ""), word)        # (but for the plain link of the name card that logs a guest in)
        self.assertEqual(self.page.count('<script src="home/home.js"></script>'), 1)
        self.assertFalse([src for src in re.findall(r'<script src="([^"]*)"', self.page) if "chat" in src and "lobby_chat" not in src], "the page's scripts are the ones that it had: the dock is not one of them")

    def test_no_other_file_of_the_site_names_the_dock_or_the_hubs_door(self):
        naming_the_dock, naming_the_door, naming_the_socket = set(), set(), set()
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
                if re.search(r"/hub/ws|/hub\b", text.replace(LOGIN_LINK, "")):
                    naming_the_door.add(rel)
                if re.search(r"/hub/ws", text):
                    naming_the_socket.add(rel)
        self.assertEqual(sorted(naming_the_dock), ["web/home/chat.css", "web/home/chat.js", "web/home/home.js"], "the front page's own script asks for the dock; no other page does")
        self.assertEqual(sorted(naming_the_socket), ["web/home/chat.js"], "only the dock's script knows the address of the hub's socket: a page that opens a socket to it is a page that has a chat")
        self.assertEqual(sorted(naming_the_door), ["web/front/player_name.js", "web/home/chat.js"], "the one other script that names the hub is the one that asks for the name to play under: its one page, GET /hub/me (tests/scripts/test_web_player_name.py)")

    def test_the_game_server_and_the_game_never_open_the_hub(self):
        for rel in (("web", "shell.html"), ("web", "front", "lobby_net.js"), ("web", "front", "lobby_chat.js")):
            self.assertNotRegex(read(*rel).replace(LOGIN_LINK, ""), r"/hub\b", rel[-1])


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
        self.assertIn("function mountDock() { if (--parts) return; try { if (win.AntsHomeChat) { dock = win.AntsHomeChat.mount({ doc: doc, win: win, host: host, chip: login, screen: function () { return screen; } }); inert(held); dockShown(); pick(tab); wantPlayers(); wantTalk(); } } catch (e) { dock = null; } }", self.js)
        self.assertIn("[bar, rbar, tabs, dock && dock.root, players && players.root].forEach(function (n) { if (n) {", self.js, "a dialog over the page, or a hand-off, takes the dock's keys and presses too: the focus does not go behind the modal")
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

    def test_it_lets_the_players_list_listen_and_speak_through_its_socket_and_nothing_else_opens_one(self):
        for part in ("tap: function (fn) { taps.push(fn); if (hello) tell1(fn, 'live', hello); else if (state === 'offline') tell1(fn, 'lost'); },", "function out(obj) { if (!sock || sock.readyState !== 1 || !welcomed) return false;", "send: out,",
                     "else if (m.t === 'who') tapped('who', m);", "tapped('live', m);", "tapped('lost');", "tapped('shown', !!on);",
                     "function tell1(fn, kind, m) { try { fn(kind, m); } catch (e) { if (env.error) env.error(e); } }"):
            self.assertIn(part, self.js, part)
        self.assertEqual(self.code.count("new Socket("), 1, "one socket for the page, whoever speaks through it")

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
        self.assertEqual(self.code.count("        paint();\n        go();\n        return {"), 1, "go() runs when the dock is mounted, never when the file is loaded")
        self.assertNotRegex(self.code, r"\n    go\(|\n\}\)\(.*go\(")
        self.assertIn("root.AntsHomeChat = api;", self.js)

    def test_the_limits_are_the_hubs_and_the_pace_is_below_it(self):
        self.assertIn("var LIMIT = { name: 32, line: 200, feed: 100, history: 50, frame: 120000, bad: 30, field: 400, count: 1e9, here: 500, said: 2 };", self.js)
        self.assertIn("var MS = { ping: 25000, answer: 30000, connect: 15000, stable: 30000, notice: 6000, regain: 1000, flood: 3000, pending: 8000 };", self.js)
        self.assertIn("var BACKOFF = { min: 1000, max: 60000, low: 0.5, high: 1.5 };", self.js, "the wait is spread from half a step to one and a half, within 1 to 60 s")
        self.assertIn("var PACE = { burst: 2, every: 2500, same: 31000 };", self.js, "below the hub's: 3 at once, one every 2 s, the same text twice within 30 s")


    def test_the_switch_of_the_name_is_one_frame_and_the_page_sends_nothing_else_of_its_own_about_it(self):
        self.assertEqual(self.code.count("{ t: 'pick'"), 1, "one place writes the frame")
        self.assertIn("pick: you.pick === 'display' ? 'username' : 'display'", self.js, "the other of the two words, never the one that is shown")
        self.assertEqual(sorted(re.findall(r"out\(\{ t: '(\w+)'", self.code)), ["pick"], "the dock's own frames (a line and a ping are written where they were); the players list's come through send()")
        self.assertIn("else if (m.t === 'err' && m.code === 'pick') tell('failed');", self.js)
        self.assertNotIn("'pick'", re.search(r"var SAID = \[(.*?)\];", self.code).group(1), "a refused switch is no line's refusal")
        self.assertIn("function mine(e) { return !!you && (you.k ? e.k === you.k : e.who === you.name); }", self.js, "a line is the visitor's by the hub's key; by the name only for a hub that sends none")
        self.assertNotIn("own = !!you && e.who", self.code, "no other place tells a line's author by its name")

    def test_the_words_of_the_switch_are_these(self):
        for words in ("shows: 'Chat shows your ', names: ['display name', 'username'], use: 'Show my '", "pickNote.textContent = TEXT.shows + TEXT.names[a] + '.'", "pickBtn.textContent = TEXT.use + TEXT.names[1 - a] + ': ' + you.other;"):
            self.assertIn(words, self.js, words)

    def test_a_name_is_cleaned_in_one_place_and_the_username_of_a_line_and_the_key_are_text_like_it(self):
        self.assertEqual(self.code.count("clip(plain("), 2, "one cleaner of a name (nameOf) and one of a line's text")
        for part in ("k: nameOf(m.k), u: nameOf(m.u)", "k: nameOf(y.k)", "nameOf(y.other)", "who: nameOf(m.who) || '?'"):
            self.assertIn(part, self.js, part)

    def test_the_words_are_the_agreed_ones(self):
        for words in ("Chat is offline", "reconnecting…", "Chat is read-only for now", "Slow down a little", "Log in with Discord to chat", "Global chat"):
            self.assertIn(words, self.js)

    def test_the_login_place_is_made_hidden_and_shown_only_for_a_named_visitor_where_the_hub_has_a_login(self):
        self.assertIn("slot.id = 'gchat-login-slot'; slot.setAttribute('data-slot', 'login'); slot.hidden = true;", self.js)
        self.assertIn("slot.hidden = !(live && you && login && menuOn);", self.js, "the account's dropdown: the welcome names the visitor, the hub has a login, and the chip has opened it; nothing else shows it")
        self.assertIn("chip.hidden = !(live && login);", self.js, "the bar's chip: only where the hub offers a login, and not while the hub is not there")
        self.assertIn("var ask = live && !you && login;", self.js, "the field is off and asks for the login only where one is offered")
        self.assertIn("send.hidden = !you || barred; enter.hidden = !!you;", self.js, "Send for a named visitor who may write, Log in for the others, never both")
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

    def test_the_panel_is_along_the_bottom_of_home_under_the_games_and_the_players_and_folds_to_its_header(self):
        body = self.rule(".gc")
        for declaration in ("order: 5;", "flex: none;", "margin: 0 24px 22px;", "height: clamp(160px, 28vh, 260px);"):
            self.assertIn(declaration, body)
        self.assertNotIn("position: absolute", body, "the panel is a part of Home's column, as wide as the games and the players together: nothing lies over it and it lies over nothing")
        self.assertNotIn("z-index", body)
        self.assertIn("z-index: 60;", self.home_css, "the Menu stays above the dock")
        self.assertIn("height: auto;", self.rule('.gc[data-open="false"]'))
        self.assertIn("min-height: 50px;", self.rule(".gc-head"))
        self.assertIn("#home { flex: 1 1 auto; min-height: 0; display: flex; flex-direction: column;", self.home_css, "the column that the dock is a part of")

    def test_the_field_has_the_row_under_the_log_in_a_wide_window(self):
        wide = re.search(r"@media \(min-width: 701px\), \(max-width: 700px\) and \(max-height: 500px\) \{(.*?)\n\}", self.css, re.S).group(1)
        self.assertIn("grid-template-columns: minmax(0, 1fr) auto;", wide)
        self.assertIn(".gc-form { grid-area: 3 / 1 / 4 / -1; }", wide)
        self.assertNotIn(".gc-slot", wide, "the account line is not in the panel any more (the chip's dropdown has it)")
        self.assertIn(".gc-log, .gc-note, .gc-guest { grid-column: 1 / -1; }", wide)

    def test_a_phone_has_the_chat_as_its_own_view_open_and_without_a_fold_and_a_short_window_opens_it_folded(self):
        self.assertIn("(win.innerWidth || 0) >= 701 && (win.innerHeight || 0) >= 601", read("web", "home", "chat.js"), "the script opens the panel for a window of 701 x 601 or more (home.js has the same limits)")
        phone = re.search(r"@media \(max-width: 700px\) \{(.*?)\n\}", self.css, re.S).group(1)
        self.assertIn(".gc { flex: 1 1 auto; height: auto; min-height: 0; margin: 4px 12px 8px; overflow: hidden; }", phone, "the view fills the page between the bar and the strip")
        self.assertIn(".gc-fold { display: none; }", phone, "home.js opens it with its view and shuts it with the others: no fold to leave it shrunk")
        self.assertNotIn("--gc-min", self.css, "the panel is never put away for want of room: it is a part of the column, and the games take what is left")
        self.assertIn("@media (max-height: 500px) and (orientation: landscape) { .gc-line { font-size: 16px; } .gc-body { padding-bottom: 10px; } }", self.css)
        self.assertIn("@media (max-width: 700px) and (max-height: 500px) { .gc-head { display: none; } .gc-form { margin-top: 6px; } .gc-body { padding-top: 6px; } .gc-log { padding: 2px 12px; gap: 2px; } .gc-input, .gc-send { min-height: 44px; } }", self.css, "a phone held sideways: the Chat view has about 150 px, the heading gives its place to the log (the account line stays, on the field's row) and nothing of the panel reaches over the strip")

    def test_the_header_stays_when_the_panel_is_folded_and_the_title_is_one_word_group(self):
        self.assertNotIn(".gc-short", self.css)
        self.assertNotIn(".gc-peek", self.css)
        self.assertNotIn(".gc-long", self.css)

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
        self.assertIn("@media (max-height: 700px) { .gc-guest { display: none; } }", self.css, "in a short window the guest line gives its place to the log")

    def test_the_input_is_16_px_so_that_a_phone_does_not_zoom_into_it(self):
        self.assertIn("font-size: 16px;", self.rule(".gc-input"))

    def test_motion_stops_for_a_person_who_asks(self):
        self.assertIn("@media (prefers-reduced-motion: reduce) { .gc-dot { animation: none; } }", self.css)
        self.assertIn("animation: gc-pulse", self.css)

    def test_the_card_that_says_that_the_game_is_opening_is_above_the_dock_and_the_summary_place_is_not_seen(self):
        card = re.search(r"\.hopen \{[^}]*z-index: (\d+);", self.home_css)
        self.assertGreaterEqual(int(card.group(1)), 8, "the card is a layer over the page's columns, the dock's panel among them (it has none of its own)")
        self.assertNotIn("z-index", self.rule(".gc"))
        news = self.rule(".gc-news")
        self.assertIn("clip: rect(0 0 0 0);", news)
        self.assertNotRegex(news, r"display: *none|visibility: *hidden", "a screen reader reads it")

    def test_the_login_in_the_bar_and_in_the_dock(self):
        self.assertIn("@media (max-width: 1279px), (max-height: 500px) { .hbar .hlogin { padding: 0; } .hlogin .gc-cn { display: none; } }", self.css, "on a phone, held either way, and in any window under 1280 px wide, the chip is the mark or the circle only (46 x 46, as drawn; the name needs the width of a desktop: docs/HOME_SCREEN.md, \"The top bar\")")
        name = self.rule(".gc-an")
        self.assertIn("overflow-wrap: anywhere;", name)
        self.assertIn("min-width: 0;", name)
        cut = self.rule(".gc-cn")
        for declaration in ("overflow: hidden;", "text-overflow: ellipsis;", "white-space: nowrap;", "max-width: 11em;"):
            self.assertIn(declaration, cut, "a long name is cut in the bar and does not push the Menu button off the bar")
        for selector in (".gc-out", ".gc-forget"):
            self.assertRegex(self.rule(selector), r"min-height: (4[4-9]|[5-9]\d)px;", selector)
        slot = self.rule(".gc-slot")
        for declaration in ("position: fixed;", "z-index: 60;", "overflow-y: auto;", "width: min(300px, calc(100vw - 20px));"):
            self.assertIn(declaration, slot, "the account's dropdown stands over the page under the chip, and scrolls inside itself where the window is short (a phone held sideways)")
        for gone in (".gc-link", ".gc-more", ".gc-menu"):
            self.assertNotIn(gone + " {", self.css, "the account line of the panel and its More button are gone: " + gone)
        self.assertIn("-webkit-mask: var(--dc) no-repeat center / contain; mask: var(--dc) no-repeat center / contain;", self.css, "the mark takes the color of the text around it")

    def test_the_switch_is_a_target_and_the_username_of_a_line_is_not_drawn(self):
        self.assertRegex(self.rule(".gc-pick"), r"min-height: (4[4-9]|[5-9]\d)px;")
        self.assertNotRegex(self.css, r"\.gc-u\b", "the username of a moderator's copy is not drawn: no style for it")
        self.assertNotIn("'gc-u'", read("web", "home", "chat.js"), "... and no element for it")

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


def luminance(color):
    channels = [int(color.lstrip("#")[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    channels = [c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4 for c in channels]
    return 0.2126 * channels[0] + 0.7152 * channels[1] + 0.0722 * channels[2]


def contrast(a, b):
    high, low = sorted((luminance(a), luminance(b)), reverse=True)
    return (high + 0.05) / (low + 0.05)


class TheRoles(unittest.TestCase):
    """the page's side of the roles (docs/HUB.md "Roles" and "Moderation"): what it reads, what it draws, the words, and what it never does with a person's text; the behavior is run in
    tests/scripts/web_home_chat_check.js (the fake clock) and web_home_chat_hub_check.js (a real socket)"""
    @classmethod
    def setUpClass(cls):
        cls.js = read("web", "home", "chat.js")
        cls.code = code_of(cls.js)
        cls.css = read("web", "home", "chat.css")
        cls.players_css = read("web", "home", "players.css")
        cls.home_css = read("web", "home", "home.css")
        cls.lobby = read("web", "lobby.html")

    def rule(self, css, selector):
        rules = re.findall(r"([^{}]+)\{([^{}]*)\}", re.sub(r"/\*.*?\*/", "", css, flags=re.S))
        return next(body for sel, body in rules if " ".join(sel.split()) == selector)

    def token(self, name):
        return re.search(r"--%s: (#[0-9a-f]{6})" % name, self.lobby).group(1)

    def test_the_red_is_the_red_of_the_leave_buttons_and_the_gold_is_the_pages_gold(self):
        leave = re.search(r"\.leave \{[^}]*var\(--sheen\) #([0-9a-f]{6})", self.home_css).group(1)
        for css, selector in ((self.css, ".gc-badge-admin"), (self.css, ".gc-cr-admin"), (self.players_css, ".pl-badge-admin")):
            self.assertIn("background: #%s;" % leave, self.rule(css, selector), selector)
        for css, selector in ((self.css, ".gc-badge"), (self.css, ".gc-cr"), (self.players_css, ".pl-badge, .pl-tag")):
            self.assertIn("background: var(--gold);", self.rule(css, selector), selector)
        self.assertEqual(self.token("gold"), "#f6d058")

    def test_the_words_on_the_marks_can_be_read_the_color_is_never_the_only_sign(self):
        red = re.search(r"\.leave \{[^}]*var\(--sheen\) #([0-9a-f]{6})", self.home_css).group(1)
        self.assertGreaterEqual(contrast(self.token("cream"), "#" + red), 4.5, "ADMIN: the cream word on the red")
        self.assertGreaterEqual(contrast("#1a1405", self.token("gold")), 4.5, "MOD: the dark word on the gold")
        for css, selector in ((self.css, ".gc-badge-admin"), (self.css, ".gc-cr-admin"), (self.players_css, ".pl-badge-admin")):
            self.assertIn("color: var(--cream);", self.rule(css, selector), selector)
            self.assertIn("box-shadow: ", self.rule(css, selector), selector + ": a light edge, because the red alone is dim against the dark panels")
        self.assertGreaterEqual(contrast("#e0707a", "#0a1016"), 3, "the edge of the red against the log")
        for css, selector in ((self.css, ".gc-badge"), (self.css, ".gc-cr"), (self.players_css, ".pl-badge, .pl-tag")):
            self.assertIn("text-transform: uppercase;", self.rule(css, selector), selector)
        for word in ("admin: 'admin'", "mod: 'mod'"):
            self.assertIn(word, self.js)
        self.assertIn("(admin ? TEXT.admin : TEXT.mod) + ' '", self.js, "a line's mark is a word")
        self.assertIn("chipRole.textContent = has ? (you.role === 'admin' ? TEXT.admin : TEXT.mod) : '';", self.js, "the chip's mark is a word")

    def test_the_quiet_line_and_the_private_answers_have_text_that_can_be_read(self):
        self.assertGreaterEqual(contrast("#9cc0b3", "#0a1016"), 4.5, "the hub's own line on the log")
        for color in ("#f4ece2", "#a8cfc0", "#b9d9cc"):
            self.assertGreaterEqual(contrast(color, "#0e1c18"), 4.5, color + " on the private answer")
        self.assertGreaterEqual(contrast("#f4ece2", "#1c140c"), 4.5, "the notice of a removal")
        self.assertIn("font-style: italic;", self.rule(self.css, ".gc-event"))
        self.assertIn("border-left: 3px solid", self.rule(self.css, ".gc-private"), "a private answer is set apart by more than its color")

    def test_the_role_is_the_hubs_word_read_in_two_places_and_nothing_else_makes_one(self):
        self.assertIn("function roleOf(role, mod) {", self.js)
        self.assertIn("return mod !== true ? 'none' : role === 'admin' ? 'admin' : 'mod';", self.js, "admin only where the hub also says moderator; any other word is mod, never admin")
        self.assertIn("var name = nameOf(y.name), role = y.role === 'admin' || y.role === 'mod' || y.role === 'none' ? y.role : y.mod === true ? 'mod' : 'none';", self.js)
        self.assertIn("you = youOf(m.you);", self.js)
        self.assertEqual(self.code.count("var sys = "), 0)
        self.assertIn("sys = m.sys === true;", self.js, "the hub's own line is the hub's flag, exactly true")
        self.assertNotRegex(self.code, r"['\"]system['\"]", "no name makes a line the hub's: the page never names `system`")
        self.assertNotRegex(self.code, r"\bwas (kicked|banned)\b", "no text makes a line the hub's: the page never reads the words of the hub's line")
        self.assertIn("e.own = !e.sys && mine(e);", self.js)
        self.assertEqual(self.code.count("e.own = !e.sys && mine(e)"), 2, "the hub's own line is nobody's own line, in the history and live")

    def test_the_new_frames_are_read_and_the_live_one_reaches_the_players_list(self):
        for part in ("else if (m.t === 'you') onYou(m);", "else if (m.t === 'ok') onOk(m);", "else if (m.t === 'bans') onBans(m);", "else if (m.t === 'bye') onBye(m);", "tapped('you', m);",
                     "var next = you ? youOf(m.you) : null;", "you.role = next.role; you.mod = next.mod;", "SAID.indexOf(m.code) !== -1 || ABOUT.indexOf(m.code) !== -1"):
            self.assertIn(part, self.js, part)
        self.assertIn("var ABOUT = ['power', 'role', 'who'];", self.js)
        self.assertNotIn("'power'", re.search(r"var SAID = \[(.*?)\];", self.code).group(1), "a line is never refused with a command's code")
        self.assertIn("kind === 'you'", read("web", "home", "players.js"))

    def test_help_is_the_pages_own_and_the_commands_are_the_hubs(self):
        self.assertIn("if (word === '/help') { answer(helpOf(you.role)); return ''; }", self.js, "answered before anything is sent")
        send_at, help_at = self.js.index("sock.send(JSON.stringify({ t: 'say', text: text }))"), self.js.index("answer(helpOf(you.role))")
        self.assertLess(help_at, send_at)
        self.assertEqual(self.code.count("sock.send("), 3, "a ping, a line, and what the players list sends through out(): an answer is never sent")
        body = re.search(r"function answer\(lines\) \{.*?\n        \}", self.code, re.S).group(0)
        self.assertNotRegex(body, r"send|\.out\(|sock", "a private answer is a line of this screen only")
        words = re.findall(r"\['(/\w+)', ([12]), '", self.js)
        self.assertEqual(words, [("/mute", "1"), ("/unmute", "1"), ("/del", "1"), ("/kick", "1"), ("/ban", "2"), ("/unban", "2"), ("/bans", "2")], "the hub's seven commands and the rank of each (docs/HUB.md, Moderation)")

    def test_a_name_or_an_argument_is_never_markup_or_a_selector(self):
        self.assertNotRegex(self.code, r"querySelector|getElementsBy|getElementById|\.matches\(|new RegExp|RegExp\(", "no name and no argument is used to find an element or to make a pattern")
        self.assertNotRegex(self.code, r"innerHTML|outerHTML|insertAdjacentHTML|\beval\b|new Function")
        for kind in ("answer(", "bansOf(", "helpOf("):
            self.assertIn(kind, self.js)

    def test_the_words_are_the_agreed_ones(self):
        for words in ("You are not allowed to do that", "Could not confirm your role, try again in a moment", "Nobody by that name is here", "You are going too fast, try again in a moment", "That command is not right. Type /help to see the commands.",
                      "A moderator removed you from the chat. You can log in again.", "You are banned from the chat. You can still read it.", "Commands you can use. Only you can see this.", "There are no chat commands for you.",
                      "You are a moderator", "You are an admin", "Only you", "Nobody is banned.", "People who are banned"):
            self.assertIn(words, self.js, words)
        for word in ("let", "class", "import"):
            self.assertNotRegex(self.code, r"'[^']*\b%s\b[^']*'" % word, "the plain-ES5 test reads these words: keep them out of the texts")

    def test_a_removed_person_is_not_reconnected_by_a_loop_of_the_pages_own(self):
        self.assertEqual(self.code.count("new Socket("), 1)
        self.assertEqual(self.code.count("setTimeout(guard(go)"), 1, "the one retry is the usual one, with its backoff: a `bye` has none of its own")
        body = re.search(r"function onBye\(m\) \{.*?\n        \}", self.code, re.S).group(0)
        self.assertNotRegex(body, r"go\(|setTimeout|new Socket|lost\(|drop\(", "a bye only notes the removal: the hub closes the socket and the page's one retry does the rest")

    def test_the_chip_has_a_place_for_the_mark_and_the_style_hides_it_where_there_is_none(self):
        self.assertIn(".gc-yr[hidden], .gc-cr[hidden] { display: none; }", self.css)
        self.assertIn("chipRole = el('span', 'gc-cr'); chipRole.hidden = true;", self.js)
        self.assertIn("put(chip, [chipMark, chipAv, chipName, chipRole]);", self.js)
        self.assertIn(".pl-badge-admin", self.players_css)


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

    def test_every_name_that_the_hub_makes_comes_through_the_pages_cleaning_whole_in_a_line_and_as_a_username(self):
        sys.path.insert(0, REPO)
        sys.path.insert(0, os.path.join(REPO, "tests", "scripts"))
        try:
            from hub import text as hub_text
            import test_hub_text as table
        finally:
            sys.path.remove(REPO)
            sys.path.remove(os.path.join(REPO, "tests", "scripts"))
        import random
        rnd = random.Random(20261108)
        names = set(table.SHOWN_KEPT) | {hub_text.clean_shown(raw) for _, raw, _ in table.SHOWN_CASES}
        plain, awkward = table.alphabet()
        for _ in range(6000):
            raw = "".join(rnd.choice(plain + awkward) for _ in range(rnd.randint(2, 40)))
            names.add(hub_text.clean_shown(raw))
            names.add(hub_text.clean_shown("".join(chr(rnd.randrange(0x110000)) for _ in range(rnd.randint(2, 24)))))
        names.discard("")
        names = sorted(names)
        self.assertGreater(len(names), 1500)
        script = ("const Chat = require(process.argv[1]); const list = JSON.parse(require('fs').readFileSync(process.argv[2], 'utf8'));"
                  "const bad = list.filter((n) => { const m = Chat.lineOf({ id: 1, who: n, text: 'x', u: n }); return m.who !== n || m.u !== n || Chat.plain(n, true) !== n || Chat.clip(n, 32) !== n; });"
                  "console.log(JSON.stringify(bad));")
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "names.json")
            with open(path, "w", encoding="utf-8") as f:
                json.dump(names, f)
            done = subprocess.run([NODE, "-e", script, os.path.join(HOME, "chat.js"), path], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stderr[-2000:])
        self.assertEqual(json.loads(done.stdout), [], "names that the hub makes and that the page's own cleaning changes: the page would show another name than the hub sent")

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
    FILES = (["tools/size_limits.py", "web/lobby.html", "web/front/player_name.js", "web/shapes.js", "web/home/home.js", "web/home/home.css", "web/home/chat.js", "web/home/chat.css", "web/home/players.css", "web/home/players.js", "tests/scripts/test_web_home_chat.py", "tests/scripts/test_hub_text.py", "tests/scripts/web_home_chat_check.js",
              "tests/scripts/web_home_chat_hub_check.js", "tests/scripts/web_page_harness.js", "tests/scripts/fake_hub.js", "tests/scripts/mini_websocket.js", "Dockerfile", "build_web.sh", ".github/workflows/ci.yml",
              "docs/HOME_SCREEN.md", "hub/__init__.py", "hub/text.py", "web/matches.html", "web/live.html", "web/match.html", "web/legal.html", "web/mapmaker.html", "web/shell.html", "web/admin.html"]
             + ["web/front/" + n for n in ("lobby_rules.js", "lobby_net.js", "lobby_chat.js", "lobby_chat.css", "rematch.js", "game_modes.js", "game_type.js", "map_picker.js", "map_maker_link.js")])

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "home_chat_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import mutant_shard                     # (here and not at the top: the copies in which the mutants run have this file but not that one, and run the other classes)
        cls.mutants = mutant_shard.from_environment(cls.spec["mutants"])     # (all of them, or the units of tests/scripts/run_python_tests.py: ANTS_MUTANT_SHARD=K/N)
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
        for command in sorted({m["test"] for m in self.mutants}):
            status, out = self.run_command(command)
            self.assertEqual(status, 0, command + "\n" + out[-3000:])

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
