#!/usr/bin/env python3
"""The players' maps in the browser (run by ./run_tests.sh --fast and by the CI): the game's file system holds the six originals only, so the page fetches the map that a room (or a recording) plays
from /maps/files/<name>, checks its hash and writes it where the game looks, before the match loads (docs/PLAY_IN_BROWSER.md "Players' maps").

  - the page's own code is RUN (node, when it is installed): the hash, the address, every way a download can end, that a failure is not kept, the head of a recording (tests/scripts/web_map_files_check.js),
    and the check is shown able to fail: broken copies of the script (the hash not compared, the wrong multiplier, a failure kept, the cache skipped for nothing) are refused by it;
  - the game's side is read from the source: the calls of the web build's fetcher are the page's functions, the game asks in the room and at the Start, and the page loads the script and makes the object;
  - the hash is FNV-1a 64 as the C++ computes it (the real map of the repository is hashed here, in python, and by the script, and the C++ side is tests/test_app/test_map_fetch_app.cpp).
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCRIPT = os.path.join(REPO, "web", "map_files.js")
CHECK_JS = os.path.join(REPO, "tests", "scripts", "web_map_files_check.js")
HEAD = os.path.join(REPO, "tests", "data", "replay_head.hex")
REAL_MAP = os.path.join(REPO, "Community-Maps", "eze jungal.lvl")
MAPS_JSON = os.path.join(REPO, "Community-Maps", "maps.json")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def fnv1a64(data):
    h = 0xCBF29CE484222325
    for b in data:
        h ^= b
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % h


def run_check(script=SCRIPT):
    with open(REAL_MAP, "rb") as f:
        digest = fnv1a64(f.read())
    return subprocess.run([shutil.which("node"), CHECK_JS, script, HEAD, REAL_MAP, digest, MAPS_JSON], capture_output=True, text=True, timeout=120)


class TheGameSide(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fetch = read("src", "ants_app", "map_fetch.cpp")
        cls.maps = read("src", "ants_app", "application_maps.cpp")
        cls.page = read("web", "shell.html")
        cls.script = read("web", "map_files.js")

    def test_the_web_fetchers_calls_are_the_scripts_functions(self):
        self.assertTrue("window.antsMapFiles.request(" in self.fetch)
        self.assertTrue("window.antsMapFiles.status(" in self.fetch)
        self.assertTrue("window.antsMapFiles.message(" in self.fetch)
        for name in ("request", "status", "message"):
            self.assertRegex(self.script, r"return \{ request: request, status: status, message: message", name)
        self.assertRegex(self.script, r"PENDING = 0, READY = 1, FAILED = 2")                      # the numbers that map_fetch.cpp reads
        self.assertTrue("s === 2" in self.fetch)
        self.assertTrue("s == 1 ? State::Ready : s == 2 ? State::Failed" in self.fetch)

    def test_the_fetcher_exists_in_the_web_build_only(self):
        body = self.fetch[self.fetch.index("std::unique_ptr<MapFetcher> make_web_map_fetcher()"):]
        self.assertRegex(body, r"#if defined\(__EMSCRIPTEN__\)\s+return std::make_unique<WebMapFetcher>\(\);\s+#else\s+return nullptr;")

    def test_the_game_asks_in_the_room_without_a_hash_and_at_the_start_with_the_hash_and_waits(self):
        self.assertRegex(self.maps, r"map_fetcher_->request\(config_\.maps_dir, name, 0\)")
        self.assertRegex(self.maps, r"map_fetcher_->request\(config_\.maps_dir, start\.map_name, start\.map_hash\)")
        self.assertTrue("State::Pending) return;" in self.maps)                                       # the lock-step never waits inside a call
        self.assertTrue("report_loaded(false, status.message)" in self.maps)                          # a map that cannot be had: the start is failed for this machine, with the reason
        self.assertTrue("net_finish_load();" in self.maps)                                            # the hash of the Start is checked by the game as ever, on the file that is here

    def test_the_page_loads_the_script_and_makes_the_object_the_game_calls(self):
        self.assertTrue('<script src="map_files.js"></script>' in self.page)
        self.assertTrue("window.antsMapFiles = window.AntsMapFiles ? window.AntsMapFiles.forPage(window," in self.page)
        self.assertTrue("AntsMapFiles.mapOfReplay(ANTS_LIVE_MODE ? latest : got.bytes)" in self.page)                              # a recording of a map of the players

    def test_the_script_is_in_the_image_and_the_ci_looks_for_it(self):
        self.assertTrue("COPY web/map_files.js /usr/share/nginx/html/map_files.js" in read("Dockerfile"))
        self.assertTrue("replay_page.js map_files.js index.js" in read(".github", "workflows", "ci.yml"))

    def test_the_address_is_one_that_nginx_serves(self):
        conf = read("docker", "nginx.conf")
        self.assertTrue("location ^~ /maps/files/" in conf)
        self.assertNotIn("(window.AbortController", self.script)


@unittest.skipUnless(shutil.which("node"), "node is not installed: the page's own code for the players' maps was NOT run (tests/scripts/web_map_files_check.js)")
class ThePagesOwnCode(unittest.TestCase):
    def test_the_hash_the_address_and_every_way_a_download_ends(self):
        done = run_check()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("ok: ", done.stdout)

    def test_the_hash_is_the_c_plus_plus_one(self):
        with open(REAL_MAP, "rb") as f:
            digest = fnv1a64(f.read())
        done = subprocess.run([shutil.which("node"), "-e", "const M=require(%r);const b=require('fs').readFileSync(%r);console.log(M.hash64(b))" % (SCRIPT, REAL_MAP)], capture_output=True, text=True, timeout=60)
        self.assertEqual(done.stdout.strip(), digest)

    def test_the_check_can_fail(self):
        text = read("web", "map_files.js")
        broken = {
            "the hash of the Start is not compared": ("if (want && h !== want) {", "if (false) {"),
            "a wrong multiplier": ("var low = lo * 0x1b3;", "var low = lo * 0x1b5;"),
            "the high word is dropped": ("+ carry + (lo << 8)) >>> 0;", "+ carry) >>> 0;"),
            "a wrong prefetched copy stays in the game": ("if (result.mismatch) discard(e, name);", ""),
            "a prefetch keeps the old hash": ("e.expect = hash || '';", "if (hash) e.expect = hash;"),
            "the announced size is not looked at": ("if (announced > MAX_BYTES)", "if (false)"),
            "a failure is kept": ("            e.folder = folder;\n", "            e.folder = folder;\n            if (e.state === FAILED) return;\n"),
            "the cache is skipped for nothing": ("if (fresh) init.cache = 'reload';", "init.cache = 'reload';"),
            "a web page is taken for a map": ("if (/^\\s*text\\/html/i.test(type))", "if (false)"),
            "a map of any size is taken": ("buffer.byteLength > MAX_BYTES", "false"),
            "a 404 is tried again": ("finish({ ok: false, retry: false, text: 'The map ' + name + ' is not on the server", "finish({ ok: false, retry: true, text: 'The map ' + name + ' is not on the server"),
            "the address is not encoded": ("out += /[A-Za-z0-9\\-._~]/.test(c) ?", "out += /[A-Za-z0-9\\-._~ ()!']/.test(c) ?"),
            "warm asks for a bad name": ("if (!win.fetch || !validName(name)) return;", "if (!win.fetch) return;"),
            "warm has no time limit": ("if (win.setTimeout) timer = win.setTimeout(abort, TIMEOUT_MS);", ""),
            "warm waits longer than a real try": ("timer = win.setTimeout(abort, TIMEOUT_MS);", "timer = win.setTimeout(abort, TIMEOUT_MS * 2);"),
            "warm reads a body of any size": ("announced > MAX_BYTES) { abort(); return null; }", "false) { abort(); return null; }"),
            "warm cancels a body of exactly MAX_BYTES": ("announced > MAX_BYTES) { abort(); return null; }", "announced >= MAX_BYTES) { abort(); return null; }"),
            "warm reads an answer that is no file": ("if (!r.ok || announced > MAX_BYTES) { abort(); return null; }", "if (announced > MAX_BYTES) { abort(); return null; }"),
            "warm leaves its timer running": ("}).then(stop, function () { stop(); /* the game page asks again */ });", "}).then(undefined, function () { /* the game page asks again */ });"),
            "warm leaves its timer running after a failure": ("}).then(stop, function () { stop(); /* the game page asks again */ });", "}).then(stop, function () { /* the game page asks again */ });"),
            "a failure is not said in the toast": ("if (state !== FAILED || !toast) return;", "return;"),
            "an original is fetched": ("&& !ORIGINALS.test(name) ? { name: name", "? { name: name"),
            "an original's file is a map of the players": ("return validName(name) && !ORIGINALS.test(name) ? name : '';", "return validName(name) ? name : '';"),
            "a game for one starts without its map": ("if (!file) { go(); return; }", "if (!file) { return; }"),
            "a game on an original fetches a map too": ("if (!file) { go(); return; }", "if (false) { go(); return; }"),
            "a game for one starts when its map failed": ("if (!map.ok) { fail(map.text); return; }\n            getModule()", "if (!map.ok) { fail(map.text); }\n            getModule()"),
            "a game for one does not write its map": ("getModule().preRun.push(writer(function () { return getModule().FS; }, file, map.bytes));", ""),
            "a game for one asks for a hash": ("files.fetchOnly(file, '')", "files.fetchOnly(file, 'abcdabcdabcdabcd')"),
            "a path is a map name": ("if (/[\\/\\\\:*?\"<>|]/.test(name) || name.charAt(0) === '.') return false;", ""),
        }
        with tempfile.TemporaryDirectory() as folder:
            for why, (old, new) in broken.items():
                self.assertEqual(text.count(old), 1, why)
                path = os.path.join(folder, "map_files.js")
                with open(path, "w", encoding="utf-8") as f:
                    f.write(text.replace(old, new))
                done = run_check(path)
                self.assertNotEqual(done.returncode, 0, "the check accepted a script where " + why)


if __name__ == "__main__":
    unittest.main()
