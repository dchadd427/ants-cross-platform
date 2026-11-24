#!/usr/bin/env python3
"""The public pages of the map library: one page for each map at /map/NAME and the index at /map-library (docs/MAP_PAGES.md).

usage: map_pages.py ORIGIN OUT_DIR [--library DIR] [--originals DIR] [--template FILE] [--ledger FILE] [--version V] [--build-id ID] [--build-date TEXT]
       map_pages.py --slugs [--library DIR] [--originals DIR] [--ledger FILE]      the addresses of the library as JSON, {"slug": "file name"}: what tools/map_page_slugs.json holds

ORIGIN is the address that the pages name themselves by (the one line of web/SITE_ORIGIN; the canonical links, the share cards and the structured data carry it). The library is read, never written
(tools/library_guard.py): the six maps of Original-Ants/Maps and the players' maps of Community-Maps (maps.json and the files). OUT_DIR gets
  html/map_pages/map/SLUG.html        one page for each map (the nginx of the image serves it at /map/SLUG: docker/nginx.conf)
  html/map_pages/library/N.html       the index, 48 maps to a page (/map-library and /map-library?page=N)
  html/map_library_data.js            the list that the index page's search and filters read (web/map_library.js)
  nginx/moved.map                     the other names of a map that was shared under several: `/map/OTHER /map/SLUG;` for a `map` block of nginx (a 301)
  addresses.txt                       the addresses of the pages that a search engine should list, one a line (tools/seo_files.py puts them into sitemap.xml)

The ledger (tools/map_page_slugs.json, {"slug": "file name"}) is what keeps an address from ever moving: a map in it has its address, whatever has been added to the library since, an address of a map that is gone stays taken, and a new map gets
an address that is free; `--slugs` prints the ledger with the new maps in it, to be saved. Everything that a player wrote (a file name, a description) is data: it is escaped for the place it goes into (html.escape, json with the characters of markup written as escapes) and never read as
markup or as an instruction. The pages are filled in one pass, so that a text that looks like a slot of the template is not filled in.
"""
import argparse
import hashlib
import html
import json
import os
import re
import struct
import sys
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import library_guard  # noqa: E402  (the owner's rule: nothing is written into the map library)
import seo_files  # noqa: E402  (the one check of the site's address)

REPO = os.path.abspath(os.path.join(HERE, os.pardir))
ORIGINALS_DIR = os.path.join(REPO, "Original-Ants", "Maps")
LIBRARY_DIR = os.path.join(REPO, "Community-Maps")
TEMPLATE = os.path.join(REPO, "web", "map_pages", "page.html")
LEDGER = os.path.join(HERE, "map_page_slugs.json")
ORIGINAL_KEYS = ("tiny", "small", "medium", "gauntlet", "treasure", "islands")      # the order of the six on the page and in GET /maps (docs/SERVER.md "The list of maps")
PAGE_SIZE = 48
SLUG_MAX = 60
TITLE_MAX = 70
DESCRIPTION_MAX = 175
QUERY_MAX = 40                                                                         # the match list's search box takes 40 characters
STAMPS = ("@@GAME_VERSION@@", "@@BUILD_ID@@", "@@BUILD_DATE@@")                        # (a page that nobody stamped: the Menu shows no site line)
START_MARKERS = (152, 153, 154, 155)                                                   # black, blue, green, red (docs/MAP_FORMAT.md)
POWER_UP_TILES = range(62, 67)
TEAMS = "green, red, blue and black"
SIZE_CLASSES = (("small", "Small (up to 31 x 31)", 31), ("medium", "Medium (up to 60 x 60)", 60), ("large", "Large (over 60 x 60)", None))
LENGTH_CLASSES = (("short", "Up to 10 minutes", 10), ("middle", "11 to 15 minutes", 15), ("long", "Over 15 minutes", None))
STOP_WORDS = frozenset("the and map maps ant ants for new old of a an in on to by".split())
SLOT = re.compile(r"@@([A-Z_]+)@@")
KIND_NOTE = "The map\u2019s own line, as the game\u2019s setup screen shows it"
PLAY_NOTE = "Play it opens a room on this map; share its link or add bots."


def esc(text):
    return html.escape(text, quote=True)


def stem_of(file):
    return file[:-4] if file.lower().endswith(".lvl") else file


def valid_map_file(name):
    """The rule of the map maker and the game page for a map file that they fetch from the site (web/front/mapmaker_store.js validMapFile, web/map_files.js validName)."""
    return 5 <= len(name) <= 64 and re.fullmatch(r"[\x20-\x7e]+", name) is not None and not re.search(r'[/\\:*?"<>|]', name) and name[0] != "." and name.lower().endswith(".lvl")


def percent(text):
    return urllib.parse.quote(text, safe="")


# ---- reading the library ----

def parse_lvl(data):
    """What the pages count from a version 8 map file (docs/MAP_FORMAT.md, the layout; the counts are those of src/ants_server/online_limits.cpp): the grid, the length, the description of the
    header, the food piles (the records of block 2), the power-ups (cells of layer 2 with a tile of 62 to 66) and the start markers of the four teams. None for a file that is not that."""
    try:
        version, _mode, minutes = struct.unpack_from("<IIH", data, 0)
        if version != 8:
            return None
        description = data[10:40].split(b"\0")[0].decode("latin-1")
        off = 42 + 11 * (struct.unpack_from("<H", data, 40)[0] + 1)
        rows, columns = (v & 0xFFFF for v in struct.unpack_from("<II", data, off))
        off += 8
        cells = rows * columns
        if not cells or off + 12 * cells > len(data):
            return None
        layer2 = struct.unpack_from("<%dH" % (3 * cells), data, off + 6 * cells)[0::3]
        off += 12 * cells
        records = struct.unpack_from("<H", data, off)[0]
        tiles = struct.unpack_from("<" + "HHH" * records, data, off + 2)[0::3]
        food = struct.unpack_from("<H", data, off + 2 + 6 * records)[0]
    except (struct.error, UnicodeDecodeError):
        return None
    return {"width": columns, "height": rows, "minutes": minutes, "description": description, "food": food, "powerups": sum(1 for t in layer2 if t in POWER_UP_TILES),
            "ants": [sum(1 for t in tiles if t == marker) for marker in START_MARKERS]}


class LibMap:
    """One map of the library (a page). `file` is the file name, `name` what the pages call it (the file name without .lvl)."""

    def __init__(self, file, data, original_key="", entry=None):
        facts = parse_lvl(data) or {}
        entry = entry or {}
        self.file, self.name, self.original_key = file, stem_of(file), original_key
        self.original = bool(original_key)
        self.width, self.height = entry.get("width", facts.get("width", 0)), entry.get("height", facts.get("height", 0))
        self.minutes = entry.get("minutes", facts.get("minutes", 0))
        self.players = entry.get("players", 4)
        self.raw_description = entry.get("description", facts.get("description", ""))
        self.size = len(data)
        self.digest = hashlib.sha256(data).hexdigest()
        self.food, self.powerups, self.ants = facts.get("food"), facts.get("powerups"), facts.get("ants")
        self.repaired = entry.get("repaired", "")
        self.slug, self.aliases, self.related = "", [], []
        self.text, self.cut = describe(self.raw_description)
        self.maker = "" if self.original else maker_of(self.text, self.cut)


def read_library(originals_dir=ORIGINALS_DIR, library_dir=LIBRARY_DIR):
    """The maps of the library as LibMap objects: the six originals first (in the order of the page), then the players' maps by file name, one object for every file (the copies of one file under
    several names are folded together by `plan`)."""
    found = {}
    for name in os.listdir(originals_dir):
        if name.lower().endswith(".lvl") and stem_of(name).lower() in ORIGINAL_KEYS:
            found[stem_of(name).lower()] = name
    maps = []
    for key in ORIGINAL_KEYS:
        if key in found:
            with open(os.path.join(originals_dir, found[key]), "rb") as f:
                maps.append(LibMap(found[key], f.read(), original_key=key))
            maps[-1].name = key.capitalize()
    with open(os.path.join(library_dir, "maps.json"), encoding="utf-8") as f:
        entries = json.load(f)["maps"]
    for entry in sorted(entries, key=lambda e: e["file"]):
        if not isinstance(entry.get("file"), str) or not valid_map_file(entry["file"]):
            raise ValueError("maps.json lists a name that is no map file: %r" % (entry.get("file"),))             # (the name is joined to the folder below: a path in it would read another file)
        path = os.path.join(library_dir, entry["file"])
        if not os.path.isfile(path):
            raise ValueError("maps.json lists %s, which is not in the folder" % entry["file"])
        with open(path, "rb") as f:
            maps.append(LibMap(entry["file"], f.read(), entry=entry))
    return maps


# ---- what a player wrote ----

def describe(raw):
    """(the description as a person reads it, whether it may have been cut). The header holds 27 characters at most: a longer text is cut at 24 and ends in "..." with no space before them, a shorter one is
    padded with spaces and then "...". So a text that ends in "..." right after a letter or a sign may be one that was cut (or one that its player ended so), and a name at its end may be cut: the maker rule takes
    nothing from it. The sign that the old editors put first (! * ^) and the padding are not part of the words."""
    cut = re.search(r"\S\.\.\.$", raw) is not None
    text = re.sub(r"\s+\.\.\.$", "", raw)
    text = re.sub(r"^[!*^\s]+", "", text).strip()
    return text, cut


NAME = r"[A-Za-z0-9_][A-Za-z0-9_./\-]*"
BY = r"(?P<name>%s(?:\s*(?:,|&|\band\b)\s*%s){0,2})\s*\**$" % (NAME, NAME)
NOT_NAMES = ("me", "us", "you", "hand", "mistake", "accident", "myself", "nobody", "noone", "scratch")   # (a word after "by" that is no person)
MAKER_FORMS = [re.compile(form, re.I) for form in (
    r"^(?:(?:map\s+)?made\s+|created\s+|designed\s+|hqs\s+)?by\b\s*:?\s*" + BY,           # "by Name", "Made by: Name", "HQS By Name"
    r"\b(?:made|created|designed)\s+by\b\s*:?\s*" + BY,                                   # "... made by Name"
    r"\bby\s*:\s*" + BY,                                                                   # "xpert-by:Name"
    r"[!?.]\s*by\s+" + BY)]                                                                # "Mini Murky Map!!!!!!By Name"


def maker_of(text, cut):
    """The maker that a description clearly names, else "". Clear is: the text ends in the name (one word, or up to three joined by "and", "&" or a comma) after "by", "made by", "created by",
    "designed by", "HQS by" at its start, "made by" anywhere, "by:" or a "by" that follows an exclamation mark. A text that was cut ends where a name may have been cut, so it names nobody; "improved
    by", "surrounded by" and every other "by" name nobody. The name is the player's: it is shown as text and is at most 24 characters with a letter in it."""
    if cut:
        return ""
    for form in MAKER_FORMS:
        found = form.search(text)
        if found:
            name = re.sub(r"\.+$", "", found.group("name")).strip()
            return name if 1 <= len(name) <= 24 and re.search(r"[A-Za-z]", name) and name.lower() not in NOT_NAMES else ""
    return ""


# ---- the addresses ----

def base_slug(name):
    """The address word of a name: lower case, every run of other characters one hyphen. Empty or long ones get a short hash of the name (stable: it never depends on another map)."""
    slug = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
    tag = hashlib.sha1(name.encode("latin-1", "replace")).hexdigest()
    if not slug:
        return "map-" + tag[:6]
    return slug if len(slug) <= SLUG_MAX else slug[:SLUG_MAX].rstrip("-") + "-" + tag[:4]


def sort_key(name):
    """What the lists sort a name by: lower case, and without the signs that stand in front of it ("!!!aLkAtRaZ" is among the A's); the whole name decides what that leaves tied."""
    low = name.lower()
    return (re.sub(r"^[^a-z0-9]+", "", low), low, name)


def read_ledger(path=LEDGER):
    """{address: file name} of the ledger; none when the file is not there (a tree that has never fixed an address)."""
    try:
        with open(path, encoding="utf-8") as f:
            ledger = json.load(f)
    except FileNotFoundError:
        return {}
    if not isinstance(ledger, dict) or not all(isinstance(k, str) and isinstance(v, str) and re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", k) for k, v in ledger.items()):
        raise ValueError("%s is not a ledger of addresses ({\"slug\": \"file name\"})" % path)
    return ledger


def plan(maps, ledger=None):
    """Folds the copies of one file together and gives every map its address. A file that is the same bytes under several names is one map (the page of an original, else of the name that the ledger has, else of the name
    that most of the names spell alike when only letters and digits are looked at, then one that starts with a letter or a digit, then the name that sorts first); the other names are `aliases` and redirect to it. A map that the
    `ledger` ({address: file name}) has keeps the address it has there, an address of the ledger that no map has any more stays taken, and a new map gets the word of its name when that is free, else the word and a hash of
    its file name after it (which never depends on the other maps). Returns the maps that have a page (the primaries) and {other name's slug: slug}."""
    ledger = ledger or {}
    pinned = {file: slug for slug, file in ledger.items()}
    groups = {}
    for m in maps:
        groups.setdefault(m.digest, []).append(m)
    primaries = []
    for group in groups.values():
        spellings = [re.sub(r"[^a-z0-9]", "", m.name.lower()) for m in group]
        group.sort(key=lambda m: (not m.original, m.file not in pinned, -spellings.count(re.sub(r"[^a-z0-9]", "", m.name.lower())), not m.name[:1].isalnum(), m.name.lower(), m.name))
        first = group[0]
        first.aliases = [m for m in group[1:] if m.name.lower() != first.name.lower()]
        primaries.append(first)
    primaries.sort(key=lambda m: (not m.original, ORIGINAL_KEYS.index(m.original_key) if m.original else 0, sort_key(m.name)))
    taken = {slug: None for slug in ledger}
    for m in primaries:
        if m.file in pinned:
            m.slug = pinned[m.file]
            taken[m.slug] = m
    for m in primaries:
        if m.file in pinned:
            continue
        slug = base_slug(m.name)
        if slug in taken:
            tag = hashlib.sha1(m.file.encode("latin-1", "replace")).hexdigest()
            base, length = slug[:SLUG_MAX].rstrip("-"), 4
            while "%s-%s" % (base, tag[:length]) in taken:
                length += 2
            slug = "%s-%s" % (base, tag[:length])
        taken[slug] = m
        m.slug = slug
    moved = {}
    for m in primaries:
        for other in m.aliases:
            slug = base_slug(other.name)
            if slug not in taken and slug not in moved:
                moved[slug] = m.slug
    for m in primaries:
        m.related = related_of(m, primaries)
    return primaries, moved


def words_of(name):
    return {w for w in re.findall(r"[a-z]{3,}", name.lower()) if w not in STOP_WORDS}


def spread(a, b):
    """A fixed number for a pair of maps, so that the pages that share a size do not all name the same four maps (the same every build)."""
    return int(hashlib.sha1((a.slug + "|" + b.slug).encode("ascii")).hexdigest()[:8], 16)


def related_of(m, maps, count=4):
    """The maps like this one: those that share a word of the name (most words first), then those of the same size and the same kind, never the map itself. Same every build."""
    mine = words_of(m.name)
    others = [o for o in maps if o is not m]
    shared = sorted((o for o in others if mine & words_of(o.name)), key=lambda o: (-len(mine & words_of(o.name)), (o.width, o.height) != (m.width, m.height), spread(m, o)))
    same = sorted((o for o in others if (o.width, o.height, o.original) == (m.width, m.height, m.original) and o not in shared), key=lambda o: spread(m, o))
    return (shared + same)[:count]


# ---- the words of a page ----

def kind_words(m):
    return "original map" if m.original else ("map by " + m.maker) if m.maker else "player map"


def cut_to(text, limit):
    if len(text) <= limit:
        return text
    return text[:limit - 3].rsplit(" ", 1)[0].rstrip(",;:- ") + "..." if " " in text[:limit - 3] else text[:limit - 3] + "..."


def title_of(m):
    suffix = " (%s, %d x %d) \u2014 Ants (1998)" % (kind_words(m), m.width, m.height)
    room = TITLE_MAX - len(suffix)
    name = m.name if len(m.name) <= room else m.name[:max(room - 1, 1)].rstrip() + "\u2026"
    return name + suffix


def description_of(m):
    """The meta description: the facts first, the map's own line, then the invitation, each only as far as 175 characters take it."""
    who = "one of the six original maps of the 1998 game Ants" if m.original else "a map for Ants (1998)" + (" by " + m.maker if m.maker else "")
    text = "%s, %s: %d x %d, four anthills, %d minutes." % (m.name, who, m.width, m.height, m.minutes)
    for part in ((" \u201c%s\u201d" % m.text) if m.text else "", " See it, play it free in your browser or open it in the Map maker."):
        if len(text + part) <= DESCRIPTION_MAX:
            text += part
    return cut_to(text, DESCRIPTION_MAX)


def size_text(m):
    return "%d x %d" % (m.width, m.height)


def file_size_text(m):
    return "%d KB" % max(1, round(m.size / 1024)) if m.size < 1 << 20 else "%.1f MB" % (m.size / (1 << 20))


def food_phrase(m):
    if m.food is None:
        return ""
    if m.food and m.powerups:
        return "It has food to carry home and power-ups to pick up, so it suits the original game, where the highest score wins, and it can be played as 187, a fight with no food."
    if m.food:
        return "It has food to carry home and no power-ups, so it suits the original game, where the highest score wins, and it can be played as 187, a fight with no food."
    return "It has no food, so it is a map for a fight: play it as 187, the game where the most kills win."


def paragraphs_of(m):
    """The About text of a page, as HTML: written from the facts of the file and the library, which is the text that a search engine reads (escaped: the name, the maker, the repair note and the other names are players')."""
    name = esc(m.name)
    if m.original:
        first = "%s is one of the six maps that came with Ants in 1998. " % name
    else:
        first = "%s is one of the maps that players of the 1998 game made and shared. " % name + ("It was made by %s. " % esc(m.maker) if m.maker else "")
    first += "It is a map of %d by %d squares with four anthills, one for each of the green, red, blue and black teams." % (m.width, m.height)
    second = "The map sets a game of %d minutes. " % m.minutes if m.minutes else ""
    second += food_phrase(m) + (" " if food_phrase(m) else "") + "Up to four people or bots can play it."
    found = "/matches?q=" + percent(m.name[:QUERY_MAX])
    third = "Find the matches that were played on it in the <a href=\"%s\">match history</a>." % esc(found)
    paragraphs = ["<p>%s</p>" % first, "<p>%s</p>" % second.strip(), '<p id="plays-line" data-file="%s" data-matches="%s">%s</p>' % (esc(m.file), esc(found), third)]
    if not m.text:
        paragraphs[0] = paragraphs[0][:-4] + " Its file has no description.</p>"
    if m.repaired:
        paragraphs.append("<p>Its file was damaged on its way from player to player and was repaired for this library: %s</p>" % esc(m.repaired))
    if m.aliases:
        paragraphs.append("<p>The same map was also shared as %s.</p>" % ", ".join(esc(o.name) for o in m.aliases))
    return paragraphs


def facts_of(m):
    """The rows of the Facts grid, (label, text): only what is known (no row for a count that the file did not give, none for a maker that no description names)."""
    rows = [("Kind", "Original (1998)" if m.original else "Player map"), ("Size", size_text(m) + " squares"), ("Players", "%d anthills" % m.players), ("Game length", "%d minutes" % m.minutes)]
    if m.food is not None:
        rows += [("Food", "none" if not m.food else "%d piles" % m.food), ("Power-ups", "none" if not m.powerups else str(m.powerups))]
        total = sum(m.ants)
        rows.append(("Starting ants", "%d each" % m.ants[0] if len(set(m.ants)) == 1 else "%d in all" % total))
    if m.original:
        rows.append(("Made by", "The makers of Ants"))
    elif m.maker:
        rows.append(("Made by", m.maker))
    rows.append(("File", "%s, %s" % (m.file, file_size_text(m))))
    if m.aliases:
        rows.append(("Also shared as", ", ".join(o.name for o in m.aliases)))
    return rows


def game_types_of(m):
    """The game types that a map suits (web/front/game_modes.js): 187 any map can be played as; the original game's highest score needs food."""
    return (["Highest score"] if m.food is None or m.food else []) + ["187"]


# ---- the pages ----

def picture_of(m):
    return "/front/preview_%s.png" % m.original_key if m.original else "/maps/previews/%s.webp" % percent(m.name)


def card(m, plays=None):
    tag = '<span class="mtag o">Original</span>' if m.original else ""
    extra = "" if plays is None else " \u00b7 %d plays" % plays
    say = "<p class=\"say\">%s</p>" % esc(m.text) if m.text else ""
    return ('<li class="m"><figure class="tv"><img src="%s" alt="" width="96" height="96" loading="lazy"></figure><div class="info"><h3><a href="/map/%s">%s</a>%s</h3>%s'
            '<span class="when">%s \u00b7 %d players \u00b7 %d min%s</span></div></li>') % (esc(picture_of(m)), m.slug, esc(m.name), tag, say, size_text(m), m.players, m.minutes, extra)


def play_address(m):
    """The front page's address for a room on this map (docs/HOME_SCREEN.md): the page makes the room of its own and plans the map."""
    return "/?hostmap=" + percent(m.file)


def maker_address(m):
    return "/mapmaker?mapfile=" + percent(m.file)


def map_body(m):
    meta = "One of the six maps of the 1998 game" if m.original else ("Made by " + m.maker) if m.maker else "A player\u2019s map of the 1998 game"
    meta = "%s \u00b7 %s \u00b7 %d players" % (esc(meta), size_text(m), m.players)
    tag = '<span class="tags"><span class="mtag o">Original</span></span>' if m.original else ""
    quote = "<p class=\"say\">\u201c%s\u201d<small>%s</small></p>" % (esc(m.text), KIND_NOTE) if m.text else ""
    buttons = ""
    if valid_map_file(m.file) or m.original:
        buttons = '<div class="acts"><a class="btn big" href="%s">Play it</a><a class="btn" href="%s">Open in Map maker</a></div><p class="meta why">%s</p>' % (esc(play_address(m)), esc(maker_address(m)), PLAY_NOTE)
    rows = facts_of(m)
    after = "Starting ants" if any(label == "Starting ants" for label, _ in rows) else "Game length"
    facts = "".join("<div><dt>%s</dt><dd>%s</dd></div>" % (esc(label), esc(text)) + ('<div id="plays-fact" hidden><dt>Played here</dt><dd id="plays-count"></dd></div>' if label == after else "") for label, text in rows)
    modes = "".join('<span class="fchip">%s</span>' % esc(t) for t in game_types_of(m))
    related = "".join(card(o) for o in m.related)
    return (
        '    <div class="pt"><a class="crumb" href="/map-library">&lsaquo; All maps</a></div>\n'
        '    <section class="panel mp first"><div class="hero maphero"><figure class="tv"><img src="%s" width="300" height="300" alt="The map %s of Ants, seen from above"></figure>\n'
        '    <div class="top"><h1>%s %s</h1><p class="meta">%s</p>%s</div>\n'
        '    <div class="rest">%s</div></div></section>\n'
        '    <section class="panel mp about" aria-labelledby="ab-h"><div class="head"><h2 id="ab-h">About this map</h2></div>%s</section>\n'
        '    <section class="panel mp" aria-labelledby="f-h"><div class="head"><h2 id="f-h">Facts</h2></div><dl class="facts">%s</dl>\n'
        '    <dl class="facts one"><div><dt>Game types it suits</dt><dd><span class="modes">%s</span></dd></div></dl></section>\n'
        '    <section class="panel mp" aria-labelledby="r-h"><div class="head"><h2 id="r-h">Maps like this one</h2><a class="count" href="/map-library">All maps &rsaquo;</a></div><ul class="grid">%s</ul></section>'
    ) % (esc(picture_of(m)), esc(m.name), esc(m.name), tag, meta, quote, buttons, "".join(paragraphs_of(m)), facts, modes, related)


def json_for_script(value):
    """JSON that is safe inside a <script> element: the characters of markup, and the two line separators, are written as escapes."""
    text = json.dumps(value, ensure_ascii=True, separators=(",", ":"))
    return text.replace("<", "\\u003c").replace(">", "\\u003e").replace("&", "\\u0026").replace("\u2028", "\\u2028").replace("\u2029", "\\u2029")


def breadcrumbs(origin, m):
    items = [("Home", origin + "/"), ("Maps", origin + "/map-library"), (m.name, "%s/map/%s" % (origin, m.slug))]
    data = {"@context": "https://schema.org", "@type": "BreadcrumbList",
            "itemListElement": [{"@type": "ListItem", "position": i + 1, "name": name, "item": item} for i, (name, item) in enumerate(items)]}
    return '    <script type="application/ld+json">%s</script>' % json_for_script(data)


def fill(template, slots):
    """The template with its @@SLOTS@@ filled in one pass: what a slot is filled with is never read for slots again."""
    return SLOT.sub(lambda found: slots[found.group(1)], template)


def page_slots(stamps, **own):
    slots = {"HEAD_EXTRA": "", "SCRIPTS": "", "GAME_VERSION": stamps[0], "BUILD_ID": stamps[1], "BUILD_DATE": stamps[2]}
    slots.update(own)
    return slots


def map_page(template, origin, m, stamps):
    return fill(template, page_slots(stamps, TITLE=esc(title_of(m)), DESCRIPTION=esc(description_of(m)), CANONICAL=esc("%s/map/%s" % (origin, m.slug)), IMAGE=esc(origin + picture_of(m)),
                                     IMAGE_ALT=esc("The map %s of Ants, seen from above." % m.name), HEAD_EXTRA=breadcrumbs(origin, m), BODY=map_body(m), SCRIPTS='<script src="/map_plays.js"></script>\n'))


def pages_count(maps):
    return max(1, -(-len(maps) // PAGE_SIZE))


def library_address(page):
    return "/map-library" if page == 1 else "/map-library?page=%d" % page


def pager(page, pages):
    """The pager of the index, as links (every page is a plain address): back, the first, the last and the neighbours of this page with gaps between, next."""
    shown = sorted({1, pages, page - 1, page, page + 1} | ({2, 3} if page == 1 else set()) | ({pages - 1, pages - 2} if page == pages else set()))
    shown = [n for n in shown if 1 <= n <= pages]
    out = ['<a class="pg wide" href="%s">&lsaquo; Back</a>' % library_address(page - 1) if page > 1 else '<span class="pg wide off">&lsaquo; Back</span>']
    last = 0
    for n in shown:
        if n - last > 1:
            out.append('<span class="gap">&hellip;</span>')
        out.append('<a class="pg num" aria-current="page" href="%s">%d</a>' % (library_address(n), n) if n == page else '<a class="pg num" href="%s">%d</a>' % (library_address(n), n))
        last = n
    out.append('<span class="of">of %d</span>' % pages)
    out.append('<a class="pg wide" href="%s">Next &rsaquo;</a>' % library_address(page + 1) if page < pages else '<span class="pg wide off">Next &rsaquo;</span>')
    return '<nav class="pager" id="pager" aria-label="Pages of maps">%s</nav>' % "".join(out)


def tools_panel(maps):
    """The search and the filters (hidden until web/map_library.js is there: without script the pages are the list and the pager). Players is a choice only when the library has two kinds of map."""
    counts = sorted({m.players for m in maps})
    players = ""
    if len(counts) > 1:
        players = '<label class="sel"><span>Players</span><select id="f-players"><option value="">Any</option>%s</select></label>' % "".join('<option value="%d">%d players</option>' % (n, n) for n in counts)
    sizes = "".join('<option value="%s">%s</option>' % (key, label) for key, label, _top in SIZE_CLASSES)
    lengths = "".join('<option value="%s">%s</option>' % (key, label) for key, label, _top in LENGTH_CLASSES)
    return (
        '    <section class="panel tools" id="tools" aria-label="Find a map" hidden>\n'
        '        <div class="r1"><div class="search"><input id="q" type="search" placeholder="Find a map by its name or its words" autocomplete="off" maxlength="%d" aria-label="Find a map"></div>\n'
        '        <div class="seg" id="kind" role="group" aria-label="Which maps"><button type="button" data-kind="" aria-pressed="true">All</button><button type="button" data-kind="original" aria-pressed="false">Original</button>'
        '<button type="button" data-kind="player" aria-pressed="false"><span class="long">Made by players</span><span class="short">Players</span></button></div></div>\n'
        '        <div class="r2 %s"><label class="sel"><span>Size</span><select id="f-size"><option value="">Any size</option>%s</select></label>%s'
        '<label class="sel"><span>Game length</span><select id="f-length"><option value="">Any</option>%s</select></label>'
        '<label class="sel sort"><span>Order</span><select id="f-order"><option value="">Originals first</option><option value="name">Name, A to Z</option><option value="plays">Most played</option></select></label>'
        '<button class="btn sm filt-btn" id="filters" type="button">Filters &#9662;</button></div>\n'
        '    </section>\n') % (QUERY_MAX, "four" if players else "three", sizes, players, lengths)


def library_body(maps, page):
    pages = pages_count(maps)
    shown = maps[(page - 1) * PAGE_SIZE:page * PAGE_SIZE]
    originals = sum(1 for m in maps if m.original)
    intro = "The six maps of the 1998 game and %d maps that its players made. Open a map to see it, play it in your browser or change it in the Map maker." % (len(maps) - originals)
    first = (page - 1) * PAGE_SIZE + 1
    return (
        '    <div class="pt"><h1>Maps</h1><p>%s</p></div>\n%s'
        '    <section class="panel" aria-labelledby="list-h"><div class="head"><h2 id="list-h">All maps</h2><span class="count" id="count" aria-live="polite">%d maps \u00b7 %d to %d</span></div>\n'
        '    <ul class="grid" id="list">%s</ul>\n    %s</section>\n    <noscript></noscript>'
    ) % (esc(intro), tools_panel(maps), len(maps), first, first + len(shown) - 1, "".join(card(m) for m in shown), pager(page, pages)), originals


def library_page(template, origin, maps, page, stamps):
    pages = pages_count(maps)
    body, originals = library_body(maps, page)
    players = len(maps) - originals
    if page == 1:
        title = "Maps: the %d original maps and %d player-made maps \u2014 Ants (1998)" % (originals, players)
        description = "Every map of the 1998 game Ants and the maps its players made: pictures, sizes and descriptions. Play any of them free in your browser or open it in the Map maker."
    else:
        title = "Maps, page %d of %d \u2014 Ants (1998)" % (page, pages)
        description = "Maps of the 1998 game Ants, page %d of %d: pictures, sizes and descriptions of the maps that its players made. Play free in your browser or open one in the Map maker." % (page, pages)
    scripts = '<script src="/map_library_data.js"></script>\n<script src="/map_library.js"></script>\n'
    return fill(template, page_slots(stamps, TITLE=esc(title), DESCRIPTION=esc(description), CANONICAL=esc(origin + library_address(page)), IMAGE=esc(origin + "/front/preview_treasure.png"),
                                     IMAGE_ALT="The Treasure map of Ants, seen from above.", BODY=body, SCRIPTS=scripts))


def library_data(maps):
    """The list that the index page's search reads (web/map_library.js): the tops of the sizes and of the game lengths (the choices of the page), and [slug, name, its line, width, height, minutes, picture, 1 for an original, file name, players]
    for each map."""
    rows = [[m.slug, m.name, m.text, m.width, m.height, m.minutes, picture_of(m), 1 if m.original else 0, m.file, m.players] for m in maps]
    data = {"format": 1, "page": PAGE_SIZE, "players": sorted({m.players for m in maps}), "sizes": [[key, top] for key, _label, top in SIZE_CLASSES], "lengths": [[key, top] for key, _label, top in LENGTH_CLASSES], "maps": rows}
    return "window.ANTS_MAP_LIBRARY=%s;\n" % json_for_script(data)


def build(origin, template, maps, stamps=STAMPS, ledger=None):
    """{path below OUT_DIR: text} of everything the pages are made of, for the maps that `plan` found (the origin is checked first: it goes into every page)."""
    seo_files.check_origin(origin)
    primaries, moved = plan(maps, ledger)
    files = {}
    for m in primaries:
        files["html/map_pages/map/%s.html" % m.slug] = map_page(template, origin, m, stamps)
    for page in range(1, pages_count(primaries) + 1):
        files["html/map_pages/library/%d.html" % page] = library_page(template, origin, primaries, page, stamps)
    files["html/map_library_data.js"] = library_data(primaries)
    files["nginx/moved.map"] = "".join("/map/%s /map/%s;\n" % (slug, target) for slug, target in sorted(moved.items()))
    files["addresses.txt"] = "".join(address + "\n" for address in ["/map-library"] + ["/map/" + m.slug for m in primaries])
    return files


def slugs_of(maps, ledger=None):
    return {m.slug: m.file for m in plan(maps, ledger)[0]}


def write(out_dir, files, inputs=()):
    library_guard.require_writable(out_dir, inputs)
    for name in sorted(files):
        path = os.path.join(out_dir, name)
        library_guard.require_writable(path, inputs)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(files[name])


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("origin", nargs="?")
    parser.add_argument("out_dir", nargs="?")
    parser.add_argument("--library", default=LIBRARY_DIR)
    parser.add_argument("--originals", default=ORIGINALS_DIR)
    parser.add_argument("--template", default=TEMPLATE)
    parser.add_argument("--ledger", default=LEDGER)
    parser.add_argument("--slugs", action="store_true")
    for flag, default in (("--version", STAMPS[0]), ("--build-id", STAMPS[1]), ("--build-date", STAMPS[2])):
        parser.add_argument(flag, default=default)
    args = parser.parse_args(argv[1:])
    try:
        maps = read_library(args.originals, args.library)
        ledger = read_ledger(args.ledger)
        if args.slugs:
            sys.stdout.write(json.dumps(slugs_of(maps, ledger), indent=1, sort_keys=True, ensure_ascii=True) + "\n")
            return 0
        if not args.origin or not args.out_dir:
            parser.print_usage(sys.stderr)
            return 2
        with open(args.template, encoding="utf-8") as f:
            template = f.read()
        write(args.out_dir, build(args.origin, template, maps, (args.version, args.build_id, args.build_date), ledger))
    except (ValueError, OSError, KeyError) as e:
        sys.stderr.write("map_pages.py: %s\n" % e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
