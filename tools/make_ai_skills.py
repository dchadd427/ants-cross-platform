#!/usr/bin/env python3
"""The skills that teach a person's own AI to make Ants maps (docs/MAP_AI.md, "The skills"): `ai_skills/<name>/SKILL.md` and its `reference/` files, the numbers and names inside them that
come from the program's own source, and the files that the site serves at /ai/skills/. Python standard library only.

  python3 tools/make_ai_skills.py --check            fails (exit 1, names the file and the block) when a generated block is not what the sources say now
  python3 tools/make_ai_skills.py --write            rewrites the generated blocks in place (a developer's step after a source changed)
  python3 tools/make_ai_skills.py --build OUTDIR     writes OUTDIR/ai/skills/: <name>.zip for each skill, all.zip, the folders (<name>/SKILL.md ...) and index.json; needs ai_skills/ and nothing else

A generated block is the text between `<!-- generated:NAME begin ... -->` and `<!-- generated:NAME end -->` in a skill file. Its content is made here from the sources and is never typed twice
(AGENTS.md, rule 9): the tile names from src/ants_assets/lvl_tile_names.inc, the palette, the teams, the food and the limits of the Map Maker (web/front/mapmaker_*.js), the limits of the map core
(include/ants_assets/lvl_parser.hpp) and of an online room (include/ants_net/room_limits.hpp), the checker's codes (docs/MAP_FORMAT.md), the tools and their arguments (hub/aitools.py) and the six
original maps (Original-Ants/Maps/*.LVL, read here with a small reader of the file format: docs/MAP_FORMAT.md, "The layout"). The sources are read by `--check` and `--write` only: the image's build
(`--build`) packs the files that are committed, so it needs none of them, and a test (tests/scripts/test_ai_skills.py) holds every block to its sources on every pull request.

What the site serves is the committed text without the marker lines. Zips are reproducible (sorted entries, a fixed time, fixed attributes, the same compression), so the same skills are the same
bytes. `index.json` is {"skills": [{"name", "topic", "description", "bytes" (of the zip), "files" (the paths in the folder)}], "all": {"zip", "bytes"}}; `topic` is the word that the tool `read_guide`
takes (hub/aitools.py GUIDE_TOPICS), and the page and the tool fetch /ai/skills/<name>/SKILL.md.
"""
import argparse
import ast
import collections
import json
import os
import re
import struct
import sys
import zipfile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SKILLS = os.path.join(REPO, "ai_skills")
TOPICS = collections.OrderedDict([("guide", "ants-map-guide"), ("tools", "ants-mapmaker-tools"), ("vocabulary", "ants-map-vocabulary"), ("terrain", "ants-map-terrain"),
                                  ("layout", "ants-map-layout"), ("check-and-fix", "ants-map-check-and-fix")])
BLOCK = re.compile(r"<!-- generated:([a-z0-9-]+) begin[^\n]*-->\n(.*?)<!-- generated:\1 end -->\n", re.S)
MARKER_LINE = re.compile(r"<!-- generated:[a-z0-9-]+ (?:begin[^\n]*|end) -->\n")
NAME = re.compile(r"[a-z0-9]+(?:-[a-z0-9]+)*\Z")
ZIP_TIME = (1980, 1, 1, 0, 0, 0)
HEAD = "<!-- generated:%s begin (tools/make_ai_skills.py makes this block from the program's source: change the source, not these lines) -->\n"
TAIL = "<!-- generated:%s end -->\n"


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def read_bytes(*parts):
    with open(os.path.join(REPO, *parts), "rb") as f:
        return f.read()


def number(text):
    """An integer of C++ or JS source: `0x540`, `10000`, `256u * 256u`, `512u * 1024u`."""
    text = text.replace("u", "").replace("'", "")
    total = 1
    for part in text.split("*"):
        total *= int(part.strip(), 0)
    return total


def grouped(n):
    return format(n, ",")


# ---- what the sources say ---------------------------------------------------------------------------------------------------------------------------

def balanced(src, start):
    """The text of the bracket that opens at src[start] (a [ or a {), strings skipped."""
    pairs, stack, i, quote = {"[": "]", "{": "}", "(": ")"}, [], start, None
    while i < len(src):
        c = src[i]
        if quote:
            if c == "\\":
                i += 1
            elif c == quote:
                quote = None
        elif c in "'\"":
            quote = c
        elif c in pairs:
            stack.append(pairs[c])
        elif c in "]})":
            if not stack or stack.pop() != c:
                raise ValueError("unbalanced")
            if not stack:
                return src[start:i + 1]
        i += 1
    raise ValueError("unbalanced")


def js_literal(src, name):
    """The value of `var NAME = [...]` (a list of lists of numbers and single-quoted strings) in a script."""
    at = src.index("var %s = [" % name)
    return ast.literal_eval(balanced(src, src.index("[", at)))


def tile_names():
    """The game's name of each tile id, from the list that the map core holds (one name for each id from 0)."""
    names = re.findall(r'^"([^"]*)",$', read("src", "ants_assets", "lvl_tile_names.inc"), re.M)
    if len(names) != 1344:
        raise ValueError("lvl_tile_names.inc does not hold 1,344 names")
    return names


def palette():
    src = read("web", "front", "mapmaker_palette.js")
    return {name: js_literal(src, name) for name in ("GROUND", "OBJECTS", "PLANTS", "BIGGER")}


def blocks_source():
    src = read("web", "front", "mapmaker_blocks.js")
    teams = [dict(key=m[0], label=m[1], hill=int(m[2]), marker=int(m[4])) for m in re.findall(r"\{ key: '(\w+)', label: '(\w+)', hill: (\d+), hillName: '(\w+)', marker: (\d+), markerName: '(\w+)' \}", src)]
    ants = re.search(r"var MIN_ANTS = (\d+), MAX_ANTS = (\d+);", src)
    stages = re.search(r"var MIN_STAGES = (\d+), MAX_STAGES = (\d+);", src)
    plain = {int(m[0]): (int(m[1]), int(m[2])) for m in re.findall(r"^\s+(\d): \{ units: (\d+), points: (\d+), stages:", src, re.M)}
    return {"teams": teams, "min_ants": int(ants.group(1)), "max_ants": int(ants.group(2)), "min_stages": int(stages.group(1)), "max_stages": int(stages.group(2)),
            "hill": int(re.search(r"var HILL = (\d+);", src).group(1)), "records": int(re.search(r"var MAX_RECORDS = (\d+);", src).group(1)), "items": js_literal(src, "FOOD_ITEMS"), "plain": plain}


def props_source():
    src = read("web", "front", "mapmaker_props.js")
    types = [(m[0], m[1], m[2]) for m in re.findall(r"\{ key: '(\w+)', label: '([^']+)', tile: (\d+|NONE), sprite", src)]
    ranges = re.search(r"var RANGES = \{ minutes: \{ min: (\d+), max: (\d+) \}, eggs: \{ min: (\d+), max: (\d+) \} \};", src)
    return {"types": types, "minutes": (int(ranges.group(1)), int(ranges.group(2))), "eggs": (int(ranges.group(3)), int(ranges.group(4)))}


def core_limits():
    src = read("include", "ants_assets", "lvl_parser.hpp")
    new = re.search(r"return LevelLimits\{LVL_CLASSIC_ROW_WALL, (\d+), LVL_MAX_DICTIONARY_ENTRIES, ([0-9u *]+), (\d+), ([0-9u *]+), false, LVL_EXTENDED_MAX_DESCRIPTION\};", src)
    big = re.search(r"return LevelLimits\{(\d+), (\d+), LVL_MAX_DICTIONARY_ENTRIES, ([0-9u *]+), (\d+), ([0-9u *]+), true, LVL_EXTENDED_MAX_DESCRIPTION\};", src)
    return {"wall": int(re.search(r"LVL_CLASSIC_ROW_WALL = (\d+);", src).group(1)), "dictionary": number(re.search(r"LVL_MAX_DICTIONARY_ENTRIES = (0x[0-9a-fA-F]+);", src).group(1)),
            "classic_columns": int(new.group(1)), "classic_cells": int(new.group(3)), "classic_file": number(new.group(4)),
            "ext_side": int(big.group(1)), "ext_cells": int(big.group(4)), "ext_file": number(big.group(5)),
            "ext_description": int(re.search(r"LVL_EXTENDED_MAX_DESCRIPTION = (\d+);", src).group(1)), "classic_description": int(re.search(r"at most (\d+) bytes in version 8", src).group(1)) - 1}


def online_limits():
    src = read("include", "ants_net", "room_limits.hpp")
    return {name: number(re.search(r"%s = ([0-9u *]+);" % name, src).group(1)) for name in ("kMaxCells", "kMaxStartAnts", "kHeavyStartAnts")}


def ground_names():
    src = read("web", "front", "mapmaker_ground.js")
    text = src[src.index("var NAMES = ("):src.index(").split(' ')")]
    return "".join(re.findall(r"'([^']*)'", text)).split(" ")


def aitools():
    sys.path.insert(0, REPO)
    from hub import aitools as module
    return module


def findings():
    """[(code, severity)] of the original-safe profile's table in docs/MAP_FORMAT.md, in the order of the table."""
    text = read("docs", "MAP_FORMAT.md")
    section = text[text.index("## The original-safe profile"):text.index("**Limits of the place are not rules of the original.**")]
    out = []
    for line in section.splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if line.startswith("| `") and len(cells) >= 3:
            for code in re.findall(r"`([a-z0-9_]+)`", cells[0]):
                out.append((code, re.match(r"[a-z]+", cells[1]).group(0)))
    return out


# ---- the six original maps, read as the file format says -------------------------------------------------------------------------------------------

GROUND_RANGES = [(431, 475, "mud"), (476, 484, "water"), (485, 486, "gravel"), (487, 490, "dirt"), (491, 522, "gravel"), (523, 570, "dirt"), (571, 579, "slate"), (580, 595, "gravel"), (596, 611, "mud"),
                 (612, 659, "slate"), (660, 669, "mud")]


def kind_of(tile):
    for first, last, kind in GROUND_RANGES:
        if first <= tile <= last:
            return kind
    return "other"


def read_map(path):
    """The facts of a version 8 file that the layout skill reports (docs/MAP_FORMAT.md, "The layout"): size, minutes, eggs, hills, ants, piles, power-ups, droppers, ground."""
    with open(path, "rb") as f:
        b = f.read()
    rd = lambda o: b[o] | (b[o + 1] << 8)
    minutes = rd(8)
    at = 42 + 11 * (rd(40) + 1)
    rows, cols = struct.unpack_from("<II", b, at)
    rows, cols = rows & 0xFFFF, cols & 0xFFFF
    at += 8
    cells = rows * cols
    l1, l2 = at, at + 6 * cells
    at = l2 + 6 * cells
    n1 = rd(at)
    records = [(rd(at + 2 + 6 * i), rd(at + 4 + 6 * i), rd(at + 6 + 6 * i)) for i in range(n1)]
    at += 2 + 6 * n1
    count2, piles = rd(at), []
    at += 2
    for _ in range(count2):
        piles.append({"row": rd(at), "col": rd(at + 2), "units": rd(at + 4), "points": rd(at + 6)})
        at += 10 + 4 * rd(at + 8)
    at += 4                                                     # (block 3: the word that the original drops, and the default ant type)
    count4 = rd(at)
    at += 2
    waypoints = []
    for _ in range(count4):
        flag = struct.unpack_from("<I", b, at + 4)[0]
        waypoints.append((rd(at), rd(at + 2), flag))
        at += 52 if flag else 8
    eggs = rd(at)
    markers = {152: "black", 153: "blue", 154: "green", 155: "red"}
    hills = {245: "black", 246: "blue", 247: "red", 248: "green"}
    entrance, kinds, powerups, ground = {}, collections.Counter(), 0, []
    for i in range(cells):
        tile, flags = rd(l2 + 6 * i), rd(l2 + 6 * i + 2)
        if tile in hills and flags & 1:
            entrance[hills[tile]] = (i // cols, i % cols)
        if 62 <= tile <= 66:
            powerups += 1
        kind = kind_of(rd(l1 + 6 * i))
        kinds[kind] += 1
        ground.append(kind)
    turns = [lambda r, c: (r, cols - 1 - c), lambda r, c: (rows - 1 - r, c), lambda r, c: (rows - 1 - r, cols - 1 - c)] + ([lambda r, c: (c, r)] if rows == cols else [])
    best = max(sum(1 for r in range(rows) for c in range(cols) if ground[r * cols + c] == ground[turn(r, c)[0] * cols + turn(r, c)[1]]) for turn in turns)
    far = lambda p, e: max(abs(p["row"] - e[0]), abs(p["col"] - e[1]))
    nearest = [min(far(p, e) for p in piles) for e in entrance.values()]
    shared = sum(1 for p in piles if len(entrance) > 1 and (lambda d: d[1] - d[0] <= 4)(sorted(far(p, e) for e in entrance.values())))
    ants = collections.Counter(markers[t] for t, _, _ in records if t in markers)
    plants = {(r, c) for t, r, c in records if t not in markers}
    droppers = len(plants & {(r, c) for r, c, f in waypoints if f})
    spots = sorted(entrance.values())
    dist = [max(abs(a[0] - c[0]), abs(a[1] - c[1])) for i, a in enumerate(spots) for c in spots[i + 1:]]
    return {"rows": rows, "cols": cols, "minutes": minutes, "eggs": eggs, "ants": sorted(set(ants.values())), "piles": len(piles), "points": sum(p["units"] * p["points"] for p in piles),
            "hill_distance": (min(dist), max(dist)), "nearest_food": (min(nearest), max(nearest)), "shared_food": shared, "symmetry": round(100.0 * best / cells), "powerups": powerups, "droppers": droppers, "ground": {k: round(100.0 * v / cells) for k, v in kinds.items()}}


# ---- the blocks --------------------------------------------------------------------------------------------------------------------------------

def table(head, rows):
    out = ["| " + " | ".join(head) + " |", "|" + "|".join("---" for _ in head) + "|"]
    return "\n".join(out + ["| " + " | ".join(str(c) for c in row) + " |" for row in rows]) + "\n"


def block_limits():
    core, online, blocks, props, tools = core_limits(), online_limits(), blocks_source(), props_source(), aitools()
    wall = core["wall"]
    rows = [
        ("A new Classic map: rows, columns, cells", "at most %d rows, %d columns and %s cells" % (wall, core["classic_columns"], grouped(core["classic_cells"]))),
        ("An Extended map: sides, cells", "at most %s on a side and %s cells (for example %d x %d)" % (grouped(core["ext_side"]), grouped(core["ext_cells"]), int(core["ext_cells"] ** 0.5), int(core["ext_cells"] ** 0.5))),
        ("Description", "a Classic map at most %d bytes (UTF-8 bytes, not letters), an Extended map at most %d bytes" % (core["classic_description"], core["ext_description"])),
        ("Minutes of a match", "%d to %d" % props["minutes"]),
        ("Eggs each team starts with", "%d to %d" % props["eggs"]),
        ("A hill", "%d x %d cells; its entrance is the cell in its second row and second column" % (blocks["hill"], blocks["hill"])),
        ("A clump of starting ants", "%d to %d ants (a team may have many clumps; a map holds at most %s records of ants and plants together)" % (blocks["min_ants"], blocks["max_ants"], grouped(blocks["records"]))),
        ("A food pile", "%d to %d stages; bites and points a bite %d to %d" % (blocks["min_stages"], blocks["max_stages"], tools.TOOLS["put_food"].schema["properties"]["bites"]["minimum"], tools.TOOLS["put_food"].schema["properties"]["bites"]["maximum"])),
        ("Different tiles in one map", "at most %s (a tile id is 0 to %d)" % (grouped(core["dictionary"]), core["dictionary"] - 1)),
        ("An online room plays", "a map of at most %s cells and %s starting ants (a game on your own computer has no such limit; above %s starting ants a map is called heavy)" % (grouped(online["kMaxCells"]), grouped(online["kMaxStartAnts"]), grouped(online["kHeavyStartAnts"]))),
        ("One tool call changes", "at most %s cells (at most %s listed one by one)" % (grouped(tools.MAX_CELLS), grouped(tools.MAX_LISTED))),
        ("get_region", "a window of at most %d x %d cells" % (tools.MAX_WINDOW, tools.MAX_WINDOW)),
        ("get_picture", "at most %s pixels on a side" % grouped(tools.MAX_PICTURE)),
        ("note, a map's name or a draft's name", "a note %d characters, a name %d characters" % (tools.NOTE_CHARS, tools.NAME_CHARS)),
    ]
    return table(("What", "The limit"), rows)


def block_teams():
    blocks, props, tools = blocks_source(), props_source(), aitools()
    if [t["key"] for t in blocks["teams"]] != list(tools.TEAMS):
        raise ValueError("the teams of the Map Maker and of hub/aitools.py differ")
    if [t[0] for t in props["types"]] != list(tools.ANT_TYPES):
        raise ValueError("the ant types of the Map Maker and of hub/aitools.py differ")
    first = table(("Team (the `team` argument)", "Hill tile", "Start marker tile"), [("`%s`" % t["key"], t["hill"], t["marker"]) for t in blocks["teams"]])
    second = table(("Ant type (the `type` and `default_ant` argument)", "Power-up tile that makes an ant this type"), [("`%s` (%s)" % (key, label), "none" if tile == "NONE" else tile) for key, label, tile in props["types"]])
    return first + "\n" + second


def block_ground():
    pal, names, edges = palette(), tile_names(), collections.defaultdict(set)
    letter = {"g": "gravel", "d": "dirt", "s": "slate", "m": "mud", "M": "mud", "w": "water"}
    for name in ground_names():
        m = re.match(r"^([gdsmMw])([gdsmMw])(\d\d)[a-z]", name)
        if m:
            edges[letter[m.group(1)]].add(letter[m.group(2)])
    rows = []
    for tile, name, label in pal["GROUND"]:
        if names[tile] != name:
            raise ValueError("palette name of tile %d is not the game's" % tile)
        rows.append((label, {"Gravel": "g", "Dirt": "d", "Slate": "s", "Mud": "m or M", "Water": "w"}[label], tile, name, ", ".join(sorted(edges[label.lower()])) or "none"))
    return table(("Kind", "Letter of its names", "Plain tile", "Game name", "Has edge pieces toward"), rows)


def block_objects():
    pal, names = palette(), tile_names()
    rows = []
    for tile, name, label in pal["OBJECTS"]:
        if names[tile] != name:
            raise ValueError("palette name of tile %d is not the game's" % tile)
        what = "rock" if name.startswith("smlrock") else "power-up" if 62 <= tile <= 66 else "fire wall" if tile == 134 else "finished bridge piece" if tile == 37 else "bomb" if 129 <= tile <= 132 else "object"
        rows.append((tile, label, name, what))
    return table(("Tile", "Name", "Game name", "What it is"), rows)


def block_plants():
    pal, names = palette(), tile_names()
    rows = []
    for tile, name, label in pal["PLANTS"]:
        if names[tile] != name:
            raise ValueError("palette name of tile %d is not the game's" % tile)
        rows.append((tile, label, name))
    return table(("Tile", "Name", "Game name"), rows)


def block_bigger():
    pal, names = palette(), tile_names()
    rows = []
    for tile, name, footprint in pal["BIGGER"]:
        if names[tile] != name:
            raise ValueError("palette name of tile %d is not the game's" % tile)
        cells = [tuple(int(x) for x in pair.split(",")) for pair in footprint.split(" ")]
        r0, r1, c0, c1 = min(c[0] for c in cells), max(c[0] for c in cells), min(c[1] for c in cells), max(c[1] for c in cells)
        rows.append((tile, name, len(cells), "%d x %d" % (c1 - c0 + 1, r1 - r0 + 1)))
    return table(("Tile", "Game name", "Cells it covers", "Columns x rows"), rows)


def block_food():
    blocks, names = blocks_source(), tile_names()
    rows = [(item[0], item[1], names[item[0]], item[2], item[3], len(item[4]) + 1) for item in blocks["items"]]
    plain = [(n, "%d bites, %d points a bite" % blocks["plain"][n]) for n in sorted(blocks["plain"])]
    return table(("Food id (the `food` argument)", "Item", "Game name", "Bites", "Points a bite", "Stages"), rows) + "\n" + table(("Stages (the `stages` argument)", "A plain pile of that size has"), plain)


def schema_text(schema):
    kind = schema.get("type")
    if "enum" in schema:
        return "|".join(schema["enum"])
    if kind == "integer":
        return "%s-%s" % (schema.get("minimum", ""), schema.get("maximum", ""))
    if kind == "string":
        return "text up to %d" % schema["maxLength"] if "maxLength" in schema else "text"
    if kind == "boolean":
        return "true|false"
    if kind == "array":
        inner = schema_text(schema["items"])
        return "[%s] x %d-%d" % (inner, schema.get("minItems", 0), schema.get("maxItems", 0)) if "maxItems" in schema else "[%s]" % inner
    if kind == "object":
        return "{" + ", ".join(k + ("" if k in schema.get("required", ()) else "?") + ": " + schema_text(v) for k, v in schema["properties"].items()) + "}"
    return "?"


def block_tools():
    tools, out = aitools(), []
    for tool in tools.TOOLS.values():
        args = [("%s*" if name in tool.schema["required"] else "%s") % name + " (" + schema_text(spec) + ")" for name, spec in tool.schema["properties"].items()]
        out.append("- `%s` (%s). %s" % (tool.name, tool.scope, tool.description))
        out.append("  Arguments: %s." % ("; ".join(args) if args else "none"))
    return "\n".join(out) + "\n\n`*` marks an argument that the call needs. `?` inside an object marks a field that may be left out.\n"


def block_errors():
    tools = aitools()
    return table(("Code", "HTTP status", "What it means"), [("`%s`" % code, status, text) for code, (status, text) in tools.ERRORS.items()])


def block_topics():
    tools = aitools()
    if list(TOPICS) != list(tools.GUIDE_TOPICS):
        raise ValueError("the topics of read_guide and of this tool differ")
    return table(("read_guide topic", "Skill"), [("`%s`" % topic, "`%s`" % skill) for topic, skill in TOPICS.items()])


def block_findings():
    return table(("Code", "Severity"), [("`%s`" % code, severity) for code, severity in findings()])


def span(low, high):
    return str(low) if low == high else "%d to %d" % (low, high)


def block_originals():
    first, second = [], []
    for name in ("TINY", "SMALL", "MEDIUM", "GAUNTLET", "ISLANDS", "TREASURE"):
        m = read_map(os.path.join(REPO, "Original-Ants", "Maps", name + ".LVL"))
        ground = ", ".join("%s %d%%" % (k, v) for k, v in sorted(m["ground"].items(), key=lambda kv: (-kv[1], kv[0])) if v)
        first.append((name, "%d x %d" % (m["rows"], m["cols"]), m["minutes"], m["eggs"], "/".join(str(n) for n in m["ants"]), span(*m["hill_distance"]), "%d%%" % m["symmetry"], ground))
        second.append((name, "%d (%s points)" % (m["piles"], grouped(m["points"])), span(*m["nearest_food"]), m["shared_food"], m["powerups"], m["droppers"]))
    return (table(("Map", "Size", "Minutes", "Eggs", "Ants for a team", "Hill entrance to hill entrance, in cells (rows or columns, the larger)", "Ground that matches its best mirror or turn", "Ground"), first) + "\n" +
            table(("Map", "Food piles (all the points)", "Nearest pile to a hill, in cells", "Piles about as near a second hill as the nearest (within 4 cells)", "Power-ups placed", "Flower droppers"), second))


GENERATORS = {"limits": block_limits, "teams": block_teams, "ground": block_ground, "objects": block_objects, "plants": block_plants, "bigger": block_bigger, "food": block_food,
              "tools": block_tools, "errors": block_errors, "topics": block_topics, "findings": block_findings, "originals": block_originals}


# ---- the files ---------------------------------------------------------------------------------------------------------------------------------

def skill_files():
    """{path relative to ai_skills/: text} of every file of every skill, in sorted order."""
    out = {}
    for name in sorted(os.listdir(SKILLS)):
        folder = os.path.join(SKILLS, name)
        if not os.path.isdir(folder):
            continue
        for base, folders, files in os.walk(folder):
            folders.sort()
            for f in sorted(files):
                path = os.path.join(base, f)
                with open(path, encoding="utf-8") as handle:
                    out[os.path.relpath(path, SKILLS).replace(os.sep, "/")] = handle.read()
    return out


def refresh(text, cache):
    """The text with every generated block made again (and the names of the blocks it holds)."""
    names = []

    def one(m):
        name = m.group(1)
        if name not in GENERATORS:
            raise ValueError("no generator for the block %s" % name)
        if name not in cache:
            cache[name] = GENERATORS[name]()
        names.append(name)
        return HEAD % name + "\n" + cache[name] + TAIL % name
    return BLOCK.sub(one, text), names


def stale():
    """[(path, block)] of the generated blocks that are not what the sources say."""
    cache, out = {}, []
    for path, text in skill_files().items():
        fresh, names = refresh(text, cache)
        if fresh != text:
            now = {m.group(1): m.group(2) for m in BLOCK.finditer(text)}
            want = {m.group(1): m.group(2) for m in BLOCK.finditer(fresh)}
            out += [(path, n) for n in names if now.get(n) != want.get(n)] or [(path, "?")]
    return out


def write_all():
    cache, changed = {}, []
    for path, text in skill_files().items():
        fresh, _ = refresh(text, cache)
        if fresh != text:
            with open(os.path.join(SKILLS, path), "w", encoding="utf-8", newline="\n") as f:
                f.write(fresh)
            changed.append(path)
    return changed


def served(text):
    """What a person's AI is given: the committed text without the marker lines of the generated blocks."""
    return MARKER_LINE.sub("", text).replace("\n\n\n", "\n\n")


def frontmatter(text):
    m = re.match(r"---\n(.*?)\n---\n", text, re.S)
    if not m:
        raise ValueError("no frontmatter")
    out = {}
    for line in m.group(1).splitlines():
        key, _, value = line.partition(":")
        out[key.strip()] = value.strip()
    return out


def make_zip(entries):
    """The bytes of a zip of {path: text}: sorted, a fixed time, fixed attributes, one compression."""
    import io
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for path in sorted(entries):
            info = zipfile.ZipInfo(path, ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = 0o644 << 16
            z.writestr(info, entries[path].encode("utf-8"), zipfile.ZIP_DEFLATED, 9)
    return buffer.getvalue()


def build(out_dir):
    """OUTDIR/ai/skills/: the folders, one zip for each skill, all.zip and index.json. Returns the index."""
    files = {path: served(text) for path, text in skill_files().items()}
    target = os.path.join(out_dir, "ai", "skills")
    index, every = [], {}
    for topic, name in TOPICS.items():
        mine = {p: t for p, t in files.items() if p.startswith(name + "/")}
        if name + "/SKILL.md" not in mine:
            raise ValueError("the skill %s has no SKILL.md" % name)
        meta = frontmatter(mine[name + "/SKILL.md"])
        if meta.get("name") != name or not meta.get("description"):
            raise ValueError("the frontmatter of %s does not name it" % name)
        data = make_zip(mine)
        index.append({"name": name, "topic": topic, "description": meta["description"], "bytes": len(data), "files": sorted(p[len(name) + 1:] for p in mine)})
        every.update(mine)
        for path, text in mine.items():
            os.makedirs(os.path.join(target, os.path.dirname(path)), exist_ok=True)
            with open(os.path.join(target, path), "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
        with open(os.path.join(target, name + ".zip"), "wb") as f:
            f.write(data)
    everything = make_zip(every)
    with open(os.path.join(target, "all.zip"), "wb") as f:
        f.write(everything)
    result = {"skills": index, "all": {"zip": "all.zip", "bytes": len(everything)}}
    with open(os.path.join(target, "index.json"), "w", encoding="utf-8", newline="\n") as f:
        f.write(json.dumps(result, indent=1, sort_keys=True) + "\n")
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description="The AI skills of the Map Maker (see the top of tools/make_ai_skills.py).")
    parser.add_argument("--check", action="store_true", help="fail when a generated block is not what the sources say")
    parser.add_argument("--write", action="store_true", help="make the generated blocks again, in place")
    parser.add_argument("--build", metavar="OUTDIR", help="write OUTDIR/ai/skills/ (zips, folders, index.json)")
    args = parser.parse_args(argv)
    if not (args.check or args.write or args.build):
        parser.error("say what to do: --check, --write or --build OUTDIR")
    if args.write:
        for path in write_all():
            print("rewrote " + os.path.join("ai_skills", path))
    if args.check:
        bad = stale()
        for path, name in bad:
            print("ai_skills/%s: the generated block %s is not what the sources say (run tools/make_ai_skills.py --write)" % (path, name), file=sys.stderr)
        if bad:
            return 1
    if args.build:
        built = build(args.build)
        print("%d skills, %d bytes in all.zip" % (len(built["skills"]), built["all"]["bytes"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
