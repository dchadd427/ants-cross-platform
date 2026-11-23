#!/usr/bin/env python3
"""Search engines and share cards (docs/BUILD_AND_RUN.md, "Search engines and share cards"): the head of every page, robots.txt and sitemap.xml (run by ./run_tests.sh --fast and by the CI;
tests/data/web_seo_mutants.json breaks one rule at a time, for tools/mutate.py).

The pages are read as a browser reads them (an HTML parser, never a line of text): the pages of web/ that docker/nginx.conf serves (every exact location that rewrites to a file, the front page and the
game page's template), the catalog of sprites and sounds and the changelog page, which tools/changelog_to_html.py makes. robots.txt and sitemap.xml are made by running tools/seo_files.py, and robots.txt is read the way a crawler reads it
(RFC 9309: the longest matching rule wins, * and $, Allow wins a tie). What is asked:

  - every page has a title in the one form "<page> - Ants (1998)" (an em dash; the front page's is "Ants (1998) - <a line about the game>"), one description, and the share card (Open Graph and the shape of the Twitter card) that says the same title and description,
    with a picture that exists and is big enough;
  - a page that asks to be listed has exactly one canonical link, its own address on the site's one address; a page that asks not to be (a match, the game page) says noindex, keeps its links followed, has no
    canonical link, and is not in the sitemap; the pages that are listed are the front page, the matches, the map maker, the changelog and the legal page;
  - the front page has structured data that parses and describes a free game that plays in a browser; no other page has structured data;
  - no page, script or tool names the site's host: the address is the one line of web/SITE_ORIGIN, put into the pages when the image is built (the tool refuses a text that is not an origin);
  - the sitemap lists exactly the pages that ask to be listed, by the same address as their canonical links; robots.txt keeps a crawler out of every door that nginx hands to the game server and the chat hub
    (the admin page is not named: it says noindex itself, and naming it would advertise it) and out of the addresses that hold a room or a recording, and never out of a page or of a file that a page loads to draw itself.
"""
import html.parser
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_nginx_stats as stats  # noqa: E402  (the helpers that read docker/nginx.conf)

REPO = stats.REPO
sys.path.insert(0, os.path.join(REPO, "tools"))
import changelog_to_html  # noqa: E402
import seo_files  # noqa: E402

TOOL = os.path.join(REPO, "tools", "seo_files.py")
PLACEHOLDER = "@@SITE_ORIGIN@@"
ORIGIN = "https://site.example"
TITLE = re.compile(r"^(?P<page>.+) — Ants \(1998\)$")
FRONT_TITLE = re.compile(r"^Ants \(1998\) — (?P<line>.+)$")                                # (the front page names the game first, then says in a line what it is)
LISTED = {"/", "/matches", "/mapmaker", "/changelog", "/legal"}
NOT_LISTED = {"/match", "/play", "/asset_catalog/", "/test_3d/", "/test_3d/viewer.html"}                 # (a match, the game page, the sprite catalog, and the 3D viewer: pages that are there, and ask not to be in a search engine)
NO_CARD = {"/asset_catalog/", "/test_3d/", "/test_3d/viewer.html"}                                       # (a page that is not made to be shared has no share card)
CONF = stats.strip_comments(stats.read("docker", "nginx.conf"))


class Head(html.parser.HTMLParser):
    """What a browser takes from a page: the title, the meta lines, the links, the scripts of data and the files that the page loads."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.title, self.metas, self.links, self.data_scripts, self.loads = "", [], [], [], []
        self.in_title, self.script_type, self.script_text = False, None, []

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag == "title":
            self.in_title = True
        elif tag == "meta":
            self.metas.append(a)
        elif tag == "link":
            self.links.append(a)
            if a.get("rel") in ("stylesheet", "icon", "manifest", "apple-touch-icon", "preload", "shortcut icon") and a.get("href"):
                self.loads.append(a["href"])
        elif tag == "script":
            self.script_type, self.script_text = a.get("type", ""), []
            if a.get("src"):
                self.loads.append(a["src"])
        elif tag == "img" and a.get("src"):
            self.loads.append(a["src"])

    def handle_endtag(self, tag):
        if tag == "title":
            self.in_title = False
        elif tag == "script":
            if self.script_type == "application/ld+json":
                self.data_scripts.append("".join(self.script_text))
            self.script_type = None

    def handle_data(self, data):
        if self.in_title:
            self.title += data
        if self.script_type is not None:
            self.script_text.append(data)

    def meta(self, key, value):
        """The content of every meta line whose name or property is `value`."""
        return [m.get("content") for m in self.metas if m.get(key) == value]


def served_pages():
    """{address: the page's text} of every page that docker/nginx.conf serves from a file of its own."""
    files = {
        "/": stats.read("web", "lobby.html"),
        "/asset_catalog/": stats.read("asset_catalog", "index.html"),
        "/test_3d/": stats.read("tools", "ant_models", "compare.html"),
        "/test_3d/viewer.html": stats.read("tools", "ant_models", "viewer.html"),
    }
    for address, name in re.findall(r"location = (/[a-z]+) \{\s*rewrite \^ /([a-z]+)\.html last;", CONF):
        files[address] = changelog_to_html.convert(stats.read("CHANGELOG.md"), changelog_to_html.DEFAULT_REPO_BLOB, version="v0.0.0", build_id="0000000") if address == "/changelog" \
            else stats.read("web", "shell.html" if name == "play" else name + ".html")
    return files


def parse(text):
    head = Head()
    head.feed(text)
    head.close()
    return head


def png_size(path):
    with open(path, "rb") as f:
        data = f.read(24)
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    return struct.unpack(">II", data[16:24])


def crawler_may_fetch(robots, address):
    """RFC 9309 for the group of every crawler (*): the longest rule that matches the path and the query wins, Allow on a tie; no rule matches: allowed."""
    rules = [(line.split(":", 1)[0].strip().lower(), line.split(":", 1)[1].strip()) for line in robots.splitlines() if ":" in line and not line.lstrip().startswith("#")]
    best = (-1, True)
    for kind, pattern in rules:
        if kind not in ("allow", "disallow") or not pattern:
            continue
        regex = "".join(".*" if c == "*" else "$" if c == "$" else re.escape(c) for c in pattern)
        if re.match(regex, address) and (len(pattern), kind == "allow") > (best[0], best[1]):
            best = (len(pattern), kind == "allow")
    return best[1]


def make(origin=ORIGIN):
    """Runs the tool as the image's build does: {name: text} of what it wrote."""
    with tempfile.TemporaryDirectory(prefix="ants_seo_") as out:
        done = subprocess.run([sys.executable, TOOL, origin, out], capture_output=True, text=True, timeout=60)
        files = {}
        for name in ("robots.txt", "sitemap.xml"):
            path = os.path.join(out, name)
            if os.path.exists(path):
                with open(path, encoding="utf-8") as f:
                    files[name] = f.read()
        return done.returncode, done.stderr, files


class ThePages(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.texts = served_pages()
        cls.heads = {address: parse(text) for address, text in cls.texts.items()}

    def test_the_pages_that_nginx_serves_are_the_pages_that_are_checked(self):
        self.assertEqual(set(self.heads), LISTED | NOT_LISTED)               # (a new page is listed or not on purpose: this test fails until the page is in one of the two sets and has its head)

    def test_every_page_has_a_title_in_the_one_form(self):
        titles = {}
        for address, head in self.heads.items():
            title = head.title.replace("@@SITE_TITLE@@", "")
            found = (FRONT_TITLE if address == "/" else TITLE).match(title)
            self.assertIsNotNone(found, "%s: %r" % (address, title))
            self.assertLessEqual(len(title), 70, address)                    # (a search result shows about 60 characters)
            titles[address] = title
        self.assertEqual(len(set(titles.values())), len(titles), titles)       # (two pages with one title are one page to a reader of a list of results)

    def test_every_page_has_one_description_that_fits_a_result(self):
        seen = set()
        for address, head in self.heads.items():
            found = head.meta("name", "description")
            self.assertEqual(len(found), 1, address)
            self.assertTrue(60 <= len(found[0]) <= 175, "%s: %d characters" % (address, len(found[0])))
            seen.add(found[0])
        self.assertEqual(len(seen), len(self.heads))

    def test_a_listed_page_has_one_canonical_link_that_is_its_own_address_and_a_page_that_is_not_listed_has_none_and_says_noindex(self):
        for address, head in self.heads.items():
            canonical = [l["href"] for l in head.links if l.get("rel") == "canonical"]
            robots = [c.replace(" ", "").split(",") for c in head.meta("name", "robots")]
            if address in LISTED:
                self.assertEqual(canonical, [PLACEHOLDER + address], address)
                self.assertEqual(head.meta("property", "og:url"), [PLACEHOLDER + address], address)
                self.assertEqual([r for r in robots if "noindex" in r], [], address)
            else:
                self.assertEqual(canonical, [], address)                      # (a page that is not listed has no address to name: the same page answers at /match?id=x for every match)
                self.assertEqual(head.meta("property", "og:url"), [], address)
                self.assertEqual(len(robots), 1, address)
                self.assertIn("noindex", robots[0], address)
                self.assertNotIn("nofollow", robots[0], address)             # (the links of a match page are followed: the owner's choice)

    def test_every_page_has_a_share_card_that_says_the_pages_own_title_and_description_with_a_picture_that_exists(self):
        for address, head in self.heads.items():
            if address in NO_CARD:
                continue
            where = address
            self.assertEqual(head.meta("property", "og:title"), [head.title.replace("@@SITE_TITLE@@", "")], where)
            self.assertEqual(head.meta("property", "og:description"), head.meta("name", "description"), where)
            self.assertEqual(head.meta("property", "og:type"), ["website"], where)
            self.assertEqual(head.meta("property", "og:site_name"), ["Ants (1998)"], where)
            self.assertEqual(head.meta("name", "twitter:card"), ["summary"], where)                # (the square picture below is a summary card: X and Discord take the rest from the Open Graph lines)
            image = head.meta("property", "og:image")
            self.assertEqual(len(image), 1, where)
            self.assertTrue(image[0].startswith(PLACEHOLDER + "/"), image)             # (an absolute address: the card is read by a program that is not on the site)
            path = os.path.join(REPO, "web", image[0][len(PLACEHOLDER) + 1:])
            self.assertTrue(os.path.isfile(path), image)                              # (a picture that the image serves: web/ is copied into it)
            width, height = png_size(path)
            self.assertGreaterEqual(min(width, height), 200, image)                   # (the smallest picture that a card accepts)
            self.assertEqual((head.meta("property", "og:image:width"), head.meta("property", "og:image:height")), ([str(width)], [str(height)]), where)
            self.assertEqual(len(head.meta("property", "og:image:alt")), 1, where)
            self.assertTrue(head.meta("property", "og:image:alt")[0].strip(), where)

    def test_the_front_page_has_structured_data_of_a_free_game_that_plays_in_a_browser_and_no_other_page_has_any(self):
        for address, head in self.heads.items():
            if address != "/":
                self.assertEqual(head.data_scripts, [], address)
        front = self.heads["/"]
        self.assertEqual(len(front.data_scripts), 1)
        data = json.loads(front.data_scripts[0].replace(PLACEHOLDER, ORIGIN))
        self.assertEqual(data["@context"], "https://schema.org")
        self.assertEqual(data["@type"], "VideoGame")
        self.assertEqual(data["name"], "Ants (1998)")
        self.assertEqual(data["description"], front.meta("name", "description")[0])
        self.assertEqual(data["url"], ORIGIN + "/")
        self.assertEqual(data["genre"], "Real-time strategy")
        self.assertEqual(data["gamePlatform"], "Web browser")
        self.assertIs(data["isAccessibleForFree"], True)
        self.assertEqual((data["offers"]["@type"], data["offers"]["price"]), ("Offer", "0"))
        self.assertIn("MultiPlayer", data["playMode"])
        self.assertTrue(data["image"].startswith(ORIGIN + "/"))

    def test_no_page_names_the_sites_host_and_every_address_a_page_gives_for_itself_is_the_one_placeholder(self):
        for address, text in self.texts.items():
            self.assertNotRegex(text, r"https?://[A-Za-z0-9.-]*playants\.org", address)
            self.assertNotRegex(text, r"https?://localhost", address)
        for needle in ('rel="canonical"', 'property="og:url"', 'property="og:image"'):
            for address, text in self.texts.items():
                for line in re.findall(r"<(?:link|meta)[^>]*%s[^>]*>" % needle, text):
                    self.assertIn(PLACEHOLDER, line, address)

    def test_the_one_address_is_an_origin_and_the_tool_takes_it_and_nothing_else(self):
        origin = stats.read("web", "SITE_ORIGIN")
        self.assertEqual(origin.count("\n"), 1)
        self.assertTrue(origin.endswith("\n"))
        self.assertEqual(seo_files.check_origin(origin.strip()), origin.strip())
        for bad in ("", "beta.playants.org", "ftp://example.org", "https://", "https://example.org/", "https://example.org/path", "https://example.org?x=1", "https://exa mple.org", "https://example.org;rm",
                    "https://example.org|x", "https://example.org\\", "https://example.org&", "https://user@example.org", "https://example.org:port", "https://example.org\n"):
            code, error, files = make(bad)
            self.assertEqual((code, files), (1, {}), bad)                         # (nothing is written for an address that is not an origin)
            self.assertIn("not an origin", error, bad)
        for good in ("https://example.org", "http://localhost:8080", "https://a-b.example.org:8443"):
            self.assertEqual(make(good)[0], 0, good)


class TheFiles(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        code, error, cls.files = make()
        assert code == 0, error
        cls.heads = {address: parse(text) for address, text in served_pages().items()}

    def test_the_sitemap_lists_exactly_the_pages_that_ask_to_be_listed_by_the_address_of_their_canonical_links(self):
        root = ET.fromstring(self.files["sitemap.xml"])
        self.assertEqual(root.tag, "{http://www.sitemaps.org/schemas/sitemap/0.9}urlset")
        locations = [e.find("{http://www.sitemaps.org/schemas/sitemap/0.9}loc").text for e in root]
        self.assertEqual(len(locations), len(set(locations)))
        canonicals = sorted(l["href"].replace(PLACEHOLDER, ORIGIN) for head in self.heads.values() for l in head.links if l.get("rel") == "canonical")
        self.assertEqual(sorted(locations), canonicals)
        self.assertEqual(sorted(l[len(ORIGIN):] for l in locations), sorted(LISTED))
        for location in locations:
            self.assertNotIn("?", location)
            self.assertNotIn("#", location)
        for address in NOT_LISTED:
            self.assertNotIn(ORIGIN + address, locations)

    def test_robots_txt_names_the_sitemap_by_the_same_address(self):
        self.assertEqual(re.findall(r"(?mi)^sitemap:\s*(\S+)\s*$", self.files["robots.txt"]), [ORIGIN + "/sitemap.xml"])
        self.assertEqual(re.findall(r"(?mi)^user-agent:\s*(\S+)\s*$", self.files["robots.txt"]), ["*"])

    def test_a_crawler_may_read_every_page_and_every_file_that_a_page_loads_to_draw_itself(self):
        robots = self.files["robots.txt"]
        for address in sorted(LISTED | NOT_LISTED) + ["/matches?q=ann", "/match?id=ants-X-20260101-000000Z", "/play", "/?vp", "/changelog.html", "/legal.html"]:
            self.assertTrue(crawler_may_fetch(robots, address), address)           # (a match page too: a crawler that may not read it never sees its noindex)
        for address, head in self.heads.items():
            for loaded in head.loads:
                if re.match(r"(?:[a-z]+:)?//", loaded):
                    continue
                path = loaded.split("?")[0].split("#")[0]
                path = path if path.startswith("/") else "/" + path
                self.assertTrue(crawler_may_fetch(robots, path), "%s loads %s" % (address, loaded))
        for address in ("/front/classic.css", "/home/home.js", "/home/home.css", "/site_nav.js", "/update_notice.js", "/app/icon-192.png", "/manifest.webmanifest", "/favicon.png", "/favicon.ico",
                        "/mapmaker_atlas/atlas.json", "/index.js", "/index.wasm", "/index.data", "/lvl_core.wasm", "/asset_catalog/", "/asset_catalog/sprites/sprite_0726.png", "/sprites/a.png",
                        "/sounds/a.wav", "/whats_new.json", "/sitemap.xml", "/robots.txt", "/front/preview_treasure.png"):
            self.assertTrue(crawler_may_fetch(robots, address), address)

    def test_a_crawler_may_not_read_a_door_of_the_game_server_or_the_chat_hub_or_an_address_that_holds_a_room_or_a_recording(self):
        robots = self.files["robots.txt"]
        for address in ("/hub/login", "/hub/callback", "/hub/ws", "/hub/me", "/hub/admin/api/x", "/hub/pic/%s.png" % ("a" * 32),
                        "/hub/img/%s.jpg" % ("a" * 32), "/ws", "/busy", "/stats", "/stats/local", "/live", "/live/ants-X-20260101-000000Z", "/replays", "/replays/ants-X-20260101-000000Z.antsrep",
                        "/history", "/history?limit=3", "/history/deaths", "/history/ants-X-20260101-000000Z", "/lobbies", "/maps", "/maps/all", "/maps/files/A.LVL", "/maps/previews/a.webp", "/submissions",
                        "/submissions/abc", "/test-maps", "/?room=k7m2xq", "/?join=/ws&room=abc&name=Pebble", "/?aspect=16:9&room=k7m2xq", "/play?live=ants-X-20260101-000000Z", "/play?code=abc",
                        "/play?replay=ants-X-20260101-000000Z.antsrep&embed=1"):
            self.assertFalse(crawler_may_fetch(robots, address), address)

    def test_robots_txt_does_not_name_the_admin_page(self):
        # (the page says noindex itself, and a file that every crawler and every visitor can read would only advertise its address: a rule of the product that no run of a page shows)
        self.assertNotIn("admin", self.files["robots.txt"].lower())
        self.assertNotIn("admin", self.files["sitemap.xml"].lower())

    def test_every_door_that_nginx_hands_to_another_program_is_closed_to_a_crawler(self):
        robots = self.files["robots.txt"]
        found = []
        for words in re.findall(r"^\s*location\s+((?:=|\^~)?\s*/\S*)\s*\{", CONF, re.M):
            if "proxy_pass" in stats.block_of(CONF, "location %s " % words):
                found.append(words)
        self.assertGreaterEqual(len(found), 10)                                    # (the doors are found: a change of the file's layout cannot make this test look at nothing)
        for words in found:
            path = words.split()[-1]
            if path.startswith("/admin"):
                continue                                                            # (the admin page's API: not named in a public file, see the next test)
            self.assertFalse(crawler_may_fetch(robots, path), words)
            if path.endswith("/"):
                self.assertFalse(crawler_may_fetch(robots, path + "x"), words)


if __name__ == "__main__":
    unittest.main()
