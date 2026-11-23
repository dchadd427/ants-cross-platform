#!/usr/bin/env python3
"""The images of Home's chat on the page (web/home/pics.js and pics.css, post.js and post.css, the image parts of chat.js; docs/HUB_IMAGES.md "The page"; run by ./run_tests.sh --fast and by the CI).

A line of the chat may carry an image. The reader's part (pics.js: the thumbnail, the viewer, the delete, the animation and the reader's switch) is loaded when the first image is drawn; the poster's part (post.js: the button, the
paste and the drop, the card, the upload and the line) only for a person whom the hub says may post. What this file checks, beside the code that RUNS (the last two classes):
  - the dock's side: it asks for the two parts by their base names through the part loader, once each, and nothing else of the site names them; the page does not link them; no other file opens the hub's door for images;
  - the folder: its own byte budgets (raw and gzipped), the budget that chat.js is under now, plain ES5, no markup made from text, no network of its own beyond the one door, no storage but the reader's one switch;
  - the style: every target is at least 44 px, nothing is loaded from another site, the two rows of the box are for a phone held upright only, motion rests where the reader asks for it;
  - the way into the image: the CI looks for the four files in the built site, the Dockerfile copies the folder;
  - the code RUNS in node: tests/scripts/web_home_images_check.js (the two parts on a fake page of their own) and tests/scripts/web_home_images_dock_check.js (the real dock with a fake hub);
  - tests/data/home_images_mutants.json changes one thing at a time in a COPY of the files: the checks must then fail (the last class of this file; tools/mutate.py runs the same list).
The pictures are compared by hand with the page in a real browser (tests/scripts/web_home_images_browser.js, opt-in).
"""
import gzip
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HOME = os.path.join(REPO, "web", "home")
LOBBY = os.path.join(REPO, "web", "lobby.html")
CHECK = os.path.join(REPO, "tests", "scripts", "web_home_images_check.js")
DOCK_CHECK = os.path.join(REPO, "tests", "scripts", "web_home_images_dock_check.js")
NODE = shutil.which("node")
FILES = ("pics.js", "pics.css", "post.js", "post.css")
sys.path.insert(0, os.path.join(REPO, "tools"))
from size_limits import CHAT_BUDGET, CHAT_GZIP_BUDGET, IMAGES_BUDGET, IMAGES_GZIP_BUDGET  # noqa: E402  (every byte budget: tools/size_limits.py)


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def code_of(js):
    return re.sub(r"^\s*//.*", "", js, flags=re.M)


def sizes(names):
    raw = zipped = 0
    for name in names:
        with open(os.path.join(HOME, name), "rb") as f:
            data = f.read()
        raw += len(data)
        zipped += len(gzip.compress(data, 9))
    return raw, zipped


class TheDocksSide(unittest.TestCase):
    def test_it_asks_for_each_part_once_by_its_base_name_through_the_loader_that_home_gives_it(self):
        chat = code_of(read("web", "home", "chat.js"))
        self.assertEqual(chat.count("env.load('home/pics'"), 1)
        self.assertEqual(chat.count("env.load('home/post'"), 1)
        self.assertIn("load: lazy", read("web", "home", "home.js"), "the dock is given the loader that makes the script and the style one part")

    def test_nothing_else_names_the_parts_and_the_page_does_not_link_them(self):
        naming = []
        for folder, _, names in os.walk(os.path.join(REPO, "web")):
            for name in names:
                if name.endswith((".js", ".html")) and re.search(r"home/(pics|post)\b|AntsHome(Pics|Post)", read(os.path.relpath(os.path.join(folder, name), REPO))):
                    naming.append(os.path.relpath(os.path.join(folder, name), REPO).replace(os.sep, "/"))
        self.assertEqual(sorted(naming), ["web/home/chat.js", "web/home/pics.js", "web/home/post.js"])
        lobby = read("web", "lobby.html")
        self.assertNotIn("pics.", lobby)
        self.assertNotIn("post.js", lobby)

    def test_only_the_poster_opens_the_door_for_uploads_and_only_the_reader_makes_the_addresses_of_files(self):
        doors = {}
        for name in ("chat.js", "pics.js", "post.js", "home.js", "games.js", "players.js", "talk.js", "mod.js", "cmds.js"):
            for door in re.findall(r"/hub/img/[a-z]*", code_of(read("web", "home", name))):
                doors.setdefault(door, set()).add(name)
        self.assertEqual({k: sorted(v) for k, v in doors.items()}, {"/hub/img/": ["pics.js"], "/hub/img/up": ["post.js"]})


class TheFolderAndTheScripts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pics = code_of(read("web", "home", "pics.js"))
        cls.post = code_of(read("web", "home", "post.js"))

    def test_they_are_under_their_budgets_and_so_is_the_dock(self):
        raw, zipped = sizes(FILES)
        self.assertLess(raw, IMAGES_BUDGET, "the four files are %d bytes" % raw)
        self.assertLess(zipped, IMAGES_GZIP_BUDGET, "and %d bytes gzipped" % zipped)
        raw, zipped = sizes(("chat.js", "chat.css"))
        self.assertLess(raw, CHAT_BUDGET, "chat.js and chat.css are %d bytes" % raw)
        self.assertLess(zipped, CHAT_GZIP_BUDGET, "and %d bytes gzipped" % zipped)

    def test_the_scripts_are_plain_es5(self):
        for code in (self.pics, self.post):
            for token in (r"\bconst\b", r"\blet\b", r"=>", r"`", r"\bimport\b", r"\bclass\s+\w+\s*[{(]", r"(?<!')\basync\b(?!')", r"\bawait\b", r"\.\.\.[A-Za-z_]"):
                self.assertIsNone(re.search(token, code), token)
        self.assertIn("'use strict';", read("web", "home", "pics.js"))
        self.assertIn("'use strict';", read("web", "home", "post.js"))

    def test_nothing_that_is_said_or_chosen_becomes_markup_a_link_or_code(self):
        for code in (self.pics, self.post):
            self.assertIsNone(re.search(r"innerHTML|outerHTML|insertAdjacentHTML|document\.write|\beval\(|new Function|setTimeout\(\s*['\"]", code))

    def test_the_reader_keeps_one_switch_and_the_poster_keeps_nothing(self):
        self.assertEqual(len(re.findall(r"localStorage|sessionStorage|indexedDB|document\.cookie", self.pics)), 1)
        self.assertEqual(len(re.findall(r"getItem|setItem", self.pics)), 2)
        self.assertIsNone(re.search(r"localStorage|sessionStorage|indexedDB|document\.cookie", self.post))

    def test_the_only_network_is_the_one_upload_and_the_files_it_names(self):
        for code in (self.pics, self.post):
            self.assertIsNone(re.search(r"WebSocket|sendBeacon|EventSource|importScripts|\.src\s*=\s*['\"]http", code))
        self.assertIsNone(re.search(r"\bfetch\(", self.post))
        self.assertEqual(len(re.findall(r"\bfetch\(", self.pics)), 1, "the viewer's Save is the one request of the reader (for the file's own address: tests/scripts/web_home_images_check.js)")
        self.assertEqual(self.pics.count("XMLHttpRequest"), 0)
        self.assertEqual(self.post.count("new win.XMLHttpRequest()"), 1)
        self.assertEqual(self.post.count(".send("), 1)

    def test_a_dropped_or_pasted_file_is_never_read_as_text_or_opened(self):
        for token in (r"FileReader", r"readAsText", r"readAsDataURL", r"window\.open", r"location\s*=", r"\.href\s*="):
            self.assertIsNone(re.search(token, self.post), token)


class TheStyle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pics = read("web", "home", "pics.css")
        cls.post = read("web", "home", "post.css")

    def test_every_target_is_at_least_44_px(self):
        for css, selector in ((self.pics, r"\.gc-thumb\.gc-nopic"), (self.pics, r"\.gv-x"), (self.pics, r"\.gv-foot \.btn"), (self.pics, r"\.gv-foot \.gv-url"), (self.post, r"\.gp-x"), (self.post, r"\.gp-card \.gp-again")):
            block = "".join(re.findall(selector + r"\s*\{([^}]*)\}", css))
            sized = [int(n) for n in re.findall(r"(?:^|[ ;])(?:min-)?(?:width|height):\s*(\d+)px", block)]
            self.assertTrue(sized and max(sized) >= 44, selector + " " + block)

    def test_nothing_is_loaded_from_another_site_and_no_picture_is_part_of_the_style(self):
        for css in (self.pics, self.post):
            self.assertIsNone(re.search(r"https?:|//[a-z]|@import|url\(", css))

    def test_motion_rests_where_the_reader_asks_for_it(self):
        self.assertIn("@media (prefers-reduced-motion: reduce)", self.pics)

    def test_the_two_rows_of_the_box_are_for_a_phone_held_upright_only(self):
        self.assertRegex(self.post, r"@media \(max-width: 700px\) and \(min-height: 501px\)")
        self.assertEqual(len(re.findall(r"\.gp-two", self.post)), 2, "the two rows are named only inside that one media query")

    def test_the_viewer_covers_the_page_and_a_picture_never_runs_out_of_its_box(self):
        self.assertRegex(self.pics, r"\.gv-scrim \{[^}]*position: fixed;[^}]*inset: 0;")
        self.assertRegex(self.pics, r"\.gv-img \{[^}]*max-width: 100%;[^}]*max-height: calc")
        self.assertRegex(self.pics, r"\.gc-thumb img \{[^}]*--mw: 180px; --mh: 104px;[^}]*aspect-ratio: var\(--tw\) / var\(--th\);")
        self.assertRegex(self.pics, r"@media \(min-width: 701px\) \{[^@]*--mw: 144px; --mh: var\(--img-mh, 81px\);")


class TheWayIntoTheImage(unittest.TestCase):
    def test_the_ci_looks_for_the_four_files_in_the_built_site(self):
        ci = read(".github", "workflows", "ci.yml")
        for name in FILES:
            self.assertIn(" home/" + name + " ", ci)

    def test_the_dockerfile_copies_the_folder_and_the_local_build_packs_it(self):
        self.assertIn("COPY web/home/ /usr/share/nginx/html/home/\n", read("Dockerfile"))
        self.assertIn('cp -f "${SCRIPT_DIR}/web/home"/* "${DIST_DIR}/home/"', read("build_web.sh"))


class TheDocument(unittest.TestCase):
    def test_the_document_names_the_files_the_budgets_and_the_words_the_page_owns(self):
        doc = read("docs", "HUB_IMAGES.md")
        self.assertIn("## The page", doc)
        for name in ("pics.js", "post.js", "chat.js"):
            self.assertIn("web/home/" + name, doc)
        for word in ("Play animated images", "prefers-reduced-motion", "Open in a new tab", "Copy link", "Delete my image"):
            self.assertIn(word, doc)


@unittest.skipUnless(NODE, "node is not installed: the images were NOT run (tests/scripts/web_home_images_check.js)")
class TheCodeRuns(unittest.TestCase):
    def test_the_two_parts_on_a_fake_page(self):
        done = subprocess.run([NODE, CHECK, LOBBY], capture_output=True, text=True, timeout=300, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout[-4000:] + done.stderr[-2000:])
        self.assertRegex(done.stdout, r"\d+ checks, 0 failed")

    def test_the_real_dock_with_a_fake_hub(self):
        done = subprocess.run([NODE, DOCK_CHECK, LOBBY], capture_output=True, text=True, timeout=300, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout[-4000:] + done.stderr[-2000:])
        self.assertRegex(done.stdout, r"\d+ checks, 0 failed")


class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/home_images_mutants.json (the list that tools/mutate.py runs too, with `--keep-going --jobs 1`) on a COPY of the files, one at a time: each must make the checks fail, and the
    copy with nothing changed must pass"""
    FILES = (["tools/size_limits.py", "web/lobby.html", "web/rejoin_key.js", "web/alert_core.js", "web/hub_here.js", "web/front/player_name.js", "web/shapes.js", "web/home/home.js", "web/home/home.css", "web/home/chat.js", "web/home/chat.css",
              "web/home/pics.js", "web/home/pics.css", "web/home/post.js", "web/home/post.css", "tests/scripts/web_home_images_check.js", "tests/scripts/web_home_images_dock_check.js", "tests/scripts/web_page_harness.js",
              "web/matches.html", "web/match.html", "web/legal.html", "web/mapmaker.html", "web/shell.html", "web/admin.html"]
             + ["web/front/" + n for n in ("classic.css", "lobby_rules.js", "lobby_net.js", "lobby_chat.js", "lobby_chat.css", "rematch.js", "game_modes.js", "game_type.js", "room_fog.js", "room_sheet.js", "seat_menu.js", "map_picker.js", "map_maker_link.js")])

    @classmethod
    def setUpClass(cls):
        if not NODE:
            raise unittest.SkipTest("node is not installed")
        with open(os.path.join(REPO, "tests", "data", "home_images_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_home_images_mutants.")
        for name in cls.FILES:
            os.makedirs(os.path.dirname(os.path.join(cls.folder, *name.split("/"))), exist_ok=True)
            shutil.copy(os.path.join(REPO, *name.split("/")), os.path.join(cls.folder, *name.split("/")))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(getattr(cls, "folder", ""), ignore_errors=True)

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
