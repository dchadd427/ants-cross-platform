#!/usr/bin/env python3
"""The players' maps of the repository (Community-Maps/): which maps of a collection are taken in, and the list that says what is there.

usage: community_maps.py build SRC_DIR SWEEP_REPORT DEST_DIR   copy the maps that pass into DEST_DIR and write DEST_DIR/maps.json
       community_maps.py discards SRC_DIR SWEEP_REPORT         the maps that do not pass, one line each with the reason, then the counts
       community_maps.py verify DIR SWEEP_REPORT               DIR holds only maps that pass in a sweep of DIR itself, and maps.json lists exactly them

SWEEP_REPORT is the --out file of `map_sweep FOLDER --out REPORT` (tools/map_sweep.cpp) over the same folder: it says for every file whether the engine loads it,
whether it can be played with every roster and is deterministic, and what is odd about it. A map passes when
  - the engine loads it and every roster it can host plays without a crash, a hang or an error, and three plays give one state hash;
  - every tile on it is a name of its own dictionary (a tile outside it is what the original draws as heap garbage, so there is nothing to copy);
  - the file holds no email address (the repository is public: no personal data) and no program (a map that carries one is not a map).
The six maps of the original game are not taken: they are in Original-Ants/Maps.
"""
import json
import os
import re
import shutil
import sys

ORIGINAL_NAMES = ("TINY", "SMALL", "MEDIUM", "GAUNTLET", "TREASURE", "ISLANDS")
EMAIL = re.compile(r"[\w.+-]+@[\w-]+(\.[\w-]+)+")
PROGRAM = re.compile(r"this program (cannot|requires)|\.(exe|dll|sys|bat)\b|kernel32", re.I)
PRINTABLE_RUN = re.compile(rb"[\x20-\x7e]{4,}")
REASONS = ("does not load", "not playable", "play fails", "tile outside the dictionary", "email address inside", "program code inside")


def fnv1a64(data):
    """The identity of a map file in the network protocol (ants::net::hash_file)."""
    h = 14695981039346656037
    for byte in data:
        h = ((h ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % h


def reason(entry, data):
    """None when the map passes; else the first of REASONS that applies. `entry` is one object of the sweep report's "maps", `data` the file's bytes."""
    if not entry["loads"]:
        return REASONS[0]
    if not entry["playable"]:
        return REASONS[1]
    if any(r["status"] != "ok" or not r["deterministic"] for r in entry["runs"]):
        return REASONS[2]
    if any(p["kind"] == "tile_outside_dictionary" for p in entry["problems"]):
        return REASONS[3]
    texts = [m.group().decode("ascii") for m in PRINTABLE_RUN.finditer(data)]
    if any(EMAIL.search(t) for t in texts):
        return REASONS[4]
    if any(PROGRAM.search(t) for t in texts):
        return REASONS[5]
    return None


def is_original(name):
    return os.path.splitext(name)[0].upper() in ORIGINAL_NAMES


def header(data):
    """(minutes, description) of a level file: the header is version, mode, minutes (u16) and a 30 byte text, as the setup screen's Map Info shows them."""
    minutes = int.from_bytes(data[8:10], "little")
    text = data[10:40].split(b"\0")[0].decode("latin-1")
    return minutes, "".join(c for c in text if c.isprintable()).strip()


def manifest_entry(entry, data):
    minutes, description = header(data)
    # players: the teams that have a start marker or a hill (a map with neither is played by the sweep with all four teams, so it counts four)
    teams = sum(1 for marks, hills in zip(entry["start_markers"], entry["hill_cells"]) if marks or hills) or 4
    return {
        "file": entry["name"], "size": len(data), "hash": fnv1a64(data), "width": entry["width"], "height": entry["height"], "minutes": minutes,
        "players": teams, "description": description,
    }


def load_report(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)["maps"]


def read(folder, name):
    with open(os.path.join(folder, name), "rb") as f:
        return f.read()


def build(src, report, dest):
    kept = []
    for entry in report:
        if is_original(entry["name"]):
            continue
        data = read(src, entry["name"])
        if reason(entry, data) is None:
            kept.append((entry, data))
    os.makedirs(dest, exist_ok=True)
    for entry, _ in kept:
        shutil.copyfile(os.path.join(src, entry["name"]), os.path.join(dest, entry["name"]))
    rows = sorted((manifest_entry(e, d) for e, d in kept), key=lambda r: r["file"].encode("latin-1"))
    with open(os.path.join(dest, "maps.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump({"format": 1, "maps": rows}, f, indent=1, ensure_ascii=True)
        f.write("\n")
    return len(rows)


def discards(src, report):
    counts = {r: 0 for r in REASONS}
    for entry in report:
        if is_original(entry["name"]):
            continue
        why = reason(entry, read(src, entry["name"]))
        if why:
            counts[why] += 1
            print("%s\t%s" % (why, entry["name"]))
    return counts


def verify(folder, report):
    """Problems found (a list of texts, empty when all is well)."""
    problems = []
    names = sorted(n for n in os.listdir(folder) if n.lower().endswith(".lvl"))
    if sorted(e["name"] for e in report) != names:
        problems.append("the sweep report and the folder name different files")
    for entry in report:
        why = reason(entry, read(folder, entry["name"])) if entry["name"] in names else None
        if why:
            problems.append("%s: %s" % (entry["name"], why))
        if is_original(entry["name"]):
            problems.append("%s: an original map belongs in Original-Ants/Maps" % entry["name"])
    with open(os.path.join(folder, "maps.json"), encoding="utf-8") as f:
        listed = json.load(f)["maps"]
    if [r["file"] for r in listed] != sorted((r["file"] for r in listed), key=lambda n: n.encode("latin-1")) or sorted(r["file"] for r in listed) != names:
        problems.append("maps.json does not list exactly the folder's maps in order")
    by_name = {e["name"]: e for e in report}
    for row in listed:
        if row["file"] in by_name and row != manifest_entry(by_name[row["file"]], read(folder, row["file"])):
            problems.append("%s: its line in maps.json is not what the file and the sweep say" % row["file"])
    return problems


def main(argv):
    if len(argv) == 5 and argv[1] == "build":
        print("%d maps taken in" % build(argv[2], load_report(argv[3]), argv[4]))
        return 0
    if len(argv) == 4 and argv[1] == "discards":
        counts = discards(argv[2], load_report(argv[3]))
        for why in REASONS:
            print("%d\t%s" % (counts[why], why), file=sys.stderr)
        return 0
    if len(argv) == 4 and argv[1] == "verify":
        problems = verify(argv[2], load_report(argv[3]))
        for line in problems:
            print(line)
        return 1 if problems else 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
