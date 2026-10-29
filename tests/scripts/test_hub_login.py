#!/usr/bin/env python3
"""The Discord login of the chat hub (hub/login.py, hub/discord.py; docs/HUB.md "Login"; run by ./run_tests.sh --fast and by the CI), against a FAKE Discord (tests/scripts/fake_discord.py): real
sockets, the real hub, a browser that is a cookie jar (tests/scripts/login_flow.py). Real Discord is never asked.

  - /hub/login: the state (128 random bits, in the cookie and in the address), the cookie's flags, the return address as a word of a list (never what was sent), a link of another site, the limits;
  - /hub/callback: the good way through, and every way that is not one (no cookie, a state that is not the cookie's, unknown, used, expired, tossed twice, a replayed code, a visitor who said no,
    two callbacks at once), and Discord going wrong in every way (a status, no answer, a drip, a flood, JSON that is cut off or of the wrong shape, a redirect): a calm page, never a session;
  - what is kept: the id and the name, the SHA-256 of the cookie, nothing of the token; the cookie: __Host-, HttpOnly, Secure, SameSite=Strict, a new value for each login;
  - the socket: the identity is fixed at the handshake and ends with the login, Origin and Sec-Fetch-Site, young accounts, bans and mutes;
  - /hub/me, /hub/logout, /hub/forget: the same-origin rules, what they delete and what they never do (lift a ban);
  - no token, code, state, cookie, secret, account id or name in the log, for the hub in this process and for the real process.
"""
import concurrent.futures
import hashlib
import json
import os
import re
import signal
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.parse

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
import fake_discord as fd  # noqa: E402
import hub_client as hc  # noqa: E402
import hub_rig  # noqa: E402
from login_flow import Visitor  # noqa: E402
from hub import config as configmod, discord as discordmod, limits as limitmod, login as loginmod  # noqa: E402

CALLBACK = hc.SITE + "/hub/callback"
SESSION_COOKIE = re.compile(r"^__Host-ants_hub=([0-9a-f]{32}); Path=/; Max-Age=2592000; Secure; HttpOnly; SameSite=Strict$")
STATE_COOKIE = re.compile(r"^__Host-ants_oauth=([0-9a-f]{32}); Path=/; Max-Age=600; Secure; HttpOnly; SameSite=Lax$")
CLEARED_STATE = "__Host-ants_oauth=; Path=/; Max-Age=0; Secure; HttpOnly; SameSite=Lax"
CLEARED_SESSION = "__Host-ants_hub=; Path=/; Max-Age=0; Secure; HttpOnly; SameSite=Strict"
UNAVAILABLE = "Login unavailable, try again later"
OLD = fd.snowflake(900)                    # an account that is two and a half years old
FUTURE = fd.snowflake(-30)                 # an id that says it was made next month


def sha(value):
    return hashlib.sha256(value.encode("ascii")).hexdigest()


class LoginTest(unittest.TestCase):
    mods = ""
    limits = {}
    user = (OLD, "maple.leaf")
    env = None

    def setUp(self):
        self.fake = fd.FakeDiscord(CALLBACK, user=self.user)
        self.fake.consent = False
        self.rig = hub_rig.Rig(discord=self.fake, mods=self.mods, env=self.env, **dict(dict(sockets_per_address=100), **self.limits))
        self.rig.__enter__()
        self.clients = []
        self.count = 0
        self.addCleanup(self.finish)

    def finish(self):
        for c in self.clients:
            c.close()
        self.rig.__exit__(None, None, None)
        self.fake.close()

    # ---- helpers -----------------------------------------------------------------------------------------------------------------------------

    def visitor(self):
        self.count += 1
        return Visitor(self.rig.port, self.fake, address="198.51.100.%d" % self.count)

    def member(self, user=None, next=None):
        """A visitor who has logged in (as `user` if given), and the answer of the callback."""
        if user:
            self.fake.user = user
        v = self.visitor()
        r = v.login(next)
        self.assertEqual(r.status, 302, r.text())
        return v

    def rows(self, table="sessions"):
        db = sqlite3.connect(self.rig.db)
        try:
            return db.execute("SELECT * FROM %s" % table).fetchall()
        finally:
            db.close()

    def token_requests(self):
        return [r for r in self.fake.requests if r[0] == "POST" and r[1] == "/api/v10/oauth2/token"]

    def me_requests(self):
        return [r for r in self.fake.requests if r[1] == "/api/v10/users/@me"]

    def socket(self, v=None, headers=None, welcome=True, **kw):
        h = {"Sec-Fetch-Site": "same-origin"}
        if v is not None and v.jar.header():
            h["Cookie"] = v.jar.header()
        h.update(headers or {})
        c = hc.Client(self.rig.port, headers=h, **kw)
        self.clients.append(c)
        if c.ok and welcome:
            c.welcome = c.recv_json()
        return c

    def tick(self, seconds):
        """Move the store's clock (the login's and the sessions') forward."""
        base = self.rig.store.clock
        shift = seconds
        self.rig.call(setattr, self.rig.store, "clock", lambda: base() + shift)

    def assert_page(self, r, status, heading=None):
        self.assertEqual(r.status, status, r.text()[:300])
        self.assertTrue(r.get("content-type").startswith("text/html"), r.headers)
        self.assertIn("default-src 'none'", r.get("content-security-policy"))
        self.assertEqual(r.get("referrer-policy"), "no-referrer")
        self.assertEqual(r.get("cache-control"), "no-store")
        self.assertIn('<a href="/">Back to the front page</a>', r.text())
        if heading:
            self.assertIn("<h1>%s</h1>" % heading, r.text())
        for cookie in r.all("set-cookie"):
            self.assertFalse(cookie.startswith("__Host-ants_hub=") and "Max-Age=0" not in cookie, "a page that refuses must not log anybody in: " + cookie)

    def assert_no_login(self, v=None):
        self.assertEqual(self.rows(), [])
        if v is not None:
            self.assertIsNone(v.session())


# ===================================================================================================================================================
class TheStart(LoginTest):
    def test_it_sends_the_visitor_to_discord_with_a_state_and_a_cookie_for_it(self):
        v = self.visitor()
        r = v.start("home")
        self.assertEqual(r.status, 302)
        url = urllib.parse.urlsplit(r.get("location"))
        self.assertEqual("%s://%s%s" % (url.scheme, url.netloc, url.path), self.fake.url + "/oauth2/authorize")
        self.assertEqual(urllib.parse.parse_qsl(url.query),
                         [("client_id", self.fake.client_id), ("response_type", "code"), ("scope", "identify"), ("redirect_uri", CALLBACK), ("state", v.state), ("prompt", "none")])
        self.assertRegex(v.state, r"^[0-9a-f]{32}$")
        cookies = r.all("set-cookie")
        self.assertEqual(len(cookies), 1)
        m = STATE_COOKIE.match(cookies[0])
        self.assertIsNotNone(m, cookies[0])
        self.assertEqual(m.group(1), v.state)                                  # (the cookie holds what the address holds)
        self.assertEqual(r.get("cache-control"), "no-store")
        self.assertEqual(r.get("content-length"), "0")
        self.assertNotIn("secret", url.query.lower())
        self.assertNotIn(self.fake.client_secret, r.get("location"))

    def test_the_state_is_128_random_bits_and_never_twice(self):
        states = set()
        for _ in range(60):
            v = self.visitor()
            v.start()
            states.add(v.state)
            self.assertEqual(len(v.state), 32)
        self.assertEqual(len(states), 60)
        self.assertGreater(len({s[:6] for s in states}), 50)

    def test_the_return_address_is_a_word_of_a_list_and_never_what_was_sent(self):
        for value in ("home", "room", "", "HOME", "Room", "https://evil.example", "//evil.example", "/\\evil.example", "room%0d%0aSet-Cookie:%20x=1", "home&next=room", "room&next=home", "%00", "../",
                      "javascript:alert(1)", "room%20", "ro%6fm", "http%3A%2F%2Fevil.example%2F", "evil.example", "/", "/#room", "home#x", "a" * 1400, "%c3%a9"):
            v = self.visitor()
            r = v.start(value)
            self.assertEqual(r.status, 302, value)
            r = v.callback(v.at_discord(r.get("location")))
            self.assertEqual(r.status, 302, value)
            expect = "/#room" if value in ("room", "ro%6fm") else "/"                  # (the word room, as it is or with a letter written as %6f: the same word)
            self.assertEqual(r.get("location"), expect, value)
            self.assertIn(r.get("location"), ("/", "/#room"))
        self.assertEqual(self.visitor().start("a" * 3000).status, 400)             # (a request line of more than 2,000 characters is refused by the head reader)
        v = self.visitor()                                                       # a name that comes twice is nobody's
        v.start()
        r = v.call("GET", "/hub/login?next=room&next=room")
        v.state = dict(urllib.parse.parse_qsl(urllib.parse.urlsplit(r.get("location")).query))["state"]
        self.assertEqual(v.callback(v.at_discord(r.get("location"))).get("location"), "/")

    def test_a_link_from_another_site_cannot_start_a_login(self):
        for site in ("cross-site", "same-site", "bogus", ""):
            v = self.visitor()
            r = v.start(site=site)
            self.assert_page(r, 403, "Log in from the front page")
            self.assertEqual(r.all("set-cookie"), [], site)
        self.assertEqual(self.rig.call(lambda: len(self.rig.hub.sessions.states)), 0)
        for site in ("same-origin", "none", None):                               # (the button of the front page, the address typed, a browser with no Fetch Metadata)
            self.assertEqual(self.visitor().start(site=site).status, 302, site)

    def test_only_a_get(self):
        for method in ("POST", "PUT", "DELETE", "HEAD"):
            r = hc.request(self.rig.port, method, "/hub/login", {"Sec-Fetch-Site": "same-origin"})
            self.assertEqual(r.status, 405, method)
            self.assertEqual(r.get("allow"), "GET")
            self.assertEqual(r.all("set-cookie"), [])
        self.assertEqual(hc.request(self.rig.port, "GET", "/hub/login/").status, 404)
        self.assertEqual(hc.request(self.rig.port, "GET", "/hub/LOGIN").status, 404)

    def test_a_body_is_refused(self):
        r = hc.request(self.rig.port, "GET", "/hub/login", {"Content-Length": "5"}, send_length=False)
        self.assertEqual(r.status, 400)
        r = hc.request(self.rig.port, "GET", "/hub/login", [("Transfer-Encoding", "chunked")])
        self.assertEqual(r.status, 400)


class TheLimitsOfStart(LoginTest):
    limits = dict(login_burst=3, login_refill_seconds=0.5)

    def test_an_address_has_a_burst_and_then_a_pace_and_others_are_not_held_back(self):
        v = self.visitor()
        answers = [v.start().status for _ in range(5)]
        self.assertEqual(answers, [302, 302, 302, 429, 429])
        r = v.start()
        self.assert_page(r, 429, "Too many tries")
        self.assertEqual(r.all("set-cookie"), [])
        self.assertEqual(self.visitor().start().status, 302)                     # another address
        time.sleep(0.6)
        self.assertEqual(v.start().status, 302)                                  # the pace gives one back
        self.assertEqual(v.start().status, 429)

    def test_the_address_is_the_one_nginx_names_not_the_one_the_visitor_writes_in_a_header(self):
        v = self.visitor()
        for _ in range(3):
            v.start()
        r = v.call("GET", "/hub/login", headers=[("X-Forwarded-For", "203.0.113.9"), ("X-Real-IP", "203.0.113.10")])
        self.assertEqual(r.status, 429)


class TheLimitsOfTheCallback(LoginTest):
    limits = dict(login_burst=2, login_refill_seconds=60.0)

    def test_the_callback_has_a_limit_of_its_own(self):
        v = self.visitor()
        self.assertEqual([v.start().status for _ in range(2)], [302, 302])        # (the two have their own buckets: starts do not use up callbacks)
        back = CALLBACK + "?code=x&state=" + "0" * 32
        answers = [v.callback(back).status for _ in range(4)]
        self.assertEqual(answers, [400, 400, 429, 429])


class TheStatesAreBounded(LoginTest):
    limits = dict(login_states_max=5)

    def test_the_oldest_login_that_never_finished_goes_first(self):
        visitors = []
        for _ in range(7):
            v = self.visitor()
            r = v.start()
            visitors.append((v, r))
        self.assertEqual(self.rig.call(lambda: len(self.rig.hub.sessions.states)), 5)
        first, r = visitors[0]
        self.assert_page(first.callback(first.at_discord(r.get("location"))), 400, "That login has run out")
        last, r = visitors[-1]
        self.assertEqual(last.callback(last.at_discord(r.get("location"))).status, 302)


    def test_a_state_that_ran_out_is_swept_when_the_next_login_starts_and_when_the_hub_ticks(self):
        count = lambda: self.rig.call(lambda: len(self.rig.hub.sessions.states))
        for _ in range(3):
            self.visitor().start()
        self.assertEqual(count(), 3)
        self.tick(601)
        self.visitor().start()
        self.assertEqual(count(), 1)
        self.tick(601)
        self.rig.call(self.rig.hub.sessions.forget_idle, self.rig.hub.clock(), self.rig.store.clock())
        self.assertEqual(count(), 0)


# ===================================================================================================================================================
class TheCallback(LoginTest):
    def test_a_good_login_keeps_the_id_and_the_name_and_hands_out_the_cookie(self):
        v = self.visitor()
        r = v.login("home")
        self.assertEqual(r.status, 302)
        self.assertEqual(r.get("location"), "/")
        cookies = r.all("set-cookie")
        self.assertEqual(len(cookies), 2)
        m = SESSION_COOKIE.match(cookies[0])
        self.assertIsNotNone(m, cookies[0])
        self.assertEqual(cookies[1], CLEARED_STATE)
        self.assertNotIn("Domain", "".join(cookies))
        value = m.group(1)
        rows = self.rows()
        self.assertEqual(len(rows), 1)
        key, account, name, created, expires = rows[0]
        self.assertEqual((key, account, name), (sha(value), OLD, "maple.leaf"))     # (the SHA-256 of the cookie's value: never the value)
        self.assertNotEqual(key, value)
        self.assertEqual(expires - created, 30 * 86400)
        self.assertAlmostEqual(created, time.time(), delta=5)
        self.assertEqual(r.get("referrer-policy"), "no-referrer")
        self.assertEqual(r.get("cache-control"), "no-store")

    def test_the_room_is_where_a_visitor_who_came_from_it_goes_back_to(self):
        self.assertEqual(self.visitor().login("room").get("location"), "/#room")

    def test_the_exchange_is_a_form_post_with_the_secret_in_the_body_and_nowhere_else(self):
        self.visitor().login()
        (method, path, query, headers, body), = self.token_requests()
        form = dict(urllib.parse.parse_qsl(body))
        self.assertEqual(sorted(form), ["client_id", "client_secret", "code", "grant_type", "redirect_uri"])
        self.assertEqual((form["client_secret"], form["grant_type"], form["redirect_uri"]), (self.fake.client_secret, "authorization_code", CALLBACK))
        self.assertEqual(headers["content-type"], "application/x-www-form-urlencoded")
        self.assertEqual(query, "")
        self.assertNotIn(self.fake.client_secret, str(headers) + path + query)
        (m2, p2, q2, h2, b2), = self.me_requests()
        self.assertEqual((m2, q2, b2), ("GET", "", ""))
        self.assertTrue(h2["authorization"].startswith("Bearer "))
        for _method, path, query, headers, _body in self.token_requests() + self.me_requests():
            self.assertNotIn("cookie", headers, "no cookie of the visitor goes to Discord")
            self.assertEqual(headers["host"], "127.0.0.1:%d" % self.fake.port)
            self.assertTrue(headers["user-agent"].startswith("AntsHub/"))
            self.assertEqual(headers["connection"], "close")

    def test_nothing_of_the_token_the_email_or_the_other_fields_is_kept_anywhere(self):
        self.visitor().login()
        token = next(iter(self.fake.tokens))
        self.rig.call(self.rig.store.checkpoint)
        blob = b""
        for suffix in ("", "-wal", "-shm"):
            if os.path.exists(self.rig.db + suffix):
                with open(self.rig.db + suffix, "rb") as f:
                    blob += f.read()
        for canary in (token, "canary-refresh-", "canary-email", "Maple", self.fake.client_secret, "Bearer"):
            self.assertNotIn(canary.encode(), blob, canary)
        self.assertEqual(self.rows("lines"), [])
        log = self.rig.log_text.getvalue()
        for canary in (token, "canary-refresh", "canary-email", self.fake.client_secret):
            self.assertNotIn(canary, log)

    def test_the_login_makes_the_socket_a_member_who_may_write(self):
        v = self.member()
        member = self.socket(v)
        self.assertEqual((member.welcome["login"], member.welcome["you"]), (True, {"name": "maple.leaf", "mod": False}))
        reader = self.socket()
        self.assertEqual((reader.welcome["login"], reader.welcome["you"]), (True, None))
        member.say("hello from discord")
        self.assertEqual(next(m for m in reader.drain(0.5) if m["t"] == "line")["who"], "maple.leaf")


    def test_no_cookie_of_the_state_is_a_page_and_no_call_to_discord(self):
        v = self.visitor()
        r = v.start()
        back = v.at_discord(r.get("location"))
        out = v.callback(back, cookies=False)
        self.assert_page(out, 400, "Your browser did not keep the login cookie")
        self.assertEqual(out.all("set-cookie"), [CLEARED_STATE])
        self.assertEqual((self.token_requests(), self.rows()), ([], []))

    def test_a_state_that_is_not_the_cookies_is_refused_and_spends_the_state_of_the_address(self):
        a, b = self.visitor(), self.visitor()
        ra, rb = a.start(), b.start()
        back_b = b.at_discord(rb.get("location"))
        out = a.callback(back_b)                                                 # (a's cookie, b's state and code: what an attacker who has a login of his own could make a victim's browser do)
        self.assert_page(out, 400, "That login has run out")
        self.assertEqual(self.token_requests(), [])
        self.assert_no_login(a)
        self.assert_page(b.callback(back_b), 400, "That login has run out")        # (b's state was spent by that call: one callback for a state, whatever it ends in)
        self.assertEqual(self.token_requests(), [])

    def test_a_missing_unknown_odd_or_doubled_state_is_refused(self):
        v = self.visitor()
        r = v.start()
        good = v.at_discord(r.get("location"))
        parts = urllib.parse.urlsplit(good)
        q = dict(urllib.parse.parse_qsl(parts.query))
        other = "0" * 32
        for label, query in (("no state", {"code": q["code"]}), ("unknown", {"code": q["code"], "state": other}), ("upper case", {"code": q["code"], "state": q["state"].upper()}),
                             ("short", {"code": q["code"], "state": q["state"][:30]}), ("long", {"code": q["code"], "state": q["state"] + "0"}), ("empty", {"code": q["code"], "state": ""}),
                             ("not hex", {"code": q["code"], "state": "g" * 32})):
            w = self.visitor()
            w.jar.cookies["__Host-ants_oauth"] = query.get("state") or other
            out = w.callback(hc_url(parts, query))
            self.assert_page(out, 400)
            self.assertEqual(self.token_requests(), [], label)
        for label, make in (("state twice", lambda c, s: "code=%s&state=%s&state=%s" % (c, s, s)), ("code twice", lambda c, s: "code=%s&code=%s&state=%s" % (c, c, s)),
                            ("junk", lambda c, s: "code=%s&state=%s&%s" % (c, s, "x" * 2000)), ("long", lambda c, s: "code=%s&state=%s&x=%s" % (c, s, "y" * 1500)),
                            ("many fields", lambda c, s: "code=%s&state=%s&%s" % (c, s, "&".join("a%d=1" % i for i in range(40))))):
            w = self.visitor()                                                       # (a state is good for one callback: each odd query has a state of its own)
            w.at_discord(w.start().get("location"))
            self.assertEqual(w.callback(parts.path + "?" + make(w.code, w.state)).status, 400, label)
            self.assert_no_login(w)
        self.assertEqual(self.token_requests(), [])
        self.assert_no_login(v)

    def test_a_code_of_a_shape_that_discord_never_sends_is_refused_before_discord_is_asked(self):
        for label, code in (("empty", ""), ("a blank", "ab%20cd"), ("a line end", "ab%0Acd"), ("too long", "a" * 513), ("not ascii", "caf%C3%A9"), ("a control character", "ab%01cd")):
            w = self.visitor()
            w.start()
            out = w.callback(CALLBACK + "?code=%s&state=%s" % (code, w.state))
            self.assert_page(out, 400, "That login has run out")
            self.assertEqual((self.token_requests(), self.rows()), ([], []), label)
        w = self.visitor()
        w.start()
        self.assertEqual(w.callback(CALLBACK + "?code=%s&state=%s" % ("a" * 512, w.state)).status, 400)                  # (the longest that is allowed: Discord's page for it)
        self.assertEqual(len(self.token_requests()), 1)

    def test_a_cookie_of_the_state_that_came_twice_is_nobodys(self):
        v = self.visitor()
        r = v.start()
        back = v.at_discord(r.get("location"))
        out = v.callback(back, cookies=False, headers=[("Cookie", "__Host-ants_oauth=%s; __Host-ants_oauth=%s" % ("1" * 32, v.state))])
        self.assert_page(out, 400, "Your browser did not keep the login cookie")
        self.assertEqual(self.token_requests(), [])

    def test_a_state_that_ran_out_is_refused_before_discord_is_asked(self):
        v = self.visitor()
        r = v.start()
        back = v.at_discord(r.get("location"))
        self.tick(601)
        self.assert_page(v.callback(back), 400, "That login has run out")
        self.assertEqual((self.token_requests(), self.rows()), ([], []))

    def test_a_state_at_the_edge_of_its_ten_minutes(self):
        v = self.visitor()
        back = v.at_discord(v.start().get("location"))
        self.tick(599)
        self.assertEqual(v.callback(back).status, 302)

    def test_a_replayed_callback_is_refused_and_discord_is_asked_once(self):
        v = self.visitor()
        r = v.start()
        back = v.at_discord(r.get("location"))
        cookie = v.jar.header()
        self.assertEqual(v.callback(back).status, 302)
        again = v.callback(back, cookies=False, headers=[("Cookie", cookie)])        # (an attacker who has the whole address and the cookie)
        self.assert_page(again, 400, "That login has run out")
        self.assertEqual(len(self.token_requests()), 1)
        self.assertEqual(len(self.rows()), 1)

    def test_a_code_that_discord_has_seen_is_refused_by_discord_and_makes_no_login(self):
        v = self.visitor()
        back = v.at_discord(v.start().get("location"))
        self.assertEqual(v.callback(back).status, 302)
        old_code = v.code
        w = self.visitor()
        r = w.start()
        w.at_discord(r.get("location"))
        out = w.callback(CALLBACK + "?code=%s&state=%s" % (old_code, w.state))
        self.assert_page(out, 400, "That login did not work")
        self.assertEqual(len(self.token_requests()), 2)
        self.assertEqual(len(self.rows()), 1)
        self.assertIsNone(w.session())

    def test_a_visitor_who_said_no_gets_a_page_and_nothing_is_kept(self):
        v = self.visitor()
        r = v.start()
        out = v.callback(CALLBACK + "?error=access_denied&error_description=Access+was+denied&state=" + v.state)
        self.assert_page(out, 400, "Login cancelled")
        self.assertNotIn("denied", out.text())                                  # (what Discord says is never shown)
        self.assertEqual((self.token_requests(), self.rows()), ([], []))
        self.assert_page(v.callback(CALLBACK + "?code=x&state=" + v.state, cookies=False, headers=[("Cookie", "__Host-ants_oauth=" + v.state)]), 400, "That login has run out")

    def test_a_post_or_a_body_is_not_a_callback(self):
        v = self.visitor()
        v.start()
        r = v.call("POST", "/hub/callback?code=x&state=" + v.state, site="cross-site")
        self.assertEqual(r.status, 405)
        self.assertEqual(self.token_requests(), [])

    def test_two_callbacks_at_once_make_one_login_and_one_call_to_discord(self):
        v = self.visitor()
        back = v.at_discord(v.start().get("location"))
        cookie = v.jar.header()
        results = []

        def go():
            results.append(hc.request(self.rig.port, "GET", urllib.parse.urlsplit(back).path + "?" + urllib.parse.urlsplit(back).query,
                                      [("Cookie", cookie), ("Sec-Fetch-Site", "cross-site"), ("X-Client-Address", "203.0.113.%d" % (len(results) + 1))]))

        threads = [threading.Thread(target=go) for _ in range(8)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.assertEqual(sorted(r.status for r in results), [302] + [400] * 7)
        self.assertEqual(len(self.token_requests()), 1)
        self.assertEqual(len(self.rows()), 1)


def hc_url(parts, query):
    return parts.path + "?" + urllib.parse.urlencode(query)


# ===================================================================================================================================================
class TheCookie(LoginTest):
    def test_every_login_has_a_value_of_its_own_and_a_new_login_replaces_the_one_that_the_browser_had(self):
        v = self.visitor()
        v.login()
        first = v.session()
        self.assertRegex(first, r"^[0-9a-f]{32}$")
        socket = self.socket(v)
        self.assertEqual(socket.welcome["you"]["name"], "maple.leaf")
        v.login()                                                               # (the same browser, logging in again: it holds the first cookie while it does)
        second = v.session()
        self.assertNotEqual(first, second)
        self.assertEqual([r[0] for r in self.rows()], [sha(second)])             # (the old one is over: it is deleted, not left to run out)
        self.assertEqual(socket.expect_close(3), 1000)                           # (and the socket that it let in)
        self.assertNotIn(first, self.visitor().login().text())

    def test_a_value_is_not_made_of_a_small_number_of_bits(self):
        values = []
        for _ in range(12):
            v = self.visitor()
            v.login()
            values.append(v.session())
        self.assertEqual(len(set(values)), 12)
        for value in values:
            for period in (1, 2, 4, 8, 16):
                self.assertNotEqual(value, value[:period] * (32 // period), value)

    def test_a_value_that_is_not_the_shape_of_ours_is_not_even_looked_up(self):
        good = self.member().session()
        seen = []
        real = self.rig.store.session
        self.rig.call(setattr, self.rig.store, "session", lambda key: seen.append(key) or real(key))
        who_of = self.rig.hub.sessions.who_of
        for cookie in ("__Host-ants_hub=" + good.upper(), "__Host-ants_hub=" + good[:31], "__Host-ants_hub=caf\u00e9", "__Host-ants_hub=" + "g" * 32, "__Host-ants_hub=", "__Host-ants_hub=" + good + "0"):
            self.assertIsNone(self.rig.call(who_of, {"cookie": cookie}), cookie)
        self.assertEqual(seen, [])
        self.assertEqual(self.rig.call(who_of, {"cookie": "__Host-ants_hub=" + good}).name, "maple.leaf")
        self.assertEqual(seen, [sha(good)])

    def test_a_value_that_a_visitor_picked_is_never_adopted(self):
        v = self.visitor()
        v.jar.cookies["__Host-ants_hub"] = "ab" * 16                             # (session fixation: the visitor has a value of its own, and the attacker knows it)
        r = v.login()
        self.assertEqual(r.status, 302)
        self.assertNotEqual(v.session(), "ab" * 16)
        self.assertNotIn(sha("ab" * 16), [row[0] for row in self.rows()])
        c = self.socket(headers={"Cookie": "__Host-ants_hub=" + "ab" * 16})
        self.assertIsNone(c.welcome["you"])

    def test_a_login_that_was_made_for_the_state_does_not_log_in_before_the_callback(self):
        v = self.visitor()
        v.start()
        self.assertIsNone(v.session())
        self.assertIsNone(self.socket(v).welcome["you"])

    def test_a_cookie_that_is_not_ours_is_nobody(self):
        v = self.member()
        good = v.session()
        for label, cookie in (("another name", "ants_hub=" + good), ("upper case", "__Host-ants_hub=" + good.upper()), ("short", "__Host-ants_hub=" + good[:31]), ("long", "__Host-ants_hub=" + good + "0"),
                              ("empty", "__Host-ants_hub="), ("twice", "__Host-ants_hub=%s; __Host-ants_hub=%s" % (good, good)), ("twice, one wrong", "__Host-ants_hub=%s; __Host-ants_hub=%s" % ("0" * 32, good)),
                              ("quoted", '__Host-ants_hub="%s"' % good), ("with a blank", "__Host-ants_hub= %s x" % good), ("unknown", "__Host-ants_hub=" + "0" * 32)):
            self.assertIsNone(self.socket(headers={"Cookie": cookie}).welcome["you"], label)
        self.assertEqual(self.socket(headers={"Cookie": "a=b;  __Host-ants_hub=%s ; c=d" % good}).welcome["you"]["name"], "maple.leaf")      # (other cookies, blanks around)


class TheCapOfLoginsForAnAccount(LoginTest):
    limits = dict(sessions_per_account=3)

    def test_an_account_keeps_a_few_logins_and_the_oldest_go(self):
        values = []
        for _ in range(5):
            v = self.visitor()
            v.login()
            values.append(v.session())
        self.assertEqual(sorted(r[0] for r in self.rows()), sorted(sha(x) for x in values[2:]))
        self.fake.user = ("555555555555555555", "someone.else")
        self.visitor().login()
        self.assertEqual(len(self.rows()), 4)                                    # (the cap is for each account)
        self.assertEqual(self.socket(headers={"Cookie": "__Host-ants_hub=" + values[0]}).welcome["you"], None)
        self.assertEqual(self.socket(headers={"Cookie": "__Host-ants_hub=" + values[4]}).welcome["you"]["name"], "maple.leaf")


class TheCapOfLoginsInAll(LoginTest):
    limits = dict(sessions_total=3)

    def test_a_crowd_of_accounts_cannot_make_the_table_grow_and_the_oldest_go(self):
        keys = []
        for n in range(6):
            self.fake.user = (str(333333333333333300 + n), "crowd.%d" % n)
            v = self.visitor()
            v.login()
            keys.append(sha(v.session()))
        self.assertEqual(sorted(r[0] for r in self.rows()), sorted(keys[3:]))


class TheLimitsAreWhatTheDocsSay(unittest.TestCase):
    def test_the_defaults_of_the_login_are_the_numbers_of_the_row_of_the_table(self):
        with open(os.path.join(REPO, "docs", "HUB.md"), encoding="utf-8") as f:
            row = next(line for line in f if line.startswith("| the login's pages |"))
        d = limitmod.Limits()
        self.assertEqual((d.sessions_per_account, d.sessions_total, d.discord_total_seconds, d.session_seconds), (10, 100000, 10.0, 30 * 86400.0))
        self.assertIn("10 seconds for Discord's two calls in all", row)
        self.assertIn("10 logins kept for an account and 100,000 in all (the oldest go)", row)


class TheSessionsAreKept(LoginTest):
    def test_an_expired_login_is_nobody_and_the_hub_cleans_up_after_it(self):
        v = self.member()
        self.assertEqual(self.socket(v).welcome["you"]["name"], "maple.leaf")
        self.tick(30 * 86400 + 5)
        self.assertIsNone(self.socket(v).welcome["you"])                         # (not looked up: it ran out)
        self.assertEqual(len(self.rows()), 1)                                    # (still in the table until the hub's hour comes)
        self.rig.call(self.rig.store.purge, 2000)
        self.assertEqual(self.rows(), [])

    def test_the_last_second(self):
        v = self.member()
        self.tick(30 * 86400 - 5)
        self.assertEqual(self.socket(v).welcome["you"]["name"], "maple.leaf")

    def test_a_socket_does_not_outlive_its_login(self):
        v = self.member()
        c = self.socket(v)
        self.tick(30 * 86400 + 5)
        self.assertEqual(c.expect_close(3), 1000)

    def test_purge_only_takes_what_ran_out(self):
        a = self.member()
        self.tick(20 * 86400)
        self.fake.user = ("333333333333333333", "sam")
        b = self.visitor()
        b.login()
        self.tick(11 * 86400)                                                    # (a: 31 days, b: 11)
        self.rig.call(self.rig.store.purge, 2000)
        self.assertEqual([r[2] for r in self.rows()], ["sam"])


class TheSocket(LoginTest):
    def test_origin_and_fetch_site_of_the_handshake(self):
        v = self.member()
        good = v.jar.header()
        for site, status in (("same-origin", "101"), (None, "101"), ("cross-site", "403"), ("same-site", "403"), ("none", "403"), ("", "403"), ("same-origin, cross-site", "403")):
            h = {"Cookie": good}
            if site is not None:
                h["Sec-Fetch-Site"] = site
            c = hc.Client(self.rig.port, headers=h)
            self.clients.append(c)
            self.assertEqual(c.status.split()[1], status, repr(site))
        for origin in (None, "null", "https://evil.example", "https://chat.example.org.evil.example", "http://chat.example.org", "https://sub.chat.example.org"):
            c = hc.Client(self.rig.port, origin=origin, headers={"Cookie": good, "Sec-Fetch-Site": "same-origin"})
            self.clients.append(c)
            self.assertEqual(c.status.split()[1], "403", repr(origin))

    def test_the_test_login_header_is_not_believed(self):
        v = self.member()
        c = self.socket(headers={"X-Hub-Test-Login": "1001:mallory"})
        self.assertEqual((c.welcome["login"], c.welcome["you"]), (True, None))
        c.say("hello")
        self.assertEqual(c.recv_json(), {"t": "err", "code": "login"})
        c2 = self.socket(v, headers={"X-Hub-Test-Login": "1001:mallory"})
        self.assertEqual(c2.welcome["you"]["name"], "maple.leaf")                # (the cookie decides)

    def test_two_cookie_headers_are_refused_by_the_head_reader(self):
        v = self.member()
        request, _ = hc.handshake_request(hc.SITE, {"Cookie": "a=b"})
        import socket as sock_module
        with sock_module.create_connection(("127.0.0.1", self.rig.port), timeout=3) as s:
            s.sendall(request.replace(b"\r\n\r\n", b"\r\nCookie: " + v.jar.header().encode() + b"\r\n\r\n"))
            self.assertEqual(hc.read_head(s)[0].split()[1], "400")

    def test_the_identity_is_fixed_for_the_life_of_the_socket(self):
        v = self.member()
        c = self.socket(v)
        other = self.socket(v)
        c.say("one")
        self.assertEqual(next(m for m in other.drain(0.5) if m["t"] == "line")["who"], "maple.leaf")
        v.jar.cookies.clear()                                                    # (the browser lost the cookie: this socket is what it was)
        c.say("two, and a different text")
        self.assertTrue(any(m["t"] == "line" for m in other.drain(3.0)))

    def test_young_accounts_read_and_cannot_write(self):
        for age, young in ((2, True), (6.9, True), (7.1, False), (900, False)):
            self.fake.user = (fd.snowflake(age), "n%d" % int(age * 10))
            v = self.visitor()
            self.assertEqual(v.login().status, 302)
            c = self.socket(v)
            self.assertEqual(c.welcome["you"]["name"], self.fake.user[1])
            reader = self.socket()
            reader.drain(0.2)
            c.say("hello from age %s" % age)
            if young:
                self.assertEqual(c.recv_json(), {"t": "err", "code": "young"}, age)
                self.assertEqual([m for m in reader.drain(0.4) if m["t"] == "line"], [])
            else:
                self.assertEqual(next(m for m in reader.drain(0.5) if m["t"] == "line")["text"], "hello from age %s" % age)
            reader.close()
            c.close()
        self.assertEqual(self.rows("lines").__len__(), 2)

    def test_an_id_from_the_future_is_young(self):
        self.fake.user = (FUTURE, "from.tomorrow")
        v = self.visitor()
        v.login()
        c = self.socket(v)
        c.say("hello")
        self.assertEqual(c.recv_json(), {"t": "err", "code": "young"})


class TheModeratorsOfTheSettings(LoginTest):
    mods = "5550001,%s" % OLD

    def test_a_moderator_is_marked_in_the_welcome_and_his_lines(self):
        v = self.member()
        c = self.socket(v)
        self.assertEqual(c.welcome["you"], {"name": "maple.leaf", "mod": True})
        c.say("a word from a moderator")
        self.assertEqual(next(m for m in c.drain(0.5) if m["t"] == "line")["mod"], True)


class TheMeOfAModerator(LoginTest):
    mods = OLD

    def test_me_says_mod(self):
        v = self.member()
        self.assertEqual(v.call("GET", "/hub/me").json()["you"], {"name": "maple.leaf", "mod": True})


class TheAgeSetting(LoginTest):
    env = {"ANTS_HUB_MIN_ACCOUNT_DAYS": "30"}

    def test_the_number_of_days_is_a_setting(self):
        self.fake.user = (fd.snowflake(10), "ten.days")
        v = self.visitor()
        v.login()
        c = self.socket(v)
        c.say("hello")
        self.assertEqual(c.recv_json(), {"t": "err", "code": "young"})


class TheAgeSettingZero(LoginTest):
    env = {"ANTS_HUB_MIN_ACCOUNT_DAYS": "0"}

    def test_zero_days_is_no_limit(self):
        self.fake.user = (fd.snowflake(0), "brand.new")
        v = self.visitor()
        v.login()
        c = self.socket(v)
        c.say("hello")
        self.assertEqual(next(m for m in c.drain(0.5) if m["t"] == "line")["text"], "hello")

    def test_zero_days_does_not_mind_an_id_that_is_from_the_future_either(self):
        self.fake.user = (FUTURE, "from.next.month")
        v = self.visitor()
        v.login()
        c = self.socket(v)
        c.say("hello")
        self.assertEqual(next(m for m in c.drain(0.5) if m["t"] == "line")["text"], "hello")


class TheBansAndMutesStandWhateverTheAccountDoes(LoginTest):
    def test_a_banned_account_can_read_and_log_in_and_cannot_write_and_nothing_it_does_lifts_it(self):
        self.rig.call(self.rig.store.ban, OLD, "test", "maple.leaf")
        v = self.member()
        reader = self.socket()
        c = self.socket(v)
        self.assertEqual(c.welcome["you"], {"name": "maple.leaf", "mod": False})    # (it can log in: it is a reader with a name)
        self.assertTrue(c.welcome["history"] == [] and c.welcome["login"])
        c.say("let me in")
        self.assertEqual(c.recv_json(), {"t": "err", "code": "banned"})
        self.assertEqual([m for m in reader.drain(0.4) if m["t"] == "line"], [])
        r = v.call("POST", "/hub/logout")                                        # logging out and in again, in another browser too
        self.assertEqual(r.status, 200)
        w = self.visitor()
        w.login()
        c2 = self.socket(w)
        c2.say("again")
        self.assertEqual(c2.recv_json(), {"t": "err", "code": "banned"})
        r = w.call("POST", "/hub/forget")                                        # forgetting is not an exit either
        self.assertEqual(r.status, 200)
        self.assertEqual(len(self.rows("bans")), 1)
        self.assertEqual(self.rows("bans")[0][0], OLD)
        self.assertEqual(self.rows("bans")[0][3], "")                            # (the name is wiped, the id stays)
        x = self.visitor()
        x.login()
        c3 = self.socket(x)
        c3.say("a third time")
        self.assertEqual(c3.recv_json(), {"t": "err", "code": "banned"})
        self.assertEqual(self.rows("lines"), [])

    def test_a_mute_stands_too(self):
        v = self.member()
        self.rig.call(self.rig.store.mute, OLD, 60, "test", "maple.leaf")
        for _ in range(2):
            c = self.socket(v)
            c.say("hello")
            self.assertEqual(c.recv_json(), {"t": "err", "code": "muted"})
            self.assertEqual(v.call("POST", "/hub/forget").status, 200)
            v.login()
        self.assertEqual(self.rows("mutes")[0][4], "")


class TheMe(LoginTest):
    def test_it_says_who_the_cookie_is_and_to_nobody_else(self):
        v = self.member()
        r = v.call("GET", "/hub/me")
        self.assertEqual((r.status, r.json()), (200, {"login": True, "you": {"name": "maple.leaf", "mod": False}}))
        self.assertEqual(r.get("cache-control"), "no-store")
        self.assertTrue(r.get("content-type").startswith("application/json"))
        self.assertEqual(self.visitor().call("GET", "/hub/me").json(), {"login": True, "you": None})

    def test_only_the_front_page_may_ask(self):
        v = self.member()
        for site in ("cross-site", "same-site", "none", None, ""):
            r = v.call("GET", "/hub/me", site=site)
            self.assertEqual(r.status, 403, site)
            self.assertNotIn("maple", r.text())
        self.assertEqual(v.call("GET", "/hub/me", origin="https://evil.example").status, 403)
        self.assertEqual(v.call("GET", "/hub/me", origin=hc.SITE).status, 200)
        self.assertEqual(v.call("POST", "/hub/me").status, 405)
        self.assertEqual(v.call("GET", "/hub/me?x=1").status, 404)


class TheDatabaseThatCannotBeUsed(LoginTest):
    """A write lock held by another process (or a full disk) for longer than the hub waits: each page says so and the hub goes on."""

    def failing(self, name):
        def fail(*a, **k):
            raise sqlite3.OperationalError("database is locked")
        self.rig.call(setattr, self.rig.store, name, fail)

    def test_the_callback_that_cannot_keep_the_login_is_the_calm_page_and_no_login(self):
        v = self.visitor()
        back = v.at_discord(v.start().get("location"))
        self.failing("add_session")
        out = v.callback(back)
        self.assert_page(out, 503, UNAVAILABLE)
        self.assertIsNone(v.session())
        self.assertRegex(self.rig.log_text.getvalue(), r" login_failed kind=database error=OperationalError")

    def test_me_logout_and_forget_say_busy_and_do_nothing(self):
        v = self.member()
        for name, method, path in (("session", "GET", "/hub/me"), ("delete_session", "POST", "/hub/logout"), ("forget", "POST", "/hub/forget")):
            self.failing(name)
            r = v.call(method, path)
            self.assertEqual((r.status, r.json()), (503, {"error": "busy"}), path)
        self.assertEqual(hc.http_get(self.rig.port, "/hub/healthz")[0], 200)


class TheLogout(LoginTest):
    def test_it_ends_this_login_and_the_sockets_of_it_and_clears_the_cookie(self):
        v, w = self.member(), self.visitor()
        w.login()                                                                # (a second browser of the same account)
        c, other = self.socket(v), self.socket(w)
        r = v.call("POST", "/hub/logout")
        self.assertEqual((r.status, r.json()), (200, {"ok": True}))
        self.assertEqual(r.all("set-cookie"), [CLEARED_SESSION])
        self.assertEqual(r.get("cache-control"), "no-store")
        self.assertEqual(c.expect_close(3), 1000)
        self.assertEqual(len(self.rows()), 1)                                    # (only this browser's login)
        self.assertEqual(self.socket(headers={"Cookie": "__Host-ants_hub=" + w.session()}).welcome["you"]["name"], "maple.leaf")
        other.say("still here")
        self.assertTrue(any(m["t"] == "line" for m in other.drain(0.5)))

    def test_the_old_cookie_is_nobody_after_it(self):
        v = self.member()
        value = v.session()
        v.call("POST", "/hub/logout")
        self.assertIsNone(self.socket(headers={"Cookie": "__Host-ants_hub=" + value}).welcome["you"])
        self.assertEqual(self.rows(), [])

    def test_without_a_login_it_is_fine_and_clears_nothing_it_does_not_have(self):
        r = self.visitor().call("POST", "/hub/logout")
        self.assertEqual((r.status, r.json()), (200, {"ok": True}))

    def test_a_request_of_another_site_is_refused_and_the_login_stands(self):
        v = self.member()
        value = v.session()
        for label, site, origin in (("cross-site", "cross-site", "https://evil.example"), ("no origin", "same-origin", None), ("no fetch site", None, hc.SITE), ("neither", None, None),
                                    ("same-site sibling", "same-site", "https://evil.chat.example.org"), ("null origin", "cross-site", "null"), ("right origin, cross-site", "cross-site", hc.SITE),
                                    ("wrong origin, same-origin", "same-origin", "https://evil.example"), ("origin with a path", "same-origin", hc.SITE + "/"), ("none", "none", hc.SITE),
                                    ("two values", "same-origin, same-origin", hc.SITE)):
            r = v.call("POST", "/hub/logout", site=site, origin=origin)
            self.assertEqual(r.status, 403, label)
            self.assertEqual(r.all("set-cookie"), [], label)
            self.assertEqual([row[0] for row in self.rows()], [sha(value)], label)
        self.assertEqual(self.socket(v).welcome["you"]["name"], "maple.leaf")

    def test_get_is_not_a_logout(self):
        v = self.member()
        for path in ("/hub/logout", "/hub/forget"):
            r = v.call("GET", path)
            self.assertEqual(r.status, 405)
        self.assertEqual(len(self.rows()), 1)

    def test_a_body_a_query_and_the_wrong_path(self):
        v = self.member()
        self.assertEqual(v.call("POST", "/hub/logout", headers=[("Content-Length", "2")]).status, 400)
        self.assertEqual(v.call("POST", "/hub/logout?x=1").status, 404)
        self.assertEqual(v.call("POST", "/hub/logout/").status, 404)
        self.assertEqual(len(self.rows()), 1)


class TheForget(LoginTest):
    def test_it_deletes_every_login_and_every_line_of_the_account_and_none_of_anybody_elses(self):
        v, w = self.member(), self.visitor()
        w.login()
        self.fake.user = ("444444444444444444", "sam.the.other")
        s = self.visitor()
        s.login()
        mine, mine2, theirs, reader = self.socket(v), self.socket(w), self.socket(s), self.socket()
        for text in ("my first", "my second"):
            mine.say(text)
            time.sleep(2.1)
        theirs.say("sam's line")
        reader.drain(0.5)
        history_before = self.socket().welcome["history"]
        self.assertEqual([h["text"] for h in history_before], ["my first", "my second", "sam's line"])
        r = v.call("POST", "/hub/forget")
        self.assertEqual((r.status, r.json()), (200, {"ok": True}))
        self.assertEqual(r.all("set-cookie"), [CLEARED_SESSION])
        self.assertEqual([row[1] for row in self.rows()], ["444444444444444444"])    # (both of its logins went, and sam's did not)
        self.assertEqual([row[3] for row in self.rows("lines")], ["sam.the.other"])
        deleted = [m for m in reader.drain(0.6) if m["t"] == "del"]
        self.assertEqual(sorted(m["id"] for m in deleted), [h["id"] for h in history_before[:2]])      # (the screens drop them at once)
        self.assertEqual(mine.expect_close(3), 1000)
        self.assertEqual(mine2.expect_close(3), 1000)
        self.assertEqual([h["text"] for h in self.socket().welcome["history"]], ["sam's line"])
        theirs.say("sam goes on")
        self.assertTrue(any(m["t"] == "line" for m in reader.drain(0.5)))
        self.assertIsNone(self.socket(w).welcome["you"])
        self.assertEqual(v.call("POST", "/hub/forget").status, 401)              # (nobody left to forget)

    def test_a_reader_cannot_forget_anybody(self):
        v = self.member()
        r = self.visitor().call("POST", "/hub/forget")
        self.assertEqual((r.status, r.json()), (401, {"error": "login"}))
        self.assertEqual(len(self.rows()), 1)

    def test_the_same_rules_of_origin_as_the_logout(self):
        v = self.member()
        for site, origin in (("cross-site", "https://evil.example"), ("same-origin", None), (None, hc.SITE), ("cross-site", hc.SITE), ("same-site", hc.SITE)):
            self.assertEqual(v.call("POST", "/hub/forget", site=site, origin=origin).status, 403, (site, origin))
        self.assertEqual(len(self.rows()), 1)

    def test_it_never_asks_discord(self):
        v = self.member()
        before = len(self.fake.requests)
        v.call("POST", "/hub/forget")
        self.assertEqual(len(self.fake.requests), before)


class TheLimitsOfTheSessionPages(LoginTest):
    limits = dict(session_burst=3, session_refill_seconds=60.0)

    def test_an_address_has_a_burst(self):
        v = self.member()
        answers = [v.call("GET", "/hub/me").status for _ in range(5)]
        self.assertEqual(answers, [200, 200, 200, 429, 429])
        self.assertEqual(v.call("POST", "/hub/logout").status, 429)
        self.assertEqual(self.visitor().call("GET", "/hub/me").status, 200)


# ===================================================================================================================================================
class TheCallbackWhenDiscordGoesWrong(LoginTest):
    limits = dict(discord_seconds=1.0, discord_connect_seconds=1.0, login_inflight=4)
    TOKEN = ("status:401", "status:403", "status:429", "status:500", "status:503", "status:404", "status:204", "status:200", "hang", "drip", "close", "partial", "huge", "huge_stream", "huge_chunked",
             "huge_head", "notjson", "cutoff", "array", "nan", "deep", "chunked_length", "redirect301", "redirect302", "redirect307", "redirect308")

    def go(self, v=None):
        v = v or self.visitor()
        started = time.monotonic()
        r = v.login()
        return v, r, time.monotonic() - started

    def assert_calm(self, v, r, label, took=0.0):
        self.assert_page(r, 503, UNAVAILABLE)
        self.assertIn("The chat can still be read", r.text())
        self.assertEqual(r.all("set-cookie"), [CLEARED_STATE], label)
        self.assertEqual(self.rows(), [], label)
        self.assertIsNone(v.session(), label)
        self.assertLess(took, 3.0, label)
        self.assertNotIn("canary-error-body", r.text() + str(r.headers))

    def test_every_way_for_the_exchange_to_fail_is_a_calm_page_and_no_login(self):
        for mode in self.TOKEN:
            self.fake.token_mode = mode
            before = len(self.token_requests())
            v, r, took = self.go()
            self.assert_calm(v, r, mode, took)
            self.assertEqual(len(self.token_requests()) - before, 1, mode)           # (never a second try)
            self.assertEqual(self.me_requests(), [], mode)
            self.assertEqual([r for r in self.fake.requests if r[1] == "/followed"], [], mode)      # (a redirect is not followed)

    def test_every_way_for_the_question_to_fail_is_a_calm_page_and_no_login(self):
        for mode in self.TOKEN:
            self.fake.me_mode = mode
            v, r, took = self.go()
            self.assert_calm(v, r, "me " + mode, took)
        self.assertEqual(len(self.me_requests()), len(self.TOKEN))

    def test_json_that_is_not_an_object_is_a_call_that_failed_and_not_a_bug_of_the_hub(self):
        for mode in ("array", "notjson", "cutoff", "nan", "deep"):
            self.fake.token_mode = mode
            self.go()
        log = self.rig.log_text.getvalue()
        self.assertNotIn("kind=bug", log)
        self.assertEqual(log.count(" login_failed kind=down"), 5)

    def test_a_code_that_discord_refuses_asks_the_visitor_to_try_again(self):
        self.fake.token_mode = "status:400"
        v, r, _ = self.go()
        self.assert_page(r, 400, "That login did not work")
        self.assertEqual(self.rows(), [])

    def test_a_refused_application_is_a_calm_page_and_a_line_for_the_operator(self):
        self.fake.client_secret = "a different secret that the hub does not have"
        v, r, _ = self.go()
        self.assert_page(r, 503, UNAVAILABLE)
        log = self.rig.log_text.getvalue()
        self.assertRegex(log, r" login_failed kind=config status=401 ")

    def test_two_slow_calls_are_more_than_the_hub_waits_for_the_login(self):
        self.rig.__exit__(None, None, None)
        self.rig = hub_rig.Rig(discord=self.fake, discord_seconds=2.0, discord_connect_seconds=1.0, discord_total_seconds=1.4, sockets_per_address=100)
        self.rig.__enter__()
        self.fake.token_mode = self.fake.me_mode = "slow:0.8"                               # (each call is in time, the two are not)
        v, r, took = self.go()
        self.assert_calm(v, r, "two slow calls", took)
        self.assertLess(took, 2.4)
        self.fake.token_mode = self.fake.me_mode = "slow:0.3"
        self.assertEqual(self.visitor().login().status, 302)

    def test_an_answer_of_the_wrong_shape_is_no_login(self):
        token = {"access_token": 5}, {"access_token": ""}, {"access_token": "abc\r\nX-Evil: 1"}, {"access_token": "a b"}, {"access_token": "x" * 600}, {"access_token": None}, {"token_type": "mac"}, \
            {"token_type": 5}, {"scope": "guilds"}, {"scope": ["identify"]}, {"scope": "identifyx"}
        for merge in token:
            self.fake.token_mode = "merge:" + json.dumps(merge)
            v, r, took = self.go()
            self.assert_calm(v, r, str(merge), took)
        self.assertEqual(self.me_requests(), [])
        self.fake.token_mode = "ok"
        ids = ({"id": 5}, {"id": "0"}, {"id": "012"}, {"id": "abc"}, {"id": "-1"}, {"id": "1" * 21}, {"id": ""}, {"id": None}, {"id": ["1"]}, {"id": " 123"}, {"id": "123\n"}, {"id": "1e5"})
        for merge in ids:
            self.fake.me_mode = "merge:" + json.dumps(merge)
            v, r, took = self.go()
            self.assert_calm(v, r, str(merge), took)

    def test_a_name_that_is_not_a_name_is_refused_and_never_shown(self):
        names = ("Maple", "m", "x" * 33, "with space", "evil\r\nSet-Cookie: a=b", "<script>alert(1)</script>", "m\u00e4ple", "", "\u202eevil", "a\u200bb", "line\nbreak", "tab\tx", "semi;colon", "quote\"",
                 "back\\slash", "%0d%0a", "al\u0131ce", "\ud835\udc00bc", "mod", "admin", "moderator", "system", "hub", "ants", "everyone", "here")           # (the last eight could pass for the hub's own voice)
        for name in names:
            self.fake.me_mode = "merge:" + json.dumps({"username": name})
            v, r, _ = self.go()
            self.assert_page(r, 400, "This Discord name cannot be used here")
            self.assertNotIn("script", r.text().lower().replace("</style>", ""))
            self.assertNotIn("Set-Cookie: a=b", r.text() + str(r.headers))
            self.assertEqual(r.all("set-cookie"), [CLEARED_STATE])
        for bad in (5, None, ["a"], {"a": 1}):
            self.fake.me_mode = "merge:" + json.dumps({"username": bad})
            v, r, _ = self.go()
            self.assert_calm(v, r, str(bad))
        self.assertEqual(self.rows(), [])

    def test_names_that_are_names_pass(self):
        for name in ("ab", "x" * 32, "a.b_c9", "9", "_.", "maple.leaf", "0123456789"):
            if len(name) < 2:
                continue
            self.fake.me_mode = "merge:" + json.dumps({"username": name})
            v = self.visitor()
            self.assertEqual(v.login().status, 302, name)
        self.assertEqual(len(self.rows()), 6)

    def test_a_chunked_answer_and_one_that_ends_with_the_connection_are_fine(self):
        for mode in ("chunked", "nolength"):
            self.fake.token_mode = self.fake.me_mode = mode
            self.assertEqual(self.visitor().login().status, 302, mode)

    def test_the_hub_serves_everybody_else_while_discord_hangs(self):
        self.fake.token_mode = "hang"
        v = self.visitor()
        back = v.at_discord(v.start().get("location"))
        result = []
        t = threading.Thread(target=lambda: result.append(v.callback(back)))
        t.start()
        time.sleep(0.2)
        started = time.monotonic()
        c = self.socket()
        self.assertEqual(c.welcome["t"], "welcome")
        self.assertEqual(hc.http_get(self.rig.port, "/hub/healthz")[0], 200)
        self.assertEqual(self.visitor().call("GET", "/hub/me").status, 200)
        self.assertLess(time.monotonic() - started, 0.5)
        t.join(5)
        self.assert_page(result[0], 503, UNAVAILABLE)

    def test_a_login_that_waits_for_discord_does_not_hold_a_place_of_the_handshakes(self):
        self.rig.__exit__(None, None, None)
        self.rig = hub_rig.Rig(discord=self.fake, pending_total=2, discord_seconds=1.5, discord_connect_seconds=1.0, login_inflight=3, sockets_per_address=100)
        self.rig.__enter__()
        self.fake.token_mode = "hang"
        visitors = []
        for _ in range(3):
            v = self.visitor()
            visitors.append((v, v.at_discord(v.start().get("location"))))
        def wait_for_discord(pair):
            try:
                pair[0].callback(pair[1])
            except OSError:                                                              # (a connection that the hub has to turn away while it is full of heads: no matter here)
                pass

        threads = [threading.Thread(target=wait_for_discord, args=(pair,)) for pair in visitors]
        for t in threads:
            t.start()
        time.sleep(0.4)                                                                  # (three logins wait for Discord; the hub takes only two connections that have not finished their head)
        c = self.socket()
        self.assertEqual(c.welcome["t"], "welcome")
        for t in threads:
            t.join(5)

    def test_only_a_few_logins_wait_for_discord_at_one_time_the_rest_get_the_page_at_once(self):
        self.fake.token_mode = "hang"
        visitors = []
        for _ in range(8):
            v = self.visitor()
            visitors.append((v, v.at_discord(v.start().get("location"))))
        took = []

        def go(pair):
            started = time.monotonic()
            r = pair[0].callback(pair[1])
            took.append((time.monotonic() - started, r.status))

        with concurrent.futures.ThreadPoolExecutor(8) as pool:
            list(pool.map(go, visitors))
        quick = [t for t, status in took if t < 0.5]
        self.assertEqual(len(quick), 4)                                            # (login_inflight=4: four waited their second, four were told at once)
        self.assertEqual({status for _, status in took}, {503})
        self.assertEqual(len(self.token_requests()), 4)


class TheDiscordClient(unittest.TestCase):
    """hub/discord.py on its own."""

    def setUp(self):
        self.fake = fd.FakeDiscord(CALLBACK)
        self.addCleanup(self.fake.close)

    def client(self, test=True, **limits):
        env = {"ANTS_HUB_PUBLIC_URL": hc.SITE, "ANTS_HUB_DISCORD_CLIENT_ID": self.fake.client_id, "ANTS_HUB_DISCORD_CLIENT_SECRET": self.fake.client_secret, "ANTS_HUB_HOST": "127.0.0.1"}
        if test:
            env["ANTS_HUB_TEST_DISCORD_URL"] = self.fake.url
        return discordmod.Discord(configmod.load(env), limitmod.Limits(**limits))

    def run_call(self, client, code="x"):
        import asyncio
        return asyncio.run(client.who(code))

    def test_the_address_of_discord_is_https_and_the_fake_is_a_test_setting(self):
        client = self.client(test=False)
        url = client.authorize_url("s" * 32)
        self.assertTrue(url.startswith("https://discord.com/oauth2/authorize?client_id="), url)
        self.assertTrue(client.api.startswith("https://discord.com/api/v10"))
        self.assertEqual(self.client().authorize_base, self.fake.url + "/oauth2/authorize")

    def test_plain_http_is_not_spoken_without_the_test_setting(self):
        client = self.client(test=False)
        client.api = self.fake.url + "/api/v10"                        # (as if a bug or an edit pointed it at a plain address)
        with self.assertRaises(discordmod.DiscordError) as e:
            self.run_call(client)
        self.assertEqual(e.exception.kind, "down")
        self.assertEqual(self.fake.requests, [])                       # (nothing was sent)

    def test_an_answer_is_read_up_to_the_cap_and_not_a_byte_more(self):
        import asyncio
        for mode in ("huge", "huge_stream", "huge_chunked", "huge_head"):
            self.fake.token_mode = mode
            started = time.monotonic()
            with self.assertRaises(discordmod.DiscordError) as e:
                self.run_call(self.client(discord_body_max=2048, discord_seconds=2.0))
            self.assertEqual(e.exception.kind, "down", mode)
            self.assertLess(time.monotonic() - started, 1.5, mode)
            if mode != "huge_head":
                time.sleep(0.3)
                self.assertLess(self.fake.sent, 30000000, "%s: the hub kept reading (%d bytes of about 98 million)" % (mode, self.fake.sent))             # (it hung up at its cap: the socket's own buffers hold a few megabytes at the most)

    def test_the_token_is_nowhere_in_the_client_or_the_login_when_the_call_is_over(self):
        client = self.client()
        self.fake.user = (OLD, "maple.leaf")
        code = "c0de"
        with self.fake.lock:
            self.fake.codes[code] = self.fake.user
        self.assertEqual(self.run_call(client, code), (OLD, "maple.leaf"))
        token = next(iter(self.fake.tokens))
        for obj in (client, client.limits, client.ssl):
            self.assertNotIn(token, repr(getattr(obj, "__dict__", {})), type(obj).__name__)
        self.assertEqual(client.inflight, 0)

    def test_the_whole_call_has_a_time_not_only_each_read(self):
        self.fake.token_mode = "drip"
        started = time.monotonic()
        with self.assertRaises(discordmod.DiscordError):
            self.run_call(self.client(discord_seconds=1.0))
        self.assertLess(time.monotonic() - started, 2.0)

    def test_a_certificate_that_does_not_verify_is_refused(self):
        import shutil
        import ssl
        import socketserver
        if not shutil.which("openssl"):
            self.skipTest("no openssl program here")
        folder = tempfile.mkdtemp(prefix="hub-tls-")
        self.addCleanup(shutil.rmtree, folder, ignore_errors=True)
        cert, key = os.path.join(folder, "c.pem"), os.path.join(folder, "k.pem")
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", key, "-out", cert, "-days", "2", "-subj", "/CN=discord.com"], check=True, capture_output=True)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cert, key)
        served = []

        class Handler(socketserver.BaseRequestHandler):
            def handle(self):
                try:
                    served.append(self.request.recv(100))
                except OSError:
                    pass

        server = socketserver.TCPServer(("127.0.0.1", 0), Handler)
        server.socket = context.wrap_socket(server.socket, server_side=True)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        client = self.client(test=False)
        client.api = "https://127.0.0.1:%d/api/v10" % server.server_address[1]
        with self.assertRaises(discordmod.DiscordError) as e:
            self.run_call(client)
        self.assertEqual(e.exception.kind, "down")
        self.assertEqual(served, [])                                               # (it hung up before it sent the secret)


# ===================================================================================================================================================
class TheLimitTable(unittest.TestCase):
    def test_a_crowd_of_addresses_cannot_make_the_table_grow_without_end(self):
        rate = limitmod.RateByAddress(burst=2, refill_seconds=60.0, keys_max=5)
        for n in range(5):
            self.assertTrue(rate.take("a%d" % n, 0.0))
        self.assertFalse(rate.take("another", 0.0))                       # (the table is full of addresses that are not idle: a new one is refused, and the table does not grow)
        self.assertEqual(len(rate.buckets), 5)
        self.assertTrue(rate.take("a1", 0.0))                              # (an address that is in it goes on)
        self.assertTrue(rate.take("another", 200.0))                        # (the others are full again, so they are forgotten, and there is room)
        self.assertLessEqual(len(rate.buckets), 5)

    def test_an_address_has_a_burst_and_a_pace(self):
        rate = limitmod.RateByAddress(burst=3, refill_seconds=2.0)
        self.assertEqual([rate.take("x", 0.0) for _ in range(4)], [True, True, True, False])
        self.assertTrue(rate.take("x", 2.0))
        self.assertFalse(rate.take("x", 2.1))
        self.assertTrue(rate.take("y", 2.1))


class TheHeadersAreSafe(LoginTest):
    def test_nothing_that_came_from_outside_can_start_another_header(self):
        from hub import http
        for bad in ("a\r\nSet-Cookie: x=1", "a\nb", "a\rb", "caf\u00e9", "a\x00b", "a\x7fb", "\x1b[0m"):
            with self.assertRaises(ValueError):
                http.response(200, {"ok": True}, [("X-Test", bad)])
        for name in ("X Test", "X:Test", "", "X\r\nY", "X-\u00e9"):
            with self.assertRaises(ValueError):
                http.response(200, {"ok": True}, [(name, "v")])
        self.assertIn(b"X-Test: a\tb", http.response(200, {"ok": True}, [("X-Test", "a\tb")]))

    def test_a_flood_of_odd_requests_leaves_the_hub_serving(self):
        paths = ["/hub/login?" + "next=" * 400, "/hub/callback?" + "a=1&" * 100, "/hub/callback?state=%00%ff&code=%", "/hub/callback?%zz=1&state=" + "a" * 32, "/hub/me?" + "x" * 3000, "/hub/callback",
                 "/hub/login?next=%c3%28", "/hub/callback?code=" + "a" * 3000 + "&state=" + "b" * 32]
        for path in paths:
            for headers in ({}, {"Cookie": "__Host-ants_oauth=" + "a" * 32}, {"Cookie": "\u00e9=1".encode("utf-8").decode("latin-1")}, {"Cookie": "=" * 50 + ";;;" * 50}):
                try:
                    r = hc.request(self.rig.port, "GET", path, headers)
                except (ConnectionError, ValueError, IndexError):
                    continue                                                    # (the head reader may say no: that is an answer too)
                self.assertIn(r.status, (302, 400, 404, 429, 431, 503))
        self.assertEqual(hc.http_get(self.rig.port, "/hub/healthz")[0], 200)
        self.assertEqual(self.rows(), [])

    def test_the_error_of_a_page_is_in_a_page_and_never_a_stack(self):
        r = hc.request(self.rig.port, "GET", "/hub/callback?code=x&state=y")
        self.assertNotIn("Traceback", r.text())
        self.assertNotIn("File ", r.text())


# ===================================================================================================================================================
class TheLogHoldsNothingSecret(LoginTest):
    def test_the_whole_flow_leaves_no_token_code_state_cookie_secret_id_or_name_in_the_log(self):
        v = self.visitor()
        r = v.start()
        state = v.state
        back = v.at_discord(r.get("location"))
        out = v.callback(back)
        value = v.session()
        c = self.socket(v)
        c.say("a line of chat canary-41b7")
        token = next(iter(self.fake.tokens))
        v.call("GET", "/hub/me")
        v.call("POST", "/hub/logout")
        w = self.visitor()
        w.login()
        w.call("POST", "/hub/forget")
        self.fake.token_mode = "status:500"
        self.visitor().login()
        self.fake.token_mode = "ok"
        self.fake.me_mode = "merge:" + json.dumps({"username": "Evil Name"})
        self.visitor().login()
        log = self.rig.log_text.getvalue()
        self.assertIn(" login tag=", log)
        self.assertIn(" logout tag=", log)
        self.assertIn(" forget tag=", log)
        self.assertRegex(log, r" login_failed kind=down status=500 ")
        for canary in (state, self.fake.codes and next(iter(self.fake.codes)), value, token, self.fake.client_secret, self.fake.client_id, OLD, "maple.leaf", "Evil", "canary-41b7", "198.51.100", "127.0.0.1",
                       "canary-error-body", "canary-refresh", "canary-email", "Bearer", "__Host", "Set-Cookie", "Cookie"):
            self.assertNotIn(canary, log, "the log shows " + str(canary))
        for line in log.splitlines():
            self.assertRegex(line, r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ [a-z_]+( [a-z_]+=[A-Za-z0-9_.:/-]*)*$", line)


class TheBugsAreCalm(LoginTest):
    def test_a_bug_in_the_callback_is_a_calm_page_and_a_line_with_the_kind_of_error_and_not_its_text(self):
        async def broken(code):
            raise RuntimeError("canary-bug-text " + code)
        self.rig.call(setattr, self.rig.hub.sessions.discord, "who", broken)
        v = self.visitor()
        r = v.login()
        self.assert_page(r, 503, UNAVAILABLE)
        self.assertEqual(r.all("set-cookie"), [CLEARED_STATE])
        log = self.rig.log_text.getvalue()
        self.assertRegex(log, r" login_failed kind=bug error=RuntimeError$")
        self.assertNotIn("canary-bug-text", log)
        self.assertNotIn(v.code, log)
        self.assertEqual(self.rows(), [])
        self.assertEqual(self.visitor().call("GET", "/hub/me").status, 200)                  # (the hub goes on)

    def test_the_line_for_a_call_that_failed_holds_the_kind_the_status_and_the_tag_and_nothing_else(self):
        self.fake.token_mode = "status:500"
        self.visitor().login()
        lines = [line for line in self.rig.log_text.getvalue().splitlines() if " login_failed" in line]
        self.assertEqual(len(lines), 1)
        self.assertRegex(lines[0], r"^\S+ login_failed kind=down status=500 tag=[0-9a-f]+$")


class TheProcess(unittest.TestCase):
    """`python -m hub` with the Discord settings, as the image runs it, in front of the fake Discord: the secrets come from the environment and are in no log and on no command line."""

    SECRET = fd.CLIENT_SECRET

    def test_the_real_process_logs_in_a_visitor_and_its_log_and_command_line_hold_no_secret(self):
        fake = fd.FakeDiscord(CALLBACK)
        fake.consent = False
        folder = tempfile.mkdtemp(prefix="hub-login-proc-")
        self.addCleanup(lambda: __import__("shutil").rmtree(folder, ignore_errors=True))
        self.addCleanup(fake.close)
        env = {k: v for k, v in os.environ.items() if not k.startswith("ANTS_HUB_")}
        env.update({"ANTS_HUB_PORT": "0", "ANTS_HUB_HOST": "127.0.0.1", "ANTS_HUB_DB": os.path.join(folder, "hub.db"), "ANTS_HUB_PUBLIC_URL": hc.SITE, "PYTHONDONTWRITEBYTECODE": "1",
                    "ANTS_HUB_DISCORD_CLIENT_ID": fake.client_id, "ANTS_HUB_DISCORD_CLIENT_SECRET": self.SECRET, "ANTS_HUB_TEST_DISCORD_URL": fake.url, "ANTS_HUB_MODS": "5550001"})
        log_path = os.path.join(folder, "hub.log")
        with open(log_path, "w") as log:
            proc = subprocess.Popen([sys.executable, "-m", "hub"], cwd=REPO, env=env, stdout=log, stderr=subprocess.STDOUT)
        self.addCleanup(lambda: proc.poll() is None and proc.kill())
        port = None
        for _ in range(100):
            with open(log_path) as f:
                m = re.search(r" listening port=(\d+)", f.read())
            if m:
                port = int(m.group(1))
                break
            time.sleep(0.1)
        with open(log_path) as f:
            self.assertIsNotNone(port, f.read())
        cmdline = subprocess.run(["ps", "-o", "args=", "-p", str(proc.pid)], capture_output=True, text=True).stdout
        self.assertNotIn(self.SECRET, cmdline)
        v = Visitor(port, fake, address="198.51.100.9")
        r = v.login("room")
        self.assertEqual((r.status, r.get("location")), (302, "/#room"))
        c = hc.Client(port, headers={"Cookie": v.jar.header(), "Sec-Fetch-Site": "same-origin"})
        self.addCleanup(c.close)
        welcome = c.recv_json()
        self.assertEqual((welcome["login"], welcome["you"]), (True, {"name": "maple.leaf", "mod": False}))
        c.say("process canary 77c1")
        self.assertEqual(next(m for m in c.drain(0.5) if m["t"] == "line")["text"], "process canary 77c1")
        self.assertEqual(v.call("POST", "/hub/logout").status, 200)
        self.assertEqual(c.expect_close(3), 1000)
        fake.token_mode = "hang"
        w = Visitor(port, fake, address="198.51.100.10")
        started = time.monotonic()
        out = w.login()
        self.assertEqual(out.status, 503)
        self.assertLess(time.monotonic() - started, 12.0)                         # (the real limits: 6 seconds for a call, 10 for the two)
        self.assertEqual(hc.http_get(port, "/hub/healthz")[0], 200)
        proc.send_signal(signal.SIGTERM)
        self.assertEqual(proc.wait(10), 0)
        with open(log_path) as f:
            text = f.read()
        for canary in (self.SECRET, fake.client_id, next(iter(fake.tokens)), v.state, v.code, "maple.leaf", OLD, "process canary", "198.51.100", "5550001", "Traceback", folder):
            self.assertNotIn(canary, text, "the log shows " + str(canary))
        self.assertIn(" start ", text)
        self.assertRegex(text, r" start port=0 origin=https://chat.example.org mods=1 test_login=0 login_configured=1 min_account_days=7")
        self.assertIn(" warning what=a_fake_discord_is_on", text)
        for line in text.splitlines():
            self.assertRegex(line, r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ [a-z_]+( [a-z_]+=[A-Za-z0-9_.:/-]*)*$", line)


class TheSettings(unittest.TestCase):
    def load(self, **extra):
        env = {"ANTS_HUB_PUBLIC_URL": hc.SITE, "ANTS_HUB_DISCORD_CLIENT_ID": "4242424242420001", "ANTS_HUB_DISCORD_CLIENT_SECRET": "secret-0123456789"}
        env.update(extra)
        return configmod.load({k: v for k, v in env.items() if v is not None})

    def test_the_login_needs_the_id_the_secret_and_the_site(self):
        self.assertTrue(self.load().login_on)
        for drop in ("ANTS_HUB_DISCORD_CLIENT_ID", "ANTS_HUB_DISCORD_CLIENT_SECRET", "ANTS_HUB_PUBLIC_URL"):
            self.assertFalse(self.load(**{drop: None}).login_on, drop)
            self.assertFalse(self.load(**{drop: ""}).login_on, drop)
        self.assertFalse(configmod.load({}).login_on)

    def test_wrong_values_stop_the_start_and_are_never_echoed(self):
        for key, value in (("ANTS_HUB_DISCORD_CLIENT_ID", "canary-not-digits"), ("ANTS_HUB_DISCORD_CLIENT_ID", "12"), ("ANTS_HUB_DISCORD_CLIENT_SECRET", "short"), ("ANTS_HUB_DISCORD_CLIENT_SECRET", "has a blank 12345"),
                           ("ANTS_HUB_DISCORD_CLIENT_SECRET", "x" * 300), ("ANTS_HUB_DISCORD_CLIENT_SECRET", "caf\u00e9-canary-123456"), ("ANTS_HUB_MIN_ACCOUNT_DAYS", "canary"), ("ANTS_HUB_MIN_ACCOUNT_DAYS", "-1"),
                           ("ANTS_HUB_MIN_ACCOUNT_DAYS", "9999")):
            with self.assertRaises(configmod.ConfigError, msg=(key, value)) as e:
                self.load(**{key: value})
            self.assertNotIn(value, str(e.exception))
            self.assertIn(key, str(e.exception))

    def test_the_days(self):
        self.assertEqual(self.load().min_account_days, 7)
        self.assertEqual(self.load(ANTS_HUB_MIN_ACCOUNT_DAYS="0").min_account_days, 0)
        self.assertEqual(self.load(ANTS_HUB_MIN_ACCOUNT_DAYS=" 30 ").min_account_days, 30)

    def test_a_secret_is_in_no_text_of_the_settings(self):
        cfg = self.load(ANTS_HUB_DISCORD_CLIENT_SECRET="canary-secret-5d2e9a1f")
        for text in (repr(cfg), str(cfg), json.dumps(cfg.summary())):
            self.assertNotIn("canary-secret", text)
            self.assertNotIn("4242424242420001", text)
        self.assertEqual(cfg.summary()["login_configured"], 1)

    def test_the_fake_discord_is_for_a_hub_on_a_loopback_address_and_to_a_loopback_address(self):
        ok = self.load(ANTS_HUB_TEST_DISCORD_URL="http://127.0.0.1:4999", ANTS_HUB_HOST="127.0.0.1")
        self.assertEqual(ok.discord_test_url, "http://127.0.0.1:4999")
        self.assertEqual(self.load(ANTS_HUB_TEST_DISCORD_URL="http://localhost:4999", ANTS_HUB_HOST="localhost").discord_test_url, "http://localhost:4999")
        self.assertEqual(self.load(ANTS_HUB_TEST_DISCORD_URL="http://[::1]:4999", ANTS_HUB_HOST="::1").discord_test_url, "http://[::1]:4999")
        for host in ("0.0.0.0", "::", "10.0.0.5", "example.org", ""):
            with self.assertRaises(configmod.ConfigError, msg=host):
                self.load(ANTS_HUB_TEST_DISCORD_URL="http://127.0.0.1:4999", ANTS_HUB_HOST=host)
        for url in ("http://discord.com:4999", "https://discord.com", "http://10.0.0.1:4999", "http://127.0.0.1", "http://127.0.0.1:4999/x", "http://127.0.0.1:4999?a=1", "ftp://127.0.0.1:4999",
                    "http://user@127.0.0.1:4999", "http://127.0.0.1.evil.example:4999", "http://127.0.0.1:4999#x", "garbage", "http://0.0.0.0:4999", "http://127.0.0.1:99999"):
            with self.assertRaises(configmod.ConfigError, msg=url):
                self.load(ANTS_HUB_TEST_DISCORD_URL=url, ANTS_HUB_HOST="127.0.0.1")
        self.assertEqual(self.load().discord_test_url, "")                              # (not set: Discord itself)

    def test_the_test_login_and_the_discord_login_are_not_both_on(self):
        with self.assertRaises(configmod.ConfigError):
            self.load(ANTS_HUB_TEST_LOGIN="1", ANTS_HUB_HOST="127.0.0.1")
        self.assertTrue(self.load(ANTS_HUB_TEST_LOGIN="1", ANTS_HUB_HOST="127.0.0.1", ANTS_HUB_DISCORD_CLIENT_SECRET=None).test_login)

    def test_a_hub_without_the_settings_has_no_login_pages_and_says_login_false(self):
        with hub_rig.Rig(test_login=False) as rig:
            c = hc.Client(rig.port)
            self.assertEqual(c.recv_json()["login"], False)
            c.close()
            for path in ("/hub/login", "/hub/callback?code=x&state=y", "/hub/me", "/hub/logout", "/hub/forget"):
                self.assertEqual(hc.request(rig.port, "GET", path, {"Sec-Fetch-Site": "same-origin"}).status, 404, path)


if __name__ == "__main__":
    unittest.main()
