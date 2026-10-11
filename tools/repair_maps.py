#!/usr/bin/env python3
"""Repairs of the players' maps that the engine cannot take as they are (`tools/community_maps.py repair` runs them; Community-Maps/README.md says which maps were repaired and how).

A repair is a deterministic function of the file's bytes and changes as little as it can: every record that the file holds completely and makes sense keeps its meaning. The layout
below is the one the engine's loader reads (src/ants_assets/lvl_parser.cpp): header, tile dictionary, grid size, two layers of cells, block 1 (start markers and plants), block 2
(food objects), block 3 (two words), block 4 (waypoints), the egg stock word.

  undo_text_damage   a file that went through a text transfer: a 0x0D byte lost the 0x00 behind it (the word 13), a 0x0D 0x0A pair stands for a 0x0A byte (the word 10 got a 0x0D),
                     and a 0x0D was lost in front of a 0x0A (the pair of the words 13 and 10 became one byte). The records behind the first change are shifted, so the loader reads garbage
                     or runs into the end of the file. The inverse is searched in the whole file, item by item along the layout (a dynamic programme that keeps the two cheapest
                     readings of each state), and is taken only when the cheapest reading ends exactly at the end of the file, has at most MAX_COST cells of a kind that no map has
                     (flags and properties words of shifted bytes), and the next best reading has more.
  fix_start_markers  a start marker (a block 1 record whose tile is a team's) with a row or column outside the grid: when the low bytes of both coordinates are inside the grid the
                     marker goes there (the high byte is garbage: 21512 is 0x5408 and the sister maps have row 8), else the marker is dropped.
  fix_tiles          a layer 1 cell whose tile is outside the dictionary: the tile that most of its eight neighbours have (else the layer's commonest tile), flags and properties kept (the
                     engine reads them verbatim); a layer 2 cell: the empty cell; block 3's tile outside the dictionary: the empty tile 0x7FFE, which 416 of the 586 maps have there.
                     Not done when more than 5 % of the cells are such: the layer is garbage, not a few cells.

`repair(data)` applies them in this order and returns (new bytes, [what was done, one short sentence each]) or (None, []) when none applies or the result is not a whole level.
Not repaired: a file whose bytes were overwritten (zeros become other values), a file that ends inside a block (what follows is lost, not damaged), and what is no level.
"""
import struct
import sys
from collections import Counter

VERSION = 8
EMPTY = 0x7FFE                  # the empty tile of layer 2 and of block 3 (lvl_parser.hpp LVL_EMPTY_TILE)
HEADER_SIZE = 40                # version, mode, minutes and the 30 byte description
MAX_SIDE = 100                  # the largest grid the engine hosts (LVL_MAX_GRID_SIDE)
MAX_COST = 2                    # undo_text_damage: the most implausible cells that the reading it takes may have,
MARGIN = 1                      # and how many more the next best reading must have
TILE_SHARE = 0.05               # fix_tiles: the share of cells that may be repaired
TEAM_WORDS = (("GSTART", "GREENHILL", "ghill", "gstart"), ("RSTART", "REDHILL", "rhill", "rstart"),
              ("USTART", "BLUEHILL", "blhill", "ustart"), ("BSTART", "BLACKHILL", "bkhill", "bstart"))     # (the loader's substrings, team 0 .. 3)


def u16(data, at):
    return data[at] | data[at + 1] << 8


def put16(buf, at, value):
    buf[at] = value & 0xFF
    buf[at + 1] = value >> 8 & 0xFF


class Layout:
    """Where the parts of a version 8 level are. `end` is None when the file holds the whole of it, else the name of the block it ends in."""

    def __init__(self):
        self.tiles = 0          # the number of dictionary names (the file's word + 1)
        self.names = []
        self.rows = self.cols = 0
        self.layer1 = self.layer2 = 0
        self.b1 = self.b1_count = 0      # offset of the count word, the count
        self.b2 = self.b2_count = 0
        self.objects = []                # (offset, stages) of the complete block 2 objects
        self.b3 = 0
        self.b4 = self.b4_count = 0
        self.final = 0                   # offset of the egg stock word, 0 when the file ends before it
        self.end = None
        self.ended_early = False         # block 2 ended on an object without stages


def team_of(name):
    for team, words in enumerate(TEAM_WORDS):
        if any(w in name for w in words):
            return team
    return None


def layout(data):
    """The Layout of `data`; None when it is no version 8 level with a grid and a dictionary. A file that ends inside a block gives a Layout whose `end` names the block."""
    n = len(data)
    if n < HEADER_SIZE + 2 or int.from_bytes(data[:4], "little") != VERSION:
        return None
    lay = Layout()
    lay.tiles = u16(data, HEADER_SIZE) + 1
    at = HEADER_SIZE + 2
    if at + 11 * lay.tiles + 8 > n:
        return None
    lay.names = [data[at + 11 * i:at + 11 * i + 11].split(b"\0")[0].decode("latin-1") for i in range(lay.tiles)]
    at += 11 * lay.tiles
    lay.rows = int.from_bytes(data[at:at + 4], "little") & 0xFFFF
    lay.cols = int.from_bytes(data[at + 4:at + 8], "little") & 0xFFFF
    at += 8
    if not (0 < lay.rows <= MAX_SIDE and 0 < lay.cols <= MAX_SIDE) or at + 12 * lay.rows * lay.cols > n:
        return None
    lay.layer1 = at
    lay.layer2 = at + 6 * lay.rows * lay.cols
    at = lay.b1 = lay.layer2 + 6 * lay.rows * lay.cols
    if at + 2 > n:
        lay.end = "block 1"
        return lay
    lay.b1_count = u16(data, at)
    at += 2 + 6 * lay.b1_count
    if at > n:
        lay.end = "block 1"
        return lay
    lay.b2 = at
    if at + 2 > n:
        lay.end = "block 2"
        return lay
    lay.b2_count = u16(data, at)
    at += 2
    for _ in range(lay.b2_count):
        if at + 10 > n:
            lay.end = "block 2"
            return lay
        stages = u16(data, at + 8)
        if stages == 0:
            lay.ended_early = True
            at += 10
            break
        if at + 10 + 4 * stages > n:
            lay.end = "block 2"
            return lay
        lay.objects.append((at, stages))
        at += 10 + 4 * stages
    lay.b3 = at
    if at + 4 > n:
        lay.end = "block 3"
        return lay
    at += 4
    lay.b4 = at
    if at + 2 > n:
        return lay
    lay.b4_count = u16(data, at)
    at += 2
    for _ in range(lay.b4_count):
        if at + 8 > n:
            return lay
        at += 8
        if int.from_bytes(data[at - 4:at], "little"):
            at += 44
    if at + 2 <= n:
        lay.final = at
    return lay


# --------------------------------------------------------------------------------------------------------------------------------------------------------------------
# undo_text_damage
# --------------------------------------------------------------------------------------------------------------------------------------------------------------------

def readings(o, pos, pend, k):
    """The ways the next k bytes of the original file can be read from the damaged stream `o` at `pos` (`pend`: a byte that the damage took out and that is owed first, else None).
    Yields (bytes, pos, pend, decisions); a decision is (position, kind) with kind "nul" (a 0x00 lost behind a 0x0D), "lf" (0x0D 0x0A where the original has 0x0A) or "cr" (a 0x0D
    lost in front of a 0x0A)."""
    if k == 0:
        yield b"", pos, pend, ()
        return
    if pend is not None:
        for rest, p2, pe2, d in readings(o, pos, None, k - 1):
            yield bytes((pend,)) + rest, p2, pe2, d
        return
    if pos >= len(o):
        return
    b = o[pos]
    for rest, p2, pe2, d in readings(o, pos + 1, None, k - 1):
        yield bytes((b,)) + rest, p2, pe2, d
    if b == 0x0D:
        for rest, p2, pe2, d in readings(o, pos + 1, 0, k - 1):                     # the 0x00 behind it was lost
            yield b"\r" + rest, p2, pe2, ((pos, "nul"),) + d
        if pos + 1 < len(o) and o[pos + 1] == 0x0A:
            for rest, p2, pe2, d in readings(o, pos + 2, None, k - 1):              # 0x0D 0x0A is a 0x0A
                yield b"\n" + rest, p2, pe2, ((pos, "lf"),) + d
    elif b == 0x0A:
        for rest, p2, pe2, d in readings(o, pos + 1, 0x0A, k - 1):                  # the 0x0D in front of it was lost
            yield b"\r" + rest, p2, pe2, ((pos, "cr"),) + d


def cell_cost(layer, cell, rows, cols):
    """0 for a cell the way the maps of the editor have them, 1 for one that is shifted garbage (flags and properties words that no cell has)."""
    tile, flags, props = struct.unpack("<HHH", cell)
    if layer == 1:
        return 0 if (flags <= 0x1FF or flags == 0xCDCD) and props in (0, 0xCDCD) else 1
    if tile == EMPTY:
        return 0 if flags <= 0x100 else 1
    return 0 if flags <= 0x1FF and (props & 0xFF) < rows and (props >> 8) < cols else 1


def readings_of_file(data):
    """The cheapest complete readings of `data` as (cost, decisions, repaired bytes), at most two, cheapest first; [] when no reading fits the layout."""
    n = len(data)
    if n < HEADER_SIZE + 2 + 11 or int.from_bytes(data[:4], "little") != VERSION:
        return []
    tiles = u16(data, HEADER_SIZE) + 1
    head = HEADER_SIZE + 2 + 11 * tiles + 8
    if head > n:
        return []
    rows = int.from_bytes(data[head - 8:head - 4], "little")
    cols = int.from_bytes(data[head - 4:head], "little")
    if not (0 < rows <= MAX_SIDE and 0 < cols <= MAX_SIDE):
        return []
    top = tiles - 1
    cells = 2 * rows * cols
    # the search runs level by level over the items of the layout: a state is (what comes next, position in the damaged file, byte owed); it keeps its two cheapest readings
    levels = [{}]
    levels[0][(("c", 0), head, None)] = [(0, None, 0, b"", ())]
    final = []
    level = 0
    while levels[level]:
        nxt = {}

        def go(key, ms, cost, rank, chunk, decs, p2, pe2):
            slot = nxt.setdefault((ms, p2, pe2), [])
            slot.append((cost, key, rank, chunk, decs))
            slot.sort(key=lambda e: e[0])
            del slot[2:]

        for key, entries in levels[level].items():
            ms, pos, pend = key
            for rank, entry in enumerate(entries):
                base = entry[0]
                stage = ms[0]
                if stage == "c":
                    i = ms[1]
                    layer = 1 if i < cells // 2 else 2
                    for b, p2, pe2, d in readings(data, pos, pend, 6):
                        tile = u16(b, 0)
                        if not (tile <= top or tile == EMPTY):
                            continue
                        go(key, ("c", i + 1) if i + 1 < cells else ("b1n",), base + cell_cost(layer, b, rows, cols), rank, b, d, p2, pe2)
                elif stage == "b1n":
                    for b, p2, pe2, d in readings(data, pos, pend, 2):
                        c = u16(b, 0)
                        go(key, ("b1", c) if c else ("b2n",), base, rank, b, d, p2, pe2)
                elif stage == "b1":
                    for b, p2, pe2, d in readings(data, pos, pend, 6):
                        tile, y, x = struct.unpack("<HHH", b)
                        if tile > top or y >= rows or x >= cols:
                            continue
                        go(key, ("b1", ms[1] - 1) if ms[1] > 1 else ("b2n",), base, rank, b, d, p2, pe2)
                elif stage == "b2n":
                    for b, p2, pe2, d in readings(data, pos, pend, 2):
                        c = u16(b, 0)
                        go(key, ("b2o", c) if c else ("b3",), base, rank, b, d, p2, pe2)
                elif stage == "b2o":
                    for b, p2, pe2, d in readings(data, pos, pend, 10):
                        y, x, _units, _points, stages = struct.unpack("<5H", b)
                        if y >= rows or x >= cols or not 0 < stages <= 64:
                            continue
                        go(key, ("b2s", ms[1], stages), base, rank, b, d, p2, pe2)
                elif stage == "b2s":
                    left, stages = ms[1], ms[2]
                    for b, p2, pe2, d in readings(data, pos, pend, 4):
                        tile = u16(b, 2)
                        if not (tile <= top or tile == EMPTY):
                            continue
                        following = ("b2s", left, stages - 1) if stages > 1 else ("b2o", left - 1) if left > 1 else ("b3",)
                        go(key, following, base, rank, b, d, p2, pe2)
                elif stage == "b3":
                    for b, p2, pe2, d in readings(data, pos, pend, 4):
                        go(key, ("b4n",), base, rank, b, d, p2, pe2)
                elif stage == "b4n":
                    for b, p2, pe2, d in readings(data, pos, pend, 2):
                        c = u16(b, 0)
                        go(key, ("b4", c) if c else ("fin",), base, rank, b, d, p2, pe2)
                elif stage == "b4":
                    following = ("b4", ms[1] - 1) if ms[1] > 1 else ("fin",)
                    for b, p2, pe2, d in readings(data, pos, pend, 8):
                        y, x, flag = struct.unpack("<HHI", b)
                        if y >= rows or x >= cols:
                            continue
                        if flag == 0:
                            go(key, following, base, rank, b, d, p2, pe2)
                        else:
                            for b2, p3, pe3, d2 in readings(data, p2, pe2, 44):
                                go(key, following, base, rank, b + b2, d + d2, p3, pe3)
                else:                                   # "fin": the egg stock word, and the file ends there
                    for b, p2, pe2, d in readings(data, pos, pend, 2):
                        if p2 == n and pe2 is None:
                            final.append((base, key, rank, b, d, level))
        levels.append(nxt)
        level += 1
    if not final:
        return []
    final.sort(key=lambda e: e[0])
    found = []
    for cost, key, rank, chunk, decs, lev in final[:2]:
        parts = [chunk]
        all_decs = list(decs)
        while True:
            entry = levels[lev][key][rank]
            if entry[1] is None:
                break
            parts.append(entry[3])
            all_decs.extend(entry[4])
            key, rank = entry[1], entry[2]
            lev -= 1
        found.append((cost, sorted(all_decs), data[:head] + b"".join(reversed(parts))))
    return found


def undo_text_damage(data):
    """(repaired bytes, 0x00 restored, 0x0D 0x0A made 0x0A, 0x0D restored) or None when the file is not damaged this way or the reading is not clear."""
    found = readings_of_file(data)
    if not found or found[0][0] > MAX_COST or len(found) > 1 and found[1][0] - found[0][0] < MARGIN:
        return None
    _cost, decs, out = found[0]
    if not decs:
        return None
    return (out,) + tuple(sum(1 for _, k in decs if k == kind) for kind in ("nul", "lf", "cr"))


# --------------------------------------------------------------------------------------------------------------------------------------------------------------------
# the other repairs
# --------------------------------------------------------------------------------------------------------------------------------------------------------------------

def fix_start_markers(data):
    """(repaired bytes, markers moved, markers dropped) or None."""
    lay = layout(data)
    if lay is None or lay.end == "block 1":
        return None
    moved = dropped = 0
    records = bytearray()
    for i in range(lay.b1_count):
        at = lay.b1 + 2 + 6 * i
        tile, y, x = struct.unpack_from("<HHH", data, at)
        name = lay.names[tile] if tile < lay.tiles else ""
        if team_of(name) is None or (y < lay.rows and x < lay.cols):
            records += data[at:at + 6]
            continue
        y2, x2 = (v if v < dim else v & 0xFF for v, dim in ((y, lay.rows), (x, lay.cols)))
        if y2 < lay.rows and x2 < lay.cols:
            records += struct.pack("<HHH", tile, y2, x2)
            moved += 1
        else:
            dropped += 1
    if not moved and not dropped:
        return None
    count = lay.b1_count - dropped
    return data[:lay.b1] + struct.pack("<H", count) + bytes(records) + data[lay.b1 + 2 + 6 * lay.b1_count:], moved, dropped


def fix_tiles(data):
    """(repaired bytes, layer 1 cells, layer 2 cells, block 3 words changed) or None."""
    lay = layout(data)
    if lay is None or lay.end is not None:
        return None
    top = lay.tiles
    rows, cols = lay.rows, lay.cols
    out = bytearray(data)

    def tile_at(layer, i):
        return u16(data, layer + 6 * i)

    bad1 = [i for i in range(rows * cols) if tile_at(lay.layer1, i) >= top and tile_at(lay.layer1, i) != EMPTY]
    bad2 = [i for i in range(rows * cols) if tile_at(lay.layer2, i) >= top and tile_at(lay.layer2, i) != EMPTY]
    ambient = u16(data, lay.b3 + 2)
    bad3 = ambient >= top and ambient != EMPTY
    if not bad1 and not bad2 and not bad3:
        return None
    if len(bad1) + len(bad2) > TILE_SHARE * rows * cols:
        return None
    good = [tile_at(lay.layer1, i) for i in range(rows * cols) if tile_at(lay.layer1, i) < top]
    common = min(Counter(good).items(), key=lambda kv: (-kv[1], kv[0]))[0] if good else 0
    bad_set = set(bad1)
    for i in bad1:
        y, x = divmod(i, cols)
        near = Counter(tile_at(lay.layer1, j * cols + k) for j in range(max(0, y - 1), min(rows, y + 2)) for k in range(max(0, x - 1), min(cols, x + 2))
                       if (j, k) != (y, x) and j * cols + k not in bad_set and tile_at(lay.layer1, j * cols + k) < top)
        pick = min(near.items(), key=lambda kv: (-kv[1], kv[0]))[0] if near else common
        put16(out, lay.layer1 + 6 * i, pick)
    for i in bad2:
        out[lay.layer2 + 6 * i:lay.layer2 + 6 * i + 6] = struct.pack("<HHH", EMPTY, 0, 0)
    if bad3:
        put16(out, lay.b3 + 2, EMPTY)
    return bytes(out), len(bad1), len(bad2), int(bad3)


def plural(n, one, many=None):
    return "%d %s" % (n, one if n == 1 else many or one + "s")


def repair(data):
    """(new bytes, [sentences]) when the repairs change the file into one the layout accepts as a whole, else (None, [])."""
    notes = []
    cur = data
    got = undo_text_damage(cur)
    if got:
        cur, nuls, lfs, crs = got
        parts = []
        if nuls:
            parts.append("%s put back behind a 0x0D byte" % plural(nuls, "zero byte"))
        if lfs:
            parts.append("%s made a single 0x0A byte again" % plural(lfs, "0x0D 0x0A pair"))
        if crs:
            parts.append("%s put back in front of a 0x0A byte" % plural(crs, "0x0D byte"))
        notes.append("text-transfer damage undone: " + ", ".join(parts))
    got = fix_start_markers(cur)
    if got:
        cur, moved, dropped = got
        if moved:
            notes.append("%s outside the grid put on the cell that the low bytes of its coordinates name" % plural(moved, "start marker"))
        if dropped:
            notes.append("%s outside the grid dropped" % plural(dropped, "start marker"))
    got = fix_tiles(cur)
    if got:
        cur, c1, c2, c3 = got
        parts = []
        if c1:
            parts.append("%s of layer 1 given the tile that most neighbouring cells have" % plural(c1, "cell"))
        if c2:
            parts.append("%s of layer 2 emptied" % plural(c2, "cell"))
        if c3:
            parts.append("the default ant tile of block 3 set to the empty tile")
        notes.append("tiles outside the dictionary: " + ", ".join(parts))
    if cur == data:
        return None, []
    lay = layout(cur)
    if lay is None or lay.end is not None:
        return None, []
    return cur, notes


if __name__ == "__main__":
    for path in sys.argv[1:]:
        with open(path, "rb") as f:
            fixed, said = repair(f.read())
        print("%s: %s" % (path, "; ".join(said) if fixed else "nothing to repair"))
