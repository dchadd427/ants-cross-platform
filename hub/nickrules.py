"""The rules of a nickname, a tag and a guest's name (docs/HUB_NICKNAMES.md, "The rules"): pure functions of text, no store and no clock. hub/nicknames.py adds who has what, how often it may change, and the frames.

A nickname is 3 to 16 characters: the letters of ONE alphabet (Latin or Cyrillic, hub/name_letters.py: exactly what the game's font draws), the digits 0 to 9, spaces and `. _ - '`; it starts and ends with a letter or a digit,
has a letter in it and never two signs in a row. A tag is 2 to 5 letters and digits of one alphabet, shown as it was typed ("Ant", "ANT" and "aNt" are three tags, all read as one word by the checks). A guest's name is a nickname that nobody owns. Every refusal is a word (the page owns the sentences):

  empty, short, long   the length (in characters, after the rules below have been applied)
  chars                a character that is no letter of the table, digit, space or `. _ - '`, or a sign where none may be
  script               a letter of another script (Greek, Hebrew, Arabic, CJK ...): the game's font cannot draw it yet
  alphabets            letters of two alphabets in one name (the usual way to write a name that looks like another)
  reserved             a name that could pass for the hub's own voice, a role or a placeholder (text.reserved, `guest 123456`, bot, ...); for a tag also `staff`: a lookalike of a staff badge
  rude                 a word of hub/rude_words.txt, read the way a person reads it
  bot                  (tags) the whole name "[TAG] ..." would read as a computer player's label (the room refuses such a name)

The text first: NFC, the curly apostrophes and the dashes of a phone's keyboard are the plain ones, every blank is one space, the ends are trimmed. Nothing else is removed or changed: what is not allowed is refused.
"""
import os
import re
import unicodedata

from . import name_letters, text

NICK_MIN, NICK_MAX = 3, 16
TAG_MIN, TAG_MAX = 2, 5
RAW_MAX = 64                                  # more code points than this are not read at all: 'long'
SIGNS = frozenset(" ._-'")
FOLDED = {0x2018: "'", 0x2019: "'", 0x02BC: "'", 0x2010: "-", 0x2011: "-", 0x2012: "-", 0x2013: "-", 0x2014: "-", 0x2015: "-"}
BLANKS = re.compile(r"[\s\u00a0\u1680\u2000-\u200a\u2028\u2029\u202f\u205f\u3000]+")
GUEST_SUFFIX = " (Guest)"
LOGIN_SUFFIX = " (Discord)"                        # what a login with no nickname is shown with when its name and its username both look like somebody's nickname
GUEST_PLACEHOLDER = re.compile(r"guest[0-9]*\Z")
NICK_RESERVED = ("owner", "developer", "dev", "host", "gm", "guest", "player", "computer", "anonymous", "null", "undefined", "bot", "bots", "everyone", "nobody", "unknown")
TAG_RESERVED = ("owner", "dev", "devs", "team", "gm", "host", "guest", "everyone", "unknown")
NICK_RESERVED_SQUASHED = frozenset(text.squash(w) for w in NICK_RESERVED)
TAG_RESERVED_SQUASHED = frozenset(text.squash(w) for w in text.RESERVED) | frozenset(text.squash(w) for w in TAG_RESERVED)
TAG_RESERVED_EXACT = frozenset(w.lower() for w in text.RESERVED) | frozenset(TAG_RESERVED)


def normalize(raw):
    """NFC, plain apostrophes and hyphens, one space for every run of blanks, the ends trimmed; '' for anything that is not text."""
    if not isinstance(raw, str) or len(raw) > RAW_MAX:
        return None
    out = unicodedata.normalize("NFC", raw).translate(FOLDED)
    return BLANKS.sub(" ", out).strip(" ")


def alphabets_of(name):
    return {name_letters.alphabet(c) for c in name} - {None}


def _characters(name, signs):
    """None when every character may be in a name, else `script` or `chars`."""
    other = False
    for ch in name:
        if name_letters.is_letter(ch) or name_letters.is_digit(ch) or (signs and ch in SIGNS):
            continue
        if unicodedata.category(ch)[0] == "L":
            return "script"
        other = True
    return "chars" if other else None


def check_nickname(raw):
    """(nickname, None) for text that may be a nickname or a guest's name, else (None, why). The refusals that need the store (taken, a lookalike, how often) are the caller's."""
    name = normalize(raw)
    if name is None:
        return None, "long"
    if not name:
        return None, "empty"
    why = _characters(name, True)
    if why:
        return None, why
    if len(name) < NICK_MIN:
        return None, "short"
    if len(name) > NICK_MAX:
        return None, "long"
    if len(alphabets_of(name)) > 1:
        return None, "alphabets"
    if not (name_letters.is_letter(name[0]) or name_letters.is_digit(name[0])) or not (name_letters.is_letter(name[-1]) or name_letters.is_digit(name[-1])):
        return None, "chars"
    if not any(name_letters.is_letter(c) for c in name) or any(a in SIGNS and b in SIGNS for a, b in zip(name, name[1:])):
        return None, "chars"
    if text.reserved(name) or text.squash(name) in NICK_RESERVED_SQUASHED or GUEST_PLACEHOLDER.match(re.sub(r"[ ._'-]", "", name.lower())):
        return None, "reserved"
    if is_rude(name):
        return None, "rude"
    return name, None


def check_tag(raw, nickname=""):
    """(tag, None) for text that may be a tag (kept as it was typed: its capitals are the person's), else (None, why); '' is no tag and always fine. `nickname` is the one that the tag will stand before: the whole name has to be a name the room takes.
    Every check that compares a tag with a word (reserved, staff, rude, a computer player's label) reads it in capitals, so a tag cannot get past them by its case."""
    if raw is None or raw == "":
        return "", None
    tag = normalize(raw)
    if tag is None:
        return None, "long"
    if not tag:
        return "", None
    why = _characters(tag, False)
    if why:
        return None, why
    if len(tag) < TAG_MIN:
        return None, "short"
    if len(tag) > TAG_MAX:
        return None, "long"
    if len(alphabets_of(tag)) > 1:
        return None, "alphabets"
    capitals = tag.upper()
    if capitals.lower() in TAG_RESERVED_EXACT:
        return None, "reserved"
    if text.squash(capitals) in TAG_RESERVED_SQUASHED:
        return None, "staff"
    if is_rude(capitals):
        return None, "rude"
    if reads_as_bot("[%s] %s" % (capitals, nickname or "Name")):
        return None, "bot"
    return tag, None


def shown(nickname, tag=""):
    """The name that the others see: "[TAG] Nickname", or the nickname alone."""
    return "[%s] %s" % (tag, nickname) if tag else nickname


def login_shown(username):
    """The name of a login whose display name and username both look like somebody's nickname, as the chat shows it: "alice (Discord)"."""
    return username + LOGIN_SUFFIX


def guest_shown(name):
    """The name of a guest as every screen shows it, and the game: "Wren (Guest)"."""
    return name + GUEST_SUFFIX


# ---- rude words -----------------------------------------------------------------------------------------------------------------------------------------------------------------------

def _written(name):
    """The name as it is written, without its marks, in lower case, with no blanks or signs between the letters (the Cyrillic words are matched on this form)."""
    plain = "".join(c for c in unicodedata.normalize("NFKD", name) if not unicodedata.category(c).startswith("M"))
    return re.sub(r"[ ._'-]", "", plain.casefold())


def _load_words():
    whole, inside = set(), []
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "rude_words.txt")
    with open(path, encoding="utf-8") as f:
        for line in f:
            word = line.strip()
            if not word or word.startswith("#"):
                continue
            if word.startswith("="):
                whole.add(_written(word[1:]))                 # (a word is read the way a name is: its marks go, so a Cyrillic word with a breve on a letter and the same word without it are one word in both)
            else:
                inside.append(_written(word))
    return frozenset(whole), tuple(inside)


WHOLE_WORDS, INSIDE_WORDS = _load_words()


def is_rude(name):
    """Whether a word of the list is the name or is in it, in the written form or in the form that a lookalike makes of it (text.squash: sh1t, f.u.c.k, a Cyrillic o in a Latin word)."""
    forms = {_written(name), text.squash(name)}
    for form in forms:
        if form in WHOLE_WORDS or any(word in form for word in INSIDE_WORDS):
            return True
    return False


# ---- names that read as a computer player's label (ai::is_reserved_bot_name, web/front/bot_names.js: tests/data/bot_label_cases.tsv is the table that all three answer) -------------------------------

SKIP, SPACE, OPEN, CLOSE, LETTER, SIGN = 0, 1, 2, 3, 4, 5
LOOKALIKES = (            # (first, last, letter) as in bot_names.cpp (kLookalikes)
    (0xB0, 0xB0, "o"), (0xBA, 0xBA, "o"), (0xD2, 0xD6, "o"), (0xD8, 0xD8, "o"), (0xF2, 0xF6, "o"), (0xF8, 0xF8, "o"), (0x14C, 0x151, "o"), (0x1A0, 0x1A1, "o"), (0x1D1, 0x1D2, "o"), (0x1EA, 0x1ED, "o"), (0x1ECC, 0x1EE3, "o"),
    (0x39F, 0x39F, "o"), (0x3BF, 0x3BF, "o"), (0x41E, 0x41E, "o"), (0x43E, 0x43E, "o"), (0x585, 0x585, "o"), (0x1D0F, 0x1D0F, "o"), (0x1D52, 0x1D52, "o"), (0x2092, 0x2092, "o"),
    (0x180, 0x180, "b"), (0x253, 0x253, "b"), (0x299, 0x299, "b"), (0x392, 0x392, "b"), (0x3B2, 0x3B2, "b"), (0x412, 0x412, "b"), (0x432, 0x432, "b"), (0x42C, 0x42C, "b"), (0x44C, 0x44C, "b"), (0x1D2E, 0x1D2E, "b"), (0x1D47, 0x1D47, "b"), (0x1E02, 0x1E07, "b"),
    (0x162, 0x167, "t"), (0x1AB, 0x1AB, "t"), (0x1AD, 0x1AD, "t"), (0x21A, 0x21B, "t"), (0x3A4, 0x3A4, "t"), (0x3C4, 0x3C4, "t"), (0x422, 0x422, "t"), (0x442, 0x442, "t"), (0x1D1B, 0x1D1B, "t"), (0x1D57, 0x1D57, "t"), (0x1E6A, 0x1E6F, "t"), (0x2020, 0x2020, "t"),
)
OPENERS = (0x2329, 0x2768, 0x276A, 0x276C, 0x2770, 0x27E6, 0x27E8, 0x27EA, 0x3008, 0x300A, 0x300C, 0x300E, 0x3010, 0x3014, 0xFE59, 0xFE5B, 0xFE5D)


def _fold(cp):
    """One code point as (kind, letter)."""
    if cp < 0x80:
        c = chr(cp + 32 if 65 <= cp <= 90 else cp)
        if "a" <= c <= "z" or "0" <= c <= "9":
            return LETTER, "o" if c == "0" else c
        if c in "([{<":
            return OPEN, ""
        if c in ")]}>":
            return CLOSE, ""
        return (SPACE if c == " " or 9 <= cp <= 13 else SIGN), ""
    if 0xFF01 <= cp <= 0xFF5E:
        return _fold(cp - 0xFEE0)
    if 0x1D400 <= cp <= 0x1D6A3:
        return LETTER, chr(97 + (cp - 0x1D400) % 52 % 26)
    if 0x1D7CE <= cp <= 0x1D7FF:
        d = chr(48 + (cp - 0x1D7CE) % 10)
        return LETTER, "o" if d == "0" else d
    if 0x24B6 <= cp <= 0x24CF:
        return LETTER, chr(97 + cp - 0x24B6)
    if 0x24D0 <= cp <= 0x24E9:
        return LETTER, chr(97 + cp - 0x24D0)
    if (cp in (0xAD, 0x34F, 0xFEFF) or 0x300 <= cp <= 0x36F or 0x1AB0 <= cp <= 0x1AFF or 0x1DC0 <= cp <= 0x1DFF or 0x200B <= cp <= 0x200F or 0x202A <= cp <= 0x202E or 0x2060 <= cp <= 0x206F
            or 0x20D0 <= cp <= 0x20FF or 0xFE00 <= cp <= 0xFE0F):
        return SKIP, ""
    for opener in OPENERS:
        if cp == opener:
            return OPEN, ""
        if cp == opener + 1:
            return CLOSE, ""
    for first, last, letter in LOOKALIKES:
        if first <= cp <= last:
            return LETTER, letter
    if cp in (0x85, 0xA0, 0x1680, 0x2028, 0x2029, 0x202F, 0x205F, 0x3000) or 0x2000 <= cp <= 0x200A:
        return SPACE, ""
    return SIGN, ""


def _is_break(g):
    return g[0] in (SPACE, SIGN)


def _bot_word_at(f, at):
    """The word bot from `at` (blanks and signs may stand before it) as (end, parted), or None: the three letters one after the other, or each pair parted by blanks or signs, never one pair parted and not the other."""
    i, gap = at, [False, False]
    while i < len(f) and _is_break(f[i]):
        i += 1
    for k, want in enumerate("bot"):
        if i >= len(f) or f[i][0] != LETTER or f[i][1] != want:
            return None
        i += 1
        if k < 2:
            before = i
            while i < len(f) and _is_break(f[i]):
                i += 1
            gap[k] = i > before
    return None if gap[0] != gap[1] else (i, gap[0])


def reads_as_bot(name):
    """Whether a person may not be called `name`: it reads as a computer player's label ("Bot (Hard)", "Pip (bot)", "[BOT] Zoe": docs/BOT_NAMES.md)."""
    f = [g for g in (_fold(ord(c)) for c in (name if isinstance(name, str) else "")) if g[0] != SKIP]
    n = len(f)
    word = _bot_word_at(f, 0)
    if word:
        end = word[0]
        while end < n and _is_break(f[end]):
            end += 1
        if end < n and f[end][0] == OPEN:
            return True
    for i in range(n):
        if f[i][0] != OPEN:
            continue
        word = _bot_word_at(f, i + 1)
        if not word:
            continue
        end = word[0]
        nxt = end
        if word[1]:
            while nxt < n and f[nxt][0] == SPACE:
                nxt += 1
        if end < n and not (nxt < n and f[nxt][0] == LETTER):
            return True
    return False
