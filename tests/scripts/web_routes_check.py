#!/usr/bin/env python3
"""The routes of the site, checked against a running nginx (the CI runs it against the web image; tests/scripts/test_nginx_routes.py runs it against docker/nginx.conf with two stand-in pages).

usage: web_routes_check.py --web http://127.0.0.1:19980/

The front page and the game page share the address "/": the Play online page is the front page (the lobby now), and every address that the game
page ever had keeps opening it. The rules (docker/nginx.conf):
  /                                     the lobby (lobby.html), unless the query has join=<something> (a match on the game server: the games of the lobby's links and of every link shared before):
                                        those open the game page (index.html), as "/" always did. Every other query is the front page, an old ?map=... or ?embed=1 too
  /four.html[?query]                    a permanent redirect to /[?query] (the Play online page's own address, and its links with ?room=, ?aspect=)
  /play[?map=...&bots=...]              the game page, at a clean path of its own (the games on this computer: START for one on the front page)
  /play.html, /index.html, /lobby.html  temporary redirects (302) with the query kept: /play.html to /play, the other two to /
Every html answer has the headers that every page of the site has always had: Cache-Control "no-cache, must-revalidate" (exactly one line), the two cross-origin headers, an ETag (a conditional
request is answered 304), and the redirect has the server's cross-origin headers too. The pages are recognised by one marker each: the lobby has the room (id="lobby"; the build writes it with or without quotes),
the game page has the stage (id="game-stage"; the build writes it with or without quotes).
/stats, /stats/local                  the numbers of the front page: a GET and a POST that go to the game server (only what nginx itself refuses is checked here: other methods, a query)
/front/classic.css, the font          the Classic look's own files: served as files (an unknown address falls back to the game page, so the type is checked), the stylesheet with the pages' revalidation
/matches, /match, their stylesheet and the code that they and the game page in replay mode (play?replay=<file>) share; /matches.html and /match.html
                                      (and the old /watch.html, to /matches) go to the clean address with the query (a 302); /live-now, /live-now/ and /live.html go to / with no query (a 302); /live is the game server's list, not a page
/manifest.webmanifest, /app/icon-192.png, /favicon.png, /favicon.ico  the web app's manifest (revalidated), an icon and the browser-tab icon (with or without ?v=): files, not the game page; /?vp is the front page and /home/vp.js its script of numbers
/changelog, /legal, /mapmaker,        the other pages of the site: each is its own page with the pages' headers (the map maker has its own policy) and links /front/classic.css (not the map maker);
/asset_catalog/                         the .html address of each (not changelog.html, legal.html, matches.html: they answer 200, as before) is a 302 to the clean one with the query, a slash after the clean one goes to it; /catalog, /viewer and /asset_catalog go to the catalog
Exit status 0: every check passed; 1: a check failed; 3: nothing answers at the address.
"""
import argparse
import http.client
import re
import sys
import urllib.parse

LOBBY = re.compile(r'id="?lobby"?[ >]')
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

    def get(path, headers=None, method="GET", wait=20):
        conn = http.client.HTTPConnection(host, port, timeout=wait)
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
    lobby_paths = ["/", "/?room=k7m2xq", "/?room=k7m2xq&roommap=small&roomseats=2", "/?room=k7m2xq&roommap=treasure&roomseats=4&roomteams=0%2B1&roomleaderstart=1&fill=medium&aspect=4:3",
                   "/?map=treasure&players=1&fill=medium", "/?map=tiny&players=4&fill=easy&play=here&aspect=4:3",
                   "/?aspect=4:3", "/?clear_cache=1790000000", "/?x=join&y=embed", "/?join=", "/?embed=0", "/?embed=", "/?embed=1", "/?embed=1&aspect=4:3", "/?joined=/ws", "/?map=small"]
    game_paths = ["/play", "/play?map=treasure&bots=medium&name=Bob&aspect=16:9", "/play?map=islands", "/play?live=abc", "/play?replay=ants-X-20260101-000000Z.antsrep&embed=1", "/?join=/ws&room=k7m2xq&roommap=small&roomseats=2",
                  "/?join=/ws&room=k7m2xq&roommap=small&roomseats=2&roomteams=0%2B1&roomleaderstart=1&platform=browser-linux&seat=1&name=Bob&aspect=16:9",
                  "/?join=/ws&room=abc&seat=1&name=Bob&aspect=16:9", "/?join=/ws&room=abc&fill=hard", "/?aspect=4:3&join=/ws&room=r", "/?join=%2Fws&room=r",
                  "/?room=r&join=/ws", "/?join=/ws"]
    for path in lobby_paths:
        status, headers, names, body = get(path)
        page_headers("the lobby at " + path, status, headers, names)
        check(bool(LOBBY.search(body)) and not GAME.search(body), "the lobby at %s is the front page (its room, not the game's stage)" % path)
    for path in game_paths:
        status, headers, names, body = get(path)
        page_headers("the game page at " + path, status, headers, names)
        check(bool(GAME.search(body)) and not LOBBY.search(body), "the game page at %s is the game (its stage, not the lobby's room)" % path)

    print("[web routes] the old Play online address redirects for good, with its query")
    for query in ("", "?room=abc", "?room=k7m2xq&roommap=small&roomseats=2&roomteams=0%2B1&fill=medium&aspect=4:3", "?map=tiny&players=2&fill=easy&play=here", "?map=treasure&players=1&fill=hard", "?name=Ann%20%26%20Bob&x=%3Cb%3E", "?"):
        status, headers, names, body = get("/four.html" + query)
        want = "/" + (query if query != "?" else "")                    # (a "?" with nothing behind it carries no query: nginx's $args is empty)
        check(status == 301 and headers.get("location") == want, "/four.html%s answers 301 to %s (%s %s)" % (query, want, status, headers.get("location")))
        check(headers.get("cross-origin-opener-policy") == "same-origin" and headers.get("cross-origin-embedder-policy") == "require-corp", "... with the cross-origin headers of the site")
    status, headers, names, body = get("/four.html", method="HEAD")
    check(status == 301 and headers.get("location") == "/", "HEAD /four.html is the same redirect")

    print("[web routes] revalidation: a page that has not changed is answered 304 at every one of its addresses")
    for path in ("/", "/?join=/ws&room=x", "/play?map=tiny", "/play"):
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

    print("[web routes] the Classic look: its stylesheet and font are files of the site, and the changelog page, the Legal page and Sprites and sounds are pages of their own that link the stylesheet")
    status, headers, names, body = get("/front/classic.css")
    check(status == 200 and headers.get("content-type", "").startswith("text/css") and "--clay" in body, "/front/classic.css is the stylesheet, not the game page that an unknown address falls back to (%s, %s)" % (status, headers.get("content-type")))
    check(headers.get("cache-control") == "no-cache, must-revalidate" and names.count("cache-control") == 1, "/front/classic.css: Cache-Control is exactly one line, no-cache, must-revalidate (%s)" % headers.get("cache-control"))
    tag = headers.get("etag")
    status2 = get("/front/classic.css", {"If-None-Match": tag})[0] if tag else 0
    check(bool(tag) and status2 == 304, "/front/classic.css: an unchanged stylesheet is answered 304 to If-None-Match (%s)" % status2)
    status, headers, names, body = get("/front/LibreFranklin-Medium.ttf")
    check(status == 200 and not headers.get("content-type", "").startswith("text/html"), "/front/LibreFranklin-Medium.ttf is the font, not a page (%s, %s)" % (status, headers.get("content-type")))
    print("[web routes] the map maker's sprite atlas: its index and two sheets are files of the site, revalidated (never the week that other PNG files get), and an unknown name in the folder is a 404, not the game page")
    for name, kind in (("atlas.json", "application/json"), ("ground.png", "image/png"), ("objects.png", "image/png")):
        status, headers, names, body = get("/mapmaker_atlas/" + name)
        check(status == 200 and headers.get("content-type", "").startswith(kind) and not GAME.search(body), "/mapmaker_atlas/%s is the file, not the game page that an unknown address falls back to (%s, %s)" % (name, status, headers.get("content-type")))
        check(headers.get("cache-control") == "no-cache, must-revalidate" and names.count("cache-control") == 1 and "etag" in headers, "/mapmaker_atlas/%s: Cache-Control is exactly one line, no-cache, must-revalidate, with an ETag (%s)" % (name, headers.get("cache-control")))
    status, headers, names, body = get("/mapmaker_atlas/nothing.png")
    check(status == 404 and not GAME.search(body), "/mapmaker_atlas/nothing.png is a 404 (%s)" % status)
    print("[web routes] the web app's manifest and an icon are files of the site, never the game page that an unknown address falls back to")
    status, headers, names, body = get("/manifest.webmanifest")
    check(status == 200 and headers.get("content-type", "").split(";")[0] == "application/manifest+json" and '"name": "Ants!"' in body and not GAME.search(body),
          "/manifest.webmanifest is the manifest (%s, %s)" % (status, headers.get("content-type")))
    check(headers.get("cache-control") == "no-cache, must-revalidate" and names.count("cache-control") == 1, "/manifest.webmanifest: Cache-Control is exactly one line, no-cache, must-revalidate (%s)" % headers.get("cache-control"))
    status, headers, names, body = get("/app/icon-192.png")
    check(status == 200 and headers.get("content-type", "").split(";")[0] == "image/png" and body[1:4] == "PNG", "/app/icon-192.png is the icon (%s, %s)" % (status, headers.get("content-type")))
    for path in ("/favicon.png?v=0", "/favicon.png", "/favicon.ico?v=0"):
        status, headers, names, body = get(path)
        check(status == 200 and not GAME.search(body) and not LOBBY.search(body) and (body[1:4] == "PNG" if ".png" in path else True), "%s is the browser-tab icon, with or without ?v= (%s, %s)" % (path, status, headers.get("content-type")))
    status, headers, names, body = get("/?vp")
    check(status == 200 and headers.get("content-type", "").split(";")[0] == "text/html" and bool(LOBBY.search(body)) and not GAME.search(body), "/?vp is the front page (the box of numbers is made by its script) (%s, %s)" % (status, headers.get("content-type")))
    status, headers, names, body = get("/home/vp.js")
    check(status == 200 and "javascript" in headers.get("content-type", "") and "AntsHomeVp" in body and not GAME.search(body), "/home/vp.js is the script of the box of numbers, not the game page that an unknown address falls back to (%s, %s)" % (status, headers.get("content-type")))
    for path in ("/changelog", "/legal", "/asset_catalog/"):
        status, headers, names, body = get(path)
        page_headers(path, status, headers, names)
        check('href="/front/classic.css"' in body and not GAME.search(body) and not LOBBY.search(body), "%s is a page of its own that links the Classic stylesheet (not the game page, not the front page)" % path)
    for path, want in (("/catalog", "/asset_catalog/"), ("/viewer", "/asset_catalog/"), ("/asset_catalog", "/asset_catalog/")):
        status, headers, names, body = get(path)
        check(status == 301 and headers.get("location") == want, "%s answers 301 to %s (%s %s)" % (path, want, status, headers.get("location")))

    print("[web routes] the match history: its two pages, their stylesheet and the code they share with the player are files of the site (/history, /live and /replays themselves are the game server's, tests/scripts/test_nginx_replays.py)")
    for path, marker in (("/matches", 'id="list-panel"'), ("/match", 'id="sections"')):
        status, headers, names, body = get(path)
        page_headers(path, status, headers, names)         # (the same Cross-Origin-Opener-Policy and Cross-Origin-Embedder-Policy as the game page: match.html puts the game page in a frame)
        check(marker in body and "@@" not in body and not GAME.search(body) and not LOBBY.search(body), "%s is its own page, filled in, not the game page or the front page" % path)
        tag = headers.get("etag")
        check(bool(tag) and get(path, {"If-None-Match": tag})[0] == 304, "%s: an unchanged page is answered 304 to If-None-Match" % path)
    status, headers, names, body = get("/watch.html")
    check(status == 302 and headers.get("location") == "/matches", "/watch.html answers 302 to /matches (%s %s)" % (status, headers.get("location")))
    for path, word, kind in (("/replay_page.js", "AntsReplay", "javascript"), ("/history_page.js", "AntsHistory", "javascript"), ("/match_stats.js", "AntsMatchStats", "javascript"), ("/death_map.js", "AntsDeathMap", "javascript"), ("/matches.css", "--won", "css"), ("/match_stats.css", ".chips", "css"), ("/death_map.css", ".dm-stage", "css"), ("/replay_stats.css", "body.embed", "css")):
        status, headers, names, body = get(path)
        check(status == 200 and word in body and not headers.get("content-type", "").startswith("text/html"), "%s is the file, not the game page that an unknown address falls back to (%s, %s)" % (path, status, headers.get("content-type")))
        check(headers.get("cache-control") == "no-cache, must-revalidate" and names.count("cache-control") == 1, "%s: Cache-Control is exactly one line, no-cache, must-revalidate (%s)" % (path, headers.get("cache-control")))
        check(headers.get("cross-origin-opener-policy") == "same-origin" and headers.get("cross-origin-embedder-policy") == "require-corp", "%s carries the cross-origin isolation headers (%s, %s)" % (path, headers.get("cross-origin-opener-policy"), headers.get("cross-origin-embedder-policy")))

    print("[web routes] clean addresses: every page is asked for without .html, its old address is a temporary redirect (302) that keeps the query, except the three files that an earlier redirect led to, which answer 200, and nothing else is caught")
    PAGES = (("/changelog", "/changelog.html"), ("/legal", "/legal.html"), ("/matches", "/matches.html"), ("/match", "/match.html"), ("/play", "/play.html"), ("/mapmaker", "/mapmaker.html"))
    QUERIES = ("", "?live=ants-X-20260101-000000Z", "?room=k7m2xq&roommap=small&roomseats=2", "?code=ab12cd", "?replay=ants-X-20260101-000000Z.antsrep&embed=1", "?id=ants-X-20260101-000000Z&x=%20%26", "?q=ann&page=2")
    for clean, old in PAGES:
        s_clean, h_clean, n_clean, b_clean = get(clean)
        check(s_clean == 200 and h_clean.get("content-type", "").startswith("text/html"), "%s is a page (%s)" % (clean, s_clean))
        check(h_clean.get("cache-control") == "no-cache, must-revalidate" and n_clean.count("cache-control") == 1 and "etag" in h_clean, "%s: Cache-Control is exactly one line, no-cache, must-revalidate, with an ETag" % clean)
        check(n_clean.count("content-security-policy") == 1 and "frame-ancestors" in h_clean.get("content-security-policy", "") and n_clean.count("x-frame-options") == 1 and h_clean.get("strict-transport-security", "").startswith("max-age="), "%s carries the security headers of a page, each once" % clean)
        kept = clean in ("/changelog", "/legal", "/matches")      # (these three clean addresses were redirects to their files: the files answer 200, so a browser that kept the old redirect lands on a page)
        s_old, h_old, n_old, b_old = get(old)
        if kept:
            check(s_old == 200 and "location" not in h_old and b_old == b_clean, "%s answers 200, the same page as %s, with no redirect (%s)" % (old, clean, s_old))
        else:
            check(s_old == 302 and h_old.get("location") == clean and "cache-control" not in h_old, "%s answers 302 to %s with no Cache-Control (%s %s)" % (old, clean, s_old, h_old.get("location")))
        for query in QUERIES:
            s_q, h_q, n_q, b_q = get(old + query)
            if kept:
                check(s_q == 200 and "location" not in h_q, "%s%s answers 200, not a redirect (%s)" % (old, query, s_q))
            else:
                check(s_q == 302 and h_q.get("location") == clean + query, "%s%s answers 302 to %s%s, the query kept (%s)" % (old, query, clean, query, h_q.get("location")))
            s_c, h_c, n_c, b_c = get(clean + query)
            check(s_c == 200 and b_c == b_clean, "%s%s is the same page as %s (%s)" % (clean, query, clean, s_c))
        tag = h_clean.get("etag")
        check(bool(tag) and get(clean, {"If-None-Match": tag})[0] == 304, "%s: an unchanged page is answered 304 to If-None-Match" % clean)
        check(get(clean, method="HEAD")[0] == 200 and (get(old + "?a=b", method="HEAD")[0] == 200 if kept else get(old + "?a=b", method="HEAD")[1].get("location") == clean + "?a=b"), "HEAD works at %s and at %s" % (clean, old))
        check(get(clean + "/")[1].get("location") == clean and get(clean + "/?a=b")[1].get("location") == clean + "?a=b", "%s/ goes to %s (a slash after a page's address), the query kept" % (clean, clean))
        check(get(clean + "/", method="HEAD")[0] == 302, "HEAD %s/ is the same redirect" % clean)
    for old in ("/index.html", "/lobby.html"):
        for query in QUERIES + ("?join=/ws&room=abc",):
            status, headers, names, body = get(old + query)
            check(status == 302 and headers.get("location") == "/" + query, "%s%s answers 302 to /%s (%s)" % (old, query, query, headers.get("location")))
    check(bool(GAME.search(get("/?join=/ws&room=abc")[3])), "a link that was shared before (/?join=/ws&room=...) still opens the game page")
    check(bool(LOBBY.search(get("/?room=k7m2xq")[3])), "a room link (/?room=...) still opens the front page")
    print("[web routes] the page of the matches that run now is gone: its old addresses are a temporary redirect (302) to the front page with no query")
    for old in ("/live-now", "/live-now/", "/live.html", "/live-now?x=1", "/live.html?join=/ws&room=k7m2xq&replay=a.antsrep", "/live-now/?room=k7m2xq"):
        status, headers, names, body = get(old)
        check(status == 302 and headers.get("location") == "/" and "cache-control" not in headers, "%s answers 302 to / and drops the query (%s %s)" % (old, status, headers.get("location")))
        check(get(old, method="HEAD")[1].get("location") == "/", "HEAD %s is the same redirect" % old)
    print("[web routes] /live is the game server's list, not a page; the other paths of the server are not caught by a page's name")
    for path in ("/live", "/live/", "/replays", "/replays/", "/history", "/history/", "/lobbies", "/maps", "/maps/", "/submissions", "/stats", "/busy", "/admin/api/me", "/hub/me", "/ws"):
        try:
            status, headers, names, body = get(path, wait=2)
        except OSError:                 # (the path goes to the game server, which this check does not start: nginx waits for it, and that is not a page either)
            continue
        check(not (status == 200 and "text/html" in headers.get("content-type", "")) and "location" not in headers, "%s is not a page and is not redirected (%s)" % (path, status))
    for path in ("/playx", "/matchesx", "/legal/x", "/mapmaker/x", "/changelogs", "/match/x"):
        status, headers, names, body = get(path)
        check(status in (200, 404) and "location" not in headers and (status != 200 or bool(GAME.search(body))), "%s is no page: it is what any unknown address is (%s)" % (path, status))
    status, headers, names, body = get("/admin.html")
    check(status == 404, "/admin.html is still a 404 (the admin page is /admin) (%s)" % status)
    status, headers, names, body = get("/nothing.html")
    check(status == 404, "an .html file that is not there is a 404 (%s)" % status)
    print("[web routes] no version of the web server: the Server header has no number, on a page, a redirect, a file and an error")
    for path in ("/", "/play", "/play.html", "/four.html", "/nothing.html", "/stats?x=1", "/front/classic.css", "/matches?x=1"):
        status, headers, names, body = get(path)
        check(re.fullmatch(r"nginx", headers.get("server", "nginx")) is not None and not re.search(r"\d+\.\d+", headers.get("server", "")), "%s: the Server header names no version (%s)" % (path, headers.get("server")))
        if status >= 400:
            check(not re.search(r"nginx/\d", body), "%s: the error page names no version" % path)

    print("[web routes] %d checks, %d failed" % (count[0], len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
