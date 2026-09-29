# Cluster: collision — tile-entry blocking for walking ants (Ants.exe, image base 0x01000000)

Everything below comes from Capstone disassembly of Original-Ants/Ants.exe (Capstone helper scripts).
The Ghidra C output was used only to find things. Addresses in `// @xxxxxxx` comments are instruction addresses.
All `this` pointers are `ecx` (MSVC thiscall). "ret N" gives the callee-pops byte count.

Shared helpers (verified):
- `IsLocal(ant)` = FUN_0100cd7d: `world[0x4ae4]==0 && word world[0xf2a] == ant->team(+0x56)` (@100cd7d..100cd9e).
- `IsStationaryAction(a)` (the pattern repeated inline everywhere, signed compares):
  `a==0 || (a>=3 && a<=9) || a==0xb`. "Moving" means NOT stationary, i.e. a in {1,2,10} or a>=12 (or a<0).
- `GetType(0)` = FUN_0100f9cb(0): returns `+0x54`; if that is 0 it returns FUN_01021087(map+0x70) (@100fa2c..100fa47).
- `TeamObj(t)` = `[world+0x4958+t*4]`. `TeamObj->GetAnt(i)` = FUN_0100cfb1: `i < dword +0x24 ? *(base(+0x18) + stride(+0x1c)*i) : NULL`.
- `abs` = FUN_01034f20. `FUN_01017531(from,to)` = dirTable[(to.row-from.row+1)*3 + (to.col-from.col+1)] with table 0x1002b28 = {7,0,1,6,0,2,5,4,3}.
- `CentreOf(tile)` = FUN_0100cd00 → point (col*32+16, row*32+16). `PixelTile(ant)` = FUN_0100ccc0 → (y/32, x/32) of the sprite position.
- Ant vtable+0x18 = 0x101a928 `SetPosition(point)`: writes +0x38/+0x3a, sets bit 0x40 of +0xe, and calls FUN_0100f17f(world, this, &(pos/32)), which re-registers the ant in the occupancy grid (@101a928..101a995).
- Status-bar text: FUN_0100e8f5(world, strId, 0) loads the string resource with LoadStringA and shows it (5000 ms timer). Resource 0x30 = "Can't do that...", 0x3a = "Can't go there." (read from the PE string table).

---------------------------------------------------------------------------------------------------
## 0. Occupancy grid (needed to understand "occupant")

`world+0x553c` holds an array of row pointers. Each tile has one uint16 cell `g`:
- bits 0-2 = team (7 means empty)
- bits 3-7 = the ant's index within its team (+0x58 & 0x1f)
- bit 8 = MULTI flag (more than one ant is registered on the tile)

FUN_0100f17f(world, ant, Tile* nt) [ret 8] — called from SetPosition and at the end of every walk step (C11):
```
if (!TeamObj(ant->team) || !TeamObj->GetAnt(ant->idx58)) return;          // @100f193..100f1ae
old = ant->tile5a;
if (old.row != 0x5a) {                                                     // 0x5a = "not placed" sentinel
   if (*nt == old) return;                                                 // @100f1c8..100f1d8
   g = &grid[old.row][old.col];
   if (g->bit8) {                                                          // @100f1f8
      FUN_0100f2cd(&old, exclude=ant, &cnt, &best, NULL);                 // @100f210
      multi = (cnt > 1);  g = (g & 0xfeff) | multi<<8;                     // @100f215..100f234
      if (!multi && cnt == 1) g = (g & 0xff00) | best->team&7 | (best->idx58&0x1f)<<3;   // @100f239..100f25f
   } else  g.lowbyte = 0xff;                                               // empty (team 7, idx 31) @100f2c5
}
ant->tile5a = *nt;                                                         // @100f26d / 100f276
if (nt->row == 0x5a) return;
g = &grid[nt->row][nt->col];
if ((g & 7) == 7)  g = (g & 0xff00) | ant->team&7 | (ant->idx58&0x1f)<<3;  // @100f297..100f2b3
else               g |= 0x100;                                             // @100f2b8 MULTI
```
Because the walk step calls this with pos+d every step, an ant is registered on whichever tile holds its pixel position. It switches tile the moment it crosses the 32 px boundary.

FUN_0100f2cd(Tile* t, Ant* exclude, uint16* outCount, Ant** outBest, List* outList) [ret 0x14] — picks the "best" occupant of a MULTI tile:
```
count=0; best=NULL; bestLocal=0;
for (slot = 0x4958; slot < 0x4968; slot += 4) {            // always 4 team slots  @100f2e1..100f3ab
  team = [world+slot]; if (!team) continue;
  for (i = 0; i < word team->+0x24; i++) {
    a = team->GetAnt(i);
    if (!a || a->tile5a != *t || a == exclude) continue;    // @100f31c..100f335
    count++; if (outList) outList->Append(a);               // @100f33a..100f342
    if (best == NULL) best = a;                             // @100f347
    else if (a->+0xd8 == 0 && IsStationaryAction(a->action) && (IsLocal(a) || !bestLocal))
        best = a;                                           // @100f34d..100f37f
    if (best && IsLocal(best)) bestLocal = 1;               // @100f382..100f392
  }
}
*outCount = count; *outBest = best;
```
So when several ants share a tile, the occupant reported is the last stationary ant with no path, and a local-team ant is preferred over a non-local one. If no ant qualifies, it is the first ant found in team-slot and index order.

---------------------------------------------------------------------------------------------------
## 1. FUN_0100f4ab — TileInfo query  (thiscall world, (Tile* t, uint mask, TileInfo* out)) [ret 0xc]

`memset(out,0,0x3c)` @100f4c5; `out->tile=*t` @100f4d2; `out->mask=mask` @100f4d9.

| off | type | mask bit | contents |
|---|---|---|---|
| +0x00 | Tile | – | the queried tile |
| +0x04 | uint | – | mask |
| +0x08 | Ant* | 0x001 | occupant: grid bit8 set → FUN_0100f2cd(t,0,..).best; else team bits==7 → NULL; else TeamObj(team)->GetAnt(idx) @100f511..100f588 |
| +0x0c | List* | 0x200 | new list of every ant on the tile @100f58c..100f62e |
| +0x10 | int16 | 0x002 | terrain class FUN_01008af7(row,col) @100f633..100f64d |
| +0x12 | int16 | 0x004 | layer-1 tile id FUN_01008b3d(1,..) |
| +0x14 | int16 | 0x008 | layer-2 tile id FUN_01008b3d(2,..) (0x7ffe when out of bounds) |
| +0x18 | uint flags | | bit0 (mask 0x20): grid team bits != 7, i.e. any ant registered @100f4fc. bit1 (mask 0x10): anthill cell of a live team @100f805. bit2 (mask 0x80): food object here @100f786 |
| +0x1c | int | 0x040 | FUN_0100cf0f: layer-1 cell word0 bit0 ("solid" footprint bit), or 1 when out of bounds @100f6ea |
| +0x20 | int16 | 0x100 | layer-1 cell word1 |
| +0x22 | int16 | 0x100 | layer-2 cell word1 (owner team for bombs) @100f6b6..100f6d1 |
| +0x24 | int | 0x020 | 1 if grid bit8 (MULTI) @100f50a |
| +0x28 | Tile | 0x010 | anthill object origin (FUN_010076b6) = entrance tile |
| +0x2c | Tile | 0x010 | TeamObj(hillTeam)->+0x32 tile (the thief tile) |
| +0x30 | int16 | 0x010 | hillTeam (team index) |
| +0x34 | Tile | 0x080 | food object origin |
| +0x38 | Obj* | 0x080 | food object pointer |

Mask 0x10 detail (@100f7a6..100f842): `c = FUN_0100ecac(layer2id)` maps 0xf5→0, 0xf6→1, 0xf7→2, 0xf8→3, anything else → 4. If c != 4, then `t = FUN_0100f43c(c)` = the first team index 0..3 whose TeamObj+0x2c == c (4 if none). If t != 4, set flags|=2, +0x28 = origin(tile), +0x30 = t, +0x2c = TeamObj(t)->(+0x32,+0x34).

Mask 0x80 detail (@100f6f2..100f7a4): the layer-2 id must not be 0x7ffe and must have tileinfo flag 2 (food, FUN_010071dd). Take origin = FUN_010076b6 (the cell itself if layer-2 byte0 bit0 is set, else layer-2 bytes 2/3). Scan the map object list (count dword map+0x44, base map+0x38, stride map+0x3c). An object with +8/+0xa == origin gives flags|=4, +0x34 = origin, +0x38 = obj. The scan does not break early, so the last match wins.

**FUN_0100f421(Tile* t)** [ret 4] = `TileInfo i; FUN_0100f4ab(t, 1, &i); return i.occupant;` (@100f421..100f439).
Related helpers:
- FUN_0100f3ca(t) = f4ab(t,0x20) → `flags18 & 1` (tile occupied).
- FUN_0100f3e8(t) = occupant exists && occupant->+0xd8==0 && IsStationaryAction(occupant->action).

Anthill layout (FUN_0100ecdf @100eda5..100ee25). O=(r,c) is the hill object origin.
- TeamObj+0x2e = (r,c): entrance.
- +0x32 = (r+1,c+2): thief/food tile.
- +0x36 = (r-2,c-1), +0x3a = (r-2,c), +0x3e = (r-2,c+1): queue tiles Q1..Q3.
- +0x42 = (r+3,c+3), +0x46 = (r+2,c-2).
- It also clears the layer-1 solid bit at (r,c) and at (r-1,c) (FUN_0100660c(row,col,0) @100ed7b/100ed8d).

---------------------------------------------------------------------------------------------------
## 2. FUN_0101f780 — CanEnter(Tile* t, uint flags, TileInfo* info) [ret 0xc]  (this = ant)

```
int Ant::CanEnter(Tile* t, uint flags, TileInfo* info)
{
  TileInfo local;
  if (info == NULL || info->mask != 0x1db) {           // @101f790..101f7b4 (0x1db = 0x100|0x80|0x40|0x10|0x08|0x02|0x01)
      FUN_0100f4ab(world, t, 0x1db, &local); info = &local; }
  // NOTE: FUN_0101c4f2 passes an info built with mask 0xdb, so it is ALWAYS recomputed here.

  // ---- R1 terrain  (WORD_ARRAY_010049b8 = {1,1,0,1,1,0,0,0} for class 0 grass,1 sand,2 water,3 mud,4 dirt)
  terr = info->terrain;                                 // @101f7b7
  if (passTbl[terr] == 0) {                             // @101f7c1
      if (terr != 2) return 0;                          // @101f7cb
      if (GetType(0) != 5) return 0;                    // @101f7d5..101f7e1 swimmer only
  }
  // ---- R2 occupant
  occ = info->occupant;                                 // @101f7e7
  if (occ && !(flags & 0x40)) {                         // 0x40 = ignore occupants
      if ((occ == this || occ->team != this->team) && IsLocal(this))  goto R2b;     // @101f7fc..101f813
      /* same-team other ant, or (self/enemy while this is NOT local) */
      if (!(flags & 0x80)) return 0;                    // @101f815  0x80 = allow moving occupants
      if (occ->+0xd8 == 0 && IsStationaryAction(occ->action)) return 0;   // @101f81f..101f849
      // occupant has a waypoint array, or its action is a moving one → fall through
   R2b:
      if (IsLocal(this) && occ != this && occ->team != this->team) {      // @101f84f..101f869
          if (this->order != 3)            return 0;    // @101f86b attack order
          if (this->+0xb0 != occ->team)    return 0;    // @101f878 target team
          if (this->+0xb2 != occ->+0x58)   return 0;    // @101f885 target index
      }
  }
  // ---- R3 anthill cells (any footprint cell with layer-2 id 0xf5..0xf8 of a live team)
  if (info->flags18 & 2) {                              // @101f896
      if (this->order == 0xb && this->+0xb0 == info->hillTeam && *t == info->thiefTile(+0x2c))
          goto R6;                                      // @101f89c..101f8c6
      if (info->hillTeam != this->team) return 0;       // @101f8d1
      if (!(flags & 4)) return 0;                       // @101f8df  4 = entering own hill allowed
      if (!(flags & 1)) goto R6;                        // @101f8e9  1 = t is the final destination
      if (*t == info->hillOrigin(+0x28)) goto R6;       // @101f8f3..101f908
      return 0;
  }
  // ---- R4 solid layer-1 cell (object footprint / out of bounds)
  if (info->blocked1c) {                                // @101f913
      if ((flags & 8) && (tileinfo[info->layer2].flags & 4)) return 1;   // powerup @101f918..101f935 (FUN_01007202)
      if (info->layer2 == 0x86 && GetType(0) == 2) return 1;              // fire wall + FireAnt @101f93b..101f94f
      if (!(info->flags18 & 4)) return 0;                                 // no food object @101f955
      if (this->order != 5) return 0;                                     // harvest @101f95f
      return (dword this->+0xb0 == (dword)info->foodObj(+0x38)) ? 1 : 0;  // @101f96c..101f975
  }
  // ---- R5 anthill queue tiles (normal walkable cells)
  if (!(flags & 0x100)) {                               // @101f980  0x100 = skip queue rules
    for (tm = 0; tm < 4; tm++) {                        // @101f98a..101fb29
      T = TeamObj(tm);
      if (!T || T->+0x64 != 0) continue;                // @101f99e..101f9aa (team +0x64 != 0 → skipped)
      Q1=T->+0x36; Q2=T->+0x3a; Q3=T->+0x3e;
      if (*t != Q1 && *t != Q2 && *t != Q3) continue;   // @101f9df..101fa12
      if (tm != this->team) return 0;                   // @101fa18..101fa23  other team's queue: blocked
      if (!(flags & 1) || !(flags & 0x10)) continue;    // @101fa29, @101fa33
      a = FUN_0100f3ca(&Q1)?1:0;  b = FUN_0100f3ca(&Q2)?1:0;  c = FUN_0100f3ca(&Q3)?1:0;   // @101fa3d..101fa85
      for (i = 0; i < word T->+0x24; i++) {             // @101fa87..101fb08
         x = T->GetAnt(i);
         if (!x || x == this || x->order != 1) continue;
         if (x->+0xac == Q1) a++;  if (x->+0xac == Q2) b++;  if (x->+0xac == Q3) c++;
      }
      if ((uint16)a + (uint16)b + (uint16)c == 2) return 0;   // @101fb0a..101fb1b  EXACTLY 2 (3 passes!)
    }
  }
R6: // ---- R6 claimed-by-teammate (flag 2)
  if (flags & 2) {                                      // @101fb35
    T = TeamObj(this->team);
    for (i = 0; i < word T->+0x24; i++) {               // @101fb49..101fbd2
      x = T->GetAnt(i);
      if (!x || x == this) continue;
      if ((x->order == 1 || x->order == 2) && x->+0xac == *t) return 0;   // @101fb6d..101fba1
      if (*t == T->entrance(+0x2e) && this->+0x68 != 2 && x->+0x68 == 2) return 0;   // @101fba8..101fbc5
    }
  }
  // ---- R7 bombs
  if (flags & 0x20) return 1;                           // @101fbd4  0x20 = ignore bomb ownership
  if (!FUN_01008bc6(info->layer2)) return 1;            // @101fbda..101fbf1 (ids 0x81,0x82,0x83,0x84)
  owner = info->layer2Word1(+0x22);                     // @101fbf3
  if (owner == this->team) return 0;                    // @101fbf7  own bomb blocks
  if (TeamObj(word world[0xf2a])->+0x68 == owner) return 0;   // @101fbfd..101fc14  ally of LOCAL player's team (not this ant's team!)
  return 1;                                             // enemy bomb: walk onto it
}
```
Flag bits, as used by the callers:
- 1 = t is the path's final tile.
- 2 = check tiles claimed by team-mates on move orders.
- 4 = may enter own anthill cells.
- 8 = powerup cell is enterable.
- 0x10 = queue-slot counting.
- 0x20 = ignore bomb ownership.
- 0x40 = ignore occupant.
- 0x80 = moving occupants don't block.
- 0x100 = skip queue-tile rules.

The pathfinder builds its flags in FUN_010202e7 (@1020362..10203a7): (a2?0x20|8|0x10:0) | (a3?1:0) | (a4?2:0) | (t==own entrance?4:0) | (order==3 && t==+0xac?0x100:0). FUN_01020128 uses 0x81. FUN_0101cbcc uses 0. FUN_0101dafb uses 0x40 or 0x140.

---------------------------------------------------------------------------------------------------
## 3. FUN_0101c4f2 — TryEnterTile(Tile* nt) [ret 4], this = walking ant
Only caller: the walk step callback @101bfb4 (FUN_0101b8cb), with nt = (pos+d)/32 when a tile boundary is crossed.
The caller then does this (@101bfb9..101bff7):
- `if (r==0 && !+0xfc) d = CentreOf(curTile) - pos;` (snap back, stay in the old tile)
- `else if (+0x60 || +0xfc) d = 0;` (paused: freeze at the boundary)
- otherwise the walk continues.

Frame locals: ebp-0x18 own = PixelTile(this); ebp-0x68 infoN = f4ab(nt,0xdb); ebp-0xa4 infoO = f4ab(own,0xdb) (@101c504..101c53a).
`local` below means IsLocal(this) (it is tested inline each time).

```
int Ant::TryEnterTile(Tile* nt)
{
  own = PixelTile(this);  infoN = TileInfo(nt,0xdb);  infoO = TileInfo(&own,0xdb);
  occ = infoN.occupant;

  // ===== A. contact with the attack target (local ants only) =====
  if (local && (order==3 || order==0xf) && occ && occ->team == +0xb0 && occ->+0x58 == +0xb2) {  // @101c53f..101c59e
     if (infoN.terrain == 2 || infoO.terrain == 2) {                     // @101c5a4..101c5b7
         FUN_010214d9(this);  FUN_0101ace3(0xb);  ShowText(0x3a /*"Can't go there."*/);    // @101c68b..101c6a4
         goto A_fail_tail; }
     if (!FUN_0101cb0c(/*ecx=*/occ, &this->tile5a)                       // @101c5bd..101c5c8 (NB: this=occupant!)
         || TeamObj(team)->+0x68 /*ally*/ == +0xb0) {                    // @101c5ce..101c5ea
         FUN_010214d9(this);  FUN_0101ace3(0xb);  ShowText(0x30 /*"Can't do that..."*/);   // @101c676..101c6a4
     A_fail_tail:
         FUN_0101ab56(this);                                             // clear path @101c6ab
         if (word +0x54 == 4) FUN_0101dd6f(this);                        // raw type CombatAnt @101c6b0..101c6bd
         return 0;
     }
     kbRange = (GetType(0)==4) ? 4 : 1;                                   // @101c5f0..101c607 (neg/sbb/and 0xfd/add 4)
     dir = FUN_0101d8ed(&occ->tile5a, &this->tile5a, kbRange);           // @101c610
     msg = FUN_01022c57(+0xb0, +0xb2, team, +0x58, dword occ->tile5a, dword this->tile5a,
                        dir, GetType(0)==4);                              // @101c615..101c653
     FUN_0100d791(world, msg, 0xa, 0);  operator delete(msg);            // @101c658..101c670
     return 0;
  }

  // ===== B. passability =====
  flags = 0;                                                             // @101c6cd
  if (order == 4   && *nt == +0xac) flags  = 8;                          // @101c6d0..101c6f6 powerup order target
  if (order == 0xa && *nt == +0xac) flags |= 0x20;                       // @101c6ff..101c725 bomb order target
  E = TeamObj(team)->entrance(+0x2e);                                    // @101c72b..101c749
  if (order == 2 || own == E || own == Tile(E.row-1 /*16-bit dec of the packed dword*/, E.col))
      flags |= 4;                                                        // @101c74c..101c76a (uses OWN tile, not nt)
  if (CanEnter(nt, flags, &infoN)) return 1;                             // @101c76e..101c782

  // ===== C. blocked =====
  final = waypoints(+0xd8)[count(+0xdc) - 1];                            // @101c788..101c7a7 (no count==0 guard)
  isFinal = (*nt == final);                                              // @101c7ab..101c7b8
  if (!local) {                                                          // ---- C1 remote ant @101c7ba..101c7d3
     if (infoN.terrain == 2 && GetType(0) != 5) {                        // @101c7d9..101c7ec
         FUN_0101cc1e(this,1);                                           // pause 300 ms @101c7f2
         SetPosition(CentreOf(own));                                     // vtable+0x18 @101c7f7..101c824
         FUN_0101ace3(0); return 1; }                                    // @101c8f3..101c8fb
     if (isFinal) {
         if (order==3 || order==0xf) { FUN_0101cc1e(this,1); SetPosition(CentreOf(own));
                                       FUN_0101ace3(0); return 1; }      // @101c838..101c887
         return 1;                                                       // @101c889 proceed anyway
     }
     o2 = FUN_0100f421(nt);                                              // @101c88d
     if (!o2 || IsStationaryAction(o2->action)) return 1;                // @101c895..101c8b0 proceed
     FUN_0101cc1e(this,1); FUN_0101ace3(0); return 1;                     // @101c8ea..101c8fb pause
  }
  // ---- C2 local ant @101c8b4
  if (!isFinal) {
     o2 = FUN_0100f421(nt);                                              // @101c8b8
     if (o2 && !IsStationaryAction(o2->action))                          // @101c8c0..101c8db
        { FUN_0101cc1e(this,1); FUN_0101ace3(0); return 1; }             // moving occupant → wait 300 ms, retry
     goto REPATH;                                                        // @101c8dd
  }
  if (order == 0xb) { FUN_0101cc1e(this,1); FUN_0101ace3(0); return 1; } // @101c8e1..101c8fb thief waits at final
REPATH:                                                                  // @101c90f
  FUN_0101ace3(0);  FUN_0101ab56(this);                                  // idle, free waypoints
  +0xac = Tile(0x5a,0x78);                                               // sentinel @101c91e..101c92d
  was68 = (+0x68 == 1);  +0x68 = 0;                                      // @101c933..101c93f
  if (isFinal && local && order ∈ {6,7,8,9,0xd,0xe,3}) isFinal = 0;      // @101c947..101c989 (0xf NOT in set)
  if (isFinal || !local) {                                               // @101c98b..101c9aa
      FUN_010214d9(this);                                                // broadcast "stop at tile" @101caf2
      if (was68) +0x68 = 2;                                              // @101caf9..101cafe
      return 0;
  }
  switch (order) {                                                       // @101c9b0..101cacc  FUN_0101fc50(tile, a2, a3, a4)
   case 5:            FUN_0101fc50(&tile(+0xb4), 0,0,0); break;          // harvest: food tile
   case 6: case 7: case 8: case 9: case 0xd: case 0xe:
                      FUN_0101fc50(&tile(+0xb0), 0,1,0); break;          // +0xb0 read as a Tile
   case 4:            FUN_0101fc50(&final, 1,0,0); break;
   case 3: { Ant* tg = TeamObj(+0xb0)->GetAnt(+0xb2);                    // inline cfb1 @101ca1d..101ca47
             if (tg) FUN_0101fc50(PixelTile(tg), 1,0,0); else FUN_0101fc50(&final, 1,0,0); break; }
   default:           FUN_0101fc50(&final, 0,0,0); break;
  }
  if (was68) +0x68 = 1;                                                  // @101cad1..101cad6
  FUN_010100e5(world, nt, 0xdc);                                         // "bump" sprite+sound at blocked tile @101caeb
  return 0;
}
```
The pause-and-retry paths return 1 with +0x60 set, so the caller freezes the ant exactly where it is on the boundary. The ANTPAUSE task later restarts the walk (section 5), and the next boundary crossing calls TryEnterTile again.

Callee notes:
- **FUN_0101ace3(action)** [ret 4] = FUN_0101ad02(action, +0xe0, 0xffff, 0xffff, 0, 0) (@101ace3..101acff).
  - action 0 = idle. It does not clear the pause, because FUN_0101ad02 calls FUN_0101cc1e(0) only when action != 0 (@101ae36..101ae40). Old action 1 needs no cleanup (the jump-table index goes out of range, @101ad3d..101ad43).
  - action 0xb (handler 0x101b22e) sets +0xac = sentinel and plays world[0x4060 + 4*(type + 6*colour(+0xd4))], or world[0x40c0 + …] when carrying food (+0xe8). This is the "can't" animation.
- **FUN_0101ab56** = free +0xd8, then +0xd8=0, +0xdc=0, +0xde=0 (@101ab56..101ab86). FUN_0101ab87 = the same, plus order=0 and +0xac=sentinel, then optionally allocates a new waypoint array.
- **FUN_010214d9** (@10214d9..1021519): does nothing unless IsLocal. Otherwise msg = FUN_0102368f(PixelTile(this), team, +0x58, 0), which is msg type 0x13, size 0x10, with +8 team (bit15 = arg4), +0xa idx and +0xc tile. It is dispatched with FUN_0100d791(msg,0xa,0) and then deleted.
  - Dispatch flag 0xa means the handler runs locally at once (table 0x1047320[type*8] → 0x10236c1) and the message is also sent over the network (bit 8).
  - Handler → FUN_01021664(ant, tile, flag=0) (@1021664..1021787). With flag 0 there is no gating. It snaps the ant to CentreOf(tile) (vtable+0x18) and moves the child sprite +0x50. It calls FUN_0101cc1e(0). If the ant has a path, an order, or a moving action, it calls FUN_0101ace3(0) and FUN_0101ab87(0,0,1,0), clearing path and order.
  - With the 0x8000 flag set (remote copies) it is skipped for local ants, for actions 0xe/0x13/0xa, for +0x84==1, and when timeGetTime()-(+0x100) < 2000.
- **FUN_010100e5(world, Tile* t, 0xdc)** (@10100e5..101018b) creates a 0x48-byte sprite from CHD Table-4 animation 0xdc = "bump" (1 frame, 1000 ms, sound id 47). It places the sprite at pixel (t.col*32, t.row*32) (FUN_010100ab), adds it to the map display list (layer world+0x4a7c) and to the world+0xf2c effect list. This is the positional bump sound.
- **FUN_0101fc50(Tile* t, a2, a3, a4)** [ret 0x10] re-issues an order (@101fc50..101ff27):
  - If a2 is set, it first requires FUN_0101ff5a: GetType(1)==GetType(0), vtable+0x40()==0, +0x84==0 and action ∈ {0,1,3}.
  - If t is an anthill cell: own hill → t = origin; thief (+0x54==3) → t = that hill's +0x32 tile; otherwise it fails via FUN_010214d9.
  - It then sets action idle, snaps to the centre of the current pixel tile (vtable+0x18), calls FUN_0101cc1e(0), clears path and order (FUN_0101ab87(0,0,1,0)), cancels task +0x80 (FUN_0101c152), sets +0x98 = timeGetTime(), and calls FUN_01020655(t, a3, a2) and the pathfinder FUN_010202e7(t, a2, 1, 1, 5).
  - +0x68 = 1 and +0x6c = a2 are set at @101fed7..101fee1 when the flag at @101febc was set. That happens on the FUN_010202e7 path when the pathfinder's tile differs from the requested one and the requested tile is the own entrance (+0x2e). Otherwise +0x68 = 0.
  - Order-specific planning belongs to the pathfinding/orders cluster.
- **FUN_0101dd6f** (combat ant, @101dd6f..101ddec): calls FUN_0101c152 (cancel task +0x80). Then, if world+0x4b18==2 (game phase) and +0xbc != 0 and action ∈ {0,1}: idle, clear path, FUN_0101fc50(&+0xc4, (+0xc0 ∉ {0,1})?1:0, 0, 0), +0x68 = +0xc8, +0xbc = 0. This restores a saved order.
  - When called from branch A the action has just been set to 0xb, so only the task cancel happens.

---------------------------------------------------------------------------------------------------
## 4. Attack-contact helpers (branch A)

**FUN_0101cb0c(Tile* attackerTile5a)** — `this` is the OCCUPANT/target (ecx still holds `[ebp-0x60]` at @101c5c1) [ret 4]:
```
p = PixelTile(target);                                   // @101cb1a
dr = abs(attTile.row - p.row); dc = abs(attTile.col - p.col);   // @101cb22..101cb3f (compared as uint16)
if (target->+0x78 == 1 || target->+0x84 == 1) return 0;  // @101cb48..101cb53
if (dc > 1 || dr > 1) return 0;                          // @101cb55..101cb5d (Chebyshev adjacency)
if (target->action ∈ {0x14, 2, 0xa, 0xe, 0xf, 0xc}) return 0;   // @101cb5f..101cb81
if (terrain(attTile) == 2) return 0;                     // @101cb83..101cba0
if (terrain(p) == 2) return 0;                           // @101cba2..101cbbc
return 1;
```
**FUN_0101d8ed(Tile* target, Tile* attacker, int16 range)** [ret 0xc] computes the knock-back direction:
```
dir = FUN_01017531(attacker, target);                    // @101d8f9..101d8fd (attacker→target)
d = dir;
for (try = 0; ; ) {
   k = *target;  FUN_0101d9f7(&k, d, range);             // step `range` tiles along d: col += for 1..3, -= for 5..7; row -= for 0,1,7, += for 3..5
   ok = 0;
   if (k.row < map.rows && k.col < map.cols) {           // @101d936..101d954 (unsigned)
      TileInfo i = f4ab(&k, 0x18);
      if ((i.layer2 == 0x86 || !FUN_0100cf0f(k)) && !FUN_0101d822(&k) && !(i.flags18 & 2)) ok = 1;   // @101d965..101d9a0
   }
   try++;
   if (ok) return d;
   switch (try) { case 1: d=(dir+1)%8; case 2: d=(uint16)(dir-1)%8; case 3: d=(dir+2)%8; case 4: d=(uint16)(dir-2)%8; default: return 8; }
}
```
The check ignores terrain and occupants. FUN_0101d822(t) is true if t is any live team's (+0x64==0) +0x32, +0x36, +0x3a, +0x3e or +0x2e tile (@101d822..101d8ea).

**FUN_01022c57(tTeam,tIdx,aTeam,aIdx, dword tTile, dword aTile, int16 dir, int isCombat)** [ret 0x20] builds command message type 8, size 0x20, via FUN_010221fa(8,0x20), which writes header {+0 size, +4 type}. Fields: +8 tTeam, +0xa tIdx, +0xc aTeam, +0xe aIdx, +0x10 tTile, +0x14 aTile, +0x18 dir, +0x1c isCombat. It is dispatched through FUN_0100d791(msg,0xa,0), so it executes locally at once and is also sent. The handler is 0x1022ca1 (resolved in the combat cluster).

---------------------------------------------------------------------------------------------------
## 5. Pause / retry (verifies lead claim C12, with one correction)

**FUN_0101cc1e(int on)** [ret 4] (@101cc1e..101ccac):
```
if (on) {
   if (+0x60) return;                                    // already waiting: NOT restarted @101cc33
   t = new(0x40) ANTPAUSE(this);                         // FUN_01024cf7 @101cc4d
   +0x64 = t;  FUN_0103057b(t, 0, 300);  +0x60 = 1;       // @101cc5a..101cc6a
} else if (+0x60) {
   +0x60 = 0;  scheduler->Remove(+0x64) (vtable+0x10);  Release(+0x64);  +0x64 = 0;   // @101cc7d..101cc9c
}
```
ANTPAUSE (FUN_01024cf7, vtable 0x1004ea0): base FUN_01030508("ANTPAUSE") gives refcount word +4 = 1, name at +8, +0x18 due, +0x1c period, +0x20 scheduled, +0x28 = -1. The task stores +0x34 = ant, +0x38 = ant->action(+0xe4), +0x3c = ant->dir(+0xe0).

Task vtable+0xc = 0x10305a8:
- first call: runs slot +0x10 (0x1010f11, a bare `ret`), sets +0x2c=1, returns (+0x30==0), i.e. 1.
- later calls: run slot +0x14 (0x1024d2b), return 0.

**Fire 0x1024d2b** (@1024d2b..1024d84):
```
ant->FUN_0101cc1e(0);                                    // unregister + release (tick still holds a ref, so no use-after-free)
if (ant->FUN_0101cbcc()) {                               // final tile of attack-type order still blocked
    ant->FUN_0101cc1e(1);                                // new task saves CURRENT action/dir...
    ant->+0x64->+0x38 = this->+0x38;  ant->+0x64->+0x3c = this->+0x3c;   // ...then overwritten with the ORIGINAL saved ones
    return; }
ant->FUN_0101ad02(saved_action, saved_dir, 0xffff, 0xffff, 0, 1);        // @1024d6a..1024d7e
```
CORRECTION to C12: the 6th argument is 1, not 0. The `push 1` at @1024d42 stays on the stack for the fall-through path. FUN_0101ad02 never reads [ebp+0x1c], so the difference has no effect.

**FUN_0101cbcc** (@101cbcc..101cc1d): returns 1 when all of these hold:
- order ∈ {0xb,3,0xf}
- count(+0xdc) > 0
- idx(+0xde) == count-1
- +0xd8 != 0
- !CanEnter(&wp[idx], 0, NULL)

Otherwise it returns 0.

**Scheduler timing** (FUN_0103057b @103057b..10305a5):
- It sets +0x2c=0 and +0x30 = (delay==0x10f447), then calls FUN_01031e92(app=[0x104b478], task, a=0, delay=300, 0). That forwards to `[app+0xe88]->vtable+0xc(task, a, delay, 0)` (@1031e92..1031ead).
- Default scheduler: [0x104b448]==0 unless the command line says "newtask" (@1031849..1031875). It is the sorted-list scheduler FUN_01030d6a (vtable 0x1005208).
- Insert 0x1030e7b (@1030ebe..1030ee4):
  - +0x28 = a, +0x1c = a, +0x20 = 1.
  - FUN_010311a8 sets due(+0x18) = timeGetTime() + a (= now + 0) and inserts in ascending due order, after existing entries with an equal due time (@10311a8..103120e).
  - Then +0x1c = delay (300) becomes the reschedule period. Nothing is inserted if app+0xe50 != 0.
- Tick 0x10310e8 is called from the message loop FUN_01031916 @103197d on every idle PeekMessage iteration (no Sleep). It looks only at the list head (@1031106..1031188):
  ```
  if (timeGetTime() >= head.due) { pop; r = head->vtable+0xc();
     if (r == 1) reinsert with due = timeGetTime() + head.+0x1c; else head.+0x20 = 0;  Release; }
  ```
- Result for ANTPAUSE: the first no-op call runs on the first idle tick after the pause starts. The real fire comes 300 ms of timeGetTime (WINMM) after that tick, so the effective pause is 300 ms plus up to one scheduler-tick of latency.
- The alternative "newtask" timer wheel (vtable 0x1005248, 1024 buckets of 8 ms, @1031465..10314b3) behaves the same way: first call at bucket now+0, then the fire at now+300.
- For a 20 Hz lockstep remake this is about 6 ticks, and the retry repeats every such period while the tile stays blocked.

---

## Adversarial verification

An independent second pass re-derived every claim above from the Capstone disassembly and recorded a verdict per claim.

# Adversarial verification of re_collision.md (Ants.exe, base 0x01000000)

All checks below were re-derived from fresh Capstone disassembly (cs.py / xref.py). Dumps written as
v_f780.txt (CanEnter), v_f4ab.txt (TileInfo), v_c4f2.txt (TryEnterTile), v_b8cb.txt (walk step),
v_ad02.txt (SetAction), v_fc50.txt (re-issue order).

## Verdict table

| # | Claim | Verdict |
|---|---|---|
| 1 | passTbl 0x10049b8 = {1,1,0,1,1,0,0,0}; water only for GetType(0)==5 | CONFIRMED (@101f7be movzx, @101f7c1 cmp word [ecx*2+0x10049b8],di(=0); @101f7cb cmp ax,2; @101f7d5 push 0 / call 100f9cb; @101f7dd cmp ax,5). GetType(0) = +0x54, or FUN_01021087(map+0x70) when +0x54==0 (@100fa2c..100fa47). |
| 2 | CanEnter recomputes with mask 0x1db; c4f2 info (0xdb) unused | CONFIRMED (@101f790..101f7b4; f4ab stores mask verbatim at +4 @100f4d9; c4f2 @101c519 edi=0xdb, passes &infoN @101c771). |
| 3 | Occupant rule | CONFIRMED with precision fix: the "own tile" exception is local-only. A remote ant (IsLocal false) whose occupant is itself goes to @101f815 and is blocked without 0x80. With 0x80 a LOCAL ant facing an ENEMY occupant skips the moving-check and goes straight to R2b (needs order 3 + target match), so 0x80 only lets local ants past same-team moving occupants; remote ants get past any moving occupant. |
| 4 | Anthill cells | CONFIRMED (@101f896..101f90e; f4ab mask 0x10 @100f7a6..100f842: +0x28 origin, +0x2c = TeamObj(t)+0x32/+0x34, +0x30 = t; FUN_0100ecac 0xf5..0xf8 -> 0..3; FUN_0100f43c first team whose word +0x2c == c). Passing R3 jumps to R6 (0x101fb35), skipping R4 solid and R5 queue. |
| 5 | Solid cell exceptions | CONFIRMED (@101f913..101f97b; FUN_0100cf0f = layer-1 byte0&1, 1 if OOB unsigned; FUN_01007202 = tileinfo[id].flags&4, id<0x540). Exceptions return 1 at 0x101fc1a. |
| 6 | Queue tiles | CONFIRMED. Layout from FUN_0100ecdf @100eda5..100ee25: +0x2e (r,c), +0x32 (r+1,c+2), +0x36 (r-2,c-1), +0x3a (r-2,c), +0x3e (r-2,c+1), +0x42 (r+3,c+3), +0x46 (r+2,c-2). Sum==2 test @101fb18 (movzx words). Addition: occupancy via FUN_0100f3ca counts ANY registered ant, including this ant itself (only the claim loop excludes `this`). |
| 7 | Bomb rule | CONFIRMED (@101fbd4..101fc14; FUN_01008bc6 = 0x81/0x82/0x83/0x84). owner = word info+0x22 (layer-2 word1, mask 0x100 @100f6b6..100f6d1). The ally test uses TeamObj(word world+0xf2a)->word +0x68, i.e. the LOCAL player's team. Team+0x68 is written from messages (@1023f1f, @1010d64, @1024261) and defaults to 4 (@100d15a, @101094b, @1015639) = "no ally", consistent with an ally-team field. |
| 8 | c4f2 flags | CONFIRMED (@101c6c7..101c76a). The row-1 test uses a 32-bit `dec edx` on the packed tile but only the low word is compared. |
| 9 | Local blocked ant | CONFIRMED (@101c8b4..101c8fb, @101c90f..101c98b, @101caf2). Stationary test here is action-only (no +0xd8 check). REPATH first does SetAction(0), frees waypoints, sets +0xac sentinel and +0x68=0 even when it then takes the broadcast-stop exit. |
| 10 | Re-path args | CONFIRMED push order: order 5 (&+0xb4,0,0,0) @101c9bb..101c9da; 6/7 @101caa9, 8/9 @101ca87, 0xd/0xe @101ca65 all (&+0xb0,0,1,0); 4 (&final,1,0,0) @101ca12; 3 inline GetAnt, (&PixelTile(tg) or &final,1,0,0) @101ca1d..101ca5b; default (&final,0,0,0). Bump: FUN_010100e5(nt,0xdc) @101cae3..101caeb, sprite placed at (col*32,row*32) via FUN_010100ab; anim = [world+0x4954]->vt+0xc(0)[0xdc]; chd.anims()[0xdc] = 'bump', 1 frame 1000 ms, snd 47. |
| 11 | Remote ants | CONFIRMED (@101c7d9..101c8b2). Water: pause, SetPosition(CentreOf(fresh PixelTile)), SetAction(0), return 1. |
| 12 | Attack contact | CONFIRMED. ecx=[ebp-0x60] (occupant) unchanged from @101c577 to call @101c5c1. FUN_0101cb0c @101cb0c..101cbc9 exactly as stated. kbRange: sub 4/neg/sbb/and al,0xfd/add 4 -> 1 or 4. FUN_0101d8ed(&occ+0x5a,&this+0x5a,range); dir = FUN_01017531(attacker,target); retries d, d+1, d-1, d+2, d-2 (movzx then %8), else 8; landing test = in bounds && (layer2==0x86 || !solid) && !FUN_0101d822 && !(flags18&2). FUN_01022c57 fields as stated; FUN_010221fa(8,0x20) writes {+0 size, +4 type}. Dispatch FUN_0100d791 flag 0xa: handler [0x1047320+type*8] runs locally @100d965, then network send because bit 8 is set @100d96c. Table entry 0x1047360 = 0x1022ca1. Branch A returns 0 (@101cb05). |
| 13 | String ids | CONFIRMED (PE string table 48 "Can't do that...", 58 "Can't go there."; FUN_010292dc calls LoadStringA 0x10011d8). |
| 14 | Occupancy grid | CONFIRMED with nuance. bits 0-2 team, 3-7 idx, bit 8 MULTI (@100f4de..100f588, @100f17f..100f2cb). FUN_0100f2cd: best = first match; replaced by a later ant only if +0xd8==0, action stationary and (IsLocal(a) or !bestLocal). bestLocal is sticky and is also set when the default first-found ant is local, even if that ant is moving. A moving local ant found first therefore cannot be displaced by a stationary remote one. |
| 15 | ANTPAUSE | CONFIRMED (@101cc1e..101ccac; ctor @1024cf7..1024d28; fire @1024d2b..1024d84; vtable 0x1004ea0: +0xc 0x10305a8, +0x10 0x1010f11 (ret), +0x14 0x1024d2b). The `push 1` @1024d42 is cc1e's argument on the re-pause path and the 6th arg of ad02 on the other path. ad02 is `ret 0x18` @101b42d and never reads [ebp+0x1c]. SetAction(0) does not clear the pause (cc1e(0) only when action!=0 @101ae36..101ae40). |
| 16 | Timing | CONFIRMED with minor correction. Insert 0x1030e7b: +0x1c=a(0), FUN_010311a8 due=timeGetTime()+0, then +0x1c=300. Tick 0x10310e8 (vtable 0x1005208 slot +0x18) is called from FUN_01031916 @103197d only when PeekMessage(PM_NOREMOVE) finds no message, and processes only the list head (one task per call). The no-op first call returns 1, then reinsert at timeGetTime()+300. Effective pause = 300 ms + latency before the first head-processing tick + latency after due. This is not bounded by "one tick": due tasks ahead in the list and pending window messages both delay it. The "newtask" wheel forces at least one 8 ms bucket (@103147f..1031483), so its first call is not at now+0. That path is off by default: [0x104b448]=0; "oldtask" clears it, "newtask" sets 1. |

## Additional findings (missed or imprecise in the report)
- Walk-step caller (@101bf0a..101bfb4): TryEnterTile is called directly with (pos+d)/32 only when that tile equals the current waypoint wp[+0xde]. Otherwise the caller first nudges d by one pixel per axis and recomputes nt: step = (d<=0 ? -1 : +1), so a zero component becomes -1. When the tile has not changed but differs from wp[idx], the caller exits early (@101bf00).
- FUN_0101fc50 (re-path) calls FUN_0101ab87 first (@101fd14), which zeroes the order +0xa8 (@101ab94). The order is then re-derived from the target tile by FUN_01020655(t,a3,a2) (@101fd48, e.g. own hill -> order 2 @1020688, enemy hill -> 0xb). A "re-path" therefore re-issues a command to a tile; it does not preserve the order type. If the a2 gate FUN_0101ff5a fails (@101fc84 -> 0x101fef5), fc50 returns 0 without clearing the order or broadcasting. TryEnterTile has already freed the waypoints and set the ant idle, so the ant is left idle with a stale order and no path.
- FUN_0101fc50 and the stop handler FUN_01021664 (reached synchronously through FUN_010214d9 -> dispatch 0xa -> 0x10236c1) both SetPosition to the centre of the ant's current pixel tile. After TryEnterTile returns 0, the caller's d = CentreOf(+0x5a) - pos is therefore 0 (visible snap-to-centre on re-path, stop or "can't").
- FUN_0100f17f edge case: when leaving a MULTI cell and 0 other ants remain, only bit 8 is cleared and the low byte keeps the departing ant's team/idx. When leaving a non-MULTI cell, the low byte is set to 0xff unconditionally.
- Ant vtable slot +0x28 is 0x101a93a = SetPosition(x,y). The lead's "0x102b7bb?" guess is wrong: the vtable dword at 0x1004c08 is 0x101a93a. Slot +0x18 0x101a928 = SetPosition(point), which forwards to it.
- world == app object: the scheduler pointer at +0xe88 is the same whether reached via [0x104b478] or [0x104b350]. The world ctor calls the app ctor chain at @100a2ea -> 0x102c317 -> 0x10314c6.
