# RE report: ant animation tables (cluster "animtables")

All claims below were derived from Capstone disassembly of `Original-Ants/Ants.exe` (image base 0x01000000)
and the CHD Table-4 reader `chd.py`. Ghidra (`docs/legacy/Ants.exe.c`) was used only to navigate; where
Ghidra dropped arguments (FUN_01018a7a, FUN_010192fd, FUN_01018d48) the Capstone listing is authoritative.

Notation: `W` = world singleton `[0x104b350]`. `W.d[i]` = dword at `W + 4*i`. `c` = ant colour index
(`ant+0xd4`), `t` = ant type (FUN_0100f9cb(0)), `d` = facing dir (`ant+0xe0`, 0=N..7=NW), `tr` = terrain class.

---------------------------------------------------------------------------------------------------
## 0. Headline findings

1. **Mirroring (FUN_01018b9f)**: dirs 5/6/7 are made from dirs 3/2/1 by a DEEP copy of the sequence with:
   frame `dx := -dx` (dy, duration, event, sound, frame box unchanged); every sprite part
   `part.dx := -part.dx + (part.rect.L - part.rect.R)` = `-part.dx - imageWidth` (part.dy unchanged);
   each part gets a NEW image-wrapper object (vtable 0x1004940) sharing the same surface, with
   `wrapper.flags(+8) |= 1` = horizontal-mirror blit flag (consumed by the part blitter FUN_0102cfef at
   0x102d000..0x102d006 and the right-to-left store loop 0x102d150..0x102d159). Frame parts-bbox (+0x1a) and
   sequence bbox (+0x20) are recomputed from the mirrored parts. The 2nd parameter (e.g. 3138) is written to
   the new sequence's `+0xc` (animation id) and is a "virtual" id >= 1344 (not a CHD index).
2. **Tables have a colour dimension, not a team dimension.** Every static table is laid out
   `[4 colours][...]`. Colour-0 entries are the CHD animations (+ mirrored copies); colours 1..3 are created
   at game start by FUN_01018d48 -> FUN_010192fd, which makes a SHALLOW copy (shares the exact same frame
   list object - identical dx/dy/durations/events/sounds/parts) and only sets a new id (+0xc). The colour
   parameter passed to FUN_010192fd is never read.
3. **Colour index = `{3,2,1,0}[team]`** (word table 0x10021b0, used at 0x100da72 when the player object is
   created by FUN_010108de(colour) -> `player+0x2c`), copied into `ant+0xd4` by the ant constructor
   FUN_0101a77a (0x101a7c6..0x101a7d7). Team 3 uses colour 0 (the raw CHD tables).
4. **Team colouring is a palette-index shift at blit time**, not different frames: the ant draw method
   FUN_0101b802 (ant vtable+0x0c) sets `W+0xed4=1`, `W+0xed8 = byte[0x10049b0 + colour]` = {0,20,40,60},
   `W+0xedc = 0x101b7eb`; the part blitter adds that byte to every non-transparent 8-bit pixel
   (0x102d144..0x102d14e) unless the part's image name begins with a digit (callback 0x101b7eb =
   `!isdigit(part.name[0])`; `part+0x10` = first byte of the CHD image name, FUN_0102a683 0x102a6e5).
   So e.g. `3snork*.bmp` (swimming head) and `9death*.bmp` are never recoloured.
5. FUN_0101ad02 full action->animation map is in section 5; walk-case restart logic confirmed (C7), with
   corrections: the table block index is the **colour** (`+0xd4`), idle-in-water is a single per-colour
   animation (no direction), and non-swimmer "water" terrain uses the mud walk (class 3).
6. Walk data (C9) confirmed identical across the 6 types and carry/non-carry EXCEPT the phase of the mud
   diagonal (1,1)/(2,2) alternation, which differs per type/carry (table in section 7.1).
7. Sequence flags: bit3 = per-frame "sound once" mask (+0x30), bit4 = allow overlapping duplicate sound
   buffer if that sound is already playing, bit5 = remember started sound handles in list +0x34 and stop them
   when the animation is replaced/stopped. **None of them affect frame timing or movement.**
8. Data quirks in the original: 6 per-colour tables are 4-directional only (N,E,S,W); DAT_01004778
   colour-1 row has 0x7ffe for S and 0 for W/NW (team 2's action 0x11-on-land facing S has a NULL anim);
   swimmer (type 5) "dr" animation is the worker's `agdr301`.

---------------------------------------------------------------------------------------------------
## 1. Object layouts needed (verified)

Sequence / sprite object (base ctor FUN_0102b6da 0x102b6da, 0x48-byte sprite ctor FUN_01008faa):
```
+0x00 vtable            +0x08 dword  sound "view" (FUN_0102bac8 uses it when arg==0)
+0x0c word  anim id     (ctor arg; FUN_0102c0db copies template id; mirror/colour copies overwrite)
+0x0e word  flags: bit0 load-mark, bit1 loaded, bit2 RUNNING, bit3 sound-once, bit4 sound-dup, bit5 sound-track,
                   bit6 dirty(position changed). Base ctor: (old & 0xfff0) | 0x70 (0x102b712..0x102b725)
+0x10 dword next frame time (ms)   +0x14 char name[] (strcpy by FUN_0102c0db 0x102c14a)
+0x20 rect  seq bbox (L,T,R,B int16) +0x28 frame list (generic list, may be SHARED between sprites)
+0x2c frame cursor node  +0x30 sound-once bitmask  +0x34 list of started sound handles  +0x38/+0x3a int16 x,y
```
Frame record (0x2c bytes; reader FUN_0102a977, copier FUN_0102ac4a):
```
+0x08 int16 dx  +0x0a int16 dy  +0x0c uint16 duration ms  +0x0e word event (1111111 -> 11111)
+0x10 int16 sound id (-1 none)  +0x12..+0x18 int16 box L,T,R,B (from CHD; always (5,5,20,20) in ant anims)
+0x1a..+0x20 rect parts-bbox (union of part rects offset by part dx/dy)  +0x24 sound obj  +0x28 part list
```
Part record (0x20 bytes, vtable 0x1005028, ctor FUN_0102a1d4, reader 0x102a710, binder FUN_0102a683):
```
+0x08 int16 dx  +0x0a int16 dy  +0x0c word CHD image index  +0x0e flags (bit2 = hi-colour)
+0x10 byte  first char of image name (8-bit mode) / converted value (hi-colour)   [NOT a colour key]
+0x14 rect  (0,0,w,h) of the image            +0x1c image wrapper (+0x08 bit0 = mirror, +0x0c surface)
```
Rect helpers: FUN_010304c8(rect,dx,dy) offset; FUN_0103040b(rect,other) union (empty-aware; zero rect = empty).

---------------------------------------------------------------------------------------------------
## 2. FUN_01018a7a / FUN_01018b9f / FUN_01018ac1 (mirroring)

```c
// __thiscall(world) ret 8   0x1018a7a
void MirrorRow(dword *row /*8 anim ptrs*/, uint16 *ids /*8 static ids*/) {
    row[5] = MirrorAnim(row[3], ids[5]);   // 0x1018a87..0x1018a94  SE -> SW
    row[6] = MirrorAnim(row[2], ids[6]);   // 0x1018a97..0x1018aa6  E  -> W
    row[7] = MirrorAnim(row[1], ids[7]);   // 0x1018aa9..0x1018ab8  NE -> NW
}

// __thiscall(world, unused) ret 8   0x1018b9f
Sprite *MirrorAnim(Sprite *src, uint16 id) {
    if (!src) return 0;                                         // 0x1018baf..0x1018bb6
    Sprite *dst = new Sprite(0x48) ; FUN_01008faa(dst, 0x7ffe); // 0x1018bbd..0x1018bdb
    FUN_0102c0db(src, dst, /*deep*/1, /*partCopyCb*/0x1018ac1); // 0x1018bee..0x1018bf8
    dst->id = id;                                               // 0x1018bfd..0x1018c04
    List *fl = dst->frames; saved = fl->cursor;                 // 0x1018c01..0x1018c11
    Rect seq = {0,0,0,0};                                       // 0x1018c1c..0x1018c28
    for (Frame *f = fl->first(); f; f = fl->next(/*wrap*/0)) {  // 0x1018c2c / 0x1018d05
        f->dx = -f->dx;                                         // 0x1018c4d..0x1018c57
        List *pl = f->parts; savedP = pl->cursor;               // 0x1018c51..0x1018c61
        f->partsBox = {0,0,0,0};                                // 0x1018c64..0x1018c75
        for (Part *p = pl->first(); p; p = pl->next(0)) {       // 0x1018c78 / 0x1018cda
            p->dx = (int16)(-p->dx + (p->rect.L - p->rect.R));  // 0x1018c83..0x1018c9b  (= -dx - width)
            p->image->flags |= 1;                               // 0x1018c98..0x1018ca2  mirror blit
            Rect r = p->rect; r.offset(p->dx, p->dy);           // 0x1018ca6..0x1018cbc
            f->partsBox.union(r);                               // 0x1018cc1..0x1018cc8
        }
        pl->cursor = savedP;                                    // 0x1018cf6
        seq.union(f->partsBox);                                 // 0x1018ce1..0x1018ce8
    }
    dst->bbox = seq;                                            // 0x1018d1a..0x1018d23
    fl->cursor = saved;                                         // 0x1018d26..0x1018d29
    return dst;
}
// part-copy callback 0x1018ac1 -> FUN_01018ad3(world, srcPart): new 0x10-byte wrapper (vtable 0x1004940,
// ctor 0x1018b23 clears +0xc and bit0 of +8), wrapper->surface(+0xc) = srcPart->image->surface, addref.
// FUN_0102a5c4 (part copy) copies dx,dy,rect,+0x10,+0xe bit2 and calls the callback for +0x1c (0x102a66a).
```
Frame dy, duration, event, sound, frame box (+0x12..+0x18) are NOT touched (only +8 and +0x1a..+0x20).
Example (worker grass E `agwg901` -> W, id 3139): frames 12x(dx -4, dy 0, 50 ms); frame0 part
`agwa901.bmp` w=28 dx=-14 -> mirrored part dx = 14-28 = -14 drawn flipped.

Ordering: mirroring runs at startup at the end of FUN_010175ad (0x101888d..0x1018a53) over colour-0 blocks
only; per-colour copies (section 3) are made later and copy all 8 dirs, so mirrored anims of colours 1..3
share colour 0's mirrored frame lists.

Mirror call list (0x10188bf..0x1018a53), for t=0..5 and tr=0..4:
```
MirrorRow(W+0x1240+t*0xa0+tr*0x20, DAT_01002fb8+(t*40+tr*8)*2)   walk
MirrorRow(W+0x24a0+t*0xa0+tr*0x20, DAT_01003738+(t*40+tr*8)*2)   carry walk
MirrorRow(W+0x0f40+t*0x20, DAT_01002cb8+t*16)  idle      MirrorRow(W+0x21a0+t*0x20, DAT_01002e38+t*16) carry idle
MirrorRow(W+0x3400+t*0x20, DAT_01003f18+t*16)  (act 5)   MirrorRow(W+0x3700+t*0x20, DAT_01004098+t*16) (act 0x12)
MirrorRow(W+0x3a00+t*0x20, DAT_01004218+t*16)  (act 0xe) MirrorRow(W+0x3d00+t*0x20, DAT_01004398+t*16) (act 0x13)
then single rows: 0x4240/0x1004638, 0x42c0/0x1004678, 0x4540/0x10047b8, 0x45c0/0x10047f8, 0x4640/0x1004838,
0x46c0/0x1004878, 0x4740/0x10048c0, 0x4340/0x10046b8, 0x43c0/0x1004738, 0x4440/0x10046f8, 0x44c0/0x1004778
```
Water rows (tr=2) of walk tables are NULL (static 0x7ffe) so their mirrors stay NULL.

---------------------------------------------------------------------------------------------------
## 3. Table construction and per-colour copies

### 3.1 Startup, colour 0 (FUN_010175ad, called once from 0x100acff)
`esi = CHD anim array` (world+0x4954 vtable+0xc(0)); `anim(id) = esi[id]` (addref'd, FUN_010297ed).
Only dirs 0..4 are loaded from the static tables; 0x7ffe -> NULL for walk/carry and the 4-dir tables.
Evidence: walk/carry loop 0x1018326..0x10183b4 (world index `t*40+tr*8+d`, static same index);
idle/carry-idle/0x3400/0x3700/0x3a00/0x3d00 at 0x10183b6..0x10184b8 (world dword index `base + t*8 + d`);
per-type singles 0x10184c1..0x1018601; per-colour-dir tables 0x101860d..0x1018817; singles 0x101881d..0x1018876.

### 3.2 Game start, colours 1..3 (FUN_01018d48, called from 0x100e32c)
```c
for (slot = 0; slot < 4; slot++) {                                   // 0x1018d53..0x10192f2
    Player *p = W->players[slot];  if (!p) continue;                 // W+0x4958+slot*4
    c = p->colour (+0x2c);  if (c == 0) continue;                    // 0x1018d76..0x1018d83
    for t in 0..5: for d in 0..7:                                    // ALL 8 dirs (incl. mirrored)
        for tr in 0..4:
            W.d[0x490 + 240c + 40t + 8tr + d] = Copy(W.d[0x490 + 40t + 8tr + d], DAT_01002fb8[240c+40t+8tr+d])
            W.d[0x928 + ...same...]           = Copy(W.d[0x928 + 40t+8tr+d],   DAT_01003738[...])
        for (base,tbl) in (0x3d0,2cb8)(0x868,2e38)(0xd00,3f18)(0xdc0,4098)(0xe80,4218)(0xf40,4398):
            W.d[base + 8(t+6c) + d] = Copy(W.d[base + 8t + d], tbl[48c + 8t + d])
      per-type singles (byte offsets):  W[off + 0x18*c + 4*t] = Copy(W[off + 4*t], tbl[6c + t])
        off/tbl: 0x2140/3eb8 0x33a0/3ee8 0x4000/4518 0x4060/4548 0x40c0/4578 0x4120/45a8 0x4180/45d8 0x41e0/4608 0x47f0/4910
    for d in 0..7: per-colour-dir: W[base + 0x20*c + 4*d] = Copy(W[base + 4*d], tbl[8c + d])     // 0x1019104..0x1019282
        base/tbl: 0x4240/4638 0x42c0/4678 0x4340/46b8 0x43c0/4738 0x4440/46f8 0x44c0/4778 0x4540/47b8 0x45c0/47f8
                  0x4640/4838 0x46c0/4878 0x4740/48c0
    W[0x47c0+4c] = Copy(W[0x47c0], word[0x10048b8+2c]);  W[0x47d0+4c] = Copy(W[0x47d0], word[0x1004900+2c]);
    W[0x47e0+4c] = Copy(W[0x47e0], word[0x1004908+2c]);                                     // 0x1019288..0x10192e4
}
// FUN_010192fd(src, id, colourUnused) ret 0xc   0x10192fd
Sprite *Copy(Sprite *src, uint16 id) {
    if (!src || id == 0x7ffe) return 0;                  // 0x1019307..0x101931b
    Sprite *n = new Sprite(0x48); FUN_01008faa(n, 0x7ffe);
    FUN_0102c0db(src, n, /*deep*/0, 0);                  // 0x1019345..0x101934a: n->frames = src->frames (addref, SHARED)
    n->id = id;                                          // 0x101934f
    return n;
}
```
`W+0x4850..0x485c` (death1..4 = CHD 102..105, set at 0x1017fc4..0x101801c) have no colour copies.

### 3.3 Colour index source
```
0x100da72  cx = word[0x10021b0 + team*2]   // table = 3,2,1,0
0x100da7d  FUN_010108de(player, cx) -> 0x101090d  player+0x2c = colour
0x101a7c6  ant ctor FUN_0101a77a(team,arg): ant+0xd4 = W->players[team]->+0x2c ; ant+0x56 = team
```
FUN_01021c68 (action 0xa helper) recomputes the colour the same way from `ant+0x56` (0x1021cb2..0x1021cc5).

### 3.4 Team colour rendering (for completeness; not a frame-data change)
FUN_0101b802 (ant vtable+0x0c, 0x101b81e..0x101b85b): `W+0xed8 = byte[0x10049b0 + colour]` (00 14 28 3c),
`W+0xed4 = 1`, `W+0xedc = 0x101b7eb`, draws, then `W+0xed4 = 0`. FUN_0102cfef (part blit):
`mirror = part->image->flags & 1` (0x102d000); `remap = W+0xed4 && callback(part)`; if neither, fast blit
(0x102d039..0x102d04c); else per-pixel loop: skip pixels == `W+0xee8` (transparent, 0xfe by default,
0x102c3b4), else `dest = src + (remap ? W+0xed8 : 0)`; dest pointer walks right-to-left when mirrored.
Callback 0x101b7eb returns `isdigit((signed char)part[+0x10]) == 0` (FUN_01034ab0 = CRT isdigit, ctype mask 4).
The blitter reads these fields from `[0x104b450]` (engine) while the ant writes `[0x104b350]+...`: they are the same
object — the world ctor 0x100a2c9 calls the engine base ctor FUN_0102c317 on the same `this` (0x100a2ea), which stores
`[0x104b450] = this` (0x102c402); the world ctor later stores `[0x104b350] = this` (0x100aa26).
Hi-colour mode (`W+0xeb8 != 0`) uses a different part+0x10 conversion (FUN_0102936c) that was not decoded.

---------------------------------------------------------------------------------------------------
## 4. Complete world animation-table map (byte offset from W; every entry = Sprite* template)

`K = t + 6*c` (type+colour block). Static tables are `uint16` CHD indices laid out with the same shape
(colour-major); 0x7ffe = none. dirs 5..7 of every static row hold the virtual ids used for the mirrored
copies (colour 0) or the per-colour copies (colours 1..3).

| W offset | dword idx | shape | static source | CHD names (c0, t0, d0..4) | used by (FUN_0101ad02 action) |
|---|---|---|---|---|---|
| 0x0f40 | 0x3d0 | [4c][6t][8d] | DAT_01002cb8 | agst701/801/901/201/301 | 0 idle (not holding); 0xa (+0xb4, not holding) |
| 0x1240 | 0x490 | [4c][6t][5tr][8d] | DAT_01002fb8 | agwg701.. (tr0), agws (1), none (2), agwm (3), agwd (4) | 1 walk (not holding) |
| 0x2140 | 0x850 | [4c][6t] | DAT_01003eb8 | agh0 | 2 (not holding) |
| 0x21a0 | 0x868 | [4c][6t][8d] | DAT_01002e38 | hgst701.. | 0 idle holding; 0xa holding |
| 0x24a0 | 0x928 | [4c][6t][5tr][8d] | DAT_01003738 | hgwg701.. | 1 walk holding |
| 0x33a0 | 0xce8 | [4c][6t] | DAT_01003ee8 | hgh0 | 2 holding |
| 0x3400 | 0xd00 | [4c][6t][8d] | DAT_01003f18 | aggf701/887 aggf801/.. | 5 |
| 0x3700 | 0xdc0 | [4c][6t][8d] | DAT_01004098 | agat701.. | 0x12 |
| 0x3a00 | 0xe80 | [4c][6t][8d] | DAT_01004218 | aggh701.. | 0xe |
| 0x3d00 | 0xf40 | [4c][6t][8d] | DAT_01004398 | aggb701.. | 0x13; 0xa when +0xb4==0 |
| 0x4000 | 0x1000 | [4c][6t] | DAT_01004518 | agbu301 | FUN_01021c68 overlay object (action 0xa, +0xb4!=0) |
| 0x4060 | 0x1018 | [4c][6t] | DAT_01004548 | agcg301 | 0xb (not holding) |
| 0x40c0 | 0x1030 | [4c][6t] | DAT_01004578 | hgcg301 | 0xb holding |
| 0x4120 | 0x1048 | [4c][6t] | DAT_010045a8 | aghatch | 0x14 |
| 0x4180 | 0x1060 | [4c][6t] | DAT_010045d8 | agsd301 | 3 (not holding) |
| 0x41e0 | 0x1078 | [4c][6t] | DAT_01004608 | hgsd301 | 3 holding |
| 0x4240 | 0x1090 | [4c][8d] 4-dir | DAT_01004638 | afsf701, -, afsf901, -, afsf301 | 6 |
| 0x42c0 | 0x10b0 | [4c][8d] 4-dir | DAT_01004678 | afxf701, -, afxf901, -, afxf301 | 7 |
| 0x4340 | 0x10d0 | [4c][8d] 4-dir | DAT_010046b8 | asbbw701/901/301 | 0x10, own tile water |
| 0x43c0 | 0x10f0 | [4c][8d] 4-dir | DAT_01004738 | asbbl701/901/301 | 0x10, own tile not water |
| 0x4440 | 0x1110 | [4c][8d] 4-dir | DAT_010046f8 | asdbw701/901/301 | 0x11, water |
| 0x44c0 | 0x1130 | [4c][8d] 4-dir | DAT_01004778 | asdbl701/901/301 | 0x11, not water |
| 0x4540 | 0x1150 | [4c][8d] 4-dir | DAT_010047b8 | absb701/901/301 | 8 |
| 0x45c0 | 0x1170 | [4c][8d] 4-dir | DAT_010047f8 | abdb701/901/301 | 9 |
| 0x4640 | 0x1190 | [4c][8d] | DAT_01004838 | assw701/801/901/201/301 | 1 walk, terrain 2 (type 5) |
| 0x46c0 | 0x11b0 | [4c][8d] | DAT_01004878 | asdi701.. | 1 walk dive-in |
| 0x4740 | 0x11d0 | [4c][8d] | DAT_010048c0 | asgo701.. | 1 walk climb-out |
| 0x47c0 | 0x11f0 | [4c] | word 0x10048b8[c] | astw301 (CHD 1006) | 0 idle when terrain==2 (no direction) |
| 0x47d0 | 0x11f4 | [4c] | word 0x1004900[c] | atcr501 (1095) | 0xd |
| 0x47e0 | 0x11f8 | [4c] | word 0x1004908[c] | getpow (55) | 4 |
| 0x47f0 | 0x11fc | [4c][6t] | DAT_01004910 | agdr301 (t5 also agdr301) | 0xf |
| 0x4850 | 0x1214 | [4] | CHD 102..105 (0x1017fc4) | death1..death4 | 0xc (rand()%4) |

Walk-table index (dword): `0x490 + d + 8*(tr + 5*K)` (carry 0x928), exactly `lea` at 0x101b444..0x101b47a.
Per-type-dir: `base + d + 8*K`. Per-type single: `off + 4*K` bytes. Per-colour-dir: `base + d + 8*c` (dword).
Colour-id scheme example (walk t0 grass): c0 = 817,818,819,820,816,(3138,3139,3140); c1 = 1352..1359;
c2 = 1934..1941; c3 = 2516..2523.

---------------------------------------------------------------------------------------------------
## 5. FUN_0101ad02 — SetAction(action, dir, terrA, terrB, flag, extra)  [__thiscall ant, ret 0x18]

`extra` ([ebp+0x1c]) is never read inside the function. terrA/terrB are compared as int16.
```c
void Ant::SetAction(int action, int16 dir, int16 terrA, int16 terrB, int flag, int extra) {
    int sameAction = (this->action(+0xe4) == action);                         // 0x101ad19..0x101ad29
    int cflag = (action == 3 || this->+0x84 != 0);                             // 0x101ad26..0x101ad3c
    switch (this->action) {            // old action; jump table 0x101b48f for old 2..0x14 (0x101ad3d)
      case 2: case 0x14: FUN_0101e165(cflag); break;
      case 5:  FUN_0101e342(cflag); break;      case 6:  FUN_0101e798(cflag); break;
      case 7:  FUN_0101e97b(cflag); break;      case 8:  FUN_0101e433(cflag); break;
      case 9:  FUN_0101e599(cflag); break;      case 0xd: FUN_0101e27f(cflag); break;
      case 0x10: FUN_0101eaec(cflag); break;    case 0x11: FUN_0101ecdf(cflag); break;
      case 0x12: FUN_0101ecc4(cflag); break;
      case 0xa: case 0xe: case 0x13:                                          // 0x101ade0
        if (action!=0xc && action!=0xf && action!=0xe && action!=0x13 && action!=0xa)
            if (FUN_0101e68c(cflag) && this->hp(+0x74) == 0) goto done;        // ignored
        break;
      case 0xc: FUN_0101e120(cflag); goto done;                                // dying: never changes again
      case 0xf: if (action == 0xf) goto done; if (!FUN_0101e0c2(cflag)) goto done; break;
      default /*0,1,3,4,0xb*/: break;
    }
    int sameDir = (this->dir(+0xe0) == dir);                                   // 0x101ae19..0x101ae38
    this->dir = dir;  this->action = action;
    if (action != 0) FUN_0101cc1e(this, 0);                                    // 0x101ae3b
    if (this->action != 3) FUN_0102151a(this, 0);                              // 0x101ae45
    this->+0x08 = W->+0x4a7c;               // sound view valid only inside this call (cleared at 0x101b41f)
    int supplied;
    // ownTile = FUN_0100ccc0(this): tile from pixel position (x/32, y/32)
    if (terrA == -1) { supplied = 0; terrA = FUN_01008af7(map, ownTile.row, ownTile.col); }   // 0x101ae5b..0x101ae90
    else supplied = 1;
    int16 t = FUN_0100f9cb(this, 0);   int c = this->colour(+0xd4);  int K = t + 6*c;  int d = this->dir;
    Sprite *a;
    switch (this->action) {                                                   // table 0x101b4db (0..0x14)
    case 0:  // 0x101aebc   idle
        a = (terrA == 2) ? W[0x47c0 + 4c] : W.d[(holding(+0xe8) ? 0x868 : 0x3d0) + d + 8K];
        a->Play(this,0,0); this->+0x88 = 0; goto done;                         // always restarts
    case 1:  goto walk;                                                        // 0x101b032
    case 2:  // 0x101b1c1
        r = 10 - this->hp;                                                     // FUN_0101e204
        a = holding ? W[0x33a0 + 4K] : W[0x2140 + 4K];
        a->Play(this, /*deep copy*/1, 0);
        FUN_0101e20d(this, r): if (r) for each frame f with f->event==5: f->duration = r*200;  // 0x101e24c..0x101e253
        goto done;
    case 3:  this->dir = 4;  a = holding ? W[0x41e0+4K] : W[0x4180+4K]; break;  // 0x101b39f (dir forced S, AFTER sameDir)
    case 4:  a = W[0x47e0 + 4c]; break;                                        // getpow
    case 5:  a = W.d[0xd00 + d + 8K]; break;
    case 6:  a = W.d[0x1090 + d + 8c]; break;      case 7: a = W.d[0x10b0 + d + 8c]; break;
    case 8:  a = W.d[0x1150 + d + 8c]; break;      case 9: a = W.d[0x1170 + d + 8c]; break;
    case 0xa:  // 0x101af27
        if (this->+0xb4) {
            o = new(0x4c) FUN_01021c68(this);   // plays W[0x4000+4K] at centre of ant tile, added to display
            W->+0x4a7c->vtbl[0x28](o, 0); release(o);
            a = W.d[(holding ? 0x868 : 0x3d0) + d + 8K];       // ant itself shows idle
        } else a = W.d[0xf40 + d + 8K];                       // same as 0x13
        break;
    case 0xb:  this->orderTarget(+0xac) = {0x5a,0x78}; a = holding ? W[0x40c0+4K] : W[0x4060+4K]; break;
    case 0xc:  a = W[0x4850 + 4*(rand() % 4)]; break;                         // 0x101b3fd (CRT rand 0x10345c0)
    case 0xd:  W[0x47d0+4c]->Play(this,0,0);                                   // 0x101b154
               if (this->child(+0x50) && (child->flags & 4)) W->+0x4a7c->vtbl[0x2c](child);  // remove from display
               goto done;
    case 0xe:  a = W.d[0xe80 + d + 8K]; break;
    case 0xf:  a = W[0x47f0 + 4K]; break;
    // 0x10/0x11 use the STORED tile (+0x5a row, +0x5c col), not the pixel-derived tile (0x101b2dc..0x101b2f1)
    case 0x10: a = (FUN_01008af7(map, this->+0x5a, this->+0x5c) == 2) ? W.d[0x10d0 + d + 8c] : W.d[0x10f0 + d + 8c]; break;
    case 0x11: a = (FUN_01008af7(map, this->+0x5a, this->+0x5c) == 2) ? W.d[0x1110 + d + 8c] : W.d[0x1130 + d + 8c]; break;
    case 0x12: this->orderTarget = {0x5a,0x78}; a = W.d[0xdc0 + d + 8K]; break;
    case 0x13: a = W.d[0xf40 + d + 8K]; break;
    case 0x14: a = W[0x4120 + 4K]; break;
    default /* >0x14 */: goto done;
    }
    a->Play(this, 0, 0);  goto done;                                           // 0x101b417 (always restarts)

walk:   // 0x101b032
    if (flag && t == 5) {
        int diveIn   = (terrA != 2 && terrB == 2);
        int climbOut = (terrA == 2 && terrB != 2);
        if (diveIn || climbOut) {
            a = diveIn ? W.d[0x11b0 + d + 8c] : W.d[0x11d0 + d + 8c];
            a->Play(this,0,0); this->+0x88 = 1; goto done;                     // 0x101b0b5..0x101b0ba
        }
    }
    int16 tr = (!supplied) ? terrA : (flag ? terrA : terrB);                  // 0x101b0c9..0x101b0d6
    if (tr == 2 && t != 5) tr = 3;                                             // 0x101b0d9..0x101b0e4
    if (tr == 2)            a = W.d[0x1190 + dir + 8c];                        // 0x101b430 (uses the dir argument)
    else if (tr <= 4)       a = W.d[(holding ? 0x928 : 0x490) + d + 8*(tr + 5K)];   // 0x101b444
    else                    a = (Sprite*)dir;   // unreachable garbage path (0x101b10a), never happens: classes are 0..4
    int restart;
    if (!sameAction)            restart = 1;
    else if (!supplied)         restart = 1;
    else if (this->+0x88)     { this->+0x88 = 0; restart = 1; }
    else if (flag)              restart = !sameDir;
    else                        restart = (terrA != terrB);
    if (restart) a->Play(this, 0, 0);                                          // 0x101b14c -> 0x101b417
done:
    this->+0x08 = 0;                                                           // 0x101b41f
}
```
`a->Play(target, deep, cb)` = FUN_0102c0db (section 8). A NULL `a` would fault inside FUN_0102c0db; callers
must only request actions whose table entry exists (4-dir tables only have N/E/S/W).

Action/CHD summary (colour 0, type 0 = worker; `d0..4` = N,NE,E,SE,S):

| act | table | worker CHD anims | notes |
|---|---|---|---|
| 0 | 0x3d0 / 0x868 / 0x47c0 | agst701,801,901,201,301 / hgst* / astw301 | water idle ignores dir |
| 1 | 0x490 / 0x928 / 0x1190 / 0x11b0 / 0x11d0 | agwg* agws* agwm* agwd* / hg** / assw* asdi* asgo* | see 7.1 |
| 2 | 0x2140 / 0x33a0 | agh0 / hgh0 | frames with evt 5 get 200 ms x (10-hp) |
| 3 | 0x4180 / 0x41e0 | agsd301 / hgsd301 | forces dir 4 |
| 4 | 0x47e0 | getpow | per colour only |
| 5 | 0xd00 | aggf701,aggf801(887),aggf901,aggf201(886),aggf301 | |
| 6 | 0x1090 | afsf701,-,afsf901,-,afsf301 (W = mirror id 3277) | 4-dir |
| 7 | 0x10b0 | afxf701,-,afxf901,-,afxf301 (W 3280) | 4-dir |
| 8 | 0x1150 | absb701,-,absb901,-,absb301 (W 3190) | 4-dir |
| 9 | 0x1170 | abdb701,-,abdb901,-,abdb301 (W 3193) | 4-dir |
| 0xa | overlay 0x4000 + idle, or 0xf40 | agbu301 + agst*, or aggb* | depends on +0xb4 |
| 0xb | 0x4060 / 0x40c0 | agcg301 / hgcg301 | clears +0xac |
| 0xc | 0x4850 | death1..death4 random | locks further SetAction |
| 0xd | 0x47d0 | atcr501 | also hides +0x50 child |
| 0xe | 0xe80 | aggh701.. | knock-back 32 px |
| 0xf | 0x47f0 | agdr301 | |
| 0x10 | 0x10d0 / 0x10f0 | asbbw* / asbbl* | 4-dir, own-tile terrain chooses |
| 0x11 | 0x1110 / 0x1130 | asdbw* / asdbl* | 4-dir |
| 0x12 | 0xdc0 | agat701.. | clears +0xac |
| 0x13 | 0xf40 | aggb701.. | blown back 128 px |
| 0x14 | 0x4120 | aghatch | |

---------------------------------------------------------------------------------------------------
## 6. Play (FUN_0102c0db) and sequence flags — only what affects timing

```c
// __thiscall(template) ret 0xc   0x102c0db
void Play(Sprite *tpl, Sprite *tgt, int deep, void *partCb) {
    if (tgt->flags & 0x20) FUN_0102bdab(tgt);             // stop sounds started by the previous anim (0x102c0ef)
    tgt->flags = (tgt->flags & ~0x38) | (tpl->flags & 0x38);  // bits 3,4,5 copied; RUNNING bit2 kept (0x102c100..0x102c12a)
    tgt->id = tpl->id; tgt->bbox = tpl->bbox; strcpy(tgt->name, tpl->name);
    release(tgt->frames); tgt->frames = 0;
    if (deep) { for each frame f of tpl (cursor saved/restored): append(tgt, FUN_0102ac4a(f, partCb)); }
    else      { addref(tpl->frames); tgt->frames = tpl->frames; }          // SHARED list (0x102c1d0..0x102c1df)
    FUN_0102c1fc(tgt, 0);
}
void FUN_0102c1fc(Sprite *s, int arg) {        // 0x102c1fc
    if (!s->frames) return;
    if (s->flags & 8) s->soundOnceMask(+0x30) = 0;
    s->cursor(+0x2c) = 0;  s->nextTime(+0x10) = timeGetTime();
    if (s->flags & 4) s->vtbl[0x10](arg);        // immediate "start" step when already on the display list
}
```
Because the frame list (and its internal cursor) can be shared by many sprites, FUN_0102b997 re-seeks the
list to the sprite's own node every step (0x102b9ef..0x102b9f5).

Sequence flag bits (FUN_0102be51 0x102beae..0x102bf1c: 1st dword!=0 -> bit3, 2nd -> bit4, 3rd -> bit5):
- bit3 "sound once": FUN_0102bac8 0x102bb26..0x102bb4c: `m = 1 << cursorIndex; play = !(mask & m); mask |= m`
  (mask reset at Play). Effect: each frame's sound plays only the first loop.
- bit4: passed as the last arg of the positional/non-positional play (0x102bbb7..0x102bbcd /
  0x102bbe2..0x102bbf4) -> FUN_0102e955 `[ebp+0x18]`: if the sound is already playing, duplicate the buffer
  and play again (0x102e9ef..0x102ea38); otherwise only volume/pan are updated (0x102ea5c..0x102ea7a).
- bit5: started sound handles are appended to list +0x34 (0x102bbfb..0x102bc40); FUN_0102bdab stops them
  (called from Play on the old anim and from vtbl+0x24 stop 0x102c245).
- None of these change `+0x10`/durations/dx/dy. Walk animations have no frame sounds at all.

---------------------------------------------------------------------------------------------------
## 7. Frame data (colour 0; colours 1..3 are byte-identical by construction)

Format per anim: `CHD# name [flags O=bit3 L=bit4 T=bit5] n=frames T=total ms d=(sum dx, sum dy)` then
run-length frames `Nx(dx,dy,dur[,eEvent][,sSound])`. Frame box is (5,5,20,20) for every frame listed.
Mirrored dirs (5,6,7) = dirs (3,2,1) with every dx negated (section 2).

### 7.1 Walk (DAT_01002fb8) / carry walk (DAT_01003738): verified
All 6 types and carry/non-carry have identical per-frame (dx,dy,dur) for every terrain/dir, no events, no
sounds, 12 frames per loop, except the PHASE of the mud diagonals (which step comes first):

| terrain | dir0 N | dir1 NE | dir2 E | dir3 SE | dir4 S |
|---|---|---|---|---|---|
| 0 grass | 12x(0,-4,50) | 12x(3,-3,50) | 12x(4,0,50) | 12x(3,3,50) | 12x(0,4,50) |
| 1 sand  | 12x(0,-4,40) | 12x(3,-3,40) | 12x(4,0,40) | 12x(3,3,40) | 12x(0,4,40) |
| 2 water | none (swimmer uses assw; non-swimmer uses class 3) | | | | |
| 3 mud   | 12x(0,-2,60) | 6x[(1,-1),(2,-2)] or 6x[(2,-2),(1,-1)] @60 | 12x(2,0,60) | 6x[(1,1),(2,2)] or 6x[(2,2),(1,1)] @60 | 12x(0,2,60) |
| 4 dirt  | 12x(0,-4,60) | 12x(3,-3,60) | 12x(4,0,60) | 12x(3,3,60) | 12x(0,4,60) |

Mud diagonal phase ("1" = starts with (1,1)-magnitude step, "2" = starts with (2,2)):

| table | dir | t0 | t1 | t2 | t3 | t4 | t5 |
|---|---|---|---|---|---|---|---|
| walk  | NE (1) | 1 | 2 | 2 | 1 | 1 | 2 |
| walk  | SE (3) | 1 | 1 | 1 | 2 | 1 | 1 |
| carry | NE (1) | 2 | 1 | 2 | 2 | 1 | 2 |
| carry | SE (3) | 2 | 1 | 2 | 2 | 1 | 2 |

(Mirrors inherit: SW phase = SE phase, NW phase = NE phase.) CHD names: walk `a{g,b,f,t,c,s}w{g,s,m,d}{7,8,9,2,3}01`,
carry `h{g,b,f,t,c,s}w..` (e.g. worker grass 817 818 819 820 816; carry names hgwg701.. ).

### 7.2 Idle (DAT_01002cb8 -> W+0xf40) and carry idle (DAT_01002e38 -> W+0x21a0)
All idle frames have dx=dy=0, no events, no sounds. Note hcst201 is CHD 939 (out of sequence) and the
combat-ant idle facing S (acst301/hcst301) is a 64-frame 7820 ms fidget.
```
### idle DAT_01002cb8
  t0 N :  812 agst701    [LT ] n= 6 T=  900ms d=(0,0)
        6x(0,0,150)
  t0 NE:  813 agst801    [OLT] n=12 T= 1800ms d=(0,0)
        12x(0,0,150)
  t0 E :  814 agst901    [LT ] n=12 T= 1725ms d=(0,0)
        8x(0,0,150) (0,0,75) 3x(0,0,150)
  t0 SE:  815 agst201    [LT ] n=13 T= 1800ms d=(0,0)
        8x(0,0,150) 2x(0,0,75) 3x(0,0,150)
  t0 S :  811 agst301    [LT ] n=12 T= 1650ms d=(0,0)
        8x(0,0,150) 2x(0,0,75) 2x(0,0,150)
  t0 mirror ids 5..7: [3135, 3136, 3137]
  t1 N :  775 abst701    [LT ] n= 8 T=  800ms d=(0,0)
        8x(0,0,100)
  t1 NE:  776 abst801    [LT ] n= 8 T=  800ms d=(0,0)
        8x(0,0,100)
  t1 E :  777 abst901    [LT ] n= 8 T=  800ms d=(0,0)
        8x(0,0,100)
  t1 SE:  773 abst201    [LT ] n= 8 T=  800ms d=(0,0)
        8x(0,0,100)
  t1 S :  774 abst301    [LT ] n= 8 T=  800ms d=(0,0)
        8x(0,0,100)
  t1 mirror ids 5..7: [3162, 3163, 3164]
  t2 N :  677 afst701    [LT ] n= 6 T=  600ms d=(0,0)
        6x(0,0,100)
  t2 NE:  678 afst801    [LT ] n= 6 T=  600ms d=(0,0)
        6x(0,0,100)
  t2 E :  679 afst901    [LT ] n= 6 T=  600ms d=(0,0)
        6x(0,0,100)
  t2 SE:  675 afst201    [LT ] n= 6 T=  600ms d=(0,0)
        6x(0,0,100)
  t2 S :  676 afst301    [LT ] n= 6 T=  600ms d=(0,0)
        6x(0,0,100)
  t2 mirror ids 5..7: [3249, 3250, 3251]
  t3 N : 1086 atst701    [LT ] n=26 T= 2520ms d=(0,0)
        11x(0,0,100) 2x(0,0,80) 11x(0,0,100) 2x(0,0,80)
  t3 NE: 1087 atst801    [LT ] n=26 T= 2520ms d=(0,0)
        11x(0,0,100) 2x(0,0,80) 11x(0,0,100) 2x(0,0,80)
  t3 E : 1088 atst901    [LT ] n=26 T= 2520ms d=(0,0)
        11x(0,0,100) 2x(0,0,80) 11x(0,0,100) 2x(0,0,80)
  t3 SE: 1084 atst201    [LT ] n=26 T= 2520ms d=(0,0)
        11x(0,0,100) 2x(0,0,80) 11x(0,0,100) 2x(0,0,80)
  t3 S : 1085 atst301    [OLT] n=26 T= 2520ms d=(0,0)
        11x(0,0,100) 2x(0,0,80) 11x(0,0,100) 2x(0,0,80)
  t3 mirror ids 5..7: [3321, 3322, 3323]
  t4 N :  898 acst701    [LT ] n= 4 T=  600ms d=(0,0)
        4x(0,0,150)
  t4 NE:  899 acst801    [LT ] n= 4 T=  600ms d=(0,0)
        4x(0,0,150)
  t4 E :  900 acst901    [LT ] n= 4 T=  600ms d=(0,0)
        4x(0,0,150)
  t4 SE:  896 acst201    [LT ] n= 4 T=  600ms d=(0,0)
        4x(0,0,150)
  t4 S :  897 acst301    [LT ] n=64 T= 7820ms d=(0,0)
        8x(0,0,125) (0,0,60) (0,0,80) (0,0,200) (0,0,60) (0,0,100) (0,0,120) 8x(0,0,125) (0,0,60) (0,0,80) (0,0,200) (0,0,60) (0,0,100) (0,0,120) 8x(0,0,125) (0,0,60) (0,0,80) (0,0,200) (0,0,60) (0,0,100) (0,0,120) 8x(0,0,125) (0,0,80) 2x(0,0,100) (0,0,80) 3x(0,0,300) (0,0,80) (0,0,60) (0,0,80) (0,0,200) (0,0,60) (0,0,100) (0,0,120)
  t4 mirror ids 5..7: [3195, 3196, 3197]
  t5 N :  964 asst701    [LT ] n= 6 T=  900ms d=(0,0)
        6x(0,0,150)
  t5 NE:  965 asst801    [LT ] n= 6 T=  900ms d=(0,0)
        6x(0,0,150)
  t5 E :  966 asst901    [LT ] n= 6 T=  900ms d=(0,0)
        6x(0,0,150)
  t5 SE:  962 asst201    [LT ] n= 6 T=  900ms d=(0,0)
        6x(0,0,150)
  t5 S :  963 asst301    [LT ] n= 6 T=  900ms d=(0,0)
        6x(0,0,150)
  t5 mirror ids 5..7: [3282, 3283, 3284]
### carry idle DAT_01002e38
  t0 N :  858 hgst701    [LT ] n= 6 T=  900ms d=(0,0)
        6x(0,0,150)
  t0 NE:  859 hgst801    [OLT] n=12 T= 1800ms d=(0,0)
        12x(0,0,150)
  t0 E :  860 hgst901    [LT ] n=12 T= 1725ms d=(0,0)
        8x(0,0,150) (0,0,75) 3x(0,0,150)
  t0 SE:  861 hgst201    [LT ] n=13 T= 1800ms d=(0,0)
        8x(0,0,150) 2x(0,0,75) 3x(0,0,150)
  t0 S :  857 hgst301    [LT ] n=12 T= 1650ms d=(0,0)
        8x(0,0,150) 2x(0,0,75) 2x(0,0,150)
  t0 mirror ids 5..7: [3456, 3457, 3458]
  t1 N :  800 hbst701    [LT ] n= 8 T=  800ms d=(0,0)
        8x(0,0,100)
  t1 NE:  801 hbst801    [LT ] n= 8 T=  800ms d=(0,0)
        8x(0,0,100)
  t1 E :  802 hbst901    [LT ] n= 8 T=  800ms d=(0,0)
        8x(0,0,100)
  t1 SE:  798 hbst201    [LT ] n= 8 T=  800ms d=(0,0)
        8x(0,0,100)
  t1 S :  799 hbst301    [LT ] n= 8 T=  800ms d=(0,0)
        8x(0,0,100)
  t1 mirror ids 5..7: [3471, 3472, 3473]
  t2 N :  748 hfst701    [LT ] n= 6 T=  600ms d=(0,0)
        6x(0,0,100)
  t2 NE:  749 hfst801    [LT ] n= 6 T=  600ms d=(0,0)
        6x(0,0,100)
  t2 E :  750 hfst901    [LT ] n= 6 T=  600ms d=(0,0)
        6x(0,0,100)
  t2 SE:  746 hfst201    [LT ] n= 6 T=  600ms d=(0,0)
        6x(0,0,100)
  t2 S :  747 hfst301    [LT ] n= 6 T=  600ms d=(0,0)
        6x(0,0,100)
  t2 mirror ids 5..7: [3501, 3502, 3503]
  t3 N : 1124 htst701    [LT ] n=26 T= 2520ms d=(0,0)
        11x(0,0,100) 2x(0,0,80) 11x(0,0,100) 2x(0,0,80)
  t3 NE: 1125 htst801    [LT ] n=26 T= 2520ms d=(0,0)
        11x(0,0,100) 2x(0,0,80) 11x(0,0,100) 2x(0,0,80)
  t3 E : 1126 htst901    [LT ] n=26 T= 2520ms d=(0,0)
        11x(0,0,100) 2x(0,0,80) 11x(0,0,100) 2x(0,0,80)
  t3 SE: 1122 htst201    [LT ] n=26 T= 2520ms d=(0,0)
        11x(0,0,100) 2x(0,0,80) 11x(0,0,100) 2x(0,0,80)
  t3 S : 1123 htst301    [OLT] n=26 T= 2520ms d=(0,0)
        11x(0,0,100) 2x(0,0,80) 11x(0,0,100) 2x(0,0,80)
  t3 mirror ids 5..7: [3531, 3532, 3533]
  t4 N :  930 hcst701    [OLT] n= 4 T=  600ms d=(0,0)
        4x(0,0,150)
  t4 NE:  931 hcst801    [LT ] n= 4 T=  600ms d=(0,0)
        4x(0,0,150)
  t4 E :  932 hcst901    [LT ] n= 4 T=  600ms d=(0,0)
        4x(0,0,150)
  t4 SE:  939 hcst201    [LT ] n= 4 T=  600ms d=(0,0)
        4x(0,0,150)
  t4 S :  929 hcst301    [LT ] n=64 T= 7820ms d=(0,0)
        8x(0,0,125) (0,0,60) (0,0,80) (0,0,200) (0,0,60) (0,0,100) (0,0,120) 8x(0,0,125) (0,0,60) (0,0,80) (0,0,200) (0,0,60) (0,0,100) (0,0,120) 8x(0,0,125) (0,0,60) (0,0,80) (0,0,200) (0,0,60) (0,0,100) (0,0,120) 8x(0,0,125) (0,0,80) 2x(0,0,100) (0,0,80) 3x(0,0,300) (0,0,80) (0,0,60) (0,0,80) (0,0,200) (0,0,60) (0,0,100) (0,0,120)
  t4 mirror ids 5..7: [3486, 3487, 3488]
  t5 N : 1014 hsst701    [LT ] n= 6 T=  900ms d=(0,0)
        6x(0,0,150)
  t5 NE: 1015 hsst801    [LT ] n= 6 T=  900ms d=(0,0)
        6x(0,0,150)
  t5 E : 1016 hsst901    [LT ] n= 6 T=  900ms d=(0,0)
        6x(0,0,150)
  t5 SE: 1012 hsst201    [LT ] n= 6 T=  900ms d=(0,0)
        6x(0,0,150)
  t5 S : 1013 hsst301    [LT ] n= 6 T=  900ms d=(0,0)
        6x(0,0,150)
  t5 mirror ids 5..7: [3516, 3517, 3518]
```

### 7.3 Swimmer tables, 4-direction per-colour tables, per-colour singles, death
assw = swim (walk terrain 2), asdi = dive-in (entering water, 32 px), asgo = climb-out (32 px),
astw301 = idle in water (W+0x47c0, no dir), atcr501 = W+0x47d0 (action 0xd), getpow = W+0x47e0 (action 4),
death1..4 = W+0x4850 (action 0xc). Swim/idle-in-water images are `3snork*.bmp` -> never team-recoloured.
```
### swim 0x1004838 [colour0][dir]
  N :  998 assw701    [LT ] n=12 T=  480ms d=(0,-36)
        12x(0,-3,40)
  NE:  999 assw801    [LT ] n=12 T=  480ms d=(24,-24)
        12x(2,-2,40)
  E : 1000 assw901    [LT ] n=12 T=  480ms d=(36,0)
        12x(3,0,40)
  SE:  996 assw201    [LT ] n=12 T=  480ms d=(24,24)
        12x(2,2,40)
  S :  997 assw301    [LT ] n=12 T=  480ms d=(0,36)
        12x(0,3,40)
  SW: #3312 (virtual id, not in CHD)
  W : #3313 (virtual id, not in CHD)
  NW: #3314 (virtual id, not in CHD)
### dive 0x1004878 [colour0][dir]
  N :  988 asdi701    [LT ] n=16 T= 1040ms d=(0,-32)
        4x(0,0,60) (0,-8,60) (0,-10,60) (0,-10,80) (0,-4,100) (0,0,80) (0,0,60,s80) 6x(0,0,60)
  NE:  989 asdi801    [LT ] n=16 T= 1040ms d=(32,-32)
        4x(0,0,60) (8,-8,60) (10,-10,60) (10,-10,80) (4,-4,100) (0,0,80) (0,0,60,s80) 6x(0,0,60)
  E :  990 asdi901    [LT ] n=16 T= 1040ms d=(32,0)
        4x(0,0,60) (8,0,60) (10,0,60) (10,0,80) (4,0,100) (0,0,80) (0,0,60,s80) 6x(0,0,60)
  SE:  986 asdi201    [LT ] n=16 T= 1040ms d=(32,32)
        4x(0,0,60) (8,8,60) (10,10,60) (10,10,80) (4,4,100) (0,0,80) (0,0,60,s80) 6x(0,0,60)
  S :  987 asdi301    [LT ] n=16 T= 1040ms d=(0,32)
        4x(0,0,60) (0,8,60) (0,10,60) (0,10,80) (0,4,100) (0,0,80) (0,0,60,s80) 6x(0,0,60)
  SW: #3309 (virtual id, not in CHD)
  W : #3310 (virtual id, not in CHD)
  NW: #3311 (virtual id, not in CHD)
### climb 0x10048c0 [colour0][dir]
  N :  993 asgo701    [LT ] n= 7 T=  420ms d=(0,-32)
        4x(0,-4,60) (0,-6,60) (0,-10,60) (0,0,60)
  NE:  994 asgo801    [LT ] n= 7 T=  420ms d=(32,-32)
        4x(4,-4,60) (6,-6,60) (10,-10,60) (0,0,60)
  E :  995 asgo901    [LT ] n= 7 T=  420ms d=(32,0)
        4x(4,0,60) (6,0,60) (10,0,60) (0,0,60)
  SE:  991 asgo201    [LT ] n= 7 T=  420ms d=(32,32)
        4x(4,4,60) (6,6,60) (10,10,60) (0,0,60)
  S :  992 asgo301    [LT ] n= 7 T=  420ms d=(0,32)
        4x(0,4,60) (0,6,60) (0,10,60) (0,0,60)
  SW: #3315 (virtual id, not in CHD)
  W : #3316 (virtual id, not in CHD)
  NW: #3317 (virtual id, not in CHD)
### act0x6_afsf 0x1004638 [colour0][dir]
  N :  704 afsf701    [LT ] n=22 T= 1810ms d=(0,0)
        5x(0,0,100) (0,0,100,s67) (0,0,150) 10x(0,0,60) (0,0,60,s68) 4x(0,0,100)
  NE: ---- (none)
  E :  705 afsf901    [LT ] n=22 T= 1810ms d=(0,0)
        5x(0,0,100) (0,0,100,s67) (0,0,150) 10x(0,0,60) (0,0,60,s68) 4x(0,0,100)
  SE: ---- (none)
  S :  703 afsf301    [LT ] n=22 T= 1760ms d=(0,0)
        5x(0,0,100) (0,0,100,s67) (0,0,100) 10x(0,0,60) (0,0,60,s68) 4x(0,0,100)
  SW: ---- (none)
  W : #3277 (virtual id, not in CHD)
  NW: ---- (none)
### act0x7_afxf 0x1004678 [colour0][dir]
  N :  707 afxf701    [LT ] n=13 T= 1300ms d=(0,0)
        4x(0,0,100) (0,0,100,s69) 8x(0,0,100)
  NE: ---- (none)
  E :  708 afxf901    [LT ] n=13 T= 1300ms d=(0,0)
        4x(0,0,100) (0,0,100,s69) 8x(0,0,100)
  SE: ---- (none)
  S :  706 afxf301    [LT ] n=12 T= 1200ms d=(0,0)
        4x(0,0,100) (0,0,100,s69) 7x(0,0,100)
  SW: ---- (none)
  W : #3280 (virtual id, not in CHD)
  NW: ---- (none)
### act0x10water_asbbw 0x10046b8 [colour0][dir]
  N : 1040 asbbw701   [LT ] n= 8 T=  500ms d=(0,0)
        3x(0,0,60) (0,0,60,s82) 3x(0,0,60) (0,0,80)
  NE: ---- (none)
  E : 1037 asbbw901   [LT ] n= 8 T=  500ms d=(0,0)
        3x(0,0,60) (0,0,60,s82) 3x(0,0,60) (0,0,80)
  SE: ---- (none)
  S : 1041 asbbw301   [LT ] n= 8 T=  500ms d=(0,0)
        3x(0,0,60) (0,0,60,s82) 3x(0,0,60) (0,0,80)
  SW: ---- (none)
  W : #3349 (virtual id, not in CHD)
  NW: ---- (none)
### act0x10land_asbbl 0x1004738 [colour0][dir]
  N : 1043 asbbl701   [LT ] n= 8 T=  480ms d=(0,0)
        4x(0,0,60) (0,0,60,s81) 3x(0,0,60)
  NE: ---- (none)
  E : 1044 asbbl901   [LT ] n= 8 T=  480ms d=(0,0)
        4x(0,0,60) (0,0,60,s81) 3x(0,0,60)
  SE: ---- (none)
  S : 1042 asbbl301   [LT ] n= 8 T=  480ms d=(0,0)
        4x(0,0,60) (0,0,60,s81) 3x(0,0,60)
  SW: ---- (none)
  W : #3355 (virtual id, not in CHD)
  NW: ---- (none)
### act0x11water_asdbw 0x10046f8 [colour0][dir]
  N : 1035 asdbw701   [LT ] n= 8 T=  500ms d=(0,0)
        3x(0,0,60) (0,0,60,s82) 3x(0,0,60) (0,0,80)
  NE: ---- (none)
  E : 1039 asdbw901   [LT ] n= 8 T=  500ms d=(0,0)
        3x(0,0,60) (0,0,60,s82) 3x(0,0,60) (0,0,80)
  SE: ---- (none)
  S : 1036 asdbw301   [LT ] n= 8 T=  500ms d=(0,0)
        3x(0,0,60) (0,0,60,s82) 3x(0,0,60) (0,0,80)
  SW: ---- (none)
  W : #3352 (virtual id, not in CHD)
  NW: ---- (none)
### act0x11land_asdbl 0x1004778 [colour0][dir]
  N : 1033 asdbl701   [LT ] n= 8 T=  480ms d=(0,0)
        4x(0,0,60) (0,0,60,s81) 3x(0,0,60)
  NE: ---- (none)
  E : 1034 asdbl901   [LT ] n= 8 T=  480ms d=(0,0)
        4x(0,0,60) (0,0,60,s81) 3x(0,0,60)
  SE: ---- (none)
  S : 1032 asdbl301   [LT ] n= 8 T=  480ms d=(0,0)
        4x(0,0,60) (0,0,60,s81) 3x(0,0,60)
  SW: ---- (none)
  W : #3358 (virtual id, not in CHD)
  NW: ---- (none)
### act0x8_absb 0x10047b8 [colour0][dir]
  N : 1319 absb701    [LT ] n=17 T= 1360ms d=(0,0)
        3x(0,0,60) (0,0,200) 2x(0,0,60) (0,0,200) (0,0,60) (0,0,120) (0,0,60,s90) 7x(0,0,60)
  NE: ---- (none)
  E : 1318 absb901    [LT ] n=17 T= 1400ms d=(0,0)
        3x(0,0,60) (0,0,200) (0,0,100) (0,0,60) (0,0,200) (0,0,60) (0,0,120) (0,0,60,s90) 7x(0,0,60)
  SE: ---- (none)
  S : 1317 absb301    [LT ] n=17 T= 1360ms d=(0,0)
        3x(0,0,60) (0,0,200) 2x(0,0,60) (0,0,200) (0,0,60) (0,0,120) (0,0,60,s90) 7x(0,0,60)
  SW: ---- (none)
  W : #3190 (virtual id, not in CHD)
  NW: ---- (none)
### act0x9_abdb 0x10047f8 [colour0][dir]
  N :  791 abdb701    [OLT] n=12 T= 1140ms d=(0,0)
        2x(0,0,60) (0,0,100) (0,0,200,s73) 2x(0,0,100) (0,0,100,s74) 3x(0,0,100) 2x(0,0,60)
  NE: ---- (none)
  E :  790 abdb901    [OLT] n=12 T= 1140ms d=(0,0)
        2x(0,0,60) (0,0,100) (0,0,200,s73) 2x(0,0,100) (0,0,100,s74) 3x(0,0,100) 2x(0,0,60)
  SE: ---- (none)
  S :  789 abdb301    [OLT] n=12 T= 1100ms d=(0,0)
        2x(0,0,60) (0,0,100) (0,0,200,s73) 2x(0,0,100) (0,0,100,s74) 2x(0,0,100) 3x(0,0,60)
  SW: ---- (none)
  W : #3193 (virtual id, not in CHD)
  NW: ---- (none)
### singles
  1006 astw301    [LT ] n=85 T= 4365ms d=(0,0)
        72x(0,0,40) (0,0,80) (0,0,100) 9x(0,0,125) (0,0,100) (0,0,80)
  1095 atcr501    [LT ] n=33 T= 3510ms d=(0,0)
        (0,0,200) 2x(0,0,60) (0,0,200) 2x(0,0,60) (0,0,250) 2x(0,0,80) (0,0,90) 2x(0,0,60) (0,0,100) (0,0,60) (0,0,80) (0,0,90) (0,0,100) (0,0,60) (0,0,300) (0,0,60,s84) 3x(0,0,60) (0,0,200) 2x(0,0,60) (0,0,60,s85) (0,0,60) (0,0,80) (0,0,500) (0,0,60) (0,0,60,s86) (0,0,80)
    55 getpow     [OLT] n=11 T=  770ms d=(0,0)
        (0,0,70,s1) 5x(0,0,70) (0,0,70,s2) 4x(0,0,70)
   102 death1     [LT ] n=11 T=  920ms d=(0,0)
        3x(0,0,100) (0,0,200) 7x(0,0,60)
   103 death2     [LT ] n=12 T= 1000ms d=(0,0)
        3x(0,0,100) (0,0,200) (0,0,80) 7x(0,0,60)
   104 death3     [LT ] n=10 T=  980ms d=(0,0)
        3x(0,0,100) (0,0,200) 3x(0,0,60) 3x(0,0,100)
   105 death4     [LT ] n= 7 T=  600ms d=(0,0)
        2x(0,0,60) (0,0,80) 4x(0,0,100)
```

### 7.4 Per-type(-dir) action tables (actions 5, 0x12, 0xe, 0x13/0xa, 2, 0xa-overlay, 0xb, 0x14, 3, 0xf)
Section names: act0x5_gf = W+0x3400, act0x12_at = W+0x3700 (event 4 = hit frame), act0xE_gh = W+0x3a00 (knock-back,
moves 32 px AWAY from facing; event 3), act0x13_gb = W+0x3d00 (blown back 128 px; event 3),
act0x2_h0 = W+0x2140 / a2carry = W+0x33a0 (event 5 frame is re-timed to 200*(10-hp) ms on a private deep copy),
tbl4000_bu = W+0x4000 (overlay object of action 0xa), act0xB_cg = W+0x4060 / carry W+0x40c0 (action 0xb),
act0x14_hatch = W+0x4120 (action 0x14), act0x3_sd = W+0x4180 / carry W+0x41e0 (action 3), act0xF_dr = W+0x47f0 (action 0xf).
```
### act0x5_gf 0x1003f18 [colour0][type][dir]
  t0 N :  853 aggf701    [LT ] n= 7 T=  460ms d=(0,0)
        4x(0,0,60) (0,0,100,s77) 2x(0,0,60)
  t0 NE:  887 aggf801    [LT ] n= 7 T=  420ms d=(0,0)
        2x(0,0,60) (0,0,60,s66) 4x(0,0,60)
  t0 E :  854 aggf901    [LT ] n= 7 T=  420ms d=(0,0)
        4x(0,0,60) (0,0,60,s77) 2x(0,0,60)
  t0 SE:  886 aggf201    [LT ] n= 7 T=  420ms d=(0,0)
        2x(0,0,60) (0,0,60,s66) 4x(0,0,60)
  t0 S :  852 aggf301    [LT ] n= 7 T=  420ms d=(0,0)
        4x(0,0,60) (0,0,60,s77) 2x(0,0,60)
  t0 mirror ids 5..7: [3150, 3151, 3152]
  t1 N :  794 abgf701    [LT ] n= 4 T=  440ms d=(0,0)
        (0,0,110) (0,0,110,s66) 2x(0,0,110)
  t1 NE:  795 abgf801    [LT ] n= 4 T=  440ms d=(0,0)
        (0,0,110) (0,0,110,s66) 2x(0,0,110)
  t1 E :  796 abgf901    [LT ] n= 4 T=  440ms d=(0,0)
        (0,0,110) (0,0,110,s66) 2x(0,0,110)
  t1 SE:  792 abgf201    [LT ] n= 4 T=  440ms d=(0,0)
        (0,0,110) (0,0,110,s66) 2x(0,0,110)
  t1 S :  793 abgf301    [LT ] n= 4 T=  440ms d=(0,0)
        (0,0,110) (0,0,110,s66) 2x(0,0,110)
  t1 mirror ids 5..7: [3177, 3178, 3179]
  t2 N :  700 afgf701    [LT ] n= 4 T=  400ms d=(0,0)
        (0,0,100) (0,0,100,s66) 2x(0,0,100)
  t2 NE:  701 afgf801    [LT ] n= 4 T=  400ms d=(0,0)
        (0,0,100) (0,0,100,s66) 2x(0,0,100)
  t2 E :  702 afgf901    [LT ] n= 4 T=  400ms d=(0,0)
        (0,0,100) (0,0,100,s66) 2x(0,0,100)
  t2 SE:  698 afgf201    [LT ] n= 4 T=  400ms d=(0,0)
        (0,0,100) (0,0,100,s66) 2x(0,0,100)
  t2 S :  699 afgf301    [LT ] n= 4 T=  400ms d=(0,0)
        (0,0,100) (0,0,100,s66) 2x(0,0,100)
  t2 mirror ids 5..7: [3264, 3265, 3266]
  t3 N : 1097 atgf701    [LT ] n= 5 T=  340ms d=(0,0)
        4x(0,0,60) (0,0,100,s77)
  t3 NE: 1171 atgf801    [LT ] n= 5 T=  300ms d=(0,0)
        5x(0,0,60)
  t3 E : 1098 atgf901    [LT ] n= 5 T=  340ms d=(0,0)
        4x(0,0,60) (0,0,100,s77)
  t3 SE: 1172 atgf201    [LT ] n= 5 T=  300ms d=(0,0)
        5x(0,0,60)
  t3 S : 1096 atgf301    [LT ] n= 5 T=  340ms d=(0,0)
        4x(0,0,60) (0,0,100,s77)
  t3 mirror ids 5..7: [3336, 3337, 3338]
  t4 N :  913 acgf701    [LT ] n= 6 T=  360ms d=(0,0)
        (0,0,60,s77) 5x(0,0,60)
  t4 NE:  914 acgf801    [LT ] n= 6 T=  360ms d=(0,0)
        (0,0,60,s77) 5x(0,0,60)
  t4 E :  915 acgf901    [LT ] n= 6 T=  360ms d=(0,0)
        (0,0,60,s77) 5x(0,0,60)
  t4 SE:  911 acgf201    [LT ] n= 6 T=  360ms d=(0,0)
        (0,0,60,s77) 5x(0,0,60)
  t4 S :  912 acgf301    [LT ] n= 6 T=  360ms d=(0,0)
        (0,0,60,s77) 5x(0,0,60)
  t4 mirror ids 5..7: [3210, 3211, 3212]
  t5 N :  983 asgf701    [LT ] n= 4 T=  320ms d=(0,0)
        3x(0,0,80) (0,0,80,s77)
  t5 NE: 1050 asgf801    [LT ] n= 4 T=  400ms d=(0,0)
        2x(0,0,100) (0,0,100,s66) (0,0,100)
  t5 E :  984 asgf901    [LT ] n= 4 T=  320ms d=(0,0)
        3x(0,0,80) (0,0,80,s77)
  t5 SE: 1049 asgf201    [LT ] n= 4 T=  400ms d=(0,0)
        2x(0,0,100) (0,0,100,s66) (0,0,100)
  t5 S :  982 asgf301    [LT ] n= 4 T=  320ms d=(0,0)
        3x(0,0,80) (0,0,80,s77)
  t5 mirror ids 5..7: [3297, 3298, 3299]
### act0x12_at 0x1004098 [colour0][type][dir]
  t0 N :  822 agat701    [LT ] n= 6 T=  360ms d=(0,0)
        (0,0,40) (0,0,40,s75) (0,0,40) (0,0,90,e4) (0,0,60) (0,0,90)
  t0 NE:  823 agat801    [LT ] n= 6 T=  420ms d=(0,0)
        (0,0,60) (0,0,60,s75) (0,0,60) (0,0,90,e4) (0,0,60) (0,0,90)
  t0 E :  824 agat901    [LT ] n= 6 T=  420ms d=(0,0)
        (0,0,60) (0,0,60,s75) (0,0,60) (0,0,90,e4) (0,0,60) (0,0,90)
  t0 SE:  825 agat201    [LT ] n= 6 T=  420ms d=(0,0)
        (0,0,60) (0,0,60,s75) (0,0,60) (0,0,90,e4) (0,0,60) (0,0,90)
  t0 S :  821 agat301    [LT ] n= 6 T=  420ms d=(0,0)
        (0,0,60) (0,0,60,s75) (0,0,60) (0,0,90,e4) (0,0,60) (0,0,90)
  t0 mirror ids 5..7: [3153, 3154, 3155]
  t1 N :  764 abat701    [LT ] n= 6 T=  360ms d=(0,0)
        3x(0,0,60) (0,0,60,e4,s57) 2x(0,0,60)
  t1 NE:  765 abat801    [LT ] n= 6 T=  360ms d=(0,0)
        3x(0,0,60) (0,0,60,e4,s57) 2x(0,0,60)
  t1 E :  766 abat901    [LT ] n= 6 T=  360ms d=(0,0)
        3x(0,0,60) (0,0,60,e4,s57) 2x(0,0,60)
  t1 SE:  762 abat201    [LT ] n= 6 T=  360ms d=(0,0)
        3x(0,0,60) (0,0,60,e4,s57) 2x(0,0,60)
  t1 S :  763 abat301    [LT ] n= 6 T=  360ms d=(0,0)
        3x(0,0,60) (0,0,60,e4,s57) 2x(0,0,60)
  t1 mirror ids 5..7: [3180, 3181, 3182]
  t2 N :  684 afat701    [LT ] n= 6 T=  360ms d=(0,0)
        4x(0,0,60) (0,0,60,e4,s57) (0,0,60)
  t2 NE:  685 afat801    [LT ] n= 6 T=  360ms d=(0,0)
        4x(0,0,60) (0,0,60,e4,s57) (0,0,60)
  t2 E :  686 afat901    [LT ] n= 6 T=  360ms d=(0,0)
        4x(0,0,60) (0,0,60,e4,s57) (0,0,60)
  t2 SE:  682 afat201    [LT ] n= 6 T=  360ms d=(0,0)
        4x(0,0,60) (0,0,60,e4,s57) (0,0,60)
  t2 S :  683 afat301    [LT ] n= 6 T=  360ms d=(0,0)
        4x(0,0,60) (0,0,60,e4,s57) (0,0,60)
  t2 mirror ids 5..7: [3267, 3268, 3269]
  t3 N : 1061 atat701    [LT ] n=12 T=  720ms d=(0,0)
        (0,0,60) (0,0,40) (0,0,80) (0,0,60) (0,0,40,s83) (0,0,40) (0,0,80,e4) (0,0,80) (0,0,60) (0,0,80) (0,0,60) (0,0,40)
  t3 NE: 1062 atat801    [LT ] n=12 T=  720ms d=(0,0)
        (0,0,60) (0,0,40) (0,0,80) (0,0,60) (0,0,40,s83) (0,0,40) (0,0,80,e4) (0,0,80) (0,0,60) (0,0,80) (0,0,60) (0,0,40)
  t3 E : 1063 atat901    [LT ] n=12 T=  720ms d=(0,0)
        (0,0,60) (0,0,40) (0,0,80) (0,0,60) (0,0,40,s83) (0,0,40) (0,0,80,e4) (0,0,80) (0,0,60) (0,0,80) (0,0,60) (0,0,40)
  t3 SE: 1059 atat201    [LT ] n=12 T=  720ms d=(0,0)
        (0,0,60) (0,0,40) (0,0,80) (0,0,40) (0,0,40,s83) (0,0,40) (0,0,80,e4) (0,0,80) (0,0,60) (0,0,80) 2x(0,0,60)
  t3 S : 1060 atat301    [LT ] n=12 T=  720ms d=(0,0)
        (0,0,60) (0,0,40) (0,0,80) (0,0,60) (0,0,40,s83) (0,0,40) (0,0,80,e4) (0,0,80) (0,0,60) (0,0,80) (0,0,60) (0,0,40)
  t3 mirror ids 5..7: [3339, 3340, 3341]
  t4 N :  903 acat701    [LT ] n= 6 T=  540ms d=(0,0)
        (0,0,80) (0,0,100) (0,0,60,e4,s78) (0,0,80) (0,0,100) (0,0,120)
  t4 NE:  904 acat801    [LT ] n= 6 T=  540ms d=(0,0)
        (0,0,80) (0,0,100) (0,0,60,e4,s78) (0,0,80) (0,0,100) (0,0,120)
  t4 E :  905 acat901    [LT ] n= 6 T=  540ms d=(0,0)
        (0,0,80) (0,0,100) (0,0,60,e4,s78) (0,0,80) (0,0,100) (0,0,120)
  t4 SE:  901 acat201    [LT ] n= 6 T=  540ms d=(0,0)
        (0,0,80) (0,0,100) (0,0,60,e4,s78) (0,0,80) (0,0,100) (0,0,120)
  t4 S :  902 acat301    [LT ] n= 6 T=  540ms d=(0,0)
        (0,0,80) (0,0,100) (0,0,60,e4,s78) (0,0,80) (0,0,100) (0,0,120)
  t4 mirror ids 5..7: [3213, 3214, 3215]
  t5 N :  979 asat701    [LT ] n=10 T=  760ms d=(0,0)
        3x(0,0,60) 2x(0,0,80) (0,0,80,s79) (0,0,100) (0,0,100,e4) (0,0,80) (0,0,60)
  t5 NE:  980 asat801    [LT ] n=10 T=  760ms d=(0,0)
        3x(0,0,60) 2x(0,0,80) (0,0,80,s79) (0,0,100) (0,0,100,e4) (0,0,80) (0,0,60)
  t5 E :  981 asat901    [LT ] n=10 T=  760ms d=(0,0)
        3x(0,0,60) 2x(0,0,80) (0,0,80,s79) (0,0,100) (0,0,100,e4) (0,0,80) (0,0,60)
  t5 SE:  977 asat201    [LT ] n=10 T=  760ms d=(0,0)
        4x(0,0,60) (0,0,80) (0,0,80,s79) (0,0,80) (0,0,100) (0,0,100,e4) (0,0,80)
  t5 S :  978 asat301    [LT ] n=10 T=  760ms d=(0,0)
        3x(0,0,60) 2x(0,0,80) (0,0,80,s79) (0,0,100) (0,0,100,e4) (0,0,80) (0,0,60)
  t5 mirror ids 5..7: [3300, 3301, 3302]
### act0xE_gh 0x1004218 [colour0][type][dir]
  t0 N :  828 aggh701    [LT ] n= 9 T=  790ms d=(0,32)
        (0,24,100,s64) (0,8,100) (0,0,120) (0,0,80,e3,s65) 4x(0,0,60) (0,0,150)
  t0 NE:  829 aggh801    [LT ] n= 9 T=  760ms d=(-32,32)
        (-24,24,100,s64) (-8,8,100) (0,0,120) (0,0,60,e3,s65) 3x(0,0,60) (0,0,50) (0,0,150)
  t0 E :  830 aggh901    [LT ] n= 9 T=  790ms d=(-32,0)
        (-24,0,100,s64) (-8,0,100) (0,0,120) (0,0,80,e3,s65) 4x(0,0,60) (0,0,150)
  t0 SE:  831 aggh201    [LT ] n= 9 T=  790ms d=(-32,-32)
        (-24,-24,100,s64) (-8,-8,100) (0,0,120) (0,0,80,e3,s65) 4x(0,0,60) (0,0,150)
  t0 S :  827 aggh301    [LT ] n= 9 T=  800ms d=(0,-32)
        (0,-24,100,s64) (0,-8,100) (0,0,120) (0,0,80,e3,s65) (0,0,70) 3x(0,0,60) (0,0,150)
  t0 mirror ids 5..7: [3156, 3157, 3158]
  t1 N :  780 abgh701    [LT ] n= 9 T= 1000ms d=(0,32)
        (0,16,100,s64) (0,10,100) (0,6,100) (0,0,100,e3,s65) 5x(0,0,120)
  t1 NE:  781 abgh801    [LT ] n= 9 T= 1000ms d=(-32,32)
        (-16,16,100,s64) (-10,10,100) (-6,6,100) (0,0,100,e3,s65) 5x(0,0,120)
  t1 E :  782 abgh901    [LT ] n= 9 T= 1000ms d=(-32,0)
        (-16,0,100,s64) (-10,0,100) (-6,0,100) (0,0,100,e3,s65) 5x(0,0,120)
  t1 SE:  779 abgh201    [LT ] n= 9 T= 1000ms d=(-32,-32)
        (-16,-16,100,s64) (-10,-10,100) (-6,-6,100) (0,0,100,e3,s65) 5x(0,0,120)
  t1 S :  778 abgh301    [LT ] n= 9 T= 1000ms d=(0,-32)
        (0,-16,100,s64) (0,-10,100) (0,-6,100) (0,0,100,e3,s65) 5x(0,0,120)
  t1 mirror ids 5..7: [3183, 3184, 3185]
  t2 N :  690 afgh701    [LT ] n=10 T= 1000ms d=(0,32)
        (0,20,100,s64) (0,8,100) (0,4,100) (0,0,100,e3,s65) 6x(0,0,100)
  t2 NE:  691 afgh801    [LT ] n=10 T= 1000ms d=(-32,32)
        (-20,20,100,s64) (-8,8,100) (-4,4,100) (0,0,100,e3,s65) 6x(0,0,100)
  t2 E :  692 afgh901    [LT ] n=10 T= 1000ms d=(-32,0)
        (-20,0,100,s64) (-8,0,100) (-4,0,100) (0,0,100,e3,s65) 6x(0,0,100)
  t2 SE:  688 afgh201    [LT ] n=10 T= 1000ms d=(-32,-32)
        (-20,-20,100,s64) (-8,-8,100) (-4,-4,100) (0,0,100,e3,s65) 6x(0,0,100)
  t2 S :  689 afgh301    [LT ] n=10 T= 1000ms d=(0,-32)
        (0,-20,100,s64) (0,-8,100) (0,-4,100) (0,0,100,e3,s65) 6x(0,0,100)
  t2 mirror ids 5..7: [3270, 3271, 3272]
  t3 N : 1066 atgh701    [LT ] n= 8 T=  780ms d=(0,32)
        (0,32,100,s64) 2x(0,0,100) (0,0,100,e3,s65) (0,0,100) (0,0,80) 2x(0,0,100)
  t3 NE: 1067 atgh801    [LT ] n= 9 T=  950ms d=(-32,32)
        (-32,32,100,s64) 2x(0,0,100) (0,0,100,e3,s65) 4x(0,0,100) (0,0,150)
  t3 E : 1068 atgh901    [LT ] n= 9 T=  930ms d=(-32,0)
        (-32,0,100,s64) 2x(0,0,100) (0,0,100,e3,s65) 2x(0,0,100) (0,0,80) (0,0,100) (0,0,150)
  t3 SE: 1064 atgh201    [LT ] n= 9 T=  880ms d=(-32,-32)
        (-32,-32,100,s64) 2x(0,0,100) (0,0,100,e3,s65) 3x(0,0,100) (0,0,80) (0,0,100)
  t3 S : 1065 atgh301    [LT ] n= 9 T=  950ms d=(0,-32)
        (0,-32,100,s64) 2x(0,0,100) (0,0,100,e3,s65) 4x(0,0,100) (0,0,150)
  t3 mirror ids 5..7: [3342, 3343, 3344]
  t4 N :  908 acgh701    [LT ] n= 9 T=  880ms d=(0,32)
        (0,16,100,s64) (0,10,100) (0,6,100) (0,0,100,e3,s65) (0,0,80) (0,0,100) 2x(0,0,120) (0,0,60)
  t4 NE:  909 acgh801    [LT ] n= 9 T=  880ms d=(-32,32)
        (-16,16,100,s64) (-10,10,100) (-6,6,100) (0,0,100,e3,s65) (0,0,80) (0,0,100) 2x(0,0,120) (0,0,60)
  t4 E :  910 acgh901    [LT ] n= 9 T=  880ms d=(-32,0)
        (-16,0,100,s64) (-10,0,100) (-6,0,100) (0,0,100,e3,s65) (0,0,80) (0,0,100) 2x(0,0,120) (0,0,60)
  t4 SE:  906 acgh201    [LT ] n= 9 T=  880ms d=(-32,-32)
        (-16,-16,100,s64) (-10,-10,100) (-6,-6,100) (0,0,100,e3,s65) (0,0,80) (0,0,100) 2x(0,0,120) (0,0,60)
  t4 S :  907 acgh301    [LT ] n= 9 T=  880ms d=(0,-32)
        (0,-16,100,s64) (0,-10,100) (0,-6,100) (0,0,100,e3,s65) (0,0,80) (0,0,100) 2x(0,0,120) (0,0,60)
  t4 mirror ids 5..7: [3216, 3217, 3218]
  t5 N :  974 asgh701    [LT ] n= 9 T= 1050ms d=(0,32)
        (0,15,100,s64) (0,12,100) (0,5,100) (0,0,100,e3,s65) 3x(0,0,150) 2x(0,0,100)
  t5 NE:  975 asgh801    [LT ] n= 9 T= 1050ms d=(-32,32)
        (-15,15,100,s64) (-12,12,100) (-5,5,100) (0,0,100,e3,s65) 3x(0,0,150) 2x(0,0,100)
  t5 E :  976 asgh901    [LT ] n= 9 T= 1050ms d=(-32,0)
        (-15,0,100,s64) (-12,0,100) (-5,0,100) (0,0,100,e3,s65) 3x(0,0,150) 2x(0,0,100)
  t5 SE:  967 asgh201    [LT ] n= 9 T= 1050ms d=(-32,-32)
        (-15,-15,100,s64) (-12,-12,100) (-5,-5,100,s65) (0,0,100,e3) 3x(0,0,150) 2x(0,0,100)
  t5 S :  968 asgh301    [LT ] n= 9 T= 1050ms d=(0,-32)
        (0,-15,100,s64) (0,-12,100) (0,-5,100,s65) (0,0,100,e3) 3x(0,0,150) 2x(0,0,100)
  t5 mirror ids 5..7: [3303, 3304, 3305]
### act0x13_gb 0x1004398 [colour0][type][dir]
  t0 N :  848 aggb701    [LT ] n=12 T=  890ms d=(0,128)
        (0,128,60,s64) 4x(0,0,60) (0,0,120) (0,0,80,e3,s65) 4x(0,0,60) (0,0,150)
  t0 NE:  849 aggb801    [LT ] n=12 T=  860ms d=(-128,128)
        (-128,128,60,s64) 4x(0,0,60) (0,0,120) (0,0,60,e3,s65) 3x(0,0,60) (0,0,50) (0,0,150)
  t0 E :  850 aggb901    [LT ] n=12 T=  870ms d=(-128,0)
        (-128,0,100,s64) 5x(0,0,60) (0,0,80,e3,s65) 4x(0,0,60) (0,0,150)
  t0 SE:  851 aggb201    [LT ] n=12 T=  930ms d=(-128,-128)
        (-128,-128,100,s64) 4x(0,0,60) (0,0,120) (0,0,80,e3,s76) 4x(0,0,60) (0,0,150)
  t0 S :  847 aggb301    [LT ] n=12 T=  900ms d=(0,-128)
        (0,-128,60,s64) 4x(0,0,60) (0,0,120) (0,0,80,e3,s65) (0,0,70) 3x(0,0,60) (0,0,150)
  t0 mirror ids 5..7: [3159, 3160, 3161]
  t1 N :  785 abgb701    [LT ] n=12 T= 1100ms d=(0,128)
        (0,128,60,s64) 4x(0,0,60) (0,0,100) (0,0,100,e3,s65) 5x(0,0,120)
  t1 NE:  786 abgb801    [LT ] n=12 T= 1060ms d=(-128,128)
        (-128,128,60,s64) 5x(0,0,60) (0,0,100,e3,s65) 5x(0,0,120)
  t1 E :  787 abgb901    [LT ] n=12 T= 1060ms d=(-128,0)
        (-128,0,60,s64) 5x(0,0,60) (0,0,100,e3,s65) 5x(0,0,120)
  t1 SE:  784 abgb201    [LT ] n=12 T= 1060ms d=(-128,-128)
        (-128,-128,60,s64) 5x(0,0,60) (0,0,100,e3,s65) 5x(0,0,120)
  t1 S :  783 abgb301    [LT ] n=12 T= 1060ms d=(0,-128)
        (0,-128,60,s64) 5x(0,0,60) (0,0,100,e3,s65) 5x(0,0,120)
  t1 mirror ids 5..7: [3186, 3187, 3188]
  t2 N :  695 afgb701    [LT ] n=13 T= 1020ms d=(0,128)
        (0,0,60,s64) (0,128,60) 4x(0,0,60) (0,0,60,e3,s65) 6x(0,0,100)
  t2 NE:  696 afgb801    [LT ] n=13 T=  980ms d=(-128,128)
        (0,0,60,s64) (-128,128,60) 4x(0,0,60) (0,0,60,e3,s65) (0,0,60) 5x(0,0,100)
  t2 E :  697 afgb901    [LT ] n=13 T= 1060ms d=(-128,0)
        (0,0,60,s64) (-128,0,60) 4x(0,0,60) (0,0,100,e3,s65) 6x(0,0,100)
  t2 SE:  693 afgb201    [LT ] n=13 T= 1100ms d=(-128,-128)
        (0,0,100,s64) (-128,-128,60) 4x(0,0,60) (0,0,100,e3,s65) 6x(0,0,100)
  t2 S :  694 afgb301    [LT ] n=13 T=  980ms d=(0,-128)
        (0,0,60,s64) (0,-128,60) 4x(0,0,60) (0,0,60,e3,s65) (0,0,60) 5x(0,0,100)
  t2 mirror ids 5..7: [3273, 3274, 3275]
  t3 N : 1090 atgb701    [LT ] n=11 T=  910ms d=(0,128)
        (0,128,60,s64) 4x(0,0,60) (0,0,100) (0,0,100,e3,s65) 2x(0,0,100) (0,0,150) (0,0,60)
  t3 NE: 1091 atgb801    [LT ] n=12 T= 1000ms d=(-128,128)
        (-128,128,60,s64) 4x(0,0,60) (0,0,100) (0,0,100,e3,s65) 5x(0,0,100)
  t3 E : 1092 atgb901    [LT ] n=12 T=  920ms d=(-128,0)
        (-128,0,60,s64) 5x(0,0,60) (0,0,100,e3,s65) 4x(0,0,100) (0,0,60)
  t3 SE: 1093 atgb201    [LT ] n=12 T= 1000ms d=(-128,-128)
        (-128,-128,100,s64) 5x(0,0,60) (0,0,100,e3,s65) 5x(0,0,100)
  t3 S : 1094 atgb301    [LT ] n=12 T=  960ms d=(0,-128)
        (0,-128,60,s64) 4x(0,0,60) (0,0,100) (0,0,100,e3,s65) 4x(0,0,100) (0,0,60)
  t3 mirror ids 5..7: [3345, 3346, 3347]
  t4 N :  921 acgb701    [LT ] n=12 T=  980ms d=(0,128)
        (0,128,60,s64) 4x(0,0,60) 2x(0,0,100) (0,0,80,e3,s65) (0,0,100) 2x(0,0,120) (0,0,60)
  t4 NE:  922 acgb801    [LT ] n=12 T=  980ms d=(-128,128)
        (-128,128,60,s64) 4x(0,0,60) 2x(0,0,100) (0,0,80,e3,s65) (0,0,100) 2x(0,0,120) (0,0,60)
  t4 E :  923 acgb901    [LT ] n=12 T=  980ms d=(-128,0)
        (-128,0,60,s64) 4x(0,0,60) 2x(0,0,100) (0,0,80,e3,s65) (0,0,100) 2x(0,0,120) (0,0,60)
  t4 SE:  919 acgb201    [LT ] n=12 T=  980ms d=(-128,-128)
        (-128,-128,60,s64) 4x(0,0,60) 2x(0,0,100) (0,0,80,e3,s65) (0,0,100) 2x(0,0,120) (0,0,60)
  t4 S :  920 acgb301    [LT ] n=12 T=  980ms d=(0,-128)
        (0,-128,60,s64) 4x(0,0,60) 2x(0,0,100) (0,0,80,e3,s65) (0,0,100) 2x(0,0,120) (0,0,60)
  t4 mirror ids 5..7: [3219, 3220, 3221]
  t5 N :  969 asgb701    [LT ] n=12 T= 1150ms d=(0,128)
        (0,128,60,s64) 4x(0,0,60) (0,0,100) (0,0,100,e3,s65) 3x(0,0,150) 2x(0,0,100)
  t5 NE:  970 asgb801    [LT ] n=12 T= 1110ms d=(-128,128)
        (-128,128,60,s64) 5x(0,0,60) (0,0,100,e3,s65) 3x(0,0,150) 2x(0,0,100)
  t5 E :  971 asgb901    [LT ] n=12 T= 1150ms d=(-128,0)
        (-128,0,60,s64) 4x(0,0,60) (0,0,100) (0,0,100,e3,s65) 3x(0,0,150) 2x(0,0,100)
  t5 SE:  972 asgb201    [LT ] n=12 T= 1110ms d=(-128,-128)
        (-128,-128,60,s64) 5x(0,0,60) (0,0,100,e3,s65) 3x(0,0,150) 2x(0,0,100)
  t5 S :  973 asgb301    [LT ] n=12 T= 1150ms d=(0,-128)
        (0,-128,60,s64) 4x(0,0,60) (0,0,100) (0,0,100,e3,s65) 3x(0,0,150) 2x(0,0,100)
  t5 mirror ids 5..7: [3306, 3307, 3308]
### act0x2_h0 0x1003eb8 [colour0][type]
  t0:  856 agh0       [LT ] n=17 T= 1000ms d=(0,0)
        8x(0,0,60) (0,0,40,e5) 8x(0,0,60)
  t1:  806 abh0       [LT ] n=13 T= 1240ms d=(0,0)
        6x(0,0,100) (0,0,40,e5) 6x(0,0,100)
  t2:  680 afh0       [LT ] n= 9 T= 1240ms d=(0,0)
        2x(0,0,100) (0,0,500) 2x(0,0,100) (0,0,40,e5) 3x(0,0,100)
  t3: 1100 ath0       [LT ] n=17 T= 1000ms d=(0,0)
        8x(0,0,60) (0,0,40,e5) 8x(0,0,60)
  t4:  935 ach0       [LT ] n=15 T= 1160ms d=(0,0)
        7x(0,0,80) (0,0,40,e5) 7x(0,0,80)
  t5: 1045 ash0       [LT ] n=15 T=  880ms d=(0,0)
        7x(0,0,60) (0,0,40,e5) 7x(0,0,60)
### act0x2carry_h0 0x1003ee8 [colour0][type]
  t0:  883 hgh0       [LT ] n=17 T= 1000ms d=(0,0)
        8x(0,0,60) (0,0,40,e5) 8x(0,0,60)
  t1:  807 hbh0       [LT ] n=13 T= 1240ms d=(0,0)
        6x(0,0,100) (0,0,40,e5) 6x(0,0,100)
  t2:  751 hfh0       [LT ] n= 9 T= 1240ms d=(0,0)
        2x(0,0,100) (0,0,500) 2x(0,0,100) (0,0,40,e5) 3x(0,0,100)
  t3: 1127 hth0       [LT ] n=17 T= 1000ms d=(0,0)
        8x(0,0,60) (0,0,40,e5) 8x(0,0,60)
  t4:  934 hch0       [LT ] n=15 T= 1160ms d=(0,0)
        7x(0,0,80) (0,0,40,e5) 7x(0,0,80)
  t5: 1047 hsh0       [LT ] n=15 T=  880ms d=(0,0)
        7x(0,0,60) (0,0,40,e5) 7x(0,0,60)
### tbl4000_bu 0x1004518 [colour0][type]
  t0:  885 agbu301    [LT ] n=11 T= 1150ms d=(0,0)
        (0,0,100,s64) (0,0,100) (0,0,150) 2x(0,0,100) (0,0,100,e3,s65) 5x(0,0,100)
  t1:  788 abbu301    [LT ] n=16 T= 1610ms d=(0,0)
        (0,0,60,s64) (0,0,60) (0,0,80) (0,0,120) (0,0,150) (0,0,120) (0,0,80) 2x(0,0,60) (0,0,100) (0,0,120,e3,s65) 5x(0,0,120)
  t2:  710 afbu301    [LT ] n=13 T= 1330ms d=(0,0)
        (0,0,100,s64) (0,0,150) (0,0,200) (0,0,100) 2x(0,0,60) (0,0,60,s65) 6x(0,0,100)
  t3: 1129 atbu301    [LT ] n=11 T= 1150ms d=(0,0)
        (0,0,100,s64) (0,0,100) (0,0,150) (0,0,100) (0,0,100,e3,s65) 6x(0,0,100)
  t4:  936 acbu301    [LT ] n=10 T= 1165ms d=(0,0)
        (0,0,60,s64) (0,0,80) (0,0,120) (0,0,180) (0,0,120) (0,0,100) (0,0,200,e3,s65) (0,0,120) (0,0,125) (0,0,60)
  t5:  985 asbu301    [LT ] n=14 T= 1050ms d=(0,0)
        (0,0,60,s64) (0,0,80) (0,0,100) (0,0,150) (0,0,100) (0,0,80) (0,0,60,e3,s65) 7x(0,0,60)
### act0xB_cg 0x1004548 [colour0][type]
  t0:  884 agcg301    [LT ] n= 6 T=  360ms d=(0,0)
        (0,0,60,s63) 5x(0,0,60)
  t1:  767 abcg301    [LT ] n=15 T= 1380ms d=(0,0)
        (0,0,60,s63) (0,0,60) 8x(0,0,120) 5x(0,0,60)
  t2:  687 afcg301    [LT ] n= 9 T=  660ms d=(0,0)
        (0,0,80,s63) (0,0,120) 5x(0,0,60) (0,0,100) (0,0,60)
  t3: 1128 atcg301    [LT ] n=11 T=  890ms d=(0,0)
        (0,0,70,s63) 2x(0,0,100) 2x(0,0,50) (0,0,40) (0,0,100) (0,0,150) (0,0,70) 2x(0,0,80)
  t4:  916 accg301    [LT ] n= 6 T=  620ms d=(0,0)
        (0,0,80,s63) (0,0,80) 2x(0,0,150) 2x(0,0,80)
  t5: 1048 ascg301    [LT ] n=12 T=  960ms d=(0,0)
        (0,0,80,s63) 11x(0,0,80)
### act0xBcarry_cg 0x1004578 [colour0][type]
  t0:  888 hgcg301    [LT ] n= 6 T=  360ms d=(0,0)
        (0,0,60,s63) 5x(0,0,60)
  t1:  808 hbcg301    [LT ] n=15 T= 1380ms d=(0,0)
        (0,0,60,s63) (0,0,60) 8x(0,0,120) 5x(0,0,60)
  t2:  753 hfcg301    [LT ] n= 9 T=  660ms d=(0,0)
        (0,0,80,s63) (0,0,120) 5x(0,0,60) (0,0,100) (0,0,60)
  t3: 1132 htcg301    [LT ] n=11 T=  890ms d=(0,0)
        (0,0,70,s63) 2x(0,0,100) 2x(0,0,50) (0,0,40) (0,0,100) (0,0,150) (0,0,70) 2x(0,0,80)
  t4:  933 hccg301    [LT ] n= 6 T=  620ms d=(0,0)
        (0,0,80,s63) (0,0,80) 2x(0,0,150) 2x(0,0,80)
  t5: 1051 hscg301    [LT ] n=12 T=  960ms d=(0,0)
        (0,0,80,s63) 11x(0,0,80)
### act0x14_hatch 0x10045a8 [colour0][type]
  t0:  890 aghatch    [LT ] n= 9 T=  520ms d=(0,0)
        (0,0,40) 8x(0,0,60)
  t1:  810 abhatch    [LT ] n= 7 T=  640ms d=(0,0)
        (0,0,40) 6x(0,0,100)
  t2:  756 afhatch    [LT ] n= 3 T=  300ms d=(0,0)
        3x(0,0,100)
  t3: 1133 athatch    [LT ] n= 9 T=  520ms d=(0,0)
        (0,0,40) 8x(0,0,60)
  t4:  940 achatch    [LT ] n= 8 T=  640ms d=(0,0)
        8x(0,0,80)
  t5: 1053 ashatch    [LT ] n= 8 T=  480ms d=(0,0)
        8x(0,0,60)
### act0x3_sd 0x10045d8 [colour0][type]
  t0:  855 agsd301    [LT ] n=25 T= 3125ms d=(0,0)
        (0,0,125,s70) 24x(0,0,125)
  t1:  805 absd301    [LT ] n=28 T= 3835ms d=(0,0)
        (0,0,105,s70) 26x(0,0,105) (0,0,1000)
  t2:  709 afsd301    [LT ] n=30 T= 3000ms d=(0,0)
        (0,0,100,s70) 29x(0,0,100)
  t3: 1099 atsd301    [LT ] n=29 T= 2610ms d=(0,0)
        (0,0,10,s70) 7x(0,0,100) (0,0,80) 4x(0,0,60) (0,0,80) 15x(0,0,100)
  t4:  918 acsd301    [LT ] n=30 T= 3000ms d=(0,0)
        (0,0,100,s70) 29x(0,0,100)
  t5: 1001 assd301    [LT ] n=30 T= 2880ms d=(0,0)
        (0,0,100,s70) 17x(0,0,100) (0,0,80) 2x(0,0,60) (0,0,80) 8x(0,0,100)
### act0x3carry_sd 0x1004608 [colour0][type]
  t0:  889 hgsd301    [LT ] n=25 T= 3125ms d=(0,0)
        (0,0,125,s70) 24x(0,0,125)
  t1:  809 hbsd301    [LT ] n=28 T= 3835ms d=(0,0)
        (0,0,105,s70) 26x(0,0,105) (0,0,1000)
  t2:  754 hfsd301    [LT ] n=30 T= 3000ms d=(0,0)
        (0,0,100,s70) 29x(0,0,100)
  t3: 1131 htsd301    [LT ] n=29 T= 2610ms d=(0,0)
        (0,0,10,s70) 7x(0,0,100) (0,0,80) 4x(0,0,60) (0,0,80) 15x(0,0,100)
  t4:  938 hcsd301    [LT ] n=30 T= 3000ms d=(0,0)
        (0,0,100,s70) 29x(0,0,100)
  t5: 1052 hssd301    [LT ] n=30 T= 2880ms d=(0,0)
        (0,0,100,s70) 17x(0,0,100) (0,0,80) 2x(0,0,60) (0,0,80) 8x(0,0,100)
### act0xF_dr 0x1004910 [colour0][type]
  t0: 1134 agdr301    [OLT] n=22 T= 2370ms d=(0,0)
        (0,0,100,s71) (0,0,90,s72) (0,0,80) 3x(0,0,60) 16x(0,0,120)
  t1:  804 abdr301    [OLT] n=22 T= 2380ms d=(0,0)
        (0,0,100,s71) (0,0,100,s72) (0,0,80) 3x(0,0,60) 16x(0,0,120)
  t2:  755 afdr301    [OLT] n=22 T= 2370ms d=(0,0)
        (0,0,100,s71) (0,0,90,s72) (0,0,80) 3x(0,0,60) 16x(0,0,120)
  t3: 1130 atdr301    [OLT] n=22 T= 2370ms d=(0,0)
        (0,0,100,s71) (0,0,90,s72) (0,0,80) 3x(0,0,60) 16x(0,0,120)
  t4:  941 acdr301    [OLT] n=22 T= 2370ms d=(0,0)
        (0,0,100,s71) (0,0,90,s72) (0,0,80) 3x(0,0,60) 16x(0,0,120)
  t5: 1134 agdr301    [OLT] n=22 T= 2370ms d=(0,0)
        (0,0,100,s71) (0,0,90,s72) (0,0,80) 3x(0,0,60) 16x(0,0,120)
```

---------------------------------------------------------------------------------------------------
## 8. Quirks / corrections to prior claims

- C7 correction: the block index in every lookup is `ant+0xd4` (colour), not `ant+0x56` (team);
  colour = {3,2,1,0}[team]. `W.d[0x490 + d + 8*(tr + 5*(t + 6*colour))]`.
- C7 detail: `supplied` flag = terrA != -1. With terrA == -1 (ANTPAUSE resume, FUN_01024d2b) the walk
  animation ALWAYS restarts, using the terrain of the pixel-derived own tile.
- C7 detail: dive/climb test uses terrA (tile being left, at the tile-centre call with flag=1) vs terrB (next
  tile). After starting dive/climb `+0x88 = 1`; the next walk SetAction with same action consumes it
  (`+0x88 = 0`, forced restart).
- Idle (action 0) ALWAYS replays (no restart suppression) and clears `+0x88`. The water-idle variant is chosen
  purely by `terrA == 2` and has no directional variants.
- C8 correction: static walk tables are `[4 colours][6][5][8]` (960 words each: 0x1002fb8..0x1003738 and
  0x1003738..0x1003eb8). Rows 5..7 of colour 0 hold mirror ids; colours 1..3 hold 8 real per-colour ids.
- 4-direction tables (actions 6,7,8,9,0x10,0x11): only N/E/S/W exist (+W by mirroring E); NE/SE/SW/NW are NULL.
- Data bug: DAT_01004778 row for colour 1 (raw bytes at 0x1004788: `38 0d fe 7f 3a 0d fe 7f fe 7f fe 7f 00 00 00 00`)
  = {3384, 7ffe, 3386, 7ffe, 7ffe, 7ffe, 0, 0}: colour 1 (team 2) action 0x11 on land facing S -> NULL
  entry (FUN_010192fd returns 0 for id 0x7ffe); facing W gets a copy with id 0.
- DAT_01004910 type 5 = CHD 1134 `agdr301` (worker's), not a swimmer-specific anim.
- Action 2 stretches evt-5 frames on a deep copy: `duration = (uint16)((10 - hp) * 200)`; with hp==10 no change.
- Frame box (+0x12..+0x18) is passed to the step callback as evt[2..3] and is NOT mirrored; in all ant
  animations it is the constant (5,5,20,20).
- Walk case with terrain class > 4 would use the dir value as a Sprite pointer (0x101b10a) — unreachable.
- Ant vtable+0x28 is FUN_0101a93a (not 0x102b7bb): sets +0x38/+0x3a and bit6, recomputes tile by
  `x/32, y/32` (idiv), calls FUN_0100f17f(world, this, &tile), and calls FUN_01006af4(map, tile, 6, W+0x4a58)
  (0x101a9d9..0x101a9f8) when (W+0x4ae4==0 && ant.team==W+0xf2a) || W->players[W+0xf2a]->+0x68 == ant.team
  (outside this cluster; looks like fog reveal radius 6). 0x102b7bb is the point-object overload (vtable+0x18 style)
  that forwards to vtable+0x28.

## 9. Evidence index
- Mirror: 0x1018a7a..0x1018abe, 0x1018b9f..0x1018d3c, callback 0x1018ac1/0x1018ad3/0x1018b23, vtable 0x1004940.
- Deep/shallow copy: FUN_0102c0db 0x102c0db..0x102c1f9; frame copy FUN_0102ac4a; part copy FUN_0102a5c4.
- Colour-0 build: FUN_010175ad 0x1018326..0x1018876 (tables), 0x101888d..0x1018a53 (mirrors); death slots 0x1017fc4..0x101801c.
- Colour copies: FUN_01018d48 0x1018d48..0x10192fc; FUN_010192fd 0x10192fd..0x1019366; call site 0x100e32c.
- Colour source: 0x100da72 (table 0x10021b0 = 3,2,1,0), 0x101090d, 0x101a7c6..0x101a7d7.
- Recolour: 0x101b81e..0x101b85b, table 0x10049b0 = 00 14 28 3c, callback 0x101b7eb, blit 0x102cfef..0x102d18c.
- SetAction: 0x101ad02..0x101b48a; jump tables 0x101b48f (cleanup, old 2..0x14) and 0x101b4db (new 0..0x14).
- Action 2 helpers: 0x101e204 (10-hp), 0x101e20d..0x101e27c.
- Sequence flags: 0x102be51..0x102bf1c; sound 0x102bac8..0x102bc5b, 0x102bdab, 0x102e955..0x102ea8e.

## 10. Open questions
- Consumers of the sequence id `+0xc` (virtual ids >= 1344 for mirrored / per-colour copies) were not found in
  the draw or movement path; likely only identity/serialization.
- Whether any caller can issue action 0x11 on land facing S for a colour-1 (team 2) ant (NULL template ->
  fault in FUN_0102c0db) — depends on callers outside this cluster.
- The step callback FUN_0101ee84 may clamp/zero the raw frame dx/dy of knock-back anims (actions 0xe/0x13 move
  32/128 px in the frame data); not analysed here.
- Hi-colour recolour path (W+0xeb8 != 0) not decoded.

---

## Adversarial verification

An independent second pass re-derived every claim above from the Capstone disassembly and recorded a verdict per claim.

# Adversarial verification: cluster "animtables" (re_animtables.md)

Everything below was re-derived from Capstone disassembly (cs.py), raw static-table dumps (v_tbl.py, v_ids.py)
and the CHD reader (v_walk.py). Ghidra was not used as evidence.

## Verdicts per claim

| # | claim (short) | verdict |
|---|---|---|
| 1 | FUN_01018a7a row[5..7] = Mirror(row[3..1], ids[5..7]) | CONFIRMED |
| 2 | FUN_01018b9f deep copy, negate frame dx, part dx = -dx + (L-R) | CONFIRMED (nuance: deep copy drops frame +0x10) |
| 3 | new wrapper vtable 0x1004940, bit0 of +8 = mirror blit | CONFIRMED |
| 4 | 2nd arg becomes id +0xc, virtual id >= 1344 | CONFIRMED |
| 5 | parts-bbox and seq bbox recomputed | CONFIRMED |
| 6 | static tables colour-major, shapes | CONFIRMED |
| 7 | startup builds colour 0 dirs 0..4 then mirrors; FUN_01018d48 builds colours 1..3 | CONFIRMED |
| 8 | FUN_010192fd shallow copy, colour arg unused | CONFIRMED |
| 9 | colour = {3,2,1,0}[team slot] -> ant+0xd4 | CONFIRMED |
| 10 | palette offset {0,20,40,60}, digit-named images excluded | CONFIRMED |
| 11 | walk case dive/climb, terrain choice, index | CONFIRMED |
| 12 | walk restart rule | CONFIRMED (nuance on when +0x88 is cleared) |
| 13 | idle always replays; water idle = W[0x47c0+4c] | CONFIRMED |
| 14 | full action -> table map | CONFIRMED |
| 15 | old-action cleanup | CONFIRMED (FUN_0101e68c side effects added) |
| 16 | walk/carry data identical across types except mud diagonal phase | CONFIRMED |
| 17 | 4-dir tables N/E/S + mirrored W only | CONFIRMED |
| 18 | DAT_01004778 colour-1 row bug | CONFIRMED |
| 19 | sequence flag bits 3/4/5 | PARTLY CORRECTED (bit4-off path also rewinds the buffer) |
| 20 | ant vtable+0x28 = FUN_0101a93a | CONFIRMED |

## Key evidence re-checked

- 0x1018a7a: esi=[esp+0xc]=arg1 (row), edi=[esp+0x14]=arg2 (ids); push id then push row[k] => Mirror(row[k], id).
  Callers 0x10188bf..0x1018a53: walk row W+0x1240+t*0xa0+tr*0x20 with static 0x1002fb8+(40t+8tr)*2, etc. (matches report).
- 0x1018b9f: FUN_0102c0db(this=src, dst, deep=1, cb=0x1018ac1) at 0x1018bee..0x1018bf8; id at 0x1018c04;
  frame loop writes only [edi+8] (neg), [edi+0x1a..0x21]; part loop writes [esi+8] = -dx + (L-R) (0x1018c83..0x1018c9b),
  [[esi+0x1c]+8] |= 1 (0x1018ca2). Rect helpers: 0x10304c8 = offset(dx,dy), 0x103040b = union (zero rect = empty).
- Part rect from binder 0x102a683: (0,0,img+0xc,img+0x10) = (0,0,w,h) (img fields read at 0x102e22b..0x102e26b:
  pitch, w, h, name). So mirrored part dx = -dx - w. Blitter 0x102d130 starts mirrored stores at dest+w and decrements,
  giving an exact reflection about the anchor pixel column.
- Part copy 0x102a5c4: copies dx, dy, rect, +0x10, flag bit2; +0x1c from callback when cb != 0 (0x102a66a) else shared+addref.
- Frame copy 0x102ac4a copies +8,+0xa,+0xc,+0x12..+0x21,+0xe,+0x24(addref) and parts. It does NOT copy +0x10 (the sound
  id word; ctor 0x102a881 leaves it uninitialised). Harmless: the frame sound player 0x102bac8 only uses +0x24.
- FUN_01018d48: players loop 4 slots (0x1018d5a), skip null player or colour 0 (0x1018d6e, 0x1018d83); dir loop 8
  (0x1018ddc, 0x1019104); walk dest 0x490+240c+40t+8tr+d from src 0x490+40t+8tr+d, id DAT_2fb8[240c+40t+8tr+d];
  per-type singles dest off+0x18c+4t (off from ebx=esi+0x33a0 +0xc60/.. => 0x4000,0x4060,0x40c0,0x4120,0x4180,0x41e0,0x47f0),
  per-colour-dir dest base+0x20c+4d (0x101910b..0x1019116), singles 0x47c0/0x47d0/0x47e0 + 4c (0x1019288..0x10192e4).
- FUN_010192fd: 0x1019307 src==0 -> 0; 0x1019318 word id == 0x7ffe -> 0; FUN_0102c0db(src -> new, 0, 0) (shared list,
  0x102c1d0..0x102c1df); id at 0x101934f; [ebp+0x10] never read; ret 0xc.
- Static table dumps: 0x1004638..0x10047f8 colour-0 rows are {N,7ffe,E,7ffe,S,7ffe,Wmirror,7ffe}; 0x1004788 raw
  `38 0d fe 7f 3a 0d fe 7f fe 7f fe 7f 00 00 00 00`. All other virtual ids (colour>0 or colour-0 dirs 5..7) are >= 1344.
  Unchecked startup loads (idle/gf/at/gh/gb/singles/swim/dive/climb) have no 0x7ffe in dirs 0..4, so no OOB read.
- Colour: 0x100da72 word[0x10021b0 + slot*2] (= 3,2,1,0) -> FUN_010108de -> player+0x2c (0x101090d);
  ant ctor 0x101a797 ant+0x56=team, 0x101a7c6..0x101a7d7 ant+0xd4 = players[team]->+0x2c.
- Recolour: 0x101b81e..0x101b845 (uses players[ant+0x56]->+0x2c, same value as ant+0xd4), table 0x10049b0 = 00 14 28 3c,
  callback 0x101b7eb = !isdigit(part+0x10) (0x1034ab0 = CRT isdigit, mask 4), blit add 0x102d14b.
- SetAction 0x101ad02: jump tables 0x101b48f/0x101b4db dumped and match the report exactly. Walk case 0x101b032..0x101b146
  and 0x101b430/0x101b444 match (index d + 8*(tr + 5*(t+6c)) + 0x490/0x928; swim 0x1190 + dirArg + 8c).
- FUN_0101e204 = 10 - hp (16-bit); FUN_0101e20d sets duration = r*200 for frames with event 5 when r != 0.
- rand = 0x10345c0 (MS CRT LCG), idiv 4 -> 0x4850 + 4*(rand%4).
- Ant vtable 0x1004be0: +0x18 = 0x101a928, +0x28 = 0x101a93a; base 0x1001ff0: +0x18 = 0x102b7bb, +0x28 = 0x1009002.
  0x101a93a: x,y -> +0x38/+0x3a, flag bit6, tile = (y/32, x/32) signed idiv, FUN_0100f17f(world, ant, &tile), fog call.
- CHD walk comparison (v_walk.py): only differences across types/carry are the mud NE/SE (1,2) vs (2,1) phase; phase table in
  the report is correct; all walk anims have 12 frames, no sounds; terrain-2 rows are all 0x7ffe.

## Corrections

1. Claim 19 (bit4): when the sound's buffer is already playing and bit4 is CLEAR, FUN_0102e955 (0x102ea5c..0x102ea7a) does
   not only update volume/pan. If newVolume >= stored volume (+0x38) it sets volume, pan AND calls buffer vtbl+0x34 with 0
   (IDirectSoundBuffer::SetCurrentPosition(0)), i.e. rewinds and restarts the playing sound. If newVolume < stored volume
   nothing happens. (Buffer vtable slots used: +0x24 GetStatus &1, +0x30 Play, +0x3c SetVolume, +0x40 SetPan.) Audio only.

## Nuances (claims stand)

- Claim 12: +0x88 is only cleared in the walk path when sameAction && supplied (checks in order at 0x101b10f..0x101b127).
  A restart caused by a new action or by terrA == -1 leaves +0x88 set; the idle case always clears it (0x101af1c).
- Claim 15: FUN_0101e68c(cflag) writes ant+0x100 = timeGetTime() and returns FUN_0102151a(this,1) only when cflag == 0
  (new action != 3 and +0x84 == 0); with cflag != 0 it returns 0, so the new action is never ignored.
- Claim 11: the "garbage" path 0x101b10a (template = dir value) is reached whenever the chosen terrain word is > 4
  unsigned (e.g. a caller passing terrB = -1 with flag = 0). Unreachable only if callers always pass classes 0..4.
- Claim 11: swimmers on terrain 2 use 0x1190 regardless of the holding flag (no carry-swim table).

## Missed behaviour relevant to movement timing (stepper FUN_0102b95f / FUN_0102b997)

- Catch-up loop: FUN_0102b95f (vtbl+0x10) takes now = timeGetTime() once and calls the step repeatedly while it returns
  nonzero (0x102b96e..0x102b98d), unless engine+0xec4 != 0. +0xec4 is set only by the command-line switch "nocatchup"
  (string 0x10475b8, 0x102c6e2..0x102c6f3). Default = catch up all overdue frames in one update.
- Play (0x102c1fc): cursor = 0, nextTime = timeGetTime(), and if the sprite is RUNNING (flag bit2) it calls vtbl+0x10
  immediately. That first step (cursor == 0 path, 0x102b9b1..0x102b9d2) delivers dx = dy = 0, event of frame 0, a zero box,
  wrap flag 0, calls setpos with the unchanged position, sets nextTime += dur(frame0), plays frame 0's sound, returns 0.
- Normal step (0x102b9ef..0x102bab9): only when now >= nextTime (unsigned). dx/dy come from the frame whose duration just
  ended; event and box come from the new frame; wrap flag = 2 if the old frame was the tail, else 1. The callback
  (vtbl+0x38) gets a pointer to {dx, dy, box.LT, box.RB, event, wrapFlag} and may rewrite dx/dy. The position is then
  set to (x+dx, y+dy) through vtbl+0x28 (0x102ba77..0x102ba8b), and nextTime += duration of the new frame (0x102ba9f,
  accumulated, never reset to now). So frame i's displacement is applied when frame i ENDS, and the last frame's
  displacement is applied at the wrap together with wrapFlag = 2.
- A sequence with a single frame never advances after the first step (0x102b9d4..0x102b9de, 0x10298b6 true for one node).
- The ant callback FUN_0101ee84 zeroes dx/dy when frozen (+0xfc, 0x101eed1..0x101eedf), treats wrapFlag == 2 ([arg+0x14],
  0x101ef14 / 0x101ef5f) as "loop finished", and when +0x60 (waiting) is set returns immediately (0x101eecb -> 0x101f71b)
  WITHOUT zeroing dx/dy, so the stepper still applies the raw frame displacement. Which animation runs while waiting was
  not checked here.
