#!/usr/bin/env python3
"""The routes of the site, checked against a running nginx (the CI runs it against the web image; tests/scripts/test_nginx_routes.py runs it against docker/nginx.conf with two stand-in pages).

usage: web_routes_check.py --web http://127.0.0.1:19980/

The front page and the game page share the address "/": the owner made the Play online page the front page (one card for every game), and every address that the game
page ever had keeps opening it. The rules (docker/nginx.conf):
  /                                     the lobby (lobby.html), unless the query has join=<something> (a match on the game server: the games of the lobby's links and of every link shared before)
                                        or embed=1 (a frame of the lobby): those open the game page (index.html), as "/" always did
  /four.html[?query]                    a permanent redirect to /[?query] (the Play online page's own address, and its links with ?room=, ?map=, ?players=, ?fill=, ?play=here, ?aspect=)
  /play.html[?map=...&bots=...]         the game page, at an explicit path of its own (the games on this computer: the lobby's Play button)
  /index.html, /lobby.html              the two files themselves
Every html answer has the headers that every page of the site has always had: Cache-Control "no-cache, must-revalidate" (exactly one line), the two cross-origin headers, an ETag (a conditional
request is answered 304), and the redirect has the server's cross-origin headers too. The pages are recognised by one marker each: the lobby has the field of the name (id="player-name"),
the game page has the stage (id="game-stage"; the build writes it with or without quotes).
/stats, /stats/local                  the numbers of the front page: a GET and a POST that go to the game server (only what nginx itself refuses is checked here: other methods, a query)
/front/classic.css, the font          the Classic look's own files: served as files (an unknown address falls back to the game page, so the type is checked), the stylesheet with the pages' revalidation
/changelog.html, /changelog_archive.html, /asset_catalog/     the other pages of the site: each is its own page with the pages' headers and links /front/classic.css; /changelog,
                                        /catalog, /viewer and /asset_catalog go to them
Exit status 0: every check passed; 1: a check failed; 3: nothing answers at the address.
"""
import argparse
import http.client
import re
import sys
import urllib.parse

LOBBY = re.compile(r'id="?player-name"?')
GAME = re.compile(r'id="?game-stage"?')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--web", required=True, help="the site, e.g. http://127.0.0.1:19980/")
    args = ap.parse_args()
    parts = urllib.parse.urlsplit(args.web)
    host, port = parts.hostname, parts.port or 80
    failures = []
    count = [0]

    def check(ok, what):
        count[0] += 1
        print("  %s: %s" % ("ok  " if ok else "FAIL", what))
        if not ok:
            failures.append(what)

    def get(path, headers=None, method="GET"):
        conn = http.client.HTTPConnection(host, port, timeout=20)
        try:
            conn.request(method, path, headers=headers or {})
            r = conn.getresponse()
            body = r.read().decode("utf-8", "replace")
            raw = r.getheaders()
            return r.status, {k.lower(): v for k, v in raw}, [k.lower() for k, _ in raw], body
        finally:
            conn.close()

    try:
        get("/")
    except OSError as e:
        print("  SKIP: nothing answers at %s (%s)" % (args.web, e))
        return 3

    def page_headers(label, status, headers, names):
        check(status == 200, "%s: 200 (%s)" % (label, status))
        check(headers.get("cache-control") == "no-cache, must-revalidate" and names.count("cache-control") == 1, "%s: Cache-Control is exactly one line, no-cache, must-revalidate (%s)" % (label, headers.get("cache-control")))
        check(headers.get("cross-origin-opener-policy") == "same-origin" and headers.get("cross-origin-embedder-policy") == "require-corp", "%s: the two cross-origin headers (%s, %s)" % (label, headers.get("cross-origin-opener-policy"), headers.get("cross-origin-embedder-policy")))
        check("etag" in headers, "%s: an ETag is sent" % label)
        check(headers.get("content-type", "").startswith("text/html"), "%s: it is html (%s)" % (label, headers.get("content-type")))

    print("[web routes] the front page, the game page's addresses, the redirect of four.html")
    lobby_paths = ["/", "/lobby.html", "/?room=k7m2xq9p", "/?room=k7m2xq9p&roommap=small&roomseats=2", "/?room=k7m2xq9p&roommap=treasure&roomseats=4&roomteams=0%2B1&roomleaderstart=1&fill=medium&aspect=4:3",
                   "/?map=treasure&players=1&fill=medium", "/?map=tiny&players=4&fill=easy&play=here&aspect=4:3",
                   "/?aspect=4:3", "/?clear_cache=1790000000", "/?x=join&y=embed", "/?join=", "/?embed=0", "/?embed=", "/?joined=/ws", "/?map=small"]
    game_paths = ["/index.html", "/play.html", "/play.html?map=treasure&bots=medium&name=Bob&aspect=16:9", "/play.html?map=islands", "/?join=/ws&room=k7m2xq9p&roommap=small&roomseats=2",
                  "/?join=/ws&room=k7m2xq9p&roommap=small&roomseats=2&roomteams=0%2B1&roomleaderstart=1&platform=browser-linux&seat=1&name=Bob&aspect=16:9",
                  "/?join=/ws&room=abc&seat=1&name=Bob&aspect=16:9&embed=1", "/?join=/ws&room=abc&fill=hard", "/?embed=1", "/?embed=1&aspect=4:3", "/?aspect=4:3&join=/ws&room=r", "/?join=%2Fws&room=r",
                  "/?room=r&join=/ws", "/?join=/ws"]
    for path in lobby_paths:
        status, headers, names, body = get(path)
        page_headers("the lobby at " + path, status, headers, names)
        check(bool(LOBBY.search(body)) and not GAME.search(body), "the lobby at %s is the front page (its name field, not the game's stage)" % path)
    for path in game_paths:
        status, headers, names, body = get(path)
        page_headers("the game page at " + path, status, headers, names)
        check(bool(GAME.search(body)) and not LOBBY.search(body), "the game page at %s is the game (its stage, not the lobby's name field)" % path)

    print("[web routes] the old Play online address redirects for good, with its query")
    for query in ("", "?room=abc", "?room=k7m2xq9p&roommap=small&roomseats=2&roomteams=0%2B1&fill=medium&aspect=4:3", "?map=tiny&players=2&fill=easy&play=here", "?map=treasure&players=1&fill=hard", "?name=Ann%20%26%20Bob&x=%3Cb%3E", "?"):
        status, headers, names, body = get("/four.html" + query)
        want = "/" + (query if query != "?" else "")                    # (a "?" with nothing behind it carries no query: nginx's $args is empty)
        check(status == 301 and headers.get("location") == want, "/four.html%s answers 301 to %s (%s %s)" % (query, want, status, headers.get("location")))
        check(headers.get("cross-origin-opener-policy") == "same-origin" and headers.get("cross-origin-embedder-policy") == "require-corp", "... with the cross-origin headers of the site")
    status, headers, names, body = get("/four.html", method="HEAD")
    check(status == 301 and headers.get("location") == "/", "HEAD /four.html is the same redirect")

    print("[web routes] revalidation: a page that has not changed is answered 304 at every one of its addresses")
    for path in ("/", "/?join=/ws&room=x", "/?embed=1", "/play.html?map=tiny", "/lobby.html", "/index.html"):
        status, headers, names, body = get(path)
        tag = headers.get("etag")
        status2, headers2, names2, body2 = get(path, {"If-None-Match": tag}) if tag else (0, {}, [], "")
        check(status == 200 and tag and status2 == 304, "%s: a second request with If-None-Match is answered 304 (%s, %s)" % (path, status, status2))
    s1, h1, _, _ = get("/")
    s2, h2, _, _ = get("/?join=/ws&room=x")
    check(h1.get("etag") and h2.get("etag") and h1["etag"] != h2["etag"], "the two pages that share the address / have their own ETags (one cannot revalidate as the other)")

    print("[web routes] the numbers of the front page: /stats takes a plain GET and /stats/local a POST, nothing else (what nginx refuses itself: none of these reaches the game server, none is counted)")
    for method, path, want in (("POST", "/stats", 405), ("PUT", "/stats", 405), ("DELETE", "/stats", 405), ("HEAD", "/stats", 405), ("GET", "/stats?x=1", 404), ("GET", "/stats/local", 405), ("HEAD", "/stats/local", 405),
                               ("PUT", "/stats/local", 405), ("DELETE", "/stats/local", 405), ("POST", "/stats/local?x=1", 404), ("POST", "/stats/local?a=b&c=d", 404)):
        status = get(path, method=method)[0]
        check(status == want, "%s %s is refused with %d (%s)" % (method, path, want, status))
    for path in ("/stats/", "/stats/local/", "/stats/x", "/statsx"):
        status, headers, names, body = get(path)
        check(status == 200 and bool(GAME.search(body)), "%s is no address of the numbers: it is the game page like any unknown address (%s)" % (path, status))

    print("[web routes] the Classic look: its stylesheet and font are files of the site, and the changelog pages and Sprites and sounds are pages of their own that link the stylesheet")
    status, headers, names, body = get("/front/classic.css")
    check(status == 200 and headers.get("content-type", "").startswith("text/css") and "--clay" in body, "/front/classic.css is the stylesheet, not the game page that an unknown address falls back to (%s, %s)" % (status, headers.get("content-type")))
    check(headers.get("cache-control") == "no-cache, must-revalidate" and names.count("cache-control") == 1, "/front/classic.css: Cache-Control is exactly one line, no-cache, must-revalidate (%s)" % headers.get("cache-control"))
    tag = headers.get("etag")
    status2 = get("/front/classic.css", {"If-None-Match": tag})[0] if tag else 0
    check(bool(tag) and status2 == 304, "/front/classic.css: an unchanged stylesheet is answered 304 to If-None-Match (%s)" % status2)
    status, headers, names, body = get("/front/LibreFranklin-Medium.ttf")
    check(status == 200 and not headers.get("content-type", "").startswith("text/html"), "/front/LibreFranklin-Medium.ttf is the font, not a page (%s, %s)" % (status, headers.get("content-type")))
    for path in ("/changelog.html", "/changelog_archive.html", "/asset_catalog/"):
        status, headers, names, body = get(path)
        page_headers(path, status, headers, names)
        check('href="/front/classic.css"' in body and not GAME.search(body) and not LOBBY.search(body), "%s is a page of its own that links the Classic stylesheet (not the game page, not the front page)" % path)
    for path, want in (("/changelog", "/changelog.html"), ("/catalog", "/asset_catalog/"), ("/viewer", "/asset_catalog/"), ("/asset_catalog", "/asset_catalog/")):
        status, headers, names, body = get(path)
        check(status == 301 and headers.get("location") == want, "%s answers 301 to %s (%s %s)" % (path, want, status, headers.get("location")))

    print("[web routes] %d checks, %d failed" % (count[0], len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
