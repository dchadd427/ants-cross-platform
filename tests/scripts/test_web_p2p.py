#!/usr/bin/env python3
"""The game page's data channels (web/p2p.js, docs/P2P_HINTS.md; run by ./run_tests.sh --fast and by CI).

The game's hints (a player's orders, sent to the other players ahead of their sealed turns) travel between the browsers over WebRTC data channels, always through the operator's relay unless two players
both turned on direct connections. What this file checks:

  - the code RUNS in node (tests/scripts/web_p2p_check.js: a fake RTCPeerConnection, the rule of the address for every pair of settings, the browser's own choice, who offers and who answers, the hints, the
    echo that measures the round trip to each player, a channel that fails);
  - the files, where no run of the page's code can look: the game page loads the script before the inline script that makes its arguments, the Dockerfile's COPY steps put the file in the image, the local
    web build and the CI's list of the image's files name it (a page without the script opens no channel, without a word), nginx serves it by the rule for scripts, it is within its byte budget
    (tools/size_limits.py), and the page's side of the setting is the one the room writes (the key `ants.direct`);
  - tests/data/p2p_mutants.json changes one thing at a time in a COPY of the files: the node check must then fail (the last class of this file; tools/mutate.py runs the same list).
"""
import gzip
import html.parser
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools"))
import size_limits  # noqa: E402
import web_without_docker as wd  # noqa: E402  (it replays the Dockerfile's steps)

SCRIPT = os.path.join(REPO, "web", "p2p.js")
CHECK = os.path.join(REPO, "tests", "scripts", "web_p2p_check.js")
NODE = shutil.which("node")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def location_of(conf, path):
    """The words and the body of the location that nginx picks for `path`: an exact one, else the longest prefix that is `^~`, else the first regular expression that matches, else the longest prefix."""
    blocks = []
    for m in re.finditer(r"^\s*location\s+(.*?)\s*\{", conf, re.M):
        depth, i = 1, m.end()
        while depth:
            depth += (conf[i] == "{") - (conf[i] == "}")
            i += 1
        blocks.append((m.group(1), conf[m.end():i - 1]))
    prefixes = []
    for words, body in blocks:
        parts = words.split()
        if parts[0] == "=" and parts[1] == path:
            return words, body
        if parts[0] == "^~" and path.startswith(parts[1]):
            prefixes.append((len(parts[1]), words, body, True))
        elif parts[0] not in ("=", "~", "~*") and not parts[0].startswith("@") and path.startswith(parts[0]):
            prefixes.append((len(parts[0]), words, body, False))
    best = max(prefixes, default=None, key=lambda p: p[0])
    if best and best[3]:
        return best[1], best[2]
    for words, body in blocks:
        parts = words.split(None, 1)
        if parts[0] in ("~", "~*"):
            pattern = parts[1].strip('"')
            try:
                if re.search(pattern, path, re.I if parts[0] == "~*" else 0):
                    return words, body
            except re.error:
                continue
    return (best[1], best[2]) if best else (None, None)


class Scripts(html.parser.HTMLParser):
    """The scripts of a page in the order of the page: ("src", the address) for a script that is loaded, ("inline", the text) for one that is written in it."""

    def __init__(self):
        super().__init__(convert_charrefs=False)
        self.found, self.open = [], None

    def handle_starttag(self, tag, attrs):
        if tag == "script":
            src = dict(attrs).get("src")
            if src:
                self.found.append(("src", src))
            else:
                self.open = ["inline", ""]
                self.found.append(self.open)

    def handle_data(self, data):
        if self.open is not None:
            self.open[1] += data

    def handle_endtag(self, tag):
        if tag == "script":
            self.open = None


def words_of(line):
    try:
        return shlex.split(line, comments=True)
    except ValueError:
        return []


def image_holds(name):
    """The bytes that the image has at /usr/share/nginx/html/<name> from the Dockerfile's COPY steps of web/ (replayed by tools/web_without_docker.py), or None."""
    steps = wd.parse_dockerfile(read("Dockerfile"))
    last = max(step.stage for step in steps)
    fs = tempfile.mkdtemp(prefix="ants_p2p_image.")
    try:
        replay = wd.Replay(steps, REPO, fs, {})
        for folder in wd.BASE_FOLDERS:
            os.makedirs(replay.real(folder), exist_ok=True)
        for step in steps:
            if step.stage == last and step.op == "COPY" and "from" not in step.flags and step.words[0].startswith("web/"):
                replay.do_copy(step)
        path = os.path.join(fs, "usr", "share", "nginx", "html", name)
        if not os.path.isfile(path):
            return None
        with open(path, "rb") as f:
            return f.read()
    finally:
        shutil.rmtree(fs, ignore_errors=True)


class ThePageAndTheImage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.js = read("web", "p2p.js")

    def test_the_game_page_loads_the_script_once_before_the_inline_script_that_makes_the_arguments(self):
        scripts = Scripts()
        scripts.feed(read("web", "shell.html"))
        loaded = [i for i, (kind, what) in enumerate(scripts.found) if kind == "src" and what.split("?")[0] == "p2p.js"]
        called = [i for i, (kind, what) in enumerate(scripts.found) if kind == "inline" and "function joinArguments" in what and "AntsP2p" in what]
        self.assertEqual(len(loaded), 1, "the page loads p2p.js once")
        self.assertEqual(len(called), 1, "the page's arguments ask AntsP2p for the player's setting, in one script")
        self.assertLess(loaded[0], called[0], "loaded before it is called")

    def test_the_image_has_the_file(self):
        with open(SCRIPT, "rb") as f:
            source = f.read()
        self.assertEqual(image_holds("p2p.js"), source, "the Dockerfile's COPY steps of web/ put web/p2p.js in /usr/share/nginx/html")

    def test_the_local_web_build_copies_it_and_the_ci_check_of_the_image_lists_it(self):
        copied = []
        for line in read("build_web.sh").splitlines():
            words = words_of(line)
            if words[:2] == ["cp", "-f"] and len(words) > 3:
                copied += [(word, words[-1]) for word in words[2:-1]]
        self.assertIn(("${SCRIPT_DIR}/web/p2p.js", "${DIST_DIR}/"), copied)
        listed = []
        for line in read(".github", "workflows", "ci.yml").splitlines():
            if "ls -l index.html" in line:
                listed += [word for command in words_of(line)[-1:] for word in command.split()]
        self.assertIn("p2p.js", listed, "the image check of the CI lists the files that the page needs")

    def test_nginx_serves_it_by_the_rule_for_scripts_that_revalidates(self):
        words, body = location_of(re.sub(r"(?m)#.*$", "", read("docker", "nginx.conf")), "/p2p.js")
        self.assertIsNotNone(words)
        self.assertIn('add_header Cache-Control "no-cache, must-revalidate" always;', body, "the location %s" % words)
        self.assertNotIn("expires", body)

    def test_the_setting_is_the_one_the_room_writes(self):
        """the page reads `ants.direct` and the room (web/front/lobby_direct.js) writes it: one name in two files, and a different one would leave every player on the relay without a word"""
        self.assertIn("var SETTING = 'ants.direct';", self.js)
        room = read("web", "front", "lobby_direct.js")
        self.assertIn("'ants.direct'", room)

    def test_it_is_within_its_download_budget(self):
        data = self.js.encode("utf-8")
        self.assertLess(len(data), size_limits.P2P_BUDGET, "p2p.js is %d bytes" % len(data))
        self.assertLess(len(gzip.compress(data, 9)), size_limits.P2P_GZIP_BUDGET, "and %d bytes gzipped" % len(gzip.compress(data, 9)))


@unittest.skipUnless(NODE, "node is not installed: the data channels were NOT run (tests/scripts/web_p2p_check.js)")
class TheCodeRuns(unittest.TestCase):
    def test_the_lock_against_fakes_for_the_browser_the_page_and_the_game(self):
        done = subprocess.run([NODE, CHECK, SCRIPT], capture_output=True, text=True, timeout=300, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout[-4000:] + done.stderr[-2000:])
        self.assertRegex(done.stdout, r"ok \d+ checks, 0 failed")


class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/p2p_mutants.json (the list that tools/mutate.py runs too, with `--keep-going --jobs 1`) on a COPY of the files, one at a time: each must make the check fail, and the copy
    with nothing changed must pass"""
    FILES = ["web/p2p.js", "tests/scripts/web_p2p_check.js"]

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "p2p_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_p2p_mutants.")
        for name in cls.FILES:
            os.makedirs(os.path.dirname(os.path.join(cls.folder, *name.split("/"))), exist_ok=True)
            shutil.copy(os.path.join(REPO, *name.split("/")), os.path.join(cls.folder, *name.split("/")))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.folder, ignore_errors=True)

    def run_command(self, command):
        done = subprocess.run(command, shell=True, cwd=self.folder, capture_output=True, text=True, timeout=600)
        return done.returncode, done.stdout + done.stderr

    @unittest.skipUnless(NODE, "node is not installed: the mutants of the data channels were NOT run")
    def test_the_copy_with_nothing_changed_passes(self):
        for command in sorted({m["test"] for m in self.spec["mutants"]} | {self.spec["baseline"]["test"]}):
            status, out = self.run_command(command)
            self.assertEqual(status, 0, command + "\n" + out[-3000:])

    @unittest.skipUnless(NODE, "node is not installed: the mutants of the data channels were NOT run")
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
        self.assertEqual(missed, [], "the check did not see these changes")


if __name__ == "__main__":
    unittest.main()
