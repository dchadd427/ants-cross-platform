"""Everything a visitor types is data (docs/HUB.md, "Text rules"). clean_line() turns what a client sent into the text that may be shown, or says why it may not.

The rules, in order:
  1. Unicode NFC (and again at the end, when removed characters have joined a letter to its mark).
  2. A blank (a space, a tab or line end, the other spaces of category Zs, the line and paragraph separators Zl and Zp) becomes one plain space; runs of blanks become one; the ends are trimmed.
  3. These are removed: the other control characters (Cc), format characters (Cf: the bidirectional controls, zero-width space, joiners, the byte order mark, ...), surrogates (Cs), private use
     (Co), characters no Unicode version of this Python has assigned (Cn), and the letters that draw as blanks (the Hangul fillers, the braille blank, the combining grapheme joiner, the
     Khmer inherent vowels). So nothing is left that reorders the text or hides in it.
  4. More than MAX_MARKS combining marks in a row are cut (a column of them would overlap the lines above and below).
  5. Nothing left, or nothing but combining marks (a variation selector shows nothing by itself): 'empty'. More than 200 code points or 800 bytes: 'long'. A '<' or '>': 'bad' (no markup; the page uses textContent only, shows no link and makes no mention).
The check for '<' and '>' is made on the result, after the hiding places are gone.
"""
import re
import unicodedata

MAX_CODE_POINTS = 200
MAX_BYTES = 800
MAX_MARKS = 4
MAX_RAW = 4096                      # a client frame is 2 KB: anything longer is not looked at
BLANK_CATEGORIES = frozenset(("Zs", "Zl", "Zp"))
BLANK_CONTROLS = frozenset("\t\n\r\x0b\x0c\x85")
REMOVED_CATEGORIES = frozenset(("Cc", "Cf", "Cs", "Co", "Cn"))
FILLERS = frozenset("͏ᅟᅠ឴឵⠀ㅤﾠ")
NAME = re.compile(r"[a-z0-9_.]{2,32}\Z")


def clean_line(raw):
    """(text, None) for a line that may be shown, else (None, 'empty' | 'long' | 'bad')."""
    if not isinstance(raw, str):
        return None, "bad"
    if len(raw) > MAX_RAW:
        return None, "long"
    out = []
    marks = 0
    for ch in unicodedata.normalize("NFC", raw):
        category = unicodedata.category(ch)
        if category in BLANK_CATEGORIES or ch in BLANK_CONTROLS:
            if out and out[-1] != " ":
                out.append(" ")
            marks = 0
        elif category in REMOVED_CATEGORIES or ch in FILLERS:
            continue
        elif category[0] == "M":
            marks += 1
            if marks <= MAX_MARKS:
                out.append(ch)
        else:
            marks = 0
            out.append(ch)
    text = unicodedata.normalize("NFC", "".join(out)).strip(" ")        # (again: a mark that the removed characters kept from its letter composes with it now)
    if not any(c != " " and not unicodedata.category(c).startswith("M") for c in text):       # (marks alone, as the variation selectors are, show nothing)
        return None, "empty"
    if len(text) > MAX_CODE_POINTS or len(text.encode("utf-8")) > MAX_BYTES:
        return None, "long"
    if "<" in text or ">" in text:
        return None, "bad"
    return text, None


def valid_name(name):
    """A display name: what a Discord username is (lower-case letters, digits, '_' and '.', 2 to 32 characters)."""
    return isinstance(name, str) and NAME.match(name) is not None
