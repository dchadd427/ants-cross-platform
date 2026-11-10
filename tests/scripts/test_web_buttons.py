#!/usr/bin/env python3
"""The texture of the teal buttons on every page (run by ./run_tests.sh --fast and by the CI).

A teal button has a double frame (a dark edge, a light line of 1 px inside it, a dark groove), a faint 45 degree canvas weave, and the deepest press (3 px right and 4 px down, dark inside, the red
shadow gone). A chosen button (aria-pressed, aria-expanded, .on) is the flat dark plate with the gold words, with no weave. All of it is custom properties, so a palette (docs/HOME_SCREEN.md) can
swap the colors and the texture follows: --teal --teal-hi --teal-lo --edge --shadow, and --weave --sheen --bevel --bevel-sm --bevel-dn --btn-text for the texture itself.
What is read here (no browser):
  - the four places that carry the tokens (the front page, the game page, the match pages' style sheet, the changelog pages' style sheet) say the same, the hover color is the one with enough contrast
  - the face of a button is written `background: var(--sheen) <color>` everywhere (the sheen is several layers, and a color may only be in the last one: written the other way round, the declaration is
    invalid and the button is plain), and no page has the old order left
  - a forced-colors window turns the weave, the sheen and the shadow of the words off, on every page that carries the tokens
  - the pressed rule, the chosen rule, the focus outline, the disabled filter, and no transition on a button
The same in a real browser (pictures of every page, the measured contrast of the words): see the pull request that brought the texture.
"""
import glob
import os
import re
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def strip(text):
    return re.sub(r"/\*.*?\*/", "", text, flags=re.S)


CARRIERS = {"web/lobby.html": "the front page", "web/shell.html": "the game page", "web/matches.css": "the match pages' style sheet", "web/front/classic.css": "the changelog pages' style sheet"}
TOKENS = ("weave", "sheen", "bevel", "bevel-sm", "bevel-dn", "btn-text", "teal-hi")


def token(text, name):
    found = re.findall(r"--%s:\s*([^;]+);" % re.escape(name), strip(text))
    return " ".join(found[0].split()) if found else None


def lum(rgb):
    def lin(c):
        c /= 255.0
        return c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4
    return 0.2126 * lin(rgb[0]) + 0.7152 * lin(rgb[1]) + 0.0722 * lin(rgb[2])


def ratio(a, b):
    a, b = lum(a), lum(b)
    return (max(a, b) + 0.05) / (min(a, b) + 0.05)


def hexrgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))


class TheTokensAgree(unittest.TestCase):
    def test_the_four_places_say_the_same(self):
        for name in TOKENS:
            values = {where: token(read(path), name) for path, where in CARRIERS.items() if not (name == "teal-hi" and path == "web/matches.css")}      # (the match pages say their colors in each page: below)
            self.assertNotIn(None, values.values(), "--%s: %s" % (name, values))
            self.assertEqual(len(set(values.values())), 1, "--%s: %s" % (name, values))

    def test_the_weave_is_two_faint_diagonal_line_patterns(self):
        weave = token(read("web/lobby.html"), "weave")
        self.assertEqual(len(re.findall(r"repeating-linear-gradient\(", weave)), 2, weave)
        self.assertIn("repeating-linear-gradient(45deg, rgba(255, 255, 255, .03) 0 1px", weave)
        self.assertIn("repeating-linear-gradient(-45deg, rgba(0, 0, 0, .12) 0 1px", weave)
        self.assertEqual(len(re.findall(r"1px 4px\)", weave)), 2, "a pitch of 4 px")
        sheen = token(read("web/lobby.html"), "sheen")
        self.assertTrue(sheen.startswith("var(--weave), "), sheen)

    def test_the_hover_color_keeps_the_words_readable(self):
        # --teal-hi under the brightest light of the weave (3% white) must still carry the cream words at 4.5:1; the older #31765f gave 4.3 (the measured worst pixel of a hovered Quick join: 4.28)
        hi = hexrgb(token(read("web/lobby.html"), "teal-hi"))
        lit = tuple(round(c * 0.97 + 255 * 0.03) for c in hi)
        self.assertGreaterEqual(ratio(hexrgb("#f4ece2"), lit), 4.5)
        self.assertGreaterEqual(ratio(hexrgb("#f4ece2"), hi), 4.9)
        for page in ("web/live.html", "web/match.html", "web/matches.html"):
            self.assertIn("--teal-hi: %s;" % token(read("web/lobby.html"), "teal-hi"), read(page), page)


class TheFaceOrder(unittest.TestCase):
    def test_the_sheen_comes_first_and_the_color_last_in_every_face(self):
        seen = 0
        files = [p for p in glob.glob(os.path.join(REPO, "web", "**", "*"), recursive=True) if p.endswith((".css", ".html", ".js"))]
        files += [os.path.join(REPO, "tools", "changelog_to_html.py"), os.path.join(REPO, "asset_catalog", "index.html")]
        for path in sorted(files):
            with open(path, encoding="utf-8") as f:
                text = strip(f.read())
            for found in re.finditer(r"background:\s*([^;}]*var\(--sheen[^;}]*)", text):
                seen += 1
                value = " ".join(found.group(1).split())
                self.assertRegex(value, r"^var\(--sheen(?:, none)?\) (?:var\(--teal(?:, #[0-9a-f]{6})?\)|#[0-9a-f]{6})$", "%s: %s" % (os.path.relpath(path, REPO), value))
            self.assertNotRegex(text, r"background:[^;}]*\)\s+var\(--sheen", os.path.relpath(path, REPO))
        self.assertGreaterEqual(seen, 30, "the faces that carry the sheen: found %d" % seen)


class TheStates(unittest.TestCase):
    def test_forced_colors_turn_the_texture_off(self):
        for path, where in CARRIERS.items():
            text = strip(read(path))
            found = re.search(r"@media \(forced-colors: active\) \{\s*:root \{([^}]*)\}", text)
            self.assertIsNotNone(found, where)
            body = " ".join(found.group(1).split())
            self.assertIn("--weave: linear-gradient(#0000, #0000);", body, where)
            self.assertIn("--sheen: linear-gradient(#0000, #0000);", body, where)
            self.assertIn("--btn-text: none;", body, where)
            self.assertLess(text.index(found.group(0)), text.index("{", text.index(".btn {")) , where + ": the guard comes after the tokens")

    def test_the_press_is_the_deepest(self):
        pressed = {"web/lobby.html": r"\.btn:active \{ transform: translate\(3px, 4px\); box-shadow: var\(--bevel-dn\), 0 0 0 var\(--shadow\); \}",
                   "web/matches.css": r"\.btn:active \{ transform: translate\(3px, 4px\); box-shadow: var\(--bevel-dn\), 0 0 0 var\(--shadow\); \}",
                   "web/shell.html": r"\.btn:active \{\s*transform: translate\(3px, 4px\);\s*box-shadow: var\(--bevel-dn\), 0 0 0 var\(--shadow\);",
                   "web/front/classic.css": r"\.btn:active \{ transform: translate\(3px, 4px\); box-shadow: var\(--btn-shadow-down\); \}"}
        for path, pattern in pressed.items():
            self.assertRegex(strip(read(path)), pattern, path)
        # (the press comes after `.btn.sm`, which says a box-shadow too, so a small button is pressed in as well)
        for path in ("web/lobby.html", "web/matches.css"):
            text = strip(read(path))
            self.assertLess(text.index(".btn.sm {"), text.index(".btn:active {"), path)

    def test_a_chosen_button_has_no_weave(self):
        text = strip(read("web/lobby.html"))
        self.assertIn('.btn[aria-pressed="true"], .btn[aria-expanded="true"], .btn.on { background-image: none; }', text)
        css = strip(read("web/home/home.css") + read("web/home/games.css"))
        for selector in ('.htab[aria-pressed="true"]', '.hmenu[aria-expanded="true"]', '.hchip.games[aria-expanded="true"]', '.vbtn[aria-pressed="true"]'):
            found = re.search(re.escape(selector) + r" \{([^}]*)\}", css)
            self.assertIsNotNone(found, selector)
            self.assertIn("background-color: #0b1a17;", found.group(1), selector)
            self.assertNotIn("background:", found.group(1).replace("background-color:", ""), selector)

    def test_focus_and_disabled_are_as_they_were(self):
        self.assertIn(":focus-visible { outline: 3px solid var(--gold); outline-offset: 3px; }", strip(read("web/lobby.html")))
        self.assertIn(":focus-visible { outline: 3px solid var(--gold); outline-offset: 3px; }", strip(read("web/matches.css")))
        self.assertIn("a:focus-visible, button:focus-visible", strip(read("web/front/classic.css")))
        self.assertIn(".btn[disabled] { cursor: default; filter: saturate(.45) brightness(.78); transform: none; }", strip(read("web/lobby.html")))
        self.assertIn("outline: 3px solid var(--gold);", strip(read("web/shell.html")))

    def test_the_words_have_a_shadow_and_nothing_moves_by_itself(self):
        for path in CARRIERS:
            text = strip(read(path))
            self.assertIn("text-shadow: var(--btn-text);", text, path)
            btn = re.search(r"\.btn \{[^}]*\}", text).group(0)
            self.assertNotIn("transition", btn, path)
            self.assertNotIn("animation", btn, path)


if __name__ == "__main__":
    unittest.main()
