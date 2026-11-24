#!/usr/bin/env python3
"""The plays and the likes of replays end to end, with the real program (suite 2.19.27 of ./run_tests.sh; docs/REPLAY_COUNTS.md).

The real ants_server (BUILD_DIR/src/ants_server/ants_server) is started with a database, a recording that the arena made (BUILD_DIR/bot_arena --save-replays), a --counts-port and a made-up key in
ANTS_ADMIN_GATE_SECRET, and is asked what the site's nginx would ask: a play (no pass, no cookie), the list of a person's likes with a pass that this file makes from the words of the contract
alone (hmac and hashlib: the independent maker), a like and an unlike with the token that the list gave, Forget me with the hub's own call. What the public door lists afterwards (plays, likes, the two
sorts) is read from the REAL list. Also: a request with a body or a query, the wrong method, a request that says it is cross-site, a pass that is used twice, a server with no good key (plays count, likes say
likes_off), a server without the port (nothing listens), a port that is taken (the game goes on, the log says so), and a log that holds no pass, no reference and no address.
Exit 0: all passed (or the program is not built: said, and 0).
"""
import hashlib
import hmac
import base64
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

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
BUILD = os.environ.get("BUILD_DIR", "build")
SERVER = os.path.join(REPO, BUILD, "src", "ants_server", "ants_server")
ARENA = os.path.join(REPO, BUILD, "bot_arena")
MAPS = os.path.join(REPO, "Original-Ants", "Maps")
SECRET = "checking-like-key-6e2f9a41c8b07d35-k"            # made up for this file
REF_ANN = "0123456789abcdef"
REF_BO = "fedcba9876543210"
sys.path.insert(0, HERE)
import quick_http  # noqa: E402


def free_port():
    return quick_http.free_port()


def pass_for(ref, nonce, life=60, secret=SECRET, now=None):
    """A like pass, made from the contract's words alone: v1.<base64url of the sorted JSON>.<hex HMAC-SHA-256 under HMAC(secret, label)>."""
    key = hmac.new(secret.encode(), b"ants-like-pass-v1", hashlib.sha256).digest()
    payload = json.dumps({"exp": int(now if now is not None else time.time()) + life, "nonce": nonce, "ref": ref}, separators=(",", ":"), sort_keys=True).encode()
    body = "v1." + base64.urlsafe_b64encode(payload).rstrip(b"=").decode()
    return body + "." + hmac.new(key, body.encode(), hashlib.sha256).hexdigest()


def token_for(ref, secret=SECRET):
    key = hmac.new(secret.encode(), b"ants-like-csrf-v1", hashlib.sha256).digest()
    return hmac.new(key, b"csrf1\0" + ref.encode(), hashlib.sha256).hexdigest()[:32]


class Counter:
    n = 0

    @classmethod
    def nonce(cls):
        cls.n += 1
        return "%016x" % (0x5000 + cls.n)


class Server:
    def __init__(self, results, counts=True, secret=SECRET, extra=()):
        self.results = results
        self.counts_port = free_port() if counts else 0
        self.replay_port = free_port()
        self.ws_port = free_port()
        args = [SERVER, "--maps", MAPS, "--port", "0", "--ws-port", str(self.ws_port), "--replay-port", str(self.replay_port), "--results-dir", results]
        if counts:
            args += ["--counts-port", str(self.counts_port)]
        args += list(extra)
        env = dict(os.environ)
        env.pop("ANTS_ADMIN_GATE_SECRET", None)
        if secret:
            env["ANTS_ADMIN_GATE_SECRET"] = secret
        self.proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)
        for _ in range(400):
            try:
                socket.create_connection(("127.0.0.1", self.replay_port), 0.2).close()
                return
            except OSError:
                if self.proc.poll() is not None:
                    break
                time.sleep(0.05)
        err = self.proc.communicate(timeout=10)[1] if self.proc.poll() is not None else ""
        raise AssertionError("the server did not start: " + err)

    def stop(self):
        """Stops the program (SIGTERM) and gives what it wrote (stdout, stderr); a second call gives the same."""
        if getattr(self, "output", None) is not None:
            return self.output
        if self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
        try:
            self.output = self.proc.communicate(timeout=15)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.output = self.proc.communicate()
        return self.output

    def call(self, method, path, headers=None, body=None, door="counts"):
        port = {"counts": self.counts_port, "replay": self.replay_port}[door]
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

    def person(self, ref, method, path, token=True, **extra):
        headers = {"X-Ants-Like-Pass": pass_for(ref, Counter.nonce())}
        if token:
            headers["X-Ants-Csrf"] = token_for(ref)
        headers.update(extra)
        return self.call(method, path, headers)

    def history(self, query=""):
        status, _, body = self.call("GET", "/history" + ("?" + query if query else ""), door="replay")
        return status, json.loads(body.decode())

    def wait_counted(self, wanted=1, seconds=120):
        end = time.time() + seconds
        while time.time() < end:
            status, data = self.history()
            if status == 200 and data.get("total", 0) >= wanted:
                return data
            time.sleep(0.3)
        raise AssertionError("the match was not counted in %d s" % seconds)


def j(triple):
    return json.loads(triple[2].decode("utf-8"))


@unittest.skipUnless(os.path.exists(SERVER) and os.path.exists(ARENA), "ants_server and bot_arena are not built (BUILD_DIR=%s): the plays and likes were NOT run end to end" % BUILD)
class TheWholeWay(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.mkdtemp(prefix="ants_test_counts_e2e_")
        cls.results = os.path.join(cls.work, "results")
        replays = os.path.join(cls.results, "replays")
        os.makedirs(replays)
        made = subprocess.run([ARENA, "--map", "TINY", "--seeds", "3..3", "--ticks", "1400", "--quiet", "--save-replays", replays, "--maps-dir", MAPS], capture_output=True, text=True, timeout=300)
        assert made.returncode == 0, made.stderr
        cls.server = Server(cls.results)
        data = cls.server.wait_counted(1)
        cls.id = data["history"][0]["id"]
        cls.logs = ""

    @classmethod
    def tearDownClass(cls):
        cls.server.stop()
        shutil.rmtree(cls.work, ignore_errors=True)

    def likes_of(self):
        status, data = self.server.history()
        self.assertEqual(status, 200)
        row = [r for r in data["history"] if r["id"] == self.id][0]
        return row["plays"], row["likes"]

    def test_a_play_is_one_post_with_no_pass_and_the_list_shows_it(self):
        s = self.server
        before = self.likes_of()[0]
        status, headers, body = s.call("POST", "/counts/play/" + self.id, {"Sec-Fetch-Site": "same-origin"})
        self.assertEqual((status, body), (204, b""))
        self.assertEqual(self.likes_of()[0], before + 1)
        status, _, body = s.call("GET", "/history/" + self.id, door="replay")
        self.assertEqual(json.loads(body)["plays"], before + 1)
        self.assertEqual(s.call("POST", "/counts/play/ants-TINY-20000101-000000Z")[0], 404)
        self.assertEqual(s.call("POST", "/counts/play/nope")[0], 404)
        self.assertEqual(s.call("GET", "/counts/play/" + self.id)[0], 405)
        self.assertEqual(s.call("POST", "/counts/play/" + self.id + "?x=1")[0], 404)
        self.assertEqual(s.call("POST", "/counts/play/" + self.id, {"Sec-Fetch-Site": "cross-site"})[0], 403)
        self.assertEqual(s.call("POST", "/counts/play/" + self.id, None, b"{}")[0], 413)               # (a body is turned away on the head: the door reads none)
        self.assertEqual(self.likes_of()[0], before + 1)

    def test_a_person_lists_likes_and_likes_and_unlikes_with_the_token_and_the_list_follows(self):
        s = self.server
        status, _, body = s.person(REF_ANN, "GET", "/counts/mine?ids=" + self.id, token=False)
        self.assertEqual(status, 200)
        mine = json.loads(body)
        self.assertEqual((mine["signed_in"], mine["liked"], mine["csrf"]), (True, [], token_for(REF_ANN)))
        self.assertNotIn(REF_ANN, body.decode())
        before = self.likes_of()[1]
        status, _, body = s.person(REF_ANN, "POST", "/counts/like/" + self.id)
        self.assertEqual((status, json.loads(body)), (200, {"liked": True, "likes": before + 1}))
        status, _, body = s.person(REF_ANN, "POST", "/counts/like/" + self.id)
        self.assertEqual(json.loads(body)["likes"], before + 1)                                         # (once)
        self.assertEqual(self.likes_of()[1], before + 1)
        self.assertEqual(json.loads(s.person(REF_ANN, "GET", "/counts/mine?ids=" + self.id, token=False)[2])["liked"], [self.id])
        self.assertEqual(json.loads(s.person(REF_BO, "GET", "/counts/mine?ids=" + self.id, token=False)[2])["liked"], [])
        status, data = s.history("sort=likes&limit=1")
        self.assertEqual(data["history"][0]["id"], self.id)
        status, _, body = s.person(REF_ANN, "POST", "/counts/unlike/" + self.id)
        self.assertEqual(json.loads(body), {"liked": False, "likes": before})
        self.assertEqual(self.likes_of()[1], before)

    def test_every_refusal_of_a_like(self):
        s = self.server
        before = self.likes_of()[1]
        self.assertEqual(s.person(REF_ANN, "POST", "/counts/like/" + self.id, token=False)[0], 403)
        self.assertEqual(s.call("POST", "/counts/like/" + self.id, {"X-Ants-Csrf": token_for(REF_ANN)})[0], 401)         # (no pass)
        self.assertEqual(s.call("POST", "/counts/like/" + self.id, {"X-Ants-Like-Pass": pass_for(REF_ANN, Counter.nonce(), secret="another-like-key-0c7e5b19d3a64f82-m"), "X-Ants-Csrf": token_for(REF_ANN)})[0], 401)
        self.assertEqual(s.call("POST", "/counts/like/" + self.id, {"X-Ants-Like-Pass": pass_for(REF_ANN, Counter.nonce(), life=-5), "X-Ants-Csrf": token_for(REF_ANN)})[0], 401)
        used = pass_for(REF_ANN, Counter.nonce())
        self.assertEqual(s.call("GET", "/counts/mine", {"X-Ants-Like-Pass": used})[0], 200)
        self.assertEqual(s.call("GET", "/counts/mine", {"X-Ants-Like-Pass": used})[0], 401)                             # (a pass is used once)
        self.assertEqual(s.person(REF_ANN, "POST", "/counts/like/" + self.id, **{"Sec-Fetch-Site": "cross-site"})[0], 403)
        self.assertEqual(s.person(REF_ANN, "GET", "/counts/like/" + self.id)[0], 405)
        self.assertEqual(s.person(REF_ANN, "POST", "/counts/like/ants-TINY-20000101-000000Z")[0], 404)
        self.assertEqual(s.person(REF_ANN, "GET", "/counts/mine?ids=nope", token=False)[0], 400)
        self.assertEqual(self.likes_of()[1], before)

    def test_forget_me_takes_back_every_like_of_the_person_and_the_count_drops(self):
        s = self.server
        before = self.likes_of()[1]
        s.person(REF_BO, "POST", "/counts/like/" + self.id)
        self.assertEqual(self.likes_of()[1], before + 1)
        status, _, body = s.person(REF_BO, "POST", "/counts/forget")
        self.assertEqual((status, json.loads(body)), (200, {"removed": 1}))
        self.assertEqual(self.likes_of()[1], before)
        self.assertEqual(json.loads(s.person(REF_BO, "POST", "/counts/forget")[2]), {"removed": 0})
        self.assertEqual(s.person(REF_BO, "POST", "/counts/forget", token=False)[0], 403)

    def test_without_a_good_key_plays_count_and_likes_are_off(self):
        results = os.path.join(self.work, "nokey")
        shutil.copytree(os.path.join(self.results, "replays"), os.path.join(results, "replays"))
        server = Server(results, secret="short")
        try:
            data = server.wait_counted(1)
            id_ = data["history"][0]["id"]
            self.assertEqual(server.call("POST", "/counts/play/" + id_)[0], 204)
            status, _, body = server.person(REF_ANN, "GET", "/counts/mine?ids=" + id_, token=False)
            self.assertEqual((status, json.loads(body)["error"]), (503, "likes_off"))
            self.assertEqual(server.person(REF_ANN, "POST", "/counts/like/" + id_)[0], 503)
            self.assertEqual(server.history()[1]["history"][0]["plays"], 1)
        finally:
            out, err = server.stop()
        self.assertIn("likes are OFF", out + err)
        self.assertNotIn(SECRET, out + err)

    def test_without_the_port_nothing_listens_and_a_taken_port_switches_the_door_off_and_the_game_goes_on(self):
        results = os.path.join(self.work, "noport")
        os.makedirs(results)
        server = Server(results, counts=False)
        try:
            with self.assertRaises(OSError):
                socket.create_connection(("127.0.0.1", free_port()), 0.2).close()
        finally:
            server.stop()
        with socket.socket() as taken:
            taken.bind(("127.0.0.1", 0))
            taken.listen(1)
            port = taken.getsockname()[1]
            ws, replay = free_port(), free_port()
            proc = subprocess.Popen([SERVER, "--maps", MAPS, "--port", "0", "--ws-port", str(ws), "--replay-port", str(replay), "--results-dir", os.path.join(self.work, "taken"), "--counts-port", str(port)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                for _ in range(300):
                    try:
                        socket.create_connection(("127.0.0.1", replay), 0.2).close()
                        break
                    except OSError:
                        time.sleep(0.05)
                time.sleep(1.0)                                                                            # (a server that stops because of the port stops within a moment of having bound the others)
                self.assertIsNone(proc.poll())
                conn = http.client.HTTPConnection("127.0.0.1", replay, timeout=10)
                conn.request("GET", "/healthz")
                self.assertEqual(conn.getresponse().status, 200)                                           # (the game's own doors are up)
                conn.close()
            finally:
                proc.send_signal(signal.SIGTERM)
                out, err = proc.communicate(timeout=15)
        self.assertIn("plays and likes of replays are off: cannot listen on counts port", out + err)

    def test_zz_the_server_log_holds_no_pass_no_reference_no_key_and_no_address(self):
        s = self.server
        s.call("GET", "/counts/mine", {"X-Ants-Like-Pass": "v1.e30." + "a" * 64})             # (a pass that is refused is a line of the log, once a minute)
        time.sleep(0.5)
        out, err = s.stop()
        text = out + err
        self.assertIn("plays and likes of replays on port", text)
        self.assertIn("likes are ON", text)
        self.assertIn("a pass was refused", text)
        for secret in (SECRET, REF_ANN, REF_BO, "v1.e30", "127.0.0.1"):
            self.assertNotIn(secret, text)


if __name__ == "__main__":
    unittest.main()
