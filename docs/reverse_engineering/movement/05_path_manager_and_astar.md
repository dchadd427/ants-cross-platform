# Ants.exe path planner: reverse-engineering report (cluster "pathfinder")

Every claim below comes from Capstone disassembly of `Original-Ants/Ants.exe` (image base 0x01000000). Instruction addresses are given inline. I used the Ghidra C output only to check one control-flow reading (the goal ring search). It agrees with the disassembly.

## 0. Summary

* **Algorithm:** A* on the 8-connected tile grid. There is no weighted heuristic and no fallback to the closest reachable node.
  * **Open list:** a binary min-heap of tile coordinates. The key f = g + h is read from the per-search grid on every comparison.
  * **Closed set and bookkeeping:** stored as bits in a per-search grid of dwords, one dword per map tile.
* **Scheduling:** A* does not run at click time. The click handler (`FUN_0101fc50`) queues a request on the **PATHMGR** task (list at `[world+0x4968]+0x2c`).
  * PATHMGR runs every 50 ms (real time, cooperative scheduler).
  * Each run gives **one** request a slice of at most **1000 node expansions**. Unfinished requests go to the back of the queue (round robin).
  * A finished path is sent as network message type 6. The message handler installs it in the ant through `FUN_0101ab87`.
* **Neighbour order:** N, NE, E, SE, S, SW, W, NW (direction indices 0..7). Diagonal corner cutting is **allowed**: nothing checks the two orthogonal neighbours.
* **Step cost:** `(C(from)+C(to))>>1` orthogonally and `trunc((double)(C(from)+C(to))*1.4)>>1` diagonally. Terrain costs C: grass 20, sand 16, water 8000 (21 for a swimmer), mud 48, dirt 24. An impassable edge costs 8000. The search fails when the smallest f on the heap is >= 8000.
* **Heuristic:** `16*max(|dr|,|dc|)` (Chebyshev x 16). It is admissible, because the cheapest orthogonal step is 16 and the cheapest diagonal step is 22.
* **Goal tile:** chosen before A* starts, by `FUN_010202e7`. If the clicked tile fails the destination test `FUN_0101f780`, it scans square rings at distance 1..4 in a fixed order and takes the first tile that passes. The own anthill entrance has a special alternate tile at `player+0x46`.
* **Unreachable goal:** A* returns count 0. The ant stops with action 0xb and sound 0x3a.
* **Waypoint array:** holds **start tile ... goal tile**, both included (`path[0]` = the ant's current tile, `path[count-1]` = goal). The index `+0xde` starts at 0.

## 1. Call graph (order to waypoints)

```
user group-move 0x10288xx (sorted nearest-first by FUN_01020911) / other callers
  -> FUN_0101fc50  Ant::GoTo(Tile* t, int userCmd, int special, int unused)   [ret 0x10]
       FUN_01020655  Ant::ClassifyOrder(t, special, userCmd)  -> +0xa8 order, +0xac target
       FUN_01020128  Ant::ApproachTile(t)        (orders 6,7,8,9,0xd,0xe)
       FUN_010202e7  Ant::AdjustGoal(t, userCmd, 1, 1, 5)   (all other orders except 0xb)
            FUN_0101f780  Ant::IsValidDest(t, flags, info*)
       FUN_010246e8  PathMgr::Request(antId, &curTile, &goal)       [world+0x4968]
            FUN_010197ed  PathRequest::ctor -> FUN_010198d1 PathRequest::Init (grid+heap+push start)
PATHMGR task (vtable 0x1004e28, ctor 0x102463a, run 0x1024786), scheduled every 50 ms
  -> FUN_01019a66 PathRequest::Step(1000)       (A*)
       FUN_01019f0a heap pop, FUN_01019c31 neighbours, FUN_01020951 Ant::StepCost,
       FUN_01020911 heuristic, FUN_01017531 dir, FUN_01019e20 heap push, FUN_01019d28 build path
  -> FUN_0100cba4 World::DeliverPath(antId, path, count)
       FUN_010228ff build msg type 6 -> FUN_0100d791(msg, 0x0a, 0) (local dispatch + network send)
            handler 0x10229b7 (dispatch table 0x1047320 + 6*8 = 0x1047350)
                 FUN_0101ad02(action, dir, -1, -1, 0, 1); FUN_0101ab87(count, path, order, &extra)
Other path producer: FUN_0101da6f (2-tile "lunge" path [cur, next], order 0xf) -> FUN_0100cba4 directly.
Runtime blocked-waypoint handling: FUN_0101c4f2 (waits with ANTPAUSE, or re-issues FUN_0101fc50).
```

## 2. Data structures

### 2.1 PathRequest (0x34 bytes, vtable 0x1004990, ctor FUN_010197ed, dtor 0x10199e1)
| off | type | meaning | evidence |
|---|---|---|---|
| +0x08 | int16 | ant id (ant+0x58) | 0x101980e |
| +0x0c | Ant* | `localPlayer.ants[id]` (player = `[world+0x4958+ [world+0xf2a]*4]`; data `+0x18`, stride `+0x1c`, count `+0x24`); addref'd | 0x101981a-0x1019845 |
| +0x10 | Tile | start | 0x101984f |
| +0x14 | Tile | goal | 0x101985a |
| +0x18 | Tile* | result path (new[]) | 0x1019857, 0x1019dd3 |
| +0x1c | int16 | result count (0 = no path) | 0x101985d, 0x1019db7, 0x1019c16 |
| +0x20 | dword** | search grid (row-pointer array from pool) | 0x1019946 |
| +0x24 | int16 | pool slot index (0..3) | 0x10198e5 |
| +0x28 | Heap* | open heap; NULL = search finished | 0x1019988, 0x1019c23 |
| +0x2c/+0x2e | int16 | rows/cols = map+0xd0/+0xd2 at creation | 0x101988c, 0x101989a |
| +0x30 | Map* | map (addref) | 0x1019878 |

**Grid pool.** Four global slots at 0x104b360, 8 bytes each: `{dword* grid; dword free}`. `FUN_010197b5` sets all to `{0,1}` (called from the PATHMGR ctor at 0x102466b). `FUN_010197ce` frees them (PATHMGR dtor). The request dtor sets `free=1` (0x1019a42).

### 2.2 Search-grid cell (dword per tile; `FUN_01006030` rows x cols x 4, zeroed by `FUN_0100607b` = memset(grid[0],0,rows*cols*4))
```
bits  0..13  g   (0x3fff)          0x1019ad7, 0x1019b4e, 0x1019b74-0x1019b7a
bits 14..26  h   (0x1fff << 14)    0x10199ac-0x10199ba, 0x1019b8e-0x1019b9c
bits 27..29  parent direction (dir FROM this node TO its parent)  0x1019baf-0x1019bbb
bit  30      "opened" (ever pushed)                                0x1019995, 0x1019bbd-0x1019bca
bit  31      closed (expanded)                                     0x1019bf2
f = (cell & 0x3fff) + ((cell >> 14) & 0x1fff)                      (unsigned)
```

### 2.3 Heap (0x1c bytes, vtable 0x10049a0; dynamic array `FUN_0102a085(elemSize=4, growBy=100)`)
`+8` data, `+0xc` element size (4), `+0x10` grow-by (100), `+0x14` count, `+0x18` capacity. Each element is a 4-byte Tile `{int16 row, int16 col}`.
The layout is a 0-rooted heap with parent(i) = i>>1. The root (index 0) therefore has one child (index 1), and node i >= 1 has children 2i and 2i+1.

### 2.4 Direction tables
* `0x1002b28` int16[3][3], indexed [drow+1][dcol+1] = `7 0 1 / 6 0 2 / 5 4 3`. `FUN_01017531(a,b)` returns the direction of `b-a` from raw deltas (0x1017531-0x101755d).
* `0x1004950` {int32 drow, int32 dcol}[8] = (-1,0) (-1,1) (0,1) (1,1) (1,0) (1,-1) (0,-1) (-1,-1), i.e. N NE E SE S SW W NW. Used for path reconstruction (0x1019d8d/0x1019d95).

### 2.5 Tile info struct filled by FUN_0100f4ab(world, Tile* t, uint req, Info* out) [ret 0xc]
`memset(out,0,0x3c)` (0x100f4c5); `out+0 = *t`; `out+4 = req`.

| off | size | filled when req has | content | evidence |
|---|---|---|---|---|
| +0x00 | Tile | always | queried tile | 0x100f4d2 |
| +0x04 | dword | always | req flags | 0x100f4d9 |
| +0x08 | Ant* | 0x001 | occupant ant, or 0 (see below) | 0x100f511-0x100f588 |
| +0x0c | List* | 0x200 | new list of all occupant ants | 0x100f58c-0x100f62e |
| +0x10 | int16 | 0x002 | terrain class `FUN_01008af7(row,col)` | 0x100f633-0x100f64d |
| +0x12 | int16 | 0x004 | layer-1 tile id `FUN_01008b3d(1,..)` | 0x100f651-0x100f66d |
| +0x14 | int16 | 0x008 | layer-2 tile id `FUN_01008b3d(2,..)` (0x7ffe = out of bounds) | 0x100f671-0x100f68d |
| +0x18 | dword | 0x20/0x10/0x80 | bit0 = cell occupied (occ byte0 & 7 != 7, req 0x20); bit1 = anthill object (req 0x10); bit2 = food object (req 0x80) | 0x100f4fc, 0x100f805, 0x100f786 |
| +0x1c | dword | 0x040 | `FUN_0100cf0f`: 1 if out of bounds, else layer-1 cell word0 bit0 ("blocking map object" bit; set by object placement, e.g. 0x1007ad5-0x1007b23 copies tileinfo flag 1 into it) | 0x100f6d5-0x100f6ef, 0x100cf0f |
| +0x20 | int16 | 0x100 | layer-1 cell word1 (no bounds check) | 0x100f697-0x100f6b2 |
| +0x22 | int16 | 0x100 | layer-2 cell word1 (compared with ant team as bomb owner) | 0x100f6b6-0x100f6d1 |
| +0x24 | dword | 0x020 | occupancy "multiple ants" bit (occ byte1 bit0) | 0x100f50a |
| +0x28 | Tile | 0x010 | anchor (origin) tile of the anthill object (`FUN_010076b6`) | 0x100f7fc-0x100f818 |
| +0x2c | Tile | 0x010 | `player[hillTeam]` tile at +0x32/+0x34 (the raid tile a thief targets) | 0x100f821-0x100f842 |
| +0x30 | int16 | 0x010 | hillTeam | 0x100f837 |
| +0x34 | Tile | 0x080 | food object anchor tile | 0x100f795 |
| +0x38 | Obj* | 0x080 | food object (map food array map+0x30: data +0x38, stride +0x3c, count +0x44; the last object whose +8/+0xa equals the anchor wins) | 0x100f760-0x100f7a4 |

* **Occupancy grid** `[world+0x553c]` (row pointers, 2 bytes per cell). byte0 bits 0..2 = team (7 = empty), byte0 bits 3..7 = ant index, byte1 bit0 = multiple ants on the cell. For a single occupant the ant is `player[team].ants[index]`.
* **Multiple occupants** are resolved by `FUN_0100f2cd` (0x100f2cd-0x100f3c7). It walks every player's ants on the tile. The first one found becomes the chosen ant. A later ant replaces it if that ant is stationary (`+0xd8==0` and action in {0, 3..9, 0xb}) and either it is locally controlled or the current choice is not.
* **Anthill test (req 0x10).** The layer-2 id goes through `FUN_0100ecac` (0xf5->0, 0xf6->1, 0xf7->2, 0xf8->3, else 4) and then `FUN_0100f43c` (the team whose `player+0x2c` equals that colour, else 4).
* **Food test (req 0x80).** The layer-2 id must not be 0x7ffe and must have tileinfo flag 2 (`FUN_010071dd`).
* Other tileinfo predicates: `FUN_01007202` = flag 4 (powerup); `FUN_01008bc6` = layer-2 id in {0x81,0x82,0x83,0x84} (bomb).

### 2.6 Terrain tables
* **`WORD_010049b8` (destination walkability by terrain class):** `1,1,0,1,1,0,0,0`. Grass, sand, mud and dirt are walkable; water (class 2) is walkable only for a swimmer (0x101f7c1-0x101f7e1). Its only xref is 0x101f7c1.
* **`DWORD_010049c8` (step-cost weight by terrain class):** `20, 16, 8000, 48, 24, 40` for grass, sand, water, mud, dirt, and class 5 (unused). `FUN_010208e8` returns **21** for class 2 when `FUN_0100f9cb(0)==5` (swimmer) (0x10208e8-0x102090e).
* **`double 0x10049e0` = 1.4** (0x3ff6666666666666).

## 3. Pseudocode

Notation: `W` = world (`[0x104b350]`), `M` = `W+0x494c` map, `P(t)` = `W+0x4958+t*4` player, `LT` = `W+0xf2a` local team. Every Tile is `{int16 row, int16 col}`. Tile compares are two 16-bit compares.

### 3.1 Ant::GoTo (FUN_0101fc50, SEH frame; args t, userCmd, special, unused; ret 0x10)
```c
int Ant::GoTo(Tile* pT, int userCmd, int special, int unused) {
  Tile tile = *pT;                                            // 0x101fc64
  if (userCmd && !CanTakeUserOrder()) return 0;               // 0x101fc6e-0x101fc84 (FUN_0101ff5a)
  Info in; W->Query(&tile, 0x90, &in);                        // 0x101fc9d
  if (in.flags & 2) {                                         // anthill clicked
    if (in.hillTeam == this->team) tile = in.hillAnchor;      // +0x28   0x101fcb2
    else if (this->type /*raw +0x54*/ != 3) { StopSync(); return 0; } // 0x101fcb7 -> 0x101feee
    else tile = in.hillRaidTile;                              // +0x2c   0x101fcc2
  }
  Tile cur = this->TileFromPixel();                           // FUN_0100ccc0  0x101fcce
  SetAction(0);                                               // FUN_0101ace3(0) = FUN_0101ad02(0,+0xe0,-1,-1,0,0)
  vtbl[0x18](Centre(cur));                                    // snap to tile centre 0x101fcf6
  CancelPause(0);                                             // FUN_0101cc1e(0)
  InstallPath(0, NULL, 1, NULL);                              // FUN_0101ab87: clears path, order=0, target=(0x5a,0x78)
  CancelTask80();                                             // FUN_0101c152
  this->+0x98 = timeGetTime();                                // 0x101fd2d
  Tile orig = tile;                                           // bx / [ebp+0xa]
  ClassifyOrder(&tile, special, userCmd);                     // FUN_01020655 (NOTE arg order) 0x101fd48
  int ok = 1, homeFlag = 0;
  switch (this->order) {                                      // table 0x101ff2a, index order-3
   case 3:   if (AllyAttackConfirm()) return 0;               // FUN_0101ffab (dialog if target team is our ally)
             goto adjust;
   case 0xb: if (AllyAttackConfirm()) return 0; goto request;  // no goal adjustment
   case 6:   r = W->CanPlaceHere_d762(&tile,1); goto approach;
   case 7:   r = (Layer2(tile)==0x86);           goto approach;
   case 8:   r = W->CanPlaceHere_d762(&tile,1); goto approach;
   case 9:   r = IsBomb(Layer2(tile))  /*FUN_0101d7f9*/; goto approach;
   case 0xd: r = W->WaterSpot_d6d6(&tile,1);    goto approach;
   case 0xe: r = (Layer2(tile)==0x25);          goto approach;
   default /*0,1,2,4,5,0xa,0xc,>0xe*/: goto adjust;
  }
approach:                                                     // 0x101fe05
  if (r && ApproachTile(&tile)) { this->+0xb0 = orig; goto request; }   // +0xb0 dword = original tile
  SetAction(0xb); InstallPath(0,NULL,1,NULL); W->PlaySfx(0x30,0); ok = 0; goto finish;
adjust:                                                       // 0x101fe74
  ok = AdjustGoal(&tile, userCmd, 1, 1, 5);                   // FUN_010202e7
  if (tile != orig) {
    if (orig == P(this->team)->home /*+0x2e*/) homeFlag = 1;
    ClassifyOrder(&tile, 0, userCmd);                         // 0x101fecd
  }
finish:
request:                                                      // 0x101fed2
  if (homeFlag) { this->+0x68 = 1; this->+0x6c = userCmd; } else this->+0x68 = 0;
  if (!ok) { StopSync(); return 0; }                          // FUN_010214d9 (broadcasts a stop msg if local)
  PathMgr(W+0x4968)->Request(this->id /*+0x58*/, &cur, &tile);// FUN_010246e8  0x101ff11
  return 1;
}
```
`CanTakeUserOrder` (FUN_0101ff5a) returns true only if `FUN_0100f9cb(1)==FUN_0100f9cb(0)`, `vtbl[0x40]()==0`, `+0x84==0` and action is one of {0,1,3}.

### 3.2 Ant::ClassifyOrder (FUN_01020655(t, special, userCmd), ret 0xc)
```c
W->Query(t, 0xb9, &in);
if (in.flags & 2) { if (in.hillTeam==team) order=2; else { order=0xb; +0xb0=(int16)in.hillTeam; } }
else if (special) switch (FUN_0100f9cb(0)) {
   case 1 /*bomber*/:  order=8;   if(!W->d762(t,1) && W->d7f9(t)) order=9;        break;
   case 2 /*fire*/:    order=6;   if(!W->d762(t,1) && Layer2(t)==0x86) order=7;   break;
   case 5 /*swimmer*/: order=0xd; if(!W->d6d6(t,1) && Layer2(t)==0x25) order=0xe; break;
   default: /* order left unchanged */ ;
}
else if (in.flags & 1 /*occupied, in.occ*/) {
   if (in.occ->team == team || !userCmd) order = 1;
   else { order = 3; +0xb0 = in.occ->team; +0xb2 = in.occ->+0x58; }
}
else if (in.flags & 4) { order=5; +0xb0 = in.food; +0xb4 = in.food->tile(+8,+0xa); }
else if (IsPowerup(in.layer2) && userCmd) order = 4;
else if (IsBomb(in.layer2)) order = 0xa;
else order = 1;
this->+0xac = *t;                                             // 0x102089e
```
Order meanings, as far as this code shows: 1 move, 2 go home, 3 attack ant, 4 get powerup, 5 harvest food, 6/7 fire-ant actions, 8/9 bomber actions, 0xa go to bomb, 0xb raid enemy hill (thief), 0xd/0xe swimmer actions, 0xf lunge (FUN_0101da6f).

### 3.3 Ant::ApproachTile (FUN_01020128(t), ret 4): the best of the 4 orthogonal neighbours
```c
Tile cur = TileFromPixel();
Tile c[4] = {{r-1,c},{r+1,c},{r,c-1},{r,c+1}};                // N, S, W, E  (0x1020155..0x102025b)
uint cost[4];
for k in 0..3:
  cost[k] = ((uint16)c[k].row < M->rows && (uint16)c[k].col < M->cols && IsValidDest(&c[k], 0x81, NULL))
            ? Cheb16(&cur, &c[k]) : 8000;
best = 0; bc = 8000;
for k in 0..3: if (cost[k] < bc) { bc = cost[k]; best = k; }  // strict <, ties keep the earlier of N,S,W,E (0x10202b5)
if (bc == 8000) return 0;
*t = c[best]; return 1;
```

### 3.4 Ant::AdjustGoal (FUN_010202e7(t, a2=userCmd, a3, a4, radius); called with (t,userCmd,1,1,5)); ret 0x14
```c
Tile home = P(team)->+0x2e;
uint f100 = (order==3 && +0xac == *t) ? 0x100 : 0;
uint flags = (a2?0x20:0)|(a3?1:0)|(a4?2:0)|(a2?8:0)|(a2?0x10:0)|((*t==home)?4:0)|f100;   // 0x1020362-0x10203a7
if (IsValidDest(t, flags, NULL)) return 1;
if (*t == home) {                                              // 0x10203b9
   Tile save = *t; *t = P(team)->+0x46;                        // alternate home tile
   int r = AdjustGoal(t, a2, a3, a4, radius);
   if (!r) *t = save;  return r;
}
flags &= 0xffd3;                                               // drop 0x20, 0x08, 0x04   0x1020419
int r0 = t->row, c0 = t->col;
for (int d = 1; d < radius; d++) {                             // d = 1..4 (word compares)  0x102041f / 0x1020638
  // side 1: west column, top -> bottom (skipped entirely if c0-d < 0)
  if (c0-d >= 0) for (row = max(0,r0-d); row <= r0+d; row++) TRY(row, c0-d);
  // side 2: east column, top -> bottom
  for (row = max(0,r0-d); row <= r0+d; row++) TRY(row, c0+d);
  // side 3: north row, left -> right, corners excluded (skipped entirely if r0-d < 0)
  if (r0-d >= 0) for (col = max(0,c0-d+1); col <= c0+d-1; col++) TRY(r0-d, col);
  // side 4: south row, left -> right, corners excluded
  for (col = max(0,c0-d+1); col <= c0+d-1; col++) TRY(r0+d, col);
}
return 0;                                                      // *t unchanged
TRY(r,c): if ((uint16)r < M->rows && (uint16)c < M->cols && IsValidDest(&{r,c}, flags, NULL)) { *t={r,c}; return 1; }
```
Evidence: side 1 is 0x102043b-0x10204bc, side 2 is 0x10204be-0x1020543, side 3 is 0x1020545-0x10205c1, side 4 is 0x10205c6-0x102062f. The first tile that passes wins. There is no distance sort inside a ring.

### 3.5 Ant::IsValidDest (FUN_0101f780(t, flags, Info* optional), ret 0xc)
```c
Info* in = (arg && arg->req == 0x1db) ? arg : (W->Query(t, 0x1db, &local), &local);   // 0x101f790-0x101f7b4
if (WORD_010049b8[in->terrain] == 0 && !(in->terrain == 2 && FUN_0100f9cb(0) == 5)) return 0;
Ant* o = in->occ;
if (o && !(flags & 0x40)) {                                   // 0x101f7e7
  bool gotoEnemyCheck;
  if (o != this && o->team == team) gotoEnemyCheck = false;   // same-team other ant -> 0x101f815
  else gotoEnemyCheck = IsLocallyControlled(this);            // FUN_0100cd7d = (W+0x4ae4==0 && team==LT)
  if (!gotoEnemyCheck) {                                      // 0x101f815
    if (!(flags & 0x80)) return 0;
    if (!o->path /*+0xd8*/) { a = o->action; if (a==0 || (a>=3&&a<=9) || a==0xb) return 0; }  // a<=2 signed passes
  }
  if (IsLocallyControlled(this) && o != this && o->team != team) {          // 0x101f84f
    if (!(order==3 && (int16)+0xb0==o->team && +0xb2==o->+0x58)) return 0;  // only our attack target
  }
}
if (in->flags & 2) {                                           // anthill      0x101f896
  if (!(order==0xb && (int16)+0xb0==in->hillTeam && *t==in->hillRaidTile)) {
    if (in->hillTeam != team) return 0;
    if (!(flags & 4)) return 0;
    if ((flags & 1) && *t != in->hillAnchor) return 0;
  }
} else if (in->objBit /*+0x1c*/) {                             // blocking object  0x101f913
  if ((flags & 8) && IsPowerup(in->layer2)) return 1;
  if (in->layer2 == 0x86 && FUN_0100f9cb(0) == 2) return 1;
  if ((in->flags & 4) && order==5 && +0xb0 == in->food) return 1;
  return 0;
} else if (!(flags & 0x100)) {                                 // player "special tiles"  0x101f98a
  for (tm = 0; tm < 4; tm++) {
    p = P(tm); if (!p || p->+0x64) continue;
    Tile A=p->+0x36, B=p->+0x3a, C=p->+0x3e;
    if (*t != A && *t != B && *t != C) continue;
    if (tm != team) return 0;
    if (!(flags & 1) || !(flags & 0x10)) continue;
    int16 nA = Occupied(A), nB = Occupied(B), nC = Occupied(C);   // FUN_0100f3ca (info 0x20 bit0)
    for (i = 0; i < p->antCount; i++) { a = p->ant(i); if (!a || a==this || a->order!=1) continue;
        if (a->+0xac==A) nA++; if (a->+0xac==B) nB++; if (a->+0xac==C) nC++; }
    if ((uint)(nA + nB + nC) == 2) return 0;                   // exactly 2 (0x101fb18)
  }
}
if (flags & 2) {                                               // reservations   0x101fb35
  p = P(team);
  for (i = 0; i < p->antCount; i++) { a = p->ant(i); if (!a || a==this) continue;
    if ((a->order==1 || a->order==2) && a->+0xac == *t) return 0;
    if (*t == p->+0x2e && this->+0x68 != 2 && a->+0x68 == 2) return 0; }
}
if (flags & 0x20) return 1;
if (IsBomb(in->layer2)) { if (in->l2word1 == team) return 0; if (P(LT)->+0x68 /*ally*/ == in->l2word1) return 0; }
return 1;
```
Flag meanings:
* 0x001: an anthill destination must be the anchor tile, and it enables the special-tile count check (together with 0x10).
* 0x002: reject tiles already targeted by other own ants with order 1/2, plus the home-entrance rule.
* 0x004: allow own anthill tiles.
* 0x008: allow powerup objects.
* 0x010: special-tile count check.
* 0x020: skip the own/ally bomb rejection.
* 0x040: ignore occupant ants.
* 0x080: allow same-team moving ants.
* 0x100: skip the special-tile section.

Flags used by each caller:
* GoTo goal (userCmd=1): `0x3B` (+4 when the tile is home, +0x100 when it is the attack tile).
* GoTo goal (userCmd=0): `0x03` (+4 / +0x100 in the same cases).
* Ring search: those flags `& 0xffd3`.
* ApproachTile: `0x81`.
* Runtime next-tile check: 0/4/8/0x20 (§3.12).

### 3.6 PathMgr::Request (FUN_010246e8(antId, Tile* from, Tile* to), ret 0xc)
```c
// remove the FIRST queued request with the same ant id (0x10246f8-0x102473b)
for (r = list.First(); r; r = list.Next(nowrap)) if (r->antId == antId) { list.RemoveCurrent(); break; }
req = new PathRequest(antId, from, to);   // 0x34 bytes; ctor runs Init() immediately
list.AppendTail(req);                     // 0x1024769
```

### 3.7 PathRequest::Init (FUN_010198d1)
```c
for (slot = 0; slot < 4 && !pool[slot].free; slot++);         // 0x10198e5-0x1019902
if (slot == 4) return;                                        // no grid now; Step() retries
if (!pool[slot].grid) pool[slot].grid = Alloc2D(rows, cols, 4);// FUN_01006030 (row-pointer array + one block)
grid = pool[slot].grid; pool[slot].free = 0;
memset(grid[0], 0, rows*cols*4);                              // FUN_0100607b
heap = new Heap(4, 100);
grid[start] |= 0x40000000;                                    // 0x1019995
grid[start] = (grid[start] & 0xf8003fff) | ((Cheb16(start, goal) & 0x1fff) << 14);
heap.Push(start, grid);                                       // g(start)=0
```

### 3.8 PathRequest::Step (FUN_01019a66(uint16 budget), ret 4): A*
```c
int Step(uint16 budget) {
  if (!grid) { Init(); if (!grid) return 0; }                   // 0x1019a73-0x1019a84
  uint16 it = 0;
  if (heap->count == 0) goto exhausted;
  while (it < budget) {                                         // 0x1019a9d
    it++;
    Tile cur = heap.Pop(grid);
    dword* cc = &grid[cur.row][cur.col];
    if ((*cc & 0x3fff) + ((*cc >> 14) & 0x1fff) >= 8000) goto fail;    // 0x1019ae0 (checked BEFORE goal test)
    if (cur == goal) { BuildPath(); goto done; }                 // 0x1019aec -> FUN_01019d28
    Tile nb[8]; Neighbours(&cur, nb);                            // FUN_01019c31
    for (k = 0; k < 8; k++) {
      if (nb[k].row == 0x5a) continue;                           // sentinel (0x1019b23)
      uint newg = ant->StepCost(&cur, &nb[k]) + (*cc & 0x3fff);  // FUN_01020951(this=req+0xc)
      dword* nc = &grid[nb[k].row][nb[k].col];
      if ((*nc & 0xc0000000) && (*nc & 0x3fff) <= newg) continue;   // 0x1019b5e-0x1019b6c
      *nc &= 0x7fffffff;                                         // re-open (clear closed)
      *nc = (*nc & ~0x3fff) | (newg & 0x3fff);
      *nc = (*nc & 0xf8003fff) | ((Cheb16(&nb[k], &goal) & 0x1fff) << 14);
      *nc = (*nc & 0xc7ffffff) | ((Dir(&nb[k], &cur) & 7) << 27);   // FUN_01017531(nb,cur): nb->parent
      if (!(*nc & 0x40000000)) { *nc |= 0x40000000; heap.Push(nb[k], grid); }  // pushed at most once
    }
    *cc |= 0x80000000;                                           // closed   0x1019bf2
    if (heap->count == 0) break;
  }
exhausted:                                                       // 0x1019c0d
  if (heap->count != 0) return 1;                                // budget used up, heap kept
fail:     count = 0;                                             // 0x1019c16
done:     delete heap; heap = NULL; return 1;
}
```
Important consequences (all follow from the code above):
* Impassable edges (cost 8000) are still pushed with g >= 8000. They act as a terminator: the search fails when such a node reaches the top of the heap.
* When an already-open node gets a better g, it is **not** re-heapified (no decrease-key and no re-push), so the heap order can go stale. The heap compares read the current grid f, so this must be replicated exactly.
* A closed node that gets a better g is updated in place (g, h, parent) and has its closed bit cleared, but it is never re-pushed.
* There is no node limit other than the 1000-expansions-per-slice budget and the f < 8000 cutoff.

### 3.9 Neighbours (FUN_01019c31(Tile* cur, Tile out[8]), ret 8)
```c
out[0]={r-1,c}  out[1]={r-1,c+1}  out[2]={r,c+1}  out[3]={r+1,c+1}
out[4]={r+1,c}  out[5]={r+1,c-1}  out[6]={r,c-1}  out[7]={r-1,c-1}     // 0x1019c56-0x1019cc5
if (r == 0)            out[1].row = out[0].row = out[7].row = 0x5a;
else if (r == rows-1)  out[3].row = out[4].row = out[5].row = 0x5a;     // rows = req+0x2c
if (c == 0)            out[5].row = out[6].row = out[7].row = 0x5a;
else if (c == cols-1)  out[3].row = out[2].row = out[1].row = 0x5a;     // cols = req+0x2e
```
Quirk: the sentinel test only checks `row == 90`. A real tile in row 90 would never be expanded.

### 3.10 Heap Push (FUN_01019e20(row, col, grid), ret 0xc) and Pop (FUN_01019f0a(out, grid), ret 8)
```c
Push(t): SetSize(n+1); i = n; data[i] = t; key = f(t);           // n = old count
  while (i != 0) { p = i >> 1; if (f(data[p]) <= key) break;     // unsigned; ties stop
                   swap(data[p], data[i]); i = p; }
Pop():   top = data[0]; data[0] = data[n-1]; RemoveAt(n-1);      // n = old count
  key = f(data[0]); i = 0; c = 0;
  if (count >= 1) for (;;) {                                     // count = new count
     best = c; fb = f(data[c]);
     if (c+2 <= count && f(data[c+1]) < fb) { best = c+1; fb = f(data[c+1]); }   // strict <: ties keep left
     if (key <= fb) break;
     swap(data[i], data[best]); i = best; c = 2*best;
     if (c+1 > count) break;                                     // loop while c < count
  }
  return top;
```
Note the first iteration: with c=0 it compares data[0] against data[1], so the root's only child is index 1. After that the children of i are 2i and 2i+1.

### 3.11 Step cost (Ant::StepCost, FUN_01020951(Tile* from, Tile* to), ret 8)
```c
uint StepCost(Tile* from, Tile* to) {
  Info in; W->Query(to, 0x1db, &in);
  bool hill = in.flags & 2;
  if (hill && order==0xb && (int16)+0xb0==in.hillTeam && *to==in.hillRaidTile) goto terrain;   // 0x1020977-0x10209a3
  if (Ant* o = in.occ) {
    if (o->team == LT) {                                         // friendly (local team)
      if (o->+0x60 /*waiting*/) return 8000;
      if (!o->path) { a=o->action; if ((a==0 || (a>=3&&a<=9) || a==0xb) && (uint16)o->+0xac >= 0x5a) return 8000; }
    } else if (!(order==3 && (int16)+0xb0==o->team && +0xb2==o->+0x58)) return 8000;
  }
  if (hill) {                                                    // 0x1020a29
    if (in.hillTeam != LT) return 8000;
    if (order != 2 && *from != P(team)->+0x2e) return 8000;      // can only enter the hill from the entrance tile
  }
  if (!(order==3 && +0xac==*to)) { t = SpecialTileOwner(to); if (t!=4 && t!=LT) return 8000; }  // FUN_01020c38
  if (in.objBit) {                                               // 0x1020abc
    if (!( (+0xac==*to && IsPowerup(in.layer2) && order==4)
        || ((in.flags&4) && order==5 && +0xb0==in.food)
        || (+0xac==*to && order==0xb)
        || (in.layer2==0x86 && FUN_0100f9cb(0)==2) )) return 8000;
  }
  if (!(order==0xa && +0xac==*to) && IsBomb(in.layer2)) {        // 0x1020b59
    if (in.l2word1 == team) return 8000;
    if (P(LT)->+0x68 == in.l2word1) return 8000;                 // ally's bomb
  }
terrain:                                                         // 0x1020bbe/0x1020bc4
  uint ca = TerrCost(TerrainClass(from->row, from->col));        // FUN_01008af7 then FUN_010208e8
  uint cb = TerrCost(in.terrain);
  if (ca == 8000 || cb == 8000) return 8000;
  uint s = ca + cb;
  if (from->row != to->row && from->col != to->col) s = (uint)trunc((double)s * 1.4);  // fild/fmul/__ftol 0x1020c21-0x1020c2a
  return s >> 1;
}
SpecialTileOwner(t) = first team tm in 0..3 with P(tm) && !P(tm)->+0x64 && t in {+0x36,+0x3a,+0x3e}; else 4.  // FUN_01020c38 / FUN_0101d8a4
Cheb16(a,b) = max(|a.row-b.row|, |a.col-b.col|) << 4;                         // FUN_01020911
```
**FPU precision.** The CRT initialises the x87 control word with `_controlfp(0x10000 /*_PC_53*/, 0x30000)` (0x103451f -> 0x1038400 -> 0x103bcf0). Nothing else writes the control word: the only `fldcw` instructions are in `__ftol` and `_controlfp`, and the game uses DirectDraw only, no Direct3D. So the diagonal multiply is an IEEE double multiply rounded to nearest, then truncated.
**This matters for grass-grass:** 40*1.4 rounds exactly to 56.0, giving 28. With 64-bit precision it would be 55, giving 27. Use a lookup table:

| terrain pair | orth | diag |  | terrain pair | orth | diag |
|---|---|---|---|---|---|---|
| grass-grass | 20 | 28 | | sand-dirt | 20 | 28 |
| grass-sand | 18 | 25 | | mud-mud | 48 | 67 |
| grass-mud | 34 | 47 | | mud-dirt | 36 | 50 |
| grass-dirt | 22 | 30 | | dirt-dirt | 24 | 33 |
| sand-sand | 16 | 22 | | water(swim)-water | 21 | 29 |
| sand-mud | 32 | 44 | | water-grass / sand / mud / dirt | 20/18/34/22 | 28/25/48/31 |

Water for a non-swimmer is 8000 (impassable). Bridges (layer-2 0x22..0x25) count as class 3 (mud) through FUN_01008af7.

### 3.12 Path reconstruction (FUN_01019d28)
```c
for (pass = 1; pass <= 2; pass++) {
  Tile t = goal; uint16 n = 0;
  for (;;) {
    if (pass == 2) path[count - n - 1] = t;
    n++;
    if (t == start) break;
    d = (grid[t.row][t.col] >> 27) & 7;
    t.row += dtab[d].drow; t.col += dtab[d].dcol;       // table 0x1004950
  }
  if (pass == 1) { count = n; path = new Tile[n]; }
}   // path[0] = start ... path[count-1] = goal; start==goal gives count 1
```

### 3.13 PATHMGR task
* **Creation:** ctor 0x102463a (name string 0x10474a8 "PATHMGR", vtable 0x1004e28, list at +0x2c, grid pool reset). It is created at 0x100e4f5, stored at `W+0x4968`, and scheduled with `W->AddTask(task, 0, 50, 0)` (FUN_01031e92 -> task manager `[W+0xe88]` vtbl+0xc = 0x1030e7b).
* **Scheduling:** the task's `+0x1c` is set to 0 for the first insert (due now) and then to 50 (period).
* **Scheduler:** `RunOne` (0x10310e8) runs one due task per call: if `timeGetTime() >= head.due`, it runs vtbl+0xc and, when that returns 1, reinserts the task with due = now+50. It is called from the main loop (0x103197d) whenever `PeekMessage` finds no message.
* **Run (0x1024786):**
```c
int PathMgr::Run() {
  if (!list.head) return 1;
  for (;;) {
    list.cursor = list.head; PathRequest* r = list.RemoveCurrent();       // 0x1024798-0x10247a5
    int stop = 0;
    if (r->Step(1000)) {                                                 // 0x10247b3
      stop = 1;                                                          // set before the heap test (0x10247c0 push1/pop ebx)
      if (r->heap == NULL) W->DeliverPath(r->antId, r->path, r->count);  // 0x10247e2
      else list.AppendTail(r);                                           // unfinished: back of queue
    } else list.AppendTail(r);                                           // no grid free: rotate and try next
    release(r);
    if (stop) return 1;
  }
}
```
So each 50 ms tick gives exactly one request (one that has a grid) up to 1000 expansions. A group move of N ants therefore delivers about one path per 50 ms, in the order the requests were queued (the group command sorts ants nearest-first).

### 3.14 Delivery and installation
* **FUN_0100cba4 World::DeliverPath(antId, Tile* path, uint16 count):**
```c
ant = P(LT)->ant(antId); if (!ant) return;
if (count == 0) { if (W->+0x4ae4==0 && LT==ant->team) { ant->StopSync(); ant->SetAction(0xb); W->PlaySfx(0x3a,0); } return; }
if (ant->action != 0 && ant->action != 3) return;          // path silently dropped
if (ant->TileFromPixel() != path[0]) return;              // path silently dropped
msg = MakeMsg6(LT, antId, Centre(ant tile), ant, count, path); W->Send(msg, 0x0a, 0);   // local handler runs now
```
* **Message type 6 (FUN_010228ff, size 0x26 + 4*count):** +8 team, +0xa antId, +0xc/+0x10 centre x/y, +0x14 dir (+0xe0), +0x18 action (+0xe4), +0x1c order (+0xa8), +0x20 food tile (+0xb4, order 5 only), +0x24 = 2*count, +0x26 words (row,col)*count.
* **Handler 0x10229b7:** returns if the ant is missing or its action is 0xc or 0xf. Otherwise it runs `CancelPause(0)` and `vtbl[0x18](centre)`, then `FUN_0101ad02(msg.action, msg.dir, 0xffff, 0xffff, 0, 1)`. If the action is still not 0xc/0xf, it calls `FUN_0101ab87(count, path, msg.order, &msg.foodTile)` and then frees its temporary array.
* **FUN_0101ab87 InstallPath(count, path, order, pExtra):**
```c
ClearPath(); order=0; +0xac=(0x5a,0x78);
if (count) { +0xdc=count; +0xd8=new Tile[count]; memcpy; +0xde=0; +0xa8=order; +0xac=path[count-1];
             if (order==5) { +0xb4=*pExtra; +0xb0=M->FoodAt(*pExtra) /*FUN_01008c63*/; } }
```
`FUN_0101ab87` is called with count > 0 only from the type-6 handler (0x1022ad8). Every other call passes count 0 (a clear).

### 3.15 Second path producer: FUN_0101da6f (lunge toward an enemy tile, order 0xf)
```c
cur = TileFromPixel(); dir = FUN_01017560(&cur, enemyTile) /*sign-based*/; enemy = OccupantAt(enemyTile);
+0xbc=1; SetAction(0); ClearPath(); path[0]=cur; nxt=cur; StepDir(&nxt, dir, 1) /*FUN_0101d9f7*/; path[1]=nxt;
fl = (nxt==*enemyTile) ? 0x140 : 0x40;
if (!IsValidDest(&nxt, fl, NULL)) { FUN_0101dd6f(); return; }
order=0xf; +0xac=nxt; +0xb0=enemy->team; +0xb2=enemy->+0x58; W->DeliverPath(id, path, 2);
```

### 3.16 Runtime blocked-waypoint handling (FUN_0101c4f2(Tile* next)); summary. Returns 1 = proceed or wait, 0 = stopped or re-planned.
* **Attack branch.** For a local ant with order 3/0xf whose target stands on `next`, it goes to combat setup instead of the walk logic (0x101c563-0x101c671; this belongs to the combat cluster).
* **Walk check flags:** 8 if order==4 and next==+0xac; |0x20 if order==0xa and next==+0xac; |4 if order==2, or cur==home, or cur==(home.row-1, home.col). If `IsValidDest(next, flags, &infoN)` passes, return 1.
* `isFinal = (next == path[count-1])`.
* **Locally controlled ant (0x101c8b4):**
  * If next is not final and its occupant is a moving ant (action 1, 2, 10, or >= 0xc; tested at 0x101c8c4-0x101c8db): wait. This means `CancelPause(1)` (the 300 ms ANTPAUSE), then `SetAction(0)`, and return 1.
  * If next is final and order==0xb: wait.
  * Otherwise re-plan: `SetAction(0)`, `ClearPath()`, `+0xac`=sentinel, `+0x68`=0.
    * The order is re-issued only if next is not final, or the order is one of {3,6,7,8,9,0xd,0xe}.
    * Re-issue calls: order 5 `GoTo(&+0xb4,0,0,0)`; orders 6/7/8/9/0xd/0xe `GoTo(&+0xb0,0,1,0)`; order 4 `GoTo(&goal,1,0,0)`; order 3 `GoTo(&target->tile or &goal,1,0,0)`; any other order `GoTo(&goal,0,0,0)`.
    * After a re-issue it spawns the "bump" effect (CHD Table-4 anim 0xdc) at next (FUN_010100e5) and returns 0.
    * If the order is not re-issued: StopSync, and set +0x68=2 if it was 1. Return 0.
* **Remote ant:** it never re-plans. It waits on water (non-swimmers), waits if a moving ant occupies next, and otherwise proceeds.

## 4. Re-implementation checklist (1:1)
1. **Grid cell:** use 32-bit cells laid out as in §2.2, and a heap whose comparisons read f from the grid live. Do not add decrease-key.
2. **Neighbours:** order 0..7 = N, NE, E, SE, S, SW, W, NW. Out-of-bounds neighbours are skipped. No corner-cutting checks.
3. **Costs:** use the table in §3.11 (half the sum of the per-class weights; diagonal ×1.4 truncated in double, before halving). Impassable = 8000. Stop with failure when a popped f is >= 8000.
4. **Heuristic:** `16*Chebyshev`. Check the goal only after the f >= 8000 test.
5. **Path:** includes start and goal. The waypoint index starts at 0.
6. **Budget and scheduling:** 1000 expansions per PATHMGR tick; one request per 50 ms tick; FIFO round robin. A new request for the same ant replaces its pending one. At most 4 concurrent grids.
7. **Goal adjustment:** the ring search in §3.4 with the flags in §3.5. For orders 6-9/0xd/0xe use the approach tile (§3.3). For order 0xb there is no adjustment.
8. **Delivery:** drop the path if the ant has left `path[0]` or its action is not 0 or 3. Count 0 gives StopSync + action 0xb + sfx 0x3a.

## 5. Open questions (not resolved from the binary in this pass)
* **Player special tiles** `+0x36/+0x3a/+0x3e`: what they represent in-game, and why the "== 2" occupancy rule (0x101fb18) exists. The rule itself is exact.
* **Other player tiles:** `+0x32` (raid tile), `+0x46` (alternate home tile), `+0x42` (target of a GoTo at 0x101f18a). Only their uses are known.
* **Ant `+0x68` (0/1/2 "home" flag):** its complete lifecycle outside GoTo / FUN_0101c4f2.
* **Sound ids 0x3a (no path) and 0x30 (bad approach / blocked attack):** passed to FUN_0100e8f5. Mapping them to sample names was not done.
* **Combat branch of FUN_0101c4f2** (0x101c563-0x101c671, message FUN_01022c57): belongs to the combat cluster.
* **Sentinel collision:** the A* sentinel is `row == 90`. Does any shipped map have more than 90 rows (map+0xd0)? If one does, row 90 is unreachable in the original.
* **Layer-2 cell word1:** object placement (FUN_01007a22) writes anchor row/col bytes into it, but bomb checks compare it with a team id. Bomb placement code was not examined.

---

## Adversarial verification

An independent second pass re-derived every claim above from the Capstone disassembly and recorded a verdict per claim.

# Adversarial verification of re_pathfinder.md (cluster "pathfinder")

I re-derived every claim from Capstone disassembly of Original-Ants/Ants.exe (cs.py / xref.py / findptr.py). I did not rely on Ghidra.

## Verdict per claim

| # | Claim | Verdict | Key evidence (re-checked) |
|---|---|---|---|
| 1 | A* on an 8-connected grid, deferred to PATHMGR | CONFIRMED | 0x101ff11 call 0x10246e8 is the only caller of Request. 0x1019a66 (Step) has a single caller, 0x10247b3 (PathMgr::Run). The PathRequest ctor 0x10197ed only runs Init: it allocates the grid, sets bit 30 and h on the start cell, and pushes the start node. It never steps. |
| 2 | 50 ms, one gridded request per run, 1000 budget, round robin | CONFIRMED | 0x100e4fe-0x100e50f AddTask(task,0,0x32,0). In 0x1030e7b the first insert has +0x1c=arg2=0 (due now) and then +0x1c=50. RunOne 0x10310e8 runs vtbl+0xc (=0x1024786, vtable 0x1004e28 slot 3); a return of 1 reinserts it with due = timeGetTime()+50, measured AFTER the run (0x10311b1). Run: `push 0x3e8` at 0x10247ac, `push 1/pop ebx` at 0x10247c0 before `je`, AppendTail at 0x10247c8. A return of 0 (no grid) rotates to the next request in the same run. |
| 3 | Cell layout g/h/dir/open/closed | CONFIRMED | 0x1019ad2-0x1019ade, 0x1019b6e-0x1019b7a (xor/and/xor merge of the low 14 bits), 0x1019b8e-0x1019ba6, 0x1019ba8 FUN_01017531(&nb,&cur) = direction nb->cur (0x1017531 computes b-a), 0x1019bbd-0x1019bca, 0x1019bf2. |
| 4 | Heap: parent=i>>1, ties, no decrease-key | CONFIRMED | Push 0x1019e8c `shr dx,1`, 0x1019ec5 `jbe` (stop when parent<=key, unsigned). Pop: 0x1019fb0 `c+2 > count -> skip right`, 0x1019fef `jae` keeps the left child, 0x1019ffc `jbe` stops, 0x101a032 c=2*best, 0x101a041 loops while c+1<=count. Step pushes only when bit 30 is clear. |
| 5 | Neighbour order N..NW, sentinel row 0x5a, no corner check | CONFIRMED | 0x1019c56-0x1019cc5 slot mapping re-derived exactly. The boundary writes touch only the row word. StepCost reads only the `to` Info plus the `from` terrain and the `from`==home test. |
| 6 | Step cost formula / table / 8000 | CONFIRMED | Table 0x10049c8 = 20,16,8000,48,24,40. 0x10208e8 returns 0x15 for class 2 when FUN_0100f9cb(0)==5. 0x1020bf7-0x1020bfd: 8000 if either end costs 8000. 0x1020c08-0x1020c18: diagonal only if both row and col differ. `fild qword` (high dword 0), `fmul [0x10049e0]`=1.4, __ftol 0x1034580, `shr eax,1`. I recomputed every table entry in IEEE double and all of them match the report. |
| 7 | 53-bit x87, grass diag = 28 | CONFIRMED | 0x103451f -> 0x1038400 pushes (0x10000,0x30000) -> 0x103bcf0 -> _control87. Scanning all of .text finds only 3 fldcw (0x1034596, 0x103459c, 0x103bce1), and no fninit/frstor/fldenv. Imports: DDRAW DirectDrawCreate and DSOUND only. The only dynamic LoadLibrary string is user32.dll. 40*1.4(double) is an exact tie at 53 bits (7881299347898367.5 ulp) and rounds to even = 56.0, giving 28. At 64 bits it would give 55, i.e. 27. |
| 8 | Heuristic 16*Chebyshev, admissible | CONFIRMED | 0x1020911: abs 0x1034f20, `cmp bx,ax / jbe` (unsigned max), `shl eax,4`. The minimum orthogonal step is 16 and the minimum diagonal is 22, so h is also consistent. |
| 9 | Fail on f>=8000 before goal test, no fallback, 0xb + sfx 0x3a | CONFIRMED | 0x1019ae0 `cmp edx,0x1f40 / jae 0x1019c16` precedes 0x1019aec. The heap-empty path is 0x1019bf6 -> 0x1019c0d -> 0x1019c16. DeliverPath 0x100cbe7-0x100cc24 acts only if W+0x4ae4==0 and LT==ant team. |
| 10 | Waypoints include start and goal; +0xde=0; +0xac=path[count-1] | CONFIRMED | 0x1019d55-0x1019d65 writes `[edi+edx*4-4]`, edx=count-n. 0x100cc43-0x100cc5b. 0x101ac09 and 0x101ac17/0x101ac21. |
| 11 | Ring goal search order, radius 5, home -> +0x46 | CONFIRMED | Flags at 0x1020362-0x10203a7. Home recursion at 0x10203b9-0x102040d (args t,a2,a3,a4,radius; t restored on failure; no ring around home itself). `and esi,0xffd3` at 0x1020419. Side 1 is skipped when c0-d<0 (js 0x1020447). Side 3 is skipped when r0-d<0 (jl 0x1020549). Starts clamp to 0. Rows use signed jle, map bounds use unsigned word compares. d loop at 0x1020631-0x102063c (jb). |
| 12 | Approach tile for 6-9/0xd/0xe; 0xb unadjusted | CONFIRMED | Jump table 0x101ff2a dumped: 3->0x101fe51, 4/5/0xa/0xc->0x101fe74, 6->0x101fd6f, 7->0x101fd84, 8->0x101fde4, 9->0x101fdf6, 0xb->0x101fe61, 0xd->0x101fdab, 0xe->0x101fdbd. The candidates are the target's N,S,W,E. IsValidDest(0x81). Cheb16(antTile, cand). Strict `jae` skip at 0x10202b9. The approach runs only if the per-order precondition r is true; otherwise SetAction(0xb) + sfx 0x30 + StopSync. |
| 13 | AdjustGoal flag bits and IsValidDest flag meanings | CONFIRMED (nuances below) | 0x1020362-0x10203a7 and 0x101f780-0x101fc21 were fully re-read. |
| 14 | Info struct layout (FUN_0100f4ab) | CONFIRMED | memset 0x3c at 0x100f4c5. Every field offset/req bit matches: 0x20 -> +0x18 bit0/+0x24, 1 -> +8, 0x200 -> +0xc, 2 -> +0x10, 4 -> +0x12, 8 -> +0x14, 0x100 -> +0x20/+0x22, 0x40 -> +0x1c (0x100cf0f returns 1 out of bounds, else byte0&1), 0x80 -> +0x18 bit2/+0x34/+0x38, 0x10 -> +0x18 bit1/+0x28/+0x2c/+0x30. |
| 15 | WORD_010049b8 = 1,1,0,1,1,0,0,0, single reader | CONFIRMED | Dump confirmed. xref finds only 0x101f7c1. |
| 16 | StepCost impassability rules | CORRECTED (nuances) | See below. |
| 17 | Msg 6 install path | CONFIRMED | 0x10228ff layout. Dispatch 0x1047350=0x10229b7. The handler drops on action 0xc/0xf before and after FUN_0101ad02(action,dir,0xffff,0xffff,0,1). Of the 8 FUN_0101ab87 xrefs, the 7 others all push count=0 (a clear). FUN_0100d791 with flags 0xa calls the handler synchronously (0x100d965) and then network-sends (bit 8). |
| 18 | Grid pool of 4; Request replaces the first same-ant request | CONFIRMED | 0x10197b5 sets 4x{0,1}. Init 0x10198e3-0x101994f. dtor 0x1019a42. Request 0x10246f8-0x102473b removes the first match only, then creates and AppendTails the new request. |
| 19 | Runtime blocked-waypoint FUN_0101c4f2 | CORRECTED (conditions incomplete) | See below. |

## Corrections

### StepCost (0x1020951)
* "Friendly" and "enemy" are judged against the LOCAL team LT = [W+0xf2a], not the ant's alliance. Any occupant whose team != LT (allies included) returns 8000 unless it is exactly the attack target: order 3, word +0xb0 == o.team, +0xb2 == o+0x58 (0x10209fe-0x1020a23).
* The friendly-stationary rule tests `(uint16)o->+0xac(row) >= 0x5a` (0x10209ef `jb`), not equality with the sentinel. A stationary friendly with a real +0xac target does NOT block A*.
* The raid shortcut (0x1020977-0x10209a3) jumps straight to the terrain cost and skips the occupant, special-tile, object and bomb tests. It applies when the tile is a hill, order==0xb, (int16)+0xb0==hillTeam and to==raidTile.
* The bomb test is skipped entirely when order==0xa and +0xac==to (0x1020b59-0x1020b7f jumps to the terrain cost).

### FUN_0101c4f2
* The 300 ms wait for a local ant happens only when `next` is NOT the final waypoint and its occupant (FUN_0100f421) has action 1, 2, 0xa or >=0xc (signed `jle 2`, so negative actions also wait). It also waits when next IS final and order==0xb (0x101c8e1). If next is final and occupied by a mover and the order is not 0xb, the ant re-plans (orders 3/6-9/0xd/0xe) or stops.
* FUN_0101cc1e(1) does nothing when +0x60 is already set, so it does not restart the timer. The ANTPAUSE is created with AddTask(task,0,300,0) via 0x103057b. The generic one-shot runner 0x10305a8 first runs immediately (vtbl+0x10 = ret stub 0x1010f11), reschedules +300 ms, and then calls 0x1024d2b. That callback re-pauses if FUN_0101cbcc holds (order 0xb/3/0xf, at the last waypoint, IsValidDest(path[de],0) fails). Otherwise it restores the saved action/dir via FUN_0101ad02(action,dir,-1,-1,0,1).
* A remote ant also stops in place (CancelPause(1), snap to the current tile centre, SetAction(0)) when next is final and its order is 3/0xf (0x101c838-0x101c887).
* Re-issue arguments are confirmed exactly: order 5 GoTo(&+0xb4,0,0,0); 6/7/8/9/0xd/0xe GoTo(&+0xb0,0,1,0); 4 GoTo(&goal,1,0,0); 3 GoTo(&targetTile or &goal,1,0,0); else GoTo(&goal,0,0,0). The "bump" effect is FUN_010100e5(next,0xdc) (CHD Table-4 [0xdc] = 'bump'). +0x68: 1 -> restored to 1 after a re-issue, or set to 2 on a stop.

## Nuances on claim 13 (flags)
* 0x80 accepts a same-team occupant only if it has a path (+0xd8) or an action outside {0,3..9,0xb}. For a non-locally-controlled ant the same test also covers self/enemy occupants (0x101f7fc-0x101f849).
* 0x20 makes the function return 1 immediately (0x101fbd4). This skips the own AND ally (P(LT)+0x68) bomb rejects.
* 0x02 also rejects the own home entrance when another own ant has +0x68==2 and this ant's +0x68 != 2.
* The anthill branch never returns 1 by itself. It falls through to the flag-2 reservation test and the bomb test.

## Missed behaviour (new findings)
1. **Premature "no path" from the stale heap.** A node first reached over an 8000 edge is pushed with f>=8000 and bit 30 is set. If it is later reached cheaply, g is rewritten in place but the node is never re-sifted. The root can then be an f>=8000 node while an improved node with a small f sits deeper, and the search then fails (sound 0x3a, action 0xb) even though a path exists. For the same reason, improved nodes can be popped late, which gives non-optimal paths.
2. **Live world state during the search.** StepCost reads the world live on every expansion. A search spread over several 50 ms ticks sees occupants and orders as they are at expansion time.
3. **No path off water for a non-swimmer.** A non-swimmer standing on water gets 8000 on every edge, because the `from` cost is also tested (0x1020bf7).
4. **Blocked goal.** If the goal tile itself is blocked for StepCost (for example an enemy that is not the target), the goal enters with g>=8000 and the search fails. The f test precedes the goal test.
5. **Click-time snap.** GoTo snaps the ant to its current tile centre (vtbl+0x18 = 0x101a928 writes +0x38/+0x3a) and sets action 0 at click time. The ant stands idle until the path arrives on a later PATHMGR tick. The msg-6 handler snaps it again to the tile centre carried in the message.
6. **No overflow or cycles.** g cannot overflow: an expanded node has g<8000 and a step costs at most 8000, so newg<16000<0x4000. Parent links keep g(parent)<g(child), so BuildPath cannot loop.
7. **Message gating.** FUN_0100d791 returns without installing or sending when W+0x4ae0!=0, or when W+0x4ae4!=0 and arg3==0 (0x100d7a5-0x100d7bc). Paths are then silently dropped.
8. **Scheduler drift.** The PATHMGR period is measured from the end of each run, and RunOne runs only while the message queue is idle. The effective period is therefore >= 50 ms.

## Unverified
* Whether any shipped map has more than 90 rows (row-90 sentinel collision).
* Whether a sound or video driver DLL alters the x87 control word at runtime. This cannot be settled statically.
* The group-move nearest-first ordering (0x10288xx). I did not re-check it.
* The semantic meaning of player+0x68 (treated as the ally team), and of player+0x36/+0x3a/+0x3e.
