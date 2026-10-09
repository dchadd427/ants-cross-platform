#!/usr/bin/env python3
"""Watching a replay, and a match that is being played, on the web (run by ./run_tests.sh --fast and by the CI): the list page (web/watch.html), the game page in replay mode (web/shell.html,
play.html?replay=<file>) and in live mode (play.html?live=<id>) and the code that they share (web/replay_page.js). docs/REPLAYS.md "Watching" says what they do.

  - the shared code is RUN (node, when it is installed): the address of a replay or of a live match, the day and the time in the visitor's zone, the length, the maps, the players, the chips, the
    live tag and the verdict on an answer of the live door (tests/scripts/web_replay_check.js);
  - what needs no browser is read from the files: the one place of the "Watch matches" link on each page (and its button in the header of the front page and of the game page), the pieces of the player that the game's glue looks for by id,
    the live glue (the id's shape check, the doors it asks, the controls it calls, the words of its notes), nothing made from text that a recording or the door carries (a name is text), the file's name
    shape shared by the page, the nginx rules and the store, and that the image, the CI and the local web build carry the new files.
"""
import os
import re
import shutil
import subprocess
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CHECK_JS = os.path.join(REPO, "tests", "scripts", "web_replay_check.js")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def block(text, opening):
    """The {...} that starts at `opening` (a media query or a rule's selector and its brace), braces matched."""
    start = text.index(opening)
    depth, i = 0, text.index("{", start)
    first = i
    while True:
        depth += text[i] == "{"
        depth -= text[i] == "}"
        if depth == 0:
            return text[first:i + 1]
        i += 1


def footer_links(page):
    nav = re.search(r'<nav aria-label="Footer links">(.*?)</nav>', page, re.S).group(1)
    return re.findall(r'<a href="([^"]+)"([^>]*)>([^<]+)</a>', nav)


class TheLink(unittest.TestCase):
    """ONE link to the matches, "Watch matches", for the live ones and the earlier ones alike (it was "Watch live" and "Watch replays", two links to the same page): in the footer of the front page,
    the game page and the list, and as a button in the header of the front page and of the game page."""

    def test_every_page_with_the_footer_links_to_the_list_once_by_one_name(self):
        for name, page, before in (("the game page", read("web", "shell.html"), "Menu"), ("the front page", read("web", "lobby.html"), "How it works"), ("the list page", read("web", "watch.html"), None)):
            found = footer_links(page)
            watch = [f for f in found if f[2] == "Watch matches"]
            self.assertEqual(len(watch), 1, name)
            self.assertEqual(watch[0][0], "/watch.html", name)
            self.assertIn('id="watch-link"', watch[0][1], name)
            texts = [f[2] for f in found]
            self.assertEqual(texts.index("Watch matches"), texts.index(before) + 1 if before in texts else 0, name)      # (after the Menu link, or after "How it works" on the front page; the first on the list page)
            self.assertFalse({"Watch live", "Watch replays"} & set(texts), name)                                      # (the two old names are gone: one link)
        self.assertIn('aria-current="page"', [f for f in footer_links(read("web", "watch.html")) if f[2] == "Watch matches"][0][1])
        self.assertIn("$('watch-link').setAttribute('aria-current', 'page');", read("web", "shell.html"))     # (the game page in replay or live mode says where the player is)
        self.assertIn("<title>Watch matches \u2014 Ants (1998)@@SITE_TITLE@@</title>", read("web", "watch.html"))   # (the tab's title agrees with the link)

    def test_the_site_own_tab_on_the_front_page_and_the_list_and_a_new_tab_on_the_game_page(self):
        for name, page in (("the front page", read("web", "lobby.html")), ("the list page", read("web", "watch.html"))):
            watch = [f for f in footer_links(page) if f[2] == "Watch matches"][0]
            self.assertNotIn("target=", watch[1], name)                                                    # (the site's own page: this tab)
        game = read("web", "shell.html")
        watch = [f for f in footer_links(game) if f[2] == "Watch matches"][0]
        self.assertIn('target="_blank"', watch[1])                                                         # (a match that is being played here goes on: the way back to the menu asks first, this link does not leave)
        self.assertIn('rel="noopener noreferrer"', watch[1])
        glue = game[game.index("<!-- BEGIN replay glue"):game.index("<!-- END replay glue")]
        self.assertIn("$('watch-link').removeAttribute('target');", glue)                                  # (a replay is watched in this tab, like "All matches": nothing is being played; only in the replay glue: a match played keeps the new tab)
        self.assertIn("$('watch-link').removeAttribute('rel');", glue)
        self.assertNotIn("removeAttribute('target')", game.replace(glue, ""))

    def test_the_header_of_the_front_page_has_the_button_at_the_right(self):
        lobby = read("web", "lobby.html")
        header = re.search(r'<header class="mast">(.*?)</header>', lobby, re.S).group(1)
        self.assertEqual(re.findall(r'<a class="btn sm" href="/watch.html" id="watch-top"[^>]*>Watch matches</a>', header).__len__(), 1)
        self.assertTrue(header.rstrip().endswith("Watch matches</a>"))                                      # (after the logo and the tagline: the right of the row)
        self.assertIn(".mast #watch-top { margin-left: auto;", lobby)
        phone = block(lobby, "@media (max-width: 720px)")
        self.assertIn('grid-template-areas: "logo btn" "text text"', phone)                                # (a phone: the button on the logo's row, the tagline under them; the rule is the phone's, not the page's)
        self.assertNotIn('grid-template-areas: "logo btn"', lobby.replace(phone, ""))
        self.assertIn(".mast #watch-top { grid-area: btn; justify-self: end; margin-left: 0; }", phone)
        self.assertNotIn('id="live-link"', lobby)

    def test_the_header_of_the_game_page_has_the_button_after_menu_and_more_has_it_first(self):
        game = read("web", "shell.html")
        actions = re.search(r'<div class="header-actions">(.*?)</header>', game, re.S).group(1)
        links = re.findall(r'<a href="([^"]+)"([^>]*)>([^<]+)</a>', actions)
        self.assertEqual([t for _, _, t in links][:3], ["Menu", "Watch matches", "Sprites and sounds"])
        wide = links[1]
        self.assertEqual(wide[0], "/watch.html")
        self.assertIn('id="watch-btn"', wide[1])
        self.assertIn('wide-only', wide[1])                                                                 # (a narrow window has Menu, Full and More only)
        self.assertIn('target="_blank"', wide[1])
        more = re.search(r'<div class="more-list">(.*?)</div>', actions, re.S).group(1)
        first = re.findall(r'<(?:a|button)\b([^>]*)>([^<]+)</(?:a|button)>', more)[0]
        self.assertEqual(first[1], "Watch matches")
        self.assertIn('href="/watch.html"', first[0])
        self.assertIn('target="_blank"', first[0])
        self.assertNotIn("wide-only", first[0])
        replay = re.search(r'<div class="header-actions rep-only">(.*?)</header>', game, re.S).group(1)
        self.assertNotIn("Watch matches", replay)                                                           # (a replay's header keeps "All matches": no second way to the list)
        self.assertIn("body.replay .header-actions:not(.rep-only)", game)                                   # (and the ordinary header is not shown there)

    def test_the_words_on_the_screens_say_player_never_friend(self):
        for path in (("web", "watch.html"), ("web", "replay_page.js")):
            self.assertNotRegex(read(*path), r"(?i)friend", path[-1])
        shell = read("web", "shell.html")
        start = shell.index("<!-- BEGIN replay glue")
        end = shell.index("<!-- END replay glue")
        self.assertNotRegex(shell[start:end], r"(?i)friend")


class TheListPage(unittest.TestCase):
    page = read("web", "watch.html")

    def test_it_is_filled_in_like_the_other_pages_and_loads_nothing_from_elsewhere(self):
        for placeholder in ("@@SITE_TITLE@@", "@@SITE_FOOTER@@", "@@GAME_VERSION@@", "@@BUILD_ID@@"):
            self.assertEqual(self.page.count(placeholder), 1, placeholder)
        self.assertNotRegex(self.page, r'(?:src|href)="https?://(?!github\.com/dchadd427/)')
        self.assertIn('<script src="replay_page.js"></script>', self.page)
        self.assertIn('url("front/LibreFranklin-Medium.ttf")', self.page)                                    # (the font of the game, from the page's own folder)

    def test_it_reads_the_public_list_and_links_to_the_player_and_the_file(self):
        self.assertIn("fetch('/replays'", self.page)
        self.assertIn("/play.html?replay=", self.page)
        self.assertIn("'/replays/'", self.page)
        for words in ("Earlier matches", "Nothing to watch yet", "The list can\u2019t be loaded right now", "This server doesn\u2019t keep replays", "Earlier versions", "Show them", "Hide them", "Show more", "Cut short"):
            self.assertIn(words, self.page, words)

    def test_the_live_box_looks_at_the_door_every_ten_seconds_and_says_what_the_picture_shows(self):
        self.assertIn("fetch('/live'", self.page)
        self.assertIn("LIVE_EVERY = 10000", self.page)
        self.assertIn("/play.html?live=", self.page)
        self.assertIn("document.hidden", self.page)                                                          # (a page that is out of sight does not ask)
        for words in ("Live now", "Watch a match, live or again.", "Watch a match again.", "Matches that are being played show first.", "No match is on right now.", "so far", "Watch live",
                      "A match shows up here after its first 30 seconds, and this list updates by itself. The picture is about ' + R.LIVE_LAG + ' seconds behind the players."):
            self.assertIn(words, self.page, words)
        self.assertIn("R.liveEntriesOf(", self.page)                                                         # (the door's answer goes through the shared code that keeps only what is well formed)
        self.assertIn("fetch('/live', { cache: 'no-store', credentials: 'omit'", self.page)                  # (a public list: no cookies, never from the cache)

    def test_a_match_is_playable_only_when_its_rules_are_the_ones_of_this_version(self):
        self.assertIn("sim_rules", self.page)
        self.assertRegex(self.page, r"\.sim_rules\s*(?:===|!==|==|!=)")

    def test_nothing_that_a_recording_carries_becomes_markup(self):
        for bad in (".innerHTML", "document.write", "insertAdjacentHTML", "outerHTML", "eval(", "new Function"):
            self.assertNotIn(bad, self.page, bad)
            self.assertNotIn(bad, read("web", "replay_page.js"), bad)


class ThePlayerPage(unittest.TestCase):
    page = read("web", "shell.html")

    def test_the_pieces_that_the_glue_finds_by_id_are_all_there(self):
        for ident in ("r-title", "r-meta", "all-btn", "copy-btn", "fullscreen-btn-r", "copy-more", "rtag", "endnote", "veil", "veil-t", "veil-f", "rm", "rm-title", "rm-body", "rbar", "b-play", "tl", "rail",
                      "tl-fill", "tl-ticks", "tl-thumb", "t-now", "t-all", "speed", "b-fsx", "toast", "b-tab", "redge", "rnote"):
            self.assertEqual(len(re.findall(r'\bid="%s"' % ident, self.page)), 1, ident)
        self.assertEqual(len(re.findall(r'data-s="(?:0\.5|1|2|4|8)"', self.page)), 5)
        self.assertIn('href="/watch.html"', self.page[self.page.index('id="all-btn"') - 120:self.page.index('id="all-btn"') + 20])

    def test_everything_of_the_replay_is_hidden_unless_the_address_asks_for_a_replay(self):
        self.assertIn("body:not(.replay) .rep-only { display: none !important; }", self.page)
        self.assertIn("if (/(^|[?&])(replay|live)=/.test(window.location.search)) document.body.className += ' replay';", self.page)
        self.assertIn("var ANTS_REPLAY_MODE = /(^|[?&])(replay|live)=/.test(window.location.search);", self.page)
        self.assertIn("var ANTS_LIVE_MODE = ANTS_REPLAY_MODE && !/(^|[?&])replay=/.test(window.location.search);", self.page)    # (a replay's address wins over a live id)
        self.assertIn("window.AntsReplay.fileOf(window.location.search)", self.page)
        self.assertIn("window.AntsReplay.liveIdOf(window.location.search)", self.page)
        self.assertLess(self.page.index('<script src="replay_page.js"></script>'), self.page.index("var ANTS_REPLAY_MODE"))

    def test_a_replay_has_no_pointer_lock_and_no_leave_the_game_question(self):
        self.assertIn("(!ANTS_POINTER_LOCK || ANTS_REPLAY_MODE)", self.page)
        self.assertIn("if (ANTS_REPLAY_MODE || !ANTS_POINTER_LOCK || !request || pageFullscreen || isLocked()) return false;", self.page)
        app = read("src", "ants_app", "application.cpp")
        self.assertRegex(app, r"ants_match_running\(\)\s*\{\s*return \(g_web_app != nullptr && !g_web_app->replay_mode\(\)")

    def test_the_two_controls_of_the_game_are_the_ones_that_the_page_calls(self):
        app = read("src", "ants_app", "application.cpp")
        self.assertIn("int ants_replay_get(int what)", app)
        self.assertIn("void ants_replay_do(int what, int value)", app)
        for call in ("Module._ants_replay_get", "Module._ants_replay_do", "window.antsReplayReport", "window.antsReplayLeave", "window.antsAfterReplay"):
            self.assertIn(call, self.page, call)
        self.assertIn("'--replay', '/replay.antsrep'", self.page)

    def test_only_these_two_places_make_markup_in_the_replay_glue(self):
        glue = self.page[self.page.index("<!-- BEGIN replay glue"):self.page.index("<!-- END replay glue")]
        self.assertEqual(re.findall(r"\.innerHTML\s*=\s*([^;]*);", glue), ["h", "done ? ICON.again + '<span class=\"lbl\">Watch again</span>' : running ? ICON.pause : ICON.play"])
        self.assertIn("numbers only", glue)
        for bad in ("insertAdjacentHTML", "document.write", "outerHTML", "eval("):
            self.assertNotIn(bad, glue, bad)


class TheTagAndTheFullscreenBar(unittest.TestCase):
    """The REPLAY tag lies outside the picture (above the bar), and in fullscreen a finger on the picture never calls the bar (docs/REPLAYS.md "Watching a replay"); a swipe down or the tab hides it, a tap
    or a swipe up along the bottom edge calls it. tests/scripts/web_live_check.py (part bar) shows all of it in a real browser."""
    page = read("web", "shell.html")
    glue = page[page.index("<!-- BEGIN replay glue"):page.index("<!-- END replay glue")]

    def test_the_tag_is_a_part_of_the_bar_and_not_of_the_picture(self):
        self.assertRegex(self.page, r'<div class="rbar rep-only" id="rbar">\s*<div class="rtag rep-only" id="rtag" hidden>')
        stage = self.page[self.page.index('<div id="game-container">'):self.page.index('id="pseudo-exit"')]
        self.assertNotIn('id="rtag"', stage)
        rule = re.search(r"\n\s*\.rtag \{([^}]*)\}", self.page).group(1)
        self.assertIn("position: absolute", rule)
        self.assertIn("bottom: calc(100% + 6px)", rule)
        self.assertNotIn("cqw", rule)                                                                       # (it is outside the picture's box: its sizes are pixels)
        self.assertNotIn("cqw", re.search(r"\.rtag\.lv b::before \{([^}]*)\}", self.page).group(1))

    def test_the_bar_leaves_room_for_the_tag_in_every_layout(self):
        self.assertRegex(self.page, r"\.rbar \{ position: relative; [^}]*margin: 49px 0 10px;")
        self.assertIn("margin: 45px 0 8px;", block(self.page, "@media (max-width: 700px) {\n            body.replay header { flex-wrap: wrap; }"))
        self.assertIn("margin-top: 43px;", block(self.page, "@media (max-height: 520px) {\n            #r-meta { display: none; }"))

    def test_a_finger_on_the_picture_never_calls_the_bar(self):
        for line in ("var touch = ev.type === 'touchstart' || ev.pointerType === 'touch';",
                     "if (touch && window.PointerEvent && $('rbar').classList.contains('over') && !$('rbar').contains(ev.target)) return;",
                     "if (touch && swiped !== -1 && (ev.pointerId === undefined || ev.pointerId === swiped)) return;"):
            self.assertIn(line, self.glue, line)
        self.assertNotIn("document.addEventListener(name, wake", self.glue)                                  # (the old wiring: every touch, anywhere, called the bar)
        self.assertIn("idleTimer = setTimeout(hideBar, 3000);", self.glue)                                   # (a bar that plays still goes by itself after 3 seconds)

    def test_the_bar_is_called_from_the_bottom_edge_and_hidden_by_a_swipe_or_the_tab(self):
        for line in ("$('redge').hidden = !(touchSeen && bar.classList.contains('over') && bar.classList.contains('idle'));",    # (the strip: only for a finger, only while the bar is away)
                     "edgeFrom.y - ev.clientY >= EDGE_SWIPE", "$('redge').addEventListener('click', wake);",           # (a TAP calls the bar in its click: a bar called earlier takes the click)
                     "ev.pointerType === 'touch' && $('rbar').classList.contains('over') && !$('tl').contains(ev.target)",
                     "down >= BAR_SWIPE", "$('b-tab').addEventListener('click', hideBar);"):
            self.assertIn(line, self.glue, line)
        self.assertIn('<div class="redge rep-only" id="redge" hidden></div>', self.page)
        self.assertRegex(self.page, r'<button class="rtab" type="button" id="b-tab" aria-label="Hide the bar"')
        self.assertIn(".rbar.over { touch-action: none; }", self.page)
        self.assertIn("Swipe up from the bottom edge to bring the bar back", self.page)                      # (the note of the first time)
        self.assertIn("noteShown", self.glue)
        self.assertNotIn("TAP_SLOP", self.glue)                                                              # (no tap logic on pointerup any more: see the click)
        self.assertRegex(self.page, r"@media \(max-width: 700px\)[^@]*?\.rbar\.over \.rtab \{ left: auto; right: 14px; transform: none; \}")   # (a phone upright: the tab at the right end, clear of the tag)


class TheLiveGlue(unittest.TestCase):
    """play.html?live=<id>: the game plays a file that grows (docs/REPLAYS.md "Watching a replay"); the page asks the door for it again and again and tells the game each time it has more."""
    page = read("web", "shell.html")
    glue = page[page.index("<!-- BEGIN replay glue"):page.index("<!-- END replay glue")]

    def test_the_pieces_that_only_a_live_match_uses_are_all_there_once(self):
        for ident in ("rtag-b", "livebox", "livenow", "b-live"):
            self.assertEqual(len(re.findall(r'\bid="%s"' % ident, self.page)), 1, ident)
        self.assertIn("body.live [data-dl], body.nodl [data-dl] { display: none !important; }", self.page)   # (no Download while there is no whole file)
        for words in ("Jump to live", ">Live<", ">REPLAY<"):
            self.assertIn(words, self.page, words)

    def test_the_game_is_started_to_follow_the_file_and_told_each_time_there_is_more(self):
        self.assertIn("live: 7, limit: 8, complete: 9", self.glue)                                           # (the game's ReplayValue: src/ants_app/application.cpp)
        self.assertIn("extend: 5, liveOver: 6", self.glue)                                                   # (and its ReplayControl)
        self.assertIn("ANTS_ARGS.push('--replay', '/replay.antsrep', '--replay-live');", self.glue)
        self.assertIn("Module.FS.writeFile('/replay.antsrep', latest)", self.glue)
        self.assertIn("command(DO.extend, 0)", self.glue)
        self.assertIn("command(DO.liveOver, 0)", self.glue)
        self.assertIn("FEED_MS = 4000", self.glue)
        self.assertIn("setTimeout(feed, FEED_MS)", self.glue)

    def test_it_asks_the_door_for_the_match_by_an_id_that_passed_the_shape_check_and_keeps_no_cache(self):
        self.assertIn("fetch('/live/' + LIVE_ID, { cache: 'no-store', credentials: 'same-origin' })", self.glue)
        self.assertIn("window.AntsReplay.liveIdOf(window.location.search)", self.page)                       # (an id that is not of the shape is no match: the card "That match isn’t live")
        js = read("web", "replay_page.js")
        self.assertIn(r"/^[A-Za-z0-9_]{1,24}-[0-9]{8}-[0-9]{6}Z(-[0-9]{1,4})?$/", js)
        self.assertNotIn("'/live/' + window.location", self.glue)                                            # (never the raw address: only LIVE_ID)

    def test_the_words_of_the_notes_and_the_cards_are_the_approved_ones(self):
        for words in ("The live feed stopped. Trying again…", "The match is over. It is now in the list as a replay.", "The match is over.", "This match is over", "Watch the replay",
                      "The match you were watching has ended. It is kept in the list for 30 days, and you can watch all of it.", "That match isn’t live",
                      "It has ended without being kept (a match that ran 30 seconds or more is kept for 30 days), or the link is not right. The matches that are being played now are at the top of the list."):
            self.assertIn(words, self.glue, words)

    def test_a_match_that_became_a_replay_has_the_address_of_the_replay(self):
        self.assertIn("window.history.replaceState(null, '', window.location.pathname + '?replay=' + encodeURIComponent(file))", self.glue)
        self.assertIn("'/play.html?replay=' + encodeURIComponent(why)", self.glue)                           # (the card's button)
        self.assertIn("R.liveVerdict(", self.glue)

    def test_what_the_door_says_is_text_never_markup(self):
        for bad in ("insertAdjacentHTML", "document.write", "outerHTML", "eval(", "new Function"):
            self.assertNotIn(bad, self.glue, bad)
        # the names and the map reach the page through the shared code (textContent and text nodes: tests/scripts/web_replay_check.js) and the header's own text nodes
        self.assertIn("R.chipsOf(document, ", self.glue)
        self.assertIn("document.createTextNode(R.mapTitle(map) || 'A match to watch')", self.glue)
        self.assertNotRegex(self.glue, r"innerHTML\s*=[^;]*(?:LIVE_ID|latest|info\.|overEntry|why)")


class TheFiles(unittest.TestCase):
    def test_the_file_name_shape_is_the_same_in_the_page_the_nginx_rules_and_the_store(self):
        page = read("web", "replay_page.js")
        self.assertIn(r"/^ants-[A-Za-z0-9_]{1,24}-[0-9]{8}-[0-9]{6}Z(-[0-9]{1,4})?\.antsrep$/", page)
        nginx = read("docker", "nginx.conf")
        self.assertIn(r"ants-[A-Za-z0-9_]{1,24}-[0-9]{8}-[0-9]{6}Z(-[0-9]{1,4})?\.antsrep", nginx)

    def test_the_image_the_ci_and_the_local_web_build_carry_the_two_new_files(self):
        dockerfile, ci, build = read("Dockerfile"), read(".github", "workflows", "ci.yml"), read("build_web.sh")
        self.assertIn("COPY web/replay_page.js", dockerfile)
        self.assertRegex(dockerfile, r"COPY --from=builder /src/watch\.html [^\n]*watch\.html")
        self.assertIn("replay_page.js", build)
        self.assertIn("watch.html", build)
        self.assertRegex(ci, r"ls -l index\.html play\.html lobby\.html watch\.html replay_page\.js")


@unittest.skipUnless(shutil.which("node"), "node is not installed: the replay pages' shared code was NOT run (tests/scripts/web_replay_check.js)")
class TheSharedCode(unittest.TestCase):
    def test_the_address_the_time_the_length_the_maps_the_players_and_the_chips(self):
        done = subprocess.run([shutil.which("node"), CHECK_JS, os.path.join(REPO, "web", "replay_page.js")], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn(" 0 failures", done.stdout)


if __name__ == "__main__":
    unittest.main()
