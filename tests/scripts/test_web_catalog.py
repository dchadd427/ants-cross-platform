#!/usr/bin/env python3
"""Sprites and sounds (asset_catalog/index.html, served at /asset_catalog/) in the Classic look (run by ./run_tests.sh --fast and by the CI).

The page is a catalog and inspector of the game's assets with a lot of script. Its look was moved to the Classic look of the front page (the shared stylesheet /front/classic.css, the logo
in the header, a Play button to the front page) by changing only the head's two links, the style block and the header: the markup and the script after the header are as they were. What is
pinned here is what that must not break and what it promised:

  - the head loads the font and the shared stylesheet before the page's own style, and nothing else (the script file of the data, and nothing from another site)
  - the header: the logo is a link to the front page (its size is the picture's), the page's banner, the three tabs with the ids and the calls that the script uses, Play first among the buttons
  - every element that the script asks for by a fixed id is in the page (a rewritten header cannot lose one)
  - the style block makes no colours of its own that the stylesheet has, keeps none of the old dark blue palette, leaves the frame its room, keeps the page's own phone rules (the page
    scrolls as a document, a table scrolls inside its box, the inspector's two columns stand one above the other) and overrides the colours that the markup and the script give to buttons
  - the text colours that the style pairs are at least 4.5 to 1 (the browser check measures the whole page; this reads the colours)
"""
import os
import re
import struct
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


PAGE = read("asset_catalog", "index.html")
STYLE = PAGE[PAGE.index("<style>") + len("<style>"):PAGE.index("</style>")]
HEAD = PAGE[:PAGE.index("<style>")]
HEADER = PAGE[PAGE.index("<header"):PAGE.index("</header>") + len("</header>")]
AFTER_HEADER = PAGE[PAGE.index("</header>"):]
SCRIPT = PAGE[PAGE.index("<script>"):PAGE.rindex("</script>")]
SHARED = read("web", "front", "classic.css")


def png_size(path):
    with open(path, "rb") as f:
        head = f.read(24)
    return struct.unpack(">II", head[16:24])


class TheHead(unittest.TestCase):
    def test_the_font_and_the_shared_stylesheet_come_before_the_pages_own_style(self):
        font = HEAD.index('<link rel="preload" href="/front/LibreFranklin-Medium.ttf" as="font" type="font/ttf" crossorigin>')
        sheet = HEAD.index('<link rel="stylesheet" href="/front/classic.css">')
        self.assertLess(font, sheet)
        self.assertLess(sheet, len(HEAD))                                                # (both are in the head, which ends where the style begins)
        for name in ("LibreFranklin-Medium.ttf", "classic.css"):
            self.assertTrue(os.path.exists(os.path.join(REPO, "web", "front", name)), name)

    def test_nothing_is_loaded_but_the_data_the_font_and_the_stylesheet(self):
        self.assertEqual(re.findall(r'<script[^>]*\bsrc="([^"]+)"', PAGE), ["catalog_data.js"])
        self.assertEqual(re.findall(r'<link[^>]*\bhref="([^"]+)"', PAGE), ["/front/LibreFranklin-Medium.ttf", "/front/classic.css"])
        self.assertNotIn("@import", STYLE)
        self.assertNotRegex(STYLE, r"url\(\s*[\"']?(?:https?:)?//")
        self.assertNotRegex(PAGE, r"<(?:img|iframe|video|audio|source)[^>]*src=\"(?:https?:)?//")


class TheHeader(unittest.TestCase):
    def test_the_logo_is_a_link_to_the_front_page_and_has_the_size_of_its_picture(self):
        link = re.search(r'<a class="logo" href="/"[^>]*><img src="/front/logo\.png" alt="ants!" width="(\d+)" height="(\d+)"></a>', HEADER)
        self.assertIsNotNone(link)
        self.assertEqual((int(link.group(1)), int(link.group(2))), png_size(os.path.join(REPO, "web", "front", "logo.png")))

    def test_the_banner_the_three_tabs_and_the_buttons(self):
        self.assertEqual(re.findall(r"<h1 class=\"banner gold\">(.*?)</h1>", HEADER), ["Ants Asset Catalog &amp; Inspector"])
        tabs = re.findall(r'<button class="tab-btn btn sm( active)?" data-tab="(\w+)" onclick="switchTab\(\'(\w+)\'\)">', HEADER)
        self.assertEqual([(t[1], t[2], t[0]) for t in tabs], [("sounds", "sounds", " active"), ("anims", "anims", ""), ("sprites", "sprites", "")])      # (the script's names; the first is chosen)
        for ident in ("tab-sounds-count", "tab-anims-count", "tab-sprites-count"):
            self.assertIn('<span id="%s">' % ident, HEADER)
        actions = HEADER[HEADER.index('<div class="header-actions">'):]
        self.assertTrue(actions.lstrip().startswith('<div class="header-actions">\n      <a href="/" class="btn sm" title="Return to Play Ants Game">Play</a>'), "Play, a button to the front page, comes first")
        self.assertIn('href="https://github.com/dchadd427/ants-cross-platform" target="_blank" rel="noopener noreferrer" class="btn sm"', actions)
        self.assertIn('<button class="btn sm" id="btn-shortcuts-help"', actions)


class TheScriptAndTheMarkup(unittest.TestCase):
    def test_every_element_that_the_script_asks_for_by_a_fixed_id_is_in_the_page(self):
        wanted = set(re.findall(r"getElementById\('([\w-]+)'\)", SCRIPT))
        self.assertGreater(len(wanted), 40)
        present = set(re.findall(r'\bid="([\w-]+)"', PAGE))
        self.assertEqual(sorted(wanted - present), [], "the script asks for elements that the page does not have")

    def test_the_tabs_are_found_by_the_class_and_the_name_that_the_script_uses(self):
        self.assertIn("querySelectorAll('.tab-btn')", SCRIPT)
        self.assertIn("btn.dataset.tab === tabName", SCRIPT)
        self.assertIn("view.id === tabName + '-view'", SCRIPT)
        for view in ("sounds-view", "anims-view", "sprites-view"):
            self.assertIn('id="%s" class="tab-content' % view, AFTER_HEADER)

    def test_the_stage_is_measured_by_the_script_and_so_keeps_its_own_box(self):
        rule = re.search(r"\.canvas-stage \{([^}]*)\}", STYLE).group(1)
        for needle in ("position: relative", "overflow: hidden", "touch-action: none", "cursor: grab", "height: 520px", "min-height: 380px"):
            self.assertIn(needle, rule, needle)


class TheStyle(unittest.TestCase):
    def test_it_makes_no_colours_of_the_stylesheets_and_keeps_none_of_the_old_blue_palette(self):
        self.assertNotRegex(STYLE, r"--(?:clay|ink|teal|teal-hi|teal-lo|edge|cream|inset|frame|frame-hi|frame-lo|shadow|gold|mint|muted|rule|pressed)\s*:")      # (the page's own names are aliases of them)
        for old in ("0b0f19", "070a12", "131d31", "1e2c47", "2a3b5c", "38bdf8", "0ea5e9", "94a3b8", "64748b", "1e3a8a", "0f172a", "0d1527", "334155", "0284c7", "1e293b"):
            self.assertNotIn("#" + old, STYLE.lower(), "#%s is of the old palette" % old)

    def test_the_green_frame_has_its_room_and_no_scrollbar_runs_under_it(self):
        self.assertIn("body::after { content: \"\"; position: absolute; inset: 0; pointer-events: none; z-index: 20; box-shadow: var(--screen-frame); }", STYLE)
        body = re.search(r"\n  body \{([^}]*)\}", STYLE).group(1)
        self.assertRegex(body, r"padding: 10px;")
        self.assertIn("position: relative", body)
        self.assertIn("--screen-frame:", SHARED)

    def test_the_page_scrolls_as_a_document_on_a_phone_and_keeps_its_own_responsive_rules(self):
        phone = STYLE[STYLE.index("@media (max-width: 768px)"):]
        for needle in ("overflow-y: auto !important;", "height: auto !important;", ".table-wrapper {\n      overflow-x: auto;", "#anims-split-container { flex-direction: column;",
                       ".sprites-grid {\n      grid-template-columns: repeat(auto-fill, minmax(104px, 1fr));", ".meta-grid {\n      grid-template-columns: 1fr;", ".search-input-wrapper { flex: 1 1 100%; }",
                       ".stage-hint-pill { display: none; }"):
            self.assertIn(needle, phone, needle)
        self.assertIn(".header-actions { grid-column: 2; grid-row: 1; justify-content: flex-end; gap: 8px 10px; }", phone)         # (Play stays on a phone: the old rules hid every button)
        self.assertNotRegex(phone, r"\.header-actions \{\s*display: none;")

    def test_the_colours_that_the_markup_and_the_script_give_to_buttons_are_overridden(self):
        self.assertRegex(STYLE, r"\.copy-btn \{[^}]*color: var\(--cream\) !important; border-color: var\(--edge\) !important;")        # (the links to sounds, animations and sprites)
        self.assertIn(".copy-btn.active { color: var(--pressed-ink) !important; }", STYLE)
        self.assertIn("color:#34d399", SCRIPT.replace(" ", ""))                                                                           # (the script still has them: it is not touched)
        self.assertRegex(STYLE, r"\.sprites-toolbar \{[^}]*background: var\(--inset\) !important;")
        self.assertRegex(STYLE, r"#btn-load-all \{ background: var\(--teal-hi\) !important; \}")
        self.assertRegex(STYLE, r"\.stage-select \{[^}]*padding-right: 30px !important;")

    def test_sprites_stay_on_a_dark_backdrop_never_on_the_clay(self):
        for selector in (".canvas-stage.bg-checker", ".canvas-stage.bg-black", ".canvas-stage.bg-gray", ".canvas-stage.bg-green", ".sprite-preview-box", ".anim-thumb-box", ".subitem-thumb", ".modal-preview-stage"):
            rule = re.search(re.escape(selector) + r" \{([^}]*)\}", STYLE)
            self.assertIsNotNone(rule, selector)
            self.assertNotIn("clay", rule.group(1), selector)
        checker = re.search(r"\.canvas-stage\.bg-checker \{([^}]*)\}", STYLE).group(1)
        self.assertIn("linear-gradient(45deg", checker)                                                                                   # (a checker: a transparent sprite shows its shape)


class ThePairs(unittest.TestCase):
    """The text colours that the style pairs, read from the style and the stylesheet (aliases and var() resolved)."""

    @classmethod
    def setUpClass(cls):
        cls.tokens = {m.group(1): m.group(2).strip() for m in re.finditer(r"--([a-z-]+):\s*([^;]+);", SHARED)}
        cls.tokens.update({m.group(1): m.group(2).strip() for m in re.finditer(r"--([a-z-]+):\s*([^;/]+);", STYLE[:STYLE.index("html, body")])})
        flat = re.sub(r"/\*.*?\*/", "", STYLE, flags=re.S)
        cls.rules = [(re.sub(r"\s+", " ", m.group(1).strip()), m.group(2)) for m in re.finditer(r"([^{}@]+)\{([^{}]*)\}", flat)]

    def colour(self, value):
        value = value.strip().replace("!important", "").strip()
        var = re.fullmatch(r"var\(--([a-z-]+)\)", value)
        if var:
            return self.colour(self.tokens[var.group(1)])
        if re.fullmatch(r"#[0-9a-fA-F]{3}", value):
            value = "#" + "".join(c * 2 for c in value[1:])
        self.assertRegex(value, r"#[0-9a-fA-F]{6}", value)
        return value.lower()

    def declared(self, selector, prop):
        for name, body in self.rules:
            if name == selector:
                found = re.search(r"(?:^|[;\s])%s:\s*([^;]+);" % re.escape(prop), body)
                if found:
                    return found.group(1)
        raise AssertionError("no rule %r with %s" % (selector, prop))

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

    def test_the_pairs(self):
        panels = ".table-wrapper, .anims-sidebar, .anim-header-card, .anim-player-container, .timeline-section, .layers-section, .modal-dialog"
        buttons = ".sort-btn, .copy-btn, .ctrl-btn, .stage-pill-btn, .stage-mini-btn, .view-toggle-btn, .filter-chip, .sound-quick-play, .anim-link-chip, .step-link-btn"
        pressed = ".sort-btn.active, .copy-btn.active, .stage-pill-btn.active, .view-toggle-btn.active, .filter-chip.active, .sound-quick-play.playing"
        pairs = [("the table's heads", ".sounds-table th, .catalog-table th", "color", ".sounds-table th, .catalog-table th", "background"),
                 ("the layers' heads", ".layers-table th", "color", ".layers-table th", "background"),
                 ("text in a black box", panels, "color", panels, "background"),
                 ("a button", buttons, "color", buttons, "background"),
                 ("a chosen button", pressed, "color", pressed, "background"),
                 ("the chosen tab", ".tab-btn.active", "color", ".tab-btn.active", "background"),
                 ("the number of items", ".count-info", "color", ".count-info", "background"),
                 ("the tagline", ".brand .tagline", "color", ".brand .tagline", "background"),
                 ("a sprite's card", ".sprite-card", "color", ".sprite-card", "background"),
                 ("the sort bars", ".anim-sort-bar, .sprites-toolbar", "color", ".anim-sort-bar", "background"),
                 ("the player's bar", ".player-controls", "color", ".player-controls", "background"),
                 ("the dialog's title", ".modal-header h3", "color", ".modal-header", "background"),
                 ("the dialog's close sign", ".modal-close", "color", ".modal-header", "background"),
                 ("the notice", "#toast", "color", "#toast", "background"),
                 ("a key", ".kbd", "color", ".kbd", "background"),
                 ("the animation's name", ".anim-title-area h2", "color", panels, "background"),
                 ("the timeline's title", ".timeline-header h3", "color", panels, "background"),
                 ("the zoom readout", ".hud-zoom-badge", "color", ".canvas-stage.bg-checker", "background-color"),
                 ("the chosen zoom button", ".hud-btn.active", "color", ".hud-btn.active", "background"),
                 ("the sprite's hint", ".stage-hint-pill", "color", ".canvas-stage.bg-checker", "background-color")]
        for pair in pairs:
            what, text_rule, text_prop, back_rule, back_prop = pair
            text, back = self.colour(self.declared(text_rule, text_prop)), self.colour(self.declared(back_rule, back_prop))
            self.assertGreaterEqual(self.ratio(text, back), 4.5, "%s: %s on %s" % (what, text, back))
        inset = self.colour("var(--inset)")
        for name in ("--text-primary", "--text-secondary", "--text-dim"):                                                              # (the markup and the script use these on the black boxes)
            self.assertGreaterEqual(self.ratio(self.colour(self.tokens[name[2:]]), inset), 4.5, name)
        for name in ("--badge-sound", "--badge-anim", "--badge-sprite"):
            self.assertGreaterEqual(self.ratio(self.colour(self.tokens[name[2:]]), inset), 4.5, name)


if __name__ == "__main__":
    unittest.main()
