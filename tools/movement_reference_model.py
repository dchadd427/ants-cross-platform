#!/usr/bin/env python3
"""Reference model of the original 1998 ant locomotion (Ants.exe), for auditing the golden values of
tests/test_sim/test_movement_golden.cpp.

The model reads the walk / idle / swim / dive / climb animation tables straight from
Original-Ants/Ants.exe and the frames from Original-Ants/ants.chd (with the readers of
tools/extract_movement_tables.py) and re-implements, independently of the C++ port:

  * the animation stepper (FUN_0102b95f / FUN_0102b997): millisecond clock, catch-up loop, the start step
    of a freshly played clip, and the re-entrancy quirk (a clip started from inside a step callback books
    its first frame's duration twice);
  * SetAction (FUN_0101ad02) for idle and walk, including swim / dive / climb selection and the restart rule;
  * WalkStep (FUN_0101b8cb): idiv tile detection, the +-1 nudge into a non-waypoint tile, the <= 2 px snap,
    ARRIVE, the terrain restart at tile boundaries and the dive / climb completion;
  * path completion (PathComplete -> StopSync -> StopAt -> idle).

Blocking (TryEnterTile), bombs and orders are not modelled: one ant walks a given path over open terrain.
Times are milliseconds after the path is delivered; the ant starts idle facing south (direction 4).

Usage:  python3 tools/movement_reference_model.py            # print the golden table
        python3 tools/movement_reference_model.py --trace    # also print every pixel move of each case
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import extract_movement_tables as emt  # noqa: E402  (readers for Ants.exe / ants.chd)

MIRROR = {5: 3, 6: 2, 7: 1}                 # SW, W, NW are the SE, E, NE animations with dx negated
DIRTAB = [7, 0, 1, 6, 0, 2, 5, 4, 3]         # 0x1002b28: direction by [drow + 1][dcol + 1]


class Tables:
    def __init__(self, exe, chd):
        self.chd = chd
        self.walk = exe.u16s(emt.WALK_VA, 240)             # colour-0 block [type 6][terrain 5][dir 8]
        self.carry = exe.u16s(emt.CARRY_WALK_VA, 240)
        self.idle = exe.u16s(emt.IDLE_VA, 48)              # [type 6][dir 8]
        self.carry_idle = exe.u16s(emt.CARRY_IDLE_VA, 48)
        self.swim = exe.u16s(emt.SWIM_VA, 8)
        self.dive = exe.u16s(emt.DIVE_VA, 8)
        self.climb = exe.u16s(emt.CLIMB_VA, 8)
        self.idle_water = exe.u16s(emt.IDLE_WATER_VA, 1)[0]

    def frames(self, index, mirrored=False):
        out = [(f[0], f[1], f[2]) for f in self.chd.anim(index)[2]]
        return [(-dx, dy, du) for dx, dy, du in out] if mirrored else out

    def directional(self, row, d):
        return self.frames(row[MIRROR.get(d, d)], d in MIRROR)

    def walk_clip(self, typ, terr, d, carrying):
        table = self.carry if carrying else self.walk
        return self.directional(table[typ * 40 + terr * 8: typ * 40 + terr * 8 + 8], d)

    def idle_clip(self, typ, d, carrying, terr):
        if terr == 2:
            return self.frames(self.idle_water)
        table = self.carry_idle if carrying else self.idle
        return self.directional(table[typ * 8: typ * 8 + 8], d)


def dir8(a, b):
    return DIRTAB[(b[0] - a[0] + 1) * 3 + (b[1] - a[1] + 1)]


def cdiv(a, b):  # C integer division (truncation toward zero)
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


class Ant:
    """One ant; tiles are (row, col) like the original."""

    def __init__(self, tables, terrain, typ, carrying, row, col, start_dir):
        self.t = tables
        self.terrain = terrain
        self.typ = typ
        self.carrying = carrying
        self.x = col * 32 + 16
        self.y = row * 32 + 16
        self.tile = (row, col)
        self.dir = start_dir
        self.action = 0
        self.f88 = 0                      # dive / climb animation running
        self.frames = None
        self.cursor = 0                   # 0: start step pending, else 1-based current frame
        self.next_time = 0
        self.wp = []
        self.idx = 0
        self.clock = 0
        self.arrival = None

    # ---- animation stepper (FUN_0102b95f / FUN_0102b997) --------------------------------------------
    def play(self, frames):
        self.frames = frames
        self.cursor = 0
        self.next_time = self.clock
        self.update()                      # sprite on the display list: immediate start step

    def update(self):
        while self.step(self.clock):
            pass

    def step(self, now):
        if not self.frames:
            return 0
        if self.cursor == 0:
            e = {'dx': 0, 'dy': 0, 'status': 0}
            nxt = 1
        else:
            if len(self.frames) <= 1 or now < self.next_time:
                return 0
            cur = self.frames[self.cursor - 1]
            last = self.cursor == len(self.frames)
            e = {'dx': cur[0], 'dy': cur[1], 'status': 2 if last else 1}
            nxt = 1 if last else self.cursor + 1
        before = self.frames
        self.on_anim_step(e)
        if self.frames is not before:
            nxt = self.cursor if self.cursor else 1
        self.set_pos(self.x + e['dx'], self.y + e['dy'])
        self.cursor = nxt
        self.next_time += self.frames[nxt - 1][2]
        return 1 if e['status'] != 0 else 0

    def set_pos(self, x, y):
        self.x, self.y = x, y
        self.tile = (cdiv(y, 32), cdiv(x, 32))

    # ---- behaviour ----------------------------------------------------------------------------------
    def on_anim_step(self, e):
        if self.action in (0, 1):
            self.walk_step(e)

    @staticmethod
    def centre(t):
        return (t[1] * 32 + 16, t[0] * 32 + 16)

    def set_action(self, action, d, terr_a, terr_b, flag):
        same_action = self.action == action
        same_dir = self.dir == d
        self.dir = d
        self.action = action
        supplied = terr_a is not None
        if not supplied:
            terr_a = self.terrain(*self.tile)
        if action == 0:
            self.f88 = 0
            self.play(self.t.idle_clip(self.typ, d, self.carrying, terr_a))
            return
        if flag and self.typ == 5:
            dive = terr_a != 2 and terr_b == 2
            climb = terr_a == 2 and terr_b != 2
            if dive or climb:
                self.f88 = 1
                self.play(self.t.directional(self.t.dive if dive else self.t.climb, d))
                return
        tr = terr_a if (not supplied or flag) else terr_b
        if tr == 2 and self.typ != 5:
            tr = 3                         # non-swimmers on water use the mud walk
        frames = self.t.directional(self.t.swim, d) if tr == 2 else \
            self.t.walk_clip(self.typ, tr, d, self.carrying)
        if not same_action or not supplied:
            restart = True
        elif self.f88:
            self.f88 = 0
            restart = True
        elif flag:
            restart = not same_dir
        else:
            restart = terr_a != terr_b
        if restart:
            self.play(frames)

    def walk_step(self, e):
        c = self.centre(self.tile)
        if e['status'] == 0:
            return
        if self.f88 and e['status'] == 2:
            e['dx'], e['dy'] = c[0] - self.x, c[1] - self.y
            return self.arrive(e, self.tile)
        if not self.wp:
            return
        if self.action == 0:
            return self.arrive(e, self.tile)
        nx, ny = self.x + e['dx'], self.y + e['dy']
        nt = (cdiv(ny, 32), cdiv(nx, 32))
        new_tile = nt != self.tile
        if not new_tile and nt != self.wp[self.idx]:
            return
        if new_tile and nt != self.wp[self.idx]:
            sx = 1 if e['dx'] > 0 else -1
            sy = 1 if e['dy'] > 0 else -1
            e['dx'] += sx
            nx += sx
            e['dy'] += sy
            ny += sy
            nt = (cdiv(ny, 32), cdiv(nx, 32))
        if self.f88:
            return
        n = self.centre(nt)
        if abs(nx - n[0]) <= 2 and abs(ny - n[1]) <= 2:
            e['dx'], e['dy'] = n[0] - self.x, n[1] - self.y
            return self.arrive(e, nt)
        if new_tile:
            self.set_action(1, self.dir, self.terrain(*self.tile), self.terrain(*nt), 0)

    def arrive(self, e, cur):
        self.idx += 1
        if self.idx >= len(self.wp):
            self.wp, self.idx = [], 0
            c = self.centre(self.tile)
            self.set_pos(*c)               # StopAt: exact tile centre, then idle
            self.arrival = self.clock
            self.set_action(0, self.dir, None, None, 0)
            e['dx'] = e['dy'] = 0
            return
        nxt = self.wp[self.idx]
        self.set_action(1, dir8(cur, nxt), self.terrain(*cur), self.terrain(*nxt), 1)

    def deliver_path(self, path):          # message 6: snap, restart the idle animation, set the path
        self.set_pos(*self.centre(path[0]))
        self.set_action(self.action, self.dir, None, None, 0)
        self.wp, self.idx = list(path), 0


def run(tables, path, terrain, typ=0, carrying=False, start_dir=4, limit=20000):
    a = Ant(tables, terrain, typ, carrying, path[0][0], path[0][1], start_dir)
    a.play(tables.idle_clip(typ, start_dir, carrying, terrain(*path[0])))
    a.deliver_path(path)
    moves = []
    last = (a.x, a.y)
    t = 0
    while t < limit and a.arrival is None:
        t += 1
        a.clock = t
        a.update()
        if (a.x, a.y) != last:
            moves.append((t, a.x - last[0], a.y - last[1], a.x, a.y))
            last = (a.x, a.y)
    return a, moves


def straight(n, d=(0, 1), start=(5, 5)):
    return [(start[0] + d[0] * i, start[1] + d[1] * i) for i in range(n + 1)]


GRASS, SAND, WATER, MUD, DIRT = 0, 1, 2, 3, 4
WORKER, BOMBER, FIRE, THIEF, COMBAT, SWIMMER = range(6)

# (name, path as (row, col) tiles, terrain(row, col), type, carrying) - the cases of the golden test
CASES = [
    ("Worker, grass, 1 tile E", straight(1), lambda r, c: GRASS, WORKER, False),
    ("Worker, grass, 1 tile W", straight(1, (0, -1)), lambda r, c: GRASS, WORKER, False),
    ("Worker, grass, 3 tiles E", straight(3), lambda r, c: GRASS, WORKER, False),
    ("Worker, grass, 1 tile SE", straight(1, (1, 1)), lambda r, c: GRASS, WORKER, False),
    ("Worker, grass, 3 tiles SE", straight(3, (1, 1)), lambda r, c: GRASS, WORKER, False),
    ("Worker, grass, 1 tile NW", straight(1, (-1, -1)), lambda r, c: GRASS, WORKER, False),
    ("Worker, sand, 3 tiles E", straight(3), lambda r, c: SAND, WORKER, False),
    ("Worker, dirt, 3 tiles E", straight(3), lambda r, c: DIRT, WORKER, False),
    ("Worker, mud, 1 tile E", straight(1), lambda r, c: MUD, WORKER, False),
    ("Worker, mud, 1 tile SE", straight(1, (1, 1)), lambda r, c: MUD, WORKER, False),
    ("Worker carrying food, grass, 3 tiles E", straight(3), lambda r, c: GRASS, WORKER, True),
    ("Thief, grass, 3 tiles E", straight(3), lambda r, c: GRASS, THIEF, False),
    ("Bomber, grass, 3 tiles E", straight(3), lambda r, c: GRASS, BOMBER, False),
    ("Fire ant, grass, 3 tiles E", straight(3), lambda r, c: GRASS, FIRE, False),
    ("Combat ant, grass, 3 tiles E", straight(3), lambda r, c: GRASS, COMBAT, False),
    ("Swimmer, grass, 3 tiles E", straight(3), lambda r, c: GRASS, SWIMMER, False),
    ("Worker, E then SE", [(5, 5), (5, 6), (6, 7)], lambda r, c: GRASS, WORKER, False),
    ("Worker, grass then mud from column 7, 3 tiles E", straight(3), lambda r, c: MUD if c >= 7 else GRASS, WORKER, False),
    ("Swimmer, water, 1 tile E", straight(1), lambda r, c: WATER, SWIMMER, False),
    ("Swimmer, water, 1 tile SE", straight(1, (1, 1)), lambda r, c: WATER, SWIMMER, False),
    ("Swimmer, grass to water (dive), 2 tiles E", straight(2), lambda r, c: WATER if c >= 6 else GRASS, SWIMMER, False),
    ("Swimmer, water to grass (climb), 2 tiles E", straight(2), lambda r, c: GRASS if c >= 6 else WATER, SWIMMER, False),
    ("Worker, grass, 5 tiles E", straight(5), lambda r, c: GRASS, WORKER, False),
    ("Worker, mud, 5 tiles E", straight(5), lambda r, c: MUD, WORKER, False),
]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--trace", action="store_true", help="print every pixel move")
    args = ap.parse_args()
    exe = emt.Exe(emt.REPO_ROOT / emt.EXE_REL)
    chd = emt.Chd(emt.REPO_ROOT / emt.CHD_REL)
    tables = Tables(exe, chd)
    print(f"{'case':52s} {'first':>6s} {'arrival':>8s} {'steps':>6s}  final pixel")
    for name, path, terrain, typ, carrying in CASES:
        a, moves = run(tables, path, terrain, typ, carrying)
        first = moves[0][0] if moves else None
        print(f"{name:52s} {first!s:>6s} {a.arrival!s:>8s} {len(moves):6d}  ({a.x},{a.y})")
        if args.trace:
            for m in moves:
                print("      t=%5d d=(%3d,%3d) pos=(%d,%d)" % m)


if __name__ == "__main__":
    main()
