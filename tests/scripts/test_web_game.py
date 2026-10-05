#!/usr/bin/env python3
"""The game page in the front page's look (run by ./run_tests.sh --fast and by the CI). The owner: "This page layout didn't get updated". The game page (web/shell.html, served as /play.html, as "/"
for a link that somebody shared and as /?embed=1 inside the front page's frames) has the look of web/lobby.html, which is the look of the 1998 game's own menus: the clay, the thin green frame,
the teal bevelled buttons with their red shadow, the black inset boxes, the game's own font and the small "ants!" logo (web/front/). What needs no browser is read from the two files:

  - the colours are the front page's, token for token, and the rules of the buttons, the pairs, the frame, the black boxes, the progress bar and the footer are the front page's own text
  - the header: the logo (a link back to the menu that asks as Menu does, with the same code), the seven controls in their order, and "More" for a narrow window (a <details>, no script) with
    the other five
  - the loading screen (the logo, the same ids and messages, a teal bar), the failure cards (classes, not colours written into the script), the bottom controls (the same buttons and ids), the footer
    (the version and the build where the Dockerfile and the CI look for them), the embed mode (only the game) and the pictures that the page names
  - every text is at least 4.5:1 against its background, in every state of the buttons and boxes (the browser check, tests/scripts/web_home_check.py part game, measures the page itself)
The same things in a real browser at widths from 320 to 1600 px: tests/scripts/web_home_check.py --only game (opt-in, against a running page).
"""
import os
import re
import struct
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FRONT = os.path.join(REPO, "web", "front")
GITHUB = "https://github.com/dchadd427/ants-cross-platform"


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def style_of(page):
    return re.sub(r"/\*.*?\*/", "", page[page.index("<style>"):page.index("</style>")], flags=re.S)


def blocks(style):
    """Every innermost { } block of a style as (selector, {property: value}), blanks collapsed, in file order (a rule inside @media is listed too)."""
    out = []
    for found in re.finditer(r"([^{}]+)\{([^{}]*)\}", style):
        selector = " ".join(found.group(1).split())
        declarations = {}
        for part in found.group(2).split(";"):
            if ":" in part:
                name, value = part.split(":", 1)
                declarations[name.strip()] = " ".join(value.split())
        out.append((selector, declarations))
    return out


def rule(style, selector, nth=0):
    found = [d for s, d in blocks(style) if s == selector]
    if len(found) <= nth:
        raise AssertionError("no rule %r (number %d) in the style" % (selector, nth))
    return found[nth]


def png_size(path):
    with open(path, "rb") as f:
        head = f.read(24)
    return struct.unpack(">II", head[16:24])


class PageCase(unittest.TestCase):
    def setUp(self):
        self.page = read("web", "shell.html")
        self.lobby = read("web", "lobby.html")
        self.style = style_of(self.page)
        self.lobby_style = style_of(self.lobby)


class TheLookIsTheFrontPages(PageCase):
    TOKENS = ("clay", "ink", "teal", "teal-hi", "teal-lo", "edge", "cream", "inset", "frame", "frame-hi", "frame-lo", "shadow", "gold", "bad-bg", "bad-ink", "bad-edge")

    def test_the_colours_are_the_front_pages_token_for_token(self):
        ours = rule(self.style, ":root")
        theirs = rule(self.lobby_style, ":root")
        for name in self.TOKENS:
            self.assertIn("--" + name, ours, name)
            self.assertEqual(ours["--" + name], theirs["--" + name], name)

    def test_the_font_the_clay_and_the_frame_are_the_front_pages(self):
        face = re.search(r'@font-face \{[^}]*\}', self.style).group(0)
        self.assertEqual(" ".join(face.split()), " ".join(re.search(r'@font-face \{[^}]*\}', self.lobby_style).group(0).split()))
        preload = '<link rel="preload" href="front/LibreFranklin-Medium.ttf" as="font" type="font/ttf" crossorigin>'
        self.assertIn(preload, self.page)
        self.assertIn(preload, self.lobby)
        body, lobby_body = rule(self.style, "body"), rule(self.lobby_style, "body")
        self.assertEqual(body["background"], lobby_body["background"])
        self.assertEqual(body["font"], lobby_body["font"])
        self.assertEqual(body["font-synthesis"], "none")
        # the thin green frame with its red line: the same three rings, over the page (it takes no room)
        frame = rule(self.style, "body::after")
        self.assertEqual(frame["box-shadow"], rule(self.lobby_style, ".screen::after")["box-shadow"])
        self.assertEqual((frame["position"], frame["pointer-events"]), ("absolute", "none"))
        self.assertEqual(body["position"], "relative")

    def test_the_teal_button_is_the_front_pages(self):
        btn = rule(self.style, ".btn")
        theirs = rule(self.lobby_style, ".btn, .banner")
        for name in ("background", "border", "border-radius", "box-shadow"):
            self.assertEqual(btn[name], theirs[name], name)
        self.assertEqual(btn["color"], rule(self.lobby_style, ".btn, .banner")["color"])
        for name in ("min-height", "padding", "font-size"):
            self.assertEqual(btn[name], rule(self.lobby_style, ".btn")[name], name)
        for name in ("min-height", "padding", "font-size"):
            self.assertEqual(rule(self.style, ".btn.sm")[name], rule(self.lobby_style, ".btn.sm, .btn.small")[name], name)
        self.assertEqual(rule(self.style, ".btn:active")["box-shadow"], rule(self.lobby_style, ".btn:active")["box-shadow"])
        self.assertEqual(rule(self.style, ".btn:active")["transform"], rule(self.lobby_style, ".btn:active")["transform"])
        self.assertEqual(rule(self.style, ".btn:hover")["background"], rule(self.lobby_style, ".btn:hover")["background"])
        self.assertIn("outline: 3px solid var(--gold);", self.page)                                  # (the focused control shows it, as on the front page)
        self.assertRegex(self.style, r"a:focus-visible, button:focus-visible, input:focus-visible, summary:focus-visible \{")

    def test_the_banner_is_the_front_pages(self):
        banner, theirs = rule(self.style, ".banner"), rule(self.lobby_style, ".btn, .banner")
        for name in ("background", "border", "border-radius", "box-shadow", "color"):
            self.assertEqual(banner[name], theirs[name], name)

    def test_the_two_state_buttons_are_the_front_pages_pairs_with_the_chosen_one_pressed_in(self):
        button, label = rule(self.style, ".seg button"), rule(self.lobby_style, ".pair label")
        for name in ("min-width", "padding", "font-size", "line-height", "color", "background", "border", "border-radius", "box-shadow"):
            self.assertEqual(button[name], label[name], name)
        self.assertEqual(rule(self.style, ".seg")["gap"], rule(self.lobby_style, ".pair")["gap"])
        chosen, theirs = rule(self.style, '.seg button[aria-checked="true"]'), rule(self.lobby_style, ".pair input:checked + label")
        for name in ("color", "background", "transform", "box-shadow"):
            self.assertEqual(chosen[name], theirs[name], name)
        # the buttons are the same elements as ever (the page's script and the browser checks find them by these)
        for markup in ('id="aspect-16-9" data-aspect="16:9" aria-checked="true"', 'id="aspect-4-3" data-aspect="4:3" aria-checked="false"', 'id="lock-on" data-lock="on" aria-checked="true"',
                       'id="lock-off" data-lock="off" aria-checked="false"'):
            self.assertIn(markup, self.page)
        self.assertEqual(len(re.findall(r'<button type="button" role="radio" id="(?:aspect-16-9|aspect-4-3|lock-on|lock-off)"', self.page)), 4)
        self.assertEqual(len(re.findall(r'<div class="seg[ "][^>]*role="radiogroup"', self.page)), 2)

    def test_the_black_boxes_are_the_front_pages(self):
        theirs = rule(self.lobby_style, ".infobox")
        for selector in ("#info-panel", ".progress-bar-container"):
            ours = rule(self.style, selector)
            for name in ("border", "border-color", "border-radius", "box-shadow"):
                self.assertEqual(ours[name], theirs[name], selector + " " + name)
            self.assertEqual(ours["background"], theirs["background"], selector)
        field, theirs = rule(self.style, ".name-step input"), rule(self.lobby_style, "select, input[type=text]")
        for name in ("min-height", "padding", "color", "background", "border", "border-color", "border-radius", "box-shadow"):
            self.assertEqual(field[name], theirs[name], name)

    def test_the_footer_is_the_front_pages_emerald_bar(self):
        footer, theirs = rule(self.style, "footer.bar"), rule(self.lobby_style, ".bar")
        for name in ("color", "font-size", "background", "border-top", "box-shadow"):
            self.assertEqual(footer[name], theirs[name], name)
        self.assertEqual(rule(self.style, ".bar .ver")["background"], rule(self.lobby_style, ".bar .ver")["background"])
        self.assertEqual(rule(self.style, ".bar .grow")["flex"], rule(self.lobby_style, ".bar .grow")["flex"])

    def test_the_picture_has_the_frame_of_the_front_pages_pictures(self):
        ring = rule(self.style, "#game-container")["box-shadow"]
        for token in ("var(--edge)", "var(--frame)", "var(--shadow)"):
            self.assertIn(token, ring)
        shadows = re.findall(r"(\S+) (\S+) (\S+) (\S+) var\(--(\w+)\)", ring)
        self.assertEqual([(spread, colour) for _, _, _, spread, colour in shadows], [("3px", "edge"), ("6px", "frame"), ("8px", "edge"), ("8px", "shadow")])     # (black, green, black, and the red shadow)
        self.assertEqual(shadows[3][:2], ("4px", "6px"))
        # a shadow, not a border: the box has no frame inside it (the canvas fills it exactly)
        self.assertNotIn("border", rule(self.style, "#game-container"))
        self.assertIn("isolation", rule(self.style, "#game-container"))
        self.assertEqual(rule(self.style, "#game-container")["background"], "#000")


class TheHeader(PageCase):
    CONTROLS = (("menu-btn", "/", "Menu"), (None, "/asset_catalog/", "Sprites and sounds"), (None, "/changelog.html", "Changelog"), ("reset-btn", None, "Reset"),
                ("fullscreen-btn", None, "Fullscreen"), (None, GITHUB, "GitHub"), (None, GITHUB + "/issues", "Feedback"))

    def header(self):
        return self.page[self.page.index("<header>"):self.page.index("</header>")]

    def test_the_logo_is_a_link_to_the_front_page_in_this_tab(self):
        logo = re.search(r'<a href="/" id="logo-link" class="logo"([^>]*)>\s*<img src="front/logo\.png" alt="([^"]+)" width="581" height="218">\s*</a>', self.header())
        self.assertIsNotNone(logo)
        self.assertNotIn("target=", logo.group(1))
        self.assertEqual(png_size(os.path.join(FRONT, "logo.png")), (581, 218))
        self.assertLess(self.header().index('id="logo-link"'), self.header().index('id="menu-btn"'))          # (at the left of the header)
        self.assertEqual(len(re.findall(r"<h1[ >]", self.page)), 1)                                           # (the page keeps its heading: for a screen reader)

    def test_the_seven_controls_are_there_in_their_old_order_in_the_teal_style(self):
        row = self.header()[self.header().index('<div class="header-actions">'):self.header().index('<details class="more">')]
        found = re.findall(r'<(a|button) ([^>]*)>(.*?)</\1>', row, re.S)
        self.assertEqual(len(found), 7)
        for (tag, attrs, text), (ident, href, label) in zip(found, self.CONTROLS):
            self.assertIn('class="btn sm', attrs, label)
            self.assertEqual(re.sub(r"<[^>]+>", "", text) if label != "Fullscreen" else re.search(r'<span class="btn-text">([^<]*)</span>', text).group(1), label)
            if ident:
                self.assertIn('id="%s"' % ident, attrs, label)
            if href:
                self.assertIn('href="%s"' % href, attrs, label)
                if href != "/":
                    self.assertIn('target="_blank" rel="noopener noreferrer"', attrs, label)                      # (only the links that leave the game open a new tab)
                else:
                    self.assertNotIn("target=", attrs)
        # the five that "More" holds in a narrow window are the ones that the wide row marks
        self.assertEqual([("wide-only" in attrs) for _, attrs, _ in found], [False, True, True, True, False, True, True])
        self.assertIn('<span class="btn-text">Fullscreen</span><span class="btn-text-short">Full</span>', row)

    def test_more_holds_the_other_five_with_the_same_targets_and_needs_no_script(self):
        more = re.search(r'<details class="more">\s*<summary><span class="btn sm">More</span></summary>\s*<div class="more-list">(.*?)</div>\s*</details>', self.header(), re.S)
        self.assertIsNotNone(more)
        found = re.findall(r'<(a|button) ([^>]*)>(.*?)</\1>', more.group(1), re.S)
        wanted = [c for c in self.CONTROLS if c[2] in ("Sprites and sounds", "Changelog", "Reset", "GitHub", "Feedback")]
        self.assertEqual([text for _, _, text in found], [c[2] for c in wanted])
        row = self.header()[self.header().index('<div class="header-actions">'):self.header().index('<details class="more">')]
        for (tag, attrs, text), (_, href, label) in zip(found, wanted):
            self.assertIn('class="btn sm"', attrs, label)
            self.assertNotIn("id=", attrs, label)                                                               # (an id is the wide row's: there is one of each on the page)
            if href:
                self.assertIn('href="%s"' % href, attrs, label)
                self.assertIn('target="_blank" rel="noopener noreferrer"', attrs, label)
            else:
                self.assertIn('onclick="window.location.reload()"', attrs, label)
            title = re.search(r'title="([^"]*)"', attrs).group(1)
            self.assertIn('title="%s"' % title, row, label)                                                      # (the same tooltip as the wide row's)
        self.assertEqual(len(re.findall(r'\bid="([^"]+)"', self.page)), len(set(re.findall(r'\bid="([^"]+)"', self.page))), "an id is used twice")
        self.assertNotRegex(self.header(), r"<script|onclick=\"[^\"]*\.open")

    def test_a_wide_window_shows_the_links_and_a_narrow_one_the_logo_menu_fullscreen_and_more(self):
        self.assertEqual(rule(self.style, ".more")["display"], "none")
        narrow = self.style[self.style.index("@media (max-width: 700px) {"):self.style.index("@media (max-width: 440px) {")]
        self.assertRegex(narrow, r"\.wide-only \{\s*display: none;")
        self.assertRegex(narrow, r"\.more \{\s*display: block;")
        self.assertRegex(narrow, r"\.logo img \{\s*width: 88px;")
        self.assertEqual(self.style.count(".wide-only {"), 1, "the one rule that hides the wide row's five")
        # the label of Fullscreen is short where the row is tight, and the smallest windows get smaller buttons and a smaller logo
        self.assertIn("@media (max-width: 440px)", self.style)
        self.assertIn("@media (max-width: 380px)", self.style)

    def test_the_header_is_one_row_that_the_controls_share_with_the_logo(self):
        self.assertEqual(rule(self.style, ".header-actions")["flex"], "1 1 0")
        self.assertEqual(rule(self.style, ".header-actions")["min-width"], "0")
        self.assertNotIn("flex-wrap", rule(self.style, "header"))                                                # (the controls wrap among themselves, beside the logo)


class TheMenuAndTheLogoAskTheSameQuestion(PageCase):
    def test_the_logo_is_in_the_list_of_the_code_that_asks_and_the_question_is_in_one_place(self):
        self.assertIn("['logo-link', 'menu-btn', 'menu-link'].forEach(function (id) {", self.page)
        self.assertEqual(self.page.count("Leave the game and go back to the menu?"), 1)
        self.assertEqual(self.page.count("getElementById('logo-link')"), 0)
        self.assertEqual(len(re.findall(r"id=\"(?:logo-link|menu-btn|menu-link)\"", self.page)), 3)


class TheLoadingScreen(PageCase):
    def splash(self):
        return self.page[self.page.index('<div id="splash-overlay">'):self.page.index('<button type="button" id="pseudo-exit"')]

    def test_it_shows_the_logo_a_message_and_a_bar_with_the_same_ids(self):
        splash = self.splash()
        self.assertIn('<img class="splash-logo" src="front/logo.png" alt="ants!" width="581" height="218">', splash)
        self.assertNotIn("ANTS<", splash)
        self.assertNotIn("splash-title", self.page)
        for ident in ("status-text", "progress-container", "progress-fill"):
            self.assertEqual(len(re.findall(r'id="%s"' % ident, splash)), 1, ident)
        self.assertIn(">Loading game assets...</div>", splash)                                                    # (the first message; the others come from the script, unchanged)
        for message in ("'Downloading game data package...'", "'Starting the game...'", "'Downloading data ('", "'The download failed, trying again ('"):
            self.assertIn(message, self.page)

    def test_the_overlay_is_the_clay_and_the_bar_is_teal_in_a_black_box(self):
        self.assertEqual(rule(self.style, "#splash-overlay")["background"], 'var(--clay) url("front/clay.png")')
        fill = rule(self.style, ".progress-bar-fill")
        self.assertEqual(fill["background"], "var(--teal)")
        self.assertIn("var(--teal-lo)", fill["box-shadow"])
        self.assertEqual(rule(self.style, ".progress-bar-container")["background"], "var(--inset)")
        self.assertEqual(rule(self.style, ".progress-bar-container")["display"], "none")                         # (the script shows it while the data downloads)
        self.assertEqual(rule(self.style, ".splash-status")["color"], "var(--ink)")
        self.assertEqual(rule(self.style, "#splash-overlay.failed")["pointer-events"], "auto")                    # (a failure's button needs the clicks)
        self.assertEqual(rule(self.style, "#splash-overlay")["pointer-events"], "none")

    def test_the_failure_cards_are_classes_of_the_page_and_the_buttons_are_teal(self):
        script = self.page[self.page.index("function showCacheError()"):self.page.index("window.addEventListener('error'")]
        for old in ("#22c55e", "#f87171", "#fca5a5", "#cbd5e1", "rgba(220, 40, 40"):
            self.assertNotIn(old, script, old)
            self.assertNotIn(old, self.page, old)
        self.assertEqual(script.count("style.cssText"), 0)
        self.assertEqual(len(re.findall(r'class="btn sm"', script)) + len(re.findall(r"className = 'btn sm'", script)), 2)       # (Clear Cache & Update, Reload)
        for text in ("Browser Cache Mismatch Detected", "Clear Cache &amp; Update", "The game could not be started", "The game's data could not be downloaded", "Reload"):
            self.assertIn(text, script, text)
        self.assertIn("box.id = 'load-failure';", script)
        self.assertEqual(rule(self.style, ".load-failure")["background"], "var(--bad-bg)")
        self.assertEqual(rule(self.style, ".load-failure")["color"], "var(--bad-ink)")
        self.assertEqual(rule(self.style, "#splash-overlay.failed .splash-logo")["display"], "none")              # (the card needs the room: the box of a phone is small)


class TheFooter(PageCase):
    def test_the_version_and_the_build_are_where_the_dockerfile_and_the_ci_look_for_them(self):
        footer = self.page[self.page.index('<footer class="bar">'):self.page.index("</footer>")]
        self.assertIn('<span class="ver" id="game-version-line">Version&#8197;<span id="game-version">@@GAME_VERSION@@</span><span id="game-build">&#8197;-&#8197;build&#8197;<span id="game-build-id">@@BUILD_ID@@</span></span></span>', footer)
        self.assertIn("<span>@@SITE_FOOTER@@<span class=\"ver\"", footer)
        for placeholder in ("@@SITE_TITLE@@", "@@SITE_FOOTER@@", "@@GAME_VERSION@@", "@@BUILD_ID@@", "@@BUILD_TIMESTAMP@@", "@@DATA_SIZE@@"):
            self.assertIn(placeholder, self.page, placeholder)
        for placeholder in ("@@SITE_FOOTER@@", "@@GAME_VERSION@@", "@@BUILD_ID@@"):
            self.assertEqual(self.page.count(placeholder), 1, placeholder)
        self.assertIn("document.getElementById('game-version-line')", self.page)                                  # (a build that did not fill them in hides the line)
        self.assertEqual(rule(self.style, ".bar strong")["color"], "#fff")                                        # (the staging label: readable on the bar)

    def test_the_links_are_the_front_pages_and_the_menu_comes_first(self):
        nav = re.search(r'<nav aria-label="Footer links">(.*?)</nav>', self.page, re.S).group(1)
        found = re.findall(r'<a href="([^"]+)"([^>]*)>([^<]+)</a>', nav)
        self.assertEqual([(href, text) for href, _, text in found], [("/", "Menu"), ("/asset_catalog/", "Sprites and sounds"), ("/changelog.html", "Changelog"), (GITHUB, "GitHub"), (GITHUB + "/issues", "Feedback")])
        self.assertIn('id="menu-link"', found[0][1])
        self.assertNotIn("target=", found[0][1])
        lobby_nav = re.search(r'<nav aria-label="Footer links">(.*?)</nav>', self.lobby, re.S).group(1)
        self.assertEqual([text for _, _, text in re.findall(r'<a href="([^"]+)"([^>]*)>([^<]+)</a>', lobby_nav)], [text for _, _, text in found[1:]])


class TheEmbedModeIsOnlyTheGame(PageCase):
    def test_a_frame_of_the_front_page_has_no_header_footer_frame_or_margin(self):
        body = rule(self.style, "body.embed")
        self.assertEqual((body["padding"], body["min-height"], body["background"], body["overflow"]), ("0", "0", "#000", "hidden"))
        self.assertEqual(rule(self.style, "body.embed::after")["display"], "none")                                  # (the green frame is the page's, not the game's)
        gone = [s for s, d in blocks(self.style) if d.get("display") == "none !important"]
        self.assertEqual(len(gone), 1)
        for part in ("header", "footer", ".mobile-tip-banner", ".view-bar", ".info-panel-wrapper"):
            self.assertIn("body.embed " + part, gone[0], part)
        stage = rule(self.style, "body.embed #game-stage")
        self.assertEqual((stage["width"], stage["aspect-ratio"], stage["max-width"], stage["min-width"]), ("100vw", "auto", "none", "0"))
        self.assertEqual(rule(self.style, "body.embed #game-container")["box-shadow"], "none")                       # (no ring: the picture fills the frame)

    def test_the_page_still_says_that_it_is_embedded_by_adding_the_class_to_the_body(self):
        self.assertIn("if (ANTS_EMBED) document.body.classList.add('embed');", self.page)


class TheFullscreenStageIsStillBlack(PageCase):
    def test_the_bars_of_a_fullscreen_stage_are_black_and_the_ring_goes(self):
        stage = rule(self.style, "#game-stage:fullscreen, #game-stage:-webkit-full-screen, #game-stage.pseudo-fullscreen")
        self.assertEqual((stage["background"], stage["cursor"]), ("#000", "none"))
        ring = rule(self.style, "#game-stage:fullscreen #game-container, #game-stage:-webkit-full-screen #game-container, #game-stage.pseudo-fullscreen #game-container")
        self.assertEqual(ring["box-shadow"], "none")
        self.assertEqual(rule(self.style, "#game-stage.pseudo-fullscreen")["z-index"], "1000")                      # (over the page's frame, whose layer is lower)
        self.assertLess(int(rule(self.style, "body::after")["z-index"]), 1000)
        self.assertEqual(rule(self.style, "#game-stage.pseudo-fullscreen #pseudo-exit")["width"], "44px")


class ThePicturesAndTheFont(PageCase):
    def test_every_picture_the_page_names_is_in_the_folder_and_every_img_has_the_size_of_its_file(self):
        named = set(re.findall(r"front/([\w.-]+\.(?:png|ttf))", self.page))
        self.assertEqual(named, {"logo.png", "clay.png", "LibreFranklin-Medium.ttf"})
        for name in named:
            self.assertTrue(os.path.exists(os.path.join(FRONT, name)), name)
        tags = re.findall(r"<img [^>]*>", self.page)
        self.assertEqual(len(tags), 2)                                                                              # (the header's logo and the loading screen's)
        for tag in tags:
            source = re.search(r'src="front/([^"]+)"', tag).group(1)
            width, height = int(re.search(r'width="(\d+)"', tag).group(1)), int(re.search(r'height="(\d+)"', tag).group(1))
            self.assertEqual(png_size(os.path.join(FRONT, source)), (width, height), source)
            self.assertIn("alt=", tag)

    def test_nothing_is_loaded_from_another_site(self):
        self.assertNotRegex(self.page, r'<link[^>]*href="(?:https?:)?//')
        self.assertNotRegex(self.page, r"url\(\s*[\"']?(?:https?:)?//")
        self.assertNotIn("@import", self.page)
        self.assertNotRegex(self.page, r"<(?:img|iframe|video|audio|source)[^>]*src=\"(?:https?:)?//")
        for url in set(re.findall(r'href="(https?://[^"]+)"', self.page)):
            self.assertTrue(url.startswith(GITHUB), url)

    def test_the_page_has_no_emoji_and_no_symbol_font_glyph_in_its_controls(self):
        self.assertNotRegex(self.page, "[\U0001F300-\U0001FAFF☀-➿]")
        self.assertNotIn("&#x26F6;", self.page)


class TheColours(PageCase):
    """The text of the page is at least 4.5:1 against its background, in every state of the buttons and boxes (the browser check measures the whole page; this reads the colours)."""

    CLAY_ENDS = ("#db4b13", "#fb335b")                      # the two ends of the clay tile that the front page's check measures against
    BAR_ENDS = ("#2b685f", "#2b6b4f")                       # the ends of the footer's gradient

    def token(self, name):
        return rule(self.style, ":root")["--" + name]

    @staticmethod
    def luminance(colour):
        def channel(v):
            v /= 255.0
            return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
        r, g, b = (int(colour[i:i + 2], 16) for i in (1, 3, 5))
        return 0.2126 * channel(r) + 0.7152 * channel(g) + 0.0722 * channel(b)

    def ratio(self, one, other):
        high, low = sorted((self.luminance(one), self.luminance(other)), reverse=True)
        return (high + 0.05) / (low + 0.05)

    def test_every_pair_of_text_and_background_on_the_page(self):
        cream, ink, teal, inset, gold = self.token("cream"), self.token("ink"), self.token("teal"), self.token("inset"), self.token("gold")
        pairs = [("cream on a teal button or banner", cream, teal), ("cream on a hovered button", cream, self.token("teal-hi")), ("the chosen button's gold", "#f6dc86", "#102b25"),
                 ("gold on the black boxes (the guide's headings, the tip)", gold, inset), ("the guide's labels", "#a6e8c6", inset), ("the guide's notes", "#b9cfc5", inset),
                 ("the guide's text", cream, inset), ("a key", cream, "#0e1a17"), ("the tip's white words", "#ffffff", inset), ("the name field's hint", "#7f9c92", inset),
                 ("the field's text", cream, inset), ("the notice", self.token("bad-ink"), self.token("bad-bg")), ("the notice's title", "#ffffff", self.token("bad-bg")),
                 ("the version", "#ffffff", self.token("edge")), ("the staging label on the bar", "#ffffff", self.BAR_ENDS[0]), ("gold on the banner of the name step", gold, teal)]
        for what, text, background in pairs:
            self.assertGreaterEqual(self.ratio(text, background), 4.5, "%s: %s on %s" % (what, text, background))
        for end in self.CLAY_ENDS:                                                                                  # (the labels, the loading message and the name step's words are ink on the clay tile)
            self.assertGreaterEqual(self.ratio(ink, end), 4.5, "ink on clay %s" % end)
        for end in self.BAR_ENDS:
            self.assertGreaterEqual(self.ratio(cream, end), 4.5, "cream on the footer %s" % end)

    def test_the_colours_written_in_the_rules_are_the_ones_tested(self):
        for selector, value in ((".info-col li strong", "#a6e8c6"), (".info-col li em", "#fff"), (".info-col li.note", "#b9cfc5")):
            self.assertIn(value, [d.get("color") for s, d in blocks(self.style) if s == selector])
        self.assertEqual(rule(self.style, "kbd")["background"], "#0e1a17")
        self.assertEqual(rule(self.style, ".mobile-tip-banner")["color"], "var(--gold)")
        self.assertEqual(rule(self.style, ".mobile-tip-banner")["background"], "var(--inset)")


if __name__ == "__main__":
    unittest.main()
