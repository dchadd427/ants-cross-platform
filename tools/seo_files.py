#!/usr/bin/env python3
"""robots.txt and sitemap.xml of the site (docs/BUILD_AND_RUN.md, "Search engines and share cards").

usage: seo_files.py ORIGIN OUT_DIR

ORIGIN is the address that the pages name themselves by, the one line of web/SITE_ORIGIN (https://host, no path, no slash at the end); the files are written to OUT_DIR as robots.txt and
sitemap.xml. The image's build (Dockerfile) and build_web.sh run it after the pages have the same address in their canonical links and share cards, so that the sitemap, the pages and the
robots file never name different addresses. The two lists below are the one home of what a crawler may and may not read: tests/scripts/test_web_seo.py runs this file and the pages and checks that the
sitemap lists exactly the pages that ask to be indexed, and that no page, and nothing a page loads, is closed to a crawler.
"""
import os
import re
import sys

# The pages that a search engine should list (their own canonical link is the same address). A match (/match?id=...) and the game page are pages too, but they ask not to be listed (noindex).
PUBLIC_PAGES = ("/", "/matches", "/mapmaker", "/changelog", "/legal")

# What a crawler is asked not to fetch, as paths of robots.txt (RFC 9309: a prefix; * and $ as it has them). Nothing here is a page or a file that a page loads to draw itself: a door is closed as the address
# itself, with a query, and as a folder (/history, /history?limit=3, /history/deaths), so that /history_page.js, a script of the pages, stays open. The admin page is not named: it says noindex itself, and a
# robots file that named it would only advertise its address.
DOORS = (
    ("The game server's doors: the game connection, the status, the live matches, the recorded matches and their data, the list of rooms, the maps, the numbers, the submissions",
     ("/ws", "/busy", "/live", "/replays", "/history", "/lobbies", "/maps", "/stats", "/submissions", "/test-maps")),
)
FOLDERS = (("The chat hub: the login, the connection, the pictures and images", ("/hub/",)),)
QUERIES = ("Addresses that hold a room (room=, join=, code=), a live match (live=) or a recording (replay=): a link that someone pasted into the chat is not for a search engine",
           ("/*room=", "/*join=", "/*code=", "/*live=", "/*replay="))

ORIGIN = re.compile(r"https?://[A-Za-z0-9](?:[A-Za-z0-9.-]*[A-Za-z0-9])?(?::[0-9]{1,5})?")


def check_origin(origin):
    """The origin itself, or a ValueError: the file's address is put into pages and a shell command, so only the characters of a host and a port are let through."""
    if not ORIGIN.fullmatch(origin):
        raise ValueError("not an origin (http:// or https://, a host, an optional port, no path and no slash at the end): %r" % origin)
    return origin


def robots_txt(origin):
    check_origin(origin)
    lines = ["# What a search engine may read of this site. The pages are open; the doors of the game server and the chat hub are not for crawlers.", "User-agent: *"]
    for why, paths in DOORS:
        lines.append("# " + why)
        for path in paths:
            lines.extend(["Disallow: %s$" % path, "Disallow: %s?" % path, "Disallow: %s/" % path])
    for why, paths in FOLDERS + (QUERIES,):
        lines.append("# " + why)
        lines.extend("Disallow: " + path for path in paths)
    lines += ["", "Sitemap: %s/sitemap.xml" % origin, ""]
    return "\n".join(lines)


def sitemap_xml(origin):
    check_origin(origin)
    lines = ['<?xml version="1.0" encoding="UTF-8"?>', '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">']
    lines.extend("  <url><loc>%s%s</loc></url>" % (origin, path) for path in PUBLIC_PAGES)
    lines += ["</urlset>", ""]
    return "\n".join(lines)


def main(argv):
    if len(argv) != 3:
        sys.stderr.write(__doc__)
        return 2
    try:
        robots, sitemap = robots_txt(argv[1]), sitemap_xml(argv[1])
    except ValueError as e:
        sys.stderr.write("seo_files.py: %s\n" % e)
        return 1
    os.makedirs(argv[2], exist_ok=True)
    for name, text in (("robots.txt", robots), ("sitemap.xml", sitemap)):
        with open(os.path.join(argv[2], name), "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
