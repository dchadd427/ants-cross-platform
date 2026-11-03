#!/usr/bin/env python3
"""The hub's rules for untrusted text (hub/text.py; docs/HUB.md "Text rules"): what a visitor types is data, and only plain, visible, markup-free text gets through.

Tables of the tricks that are known (bidirectional overrides and isolates, zero-width characters, joiners, tag characters, the blank-looking letters, line separators, private
use, unassigned code points, lone surrogates, stacked marks), the limits at their edges (200 code points, 800 bytes), and a fuzz over random strings of the awkward categories that
checks the laws of the result instead of examples: nothing from a removed category, no markup, no doubled or edge blanks, NFC, at most 200 code points, and the same answer when it
is run again.
"""
import os
import random
import sys
import unicodedata
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, REPO)
from hub import text  # noqa: E402

REMOVED = ("Cc", "Cf", "Cs", "Co", "Cn")
BIDI = ["\u202a", "\u202b", "\u202c", "\u202d", "\u202e", "\u2066", "\u2067", "\u2068", "\u2069", "\u200e", "\u200f", "\u061c"]
INVISIBLE = ["\u200b", "\u200c", "\u200d", "\u2060", "\ufeff", "\u00ad", "\u180e", "\u2062", "\u2063", "\u206a", "\ufff9", "\U000e0041", "\U000e007f", "\U0001d173"]
FILLERS = ["\u3164", "\u115f", "\u1160", "\uffa0", "\u2800", "\u034f", "\u17b4", "\u17b5", "\U0001d159"]
BLANKS = [" ", "\t", "\n", "\r", "\x0b", "\x0c", "\x85", "\u00a0", "\u1680", "\u2000", "\u2007", "\u200a", "\u2028", "\u2029", "\u202f", "\u205f", "\u3000"]
OTHER_CONTROLS = ["\x00", "\x01", "\x07", "\x08", "\x1b", "\x1f", "\x7f", "\x80", "\x9f"]
UNASSIGNED = ["\u0378", "\uffff", "\U0010ffff", "\ufdd0"]
PRIVATE = ["\ue000", "\uf8ff", "\U000f0000", "\U0010fffd"]
SURROGATES = ["\ud800", "\udbff", "\udc00", "\udfff"]


def clean(raw):
    return text.clean_line(raw)


class TheTablesAreTrue(unittest.TestCase):
    """The tables above say what they claim (the Unicode data of this Python agrees), so a table cannot go stale unnoticed."""

    def test_categories(self):
        for ch in BIDI + INVISIBLE + OTHER_CONTROLS:
            self.assertIn(unicodedata.category(ch), ("Cf", "Cc"), hex(ord(ch)))
        for ch in UNASSIGNED:
            self.assertEqual(unicodedata.category(ch), "Cn", hex(ord(ch)))
        for ch in PRIVATE:
            self.assertEqual(unicodedata.category(ch), "Co", hex(ord(ch)))
        for ch in SURROGATES:
            self.assertEqual(unicodedata.category(ch), "Cs", hex(ord(ch)))
        for ch in BLANKS:
            self.assertTrue(unicodedata.category(ch) in ("Zs", "Zl", "Zp") or ch in "\t\n\r\x0b\x0c\x85", hex(ord(ch)))


class TheTricks(unittest.TestCase):
    def test_a_plain_line_is_unchanged(self):
        for line in ("hello", "hello world", "Good game! See you at the hill :)", "naive cafe", "\u0645\u0631\u062d\u0628\u0627", "\u4f60\u597d", "emoji \U0001f600 ok", "a_b.c-d (e) [f] {g} 'h' \"i\" & j / k \\ l"):
            self.assertEqual(clean(line), (line, None), line)

    def test_bidirectional_controls_are_removed(self):
        for ch in BIDI:
            self.assertEqual(clean("abc%sdef" % ch), ("abcdef", None), hex(ord(ch)))
        # the classic: an override that makes "moc.evil" read as "live.com"
        self.assertEqual(clean("see \u202emoc.live\u202c now"), ("see moc.live now", None))
        self.assertEqual(clean("\u202e"), (None, "empty"))

    def test_zero_width_characters_joiners_and_tags_are_removed(self):
        for ch in INVISIBLE:
            self.assertEqual(clean("ad%smin" % ch), ("admin", None), hex(ord(ch)))
        self.assertEqual(clean("".join(chr(0xe0000 + ord(c)) for c in "secret")), (None, "empty"))      # a message written in tag characters

    def test_the_blank_looking_letters_are_removed_and_a_line_of_them_is_empty(self):
        for ch in FILLERS:
            self.assertEqual(clean(ch), (None, "empty"), hex(ord(ch)))
            self.assertEqual(clean("a%sb" % ch), ("ab", None), hex(ord(ch)))
        self.assertEqual(clean(" ".join(FILLERS)), (None, "empty"))

    def test_other_controls_private_use_unassigned_and_surrogates_are_removed(self):
        for group in (OTHER_CONTROLS, PRIVATE, UNASSIGNED, SURROGATES):
            for ch in group:
                self.assertEqual(clean("a%sb" % ch), ("ab", None), hex(ord(ch)))
                self.assertEqual(clean(ch), (None, "empty"), hex(ord(ch)))

    def test_every_kind_of_blank_is_one_space_and_runs_collapse(self):
        for ch in BLANKS:
            self.assertEqual(clean("a%sb" % ch), ("a b", None), hex(ord(ch)))
            self.assertEqual(clean("%s%sa%s%s%sb%s" % (ch, ch, ch, ch, ch, ch)), ("a b", None), hex(ord(ch)))
        self.assertEqual(clean("a \u200b \u200b b"), ("a b", None))             # blanks that were apart only by a removed character
        self.assertEqual(clean("   "), (None, "empty"))
        self.assertEqual(clean(""), (None, "empty"))
        self.assertEqual(clean("\n\n\t"), (None, "empty"))

    def test_nfc_both_ways_of_writing_a_letter_are_one(self):
        self.assertEqual(clean("e\u0301"), ("\u00e9", None))
        self.assertEqual(clean("\u00e9"), ("\u00e9", None))
        self.assertEqual(clean("e\u200b\u0301"), ("\u00e9", None))              # a mark that the removed character kept from its letter joins it afterwards
        self.assertEqual(clean("\u1100\u1161\u11a8"), ("\uac01", None))          # Hangul jamo compose

    def test_a_column_of_marks_is_cut(self):
        self.assertEqual(clean("a" + "\u0301" * 3), ("\u00e1" + "\u0301" * 2, None))      # (the first mark composes)
        out = clean("a" + "\u0300\u0301\u0302\u0303\u0304\u0305\u0306\u0307\u0308\u0309" * 5)[0]
        marks = [c for c in out if unicodedata.category(c).startswith("M")]
        self.assertEqual(len(marks), text.MAX_MARKS)
        self.assertEqual(clean("a\u0301 b\u0301")[0], "\u00e1 b\u0301".replace("b\u0301", "b\u0301"))
        # marks that belong to the letters of a real script are fine (Thai above and below, Arabic with vowels)
        self.assertEqual(clean("\u0e01\u0e34\u0e49")[0], "\u0e01\u0e34\u0e49")
        self.assertEqual(clean("\u0643\u064e\u062a\u064e\u0628\u064e")[0], "\u0643\u064e\u062a\u064e\u0628\u064e")

    def test_a_line_of_marks_alone_shows_nothing_and_is_empty(self):
        for text_ in ("\ufe0f", "\ufe00\ufe01\ufe0e", "\u180b", "\u180c\u180d", "\u180f", "\U000e0100", "\U000e01ef", "\U00016fe4", "\u0301", "\u0301\u0300\u0302", " \ufe0f ", "\ufe0f \u180b \U000e0100",
                      "\u0e34\u0e49", "\u200d\ufe0f"):
            self.assertEqual(clean(text_), (None, "empty"), repr(text_))
        for ch in ("\ufe0f", "\u180b", "\U000e0100", "\U00016fe4", "\u0301"):
            self.assertEqual(unicodedata.category(ch)[0], "M", hex(ord(ch)))      # (they are what the table says they are)
        # with a letter anywhere, they are a line: the marks belong to something
        self.assertEqual(clean("a\ufe0f"), ("a\ufe0f", None))
        self.assertEqual(clean("\ufe0f a"), ("\ufe0f a", None))
        self.assertEqual(clean("\u2764\ufe0f"), ("\u2764\ufe0f", None))
        self.assertEqual(clean("1"), ("1", None))
        self.assertEqual(clean("."), (".", None))

    def test_markup_is_refused_after_the_hiding_places_are_gone(self):
        for line in ("<b>hi</b>", "a < b", "a > b", "<script>alert(1)</script>", "x<", ">x", "<\u200bscript>", "<\u202e>", "<\u3164>"):
            self.assertEqual(clean(line), (None, "bad"), repr(line))
        self.assertEqual(clean("&lt;b&gt; &amp; &#60;"), ("&lt;b&gt; &amp; &#60;", None))      # an entity is text: the page shows it as typed, nothing decodes it
        self.assertEqual(clean("\u2264 \u2265 \uff1c \uff1e"), ("\u2264 \u2265 \uff1c \uff1e", None))     # look-alikes are text too
        self.assertEqual(clean("a \u226e b"), ("a \u226e b", None))                # NOT LESS-THAN is one letter; NFC makes "<" + U+0338 into it, so no "<" is left
        self.assertEqual(clean("<\u0338"), ("\u226e", None))

    def test_not_a_string_is_bad(self):
        for value in (None, 5, 1.5, True, [], {}, b"abc", ["a"]):
            self.assertEqual(clean(value), (None, "bad"), repr(value))

    def test_a_huge_input_is_long_without_the_work(self):
        self.assertEqual(clean("a" * 100000), (None, "long"))
        self.assertEqual(clean("\u200b" * 5000 + "a"), (None, "long"))         # (more than a frame can carry: refused unread)


class TheLimits(unittest.TestCase):
    def test_200_code_points_are_the_edge(self):
        self.assertEqual(clean("a" * 200), ("a" * 200, None))
        self.assertEqual(clean("a" * 201), (None, "long"))
        self.assertEqual(clean("a" * 199), ("a" * 199, None))

    def test_the_count_is_after_the_rules(self):
        self.assertEqual(clean("a\u200b" * 200)[1], None)                       # 200 letters and 200 hidden ones
        self.assertEqual(clean("a\u200b" * 201)[1], "long")
        self.assertEqual(clean("a " * 100 + "     ")[0], ("a " * 100).strip())      # 199 code points with the spaces
        self.assertEqual(clean("ab " * 67)[1], None)                           # 200 with the spaces (trailing one trimmed)
        self.assertEqual(clean("ab " * 68)[1], "long")

    def test_nfc_decides_the_count(self):
        self.assertEqual(clean("e\u0301" * 200), ("\u00e9" * 200, None))        # 400 code points typed, 200 after NFC
        self.assertEqual(clean("e\u0301" * 201)[1], "long")

    def test_800_bytes_are_the_other_edge(self):
        four = "\U0001f600"                                                      # 4 bytes in UTF-8
        self.assertEqual(clean(four * 200), (four * 200, None))                  # 200 code points, exactly 800 bytes
        three = "\u4f60"
        self.assertEqual(len((three * 200).encode()), 600)
        self.assertEqual(clean(three * 200)[1], None)
        # a line of 200 code points can never be over 800 bytes in UTF-8, so the byte rule is the same edge; it still has to hold if the code point rule were moved
        original = text.MAX_CODE_POINTS
        try:
            text.MAX_CODE_POINTS = 1000
            self.assertEqual(clean(four * 200)[1], None)
            self.assertEqual(clean(four * 201)[1], "long")
            self.assertEqual(clean("a" * 800)[1], None)
            self.assertEqual(clean("a" * 801)[1], "long")
        finally:
            text.MAX_CODE_POINTS = original

    def test_order_of_the_verdicts(self):
        self.assertEqual(clean("<" * 300)[1], "long")                          # too long is said before the markup
        self.assertEqual(clean("\u200b<")[1], "bad")
        self.assertEqual(clean("\u200b")[1], "empty")


class TheNames(unittest.TestCase):
    def test_a_name_is_what_a_discord_username_is(self):
        for good in ("maple.leaf", "ab", "a" * 32, "x_y.z9", "01"):
            self.assertTrue(text.valid_name(good), good)
        for bad in ("", "a", "a" * 33, "Maple", "ma ple", "m\u00e4ple", "maple\n", "maple\u200b", "ma-ple", "<b>", "\u0430bc", None, 5, b"abc"):
            self.assertFalse(text.valid_name(bad), repr(bad))


def alphabet():
    """The awkward characters, grouped, for the fuzz."""
    plain = list("abcXYZ019 .,!?-_'\"&;:()/\\") + ["\u00e9", "e\u0301", "\u4f60", "\u0645", "\U0001f600", "\u0e01", "\u0e34"]
    awkward = BIDI + INVISIBLE + FILLERS + BLANKS + OTHER_CONTROLS + UNASSIGNED + PRIVATE + SURROGATES + ["<", ">", "<\u0338", "\u0301", "\u0300", "\u0338", "\u1100", "\u1161", "\u11a8"]
    return plain, awkward


class TheLawsHold(unittest.TestCase):
    """A fuzz over random strings: the laws of the result, not examples. Reproducible (a fixed seed); ANTS_HUB_FUZZ_SCALE=10 runs ten times more."""

    CASES = 20000 * int(os.environ.get("ANTS_HUB_FUZZ_SCALE", "1"))

    def test_the_laws(self):
        rnd = random.Random(20261029)
        plain, awkward = alphabet()
        everything = list(range(0x110000))
        kept = refused = 0
        for _ in range(self.CASES):
            kind = rnd.random()
            if kind < 0.45:
                raw = "".join(rnd.choice(plain if rnd.random() < 0.6 else awkward) for _ in range(rnd.randint(0, 40)))
            elif kind < 0.9:
                raw = "".join(chr(rnd.choice(everything)) for _ in range(rnd.randint(0, 30)))
            else:
                raw = "".join(rnd.choice(plain) for _ in range(rnd.randint(150, 260)))
            out, why = clean(raw)
            if why:
                refused += 1
                self.assertIsNone(out)
                self.assertIn(why, ("empty", "long", "bad"))
                continue
            kept += 1
            self.assertTrue(out, repr(raw))
            self.assertEqual(out, unicodedata.normalize("NFC", out), repr(raw))
            self.assertLessEqual(len(out), 200)
            self.assertLessEqual(len(out.encode("utf-8")), 800)
            self.assertNotIn("<", out)
            self.assertNotIn(">", out)
            self.assertNotIn("  ", out)
            self.assertEqual(out, out.strip(" "))
            for ch in out:
                category = unicodedata.category(ch)
                self.assertNotIn(category, REMOVED, "%r kept %r" % (raw, ch))
                self.assertNotIn(category, ("Zs", "Zl", "Zp") if ch != " " else (), repr(ch))
                self.assertNotIn(ch, FILLERS)
            run = 0
            for ch in out:
                run = run + 1 if unicodedata.category(ch).startswith("M") else 0
                self.assertLessEqual(run, text.MAX_MARKS)
            self.assertEqual(clean(out), (out, None), "not stable: %r" % raw)
        self.assertGreater(kept, self.CASES // 4)
        self.assertGreater(refused, 100)

    def test_a_string_of_safe_characters_comes_out_as_it_went_in(self):
        rnd = random.Random(7)
        plain = [c for c in alphabet()[0] if c != " "]
        for _ in range(3000):
            words = ["".join(rnd.choice(plain) for _ in range(rnd.randint(1, 8))) for _ in range(rnd.randint(1, 12))]
            raw = " ".join(words)
            expected = unicodedata.normalize("NFC", raw)
            if not any(c != " " and not unicodedata.category(c).startswith("M") for c in expected):
                self.assertEqual(clean(raw), (None, "empty"))              # (a word of marks alone shows nothing)
            elif len(expected) <= 200:
                self.assertEqual(clean(raw), (expected, None))


if __name__ == "__main__":
    unittest.main()
