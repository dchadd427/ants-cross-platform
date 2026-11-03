#!/usr/bin/env python3
"""A whole site on this machine for the browser run of the chat hub's login (tests/scripts/web_home_login_browser.js, opt-in): the REAL docker/nginx.conf (only the places are changed: the listen address, the
folder of the pages, the names of the hub and of the game server, the folders of the caches) in front of the real hub (`python -m hub`, with the Discord settings from its environment and the fake
Discord of the tests as its Discord), the pages of web/ and, when a binary is given, a real ants_server (the rooms of the front page).

    python3 tests/scripts/login_site.py [--server PATH] [--consent]       prints one JSON line when it is up, then reads commands on standard input until it ends

The site is http://127.0.0.1:PORT and the fake Discord is http://localhost:PORT (another host name, so that the browser sees two SITES, as it does with the real Discord: a cookie that is SameSite=Strict is
not sent to the site on a navigation that Discord started, and the run shows that the login works anyway). Commands, one per line, answered with one JSON line: `user ID NAME` (who the next visitor to
authorize is at Discord), `consent on|off` (a first visit shows the consent page, a returning one does not), `token_mode MODE`, `me_mode MODE` (tests/scripts/fake_discord.py), `ban ID`, `unban ID`, `log` (the
hub's log so far), `requests` (what the hub sent to the fake Discord: the paths), `stop`.
"""
import argparse
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

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fake_discord as fd  # noqa: E402


# the hub as `python -m hub` runs it, with the limits of the login's pages raised (a browser run logs in a dozen times from one address in a minute; test_hub_login.py tests the limits themselves)
HUB_WITH_LOOSE_LOGIN_LIMITS = """
import dataclasses, sys
from hub import limits, server
@dataclasses.dataclass(frozen=True)
class Loose(limits.Limits):
    login_burst: int = 1000
    session_burst: int = 1000
limits.Limits = Loose
sys.exit(server.main())
"""


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def site_conf(site_port, webroot, hub_port, server_ports, dead_port, cache):
    """The real nginx.conf with its places changed."""
    with open(os.path.join(REPO, "docker", "nginx.conf"), encoding="utf-8") as f:
        text = f.read()
    text = text.replace("    listen 80;", "    listen 127.0.0.1:%d;" % site_port).replace("    listen [::]:80;", "")
    text = text.replace("/usr/share/nginx/html", webroot).replace("/var/cache/nginx/", cache + "/").replace("/srv/community-maps/", os.path.join(REPO, "Community-Maps") + "/")
    text = text.replace("    resolver 127.0.0.11 valid=10s ipv6=off;", "").replace("        resolver 127.0.0.11 valid=10s ipv6=off;", "")
    text = text.replace("include /etc/nginx/ants-trusted-proxies/*.conf;", "")
    text = text.replace("set $ants_hub ants-hub:4040;", "set $ants_hub 127.0.0.1:%d;" % hub_port)
    text = re.sub(r"(zone=ants_hub_(?:login|callback|session):1m rate=)\d+r/m", r"\g<1>600r/m", text)           # (the run logs in a dozen times in a minute from one address: the limits are tested in test_nginx_hub_login.py)
    text = re.sub(r"ants-server:(\d+)", lambda m: "127.0.0.1:%d" % server_ports.get(m.group(1), dead_port), text)
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", default=os.environ.get("ANTS_SERVER", ""), help="a built ants_server (protocol 22); without it the front page has no game server")
    ap.add_argument("--consent", action="store_true", help="the fake Discord shows a consent page (a first visit)")
    ap.add_argument("--min-days", default="7")
    args = ap.parse_args()

    tmp = tempfile.mkdtemp(prefix="ants_login_site.")
    procs, closers = [], []

    def stop_all():
        for p in procs:
            if p.poll() is None:
                p.send_signal(signal.SIGTERM)
        for p in procs:
            try:
                p.wait(5)
            except subprocess.TimeoutExpired:
                p.kill()
        for c in closers:
            c()
        shutil.rmtree(tmp, ignore_errors=True)

    try:
        site_port = free_port()
        site = "http://127.0.0.1:%d" % site_port
        fake = fd.FakeDiscord(site + "/hub/callback")
        closers.append(fake.close)
        fake.consent = args.consent
        hub_port = free_port()
        db = os.path.join(tmp, "hub.db")
        hub_log = os.path.join(tmp, "hub.log")
        env = {k: v for k, v in os.environ.items() if not k.startswith("ANTS_HUB_")}
        env.update({"ANTS_HUB_PORT": str(hub_port), "ANTS_HUB_HOST": "127.0.0.1", "ANTS_HUB_DB": db, "ANTS_HUB_PUBLIC_URL": site, "PYTHONDONTWRITEBYTECODE": "1", "PYTHONUNBUFFERED": "1",
                    "ANTS_HUB_DISCORD_CLIENT_ID": fake.client_id, "ANTS_HUB_DISCORD_CLIENT_SECRET": fake.client_secret, "ANTS_HUB_TEST_DISCORD_URL": "http://localhost:%d" % fake.port,
                    "ANTS_HUB_MIN_ACCOUNT_DAYS": args.min_days})
        with open(hub_log, "w") as log:
            hub = subprocess.Popen([sys.executable, "-c", HUB_WITH_LOOSE_LOGIN_LIMITS], cwd=REPO, env=env, stdout=log, stderr=subprocess.STDOUT)
        procs.append(hub)
        server_ports = {}
        if args.server:
            ws, replay = free_port(), free_port()
            results = os.path.join(tmp, "results")
            os.makedirs(results)
            procs.append(subprocess.Popen([args.server, "--maps", os.path.join(REPO, "Original-Ants", "Maps"), "--community-maps", os.path.join(REPO, "Community-Maps"), "--port", "0", "--ws-port", str(ws),
                                           "--replay-port", str(replay), "--no-db", "--results-dir", results, "--demo-rooms", "2", "--demo-map", "TREASURE.LVL", "--demo-maps", "TINY.LVL,TREASURE.LVL"], stdout=open(os.path.join(tmp, "server.log"), "w"), stderr=subprocess.STDOUT))
            server_ports = {"4002": ws, "4020": replay}
        webroot = os.path.join(tmp, "web")
        shutil.copytree(os.path.join(REPO, "web"), webroot, ignore=shutil.ignore_patterns("*.map"))
        cache = os.path.join(tmp, "cache")
        os.makedirs(cache)
        conf = os.path.join(tmp, "nginx.conf")
        with open(os.path.join(tmp, "site.conf"), "w", encoding="utf-8") as f:
            f.write(site_conf(site_port, webroot, hub_port, server_ports, free_port(), cache))
        with open(conf, "w", encoding="utf-8") as f:
            f.write("daemon off;\nmaster_process off;\npid %s/n.pid;\nerror_log %s/e.log;\nevents { worker_connections 512; }\nhttp {\n  include /etc/nginx/mime.types;\n  default_type application/octet-stream;\n"
                    "  client_body_temp_path %s/b;\n  proxy_temp_path %s/p;\n  fastcgi_temp_path %s/f;\n  uwsgi_temp_path %s/u;\n  scgi_temp_path %s/s;\n  include %s/site.conf;\n}\n" % ((tmp,) * 8))
        procs.append(subprocess.Popen(["nginx", "-c", conf, "-p", tmp], stdout=subprocess.DEVNULL, stderr=open(os.path.join(tmp, "nginx.err"), "w")))
        for _ in range(100):
            try:
                socket.create_connection(("127.0.0.1", site_port), timeout=0.3).close()
                socket.create_connection(("127.0.0.1", hub_port), timeout=0.3).close()
                break
            except OSError:
                if any(p.poll() is not None for p in procs):
                    raise SystemExit("a process of the site stopped: " + open(os.path.join(tmp, "nginx.err")).read()[:500] + open(hub_log).read()[:500])
                time.sleep(0.1)
        print(json.dumps({"site": site, "discord": "http://localhost:%d" % fake.port, "tmp": tmp, "hub": hub_port, "server": bool(args.server), "client_id": fake.client_id}), flush=True)
        for line in sys.stdin:
            words = line.split()
            if not words:
                continue
            cmd = words[0]
            out = {"ok": True}
            if cmd == "stop":
                break
            elif cmd == "user":
                fake.user = (words[1], words[2])
            elif cmd == "consent":
                fake.consent = words[1] == "on"
            elif cmd == "token_mode":
                fake.token_mode = words[1]
            elif cmd == "me_mode":
                fake.me_mode = words[1]
            elif cmd in ("ban", "unban"):
                done = subprocess.run([sys.executable, "-m", "hub.admin", "--db", db, cmd, words[1]], cwd=REPO, capture_output=True, text=True)
                out = {"ok": done.returncode == 0, "out": done.stdout + done.stderr}
            elif cmd == "log":
                with open(hub_log) as f:
                    out = {"log": f.read()}
            elif cmd == "requests":
                out = {"requests": [[m, p] for m, p, _q, _h, _b in fake.requests]}
            else:
                out = {"ok": False, "error": "unknown command"}
            print(json.dumps(out), flush=True)
    finally:
        stop_all()


if __name__ == "__main__":
    main()
