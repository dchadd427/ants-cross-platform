#!/usr/bin/env python3
"""The map submissions end to end, with the real programs (suite 2.19.14 of ./run_tests.sh; docs/MAP_SUBMISSIONS.md).

The real ants_server (BUILD_DIR/src/ants_server/ants_server, a folder of its own for its results), with what the site's nginx would do done by hand (the headers X-Client-Address and Sec-Fetch-Site, the cookie
that the browser keeps): the first Admin is made with the command line (the starting password is printed once, to the terminal), signs in, is made to choose a password, adds a Moderator, who signs in, chooses a
password and may decide on maps but not touch the users; a map is sent, reviewed and approved; the lobby's list, the file and the picture of the approved map are served; a take-down hides it and a put-back shows
it again; an Extended map is in the newest version's list only; a restart keeps the accounts, the sessions and the maps; the reset command ends a session. The server's log holds no address, token, map name,
maker, username or password. Exit 0: all passed (or the program is not built: said, and 0).
"""
import http.client
import json
import os
import re
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
SECRET = "0123456789abcdef0123456789abcdef0123456789"
CHOSEN = "my own chosen passphrase 1"
JSON_HEADERS = {"Content-Type": "application/json", "Sec-Fetch-Site": "same-origin", "X-Client-Address": "198.51.100.7"}


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class Server:
    def __init__(self, results, env_extra=None, extra_args=()):
        self.results = results
        self.submit_port = free_port()
        self.replay_port = free_port()
        self.ws_port = free_port()
        env = dict(os.environ)
        env["ANTS_ADMIN_SESSION_SECRET"] = SECRET
        env.update(env_extra or {})
        self.args = [SERVER, "--maps", os.path.join(REPO, "Original-Ants", "Maps"), "--community-maps", os.path.join(REPO, "Community-Maps"), "--port", "0", "--ws-port", str(self.ws_port), "--replay-port",
                     str(self.replay_port), "--submit-port", str(self.submit_port), "--results-dir", results] + list(extra_args)
        self.proc = subprocess.Popen(self.args, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        for _ in range(300):
            try:
                socket.create_connection(("127.0.0.1", self.submit_port), 0.2).close()
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


def create_admin(results, name="Owner"):
    """The first account, from the command line: the starting password is on standard output, once."""
    done = subprocess.run([SERVER, "--admin-create", name, "--results-dir", results], capture_output=True, text=True, timeout=60)
    assert done.returncode == 0, done.stderr
    return re.search(r"Starting password: (\S{20})\n", done.stdout).group(1), done


class Session:
    """A browser that signed in: the cookie and the token of the page."""

    def __init__(self, server, name, password, address="198.51.100.7"):
        self.server = server
        status, headers, body = server.call("POST", "/admin/api/login", dict(JSON_HEADERS, **{"X-Client-Address": address}), json.dumps({"username": name, "password": password}).encode())
        assert status == 200, (status, body)
        self.cookie = headers["set-cookie"].split(";")[0]
        self.info = json.loads(body)
        self.csrf = self.info["csrf"]
        self.attributes = headers["set-cookie"]

    def call(self, method, path, body=None):
        h = {"Cookie": self.cookie, "Sec-Fetch-Site": "same-origin"}
        if method == "POST":
            h["Content-Type"] = "application/json"
            h["X-Ants-Csrf"] = self.csrf
            body = json.dumps(body or {}).encode()
        return self.server.call(method, path, h, body)

    def change_password(self, current, new):
        status, headers, body = self.call("POST", "/admin/api/password", {"current": current, "new": new})
        assert status == 200, (status, body)
        self.cookie = headers["set-cookie"].split(";")[0]
        self.csrf = json.loads(body)["csrf"]


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
        cls.starting, cls.cli = create_admin(cls.results)
        cls.server = Server(cls.results)
        cls.logs = ""

    @classmethod
    def tearDownClass(cls):
        out, err = cls.server.stop()
        cls.logs += err
        shutil.rmtree(cls.work, ignore_errors=True)

    # ---- the pieces
    def me(self, cookie):
        return j(self.server.call("GET", "/admin/api/me", {"Cookie": cookie} if cookie else {}))

    def owner_call(self, method, path, body=None):
        return self.owner.call(method, path, body)

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

    # ---- the tests, in the order a map takes: they share the server, so they are one test of steps
    def test_the_whole_way(self):
        # the command line made the first Admin: the password was shown once, on standard output, and nowhere else
        self.assertEqual((self.cli.stderr, len(self.starting)), ("", 20))
        self.assertIn("Admin account created.\nUsername: Owner\nStarting password: " + self.starting + "\nShown once. Sign in at /admin and change it.\n", self.cli.stdout)
        self.assertEqual(os.stat(self.results).st_mode & 0o777, 0o700)                             # (the accounts' hashes are for the server's user only: the folder, the database and the files that SQLite keeps beside it)
        for name in os.listdir(self.results):
            if name.startswith("ants.db"):
                self.assertEqual(os.stat(os.path.join(self.results, name)).st_mode & 0o077, 0, name)
        again = subprocess.run([SERVER, "--admin-create", "Second", "--results-dir", self.results], capture_output=True, text=True, timeout=60)
        self.assertEqual((again.returncode, again.stdout), (1, ""))                                  # (an Admin exists: nothing is made, nothing is shown)
        # nobody is signed in: the page is told so; the owner's calls do not exist for a visitor
        self.assertEqual(self.me(""), {"signed_in": False})
        self.assertEqual(self.server.call("GET", "/admin/api/maps")[0], 404)
        unknown = self.server.call("GET", "/admin/api/zzz")
        self.assertEqual(self.server.call("GET", "/admin/api/maps")[2], unknown[2])
        self.assertEqual(self.server.call("GET", "/admin/api/users")[2], unknown[2])
        # a wrong name and a wrong password get the same words, and no cookie
        wrong_user = self.server.call("POST", "/admin/api/login", JSON_HEADERS, json.dumps({"username": "nobody", "password": self.starting}).encode())
        wrong_pass = self.server.call("POST", "/admin/api/login", dict(JSON_HEADERS, **{"X-Client-Address": "198.51.100.8"}), json.dumps({"username": "Owner", "password": "not the password"}).encode())
        self.assertEqual((wrong_user[0], wrong_user[2], wrong_user[1].get("set-cookie")), (401, wrong_pass[2], None))
        self.assertEqual(wrong_pass[0], 401)
        # the first sign-in with the starting password: a cookie with the flags, and nothing but the choice of a password
        self.owner = Session(self.server, "Owner", self.starting)
        for flag in ("HttpOnly", "Secure", "SameSite=Strict", "Path=/admin", "Max-Age=43200"):
            self.assertIn(flag, self.owner.attributes)
        me = self.me(self.owner.cookie)
        self.assertEqual((me["signed_in"], me["name"], me["role"], me["must_change"], len(me["csrf"])), (True, "Owner", "admin", True, 32))
        refused = self.owner.call("GET", "/admin/api/maps")
        self.assertEqual((refused[0], j(refused)["error"]), (403, "password_change_required"))
        self.assertEqual(self.owner.call("GET", "/admin/api/users")[0], 403)
        weak = self.owner.call("POST", "/admin/api/password", {"current": "", "new": "short"})
        self.assertEqual((weak[0], j(weak)["reason"]), (400, "too_short"))
        time.sleep(10.5)                                                                           # (a password check costs the one thread a third of a second: the server allows three in ten seconds, and three are used)
        self.owner.change_password("", CHOSEN)
        self.assertEqual(self.me(self.owner.cookie)["must_change"], False)
        self.assertEqual(self.owner.call("GET", "/admin/api/maps")[0], 200)
        # the Admin adds a Moderator: the starting password is in the answer, once
        status, headers, body = self.owner.call("POST", "/admin/api/users", {"username": "mia", "role": "moderator"})
        self.assertEqual(status, 201, body)
        mia_starting = j((status, headers, body))["password"]
        listing = self.owner.call("GET", "/admin/api/users")[2].decode()
        self.assertNotIn(mia_starting, listing)
        self.assertEqual([u["name"] for u in json.loads(listing)["users"]], ["Owner", "mia"])
        self.moderator = Session(self.server, "mia", mia_starting, "198.51.100.9")
        self.assertEqual(self.moderator.call("GET", "/admin/api/maps")[0], 403)                   # (a starting password: nothing else yet)
        self.moderator.change_password("", "mia chose a long phrase")
        self.assertEqual(self.moderator.call("GET", "/admin/api/maps")[0], 200)
        for method, path in (("GET", "/admin/api/users"), ("GET", "/admin/api/audit"), ("POST", "/admin/api/users"), ("POST", "/admin/api/users/1/remove")):
            denied = self.moderator.call(method, path, {"username": "evil", "role": "admin"} if method == "POST" else None)
            self.assertEqual((denied[0], denied[2]), (403, unknown[2]), method + " " + path)       # (the answer of a path that is not there, with the status 403)
        self.assertEqual([u["name"] for u in json.loads(self.owner.call("GET", "/admin/api/users")[2])["users"]], ["Owner", "mia"])
        # a map is sent: a waiting submission with the maker's token
        status, headers, body = self.send(self.tiny + b"J" * 3000, "Run One")
        self.assertEqual(status, 201, body)
        sent = json.loads(body)
        token = sent["token"]
        sid = sent["submission"]["id"]
        self.assertEqual((sent["submission"]["state"], sent["name_taken"], sent["token_is_new"]), ("waiting", False, True))
        self.assertEqual(self.listed("/maps", "Run One.lvl"), [])                                  # (nothing is public until the owner approves)
        status, headers, body = self.server.call("GET", "/submissions", {"Authorization": "Bearer " + token})
        self.assertEqual([s["id"] for s in json.loads(body)["submissions"]], [sid])
        waiting = j(self.owner_call("GET", "/admin/api/maps?state=waiting"))
        self.assertEqual((len(waiting["maps"]), waiting["maps"][0]["maker"], waiting["maps"][0]["note"], waiting["counts"]["waiting"]), (1, "Mia", "made by hand", 1))
        self.assertEqual(self.owner_call("GET", "/admin/api/maps/%s/picture" % sid)[2][:4], b"\x89PNG")
        # a map that is the library's name: sent, flagged, not approvable as it is, approved under the owner's name
        status, headers, body = self.send(self.tiny, "Tiny", address="203.0.113.6")
        taken = json.loads(body)
        self.assertTrue(taken["name_taken"])
        refused = self.owner_call("POST", "/admin/api/maps/%s/approve" % taken["submission"]["id"], {})
        self.assertEqual((refused[0], j(refused)["error"], j(refused)["suggestion"]), (409, "name_taken", "Tiny 2"))
        # the owner approves: the lobby's list has it, with its file and its picture
        approved = self.owner_call("POST", "/admin/api/maps/%s/approve" % sid, {})
        self.assertEqual((approved[0], j(approved)["map"]["state"], j(approved)["map"]["decided_by"]), (200, "approved", "Owner"))
        entry = self.listed("/maps", "Run One.lvl")
        self.assertEqual(len(entry), 1)
        self.assertEqual((entry[0]["maker"], entry[0]["width"], entry[0]["thumb"]), ("Mia", 31, "/maps/previews/Run%20One.png"))
        status, headers, file_bytes = self.server.call("GET", "/maps/files/Run%20One.lvl", door="replay")
        self.assertEqual(status, 200)
        self.assertTrue(len(file_bytes) <= len(self.tiny) and b"J" * 100 not in file_bytes)       # (the canonical file, not the upload)
        status, headers, picture = self.server.call("GET", "/maps/previews/Run%20One.png", door="replay")
        self.assertTrue(status == 200 and picture[:4] == b"\x89PNG" and headers["content-type"] == "image/png")
        self.assertEqual(self.server.call("GET", "/maps/files/Run%20One.lvl?x=1", door="replay")[0], 404)
        self.assertEqual(self.server.call("GET", "/maps/files/..%2fx.lvl", door="replay")[0], 404)
        mine = j(self.server.call("GET", "/submissions/" + sid, {"Authorization": "Bearer " + token}))["submission"]
        self.assertEqual((mine["state"], mine["approved_name"], mine["renamed_by_owner"]), ("approved", "Run One", False))
        # take down: hidden from the list and the files, nothing deleted; put back: shown again
        self.assertEqual(self.moderator.call("POST", "/admin/api/maps/%s/take-down" % sid, {})[0], 200)               # (a Moderator takes a map down)
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
        self.assertTrue(len(both) == 1 and both[0]["kind"] == "extended")
        self.assertEqual(self.server.call("GET", "/maps/files/Run%20Extended.lvl", door="replay")[0], 404)
        # what the server refuses
        self.assertEqual(self.send(b"not a map", "Bad One", address="203.0.113.9")[0], 400)
        self.assertEqual(self.send(self.extended, "Lie One", kind="classic", address="203.0.113.9")[0], 400)
        self.assertEqual(self.server.call("POST", "/submissions?kind=classic&name=Abc&maker=Mia", {"Content-Type": "application/octet-stream", "X-Client-Address": "203.0.113.9", "Content-Length": "600000"})[0], 413)
        # (the three a day for one address, the 200 waiting and the other limits are proved in test_submissions; here the server's window for checking maps holds six in ten seconds)
        # a restart keeps it all
        out, err = self.server.stop()
        type(self).logs += err
        self.assertEqual(self.server.proc.returncode, 0)                                           # (a stop by SIGTERM is a clean exit)
        self.assertIn("schema version 6", err)
        type(self).server = Server(self.results)
        self.owner.server = self.moderator.server = self.server
        self.assertEqual(self.me(self.owner.cookie)["name"], "Owner")                              # (the session and the accounts are in the database and the secret: a restart keeps them)
        self.assertEqual(self.me(self.moderator.cookie)["role"], "moderator")
        self.assertEqual(len(self.listed("/maps", "Run One.lvl")), 1)
        self.assertEqual(len(self.listed("/maps/all", "Run Extended.lvl")), 1)
        self.assertEqual(self.server.call("GET", "/maps/files/Run%20One.lvl", door="replay")[2], file_bytes)
        listing = j(self.owner_call("GET", "/admin/api/maps?state=waiting"))
        self.assertEqual(sorted(m["name"] for m in listing["maps"]), ["Tiny", "run one"])
        log = json.loads(self.owner_call("GET", "/admin/api/audit")[2])
        actions = [e["action"] for e in log["entries"]]
        for wanted in ("sign_in", "sign_in_failed", "password_changed", "user_added", "map_approved", "map_taken_down", "map_put_back"):
            self.assertIn(wanted, actions)
        self.assertNotIn(self.starting, json.dumps(log))
        self.assertEqual(self.server.call("GET", "/submissions/" + sid, {"Authorization": "Bearer " + token})[0], 200)       # (the maker's token still finds it)
        # the log: lines for the operator, nothing of a person
        out, err = self.server.stop()
        type(self).logs += err
        self.assertEqual(self.server.proc.returncode, 0)
        for fault in ("Sanitizer", "runtime error", "terminate called"):                         # (a build with the sanitizers says so in the log)
            self.assertNotIn(fault, self.logs)
        self.assertIn("map submissions on: files in", self.logs)
        for private in (token, "203.0.113.", "198.51.100.", "Mia", "Run One", "Run Extended", "made by hand", "Owner", "mia", self.starting, mia_starting, CHOSEN, "mia chose a long phrase", SECRET):
            self.assertNotIn(private, self.logs)
        # the reset command, beside the stopped server: a new password, and the old session and the old password are dead
        reset = subprocess.run([SERVER, "--admin-reset", "owner", "--results-dir", self.results], capture_output=True, text=True, timeout=60)
        self.assertEqual((reset.returncode, reset.stderr), (0, ""))
        fresh = re.search(r"New starting password: (\S{20})\n", reset.stdout).group(1)
        self.assertNotIn(fresh, self.logs)
        type(self).server = Server(self.results)                                                  # (the teardown stops one)
        self.owner.server = self.server
        self.assertEqual(self.me(self.owner.cookie), {"signed_in": False})                         # (the reset ended the session)
        self.assertEqual(self.server.call("POST", "/admin/api/login", JSON_HEADERS, json.dumps({"username": "Owner", "password": CHOSEN}).encode())[0], 401)
        again = Session(self.server, "Owner", fresh)
        self.assertTrue(self.me(again.cookie)["must_change"])


class TheSettings(unittest.TestCase):
    @unittest.skipUnless(os.path.exists(SERVER), "ants_server is not built")
    def test_the_door_needs_a_database_and_a_folder_and_the_owners_page_needs_both_settings(self):
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
            # the account commands need a database and exclude each other
            done = subprocess.run([SERVER, "--admin-create", "Owner"], capture_output=True, text=True, timeout=30)
            self.assertEqual((done.returncode, done.stdout), (1, ""))
            self.assertIn("--db", done.stderr)
            done = subprocess.run([SERVER, "--admin-create", "Owner", "--admin-reset", "Owner", "--results-dir", work], capture_output=True, text=True, timeout=30)
            self.assertEqual(done.returncode, 2)
            done = subprocess.run([SERVER, "--admin-reset", "nobody", "--results-dir", work], capture_output=True, text=True, timeout=30)
            self.assertEqual((done.returncode, done.stdout), (1, ""))
            self.assertIn("no user", done.stderr)
            # without the session secret the server runs and says the owner's page is off
            proc = subprocess.Popen(base + ["--results-dir", work + "/ok"], env={"PATH": os.environ.get("PATH", "")}, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            time.sleep(1.5)
            proc.send_signal(signal.SIGTERM)
            out, err = proc.communicate(timeout=15)
            self.assertIn("the owner's review page is OFF (ANTS_ADMIN_SESSION_SECRET", err)
        finally:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()
