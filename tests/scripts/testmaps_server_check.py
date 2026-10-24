#!/usr/bin/env python3
"""The test maps of the Map Maker end to end, with the real program and NO database (suite 2.19.19 of ./run_tests.sh; docs/MAP_SUBMISSIONS.md, "Test maps").

The real ants_server (BUILD_DIR/src/ants_server/ants_server) is started with --no-db, no --submit-port and a --test-map-port, a folder of its own for its results, and what the site's nginx would do done by hand
(the headers X-Client-Address and Sec-Fetch-Site). A real map of the original game is sent to POST /test-maps and is kept (201, the canonical file, in a folder of the server's user only); the replay door,
which is where a guest's page downloads a map from, gives the file by its name; no list shows it; the submissions are not served on any port and there is no database file; garbage, the wrong type, the wrong
method, another path and a body over 2 MiB are refused; a restart wipes the folder; with no folder, or without the port, the server starts and test rooms are off. The server's log holds no address and no
map name. Exit 0: all passed (or the program is not built: said, and 0).
"""
import http.client
import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
BUILD = os.environ.get("BUILD_DIR", "build")
SERVER = os.path.join(REPO, BUILD, "src", "ants_server", "ants_server")
LVL_TOOL = os.path.join(REPO, BUILD, "lvl_tool")
MAPS = os.path.join(REPO, "Original-Ants", "Maps")
OCTET = {"Content-Type": "application/octet-stream", "Sec-Fetch-Site": "same-origin"}


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class Server:
    def __init__(self, results, test_port=True, extra_args=(), wait_for="test"):
        self.results = results
        self.test_port = free_port() if test_port else 0
        self.replay_port = free_port()
        self.ws_port = free_port()
        self.args = [SERVER, "--maps", MAPS, "--port", "0", "--ws-port", str(self.ws_port), "--replay-port", str(self.replay_port), "--no-db"]
        if test_port:
            self.args += ["--test-map-port", str(self.test_port)]
        if results:
            self.args += ["--results-dir", results]
        self.args += list(extra_args)
        self.proc = subprocess.Popen(self.args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        wanted = self.test_port if wait_for == "test" else self.replay_port
        for _ in range(300):
            try:
                socket.create_connection(("127.0.0.1", wanted), 0.2).close()
                return
            except OSError:
                if self.proc.poll() is not None:
                    break
                time.sleep(0.05)
        err = self.proc.communicate(timeout=10)[1] if self.proc.poll() is not None else ""
        raise AssertionError("the server did not start: " + err)

    def stop(self):
        if self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
        try:
            return self.proc.communicate(timeout=15)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            return self.proc.communicate()

    def call(self, method, path, headers=None, body=None, door="test"):
        port = {"test": self.test_port, "replay": self.replay_port, "ws": self.ws_port}[door]
        c = http.client.HTTPConnection("127.0.0.1", port, timeout=20)
        h = dict(headers or {})
        if body is not None:
            h["Content-Length"] = str(len(body))
        c.request(method, path, body=body, headers=h)
        r = c.getresponse()
        data = r.read()
        out = (r.status, dict((k.lower(), v) for k, v in r.getheaders()), data)
        c.close()
        return out

    def head_only(self, method, path, length, headers=None):
        """A request that says it has a body of `length` bytes and sends none: what the door answers on the head alone."""
        s = socket.create_connection(("127.0.0.1", self.test_port), timeout=10)
        try:
            lines = ["%s %s HTTP/1.1" % (method, path), "Host: x", "Content-Length: %d" % length] + ["%s: %s" % kv for kv in (headers or {}).items()]
            s.sendall(("\r\n".join(lines) + "\r\n\r\n").encode())
            return int(s.recv(4096).split(b" ", 2)[1])
        finally:
            s.close()


def addr(n):
    return {"X-Client-Address": "203.0.113.%d" % n}


def j(triple):
    return json.loads(triple[2].decode("utf-8"))


@unittest.skipUnless(os.path.exists(SERVER), "ants_server is not built (BUILD_DIR=%s): the test maps were NOT run end to end" % BUILD)
class TheWholeWay(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.mkdtemp(prefix="ants_test_maps_e2e_")
        cls.results = os.path.join(cls.work, "results")
        os.makedirs(cls.results)
        with open(os.path.join(MAPS, "TINY.LVL"), "rb") as f:
            cls.tiny = f.read()
        cls.server = Server(cls.results)
        cls.logs = ""

    @classmethod
    def tearDownClass(cls):
        out, err = cls.server.stop()
        shutil.rmtree(cls.work, ignore_errors=True)

    def test_a_real_map_is_kept_and_a_guest_can_download_it_with_no_database_and_no_submissions(self):
        s = self.server
        status, headers, body = s.call("POST", "/test-maps?kind=classic&name=Popcorn%20Remix", dict(OCTET, **addr(1)), self.tiny)
        self.assertEqual(status, 201, body)
        test_map = json.loads(body)["test_map"]
        self.assertEqual((test_map["name"], test_map["kind"], test_map["players"], test_map["usable"]), ("Popcorn Remix", "classic", 4, [True] * 4))
        self.assertRegex(test_map["file"], r"^Popcorn Remix ~test-[0-9a-f]{8}\.lvl$")
        folder = os.path.join(self.results, "test-maps")
        files = os.listdir(folder)
        self.assertEqual(len(files), 1)
        self.assertRegex(files[0], r"^test-[0-9a-f]{8}\.lvl$")
        with open(os.path.join(folder, files[0]), "rb") as f:
            kept = f.read()
        self.assertEqual(len(kept), test_map["size"])
        if os.name == "posix":
            self.assertEqual(os.stat(folder).st_mode & 0o777, 0o700)
            self.assertEqual(os.stat(os.path.join(folder, files[0])).st_mode & 0o777, 0o600)
        # the guest's page asks the replay door (nginx's fallback for /maps/files/<name>) and gets the canonical file
        status, headers, body = s.call("GET", "/maps/files/" + urllib.parse.quote(test_map["file"]), door="replay")
        self.assertEqual((status, body), (200, kept))
        # on no list
        for path in ("/maps", "/maps/all"):
            status, headers, body = s.call("GET", path, door="replay")
            self.assertNotIn(b"~test-", body, path)
            self.assertNotIn(b"Popcorn Remix", body, path)
        # no database, no submissions folder: nothing but the test maps (and the secret and the like) was made in the results folder
        self.assertFalse(os.path.exists(os.path.join(self.results, "ants.db")))
        self.assertFalse(os.path.exists(os.path.join(self.results, "submissions")))

    def test_refusals_and_the_one_route(self):
        s = self.server
        code = lambda triple: j(triple).get("error")
        self.assertEqual(code(s.call("POST", "/test-maps?kind=classic&name=Bad", dict(OCTET, **addr(10)), b"this is not a map file at all")), "not_a_map")
        self.assertEqual(s.call("POST", "/test-maps?kind=classic&name=Bad", dict({"Content-Type": "text/plain"}, **addr(11)), self.tiny)[0], 415)
        self.assertEqual(s.call("POST", "/test-maps?kind=classic&name=Bad", dict(OCTET, **{"Sec-Fetch-Site": "cross-site"}, **addr(12)), self.tiny)[0], 403)
        self.assertEqual(code(s.call("POST", "/test-maps?kind=extended&name=Bad", dict(OCTET, **addr(13)), self.tiny)), "kind_mismatch")
        self.assertEqual(s.call("POST", "/test-maps?kind=classic&name=Bad", addr(14), self.tiny)[0], 415)                 # (no content type)
        for method in ("GET", "PUT", "DELETE"):
            self.assertEqual(s.call(method, "/test-maps?kind=classic&name=Bad", dict(OCTET, **addr(15)), None if method == "GET" else b"x")[0], 405, method)
        for path in ("/submissions?kind=classic&test=1&name=Bad&maker=Mia", "/submissions", "/admin/api/me", "/admin/api/login", "/maps/all", "/test-maps/", "/test-maps/x", "/", "/replays", "/busy", "/stats"):
            for method in ("GET", "POST"):
                status, headers, body = s.call(method, path, dict(OCTET, **addr(16)), b"x" if method == "POST" else None)
                self.assertEqual(status, 404, method + " " + path)
                self.assertEqual(code((status, headers, body)), "not_found")
        self.assertEqual(s.call("GET", "/healthz")[0], 200)                                                                  # (the door's own health answer)
        # nothing else answers for the submissions on the other ports: the replay door only reads, the WebSocket port knows no such path
        for door in ("replay", "ws"):
            status, headers, body = s.call("POST", "/submissions?kind=classic&name=Bad&maker=Mia", dict(OCTET, **addr(17)), self.tiny, door=door)
            self.assertNotIn(status, (200, 201), door)
            self.assertNotIn(b"submission", body.lower().replace(b"submissions", b""), door)
            status, headers, body = s.call("GET", "/admin/api/me", None, None, door=door)
            self.assertNotEqual(status, 200, door)

    def test_the_door_limits_the_size_on_the_head(self):
        s = self.server
        self.assertEqual(s.head_only("POST", "/test-maps?kind=extended&name=Big", 2 * 1024 * 1024 + 1, dict(OCTET, **addr(20))), 413)
        self.assertEqual(s.head_only("POST", "/test-maps?kind=classic&name=Big", 10 * 1024 * 1024, dict(OCTET, **addr(20))), 413)
        self.assertEqual(s.head_only("POST", "/submissions", 5000, dict(OCTET, **addr(20))), 413)                            # (any other path: a few KB)
        status, headers, body = s.call("POST", "/test-maps?kind=classic&name=Big", dict(OCTET, **addr(21)), b"\x08\x00\x00\x00" + b"x" * (600 * 1024))
        self.assertEqual(status, 413)                                                                                     # (more than a Classic map has: refused on the head, 512 KB for any kind but extended)

    @unittest.skipUnless(os.path.exists(LVL_TOOL), "lvl_tool is not built")
    def test_an_extended_map_is_kept_too(self):
        out = os.path.join(self.work, "ext.lvl")
        done = subprocess.run([LVL_TOOL, "convert", os.path.join(MAPS, "TINY.LVL"), "--to", "extended", "-o", out], capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stderr)
        with open(out, "rb") as f:
            extended = f.read()
        status, headers, body = self.server.call("POST", "/test-maps?kind=extended&name=Ext", dict(OCTET, **addr(30)), extended)
        self.assertEqual(status, 201, body)
        self.assertEqual(json.loads(body)["test_map"]["kind"], "extended")


@unittest.skipUnless(os.path.exists(SERVER), "ants_server is not built (BUILD_DIR=%s): the test maps were NOT run end to end" % BUILD)
class TheRestartAndTheSwitches(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.mkdtemp(prefix="ants_test_maps_e2e_")
        with open(os.path.join(MAPS, "TINY.LVL"), "rb") as f:
            self.tiny = f.read()

    def tearDown(self):
        shutil.rmtree(self.work, ignore_errors=True)

    def test_a_restart_wipes_the_folder_and_only_its_own_files(self):
        results = os.path.join(self.work, "results")
        folder = os.path.join(results, "test-maps")
        os.makedirs(folder)
        outside = os.path.join(results, "keep.txt")
        with open(outside, "w") as f:
            f.write("not a test map")
        with open(os.path.join(folder, "notes.txt"), "w") as f:
            f.write("not a test map either")
        server = Server(results)
        try:
            status, headers, body = server.call("POST", "/test-maps?kind=classic&name=Pop", dict(OCTET, **addr(1)), self.tiny)
            self.assertEqual(status, 201, body)
            name = json.loads(body)["test_map"]["file"]
            self.assertEqual(server.call("GET", "/maps/files/" + urllib.parse.quote(name), door="replay")[0], 200)
        finally:
            out, err = server.stop()
        self.assertEqual(sorted(os.listdir(folder)), sorted(["notes.txt"] + [f for f in os.listdir(folder) if f.startswith("test-")]))
        server = Server(results)
        try:
            self.assertEqual(sorted(os.listdir(folder)), ["notes.txt"])                                                   # (the test map is gone; a file that is none of the store's stays)
            self.assertTrue(os.path.exists(outside))
            self.assertEqual(server.call("GET", "/maps/files/" + urllib.parse.quote(name), door="replay")[0], 404)
        finally:
            out, err = server.stop()
        self.assertNotIn("Pop", err)                                                                                       # (no map name and no address in the log)
        self.assertNotIn("203.0.113", err)
        self.assertIn("test rooms on", err)
        self.assertIn("POST /test-maps and nothing else", err)
        self.assertNotIn("map submissions on", err)

    def test_without_a_folder_or_without_the_port_the_server_starts_and_test_rooms_are_off(self):
        server = Server("", wait_for="replay")                                                                              # (no results folder, no --test-maps-dir)
        try:
            with self.assertRaises(OSError):
                socket.create_connection(("127.0.0.1", server.test_port), 0.5).close()                                     # (the door was shut again: nothing listens)
            self.assertEqual(server.call("GET", "/maps/all", door="replay")[0], 200)                                       # (the game is up)
        finally:
            out, err = server.stop()
        self.assertIn("test rooms are off: no --results-dir or --test-maps-dir", err)
        server = Server(os.path.join(self.work, "r2"), test_port=False, wait_for="replay")                               # (the port is not given: the program's own default is off)
        try:
            self.assertEqual(server.call("GET", "/maps/all", door="replay")[0], 200)
        finally:
            out, err = server.stop()
        self.assertNotIn("test rooms on", err)
        self.assertFalse(os.path.exists(os.path.join(self.work, "r2", "test-maps")))

    def test_a_folder_of_its_own_by_the_option(self):
        folder = os.path.join(self.work, "elsewhere")
        server = Server("", extra_args=["--test-maps-dir", folder])
        try:
            status, headers, body = server.call("POST", "/test-maps?kind=classic&name=Pop", dict(OCTET, **addr(1)), self.tiny)
            self.assertEqual(status, 201, body)
            self.assertEqual(len(os.listdir(folder)), 1)
        finally:
            server.stop()

    def test_a_port_that_is_taken_switches_test_rooms_off_and_the_game_goes_on(self):
        with socket.socket() as taken:
            taken.bind(("127.0.0.1", 0))
            taken.listen(1)
            port = taken.getsockname()[1]
            ws, replay = free_port(), free_port()
            proc = subprocess.Popen([SERVER, "--maps", MAPS, "--port", "0", "--ws-port", str(ws), "--replay-port", str(replay), "--no-db", "--results-dir", os.path.join(self.work, "r3"), "--test-map-port", str(port)],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                for _ in range(300):
                    try:
                        socket.create_connection(("127.0.0.1", replay), 0.2).close()
                        break
                    except OSError:
                        time.sleep(0.05)
                self.assertIsNone(proc.poll())
            finally:
                proc.send_signal(signal.SIGTERM)
                out, err = proc.communicate(timeout=15)
        self.assertIn("test rooms are off: cannot listen on test-map port", err)


if __name__ == "__main__":
    unittest.main()
