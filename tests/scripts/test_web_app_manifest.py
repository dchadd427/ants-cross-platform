#!/usr/bin/env python3
"""The installable web app (web/manifest.webmanifest, the icons of web/app/, the head lines of the pages; docs/INSTALL_AS_APP.md): run by ./run_tests.sh --fast and by the CI.

What this holds it to, without a browser (the nginx side, the type, the caching and the headers, is tests/scripts/test_nginx_headers.py; tests/data/web_app_manifest_mutants.json breaks one rule at a time):
  - the manifest is valid JSON with the fields that make a browser offer to install (a name and short name "Ants!", an id, start address and scope "/", display standalone, the theme color of the pages'
    frame and the orange of the menus as background, categories) and no field that the site does not want (no orientation: the game page is sideways and the front page is upright);
  - every icon that it names (and every shortcut's) is a file of web/app/ of the size it states, named with ?v= and the first 8 hex digits of the file's sha256 (a browser keeps a .png for a week),
    one is `maskable` and none is both; the Apple icon is 180 x 180 and has no transparency, and so has the maskable icon (the system cuts it); web/app/ holds those four files and no other;
  - the same four head lines (the manifest, the theme color, the Apple icon, the name under the icon of an iPhone) on every page of the site but the admin page, and identical on each;
  - the shortcuts are addresses of pages that exist; no file of the site registers a service worker (the installed app always loads the current site);
  - the byte budgets of tools/size_limits.py, that git does not ignore the icons, and that the content of the maskable icon stays inside its safe circle (computed from the file).
"""
import glob
import hashlib
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

try:
    from PIL import Image
except ImportError:
    Image = None                   # (the runners of CI have no Pillow: the tests that run the icon tool are skipped there)

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools"))
from size_limits import APP_ICONS_BUDGET, APP_MANIFEST_BUDGET, FAVICON_BUDGET  # noqa: E402  (every byte budget: tools/size_limits.py)

THEME = "#2b5f43"                  # the outermost line of the pages' green frame (web/lobby.html)
BACKGROUND = "#db4b13"             # the orange of the game's menus (web/front/clay.png is this with a little noise)
PAGES = ["web/shell.html", "web/lobby.html", "web/matches.html", "web/match.html", "web/legal.html", "web/mapmaker.html", "asset_catalog/index.html"]   # (and the changelog page, made by a tool)
SHORTCUT_PAGES = {"/matches": "web/matches.html", "/mapmaker": "web/mapmaker.html", "/?vp": "web/lobby.html"}
ICON_FILES = {"icon-192.png": 192, "icon-512.png": 512, "icon-maskable-512.png": 512, "apple-touch-icon.png": 180}


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def raw(*parts):
    with open(os.path.join(REPO, *parts), "rb") as f:
        return f.read()


def changelog_page():
    sys.path.insert(0, os.path.join(REPO, "tools"))
    try:
        import changelog_to_html
    finally:
        sys.path.pop(0)
    return changelog_to_html.convert(read("CHANGELOG.md"), changelog_to_html.DEFAULT_REPO_BLOB, version="v0.0.0", build_id="0000000")


def png(path):
    """(width, height, has transparency) of a PNG file, read from its chunks."""
    data = raw(*path.split("/"))
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    width, height, depth, kind = struct.unpack(">IIBB", data[16:26])
    transparent = kind in (4, 6)
    at = 8
    while at < len(data):
        length, name = struct.unpack(">I4s", data[at:at + 8])
        if name == b"tRNS":
            transparent = True
        at += 12 + length
    return width, height, transparent


def png_pixels(path):
    """(width, height, [palette index of every pixel]) of an 8 bit palette PNG without interlacing, read with zlib alone."""
    data = raw(*path.split("/"))
    width, height, depth, kind, _, _, interlace = struct.unpack(">IIBBBBB", data[16:29])
    assert (depth, kind, interlace) == (8, 3, 0), path
    packed, at = b"", 8
    while at < len(data):
        length, name = struct.unpack(">I4s", data[at:at + 8])
        if name == b"IDAT":
            packed += data[at + 8:at + 8 + length]
        at += 12 + length
    lines, out, prev = zlib.decompress(packed), [], bytearray(width)
    for row in range(height):
        kind_of_filter, line = lines[row * (width + 1)], bytearray(lines[row * (width + 1) + 1:(row + 1) * (width + 1)])
        for x in range(width):
            left, up, upleft = line[x - 1] if x else 0, prev[x], prev[x - 1] if x else 0
            if kind_of_filter == 1:
                line[x] = (line[x] + left) & 255
            elif kind_of_filter == 2:
                line[x] = (line[x] + up) & 255
            elif kind_of_filter == 3:
                line[x] = (line[x] + (left + up) // 2) & 255
            elif kind_of_filter == 4:
                guess = left + up - upleft
                best = min((abs(guess - left), left), (abs(guess - up), up), (abs(guess - upleft), upleft))[1]
                line[x] = (line[x] + best) & 255
        out.extend(line)
        prev = line
    return width, height, out


def digits(path):
    return hashlib.sha256(raw(*path.split("/"))).hexdigest()[:8]


def head_lines(text):
    """The head lines of the web app in a page, as written (stripped)."""
    head = text[text.index("<head"):text.index("</head>")]
    return [line.strip() for line in head.splitlines() if re.search(r'rel="manifest"|name="theme-color"|rel="apple-touch-icon"|apple-mobile-web-app-title', line)]


MANIFEST_TEXT = read("web", "manifest.webmanifest")
MANIFEST = json.loads(MANIFEST_TEXT)
WANT_HEAD = ['<link rel="manifest" href="/manifest.webmanifest">', '<meta name="theme-color" content="%s">' % THEME,
             '<link rel="apple-touch-icon" href="/app/apple-touch-icon.png?v=%s">' % digits("web/app/apple-touch-icon.png"), '<meta name="apple-mobile-web-app-title" content="Ants!">']


class TheManifest(unittest.TestCase):
    def test_it_names_the_app_and_where_it_starts_and_how_it_looks(self):
        m = MANIFEST
        self.assertEqual((m["name"], m["short_name"]), ("Ants!", "Ants!"))
        self.assertEqual((m["id"], m["start_url"], m["scope"]), ("/", "/", "/"))
        self.assertEqual(m["display"], "standalone")
        self.assertEqual(m["theme_color"], THEME)
        self.assertEqual(m["background_color"], BACKGROUND)
        self.assertEqual(m["categories"], ["games", "strategy"])
        self.assertTrue(m["description"].strip())
        self.assertEqual(m["lang"], "en")

    def test_it_has_nothing_the_site_does_not_want(self):
        for field in ("orientation", "display_override", "related_applications", "prefer_related_applications", "serviceworker", "screenshots", "protocol_handlers", "file_handlers", "share_target"):
            self.assertNotIn(field, MANIFEST, field)
        self.assertTrue(MANIFEST_TEXT.isascii())
        self.assertNotIn("http", MANIFEST_TEXT)                                      # (every address is of this site: a path)

    def test_every_icon_is_a_file_of_the_size_it_states_and_is_named_with_the_digits_of_its_file(self):
        for icon in MANIFEST["icons"] + [i for s in MANIFEST["shortcuts"] for i in s["icons"]]:
            src, _, query = icon["src"].partition("?v=")
            self.assertTrue(src.startswith("/app/"), icon)
            width, height, _ = png("web" + src)
            self.assertEqual(icon["sizes"], "%dx%d" % (width, height), icon)
            self.assertEqual(icon["type"], "image/png")
            self.assertEqual(query, digits("web" + src), "%s: put the first 8 digits of its sha256 after ?v=" % src)

    def test_there_are_the_two_sizes_every_browser_wants_and_one_maskable_icon_that_is_not_also_plain(self):
        icons = MANIFEST["icons"]
        plain = {i["sizes"] for i in icons if i.get("purpose", "any") == "any"}
        self.assertTrue({"192x192", "512x512"} <= plain)
        masked = [i for i in icons if "maskable" in i.get("purpose", "")]
        self.assertEqual(len(masked), 1)
        self.assertEqual(masked[0]["purpose"], "maskable")                           # (not "any maskable": the plain icon has corners and a frame, the maskable one is orange to every edge)
        self.assertEqual(masked[0]["sizes"], "512x512")
        self.assertEqual(masked[0]["src"].split("?")[0], "/app/icon-maskable-512.png")

    def test_the_icons_that_the_system_cuts_have_no_transparency_and_the_apple_icon_is_180(self):
        for name in ("icon-maskable-512.png", "apple-touch-icon.png"):
            self.assertFalse(png("web/app/" + name)[2], name + " has transparency: the system fills it with black")
        self.assertEqual(png("web/app/apple-touch-icon.png")[:2], (180, 180))
        self.assertTrue(png("web/app/icon-512.png")[2], "the plain icons have transparent corners")

    def test_the_folder_holds_the_four_icons_and_no_other_file(self):
        self.assertEqual(sorted(os.listdir(os.path.join(REPO, "web", "app"))), sorted(ICON_FILES))
        for name, size in ICON_FILES.items():
            self.assertEqual(png("web/app/" + name)[:2], (size, size), name)

    def test_the_shortcuts_are_pages_that_exist(self):
        urls = {s["url"] for s in MANIFEST["shortcuts"]}
        self.assertEqual(urls, set(SHORTCUT_PAGES))
        nginx = read("docker", "nginx.conf")
        for url, page in SHORTCUT_PAGES.items():
            self.assertTrue(os.path.isfile(os.path.join(REPO, page)), page)
            self.assertIn("location = %s {" % url.split("?")[0], nginx, url)
            self.assertTrue(re.match(r"^[A-Za-z]", [s for s in MANIFEST["shortcuts"] if s["url"] == url][0]["short_name"]))
        for shortcut in MANIFEST["shortcuts"]:
            self.assertTrue(shortcut["name"] and shortcut["short_name"])

    def test_the_budgets(self):
        self.assertLessEqual(sum(os.path.getsize(os.path.join(REPO, "web", "app", n)) for n in ICON_FILES), APP_ICONS_BUDGET)
        self.assertLessEqual(len(MANIFEST_TEXT.encode("utf-8")), APP_MANIFEST_BUDGET)
        self.assertLessEqual(sum(os.path.getsize(os.path.join(REPO, "web", n)) for n in ("favicon.png", "favicon.ico")), FAVICON_BUDGET)


class TheHeadLines(unittest.TestCase):
    def test_every_page_but_the_admin_page_has_the_same_four_lines_once(self):
        pages = {name: read(*name.split("/")) for name in PAGES}
        pages["the changelog page"] = changelog_page()
        self.assertEqual(len(pages), 8)
        for name, text in pages.items():
            self.assertEqual(head_lines(text), WANT_HEAD, name)

    def test_the_admin_page_has_none_of_them(self):
        text = read("web", "admin.html")
        self.assertEqual(head_lines(text), [])
        self.assertNotIn("manifest", text)

    def test_no_other_html_page_of_web_is_left_out(self):
        known = {os.path.basename(p) for p in PAGES} | {"admin.html"}
        for path in glob.glob(os.path.join(REPO, "web", "*.html")):
            self.assertIn(os.path.basename(path), known, "%s is a page without the head lines of the web app (add it to PAGES, or to the admin page's side)" % path)


FAVICON_LINKS = re.compile(r'<link rel="(?:shortcut )?icon"[^>]*href="(/favicon\.(?:png|ico))(?:\?v=([0-9a-f]{8}))?"')


class TheFavicon(unittest.TestCase):
    def test_the_tab_icon_is_a_32_pixel_png_with_transparency_and_an_ico_of_three_sizes(self):
        width, height, transparent = png("web/favicon.png")
        self.assertEqual((width, height, transparent), (32, 32, True))
        ico = raw("web", "favicon.ico")
        reserved, kind, count = struct.unpack("<HHH", ico[:6])
        self.assertEqual((reserved, kind), (0, 1))
        sizes = sorted((ico[6 + 16 * i] or 256) for i in range(count))
        self.assertEqual(sizes, [16, 32, 48])

    def test_every_page_with_a_tab_icon_links_it_with_the_digits_of_its_file_and_names_no_other_file(self):
        pages = {name: read(*name.split("/")) for name in PAGES + ["web/admin.html"]}
        pages["the changelog page"] = changelog_page()
        linked = 0
        for name, text in pages.items():
            for path, version in FAVICON_LINKS.findall(text):
                linked += 1
                self.assertEqual(version, digits("web" + path), "%s: %s has stale digits (or none)" % (name, path))
            self.assertEqual(len(re.findall(r'rel="(?:shortcut )?icon"', text)), len(FAVICON_LINKS.findall(text)), "%s: an icon link that is not /favicon.png or /favicon.ico with ?v=" % name)
        self.assertGreaterEqual(linked, 9)
        for name in ("web/shell.html", "web/match.html", "web/matches.html"):
            self.assertEqual([p for p, v in FAVICON_LINKS.findall(pages[name])], ["/favicon.png", "/favicon.ico"], name)

    def test_the_image_copies_the_favicons_and_the_icons(self):
        dockerfile = read("Dockerfile")
        self.assertIn("COPY web/favicon.* /usr/share/nginx/html/", dockerfile)
        self.assertRegex(dockerfile, r"COPY web/app/? /usr/share/nginx/html/app/")


@unittest.skipUnless(Image, "Pillow is needed")
class TheIconTool(unittest.TestCase):
    def test_check_passes_and_the_tool_writes_the_committed_pictures_again(self):
        with tempfile.TemporaryDirectory() as app, tempfile.TemporaryDirectory() as fav:
            done = subprocess.run([sys.executable, "-I", os.path.join(REPO, "tools", "make_app_icons.py"), "--out", app, "--favicon-dir", fav, "--check"], capture_output=True, text=True, timeout=300)
            self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
            self.assertEqual(sorted(os.listdir(app)), sorted(ICON_FILES))
            self.assertEqual(sorted(os.listdir(fav)), ["favicon.ico", "favicon.png"])
            for folder, name, committed in ((app, "icon-192.png", "web/app"), (app, "icon-512.png", "web/app"), (app, "icon-maskable-512.png", "web/app"), (app, "apple-touch-icon.png", "web/app"),
                                            (fav, "favicon.png", "web"), (fav, "favicon.ico", "web")):
                with Image.open(os.path.join(folder, name)) as a, Image.open(os.path.join(REPO, committed, name)) as b:
                    made, kept = a.convert("RGBA"), b.convert("RGBA")
                self.assertEqual(made.size, kept.size, name)
                self.assertEqual(made.tobytes(), kept.tobytes(), name + " differs from the committed picture")

    def test_the_tool_names_no_path_of_anybodys_machine_and_reads_no_picture(self):
        text = read("tools", "make_app_icons.py")
        self.assertNotIn("/home/", text)
        self.assertNotIn("Image.open(", text.split("def check")[0])


class NoServiceWorker(unittest.TestCase):
    def test_nothing_registers_a_service_worker_and_the_site_names_no_worker_file(self):
        found = []
        for folder in ("web", "asset_catalog", "tools", "docker"):
            for path in glob.glob(os.path.join(REPO, folder, "**", "*"), recursive=True):
                if not os.path.isfile(path) or not path.endswith((".html", ".js", ".py", ".conf", ".json", ".webmanifest")) or path.endswith("test_web_app_manifest.py"):
                    continue
                with open(path, encoding="utf-8", errors="replace") as f:
                    text = f.read()
                if re.search(r"serviceWorker\s*\.\s*register|navigator\.serviceWorker|\"serviceworker\"|importScripts\(\s*[\"']/?(?:sw|service-worker)", text, re.I):
                    found.append(os.path.relpath(path, REPO))
        self.assertEqual(found, [])
        self.assertEqual(glob.glob(os.path.join(REPO, "web", "sw.js")) + glob.glob(os.path.join(REPO, "web", "service-worker.js")), [])
        self.assertNotIn("Service-Worker-Allowed", read("docker", "nginx.conf"))


class TheIconFiles(unittest.TestCase):
    def test_git_keeps_the_icons(self):
        if not shutil.which("git") or subprocess.run(["git", "rev-parse", "--git-dir"], cwd=REPO, capture_output=True).returncode != 0:
            self.skipTest("not a git checkout")
        for name in ICON_FILES:
            done = subprocess.run(["git", "check-ignore", "-q", "web/app/" + name], cwd=REPO)
            self.assertEqual(done.returncode, 1, "git ignores web/app/%s (a rule of .gitignore hides it from a commit)" % name)

    def test_the_content_of_the_maskable_icon_stays_inside_the_safe_circle_of_80_percent(self):
        width, height, pixels = png_pixels("web/app/icon-maskable-512.png")
        self.assertEqual((width, height), (512, 512))
        ground = pixels[0]
        self.assertEqual({pixels[0], pixels[width - 1], pixels[width * (height - 1)], pixels[-1]}, {ground})      # (orange in every corner)
        centre = (width - 1) / 2
        farthest = max(math.hypot(i % width - centre, i // width - centre) for i, p in enumerate(pixels) if p != ground)
        self.assertLessEqual(farthest, 0.40 * width, "the word or its shadow reaches %.1f px from the middle: Android may cut it (safe radius %.1f)" % (farthest, 0.40 * width))
        self.assertGreater(farthest, 0.85 * 0.40 * width, "the word is much smaller than the safe circle allows: it is hard to read at 48 px")


if __name__ == "__main__":
    unittest.main()
