#!/usr/bin/env python3
"""The login helper of the owner's page (docs/MAP_SUBMISSIONS.md, "The owner's page"): Sign in with Discord, then a signed cookie.

It does one thing: sends the browser to Discord, takes the answer at /admin/callback, asks Discord who the person is, and hands the browser a cookie that says so. It decides nothing about
who is the owner: the game server does (it knows ANTS_ADMIN_DISCORD_ID and checks the cookie with the shared secret on every call). The cookie is
"v1.<base64url of {"id", "name", "exp", "n"}>.<hex HMAC-SHA-256 of "v1.<payload>">", HttpOnly, SameSite=Lax, Secure when the public address is https, path /admin.

Routes (the site's nginx passes only these three here):
  GET  /admin/login     remembers a random state in a cookie and redirects to Discord
  GET  /admin/callback  checks the state, trades the code for a token, asks Discord for the person's id and name, sets the cookie, redirects to /admin
  POST /admin/logout    deletes the cookie, redirects to /admin

Settings, all from the environment (never from the command line, never from a file of the repository):
  ANTS_DISCORD_CLIENT_ID, ANTS_DISCORD_CLIENT_SECRET   the Discord application (developer portal)
  ANTS_ADMIN_SESSION_SECRET                            32 or more characters, the same value as the game server's
  ANTS_PUBLIC_URL                                      the site's address, for example https://example.org (no path); the redirect address is <it>/admin/callback
  ANTS_ADMIN_LOGIN_PORT (4040), ANTS_ADMIN_SESSION_HOURS (8, 1 to 24)
  ANTS_DISCORD_API, ANTS_DISCORD_AUTHORIZE_URL         where Discord is (the real addresses by default; tests point them at a fake)
The token that Discord gives is used once and thrown away. Only the id and the display name of the person go into the cookie. Nothing of a person is logged.
"""
import base64
import hashlib
import hmac
import json
import os
import re
import secrets
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

STATE_COOKIE = "ants_admin_state"
SESSION_COOKIE = "ants_admin"
STATE_SECONDS = 600
TIMEOUT_SECONDS = 10
MAX_NAME_CHARS = 64
USER_AGENT = "AntsAdminLogin/1 (+https://github.com/dchadd427/ants-cross-platform)"


class ConfigError(Exception):
    pass


def b64url(raw):
    return base64.urlsafe_b64encode(raw).rstrip(b"=").decode("ascii")


def load_config(env):
    """The settings of the environment, checked; ConfigError says what is wrong in one sentence."""
    def need(name):
        value = env.get(name, "")
        if not value:
            raise ConfigError("%s is not set" % name)
        return value

    cfg = {
        "client_id": need("ANTS_DISCORD_CLIENT_ID"),
        "client_secret": need("ANTS_DISCORD_CLIENT_SECRET"),
        "secret": need("ANTS_ADMIN_SESSION_SECRET"),
        "public_url": need("ANTS_PUBLIC_URL").rstrip("/"),
        "api": env.get("ANTS_DISCORD_API", "https://discord.com/api").rstrip("/"),
        "authorize": env.get("ANTS_DISCORD_AUTHORIZE_URL", "https://discord.com/oauth2/authorize"),
    }
    if len(cfg["secret"]) < 32 or any(ord(c) < 33 or ord(c) > 126 for c in cfg["secret"]):
        raise ConfigError("ANTS_ADMIN_SESSION_SECRET must be 32 or more visible characters")
    if not re.match(r"^https?://[^/?#\s]+$", cfg["public_url"]):
        raise ConfigError("ANTS_PUBLIC_URL must look like https://example.org (no path)")
    if not re.match(r"^[0-9]{1,32}$", cfg["client_id"]):
        raise ConfigError("ANTS_DISCORD_CLIENT_ID must be the application's number")
    try:
        cfg["port"] = int(env.get("ANTS_ADMIN_LOGIN_PORT", "4040"))
        cfg["hours"] = int(env.get("ANTS_ADMIN_SESSION_HOURS", "8"))
    except ValueError:
        raise ConfigError("ANTS_ADMIN_LOGIN_PORT and ANTS_ADMIN_SESSION_HOURS are whole numbers")
    if not 1 <= cfg["port"] <= 65535 or not 1 <= cfg["hours"] <= 24:
        raise ConfigError("ANTS_ADMIN_LOGIN_PORT is 1 to 65535 and ANTS_ADMIN_SESSION_HOURS 1 to 24")
    cfg["redirect_uri"] = cfg["public_url"] + "/admin/callback"
    cfg["secure"] = cfg["public_url"].startswith("https://")
    return cfg


def make_session_cookie_value(secret, user_id, name, expires, nonce):
    """The value that the game server verifies (src/ants_server/submissions_http.cpp, session_of)."""
    payload = json.dumps({"id": user_id, "name": name, "exp": expires, "n": nonce}, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    signed = "v1." + b64url(payload)
    mac = hmac.new(secret.encode("ascii"), signed.encode("ascii"), hashlib.sha256).hexdigest()
    return signed + "." + mac


def cookie_header(name, value, cfg, max_age):
    parts = ["%s=%s" % (name, value), "Path=/admin", "HttpOnly", "SameSite=Lax", "Max-Age=%d" % max_age]
    if cfg["secure"]:
        parts.append("Secure")
    return "; ".join(parts)


def parse_cookies(header):
    out = {}
    for part in (header or "").split(";"):
        key, sep, value = part.strip().partition("=")
        if sep and key not in out:
            out[key] = value
    return out


def discord_exchange(cfg, code):
    """Trades the code for the person's id and name. None when Discord says no or does not answer; never raises for a network fault."""
    form = urllib.parse.urlencode({"client_id": cfg["client_id"], "client_secret": cfg["client_secret"], "grant_type": "authorization_code", "code": code,
                                   "redirect_uri": cfg["redirect_uri"]}).encode("ascii")
    try:
        request = urllib.request.Request(cfg["api"] + "/oauth2/token", data=form, method="POST",
                                         headers={"Content-Type": "application/x-www-form-urlencoded", "User-Agent": USER_AGENT, "Accept": "application/json"})
        with urllib.request.urlopen(request, timeout=TIMEOUT_SECONDS) as answer:
            token = json.loads(answer.read(65536).decode("utf-8")).get("access_token", "")
        if not isinstance(token, str) or not token:
            return None
        request = urllib.request.Request(cfg["api"] + "/users/@me", headers={"Authorization": "Bearer " + token, "User-Agent": USER_AGENT, "Accept": "application/json"})
        with urllib.request.urlopen(request, timeout=TIMEOUT_SECONDS) as answer:
            me = json.loads(answer.read(65536).decode("utf-8"))
    except (urllib.error.URLError, OSError, ValueError):
        return None
    user_id = me.get("id") if isinstance(me, dict) else None
    if not isinstance(user_id, str) or not re.match(r"^[0-9]{1,32}$", user_id):
        return None
    shown = me.get("global_name") or me.get("username") or "someone"
    return user_id, str(shown)[:MAX_NAME_CHARS]


PAGE = ("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>Sign in</title>"
        "<body style='font:18px/1.4 sans-serif;max-width:32em;margin:3em auto;padding:0 1em'><p>%s</p><p><a href='/admin'>Back</a></p>")


def make_handler(cfg):
    class Handler(BaseHTTPRequestHandler):
        server_version = "ants-admin-login"
        sys_version = ""
        protocol_version = "HTTP/1.0"

        def log_message(self, *args):       # (the default line holds the address and the query, which holds the code: nothing is logged)
            pass

        def send(self, status, body=b"", headers=(), content_type="text/html; charset=utf-8"):
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Referrer-Policy", "no-referrer")
            for name, value in headers:
                self.send_header(name, value)
            self.end_headers()
            self.wfile.write(body)

        def redirect(self, location, headers=()):
            self.send(303 if self.command == "POST" else 302, b"", list(headers) + [("Location", location)])

        def fail(self, status, text):
            self.send(status, (PAGE % text).encode("utf-8"), [("Set-Cookie", cookie_header(STATE_COOKIE, "", cfg, 0))])

        def do_GET(self):
            url = urllib.parse.urlsplit(self.path)
            if url.path == "/admin/login":
                state = b64url(secrets.token_bytes(32))
                query = urllib.parse.urlencode({"client_id": cfg["client_id"], "response_type": "code", "redirect_uri": cfg["redirect_uri"], "scope": "identify", "state": state,
                                                "prompt": "none"})
                self.redirect(cfg["authorize"] + "?" + query, [("Set-Cookie", cookie_header(STATE_COOKIE, state, cfg, STATE_SECONDS))])
            elif url.path == "/admin/callback":
                args = urllib.parse.parse_qs(url.query, keep_blank_values=True)
                one = lambda key: args[key][0] if len(args.get(key, [])) == 1 else ""
                cookie_state = parse_cookies(self.headers.get("Cookie")).get(STATE_COOKIE, "")
                state = one("state")
                if not state or not cookie_state or not hmac.compare_digest(state.encode("utf-8"), cookie_state.encode("utf-8")):
                    return self.fail(400, "The sign-in did not start here, or took too long. Please try again.")
                code = one("code")
                if not code or len(code) > 512 or one("error"):
                    return self.fail(400, "Discord did not sign you in.")
                who = discord_exchange(cfg, code)
                if who is None:
                    return self.fail(502, "Discord did not answer. Please try again in a moment.")
                expires = int(time.time()) + cfg["hours"] * 3600
                value = make_session_cookie_value(cfg["secret"], who[0], who[1], expires, b64url(secrets.token_bytes(12)))
                self.redirect("/admin", [("Set-Cookie", cookie_header(SESSION_COOKIE, value, cfg, cfg["hours"] * 3600)), ("Set-Cookie", cookie_header(STATE_COOKIE, "", cfg, 0))])
            else:
                self.send(404, b'{"error":"not_found"}', content_type="application/json")

        def do_POST(self):
            if urllib.parse.urlsplit(self.path).path == "/admin/logout":
                self.redirect("/admin", [("Set-Cookie", cookie_header(SESSION_COOKIE, "", cfg, 0))])
            else:
                self.send(404, b'{"error":"not_found"}', content_type="application/json")

    return Handler


def make_off_handler():
    """What the helper answers when it is not configured: 503 for every path (a container that exits would only be restarted again and again)."""
    class Off(BaseHTTPRequestHandler):
        server_version = "ants-admin-login"
        sys_version = ""
        protocol_version = "HTTP/1.0"

        def log_message(self, *args):
            pass

        def answer(self):
            body = b'{"error":"admin_login_off"}'
            self.send_response(503)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        do_GET = do_POST = answer

    return Off


def main():
    try:
        cfg = load_config(os.environ)
        handler = make_handler(cfg)
        port = cfg["port"]
    except ConfigError as e:
        sys.stderr.write("admin login: %s; every request is answered 503 until it is set\n" % e)
        handler = make_off_handler()
        try:
            port = int(os.environ.get("ANTS_ADMIN_LOGIN_PORT", "4040"))
        except ValueError:
            port = 4040
        cfg = None
    server = ThreadingHTTPServer(("0.0.0.0", port), handler)
    server.daemon_threads = True
    if cfg is not None:
        sys.stderr.write("admin login helper on port %d, redirect address %s\n" % (port, cfg["redirect_uri"]))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
