#!/usr/bin/env python3
"""The pages of the match history (run by ./run_tests.sh --fast and by the CI): the list of every match (web/matches.html), one match with its stats and replay (web/match.html), the matches that run now
(web/live.html), the code that they share (web/history_page.js), their stylesheet (web/matches.css), and the stats and the frame mode of the game page (web/shell.html, web/replay_stats.css).
docs/REPLAYS.md "The match pages" says what they do; the way to them from the other pages is tests/scripts/test_web_replay.py.

  - the shared code is RUN (node, when it is installed): the ids, the names of the seats, the sides of a record, the numbers of a seat, the state of the list and the words that it asks the server for (every
    one is one that docker/nginx.conf lets through), the pages, the texts of a failed look, the pictures of the maps (tests/scripts/web_history_check.js, web_history_pictures_check.js), and again on
    broken copies of the file, which the checks must refuse;
  - what needs no browser is read from the files: where the pages ask the server and with which words, that nothing a record carries becomes markup, that the replay's frame is made in one place
    only, when "Show replay" is pressed, and goes away with "Hide replay" (the rule is a function that is run on broken copies of the page too), that a section of the match page is one entry of a list,
    and that the game page asks for the stats only of a replay of the server's own store;
  - tests/scripts/test_web_matches.sh (opt-in) shows the same in a real browser against a fake door.
"""
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HELPERS_JS = os.path.join(REPO, "tests", "scripts", "web_history_check.js")
PICTURES_JS = os.path.join(REPO, "tests", "scripts", "web_history_pictures_check.js")
PAGES = ("matches.html", "match.html", "live.html")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def function_body(text, header):
    """The text of `function name(...) { ... }` that starts at `header`, braces matched."""
    start = text.index(header)
    depth, i = 0, text.index("{", start)
    first = i
    while True:
        depth += text[i] == "{"
        depth -= text[i] == "}"
        if depth == 0:
            return text[first:i + 1]
        i += 1


def lazy_problems(page):
    """What is wrong with the way that web/match.html makes the replay (an empty list: nothing). The rule: the game page is asked for in ONE place, setReplay, when the frame is made on opening; nothing
    makes the frame, sets its address or asks for a recording anywhere else, the markup holds no frame, and closing takes the frame away and empties its address."""
    problems = []
    markup = page[:page.index("<script>")]
    if re.search(r"<iframe", markup, re.I):
        problems.append("the markup holds a frame")
    script = page[page.index("<script>"):]
    if len(re.findall(r"el\('iframe'\)", script)) != 1:
        problems.append("the frame is not made in one place")
    body = function_body(script, "function setReplay(open)")
    if "el('iframe')" not in body or "if (open && !frame)" not in body:
        problems.append("the frame is not made inside setReplay, when it opens")
    if "frame.src = '/play.html?replay=' + encodeURIComponent(file) + '&embed=1';" not in body:
        problems.append("the frame's address is not the replay in embed mode")
    if "frame.src = 'about:blank'" not in body or "removeChild(frame)" not in body or "frame = null" not in body:
        problems.append("closing does not take the frame away")
    if sorted(re.findall(r"setReplay\(([^)]*)\)", script)) != ["!frame", "false", "open"]:          # (the definition, the press that toggles, the game's own Quit that closes: nothing opens it by itself)
        problems.append("setReplay is called by something else than the press and the game's Quit")
    # the only requests of the page: the match, the list of live matches (a button), the sum of the map's deaths (only when "All matches on this map" is pressed), the maps (history_page.js); a recording is a link, never a request
    asked = re.findall(r"fetch\('([^']*)'", script)
    if sorted(asked) != ["/history/", "/history/deaths?map=", "/live"]:
        problems.append("the page asks for something else than the match, the sum of the map's deaths and the live list: %s" % asked)
    if "dl.href = '/replays/' + encodeURIComponent(st.file);" not in script:
        problems.append("the recording is not a download link")
    return problems


class TheCommonFiles(unittest.TestCase):
    def test_every_page_is_filled_in_like_the_other_pages_and_loads_nothing_from_elsewhere(self):
        for name in PAGES:
            page = read("web", name)
            for placeholder in ("@@SITE_TITLE@@", "@@SITE_FOOTER@@", "@@GAME_VERSION@@", "@@BUILD_ID@@", "@@BUILD_DATE@@"):
                self.assertEqual(page.count(placeholder), 1, name + " " + placeholder)
            self.assertNotRegex(page, r'(?:src|href)="https?://(?!github\.com/dchadd427/|discord\.gg/WaWrXQnKPj")', name)
            self.assertIn('<link rel="stylesheet" href="matches.css">', page, name)
            self.assertIn('<script src="replay_page.js"></script>\n<script src="front/map_picker.js"></script>\n<script src="history_page.js"></script>', page, name)    # (the helpers before the page's own script)
            self.assertIn('<link rel="preload" href="front/LibreFranklin-Medium.ttf" as="font" type="font/ttf" crossorigin>', page, name)
            self.assertIn("$('game-version-line').style.display = 'none'", page, name)       # (a page that was not built hides the line of the version)
            self.assertIn('<noscript>', page, name)
        css = read("web", "matches.css")
        self.assertIn('url("front/LibreFranklin-Medium.ttf")', css)
        self.assertNotRegex(css, r"url\((?:https?:)?//")

    def test_nothing_that_a_record_or_the_server_carries_becomes_markup(self):
        for path in [("web", n) for n in PAGES] + [("web", "history_page.js")]:
            text = read(*path)
            for bad in (".innerHTML", "document.write", "insertAdjacentHTML", "outerHTML", "eval(", "new Function"):
                self.assertNotIn(bad, text, "%s %s" % (path[-1], bad))

    def test_a_look_at_the_server_keeps_no_cookie_and_no_cache(self):
        for name in PAGES:
            page = read("web", name)
            for call in re.findall(r"fetch\([^;]*?\{[^}]*\}", page):
                if "/maps" in call:
                    continue
                self.assertIn("credentials: 'omit'", call, name + ": " + call)
                self.assertIn("cache: 'no-store'", call, name + ": " + call)
        self.assertIn("fetch('/maps/all', { credentials: 'same-origin' })", read("web", "history_page.js").replace("fetcher(", "fetch("))      # (the list of the maps is the site's own and cached by it)

    def test_every_page_ends_its_header_buttons_with_the_front_pages_discord_link(self):
        lobby = read("web", "lobby.html")
        link = re.search(r'<a class="btn sm discord".*?</a>', lobby).group(0)                              # (the same markup as the front page's header: its mark, its words, its new tab)
        self.assertIn('href="https://discord.gg/WaWrXQnKPj" id="discord-top" target="_blank" rel="noopener noreferrer"', link)
        for name in PAGES:
            page = read("web", name)
            acts = re.search(r'<div class="acts">(.*?)</div>', page, re.S).group(1)
            self.assertTrue(acts.endswith(link), name + ": the last button of the header's row")
            self.assertEqual(page.count("discord.gg"), 1, name)
            self.assertEqual(len(re.findall(r"<img", re.search(r"<header.*?</header>", page, re.S).group(0))), 1, name + ": the logo is the header's only picture")
        css = read("web", "matches.css")
        phone = css[css.index(".mast .acts .btn.hide-s { display: none; }"):]                           # (the phone's rules that come after the hide-s rule, inside the 720 px block that holds it)
        self.assertIn(".discord::before { content: \"\"; position: absolute; left: -4px; right: -4px; top: -6px; bottom: -6px; }", css)      # (the 44 px target)
        self.assertRegex(phone, r"\.discord \.dt \{ position: absolute; width: 1px; height: 1px; overflow: hidden; clip: rect\(0 0 0 0\); \}")     # (a phone: the mark alone, the word stays for a screen reader)
        self.assertIn(".discord { min-width: 44px;", phone)
        self.assertIn("@media (max-width: 389px)", css)
        self.assertRegex(css[css.index("@media (max-width: 389px)"):], r"\.mast \.acts \{[^}]*flex-wrap: wrap;")       # (with a match running there are three buttons: they never cover the logo)

    def test_the_one_stylesheet_is_the_pages_and_the_colors_are_the_front_pages(self):
        css = read("web", "matches.css")
        self.assertEqual(len(re.findall(r"\bbody \{", css)), 1)
        self.assertIn("@media (max-width: 720px)", css)
        self.assertNotIn("@import", css)
        for name in PAGES:
            self.assertEqual(len(re.findall(r"<style>", read("web", name))), 1, name)                    # (only the colors: test_web_pages_classic.py keeps them the same as web/front/classic.css)


class TheListPage(unittest.TestCase):
    page = read("web", "matches.html")

    def test_it_asks_for_the_list_in_the_servers_words_with_a_pause_and_one_look_at_a_time(self):
        self.assertIn("fetch('/history' + (plan.query ? '?' + plan.query : '')", self.page)
        self.assertIn("H.planOf(st)", self.page)
        self.assertIn("var SEARCH_WAIT = 450;", self.page)                                                   # (the server allows two lists with a query a second)
        self.assertIn("searchTimer = setTimeout(run, SEARCH_WAIT)", self.page)
        self.assertIn("var mine = ++round", self.page)                                                       # (an answer that is late for a newer look is dropped)
        self.assertIn("if (mine !== round) return;", self.page)
        self.assertIn("controller.abort()", self.page)
        self.assertIn("window.addEventListener('popstate'", self.page)                                       # (Back and Forward: the address is the state)
        self.assertIn("window.history[push && !askTimer ? 'pushState' : 'replaceState']", self.page)        # (choices that come quickly are one step of the history)
        self.assertIn("var ASK_WAIT = 350;", self.page)                                                      # (a select, a button and the pager wait a moment too: six presses of an arrow key are one request)
        self.assertIn("askTimer = setTimeout(run, wait)", self.page)
        self.assertIn("go(copy(state, { q: q }), false, false, 0)", self.page)                               # (the search has waited already)
        self.assertIn("'Back to the first page'", self.page)                                                 # (a page past the last one has a way back)
        self.assertIn("fetch('/live'", self.page)                                                            # (the button of the live page, one look)
        self.assertIn("' live now'", self.page)

    def test_it_says_what_it_shows_and_what_went_wrong_in_the_approved_words(self):
        for words in ("Every match played here.", "Find a map or a player", "No match fits those filters.", "Clear the filters", "Stats & replay", "Older version", "Replay removed",
                      "No match has been played here yet.", "A match that ran 30 seconds or more shows up here.", "There are no matches on this page.", "Clear all", "Try again"):
            self.assertIn(words, self.page + read("web", "history_page.js"), words)

    def test_the_whole_card_is_one_link_and_a_recording_that_cannot_be_played_is_marked(self):
        self.assertIn("'match.html?id=' + encodeURIComponent(rec.id)", self.page)
        self.assertIn("var li = el('li', 'm' + (s.kind === 'watch' ? '' : ' dim'));", self.page)
        self.assertIn("rec.mode !== 'highest-score'", self.page)                                              # (the mode is a pill when it is not the original's game)
        self.assertIn("sd.winner && rec.draw !== true", self.page)                                            # (a draw has no winner)

    def test_the_list_and_the_phone_have_what_the_pictures_show(self):
        css = read("web", "matches.css")
        for needle in (".m::after { content: \"\\203a\";", ".tools.open .r2 .sel:not(.sort) { display: grid; }", ".m.liveentry { grid-template-areas:", ".pager .pg.num { display: none; }", ".tbl { display: none; }", ".seatcards { display: grid;"):
            self.assertIn(needle, css, needle)


class TheMatchPage(unittest.TestCase):
    page = read("web", "match.html")

    def test_the_sections_are_a_list_so_that_a_later_one_is_one_more_entry(self):
        self.assertIn("var SECTIONS = [resultSection, replaySection, stats.summary, boardSection, stats.overtime, deathsSection, stats.activity, moreSection, aboutSection];", self.page)
        self.assertIn("put(SECTIONS.map(function (f) { return f(rec, ctx); }));", self.page)
        self.assertIn("function put(nodes) { var box = $('sections'); box.textContent = ''; nodes.forEach(function (n) { if (n) box.appendChild(n); }); }", self.page)    # (a section that has nothing to say is null)
        self.assertIn("H.detailsOf", self.page)                                                              # (the comment that says how the stats sections read what a replay pass keeps: web/match_stats.js, tests/scripts/test_web_match_stats.py)
        for ident in ("result", "replay", "board", "about"):
            self.assertRegex(self.page, r"panel\('%s'," % ident)

    def test_a_map_name_is_text_in_the_title_whatever_characters_it_has(self):
        self.assertIn("document.title.replace(/^Match/, function () { return R.mapTitle(rec.map)", self.page)      # (a function: a string would read $& and $' in a map name as patterns of replace)
        self.assertNotIn("replace(/^Match/, R.mapTitle", self.page)

    def test_every_number_of_the_record_is_on_the_page(self):
        columns = ("score", "killed", "lost", "hatched", "bombs_planted", "bombs_defused", "fires_lit")
        helpers = read("web", "history_page.js")
        for key in columns:
            self.assertIn("key: '%s'" % key, helpers, key)
        for fact in ("Map", "Mode", "Players", "Format", "Length", "Ended", "How it ended", "Recorded by"):
            self.assertIn("['%s'," % fact, self.page, fact)
        self.assertIn("'Match ' + rec.id", self.page)

    def test_the_replay_is_a_frame_of_the_game_page_made_only_when_it_is_asked_for(self):
        self.assertEqual(lazy_problems(self.page), [])
        self.assertIn("frame.setAttribute('allow', 'fullscreen; autoplay')", self.page)
        self.assertIn("ev.origin === window.location.origin && ev.data && ev.data.ants === 'replay-leave' && frame && ev.source === frame.contentWindow", self.page)     # (the game's own Quit)
        self.assertIn("about 9 MB the first time", self.page)                                                # (the words that say what the press costs)

    def test_the_rule_for_the_frame_can_fail(self):
        broken = {
            "the markup holds the frame": ('<div id="sections" aria-live="polite">', '<div id="sections" aria-live="polite"><iframe src="/play.html?replay=x&embed=1"></iframe>'),
            "the page opens the replay as soon as it is made": ("        current = rec;\n", "        current = rec;\n        setReplay(true);\n"),
            "a second place makes a frame": ("        var show = $('show'); if (show)", "        var extra = el('iframe');\n        var show = $('show'); if (show)"),
            "the frame is made on closing as well": ("if (open && !frame) {", "if (!frame) {"),
            "closing leaves the frame": ("if (!open && frame) { frame.src = 'about:blank'; if (frame.parentNode) frame.parentNode.removeChild(frame); frame = null; }", ""),
            "the frame is the page without the embed mode": ("'&embed=1';", "'';"),
            "the page asks for the recording itself": ("function build(rec) {", "function build(rec) {\n        fetch('/replays/' + rec.file);"),
            "the page asks for the game page itself": ("        if (open && !frame) {", "        fetch('/play.html');\n        if (open && !frame) {"),
        }
        for why, (old, new) in broken.items():
            self.assertEqual(self.page.count(old), 1, why)
            self.assertNotEqual(lazy_problems(self.page.replace(old, new)), [], "the rule accepted a page where " + why)

    def test_a_match_that_is_not_there_says_so_and_offers_what_there_is(self):
        for words in ("This match is not in the history.", "A match that has only just ended can take a few minutes to be counted; its replay may already be there.", "Try the replay", "That is not the address of a match.",
                      "The recording was made by an older version", "Replay removed: recordings are kept for 30 days. The stats below stay."):
            self.assertIn(words, self.page + read("web", "history_page.js"), words)
        self.assertIn("r.data.error === 'no such match'", self.page)                                         # (the server's own words for an id that is not in the history)
        self.assertIn("if (!id) {", self.page)                                                               # (an address that is no id asks for nothing)


class TheLivePage(unittest.TestCase):
    page = read("web", "live.html")

    def test_it_looks_at_the_door_every_ten_seconds_and_says_what_the_picture_shows(self):
        self.assertIn("fetch('/live', { cache: 'no-store', credentials: 'omit'", self.page)
        self.assertIn("LIVE_EVERY = 10000", self.page)
        self.assertIn("document.hidden", self.page)                                                          # (a page that is out of sight does not ask)
        self.assertIn("R.liveEntriesOf(", self.page)                                                         # (the door's answer goes through the shared code that keeps only what is well formed)
        self.assertIn("'/play.html?live=' + encodeURIComponent(e.id)", self.page)
        for words in ("Matches being played right now.", "No match is on right now.", "Watch live", "so far", "See all matches", "This server doesn’t show matches that are being played.",
                      "A match shows up here after its first 30 seconds, and this list updates by itself. The picture is about ' + R.LIVE_LAG + ' seconds behind the players."):
            self.assertIn(words, self.page, words)
        self.assertNotIn("/history", self.page)                                                              # (a running match has no stats: the page does not ask for any)


class TheGamePagesStats(unittest.TestCase):
    page = read("web", "shell.html")

    def test_the_stats_are_asked_for_only_for_a_replay_of_the_servers_own_store(self):
        self.assertIn("if (ANTS_REPLAY_MODE && !ANTS_LIVE_MODE && !ANTS_EMBED && ANTS_REPLAY_FILE && typeof fetch === 'function') window.AntsHistory.replayStats({ document: document, fetch: fetch.bind(window), file: ANTS_REPLAY_FILE, redraw: window.AntsDeathMap && window.AntsDeathMap.redraw, deathMap: window.AntsDeathMap && window.AntsDeathMap.create(", self.page)
        helpers = read("web", "history_page.js")
        self.assertIn("id = idOfFile(env.file);", helpers)
        self.assertIn("env.fetch('/history/' + id, { cache: 'no-store', credentials: 'omit' })", helpers)
        self.assertIn("rec.id === id", helpers)                                                              # (an answer for another match is no stats)
        self.assertLess(self.page.index('<script src="replay_page.js"></script>'), self.page.index('<script src="history_page.js"></script>'))
        self.assertLess(self.page.index('<script src="history_page.js"></script>'), self.page.index("var ANTS_REPLAY_MODE"))

    def test_the_pieces_are_there_once_and_closed_at_first(self):
        for ident, markup in (("stats-btn", r'<button id="stats-btn" type="button" class="btn sm wide-only"[^>]*aria-expanded="false" aria-controls="mstats" hidden>Stats</button>'),
                              ("mstats-flow", r'<section class="mstats flow rep-only" id="mstats-flow" aria-label="Match stats" hidden></section>'),
                              ("mstats", r'<aside class="mstats pop rep-only" id="mstats" role="dialog" aria-label="Match stats" hidden></aside>')):
            self.assertEqual(len(re.findall(r'\bid="%s"' % ident, self.page)), 1, ident)
            self.assertRegex(self.page, markup, ident)
        self.assertLess(self.page.index('id="stats-btn"'), self.page.index('id="fullscreen-btn-r"'))        # (in the replay's header, before Fullscreen)
        self.assertLess(self.page.index('id="rbar"'), self.page.index('id="mstats-flow"'))                  # (a phone's row is under the controls)
        css = read("web", "replay_stats.css")
        self.assertIn("@media (max-width: 700px) { .mstats.pop { display: none !important; } }", css)         # (the page's own phone breakpoint: the panel over the picture is for the wide screens)
        self.assertIn("@media (min-width: 701px) { .mstats.flow { display: none !important; } }", css)
        self.assertIn(".mstats.flow.closed .mbody { display: none; }", css)

    def test_the_closed_row_of_a_phone_does_not_tell_who_won(self):
        helpers = read("web", "history_page.js")
        self.assertNotIn("' won with '", helpers)                                                           # (the owner's wish: closed at first so that it does not tell who won; the picture of the row had the winner on it)
        self.assertNotIn("mk('small'", helpers)
        self.assertNotIn(".mt small", read("web", "replay_stats.css"))

    def test_the_frame_of_a_match_page_leaves_by_telling_it(self):
        self.assertIn("if (ANTS_EMBED && window.parent !== window) { try { window.parent.postMessage({ ants: 'replay-leave' }, window.location.origin); } catch (e) { /* the page around the frame is gone */ } return; }", self.page)
        self.assertIn("window.location.assign('/matches.html');", self.page)

    def test_fullscreen_of_the_match_page_is_the_games_own_not_the_frames(self):
        # the frame holds the game page, whose own Fullscreen button makes the game's stage fullscreen (the picture as large as the screen allows, the bar over it); the frame itself goes only where the page cannot be reached
        match = read("web", "match.html")
        self.assertIn("var touched = finger; finger = false;", match)                                        # (a finger is told once: a later key press on the button is none)
        self.assertIn("if (touched) { try { d.dispatchEvent(new f.contentWindow.PointerEvent('pointerdown', { pointerType: 'touch', bubbles: true })); } catch (e) { /* no PointerEvent there: the page just is not told of the finger */ } } own.click(); return;", match)
        self.assertLess(match.index("own.click(); return;"), match.index("var go = f.requestFullscreen"))
        # (a finger that pressed the button outside the frame is told to the frame: the strip that calls the bar back is there from the start)


@unittest.skipUnless(shutil.which("node"), "node is not installed: the shared code of the match pages was NOT run (tests/scripts/web_history_check.js)")
class TheSharedCode(unittest.TestCase):
    def run_check(self, path):
        return subprocess.run([shutil.which("node"), HELPERS_JS, path], capture_output=True, text=True, timeout=120)

    def test_the_ids_the_seats_the_sides_the_state_the_words_the_pages_and_the_failures(self):
        done = self.run_check(os.path.join(REPO, "web", "history_page.js"))
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r" 0 failures")

    MUTANTS = {
        "a bot with a name is not shown as Name (bot, Level)": ("' (bot, ' + bot + ')' : 'Bot (' + bot + ')'", "' (' + bot + ')' : 'Bot (' + bot + ')'"),
        "a bot with no name is not Bot (Level)": ("'Bot (' + bot + ')'", "bot"),
        "a seat with an unknown color is a seat": ("if (s && typeof s === 'object' && colorOf(s)) out.push(s);", "if (s && typeof s === 'object') out.push(s);"),
        "an older server's seat (the older key alone) has no color": ("COLORS.indexOf(s.colour) !== -1 ? s.colour : ''", "''"),
        "an old bookmark's older parameter is ignored": ("if (v === null && f.key === 'color') v = q.get('colour');", ""),
        "the winner of a side is not its row's": ("winner: r.winner === true, left: false }); members", "winner: false, left: false }); members"),
        "a seat with no row is a side that left": ("winner: false, left: true }); });\n        return out;", "winner: false, left: false }); });\n        return out;"),
        "a card of mode 187 calls its kills a score": ("if (k && sort === 'score' && rec && rec.mode === '187') k = SORT_KEYS.kills;", ""),
        "a draw is not told as one": ("rec && rec.draw === true ? 'A draw: nobody won' : 'No winner'", "'No winner'"),
        "a side that left is in the ranking": ("return sides.filter(function (s) { return !s.left; });", "return sides.slice();"),
        "a record with no rows has no sides": ("            seats.sort(function (a, b) { return whole(b.score) - whole(a.score); });\n            return seats.map(", "            seats.sort(function (a, b) { return whole(b.score) - whole(a.score); });\n            return [].map("),
        "the score of mode 187 is shown as a score": ("rec && rec.mode === '187' ? numberText(side.row.killed) + ' kills' : numberText(side.row.score)", "numberText(side.row.score)"),
        "a draw has a winner": ("if (!rec || rec.draw === true) return null;", "if (!rec) return null;"),
        "the shares do not add up": ("parts.map(function (p, i) { return i; }).sort(", "[].sort("),
        "mode 187 has a Score column": ("if (!rec || rec.mode !== '187') cols.push", "if (true) cols.push"),
        "a recording with no good file is watchable": ("if (kind !== 'removed' && !file) kind = 'removed';", ""),
        "the details may be anything": ("if (!d || typeof d !== 'object' || Array.isArray(d)) return null;", "if (!d) return null;"),
        "an id may have anything after it": ("var ID_RE = /^ants-[A-Za-z0-9_]{1,24}-[0-9]{8}-[0-9]{6}Z(-[0-9]{1,4})?$/;", "var ID_RE = /^ants-[A-Za-z0-9_]{1,24}-[0-9]{8}-[0-9]{6}Z(-[0-9]{1,4})?/;"),
        "a word that is not the server's is kept": ("if (has(WHO, q.get('who'))) st.who = q.get('who');", "if (q.get('who')) st.who = q.get('who');"),
        "a search is not cut": ("t = t.slice(0, Q_MAX);", ""),
        "a search keeps what is not printable ASCII": ("replace(/[^\\x20-\\x7e]/g, '')", "replace(/[\\x00]/g, '')"),
        "a query is not escaped for the site's nginx": (".replace(/[!'()*~]/g, function (c) { return '%' + c.charCodeAt(0).toString(16).toUpperCase(); })", ""),
        "the default view asks for a query": ("st.page <= 2;", "st.page <= 0;"),
        "the plain list serves three pages": ("st.page <= 2;", "st.page <= 3;"),
        "a filter is sent as 'all'": ("if (st[k] !== d[k]) parts.push(k + '=' + st[k]); });\n        if (st.q) parts.push('q=' + encodeQuery(st.q));\n        var p", "parts.push(k + '=' + st[k]); });\n        if (st.q) parts.push('q=' + encodeQuery(st.q));\n        var p"),
        "the offset is the page": ("'offset=' + (st.page - 1) * PAGE", "'offset=' + st.page"),
        "the pager has no gap": ("if (n - last > 1) items.push('gap');", ""),
        "the range counts from 0": ("var from = offset + 1, to = offset + shown;", "var from = offset, to = offset + shown;"),
        "a busy server can not be tried again": ("if (status === 503) return { text: said || 'The match history is busy. Try again in a moment.', retry: true };", "if (status === 503) return { text: said || 'The match history is busy. Try again in a moment.', retry: false };"),
        "the server's words are not cut": ("body.error.slice(0, 200)", "body.error"),
    }

    def test_the_check_can_fail(self):
        text = read("web", "history_page.js")
        with tempfile.TemporaryDirectory() as folder:
            for why, (old, new) in self.MUTANTS.items():
                self.assertEqual(text.count(old), 1, why)
                path = os.path.join(folder, "history_page.js")
                with open(path, "w", encoding="utf-8") as f:
                    f.write(text.replace(old, new))
                self.assertNotEqual(self.run_check(path).returncode, 0, "the check accepted a script where " + why)


def run_pictures(history):
    return subprocess.run([shutil.which("node"), PICTURES_JS, history, os.path.join(REPO, "web", "front", "map_picker.js")], capture_output=True, text=True, timeout=120)


@unittest.skipUnless(shutil.which("node"), "node is not installed: the pictures of the maps were NOT run (tests/scripts/web_history_pictures_check.js)")
class ThePictures(unittest.TestCase):
    page = read("web", "history_page.js")

    def test_the_pictures_of_the_maps_of_the_players_on_a_fake_page(self):
        done = run_pictures(os.path.join(REPO, "web", "history_page.js"))
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("ok: ", done.stdout)

    def test_every_box_that_is_built_again_forgets_its_old_squares(self):
        self.assertIn("pics.forget('list');", read("web", "matches.html"))
        self.assertIn("pics.forget('live');", read("web", "live.html"))
        self.assertIn("pics.node(rec.map, 'list')", read("web", "matches.html"))
        self.assertIn("pics.node(e.map, 'live')", read("web", "live.html"))
        self.assertIn("pics.node(rec.map, 'page')", read("web", "match.html"))

    def test_the_check_can_fail(self):
        text = self.page
        broken = {
            "the list never corrects an address": ("list.forEach(function (item) { if (picker.thumbFor(item.map, maps) !== item.tried) fill(item.tv, item.map, item); });", "list.forEach(function (item) { if (false) fill(item.tv, item.map, item); });"),
            "every square is redone when the list comes": ("if (picker.thumbFor(item.map, maps) !== item.tried) fill(", "if (true) fill("),
            "the picture is the map's name": ("                img.src = url;\n", "                img.src = map;\n"),
            "a failed picture stays": ("img.addEventListener('error', function () { if (img.parentNode === tv) { tv.textContent = ''; tv.appendChild(el('div', 'blank')); } });", "img.addEventListener('error', function () {});"),
            "a failed picture leaves an empty square": ("if (img.parentNode === tv) { tv.textContent = ''; tv.appendChild(el('div', 'blank')); }", "if (img.parentNode === tv) { tv.textContent = ''; }"),
            "a map with no address gets an image": ("if (!key && !url) {", "if (false) {"),
            "the list of the maps is never asked for": ("if (item) { list.push(item); lookMaps(); }", "if (item) { list.push(item); }"),
            "the list is asked for with every square": ("if (asked) return asked;", ""),
            "a box forgets nothing": ("function forget(group) { list = list.filter(function (item) { return item.group !== group; }); }", "function forget(group) {}"),
            "a box forgets the other box": ("return item.group !== group; }); }\n        return { node", "return item.group === group; }); }\n        return { node"),
            "the squares that left the page are kept": ("list = list.filter(function (item) { return doc.body.contains(item.tv); });      // (squares that left the page)", ""),
            "the image is not lazy": ("img.loading = 'lazy'; ", ""),
            "the image decodes on the main thread": ("img.decoding = 'async';", ""),
        }
        with tempfile.TemporaryDirectory() as folder:
            for why, (old, new) in broken.items():
                self.assertEqual(text.count(old), 1, why)
                path = os.path.join(folder, "history_page.js")
                with open(path, "w", encoding="utf-8") as f:
                    f.write(text.replace(old, new))
                self.assertNotEqual(run_pictures(path).returncode, 0, "the check accepted a page where " + why)


class TheDocument(unittest.TestCase):
    def test_the_replays_page_describes_the_pages_the_stats_and_what_a_later_section_reads(self):
        text = read("docs", "REPLAYS.md")
        self.assertIn("## The match pages", text)
        for needle in ("web/matches.html", "web/match.html", "web/live.html", "web/history_page.js", "embed=1", "details", "Show replay", "/matches.html", "/live.html"):
            self.assertIn(needle, text, needle)
        self.assertNotIn("web/watch.html", text)                                                             # (the old list is gone: one home for the topic)


if __name__ == "__main__":
    unittest.main()
