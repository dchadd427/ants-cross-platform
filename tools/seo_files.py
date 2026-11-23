#!/usr/bin/env python3
"""robots.txt and sitemap.xml of the site (docs/BUILD_AND_RUN.md, "Search engines and share cards").

usage: seo_files.py ORIGIN OUT_DIR [--pages FILE]

ORIGIN is the address that the pages name themselves by, the one line of web/SITE_ORIGIN (https://host, no path, no slash at the end); the files are written to OUT_DIR as robots.txt and
sitemap.xml. FILE lists more pages for the sitemap, an address a line (tools/map_pages.py writes it: the map index and the map pages, made from the map library, never written by hand). The image's build (Dockerfile) and build_web.sh run it after the pages have the same address in their canonical links and share cards, so that the sitemap, the pages and the
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

# What a crawler may read below a path that is closed: the pictures of the maps (the map pages show them and a share card names one; a crawler that may not fetch the picture shows the page without it).
# The longest rule wins (RFC 9309), so this opens /maps/previews/ and nothing else under /maps/ (the files of the maps stay closed).
OPEN_BELOW = ("The pictures of the maps: the map pages and their share cards show them", ("/maps/previews/",))

ADDRESS = re.compile(r"/(?:[A-Za-z0-9_~-][A-Za-z0-9._~-]*(?:/[A-Za-z0-9_~-][A-Za-z0-9._~-]*)*)?")      # (a path of clean segments: none empty, none that starts with a dot)
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
    lines.append("# " + OPEN_BELOW[0])
    lines.extend("Allow: " + path for path in OPEN_BELOW[1])
    lines += ["", "Sitemap: %s/sitemap.xml" % origin, ""]
    return "\n".join(lines)


def sitemap_xml(origin, more=()):
    """The sitemap: the fixed pages, then `more` (addresses that begin with a slash and hold nothing but the characters of a clean address), each once."""
    check_origin(origin)
    for path in more:
        if not ADDRESS.fullmatch(path):
            raise ValueError("not a page address: %r" % path)
    lines = ['<?xml version="1.0" encoding="UTF-8"?>', '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">']
    lines.extend("  <url><loc>%s%s</loc></url>" % (origin, path) for path in list(PUBLIC_PAGES) + [p for p in dict.fromkeys(more) if p not in PUBLIC_PAGES])
    lines += ["</urlset>", ""]
    return "\n".join(lines)


def main(argv):
    if len(argv) not in (3, 5) or (len(argv) == 5 and argv[3] != "--pages"):
        sys.stderr.write(__doc__)
        return 2
    try:
        more = []
        if len(argv) == 5:
            with open(argv[4], encoding="utf-8") as f:
                more = [line.strip() for line in f if line.strip()]
        robots, sitemap = robots_txt(argv[1]), sitemap_xml(argv[1], more)
    except (ValueError, OSError) as e:
        sys.stderr.write("seo_files.py: %s\n" % e)
        return 1
    os.makedirs(argv[2], exist_ok=True)
    for name, text in (("robots.txt", robots), ("sitemap.xml", sitemap)):
        with open(os.path.join(argv[2], name), "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
