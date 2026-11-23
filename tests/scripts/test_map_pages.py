#!/usr/bin/env python3
"""The map pages (tools/map_pages.py, docs/MAP_PAGES.md): one page for each map of the library at /map/NAME and the index at /map-library (run by ./run_tests.sh --fast and by the CI;
tests/data/map_pages_mutants.json breaks one rule at a time, for tools/mutate.py).

The pages are made from the real library (Original-Ants/Maps, Community-Maps) and read as a browser reads them (an HTML parser, never a line of text). What is asked:
  - every map has one address that is lower case letters and digits with single hyphens; two maps never share one, and a map that another one's name would take it from keeps it (the ledger of
    tests/data/map_page_slugs.json: an address that a search engine has listed never changes); the copies of one file under several names are one page, and the other names are redirected to it;
  - every page has its title in the site's one form and at most 70 characters, a description that fits a result, one canonical link that is its own address on the site's one address, a share card that says
    the same, a picture that exists, and a BreadcrumbList that parses; the sitemap lists exactly the canonical addresses of the pages that ask to be listed (made by tools/seo_files.py from the list that the tool writes);
  - a maker is shown only when the description clearly names one (a table of descriptions, and every maker that the real library gets is in the words of its description); a page with no maker has no "Made by";
  - what a player wrote (a map's name, its line) is data: put into a page it is text, in the share card, the title, the structured data and the links, whatever it holds, and a name that looks like a slot of the
    template is not filled in; a count that a file did not give is not shown; a map with no food is not offered as Highest score;
  - the index holds every map once, 48 to a page, each a plain link; the tool never writes into the map library and takes only an origin.
"""
import html
import html.parser
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import urllib.parse
import xml.etree.ElementTree as ET

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import map_pages as mp  # noqa: E402
import seo_files  # noqa: E402
from test_web_seo import parse  # noqa: E402  (the head of a page as a browser takes it: title, metas, links, scripts of data)

TOOL = os.path.join(REPO, "tools", "map_pages.py")
ORIGIN = "https://site.example"
LEDGER = os.path.join(REPO, "tests", "data", "map_page_slugs.json")
TITLE = re.compile(r"^(?P<page>.+) — Ants \(1998\)$")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


TEMPLATE = read("web", "map_pages", "page.html")


class Body(html.parser.HTMLParser):
    """What the body of a page holds: its links (address and words), its pictures, the headings and the text of every element that has a class."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.links, self.images, self.headings, self.texts, self.dts, self.tags = [], [], [], {}, {}, []
        self.stack, self.in_body = [], False

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        self.stack.append((tag, a, []))
        if tag == "body":
            self.in_body = True
        if tag == "img":
            self.images.append(a)
        if tag in ("script", "style") and self.in_body and not a.get("src"):
            self.tags.append(tag)                                                                # (an inline script or style in the body: the pages have none)

    def handle_endtag(self, tag):
        while self.stack:
            open_tag, a, parts = self.stack.pop()
            text = "".join(parts)
            if open_tag == "a":
                self.links.append((a.get("href"), text))
            if open_tag in ("h1", "h2"):
                self.headings.append((open_tag, text))
            if a.get("class"):
                self.texts.setdefault(a["class"], []).append(text)
            if self.stack:
                self.stack[-1][2].append(text)
            if open_tag == tag:
                break

    def handle_data(self, data):
        if self.stack:
            self.stack[-1][2].append(data)


def body_of(page):
    b = Body()
    b.feed(page)
    b.close()
    return b


def fake(file, text="", width=31, height=31, minutes=12, data=None, **extra):
    """A map of the library made up for a case: `text` is the header's description as the library holds it."""
    entry = {"file": file, "width": width, "height": height, "minutes": minutes, "players": 4, "description": text}
    entry.update(extra)
    return mp.LibMap(file, data if data is not None else file.encode("latin-1"), entry=entry)


def original(key, data=b"o"):
    m = mp.LibMap(key.upper() + ".LVL", data + key.encode(), original_key=key, entry={"width": 60, "height": 60, "minutes": 10, "players": 4, "description": "x"})
    m.name = key.capitalize()
    return m


class TheLibrary(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.maps = mp.read_library()
        cls.primaries, cls.moved = mp.plan(cls.maps)
        cls.files = mp.build(ORIGIN, TEMPLATE, cls.maps)

    def test_every_map_of_the_library_is_a_page_or_a_name_that_redirects_to_one(self):
        every = {m.file for m in self.maps}
        shown = {m.file for m in self.primaries} | {o.file for m in self.primaries for o in m.aliases}
        self.assertEqual(shown, every)
        self.assertEqual(len([m for m in self.primaries if m.original]), 6)
        with open(os.path.join(REPO, "Community-Maps", "maps.json"), encoding="utf-8") as f:
            self.assertEqual(len(every), 6 + len(json.load(f)["maps"]))                       # (nothing of maps.json is left out)
        self.assertEqual(len({m.digest for m in self.maps}), len(self.primaries))             # (one page for each different file)

    def test_an_address_is_lower_case_letters_and_digits_with_single_hyphens_and_two_maps_never_share_one(self):
        slugs = [m.slug for m in self.primaries]
        self.assertEqual(len(set(slugs)), len(slugs))
        for slug in slugs:
            self.assertRegex(slug, r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
            self.assertLessEqual(len(slug), 80, slug)
        self.assertEqual({p for p in self.files if p.startswith("html/map_pages/map/")}, {"html/map_pages/map/%s.html" % s for s in slugs})

    def test_the_index_lists_the_originals_in_the_order_of_the_page_then_the_maps_of_the_players_by_name_without_the_signs_in_front(self):
        self.assertEqual([m.name for m in self.primaries[:6]], ["Tiny", "Small", "Medium", "Gauntlet", "Treasure", "Islands"])
        keys = [mp.sort_key(m.name) for m in self.primaries[6:]]
        self.assertEqual(keys, sorted(keys))
        self.assertRegex(self.primaries[6].name, r"^[0-9A-Za-z]")                                  # (a map that starts with signs is among the letters, not first of all)
        order, _ = mp.plan([fake("!!!Zed.lvl", data=b"z"), fake("Alpha.lvl", data=b"a"), fake("~Beta.lvl", data=b"b"), fake("0 first.lvl", data=b"f")])
        self.assertEqual([m.name for m in order], ["0 first", "Alpha", "~Beta", "!!!Zed"])

    def test_the_ledger_of_addresses_is_what_the_library_gets(self):
        with open(LEDGER, encoding="utf-8") as f:
            pinned = json.load(f)
        got = mp.slugs_of(self.maps)
        for slug, file in pinned.items():
            self.assertEqual(got.get(slug), file, "the address /map/%s was for %s: an address that a search engine has listed is never taken away or given to another map" % (slug, file))
        self.assertEqual(sorted(set(got) - set(pinned)), [], "a new map: add its address to tests/data/map_page_slugs.json with `python3 tools/map_pages.py --slugs > tests/data/map_page_slugs.json` "
                                                            "(the old ones must not change)")

    def test_a_name_that_is_in_the_library_twice_with_the_same_bytes_is_one_page_with_the_other_names_redirected(self):
        for m in self.primaries:
            for other in m.aliases:
                self.assertEqual(other.digest, m.digest)
        for old, new in self.moved.items():
            self.assertIn(new, {m.slug for m in self.primaries})
            self.assertNotIn(old, {m.slug for m in self.primaries})                          # (an address that is a page is never redirected)
        lines = self.files["nginx/moved.map"].splitlines()
        self.assertEqual(len(lines), len(self.moved))
        for line in lines:
            self.assertRegex(line, r"^/map/[a-z0-9-]+ /map/[a-z0-9-]+;$")
        treasure = next(m for m in self.primaries if m.slug == "treasure")
        self.assertTrue(treasure.original)

    def test_the_pictures_that_the_pages_name_are_files_of_the_repository(self):
        for m in self.primaries:
            path = mp.picture_of(m)
            if m.original:
                self.assertTrue(os.path.isfile(os.path.join(REPO, "web", path.lstrip("/"))), path)
            else:
                name = urllib.parse.unquote(path[len("/maps/previews/"):])
                self.assertTrue(os.path.isfile(os.path.join(REPO, "Community-Maps", "previews", name)), path)

    def test_every_file_name_of_the_library_is_one_that_the_map_maker_and_the_game_page_fetch(self):
        for m in self.maps:
            self.assertTrue(mp.valid_map_file(m.file), m.file)

    def test_the_counts_come_from_the_file_and_agree_with_the_six_originals(self):
        by = {m.original_key: m for m in self.maps if m.original}
        self.assertEqual((by["tiny"].powerups, by["tiny"].width), (0, 31))                    # ("Tiny map with no PowerUps": the header says it and the count agrees)
        self.assertGreater(by["treasure"].powerups, 0)
        for m in self.maps:
            self.assertIsNotNone(m.food, m.file)
            self.assertEqual(len(m.ants), 4, m.file)


class TheAddresses(unittest.TestCase):
    def test_a_name_becomes_its_address_word(self):
        for name, slug in (("!!!!!!aLkAtRaZ!!!!!!", "alkatraz"), ("Fire!!!!!!!", "fire"), ("OCEAN#2", "ocean-2"), ("Ice Bridges 1.1", "ice-bridges-1-1"), ("A&E", "a-e")):
            self.assertEqual(mp.base_slug(name), slug)
        self.assertRegex(mp.base_slug("!!!"), r"^map-[0-9a-f]{6}$")                             # (a name with no letter or digit)
        long = mp.base_slug("a" * 70)
        self.assertLessEqual(len(long), mp.SLUG_MAX + 5)
        self.assertNotEqual(mp.base_slug("a" * 70), mp.base_slug("a" * 70 + "b"))              # (a cut name keeps a tag of the whole name)

    def test_two_different_maps_with_one_word_the_first_keeps_it_and_the_others_get_a_tag_of_their_own_file_name(self):
        primaries, _ = mp.plan([fake("Fire.lvl"), fake("FiRe.lvl"), fake("Fire!!!!.lvl")])
        slugs = {m.file: m.slug for m in primaries}
        self.assertEqual(slugs["FiRe.lvl"], "fire")                                               # (the name that sorts first)
        self.assertEqual(sorted(slugs.values()).count("fire"), 1)
        for file in ("Fire.lvl", "Fire!!!!.lvl"):
            self.assertRegex(slugs[file], r"^fire-[0-9a-f]{4,}$", file)
        alone, _ = mp.plan([fake("FiRe.lvl"), fake("Fire.lvl")])                                  # (the tag is of the file's name: another map being there or not does not change it)
        self.assertEqual({m.file: m.slug for m in alone}["Fire.lvl"], slugs["Fire.lvl"])

    def test_an_original_keeps_its_word_against_a_map_of_the_players(self):
        primaries, _ = mp.plan([fake("!Treasure.lvl"), original("treasure")])
        slugs = {m.file: m.slug for m in primaries}
        self.assertEqual(slugs["TREASURE.LVL"], "treasure")
        self.assertRegex(slugs["!Treasure.lvl"], r"^treasure-[0-9a-f]{4,}$")

    def test_the_same_bytes_under_several_names_are_one_page_of_the_spelling_that_most_names_share(self):
        same = b"the same file"
        group = [fake(n, data=same) for n in ("Treasure2.lvl", "WARP ZONE.lvl", "Treasure 2.lvl", "TrEaSuRe 3.lvl")]
        primaries, moved = mp.plan(group)
        self.assertEqual([m.name for m in primaries], ["Treasure 2"])
        self.assertEqual(sorted(o.name for o in primaries[0].aliases), ["TrEaSuRe 3", "Treasure2", "WARP ZONE"])
        self.assertEqual(moved, {"treasure2": "treasure-2", "treasure-3": "treasure-2", "warp-zone": "treasure-2"})
        spelled, _ = mp.plan([fake(n, data=b"w") for n in ("Aardvark.lvl", "Zed.lvl", "zed!.lvl")])
        self.assertEqual([m.name for m in spelled], ["Zed"])                                      # (two names spell it "zed": not the one that sorts first of all)
        decorated, _ = mp.plan([fake(n, data=b"x") for n in ("#Pond.lvl", "lvl#Pond.lvl", "!Pond.lvl")])
        self.assertEqual([m.name for m in decorated], ["!Pond"])                                  # (two names spell it "pond": the one that sorts first)
        plain, _ = mp.plan([fake(n, data=b"y") for n in ("!Turtle World.lvl", "Turtle World.lvl", "#Turtle World.lvl")])
        self.assertEqual([m.name for m in plain], ["Turtle World"])                               # (all spell it alike: the one that starts with a letter)

    def test_a_name_that_is_the_page_of_another_map_or_the_pages_own_word_is_not_redirected(self):
        primaries, moved = mp.plan([fake("Alpha.lvl", data=b"a"), fake("Beta.lvl", data=b"a"), fake("beta!.lvl", data=b"b")])
        self.assertEqual(sorted((m.name, m.slug) for m in primaries), [("Alpha", "alpha"), ("beta!", "beta")])
        self.assertEqual(moved, {})                                                               # ("beta" is the page of another map: that map keeps it)
        _, same = mp.plan([fake("Sand Box.lvl", data=b"c"), fake("sand-box.lvl", data=b"c")])
        self.assertEqual(same, {})                                                                # (the same word as the page's own: nothing to redirect)

    def test_related_maps_are_four_and_never_the_map_itself_and_the_same_every_time(self):
        maps = [fake("Treasure %d.lvl" % n, width=60, height=60) for n in range(8)] + [fake("Other %d.lvl" % n) for n in range(6)]
        primaries, _ = mp.plan(maps)
        for m in primaries:
            self.assertEqual(len(m.related), 4)
            self.assertNotIn(m, m.related)
        treasure = next(m for m in primaries if m.name == "Treasure 3")
        self.assertTrue(all(o.name.startswith("Treasure") for o in treasure.related))
        again, _ = mp.plan(maps)
        self.assertEqual([[o.slug for o in m.related] for m in primaries], [[o.slug for o in m.related] for m in again])


def lvl_bytes(rows, columns, power_ups=(), markers=(), food=0, minutes=12, description="a test map"):
    """A small version 8 map file made as docs/MAP_FORMAT.md lays it out: the cells of layer 2 hold `power_ups` (tile ids, from the first cell on), block 1 one record for every tile id of `markers`, block 2 `food` piles."""
    word, dword = (lambda v: struct.pack("<H", v)), (lambda v: struct.pack("<I", v))
    cells = rows * columns
    layer2 = [(power_ups[i] if i < len(power_ups) else 0x7FFE) for i in range(cells)]
    out = dword(8) + dword(1) + word(minutes) + description.encode("latin-1").ljust(30, b"\0") + word(0) + b"." + b"\0" * 10 + dword(rows) + dword(columns)
    out += b"".join(word(0) + word(0) + word(0) for _ in range(cells)) + b"".join(word(t) + word(0) + word(0) for t in layer2)
    out += word(len(markers)) + b"".join(word(t) + word(0) + word(i) for i, t in enumerate(markers))
    out += word(food) + b"".join(word(0) * 4 + word(0) for _ in range(food)) + word(0) + word(0) + word(0) + word(0)
    return out


class TheCountsOfAFile(unittest.TestCase):
    def test_the_counts_are_read_from_the_layout_of_the_file(self):
        data = lvl_bytes(3, 4, power_ups=(62, 63, 64, 65, 66, 61, 67, 62), markers=(152,) + (153,) * 2 + (154,) * 3 + (155,) * 4 + (31, 200), food=2, minutes=9, description="by Ann")
        self.assertEqual(mp.parse_lvl(data), {"width": 4, "height": 3, "minutes": 9, "description": "by Ann", "food": 2, "powerups": 6, "ants": [1, 2, 3, 4]})
        # (the tiles 61 and 67 are no power-ups, two other kinds of record are no start markers, and the first dword of the grid is the rows)

    def test_a_file_that_is_not_a_version_8_map_is_not_counted_and_never_stops_the_pages(self):
        good = lvl_bytes(2, 2)
        for bad in (b"", b"\0" * 5, b"x" * 100, good[:60], b"\x09" + good[1:], good[:53] + b"\xff\xff\xff\xff" + good[57:]):
            self.assertIsNone(mp.parse_lvl(bad), bad[:12])
        counted = mp.LibMap("Odd.lvl", b"not a map", entry={"width": 31, "height": 31, "minutes": 5, "players": 4, "description": "x"})
        self.assertEqual((counted.food, counted.powerups, counted.ants), (None, None, None))


class TheFileNames(unittest.TestCase):
    def test_a_file_name_is_one_that_the_map_maker_and_the_game_page_fetch(self):
        for good in ("a.lvl", "Treasure.lvl", "TREASURE.LVL", "A&E.lvl", "!Pond.lvl", "Ant's Island.lvl", "x" * 60 + ".lvl"):
            self.assertTrue(mp.valid_map_file(good), good)
        for bad in (".lvl", ".hidden.lvl", "a/b.lvl", "a\\b.lvl", "a:b.lvl", "a*b.lvl", "a?b.lvl", 'a"b.lvl', "a<b.lvl", "a>b.lvl", "a|b.lvl", "Treasure.txt", "Treasure", "\u00e9t\u00e9.lvl", "tab\t.lvl",
                    "x" * 61 + ".lvl", ""):
            self.assertFalse(mp.valid_map_file(bad), repr(bad))

    def test_a_map_whose_file_name_cannot_be_fetched_has_no_play_or_map_maker_button(self):
        m = fake("a:b<c>.lvl", data=b"colon")
        mp.plan([m])
        body = mp.map_body(m)
        self.assertNotIn("Play it", body)
        self.assertNotIn("Open in Map maker", body)
        ok = fake("Fine.lvl", data=b"fine")
        mp.plan([ok])
        self.assertIn("Play it", mp.map_body(ok))


class TheMaker(unittest.TestCase):
    NAMED = (("by Angel/Eclipse", "Angel/Eclipse"), ("*by Angel/Eclipse       ...", "Angel/Eclipse"), ("*Made By: Arn437        ...", "Arn437"), ("made by _Dangerb0y_", "_Dangerb0y_"),
             ("!Map Made By Ice        ...", "Ice"), ("*HQS By HaNiBaL*        ...", "HaNiBaL"), ("** By: TheDMaster *     ...", "TheDMaster"), ("*By YaMaHa and Pr0nGs   ...", "YaMaHa and Pr0nGs"),
             ("Made By __SaLtY_......", "__SaLtY_"), ("*xpert-by:Black_Tiger66 ...", "Black_Tiger66"), ("Mini Murky Map!!!!!!By Scythe", "Scythe"), ("by perfect_chaos", "perfect_chaos"),
             ("*made by:Hawkmon_92     ...", "Hawkmon_92"), ("MaDE By: __RiP_ChUcKy_", "__RiP_ChUcKy_"), ("Created by Pip, Pop", "Pip, Pop"), ("Made by LP_Linkin_Park_...", "LP_Linkin_Park_"))
    NOT_NAMED = ("*Lets Have Fun by Arcade...", "*Improved by Arcade     ...", "From Rexall to you", "Another from Chesha", "surrounded by water", "Attack by water", "stand by me", "Made by hand",
                 "!By: Fuel  have fun     ...", "^by Antsss.com Staff", "Have fun on this map by ONION", "by 123", "by", "", "*Get Food From Every Sid...", "Made by Ann and Bob and Cy and Di", "by A, B, C, D", "by a very very long name indeed here", "by " + "x" * 30)

    def maker(self, raw):
        return mp.maker_of(*mp.describe(raw))

    def test_a_maker_is_shown_when_the_description_clearly_names_one(self):
        for raw, name in self.NAMED:
            self.assertEqual(self.maker(raw), name, raw)

    def test_a_description_that_does_not_clearly_name_a_maker_names_none(self):
        for raw in self.NOT_NAMED:
            self.assertEqual(self.maker(raw), "", raw)

    def test_a_description_that_was_cut_names_nobody_because_the_name_may_be_cut(self):
        cut = "!Made by SomeVeryLongNam..."                                                       # (the header holds 27 characters: a longer text is cut at 24 and ends in three dots)
        self.assertEqual((len(cut), mp.describe(cut)[1]), (27, True))
        self.assertEqual(self.maker(cut), "")
        padded = "!Made by Some".ljust(24) + "..."                                                # (a shorter text is padded with spaces: the whole text is there)
        self.assertEqual((len(padded), mp.describe(padded)[1]), (27, False))
        self.assertEqual(self.maker(padded), "Some")

    def test_the_words_of_a_description_lose_the_sign_the_old_editors_put_first_and_the_padding(self):
        self.assertEqual(mp.describe("*by Angel/Eclipse       ..."), ("by Angel/Eclipse", False))
        self.assertEqual(mp.describe("!the world of bombs     ..."), ("the world of bombs", False))
        self.assertEqual(mp.describe("!iTs_AlKaTrAz_WiTh_BoMbS..."), ("iTs_AlKaTrAz_WiTh_BoMbS...", True))
        self.assertEqual(mp.describe("***BURP***"), ("BURP***", False))
        self.assertEqual(mp.describe("")[0], "")

    def test_every_maker_that_the_library_gets_is_in_the_words_of_the_description_and_its_page_says_made_by_only_then(self):
        maps = mp.read_library()
        primaries, _ = mp.plan(maps)
        files = mp.build(ORIGIN, TEMPLATE, maps)
        named = 0
        for m in primaries:
            page = files["html/map_pages/map/%s.html" % m.slug]
            page = page[page.index("<main"):]
            page = re.sub(r'<p class="say">.*?</p>', "", page)                                   # (the map's own line is the player's: it may say anything)
            text = html.unescape(" ".join(re.sub(r"<[^>]+>", " ", page).split()))
            if m.maker:
                named += 1
                self.assertTrue(set(re.findall(r"[A-Za-z0-9]+", m.maker)) <= set(re.findall(r"[A-Za-z0-9]+", m.text)), "%s: %r from %r" % (m.file, m.maker, m.text))
                self.assertIn("Made by %s" % m.maker, text, m.file)
            elif not m.original:
                self.assertNotIn("Made by", text, m.file)                                        # (no maker line where no description names one)
        self.assertGreater(named, 10)                                                              # (the rule finds makers in the library: a rule that never matched would pass the rest)
        self.assertLess(named, len(primaries) // 2)


class ThePages(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.maps = mp.read_library()
        cls.primaries, cls.moved = mp.plan(cls.maps)
        cls.files = mp.build(ORIGIN, TEMPLATE, cls.maps)
        cls.pages = {p: t for p, t in cls.files.items() if p.endswith(".html")}
        cls.slugs = {m.slug: m for m in cls.primaries}

    def address_of(self, path):
        folder, name = os.path.split(path)
        number = name[:-5]
        return "/map/" + number if folder.endswith("/map") else mp.library_address(int(number))

    def test_there_is_a_page_for_every_map_and_one_for_every_48_maps_of_the_index(self):
        maps = [p for p in self.pages if "/map/" in p]
        index = [p for p in self.pages if "/library/" in p]
        self.assertEqual(len(maps), len(self.primaries))
        self.assertEqual(len(index), -(-len(self.primaries) // 48))
        listed = []
        for p in sorted(index, key=lambda x: int(os.path.basename(x)[:-5])):
            links = [h for h, _ in body_of(self.pages[p]).links if h.startswith("/map/")]
            self.assertLessEqual(len(links), 48)
            listed += [h[len("/map/"):] for h in links]
        self.assertEqual(sorted(listed), sorted(self.slugs))                                    # (every map is on one page of the index, once, as a plain link)

    def test_every_page_has_a_title_in_the_one_form_a_description_that_fits_and_a_share_card_that_says_the_same(self):
        titles, descriptions = set(), set()
        for path, text in self.pages.items():
            head = parse(text)
            address = self.address_of(path)
            title = head.title
            self.assertRegex(title, TITLE, path)
            self.assertLessEqual(len(title), 70, path)
            titles.add(title)
            found = head.meta("name", "description")
            self.assertEqual(len(found), 1, path)
            self.assertTrue(60 <= len(found[0]) <= 175, "%s: %d characters" % (path, len(found[0])))
            descriptions.add(found[0])
            canonical = [l["href"] for l in head.links if l.get("rel") == "canonical"]
            expected = ORIGIN + address
            self.assertEqual(canonical, [expected], path)                                       # (its own address on the site's one address; page 1 of the index is /map-library)
            self.assertEqual(head.meta("property", "og:url"), [expected], path)
            self.assertEqual(head.meta("property", "og:title"), [title], path)
            self.assertEqual(head.meta("property", "og:description"), found, path)
            self.assertEqual(head.meta("name", "twitter:card"), ["summary"], path)
            self.assertEqual(head.meta("name", "robots"), [], path)                             # (listed: no noindex)
            image = head.meta("property", "og:image")
            self.assertEqual(len(image), 1, path)
            self.assertTrue(image[0].startswith(ORIGIN + "/"), image)
            self.assertEqual((head.meta("property", "og:image:width"), head.meta("property", "og:image:height")), (["300"], ["300"]), path)
            self.assertTrue(head.meta("property", "og:image:alt")[0].strip(), path)
        self.assertEqual(len(titles), len(self.pages))                                          # (no two pages with one title or one description)
        self.assertEqual(len(descriptions), len(self.pages))

    def test_the_share_card_of_a_map_names_its_own_picture(self):
        for slug, m in self.slugs.items():
            head = parse(self.files["html/map_pages/map/%s.html" % slug])
            self.assertEqual(head.meta("property", "og:image"), [ORIGIN + mp.picture_of(m)], slug)

    def test_a_map_page_has_a_breadcrumb_that_parses_and_ends_at_the_page_and_no_other_page_has_structured_data(self):
        for path, text in self.pages.items():
            head = parse(text)
            if "/map/" not in path:
                self.assertEqual(head.data_scripts, [], path)
                continue
            self.assertEqual(len(head.data_scripts), 1, path)
            raw = head.data_scripts[0]
            self.assertNotRegex(raw, r"[<>&]", path)                                            # (nothing in it can end the script element)
            data = json.loads(raw)
            self.assertEqual((data["@context"], data["@type"]), ("https://schema.org", "BreadcrumbList"))
            items = data["itemListElement"]
            self.assertEqual([i["position"] for i in items], [1, 2, 3])
            self.assertEqual([i["item"] for i in items], [ORIGIN + "/", ORIGIN + "/map-library", ORIGIN + self.address_of(path)])
            self.assertEqual(items[2]["name"], self.slugs[os.path.basename(path)[:-5]].name)

    def test_a_map_page_has_one_h1_a_play_button_and_a_map_maker_button_that_name_the_map_and_no_address_has_dot_html(self):
        for path, text in self.pages.items():
            body = body_of(text)
            self.assertEqual(len([h for h in body.headings if h[0] == "h1"]), 1, path)
            self.assertEqual(body.tags, [], path)                                               # (no script or style in the body: the head has the files, and the data is in one block)
            for href, _words in body.links:
                self.assertNotRegex(href or "", r"\.html", path)
                self.assertTrue((href or "").startswith(("/", "https://discord.gg/")), (path, href))
        for slug, m in self.slugs.items():
            links = dict((words, href) for href, words in body_of(self.pages["html/map_pages/map/%s.html" % slug]).links)
            play, maker = urllib.parse.urlsplit(links["Play it"]), urllib.parse.urlsplit(links["Open in Map maker"])
            self.assertEqual(play.path, "/play")
            query = urllib.parse.parse_qs(play.query)
            self.assertEqual(query, {"map": [m.original_key], "bots": ["medium"]} if m.original else {"mapfile": [m.file], "bots": ["medium"]}, slug)
            self.assertEqual((maker.path, urllib.parse.parse_qs(maker.query)), ("/mapmaker", {"mapfile": [m.file]}), slug)
            self.assertTrue(os.path.isfile(os.path.join(REPO, "Original-Ants", "Maps", m.file) if m.original else os.path.join(REPO, "Community-Maps", m.file)), slug)

    def test_every_link_of_a_map_page_goes_to_a_page_that_exists(self):
        index = {"/map-library"} | {mp.library_address(n) for n in range(1, 40)}
        for path, text in self.pages.items():
            for href, _ in body_of(text).links:
                address = urllib.parse.urlsplit(href)
                if href.startswith("https://discord.gg/"):
                    continue
                if href.startswith("/map/"):
                    self.assertIn(address.path[len("/map/"):], self.slugs, (path, href))
                elif address.path == "/map-library":
                    self.assertIn(href, index, (path, href))
                else:
                    self.assertIn(address.path, ("/", "/matches", "/play", "/mapmaker"), (path, href))

    def test_the_about_text_and_the_facts_are_written_from_the_map(self):
        treasure = body_of(self.pages["html/map_pages/map/treasure.html"])
        facts = dict(re.findall(r"<dt>([^<]*)</dt><dd>([^<]*)</dd>", self.pages["html/map_pages/map/treasure.html"]))
        self.assertEqual((facts["Kind"], facts["Size"], facts["Players"], facts["Made by"]), ("Original (1998)", "60 x 60 squares", "4 anthills", "The makers of Ants"))
        self.assertRegex(facts["Food"], r"^\d+ piles$")
        self.assertIn(("h2", "About this map"), treasure.headings)
        about = " ".join(treasure.texts.get("panel mp about", []))
        self.assertIn("one of the six maps that came with Ants in 1998", about)
        self.assertIn("12 minutes", about)

    def test_the_game_types_follow_the_food_and_a_count_that_the_file_did_not_give_is_not_shown(self):
        fed, hungry = fake("Fed.lvl"), fake("Hungry.lvl")
        fed.food, fed.powerups, fed.ants = 3, 2, [4, 4, 4, 4]
        hungry.food, hungry.powerups, hungry.ants = 0, 0, [3, 3, 4, 4]
        unknown = fake("Unknown.lvl")
        self.assertEqual(mp.game_types_of(fed), ["Highest score", "187"])
        self.assertEqual(mp.game_types_of(hungry), ["187"])                                      # (no food: only 187)
        self.assertEqual(dict(mp.facts_of(fed))["Starting ants"], "4 each")
        self.assertEqual(dict(mp.facts_of(hungry))["Starting ants"], "14 in all")
        self.assertEqual((dict(mp.facts_of(hungry))["Food"], dict(mp.facts_of(hungry))["Power-ups"]), ("none", "none"))
        for label in ("Food", "Power-ups", "Starting ants"):
            self.assertNotIn(label, dict(mp.facts_of(unknown)))
        self.assertNotIn("Made by", dict(mp.facts_of(unknown)))
        self.assertEqual(mp.food_phrase(unknown), "")

    def test_a_repaired_map_says_what_was_changed_and_a_map_with_other_names_lists_them(self):
        m = fake("Fixed.lvl", repaired="Text-transfer damage undone: 8 zero bytes put back.")
        self.assertIn("Text-transfer damage undone", " ".join(mp.paragraphs_of(m)))
        group, _ = mp.plan([fake("Alley Ants.lvl", data=b"z"), fake("AlleyAnts.lvl", data=b"z")])
        self.assertEqual(dict(mp.facts_of(group[0]))["Also shared as"], "AlleyAnts")
        self.assertIn("also shared as AlleyAnts", " ".join(mp.paragraphs_of(group[0])))

    def test_the_six_originals_have_the_counts_of_their_files(self):
        # (the files never change: food piles, power-ups and the start markers of a team; the same counts as the server's, src/ants_server/online_limits.cpp)
        by = {m.original_key: (m.food, m.powerups, m.ants) for m in mp.read_library() if m.original}
        self.assertEqual(by, {"tiny": (14, 0, [3, 3, 3, 3]), "small": (5, 2, [4, 4, 4, 4]), "medium": (4, 0, [6, 6, 6, 6]), "gauntlet": (2, 10, [6, 6, 6, 6]),
                              "treasure": (18, 20, [6, 6, 6, 6]), "islands": (10, 40, [8, 8, 8, 8])})

    def test_the_players_choice_is_there_only_when_the_library_has_two_kinds_of_map(self):
        four = [fake("A.lvl", data=b"1"), fake("B.lvl", data=b"2")]
        mixed = [fake("A.lvl", data=b"1"), fake("B.lvl", data=b"2", players=2)]
        self.assertNotIn('id="f-players"', mp.tools_panel(four))
        self.assertIn('id="f-players"', mp.tools_panel(mixed))
        self.assertIn('<option value="2">2 players</option>', mp.tools_panel(mixed))

    def test_the_index_pager_names_the_neighbours_and_every_page_is_a_plain_address(self):
        out = mp.pager(1, 11)
        self.assertEqual(re.findall(r'href="([^"]*)"', out), ["/map-library", "/map-library?page=2", "/map-library?page=3", "/map-library?page=11", "/map-library?page=2"])
        mid = mp.pager(6, 11)
        self.assertEqual([n for n in re.findall(r'class="pg num[^>]*>(\d+)<', mid)], ["1", "5", "6", "7", "11"])
        self.assertIn('aria-current="page" href="/map-library?page=6"', mid)
        self.assertEqual(re.findall(r'class="pg wide" href="([^"]*)"', mid), ["/map-library?page=5", "/map-library?page=7"])
        self.assertNotIn("<a", mp.pager(1, 1).replace('<a class="pg num" aria-current="page" href="/map-library">1</a>', ""))
        last = mp.pager(11, 11)
        self.assertIn('<span class="pg wide off">Next', last)
        self.assertIn('<span class="pg wide off">&lsaquo; Back', out)


class TheTextOfAPlayerIsData(unittest.TestCase):
    EVIL = '<img src=x onerror=alert(1)>"\'</title></script><script>alert(2)</script>@@BODY@@&amp;'

    def build(self, name, text):
        maps = [fake(name + ".lvl", text, data=b"evil")]
        return mp.build(ORIGIN, TEMPLATE, maps), maps

    def test_a_name_and_a_line_that_look_like_markup_are_text_everywhere_a_page_puts_them(self):
        files, maps = self.build("Evil " + self.EVIL, "by Eve<b>")
        page = files["html/map_pages/map/%s.html" % mp.plan(maps)[0][0].slug]
        self.assertNotIn("<script>alert", page)
        self.assertNotIn("<img src=x", page)
        head, body = parse(page), body_of(page)
        self.assertEqual(body.tags, [])                                                          # (no element came out of the name)
        self.assertEqual(body.images[0]["alt"], "The map Evil %s of Ants, seen from above" % self.EVIL)       # (the attribute holds the name as it is)
        self.assertEqual(head.title.split(" (")[0].startswith("Evil <img"), True)
        self.assertEqual(head.meta("property", "og:title"), [head.title])
        self.assertIn("@@BODY@@", body.headings[0][1])                                           # (a name that is the word of a slot is not filled in: one pass)
        data = json.loads(head.data_scripts[0])
        self.assertEqual(data["itemListElement"][2]["name"], "Evil " + self.EVIL)
        self.assertNotIn("</script><script>", page)

    def test_the_list_that_the_index_reads_cannot_end_its_script_and_keeps_the_text(self):
        maps = [fake("Evil </script><script>alert(1)</script>.lvl", "  </script>", data=b"e2")]
        mp.plan(maps)
        text = mp.library_data(maps)
        self.assertNotIn("</script>", text)
        self.assertNotIn(" ", text)
        data = json.loads(text[len("window.ANTS_MAP_LIBRARY="):].rstrip(";\n"))
        self.assertEqual(data["maps"][0][1], "Evil </script><script>alert(1)</script>")

    def test_a_link_to_the_match_list_and_to_the_map_maker_carries_the_name_percent_encoded(self):
        m = fake("A&B #1 '%20'.lvl", data=b"q")
        mp.plan([m])
        self.assertEqual(mp.maker_address(m), "/mapmaker?mapfile=A%26B%20%231%20%27%2520%27.lvl")
        self.assertIn("/matches?q=A%26B%20%231%20%27%2520%27", " ".join(mp.paragraphs_of(m)))
        self.assertNotIn("&B", mp.card(m).replace("&amp;B", ""))

    def test_the_repair_note_and_the_other_names_are_text_too_and_the_match_search_gets_at_most_what_its_box_takes(self):
        group, _ = mp.plan([fake("Plain.lvl", data=b"p", repaired="<b>fixed</b> & done"), fake("<i>Other</i>.lvl", data=b"p")])
        text = " ".join(mp.paragraphs_of(group[0]))
        self.assertNotIn("<b>", text)
        self.assertNotIn("<i>", text)
        self.assertIn("&lt;b&gt;fixed&lt;/b&gt; &amp; done", text)
        self.assertIn("&lt;i&gt;Other&lt;/i&gt;", text)
        long = fake("L" * 60 + ".lvl", data=b"l60")
        query = re.search(r"/matches\?q=([^\"]*)", " ".join(mp.paragraphs_of(long))).group(1)
        self.assertEqual(urllib.parse.unquote(query), "L" * 40)

    def test_a_long_name_is_cut_to_a_title_of_70_characters_and_the_description_to_175(self):
        m = fake("N" * 60 + ".lvl", "x" * 27, data=b"long")
        mp.plan([m])
        self.assertLessEqual(len(mp.title_of(m)), 70)
        self.assertTrue(mp.title_of(m).endswith(" (player map, 31 x 31) — Ants (1998)"))
        self.assertLessEqual(len(mp.description_of(m)), 175)
        huge = fake("H" * 200 + ".lvl", "x", data=b"huge")
        self.assertLessEqual(len(mp.description_of(huge)), 175)
        self.assertTrue(mp.description_of(huge).endswith("..."))
        self.assertGreaterEqual(len(mp.description_of(fake("A.lvl"))), 60)


class TheSitemap(unittest.TestCase):
    def test_the_sitemap_lists_the_pages_that_ask_to_be_listed_by_their_canonical_addresses_and_nothing_else(self):
        files = mp.build(ORIGIN, TEMPLATE, mp.read_library())
        root = ET.fromstring(seo_files.sitemap_xml(ORIGIN, files["addresses.txt"].split()))
        ns = "{http://www.sitemaps.org/schemas/sitemap/0.9}"
        locations = [e.find(ns + "loc").text for e in root]
        self.assertEqual(len(locations), len(set(locations)))
        canon = [l["href"] for path, text in files.items() if path.endswith(".html") for l in parse(text).links if l.get("rel") == "canonical"]
        fixed = {ORIGIN + page for page in seo_files.PUBLIC_PAGES}
        self.assertTrue(fixed <= set(locations))                                                 # (the pages of the site are still listed)
        self.assertEqual(set(locations) - fixed, {c for c in canon if "?" not in c})            # (the pages of the library that ask to be listed: page 1 of the index and every map)
        for location in locations:
            self.assertNotIn("?", location)
        self.assertNotIn(ORIGIN + "/map-library?page=2", locations)                              # (the other pages of the index are reached by its links, and name themselves)
        self.assertIn(ORIGIN + "/map-library?page=2", canon)

    def test_the_tool_takes_a_file_of_addresses_and_refuses_what_is_no_address(self):
        text = seo_files.sitemap_xml(ORIGIN, ["/map/a", "/map/a", "/map-library"])
        self.assertEqual(text.count("/map/a<"), 1)
        for bad in ("map/a", "//evil.example", "/a b", "/a?x=1", "/a#x", "/<x>", "http://x/", "/a\n/b"):
            with self.assertRaises(ValueError, msg=bad):
                seo_files.sitemap_xml(ORIGIN, [bad])
        with tempfile.TemporaryDirectory(prefix="ants_seo_") as tmp:
            pages = os.path.join(tmp, "pages.txt")
            with open(pages, "w", encoding="utf-8") as f:
                f.write("/map-library\n/map/treasure\n\n")
            done = subprocess.run([sys.executable, os.path.join(REPO, "tools", "seo_files.py"), ORIGIN, tmp, "--pages", pages], capture_output=True, text=True, timeout=60)
            self.assertEqual(done.returncode, 0, done.stderr)
            self.assertIn("<loc>%s/map/treasure</loc>" % ORIGIN, read_file(os.path.join(tmp, "sitemap.xml")))
            missing = subprocess.run([sys.executable, os.path.join(REPO, "tools", "seo_files.py"), ORIGIN, tmp, "--pages", os.path.join(tmp, "nothing.txt")], capture_output=True, text=True, timeout=60)
            self.assertEqual(missing.returncode, 1)


def read_file(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


class TheTool(unittest.TestCase):
    def run_tool(self, *args):
        return subprocess.run([sys.executable, TOOL] + list(args), capture_output=True, text=True, timeout=120)

    def test_the_tool_writes_the_pages_the_list_the_redirects_and_the_addresses_below_its_folder(self):
        with tempfile.TemporaryDirectory(prefix="ants_mp_") as out:
            done = self.run_tool(ORIGIN, out, "--version", "v1.2.3", "--build-id", "abc1234", "--build-date", "2026-01-01 09:00 PT")
            self.assertEqual(done.returncode, 0, done.stderr)
            self.assertTrue(os.path.isfile(os.path.join(out, "html", "map_pages", "map", "treasure.html")))
            self.assertTrue(os.path.isfile(os.path.join(out, "html", "map_pages", "library", "1.html")))
            for name in ("html/map_library_data.js", "nginx/moved.map", "addresses.txt"):
                self.assertTrue(os.path.getsize(os.path.join(out, name)) > 0, name)
            page = read_file(os.path.join(out, "html", "map_pages", "map", "treasure.html"))
            self.assertIn('data-version="v1.2.3" data-build="abc1234" data-built="2026-01-01 09:00 PT"', page)           # (the stamps of the other pages)
            self.assertNotRegex(page, r"@@[A-Z_]+@@")

    def test_an_address_that_is_not_an_origin_stops_it_before_anything_is_written(self):
        for bad in ("beta.playants.org", "https://site.example/", "https://site.example/x", "ftp://x.example"):
            with tempfile.TemporaryDirectory(prefix="ants_mp_") as out:
                done = self.run_tool(bad, out)
                self.assertEqual(done.returncode, 1, bad)
                self.assertIn("not an origin", done.stderr)
                self.assertEqual(os.listdir(out), [], bad)

    def test_the_tool_never_writes_into_the_map_library(self):
        for folder in (os.path.join(REPO, "Community-Maps", "pages_x"), os.path.join(REPO, "Original-Ants", "x")):
            self.addCleanup(shutil.rmtree, folder, True)                                       # (a tool that did write there must not leave the library dirty: the test fails, the folder goes)
            done = self.run_tool(ORIGIN, folder)
            self.assertEqual(done.returncode, 1)
            self.assertIn("map library", done.stderr)
            self.assertFalse(os.path.exists(folder))

    def test_a_library_whose_list_names_a_file_that_is_not_there_stops_the_build(self):
        with tempfile.TemporaryDirectory(prefix="ants_mp_") as tmp:
            library = os.path.join(tmp, "lib")
            os.makedirs(library)
            with open(os.path.join(library, "maps.json"), "w", encoding="utf-8") as f:
                json.dump({"format": 1, "maps": [{"file": "Gone.lvl", "width": 31, "height": 31, "minutes": 5, "players": 4, "description": "x"}]}, f)
            done = self.run_tool(ORIGIN, os.path.join(tmp, "out"), "--library", library)
            self.assertEqual(done.returncode, 1)
            self.assertIn("Gone.lvl", done.stderr)

    def test_the_slugs_option_prints_the_ledger(self):
        done = self.run_tool("--slugs")
        self.assertEqual(done.returncode, 0, done.stderr)
        got = json.loads(done.stdout)
        self.assertEqual(got["treasure"], "TREASURE.LVL")
        self.assertEqual(got, mp.slugs_of(mp.read_library()))

    def test_every_slot_of_the_template_is_one_that_the_tool_fills(self):
        slots = set(re.findall(r"@@([A-Z_]+)@@", TEMPLATE))
        self.assertEqual(slots - set(mp.page_slots(("a", "b", "c"), TITLE="", DESCRIPTION="", CANONICAL="", IMAGE="", IMAGE_ALT="", BODY="")), set())


if __name__ == "__main__":
    unittest.main()
