"""Everything a visitor types is data (docs/HUB.md, "Text rules"). clean_line() turns what a client sent into the text that may be shown, or says why it may not.

The rules, in order:
  1. Unicode NFC (and again at the end, when removed characters have joined a letter to its mark).
  2. A blank (a space, a tab or line end, the other spaces of category Zs, the line and paragraph separators Zl and Zp) becomes one plain space; runs of blanks become one; the ends are trimmed.
  3. These are removed: the other control characters (Cc), format characters (Cf: the bidirectional controls, zero-width space, joiners, the byte order mark, ...), surrogates (Cs), private use
     (Co), characters no Unicode version of this Python has assigned (Cn), and the letters that draw as blanks (the Hangul fillers, the braille blank, the combining grapheme joiner, the
     Khmer inherent vowels, the musical null notehead). So nothing is left that reorders the text or hides in it.
  4. More than MAX_MARKS combining marks in a row are cut (a column of them would overlap the lines above and below).
  5. Nothing left, or nothing but combining marks (a variation selector shows nothing by itself): 'empty'. More than 200 code points or 800 bytes: 'long'. A '<' or '>': 'bad' (no markup; the page uses textContent only, shows no link and makes no mention).
The check for '<' and '>' is made on the result, after the hiding places are gone.

The names of people (docs/HUB.md, "Names"): a username is what Discord gives out (valid_name); the name that the chat shows may be the person's Discord display name, which is free text from
Discord and is cleaned by clean_shown() on its own, stricter rules (below), and is refused when it could pass for the hub's own voice (reserved()). A display name that does not pass is no name:
the chat shows the username, and nothing else happens.
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
FILLERS = frozenset("\u034f\u115f\u1160\u17b4\u17b5\u2800\u3164\uffa0\U0001d159")        # (written as escapes: they draw as nothing)
NAME = re.compile(r"[a-z0-9_.]{2,32}\Z")

MIN_SHOWN = 2                       # code points of a shown name ...
MAX_SHOWN = 32                      # ... to this many (what Discord gives out; a longer one is cut)
MAX_SHOWN_RAW = 128                 # what is looked at of a display name: one of more code points than this is not Discord's, and is not read at all
MAX_SHOWN_MARKS = 2                 # combining marks in a row that a shown name keeps
SHOWN_PUNCTUATION = frozenset(".-_'")
FOLDED = str.maketrans({"\u2018": "'", "\u2019": "'", "\u2010": "-", "\u2012": "-", "\u2013": "-", "\u2014": "-", "\u2015": "-"})       # (the curly apostrophes and the dashes that a phone's keyboard types: the plain ones)
INVISIBLE_MARKS = frozenset([chr(c) for c in (*range(0x180b, 0x180e), 0x180f, *range(0xfe00, 0xfe10), *range(0xe0100, 0xe01f0), 0x16fe4)])      # (the variation selectors and the Khitan filler: marks that show nothing)

RESERVED = frozenset(("mod", "admin", "moderator", "administrator", "system", "server", "staff", "ants", "ants.bot", "hub", "everyone", "here"))      # (names that could pass for the hub's own voice)
LOOKALIKE = str.maketrans("013457", "oieast")           # (0 o, 1 i, 3 e, 4 a, 5 s, 7 t: a digit for the letter that it looks like)
LOOKALIKE_LETTERS = (                                   # the Latin letter, and the letters of other scripts (Cyrillic, Greek, the dotless i) that are drawn like it, in both cases
    ("a", "\u0430\u0410\u03b1\u0391"), ("b", "\u0412\u0392"), ("c", "\u0441\u0421\u03f2\u03f9"), ("d", "\u0501\u0500"), ("e", "\u0435\u0415\u03b5\u0395"), ("f", "\u03dc\u03dd"), ("h", "\u043d\u041d\u04bb\u04ba\u0397"),
    ("i", "\u0456\u0406\u04cf\u04c0\u03b9\u0399\u0131"), ("j", "\u0458\u0408"), ("k", "\u043a\u041a\u03ba\u039a"), ("m", "\u043c\u041c\u039c"), ("n", "\u03b7\u039d"), ("o", "\u043e\u041e\u03bf\u039f"),
    ("p", "\u0440\u0420\u03c1\u03a1"), ("q", "\u051b"), ("r", "\u0433\u0413"), ("s", "\u0455\u0405"), ("t", "\u0442\u0422\u03c4\u03a4"), ("u", "\u03bc\u03c5"), ("v", "\u0475\u0474\u03bd"),
    ("w", "\u051d\u03c9"), ("x", "\u0445\u0425\u03c7\u03a7"), ("y", "\u0443\u0423\u03a5\u03b3"), ("z", "\u0396"))
SCRIPT_LOOKALIKE = {ord(other): latin for latin, others in LOOKALIKE_LETTERS for other in others}


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
    """A username: what a Discord username is (lower-case letters, digits, '_' and '.', 2 to 32 characters). The key of an account for a moderator; never the name that is shown."""
    return isinstance(name, str) and NAME.match(name) is not None


def squash(name):
    """What a name looks like at a glance: no dots, underscores, hyphens, apostrophes (the letters that are drawn as one too) or spaces, no accents, a letter of another script for the Latin letter that it is drawn like (see
    LOOKALIKE_LETTERS), a digit for the letter that it looks like read as that letter ("adm1n", "admin.", "a.d.m.i.n", "system_", "\u0410dmin" with a Cyrillic A are all "admin" and "system" to a
    reader). Digits at the end are not dropped: "system32", "hub99" and "ants1998" are names of real people, and a lookalike gains nothing (a moderator is the hub's `mod` flag, by Discord id,
    never a name)."""
    plain = "".join(c for c in unicodedata.normalize("NFKD", name) if not unicodedata.category(c).startswith("M"))
    return re.sub("[ ._'\u02bb\u02bc-]", "", plain.translate(SCRIPT_LOOKALIKE).lower()).translate(LOOKALIKE)


RESERVED_SQUASHED = frozenset(squash(n) for n in RESERVED)


def reserved(name):
    """Whether a name could pass for the hub's own voice: a word of RESERVED, or what looks like one at a glance (squash)."""
    return name in RESERVED or squash(name) in RESERVED_SQUASHED


def clean_shown(raw):
    """The name to show for what Discord says the person's display name is (`global_name`), or '' when there is none that may be shown: then the chat shows the username, and nothing else
    happens (a login is never refused for it). `raw` is text from a stranger of any kind and size: anything that is not text, or has more than MAX_SHOWN_RAW code points (more than twice what
    Discord gives out), is not read at all. In order: NFKC (the letter forms of fancy fonts, full-width letters and the like become the plain letters); a blank (any space, a tab or line end) is one
    space; control, format (the bidirectional controls and the zero-width characters among them), private use, unassigned and surrogate code points and the letters that draw as blanks are removed;
    letters and decimal digits stay; so do `.`, `_`, `-` and `'` (the curly apostrophes and the dashes of a phone's keyboard are made plain ones); a combining mark stays only after a letter or
    a digit, at most MAX_SHOWN_MARKS in a row, and not a variation selector or a mark that encloses; everything else goes (emoji and symbols, the rest of the punctuation, the other numbers). Then NFC,
    the ends are trimmed, the name is cut at MAX_SHOWN code points (and trimmed again); one of fewer than MIN_SHOWN, with no letter or digit, or that squash() reads as a reserved name, is ''."""
    if not isinstance(raw, str) or len(raw) > MAX_SHOWN_RAW:
        return ""
    out = []
    marks = 0
    after = False                                    # (the last character that stays is a letter or a digit, or a mark that stays after one: a mark may follow)
    for ch in unicodedata.normalize("NFKC", raw).translate(FOLDED):
        category = unicodedata.category(ch)
        if category in BLANK_CATEGORIES or ch in BLANK_CONTROLS:
            if out and out[-1] != " ":
                out.append(" ")
            marks, after = 0, False
        elif ch in FILLERS:
            continue                                 # (what no branch keeps is dropped, whatever it is: controls, format characters, private use, unassigned and surrogates, symbols, the rest)
        elif category[0] == "L" or category == "Nd":
            out.append(ch)
            marks, after = 0, True
        elif ch in SHOWN_PUNCTUATION:
            out.append(ch)
            marks, after = 0, False
        elif category in ("Mn", "Mc") and after and marks < MAX_SHOWN_MARKS and ch not in INVISIBLE_MARKS:
            out.append(ch)
            marks += 1
    name = unicodedata.normalize("NFC", "".join(out)).strip(" ")
    if len(name) > MAX_SHOWN:
        name = name[:MAX_SHOWN].rstrip(" ")
    if len(name) < MIN_SHOWN or not any(unicodedata.category(c)[0] == "L" or unicodedata.category(c) == "Nd" for c in name) or reserved(name):
        return ""
    return name


def valid_shown(name):
    """A name that clean_shown() gives back as it is: what the hub takes as the shown name of an identity."""
    return isinstance(name, str) and name != "" and clean_shown(name) == name
