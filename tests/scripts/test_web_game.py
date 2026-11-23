#!/usr/bin/env python3
"""The game page in the front page's look (run by ./run_tests.sh --fast and by the CI). The page layout had not been updated. The game page (web/shell.html, served as /play and, as "/", for
a link that somebody shared) has the look of web/lobby.html, which is the look of the 1998 game's own menus: the clay, the thin green frame,
the teal bevelled buttons with their red shadow, the black inset boxes, the game's own font and, on the loading screen, the "ants!" logo (web/front/). What needs no browser is read from the two files:

  - the colors are the front page's, token for token, and the rules of the buttons, the two-state buttons, the frame, the black boxes, the progress bar are the front page's own
    text, where the front page (the lobby, drawn as the approved pictures draw it) has the same piece; the buttons' corners are one rounded radius, their face is lit from above (the
    sheen) and their edges are bevelled on all four sides (the bevel tokens), on the front page, this page, the changelog pages' style sheet and Sprites and sounds
  - the bar at the top: the site's own (web/site_nav.css, web/site_nav.js: the same four links, the Menu), no logo, with the page's tools in it (Fullscreen; in a recording the way back, the title, Copy link, Stats and Download); no footer
  - the loading screen (the logo, the same ids and messages, a teal bar), the failure cards (classes, not colors written into the script), the bottom controls (the same buttons and ids), the version stamp
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


# The buttons that are cut like the menus' teal button: the classes of the game page, the changelog pages and Sprites and sounds (a rule that names one of them, or a two-state button of the game page,
# is a rule of the family)
FAMILY_CLASSES = ("btn", "banner", "startbtn", "sit", "sort-btn", "copy-btn", "ctrl-btn", "stage-pill-btn", "stage-mini-btn", "view-toggle-btn", "filter-chip", "sound-quick-play", "anim-link-chip", "step-link-btn", "tab-btn")
FAMILY = re.compile(r"(?<![\w-])\.(?:%s)(?![\w-])|\.pair label|\.seg button" % "|".join(FAMILY_CLASSES))
# The front page (the lobby) is drawn as the approved pictures draw it and names its pieces differently: its `.banner` is the picture's message strip, a black box with a frame (not a teal
# face, not a button: it is not in the family) and its teal heading banner is `.name-step-title`. (`.pair label` stays in both patterns, so that the old front page's pair cannot come
# back unchecked.)
LOBBY_FAMILY_CLASSES = tuple(name for name in FAMILY_CLASSES if name != "banner") + ("name-step-title",)
LOBBY_FAMILY = re.compile(r"(?<![\w-])\.(?:%s)(?![\w-])|\.pair label|\.seg button" % "|".join(LOBBY_FAMILY_CLASSES))


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
    TOKENS = ("clay", "ink", "teal", "teal-hi", "teal-lo", "edge", "cream", "inset", "frame", "frame-hi", "frame-lo", "shadow", "gold", "bad-bg", "bad-ink", "bad-edge", "sheen", "bevel", "bevel-sm")

    def test_the_colors_are_the_front_pages_token_for_token(self):
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
        # (the front page names the tile once, in its --tile token, and the body says var(--tile); this page writes the address in the body: the same tile, the same hash)
        self.assertEqual(body["background"], lobby_body["background"].replace("var(--tile)", rule(self.lobby_style, ":root")["--tile"]))
        self.assertEqual(body["font"], lobby_body["font"])
        self.assertEqual(body["font-synthesis"], "none")
        # the thin green frame with its red line: the same three rings, over the page (it takes no room)
        frame = rule(self.style, "body::after")
        self.assertEqual(frame["box-shadow"], rule(self.lobby_style, ".screen::after")["box-shadow"])
        self.assertEqual((frame["position"], frame["pointer-events"]), ("absolute", "none"))
        self.assertEqual(body["position"], "relative")

    def test_the_teal_button_is_the_front_pages(self):
        btn = rule(self.style, ".btn")
        theirs = rule(self.lobby_style, ".btn")
        for name in ("background", "border", "border-radius", "box-shadow", "color"):
            self.assertEqual(btn[name], theirs[name], name)
        for name in ("min-height", "padding", "font-size"):
            self.assertEqual(btn[name], theirs[name], name)
        for name in ("min-height", "padding", "font-size"):
            self.assertEqual(rule(self.style, ".btn.sm")[name], rule(self.lobby_style, ".btn.sm")[name], name)
        self.assertEqual(rule(self.style, ".btn:active:not([disabled])")["box-shadow"], rule(self.lobby_style, ".btn:active:not([disabled])")["box-shadow"])
        self.assertEqual(rule(self.style, ".btn:active:not([disabled])")["transform"], rule(self.lobby_style, ".btn:active:not([disabled])")["transform"])
        self.assertEqual(rule(self.style, ".btn:hover")["background-color"], rule(self.lobby_style, ".btn:hover")["background-color"])
        self.assertIn("outline: 3px solid var(--gold);", self.page)                                  # (the focused control shows it, as on the front page)
        self.assertEqual(rule(self.lobby_style, ":focus-visible")["outline"], "3px solid var(--gold)")
        self.assertRegex(self.style, r"a:focus-visible, button:focus-visible, input:focus-visible, summary:focus-visible \{")

    def test_the_banner_is_the_front_pages(self):
        # The game page's .banner is a teal heading banner, cut like the teal button: it shares the face (the color with the sheen, the frame, the corners, the bevel and the red shadow, the cream words)
        # with the front page's .btn (the front page's own .banner is another thing, the picture's black message strip). The front page's teal heading banner, .name-step-title (the same card as the
        # game page's own, which is this page's .name-step-title.banner), has that face too; the gold words of the title are the game page's .name-step-title.
        banner, theirs = rule(self.style, ".banner"), rule(self.lobby_style, ".btn")
        for name in ("background", "border", "border-radius", "box-shadow", "color"):
            self.assertEqual(banner[name], theirs[name], name)
        for name in ("background", "border", "border-radius", "box-shadow"):
            self.assertEqual(banner[name], rule(self.lobby_style, ".name-step-title")[name], ".name-step-title " + name)
        for name in ("color", "text-shadow"):
            self.assertEqual(rule(self.style, ".name-step-title")[name], rule(self.lobby_style, ".name-step-title")[name], name)

    def family_styles(self):
        """{where: (style, pattern of the selectors of its family)} of the front page, the game page, the style sheet of the changelog pages and Sprites and sounds."""
        classic = re.sub(r"/\*.*?\*/", "", read("web", "front", "classic.css"), flags=re.S)
        return {"the front page": (self.lobby_style, LOBBY_FAMILY), "the game page": (self.style, FAMILY), "the style sheet of the changelog pages": (classic, FAMILY),
                "Sprites and sounds": (style_of(read("asset_catalog", "index.html")), FAMILY)}

    def faces_and_corners(self):
        """{where: rule} of every teal button face of the site: the front page (the buttons, the heading banner of the name card and the picture's dropdown; its black message strip .banner is no teal
        face), the game page, the style sheet of the changelog pages and Sprites and sounds."""
        classic = re.sub(r"/\*.*?\*/", "", read("web", "front", "classic.css"), flags=re.S)
        group = [d for s, d in blocks(style_of(read("asset_catalog", "index.html"))) if s.startswith(".sort-btn, .copy-btn,") and "border-radius" in d]
        self.assertEqual(len(group), 1, "Sprites and sounds: one rule for the small buttons (it starts with .sort-btn, .copy-btn,) that says their corners")
        return {
            "the front page's buttons": rule(self.lobby_style, ".btn"),
            "the front page's name card title": rule(self.lobby_style, ".name-step-title"),
            "the game page's buttons": rule(self.style, ".btn"),
            "the game page's banner": rule(self.style, ".banner"),
            "the game page's pairs": rule(self.style, ".seg button"),
            "the game page's picture dropdown": rule(self.style, ".pick-select"),
            "the changelog pages' buttons and banners": rule(classic, ".btn, .banner"),
            "the small buttons of Sprites and sounds": group[0],
        }

    def test_the_buttons_have_rounded_corners_on_every_page(self):
        # Requested on 2026-10-05: the buttons slightly rounded on the corners (they were 2 px, which reads as square), then, at 6 px: a little more round, then, at 10 px:
        # a little less curved on the corners (8 px). One radius for everything that is cut like the menus' teal button (the buttons, the two-state buttons, the heading banners that look like them),
        # on the front page, the game page and the style sheet of the changelog pages.
        radius = {where: declarations["border-radius"] for where, declarations in self.faces_and_corners().items()}
        for where, value in radius.items():
            found = re.fullmatch(r"(\d+)px", value)
            self.assertIsNotNone(found, "%s: %r" % (where, value))
            self.assertTrue(6 <= int(found.group(1)) <= 12, "%s: %s (clearly rounded, but not a pill)" % (where, value))
        self.assertEqual(len(set(radius.values())), 1, radius)                                       # (one radius: no page has squarer buttons than another)
        (one,) = set(radius.values())
        # (the front page has no <summary> since its redesign, "How it works" is a dialog; if one comes back it needs its rule, with the corners of the button)
        lobby_summaries = [d for s, d in blocks(self.lobby_style) if re.search(r"\bsummary\b", s)]
        self.assertEqual(bool(lobby_summaries), "<summary" in self.lobby, "the front page: a <summary> has a rule with the corners of the button, and a rule has a <summary>")
        for declarations in lobby_summaries:
            self.assertEqual(declarations["border-radius"], one)

    def test_no_other_rule_gives_a_button_of_the_family_other_corners(self):
        # A rule further down a style that says .btn.sm { border-radius: 2px } would pass the test above and show a square button: every rule that names a button of the family
        # (or says a corner of its own) has the family's radius.
        (one,) = {d["border-radius"] for d in self.faces_and_corners().values()}
        seen = 0
        for where, (style, family) in self.family_styles().items():
            for selector, declarations in blocks(style):
                if not family.search(selector):
                    continue
                for name, value in declarations.items():
                    if re.fullmatch(r"border(?:-(?:top|bottom)-(?:left|right))?-radius", name):
                        seen += 1
                        self.assertEqual(value, one, "%s: %s { %s: %s }" % (where, selector, name, value))
        self.assertGreaterEqual(seen, 7, "the rules of the family that say corners: found %d" % seen)

    def test_nothing_wipes_the_face_of_a_button_of_the_family(self):
        # The review of v0.8.2 found `background: #1e3a8a;` in the style attribute of one button of Sprites and sounds: a background shorthand resets the background image, so that button alone was flat.
        # A rule that names a button of the family says its face (the color with the sheen over it), the flat plate of a chosen one, or only a color (background-color); and no style attribute of such a
        # button says a background or corners of its own.
        faces = {"var(--sheen) var(--teal)", "var(--sheen, none) var(--teal)", "#102b25", "var(--pressed)", "var(--gold)"}
        seen = 0
        for where, (style, family) in self.family_styles().items():
            for selector, declarations in blocks(style):
                if family.search(selector) and "background" in declarations:
                    seen += 1
                    self.assertIn(declarations["background"], faces, "%s: %s { background: %s } (this wipes the sheen: say background-color)" % (where, selector, declarations["background"]))
        self.assertGreaterEqual(seen, 10, "the rules of the family that say a background: found %d" % seen)
        pages = {"the front page": (self.lobby, LOBBY_FAMILY_CLASSES), "the game page": (self.page, FAMILY_CLASSES), "Sprites and sounds": (read("asset_catalog", "index.html"), FAMILY_CLASSES),
                 "the generator of the changelog pages": (read("tools", "changelog_to_html.py"), FAMILY_CLASSES)}
        tags = 0
        for where, (text, family_classes) in pages.items():
            for tag in re.finditer(r"<[a-zA-Z][^<>]*>", text):                                     # (markup and the templates of the scripts alike)
                classes = re.search(r"\bclass\s*=\s*(?:\"([^\"]*)\"|'([^']*)')", tag.group(0))
                inline = re.search(r"\bstyle\s*=\s*(?:\"([^\"]*)\"|'([^']*)')", tag.group(0))
                if not (classes and inline) or not set((classes.group(1) or classes.group(2)).split()) & set(family_classes):
                    continue
                tags += 1
                self.assertNotRegex(inline.group(1) or inline.group(2), r"background(?!-color)|border-radius", "%s: %s" % (where, tag.group(0)[:120]))
        self.assertGreaterEqual(tags, 1, "the style attributes of the family's buttons: found %d" % tags)

    def test_the_teal_buttons_are_lit_from_above_on_every_page(self):
        # Requested on 2026-10-05: the buttons appear a little less flat. The face of every teal button is its color with the sheen over it: a light edge at the top and shade toward the bottom.
        # The same sheen on every page; a chosen (pressed in) button is a flat dark plate, and the hover changes only the color and keeps the sheen.
        classic = re.sub(r"/\*.*?\*/", "", read("web", "front", "classic.css"), flags=re.S)
        catalogue = style_of(read("asset_catalog", "index.html"))
        sheen = rule(self.lobby_style, ":root")["--sheen"]
        self.assertEqual(rule(self.style, ":root")["--sheen"], sheen)
        self.assertEqual(rule(classic, ":root")["--sheen"], sheen)
        for where, declarations in self.faces_and_corners().items():
            # (Sprites and sounds takes the token from the changelog pages' style sheet: with an older style sheet its buttons are flat teal, not transparent)
            self.assertEqual(declarations["background"], "var(--sheen, none) var(--teal)" if where == "the small buttons of Sprites and sounds" else "var(--sheen) var(--teal)", where)
        color_only = {
            "the front page's hovered button": rule(self.lobby_style, ".btn:hover"),
            # (the front page's teal match banner, `.card.match > .banner`, is gone: its .banner is the picture's black message strip, which has no teal face to keep the sheen of)
            "the game page's hovered button": rule(self.style, ".btn:hover"),
            "the game page's hovered pair": rule(self.style, '.seg button[aria-checked="false"]:hover'),
            "the game page's hovered picture dropdown": rule(self.style, ".pick-select:hover"),
            "the changelog pages' hovered button": rule(classic, ".btn:hover"),
            "the hovered small buttons of Sprites and sounds": next(d for s, d in blocks(catalogue) if s.startswith(".sort-btn:hover, .copy-btn:hover,")),
        }
        for where, declarations in color_only.items():
            self.assertNotIn("background", declarations, where + " (the shorthand would wipe the sheen)")
            self.assertRegex(declarations["background-color"], r"^(?:var\(--teal(?:-hi)?\)|#[0-9a-f]{6})$", where)
        for where, declarations in (("the game page's chosen pair", rule(self.style, '.seg button[aria-checked="true"]')),):
            self.assertEqual(declarations["background"], "#102b25", where)                           # (flat and dark: pressed in)
        # The sheen is the weave (a faint 45 degree canvas: a light and a dark line pattern, 4 px apart) and a soft light from above. The light sits at the top and is gone by 18% of the height, above the
        # words, and the weave has no light dots that matter (its light lines are at most 4% white), so every text is still at least 4.5:1 on its face, hovered too (the pairs of the tests above are the colors
        # without the sheen; tests/scripts/test_web_buttons.py has the measured numbers' limits). The color of the face goes LAST in `background` (a color may only be in the last layer of several).
        weave = rule(self.lobby_style, ":root")["--weave"]
        self.assertEqual(rule(self.style, ":root")["--weave"], weave)
        self.assertEqual(rule(classic, ":root")["--weave"], weave)
        self.assertTrue(sheen.startswith("var(--weave), linear-gradient(180deg, "), sheen)
        gradient = sheen.split("linear-gradient(180deg, ", 1)[1]
        stops = [((int(m.group(1)), int(m.group(2)), int(m.group(3))), float(m.group(4)), m.group(5))
                 for m in re.finditer(r"rgba\((\d+), (\d+), (\d+), ([\d.]+)\) ([\d.]+(?:px|%)?)", gradient)]
        self.assertEqual(len(stops), len(re.findall(r"rgba\(", gradient)), sheen)
        light = [(alpha, position) for color, alpha, position in stops if color == (255, 255, 255)]
        self.assertEqual([position for alpha, position in light if alpha > 0], ["0"], sheen)           # (the light starts at the top edge ...)
        self.assertLessEqual(max(alpha for alpha, position in light), 0.15, sheen)
        self.assertTrue(all(re.fullmatch(r"[\d.]+%", position) and float(position[:-1]) <= 18 for alpha, position in light if alpha == 0), sheen)   # (... and is gone by 18%)
        shade = [alpha for color, alpha, position in stops if color == (0, 0, 0)]
        self.assertTrue(shade and max(shade) <= 0.3, sheen)
        self.assertEqual(stops[-1][0], (0, 0, 0), sheen)                                               # (the bottom is the darkest part)
        self.assertTrue(all(float(alpha) <= 0.04 for alpha in re.findall(r"rgba\(255, 255, 255, ([\d.]+)\)", weave) if float(alpha) > 0), weave)   # (no light dots in the weave)


    def test_the_teal_buttons_have_the_double_frame_on_every_page(self):
        # The bevel is a double frame, the same on the front page, this page and the style sheet of the changelog pages (every teal face takes its inset shadows from the tokens, none draws its own):
        # a light line of 1 px just inside the dark edge, then a dark groove, then a darker lower lip (-sm: the small buttons'), and a pressed button (-dn) is dark inside, with the light line gone.
        classic = re.sub(r"/\*.*?\*/", "", read("web", "front", "classic.css"), flags=re.S)
        roots = {"the front page": rule(self.lobby_style, ":root"), "the game page": rule(self.style, ":root"), "the style sheet of the changelog pages": rule(classic, ":root")}
        for name in ("bevel", "bevel-sm", "bevel-dn", "btn-text", "teal-hi"):
            values = {where: declarations["--" + name] for where, declarations in roots.items()}
            self.assertEqual(len(set(values.values())), 1, values)
        bevel, small, down = (roots["the front page"]["--" + name] for name in ("bevel", "bevel-sm", "bevel-dn"))
        self.assertTrue(bevel.startswith("inset 0 0 0 1px rgba(255, 255, 255, "), bevel)                 # (the light line ...)
        self.assertIn(", inset 0 0 0 3px rgba(0, 0, 0, ", bevel)                                          # (... the groove ...)
        self.assertIn(", inset 0 -4px 0 3px rgba(0, 0, 0, ", bevel)                                       # (... and the lip)
        self.assertEqual(small, bevel.replace("inset 0 -4px 0 3px", "inset 0 -3px 0 3px"))
        self.assertTrue(down.startswith("inset 0 0 0 1px rgba(0, 0, 0, "), down)
        self.assertIn("inset 0 5px 8px 1px rgba(0, 0, 0, ", down)
        for where, declarations in self.faces_and_corners().items():
            self.assertRegex(declarations["box-shadow"], r"^var\(--(?:bevel|bevel-sm|btn-shadow|btn-shadow-sm)\)", where)
        # the pressed button goes 3 px right and 4 px down, is dark inside and has lost its red shadow; the small buttons (`.btn.sm`) are from the same tokens
        for where, declarations in (("the front page's pressed button", rule(self.lobby_style, ".btn:active:not([disabled])")), ("the game page's pressed button", rule(self.style, ".btn:active:not([disabled])")),
                                    ("the changelog pages' pressed button", rule(classic, ".btn:active:not([disabled])"))):
            self.assertEqual(declarations["transform"], "translate(3px, 4px)", where)
            self.assertIn(declarations.get("box-shadow", "var(--btn-shadow-down)"), ("var(--bevel-dn), 0 0 0 var(--shadow)", "var(--btn-shadow-down)"), where)
        self.assertEqual(roots["the style sheet of the changelog pages"]["--btn-shadow-down"], "var(--bevel-dn), 0 0 0 var(--shadow)")
        self.assertTrue(rule(self.lobby_style, ".btn.sm")["box-shadow"].startswith("var(--bevel-sm), "))
        for token, bevel_token in (("--btn-shadow", "var(--bevel)"), ("--btn-shadow-sm", "var(--bevel-sm)")):
            self.assertTrue(roots["the style sheet of the changelog pages"][token].startswith(bevel_token + ", "), token)


    def test_the_picture_dropdown_keeps_the_face_of_the_pages_buttons_and_the_two_state_buttons_stay_the_game_pages(self):
        # The game page's Picture dropdown (`.pick-select`, in the recording's Picture window; the settings menu's is `.gm-select`) has the colors, the frame, the corners and the bevel of the buttons. The two-state
        # buttons that are left (the replay's speed) are this page's `.seg button`; the front page has no Picture dropdown.
        ours, button = rule(self.style, ".pick-select"), rule(self.style, ".btn")
        for name in ("color", "border-radius"):
            self.assertEqual(ours[name], button[name], name)
        self.assertEqual(ours["border"].split()[1:], button["border"].split()[1:], "the frame's style and color")
        self.assertRegex(ours["box-shadow"], r"^var\(--bevel-sm\), \d+px \d+px 0 var\(--shadow\)$")
        self.assertEqual(len(re.findall(r'<div class="seg[ "][^>]*role="radiogroup"', self.page)), 1)           # (the replay bar's speed: TheReplayPage)

    def test_the_black_boxes_are_the_front_pages(self):
        # (the front page's black box with its frame is `.panel`; its name card, the same card as this page's own, has the same field)
        theirs = rule(self.lobby_style, ".panel")
        for selector in ("#info-panel", ".progress-bar-container"):
            ours = rule(self.style, selector)
            for name in ("border", "border-color", "border-radius", "box-shadow"):
                self.assertEqual(ours[name], theirs[name], selector + " " + name)
            self.assertEqual(ours["background"], theirs["background"], selector)
        field, theirs = rule(self.style, ".name-step input"), rule(self.lobby_style, ".name-step input")
        for name in ("min-height", "padding", "color", "background", "border", "border-color", "border-radius", "box-shadow"):
            self.assertEqual(field[name], theirs[name], name)

    def test_the_picture_has_the_frame_of_the_front_pages_pictures(self):
        ring = rule(self.style, "#game-container")["box-shadow"]
        for token in ("var(--edge)", "var(--frame)", "var(--shadow)"):
            self.assertIn(token, ring)
        shadows = re.findall(r"(\S+) (\S+) (\S+) (\S+) var\(--(\w+)\)", ring)
        self.assertEqual([(spread, color) for _, _, _, spread, color in shadows], [("3px", "edge"), ("6px", "frame"), ("8px", "edge"), ("8px", "shadow")])     # (black, green, black, and the red shadow)
        self.assertEqual(shadows[3][:2], ("4px", "6px"))
        # a shadow, not a border: the box has no frame inside it (the canvas fills it exactly)
        self.assertNotIn("border", rule(self.style, "#game-container"))
        self.assertIn("isolation", rule(self.style, "#game-container"))
        self.assertEqual(rule(self.style, "#game-container")["background"], "#000")


class TheBar(PageCase):
    """The bar at the top is the site's (docs/SITE_NAV.md "The game page"): its Menu, its four links, and the page's own tools; what a match does with its links and keys is run in a browser (tests/scripts/test_web_game_bar.py)."""

    def header(self):
        return self.page[self.page.index('<header class="sb"'):self.page.index("</header>")]

    def test_the_bar_is_the_sites_with_home_first_and_the_pages_tools_in_it(self):
        bar = self.header()
        self.assertNotIn("<img", bar)
        self.assertEqual(re.findall(r'<a class="btn[a-z ]*" href="([^"]*)"', re.search(r'<nav aria-label="Pages">(.*?)</nav>', bar, re.S).group(1)), ["/", "/matches", "/mapmaker", "https://discord.gg/WaWrXQnKPj"])
        home = re.search(r'<a class="btn sbhome" href="/" id="menu-btn"([^>]*)>', bar)
        self.assertIsNotNone(home)
        self.assertNotIn("target=", home.group(1))
        self.assertEqual(bar.count('aria-controls="sbm"'), 1)                                                  # (one Menu button)
        for ident in ("fullscreen-btn", "all-btn", "copy-btn", "stats-btn", "r-title", "r-meta"):
            self.assertEqual(len(re.findall(r'id="%s"' % ident, self.page)), 1, ident)
        self.assertEqual(bar.count("data-dl"), 1)
        self.assertEqual(len(re.findall(r"<h1[ >]", self.page)), 1)                                           # (the page keeps its heading: for a screen reader)
        self.assertEqual(len(re.findall(r'\bid="([^"]+)"', self.page)), len(set(re.findall(r'\bid="([^"]+)"', self.page))), "an id is used twice")
        self.assertNotRegex(bar, r"<script|onclick=")

    def test_the_old_header_the_more_lists_and_the_reset_button_are_gone(self):
        for gone in ("more-list", "reset-btn", "home-btn", "menu-link", "watch-link", "watch-btn", "feedback-btn", "copy-more", "fullscreen-btn-r", "header-actions"):
            self.assertNotIn(gone, self.page, gone)
        self.assertNotIn("Reset", re.sub(r"<script.*?</script>", "", self.page, flags=re.S).replace("Reset Game", ""))

    def test_the_bar_files_load_once_the_site_script_first_and_before_the_page_script(self):
        self.assertEqual(self.page.count('<link rel="stylesheet" href="site_nav.css"><link rel="stylesheet" href="game_bar.css">'), 1)
        self.assertEqual(self.page.count('<script src="site_nav.js"></script><script src="game_bar.js"></script>'), 1)
        self.assertLess(self.page.index("game_bar.js"), self.page.index("AntsGameBar.start("))
        self.assertLess(self.page.index('<header class="sb"'), self.page.index("site_nav.js"))

    def test_the_bar_is_as_high_as_the_header_it_replaced_so_the_picture_keeps_its_size(self):
        css = read("web", "game_bar.css")
        self.assertRegex(css, r"header\.sb \{[^}]*min-height: 44px;[^}]*margin: 0 0 18px;")
        self.assertRegex(css, r"@media \(max-width: 700px\) \{ header\.sb \{ margin-bottom: 16px; \} \}")
        self.assertRegex(css, r"@media \(min-width: 701px\) and \(max-height: 520px\) \{ header\.sb \{ margin-bottom: 14px; \} \}")


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
        self.assertRegex(rule(self.style, "#splash-overlay")["background"], r'^var\(--clay\) url\("front/clay\.png\?v=[0-9a-f]{8}"\)$')     # (the tile's address carries its hash: test_web_front.py)
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


class TheVersionStamp(PageCase):
    def test_the_version_and_the_build_are_where_the_dockerfile_and_the_ci_look_for_them_and_the_page_has_no_footer(self):
        self.assertNotIn("<footer", self.page)
        stamp = re.search(r'<span class="ver" id="game-version-line" hidden>(.*?)</span></span></span>', self.page).group(0)
        self.assertIn('<span id="game-version">@@GAME_VERSION@@</span>', stamp)
        self.assertIn('<span id="game-build-id">@@BUILD_ID@@</span>', stamp)
        self.assertIn('<span id="game-build-date">(@@BUILD_DATE@@)</span>', stamp)
        for placeholder in ("@@SITE_TITLE@@", "@@GAME_VERSION@@", "@@BUILD_ID@@", "@@BUILD_DATE@@", "@@BUILD_TIMESTAMP@@", "@@DATA_SIZE@@"):
            self.assertIn(placeholder, self.page, placeholder)
        self.assertNotIn("@@SITE_FOOTER@@", self.page)
        self.assertIn('data-version="@@GAME_VERSION@@" data-build="@@BUILD_ID@@" data-built="@@BUILD_DATE@@"', self.header_of_bar())     # (the three stamps on the bar: its Menu's last line)
        self.assertEqual(self.page.count("@@GAME_VERSION@@"), 2)                                                  # (the bar's and the stamp's)
        self.assertIn("document.getElementById('game-version-line')", self.page)                                  # (a build that did not fill them in hides the line)

    def header_of_bar(self):
        return self.page[self.page.index('<header class="sb"'):self.page.index(">", self.page.index('<header class="sb"')) + 1]


class TheGamePageHasNoFrameModeButTheMatchPages(PageCase):
    def test_nothing_of_the_old_frames_of_the_front_page_is_left(self):
        self.assertNotIn("classList.add('embed')", self.page)                                                        # (the front page holds no game in a frame: the test room that did is gone)
        self.assertNotIn("embed", self.lobby)

    def test_the_one_frame_is_the_replay_in_a_match_page_and_asks_for_nothing_else(self):
        # play.html?replay=<file>&embed=1, in the frame of web/match.html (docs/REPLAYS.md "The match pages"): only a replay, never a match played, a room or a live match
        self.assertIn("if (/(^|[?&])replay=/.test(window.location.search) && /(^|[?&])embed=1(&|$)/.test(window.location.search)) document.body.className += ' embed';", self.page)
        self.assertIn("var ANTS_EMBED = ANTS_REPLAY_MODE && !ANTS_LIVE_MODE && !ANTS_OWN_MODE && /(^|[?&])embed=1(&|$)/.test(window.location.search);", self.page)
        embed_css = read("web", "replay_stats.css")                                                                  # (its own stylesheet: the game page may not grow, tools/check_hygiene.py)
        self.assertIn('<link rel="stylesheet" href="replay_stats.css">', self.page)
        self.assertEqual(embed_css.count("body.embed header, body.embed #mobile-tip-banner, body.embed .mstats, body.embed #stats-btn { display: none !important; }"), 1)
        self.assertEqual(embed_css.count("body.embed::after { content: none; }"), 1)                               # (the page's own frame line: the match page has its own)
        self.assertEqual(embed_css.count("body.embed { padding-top: 10px; padding-left: 12px; padding-right: 12px; --side: 12px; }"), 1)
        self.assertNotIn("body.embed", self.style)                                                                   # (nothing of the frame mode in the page's own style)


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
        self.assertEqual(named, {"logo.png", "clay.png", "LibreFranklin-Medium.ttf", "preview_treasure.png"})       # (the last is the picture of the page's share card, named in its head: tests/scripts/test_web_seo.py)
        for name in named:
            self.assertTrue(os.path.exists(os.path.join(FRONT, name)), name)
        tags = re.findall(r"<img [^>]*>", self.page)
        self.assertEqual(len(tags), 1)                                                                              # (the loading screen's logo: the header shows none)
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
            self.assertTrue(url.startswith(GITHUB) or url.startswith("https://discord.gg/"), url)       # (the bar's Discord link opens in a new tab)

    def test_the_page_has_no_emoji_and_no_symbol_font_glyph_in_its_controls(self):
        self.assertNotRegex(self.page, "[\U0001F300-\U0001FAFF☀-➿]")
        self.assertNotIn("&#x26F6;", self.page)


class TheColors(PageCase):
    """The text of the page is at least 4.5:1 against its background, in every state of the buttons and boxes (the browser check measures the whole page; this reads the colors)."""

    CLAY_ENDS = ("#d84710", "#e95e24")                      # the two ends of the clay tile (its deepest and its lightest broad shade) that the front page's check measures against

    def token(self, name):
        return rule(self.style, ":root")["--" + name]

    @staticmethod
    def luminance(color):
        def channel(v):
            v /= 255.0
            return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
        r, g, b = (int(color[i:i + 2], 16) for i in (1, 3, 5))
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
                 ("gold on the banner of the name step", gold, teal)]
        for what, text, background in pairs:
            self.assertGreaterEqual(self.ratio(text, background), 4.5, "%s: %s on %s" % (what, text, background))
        for end in self.CLAY_ENDS:                                                                                  # (the labels, the loading message and the name step's words are ink on the clay tile)
            self.assertGreaterEqual(self.ratio(ink, end), 4.5, "ink on clay %s" % end)

    def test_the_colors_written_in_the_rules_are_the_ones_tested(self):
        for selector, value in ((".info-col li strong", "#a6e8c6"), (".info-col li em", "#fff"), (".info-col li.note", "#b9cfc5")):
            self.assertIn(value, [d.get("color") for s, d in blocks(self.style) if s == selector])
        self.assertEqual(rule(self.style, "kbd")["background"], "#0e1a17")
        self.assertEqual(rule(self.style, ".mobile-tip-banner")["color"], "var(--gold)")
        self.assertEqual(rule(self.style, ".mobile-tip-banner")["background"], "var(--inset)")


if __name__ == "__main__":
    unittest.main()
