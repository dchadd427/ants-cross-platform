"""The letters that a person's name may hold: the one table (docs/HUB_NICKNAMES.md, "The letters").

A nickname, a tag and a guest's name are made of the letters of ONE alphabet, Latin or Cyrillic, ASCII digits and a few signs. The letters are exactly those that the game's font file
(Original-Ants/LibreFranklin-Medium.ttf, the file that the web game packs too) draws, so that nothing in a results screen or a countdown card is a box: all of Basic Latin, the letters of
Latin-1 and Latin Extended-A (German, Nordic, French, Spanish, Polish, Czech, Hungarian, Turkish, Baltic, Welsh, Maltese, Croatian), the comma-below letters of Romanian, the letters of Vietnamese,
and the modern letters of Russian, Ukrainian, Belarusian, Serbian, Macedonian and Bulgarian. Greek, the other scripts, combining marks, emoji and signs are out (the face has no Greek, 4 letters only).
The font is checked by tests/scripts/test_name_letters.py, which reads the file's own character table: a letter of this table that the file lacks fails it.

This file is the home of the table. tools/gen_name_letters.py writes it into the game's table (src/ants_net/name_letters_data.inc) and into the page's script (the generated block of
web/front/name_letters.js); a test fails when either differs. Names are in NFC (Vietnamese letters are the single code points of U+1EA0 - U+1EF9).
"""

# (first, last) of each range, both included
LATIN = (
    (0x41, 0x5A), (0x61, 0x7A),                  # A - Z, a - z
    (0xC0, 0xD6), (0xD8, 0xF6), (0xF8, 0xFF),     # Latin-1 letters (not the signs U+00D7 and U+00F7)
    (0x100, 0x17E),                              # Latin Extended-A (not the long s, U+017F, which the face lacks and which reads as an f)
    (0x1A0, 0x1A1), (0x1AF, 0x1B0),              # O and U with a horn (Vietnamese)
    (0x218, 0x21B),                              # S and T with a comma below (Romanian)
    (0x1EA0, 0x1EF9),                            # the Vietnamese letters with a tone mark
)
CYRILLIC = (
    (0x400, 0x45F),                              # the letters of Russian, Ukrainian, Belarusian, Serbian, Macedonian and Bulgarian (all that the face draws of U+0400 - U+045F)
    (0x490, 0x491),                              # Ukrainian Ghe with an upturn
)
ALPHABETS = {"latin": LATIN, "cyrillic": CYRILLIC}
DIGITS = "0123456789"
# printable ASCII (0x20 - 0x7E) is what the wire always allowed; the letters above come on top of it (src/ants_net/name_rules.hpp)
PRINTABLE_ASCII = (0x20, 0x7E)


def alphabet(ch):
    """'latin', 'cyrillic' or None for a single character: the alphabet that the letter belongs to (the digits and signs belong to none)."""
    cp = ord(ch)
    for name, ranges in ALPHABETS.items():
        for first, last in ranges:
            if first <= cp <= last:
                return name
    return None


def is_letter(ch):
    return alphabet(ch) is not None


def is_digit(ch):
    return ch in DIGITS and len(ch) == 1
