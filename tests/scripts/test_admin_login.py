#!/usr/bin/env python3
"""The login helper of the owner's page (tools/admin_login.py, docs/MAP_SUBMISSIONS.md "The owner's page"), run against a fake Discord (run by ./run_tests.sh --fast and by the CI).

The helper sends the browser to Discord, takes the answer at /admin/callback, asks Discord who the person is and hands the browser a signed cookie, which the game server checks on every call. These tests RUN
the real program (a copy of the settings in its environment, the fake Discord on a loopback port) and read what it sends:

  - the settings are checked (a missing, short or wrong one is said in one sentence); a helper without settings answers 503 to everything and does not exit (a container that exits is restarted for ever);
  - /admin/login redirects to Discord with the exact parameters and a random state that is also in a cookie (HttpOnly, SameSite=Lax, Path=/admin, 10 minutes, Secure behind https);
  - /admin/callback with the right state trades the code, sets the session cookie (the signature checks out with the shared secret, the id and name are Discord's, the life is 8 hours) and clears the state; it refuses
    a missing, wrong or repeated state, a missing code, an error from Discord, a Discord that says no or does not answer, and never reflects what the visitor sent;
  - /admin/logout is a POST that deletes the cookie; every other path is a 404; nothing of a person or a code is logged (the helper's stderr is read when it stops).
"""
import hashlib
import hmac
import http.client
import importlib.util
import json
import os
import re
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock
import urllib.parse
import base64
from http.server import BaseHTTPRequestHandler

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import quick_http  # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCRIPT = os.path.join(REPO, "tools", "admin_login.py")
SECRET = "0123456789abcdef0123456789abcdef0123"
CLIENT_ID = "123456789012345678"


def load_module():
    spec = importlib.util.spec_from_file_location("admin_login", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class FakeDiscord:
    """The two calls of the logon: the token and who the person is. Records what it was asked."""

    def __init__(self):
        self.requests = []
        self.users = {"good-1": {"id": "111222333444", "username": "daveuser", "global_name": "Dave"},
                      "good-2": {"id": "999888777666", "username": "miauser", "global_name": None},
                      "good-evil": {"id": "555444333222", "username": "x", "global_name": "a\r\nSet-Cookie: pwned=1"},
                      "good-noid": {"id": "not digits", "username": "x"}}
        outer = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def reply(self, status, obj):
                body = json.dumps(obj).encode()
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def do_POST(self):
                length = int(self.headers.get("Content-Length", "0"))
                form = urllib.parse.parse_qs(self.rfile.read(length).decode())
                outer.requests.append(("POST", self.path, dict((k, v[0]) for k, v in form.items()), dict(self.headers)))
                code = form.get("code", [""])[0]
                if self.path != "/api/oauth2/token" or code not in outer.users:
                    return self.reply(400, {"error": "invalid_grant"})
                self.reply(200, {"access_token": "token-for-" + code, "token_type": "Bearer"})

            def do_GET(self):
                outer.requests.append(("GET", self.path, {}, dict(self.headers)))
                auth = self.headers.get("Authorization", "")
                code = auth[len("Bearer token-for-"):] if auth.startswith("Bearer token-for-") else ""
                if self.path != "/api/users/@me" or code not in outer.users:
                    return self.reply(401, {"message": "401: Unauthorized"})
                self.reply(200, outer.users[code])

        self.server = quick_http.QuickThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.port = self.server.server_address[1]
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def stop(self):
        self.server.shutdown()
        self.server.server_close()


def start_helper(extra_env=None, drop=()):
    """Starts the helper on a port that was free a moment ago and waits until the helper itself answers an HTTP request on it (a connection alone could be
    some other program's socket). A port that someone took in between, or a helper that is slow, gets a fresh port and another try; the last failure says
    what the helper wrote, whether it still ran and which port it was."""
    notes = []
    for attempt in range(3):
        port = free_port()
        env = {"PATH": os.environ.get("PATH", ""), "ANTS_DISCORD_CLIENT_ID": CLIENT_ID, "ANTS_DISCORD_CLIENT_SECRET": "client-secret-xyz", "ANTS_ADMIN_SESSION_SECRET": SECRET,
               "ANTS_PUBLIC_URL": "https://example.test", "ANTS_ADMIN_LOGIN_PORT": str(port)}
        env.update(extra_env or {})
        for name in drop:
            env.pop(name, None)
        proc = subprocess.Popen([sys.executable, "-I", SCRIPT], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        deadline = time.monotonic() + 20
        last = ""
        while time.monotonic() < deadline:
            try:
                c = http.client.HTTPConnection("127.0.0.1", port, timeout=2)
                c.request("GET", "/admin/login")
                c.getresponse().read()
                c.close()
                return proc, port
            except (OSError, http.client.HTTPException) as e:
                last = repr(e)
                if proc.poll() is not None:
                    break
                time.sleep(0.05)
        running = proc.poll() is None
        if running:
            proc.kill()
        out, err = proc.communicate(timeout=10)
        notes.append("attempt %d: port %d, helper %s (exit %s), last error %s, stdout %r, stderr %r" % (
            attempt + 1, port, "still running and killed" if running else "ended", proc.returncode, last, out[-500:], err[-1500:]))
    raise AssertionError("the helper did not start: " + " | ".join(notes))


def stop_helper(proc):
    if proc.poll() is None:
        proc.send_signal(signal.SIGTERM)
    try:
        return proc.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        return proc.communicate()


def ask(port, method, path, headers=None, body=None):
    c = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
    c.request(method, path, body=body, headers=headers or {})
    r = c.getresponse()
    data = r.read()
    cookies = [v for k, v in r.getheaders() if k.lower() == "set-cookie"]
    out = (r.status, dict((k.lower(), v) for k, v in r.getheaders()), data, cookies)
    c.close()
    return out


def cookie_pair(set_cookie):
    return set_cookie.split(";", 1)[0]


def attributes(set_cookie):
    return [a.strip() for a in set_cookie.split(";")[1:]]


def verify(value, secret=SECRET):
    """What the game server does: v1.<payload>.<mac>; returns the payload when the signature is right."""
    signed, _, mac = value.rpartition(".")
    if not hmac.compare_digest(mac, hmac.new(secret.encode(), signed.encode(), hashlib.sha256).hexdigest()):
        return None
    payload = signed[3:]
    return json.loads(base64.urlsafe_b64decode(payload + "=" * (-len(payload) % 4)).decode("utf-8"))


class TheStart(unittest.TestCase):
    def test_the_server_binds_and_serves_without_asking_for_the_name_of_the_machine(self):
        # (HTTPServer.server_bind() calls socket.getfqdn(): on the macOS runner of the CI the helper sat in that reverse lookup for minutes and never listened)
        module = load_module()

        def refuse(*args, **kwargs):
            raise AssertionError("a name lookup: %r" % (args,))
        with mock.patch.object(socket, "getfqdn", refuse), mock.patch.object(socket, "gethostbyaddr", refuse), mock.patch.object(socket, "gethostbyname", refuse):
            server = module.QuickServer(("127.0.0.1", 0), module.make_off_handler())
        self.addCleanup(server.server_close)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        self.addCleanup(server.shutdown)
        status, headers, body, cookies = ask(server.server_address[1], "GET", "/admin/login")
        self.assertEqual(status, 503)
        self.assertTrue(server.daemon_threads)


class TheSettings(unittest.TestCase):
    def test_the_logon_does_not_ask_discord_to_skip_the_screen_and_a_silent_client_is_let_go(self):
        module = load_module()
        cfg = module.load_config({"ANTS_DISCORD_CLIENT_ID": CLIENT_ID, "ANTS_DISCORD_CLIENT_SECRET": "s", "ANTS_ADMIN_SESSION_SECRET": SECRET, "ANTS_PUBLIC_URL": "https://example.test"})
        self.assertEqual(module.make_handler(cfg).timeout, module.SOCKET_SECONDS)
        self.assertEqual(module.make_off_handler().timeout, module.SOCKET_SECONDS)
        self.assertTrue(0 < module.SOCKET_SECONDS <= 30)

    def test_a_missing_or_wrong_setting_is_said_in_one_sentence(self):
        module = load_module()
        good = {"ANTS_DISCORD_CLIENT_ID": CLIENT_ID, "ANTS_DISCORD_CLIENT_SECRET": "s", "ANTS_ADMIN_SESSION_SECRET": SECRET, "ANTS_PUBLIC_URL": "https://example.test"}
        cfg = module.load_config(good)
        self.assertEqual((cfg["redirect_uri"], cfg["port"], cfg["hours"], cfg["secure"]), ("https://example.test/admin/callback", 4040, 8, True))
        self.assertFalse(module.load_config(dict(good, ANTS_PUBLIC_URL="http://localhost:8080/"))["secure"])
        self.assertEqual(module.load_config(dict(good, ANTS_PUBLIC_URL="https://example.test/"))["public_url"], "https://example.test")
        for name in good:
            broken = dict(good)
            del broken[name]
            with self.assertRaises(module.ConfigError) as caught:
                module.load_config(broken)
            self.assertIn(name, str(caught.exception))
        bad = [("ANTS_ADMIN_SESSION_SECRET", "short"), ("ANTS_ADMIN_SESSION_SECRET", "x" * 31 + " "), ("ANTS_ADMIN_SESSION_SECRET", "x" * 40 + "é"), ("ANTS_PUBLIC_URL", "example.test"),
               ("ANTS_PUBLIC_URL", "https://example.test/path"), ("ANTS_PUBLIC_URL", "https://exa mple.test"), ("ANTS_PUBLIC_URL", "ftp://example.test"), ("ANTS_DISCORD_CLIENT_ID", "abc"),
               ("ANTS_ADMIN_LOGIN_PORT", "0"), ("ANTS_ADMIN_LOGIN_PORT", "70000"), ("ANTS_ADMIN_LOGIN_PORT", "x"), ("ANTS_ADMIN_SESSION_HOURS", "0"), ("ANTS_ADMIN_SESSION_HOURS", "25"),
               ("ANTS_ADMIN_SESSION_HOURS", "x")]
        for name, value in bad:
            with self.assertRaises(module.ConfigError, msg=name + "=" + value):
                module.load_config(dict(good, **{name: value}))

    def test_a_helper_without_its_settings_answers_503_to_everything_and_stays_up(self):
        proc, port = start_helper(drop=("ANTS_DISCORD_CLIENT_SECRET",))
        try:
            for method, path in (("GET", "/admin/login"), ("GET", "/admin/callback?code=x&state=y"), ("POST", "/admin/logout"), ("GET", "/other")):
                status, headers, body, cookies = ask(port, method, path, body="" if method == "POST" else None)
                if path.startswith(("/admin/login", "/admin/callback")):                 # (a person's browser gets words, not JSON)
                    self.assertTrue(status == 503 and "text/html" in headers["content-type"] and "not set up" in body.decode() and b"admin_login_off" not in body and cookies == [], path)
                else:
                    self.assertEqual((status, json.loads(body)["error"], cookies), (503, "admin_login_off", []), path)
            self.assertIsNone(proc.poll())
        finally:
            _, err = stop_helper(proc)
        self.assertIn("ANTS_DISCORD_CLIENT_SECRET", err)


class TheLogon(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.discord = FakeDiscord()
        cls.proc, cls.port = start_helper({"ANTS_DISCORD_API": "http://127.0.0.1:%d/api" % cls.discord.port, "ANTS_DISCORD_AUTHORIZE_URL": "http://127.0.0.1:%d/authorize" % cls.discord.port})

    @classmethod
    def tearDownClass(cls):
        cls.out, cls.err = stop_helper(cls.proc)
        cls.discord.stop()

    def begin(self):
        status, headers, body, cookies = ask(self.port, "GET", "/admin/login")
        self.assertEqual(status, 302)
        url = urllib.parse.urlsplit(headers["location"])
        args = urllib.parse.parse_qs(url.query)
        return headers, args, cookies

    def test_login_redirects_to_discord_with_a_random_state_that_is_also_in_a_cookie(self):
        headers, args, cookies = self.begin()
        self.assertTrue(headers["location"].startswith("http://127.0.0.1:%d/authorize?" % self.discord.port))
        self.assertEqual(args["client_id"], [CLIENT_ID])
        self.assertEqual(args["response_type"], ["code"])
        self.assertNotIn("prompt", args)                                                       # (prompt=none would make a first logon fail: Discord must be free to ask)
        self.assertEqual(args["scope"], ["identify"])                                                   # (nothing more is asked of Discord)
        self.assertEqual(args["redirect_uri"], ["https://example.test/admin/callback"])
        self.assertTrue(re.fullmatch(r"[A-Za-z0-9_-]{43}", args["state"][0]))
        self.assertEqual(len(cookies), 1)
        self.assertEqual(cookie_pair(cookies[0]), "ants_admin_state=" + args["state"][0])
        for flag in ("HttpOnly", "SameSite=Lax", "Path=/admin", "Max-Age=600", "Secure"):
            self.assertIn(flag, attributes(cookies[0]))
        self.assertNotIn("client-secret-xyz", headers["location"])
        self.assertEqual(headers["cache-control"], "no-store")
        self.assertNotEqual(self.begin()[1]["state"], args["state"])                                       # (a new state each time)

    def callback(self, state_cookie, query):
        headers = {"Cookie": "ants_admin_state=" + state_cookie} if state_cookie is not None else {}
        return ask(self.port, "GET", "/admin/callback?" + query, headers)

    def test_a_good_callback_sets_a_signed_session_and_clears_the_state(self):
        _, args, _ = self.begin()
        state = args["state"][0]
        before = len(self.discord.requests)
        status, headers, body, cookies = self.callback(state, "code=good-1&state=" + state)
        self.assertEqual((status, headers["location"]), (302, "/admin"))
        names = dict((cookie_pair(c).split("=", 1)[0], c) for c in cookies)
        self.assertEqual(sorted(names), ["ants_admin", "ants_admin_state"])
        self.assertIn("Max-Age=0", attributes(names["ants_admin_state"]))
        session = names["ants_admin"]
        for flag in ("HttpOnly", "SameSite=Lax", "Path=/admin", "Max-Age=28800", "Secure"):
            self.assertIn(flag, attributes(session))
        payload = verify(cookie_pair(session).split("=", 1)[1])
        self.assertIsNotNone(payload)
        self.assertEqual((payload["id"], payload["name"]), ("111222333444", "Dave"))
        self.assertTrue(abs(payload["exp"] - (time.time() + 8 * 3600)) < 30)
        self.assertTrue(re.fullmatch(r"[A-Za-z0-9_-]{16}", payload["n"]))
        self.assertIsNone(verify(cookie_pair(session).split("=", 1)[1], "another-secret-another-secret-another"))      # (the secret is what makes it theirs)
        # what Discord was asked: the code trade as a form with the secret in the body, then the person with the token
        new = self.discord.requests[before:]
        self.assertEqual([(r[0], r[1]) for r in new], [("POST", "/api/oauth2/token"), ("GET", "/api/users/@me")])
        form = new[0][2]
        self.assertEqual((form["client_id"], form["client_secret"], form["grant_type"], form["code"], form["redirect_uri"]), (CLIENT_ID, "client-secret-xyz", "authorization_code", "good-1", "https://example.test/admin/callback"))
        self.assertEqual(new[1][3]["Authorization"], "Bearer token-for-good-1")
        self.assertTrue(new[0][3]["User-Agent"] and "Python" not in new[0][3]["User-Agent"])           # (Discord refuses the default agent of the library)
        # a second person gets a cookie too: the helper decides nothing about who the owner is
        _, args2, _ = self.begin()
        status, headers, body, cookies = self.callback(args2["state"][0], "code=good-2&state=" + args2["state"][0])
        payload2 = verify(cookie_pair([c for c in cookies if c.startswith("ants_admin=")][0]).split("=", 1)[1])
        self.assertEqual((payload2["id"], payload2["name"]), ("999888777666", "miauser"))                 # (no global name: the user name)
        self.assertNotEqual(payload2["n"], payload["n"])

    def test_the_state_must_come_back_exactly_and_only_once_in_one_piece(self):
        _, args, _ = self.begin()
        state = args["state"][0]
        before = len(self.discord.requests)
        refused = [self.callback(None, "code=good-1&state=" + state), self.callback("", "code=good-1&state=" + state), self.callback(state + "x", "code=good-1&state=" + state),
                   self.callback(state, "code=good-1&state=" + state[:-1] + ("A" if state[-1] != "A" else "B")), self.callback(state, "code=good-1"),
                   self.callback(state, "code=good-1&state="), self.callback(state, "code=good-1&state=" + state + "&state=" + state)]
        for status, headers, body, cookies in refused:
            self.assertEqual(status, 400)
            self.assertFalse([c for c in cookies if c.startswith("ants_admin=")])
        self.assertEqual(len(self.discord.requests), before)                                            # (Discord was not even asked)

    def test_a_callback_without_a_code_or_with_an_error_or_with_a_refusing_or_silent_discord_is_refused_and_nothing_is_reflected(self):
        _, args, _ = self.begin()
        state = args["state"][0]
        odd = "<script>alert(1)</script>"
        cases = [(400, "state=%s" % state), (400, "code=&state=%s" % state), (400, "code=good-1&error=access_denied&state=%s" % state), (400, "code=%s&state=%s" % ("x" * 600, state)),
                 (502, "code=%s&state=%s" % (urllib.parse.quote(odd), state)), (502, "code=unknown&state=%s" % state), (502, "code=good-noid&state=%s" % state)]
        for want, query in cases:
            status, headers, body, cookies = self.callback(state, query)
            self.assertEqual(status, want, query)
            self.assertFalse([c for c in cookies if c.startswith("ants_admin=")], query)
            self.assertNotIn(b"script", body)
            self.assertNotIn(b"alert", body)
            self.assertNotIn(state.encode(), body)
        self.assertEqual(headers["content-type"], "text/html; charset=utf-8")

    def test_a_name_that_tries_to_end_the_header_is_only_data_in_the_signed_text(self):
        _, args, _ = self.begin()
        state = args["state"][0]
        status, headers, body, cookies = self.callback(state, "code=good-evil&state=" + state)
        self.assertEqual(status, 302)
        self.assertEqual(sorted(cookie_pair(c).split("=", 1)[0] for c in cookies), ["ants_admin", "ants_admin_state"])      # (no third cookie came out of the name)
        value = cookie_pair([c for c in cookies if c.startswith("ants_admin=")][0]).split("=", 1)[1]
        self.assertTrue(re.fullmatch(r"v1\.[A-Za-z0-9_-]+\.[0-9a-f]{64}", value))
        self.assertEqual(verify(value)["name"], "a\r\nSet-Cookie: pwned=1")

    def test_logout_is_a_post_that_deletes_the_cookie_and_everything_else_is_not_found(self):
        status, headers, body, cookies = ask(self.port, "POST", "/admin/logout", body="")
        self.assertEqual((status, headers["location"]), (303, "/admin"))
        self.assertEqual(len(cookies), 1)
        self.assertEqual(cookie_pair(cookies[0]), "ants_admin=")
        self.assertIn("Max-Age=0", attributes(cookies[0]))
        self.assertEqual(ask(self.port, "GET", "/admin/logout")[0], 404)
        for method, path in (("GET", "/"), ("GET", "/admin"), ("GET", "/admin/"), ("GET", "/admin/api/me"), ("POST", "/admin/login"), ("POST", "/admin/callback"), ("GET", "/admin/callback/x"), ("PUT", "/admin/login")):
            self.assertEqual(ask(self.port, method, path, body="" if method in ("POST", "PUT") else None)[0] in (404, 501), True, method + " " + path)


class TheLog(unittest.TestCase):
    def test_nothing_of_a_person_or_a_code_is_logged(self):
        discord = FakeDiscord()
        proc, port = start_helper({"ANTS_DISCORD_API": "http://127.0.0.1:%d/api" % discord.port})
        try:
            status, headers, body, cookies = ask(port, "GET", "/admin/login")
            state = urllib.parse.parse_qs(urllib.parse.urlsplit(headers["location"]).query)["state"][0]
            status, headers, body, cookies = ask(port, "GET", "/admin/callback?code=good-1&state=" + state, {"Cookie": "ants_admin_state=" + state})
            self.assertEqual(status, 302)
        finally:
            out, err = stop_helper(proc)
            discord.stop()
        for secret in ("good-1", "token-for", "client-secret-xyz", SECRET, "111222333444", "Dave", "daveuser", state):
            self.assertNotIn(secret, out + err)
        self.assertIn("admin login helper on port", err)


class TheHttpVariant(unittest.TestCase):
    def test_behind_plain_http_the_cookies_are_not_marked_secure(self):
        discord = FakeDiscord()
        proc, port = start_helper({"ANTS_DISCORD_API": "http://127.0.0.1:%d/api" % discord.port, "ANTS_PUBLIC_URL": "http://localhost:8080", "ANTS_ADMIN_SESSION_HOURS": "2"})
        try:
            status, headers, body, cookies = ask(port, "GET", "/admin/login")
            self.assertNotIn("Secure", attributes(cookies[0]))
            self.assertIn("redirect_uri=http%3A%2F%2Flocalhost%3A8080%2Fadmin%2Fcallback", headers["location"])
            state = urllib.parse.parse_qs(urllib.parse.urlsplit(headers["location"]).query)["state"][0]
            status, headers, body, cookies = ask(port, "GET", "/admin/callback?code=good-1&state=" + state, {"Cookie": "ants_admin_state=" + state})
            session = [c for c in cookies if c.startswith("ants_admin=")][0]
            self.assertTrue("Secure" not in attributes(session) and "Max-Age=7200" in attributes(session) and "HttpOnly" in attributes(session))
        finally:
            stop_helper(proc)
            discord.stop()

    def test_a_discord_that_is_not_there_is_a_502_page_and_no_cookie(self):
        proc, port = start_helper({"ANTS_DISCORD_API": "http://127.0.0.1:%d/api" % free_port()})
        try:
            status, headers, body, cookies = ask(port, "GET", "/admin/login")
            state = urllib.parse.parse_qs(urllib.parse.urlsplit(headers["location"]).query)["state"][0]
            status, headers, body, cookies = ask(port, "GET", "/admin/callback?code=good-1&state=" + state, {"Cookie": "ants_admin_state=" + state})
            self.assertEqual(status, 502)
            self.assertFalse([c for c in cookies if c.startswith("ants_admin=")])
        finally:
            stop_helper(proc)


if __name__ == "__main__":
    unittest.main()
