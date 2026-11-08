#!/usr/bin/env python3
"""The hub's rules for untrusted text (hub/text.py; docs/HUB.md "Text rules"): what a visitor types is data, and only plain, visible, markup-free text gets through.

Tables of the tricks that are known (bidirectional overrides and isolates, zero-width characters, joiners, tag characters, the blank-looking letters, line separators, private
use, unassigned code points, lone surrogates, stacked marks), the limits at their edges (200 code points, 800 bytes), and a fuzz over random strings of the awkward categories that
checks the laws of the result instead of examples: nothing from a removed category, no markup, no doubled or edge blanks, NFC, at most 200 code points, and the same answer when it
is run again.

The name that the chat shows (clean_shown: the Discord display name, which is free text from Discord) has its own tables below: the scripts that must pass, every kind of character that is
taken out, the marks, the edges of the length, the reserved names with their lookalikes (digits, punctuation, accents, Cyrillic and Greek letters, fancy and full-width letters), what is not
text at all; and a fuzz of the laws, including that cleaning a clean name changes nothing.
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


# ---- the name that the chat shows (the Discord display name) ---------------------------------------------------------------------------------------------------

def shown(raw):
    return text.clean_shown(raw)


# (what Discord said, what the chat shows): a name that is fine comes out as it went in, in NFC
SHOWN_KEPT = [
    "Maple", "maple.leaf", "Mary Jane", "O'Brien", "Jean-Luc", "snake_case", "Mr. X", "a1", "007", "Zo\u00eb", "Jos\u00e9 Mu\u00f1oz", "\u00dcnal", "na\u00efve caf\u00e9", "Nguy\u1ec5n V\u0103n", "ab", "x" * 32,
    "\u041c\u0430\u0440\u0438\u044f", "\u0418\u0432\u0430\u043d \u041f\u0435\u0442\u0440\u043e\u0432", "\u039d\u03af\u03ba\u03bf\u03c2", "\u0395\u03bb\u03ad\u03bd\u03b7",          # Cyrillic and Greek (with their accents)
    "\u7530\u4e2d\u592a\u90ce", "\u738b\u5c0f\u660e", "\u3055\u304f\u3089", "\u30ab\u30bf\u30ab\u30ca", "\u9234\u6728 \u3055\u304f\u3089", "\uae40\ubbfc\uc218",                          # Chinese, Japanese, Korean
    "\u0645\u0631\u064a\u0645", "\u0645\u064e\u0631\u0652\u064a\u064e\u0645", "\u05d3\u05d5\u05d3", "\u05d3\u05b8\u05bc\u05d5\u05b4\u05d3",                                      # Arabic (and with its vowels), Hebrew (and with niqqud: two marks on a letter)
    "\u0930\u093e\u091c\u0947\u0936", "\u0915\u094d\u0937\u0924\u094d\u0930\u093f\u092f", "\u0e2a\u0e21\u0e0a\u0e32\u0e22", "\u0e01\u0e23\u0e38\u0e07\u0e40\u0e17\u0e1e", "\u0e01\u0e34\u0e49\u0e07",    # Devanagari, Thai (with its vowels and tone marks)
    "Dave \u05d3\u05d5\u05d3", "\u0645\u0631\u064a\u0645 Maple", "\u0661\u0662\u0663",                                                                                          # right to left next to left to right; Arabic-Indic digits
]
# (label, what Discord said, what the chat shows)
SHOWN_CASES = [
    # blanks
    ("blanks are one space and the ends are trimmed", "  Maple   Leaf  ", "Maple Leaf"),
    ("tabs and line ends are blanks", "Maple\tLeaf\nTwo\r\nThree", "Maple Leaf Two Three"),
    ("every kind of space is one", "a\u00a0b\u2003c\u3000d\u2028e\u2029f\u0085g\u1680h", "a b c d e f g h"),
    # normalization
    ("a letter and its accent are one letter", "e\u0301a", "\u00e9a"),
    ("math script letters are plain ones", "\U0001d4dc\U0001d4ea\U0001d4f9\U0001d4f5\U0001d4ee", "Maple"),
    ("double-struck letters are plain ones", "\U0001d544\U0001d552\U0001d561\U0001d55d\U0001d556", "Maple"),
    ("full-width letters are plain ones", "\uff2d\uff41\uff50\uff4c\uff45", "Maple"),
    ("circled letters are plain ones", "\u24dc\u24d0\u24df\u24db\u24d4", "maple"),
    ("small capitals and modifier letters stay letters", "\u1d0d\u1d00\u1d18", "\u1d0d\u1d00\u1d18"),
    ("a ligature is its letters", "\ufb01sh", "fish"),
    ("a superscript digit is a digit", "x\u00b2y", "x2y"),
    ("a roman numeral is letters", "Mr \u216b", "Mr XII"),
    ("a name of two letters can grow when it is normalized, and is cut", "\ufdfa\ufdfa",
     "\u0635\u0644\u0649 \u0627\u0644\u0644\u0647 \u0639\u0644\u064a\u0647 \u0648\u0633\u0644\u0645" "\u0635\u0644\u0649 \u0627\u0644\u0644\u0647 \u0639\u0644\u064a\u0647"),     # (eighteen letters each; the cut at thirty-two ends in a blank, which goes)
    # characters that show nothing, or that change how text is laid out, are removed
    ("zero-width space, joiners and the word joiner", "Ma\u200bp\u200cl\u200de\u2060", "Maple"),
    ("the bidirectional controls, an override too", "\u202eelpaM\u202c\u200f\u200e\u2066x\u2069", "elpaMx"),
    ("the byte order mark and the soft hyphen", "\ufeffMa\u00adple", "Maple"),
    ("tag characters", "Ma\U000e0041ple\U000e007f", "Maple"),
    ("the letters that draw as blanks", "Ma\u3164p\u115fl\u1160e\uffa0", "Maple"),
    ("the braille blank and the combining grapheme joiner", "Ma\u2800p\u034fle", "Maple"),
    ("controls", "Ma\x00p\x07l\x1be\x7f\x9f", "Maple"),
    ("private use, unassigned and lone surrogates", "Ma\ue000p\U000f0000l\u0378e\ud800" "\udfffx\udbff", "Maplex"),
    ("variation selectors", "Maple\ufe0f\ufe0e\U000e0100\u180b", "Maple"),
    # emoji, symbols and the rest of the punctuation are removed
    ("an emoji at the end", "Maple \U0001f341", "Maple"),
    ("emoji around", "\U0001f525Fire\U0001f525", "Fire"),
    ("a name of emoji only is none", "\U0001f600\U0001f600\U0001f44d", ""),
    ("a family and a flag are none", "\U0001f468\u200d\U0001f469\u200d\U0001f467\U0001f1fa\U0001f1f8", ""),
    ("a skin tone", "Ma\U0001f44d\U0001f3fdple", "Maple"),
    ("symbols", "a\u2665b\u2605c\u00a9d\u2192e\u26a1f\u263a", "abcdef"),
    ("a trade mark sign is the two letters that it stands for", "Maple\u2122", "MapleTM"),
    ("an ellipsis is three dots", "Maple\u2026", "Maple..."),
    ("stars around a name", "\u2605\u5f61Maple\u5f61\u2605", "\u5f61Maple\u5f61"),
    ("brackets, quotes, marks of sentences", "(Maple)[x]{y}\u201cz\u201d\u00abw\u00bb!?,;:", "Maplexyzw"),
    ("markup is only letters once the signs are gone", "<b>Maple</b>", "bMapleb"),
    ("a mention and a slash and a hash", "@Maple #1 a/b\\c", "Maple 1 abc"),
    ("a keycap is a digit", "a1\ufe0f\u20e3", "a1"),
    ("a lone keycap is nothing", "1\ufe0f\u20e3", ""),
    ("the other numbers", "a\u00bdb\u2460c", "a12b1c"),
    # the marks that stay, and the plain forms of curly ones
    ("the four marks stay", "a.b_c-d'e", "a.b_c-d'e"),
    ("curly apostrophes are plain ones", "Jo\u2019s \u2018Ants\u2019", "Jo's 'Ants'"),
    ("dashes of every length are a hyphen", "A\u2010B\u2011C\u2012D\u2013E\u2014F\u2015G", "A-B-C-D-E-F-G"),
    # combining marks
    ("a third mark in a row is cut (the first of them composes with its letter before they are counted)", "a\u0323\u0301\u0302\u0303\u0304b", "\u1ea1\u0301\u0302b"),
    ("marks that a script uses stay (Thai above and below, Arabic with vowels)", "\u0e01\u0e34\u0e49 \u0643\u064e\u062a\u064e", "\u0e01\u0e34\u0e49 \u0643\u064e\u062a\u064e"),
    ("a mark with no letter before it goes", "\u0301Maple", "Maple"),
    ("a mark after a blank goes", "Ma \u0301ple", "Ma ple"),
    ("a mark after a punctuation mark goes", "a.\u0301b", "a.b"),
    ("a mark after something that was removed joins the letter before it", "a\U0001f642\u0301b", "\u00e1b"),
    ("a mark that encloses goes", "ab\u20dd\u20e2", "ab"),
    ("a mark that is all there is, is none", "\u0301\u0302\u0303", ""),
    ("a column of marks above a letter (the look of damaged text) is two", "Z" + "\u0351\u0352\u0353\u0354\u0355\u0356" * 4 + "a", "Z\u0353\u0354a"),         # (the marks are put in their order first: the two that come first are the two that stay)
    # the length: 2 to 32 code points
    ("one letter is too short", "a", ""),
    ("one letter and an emoji is too short", "a\U0001f642", ""),
    ("one Chinese character is too short", "\u65e5", ""),
    ("one letter and a mark that composes is too short", "e\u0301", ""),
    ("two is the least", "ab", "ab"),
    ("two Chinese characters", "\u65e5\u672c", "\u65e5\u672c"),
    ("thirty-two stay", "a" * 32, "a" * 32),
    ("thirty-three are cut to thirty-two", "a" * 33, "a" * 32),
    ("a long one is cut at a code point", "abcdefghij" * 4, "abcdefghij" * 3 + "ab"),
    ("a cut that ends in a blank is trimmed again", "a" * 31 + " b", "a" * 31),
    ("the most that is read, cut", "a" * 128, "a" * 32),
    ("one more than that is not read", "a" * 129, ""),
    ("a huge input is not read", "a" * 100000, ""),
    ("a huge input of nothing but removed characters is not read either", "\u200b" * 100000, ""),
    ("a name of nothing", "", ""),
    ("blanks only", " \t\n\u00a0", ""),
    ("only characters that are removed", "\u200b\u202e\u3164\ufe0f", ""),
    # at least a letter or a digit
    ("dots only", "..", ""),
    ("hyphens and underscores", "-_- __ '", ""),
    ("an emoticon of marks", "._. ,_,", ""),
    ("a digit is enough", "-1-", "-1-"),
    ("digits are fine", "007", "007"),
    # more of what people put in a display name
    ("an embedding of right to left text", "\u202b\u0645\u0631\u064a\u0645\u202c", "\u0645\u0631\u064a\u0645"),
    ("marks of direction around a Hebrew name", "\u200f\u05d3\u05d5\u05d3\u200e", "\u05d3\u05d5\u05d3"),
    ("an override inside a name", "Dave \u202eevad", "Dave evad"),
    ("pronouns in brackets lose their brackets and their slash", "Alex (they/them)", "Alex theythem"),
    ("a bar between words", "Maple | Ants Player", "Maple Ants Player"),
    ("a tag in brackets", "[TAG] Maple", "TAG Maple"),
    ("a discriminator", "Maple#1234", "Maple1234"),
    ("full-width letters and the ideographic space", "\uff2d\uff41\uff50\uff4c\uff45\u3000\uff2c\uff45\uff41\uff46", "Maple Leaf"),
    ("full-width digits", "\uff11\uff12", "12"),
    ("a joiner between an emoji and a letter", "a\u200d\U0001f600b", "ab"),
    ("a NUL", "Maple\x00", "Maple"),
    ("the case is kept", "mApLe", "mApLe"),
    ("a name that starts and ends with a mark", "-Maple-", "-Maple-"),
    ("quotes of the plain kind around", "'Maple'", "'Maple'"),
    ("Hangul jamo are composed", "x\u1100\u1161\u11a8", "x\uac01"),
    ("a lone Hangul syllable is too short", "\u1100\u1161\u11a8", ""),
    ("a letter with an accent that is a letter of its own", "\u0142ukasz S\u00f8ren \u00c5sa", "\u0142ukasz S\u00f8ren \u00c5sa"),
    ("a lot of invisible characters around a name is not Discord's", "\u200b" * 150 + "Maple", ""),
    ("a hundred and twenty-eight is read, with the invisible ones taken out", "\u200b" * 120 + "Maple", "Maple"),
    ("a reserved word with an invisible character between each letter", "m\u200bo\u200bd", ""),
    ("a reserved word in superscript letters", "\u1d43\u1d48\u1d50\u2071\u207f", ""),
    # reserved names
    ("mod", "mod", ""), ("Mod", "Mod", ""), ("MOD", "MOD", ""), ("admin", "admin", ""), ("Admin", "Admin", ""), ("ADMIN", "ADMIN", ""), ("moderator", "Moderator", ""),
    ("administrator", "Administrator", ""), ("system", "System", ""), ("server", "SERVER", ""), ("staff", "staff", ""), ("ants", "Ants", ""), ("ants.bot", "ants.bot", ""), ("a name with a space", "Ants Bot", ""),
    ("hub", "Hub", ""), ("everyone", "Everyone", ""), ("here", "here", ""),
    ("digits for letters", "Adm1n", ""), ("m0d", "m0d", ""), ("syst3m", "Syst3m", ""), ("st4ff", "St4ff", ""), ("3veryone", "3veryone", ""), ("h3r3", "h3r3", ""), ("4nts", "4nts", ""), ("m0d3r4t0r", "m0d3r4t0r", ""),
    ("dots, underscores, hyphens, apostrophes and spaces", "a.d.m.i.n", ""), ("spaced", "a d m i n", ""), ("underscores", "a_d_m_i_n", ""), ("hyphen", "Ad-min", ""), ("apostrophe", "Ad'min", ""),
    ("a name with dots around it", ". admin .", ""), ("a dot at the end", "admin.", ""),
    ("an accent", "Adm\u00edn", ""), ("an accent over the first letter", "\u00c1dmin", ""), ("a mark that is a column of accents", "A\u0301d\u0302m\u0303i\u0304n\u0305", ""),
    ("hidden characters inside", "Ad\u200bmi\u202en", ""),
    ("fancy letters", "\U0001d400\U0001d41d\U0001d426\U0001d422\U0001d427", ""), ("full-width letters", "\uff21\uff24\uff2d\uff29\uff2e", ""),
    ("a Cyrillic a", "\u0430dmin", ""), ("a Cyrillic A", "\u0410dmin", ""), ("Cyrillic a and i", "\u0430dm\u0456n", ""), ("a Cyrillic s", "\u0455ystem", ""), ("a Cyrillic o", "m\u043ed", ""),
    ("a Cyrillic M", "\u041cod", ""), ("a Cyrillic h", "\u04bbub", ""), ("a Cyrillic e and r", "serve\u0433", ""), ("Cyrillic t and a", "s\u0442\u0430ff", ""), ("a Cyrillic y and o", "ever\u0443\u043ene", ""),
    ("a Greek Alpha", "\u0391dmin", ""), ("all capitals, Greek and Latin", "\u0391D\u039c\u0399\u039d", ""), ("a Greek omicron", "m\u03bfd", ""), ("Greek epsilon, nu, upsilon, omicron and eta", "\u03b5\u03bd\u03b5r\u03a5\u03bf\u03b7\u03b5", ""), ("a Greek gamma", "ever\u03b3one", ""), ("a Greek digamma", "sta\u03dc\u03dc", ""),
    ("a dotless i", "adm\u0131n", ""), ("Greek tau and kappa", "s\u03c4\u03b1ff", ""),
    ("all of it at once", "\u0410\u2009d\u200bm\u0301\u0456\u02bc-\uff4e\ufe0f", ""),
    # names that are not reserved: only the whole name is read, as it is for a username
    ("a word and more", "Admin Dave", "Admin Dave"), ("modern", "Modern", "Modern"), ("mods", "Mods", "Mods"), ("a number after", "system32", "system32"), ("hub99", "hub99", "hub99"), ("ants1998", "ants1998", "ants1998"),
    ("antsy", "Antsy", "Antsy"), ("staffan", "Staffan", "Staffan"), ("a phrase", "Here Be Dragons", "Here Be Dragons"), ("an ant", "Ant Farm", "Ant Farm"),
    ("Russian for administrator is not a lookalike", "\u0410\u0434\u043c\u0438\u043d", "\u0410\u0434\u043c\u0438\u043d"), ("Chinese for administrator", "\u7ba1\u7406\u5458", "\u7ba1\u7406\u5458"),
    ("a name that has a word of another that is only close", "Admiral", "Admiral"), ("a plural", "Servers", "Servers"), ("a word that holds a reserved word", "Moderate", "Moderate"),
    ("Cyrillic that is not a lookalike of anything reserved", "\u041c\u043e\u0434", "\u041c\u043e\u0434"), ("Greek that is not", "\u039c\u03bf\u03bd\u03ac\u03b4\u03b1", "\u039c\u03bf\u03bd\u03ac\u03b4\u03b1"),
]
NOT_TEXT = [None, 5, 1.5, True, False, [], {}, b"abc", ["Maple"], {"name": "Maple"}, ("Maple",), object()]


class TheShownName(unittest.TestCase):
    def test_a_name_that_is_fine_comes_out_as_it_went_in(self):
        for name in SHOWN_KEPT:
            self.assertEqual(shown(name), unicodedata.normalize("NFC", name), repr(name))
            self.assertEqual(shown(unicodedata.normalize("NFD", name)), unicodedata.normalize("NFC", name), "the same name written in the other form: " + repr(name))

    def test_the_table(self):
        wrong = [(label, raw, shown(raw), want) for label, raw, want in SHOWN_CASES if shown(raw) != want]
        self.assertEqual(wrong, [])
        self.assertGreater(len(SHOWN_CASES) + len(SHOWN_KEPT), 200)

    def test_the_labels_are_not_used_twice_and_every_reserved_word_and_every_script_is_in_the_table(self):
        labels = [label for label, _, _ in SHOWN_CASES]
        self.assertEqual(len(labels), len(set(labels)), [l for l in labels if labels.count(l) > 1])
        for word in text.RESERVED:
            self.assertEqual(shown(word), "", word)
            self.assertEqual(shown(word.upper()), "", word)
            self.assertEqual(shown(word.title()), "", word)
        scripts = {unicodedata.name(c).split()[0] for name in SHOWN_KEPT for c in name if c.isalpha()}
        for script in ("LATIN", "CYRILLIC", "GREEK", "CJK", "HIRAGANA", "KATAKANA", "HANGUL", "ARABIC", "HEBREW", "DEVANAGARI", "THAI"):
            self.assertIn(script, scripts, script)

    def test_what_is_not_text_is_no_name_and_nothing_raises(self):
        for value in NOT_TEXT:
            self.assertEqual(shown(value), "", repr(value))

    def test_the_names_that_may_pass_for_the_hubs_voice_are_the_same_words_for_a_username_and_a_name(self):
        self.assertIs(text.reserved("admin"), True)
        self.assertIs(text.reserved("adm1n"), True)
        self.assertIs(text.reserved("a.d.m.i.n"), True)
        self.assertIs(text.reserved("system32"), False)
        self.assertEqual(text.squash("a.d_m i-n'"), "admin")
        self.assertEqual(text.squash("\u0410dm\u00edn"), "admin")
        self.assertEqual(text.squash("maple.leaf"), "mapleleaf")
        self.assertEqual(text.squash("syst3m"), "system")
        self.assertEqual(text.squash("system32"), "systeme2")                      # (a digit that is not read as a letter stays)

    def test_the_lookalike_table_maps_each_letter_to_one_latin_letter_and_no_latin_letter_is_in_it(self):
        seen = {}
        for latin, others in text.LOOKALIKE_LETTERS:
            self.assertEqual(len(latin), 1)
            self.assertTrue("a" <= latin <= "z", latin)
            for other in others:
                self.assertGreater(ord(other), 127, hex(ord(other)))
                self.assertNotIn(other, seen, "%s is in the table twice" % hex(ord(other)))
                seen[other] = latin
                self.assertEqual(text.SCRIPT_LOOKALIKE[ord(other)], latin)
        self.assertEqual(len(text.SCRIPT_LOOKALIKE), len(seen))
        self.assertGreater(len(seen), 70)
        for letter in "abdefhimnorstuvy":                                      # (every letter that the reserved words are made of)
            self.assertIn(letter, dict(text.LOOKALIKE_LETTERS), letter)

    def test_a_shown_name_is_one_that_the_cleaner_gives_back_as_it_is(self):
        for name in ("Maple", "maple.leaf", "\u0e01\u0e34\u0e49\u0e07", "a1"):
            self.assertTrue(text.valid_shown(name), name)
        for name in ("", "a", " Maple", "Maple ", "Ma  ple", "Ma\u200bple", "Admin", "Maple \U0001f341", "x" * 33, "e\u0301a", None, 5, b"abc", ["Maple"]):
            self.assertFalse(text.valid_shown(name), repr(name))

    def test_the_limits_are_the_documented_ones(self):
        self.assertEqual((text.MIN_SHOWN, text.MAX_SHOWN, text.MAX_SHOWN_RAW, text.MAX_SHOWN_MARKS), (2, 32, 128, 2))
        self.assertEqual(set(text.SHOWN_PUNCTUATION), set(".-_'"))

    def test_a_huge_input_is_not_read_so_it_costs_nothing(self):
        import time
        started = time.monotonic()
        for _ in range(200):
            shown("\ufdfa" * 100000)
            shown("a" * 129)
            self.assertLess(time.monotonic() - started, 1.0)                       # (inside the loop: a cleaner that reads all of it fails at the second call, not after two hundred)
        started = time.monotonic()
        for _ in range(200):
            shown("\ufdfa" * 128)                                                  # (the most that is read, each letter growing to eighteen)
        self.assertLess(time.monotonic() - started, 3.0)


class TheShownNameLaws(unittest.TestCase):
    """A fuzz over random strings: the laws of the result. Reproducible (a fixed seed); ANTS_HUB_FUZZ_SCALE=10 runs ten times more."""

    CASES = 20000 * int(os.environ.get("ANTS_HUB_FUZZ_SCALE", "1"))

    def test_the_laws(self):
        rnd = random.Random(20261108)
        plain, awkward = alphabet()
        words = ["admin", "Mod", "system", "staff", "ants", "hub", "a.d.m.i.n", "\u0430dmin", "\u0391dmin", "m0d"]
        everything = list(range(0x110000))
        kept = refused = reserved_seen = 0
        for _ in range(self.CASES):
            kind = rnd.random()
            if kind < 0.4:
                raw = "".join(rnd.choice(plain if rnd.random() < 0.6 else awkward) for _ in range(rnd.randint(0, 50)))
            elif kind < 0.7:
                raw = "".join(chr(rnd.choice(everything)) for _ in range(rnd.randint(0, 40)))
            elif kind < 0.85:
                raw = rnd.choice(plain[:20]).join(rnd.choice(words) for _ in range(rnd.randint(1, 2))) + "".join(rnd.choice(awkward) for _ in range(rnd.randint(0, 3)))
            else:
                raw = "".join(rnd.choice(plain) for _ in range(rnd.randint(20, 140)))
            out = shown(raw)
            if not out:
                refused += 1
                continue
            kept += 1
            self.assertEqual(out, unicodedata.normalize("NFC", out), repr(raw))
            self.assertTrue(text.MIN_SHOWN <= len(out) <= text.MAX_SHOWN, repr(raw))
            self.assertEqual(out, out.strip(" "))
            self.assertNotIn("  ", out)
            self.assertFalse(text.reserved(out), "%r is reserved" % out)
            self.assertTrue(any(unicodedata.category(c)[0] == "L" or unicodedata.category(c) == "Nd" for c in out), repr(out))
            run = 0
            previous = " "
            for ch in out:
                category = unicodedata.category(ch)
                self.assertNotIn(category, REMOVED, "%r kept %r" % (raw, ch))
                self.assertNotIn(ch, FILLERS)
                self.assertNotIn(ch, text.INVISIBLE_MARKS)
                if category in ("Mn", "Mc"):
                    run += 1
                    self.assertLessEqual(run, text.MAX_SHOWN_MARKS, repr(out))
                    self.assertTrue(unicodedata.category(previous)[0] == "L" or unicodedata.category(previous) == "Nd" or unicodedata.category(previous) in ("Mn", "Mc"), "a mark after %r in %r" % (previous, out))
                else:
                    run = 0
                    self.assertTrue(category[0] == "L" or category == "Nd" or ch in text.SHOWN_PUNCTUATION or ch == " ", "%r kept %r (%s)" % (raw, ch, category))
                previous = ch
            self.assertEqual(shown(out), out, "not stable: %r" % raw)
            self.assertTrue(text.valid_shown(out))
            self.assertNotIn("<", out)
            self.assertNotIn(">", out)
        self.assertGreater(kept, self.CASES // 5)
        self.assertGreater(refused, 100)


if __name__ == "__main__":
    unittest.main()
