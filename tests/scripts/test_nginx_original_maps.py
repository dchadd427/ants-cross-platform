#!/usr/bin/env python3
"""The six original maps can be fetched from the site, so that the Map Maker can open one as a starting point (run by ./run_tests.sh; the live part needs ants_server and the program nginx).

The Map Maker's "Start from a game map" downloads the chosen map from /maps/files/<name> (web/front/mapmaker_store.js). The site's folder of level files used to hold the players' maps only, and the game
server answers that address for the maps the owner approved, so TINY.LVL, SMALL.LVL, MEDIUM.LVL, GAUNTLET.LVL, TREASURE.LVL and ISLANDS.LVL (the entries of the lobby's list with "original": true) were a 404 and the
editor said "check your connection" for each. The web image now puts the six files of Original-Ants/Maps (read only) into the same folder (Dockerfile), where nginx serves them under the rules of every
map file: this test lays a folder out as the Dockerfile's COPY lines do, puts the real ants_server's list and the real blocks of docker/nginx.conf in front of it, and fetches every original that the list names.
"""
import glob
import hashlib
import json
import os
import re
import shlex
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import submissions_server_check as live  # noqa: E402
import test_nginx_maps as maps  # noqa: E402

REPO = maps.REPO
ORIGINALS = ("TINY.LVL", "SMALL.LVL", "MEDIUM.LVL", "GAUNTLET.LVL", "TREASURE.LVL", "ISLANDS.LVL")       # (the lobby's six: web/lobby_rules.js MAPS, MapStore::originals())
FILES_FOLDER = "/srv/community-maps/files/"


def image_files():
    """{name: source path} of the folder /srv/community-maps/files/ as the web image's Dockerfile fills it: every COPY line with that destination, its wildcards read against the checkout, in order."""
    out = {}
    for line in maps.read("Dockerfile").replace("\\\n", " ").splitlines():
        if "/srv/community-maps" in line.split("#", 1)[0]:
            known = line.startswith("COPY ") and shlex.split(line.split("#", 1)[0])[-1] in (FILES_FOLDER, "/srv/community-maps/previews/")
            assert known, "a Dockerfile line puts something under /srv/community-maps in a way this test does not read (a file as the destination, ADD, RUN cp, ...): " + line
        if not line.startswith("COPY "):
            continue
        words = shlex.split(line.split("#", 1)[0])
        if len(words) < 3 or words[-1] != FILES_FOLDER:
            continue
        for pattern in words[1:-1]:
            for path in sorted(glob.glob(os.path.join(REPO, pattern))):
                name = os.path.basename(path)
                path = os.path.normpath(path)
                assert out.get(name, path) == path, "two COPY lines put %s into the folder: %s and %s" % (name, out[name], path)       # (Windows folds case: *.lvl and *.LVL find the same files there)
                out[name] = path
    return out


def sha(data):
    return hashlib.sha256(data).hexdigest()


class TheImageHoldsTheOriginals(unittest.TestCase):
    def test_the_dockerfile_puts_the_six_originals_into_the_folder_that_nginx_serves_files_from(self):
        folder = image_files()
        for name in ORIGINALS:
            self.assertIn(name, folder, name + " is not copied into the web image's folder of map files")
            self.assertEqual(folder[name], os.path.normpath(os.path.join(REPO, "Original-Ants", "Maps", name)), name)
        self.assertIn("alias /srv/community-maps/files/;", maps.FILES)                       # (the folder of the Dockerfile is the one the location serves)

    def test_the_originals_are_exactly_the_files_of_original_ants_maps(self):
        self.assertEqual(sorted(os.listdir(os.path.join(REPO, "Original-Ants", "Maps"))), sorted(ORIGINALS))        # (a seventh file would be served and not be an original of the list)

    def test_no_players_map_has_the_name_of_an_original_in_any_letter_case(self):
        # one folder holds both: a players' map called tiny.lvl would, on a disk that folds case, be the original (or the other way round)
        folder = image_files()
        low = {}
        for name in folder:
            low.setdefault(name.lower(), []).append(name)
        self.assertEqual([names for names in low.values() if len(names) > 1], [])
        for name in ORIGINALS:
            self.assertEqual(low[name.lower()], [name])

    def test_the_opt_in_browser_check_is_there_and_says_what_it_covers(self):
        script = maps.read("tests", "scripts", "test_web_mapmaker_open.sh")
        self.assertIn("OPT-IN, not part of ./run_tests.sh", script)
        self.assertIn("web_mapmaker_open_browser.py", script)
        self.assertTrue(os.access(os.path.join(REPO, "tests", "scripts", "test_web_mapmaker_open.sh"), os.X_OK))
        text = maps.read("tests", "scripts", "web_mapmaker_open_browser.py")
        for word in ORIGINALS + ("not available from the site right now", "The site could not send this map", "Check your connection"):
            self.assertIn(word, text)

    def test_every_original_fits_the_limits_of_a_map_file_of_the_location(self):
        pattern = maps.name_pattern(maps.FILES, "files")
        for name in ORIGINALS:
            self.assertTrue(pattern.match("/maps/files/" + name), name)
            size = os.path.getsize(os.path.join(REPO, "Original-Ants", "Maps", name))
            self.assertTrue(0 < size < 1024 * 1024, name)                                         # (the door's 1 MiB and the editor's 4 MiB are far above)


class Rig(maps.Asking):
    """The real docker/nginx.conf (maps.make_config: the folders and the port of a temporary folder put in, the allowances of the maps lifted) in front of the REAL game server's replay port, with the
    folder of map files laid out as the web image's Dockerfile does."""
    def __init__(self, replay_port):
        self.work = tempfile.mkdtemp(prefix="ants_original_rig_")
        for sub in ("srv/previews", "srv/files", "cache/ants_stats", "cache/ants_history", "tmp"):
            os.makedirs(os.path.join(self.work, sub), exist_ok=True)
        for name, path in image_files().items():
            shutil.copyfile(path, os.path.join(self.work, "srv", "files", name))
        self.port = maps.free_port()
        conf = os.path.join(self.work, "nginx.conf")
        with open(conf, "w") as f:
            f.write(maps.make_config(self.work, self.port, replay_port, relaxed=True))
        test = subprocess.run(["nginx", "-t", "-p", self.work, "-c", conf], capture_output=True, text=True)
        if test.returncode != 0:
            raise AssertionError("nginx -t: " + test.stderr)
        self.proc = subprocess.Popen(["nginx", "-p", self.work, "-c", conf, "-g", "daemon off;"], stderr=subprocess.DEVNULL)
        for _ in range(100):
            try:
                socket.create_connection(("127.0.0.1", self.port), timeout=0.2).close()
                break
            except OSError:
                time.sleep(0.05)

    def stop(self):
        self.proc.terminate()
        self.proc.wait(timeout=10)
        shutil.rmtree(self.work, ignore_errors=True)


@unittest.skipUnless(os.path.exists(live.SERVER) and shutil.which("nginx"), "ants_server or nginx is missing: the originals were NOT fetched through nginx")
class TheOriginalsAreServedThroughNginx(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.mkdtemp(prefix="ants_original_maps_")
        cls.server = live.Server(os.path.join(cls.work, "results"))
        cls.rig = Rig(cls.server.replay_port)

    @classmethod
    def tearDownClass(cls):
        cls.rig.stop()
        cls.server.stop()
        shutil.rmtree(cls.work, ignore_errors=True)

    def original(self, name):
        with open(os.path.join(REPO, "Original-Ants", "Maps", name), "rb") as f:
            return f.read()

    def test_the_servers_list_marks_the_six_and_every_one_of_them_is_served_byte_for_byte(self):
        status, _, body = self.rig.ask("GET", "/maps/all")
        self.assertEqual(status, 200)
        marked = [m["file"] for m in json.loads(body)["maps"] if m.get("original")]
        self.assertEqual(sorted(marked), sorted(ORIGINALS))
        status, _, body = self.rig.ask("GET", "/maps")
        self.assertEqual(sorted(m["file"] for m in json.loads(body)["maps"] if m.get("original")), sorted(ORIGINALS))
        for name in marked:                                                                    # (what the editor's picker offers is what it can fetch)
            status, headers, got = self.rig.ask("GET", "/maps/files/" + maps.quote(name))
            self.assertEqual(status, 200, name)
            self.assertEqual(sha(got), sha(self.original(name)), name)

    def test_the_headers_are_those_of_every_map_file(self):
        status, headers, body = self.rig.ask("GET", "/maps/files/TREASURE.LVL")
        self.assertEqual(status, 200)
        self.assertEqual(self.rig.names(headers, "cache-control"), ["public, max-age=3600"])
        self.assertEqual(self.rig.names(headers, "content-type"), ["application/octet-stream"])
        self.assertEqual(self.rig.names(headers, "x-content-type-options"), ["nosniff"])
        self.assertEqual(self.rig.names(headers, "content-length"), [str(len(body))])
        self.assertEqual(self.rig.ask("HEAD", "/maps/files/TREASURE.LVL")[0], 200)

    def test_the_servers_own_file_is_the_same_file(self):
        for name in ORIGINALS:
            self.assertEqual(sha(self.rig.ask("GET", "/maps/files/" + name)[2]), sha(self.original(name)), name)

    def test_another_spelling_of_the_name_is_a_404_and_never_another_map(self):
        for name in ORIGINALS:
            for spelling in (name.lower(), name.capitalize(), name[:-3] + "lvl", name[:-4] + ".Lvl", name + " ", name[:-4] + " 2.LVL"):
                self.assertEqual(self.rig.ask("GET", "/maps/files/" + maps.quote(spelling))[0], 404, repr(spelling))

    def test_traversal_odd_names_queries_and_other_methods_are_refused(self):
        for target in ("/maps/files/..%2fTINY.LVL", "/maps/files/%2e%2e%2fOriginal-Ants%2fMaps%2fTINY.LVL", "/maps/files/../TINY.LVL", "/maps/files/TINY.LVL/", "/maps/files/TINY.LVL%00",
                       "/maps/files/TINY.LVL?x=1", "/maps/files/TINY.LVL%2f", "/maps/files/.LVL", "/maps/files/TINY.LVL.txt"):
            try:
                status = self.rig.ask("GET", target)[0]
            except OSError:
                status = 400
            self.assertIn(status, (400, 404), target)
        for method in ("POST", "PUT", "DELETE"):
            self.assertEqual(self.rig.ask(method, "/maps/files/TINY.LVL", {"Content-Length": "0"})[0], 405, method)

    def test_the_players_maps_and_the_lists_are_as_before(self):
        folder = os.path.join(REPO, "Community-Maps")
        for name in ("Treasure 2.lvl", "Mega Treasure.lvl", "!!!!!!!!!!!!!!!! Danger_World!_II!!!.lvl"):
            with open(os.path.join(folder, name), "rb") as f:
                want = f.read()
            status, _, got = self.rig.ask("GET", "/maps/files/" + maps.quote(name))
            self.assertEqual((status, sha(got)), (200, sha(want)), name)
        self.assertNotEqual(sha(self.original("TREASURE.LVL")), sha(self.rig.ask("GET", "/maps/files/" + maps.quote("Treasure 2.lvl"))[2]))        # (a community map of the same size is another map)
        self.assertEqual(self.rig.ask("GET", "/maps/files/Nothing%20Here.LVL")[0], 404)


if __name__ == "__main__":
    unittest.main()
