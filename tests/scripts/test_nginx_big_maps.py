#!/usr/bin/env python3
"""Approved maps over 1 MiB are served end to end: the real ants_server behind the real blocks of docker/nginx.conf (run by ./run_tests.sh; the program nginx, ants_server and lvl_tool must be there).

An Extended map is up to 4 MiB (a 512 x 512 map is 3.1 MB) and the game server's public door answers at most 1 MiB for everything else, so the file of a big approved map used to be an HTTP 500 ("internal error") on
GET /maps/files/<name>.lvl: the map was in the lists, a leader could choose it, and no browser could fetch it. Here real maps of 256 x 256, 300 x 300, 400 x 400 and 512 x 512 are sent, approved by the owner
and fetched through nginx: each answer is 200, byte for byte the file that the game server keeps, and a map that is bigger than the rooms play (OnlineLimits: 65,536 cells) says so in the list ("online": false)
while its file and picture are still served (it can be opened in the Map Maker and played on the player's own computer).
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import submissions_server_check as live  # noqa: E402
import test_nginx_submissions as nginx_rig  # noqa: E402

SIZES = (256, 300, 400, 512)
HEADERS = {"Content-Type": "application/octet-stream", "Sec-Fetch-Site": "same-origin"}


def have_nginx():
    return shutil.which("nginx") is not None


@unittest.skipUnless(os.path.exists(live.SERVER) and os.path.exists(live.LVL_TOOL) and have_nginx(), "ants_server, lvl_tool or nginx is missing: big maps were NOT served end to end")
class BigMapsThroughNginx(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.mkdtemp(prefix="ants_big_maps_")
        cls.results = os.path.join(cls.work, "results")
        cls.files = {}
        tiny = os.path.join(live.REPO, "Original-Ants", "Maps", "TINY.LVL")
        for side in SIZES:
            path = os.path.join(cls.work, "big%d.lvl" % side)
            done = subprocess.run([live.LVL_TOOL, "convert", tiny, "--to", "extended", "-o", path, "--rows", str(side), "--columns", str(side)], capture_output=True, text=True, timeout=120)
            assert done.returncode == 0, done.stdout + done.stderr
            with open(path, "rb") as f:
                cls.files[side] = f.read()
        starting, _ = live.create_admin(cls.results)
        cls.server = live.Server(cls.results)
        owner = live.Session(cls.server, "Owner", starting)
        owner.change_password("", live.CHOSEN)
        cls.names = {}
        for index, side in enumerate(SIZES):
            name = "Big Map %d" % side
            query = "kind=extended&name=%s&maker=Mia&note=x" % name.replace(" ", "%20")
            status, headers, body = cls.server.call("POST", "/submissions?" + query, dict(HEADERS, **{"X-Client-Address": "203.0.113.%d" % (10 + index)}), cls.files[side])
            assert status == 201, (side, status, body[:200])
            sid = json.loads(body)["submission"]["id"]
            approved = owner.call("POST", "/admin/api/maps/%s/approve" % sid, {})
            assert approved[0] == 200, (side, approved[0], approved[2][:200])
            cls.names[side] = name + ".lvl"
        cls.rig = nginx_rig.Rig(real_ports=(cls.server.submit_port, cls.server.replay_port))

    @classmethod
    def tearDownClass(cls):
        cls.rig.stop()
        cls.server.stop()
        shutil.rmtree(cls.work, ignore_errors=True)

    def target(self, kind, side):
        stem = self.names[side][:-4].replace(" ", "%20")
        return "/maps/files/%s.lvl" % stem if kind == "files" else "/maps/previews/%s.png" % stem

    def test_the_sizes_are_what_the_test_means_them_to_be(self):
        self.assertTrue(len(self.files[256]) < 1024 * 1024 < len(self.files[300]))                 # (the 1 MiB of the door lies between these two)
        self.assertTrue(len(self.files[512]) > 3 * 1000 * 1000)                                    # (a real 512 x 512 map)

    def test_the_game_servers_own_door_sends_every_file_whole(self):
        for side in SIZES:
            status, headers, body = self.server.call("GET", self.target("files", side), door="replay")
            self.assertEqual(status, 200, "%d x %d: the replay door answered %d %r" % (side, side, status, body[:80]))
            self.assertEqual(headers["content-type"], "application/octet-stream")
            self.assertEqual(struct_version(body), 9)
            self.assertTrue(len(body) > (3 * 1000 * 1000 if side == 512 else 700 * 1000), len(body))

    def test_nginx_passes_every_file_on_whole_and_the_picture_too(self):
        for side in SIZES:
            direct = self.server.call("GET", self.target("files", side), door="replay")[2]
            status, headers, body = self.rig.ask("GET", self.target("files", side))
            self.assertEqual(status, 200, "%d x %d through nginx: %d %r" % (side, side, status, body[:80]))
            self.assertEqual(hashlib.sha256(body).hexdigest(), hashlib.sha256(direct).hexdigest())
            self.assertIn(("cache-control", "public, max-age=300"), headers)
            status, headers, picture = self.rig.ask("GET", self.target("previews", side))
            self.assertEqual((status, picture[:4]), (200, b"\x89PNG"), "%d x %d: the picture" % (side, side))

    def test_a_downloaded_big_map_is_a_map_that_the_checker_accepts(self):
        status, headers, body = self.rig.ask("GET", self.target("files", 512))
        self.assertEqual(status, 200)
        path = os.path.join(self.work, "downloaded512.lvl")
        with open(path, "wb") as f:
            f.write(body)
        done = subprocess.run([live.LVL_TOOL, "check", path, "--extended"], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout)

    def test_the_list_says_which_maps_a_room_does_not_play(self):
        status, headers, body = self.rig.ask("GET", "/maps/all")
        self.assertEqual(status, 200)
        listed = dict((m["file"], m) for m in json.loads(body)["maps"])
        self.assertNotIn("online", listed[self.names[256]])                                        # (256 x 256 is the most that a room plays)
        for side in (300, 400, 512):
            self.assertIs(listed[self.names[side]].get("online"), False, side)
            self.assertEqual(listed[self.names[side]]["kind"], "extended")
        status, headers, body = self.rig.ask("GET", "/maps")
        self.assertEqual(status, 200)
        self.assertEqual([m for m in json.loads(body)["maps"] if m["file"] in self.names.values()], [])       # (an older page never sees an Extended map)


def struct_version(data):
    return int.from_bytes(data[:4], "little")


if __name__ == "__main__":
    unittest.main()
