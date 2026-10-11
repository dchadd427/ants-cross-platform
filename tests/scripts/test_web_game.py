#!/usr/bin/env python3
"""The game page in the front page's look (run by ./run_tests.sh --fast and by the CI). The page layout had not been updated. The game page (web/shell.html, served as /play.html and, as "/", for
a link that somebody shared) has the look of web/lobby.html, which is the look of the 1998 game's own menus: the clay, the thin green frame,
the teal bevelled buttons with their red shadow, the black inset boxes, the game's own font and the small "ants!" logo (web/front/). What needs no browser is read from the two files:

  - the colours are the front page's, token for token, and the rules of the buttons, the two-state buttons, the frame, the black boxes, the progress bar and the footer are the front page's own
    text, where the front page (the lobby, drawn as the approved pictures draw it) has the same piece; the buttons' corners are one rounded radius, their face is lit from above (the
    sheen) and their edges are bevelled on all four sides (the bevel tokens), on the front page, this page, the changelog pages' style sheet and Sprites and sounds
  - the header: the logo (a link back to the menu that asks as Menu does, with the same code), the eight controls in their order, and "More" for a narrow window (a <details>, no script) with
    the other six
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


# The buttons that are cut like the menus' teal button: the classes of the game page, the changelog pages and Sprites and sounds (a rule that names one of them, or a two-state button of the game page,
# is a rule of the family)
FAMILY_CLASSES = ("btn", "banner", "startbtn", "sit", "sort-btn", "copy-btn", "ctrl-btn", "stage-pill-btn", "stage-mini-btn", "view-toggle-btn", "filter-chip", "sound-quick-play", "anim-link-chip", "step-link-btn", "tab-btn")
FAMILY = re.compile(r"(?<![\w-])\.(?:%s)(?![\w-])|\.pair label|\.seg button" % "|".join(FAMILY_CLASSES))
# The front page (the lobby) is drawn as the approved pictures draw it and names its pieces differently: its `.banner` is the picture's message strip, a black box with a frame (not a teal
# face, not a button: it is not in the family), its teal heading banner is `.name-step-title`, and its two-state buttons are the labels of the footer's radio buttons
# (`.shape .seg label`, the chosen one is `.shape .seg input:checked + label`). (`.pair label` stays in both patterns, so that the old front page's pair cannot come back unchecked.)
LOBBY_FAMILY_CLASSES = tuple(name for name in FAMILY_CLASSES if name != "banner") + ("name-step-title",)
LOBBY_FAMILY = re.compile(r"(?<![\w-])\.(?:%s)(?![\w-])|\.pair label|\.seg button|\.shape \.seg [^,{]*label" % "|".join(LOBBY_FAMILY_CLASSES))


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
        self.assertEqual(rule(self.style, ".btn:active")["box-shadow"], rule(self.lobby_style, ".btn:active")["box-shadow"])
        self.assertEqual(rule(self.style, ".btn:active")["transform"], rule(self.lobby_style, ".btn:active")["transform"])
        self.assertEqual(rule(self.style, ".btn:hover")["background-color"], rule(self.lobby_style, ".btn:hover")["background-color"])
        self.assertIn("outline: 3px solid var(--gold);", self.page)                                  # (the focused control shows it, as on the front page)
        self.assertEqual(rule(self.lobby_style, ":focus-visible")["outline"], "3px solid var(--gold)")
        self.assertRegex(self.style, r"a:focus-visible, button:focus-visible, input:focus-visible, summary:focus-visible \{")

    def test_the_banner_is_the_front_pages(self):
        # The game page's .banner is a teal heading banner, cut like the teal button: it shares the face (the colour with the sheen, the frame, the corners, the bevel and the red shadow, the cream words)
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
        """{where: rule} of every teal button face of the site: the front page (the buttons, the heading banner of the name card and the footer's two-state buttons; its black message strip .banner is no teal
        face), the game page, the style sheet of the changelog pages and Sprites and sounds."""
        classic = re.sub(r"/\*.*?\*/", "", read("web", "front", "classic.css"), flags=re.S)
        group = [d for s, d in blocks(style_of(read("asset_catalog", "index.html"))) if s.startswith(".sort-btn, .copy-btn,") and "border-radius" in d]
        self.assertEqual(len(group), 1, "Sprites and sounds: one rule for the small buttons (it starts with .sort-btn, .copy-btn,) that says their corners")
        return {
            "the front page's buttons": rule(self.lobby_style, ".btn"),
            "the front page's name card title": rule(self.lobby_style, ".name-step-title"),
            "the front page's two-state buttons": rule(self.lobby_style, ".shape .seg label"),
            "the game page's buttons": rule(self.style, ".btn"),
            "the game page's banner": rule(self.style, ".banner"),
            "the game page's pairs": rule(self.style, ".seg button"),
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
        # a <summary> with a button inside carries the keyboard focus ring: it has the corners of the button, or the ring would be square around a rounded button
        (one,) = set(radius.values())
        self.assertEqual(rule(self.style, ".more summary")["border-radius"], one)
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
        # A rule that names a button of the family says its face (the colour with the sheen over it), the flat plate of a chosen one, or only a colour (background-color); and no style attribute of such a
        # button says a background or corners of its own.
        faces = {"var(--teal) var(--sheen)", "var(--teal) var(--sheen, none)", "#102b25", "var(--pressed)", "var(--gold)"}
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
        # Requested on 2026-10-05: the buttons appear a little less flat. The face of every teal button is its colour with the sheen over it: a light edge at the top and shade toward the bottom.
        # The same sheen on every page; a chosen (pressed in) button is a flat dark plate, and the hover changes only the colour and keeps the sheen.
        classic = re.sub(r"/\*.*?\*/", "", read("web", "front", "classic.css"), flags=re.S)
        catalogue = style_of(read("asset_catalog", "index.html"))
        sheen = rule(self.lobby_style, ":root")["--sheen"]
        self.assertEqual(rule(self.style, ":root")["--sheen"], sheen)
        self.assertEqual(rule(classic, ":root")["--sheen"], sheen)
        for where, declarations in self.faces_and_corners().items():
            # (Sprites and sounds takes the token from the changelog pages' style sheet: with an older style sheet its buttons are flat teal, not transparent)
            self.assertEqual(declarations["background"], "var(--teal) var(--sheen, none)" if where == "the small buttons of Sprites and sounds" else "var(--teal) var(--sheen)", where)
        colour_only = {
            "the front page's hovered button": rule(self.lobby_style, ".btn:hover"),
            "the front page's hovered two-state button": rule(self.lobby_style, ".shape .seg label:hover"),
            # (the front page's teal match banner, `.card.match > .banner`, is gone: its .banner is the picture's black message strip, which has no teal face to keep the sheen of)
            "the game page's hovered button": rule(self.style, ".btn:hover"),
            "the game page's hovered pair": rule(self.style, '.seg button[aria-checked="false"]:hover'),
            "the changelog pages' hovered button": rule(classic, ".btn:hover"),
            "the hovered small buttons of Sprites and sounds": next(d for s, d in blocks(catalogue) if s.startswith(".sort-btn:hover, .copy-btn:hover,")),
        }
        for where, declarations in colour_only.items():
            self.assertNotIn("background", declarations, where + " (the shorthand would wipe the sheen)")
            self.assertRegex(declarations["background-color"], r"^(?:var\(--teal(?:-hi)?\)|#[0-9a-f]{6})$", where)
        for where, declarations in (("the front page's chosen two-state button", rule(self.lobby_style, ".shape .seg input:checked + label")), ("the game page's chosen pair", rule(self.style, '.seg button[aria-checked="true"]'))):
            self.assertEqual(declarations["background"], "#102b25", where)                           # (flat and dark: pressed in)
        # The light of the sheen sits at the very top and is gone by 4 px, above the words (the gradient starts inside the border, so 4 px of it is 6 px below the outer edge; the 12 px words of the smallest
        # buttons, the chips of Sprites and sounds and the release plates, begin about 7 px down), so every text is still at least 4.5:1 on its face (the pairs of the tests above are the colours without
        # the sheen; everything else of the sheen is shade, which only makes the words stand out more). The review of v0.8.2 measured 8 px taking those words down to 4.1:1.
        stops = [((int(m.group(1)), int(m.group(2)), int(m.group(3))), float(m.group(4)), m.group(5))
                 for m in re.finditer(r"rgba\((\d+), (\d+), (\d+), ([\d.]+)\) ([\d.]+(?:px|%)?)", sheen)]
        self.assertTrue(sheen.startswith("linear-gradient(180deg, "), sheen)
        self.assertEqual(len(stops), len(re.findall(r"rgba\(", sheen)), sheen)
        light = [(alpha, position) for colour, alpha, position in stops if colour == (255, 255, 255)]
        self.assertEqual([position for alpha, position in light if alpha > 0], ["0"], sheen)           # (the light starts at the top edge ...)
        self.assertLessEqual(max(alpha for alpha, position in light), 0.3, sheen)
        self.assertTrue(all(re.fullmatch(r"[\d.]+px", position) and float(position[:-2]) <= 4 for alpha, position in light if alpha == 0), sheen)   # (... and is gone by 4 px)
        shade = [alpha for colour, alpha, position in stops if colour == (0, 0, 0)]
        self.assertTrue(shade and max(shade) <= 0.4, sheen)
        self.assertEqual(stops[-1][0], (0, 0, 0), sheen)                                               # (the bottom is the darkest part)

    def test_the_teal_buttons_are_bevelled_on_all_four_edges_on_every_page(self):
        # Requested on 2026-10-05, about the left edge of START! (a flat light strip beside a bevel that lit only the top and shaded only the bottom): that area should look more
        # beveled. The bevel is lit on the top AND the left edge and shaded on the bottom AND the right edge, so it turns the rounded corners; it is one pair of tokens that every page shares
        # (the front page, this page, the style sheet of the changelog pages that Sprites and sounds links), and every teal face takes its inset shadows from them, none draws its own.
        classic = re.sub(r"/\*.*?\*/", "", read("web", "front", "classic.css"), flags=re.S)
        roots = {"the front page": rule(self.lobby_style, ":root"), "the game page": rule(self.style, ":root"), "the style sheet of the changelog pages": rule(classic, ":root")}
        for name in ("bevel", "bevel-sm"):
            values = {where: declarations["--" + name] for where, declarations in roots.items()}
            self.assertEqual(len(set(values.values())), 1, values)
            shadows = re.findall(r"inset (-?\d+)px (-?\d+)px (\d+)(?:px)? (#[0-9a-f]{6}|rgba\([^)]*\)|var\(--[\w-]+\))", values["the front page"])
            self.assertEqual(len(shadows), values["the front page"].count("inset "), values["the front page"])         # (every part of the bevel was read)
            lit = [(int(x), int(y)) for x, y, blur, colour in shadows if int(x) > 0 and int(y) > 0]
            shaded = [(int(x), int(y)) for x, y, blur, colour in shadows if int(x) < 0 and int(y) < 0]
            self.assertEqual(len(lit) + len(shaded), len(shadows), name + ": every part lights the top and the left together, or shades the bottom and the right together")
            self.assertGreaterEqual(len(lit), 2, name)                                                # (a bright line and a facet ...)
            self.assertGreaterEqual(len(shaded), 2, name)                                             # (... and a thin dark line and a facet)
            self.assertTrue(all(x == y for x, y in lit), name + ": the left edge is lit as thickly as the top")
            self.assertTrue(all(-y >= -x for x, y in shaded), name + ": the shade is at least as thick at the bottom as at the right")
            self.assertEqual(values["the front page"].count("#3d9a7d"), 1, name)
        # no rule draws a bevel of its own: the pages' own text has the colour of the lit facet only in the two tokens
        for where, style in (("the front page", self.lobby_style), ("the game page", self.style), ("the style sheet of the changelog pages", classic)):
            self.assertEqual(style.count("#3d9a7d"), 2, where)
        for where, declarations in self.faces_and_corners().items():
            self.assertRegex(declarations["box-shadow"], r"^var\(--(?:bevel|bevel-sm|btn-shadow|btn-shadow-sm)\)", where)
        # the pressed button and the small buttons (`.btn.sm`): from the same tokens
        for where, declarations in (("the front page's pressed button", rule(self.lobby_style, ".btn:active")), ("the game page's pressed button", rule(self.style, ".btn:active"))):
            self.assertTrue(declarations["box-shadow"].startswith("var(--bevel), "), where)
        self.assertTrue(rule(self.lobby_style, ".btn.sm")["box-shadow"].startswith("var(--bevel-sm), "))
        for token, bevel in (("--btn-shadow", "var(--bevel)"), ("--btn-shadow-down", "var(--bevel)"), ("--btn-shadow-sm", "var(--bevel-sm)")):
            self.assertTrue(roots["the style sheet of the changelog pages"][token].startswith(bevel + ", "), token)

    def test_the_two_state_buttons_are_the_front_pages_with_the_chosen_one_pressed_in(self):
        # The front page's two-state buttons are the labels of the footer's Screen radio buttons (`.shape .seg label`). They have the face of this page's `.seg button` (the colours, the frame, the corners,
        # the bevel and the red shadow) but not its size: the picture's footer buttons are smaller (min-height 30px, padding 3px 10px, font 14px), while this page's are the 42 px pair of the game's
        # menus, so only the face is compared, and the line height, which is the same.
        button, label = rule(self.style, ".seg button"), rule(self.lobby_style, ".shape .seg label")
        for name in ("line-height", "color", "background", "border", "border-radius"):
            self.assertEqual(button[name], label[name], name)
        # the shadow: the same small bevel, then a red shadow without blur. Pending a decision (reported): the offset of the red shadow differs, `.seg button` 2px 3px 0 var(--shadow)
        # against `.shape .seg label` 2px 2px 0 var(--shadow), so the offset is not compared.
        for shadow in (button["box-shadow"], label["box-shadow"]):
            self.assertRegex(shadow, r"^var\(--bevel-sm\), \d+px \d+px 0 var\(--shadow\)$")
        # Pending a decision (reported): the gap between the two buttons is 6px on this page (`.seg`) and 8px on the front page (`.shape .seg`), so it is not compared.
        chosen, theirs = rule(self.style, '.seg button[aria-checked="true"]'), rule(self.lobby_style, ".shape .seg input:checked + label")
        for name in ("color", "background", "transform", "box-shadow"):
            self.assertEqual(chosen[name], theirs[name], name)
        # the buttons are the same elements as ever (the page's script and the browser checks find them by these)
        for markup in ('id="aspect-16-9" data-aspect="16:9" aria-checked="true"', 'id="aspect-4-3" data-aspect="4:3" aria-checked="false"', 'id="lock-on" data-lock="on" aria-checked="true"',
                       'id="lock-off" data-lock="off" aria-checked="false"'):
            self.assertIn(markup, self.page)
        self.assertEqual(len(re.findall(r'<button type="button" role="radio" id="(?:aspect-16-9|aspect-4-3|lock-on|lock-off)"', self.page)), 4)
        self.assertEqual(len(re.findall(r'<div class="seg[ "][^>]*role="radiogroup"', self.page)), 3)           # (the picture and the mouse, and the replay bar's speed: TheReplayPage)

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

    def test_the_footer_is_the_front_pages_emerald_bar(self):
        footer, theirs = rule(self.style, "footer.bar"), rule(self.lobby_style, ".bar")
        for name in ("color", "font-size", "background", "border-top", "box-shadow"):
            self.assertEqual(footer[name], theirs[name], name)
        self.assertEqual(rule(self.style, ".bar .ver")["background"], rule(self.lobby_style, ".bar .ver")["background"])
        # Pending a decision (reported): the spacer's basis differs, `.bar .grow` flex 1 1 280px on this page against 1 1 40px on the front page, so only that it grows and shrinks alike is compared.
        self.assertEqual(rule(self.style, ".bar .grow")["flex"].split()[:2], rule(self.lobby_style, ".bar .grow")["flex"].split()[:2])

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
    CONTROLS = (("menu-btn", "/", "Menu"), ("watch-btn", "/watch.html", "Watch matches"), (None, "/asset_catalog/", "Sprites and sounds"), (None, "/changelog.html", "Changelog"), ("reset-btn", None, "Reset"),
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

    def test_the_eight_controls_are_there_in_their_order_in_the_teal_style(self):
        row = self.header()[self.header().index('<div class="header-actions">'):self.header().index('<details class="more">')]
        found = re.findall(r'<(a|button) ([^>]*)>(.*?)</\1>', row, re.S)
        self.assertEqual(len(found), 8)
        for (tag, attrs, text), (ident, href, label) in zip(found, self.CONTROLS):
            self.assertIn('class="btn sm', attrs, label)
            self.assertEqual(re.sub(r"<[^>]+>", "", text) if label != "Fullscreen" else re.search(r'<span class="btn-text">([^<]*)</span>', text).group(1), label)
            if ident:
                self.assertIn('id="%s"' % ident, attrs, label)
            if href:
                self.assertIn('href="%s"' % href, attrs, label)
                if href != "/":
                    self.assertIn('target="_blank" rel="noopener noreferrer"', attrs, label)                      # (only the Menu leaves in this tab, and asks first; Watch matches opens a new tab so that a match that is being played goes on)
                else:
                    self.assertNotIn("target=", attrs)
        # the six that "More" holds in a narrow window are the ones that the wide row marks
        self.assertEqual([("wide-only" in attrs) for _, attrs, _ in found], [False, True, True, True, True, False, True, True])
        self.assertIn('<span class="btn-text">Fullscreen</span><span class="btn-text-short">Full</span>', row)

    def test_more_holds_the_other_six_with_the_same_targets_and_needs_no_script(self):
        more = re.search(r'<details class="more">\s*<summary><span class="btn sm">More</span></summary>\s*<div class="more-list">(.*?)</div>\s*</details>', self.header(), re.S)
        self.assertIsNotNone(more)
        found = re.findall(r'<(a|button) ([^>]*)>(.*?)</\1>', more.group(1), re.S)
        wanted = [c for c in self.CONTROLS if c[2] in ("Watch matches", "Sprites and sounds", "Changelog", "Reset", "GitHub", "Feedback")]
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
        self.assertEqual(self.style.count(".wide-only {"), 1, "the one rule that hides the wide row's six")
        self.assertRegex(self.style, r"@media \(min-width: 701px\) and \(max-width: 1139px\) \{\s*#watch-btn \{\s*display: none;")          # (eight buttons need 1140 px for one row; between 701 and 1139 the header keeps the seven it had, and the footer link is the way)
        self.assertEqual(self.style.count("#watch-btn"), 1)
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
        self.assertEqual([(href, text) for href, _, text in found], [("/", "Menu"), ("/watch.html", "Watch matches"), ("/asset_catalog/", "Sprites and sounds"), ("/changelog.html", "Changelog"), (GITHUB, "GitHub"), (GITHUB + "/issues", "Feedback")])
        self.assertIn('id="menu-link"', found[0][1])
        self.assertNotIn("target=", found[0][1])
        self.assertIn('id="watch-link"', found[1][1])                                                           # (a new tab: a match that is being played here goes on; test_web_replay.py)
        self.assertIn('target="_blank"', found[1][1])
        lobby_nav = re.search(r'<nav aria-label="Footer links">(.*?)</nav>', self.lobby, re.S).group(1)
        self.assertEqual([text for _, _, text in re.findall(r'<a href="([^"]+)"([^>]*)>([^<]+)</a>', lobby_nav)], [text for _, _, text in found[1:]])     # (the same links, one "Watch matches": test_web_replay.py)


class TheGamePageHasNoFrameMode(PageCase):
    def test_nothing_of_the_old_frames_of_the_front_page_is_left(self):
        self.assertNotIn("body.embed", self.style)                                                                   # (the front page holds no game in a frame: the test room that did is gone)
        self.assertNotIn("ANTS_EMBED", self.page)
        self.assertNotIn("classList.add('embed')", self.page)


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

    CLAY_ENDS = ("#d84710", "#e95e24")                      # the two ends of the clay tile (its deepest and its lightest broad shade) that the front page's check measures against
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
