#!/usr/bin/env python3
"""The map submissions end to end, with the real programs (suite 2.19.14 of ./run_tests.sh; docs/MAP_SUBMISSIONS.md).

The real ants_server (BUILD_DIR/src/ants_server/ants_server, a folder of its own for its results), with what the site's nginx would do done by hand (the headers X-Client-Address and Sec-Fetch-Site, and the
signed pass that the chat hub's gate gives and nginx hands on in X-Ants-Admin-Pass: tests/scripts/admin_pass.py makes it from the shared key): a visitor with no good pass gets one answer for every call; an admin and
the operator's account get in, each call with a pass of its own (a used pass is refused); a map is sent, reviewed and approved, signed with the username of the one who decided; the lobby's list, the file and the
picture of the approved map are served; a take-down hides it and a put-back shows it again; an Extended map is in the newest version's list only; a restart keeps the maps and the log; the accounts' commands are
gone and a server with no good key is closed. The server's log holds no address, token, map name, maker, username, pass or key. Exit 0: all passed (or the program is not built: said, and 0).
"""
import http.client
import json
import os
import shutil
import signal
import socket
import subprocess
import tempfile
import time
import sys
import unittest
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import admin_pass  # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
BUILD = os.environ.get("BUILD_DIR", "build")
SERVER = os.path.join(REPO, BUILD, "src", "ants_server", "ants_server")
LVL_TOOL = os.path.join(REPO, BUILD, "lvl_tool")
THUMB = os.path.join(REPO, BUILD, "src", "ants_thumb", "ants_thumb")          # (the picture maker: with it the server draws the pictures of the maps; without it they answer 404)
CHD = os.path.join(REPO, "Original-Ants", "ants.chd")
PICTURE_SECONDS = 120                                  # (how long a test waits for a picture: the maker draws one at a time, and a child process may take 30 seconds on a busy machine before the server kills it)
SECRET = admin_pass.TEST_SECRET                        # (the key of the pass: a made-up one)
REF_DAVE = "0123456789abcdef"
REF_MIA = "fedcba9876543210"
CLOSED = b'{"error":"closed","message":"The admin page is closed."}'


def free_port():
    """A number of this process's own range of ports (quick_http.free_port): two tests side by side are never given the same one."""
    import os
    import sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import quick_http
    return quick_http.free_port()


def wait_for_picture(ask, seconds=PICTURE_SECONDS):
    """The answer of ask() for a picture, which the picture maker draws behind the request (docs/MAP_SUBMISSIONS.md "Pictures": one child process at a time, a picture that is not made yet is a 404): a 404 is asked
    again until `seconds` have passed when the maker is built (a deadline for a machine that is busy: the answer comes as soon as the picture is there); a server that has none answers 404 for good."""
    until = time.monotonic() + (seconds if os.path.exists(THUMB) else 0)
    while True:
        answer = ask()
        if answer[0] != 404 or time.monotonic() >= until:
            return answer
        time.sleep(0.2)


class Server:
    def __init__(self, results, env_extra=None, extra_args=()):
        self.results = results
        self.submit_port = free_port()
        self.replay_port = free_port()
        self.ws_port = free_port()
        env = dict(os.environ)
        env["ANTS_ADMIN_GATE_SECRET"] = SECRET
        env.update(env_extra or {})
        self.args = [SERVER, "--maps", os.path.join(REPO, "Original-Ants", "Maps"), "--community-maps", os.path.join(REPO, "Community-Maps"), "--port", "0", "--ws-port", str(self.ws_port), "--replay-port",
                     str(self.replay_port), "--submit-port", str(self.submit_port), "--results-dir", results] + (["--map-picture-program", THUMB, "--map-picture-archive", CHD] if os.path.exists(THUMB) else []) + list(extra_args)
        self.proc = subprocess.Popen(self.args, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        for _ in range(300):
            try:
                socket.create_connection(("127.0.0.1", self.submit_port), 0.2).close()
                self.call("GET", "/admin/api/me")                  # (the door listens before the server has finished its start: an answer is the start's end)
                return
            except OSError:
                if self.proc.poll() is not None:
                    break
                time.sleep(0.05)
        out, err = self.proc.communicate(timeout=10) if self.proc.poll() is not None else ("", "")
        raise AssertionError("the server did not start: " + err)

    def stop(self):
        if self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
        try:
            return self.proc.communicate(timeout=15)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            return self.proc.communicate()

    def call(self, method, path, headers=None, body=None, door="submit"):
        port = self.submit_port if door == "submit" else self.replay_port
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


def j(triple):
    return json.loads(triple[2].decode("utf-8"))


class Admin:
    """An administrator's browser as nginx shows it to the server: a new pass for every call (a pass is used once) and the page's token of the account."""

    def __init__(self, server, name="dave", ref=REF_DAVE, role="admin"):
        self.server, self.name, self.ref, self.role = server, name, ref, role
        status, headers, body = self.call("GET", "/admin/api/me")
        assert status == 200, (status, body)
        self.info = json.loads(body)
        self.csrf = self.info["csrf"]

    def pass_header(self):
        return admin_pass.make_pass(SECRET, ref=self.ref, name=self.name, role=self.role)

    def call(self, method, path, body=None, extra=None):
        h = {admin_pass.PASS_HEADER: self.pass_header(), "Sec-Fetch-Site": "same-origin"}
        if method == "POST":
            h["Content-Type"] = "application/json"
            h["X-Ants-Csrf"] = getattr(self, "csrf", "")
            body = json.dumps(body or {}).encode()
        h.update(extra or {})
        return self.server.call(method, path, h, body)


def make_maps(work):
    with open(os.path.join(REPO, "Original-Ants", "Maps", "TINY.LVL"), "rb") as f:
        tiny = f.read()
    out = subprocess.run([LVL_TOOL, "convert", os.path.join(REPO, "Original-Ants", "Maps", "TINY.LVL"), "--to", "extended", "-o", os.path.join(work, "ext.lvl")], capture_output=True, text=True)
    assert out.returncode == 0, out.stderr
    with open(os.path.join(work, "ext.lvl"), "rb") as f:
        return tiny, f.read()


@unittest.skipUnless(os.path.exists(SERVER) and os.path.exists(LVL_TOOL), "ants_server or lvl_tool is not built (BUILD_DIR=%s): the map submissions were NOT run end to end" % BUILD)
class TheWholeWay(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.mkdtemp(prefix="ants_submissions_e2e_")
        cls.results = os.path.join(cls.work, "results")
        cls.tiny, cls.extended = make_maps(cls.work)
        cls.server = Server(cls.results)
        cls.logs = ""

    @classmethod
    def tearDownClass(cls):
        out, err = cls.server.stop()
        cls.logs += err
        shutil.rmtree(cls.work, ignore_errors=True)

    # ---- the pieces
    def owner_call(self, method, path, body=None):
        return self.owner.call(method, path, body)

    def picture_of(self, ask):
        return wait_for_picture(ask)

    def assert_picture(self, answer):
        """With the picture maker: a PNG of 300 x 300 pixels, the picture of the setup screen's box; without it: a 404, so that the page keeps its empty square."""
        if not os.path.exists(THUMB):
            self.assertEqual(answer[0], 404)
            return
        self.assertEqual(answer[0], 200)
        self.assertEqual(answer[2][:8], b"\x89PNG\r\n\x1a\n")
        self.assertEqual((int.from_bytes(answer[2][16:20], "big"), int.from_bytes(answer[2][20:24], "big")), (300, 300))

    def send(self, data, name, kind="classic", address="203.0.113.5", token=None, maker="Mia"):
        q = urllib.parse.urlencode({"kind": kind, "name": name, "maker": maker, "note": "made by hand"}, quote_via=urllib.parse.quote)
        h = {"Content-Type": "application/octet-stream", "X-Client-Address": address, "Sec-Fetch-Site": "same-origin"}
        if token:
            h["Authorization"] = "Bearer " + token
        return self.server.call("POST", "/submissions?" + q, h, data)

    def lobby(self, door_path="/maps"):
        return j(self.server.call("GET", door_path, door="replay"))

    def listed(self, door_path, file):
        return [m for m in self.lobby(door_path)["maps"] if m["file"] == file]

    def with_pass(self, pass_text, method="GET", path="/admin/api/me"):
        h = {"Sec-Fetch-Site": "same-origin"}
        if pass_text:
            h[admin_pass.PASS_HEADER] = pass_text
        return self.server.call(method, path, h)

    # ---- the tests, in the order a map takes: they share the server, so they are one test of steps
    def test_the_whole_way(self):
        # the accounts' commands are gone: a program that is asked for one stops, and nothing is made or shown
        for option in ("--admin-create", "--admin-reset"):
            gone = subprocess.run([SERVER, option, "Owner", "--results-dir", self.results], capture_output=True, text=True, timeout=60)
            self.assertEqual((gone.returncode, gone.stdout), (2, ""), option)
            self.assertIn("unknown option", gone.stderr)
        # the folder and the database are for the server's user alone
        self.assertEqual(os.stat(self.results).st_mode & 0o777, 0o700)
        for name in os.listdir(self.results):
            if name.startswith("ants.db"):
                self.assertEqual(os.stat(os.path.join(self.results, name)).st_mode & 0o077, 0, name)
        # a visitor with no good pass gets ONE answer for every call, whatever the path (nothing is learned about what is there)
        for path in ("/admin/api/me", "/admin/api/maps", "/admin/api/audit", "/admin/api/zzz", "/admin/api/users", "/admin/api/login"):
            for pass_text in ("", "garbage", admin_pass.make_pass("another-key-of-thirty-two-or-more-chars-1"), admin_pass.make_pass(SECRET, role="moderator"), admin_pass.make_pass(SECRET, now=time.time() - 300)):
                answer = self.with_pass(pass_text, path=path)
                self.assertEqual((answer[0], answer[2], answer[1].get("set-cookie")), (403, CLOSED, None), path)
        self.assertEqual(self.with_pass("", "POST", "/admin/api/login")[2], CLOSED)                  # (the sign-in is gone: not even a different answer for it)
        # an administrator gets in; each call needs a pass of its own
        self.owner = Admin(self.server, "dave", REF_DAVE, "admin")
        self.assertEqual((self.owner.info["name"], self.owner.info["ref"], self.owner.info["role"], len(self.owner.csrf)), ("dave", REF_DAVE, "admin", 32))
        self.assertEqual(sorted(self.owner.info), ["csrf", "name", "ref", "role", "untrusted", "untrusted_fields"])
        used = self.owner.pass_header()
        self.assertEqual(self.with_pass(used)[0], 200)
        replay = self.with_pass(used)
        self.assertEqual((replay[0], replay[2]), (403, CLOSED))                                      # (a pass that was used once is of no use again)
        self.assertEqual(self.owner.call("GET", "/admin/api/maps")[0], 200)
        for path in ("/admin/api/login", "/admin/api/logout", "/admin/api/password", "/admin/api/users"):    # (the routes of the retired sign-in answer as a path that is not there)
            self.assertEqual(self.owner.call("GET", path)[0], 404, path)
        unknown = self.owner.call("GET", "/admin/api/zzz")
        self.assertEqual((unknown[0], j(unknown)["error"]), (404, "not_found"))
        # a change needs the page's token of the account
        no_token = self.server.call("POST", "/admin/api/maps/" + "a" * 32 + "/approve", {admin_pass.PASS_HEADER: self.owner.pass_header(), "Content-Type": "application/json", "Sec-Fetch-Site": "same-origin"}, b"{}")
        self.assertEqual((no_token[0], j(no_token)["error"]), (403, "csrf"))
        # the operator's account is another account: its own reference and token
        self.operator = Admin(self.server, "mia", REF_MIA, "owner")
        self.assertEqual((self.operator.info["role"], self.operator.info["ref"]), ("owner", REF_MIA))
        self.assertNotEqual(self.operator.csrf, self.owner.csrf)
        # a map is sent: a waiting submission with the maker's token
        status, headers, body = self.send(self.tiny + b"J" * 3000, "Run One")
        self.assertEqual(status, 201, body)
        sent = json.loads(body)
        token = sent["token"]
        sid = sent["submission"]["id"]
        self.assertEqual((sent["submission"]["state"], sent["name_taken"], sent["token_is_new"]), ("waiting", False, True))
        self.assertEqual(self.listed("/maps", "Run One.lvl"), [])                                  # (nothing is public until an administrator approves)
        status, headers, body = self.server.call("GET", "/submissions", {"Authorization": "Bearer " + token})
        self.assertEqual([s["id"] for s in json.loads(body)["submissions"]], [sid])
        waiting = j(self.owner_call("GET", "/admin/api/maps?state=waiting"))
        self.assertEqual((len(waiting["maps"]), waiting["maps"][0]["maker"], waiting["maps"][0]["note"], waiting["counts"]["waiting"]), (1, "Mia", "made by hand", 1))
        self.assert_picture(self.picture_of(lambda: self.owner_call("GET", "/admin/api/maps/%s/picture" % sid)))          # (drawn when the map was sent)
        # a map that is the library's name: sent, flagged, not approvable as it is
        status, headers, body = self.send(self.tiny, "Tiny", address="203.0.113.6")
        taken = json.loads(body)
        self.assertTrue(taken["name_taken"])
        refused = self.owner_call("POST", "/admin/api/maps/%s/approve" % taken["submission"]["id"], {})
        self.assertEqual((refused[0], j(refused)["error"], j(refused)["suggestion"]), (409, "name_taken", "Tiny 2"))
        # the administrator approves: the lobby's list has it, with its file and its picture; the decision is signed with the username
        approved = self.owner_call("POST", "/admin/api/maps/%s/approve" % sid, {})
        self.assertEqual((approved[0], j(approved)["map"]["state"], j(approved)["map"]["decided_by"]), (200, "approved", "dave"))
        entry = self.listed("/maps", "Run One.lvl")
        self.assertEqual(len(entry), 1)
        self.assertEqual((entry[0]["maker"], entry[0]["width"], entry[0]["thumb"]), ("Mia", 31, "/maps/previews/Run%20One.png"))
        status, headers, file_bytes = self.server.call("GET", "/maps/files/Run%20One.lvl", door="replay")
        self.assertEqual(status, 200)
        self.assertTrue(len(file_bytes) <= len(self.tiny) and b"J" * 100 not in file_bytes)       # (the canonical file, not the upload)
        answer = self.picture_of(lambda: self.server.call("GET", "/maps/previews/Run%20One.png", door="replay"))
        self.assert_picture(answer)
        if os.path.exists(THUMB):
            self.assertEqual(answer[1]["content-type"], "image/png")
        self.assertEqual(self.server.call("GET", "/maps/files/Run%20One.lvl?x=1", door="replay")[0], 404)
        self.assertEqual(self.server.call("GET", "/maps/files/..%2fx.lvl", door="replay")[0], 404)
        mine = j(self.server.call("GET", "/submissions/" + sid, {"Authorization": "Bearer " + token}))["submission"]
        self.assertEqual((mine["state"], mine["approved_name"], mine["renamed_by_owner"]), ("approved", "Run One", False))
        self.assertNotIn("dave", json.dumps(mine))                                                   # (the maker is not told who decided)
        # take down: hidden from the list and the files, nothing deleted; put back: shown again
        self.assertEqual(self.operator.call("POST", "/admin/api/maps/%s/take-down" % sid, {})[0], 200)               # (the operator's account takes a map down)
        self.assertEqual(self.listed("/maps", "Run One.lvl"), [])
        self.assertEqual(self.server.call("GET", "/maps/files/Run%20One.lvl", door="replay")[0], 404)
        self.assertEqual(self.server.call("GET", "/maps/previews/Run%20One.png", door="replay")[0], 404)
        again = self.send(self.tiny, "run one", address="203.0.113.7")                            # (the name stays protected, whatever the letter case)
        self.assertTrue(json.loads(again[2])["name_taken"])
        self.assertEqual(self.owner_call("POST", "/admin/api/maps/%s/approve" % json.loads(again[2])["submission"]["id"], {})[0], 409)
        self.assertEqual(self.owner_call("POST", "/admin/api/maps/%s/put-back" % sid)[0], 200)
        self.assertEqual(len(self.listed("/maps", "Run One.lvl")), 1)
        # an Extended map is in the newest version's list only, and its file is not served yet
        status, headers, body = self.send(self.extended, "Run Extended", kind="extended", address="203.0.113.8", token=None)
        self.assertEqual(status, 201, body)
        ext_id = json.loads(body)["submission"]["id"]
        self.assertEqual(self.owner_call("POST", "/admin/api/maps/%s/approve" % ext_id, {})[0], 200)
        self.assertEqual(self.listed("/maps", "Run Extended.lvl"), [])
        both = self.listed("/maps/all", "Run Extended.lvl")
        self.assertTrue(len(both) == 1 and both[0]["kind"] == "extended" and both[0]["thumb"] == "/maps/previews/Run%20Extended.png")
        self.assertEqual(self.server.call("GET", "/maps/files/Run%20Extended.lvl", door="replay")[0], 200)
        self.assert_picture(self.picture_of(lambda: self.server.call("GET", "/maps/previews/Run%20Extended.png", door="replay")))
        # what the server refuses
        self.assertEqual(self.send(b"not a map", "Bad One", address="203.0.113.9")[0], 400)
        self.assertEqual(self.send(self.extended, "Lie One", kind="classic", address="203.0.113.9")[0], 400)
        self.assertEqual(self.server.call("POST", "/submissions?kind=classic&name=Abc&maker=Mia", {"Content-Type": "application/octet-stream", "X-Client-Address": "203.0.113.9", "Content-Length": "600000"})[0], 413)
        # a restart keeps it all (a pass of before the restart is old by the clock anyway: the account's next call brings a new one)
        out, err = self.server.stop()
        type(self).logs += err
        self.assertEqual(self.server.proc.returncode, 0)                                           # (a stop by SIGTERM is a clean exit)
        self.assertIn("schema version 10", err)
        type(self).server = Server(self.results)
        self.owner.server = self.operator.server = self.server
        self.assertEqual(self.owner.call("GET", "/admin/api/me")[0], 200)
        self.assertEqual(len(self.listed("/maps", "Run One.lvl")), 1)
        self.assertEqual(len(self.listed("/maps/all", "Run Extended.lvl")), 1)
        self.assertEqual(self.server.call("GET", "/maps/files/Run%20One.lvl", door="replay")[2], file_bytes)
        listing = j(self.owner_call("GET", "/admin/api/maps?state=waiting"))
        self.assertEqual(sorted(m["name"] for m in listing["maps"]), ["Tiny", "run one"])
        log = json.loads(self.owner_call("GET", "/admin/api/audit")[2])
        self.assertEqual([(e["who"], e["ref"], e["action"]) for e in log["entries"]], [("dave", REF_DAVE, "map_approved"), ("dave", REF_DAVE, "map_put_back"), ("mia", REF_MIA, "map_taken_down"), ("dave", REF_DAVE, "map_approved")])
        self.assertEqual(self.server.call("GET", "/submissions/" + sid, {"Authorization": "Bearer " + token})[0], 200)       # (the maker's token still finds it)
        # the log: lines for the operator, nothing of a person
        out, err = self.server.stop()
        type(self).logs += err
        self.assertEqual(self.server.proc.returncode, 0)
        for fault in ("Sanitizer", "runtime error", "terminate called"):                         # (a build with the sanitizers says so in the log)
            self.assertNotIn(fault, self.logs)
        self.assertIn("map submissions on: files in", self.logs)
        self.assertIn("the admin page's API is on", self.logs)
        self.assertIn("admin_gate kind=no_pass", self.logs)                                        # (the refusals above: the kind and nothing else)
        self.assertIn("admin_gate kind=replay", self.logs)
        for private in (token, "203.0.113.", "198.51.100.", "Mia", "Run One", "Run Extended", "made by hand", "dave", "mia", REF_DAVE, REF_MIA, SECRET, "v1.", used):
            self.assertNotIn(private, self.logs)
        type(self).server = Server(self.results)                                                  # (the teardown stops one)


class TheSettings(unittest.TestCase):
    @unittest.skipUnless(os.path.exists(SERVER), "ants_server is not built")
    def test_the_door_needs_a_database_and_a_folder_and_the_admin_page_needs_a_good_key(self):
        work = tempfile.mkdtemp(prefix="ants_submissions_set_")
        try:
            base = [SERVER, "--maps", os.path.join(REPO, "Original-Ants", "Maps"), "--port", "0", "--ws-port", str(free_port()), "--submit-port", str(free_port())]
            done = subprocess.run(base + ["--results-dir", work, "--no-db"], capture_output=True, text=True, timeout=30)
            self.assertEqual(done.returncode, 1)
            self.assertIn("needs the database", done.stderr)
            done = subprocess.run(base, capture_output=True, text=True, timeout=30)               # (no results folder: no database either)
            self.assertEqual(done.returncode, 1)
            for bad in (["--submit-port", "x"], ["--submit-max-waiting", "0"], ["--submit-per-address", "100001"], ["--submissions-dir", ""]):
                done = subprocess.run([SERVER, "--maps", os.path.join(REPO, "Original-Ants", "Maps"), "--port", "0"] + bad, capture_output=True, text=True, timeout=30)
                self.assertEqual(done.returncode, 2, bad)
            blocker = os.path.join(work, "file")
            with open(blocker, "w") as f:
                f.write("x")
            done = subprocess.run(base + ["--results-dir", work, "--submissions-dir", os.path.join(blocker, "inside")], capture_output=True, text=True, timeout=30)
            self.assertEqual(done.returncode, 1)
            self.assertIn("map submissions:", done.stderr)
            # without a good key the server runs, says that the admin page is closed, and every call under /admin/api is the one answer, even with a pass that the right key made
            for tag, setting, words in (("none", None, "the admin page is CLOSED (ANTS_ADMIN_GATE_SECRET is not set)"), ("short", "too-short-for-a-key", "the admin page is CLOSED (ANTS_ADMIN_GATE_SECRET is not 32 to 256 printable characters without a blank)"),
                                        ("blank", "a key with blanks inside it is not a key 12345", "the admin page is CLOSED (ANTS_ADMIN_GATE_SECRET is not 32 to 256 printable characters without a blank)"),
                                        ("sample", admin_pass.SAMPLE_SECRET, "the admin page is CLOSED (ANTS_ADMIN_GATE_SECRET is the sample key of the documents")):
                env = {"PATH": os.environ.get("PATH", "")}
                if setting is not None:
                    env["ANTS_ADMIN_GATE_SECRET"] = setting
                port = free_port()
                args = base[:-1] + [str(port), "--results-dir", os.path.join(work, "ok-" + tag)]
                proc = subprocess.Popen(args, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                try:
                    for _ in range(100):
                        try:
                            socket.create_connection(("127.0.0.1", port), 0.2).close()
                            break
                        except OSError:
                            time.sleep(0.05)
                    c = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
                    for signer in (SECRET, setting or SECRET):                                         # (a pass that the right key made, and one that the odd key itself made: neither opens it)
                        c.request("GET", "/admin/api/me", headers={admin_pass.PASS_HEADER: admin_pass.make_pass(signer)})
                        r = c.getresponse()
                        self.assertEqual((r.status, r.read()), (403, CLOSED), tag)
                    c.close()
                finally:
                    proc.send_signal(signal.SIGTERM)
                    out, err = proc.communicate(timeout=15)
                self.assertIn(words, err, tag)
                self.assertNotIn(SECRET, err)
                self.assertNotIn(setting or "<none>", err)                                        # (the key is never in the log, good or not)
        finally:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()
