#!/usr/bin/env python3
"""The players' maps of the repository (Community-Maps/): which maps of a collection are taken in, the repairs of those that are not, and the list that says what is there.

usage: community_maps.py repair SRC_DIR SWEEP_REPORT REPAIRED_DIR   write into REPAIRED_DIR the repaired file of every map that is not taken in as it is and that tools/repair_maps.py can repair, and REPAIRED_DIR/repairs.json
       community_maps.py build SRC_DIR SWEEP_REPORT DEST_DIR [REPAIRED_DIR REPAIRED_SWEEP_REPORT]   copy the maps that pass (and the repaired ones that pass) into DEST_DIR and write DEST_DIR/maps.json
       community_maps.py discards SRC_DIR SWEEP_REPORT [REPAIRED_DIR REPAIRED_SWEEP_REPORT]   the maps that are not taken in, one line each with the reason (an original map says so), then the counts
       community_maps.py verify DIR SWEEP_REPORT               DIR holds only maps that pass in a sweep of DIR itself, and maps.json lists exactly them
exit status: 0 all is well; 1 verify found problems (one line each) or a file could not be read; 2 usage.

SWEEP_REPORT is the --out file of `map_sweep FOLDER --out REPORT` (tools/map_sweep.cpp) over the same folder: it says for every file whether the engine loads it, what its name
is worth in the network protocol, what was played on it (the roster of all four teams and, when the map has a start marker or a hill for both the first and the last team, those
two teams alone: green and black) and whether the plays were deterministic, and what is odd about the file. A map passes when
  - the engine loads it, the rosters above were played on it (a report without a play, as `--skip-run` makes it, passes nothing) and every one of them ended without a crash, a hang
    or an error, with one state hash in three plays;
  - every tile on it is a name of its own dictionary (a tile outside it is what the original draws as heap garbage, so there is nothing to copy);
  - the file holds no program (a map that carries one is not a map);
  - its name is one that the network protocol accepts (a room could not use it otherwise).
The maps of the original game are not taken: the six in Original-Ants/Maps are known by their names, and a file that has the bytes of one of them under another name is the same
map (the tool reads Original-Ants/Maps beside it; without that folder only the names tell).

A map that does not pass may be repaired (tools/repair_maps.py): the repaired file is new bytes under the same name, it is swept on its own (REPAIRED_SWEEP_REPORT is the report of a
sweep of REPAIRED_DIR) and passes by the same rule or stays out. `build` writes the sentence that says what was changed into maps.json as "repaired".
"""
import json
import os
import re
import sys

import repair_maps

ORIGINAL_NAMES = ("TINY", "SMALL", "MEDIUM", "GAUNTLET", "TREASURE", "ISLANDS")
ORIGINALS_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir, "Original-Ants", "Maps"))
PROGRAM = re.compile(r"this program (cannot|requires)|\.(exe|dll|sys|bat)\b|kernel32", re.I)
PRINTABLE_RUN = re.compile(rb"[\x20-\x7e]{4,}")
REASONS = ("does not load", "not playable", "play fails", "tile outside the dictionary", "program code inside", "name the protocol refuses")
ORIGINAL = "original map"       # (not one of REASONS: an original map belongs in Original-Ants/Maps)
REPAIRED = "repaired"           # (nor this: a map that was repaired and then passes)
REPAIRS_FILE = "repairs.json"   # in the folder of repaired files: {"format": 1, "repairs": {file name: the sentence}}
REPAIRED_FIELD_MAX = 300
ALL_FOUR_TEAMS = 15             # the rosters of tools/map_sweep.cpp: bit t is team t
GREEN_AND_BLACK = 9


def fnv1a64(data):
    """The identity of a map file in the network protocol (ants::net::hash_file)."""
    h = 14695981039346656037
    for byte in data:
        h = ((h ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % h


def teams_of(entry):
    """Which of the four teams have a start marker or a hill (what tools/map_sweep.cpp asks as MapInfo::has_team)."""
    return [bool(marks or hills) for marks, hills in zip(entry["start_markers"], entry["hill_cells"])]


def rosters_swept(entry):
    """The rosters that the sweep plays on a map that loads: all four teams, and green and black alone when the map has both."""
    teams = teams_of(entry)
    return (ALL_FOUR_TEAMS, GREEN_AND_BLACK) if len(teams) == 4 and teams[0] and teams[3] else (ALL_FOUR_TEAMS,)


def reason(entry, data):
    """None when the map passes; else the first of REASONS that applies. `entry` is one object of the sweep report's "maps", `data` the file's bytes."""
    if not entry["loads"]:
        return REASONS[0]
    if not entry["playable"]:
        return REASONS[1]
    if not set(rosters_swept(entry)) <= {r["roster"] for r in entry["runs"]} or any(r["status"] != "ok" or not r["deterministic"] for r in entry["runs"]):
        return REASONS[2]
    if any(p["kind"] == "tile_outside_dictionary" for p in entry["problems"]):
        return REASONS[3]
    if any(PROGRAM.search(m.group().decode("ascii")) for m in PRINTABLE_RUN.finditer(data)):
        return REASONS[4]
    if not entry["name_ok"]:
        return REASONS[5]
    return None


def load_originals(folder=None):
    """{bytes: file name} of the original game's maps in Original-Ants/Maps beside this tool; empty when the folder is not there."""
    folder = ORIGINALS_DIR if folder is None else folder
    try:
        names = sorted(n for n in os.listdir(folder) if n.lower().endswith(".lvl"))
    except OSError:
        return {}
    return {read(folder, n): n for n in names}


def original_of(name, data, originals):
    """None for a map of the players; else what makes it an original: its name, or the bytes of one of the files in `originals` ({bytes: name})."""
    if os.path.splitext(name)[0].upper() in ORIGINAL_NAMES:
        return "its name"
    if data in originals:
        return "the bytes of %s" % originals[data]
    return None


def is_original(name, data=b"", originals=None):
    return original_of(name, data, originals or {}) is not None


def header(data):
    """(minutes, description) of a level file: the header is version, mode, minutes (u16) and a 30 byte text, as the setup screen's Map Info shows them."""
    minutes = int.from_bytes(data[8:10], "little")
    text = data[10:40].split(b"\0")[0].decode("latin-1")
    return minutes, "".join(c for c in text if c.isprintable()).strip()


def manifest_entry(entry, data):
    minutes, description = header(data)
    # players: the teams that have a start marker or a hill (a map with neither is played by the sweep with all four teams, so it counts four)
    teams = sum(teams_of(entry)) or 4
    return {
        "file": entry["name"], "size": len(data), "hash": fnv1a64(data), "width": entry["width"], "height": entry["height"], "minutes": minutes,
        "players": teams, "description": description,
    }


def load_report(path):
    with open(path, encoding="utf-8") as f:
        maps = json.load(f)["maps"]
    if not isinstance(maps, list) or not all(isinstance(m, dict) and isinstance(m.get("name"), str) for m in maps):
        raise ValueError("not a list of maps")
    return maps


def read(folder, name):
    with open(os.path.join(folder, name), "rb") as f:
        return f.read()


def sentence(notes):
    """The sentence of maps.json's "repaired": what tools/repair_maps.py did, one capital letter and a full stop."""
    text = "; ".join(notes)
    return text[:1].upper() + text[1:] + "."


def repair(src, report, dest, originals=None):
    """Writes into `dest` the repaired file of every map of the report that is not an original, does not pass as it is and has a repair; writes dest/repairs.json; returns the count."""
    originals = load_originals() if originals is None else originals
    os.makedirs(dest, exist_ok=True)
    done = {}
    for entry in report:
        data = read(src, entry["name"])
        if original_of(entry["name"], data, originals) is not None or reason(entry, data) is None:
            continue
        fixed, notes = repair_maps.repair(data)
        if fixed is None:
            continue
        with open(os.path.join(dest, entry["name"]), "wb") as f:
            f.write(fixed)
        done[entry["name"]] = sentence(notes)
    with open(os.path.join(dest, REPAIRS_FILE), "w", encoding="utf-8", newline="\n") as f:
        json.dump({"format": 1, "repairs": dict(sorted(done.items(), key=lambda kv: kv[0].encode("latin-1")))}, f, indent=1, ensure_ascii=True)
        f.write("\n")
    return len(done)


def read_repairs(folder):
    """{file name: sentence} of a folder of repaired files."""
    with open(os.path.join(folder, REPAIRS_FILE), encoding="utf-8") as f:
        repairs = json.load(f)["repairs"]
    if not isinstance(repairs, dict) or not all(isinstance(k, str) and isinstance(v, str) for k, v in repairs.items()):
        raise ValueError("%s is not a list of repairs" % REPAIRS_FILE)
    return repairs


def repaired_passes(name, repaired):
    """(the bytes of the repaired file, its sentence) when the map `name` has a repaired file that passes in the sweep of its folder, else None. `repaired` is (folder, report) or None."""
    if repaired is None:
        return None
    folder, report = repaired
    repairs = read_repairs(folder)
    entry = next((e for e in report if e["name"] == name), None)
    if name not in repairs or entry is None:
        return None
    data = read(folder, name)
    if entry.get("hash") != fnv1a64(data):
        raise ValueError("%s: the sweep report of the repaired files is not of this file (the hash differs)" % name)
    return (data, repairs[name]) if reason(entry, data) is None else None


def taken(src, report, originals, repaired=None):
    """[(sweep object, bytes, sentence or None)] of the maps that are taken in: the ones that pass as they are, else their repaired file when that passes. Never an original."""
    out = []
    for entry in report:
        data = read(src, entry["name"])
        if original_of(entry["name"], data, originals) is not None:
            continue
        if reason(entry, data) is None:
            out.append((entry, data, None))
            continue
        got = repaired_passes(entry["name"], repaired)
        if got:
            out.append(([e for e in repaired[1] if e["name"] == entry["name"]][0], got[0], got[1]))
    return out


def build(src, report, dest, originals=None, repaired=None):
    """Copies the maps that are taken in into `dest` and writes dest/maps.json; returns their number. `repaired` is (folder of repaired files, report of their sweep) or None."""
    originals = load_originals() if originals is None else originals
    kept = taken(src, report, originals, repaired)
    os.makedirs(dest, exist_ok=True)
    rows = []
    for entry, data, note in kept:
        with open(os.path.join(dest, entry["name"]), "wb") as f:
            f.write(data)
        row = manifest_entry(entry, data)
        if note:
            row["repaired"] = note
        rows.append(row)
    rows.sort(key=lambda r: r["file"].encode("latin-1"))
    with open(os.path.join(dest, "maps.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump({"format": 1, "maps": rows}, f, indent=1, ensure_ascii=True)
        f.write("\n")
    return len(rows)


def discards(src, report, originals=None, repaired=None):
    """Prints one line for every map that is not taken in; returns the counts: one for each of REASONS, one for the originals and, when `repaired` is given, one for the repaired maps."""
    originals = load_originals() if originals is None else originals
    counts = {r: 0 for r in REASONS + (ORIGINAL,)}
    if repaired is not None:
        counts[REPAIRED] = 0
    for entry in report:
        data = read(src, entry["name"])
        which = original_of(entry["name"], data, originals)
        why = ORIGINAL if which else reason(entry, data)
        if why is None:
            continue
        if not which and repaired_passes(entry["name"], repaired):
            counts[REPAIRED] += 1
            continue
        counts[why] += 1
        print("%s\t%s" % ("%s (%s)" % (why, which) if which else why, entry["name"]))
    return counts


def shown(names):
    return ", ".join(names[:5]) + (", ... (%d in all)" % len(names) if len(names) > 5 else "") if names else "none"


def read_list(folder):
    """(the rows of the folder's maps.json, None), or (None, the problem with the file)."""
    try:
        with open(os.path.join(folder, "maps.json"), encoding="utf-8") as f:
            rows = json.load(f)["maps"]
        if not isinstance(rows, list) or not all(isinstance(r, dict) and isinstance(r.get("file"), str) for r in rows):
            raise ValueError("not a list of maps")
    except FileNotFoundError:
        return None, "maps.json is missing"
    except (OSError, ValueError, KeyError, TypeError) as err:
        return None, "maps.json cannot be read (%s)" % (err.__class__.__name__ + ": " + str(err))
    return rows, None


def verify(folder, report, originals=None):
    """Problems found (a list of texts, empty when all is well)."""
    originals = load_originals() if originals is None else originals
    problems = []
    names = sorted(n for n in os.listdir(folder) if n.lower().endswith(".lvl"))
    data_of = {n: read(folder, n) for n in names}
    in_report = [e["name"] for e in report]
    if sorted(in_report) != names:
        problems.append("the sweep report and the folder name different files (only in the report: %s; only in the folder: %s)"
                        % (shown(sorted(set(in_report) - set(names))), shown(sorted(set(names) - set(in_report)))))
    for entry in report:
        if entry["name"] not in data_of:
            continue
        data = data_of[entry["name"]]
        why = reason(entry, data)
        if why:
            problems.append("%s: %s" % (entry["name"], why))
        if entry.get("hash") != fnv1a64(data):
            problems.append("%s: the hash in the sweep report is not the file's (the report is of another file)" % entry["name"])
    for name in names:
        if original_of(name, data_of[name], originals):
            problems.append("%s: an original map belongs in Original-Ants/Maps" % name)
    listed, trouble = read_list(folder)
    if trouble:
        problems.append(trouble)
        return problems
    files = [r["file"] for r in listed]
    if files != sorted(files, key=lambda n: n.encode("latin-1")) or sorted(files) != names:
        problems.append("maps.json does not list exactly the folder's maps in order")
    by_name = {e["name"]: e for e in report}
    for row in listed:
        said = row.get("repaired")
        if "repaired" in row and not (isinstance(said, str) and 0 < len(said) <= REPAIRED_FIELD_MAX and said.isascii() and said.isprintable()):
            problems.append("%s: its \"repaired\" in maps.json is not a sentence of printable ASCII (1 to %d characters)" % (row["file"], REPAIRED_FIELD_MAX))
        plain = {k: v for k, v in row.items() if k != "repaired"}
        if row["file"] in by_name and row["file"] in data_of and plain != manifest_entry(by_name[row["file"]], data_of[row["file"]]):
            problems.append("%s: its line in maps.json is not what the file and the sweep say" % row["file"])
    return problems


USAGE_ARGS = {"repair": (5,), "build": (5, 7), "discards": (4, 6), "verify": (4,)}


def main(argv):
    if len(argv) < 2 or len(argv) not in USAGE_ARGS.get(argv[1], ()):
        print(__doc__)
        return 2
    command = argv[1]
    optional = {"build": 5, "discards": 4}.get(command)           # (where the optional REPAIRED_DIR REPAIRED_SWEEP_REPORT begin)
    path = argv[3]
    try:
        report = load_report(path)
        repaired = None
        if optional is not None and len(argv) == optional + 2:
            path = argv[optional + 1]
            repaired = (argv[optional], load_report(path))
    except (OSError, ValueError, KeyError, TypeError) as err:
        print("%s: the sweep report cannot be read (%s: %s)" % (path, err.__class__.__name__, err), file=sys.stderr)
        return 1
    originals = load_originals()
    if not originals:
        print("note: %s is not there, so only the names tell an original map" % ORIGINALS_DIR, file=sys.stderr)
    try:
        if command == "repair":
            print("%d maps repaired" % repair(argv[2], report, argv[4], originals))
            return 0
        if command == "build":
            print("%d maps taken in" % build(argv[2], report, argv[4], originals, repaired))
            return 0
        if command == "discards":
            counts = discards(argv[2], report, originals, repaired)
            for why in REASONS + ((REPAIRED,) if repaired else ()) + (ORIGINAL,):
                print("%d\t%s" % (counts[why], why), file=sys.stderr)
            return 0
    except (OSError, ValueError, KeyError, TypeError) as err:
        print("%s: %s: %s" % (command, err.__class__.__name__, err), file=sys.stderr)
        return 1
    problems = verify(argv[2], report, originals)
    for line in problems:
        print(line)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
