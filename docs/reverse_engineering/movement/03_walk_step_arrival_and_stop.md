# Cluster `walkstep`: per-animation-step logic for walking ants (Ants.exe 1998)

Every claim below comes from Capstone disassembly of `Original-Ants/Ants.exe` (image base 0x01000000).
Ghidra output was used only to find my way around. Addresses are VAs, and `this` = ant (ecx).
Instruction dumps of FUN_0101ee84, FUN_0101b8cb, FUN_0101c4f2, FUN_0101ccaf, FUN_0100f4ab and FUN_0101f780 were used.

## 0. Corrections and additions to the lead's claims

| Claim | Verdict |
|---|---|
| vtable+0x28 = 0x102b7bb? | **Wrong.** Slot [0x1004be0+0x28] = **0x0101a93a** = `SetPosition(int16 x, int16 y)`. Slot +0x18 = 0x0101a928 = `SetPositionPt(Point*)`, which calls 0x101a93a(p->x, p->y). |
| C10 "own-team layer-2 bomb check" | The test is `IsLocal(this)` (world+0x4ae4==0 && localTeam==team) plus layer-2 tile id at **tile5a** in {0x81,0x82,0x83,0x84} (FUN_01008bc6). The bomb's owner is **not** checked. |
| C10 passability / wait / frozen checks | Confirmed. They run **only when `newTile`**. |
| C10 axis nudge | Sign is `(d>0) ? +1 : -1`, so **d==0 gives -1** (0x101bf34..0x101bf53). |
| C10 arrive "cur" | ADVANCE uses local `cur34`. From the normal arrive, `cur34 = ntile` (0x101c066). From the +0x88 snap and from idle/action-3 ADVANCE, `cur34 = tile5a` (0x101b932). In practice it is the same tile. |
| C10 missing branch | Harvest early stop (order 5, ant exactly at the tile5a centre, next waypoint is part of the target food object): d=0, idx=count, then ARRIVE (0x101be8e..0x101bef7). |
| C11 | Confirmed. In addition: (a) at entry the attached sprite (+0x50) is snapped to the ant's position, even when the ant is waiting or frozen; (b) the waiting/frozen early exits **skip the tail** (no occupancy update); (c) ends of actions 0xc/0xf and the failure path of action 0x10 also skip the tail. |
| C12 ANTPAUSE fire | It calls FUN_0101ad02(savedAction, savedDir, 0xffff, 0xffff, **0, 1**). The 6th argument ([ebp+0x1c]) is **never read** by FUN_0101ad02. The 300 ms pause is scheduled as FUN_0103057b(task, 0, 300): onStart (vt+0x10) runs on the next 8 ms timer-wheel tick, then onFire (vt+0x14) runs 300 ms later, so the pause lasts about 300..308 ms. |
| FUN_0101ace3(a) | = FUN_0101ad02(a, +0xe0, 0xffff, 0xffff, 0, 0). The 16-bit compare `cmp word [ebp+0x10],0xffff` at 0x101ae5b means terrain is **computed** from PixelTile. |

## 1. Data layouts used by this cluster

```c
struct Tile { int16 row, col; };                 // row = y/32, col = x/32 (idiv, truncation)
/* sentinel "no tile" = {row 0x5a, col 0x78} (dword 0x0078005a) */

struct StepEvt {            /* FUN_0102b997 locals ebp-0x18 .. ebp-0x4 */
  int32 dx;      /* +0x00  CURRENT frame word+8  (walk step may rewrite) */
  int32 dy;      /* +0x04  CURRENT frame word+0xa                         */
  int32 box0;    /* +0x08  NEXT frame dword +0x12                          */
  int32 box1;    /* +0x0c  NEXT frame dword +0x16                          */
  int32 event;   /* +0x10  NEXT frame word +0xe: 11111 none, 3 landing (a?gh/a?gb/a?bu), 4 attack-hit (a?at), 5 (…h0) */
  int32 status;  /* +0x14  0 start step; 1 normal; 2 = the consumed frame was the last one (cursor was at tail) */
};

/* Occupancy grid: world+0x553c = row-pointer array of int16 cells.
   bits0-2 = team (7 = empty; low byte 0xff = empty), bits3-7 = ant index in team (0..31),
   bit8 = "more than one ant on this tile".                                             */

/* TileQuery (FUN_0100f4ab(world, Tile*, flags, TileQuery* q), size 0x3c, memset 0 first) */
struct TileQuery {
  Tile   t;        /* +0x00 */
  uint32 flags;    /* +0x04 */
  Ant*   ant;      /* +0x08  flag 0x001: single occupant (or one found by FUN_0100f2cd when multi) */
  List*  ants;     /* +0x0c  flag 0x200: new list of all occupants */
  int16  terr;     /* +0x10  flag 0x002: FUN_01008af7 terrain class */
  int16  l1id;     /* +0x12  flag 0x004: layer-1 tile id */
  int16  l2id;     /* +0x14  flag 0x008: layer-2 tile id */
  uint32 f18;      /* +0x18  bit1: occupied (flag 0x20); bit2 (value 2): anthill tile (flag 0x10); bit value 4: food object (flag 0x80) */
  int32  obj1c;    /* +0x1c  flag 0x040: FUN_0100cf0f(map,row,col) (object on tile) */
  int16  l1own,l2own; /* +0x20/+0x22 flag 0x100: layer-1/layer-2 owner word */
  int32  multi;    /* +0x24  flag 0x020: 1 if bit8 of the occupancy cell is set */
  Tile   hillOrg;  /* +0x28  flag 0x010: anthill object origin tile */
  Tile   hill32;   /* +0x2c  flag 0x010: player[hillTeam]+0x32 */
  int16  hillTeam; /* +0x30 */
  Tile   foodOrg;  /* +0x34  flag 0x080 */
  void*  foodObj;  /* +0x38  flag 0x080 */
};
```

Ant fields touched here, beyond the lead's list:
- +0x0e sprite flags (|=0x40 on SetPosition).
- +0x68 home state (0 none, 1 heading to own anthill entrance, 2 arrived). +0x6c and +0x70 are home timers.
- +0x76 last damage source team.
- +0x78 stun-invulnerable flag. +0x7c is its task.
- +0x80 COMBEVT timer task. +0x98 time of last Order() (FUN_0101fc50).
- +0xb0/+0xb2 order target id (team/index, or a tile for actions 0xa/0xe/0x13/0x10/0x11). +0xb4 is a second target word or dword.
- +0xbc auto-engage active. +0xc0/+0xc4/+0xc8 saved order, target and +0x68.
- +0xec, +0xf0, +0xf4 food-source tile.

## 2. Pseudocode

### 2.1 FUN_0101ee84: animation-step callback (ant vtable+0x38, thiscall, ret 4)

```c
void Ant::OnAnimStep(StepEvt* e)                                 // 0x101ee84
{
  if (this->child50) child50->vt18(Point(x38, y3a));              // 0x101ee9b-0x101eebe
  if (this->wait60)  return;                                       // 0x101eec8 -> 0x101f71b (no tail)
  if (this->frozenFC) { e->dx = e->dy = 0; return; }               // 0x101eed1-0x101eee1 (no tail)
  switch (this->action /*+0xe4*/) {                                // 0x101eee6; table 0x101f72c; >0x14 -> tail
  case 0: case 1:                                                  // 0x101eefc
      WalkStep(e); break;                                          // 0x101f690
  case 3:                                                          // 0x101f05c
      if (e->status == 2) F102151a(0); else WalkStep(e); break;    // 0x101f069 / 0x101f68f
  case 0xa:                                                        // 0x101f624
      if (vt40() /* returns +0xfc */) break;
      if (e->status != 2) { WalkStep(e); break; }
      SetPositionPt(TileCentre(*(Tile*)&b0)); F102151a(1); break;  // 0x101f638-0x101f688
  case 0xe: case 0x13:                                             // 0x101f5c1 (flight / bounce)
      if (e->status != 2) { WalkStep(e); break; }
      SetPositionPt(TileCentre(*(Tile*)&b0)); SetActionDefault(0); break;  // 0x101f5ce-0x101f61d
  case 0x12:                                                       // 0x101ef04 (attack)
      if (e->event == 4) F101c1e2();                               // apply melee damage
      if (e->status != 2) break;
      SetActionDefault(0);                                         // 0x101ef21
      if (IsLocal() && F101dd6f()) break;                          // 0x101ef26-0x101ef51
      SetPath(0, NULL, 0, NULL); break;                            // 0x101f5b0
  case 2: case 0x14:                                               // 0x101ef5c
      if (e->status != 2) break;
      if (IsLocal()) {
        if (holdE8 && !ecFlag) Order(&tileF4, 0,0,0);              // 0x101ef9b -> 0x101f18d
        else                   Order(&player[team]->tile42, 0,0,0);// 0x101f17d
      } else { SetActionDefault(0); ClearPath(); }                 // 0x101efa9
      break;
  case 4:                                                          // 0x101f111
      if (e->status == 2) { SetActionDefault(0); SetPath(0,NULL,0,NULL); } break;   // 0x101f5a8
  case 5: case 0xd:                                                // 0x101f06f / 0x101efbd
      if (e->status != 2) break;
      SetPositionPt(TileCentre(tile5a)); SetActionDefault(0); ClearPath();
      if (IsLocal() && holdE8) Order(&player[team]->hill2e, 0,0,0);
      break;
  case 6: case 7: case 8: case 9:                                  // 0x101f237 0x101f277 0x101f52b 0x101f568
      if (e->status != 2) break;
      SetPositionPt(TileCentre(tile5a)); SetActionDefault(0); SetPath(0,NULL,0,NULL); break;
  case 0xb:                                                        // 0x101f123 ("can't" anim)
      if (e->status != 2) break;
      SetActionDefault(0); SetPath(0,NULL,0,NULL);
      if (world->F101d822(PixelTile())) { if (IsLocal()) Order(&player[team]->tile42,0,0,0); }
      else if (IsLocal()) StopSync();                              // FUN_010214d9
      break;
  case 0xc: case 0xf:                                              // 0x101f1c3 (death / drown end)
      if (e->status != 2) break;
      if (player[team]->antAt(idx58) && (IsLocal() || player[team]->f64)) { F1020f89(); return; }
      F101da46(); return;                                          // both skip the tail
  case 0x10:                                                       // 0x101f2b7 (bridge build step)
      if (e->status != 2) break;
      { Tile b = *(Tile*)&b0; int16 id = map->TileId(2,b), own = map->l2[b].owner;
        if (id == (int16)b4 && own == team) {
          map->SetTile(2, b.row, b.col, id+1, 0);  map->l2[b].owner = team;  b4 = id+1;   // FUN_01007352
          if (id+1 == 0x25) { SetPositionPt(TileCentre(tile5a)); SetActionDefault(0); SetPath(0,NULL,0,NULL); }
          break;
        }
        SetPositionPt(TileCentre(tile5a)); SetActionDefault(0); SetPath(0,NULL,0,NULL); return; } // 0x101f3fc no tail
  case 0x11:                                                       // 0x101f401 (bridge remove step)
      if (e->status != 2) break;
      { Tile b = *(Tile*)&b0; int16 id = map->TileId(2,b), own = map->l2[b].owner;
        if (id == 0x22) { SetPositionPt(TileCentre(tile5a)); SetActionDefault(0); SetPath(0,NULL,0,NULL); break; }
        if (id == (int16)b4 && own == team) { b4 = id-1; map->SetTile(2,b.row,b.col,id-1,0); map->l2[b].owner = team; break; }
        SetPositionPt(TileCentre(tile5a)); SetActionDefault(0); SetPath(0,NULL,0,NULL); break; }
  }
  /* tail 0x101f697 */
  int nx = x38 + e->dx, ny = y3a + e->dy;
  Tile t = { (int16)(ny / 32), (int16)(nx / 32) };                 // idiv (trunc toward 0)
  if (child50) child50->vt28(child50->x38 + e->dx, child50->y3a + e->dy);  // 0x101f6dd-0x101f6fc
  world->UpdateOccupancy(this, &t);                                 // FUN_0100f17f  0x101f70a
}
```

### 2.2 FUN_0101b8cb: WalkStep(StepEvt* e) (thiscall, ret 4)

```c
void Ant::WalkStep(StepEvt* e)                                   // 0x101b8cb
{
  Point c = TileCentre(tile5a);                                  // 0x101b8e0-0x101b8ee (c.x=ebp-0x24, c.y=ebp-0x20)
  Tile  cur34;
  if (e->status == 0) return;                                    // 0x101b8fb-0x101b900: start step does nothing

  /* ---- A. water-transition (dive/climb) completion ---- */
  if (f88 && e->status == 2) {                                   // 0x101b906-0x101b915
    e->dx = c.x - x38;  e->dy = c.y - y3a;                       // snap onto tile5a centre  0x101b91b-0x101b92f
    cur34 = tile5a;                                              // 0x101b932
    goto ARRIVE;
  }

  /* ---- B. local-team landing / anim-end bomb check ---- */
  if (IsLocal() &&                                               // 0x101baef-0x101bb0c
      (e->event == 3 ||
       (e->status == 2 && wp == NULL &&
        (action == 0 || (action > 2 && (action <= 9 || action == 0xb)))))) {   // 0x101bb12-0x101bb3c
    TileQuery q; world->Query(&tile5a, 8, &q);                   // 0x101bb3e
    if (IsBomb(q.l2id)) {                                         // FUN_01008bc6: 0x81..0x84
      order = 0xa; acTarget = tile5a; e->dx = e->dy = 0; wpIdx = wpCount;   // 0x101bb67-0x101bb86
      goto ARRIVE;                                               // 0x101bb8d (repeats the same test there, then ADVANCE)
    }
  }
  /* ---- C. landing pile-up: several ants on the landing tile ---- */
  if ((action == 0xa || action == 0xe || action == 0x13) && e->event == 3) {   // 0x101bb92-0x101bbab
    TileQuery q; world->Query(&tile5a, 0x20, &q);
    if (q.multi) { e->dx = e->dy = 0; Blast(0, 7); return; }     // FUN_0101c34c(0,7)  0x101bbca-0x101bc8e
  }
  /* ---- D. fire wall (layer-2 id 0x86) under the ant ---- */
  if (IsLocal() &&                                               // 0x101bbd7-0x101bc31 (same predicate as B)
      (e->event == 3 ||
       (e->status == 2 && wp == NULL &&
        (action == 0 || (action > 2 && (action <= 9 || action == 0xb)))))) {
    TileQuery q; world->Query(&tile5a, 8, &q);                   // 0x101bc44
    if (q.l2id == 0x86) {
      if (Type() != 2) {                                         // not a fire ant   0x101bc55-0x101bc61
        e->dx = 0; e->dy = 0;
        Blast(1, map->l2[tile5a].owner);                         // FUN_0101c34c(1, owner)  0x101bc63-0x101bc8e
        return;
      }
      /* fire ant */                                              // 0x101bc98
      if (!(wp == NULL && (action == 0 || (action > 2 && (action <= 9 || action == 0xb))))) {
        e->dx = e->dy = 0;
        F102151a(1);                                             // 0x101bcc2
        SetPositionPt(TileCentre(PixelTile()));                  // 0x101bcc7-0x101bceb
        return;
      }
    }
  }
  /* ---- E. landing in water ---- */
  if ((action == 0xa || action == 0xe || action == 0x13) && e->event == 3) {   // 0x101bcfa-0x101bd13
    TileQuery q; world->Query(&tile5a, 2, &q);
    if (q.terr == 2) { e->dx = e->dy = 0; F101e6b3(PixelTile()); /* falls through */ }  // 0x101bd37-0x101bd4a
  }
  /* ---- F. movement proper ---- */
  if (action != 1 && action != 0 && action != 3) return;         // 0x101bd4f-0x101bd61
  if (wp == NULL) {                                              // 0x101bd67: no path -> combat auto-engage only
    if (!CanAutoEngage()) return;                                // FUN_0101c0d5
    Tile t = PixelTile();
    if (!FindEnemy(4, &t)) return;                               // FUN_0101dbec(4,&t): rings 1..3
    c0 = order; c4 = PixelTile(); c8 = f68;                      // 0x101bd9e-0x101bdc2
    AttackTile(&t);                                              // FUN_0101da6f
    StartCombatTimer(3000);                                      // FUN_0101c184(0xbb8)
    return;                                                      // d NOT zeroed
  }
  if (action == 0 || action == 3) { cur34 = tile5a; goto ARRIVE; }   // 0x101bde2-0x101bded (no snap)

  /* action 1 with a path */                                      // 0x101bdf3
  int nx = x38 + e->dx, ny = y3a + e->dy;
  Tile ntile = { (int16)(ny/32), (int16)(nx/32) };
  bool newTile = (ntile.row != tile5a.row || ntile.col != tile5a.col);        // 0x101be42-0x101be5e
  bool notOnWp = !newTile && (ntile != wp[wpIdx]);                             // 0x101be67-0x101be87
  if (order == 5 && x38 == c.x && y3a == c.y) {                  // 0x101be8e-0x101bea7
    TileQuery q; world->Query(&wp[wpIdx], 0x80, &q);
    if ((q.f18 & 4) && q.foodObj == (void*)b0) {                 // 0x101bed1-0x101bee0
      e->dx = e->dy = 0; wpIdx = wpCount; goto ARRIVE;           // 0x101bee2-0x101bef7 (-> 0x101c06e)
    }
  }
  if (notOnWp) return;                                           // 0x101bf00: still in departure tile, keep frame d
  if (newTile) {
    if (ntile != wp[wpIdx]) {                                    // 0x101bf14-0x101bf32
      int sx = (e->dx > 0) ? 1 : -1, sy = (e->dy > 0) ? 1 : -1;  // d==0 -> -1
      e->dx += sx; nx += sx; e->dy += sy; ny += sy;
      ntile = { (int16)(ny/32), (int16)(nx/32) };
    }
    int ok = TryEnterTile(&ntile);                               // FUN_0101c4f2  0x101bfb4
    if (!ok && frozenFC == 0) { e->dx = c.x - x38; e->dy = c.y - y3a; return; }  // back to tile5a centre
    if (wait60 || frozenFC)   { e->dx = e->dy = 0; return; }     // 0x101bfe1-0x101bff7
  }
  if (f88) return;                                               // 0x101bffc: dive/climb in progress
  Point c2 = TileCentre(ntile);                                  // 0x101c017
  if ((uint16)abs(nx - c2.x) <= 2 && (uint16)abs(ny - c2.y) <= 2) {   // 0x101c01c-0x101c043
    e->dx = c2.x - x38; e->dy = c2.y - y3a;                      // exact landing on centre
    cur34 = ntile;                                               // 0x101c066
    goto ARRIVE;                                                 // 0x101c06e
  }
  if (newTile)                                                   // 0x101c075-0x101c0bf
    SetAction(1, dir_e0, Terrain(tile5a), Terrain(ntile), /*flag*/0, 1);  // restarts only if terrain differs
  return;

ARRIVE:                                                          // 0x101b938
  if (IsLocal()) {
    TileQuery q; world->Query(&tile5a, 8, &q);                   // 0x101b960
    if (IsBomb(q.l2id)) {                                        // 0x101b973
      order = 0xa; acTarget = tile5a; e->dx = e->dy = 0; wpIdx = wpCount;
      goto ADVANCE;
    }
  }
  if (CanAutoEngage() && (action == 1 || action == 0)) {         // 0x101b9aa-0x101b9c4
    Tile t = PixelTile();
    if (FindEnemy(3, &t)) {                                      // rings 1..2   0x101b9d9
      c0 = order; c4 = acTarget; c8 = f68;                       // 0x101b9e2-0x101ba15
      AttackTile(&t);                                            // FUN_0101da6f
      StartCombatTimer(2000);                                    // FUN_0101c184(0x7d0)
      e->dx = e->dy = 0; return;                                 // wpIdx NOT advanced
    }
  }
ADVANCE:                                                         // 0x101ba3c
  if (++wpIdx >= wpCount) {                                      // uint16 compare
    ClearPath();                                                 // FUN_0101ab56
    PathComplete(e);                                             // FUN_0101ccaf
    return;
  }
  Tile next = wp[wpIdx];
  int16 d8  = DirAdjacent(&cur34, &next);                        // FUN_01017531
  SetAction(1, d8, Terrain(cur34), Terrain(next), /*flag*/1, 1); // restarts only if dir changed (or +0x88 set)
}
```

`Terrain(t)` is FUN_01008af7(map, t.row, t.col). `SetAction` is FUN_0101ad02. `SetActionDefault(a)` is FUN_0101ace3.

### 2.3 FUN_0101c4f2: TryEnterTile(Tile* t) (ret 4). Gate for entering a new tile

```c
int Ant::TryEnterTile(Tile* t)                                   // 0x101c4f2
{
  Tile cur = PixelTile();                                        // 0x101c50b
  TileQuery qn, qc;
  world->Query(t,    0xdb, &qn);                                 // 0x101c523
  world->Query(&cur, 0xdb, &qc);                                 // 0x101c53a
  /* next tile contains my attack target */
  if (IsLocal() && (order == 3 || order == 0xf) && qn.ant &&
      qn.ant->team == (int16)b0 && qn.ant->index == (int16)b2) { // 0x101c544-0x101c59e
    if (qn.terr == 2 || qc.terr == 2) {                          // 0x101c5a4-0x101c5b7
      StopSync(); SetActionDefault(0xb); world->Notify(0x3a, 0); ClearPath();   // FUN_0100e8f5
      if (typeRaw54 == 4) F101dd6f(); return 0;                  // 0x101c68b..0x101c6c2
    }
    if (!qn.ant->F101cb0c(&tile5a) /* ecx=target: adjacent(<=1), not +0x78/+0x84, action ok, no water */ ||
        player[team]->ally68 == (int16)b0) {                      // 0x101c5bd-0x101c5ea
      StopSync(); SetActionDefault(0xb); world->Notify(0x30, 0); ClearPath();
      if (typeRaw54 == 4) F101dd6f(); return 0;                  // 0x101c676..
    }
    int16 r  = qn.ant->F101d8ed(&qn.ant->tile5a, &tile5a, Type()==4 ? 4 : 1);   // ecx=target 0x101c5f0-0x101c610
    Msg* m = MakeMsg08(b0, b2, team, idx58, qn.ant->tile5a, tile5a, r, Type()==4 ? 1 : 0); // FUN_01022c57
    world->Send(m, 0xa, 0); free(m); return 0;                   // 0x101c658-0x101c671
  }
  uint32 fl = 0;                                                 // 0x101c6c7
  if (order == 4   && *t == acTarget) fl = 8;
  if (order == 0xa && *t == acTarget) fl |= 0x20;
  Tile hill = player[team]->hill2e;
  if (order == 2 || cur == hill || cur == Tile{hill.row-1, hill.col}) fl |= 4;   // 0x101c72b-0x101c76a
  if (CanEnter(t, fl, &qn)) return 1;                            // FUN_0101f780  0x101c77b
  bool isLast = (*t == wp[wpCount-1]);                           // 0x101c788-0x101c7b8
  if (!IsLocal()) {                                              // 0x101c7d9..: never gives up
    if (qn.terr == 2 && Type() != 5) { StartPause(1); SetPositionPt(TileCentre(PixelTile())); SetActionDefault(0); return 1; }
    if (isLast) { if (order == 3 || order == 0xf) { StartPause(1); SetPositionPt(TileCentre(cur)); SetActionDefault(0); } return 1; }
    Ant* o = world->OccupantAt(t);                               // FUN_0100f421
    if (o && o->action != 0 && (o->action <= 2 || (o->action > 9 && o->action != 0xb))) { StartPause(1); SetActionDefault(0); }
    return 1;
  }
  if (!isLast) {                                                 // 0x101c8b4
    Ant* o = world->OccupantAt(t);
    if (o && o->action != 0 && (o->action <= 2 || (o->action > 9 && o->action != 0xb))) {
      StartPause(1); SetActionDefault(0); return 1;              // blocked by a MOVING ant: wait ~300 ms, no snap
    }
  } else if (order == 0xb) { StartPause(1); SetActionDefault(0); return 1; }
  /* give up / re-path */                                        // 0x101c90f
  SetActionDefault(0); ClearPath(); acTarget = {0x5a,0x78};
  bool wasHome = (f68 == 1); f68 = 0;
  if (isLast && IsLocal() && (order==6||order==7||order==8||order==9||order==0xd||order==0xe||order==3)) isLast = false;
  if (!isLast && IsLocal()) {
    switch (order) {                                             // 0x101c9b0-0x101cacc
      case 5:           Order(&tileB4, 0,0,0); break;
      case 6: case 7: case 8: case 9: case 0xd: case 0xe:
                        Order((Tile*)&b0, 0,1,0); break;
      case 4:           Order(&lastWp, 1,0,0); break;           // lastWp = wp[count-1] saved at 0x101c79f
      case 3: { Ant* v = player[b0]->antAt(b2);
                if (v) Order(&v->PixelTile(), 1,0,0); else Order(&lastWp, 1,0,0); } break;
      default /*0,1,2,0xa,0xb,0xc,0xf..*/: Order(&lastWp, 0,0,0); break;   // plain move re-paths to its destination
    }
    if (wasHome) f68 = 1;
    world->F10100e5(t, 0xdc);                                    // 0x101caeb
  } else {
    StopSync();                                                  // 0x101caf4
    if (wasHome) f68 = 2;
  }
  return 0;
}
```

### 2.4 FUN_0101f780: CanEnter(Tile* t, flags, TileQuery* q) (ret 0xc). Summary of what the code does

```c
bool Ant::CanEnter(Tile* t, uint fl, TileQuery* q)               // 0x101f780
{
  if (!q || q->flags != 0x1db) { world->Query(t, 0x1db, &tmp); q = &tmp; }
  if (word[0x10049b8 + 2*q->terr] == 0) {                        // table {1,1,0,1,1,0,...}
    if (q->terr != 2 || Type() != 5) return 0;                   // water: swimmers only
  }
  Ant* o = q->ant;
  if (o && !(fl & 0x40)) {                                       // 0x101f7e7
    bool skipTo84f = (o == this || o->team != team) && IsLocal();
    if (!skipTo84f) {                                            // friendly occupant, or any occupant for a non-local ant
      if (!(fl & 0x80)) return 0;                                // TryEnterTile never passes 0x80 -> blocked
      if (o->wp == NULL) { int a = o->action;
        if (a == 0 || (a >= 3 && a <= 9) || a == 0xb) return 0; }
    }
    if (IsLocal() && o != this && o->team != team &&             // 0x101f84f
        !(order == 3 && (int16)b0 == o->team && (int16)b2 == o->index)) return 0;
  }
  if (q->f18 & 2) {                                              // anthill tile  0x101f896
    if (!(order == 0xb && (int16)b0 == q->hillTeam && *t == q->hill32)) {
      if (q->hillTeam != team) return 0;
      if (!(fl & 4)) return 0;
      if ((fl & 1) && *t != q->hillOrg) return 0;
    }
  } else if (q->obj1c) {                                         // object on tile  0x101f913
    if ((fl & 8) && map->IsPowerup(q->l2id)) return 1;           // FUN_01007202
    if (q->l2id == 0x86 && Type() == 2) return 1;                // fire ant walks through fire wall
    return (q->f18 & 4) && order == 5 && (void*)b0 == q->foodObj;
  } else if (!(fl & 0x100)) {                                    // queen/entrance tiles +0x36..+0x40 of each live player
    /* 0x101f98a-0x101fb2f: tile in {p+0x36,p+0x3a,p+0x3e}: other team -> 0;
       own team with fl&1&&fl&0x10: count occupancy + ants with order 1 targeting each; ==2 -> 0 */
  }
  if (fl & 2) { /* 0x101fb35: own ants with order 1/2 targeting *t -> 0; entrance rule with +0x68==2 -> 0 */ }
  if (fl & 0x20) return 1;
  if (!IsBomb(q->l2id)) return 1;
  if (q->l2own == team) return 0;                                // cannot walk onto own bomb
  if (player[localTeam]->ally68 == q->l2own) return 0;
  return 1;                                                      // enemy bomb: enterable (ARRIVE then triggers order 0xa)
}
```

### 2.5 Combat auto-engage helpers

```c
bool Ant::CanAutoEngage()                                        // FUN_0101c0d5
{ if (Type() != 4) return 0;                                     // 0x101c0db
  if (!IsLocal()) return 0;
  if (!((uint32)(timeGetTime() - t98) > 2000)) return 0;         // 0x101c0f1-0x101c10c
  if (action == 3 || action == 0xc || action == 0xf) return 0;
  if (hp74 == 0) return 0;  if (frozenFC == 1) return 0;  if (f84 == 1) return 0;
  if (order == 3 || order == 0xf) return 0;
  return 1; }

bool Ant::FindEnemy(uint16 radius, Tile* io)                     // FUN_0101dbec, ret 8
{ for (r = 1; r < radius; ++r) {                                 // square rings, Chebyshev distance r
    if (col-r >= 0)  for (row' = max(row-r,0) .. row+r)    test {row',col-r};   // left column
                     for (row' = max(row-r,0) .. row+r)    test {row',col+r};   // right column
    if (row-r >= 0)  for (col' = max(col-r+1,0) .. col+r-1) test {row-r,col'};  // top row
                     for (col' = max(col-r+1,0) .. col+r-1) test {row+r,col'};  // bottom row
    /* first hit: *io = tile; return 1 */ }
  return 0; }
/* test = FUN_0101db55: in map bounds; o = OccupantAt; o && o->team != team && o->team != player[team]->ally68 &&
   o->action not in {0xc,0xf,0xa,2,0x13,0x14,0xe}; return frozenFC == 0 */

void Ant::AttackTile(Tile* tgt)                                  // FUN_0101da6f
{ Tile me = PixelTile(); int16 d = DirSign(&me, tgt); Ant* v = OccupantAt(tgt);
  fBC = 1; SetActionDefault(0); ClearPath();
  StepToward(&me, d, 1);                                         // FUN_0101d9f7 -> me = adjacent tile
  if (!CanEnter(&me, me == *tgt ? 0x140 : 0x40, NULL)) { F101dd6f(); return; }
  order = 0xf; acTarget = me; b0 = v->team; b2 = v->index;
  world->F100cba4(idx58, {PixelTileOld, me}, 2); }               // path request of length 2

void Ant::CancelCombatTimer()  { if (t80) { world->sched->remove(t80); release(t80); t80 = 0; } }  // FUN_0101c152
void Ant::StartCombatTimer(int ms)                               // FUN_0101c184
{ CancelCombatTimer(); t80 = new COMBEVT(this);                  // FUN_01024bc3, name "COMBEVT"
  World::Schedule(t80, ms, 0, 0); }                              // FUN_01031e92: delay=ms, period=0
/* COMBEVT onFire (vtable 0x1004e70 +0x14 = 0x1024c69): if (IsLocal(ant)) ant->F101dd6f(); */

bool Ant::F101dd6f()  /* resume after auto-engage */             // 0x101dd6f
{ CancelCombatTimer();
  if (world->state4b18 != 2 || !fBC || (action != 1 && action != 0)) return 0;
  SetActionDefault(0); ClearPath();
  Order(&c4, (c0 == 1 || c0 == 0) ? 0 : 1, 0, 0);
  f68 = c8; fBC = 0; return 1; }
```

### 2.6 FUN_0101c34c: Blast(int doDamage, int16 source) (ret 8)

```c
void Ant::Blast(int dmg, int src)                                // 0x101c34c
{ if (IsLocal()) {
    TileQuery q; world->Query(&tile5a, 0x200, &q);               // every ant on my tile, me included
    uint8 excl[0x20] = {0};
    for (Ant* a : q.ants) {
      Tile at = a->PixelTile(), dest;
      F101df5d(&at, &dest, 1, 0, excl, 1);                       // pick a landing tile (FUN_0101df5d)
      Msg* m = MakeMsg14(at, dest, a->team, a->index, dmg, src); // FUN_0102370f (type 0x14, size 0x1c)
      world->Send(m, 0xa, 0); free(m);
    }
  } else {                                                       // remote ant: cosmetic dust cloud only
    Sprite* s = new Sprite(world->anim4980 /* CHD anim 56 "battle" */, &tile5a);   // FUN_0101a2aa
    s->SetPositionPt(TileCentre(tile5a)); map->AddDisplay(s, world->layer4a7c); release(s);
  } }
/* msg 0x14 handler 0x1023749 -> ant->F101c221(A=at, B=dest, dmg, src) if ant action not 0xc/0xf:        */
void Ant::F101c221(Tile A, Tile B, int dmg, int src)             // 0x101c221, ret 0x10
{ int16 d = DirSign(&A, &B); if (dmg) TakeHit(src);              // FUN_01021627: hp--, +0x76=src (src!=7)
  SetPositionPt(TileCentre(A));
  SetAction(0xe, (uint16)(d + 4) % 8, 0xffff, 0xffff, 0, 1);     // flight, facing away
  order = 0xc; acTarget = B; *(Tile*)&b0 = B; fB4 = (A == B); }
```

### 2.7 Occupancy and position

```c
void World::UpdateOccupancy(Ant* a, Tile* t)                     // FUN_0100f17f, ret 8
{ Player* p = players[a->team]; if (!p || !p->antAt(a->idx58)) return;
  Tile old = a->tile5a;
  if (old.row != 0x5a) {
    if (*t == old) return;                                       // 0x100f1c8-0x100f1d8
    int16* cell = &occ553c[old.row][old.col];
    if (*cell & 0x100) {                                         // there were several
      int16 n; Ant* one; F100f2cd(&old, a, &n, &one, NULL);      // others still on old tile
      *cell = (*cell & 0xfeff) | ((n > 1) << 8);
      if (n == 1) *cell = (*cell & 0xff00) | (one->team & 7) | ((one->idx58 & 0x1f) << 3);
    } else *cell |= 0x00ff;                                      // empty
  }
  a->tile5a = *t;                                                // 0x100f26d / 0x100f276
  if (t->row == 0x5a) return;
  int16* cell = &occ553c[t->row][t->col];
  if ((*cell & 7) == 7) *cell = (*cell & 0xff00) | (a->team & 7) | ((a->idx58 & 0x1f) << 3);
  else                  *cell |= 0x100; }

void Sprite::SetPosition(int16 x, int16 y)   /* ant vt+0x28 = FUN_0101a93a, ret 8 */
{ f0e |= 0x40; x38 = x; y3a = y;
  Tile t = { y/32, x/32 }; world->UpdateOccupancy(this, &t);
  if (IsLocal() || team == players[localTeam]->ally68)
    map->RevealFog(&PixelTile(), 6, world->f4a58); }             // FUN_01006af4
void Sprite::SetPositionPt(Point* p) { SetPosition(p->x, p->y); } // vt+0x18 = FUN_0101a928
Point Sprite::Pos()     { return Point(x38, y3a); }              // FUN_01009b85
Tile  Sprite::PixelTile(){ return { y3a/32, x38/32 }; }          // FUN_0100ccc0
Point TileCentre(Tile t) { return Point(t.col*32+16, t.row*32+16); }   // FUN_0100cd00
```

### 2.8 Path storage

```c
void Ant::ClearPath()  { if (wp) { free(wp); wp = NULL; wpCount = 0; wpIdx = 0; } }   // FUN_0101ab56
void Ant::SetPath(uint16 n, Tile* src, int ord, Tile* tgt)      // FUN_0101ab87, ret 0x10
{ ClearPath(); order = 0; acTarget = {0x5a,0x78};
  if (n == 0) return;                                            // order argument ignored
  wpCount = n; wp = malloc(4*n); memcpy(wp, src, 4*n); wpIdx = 0;
  order = ord; acTarget = wp[n-1];
  if (ord == 5) { tileB4 = *tgt; b0 = map->FoodObjAt(*tgt); } } // FUN_01008c63
```

### 2.9 FUN_0101ccaf: PathComplete(StepEvt* e) (ret 4)

```c
void Ant::PathComplete(StepEvt* e)                               // 0x101ccaf
{ Tile old = acTarget; acTarget = {0x5a,0x78};
  bool wasHome = (f68 == 1); f68 = 0; int handled = 0;           // ebp-0x14
  switch (order) {                                               // table 0x101d680 (0..0xf)
  case 1: case 4: if (!IsLocal()) break;                         // 0x101cd1d
      if (wasHome) { f68 = 2; f70 = f6c ? 0 : timeGetTime(); break; }
      if (map->IsPowerup(world->Query(old,8).l2id)) { Send(Msg09(team,idx,l2id,old)); handled = 1; } break;
  case 2:  if (IsLocal()) { Send(Msg07(team,idx)); handled = 1; } break;
  case 3:  if (!IsLocal()) break;                                // 0x101cf13 (attack)
      SetPositionPt(TileCentre(tile5a)); SetActionDefault(0); ClearPath();
      if (Ant* v = player[b0]->antAt(b2)) { Order(&v->PixelTile(),1,0,1); if (order == 3) handled = 1; e->dx = e->dy = 0; }
      break;
  case 0xf: if (!IsLocal()) break;                               // 0x101cfcf (auto attack)
      SetPositionPt(TileCentre(tile5a)); SetActionDefault(0); ClearPath();
      if (Ant* v = player[b0]->antAt(b2)) { AttackTile(&v->PixelTile()); handled = 1; e->dx = e->dy = 0; }
      break;
  case 5:  if (!IsLocal()) break;                                // 0x101ce07 (harvest)
      if (!holdE8) { if (food(b0)->w12 > 0) { Send(Msg0a(...)); handled = 1; } break; }
      ecFlag = 0; tileF4 = food(b0)->origin; Order(&player[team]->hill2e,0,0,0); world->F100e944(str#0x11,1); handled = 1; break;
  case 6:  if (IsLocal() && F101d762(b0tile,0)) { Send(Msg0b); handled = 1; } break;
  case 7:  if (IsLocal() && map->TileId(2,b0tile) == 0x86) { Send(Msg0c); handled = 1; } break;
  case 8:  if (IsLocal() && F101d762(b0tile,0)) { Send(Msg0d); handled = 1; } break;
  case 9:  if (IsLocal() && F101d7f9(b0tile))   { Send(Msg0e); handled = 1; } break;
  case 0xa: if (IsLocal() && IsBomb(world->Query(old,8).l2id)) {           // 0x101d44f
      Tile to = (rand()%100 < 20) ? old : F101df5d(&old,&to,4,0,0,0); Send(Msg0f(old,to,team,idx)); handled = 1; } break;
  case 0xb: /* 0x101d51b steal food from enemy hill (Msg12, amount min(p->f54,50)) or go home */ break;
  case 0xd: if (IsLocal() && F101d6d6(b0tile)) { Send(Msg19); handled = 1; } break;
  case 0xe: if (IsLocal() && map->TileId(2,b0tile) == 0x25) { Send(Msg1a); handled = 1; } break;
  case 0: case 0xc: default: break;                              // plain move: nothing extra
  }
  if (!handled) {                                                // 0x101d62d
    if (IsLocal()) StopSync();                                   // FUN_010214d9
    else           StopAt(tile5a, 0);                            // FUN_01021664
    e->dx = e->dy = 0;                                           // 0x101d667-0x101d66d
  } }
/* Every Send is FUN_0100d791(world,msg,0xa,0). It calls the handler table [0x1047320+type*8] synchronously
   (0x100d965), then sends to the network (flag 8). */
```

### 2.10 Stop and idle at path end

```c
void Ant::StopSync()                                             // FUN_010214d9
{ if (!IsLocal()) return;
  Msg* m = MakeMsg13(PixelTile(), team, idx58, 0);               // FUN_0102368f: type 0x13 size 0x10; team|0x8000 if force
  world->Send(m, 0xa, 0); free(m); }                             // local handler 0x10236c1 runs immediately:
/* handler: ant = players[team&0x7fff]->antAt(index); ant->StopAt(tile, team & 0x8000); */

void Ant::StopAt(Tile t, int force)                              // FUN_01021664, ret 8
{ if (force) { if (IsLocal()) return; if (action==0xe||action==0x13||action==0xa) return;
               if (f84 == 1) return; if (timeGetTime() - t100 < 2000) return; }
  SetPositionPt(TileCentre(t));                                  // 0x10216df-0x10216fa
  if (child50) child50->vt18(Pos());
  StartPause(0);                                                 // cancel ANTPAUSE
  if (wp == NULL && order == 0 && (action == 0 || (action > 2 && (action <= 9 || action == 0xb)))) return;
  SetActionDefault(0);                                           // 0x1021769  <== idle at path end
  SetPath(0, NULL, 0, NULL); }                                   // order=0, acTarget=sentinel
```

### 2.11 Other helpers reached from WalkStep

```c
int Ant::F102151a(int flag)                                      // 0x102151a, ret 4
{ if (!flag) {                                                   // called at end of action-3 anim (case 3 status 2)
    if (action != 3) return 0;                                   // (when called from SetAction, action is already the new one, so no-op)
    f78 = 1; t7c = new Task24ae4(this); t7c->Start(0, 0);        // stun-invulnerability task
    SetActionDefault(0); ClearPath(); return LowHpCheck(1); }
  if (LowHpCheck(1)) { if (hp74 > 0) SetActionDefault(0); return 1; }
  SetActionDefault(3);                                           // action 3 anim = world+0x4180[type+6*colour] (carry 0x41e0), dir forced to 4 (S)
  order = 0; acTarget = {0x5a,0x78}; ClearPath();
  SetPositionPt(TileCentre(PixelTile())); return 0; }

int Ant::LowHpCheck(int flag)                                    // FUN_0101dded
{ if (flag && hp74 == 1 && IsLocal()) { ClearPath(); action = 0; /* raw write, no anim */
    Order(&player[team]->hill2e, 0,0,0); f6c = 1; return 1; }    // 1 HP: retreat home
  if (hp74 == 0 && action != 0xc && action != 0xf) { SetActionDefault(0xc); return 1; }   // die
  return 0; }

int Ant::F101e6b3(Tile* t)  /* landed in water */                // 0x101e6b3
{ if (Type() != 5) { if (IsLocal()) Send(Msg15(t, idx58)); else Drown(*t); return 1; }   // Drown = FUN_0101c2e2
  world->F1010008(t, 0x28, 0, 0); F102151a(1); SetPositionPt(TileCentre(*t)); return 0; }
void Ant::Drown(Tile t) { SetPositionPt(TileCentre(t)); SetActionDefault(0xf); order = 0; acTarget = t; ClearPath(); } // FUN_0101c2e2

void Ant::StartPause(int on)                                     // FUN_0101cc1e
{ if (on) { if (!wait60) { t64 = new ANTPAUSE(this /* saves action e4, dir e0 */);   // FUN_01024cf7
                          t64->Start(0, 300); wait60 = 1; } }     // FUN_0103057b(task,0,300)
  else if (wait60) { wait60 = 0; sched->remove(t64); release(t64); t64 = 0; } }
/* ANTPAUSE fire 0x1024d2b: StartPause(0); if (F101cbcc()) { StartPause(1); copy saved action/dir; }
   else SetAction(saved_action, saved_dir, 0xffff, 0xffff, 0, 1);
   F101cbcc: order in {0xb,3,0xf} && wpCount>0 && wpIdx==wpCount-1 && wp && !CanEnter(&wp[wpIdx],0,NULL) */
```

## 3. Walking, step by step (reimplementation guide)

### 3.1 How a path starts
- When `Order()` (FUN_0101fc50) accepts an order (it can return 0 early at 0x101fc84 or 0x101feee), it first does the following (0x101fcc8..0x101fd26): `SetActionDefault(0)`, **snap to the centre of the current PixelTile** (vt+0x18), cancel the pause, `SetPath(0,…)`, cancel COMBEVT, and `t98 = timeGetTime()`. The path itself arrives asynchronously through the pathfinder queue and a message. That handler (0x1022a7d) snaps to the message point, calls SetAction(msgAction, msgDir, 0xffff, 0xffff, 0, 1), then `SetPath(n, tiles, order, target)` with `wpIdx = 0`.
- The ant is now idle (action 0) with waypoints. Its next idle-animation step with status≠0 goes through `WalkStep` → 0x101bde2 → `ARRIVE` with no snap and `cur34 = tile5a`. That runs the bomb test and the combat auto-engage test, then `++wpIdx` (0→1) and `SetAction(1, DirAdjacent(tile5a, wp[1]), T(tile5a), T(wp[1]), 1, 1)`. `wp[0]` is therefore treated as the start tile and is never walked to.
- The walk animation restarts because the action changed. The nested start step does nothing (status 0). Because of C5, the first walk frame's delta is applied `2×dur0` later: 100 ms on grass.

### 3.2 Each walk step (action 1, path present)
1. `n = pos + frameDelta`, `ntile = n/32`.
2. The ant is still in its departure tile (`!newTile && tile5a != wp[idx]`): the frame delta is applied unchanged. It moves in a straight line in the direction of `+0xe0`.
3. The ant crosses into a new tile (`newTile`):
   - If `ntile != wp[idx]`, nudge each axis by ±1 (d==0 gives −1).
   - Call `TryEnterTile(ntile)`.
     - Returns 0 (the ant gave up or re-pathed; its state was reset): `d = centre(tile5a) − pos`. The ant usually has already been snapped, so this is normally 0.
     - Returns 1 but the ant is now waiting (blocked by a moving ant; 300 ms ANTPAUSE; action 0): `d = 0`. The ant stays where it is, partly across the boundary, **with no snap**. When ANTPAUSE fires, walking resumes (SetAction(1, savedDir, computed terrain…)) and the same crossing is tried again.
   - Otherwise, unless `+0x88` is set, check whether the ant is within 2 px of the new tile's centre. Normally it is 16 px away, so the result is `SetAction(1, e0, T(tile5a), T(ntile), 0, 1)`. **The walk animation restarts only if the terrain class differs**, and the new animation uses the new tile's terrain.
4. The ant is inside `wp[idx]`: each step it tests `|n − centre| ≤ 2` on both axes (16-bit unsigned compare of `abs()`). When true, `d = centre − pos`, so the ant lands exactly on the centre, then `ARRIVE`.
5. `ARRIVE` for a non-final waypoint: `++idx`, `SetAction(1, DirAdjacent(ntile, next), T(ntile), T(next), 1, 1)`. With flag=1 the animation restarts only if the direction changed (or `+0x88` was set). The animation terrain is `terrA` = the tile just reached. On a straight line the 12-frame cycle continues without interruption.
6. Swimmers (type 5), where the arrival tile and next tile differ in water-ness: SetAction plays dive-in `world[0x11b0+dir+8*colour]` or climb-out `world[0x11d0+…]` and sets `+0x88 = 1`. While `+0x88` is set, WalkStep skips the ≤2 px arrival test and the terrain restart. The dive still passes through TryEnterTile when it crosses the boundary. On the animation's last frame (status 2) the +0x88 branch replaces that frame's delta with a snap to `centre(tile5a)` and ARRIVEs.
7. Tile-boundary asymmetry, from `idiv` truncation with centre offset 16: moving toward +x/+y an orthogonal 4 px walk crosses at step 4 (offset +16). Toward −x/−y it crosses at step 5 (offset −20). Diagonal 3 px walks cross at step 6 in every direction.

Worked timings (straight line, no restart). The number of steps is the count of frame deltas until the ant is within 2 px of the next centre:

| Walk | Steps per tile | Time per tile | Last step |
|---|---|---|---|
| grass orthogonal (4 px/50 ms) | 8 | 400 ms | exact, 4 px |
| grass diagonal (3,3/50 ms) | 10 | 500 ms | snap moves (5,5) instead of (3,3) |
| sand orthogonal (4 px/40 ms) | 8 | 320 ms | |
| dirt orthogonal (4 px/60 ms) | 8 | 480 ms | |
| mud orthogonal (2 px/60 ms) | 15 | 900 ms | arrives at offset 30, snap moves 4 px |
| mud diagonal (1,1)/(2,2) alternating | 20 | 1200 ms | snap moves 4 or 3 depending on phase |

### 3.3 The step on which the LAST waypoint centre is reached (plain move, order 0)

On the timer tick where `now ≥ +0x10`, FUN_0102b997 consumes walk frame F (status 1, or 2 at the end of the cycle) and calls OnAnimStep, which calls WalkStep:
1. `n = pos + F.d` is within 2 px of `centre(ntile)` (ntile == tile5a == wp[count−1]). Set `e->d = centre − pos` and `cur34 = ntile`.
2. ARRIVE: if the ant is local and a bomb is on tile5a, the order becomes 0xa and the path ends via case 0xa. Otherwise a combat ant (local, >2 s since its last order, action 0/1) with an enemy within Chebyshev distance 2 attacks: AttackTile, a 2 s COMBEVT timer, d=0, and **the path is not advanced**. The saved order and destination are resumed by F101dd6f when COMBEVT fires.
3. `++wpIdx == wpCount`, so `ClearPath()` (free the waypoints, count = idx = 0) and then `PathComplete(e)`:
   - `acTarget = sentinel`; `+0x68: 1→(wasHome), then 0`.
   - Order 0 has no case, so `handled = 0`.
   - Local ant: `StopSync()` sends message 0x13 {PixelTile(pos before this step), team, idx}. FUN_0100d791 runs handler 0x10236c1 **synchronously**, which calls `StopAt(tile, force=0)`:
     - `SetPositionPt(centre)`. The ant is now exactly at the centre (occupancy unchanged, fog revealed with radius 6).
     - Snap the attached sprite; `StartPause(0)`.
     - wp==NULL and order==0, but action==1, so `SetActionDefault(0)` = SetAction(0, +0xe0, computed terrain): the idle animation `world[0x3d0 + dir + 8*(type+6*colour)]`, carrying `0x868`, swimming idle `world[0x47c0+4*colour]` on water, `+0x88 = 0`. It is played now, and its nested start step runs.
     - `SetPath(0,…)` makes order 0 and acTarget the sentinel.
     - The same message is then broadcast so peers do the same.
   - Remote copy of the ant: `StopAt(tile5a, 0)` is called directly.
   - `e->dx = e->dy = 0`.
4. OnAnimStep tail: `pos + 0`, so the occupancy update is a no-op.
5. Back in FUN_0102b997: the list changed, so the frame is the idle head. `SetPosition(x+0, y+0)`, `+0x2c` = idle cursor, `+0x10 += idle0.dur` (the first idle frame is shown for 2× its duration), then its sound plays. The loop in FUN_0102b95f then sees `now < +0x10` and stops.

End state: action 0 (idle), exactly on the destination centre, facing the last walk direction, no path, order 0, pause cancelled. **The walk animation does not keep running** after a plain move: `StopAt` (0x1021769) is what calls `SetActionDefault(0)`.

For orders whose PathComplete case dispatches a message (handled=1), the snap delta from step 1 is kept and the handler sets the follow-up action. That handler is outside this cluster.

## 4. Supporting facts
- FUN_0100d791 (send): returns early if `world+0x4ae4 && !arg3` or if `world+0x4ae0`. Otherwise it calls `[0x1047320 + type*8](msg)` locally (0x100d965), then sends to the network if flag&8. Messages are `{int size, int type, …}` (FUN_010221fa). Types seen: 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0x12, 0x13 (stop), 0x14 (blast), 0x15 (drown), 0x19, 0x1a.
- Scheduler (world+0xe88, vtable 0x1005248): a timing wheel of 1024 slots × 8 ms (FUN_01031465 / FUN_010313cb). `Add(task, delay, period, 0)` (0x10312bd). The timed-task wrapper 0x10305a8 calls onStart the first time (+0x2c==0) and reschedules by `period` if `+0x30==0`; the second time it calls onFire.
- Direction table words at 0x1002b28 = {7,0,1,6,0,2,5,4,3}, index `(drow+1)*3+(dcol+1)` (FUN_01017531 raw deltas; FUN_01017560 sign of deltas).
- CHD frame events: 3 only in get-hit, get-blown and bump animations (landing frame); 4 in attack animations; 5 in `…h0`. Walk animations use only the 11111 sentinel, so blocks B–E of WalkStep never fire during a normal walk. They fire when actions 0xa/0xe/0x13 (hit/flight/bounce) call WalkStep with status≠2.
- Walk animation names: `a?w{g,s,d,m}{3,7,8,9,2}01` (g grass, s sand, d dirt, m mud; digit 3=S 7=N 8=NE 9=E 2=SE; idx 816–820 = worker grass). Swim/dive/climb are the lead's `assw`/`asdi`/`asgo`.

---

## Adversarial verification

An independent second pass re-derived every claim above from the Capstone disassembly and recorded a verdict per claim.

# verify_walkstep: adversarial re-check of re_walkstep.md / claims_walkstep.json

Everything below was re-derived from new Capstone dumps of Original-Ants/Ants.exe (w_b8cb.txt, w_ee84.txt, w_ccaf.txt, w_c4f2.txt, w_f780.txt, w_ad02.txt, w_fc50.txt, w_da46.txt) plus CHD reads (vw_walk.py, vw_idle.py, vw_sim.py). Ghidra was not used as evidence.

Overall: the per-step mechanics are right. The call-graph pseudocode for FUN_0101ee84, FUN_0101b8cb, FUN_0101c4f2, FUN_0100f17f, SetPosition, ClearPath/SetPath and FUN_0101ccaf matches the binary branch for branch. There are four substantive errors:
1. A plain move is **order 1**, not order 0.
2. The default scheduler is the **sorted list**, not the 8 ms wheel.
3. "Handled keeps the snap delta" is false for orders 3 and 0xf.
4. The action-3 half of claim 12 is wrong.

The report also misses two movement behaviours: a position overshoot in handled path-end cases, and stale waypoints that survive flights.

## Direct answer: the step on which the last waypoint centre is reached (plain move)

A plain move has order 1. `this` is local unless stated.

1. FUN_0102b997 consumes walk frame F (status 1, or 2 at the cycle end) and calls vt+0x38 = FUN_0101ee84. The child sprite is re-synced (0x101ee9b), and action 1 goes to FUN_0101b8cb (0x101f692).
2. WalkStep: `ntile == tile5a == wp[last]`, so there is no TryEnterTile. It reaches `+0x88 == 0`, then the arrival test (0x101c01c-0x101c043) passes. That sets `d = centre - pos` (0x101c045-0x101c060) and `cur34 = ntile` (0x101c066), then `xor ebx, ebx` and jumps to ARRIVE (0x101c06e).
3. ARRIVE:
   - Bomb test (0x101b938-0x101b9a3). A hit sets order 0xa and zeroes d.
   - Auto-engage (0x101b9a8-0x101ba37): type 4, local, more than 2 s since the last Order(), and an enemy within Chebyshev distance 2. A hit calls AttackTile, starts COMBEVT 2000, sets d = 0 and returns **without** advancing.
4. ADVANCE: `++wpIdx >= wpCount` (16-bit `jb`, 0x101ba57). Then ClearPath (0x101ba5b), which frees wp and zeroes count and index, then FUN_0101ccaf(e) (0x101ba63).
5. FUN_0101ccaf:
   - Preamble: `old = +0xac`, `+0xac = {0x5a,0x78}`, `wasHome = (+0x68 == 1)`, `+0x68 = 0`, `handled = 0` (0x101ccc4-0x101cd07). The order is **not** reset.
   - Order 1 jumps through table 0x101d680[1] to 0x101cd1d.
   - Local ant: if wasHome, then `+0x68 = 2` and `+0x70 = (+0x6c ? 0 : timeGetTime())`. Otherwise it queries the layer-2 id at `old`. If FUN_01007202 says powerup, it sends Msg09 and sets handled = 1. The pickup is then FUN_01020cdb: snap to the destination centre, then SetActionDefault(4).
   - Remote ant: 0x101cdc5, then 0x101d62d.
6. Not handled (0x101d62d):
   - Local ant: FUN_010214d9 builds Msg13 {PixelTile of the pre-step position, team, idx, force 0} and calls FUN_0100d791(msg, 0xa, 0). Type 0x13 has no state filter (table 0x1047320, filter dword for 0x13 = 0), so handler 0x10236c1 runs synchronously at 0x100d965. It calls FUN_01021664(tile, 0), which does:
     - snap to the tile centre through vt+0x18 (0x10216fa);
     - snap the child sprite;
     - StartPause(0) (0x1021738);
     - the "already idle" test (0x102173d-0x1021764) fails, because order is 1 **and** action is 1;
     - **SetActionDefault(0) at 0x1021769**, which always replays the idle animation (or the swim idle on water) and sets +0x88 = 0;
     - SetPath(0,0,1,0) at 0x1021775, so order becomes 0 and +0xac the sentinel.

     The message is then sent to peers (flag 8).
   - Remote ant: FUN_01021664(tile5a, 0) is called directly (0x101d659).
   - Then `e->dx = e->dy = 0` (0x101d667-0x101d66d).
7. The FUN_0101ee84 tail: `pos + 0`, so the occupancy update does nothing. The stepper sees that the frame list changed and calls `SetPosition(x38+0, y38+0)` from the current (snapped) position. It then adds idle dur0 again, so the first idle frame lasts 2× its duration.

To answer the questions directly:
- The walk animation does **not** keep running.
- FUN_0101ccaf itself never calls FUN_0101ace3 for orders 0, 1 or 0xc. It does so directly only in case 3 (0x101cf5d) and case 0xf (0x101d01d).
- Action 0 at path end is set by FUN_01021664.

The idle, action-0-with-waypoints branch is confirmed at 0x101bde2 → 0x101b932: `cur34 = tile5a`, no snap, then ARRIVE and ADVANCE, where `idx` goes 0→1 and SetAction(1, …, flag 1) runs. Idle animations have dx = dy = 0 and no events, so the step before the walk starts has no drift and no B/D side effects.

## CONFIRMED

- **C1** (vtable +0x28 / +0x18). Dword 0x1004c08 = 0x101a93a and 0x1004bf8 = 0x101a928. At 0x101a94f: `or [esi+0xe],0x40`. The tile is computed with signed idiv. 0x101a995 calls FUN_0100f17f. The fog call is at 0x101a9ab-0x101a9f8, with radius 6 and arg3 = world+0x4a58; it runs when the ant is local, or when P(localTeam)+0x68 == team.
- **C2** (FUN_0101ee84):
  - The child vt18(Pos) comes first (0x101ee9b).
  - wait → 0x101f71b: epilogue, no tail, d untouched.
  - frozen → d = 0 → 0x101f71b.
  - The switch is `cmp eax,0x14; ja` (unsigned). Table 0x101f72c, dumped:

    | Action | Entry |
    |---|---|
    | 0, 1 | 0x101eefc |
    | 2, 0x14 | 0x101ef5c |
    | 3 | 0x101f05c |
    | 4 | 0x101f111 |
    | 5 | 0x101f06f |
    | 6 | 0x101f237 |
    | 7 | 0x101f277 |
    | 8 | 0x101f52b |
    | 9 | 0x101f568 |
    | 0xa | 0x101f624 |
    | 0xb | 0x101f123 |
    | 0xc, 0xf | 0x101f1c3 |
    | 0xd | 0x101efbd |
    | 0xe, 0x13 | 0x101f5c1 |
    | 0x10 | 0x101f2b7 |
    | 0x11 | 0x101f401 |
    | 0x12 | 0x101ef04 |

  - Every case body matches §2.1. The SetPath calls push `ord = 1` (0x101f139, 0x101f3f1, 0x101f5b1), which does not matter because n = 0. vt+0x40 = 0x101aa5e returns +0xfc.
  - The tail is 0x101f697-0x101f70a.
  - The early exits skip the tail: case 0xc/0xf (0x101f226 / 0x101f232) and the 0x10 failure path (0x101f3fc).
- **C3** (StepEvt): 0x102b9b1-0x102ba49. Status = 2 if 0x1029b72 (cursor == 0 or cursor->next == 0) is true, else 1. Event and box come from the Next(1,1) frame. dx and dy are movsx of the current frame. WalkStep exits on status 0 at 0x101b900.
- **C4** (+0x88 snap on status 2): 0x101b906-0x101b935.
- **C5** (ARRIVE bomb): 0x101b938-0x101b9a3. FUN_01008bc6 accepts {0x81,0x82,0x84,0x83}. The layer-2 id is q+0x14 = [ebp-0x80]. The owner is never read.
- **C6** (auto-engage):
  - FUN_0101c0d5 is exactly as stated. The time test is `cmp ecx(2000),eax; sbb; neg`, i.e. unsigned `> 2000`. The hp test is a word `jbe`.
  - ARRIVE part (0x101b9a8-0x101ba37): pushes &t then 3, so FindEnemy(3,&t). It saves c0 = order, c4 = +0xac, c8 = +0x68, pushes 0x7d0, and sets d = 0 after the timer.
  - No-path part (0x101bd67-0x101bddd): FindEnemy(4), c4 = a fresh PixelTile, 0xbb8, d untouched.
  - FUN_0101dbec: ring order left column, right column, top row, bottom row, with the same clamps; `r` runs 1..radius-1 (16-bit `jb`). The test FUN_0101db55 returns `this->+0xfc == 0`, i.e. the searching ant's own frozen flag, not the target's.
  - FUN_0101da6f and FUN_0101dd6f match §2.5.
- **C7** (newTile / notOnWp / nudge): 0x101be42-0x101be87 and 0x101bf00. The nudge at 0x101bf34-0x101bf53 is `setle / dec / and 2 / dec`, so d <= 0 gives -1. ntile is recomputed after the nudge. The newTile flag is not.
- **C8**: 0x101bf0a `je 0x101bffc`, 0x101bfb4-0x101bff7, 0x101bffc. `c = centre(tile5a)` is computed once at entry (0x101b8ee).
- **C9** (arrival test and boundary restart):
  - Arrival test 0x101c009-0x101c070. 0x1034f20 is abs. The compares are `cmp bx,2 / ja` and `cmp ax,2 / ja`, on the possibly nudged n.
  - Boundary call (pushes at 0x101c0ad-0x101c0bf) = SetAction(1, e0, T(tile5a), T(ntile), 0, 1).
  - Restart rule 0x101b10a-0x101b146: restart if the action changed, or terrain was not provided, or +0x88 was set (then cleared), or, with flag set, the direction changed; with flag clear, if A != B.
- **C10** (harvest early stop): 0x101be8e-0x101bef7 → 0x101c06e. cur34 is not written on this path. That is harmless, because idx = count.
- **C11** (ADVANCE): 0x101ba3c-0x101bacd, 16-bit unsigned. FUN_01017531 (pushes &next then &cur34) indexes table 0x1002b28 = {7,0,1,6,0,2,5,4,3} with no clamp. Push order (1, dir, T(cur), T(next), 1, 1).
- **C12, action-0 part**: 0x101bd4f-0x101bded → 0x101b932. wp[0] is the start tile: the path message is built only when action is 0 or 3 and PixelTile == start (0x100cc2e-0x100cc5b).
- **C13, mechanism part**: StopSync → FUN_0100d791 → 0x10236c1 → FUN_01021664 → SetActionDefault(0) + SetPath(0), with d zeroed. Details in the direct answer. Only the order label is wrong (see Corrections).
- **C14, preamble and message types**: 0x101ccc4-0x101cd16. Table 0x101d680 dumped: 0 → 0x101d62d, 1/4 → 0x101cd1d, 2 → 0x101cdc5, 3 → 0x101cf13, 5 → 0x101ce07, 6 → 0x101d08a, 7 → 0x101d128, 8 → 0x101d310, 9 → 0x101d3b2, 0xa → 0x101d44f, 0xb → 0x101d51b, 0xc → 0x101d62d, 0xd → 0x101d1cd, 0xe → 0x101d26b, 0xf → 0x101cfcf. Orders > 0xf go to 0x101d627, which is not handled. The message builders push types 9, 7, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0x12, 0x19, 0x1a as listed.
- **C15** (FUN_0100f17f): 0x100f17f-0x100f2cb, including the unchanged-tile early return and the claim/multi logic.
- **C16** (FUN_0101c4f2): all branches re-read.
  - Msg08 argument order, F101d8ed range `(type==4) ? 4 : 1` (neg/sbb/`and 0xfd`/`add 4`), and flags 8 / 0x20 / 4 (with row-1 of hill2e) all as stated.
  - Local, non-last tile, occupant with action ≠ 0 and (action <= 2 signed, or > 9 and ≠ 0xb): StartPause(1), SetActionDefault(0), return 1, no snap (0x101c8b4-0x101c8fb).
  - Last tile with order 0xb: pause.
  - Give-up path 0x101c90f-0x101cb07, with the re-path argument lists exactly as in §2.3.
  - Non-local ants never return 0.
- **C18** (Blast / FUN_0101c221 / message 0x14): 0x101c34c-0x101c4d8, 0x1023749, 0x101c221-0x101c2df. world+0x4980 is set from anim slot 0xe0/4 = 56 'battle' (0x1017f8a).
- **C19** (FUN_0102151a / FUN_0101dded): 0x102151a-0x1021625 and 0x101dded-0x101de7b. SetAction case 3 forces dir 4 (0x101b3b3).
- **C20, argument part**: 0x1024d42 `push 1` before the `je` gives SetAction(savedA, savedD, 0xffff, 0xffff, 0, 1). No `[ebp+0x1c]` read occurs anywhere in 0x101ad02-0x101b48f. `[ebp+0x18]` is read only in the walk case.
- **C21** (Order prologue): 0x101fcc8-0x101fd2d.
- **C22** (CHD events):
  - Event 3 occurs only in ?gb/?gh/?bu (65 animations).
  - Event 4 only in ?at.
  - Event 5 in *h0, plus one more, 'hcen301'.
  - Walk animations for all 6 types × 4 terrains × 5 directions have 12 frames and only the sentinel event.
- **§3.2 timing table**: simulated independently (vw_sim.py) with the verified step rules and CHD frames, over all 8 directions and 6 types. Every row is reproduced:

  | Terrain | Orthogonal | Diagonal | Final snap |
  |---|---|---|---|
  | grass | 8 steps / 400 ms | 10 / 500 | (±5,±5) on the diagonal |
  | sand | 8 / 320 | 10 / 400 | |
  | dirt | 8 / 480 | 10 / 600 | |
  | mud | 15 / 900 | 20 / 1200 | orthogonal 4; diagonal 3 or 4 depending on type and direction |

  Boundary crossings:
  - Orthogonal: step 4 toward +x/+y, step 5 toward -x/-y.
  - Diagonal on grass, sand and dirt: step 6.
  - Mud: step 8 or 9 orthogonal, step 11 or 12 diagonal.

## CORRECTED

1. **Claim 13, §3.3 and the §2.9 label "order 0 (plain move)".** A plain move to an empty tile has **order 1**.
   - Evidence, order assignment: FUN_01020655 is called by Order() at 0x101fd48 and 0x101fecd. With a3 == 0 it does `push 1; pop ebx` (0x10207d0). If the tile has no food (f18&4), no powerup with a2, no bomb and no anthill, it reaches 0x1020898 `mov [esi+0xa8],ebx`, i.e. order 1. A same-team occupant also gives 1, and so does an enemy occupant when a2 == 0.
   - Evidence, command paths: the group (nearest-first) command loop calls Order(&tile, 1, flag, 0) (0x102899d-0x10289b2). Another caller, at 0x1028b1b-0x1028b26, uses Order(&tile, 0, 0, 0). Both reach the order-1 default.
   - Evidence, path message: the message copies ant+0xa8 into msg+0x1c (0x1022957). The handler passes that value to SetPath (0x1022ad1).
   - Consequence: at path end, case 1 (0x101cd1d) runs. It covers the wasHome state change and the powerup pickup via Msg09, which is handled (see the direct answer). Otherwise it falls through to StopSync. In FUN_01021664, `order == 0` is also false.
   - The final state for an ordinary destination is still idle, exactly centred, order 0. It gets there by a different route than the report says.
2. **Claim 20 timing and §4 "Scheduler … vtable 0x1005248: a timing wheel".** The default scheduler is the sorted-list one.
   - Evidence, selection: at 0x103155c, `cmp [0x104b448],edi; je 0x1031583` leads to ctor 0x1030d6a, vtable 0x1005208 (Add 0x1030e7b, RunOne 0x10310e8), stored at world+0xe88 (0x10315a7). [0x104b448] sits in the zero-filled tail of .data (offset 0x4448 is past raw size 0x4000). Only the "newtask" switch sets it (0x1031864-0x1031875); "oldtask" clears it. The wheel (ctor 0x1031211, vtable 0x1005248) is not used by default.
   - Evidence, list behaviour: Add(t,a,b,0) sets `+0x1c = a`, inserts with `due = timeGetTime() + a` (0x10311a8), then sets `+0x1c = b`. RunOne looks only at the head (0x103110f) and runs it if `now >= due`. If the task returns 1, it is re-inserted with `due = timeGetTime() + period` (0x10311b1).
   - Correct statement: onStart (0x1010f11, a bare `ret`) runs at the first idle-loop RunOne that finds the task at the head. onFire runs at least 300 ms after that run. The pause is 300 ms plus two scheduling latencies. It is not "300..308 ms".
3. **Claim 14, "handled=1 … keeps the snap delta".** False for orders 3 and 0xf.
   - Both cases fall through 0x101d07d-0x101d083, which zeroes d whether or not handled was set at 0x101d07a.
   - In case 3, if Order() fails at its gate (0x101fc84 → 0x101fef5, no state change), order stays 3, so handled = 1 and no stop is issued. The ant is left idle at the centre with order 3 and no path.
   - Missing from the list: cases 5 and 0xb also have a handled "carrying food, go home" branch (0x101ce8f-0x101cf0e and 0x101d5c7-0x101d624).
4. **Claim 12, action-3 part** ("ARRIVEs on each step with status != 0"). For action 3, only status-1 steps call WalkStep. Status 2 calls FUN_0102151a(0) instead (0x101f05c → 0x101f069 → 0x101f686). The action-3 animations are a?sd301, 25-30 frames, 2610-3835 ms. So a path installed during action 3 cuts the stun short on the next status-1 step, and FUN_0102151a(0) never runs.
5. **Claim 17, precision.**
   - The "enemy occupant is allowed if it is the order-3 target" exception applies only to local ants. For a non-local ant, 0x101f80a (IsLocal false) → 0x101f815, and without flag 0x80 **any** occupant blocks, including itself.
   - The bomb-rule ally is P(localTeam)+0x68 (0x101fbfd-0x101fc10), not the ant owner's ally.
6. **§3.2 item 3, "stays where it is, partly across the boundary".** Because d = 0, the crossing step is never applied. The waiting ant stays at its pre-step position, still inside tile5a, and occupancy does not change.
7. **§3.1, "Order() can return 0 early at … 0x101feee".** 0x101feee is `call FUN_010214d9` (StopSync: snap, idle, SetPath(0)) and then `return 0`. Only 0x101fc84 (gate FUN_0101ff5a, when a2 ≠ 0) returns with no side effects. Also, the path-message handler cancels the pause (0x1022a91) before it snaps.
8. **Minor.** The TryEnterTile "order 0 re-paths" label is really the default branch (every order outside {3,4,5,6,7,8,9,0xd,0xe}), which is where the common order-1 move goes. Msg15 is (tile, team, idx), not (tile, idx). Walk-table terrain indices are 0 grass, 1 sand, 3 mud, 4 dirt (2 = water, all 0x7ffe).

## UNVERIFIED

- Which tile each remaining handled-message handler method snaps to:
  - 0x1021494 (message 7)
  - 0x10210fa (message 0xb)
  - 0x10211f2 (message 0xc)
  - 0x1021915 (message 0xd)
  - 0x10219e8 (message 0xe)
  - 0x1021a6f (message 0xf)
  - 0x102184e (message 0x12)
  - 0x10212a3 (message 0x19)
  - 0x102137b (message 0x1a)

  Every one of them calls FUN_0101ad02 or FUN_0101ace3 and TileCentre + vt18 on its main path, so the report's open question ("does the action always change?") is answered yes for the main paths. I did not trace every internal branch.
- COMBEVT fire time. I confirmed that neither 0x1030508 nor 0x1024bc3 initialises +0x2c or +0x30, and that the object comes from 0x1029411 → 0x10350b0 (assumed to be CRT malloc, which does not zero). So I agree with the report's open question.
- Not re-derived: FUN_0101df5d, FUN_010100e5, FUN_0101cb0c, FUN_0101d8ed, FUN_0101d822, how FUN_0100f421 picks an occupant, and whether the message 8 handler (0x1022ca1) moves the attacker. If it does not, WalkStep's `d = centre(tile5a) - pos` pulls the ant back to the centre on that step.
- What world+0x4ae0 means. It is set to 1 at 0x100d1f3 when that function's second argument is 1 or 2, and it suppresses all sends, including the local dispatch.

## ADDITIONAL FINDINGS

1. **The local ant overshoots in handled path-end cases.**
   - For handled cases other than 3 and 0xf, FUN_0101ccaf keeps `d = centre - pos_old`.
   - The local handler then snaps the ant to a tile centre. For Msg09 → FUN_01020cdb, the snap to the destination centre is verified at 0x1020d17. The Order()-based go-home branches of cases 5 and 0xb do the same.
   - After the callback, FUN_0102b997 calls `SetPosition(x38 + dx, y38 + dy)` from the **current** (snapped) coordinates (0x102ba77-0x102ba8b).
   - Result: the local ant ends one final-step delta past the centre, e.g. +4 px on grass orthogonal and (±5,±5) on grass diagonal. After a powerup pickup it stays there, because action 4 ends with SetActionDefault(0) and no snap.
   - Remote copies are never handled (every case tests IsLocal first). They stop exactly at the centre, and the network message snaps them again. The error is visual only; the tile is the same.
   - The harvest early stop (d = 0) and cases 3/0xf (d zeroed) do not overshoot.
2. **Waypoints survive flights 0xe and 0x13.**
   - FUN_0101c221 (message 0x14: pile-up or fire-wall blast) and FUN_0101de7e (melee knockback, which sets 0xe or 0x13) change the action, order (0xc) and target without calling ClearPath. FUN_01010335 calls FUN_0101de7e only when the victim's +0x84 != 0 and the argument is not 8; otherwise it calls FUN_0102151a(1), which does clear the path. SetAction case 0xe (0x101afd1) does not clear the path either.
   - At the end of the flight, FUN_0101ee84 case 0xe/0x13 (0x101f5ce-0x101f61d) only snaps to TileCentre(+0xb0) and calls SetActionDefault(0).
   - So an ant blasted while walking lands idle **with its old path**. On its next idle step with status ≠ 0, the action-0 branch (0x101bde2) runs ARRIVE with `cur34` = the landing tile and `++wpIdx`. The ant walks on toward the waypoint *after* the one it was heading for, still with order 0xc.
   - FUN_01017531 does not clamp, so a non-adjacent waypoint gives an out-of-table direction.
   - If the ant was on its last segment, it stops instead: PathComplete order 0xc, not handled, StopSync.
   - Case 0xa's end calls FUN_0102151a(1), which does clear the path.
3. **When the axis nudge fires.** In normal walking it fires only on mud diagonals whose axes cross on different steps. The cause is the idiv asymmetry: the +axis crosses at a cumulative 16 px, the -axis at 17 px.
   - Mud NE: types 0, 3 and 4 (phase (1,1) first).
   - Mud SW: types 0, 1, 2, 4 and 5.
   - The nudged tile is then the waypoint, and the ant is shifted 1 px on both axes. The step count stays 20.
4. **When the walk animation could keep running with wp == NULL.** Only if the synchronous local dispatch of Msg13 is suppressed: world+0x4ae0 set, or 0x4ae4 set with arg3 = 0 (but 0x4ae4 also makes IsLocal false). In that case PathComplete only zeroes d, action stays 1, and WalkStep's no-path branch returns with the frame delta each step. The result is endless straight-line drift with no TryEnterTile. In normal play this cannot happen.
5. **Every SetAction restart inside a callback doubles the new first frame** (C5). This covers:
   - the path start;
   - a direction change at a waypoint;
   - a terrain change at a tile boundary (for example grass to sand gives 2 × 40 ms before the first sand delta);
   - the start of a dive or climb;
   - the idle animation at path end.

   Resumes from ANTPAUSE happen outside the callback and are not doubled.
6. **A bomb or auto-engage at ARRIVE zeroes d after the arrival snap.** The ant then stops at pos_old instead of on the centre: 4 px short on grass/sand/dirt orthogonal, 5 px per axis on those diagonals, 3-4 px on mud.
7. **FUN_0100f17f with a multi cell and n == 0** clears only bit 8 (0x100f215-0x100f262). The departing ant's team and index stay in the low byte, so the cell still reads as occupied. The collision verifier also noted this.
8. **Default path-start latency** is unchanged from the core-refute verification: idle dur0 (100 or 150 ms) after the path message, then 2 × walk dur0 until the first pixel moves. Auto-engage cannot fire at path start, because Order() has just reset +0x98.
