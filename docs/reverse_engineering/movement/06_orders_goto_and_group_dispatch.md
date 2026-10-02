# Cluster "orders": player move command -> ant movement (Ants.exe, Capstone-verified)

All addresses are VAs in Original-Ants/Ants.exe (image base 0x01000000). Every statement below was
checked in Capstone disassembly (cs.py). Ghidra (docs/legacy/Ants.exe.c) was only used to find
function boundaries; it drops pushed args and SEH locals in most of these functions.

Conventions: `world = *(0x104b350)`, `P(t) = world[0x4958 + 4*t]` (player/team object),
`localTeam = word world+0xf2a`, `isLocal(ant) = (world+0x4ae4 == 0 && ant+0x56 == localTeam)`
(FUN_0100cd7d @0x100cd7d). Tile = {int16 row, int16 col}. `ant(t,i)` = FUN_0100cfb1(P(t), i)
(@0x100cfb1: i < P+0x24 ? *(P+0x18 + i*P+0x1c) : 0).
SENTINEL tile = {row 0x5a, col 0x78}.

-----------------------------------------------------------------------------------------------
## 0. End-to-end pipeline (one paragraph)

Left-button release with a <5 px drag (FUN_01027530) or right-button release (FUN_01027b51)
-> FUN_010277f4 / FUN_01027b51 pick a command kind from the cursor mode (world+0x54e8)
-> FUN_010287b5(world, tile, special, attack) filters the local team's *selected* ants, drops
ants that already have the identical order, sorts the rest by Chebyshev distance to the target
and calls FUN_0101fc50(ant, &tile, 1, special, 0) for each
-> FUN_0101fc50 validates, SNAPS the ant to the centre of the tile under its pixel position,
forces action 0 (idle), cancels pause/combat timers, clears path+order, classifies the order
(FUN_01020655), resolves/adjusts the destination (FUN_010202e7 ring search, or FUN_01020128
adjacent tile for special orders) and queues an asynchronous path request (FUN_010246e8)
-> the PATHMGR task (every 50 ms) finishes one A* request per tick and calls FUN_0100cba4
-> if the ant is still idle on path[0] a type-6 network message is built (FUN_010228ff) and
executed locally (FUN_010229b7) + broadcast: snap to centre of path[0], re-enter the same action
(idle anim restarted), store waypoints (FUN_0101ab87, index 0)
-> on the next idle-animation frame step (after the idle's first-frame duration) the step
callback FUN_0101b8cb takes the "has waypoints && action 0/3" branch -> arrive/advance ->
index 1 -> FUN_0101ad02(1 walk, dir(path[0]->path[1]), terr(path[0]), terr(path[1]), 1, 1).
There is NO separate "move command" network packet: each client orders only its own ants and
broadcasts the resulting path (msg 6) / stop (msg 0x13) / action messages.

-----------------------------------------------------------------------------------------------
## 1. Input layer

### FUN_0102737e(world, int eventId, int state)  @0x102737e  ret 8
```
FUN_0102653f(world);
if (FUN_01012c3b(world+0x4a94, eventId, state)) return;       // modal dialog consumed it
if (world+0x4b18 != 2 || world+0x54e8 == 6 || world+0x5518 != 0) return;
for each listener in list world+0x5574: listener->vtbl[0x10](world+0x110, eventId, state);
if (eventId == 0xc) {                        // button A (left)
   if ((int16)state) { save press point (world+0x5528/0x552c/0x5530 <- world+0x114/0x118/0x11c);
                       world+0x5534 = FUN_01028751(world) /*view under mouse*/; FUN_010274be(); return; }
   FUN_01027530(world);                      // release
} else {                                     // other button (right)
   if ((int16)state) { same press bookkeeping; return; }
   FUN_01027b51(world);                      // release @0x1027460
}
release ref world+0x5534; world+0x5534 = 0;
```

### FUN_01027530 (left release) @0x1027530
If the press->release rectangle is < 5 px in both axes (@0x1027770 region, Ghidra-confirmed
shape) it converts the mouse to world pixels (FUN_0102fcdb) and calls
FUN_010277f4(world, pointObj{x,y}) (@0x10277d4); otherwise rubber-band selection.

### FUN_010277f4(world, Point p /*16-byte by value: +8 x, +0xc y*/) @0x10277f4 ret 0x10
```
tile.col = p.x / 32  (@0x102780f idiv)   tile.row = p.y / 32 (@0x1027820)
switch (world+0x54e8 /*cursor mode*/) {                 // @0x102782a dec-chain
 case 1: FUN_01028c44(world,1); break;                   // @0x10279db
 case 2: selection click ...                              // @0x10278dd (not an order)
 case 3: case 7:                                          // @0x1027a6a
   ok = FUN_010287b5(world, tile, 0, 0);  break;          // plain move/context
 case 4:                                                  // @0x1027a16
   ok = FUN_010287b5(world, tile, 1, 0);  break;          // "special" (power) order
 case 5:                                                  // @0x102785c
   FUN_01026904(world, &t2);   // ant under cursor; t2 left UNINITIALISED if none (orig. bug)
   ok = FUN_010287b5(world, t2, 0, 1);   break;           // explicit attack
 default: return;
}
if (ok) UI button feedback: FUN_01028360(...) or FUN_01028ffe(...) (BTNPUSH task, 125 ms);
// modes 3/4/5/7 always set [ebp-0x14]=1 (edi=1 before the test, @0x1027a82/0x1027a19/0x1027867)
FUN_01010627(world, p.x, p.y);   // @0x10279f4: click-marker sprite world+0x49c4 (template
                                 // world+0x4978) restarted at the raw pixel p, EVEN IF ok == 0
```

### FUN_01027b51 (right release) @0x1027b51 (main-view branch @0x1027cba)
```
if (world+0x5534 /*view under press*/ == world+0x4a88 /*minimap*/) {        // @0x1027b98
   if (world+0x54ec != 3 && world+0x54ec != 4) return;
   pt = FUN_01009850(world+0x4a58, viewport(pressPoint));  tile = pt/32;     // minimap -> world
   if (world+0x54ec == 4) mode = 3;                                            // @0x1027c24
   else if (world+0x54fc == 2 && FUN_01026f91(world,&tile,0)) mode = 4;
   else if (FUN_01026f91(world,&tile,1)) mode = 4; else mode = 3;
   FUN_01027e65(world, mode, 0); FUN_010277f4(world, pt); FUN_01027e65(world, 1, 0);  // temp mode
   // FUN_01026f91 = "tile is a valid special target for the homogeneous selection type"
} else if (world+0x5534 == world+0x4a7c /*main*/) {
   tile = mouse/32;  m = world+0x54e8;
   if (m == 3 || m == 4 || m == 7) {                                      // @0x1027d97
      if (world+0x54ec == 4 || selType(world,1) == 0 || selType(world,1) == 4)
           FUN_010287b5(world, tile, 0, 0);          // move          @0x1027e13
      else FUN_010287b5(world, tile, 1, 0);          // special       @0x1027dd2
   } else if (m == 5) { FUN_01026904(world,&t2); FUN_010287b5(world, t2, 0, 1); } // @0x1027d40
   else return;                         // modes <= 2 or 6: nothing
   UI feedback (mode 5: unconditional; modes 3/4/7: only if FUN_010287b5 returned 1);
   FUN_01010627(world, mouse.x, mouse.y);     // @0x1027e45: click marker, unconditional
}
```
`selType = FUN_010282e0(world, flag)` @0x10282e0: returns FUN_0100f9cb(ant,flag) common to ALL
selected local ants, 0 if mixed or none selected (sentinel 0x3e8 -> 0). So a right-click with a
homogeneous bomber/fire/thief/swimmer selection issues a *special* order; workers, combat ants or
mixed selections issue a move.

### FUN_01026904(world, Tile* out) -> Ant*  @0x1026904 ret 4
Scans the 3x3 tiles around the mouse tile (drow outer -1..1, dcol inner -1..1, @0x102696a/0x1026976),
takes each tile's occupant (FUN_0100f4ab mask 1) and hit-tests the mouse pixel against the ant's
box from FUN_01026a39: `L <= x < R && T <= y < B`. The LAST hit in scan order wins; `*out =`
FUN_0100ccc0(hit) (ant's current tile). Returns the ant or 0 (out untouched).

-----------------------------------------------------------------------------------------------
## 2. Group dispatch: FUN_010287b5(world, Tile tile, int special, int attack)  @0x10287b5 ret 0xc
```
P = P(localTeam); n = 0; none = 1;
for (i = 0; i < P+0x24; i++) {                                   // @0x10287ed loop
   a = ant(localTeam, i);
   if (!a || a+0x50 == 0 /*not selected: no marker sprite*/ || !FUN_0101ff5a(a)) continue;
   same = 0;  o = a+0xa8;                                         // @0x102881a
   if (o > 5) {
      if ((o >= 6 && o <= 9) || o == 0xd || o == 0xe)             // @0x102889e
         if (special && FUN_01028a11(a) == tile) same = 1;        // FUN_01028a11: +0xb0 for these orders
   } else if (o == 4 || o == 5 || o == 1) {                       // @0x1028874
      if (!special && !attack && a+0xac == tile) same = 1;
   } else if (o == 2) {                                           // @0x1028847
      FUN_0100f4ab(world, &tile, 0x10, &info);
      if ((info.flags & 2) && info.team == localTeam) same = 1;   // clicking own hill while going home
   } else if (o == 3) {
      if (attack && a+0xac == tile) same = 1;                     // @0x102883c
   }
   if (same) continue;
   none = 0;
   e[n].ant = a;  e[n].d = FUN_01020911(FUN_0100ccc0(a), &tile);  // 16*Chebyshev, @0x1028906
   n++;                                         // NOTE: e[] holds 16 entries (0x80 bytes of stack)
}
if (none) return 0;                                               // @0x1028929
// exchange sort ascending by d (strict '>' swap)                 // @0x102893e..0x1028994
for (p = 0; p < n; p++) for (j = p+1; j < n; j++) if (e[p].d > e[j].d) swap(e[p], e[j]);
for (k = 0; k < n; k++) {                                         // @0x102899d
   r = FUN_0101fc50(e[k].ant, &tile, 1, special, 0);              // @0x10289b2
   if (r && k == 0) {                       // acknowledgement from the closest ant only
      if (attack)                    FUN_0101b711(e[0].ant);   // text 0x43 "Attack!" + voice
      else if (special && n == 1)    FUN_0101b78a(e[0].ant);   // per-type special ack
      else if (!special)             FUN_0101b67b(e[0].ant);   // text 0x42/0x44/0x46 + voice
   }
}
return 1;
```
Note the `attack` flag is NOT forwarded to FUN_0101fc50. A plain move onto an enemy ant becomes
an attack anyway (FUN_01020655 gets arg2=1 from this path). `attack` only changes the target tile
source (ant under cursor), the duplicate test and the acknowledgement.

Acknowledgements (FUN_0100e8f5(world,id,flag) = show string resource `id` as status text;
voice = FUN_0102bd7e(world[0x4860+4*v])):
* FUN_0101b67b @0x101b67b: text 0x42 "On my way." except thief 0x46 "Here I go...", combat 0x44
  "Movin' out."; voice v: worker rand()%2?8:10, bomber 0x1b, fire 0xf, thief 0xc,
  combat rand()%2?0x13:0x14, swimmer 0x18.
* FUN_0101b711 @0x101b711: text 0x43 "Attack!"; v: worker 0x2f, bomber 0x35, fire 0x31,
  thief 0x30, combat rand()%2?0x33:0x32, swimmer 0x34.
* FUN_0101b78a @0x101b78a: worker/combat nothing; bomber v 0x1c; fire v 0x10 + text 0x47
  "Burn..."; thief v 0xd + text 0x45 "My pleasure..."; swimmer v 0x19.

-----------------------------------------------------------------------------------------------
## 3. Accept predicate FUN_0101ff5a(ant) @0x101ff5a
```
if (FUN_0100f9cb(ant,1) != FUN_0100f9cb(ant,0)) return 0;
if (ant->vtbl[0x40]() /*0x101aa5e: return ant+0xfc frozen*/) return 0;
if (ant+0x84 != 0) return 0;        // "held/engaged" flag, set to 1 by FUN_01020c70 @0x1020c79
a = ant+0xe4;  return (a == 0 || a == 1 || a == 3);
```
FUN_0100f9cb(ant, flag) @0x100f9cb:
```
if (flag && (type==1 || type==2 || type==5)) {        // type = int16 ant+0x54
   if (action in {0,1,3}) return (timeGetTime() == ant+0x4c) ? 0 : type;   // @0x100fa16..0x100fa25
   return 0;
}
return type ? type : FUN_01021087(map+0x70 /*map default type*/);          // @0x100fa2c
```
So in practice: an order is accepted iff not frozen, +0x84==0 and action in {0 idle, 1 walk, 3}.

-----------------------------------------------------------------------------------------------
## 4. Per-ant order: FUN_0101fc50(Ant* this, Tile* dst, int player, int special, int unused)
@0x101fc50  ret 0x10.  (arg4 is never read.)
```
Tile t = *dst;                                            // [ebp-0x20]  @0x101fc64
if (player && !FUN_0101ff5a(this)) return 0;              // @0x101fc7b
FUN_0100f4ab(world, &t, 0x90, &info);                     // anthill(0x10)+object(0x80) query
if (info.flags & 2) {                                     // t is on an anthill   @0x101fca2
   if (info.team == this+0x56) t = info.anchor;           // info+0x28: hill anchor tile
   else if (this+0x54 == 3 /*thief*/) t = info.stealTile; // info+0x2c = P(team)+0x32
   else { FUN_010214d9(this); return 0; }                 // non-thief on enemy hill: STOP
}
Tile cur = FUN_0100ccc0(this);                            // pixel pos / 32   [ebp-0x14]
FUN_0101ace3(this, 0);                                    // action 0 (idle anim restarted)
this->vtbl[0x18](FUN_0100cd00(&cur));                     // SNAP to (cur.col*32+16, cur.row*32+16)
FUN_0101cc1e(this, 0);                                    // cancel ANTPAUSE (+0x60/+0x64)
FUN_0101ab87(this, 0, NULL, 1, NULL);                     // clear waypoints, +0xa8=0, +0xac=SENTINEL
FUN_0101c152(this);                                       // cancel COMBEVT task (+0x80)
this+0x98 = timeGetTime();                                // order timestamp  @0x101fd2d
Tile orig = t;                                            // bx / [ebp+8..0xa]
FUN_01020655(this, &t, special, player);                  // sets +0xa8, +0xac=t, +0xb0.. @0x101fd48
ok = 1; homeRedirect = 0;
switch (this+0xa8) {                                      // table 0x101ff2a, index order-3
 case 3:  if (FUN_0101ffab(this)) return 0;               // ally-attack dialog opened
          /* fallthrough */ goto dflt;
 case 11: if (FUN_0101ffab(this)) return 0;
          goto finish;                                    // enemy hill (thief): no dest resolution
 case 6: case 8:  v = FUN_0101d762(world, &t, 1); goto special_;
 case 7:          v = (FUN_01008b3d(map, 2, t.row, t.col) == 0x86); goto special_;
 case 9:          v = FUN_0101d7f9(world, &t);    goto special_;
 case 13:         v = FUN_0101d6d6(world, &t, 1); goto special_;
 case 14:         v = (FUN_01008b3d(map, 2, t.row, t.col) == 0x25); goto special_;
 default: dflt:                                           // orders 0,1,2,4,5,10,12,...  @0x101fe74
   ok = FUN_010202e7(this, &t, player, 1, 1, 5);
   if (t != orig) {
      if (orig == P(this+0x56)+0x2e /*home*/) homeRedirect = 1;   // @0x101feb1
      FUN_01020655(this, &t, 0, player);                  // re-classify at the new tile
   }
   goto finish;
}
special_:                                                 // @0x101fe07
 if (v && FUN_01020128(this, &t)) { this+0xb0 = orig /*clicked tile as dword*/; goto finish; }
 FUN_0101ace3(this, 0xb);                                 // "can't" action
 FUN_0101ab87(this, 0, NULL, 1, NULL);
 FUN_0100e8f5(world, 0x30, 0);                            // text "Can't do that..."
 ok = 0;
finish:                                                   // @0x101fed2
 if (homeRedirect) { this+0x68 = 1; this+0x6c = player; } else this+0x68 = 0;
 if (!ok) { FUN_010214d9(this); return 0; }               // broadcast stop @0x101fef0
 FUN_010246e8(world+0x4968 /*PATHMGR*/, this+0x58 /*index*/, &cur, &t);   // @0x101ff11
 return 1;
```
Key facts
* The ant is always snapped to the centre of the tile containing its CURRENT PIXEL position and
  set idle before anything else (only the enemy-hill-non-thief and !FUN_0101ff5a paths skip it).
* Paths always start at `cur` (the snapped tile). The destination `t` may be the ring-search
  substitute (default orders) or the chosen 4-neighbour (special orders).
* Return 1 means "path requested"; movement starts only after the PATHMGR delivers the path.

-----------------------------------------------------------------------------------------------
## 5. Order classification FUN_01020655(this, Tile* t, int special, int player) @0x1020655 ret 0xc
```
FUN_0100f4ab(world, t, 0xb9, &info);  // 0x80 obj,0x20 occ,0x10 hill,0x08 layer2 id,0x01 ant
if (info.flags & 2) {                                   // anthill tile
   if (info.team == this+0x56) this+0xa8 = 2;           // go home
   else { this+0xa8 = 0xb; (int16)this+0xb0 = info.team; }   // enemy hill (steal)
} else if (special) {
   switch (FUN_0100f9cb(this,0)) {
    case 1: this+0xa8 = 8;                               // bomber
            if (!FUN_0101d762(world,t,1) && FUN_0101d7f9(world,t)) this+0xa8 = 9; break;
    case 2: this+0xa8 = 6;                               // fire ant
            if (!FUN_0101d762(world,t,1) && layer2(t) == 0x86) this+0xa8 = 7; break;
    case 5: this+0xa8 = 0xd;                             // swimmer
            if (!FUN_0101d6d6(world,t,1) && layer2(t) == 0x25) this+0xa8 = 0xe; break;
    default: /* worker/thief/combat: +0xa8 left unchanged (0 after FUN_0101ab87) */ ;
   }
} else if (info.flags & 1) {                             // occupied by an ant (info+8)
   A = info.ant;
   if (A+0x56 == this+0x56 || !player) this+0xa8 = 1;
   else { this+0xa8 = 3; (int16)this+0xb0 = A+0x56; (int16)this+0xb2 = A+0x58; }   // attack
} else if (info.flags & 4) {                             // food object (info+0x38)
   this+0xa8 = 5; this+0xb0 = info.obj; this+0xb4 = {obj+8, obj+0xa} /*anchor*/;
} else if (FUN_01007202(map, info.layer2) /*tileinfo flag 4 powerup*/ && player) this+0xa8 = 4;
else if (FUN_01008bc6(map, info.layer2) /*0x81..0x84 bomb*/) this+0xa8 = 0xa;
else this+0xa8 = 1;                                     // plain move
this+0xac = *t;                                          // @0x102089e
```
Order codes: 1 move, 2 home, 3 attack ant (+0xb0 team,+0xb2 index), 4 get power-up,
5 harvest food (+0xb0 obj,+0xb4 anchor), 6 fire-ant ignite tile, 7 fire-ant at fire wall 0x86,
8 bomber drop bomb, 9 bomber at existing bomb, 0xa go to bomb tile, 0xb thief at enemy hill
(+0xb0 team), 0xd swimmer special on water, 0xe swimmer special on bridge piece 0x25,
0xf (set elsewhere; fight).

Special-target validators:
* FUN_0101d762(world,t,f) @0x101d762: terrain(t) in {0 grass,1 sand,4 dirt} && layer2(t)==0x7ffe
  (empty) && !FUN_0100cf0f(map,t) (layer-1 cell bit0 "blocked") && !FUN_0101d822(t) (no team's
  hill tiles +0x32 / +0x36..+0x40 / +0x2e) && !(f ? FUN_0100f3e8(t) : FUN_0100f3ca(t)).
* FUN_0101d6d6 @0x101d6d6: same but terrain(t)==2 (water).
* FUN_0101d7f9 @0x101d7f9: layer2(t) in 0x81..0x84 (bomb).
* FUN_0100f3ca(t) @0x100f3ca: occupancy bit (grid world+0x553c low 3 bits != 7).
  FUN_0100f3e8(t) @0x100f3e8: occupant exists AND is stationary (no waypoints && action in
  {0,3..9,0xb}).

-----------------------------------------------------------------------------------------------
## 6. Destination resolution FUN_010202e7(this, Tile* t, int a2, int a3, int a4, uint16 maxRing)
@0x10202e7 ret 0x14.  Called from FUN_0101fc50 as (this,&t,player,1,1,5).
```
home = P(this+0x56)+0x2e;
f100 = (this+0xa8 == 3 && this+0xac == *t) ? 0x100 : 0;  // always true for attack (0xac just set)
mask = (a2?0x20:0)|(a3?1:0)|(a4?2:0)|(a2?8:0)|(a2?0x10:0)|((*t==home)?4:0)|f100;
if (FUN_0101f780(this, t, mask, NULL)) return 1;         // target itself acceptable
if (*t == home) {                                        // @0x10203c5
   save = *t;  *t = *(Tile*)(P(this+0x56)+0x46);        // alternative waiting tile
   if (FUN_010202e7(this, t, a2, a3, a4, maxRing)) return 1;
   *t = save; return 0;
}
mask &= 0xffd3;                                          // drop 0x20,0x08,0x04 for the ring search
row = t->row; col = t->col;
for (r = 1; r < maxRing; r++) {                          // rings 1..4 for maxRing 5
   if (col - r >= 0)                                     // A: left column, top->bottom
      for (rr = max(0,row-r); rr <= row+r; rr++) TRY(rr, col-r);
   for (rr = max(0,row-r); rr <= row+r; rr++) TRY(rr, col+r);          // B: right column
   if (row - r >= 0)                                     // C: top row, corners excluded
      for (cc = max(0,col-r+1); cc <= col+r-1; cc++) TRY(row-r, cc);
   for (cc = max(0,col-r+1); cc <= col+r-1; cc++) TRY(row+r, cc);      // D: bottom row
}
return 0;
TRY(R,C): if ((uint16)R < map+0xd0 && (uint16)C < map+0xd2 && FUN_0101f780(this,&{R,C},mask,0))
            { *t = {R,C}; return 1; }
```
First acceptable tile in that exact scan order wins (not nearest-by-distance within a ring).

-----------------------------------------------------------------------------------------------
## 7. Tile acceptability FUN_0101f780(this, Tile* t, uint mask, TileInfo* info) @0x101f780 ret 0xc
Mask bits: 0x01 own-hill anchor only / entrance-reservation, 0x02 not already a teammate's move
target, 0x04 own anthill enterable (target==home), 0x08 power-up objects passable, 0x10 hill
entrance reservation check, 0x20 bombs passable, 0x40 ignore occupant, 0x80 moving friendly
occupant passable, 0x100 skip the entrance-reservation block.
```
if (!info || info->mask != 0x1db) { FUN_0100f4ab(world, t, 0x1db, &local); info = &local; }
terr = info->terrain;                                                  // +0x10
if (passTable[terr] == 0) {           // word table 0x10049b8 = {1,1,0,1,1,0,0,0}
   if (terr != 2 || FUN_0100f9cb(this,0) != 5) return 0;              // only swimmers on water
}
A = info->ant;                                                         // +8
if (A && !(mask & 0x40)) {                                             // @0x101f7e7
   if (!((A == this || A+0x56 != this+0x56) && isLocal(this))) {       // friend, or remote self/enemy
      if (!(mask & 0x80)) return 0;                                    // @0x101f815
      if (A+0xd8 == 0) { a = A+0xe4;                                   // A has no path:
         if (a == 0 || (a >= 3 && a <= 9) || a == 0xb) return 0; }     //   stationary -> blocked
   }
   if (isLocal(this) && A != this && A+0x56 != this+0x56) {            // enemy occupant @0x101f84f
      if (this+0xa8 != 3 || this+0xb0 != A+0x56 || this+0xb2 != A+0x58) return 0; // only the attack target
   }
}
if (info->flags & 2) {                                                 // anthill tile @0x101f896
   if (this+0xa8 == 0xb && this+0xb0 == info->team && *t == info->stealTile) goto L_b35;
   if (info->team != this+0x56) return 0;
   if (!(mask & 4)) return 0;
   if ((mask & 1) && *t != info->anchor) return 0;
   goto L_b35;
}
if (info->blocked /*+0x1c = FUN_0100cf0f layer-1 bit0*/) {             // @0x101f913
   if ((mask & 8) && FUN_01007202(map, info->layer2)) return 1;       // power-up
   if (info->layer2 == 0x86 && FUN_0100f9cb(this,0) == 2) return 1;   // fire ant into fire wall
   if ((info->flags & 4) && this+0xa8 == 5 && this+0xb0 == info->obj) return 1;  // own food target
   return 0;
}
if (!(mask & 0x100)) {                                                 // entrance reservation @0x101f980
   for (tm = 0; tm < 4; tm++) { p = P(tm); if (!p || p+0x64) continue;
      A1={p+0x36,p+0x38}; A2={p+0x3a,p+0x3c}; A3={p+0x3e,p+0x40};
      if (*t != A1 && *t != A2 && *t != A3) continue;
      if (tm != this+0x56) return 0;
      if (!(mask & 1) || !(mask & 0x10)) continue;
      c1 = occ(A1); c2 = occ(A2); c3 = occ(A3);                        // FUN_0100f3ca 0/1
      for each ant o of team tm, o != this, o+0xa8 == 1:
         if (o+0xac == A1) c1++; if (o+0xac == A2) c2++; if (o+0xac == A3) c3++;
      if (c1 + c2 + c3 == 2) return 0;                                 // exactly 2 (sic) @0x101fb18
   }
}
L_b35:
if (mask & 2) {                                                        // @0x101fb35
   for each ant o of this team, o != this:
      if ((o+0xa8 == 1 || o+0xa8 == 2) && o+0xac == *t) return 0;      // teammate already heading here
      if (*t == P(team)+0x2e && this+0x68 != 2 && o+0x68 == 2) return 0; // hill queue owner
}
if (mask & 0x20) return 1;
if (FUN_01008bc6(map, info->layer2)) {                                 // bomb on tile
   own = info->owner2 /*+0x22 layer-2 owner word*/;
   if (own == this+0x56 || own == P(localTeam)+0x68 /*local ally*/) return 0;
}
return 1;
```

-----------------------------------------------------------------------------------------------
## 8. Special-order approach tile FUN_01020128(this, Tile* t) @0x1020128 ret 4
```
cur = FUN_0100ccc0(this);
cand = { {t.row-1,t.col}, {t.row+1,t.col}, {t.row,t.col-1}, {t.row,t.col+1} };   // N,S,W,E
for i: d[i] = (in bounds (uint16 compare) && FUN_0101f780(this,&cand[i],0x81,0))
              ? FUN_01020911(&cur,&cand[i]) : 8000;
best = first i with minimal d (strict '<', start 8000);                       // @0x10202a9
if (d[best] == 8000) return 0;  *t = cand[best]; return 1;
```
FUN_01020911(a,b) @0x1020911 = 16 * max(|a.row-b.row|, |a.col-b.col|).

-----------------------------------------------------------------------------------------------
## 9. Ally confirmation FUN_0101ffab(this) @0x101ffab and callback FUN_01020076 @0x1020076
```
FUN_0101ffab: tm = (int16)this+0xb0;
  if (P(localTeam)+0x68 /*ally team*/ != tm) return 0;
  if (!FUN_01012bc1(world+0x4a94) /*no dialog open*/) {
     this+0xd2 = tm; this+0xcc = this+0xa8; if (this+0xa8 == 3) this+0xd0 = this+0xb2;
     d = new(0xa8) FUN_01016438(..., 0x1020076 /*callback*/, this);   // confirm dialog
     FUN_01012b5b(world+0x4a94, d, 0);
  }
  return 1;             // FUN_0101fc50 then returns 0 (ant already snapped + idle)
FUN_01020076(int yes, Ant* a):
  if (!yes) { FUN_0101ace3(a,0); FUN_0101ab87(a,0,0,1,0); }
  else { if (P(localTeam)+0x68 == a+0xd2) FUN_01010d26(P(localTeam));   // break alliance
         if (a+0xcc == 3) { T = ant(a+0xd2, a+0xd0); if (T) FUN_0101fc50(a, FUN_0100ccc0(T), 1, 0, 0); }
         else if (a+0xcc == 0xb) FUN_0101fc50(a, &P(a+0xd2)+0x2e, 1, 0, 0); }
  a+0xcc = 0;
```

-----------------------------------------------------------------------------------------------
## 10. Stop-at-tile: FUN_010214d9 @0x10214d9 -> msg 0x13 -> FUN_01021664 @0x1021664
```
FUN_010214d9(a): if (!isLocal(a)) return;
   m = FUN_0102368f(FUN_0100ccc0(a), a+0x56, a+0x58, 0);   // type 0x13, 16 bytes:
                                                           // +8 team(|0x8000 if flag), +0xa idx, +0xc tile
   FUN_0100d791(world, m, 0xa, 0);   // runs local handler 0x10236c1 now, then network send
handler 0x10236c1: FUN_01021664(ant(team&0x7fff, idx), m.tile, m.team & 0x8000);
FUN_01021664(a, Tile tile, int remoteFlag):
   if (remoteFlag && (isLocal(a) || a+0xe4 in {0xe,0x13,10} || a+0x84 == 1 ||
                      timeGetTime() - a+0x100 < 2000)) return;
   a->vtbl[0x18](centre(tile));  if (a+0x50) marker->vtbl[0x18](pos(a));
   FUN_0101cc1e(a, 0);
   if (a+0xd8 || a+0xa8 || (a+0xe4 != 0 && (a+0xe4 <= 2 || (a+0xe4 > 9 && a+0xe4 != 0xb))))
      { FUN_0101ace3(a, 0); FUN_0101ab87(a, 0, 0, 1, 0); }
```

-----------------------------------------------------------------------------------------------
## 11. Path request and delivery

FUN_010246e8(PathMgr* pm, int16 idx, Tile* from, Tile* to) @0x10246e8 ret 0xc:
remove every queued request in list pm+0x2c whose +8 == idx, then append
new FUN_010197ed(idx, from, to) (0x34 bytes, vtbl 0x1004990: +8 idx, +0xc ant(localTeam,idx),
+0x10 from, +0x14 to, +0x24 = 4 ...). Only the LOCAL team is ever pathed.

PATHMGR task (ctor 0x102463a, vtbl 0x1004e28, name "PATHMGR") is scheduled at 0x100e50f with
FUN_01031e92(task, delay 0, interval 0x32 = 50 ms, 0). Execute 0x1024786:
```
if (pm+0x34 /*count*/ == 0) return 1;
do { r = pop head of pm+0x2c;
     done = FUN_01019a66(r, 1000);           // A* slice of 1000 expansions (pathfinding cluster)
     if (done && r+0x28 == 0) FUN_0100cba4(world, r+8, r+0x18 /*tiles*/, r+0x1c /*count*/);
     else append r to tail;                   // unfinished (or done with r+0x28 set)
     release(r);
} while (!done);                              // i.e. exactly one request completes per tick
return 1;                                     // -> rescheduled 50 ms later
```
FUN_0100cba4(world, idx, Tile* path, uint16 count) @0x100cba4 ret 0xc:
```
a = ant(localTeam, idx); if (!a) return;
if (count == 0) { if (isLocal(a)) { FUN_010214d9(a); FUN_0101ace3(a, 0xb);
                                    FUN_0100e8f5(world, 0x3a, 0); /*"Can't go there."*/ } return; }
if (a+0xe4 != 0 && a+0xe4 != 3) return;                  // ant busy -> path DISCARDED
if (FUN_0100ccc0(a) != path[0]) return;                  // ant moved -> DISCARDED
m = FUN_010228ff(localTeam, idx, &centre(path[0]), a, count, path);   // msg type 6
FUN_0100d791(world, m, 0xa, 0);                          // local execute + broadcast
```
FUN_0100d791(world, msg, flags, force) @0x100d791: returns without doing anything if
(world+0x4ae4 != 0 && !force) or world+0x4ae0 != 0; optionally records (world+0x5330); if
the type has a state-gate entry `[0x1047324 + 8*type]` (types 0..7 only) the gate may veto it;
if flags&0x10 sends first; then calls the local handler `[0x1047320 + 8*msg.type](msg)`
(@0x100d965); if flags&8 sends to the network afterwards (flags 0xa = local then send).
Types 6 (path) and 0x13 (stop) have no gate, so they always execute locally at once.
FUN_010228ff @0x10228ff builds type 6, size count*4+0x26: +8 team, +0xa idx, +0xc x, +0x10 y,
+0x14 dir(a+0xe0), +0x18 action(a+0xe4), +0x1c order(a+0xa8), +0x20 = a+0xb4 if order==5,
+0x24 = 2*count, +0x26.. {row,col} words.
Handler FUN_010229b7 @0x10229b7 (table 0x1047320 slot 6 = 0x1047350):
```
a = ant(m.team, m.idx);  if (!a || a+0xe4 == 0xc || a+0xe4 == 0xf) goto free;
FUN_0101cc1e(a, 0);
a->vtbl[0x18](Point{m.x, m.y});                          // snap to centre of path[0]
FUN_0101ad02(a, m.action, m.dir, 0xffff, 0xffff, 0, 1);  // same action again (idle anim RESTART)
if (a+0xe4 != 0xc && a+0xe4 != 0xf)
   FUN_0101ab87(a, m.count, tiles, m.order, &m.target);  // +0xd8/+0xdc, +0xde=0, +0xa8, +0xac=last
```
FUN_0101ab87(a, uint16 n, Tile* w, int order, Tile* tgt) @0x101ab87 ret 0x10: clear path
(FUN_0101ab56), +0xa8=0, +0xac=SENTINEL; if n>0: copy n tiles to new +0xd8, +0xdc=n, +0xde=0,
+0xa8=order, +0xac=w[n-1]; if order==5: +0xb4=*tgt, +0xb0=FUN_01008c63(map,*tgt).

-----------------------------------------------------------------------------------------------
## 12. How the first step starts (FUN_0101b8cb @0x101b8cb, reached from FUN_0101ee84 action 0)
The re-entered idle animation (frames dx=dy=0) steps normally. On its first real step
(evt[5]=1 or 2; evt[5]==0 returns at 0x101b900) the local-ant special checks at 0x101baef..
0x101bd4f are skipped (they need evt[4]==3 or "no waypoints"), then at 0x101bd67:
`if (+0xd8 != 0 && (+0xe4 == 0 || +0xe4 == 3)) goto 0x101b932` (arrive/advance):
```
cur = +0x5a;                                          // == path[0]
if (isLocal && layer2(cur) is bomb 0x81..0x84) {      // @0x101b953 (any owner)
   +0xa8 = 0xa; +0xac = cur; d = 0; +0xde = +0xdc;     // then jmp 0x101ba3c: index++ >= count
}                                                     //  -> path ends, FUN_0101ccaf(order 0xa)
else if (FUN_0101c0d5(this) && (+0xe4 == 1 || +0xe4 == 0) && FUN_0101dbec(this,3,&cur)) {
   save order (+0xc0=+0xa8, +0xc4=+0xac, +0xc8=+0x68); FUN_0101da6f(this,&cur);
   FUN_0101c184(this, 2000); d = 0; return; }        // combat ant auto-engage
+0xde++;                                              // 0 -> 1
if (+0xde >= +0xdc) { FUN_0101ab56(this); FUN_0101ccaf(this, evt); return; }   // arrival
nxt = waypoint[+0xde];
dir = FUN_01017531(&cur, &nxt);                       // table 0x1002b28[drow+1][dcol+1] = 7,0,1,6,0,2,5,4,3
FUN_0101ad02(this, 1, dir, FUN_01008af7(cur), FUN_01008af7(nxt), 1, 1);       // @0x101bacd
```
FUN_0101c0d5 @0x101c0d5 (auto-engage gate): type==4 && isLocal && (timeGetTime()-(+0x98)) >
2000 (unsigned) && action not in {3,0xc,0xf} && hp(+0x74)>0 && !frozen && +0x84!=1 && order not
in {3,0xf}. The +0x98 stamp written by FUN_0101fc50 therefore suppresses auto-engage for 2 s after
every order.

Timing from path delivery to first pixel of motion (grass worker):
idle restart at T -> idle first frame -> at T+first the walk anim is started from inside the
step callback, so (lead claim C5) its first frame is booked twice: first 4-px move at
T + 150 + 2*50 ms for a grass worker. Path delivery itself happens on a PATHMGR tick (<= 50 ms
+ the latency of the scheduler passes that process the list head after the request; *(corrected: the default scheduler is the sorted list, not the 8 ms wheel of the
non-default `newtask` mode, see C2 in the verification notes)*) and only one ant's path completes per tick.
Idle first-frame durations (static tables 0x1002cb8 idle / 0x1002e38 carrying, [type*8+dir],
dirs 0..4; CHD Table-4 names): worker agst*/hgst* 150; bomber abst* 100; fire afst* 100;
thief atst* 100; combat acst* 150 except dir 4 (acst301/hcst301) 125; swimmer asst* 150.
(Swimmer on water uses world+0x47c0[colour], not resolved here.)

-----------------------------------------------------------------------------------------------
## 13. New order issued while the ant is mid-stride
There is no stride completion or reversal. FUN_0101fc50 (@0x101fcc8..0x101fcf6):
1. `cur = FUN_0100ccc0(this)` = (pixel x/32, pixel y/32) of the ant NOW (signed idiv, positions >= 0).
2. FUN_0101ace3(this,0) -> FUN_0101ad02(0, +0xe0, -1, -1, 0, 0): action 1 has no cleanup entry
   (table 0x101b48f index -1 -> skipped), facing kept, idle anim restarted, +0x88 cleared.
3. vtbl+0x18 (0x101a928 -> 0x101a93a) sets +0x38/+0x3a to (cur.col*32+16, cur.row*32+16) and
   updates occupancy (FUN_0100f17f) and fog (FUN_01006af4 radius 6).
So an ant between tile A (left) and B (right) at x = A.cx + k: for k in 0..15 it jumps BACK to
A's centre, for k in 16..31 it jumps FORWARD to B's centre (B already owns the ant in the
occupancy grid since FUN_0101ee84 updates the tile every step). The new path starts from that
tile; the ant idles there until PATHMGR delivers it (sections 11-12).
(0x101b590 is not a function: it is inside FUN_0101b52f @0x101b52f, which creates the selection
marker sprite +0x50.)

-----------------------------------------------------------------------------------------------
## 14. States / situations that reject a move order
* Not selected (+0x50 == 0) — FUN_010287b5 @0x1028801.
* FUN_0101ff5a false: frozen (+0xfc), +0x84 != 0, action not in {0,1,3} (every other action:
  2, 4 get-power-up, 5..9, 0xa hit, 0xb can't-go, 0xc, 0xd, 0xe, 0xf, 0x10..0x14), or the
  FUN_0100f9cb(1)/(0) mismatch (types 1/2/5 only; effectively never when action in {0,1,3}).
* Same order to same target already active (dedupe table in section 2) — silently skipped.
* Non-thief clicking an enemy anthill: FUN_010214d9 -> the ant STOPS at its current tile centre
  (FUN_01021664 clears path/order), no text.
* Target (and whole 4-ring neighbourhood) unacceptable (FUN_010202e7 == 0): FUN_010214d9 stop,
  no text.
* Special order with invalid target or no acceptable 4-neighbour: action 0xb, order cleared,
  text 0x30 "Can't do that...", stop broadcast.
* Attack/steal on an ally: confirmation dialog; ant is left idle until answered.
* PATHMGR returns count 0: stop, action 0xb, text 0x3a "Can't go there.".
* Path discarded (no message) if, when it arrives, the ant's action is not 0/3 or it is no longer
  on path[0]; the ant then just stays idle with +0xa8/+0xac as set by FUN_01020655.
* Msg-6 handler ignores ants in action 0xc or 0xf.

-----------------------------------------------------------------------------------------------
## 15. Blocked next tile while walking: FUN_0101c4f2(this, Tile* nt) @0x101c4f2 ret 4
(called from the walk step callback; returns 1 = go on, 0 = handled)
```
cur = FUN_0100ccc0(this); FUN_0100f4ab(world, nt, 0xdb, &ni); FUN_0100f4ab(world, &cur, 0xdb, &ci);
if (isLocal && (order==3 || order==0xf) && ni.ant && ni.ant is the attack target (+0xb0/+0xb2)) {
   if (ni.terrain == 2 || ci.terrain == 2) { stop; ace3(0xb); text 0x3a; clear path;
                                            if (type(+0x54)==4) FUN_0101dd6f(this); return 0; }
   if (!FUN_0101cb0c(this,&+0x5a) || P(team)+0x68 == +0xb0) { stop; ace3(0xb); text 0x30;
                                            clear path; if (+0x54==4) FUN_0101dd6f(this); return 0; }
   m = FUN_01022c57(+0xb0, +0xb2, team, idx, target+0x5a, +0x5a,
                    FUN_0101d8ed(&target+0x5a, &+0x5a, type==4 ? 4 : 1), type==4);
   FUN_0100d791(world, m, 0xa, 0); return 0;                     // start fight
}
f = 0;  if (order==4 && *nt == +0xac) f = 8;  if (order==0xa && *nt == +0xac) f |= 0x20;
if (order==2 || cur==home || cur=={home.row-1,home.col}) f |= 4;
if (FUN_0101f780(this, nt, f, &ni)) return 1;
isLast = (*nt == waypoint[count-1]);
if (!isLocal) {                                                        // @0x101c7d9
   if (ni.terrain == 2 && type != 5) { FUN_0101cc1e(1); snap to centre(FUN_0100ccc0(this));
                                       FUN_0101ace3(this,0); return 1; }
   if (isLast) { if (order == 3 || order == 0xf) { FUN_0101cc1e(1); snap to centre(cur);
                                                   FUN_0101ace3(this,0); } return 1; }
   occ = FUN_0100f421(nt);
   if (occ && occ is moving (action 1,2 or >=10 && !=0xb)) goto wait;
   return 1;
}
else {
   occ = FUN_0100f421(nt);
   if (!isLast && occ && occ is moving (action 1,2 or >=10 && !=0xb)) goto wait;
   if (isLast && order == 0xb) goto wait;
   // re-path
   ace3(0); clear path; +0xac = SENTINEL; wasHome = (+0x68 == 1); +0x68 = 0;
   if (isLast && order not in {3,6,7,8,9,0xd,0xe}) { FUN_010214d9(this); if (wasHome) +0x68 = 2; return 0; }
   switch (order) {
     case 5:          FUN_0101fc50(this, &+0xb4, 0, 0, 0); break;
     case 6,7,8,9,0xd,0xe: FUN_0101fc50(this, &+0xb0, 0, 1, 0); break;
     case 4:          FUN_0101fc50(this, &lastWaypoint, 1, 0, 0); break;
     case 3:          T = ant(+0xb0,+0xb2); FUN_0101fc50(this, T ? FUN_0100ccc0(T) : &lastWaypoint, 1, 0, 0); break;
     default:         FUN_0101fc50(this, &lastWaypoint, 0, 0, 0);
   }
   if (wasHome) +0x68 = 1;
   FUN_010100e5(world, nt, 0xdc);      // spawns effect #0xdc at the blocked tile (CHD 'bump', 1 frame 1000 ms, snd 47)
   return 0;
}
wait: FUN_0101cc1e(this, 1); FUN_0101ace3(this, 0); return 1;   // ANTPAUSE, resumes after 300 ms
```

-----------------------------------------------------------------------------------------------
## 16. Arrival FUN_0101ccaf(this, evt) @0x101ccaf (end of path), per order (table 0x101d680)
Common: `tgt = +0xac; +0xac = SENTINEL; wasHome = (+0x68==1); +0x68 = 0; handled = 0`.
* 1, 4: local only. If wasHome: +0x68 = 2 (queued at hill), +0x70 = (+0x6c ? 0 : timeGetTime()).
  Else if layer2(tgt) is a power-up: msg FUN_01023166 (pick up) -> handled.
* 2: local: msg FUN_01022b03 (enter anthill), flag 9 -> handled.
* 3: snap, idle, clear path; T = ant(+0xb0,+0xb2); if T: FUN_0101fc50(this, tile(T), 1, 0, 1)
  (chase); handled iff order is still 3.
* 5: not carrying and food obj+0x12 > 0: msg FUN_01022bd8 (take food) -> handled. Carrying:
  +0xec=0, +0xf4=obj anchor, FUN_0101fc50(this,&home,0,0,0), text 0x11 "Can't - already have food."
* 6: FUN_0101d762(&+0xb0,0) -> msg FUN_0102331a;  7: layer2(+0xb0)==0x86 -> msg FUN_010233c9;
  8: FUN_0101d762 -> msg FUN_0102342b;  9: FUN_0101d7f9 -> msg FUN_010234da;
  0xd: FUN_0101d6d6 -> msg FUN_01023951;  0xe: layer2==0x25 -> msg FUN_01023a00.
* 0xa: layer2(tgt) bomb: rand()%100 < 20 ? tile=tgt : FUN_0101df5d(this,&tgt,&tile,4,0,0,0);
  msg FUN_0102353c -> handled.
* 0xb: if target team is local ally -> nothing. Not carrying and P(t)+0x64==0: amount =
  min(P(t)+0x54, 50) -> msg FUN_0102361e (steal). Carrying: go home + text 0x11.
* 0xf: snap, idle, clear path; T exists -> FUN_0101da6f(this, tile(T)) handled.
* not handled: local -> FUN_010214d9 (stop); remote -> FUN_01021664(this, +0x5a, 0). evt d = 0.

-----------------------------------------------------------------------------------------------
## 17. Related orders
* STOP FUN_01028a60 @0x1028a60 (callers 0x1026306, 0x1028fee): for each selected local ant
  accepted by FUN_0101ff5a and not standing on home or {home.row-1,home.col}: +0x68=0; if it has
  a path or action in {1,2,>=10 && !=0xb} and +0xac.row < 0x5a -> FUN_0101fc50(ant,&cur,0,0,0)
  (re-order to its own tile). Then text 0x36 "Stopping.".
* ANTHILLQ task (vtbl 0x1002540, execute 0x10247f9 -> FUN_0100ff1f), scheduled at 0x100e556 with
  (delay 0, interval 200 ms): if any local ant has order 1/2 with +0xac==home, or stands on home,
  do nothing; else pick the ant with +0x68==2 (action not 0xc/0xf) with the smallest +0x70
  (first wins ties) and FUN_0101fc50(ant, &home, 0, 0, 0). Player-commanded redirected ants
  (+0x6c=1 -> +0x70=0) are admitted first.

-----------------------------------------------------------------------------------------------
## 18. Scheduler facts used above (world+0xe88; this section describes the NON-default `newtask` timing wheel, vtbl 0x1005248; the default is the sorted-list scheduler, vtbl 0x1005208, see section 12 and the verification notes)
* FUN_01031e92(task, delay, interval, x) -> add @0x10312bd: task+0x18 = interval, insert with
  FUN_01031465(task, delay): slot = max(1, (timeGetTime() + delay - cursor) >> 3), 1024 slots of
  8 ms. Run @0x10313cb: while cursor < now, process slot: execute (vtbl+0xc); if it returns 1 the
  task is re-inserted with delay = its interval.
* Timer tasks (FUN_0103057b(p1,p2) = add(task, p1, p2, 0); base execute 0x10305a8): first
  execute calls vtbl+0x10 (start) and returns 1 (unless p2 == 1111111), second calls vtbl+0x14
  (fire) and returns 0 -> the "fire" happens p1 + p2 after creation. ANTPAUSE uses (0, 300),
  BTNPUSH (0, 125).

-----------------------------------------------------------------------------------------------
## 19. Corrections / notes for the lead
* Ant vtbl+0x28 is 0x101a93a (set position x,y: +0x38/+0x3a, occupancy, fog), not 0x102b7bb;
  vtbl+0x18 is 0x101a928 (same, from a Point object) — dump at 0x1004be0.
* FUN_0100e8f5(world, id, flag) displays string resource `id` (FUN_010292dc + FUN_0100e944); it
  is not a sound call. IDs: 0x30 "Can't do that...", 0x36 "Stopping.", 0x3a "Can't go there.",
  0x11 "Can't - already have food.", 0x42..0x47 acknowledgements.
* CHECKGO (ctor 0x1024817, vtbl 0x1004e38) is the end-of-game countdown (texts 0x31/0x32/0x3b),
  CHECKDROP (0x1024aa2), UPDSDLIST (0x10244a8/0x1024560), STOPTASK/FDTASK (0x1022535/0x1022589,
  game start) are not part of the move path. COMBEVT = FUN_01024bc3 via FUN_0101c184 (+0x80),
  BTNPUSH = FUN_01024caf via FUN_01028ffe, 'Invuln' = FUN_01024ae4 (+0x7c) when leaving action 3.
* The 6th argument of FUN_0101ad02 ([ebp+0x1c]) is never read.

---

## Adversarial verification

An independent second pass re-derived every claim above from the Capstone disassembly and recorded a verdict per claim.

# Adversarial verification of re_orders.md (cluster "orders")

I re-derived every claim from fresh Capstone disassembly of Original-Ants/Ants.exe (cs.py / xref.py / grepins.py / findptr.py). CHD timings were re-read with chd.anims() (scripts: vo_idle.py, vo_sim.py). Ghidra was not used as evidence.

Verdict: the report is accurate on almost every code path I could check, including all the "when and how an ant starts moving" mechanics. It has two substantive errors:
- The PATHMGR rate. Each tick gives one 1000-expansion slice; it does not complete one request.
- The scheduler model in sections 12 and 18. The report describes the non-default "newtask" timing wheel.

There are also several smaller precision errors, listed below.

---------------------------------------------------------------------------------------------------
## CONFIRMED

Each item below was checked instruction by instruction, including push order, signedness and branch polarity.

1. **Input dispatch (claim 1).** 0x102737e: event 0xc with state==0 goes to 0x1027530; any other event with state==0 goes to 0x1027b51 (0x1027460).
   - 0x10277f4: tile = {y/32, x/32} (signed idiv at 0x102780f/0x1027820). The dec-chain at 0x102782a maps modes 3/7 → 0x1027a6a (push 0,0), 4 → 0x1027a16 (push 0, then 1), 5 → 0x102785c (push 1, then 0). The pushes are exactly as the report says.
   - The click marker is drawn in modes 3/4/5/7 even when FUN_010287b5 returns 0 ([ebp-0x14]=1 at 0x10278d5, test at 0x10279e7).
   - Right-click: selType = FUN_010282e0(world,1) (0x10282e0): the common FUN_0100f9cb(ant,1) value of the selected ants, or 0 if they differ or none is selected. Sentinel 0x3e8 → 0 via neg/sbb/and at 0x1028344. Special is sent iff 0x54ec!=4 && selType∉{0,4} (0x1027d97..0x1027dd2). Minimap branch logic as reported (0x1027ba0..0x1027ca4).
2. **Group dispatch (claim 2).** FUN_010287b5 @0x10287b5, ret 0xc.
   - Only P(localTeam) ants are considered. Skips: null entry, +0x50==0, !FUN_0101ff5a (0x10287f9..0x1028814).
   - Dedupe (0x102881a..0x10288d5): order>5 (signed jg) and in 6..9/0xd/0xe needs special && +0xb0==tile (FUN_01028a11). Orders 1/4/5 need !special && !attack && +0xac==tile. Order 2: the clicked tile is on the own hill (f4ab mask 0x10, flags&2, info+0x30==localTeam). Order 3 needs attack && +0xac==tile. Order 0 and negative orders never match.
   - Key: 16·Chebyshev(FUN_0100ccc0(a), tile) (FUN_01020911 @0x1020911: movzx, abs, unsigned word max, shl 4).
   - Exchange sort with unsigned `jbe` keeps the pair, i.e. it swaps on strict '>' (0x102893e..0x1028994).
   - Dispatch FUN_0101fc50(e[k].ant, &tile, 1, special, 0) (pushes 0, [ebp+0xc], 1, &tile at 0x102899d..0x10289b2).
   - Ack only when k==0 and r!=0: attack → FUN_0101b711; special && n==1 → FUN_0101b78a; !special → FUN_0101b67b. A special order to more than one ant plays no ack.
   - Returns 0 only if no ant qualified.
3. **Accept predicate (claim 3).** 0x101ff5a: FUN_0100f9cb(1)==FUN_0100f9cb(0) (16-bit compare), vtbl+0x40 = 0x101aa5e returns +0xfc, +0x84==0, action ∈ {0,1,3}.
   - 0x100f9cb: for flag && type∈{1,2,5} && action∈{0,1,3} it returns 0 only when timeGetTime()==+0x4c. The switch on +0x48 at 0x100f9ff..0x100fa15 is dead code: every case reaches 0x100fa16.
4. **FUN_0101fc50 prologue (claims 4 and 5): snap and forced idle.** t=*dst (0x101fc64). The player gate is at 0x101fc7b (return 0 with no side effects).
   - Hill mapping: own team → info+0x28; thief → info+0x2c; else FUN_010214d9 and return 0 (0x101fca2..0x101fcc5).
   - The rest runs in this order: cur=FUN_0100ccc0 (0x101fcce); SetAction(0) (0x101fcd6); vtbl+0x18(centre(cur)) (0x101fcf6; 0x101a928 → 0x101a93a); FUN_0101cc1e(0) (0x101fd08); FUN_0101ab87(0,NULL,1,NULL) (0x101fd14); FUN_0101c152 (0x101fd1b); +0x98=timeGetTime() (0x101fd2d).
   - SetAction(0) always replays the idle animation: case 0x101aebc → Play 0x102c0db, then +0x88=0 (0x101af1c).
   - Auto-engage gate FUN_0101c0d5 requires 2000 < now−(+0x98), unsigned (0x101c0f1..0x101c10c).
   - arg4 ([ebp+0x14]) is never read.
5. **Order classification (claim 6).** 0x1020655, every branch and store as reported. Additional detail: in the special branch, types other than 1/2/5 leave +0xa8 unchanged (0 after the earlier FUN_0101ab87). The ant/food/power-up/bomb tests are then skipped entirely.
6. **Destination resolver (claim 7).** 0x10202e7.
   - mask = (a2?0x38:0)|(a3?1:0)|(a4?2:0)|(t==home?4:0)|f100 (0x1020362..0x10203a7).
   - Home recursion to P+0x46 with restore on failure (0x10203b9..0x102040d).
   - `and esi,0xffd3` (0x1020419).
   - Segments A (skipped if col−r<0, `js` 0x1020447), B, C (skipped if row−r<0, `jl` 0x1020549), D. Start indices are clamped to 0, the inclusive bounds use signed `jle`, and the map-bounds tests are unsigned word compares.
   - r loop `jb` at 0x102063c. The first acceptable tile in scan order wins.
7. **Tile acceptability FUN_0101f780 (section 7).** Re-read end to end (0x101f780..0x101fc21); every branch matches the report's pseudocode, including the "sum==2" entrance test (0x101fb18) and the mask-2 teammate/queue test (0x101fb35..0x101fbd2).
8. **Special approach tile (claim 8).** Jump table 0x101ff2a dumped: 3→fe51, 4/5/0xa/0xc→fe74, 6→fd6f, 7→fd84, 8→fde4, 9→fdf6, 0xb→fe61, 0xd→fdab, 0xe→fdbd.
   - Validators 0x101d762/0x101d6d6/0x101d7f9 are as reported.
   - FUN_01020128: candidates N,S,W,E; FUN_0101f780(...,0x81,0); 8000 sentinel; first strict minimum (`jae` skip at 0x10202b9).
   - Success: +0xb0 = orig (0x101fe18). Failure: SetAction(0xb), FUN_0101ab87(0..), text 0x30, ok=0, then stop broadcast.
9. **Delivery gate (claim 10).** 0x100cba4: a=ant(localTeam,idx).
   - count==0: stop, SetAction(0xb), text 0x3a, only for local ants.
   - Otherwise the path is dropped unless action ∈ {0,3} (0x100cc2e..0x100cc3b) and FUN_0100ccc0(a)==path[0] (0x100cc3d..0x100cc5b).
   - Message: FUN_010228ff(localTeam, idx, &centre(cur), a, count, path), then FUN_0100d791(msg, 0xa, 0) (0x100cc74..0x100cca3).
10. **Message-6 handler (claim 11).** 0x10229b7 (table 0x1047320 slot 6 → dword 0x1047350 = 0x10229b7).
    - Drops the message on action 0xc/0xf, both before and after SetAction.
    - FUN_0101cc1e(0); vtbl+0x18(msg.x,msg.y); SetAction(msg.action, msg.dir, 0xffff, 0xffff, 0, 1) (pushes 1,0,ffff,ffff,dir,action at 0x1022aa1..0x1022ab6).
    - FUN_0101ab87(count, tiles, msg.order, &msg+0x20) at 0x1022ad8.
    - FUN_0101ab87 @0x101ab87 matches exactly (+0xde=0, +0xac=w[n−1], order-5 extras).
    - FUN_0100d791 flags 0xa: local handler at 0x100d965, then network send (bit 8).
11. **First step (claim 12).**
    - Sprite step 0x102b997: the start call has status 0 (+0x2c==0); normal steps give status 1, or 2 when the current frame is the tail (0x102ba1b..0x102ba29).
    - FUN_0101b8cb returns at 0x101b900 for status 0.
    - Blocks 1/3 (bomb and fire checks) run only if evt[4]==3 or (status 2 && no waypoints && stationary). No idle or carry anim frame carries an event (all 60 IDs dumped), so with waypoints these blocks are skipped. Blocks 2/4 need action ∈ {0xa,0xe,0x13}.
    - 0x101bd4f requires action ∈ {0,1,3}; with waypoints, action 0 or 3 goes to 0x101b932.
    - Bomb test (local, any owner, 0x101b953) → order 0xa and end of path.
    - Auto-engage: FUN_0101c0d5 && action∈{0,1} && FUN_0101dbec(this,3,&cur).
    - +0xde++ (0x101ba3c). dir = FUN_01017531(&cur,&nxt) (table 0x1002b28 = 7,0,1,6,0,2,5,4,3, index (dr+1)*3+dc+1).
    - SetAction(1,dir,terrA,terrB,1,1) at 0x101bacd (pushes 1,1,terrB,terrA,dir,1).
12. **Idle first-frame values in claim 13, for land and carry variants.** Tables 0x1002cb8/0x1002e38 [type*8+dir], dirs 0..4, filled into world[0x3d0/0x868+dir+8*type] at 0x10183c1..0x1018410.
    - Values: worker agst/hgst 150; bomber abst/hbst 100; fire afst/hfst 100; thief atst/htst 100; combat acst/hcst 150, except dir 4 (acst301/hcst301) 125; swimmer asst/hsst 150. Dirs 5..7 are mirrors of dirs 3..1.
    - The idle case always replays the anim. The msg-6 SetAction runs outside any sprite callback, so frame 0 is counted once. The walk is started inside the step callback, so the walk's first frame is counted twice (re-checked at 0x102c1fc: +0x2c=0, +0x10=now, immediate update; 0x102ba5d..0x102ba9f).
13. **Stop broadcast (claim 14).** 0x10214d9 (local only) → FUN_0102368f → FUN_0100d791(0xa).
    - Handler 0x10236c1 → FUN_01021664 (0x1021664..0x1021787). With the remote flag set, it exits early for local ants, actions 0xe/0x13/0xa, +0x84==1, or now−(+0x100) < 2000 (unsigned `jb`).
    - Otherwise it always snaps and cancels the pause. It idles and clears only if the ant has a path, has an order, or its action is not in {0, 3..9, 0xb}.
14. **Ally confirmation (claim 15).** 0x101ffab / 0x1020076 as reported (callback pushed at 0x1020033; +0xd2/+0xcc/+0xd0 save; yes → FUN_01010d26(P(local)) then re-issue with player=1; no → idle and clear).
15. **Blocked next tile (claim 16), with one precision fix.** For a local ant the 300 ms wait happens only when (next is not the final waypoint and its occupant is moving) or (next is final and order==0xb) (0x101c8b4..0x101c8ea). If next is final and occupied by a mover, the ant re-plans (orders 3/6-9/0xd/0xe) or stops (0x101c90f..0x101c98b). Bump effect 0xdc confirmed.
16. **Status text (claim 17).** FUN_0100e8f5 = LoadString (0x10292dc) + show (0x100e944). String IDs re-extracted: 0x30, 0x36, 0x3a, 0x11 and 0x42..0x47 are exactly as reported.
17. **Vtable and 0x101b590 (claim 18).** Ant vtable 0x1004be0: +0x18 = 0x101a928, +0x28 = 0x101a93a, +0x38 = 0x101ee84, +0x40 = 0x101aa5e. 0x101b590 is the last byte of the instruction at 0x101b58a inside FUN_0101b52f (the marker creator); nothing references it.
18. **ANTHILLQ (claim 19).** Created at 0x100e529..0x100e556 with AddTask(task,0,200,0); vtbl 0x1002540 slot +0xc = 0x10247f9 → FUN_0100ff1f. Unsigned `jae` keeps the first of equal +0x70 values. Arrival for orders 1/4 with wasHome: +0x68=2, +0x70 = (+0x6c ? 0 : timeGetTime()) (0x101cd3a..0x101cd58).
19. **Other checked items.**
    - FUN_01028a60 STOP (as reported; negative actions also count as moving).
    - PATHMGR creation (AddTask(task,0,50,0) at 0x100e4fe..0x100e50f; vtbl 0x1004e28 slot +0xc = 0x1024786).
    - Timer-task base 0x10305a8 / FUN_0103057b(p1,p2).
    - Acknowledgement voice/text tables at 0x101b67b / 0x101b711 / 0x101b78a (every value re-derived).
    - FUN_01026904: last hit wins; *out is written only on a hit.
    - The arrival table at 0x101d680 and its not-handled tail at 0x101d62d.

---------------------------------------------------------------------------------------------------
## CORRECTED

**C1. Claim 9 / section 11: "PATHMGR completes exactly one request per tick" is wrong.**
- FUN_01019a66 returns 0 only when no search grid can be acquired (0x1019a73..0x1019a84). Otherwise it returns 1 after at most 1000 expansions (budget test `cmp ax,[ebp+8]; jae` at 0x1019aa1).
- The heap pointer req+0x28 is released and zeroed only when the search ends: goal (0x1019c04 → 0x1019c1b..0x1019c23), f≥8000, or empty heap (0x1019c16). If the budget runs out with a non-empty heap, it returns 1 with +0x28 still set (0x1019c0d..0x1019c14 → 0x1019c27).
- In 0x1024786, ebx=1 as soon as any request returns 1 (0x10247c0). The request is delivered only if +0x28==0; otherwise it is appended to the tail (0x10247c5). The loop exits once ebx=1.
- Correct statement: each PATHMGR run gives exactly one queued request that owns a grid one slice of up to 1000 expansions. At most one path is delivered per run. A request that needs k slices uses k runs, round-robin with the other queued requests. This also answers the report's open question: "done but requeued" means "slice ran, search not finished".

**C2. Scheduler model (section 18, and "one 8-ms scheduler slot" in section 12) describes the wrong scheduler.**
- At 0x103155c, `cmp [0x104b448],0; je` selects the list scheduler FUN_01030d6a (vtbl 0x1005208). The image value of [0x104b448] is 0.
- The 8 ms × 1024 timing wheel (FUN_01031211, vtbl 0x1005248) is used only when the command line contains "newtask" (0x1031864..0x1031875; string at 0x1047728; "oldtask" at 0x1047730 clears the flag).
- Default behaviour:
  - AddTask 0x1030e7b: first due = timeGetTime()+delay.
  - Tick 0x10310e8 runs only the head task, and only if it is due.
  - A task that returns 1 is re-inserted with due = timeGetTime()+interval, measured after the run (0x10311a8 / 0x10311b1).
  - There is no 8 ms quantisation.
- So PATHMGR runs are spaced ≥50 ms apart, measured from the end of the previous run, plus main-loop latency.

**C3. Claim 4 / section 13: the maximum mid-stride jump depends on direction.**
- A tile spans pixels 32c..32c+31 with centre 32c+16. Inside a tile the ant can be up to 16 px left or up of centre, but only 15 px right or down.
- Moving +x/+y: back ≤15, forward ≤16, as the report says.
- Moving −x/−y the bounds swap: back ≤16, forward ≤15.
- With the real 4 px grass/sand/dirt steps from a centre: +x gives back ≤12 / forward ≤16; −x gives back ≤16 / forward ≤12. Diagonal 3 px steps give back ≤15 / forward ≤14 (vo_sim.py).
- The mechanism is confirmed: no stride completion and no reversal.

**C4. Claim 13: incomplete for swimmers on water.**
- SetAction case 0 uses world+0x47c0[colour] when terrA==2 (0x101aebc..0x101aed3). terrA is 0xffff in the msg-6 call, so it is computed from the ant's current tile (0x101ae5b..0x101ae90).
- world+0x47c0 ← anim ID at 0x10048b8 (0x101881d..0x1018836) = CHD #1006 'astw301', 85 frames, first frame 40 ms.
- A swimmer standing on water therefore starts walking 40 ms after delivery, not 150.
- If the path arrives while the ant is in action 3, the action-3 "sd" anim (world+0x4180/0x41e0 ← tables 0x10045d8/0x1004608, loaded at 0x10185a9/0x10185d1; dir forced to 4 at 0x101b3b3) sets the delay instead: worker 125, bomber 105, fire 100, thief 10, combat 100, swimmer 100 ms.

**C5. Section 1, right-click target point.**
- FUN_01027b51 builds its point from world+0x5528/0x552c/0x5530, which are saved at button-DOWN (0x102747f..0x10274a0). It converts that point with the current view at release (0x1027b68..0x1027b85, 0x1027ccc) and uses it for both the tile and the click marker (0x1027e3d).
- Right-click orders therefore target where the button was pressed, not the release position.
- Left-click (0x1027778) and FUN_01026904 (mode-5 target) use the current mouse position (world+0x110..0x11c).

**C6. Minor corrections.**
- FUN_010277f4 mode 1 calls FUN_01028c44(world, **0**), not 1 (`push edi` at 0x10279db, edi=0 from 0x102780d).
- For a non-0xc button press, FUN_0102737e calls FUN_010296a5, not FUN_010274be (0x10274b2).
- FUN_0100d791 state gates exist only for message types 0..3 (0x10223d1, 0x10223d1, 0x10225f0, 0x10227ee); types 4..0x27 have none. The report says "0..7".
- The gate called is the current state's gate (world+0x4b18), passed the new type (0x100d8f8..0x100d91c).
- FUN_010246e8 removes only the first queued request with the same index (0x102470a..0x102473b), not every one. This is equivalent in practice, because each ant has at most one queued request.
- "Only the enemy-hill non-thief and !FUN_0101ff5a paths skip the snap": the enemy-hill path still snaps, through FUN_01021664 (unconditional SetPosition(centre) at 0x10216fa). Only the !FUN_0101ff5a path leaves the ant untouched.

---------------------------------------------------------------------------------------------------
## UNVERIFIED

- **Reachability of the e[] overflow in FUN_010287b5 (see A5).** I could not establish the largest possible team or selection. Team size comes from the level (eggs: P+0x4a ← map+0x6c at 0x100dc98). The occupancy grid allows 32 indices. Neither the band select nor FUN_01010373 has a cap.
- **Sprite-update cadence** (how soon after the due time a step actually runs), and so the exact wall-clock latency of the "first-step" boundary.
- **Per-order bodies of FUN_0101ccaf beyond orders 1/4, the dispatch table and the tail.** The report's section 16 details for orders 3, 5, 6..0xf were not re-derived.
- **Combat internals:** FUN_0101dbec, FUN_0101da6f, FUN_01022c57. I only saw that FUN_0101dd6f re-issues the saved order (+0xc4) after an auto-engage (0x101dd6f..0x101ddea).
- **Colour 1..3 copies** of the idle and swim-idle anims are assumed to keep the colour-0 frame durations.
- **What the uninitialised mode-5 tile** (FUN_010277f4 [ebp-0x18], FUN_01027b51 [ebp-0x18]) actually produces.
- **FUN_01026f91 per-type details.**
- **Multiplayer paths:** remote-flag stops, and msg 6 received for remote ants.

---------------------------------------------------------------------------------------------------
## ADDITIONAL FINDINGS (missed or under-stated by the report)

**A1. Group orders start in a staggered sequence, nearest ant first.**
- Every accepted ant is snapped and idled synchronously, inside the click handler.
- Requests are appended in sorted order (0x1024769 AppendTail). PATHMGR pops from the head and serves one per run (C1). The k-th ant (0-based) therefore gets its path no earlier than the (k+1)-th PATHMGR run after the click, about 50 ms apart.
- Only 4 search grids exist; the 5th and later requests take a grid when an earlier request is destroyed.
- Timeline for one ant:
  - Click C: snap + idle.
  - T: the PATHMGR run that finishes its search. That is at least one run per request ahead of it, runs ≥50 ms apart, and a run happens only when the window-message queue is empty.
  - T + idle_dur0: walk anim starts.
  - T + idle_dur0 + 2·walk_dur0: first pixel of motion. walk_dur0 depends on path[0]'s terrain: grass 50, sand 40, dirt 60, mud 60.

**A2. Formation rule for a group move.**
- The nearest ant takes the exact tile, unless another own ant is registered on it: FUN_0101f780 rejects any same-team occupant other than the ordered ant itself, because player masks lack 0x80.
- Each later ant is refused tiles already targeted by teammates with order 1/2 (mask bit 2 survives the 0xffd3 mask).
- It therefore takes the first free tile in the ring scan order. Ring 1 is: (−1,−1), (0,−1), (+1,−1), (−1,+1), (0,+1), (+1,+1), (−1,0), (+1,0). So the second ant goes to the NW diagonal, not an orthogonal neighbour.

**A3. The sort is not stable.**
- Equal distances are not kept in ant-index order. Example: [A:32, B:32, C:16] sorts to [C, B, A].
- To reproduce this, the remake must use exactly this exchange sort, not a stable sort.

**A4. Re-clicking the same tile.**
- Only ants whose +0xac equals the clicked tile are skipped.
- Ants with a ring-substituted target, and ants that already arrived (arrival clears the order), are re-planned. They snap to their tile centre (a visible stutter) and restart the path delay, while the ant holding the exact tile keeps walking.

**A5. Stack overflow in FUN_010287b5's 16-entry array (latent original bug).** e[k] sits at [ebp−0x9c+8k].
- **17th accepted ant (k=16):** its entry lands on two scratch slots ([ebp−0x1c]/[ebp−0x18]). Its distance is then overwritten by every later accepted ant's FUN_0100ccc0 output, and its ant pointer by FUN_01028a11 during special dedupe.
- **18th accepted ant (k=17):** its pointer overwrites the saved world pointer [ebp−0x14], which is reloaded into esi (used as "world" by the order-2 dedupe). Its distance overwrites the none flag [ebp−0x10]. If no further ant is accepted, the function returns 0 unless that ant stands on the target, so nobody gets the order.
- **19th accepted ant (k=18):** overwrites i ([ebp−0xc]) and n ([ebp−8] ← a distance value). The sort and dispatch then run over stack memory.
- The band select in FUN_01027530 has the same 16-slot pattern ([ebp−0x74]). From about the 21st ant it corrupts its own rectangle, box and loop index.

**A6. The failure delivery skips the busy check.** The count==0 branch of FUN_0100cba4 does not test action or tile (0x100cbe7..0x100cc24). A failed search therefore forces the stop, SetAction(0xb) and "Can't go there." even on an ant that has since become busy. The success branch does check.

**A7. A dropped path leaves a stale order.**
- After a drop, +0xa8/+0xac keep the values FUN_01020655 wrote, and there is no retry.
- Clicking the same tile again is skipped by the dedupe (orders 1/4/5 compare +0xac), so the ant stays idle until the player picks a different tile.
- The stale target still counts as "teammate heading here" (mask 2, entrance counting), blocking that tile for other ants.

**A8. What can make a path get dropped while an ant waits.**
- The idle step callback runs on the waiting ant (no waypoints).
- On each anim wrap (status 2) it runs the bomb-on-tile check (order 0xa arrival) and the fire-wall check (layer-2 0x86 under a non-fire ant → FUN_0101c34c(1, …), 0x101bc49..0x101bc8e).
- For combat ants, after the 2 s +0x98 window, it runs the auto-engage check (FUN_0101dbec(this,4), COMBEVT 3000 ms, 0x101bd6f..0x101bddd).
- If any of these changes the action or tile before delivery, the path is dropped (A7).

**A9. Thief group right-click behaves differently.**
- A homogeneous thief selection makes selType=3, so the order is sent as special. FUN_01020655 has no special mapping for type 3, so the order stays 0.
- With order 0, FUN_0101f780's mask-2 reservation never sees these ants, so every thief can path to the same clicked tile.
- A single thief says "My pleasure..." (the special ack) instead of "Here I go...". A group gets no ack.
- A left-click in mode 4 with worker or combat ants hits the same order-0 path.

**A10. Special orders to several ants can converge.** Approach tiles use mask 0x81, which has no teammate reservation, so several bombers or fire ants may pick the same neighbour tile. +0xac holds the clicked tile until the path is delivered, when FUN_0101ab87 sets it to the approach tile.

**A11. Dead special-power cooldown, and the readiness test of FUN_0100f9cb(ant, 1).**
- +0x4c is set to timeGetTime() and +0x48 to 8/9/6/7 when a special is performed (0x101e572, 0x101e664, 0x101e960, 0x101eadb).
- FUN_0100f9cb(ant, 1) for an own type (+0x54) of 1, 2 or 5 (Bomber, Fire, Swimmer; Capstone 0x100f9d5 - 0x100fa27) returns 0 (Worker) for EVERY action [+0xe4] other than 0, 1 or 3 (idle, walking, stunned: `cmp eax, 1`, `cmp eax, 3`, `jne 0x100fa27`). For those three actions it returns the own type, except in the very millisecond that +0x4c was written (`call [0x10012a4]` = timeGetTime at 0x100fa16, `sub eax, [esi + 0x4c]`, `je 0x100fa27`): the per-subtype switch on [+0x48] before that call (0x100f9ff - 0x100fa15) compiled to nothing, because every branch reaches 0x100fa16. So the effective cooldown is 0 ms, but the flag-1 getter is also a test that the ant is READY for an order: a busy bomber, fire ant or swimmer is a worker for its callers with flag 1 (the accept predicate FUN_0101ff5a at 0x101ff60, which tests the action itself as well, and the HUD's cursor and right click through FUN_010282e0(1) at 0x1026f9d, 0x1027da6, 0x1027db3). An ant whose own type is 0, 3 or 4 answers as with flag 0. (Earlier text here said that flag 1 rejects only within the same millisecond; it also rejects every busy action. docs/GAME_REVERSE_ENGINEERING.md 5.62 has the callers and what the remake does.)

**A12. Action 3 ("sd" anims) nuances.**
- Action 3 is accepted for orders and for path delivery.
- The step callback routes action-3 status-2 steps to FUN_0102151a(0) (0x101f05c), which ends action 3, clears the path and starts the "Invuln" task. Status 1/0 steps go to FUN_0101b8cb.
- The msg-6 handler restarts the 25–30-frame anim, so the first step is always status 1 and the walk starts normally.
- An order issued during action 3 skips the Invuln task: the SetAction cleanup table has no entry for action 3, and FUN_0102151a(0) called from SetAction is a no-op.

**A13. Timing split is correct.** The walk template comes from terr(path[0]) (the flag=1 path in the walk handler), and the first step can be a dive or climb for swimmers because flag=1. This matches the walkstep cluster.
