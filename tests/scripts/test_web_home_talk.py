#!/usr/bin/env python3
"""The emoji picker and the voice button of the chat (web/home/talk.js and talk.css, docs/HOME_SCREEN.md "Emoji and voice", docs/HUB.md "Text rules", web/legal.html; run by ./run_tests.sh --fast and by the CI).

Two small buttons stand beside Send in the chat dock's form: one opens a picker of common emoji, one types what the person says into the field with the browser's own speech recognition (never sending it). They are
loaded by web/home/home.js after the dock. What this file checks:
  - the hub's rules against the emoji of the picker, with the REAL rules (hub/text.py): every emoji of the picker passes `clean_line` as it is, alone and all together, counts as one code point of the 200 that a line may
    have and as at most 4 bytes of 800, is a symbol that the Python of the hub knows (nothing newer than Unicode 9), and the limits that the script names are the hub's own; and what the hub does with the sequences that the
    picker avoids (a joiner, a tag character, a variation selector) is pinned here, so that a change of the rules shows;
  - the page and the other pages: web/lobby.html does not link the buttons, no other file names them, the hub's door is still the dock's script alone;
  - the loader in home.js: the two files, once, after the dock is mounted, mounted when BOTH have come, and whatever goes wrong with them stays with them;
  - the folder: its own budgets (raw and gzipped), plain ES5, no markup made from text, no storage, no network, NO recording (no MediaRecorder, no getUserMedia), the kinds of element it makes;
  - the style: every target at least 44 px, nothing loaded from another site, the picker in the form's own flow (nothing covers the field), reduced motion;
  - the privacy words: the button's tooltip, the Legal page and docs/HOME_SCREEN.md say who hears the voice and that the site receives and keeps no audio;
  - the code RUNS in node: tests/scripts/web_home_talk_check.js (the real dock, a fake speech recognition and a fake page);
  - tests/data/home_talk_mutants.json changes one thing at a time in a COPY of the files: the checks must then fail (the last class of this file; tools/mutate.py runs the same list).
The pictures are compared by hand with the page in a real browser (tests/scripts/web_home_talk_browser.js, opt-in, `--shots DIR`).
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
CHECK = os.path.join(REPO, "tests", "scripts", "web_home_talk_check.js")
LOBBY = os.path.join(REPO, "web", "lobby.html")
NODE = shutil.which("node")
FILES = ("talk.js", "talk.css")
sys.path.insert(0, os.path.join(REPO, "tools"))
from size_limits import TALK_BUDGET, TALK_GZIP_BUDGET  # noqa: E402  (every byte budget: tools/size_limits.py)

sys.path.insert(0, REPO)
from hub import limits, text as hubtext          # noqa: E402  (the hub's own rules: the tests run the real thing)


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def code_of(js):
    return re.sub(r"//.*", "", js)


def the_emoji():
    """The picker's emoji, as the script lists them (node runs the script's own function: no copy of the table is kept here)."""
    if not NODE:
        raise unittest.SkipTest("node is not installed")
    done = subprocess.run([NODE, "-e", "console.log(JSON.stringify(require(process.argv[1]).emoji()))", os.path.join(HOME, "talk.js")], capture_output=True, text=True, timeout=60, cwd=REPO)
    assert done.returncode == 0, done.stderr
    return json.loads(done.stdout)


class TheHubsRules(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.groups = the_emoji()
        cls.all = [e for g in cls.groups for e in g["list"]]

    def test_the_limits_that_the_script_names_are_the_hubs_own(self):
        self.assertEqual(limits.Limits().line_code_points, 200)
        self.assertEqual(hubtext.MAX_CODE_POINTS, 200)
        self.assertEqual(hubtext.MAX_BYTES, 800)
        self.assertIn("var LIMIT = 200;", read("web", "home", "talk.js"))
        self.assertIn("LIMIT = { name: 32, line: 200, feed: 100, history: 50, frame: 120000, bad: 30, field: 400,", read("web", "home", "chat.js"), "the dock's line of 200 code points and its field of 400 UTF-16 units: 200 code points are never more than 400 units")

    def test_every_emoji_of_the_picker_is_one_code_point_that_the_hub_keeps_as_it_is(self):
        self.assertGreaterEqual(len(self.all), 60)
        self.assertEqual(len(self.all), len(set(self.all)), "an emoji twice")
        for e in self.all:
            self.assertEqual(len(e), 1, "%r is not one code point" % e)
            self.assertEqual(hubtext.clean_line(e), (e, None), "the hub changes or refuses U+%04X" % ord(e))
            self.assertEqual(unicodedata.category(e), "So", "U+%04X is not a symbol" % ord(e))
            self.assertLessEqual(len(e.encode("utf-8")), 4)
            self.assertNotEqual(unicodedata.name(e, ""), "", "U+%04X has no name in this Python (no newer than the hub's)" % ord(e))
            self.assertNotIn(e, "<>")

    def test_the_picker_holds_exactly_these_code_points_none_newer_than_unicode_9(self):
        """The list is pinned here (the Python that runs this test may know a newer Unicode than the hub's image does: the name check cannot see that), so a new emoji is a decision that is read."""
        pinned = "1F600 1F603 1F604 1F601 1F606 1F605 1F602 1F923 1F60A 1F607 1F642 1F643 1F609 1F60D 1F618 1F60B 1F61C 1F60E 1F913 1F914 1F610 1F611 1F644 1F60F 1F62C 1F634 1F622 1F62D 1F62E 1F631 1F621 1F620 1F629 1F633 1F44D 1F44E 1F44C 1F44F 1F64C 1F64F 1F4AA 1F44B 1F91D 1F440 1F499 1F49A 1F49B 1F49C 1F494 1F495 1F4AF 1F4A5 1F525 2728 2B50 1F389 1F3C6 2705 274C 2753 2757 26A1 1F41C 1F41D 1F41B 1F40C 1F98B 1F33F 1F332 1F335 1F337 1F33B 1F340 1F34E 1F36F 1F36A 1F355 2615 1F4A7 1F30A 1F3AE 1F3B2 1F3AF 1F3C1 1F6A9 1F4A3 1F451 1F680 23F0 231B 1F4A4 1F4A1 1F512 1F514"
        self.assertEqual(" ".join("%X" % ord(e) for e in self.all), pinned)
        self.assertEqual(len(pinned.split()), 94)
        self.assertLessEqual(max(int(h, 16) for h in pinned.split()), 0x1F9C0)
        for h in pinned.split():
            self.assertLessEqual(len(h), 5)

    def test_all_of_them_together_and_a_full_line_of_them_are_one_line_of_the_right_length(self):
        everything = "".join(self.all)
        self.assertEqual(hubtext.clean_line(everything), (everything, None))
        for e in (self.all[0], self.all[-1], max(self.all, key=lambda c: len(c.encode("utf-8")))):
            self.assertEqual(hubtext.clean_line(e * 200), (e * 200, None), "200 of them are a line (at most 800 bytes: the hub's second limit)")
            self.assertEqual(hubtext.clean_line(e * 201), (None, "long"), "201 are not")

    def test_the_groups_are_named_and_in_order(self):
        self.assertEqual([g["name"] for g in self.groups], ["Faces", "Hands", "Hearts and signs", "Ants and nature", "Games"])

    def test_what_the_hub_does_with_the_sequences_that_the_picker_does_not_send(self):
        family = "\U0001F468\u200d\U0001F469\u200d\U0001F467"
        self.assertEqual(hubtext.clean_line(family), ("\U0001F468\U0001F469\U0001F467", None), "a joiner (Cf) is removed: a family comes out as three people")
        self.assertEqual(hubtext.clean_line("\U0001F3F4\U000E0067\U000E0062\U000E0065\U000E006E\U000E0067\U000E007F"), ("\U0001F3F4", None), "the tag characters of a flag of a country's part (Cf) are removed: a plain black flag is left")
        self.assertEqual(hubtext.clean_line("\u2764\ufe0f"), ("\u2764\ufe0f", None), "a variation selector (a mark) stays after its symbol")
        self.assertEqual(hubtext.clean_line("\ufe0f"), (None, "empty"), "...but alone it shows nothing, and is empty")
        self.assertEqual(hubtext.clean_line("\U0001F44D\U0001F3FD"), ("\U0001F44D\U0001F3FD", None), "a skin tone (a modifier symbol) stays")
        self.assertEqual(hubtext.clean_line("\u2764\ufe0f" * 100), ("\u2764\ufe0f" * 100, None), "a symbol and its selector are two code points: 100 of them are 200 (the count is in code points)")
        self.assertEqual(hubtext.clean_line("\u2764\ufe0f" * 101), (None, "long"))
        self.assertEqual(hubtext.clean_line("<3 \U0001F600"), (None, "bad"), "a '<' is refused, whatever stands beside it")

    def test_an_emoji_beyond_the_basic_plane_is_two_utf16_units_and_still_one_code_point(self):
        line = "\U0001F600" * 200
        self.assertEqual(len(line.encode("utf-16-le")) // 2, 400, "200 code points of them are 400 UTF-16 units: the field's maxlength, and so no cut")
        self.assertEqual(hubtext.clean_line(line), (line, None))


class ThePageAndTheOtherPages(unittest.TestCase):
    def test_the_front_page_does_not_link_the_buttons_home_loads_them_later(self):
        page = read("web", "lobby.html")
        for word in ("home/talk", "AntsHomeTalk", "talk.js", "talk.css"):
            self.assertNotIn(word, page, word)

    def test_no_other_file_of_the_site_names_them_and_the_hubs_door_is_still_the_docks_alone(self):
        naming_them, naming_the_socket = set(), set()
        for base, _dirs, names in os.walk(WEB):
            for name in names:
                if os.path.splitext(name)[1] not in (".html", ".js", ".css"):
                    continue
                path = os.path.join(base, name)
                rel = os.path.relpath(path, REPO).replace(os.sep, "/")
                with open(path, encoding="utf-8", errors="replace") as f:
                    text = f.read()
                if re.search(r"home/talk\b", text):
                    naming_them.add(rel)
                if re.search(r"/hub/ws", text):
                    naming_the_socket.add(rel)
        self.assertEqual(sorted(naming_them), ["web/home/home.js", "web/home/talk.css"], "only the front page's own script asks for them (the style names its script)")
        self.assertEqual(sorted(naming_the_socket), ["web/home/chat.js"])

    def test_the_game_and_the_other_pages_never_hear_of_them(self):
        for rel in (("web", "shell.html"), ("web", "front", "lobby_net.js"), ("web", "front", "lobby_chat.js"), ("web", "front", "lobby_rules.js")):
            self.assertNotRegex(read(*rel), r"AntsHomeTalk|home/talk|SpeechRecognition", rel[-1])


class TheLoaderInHome(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.js = read("web", "home", "home.js")

    def test_it_asks_for_the_two_files_by_their_base_name_through_the_part_loader_and_only_after_the_dock(self):
        self.assertIn("lazy('home/talk', function () {", self.js)
        self.assertEqual(self.js.count("wantTalk();"), 1, "one call, where the dock has been mounted")
        self.assertIn("wantPlayers(); wantTalk(); } } catch (e) { dock = null; } }", self.js)
        self.assertIn("if (askedTalk || !dock) return;\n            askedTalk = true;", self.js, "once, and not before there is a dock to stand in")
        self.assertEqual(sorted(set(re.findall(r"home/talk\w*", code_of(self.js)))), ["home/talk"])

    def test_it_is_given_the_dock_and_nothing_else_and_a_script_that_is_not_there_mounts_nothing(self):
        self.assertIn("if (win.AntsHomeTalk) win.AntsHomeTalk.mount({ doc: doc, win: win, dock: dock });", self.js)

    def test_the_buttons_live_in_the_docks_box_so_the_dock_being_inert_covers_them(self):
        self.assertIn("dock && dock.root", self.js)
        self.assertNotIn("talk.root", self.js)


class TheFolderAndTheScript(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.js = read("web", "home", "talk.js")
        cls.code = code_of(cls.js)

    def test_it_is_under_its_budgets(self):
        raw = zipped = 0
        for name in FILES:
            with open(os.path.join(HOME, name), "rb") as f:
                data = f.read()
            raw += len(data)
            zipped += len(gzip.compress(data, 9))
        self.assertLess(raw, TALK_BUDGET, "talk.js and talk.css are %d bytes" % raw)
        self.assertLess(zipped, TALK_GZIP_BUDGET, "and %d bytes gzipped" % zipped)

    def test_the_sources_are_ascii_only_the_emoji_are_hex_code_points(self):
        for name in FILES:
            with open(os.path.join(HOME, name), "rb") as f:
                data = f.read()
            self.assertIsNone(re.search(rb"[^\x00-\x7f]", data), name + " holds a character that is not ASCII")
        self.assertIn("function chr(cp)", self.js)

    def test_the_script_is_plain_es5(self):
        for token in (r"\bconst\b", r"\blet\b", r"=>", r"`", r"\bimport\b", r"\bclass\s+\w+\s*[{(]", r"\basync\b", r"\bawait\b", r"fromCodePoint", r"\bcodePointAt\b"):
            self.assertIsNone(re.search(token, self.code), token)
        self.assertIn("'use strict';", self.js)

    def test_nothing_that_is_said_or_typed_becomes_markup_a_link_or_code(self):
        self.assertNotRegex(self.code, r"innerHTML|outerHTML|insertAdjacentHTML|document\.write|\beval\b|new Function|srcdoc|createContextualFragment|DOMParser|setTimeout\(\s*['\"]")
        self.assertNotRegex(self.code, r"\.href\b|\.src\b|setAttribute\(\s*['\"](href|src|style|on\w+|srcdoc)")
        made = sorted(set(re.findall(r"\bel\('(\w+)'", self.code)) | set(re.findall(r"createElement\('(\w+)'", self.code)))
        self.assertEqual(made, ["button", "div", "p", "span"], "text and boxes: no a, img, script, iframe, style or link")

    def test_it_keeps_nothing_in_the_browser_and_has_no_network_of_its_own(self):
        self.assertNotRegex(self.code, r"localStorage|sessionStorage|indexedDB|document\.cookie|new WebSocket|WebSocket\(|XMLHttpRequest|fetch\(|sendBeacon")
        self.assertNotRegex(self.code, r"/hub\b|https?:")

    def test_it_records_nothing_and_uploads_nothing_the_browsers_own_recognition_is_all(self):
        self.assertNotRegex(self.code, r"MediaRecorder|getUserMedia|mediaDevices|AudioContext|webkitAudioContext|\bBlob\b|FileReader|createObjectURL|MediaStream|\.upload")
        self.assertEqual(len(re.findall(r"SpeechRecognition", self.code)), 2, "the two names (with and without the prefix), where the support is looked up")
        self.assertIn("win.isSecureContext !== true", self.js, "not on a page that is not secure")

    def test_it_never_sends_a_line_by_itself(self):
        self.assertNotRegex(self.code, r"dock\.(send|say)\b|\.submit\(|requestSubmit|dispatchEvent|\.click\(\)")
        self.assertIn("dock.tap(", self.js)

    def test_the_words_of_the_tooltip_and_the_label_are_the_privacy_words(self):
        self.assertIn("voice: 'Voice to text: your browser\\'s speech service hears you'", self.js)
        self.assertIn("micBtn = button('gt-btn gt-mic', TEXT.voice, '')", self.js, "the label and the title are the same words")
        self.assertIn("b.setAttribute('aria-label', label); b.setAttribute('title', label);", self.js)

    def test_a_press_outside_a_second_press_escape_and_a_page_that_goes_stop_what_they_should(self):
        for part in ("doc.addEventListener('visibilitychange'", "win.addEventListener('pagehide'", "form.addEventListener('submit'", "field.addEventListener('input'", "form.addEventListener('keydown'", "doc.addEventListener('pointerdown'"):
            self.assertIn(part, self.js, part)
        self.assertIn("rec.lang = (win.navigator && win.navigator.language) || 'en-US'", self.js, "the page's language")
        self.assertIn("rec.interimResults = true", self.js)


class TheStyle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.css = read("web", "home", "talk.css")
        cls.rules = re.findall(r"([^{}]+)\{([^{}]*)\}", re.sub(r"/\*.*?\*/", "", cls.css, flags=re.S))

    def rule(self, selector):
        return next(body for sel, body in self.rules if sel.strip() == selector)

    def test_the_picker_lies_over_the_log_right_above_the_form_and_the_words_are_the_forms_first_row(self):
        picker = self.rule(".gt-pick")
        for part in ("position: absolute;", "bottom: 100%;", "left: 0;", "right: 0;"):
            self.assertIn(part, picker, part)
        self.assertIn("position: relative;", self.rule(".gc-form.gt-on"), "the picker is placed against the form")
        self.assertIn("grid-column: 1 / -1;", self.rule(".gt-say"))
        self.assertNotRegex(self.css, r"position: *fixed")
        self.assertEqual(self.css.count("z-index"), 1, "one layer, the picker's, over the log")
        self.assertIn("grid-template-columns: minmax(0, 1fr) auto auto; }", self.css)
        self.assertIn("pick.style.maxHeight = Math.max(72, Math.min(180, ", read("web", "home", "talk.js"), "the script sets its height to the room above the form")

    def test_a_narrow_form_puts_the_two_buttons_in_a_row_of_their_own_under_the_field_and_send(self):
        self.assertIn("@media (max-width: 420px), (min-width: 701px) and (max-width: 1000px) and (min-height: 501px) {", self.css)
        narrow = self.css[self.css.index("@media (max-width: 420px)"):]
        self.assertIn("grid-template-columns: minmax(0, 1fr) auto; }", narrow)
        self.assertIn(".gt-on .gt-tools { order: 3; grid-column: 1 / -1; }", narrow)
        self.assertIn(".gt-on .gc-input { order: 1; } .gt-on .gc-send { order: 2; } .gt-on .gc-login { order: 2; }", narrow, "the field, Send and the login button keep their order when the buttons move to the last row")

    def test_every_target_is_at_least_44_px_high_and_wide(self):
        for selector in (".gt-btn", ".gt-e"):
            body = self.rule(selector)
            self.assertGreaterEqual(int(re.search(r"min-height: (\d+)px", body).group(1)), 44, selector)
            self.assertGreaterEqual(int(re.search(r"min-width: (\d+)px", body).group(1)), 44, selector)

    def test_a_hidden_part_stays_hidden(self):
        hidden = next(sel for sel, body in self.rules if body.strip() == "display: none;")
        for part in (".gt-tools[hidden]", ".gt-pick[hidden]", ".gt-say[hidden]"):
            self.assertIn(part, hidden, part)

    def test_the_picker_scrolls_and_the_script_makes_it_short_where_there_is_little_room(self):
        self.assertIn("overflow-y: auto", self.rule(".gt-pick"))
        self.assertIn("max-height: 150px;", self.rule(".gt-pick"), "without the script's measure (a page that cannot measure) it is 150 px")

    def test_the_dock_is_taller_where_the_buttons_have_a_row_of_their_own(self):
        self.assertIn("@media (min-width: 701px) and (max-width: 1000px) and (min-height: 501px) { .gc[data-open=\"true\"]:has(.gc-form.gt-on) { height: clamp(240px, 38vh, 320px); } }", self.css, "the middle widths: the dock is about 60 to 80 px taller (its own is 160 to 260 px), so that the log keeps its room")

    def test_the_two_pictures_are_inline_svg_in_the_buttons_own_color_and_the_listening_microphone_is_solid(self):
        self.assertIn("stroke: currentColor;", self.rule(".gt-ico"))
        self.assertIn("fill: none;", self.rule(".gt-ico"))
        self.assertIn("width: 24px; height: 24px;", self.rule(".gt-ico"))
        self.assertIn("fill: currentColor;", self.rule(".gt-hot .gt-cap"))
        js = read("web", "home", "talk.js")
        self.assertIn("M5 11a7 7 0 0 0 14 0M12 18v3M9 21h6", js, "the holder, the stem and the base")
        self.assertIn("height:11;rx:3", js, "the capsule")
        self.assertNotRegex(js, r"\b(fill|stroke)\s*:\s*'#|currentcolor")

    def test_the_listening_state_moves_only_where_motion_is_allowed(self):
        self.assertIn("@media (prefers-reduced-motion: reduce) { .btn.gt-hot { animation: none; } }", self.css)

    def test_nothing_is_loaded_from_another_site_and_there_is_no_picture(self):
        self.assertNotRegex(self.css, r"https?://|@import")
        self.assertEqual(re.findall(r"url\(", self.css), [])
        self.assertIsNone(re.search(r"overflow-x: *(scroll|auto)", self.css))


class ThePrivacyWords(unittest.TestCase):
    def test_the_legal_page_says_who_hears_the_voice_and_that_the_site_gets_and_keeps_no_audio(self):
        page = re.sub(r"\s+", " ", re.sub(r"<[^>]+>", "", read("web", "legal.html")).replace("&rsquo;", "'"))
        self.assertIn("Your browser may send the audio to its maker's speech service (Chrome, Edge and Safari do)", page)
        self.assertIn("this site receives no audio and keeps nothing of it", page)
        self.assertIn("The voice button", page)

    def test_the_docs_say_the_same_and_the_hub_changes_nothing(self):
        docs = read("docs", "HOME_SCREEN.md")
        self.assertIn("Emoji and voice", docs)
        self.assertIn("speech service", docs)
        self.assertIn("web/legal.html", docs)


class TheWayIntoTheImage(unittest.TestCase):
    def test_the_ci_looks_for_the_script_and_the_style_in_the_image(self):
        self.assertIn(" home/talk.js home/talk.css ", read(".github", "workflows", "ci.yml"))

    def test_the_dockerfile_copies_the_folder_to_where_nginx_serves_home_and_the_local_build_packs_it(self):
        self.assertIn("COPY web/home/ /usr/share/nginx/html/home/\n", read("Dockerfile"))
        self.assertIn('cp -f "${SCRIPT_DIR}/web/home"/* "${DIST_DIR}/home/"', read("build_web.sh"))


@unittest.skipUnless(NODE, "node is not installed: the buttons were NOT run (tests/scripts/web_home_talk_check.js)")
class TheCodeRuns(unittest.TestCase):
    def test_the_buttons_against_the_real_dock_and_a_fake_speech_recognition(self):
        done = subprocess.run([NODE, CHECK, LOBBY], capture_output=True, text=True, timeout=300, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout[-4000:] + done.stderr[-2000:])
        self.assertRegex(done.stdout, r"OK: \d+ checks")


class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/home_talk_mutants.json (the list that tools/mutate.py runs too, with `--keep-going --jobs 1`) on a COPY of the files, one at a time: each must make the checks fail, and the
    copy with nothing changed must pass"""
    FILES = (["tools/size_limits.py", "web/lobby.html", "web/rejoin_key.js", "web/front/player_name.js", "web/shapes.js", "web/home/home.js", "web/home/home.css", "web/home/chat.js", "web/home/chat.css", "web/home/talk.js", "web/home/talk.css",
              "tests/scripts/test_web_home_talk.py", "tests/scripts/web_home_talk_check.js", "tests/scripts/web_page_harness.js", "Dockerfile", "build_web.sh", ".github/workflows/ci.yml",
              "web/matches.html", "web/live.html", "web/match.html", "web/legal.html", "web/mapmaker.html", "web/shell.html", "web/admin.html", "docs/HOME_SCREEN.md",
              "hub/__init__.py", "hub/text.py", "hub/limits.py"]
             + ["web/front/" + n for n in ("lobby_rules.js", "lobby_net.js", "lobby_chat.js", "lobby_chat.css", "rematch.js", "game_modes.js", "game_type.js", "map_picker.js", "map_maker_link.js")])

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "home_talk_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_home_talk_mutants.")
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
