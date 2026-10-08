#!/usr/bin/env python3
"""Watching a replay on the web (run by ./run_tests.sh --fast and by the CI): the list page (web/watch.html), the game page in replay mode (web/shell.html, play.html?replay=<file>) and the code that
they share (web/replay_page.js). The owner approved the pictures of the list and of the player (2026-10-08, the mock-up's second version); docs/REPLAYS.md "Watching" says what they do.

  - the shared code is RUN (node, when it is installed): the address of a replay, the day and the time in the visitor's zone, the length, the maps, the players, the chips (tests/scripts/web_replay_check.js);
  - what needs no browser is read from the files: the one place of the "Watch replays" link on each page, the pieces of the player that the game's glue looks for by id, nothing made from text that a
    recording carries (a name is text), the file's name shape shared by the page, the nginx rules and the store, and that the image, the CI and the local web build carry the new files.
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


def footer_links(page):
    nav = re.search(r'<nav aria-label="Footer links">(.*?)</nav>', page, re.S).group(1)
    return re.findall(r'<a href="([^"]+)"([^>]*)>([^<]+)</a>', nav)


class TheLink(unittest.TestCase):
    def test_every_page_with_the_footer_links_to_the_list_in_its_own_tab_second_after_the_menu(self):
        for name, page in (("the game page", read("web", "shell.html")), ("the front page", read("web", "lobby.html")), ("the list page", read("web", "watch.html"))):
            found = footer_links(page)
            watch = [f for f in found if f[2] == "Watch replays"]
            self.assertEqual(len(watch), 1, name)
            self.assertEqual(watch[0][0], "/watch.html", name)
            self.assertIn('id="watch-link"', watch[0][1], name)
            self.assertNotIn("target=", watch[0][1], name)                                                 # (the site's own page: this tab)
            texts = [f[2] for f in found]
            self.assertEqual(texts.index("Watch replays"), texts.index("Menu") + 1 if "Menu" in texts else 0, name)
        self.assertIn('aria-current="page"', [f for f in footer_links(read("web", "watch.html")) if f[2] == "Watch replays"][0][1])

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
        for words in ("Matches to watch", "Nothing to watch yet", "The list can\u2019t be loaded right now", "This server doesn\u2019t keep replays", "Earlier versions", "Show them", "Hide them", "Show more", "Cut short"):
            self.assertIn(words, self.page, words)

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
                      "tl-fill", "tl-ticks", "tl-thumb", "t-now", "t-all", "speed", "b-fsx", "toast"):
            self.assertEqual(len(re.findall(r'\bid="%s"' % ident, self.page)), 1, ident)
        self.assertEqual(len(re.findall(r'data-s="(?:0\.5|1|2|4|8)"', self.page)), 5)
        self.assertIn('href="/watch.html"', self.page[self.page.index('id="all-btn"') - 120:self.page.index('id="all-btn"') + 20])

    def test_everything_of_the_replay_is_hidden_unless_the_address_asks_for_a_replay(self):
        self.assertIn("body:not(.replay) .rep-only { display: none !important; }", self.page)
        self.assertIn("if (/(^|[?&])replay=/.test(window.location.search)) document.body.className += ' replay';", self.page)
        self.assertIn("var ANTS_REPLAY_MODE = /(^|[?&])replay=/.test(window.location.search);", self.page)
        self.assertIn("window.AntsReplay.fileOf(window.location.search)", self.page)
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
