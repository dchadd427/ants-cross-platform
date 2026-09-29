#!/usr/bin/env python3
"""
tools/extract_movement_tables.py

Extracts the original 1998 ant-locomotion ground truth from the game files and
writes it as constexpr C++ data for the simulation:

    Original-Ants/Ants.exe   static uint16 animation-index tables, terrain-class
                             and tile-flag tables, path-cost and direction tables
    Original-Ants/ants.chd   Table 4 per-frame dx, dy, duration and event of every
                             animation those tables reference
        -> src/ants_sim/movement_tables_data.inc

Ant movement in the original is animation driven: a walking ant moves by the
current walk frame's (dx, dy) when that frame's duration expires. Which Table-4
animation is used for each ant type / terrain class / direction comes from the
static tables read here.

Usage (paths are resolved relative to the repository root, so the script can be
run from any directory):

    python3 tools/extract_movement_tables.py           # (re)write the .inc file
    python3 tools/extract_movement_tables.py --check   # exit 1 if the committed file is stale
    python3 tools/extract_movement_tables.py --stdout  # print the generated file

Requirements: python3 and pefile (`pip install pefile`).

All addresses are Ants.exe virtual addresses (image base 0x01000000); they were
reverse engineered with Capstone. Before extracting anything the script checks
the machine code that consumes each table ("code anchors") and the name of every
referenced CHD animation, so a different build of the game fails loudly instead
of producing wrong data. The output is deterministic.
"""

import argparse
import hashlib
import struct
import sys
from pathlib import Path

try:
    import pefile
except ImportError:  # pragma: no cover - environment problem, not a code path
    sys.stderr.write("error: the 'pefile' module is required (pip install pefile)\n")
    sys.exit(2)

REPO_ROOT = Path(__file__).resolve().parent.parent
EXE_REL = "Original-Ants/Ants.exe"
CHD_REL = "Original-Ants/ants.chd"
OUT_REL = "src/ants_sim/movement_tables_data.inc"

IMAGE_BASE = 0x01000000
NO_ANIM = 0x7FFE            # "no animation" marker in every static table
TILE_COUNT = 1344           # FUN_0100724c allocates 0x2a00 bytes = 1344 records of 8 bytes
EVENT_NONE_CHD = 1111111    # Table-4 "no event" value ...
EVENT_NONE = 11111          # ... stored as 11111 by the frame reader FUN_0102a977

TYPE_NAMES = ["worker", "bomber", "fire", "thief", "combat", "swimmer"]
TYPE_LETTERS = "gbftcs"     # CHD name letter per ant type (agwg*, abwg*, ...)
TERRAIN_NAMES = ["grass", "sand", "water", "mud", "dirt"]
TERRAIN_LETTERS = "gswmd"   # walk-animation terrain letter / first letter of the tile names
DIR_NAMES = ["N", "NE", "E", "SE", "S"]
DIR_DIGITS = "78923"        # CHD name digit per stored direction (N=7, NE=8, E=9, SE=2, S=3)

# ---------------------------------------------------------------------------------------------
# Static tables (colour-0 block; the colour 1..3 blocks hold only virtual per-colour ids >= 1344)
# ---------------------------------------------------------------------------------------------
WALK_VA = 0x1002FB8         # uint16 [colour 4][type 6][terrain 5][dir 8]
CARRY_WALK_VA = 0x1003738   # same shape, ant holding food
IDLE_VA = 0x1002CB8         # uint16 [colour 4][type 6][dir 8]
CARRY_IDLE_VA = 0x1002E38   # same shape, ant holding food
SWIM_VA = 0x1004838         # uint16 [colour 4][dir 8]  swimmer on water
DIVE_VA = 0x1004878         # uint16 [colour 4][dir 8]  swimmer entering water
CLIMB_VA = 0x10048C0        # uint16 [colour 4][dir 8]  swimmer leaving water
IDLE_WATER_VA = 0x10048B8   # uint16 [colour 4]         idle on water (no direction)
CANT_GO_VA = 0x1004548      # uint16 [colour 4][type 6] action 0xB "can't go"
CARRY_CANT_GO_VA = 0x1004578
# Action clips of FUN_0101ad02 (SetAction): every table has four colour blocks of the same shape; only the
# colour-0 block holds real CHD indices (dirs 5..7 of a row are virtual ids of the mirrored copies).
#   shape "type"      uint16 [type 6]
#   shape "type_dir"  uint16 [type 6][dir 8]
#   shape "dir"       uint16 [dir 8]  (cardinal-only clips: N, E, S stored, W = mirrored E, the rest empty)
#   shape "single"    uint16 [1]
# (key, C++ name, address, shape, CHD name template with {t} = ant type letter, {d} = direction digit, what)
ACTION_TABLES = [
    ("enter",        "kEnter",        0x1003EB8, "type",     "a{t}h0",       "action 2: enter the own hill"),
    ("carry_enter",  "kCarryEnter",   0x1003EE8, "type",     "h{t}h0",       "action 2, carrying food"),
    ("harvest",      "kHarvest",      0x1003F18, "type_dir", "a{t}gf{d}01",  "action 5: grab food"),
    ("attack",       "kAttack",       0x1004098, "type_dir", "a{t}at{d}01",  "action 0x12: melee attack"),
    ("hit",          "kHit",          0x1004218, "type_dir", "a{t}gh{d}01",  "action 0xE: get hit"),
    ("blown",        "kBlown",        0x1004398, "type_dir", "a{t}gb{d}01",  "action 0x13: blown away"),
    ("burn",         "kBurn",         0x1004518, "type",     "a{t}bu301",    "action 0xA: burn overlay"),
    ("hatch",        "kHatch",        0x10045A8, "type",     "a{t}hatch",    "action 0x14: newborn emerges"),
    ("stun",         "kStun",         0x10045D8, "type",     "a{t}sd301",    "action 3: stunned"),
    ("carry_stun",   "kCarryStun",    0x1004608, "type",     "h{t}sd301",    "action 3, carrying food"),
    ("ignite",       "kIgnite",       0x1004638, "dir",      "afsf{d}01",    "action 6: fire ant places a fire wall"),
    ("extinguish",   "kExtinguish",   0x1004678, "dir",      "afxf{d}01",    "action 7: fire ant puts a fire out"),
    ("bridge_build_water",    "kBridgeBuildWater",    0x10046B8, "dir", "asbbw{d}01", "action 0x10 on water"),
    ("bridge_demolish_water", "kBridgeDemolishWater", 0x10046F8, "dir", "asdbw{d}01", "action 0x11 on water"),
    ("bridge_build_land",     "kBridgeBuildLand",     0x1004738, "dir", "asbbl{d}01", "action 0x10 on land"),
    ("bridge_demolish_land",  "kBridgeDemolishLand",  0x1004778, "dir", "asdbl{d}01", "action 0x11 on land"),
    ("plant",        "kPlant",        0x10047B8, "dir",      "absb{d}01",    "action 8: bomber plants a bomb"),
    ("defuse",       "kDefuse",       0x10047F8, "dir",      "abdb{d}01",    "action 9: bomber defuses a bomb"),
    ("infiltrate",   "kInfiltrate",   0x1004900, "single",   "atcr501",      "action 0xD: thief raids an enemy hill"),
    ("getpow",       "kGetPow",       0x1004908, "single",   "getpow",       "action 4: power-up pickup"),
    ("drown",        "kDrown",        0x1004910, "type",     "a{t}dr301",    "action 0xF: drowning"),
]
# Rows whose CHD name deviates from the template (the original's own table holds the worker clip there)
ACTION_NAME_EXCEPTIONS = {("drown", 5): "agdr301"}
CARDINAL_DIRS = (0, 2, 4)   # N, E, S
PASSABLE_VA = 0x10049B8     # uint16 [8]  destination walkable by terrain class (CanEnter R1)
STEP_WEIGHT_VA = 0x10049C8  # uint32 [6]  path step-cost weight by terrain class
DIR_TABLE_VA = 0x1002B28    # int16 [3][3] direction by [drow+1][dcol+1]
NEIGHBOUR_VA = 0x1004950    # {int32 drow, int32 dcol} [8]  N, NE, E, SE, S, SW, W, NW
TERRAIN_PAIRS_VA = 0x1001360        # {uint16 tile id, uint16 class} pairs ...
TERRAIN_PAIRS_END_VA = 0x1001574    # ... up to (excluding) this address
# Tile flag id lists OR-ed into the per-tile record by FUN_0100724c: (bit, address, count, stride)
TILE_FLAG_LISTS = [
    (0x01, 0x1001838, 0xC0, 2),
    (0x02, 0x10019C0, 0x57, 2),
    (0x04, 0x1001AD8, 0x05, 2),
    (0x08, 0x1001578, 0x61, 2),
    (0x10, 0x1001AF8, 0x0E, 12),
    (0x20, 0x1001818, 0x0B, 2),
]
BUMP_PUSH_VA = 0x101CAE3            # push 0xdc; push tile; call FUN_010100e5 ("bump" effect)
BUMP_CALL_VA = 0x101CAEB
BUMP_EFFECT_FN = 0x10100E5
SWIMMER_WEIGHT_VA = 0x10208FB       # FUN_010208e8: cmp ax, 5; jne; push 0x15
BRIDGE_CMP_VAS = [0x1008B95, 0x1008B9B, 0x1008BA1, 0x1008BA7]   # FUN_01008b90: cmp ax, imm16


def le32(v):
    return struct.pack("<I", v)


# Code anchors: (address, expected bytes, what it proves). Each one is an instruction inside
# the function that consumes the table, and its operand is the table address used above.
CODE_ANCHORS = [
    # FUN_010175ad (startup animation-table build, colour 0, dirs 0..4)
    (0x101834C, b"\x66\x8b\x80" + le32(WALK_VA), "mov ax, [eax+walk]"),
    (0x101837C, b"\x66\x8b\x80" + le32(CARRY_WALK_VA), "mov ax, [eax+carry walk]"),
    (0x10183C1, b"\x0f\xb7\x87" + le32(IDLE_VA), "movzx eax, [edi+idle]"),
    (0x10183EA, b"\x0f\xb7\x87" + le32(CARRY_IDLE_VA), "movzx eax, [edi+carry idle]"),
    (0x10186F4, b"\x0f\xb7\x88" + le32(SWIM_VA), "movzx ecx, [eax+swim]"),
    (0x1018704, b"\x0f\xb7\x80" + le32(DIVE_VA), "movzx eax, [eax+dive]"),
    (0x1018726, b"\x0f\xb7\x80" + le32(CLIMB_VA), "movzx eax, [eax+climb]"),
    (0x101881D, b"\x0f\xb7\x05" + le32(IDLE_WATER_VA), "movzx eax, [idle water]"),
    (0x1018836, b"\x89\x83\xc0\x47\x00\x00", "mov [world+0x47c0], eax (idle-in-water slot)"),
    (0x1018536, b"\x0f\xb7\x87" + le32(CANT_GO_VA), "movzx eax, [edi+can't go]"),
    (0x1018549, b"\x0f\xb7\x87" + le32(CARRY_CANT_GO_VA), "movzx eax, [edi+carry can't go]"),
    # FUN_0101f780 CanEnter rule R1, FUN_010208e8 step weight, FUN_01017531 direction,
    # FUN_01019d28 path reconstruction
    (0x101F7C1, b"\x66\x39\x3c\x4d" + le32(PASSABLE_VA), "cmp [ecx*2+passable], di"),
    (0x1020906, b"\x8b\x04\xb5" + le32(STEP_WEIGHT_VA), "mov eax, [esi*4+weights]"),
    (0x1017555, b"\x66\x8b\x84\x41" + le32(DIR_TABLE_VA), "mov ax, [ecx+eax*2+dirtab]"),
    (0x1019D8D, b"\x66\x03\x04\xd5" + le32(NEIGHBOUR_VA), "add ax, [edx*8+neighbour.drow]"),
    (0x1019D95, b"\x66\x03\x0c\xd5" + le32(NEIGHBOUR_VA + 4), "add cx, [edx*8+neighbour.dcol]"),
    # FUN_0100724c (tile info build): 1344 records of 8 bytes, class word at +0, flags at +4
    (0x100724F, b"\xbf\x00\x2a\x00\x00", "mov edi, 0x2a00 (1344 * 8 bytes)"),
    (0x100726F, b"\xb8" + le32(TERRAIN_PAIRS_VA), "mov eax, terrain pairs"),
    (0x1007284, b"\x3d" + le32(TERRAIN_PAIRS_END_VA), "cmp eax, terrain pairs end"),
    (0x1007289, b"\x66\x89\x3c\xca", "mov [edx+ecx*8], di (class word)"),
    (0x100728F, b"\x6a\x02", "push 2 (stride of the uint16 lists, popped into edx)"),
    (0x1007291, b"\xb9" + le32(0x1001838) + b"\xbf\xc0\x00\x00\x00", "list 0x01: 0xc0 ids"),
    (0x10072A5, b"\x83\x4c\xc3\x04\x01", "or [rec+4], 1"),
    (0x10072B3, b"\x6a\x57\xb9" + le32(0x10019C0), "list 0x02: 0x57 ids"),
    (0x10072C4, b"\x09\x54\xc3\x04", "or [rec+4], edx (= 2)"),
    (0x10072D1, b"\x6a\x05\xb9" + le32(0x1001AD8), "list 0x04: 5 ids"),
    (0x10072E2, b"\x83\x4c\xc3\x04\x04", "or [rec+4], 4"),
    (0x10072F0, b"\x6a\x61\xb9" + le32(0x1001578), "list 0x08: 0x61 ids"),
    (0x1007301, b"\x83\x4c\xc3\x04\x08", "or [rec+4], 8"),
    (0x100730F, b"\x6a\x0e\xb9" + le32(0x1001AF8), "list 0x10: 0x0e ids"),
    (0x1007320, b"\x83\x4c\xc3\x04\x10\x83\xc1\x0c", "or [rec+4], 0x10; add ecx, 12 (stride 12)"),
    (0x100732F, b"\x6a\x0b\xb9" + le32(0x1001818), "list 0x20: 0x0b ids"),
    (0x1007340, b"\x83\x4c\xc3\x04\x20", "or [rec+4], 0x20"),
    # FUN_01008af7 terrain class of a map cell: bridge on layer 2 -> class 3
    (0x1008B04, b"\xe8" + struct.pack("<i", 0x1008B3D - (0x1008B04 + 5)), "call get layer-2 tile"),
    (0x1008B0C, b"\xe8" + struct.pack("<i", 0x1008B90 - (0x1008B0C + 5)), "call is-bridge"),
    (0x1008B15, b"\x66\xb8\x03\x00", "mov ax, 3 (bridge -> mud)"),
]


class Exe:
    """Minimal PE reader: maps virtual addresses to initialised section data."""

    def __init__(self, path):
        self.pe = pefile.PE(str(path), fast_load=True)
        if self.pe.OPTIONAL_HEADER.ImageBase != IMAGE_BASE:
            raise SystemExit(f"error: {EXE_REL}: unexpected image base "
                             f"{self.pe.OPTIONAL_HEADER.ImageBase:#x}")
        self.sections = []
        for s in self.pe.sections:
            lo = IMAGE_BASE + s.VirtualAddress
            self.sections.append((lo, lo + s.SizeOfRawData, s.get_data()))

    def read(self, va, n):
        for lo, hi, data in self.sections:
            if lo <= va and va + n <= hi:
                return data[va - lo: va - lo + n]
        raise SystemExit(f"error: {EXE_REL}: {n} bytes at {va:#x} are not initialised section data")

    def u16s(self, va, n):
        return list(struct.unpack(f"<{n}H", self.read(va, 2 * n)))

    def i16s(self, va, n):
        return list(struct.unpack(f"<{n}h", self.read(va, 2 * n)))

    def u32s(self, va, n):
        return list(struct.unpack(f"<{n}I", self.read(va, 4 * n)))

    def i32s(self, va, n):
        return list(struct.unpack(f"<{n}i", self.read(va, 4 * n)))


def wrap16(v):
    """16-bit store of a 32-bit value, read back signed (mov word ptr [..], ax)."""
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


class Chd:
    """Table-4 reader with the field order of FUN_0102be51 (sequence) / FUN_0102a977 (frame)."""

    def __init__(self, path):
        self.data = Path(path).read_bytes()
        version, _stamp, _t1, _t2, _t3, t4 = struct.unpack_from("<6I", self.data, 0)
        if version < 8:
            raise SystemExit(f"error: {CHD_REL}: unsupported version {version}")
        count = self.u32(t4)
        if count != TILE_COUNT:
            raise SystemExit(f"error: {CHD_REL}: Table 4 has {count} animations, expected {TILE_COUNT}")
        self.offsets = [self.u32(t4 + 4 + 4 * i) for i in range(count)]
        self._cache = {}

    def u32(self, off):
        return struct.unpack_from("<I", self.data, off)[0]

    def i32(self, off):
        return struct.unpack_from("<i", self.data, off)[0]

    def anim(self, index):
        """Returns (name, flags, frames) for a Table-4 animation.

        flags  = (flag1, flag2, flag3) sequence flags as raw uint32 (sound once / sound
                 duplicate / sound track; FUN_0102be51 turns them into sprite flag bits 3/4/5).
        frames = [(dx, dy, duration_ms, event, sound)] as raw int32 values.
        """
        if index in self._cache:
            return self._cache[index]
        p = self.offsets[index]
        name_len = self.u32(p)
        name = self.data[p + 4: p + 4 + name_len].split(b"\0")[0].decode("latin-1")
        p += 4 + name_len
        flag1, flag2, flag3, frame_count = struct.unpack_from("<4I", self.data, p)
        p += 16
        frames = []
        for _ in range(frame_count):
            dx, dy, dur = self.i32(p), self.i32(p + 4), self.i32(p + 8)
            event = self.i32(p + 28)      # after the L, T, R, B frame box
            sound = self.i32(p + 32)      # -1 = none
            parts = self.u32(p + 36)
            p += 40 + 12 * parts          # part = {int32 dx, int32 dy, uint32 sprite}
            frames.append((dx, dy, dur, event, sound))
        self._cache[index] = (name, (flag1, flag2, flag3), frames)
        return self._cache[index]

    def name(self, index):
        return self.anim(index)[0]


def fail(msg):
    raise SystemExit(f"error: {msg}")


def check_code_anchors(exe):
    for va, expected, what in CODE_ANCHORS:
        got = exe.read(va, len(expected))
        if got != expected:
            fail(f"{EXE_REL}: code anchor at {va:#x} ({what}) is {got.hex(' ')}, "
                 f"expected {expected.hex(' ')}; this is not the supported build of the game")


def read_imm_after(exe, va, prefix, size, what):
    got = exe.read(va, len(prefix))
    if got != prefix:
        fail(f"{EXE_REL}: code at {va:#x} ({what}) is {got.hex(' ')}, expected {prefix.hex(' ')}")
    raw = exe.read(va + len(prefix), size)
    return int.from_bytes(raw, "little")


def extract(exe, chd):
    check_code_anchors(exe)
    t = {}

    def expect_name(index, expected, where):
        if index == NO_ANIM or index >= TILE_COUNT:
            fail(f"{where}: CHD index {index:#x} is not a Table-4 animation")
        got = chd.name(index)
        if got != expected:
            fail(f"{where}: CHD {index} is named {got!r}, expected {expected!r}")

    def check_mirror_ids(row, where):
        # Dirs 5..7 of the colour-0 rows are virtual ids of the mirrored copies (FUN_01018a7a),
        # never real CHD animations.
        for d in (5, 6, 7):
            if not (row[d] >= TILE_COUNT or row[d] == NO_ANIM):
                fail(f"{where} dir {d}: {row[d]} is not a virtual mirror id")

    # --- walk / carry walk: [type][terrain][dir], index = type*40 + terrain*8 + dir -------------
    for key, va, prefix in (("walk", WALK_VA, "a"), ("carry_walk", CARRY_WALK_VA, "h")):
        raw = exe.u16s(va, 6 * 5 * 8)
        table = []
        for ty in range(6):
            per_type = []
            for tr in range(5):
                row = raw[ty * 40 + tr * 8: ty * 40 + tr * 8 + 8]
                where = f"{key} type {ty} terrain {tr}"
                if tr == 2:
                    if any(v != NO_ANIM for v in row):
                        fail(f"{where}: water row is not empty: {row}")
                else:
                    for d in range(5):
                        expect_name(row[d], f"{prefix}{TYPE_LETTERS[ty]}w{TERRAIN_LETTERS[tr]}"
                                            f"{DIR_DIGITS[d]}01", f"{where} dir {d}")
                    check_mirror_ids(row, where)
                per_type.append(row[:5])
            table.append(per_type)
        t[key] = table

    # --- idle / carry idle: [type][dir], index = type*8 + dir ------------------------------------
    for key, va, prefix in (("idle", IDLE_VA, "a"), ("carry_idle", CARRY_IDLE_VA, "h")):
        raw = exe.u16s(va, 6 * 8)
        table = []
        for ty in range(6):
            row = raw[ty * 8: ty * 8 + 8]
            for d in range(5):
                expect_name(row[d], f"{prefix}{TYPE_LETTERS[ty]}st{DIR_DIGITS[d]}01",
                            f"{key} type {ty} dir {d}")
            check_mirror_ids(row, f"{key} type {ty}")
            table.append(row[:5])
        t[key] = table

    # --- swimmer swim / dive-in / climb-out: [dir] -----------------------------------------------
    for key, va, stem in (("swim", SWIM_VA, "assw"), ("dive", DIVE_VA, "asdi"),
                          ("climb", CLIMB_VA, "asgo")):
        row = exe.u16s(va, 8)
        for d in range(5):
            expect_name(row[d], f"{stem}{DIR_DIGITS[d]}01", f"{key} dir {d}")
        check_mirror_ids(row, key)
        t[key] = row[:5]

    # --- idle in water: single animation, no direction (world+0x47c0) ----------------------------
    t["idle_water"] = exe.u16s(IDLE_WATER_VA, 1)[0]
    expect_name(t["idle_water"], "astw301", "idle in water")

    # --- can't go (action 0xB): [type], single direction -----------------------------------------
    for key, va, prefix in (("cant_go", CANT_GO_VA, "a"), ("carry_cant_go", CARRY_CANT_GO_VA, "h")):
        row = exe.u16s(va, 6)
        for ty in range(6):
            expect_name(row[ty], f"{prefix}{TYPE_LETTERS[ty]}cg301", f"{key} type {ty}")
        t[key] = row

    # --- "bump" effect played on a tile when a walking ant is blocked (FUN_0101c4f2) -------------
    t["bump"] = read_imm_after(exe, BUMP_PUSH_VA, b"\x68", 4, "push bump anim")
    call_rel = struct.pack("<i", BUMP_EFFECT_FN - (BUMP_CALL_VA + 5))
    if exe.read(BUMP_CALL_VA, 5) != b"\xe8" + call_rel:
        fail(f"{EXE_REL}: expected call FUN_010100e5 at {BUMP_CALL_VA:#x}")
    expect_name(t["bump"], "bump", "bump effect")

    # --- terrain class per CHD tile id (FUN_0100724c); ids that are not listed stay 0 ------------
    terrain = [0] * TILE_COUNT
    pair_bytes = exe.read(TERRAIN_PAIRS_VA, TERRAIN_PAIRS_END_VA - TERRAIN_PAIRS_VA)
    for k in range(len(pair_bytes) // 4):
        tile, cls = struct.unpack_from("<HH", pair_bytes, 4 * k)
        if tile >= TILE_COUNT or cls > 4:
            fail(f"terrain pair {k}: tile {tile} class {cls} out of range")
        terrain[tile] = cls
    for tile, cls in enumerate(terrain):
        if cls != 0:
            first = chd.name(tile)[:1].lower()
            if first != TERRAIN_LETTERS[cls]:
                fail(f"tile {tile} {chd.name(tile)!r} has class {cls} but a non-{TERRAIN_NAMES[cls]} name")
    t["tile_terrain"] = terrain

    # --- tile flags per CHD tile id (FUN_0100724c) ------------------------------------------------
    flags = [0] * TILE_COUNT
    for bit, va, count, stride in TILE_FLAG_LISTS:
        raw = exe.read(va, count * stride)
        for k in range(count):
            tile = struct.unpack_from("<H", raw, stride * k)[0]
            if tile >= TILE_COUNT:
                fail(f"tile flag {bit:#x} list entry {k}: tile {tile} out of range")
            flags[tile] |= bit
    t["tile_flags"] = flags

    # --- bridge tile ids (FUN_01008b90): layer-2 bridge makes a cell terrain class 3 -------------
    t["bridges"] = [read_imm_after(exe, va, b"\x66\x3d", 2, "cmp ax, bridge id") for va in BRIDGE_CMP_VAS]

    # --- path / passability tables ---------------------------------------------------------------
    t["passable"] = exe.u16s(PASSABLE_VA, 8)
    if any(v not in (0, 1) for v in t["passable"]):
        fail(f"passability table is not boolean: {t['passable']}")
    t["step_weight"] = exe.u32s(STEP_WEIGHT_VA, 6)
    t["swimmer_weight"] = read_imm_after(exe, SWIMMER_WEIGHT_VA, b"\x66\x3d\x05\x00\x75\x05\x6a", 1,
                                         "swimmer water weight")
    t["dir_table"] = exe.i16s(DIR_TABLE_VA, 9)
    if any(not 0 <= v <= 7 for v in t["dir_table"]):
        fail(f"direction table out of range: {t['dir_table']}")
    nb = exe.i32s(NEIGHBOUR_VA, 16)
    t["neighbour"] = [(nb[2 * d], nb[2 * d + 1]) for d in range(8)]
    for d, (dr, dc) in enumerate(t["neighbour"]):
        if not (-1 <= dr <= 1 and -1 <= dc <= 1) or t["dir_table"][(dr + 1) * 3 + (dc + 1)] != d:
            fail(f"neighbour {d} = {(dr, dc)} is inconsistent with the direction table")

    # --- action clips (FUN_0101ad02): colour-0 block of each table -----------------------------------
    t["actions"] = {}
    for key, _cname, va, shape, template, _what in ACTION_TABLES:
        if shape == "type":
            raw = exe.u16s(va, 6)
            rows = []
            for ty in range(6):
                exp = ACTION_NAME_EXCEPTIONS.get((key, ty), template.format(t=TYPE_LETTERS[ty], d=""))
                expect_name(raw[ty], exp, f"{key} type {ty}")
                rows.append(raw[ty])
            t["actions"][key] = rows
        elif shape == "type_dir":
            raw = exe.u16s(va, 6 * 8)
            rows = []
            for ty in range(6):
                row = raw[ty * 8: ty * 8 + 8]
                for d in range(5):
                    expect_name(row[d], template.format(t=TYPE_LETTERS[ty], d=DIR_DIGITS[d]), f"{key} type {ty} dir {d}")
                check_mirror_ids(row, f"{key} type {ty}")
                rows.append(row[:5])
            t["actions"][key] = rows
        elif shape == "dir":
            row = exe.u16s(va, 8)
            for d in range(5):
                if d in CARDINAL_DIRS:
                    expect_name(row[d], template.format(t="", d=DIR_DIGITS[d]), f"{key} dir {d}")
                elif row[d] != NO_ANIM:
                    fail(f"{key} dir {d}: expected no animation, got {row[d]}")
            if row[5] != NO_ANIM or row[7] != NO_ANIM or not (row[6] >= TILE_COUNT):
                fail(f"{key}: dirs 5..7 must be (none, mirrored E, none): {row[5:8]}")
            t["actions"][key] = row[:5]
        else:  # single
            v = exe.u16s(va, 1)[0]
            expect_name(v, template, key)
            t["actions"][key] = v

    # --- frames of every referenced animation -----------------------------------------------------
    referenced = set()
    for key in ("walk", "carry_walk"):
        for per_type in t[key]:
            for row in per_type:
                referenced.update(v for v in row if v != NO_ANIM)
    for key in ("idle", "carry_idle"):
        for row in t[key]:
            referenced.update(row)
    for key in ("swim", "dive", "climb", "cant_go", "carry_cant_go"):
        referenced.update(t[key])
    referenced.update((t["idle_water"], t["bump"]))
    for value in t["actions"].values():
        if isinstance(value, int):
            referenced.add(value)
        else:
            for item in value:
                if isinstance(item, list):
                    referenced.update(v for v in item if v != NO_ANIM)
                elif item != NO_ANIM:
                    referenced.add(item)

    frames = []
    clips = []
    for index in sorted(referenced):
        name, (flag1, flag2, flag3), raw_frames = chd.anim(index)
        if not raw_frames:
            fail(f"CHD {index} {name!r} has no frames")
        clip_flags = (1 if flag1 else 0) | (2 if flag2 else 0) | (4 if flag3 else 0)
        clips.append((index, len(frames), len(raw_frames), clip_flags, name))
        for dx, dy, dur, event, sound in raw_frames:
            # FUN_0102a977 stores dx, dy, duration and the sound id as 16-bit words and maps the
            # event sentinel 1111111 to 11111. Referenced animations never need truncation; check
            # it so the C++ asset test can compare against the raw 32-bit CHD fields.
            ev = EVENT_NONE if event == EVENT_NONE_CHD else event & 0xFFFF
            if wrap16(dx) != dx or wrap16(dy) != dy or not 0 <= dur <= 0xFFFF or \
                    (event != EVENT_NONE_CHD and ev != event) or wrap16(sound) != sound:
                fail(f"CHD {index} {name!r}: frame value does not fit the 16-bit frame record")
            frames.append((dx, dy, dur, ev, sound))
    if len(frames) > 0xFFFF:
        fail("too many frames for uint16 frame offsets")
    t["frames"] = frames
    t["clips"] = clips
    return t


# ---------------------------------------------------------------------------------------------
# C++ rendering
# ---------------------------------------------------------------------------------------------
def fmt_anim(v):
    return "0x7FFE" if v == NO_ANIM else f"{v:6d}"


def render(t, chd, exe_bytes, chd_bytes):
    out = []
    w = out.append
    w("// " + "=" * 97)
    w("// GENERATED FILE - DO NOT EDIT BY HAND.")
    w("//")
    w("// Generated by tools/extract_movement_tables.py from the original 1998 game files:")
    w(f"//   {EXE_REL}  {len(exe_bytes)} bytes, SHA-256 {hashlib.sha256(exe_bytes).hexdigest()}")
    w(f"//   {CHD_REL}  {len(chd_bytes)} bytes, SHA-256 {hashlib.sha256(chd_bytes).hexdigest()}")
    w("//")
    w("// Regenerate:  python3 tools/extract_movement_tables.py")
    w("// Verify:      python3 tools/extract_movement_tables.py --check")
    w("//")
    w("// Addresses are Ants.exe virtual addresses (image base 0x01000000). Only the colour-0 blocks")
    w("// of the static tables hold real CHD Table-4 indices (colours 1..3 share colour 0's frames).")
    w("//   kFrames, kClips   ants.chd Table-4 frames {dx, dy, duration_ms, event, sound} of every")
    w("//                     animation referenced below, as FUN_0102a977 reads them (event 1111111 ->")
    w("//                     11111 = none, sound -1 = none). A frame's dx/dy is applied when that")
    w("//                     frame's duration expires. ClipRec::flags = the sequence flags read by")
    w("//                     FUN_0102be51: bit0 flag1 (sound once per loop), bit1 flag2 (duplicate a")
    w("//                     sound that is still playing), bit2 flag3 (stop the clip's sounds when it")
    w("//                     is replaced). The flags never affect timing or movement.")
    w(f"//   kWalk             {WALK_VA:#x} [type][terrain][dir 0..4] walk (action 1)")
    w(f"//   kCarryWalk        {CARRY_WALK_VA:#x} same, ant holding food")
    w(f"//   kIdle             {IDLE_VA:#x} [type][dir 0..4] idle (action 0)")
    w(f"//   kCarryIdle        {CARRY_IDLE_VA:#x} same, ant holding food")
    w(f"//   kSwim             {SWIM_VA:#x} [dir 0..4] swimmer on water (walk, terrain 2)")
    w(f"//   kDive             {DIVE_VA:#x} [dir 0..4] swimmer entering water (one tile)")
    w(f"//   kClimb            {CLIMB_VA:#x} [dir 0..4] swimmer leaving water (one tile)")
    w(f"//   kIdleWater        {IDLE_WATER_VA:#x} [colour 0] idle on water, no direction")
    w(f"//   kCantGo           {CANT_GO_VA:#x} [type] action 0xB, single direction")
    w(f"//   kCarryCantGo      {CARRY_CANT_GO_VA:#x} same, ant holding food")
    w(f"//   kBump             push imm32 at {BUMP_PUSH_VA:#x} (FUN_0101c4f2 blocked-walk effect)")
    w(f"//   kTileTerrain      {TERRAIN_PAIRS_VA:#x}..{TERRAIN_PAIRS_END_VA:#x} {{tile, class}} pairs "
      "(FUN_0100724c); unlisted tiles are 0")
    lists = [f"{bit:#04x}@{va:#x}" + ("" if stride == 2 else f" ({stride}-byte stride)")
             for bit, va, _count, stride in TILE_FLAG_LISTS]
    w("//   kTileFlags        bits OR-ed in by FUN_0100724c from uint16 tile-id lists (bit @ address):")
    w("//                     " + ", ".join(lists[:3]) + ",")
    w("//                     " + ", ".join(lists[3:]) + ".")
    w("//                     0x01 = solid object footprint, 0x02 = food (FUN_010071dd), 0x04 = power-up")
    w("//                     (FUN_01007202); the meaning of 0x08, 0x10 and 0x20 is not decoded here.")
    w("//   kBridgeTiles      FUN_01008b90 compare immediates (layer-2 bridge -> terrain class 3)")
    w(f"//   kPassableByTerrain {PASSABLE_VA:#x} (FUN_0101f780 rule R1; class 2 only for swimmers)")
    w(f"//   kStepWeight       {STEP_WEIGHT_VA:#x} path step-cost weight by class (FUN_010208e8)")
    w(f"//   kSwimmerWaterWeight push imm8 at {SWIMMER_WEIGHT_VA + 7:#x} (FUN_010208e8, swimmer on class 2)")
    w(f"//   kDirTable         {DIR_TABLE_VA:#x} direction of a step, index (drow+1)*3+(dcol+1) (FUN_01017531)")
    w(f"//   kNeighbour        {NEIGHBOUR_VA:#x} {{drow, dcol}} per direction (FUN_01019d28)")
    w("//")
    for key, cname, va, shape, _template, what in ACTION_TABLES:
        w(f"//   {cname:<24s} {va:#x} {{shape {shape}}} {what}")
    w("//")
    w("// Directions: 0 N, 1 NE, 2 E, 3 SE, 4 S. 5 SW, 6 W and 7 NW are not stored: they are the 3, 2")
    w("// and 1 animations mirrored with every frame dx negated (FUN_01018a7a / FUN_01018b9f).")
    w("// Ant types: 0 worker, 1 bomber, 2 fire, 3 thief, 4 combat, 5 swimmer.")
    w("// Terrain classes: 0 grass, 1 sand, 2 water, 3 mud, 4 dirt. 0x7FFE = no animation.")
    w("// " + "=" * 97)
    w("#ifndef ANTS_SIM_MOVEMENT_TABLES_DATA_INC")
    w("#define ANTS_SIM_MOVEMENT_TABLES_DATA_INC")
    w("")
    w("#include <cstdint>")
    w("")
    w("// clang-format off")
    w("namespace ants::sim::movement::data {")
    w("")
    w("struct FrameRec { int16_t dx; int16_t dy; uint16_t duration_ms; uint16_t event; int16_t sound; };")
    w("struct ClipRec { uint16_t chd_index; uint16_t first_frame; uint16_t frame_count; uint8_t flags; };")
    w("")

    # kFrames
    w(f"// {len(t['frames'])} frames of {len(t['clips'])} animations, grouped per animation "
      "(same order as kClips).")
    w("inline constexpr FrameRec kFrames[] = {")
    for index, first, count, clip_flags, name in t["clips"]:
        total = sum(f[2] for f in t["frames"][first:first + count])
        w(f"    // [{first}] CHD {index} {name}: {count} frame{'s' if count != 1 else ''}, {total} ms")
        for k in range(first, first + count, 4):
            chunk = t["frames"][k:min(k + 4, first + count)]
            w("    " + " ".join(f"{{{dx:3d}, {dy:3d}, {dur:4d}, {ev:5d}, {snd:2d}}},"
                                for dx, dy, dur, ev, snd in chunk))
    w("};")
    w("")

    # kClips
    w("// One entry per referenced CHD animation, sorted by chd_index:")
    w("// {chd_index, first_frame, frame_count, flags}.")
    w("inline constexpr ClipRec kClips[] = {")
    for index, first, count, clip_flags, name in t["clips"]:
        w(f"    {{{index:4d}, {first:5d}, {count:3d}, {clip_flags}}},  // {name}")
    w("};")
    w("")

    def names_of(row):
        return " ".join("-" if v == NO_ANIM else chd.name(v) for v in row)

    for key, cname, what in (("walk", "kWalk", "walk"), ("carry_walk", "kCarryWalk", "carry walk")):
        w(f"// {what}: [type][terrain][dir N, NE, E, SE, S]; the water row is empty (swimmers use kSwim,")
        w("// every other type walks on water with its mud animation).")
        w(f"inline constexpr uint16_t {cname}[6][5][5] = {{")
        for ty in range(6):
            w(f"    {{  // type {ty} {TYPE_NAMES[ty]}")
            for tr in range(5):
                row = t[key][ty][tr]
                w(f"        {{{', '.join(fmt_anim(v) for v in row)}}},  // {TERRAIN_NAMES[tr]:5s} {names_of(row)}")
            w("    },")
        w("};")
        w("")

    for key, cname, what in (("idle", "kIdle", "idle"), ("carry_idle", "kCarryIdle", "carry idle")):
        w(f"// {what}: [type][dir N, NE, E, SE, S]")
        w(f"inline constexpr uint16_t {cname}[6][5] = {{")
        for ty in range(6):
            row = t[key][ty]
            w(f"    {{{', '.join(fmt_anim(v) for v in row)}}},  // {TYPE_NAMES[ty]:7s} {names_of(row)}")
        w("};")
        w("")

    for key, cname, what in (("swim", "kSwim", "swimmer on water"),
                             ("dive", "kDive", "swimmer entering water"),
                             ("climb", "kClimb", "swimmer leaving water")):
        row = t[key]
        w(f"// {what}: [dir N, NE, E, SE, S]  {names_of(row)}")
        w(f"inline constexpr uint16_t {cname}[5] = {{{', '.join(fmt_anim(v) for v in row)}}};")
    w("")
    w(f"inline constexpr uint16_t kIdleWater = {t['idle_water']};  // {chd.name(t['idle_water'])}")
    w("")
    for key, cname, what in (("cant_go", "kCantGo", "can't go"), ("carry_cant_go", "kCarryCantGo",
                                                                 "carry can't go")):
        row = t[key]
        w(f"// {what}: [type]  {names_of(row)}")
        w(f"inline constexpr uint16_t {cname}[6] = {{{', '.join(fmt_anim(v) for v in row)}}};")
    w("")
    w(f"inline constexpr uint16_t kBump = {t['bump']:#06x};  // {chd.name(t['bump'])}")
    w("")

    # action clips
    for key, cname, va, shape, _template, what in ACTION_TABLES:
        value = t["actions"][key]
        w(f"// {what} ({va:#x})")
        if shape == "type":
            w(f"// [type]  {names_of(value)}")
            w(f"inline constexpr uint16_t {cname}[6] = {{{', '.join(fmt_anim(v) for v in value)}}};")
        elif shape == "type_dir":
            w(f"// [type][dir N, NE, E, SE, S]")
            w(f"inline constexpr uint16_t {cname}[6][5] = {{")
            for ty in range(6):
                w(f"    {{{', '.join(fmt_anim(v) for v in value[ty])}}},  // {TYPE_NAMES[ty]:7s} {names_of(value[ty])}")
            w("};")
        elif shape == "dir":
            w(f"// [dir N, NE, E, SE, S]  {names_of(value)}")
            w(f"inline constexpr uint16_t {cname}[5] = {{{', '.join(fmt_anim(v) for v in value)}}};")
        else:
            w(f"inline constexpr uint16_t {cname} = {value};  // {chd.name(value)}")
        w("")

    def byte_table(cname, values, what):
        w(f"// {what}")
        w(f"inline constexpr uint8_t {cname}[{len(values)}] = {{")
        for k in range(0, len(values), 16):
            w(f"    /* {k:#05x} */ " + " ".join(f"{v:2d}," for v in values[k:k + 16]))
        w("};")
        w("")

    byte_table("kTileTerrain", t["tile_terrain"], "terrain class per CHD tile id (0 grass, 1 sand, "
               "2 water, 3 mud, 4 dirt)")
    byte_table("kTileFlags", t["tile_flags"], "tile flag bits per CHD tile id (0x01 solid footprint, "
               "0x02 food, 0x04 power-up; see the header)")

    w(f"inline constexpr uint16_t kBridgeTiles[4] = {{{', '.join(f'{v:#06x}' for v in t['bridges'])}}};  // "
      + " ".join(chd.name(v) for v in t["bridges"]))
    w("")
    w(f"inline constexpr uint8_t kPassableByTerrain[8] = {{{', '.join(str(v) for v in t['passable'])}}};")
    w(f"inline constexpr uint32_t kStepWeight[6] = {{{', '.join(str(v) for v in t['step_weight'])}}};")
    w(f"inline constexpr uint32_t kSwimmerWaterWeight = {t['swimmer_weight']};")
    w(f"inline constexpr uint8_t kDirTable[9] = {{{', '.join(str(v) for v in t['dir_table'])}}};")
    w("// {drow, dcol} per direction N, NE, E, SE, S, SW, W, NW")
    w("inline constexpr int8_t kNeighbour[8][2] = {" +
      ", ".join(f"{{{dr}, {dc}}}" for dr, dc in t["neighbour"]) + "};")
    w("")
    w("}  // namespace ants::sim::movement::data")
    w("// clang-format on")
    w("")
    w("#endif  // ANTS_SIM_MOVEMENT_TABLES_DATA_INC")
    return "\n".join(out) + "\n"


def generate():
    exe_path = REPO_ROOT / EXE_REL
    chd_path = REPO_ROOT / CHD_REL
    for p, rel in ((exe_path, EXE_REL), (chd_path, CHD_REL)):
        if not p.is_file():
            fail(f"{rel} not found (run from a checkout that contains Original-Ants/)")
    exe = Exe(exe_path)
    chd = Chd(chd_path)
    tables = extract(exe, chd)
    return render(tables, chd, exe_path.read_bytes(), chd.data)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[1].strip(),
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true",
                      help=f"regenerate in memory and exit 1 if {OUT_REL} differs")
    mode.add_argument("--stdout", action="store_true", help="print the generated file instead of writing it")
    args = ap.parse_args()

    text = generate()
    out_path = REPO_ROOT / OUT_REL
    if args.stdout:
        sys.stdout.write(text)
        return 0
    if args.check:
        current = out_path.read_text(encoding="utf-8") if out_path.is_file() else None
        if current == text:
            print(f"{OUT_REL} is up to date")
            return 0
        if current is None:
            print(f"{OUT_REL} is missing; run: python3 tools/extract_movement_tables.py", file=sys.stderr)
            return 1
        old, new = current.splitlines(), text.splitlines()
        line = next((i for i, (a, b) in enumerate(zip(old, new)) if a != b), min(len(old), len(new)))
        print(f"{OUT_REL} is stale (first difference at line {line + 1}); "
              "run: python3 tools/extract_movement_tables.py", file=sys.stderr)
        return 1
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(f"wrote {OUT_REL}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
