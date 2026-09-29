# core-refute: adversarial verification of locomotion claims C1..C12

Everything below was re-derived from Capstone disassembly of Original-Ants/Ants.exe (image base 0x01000000).
Ghidra output was only used to find function boundaries. The CHD data was re-read with a new parser
(`rc_chd.py`) written from the loader code at 0x102da16, 0x102be51, 0x102a977 and 0x102a710.
chd.py was not used. Throw-away Capstone / CHD helper scripts were used for the dumps.

## Verdict table

| Claim | Verdict | What changed |
|---|---|---|
| C1 | CONFIRMED | Adds the return value: 1 if any step returned nonzero. The catch-up loop reuses one `now`. |
| C2 | CONFIRMED, with details added | evt[2..3] are zero on the start step. The duration added is that of the frame advanced **to**. Ant vtable slot +0x28 is **0x101a93a**, not 0x102b7bb. |
| C3 | CONFIRMED | Adds: refcount at +4, part-union rect at +0x1a, sound object at +0x24, part list at +0x28. |
| C4 | CONFIRMED | Adds: a play call first stops tracked sounds if the target's old anim had flag3. It copies only bits 3/4/5 (bit 2 'running' is kept). |
| C5 | CONFIRMED | Ants have the running bit. The recursive start step finishes before the outer function adds the duration again. A same-list replay variant also exists (see C5). |
| C6 | CONFIRMED, partly | Iteration details added. The call frequency of the display-list pass was not traced. |
| C7 | CONFIRMED, with corrections | The formulas use the colour index +0xd4, not the team. The old-action switch has **early exits** that return before anything is stored. +8 is set, then cleared. The computed terrain uses the pixel tile. All compares are 16-bit. |
| C8 | CORRECTED (minor) | The dir 5..7 entries ARE read: they are synthetic anim IDs (3135..3545 > 1343) passed to the mirror function and stored at template +0xc. Mirroring negates dx only, and the box is not mirrored. Per-colour copies are built by FUN_01018d48. |
| C9 | CORRECTED (minor) | Speeds, frame counts and distances confirmed. "All six types identical" is false for **mud diagonals**: the 1/2 alternation phase differs by type (table below). |
| C10 | CONFIRMED core, several omissions | The zero-component nudge is -1. The c4f2/+0x60/+0xfc block runs only on a new tile. Missed: pre-checks, action gating, the idle/no-path branch, an immediate arrive for action 0/3 with a path, a harvest early-finish, and auto-engage cancelling the snap. Arrive's bomb check reads +0x5a. |
| C11 | CONFIRMED, with details added | The +0x60 return does **not** zero d. The +0xfc path also returns immediately. Child sprite handling added. |
| C12 | CONFIRMED, with corrections | Timer = scheduler add(task, 0, 300, 0): initial delay 0, period 300, and the first run is a no-op, so the body runs about 300 ms later. The resume call is FUN_0101ad02(a, d, -1, -1, 0, **1**); arg6 is unused, so this is harmless. Re-waiting copies the old saved action/dir. FUN_0101cc1e(1) does nothing if already waiting. |

---

## C1: FUN_0102b95f (ant vtable+0x10) CONFIRMED

```
0102b964 call [0x10012a4]            ; IAT -> "timeGetTime" (hint/name at 0x10460bc)
0102b96a edi = now ; ebx = 0
0102b96e push edi ; push [esp+0x14](arg) ; call 0x102b997  -> FUN_0102b997(arg, now)
0102b97a if eax==0 -> exit
0102b983 ebx = 1
0102b986 cmp [[0x104b450]+0xec4],0 ; je loop
0102b990 return ebx                   ; ret 4
```
```c
int Sprite_Update(this, arg) {            // 0x102b95f
    DWORD now = timeGetTime();            // taken once, reused for every catch-up step
    int any = 0;
    while (Sprite_Step(this, arg, now)) {  // 0x102b997
        any = 1;
        if (*(int*)(*(int*)0x104b450 + 0xec4) != 0) break;
    }
    return any;
}
```
A normal step always returns evt[5] != 0 (1 or 2) unless the callback changes it. The loop therefore keeps stepping until `now < +0x10` (catch-up). A start step returns 0.

## C2: FUN_0102b997 step CONFIRMED (details added)

The evt block is on the stack at ebp-0x18: `{int dx; int dy; int16 box[4]; int event; int status}`, at offsets 0,4,8,0x10,0x14.
0x102b9ac calls 0x10303a5, which zeroes the 4 box words, so **evt[2], evt[3] = 0 on a start step**.

```
0102b9a4 if [esi+0x28]==0 -> return 0
0102b9b1 if [esi+0x2c]==0:                         ; START step
0102b9c1   edi = list->First(addref=1)  (0x10298fc: cursor=head, returns head.data)
           dx=dy=0 ; evt.event = movzx word[edi+0xe] ; evt.status=0 ; goto CALL
0102b9d7 if list->IsEmptyOrSingle() (0x10298b6) -> return 0      ; checked BEFORE the time test
0102b9e3 if (unsigned)now < [esi+0x10] (jae) -> return 0
0102b9f5 list->SetCursor([esi+0x2c]) (0x1029b68)
0102b9fe cur = list->Current(remove=0) (0x1029acf) ; dx = movsx word[cur+8] ; dy = movsx word[cur+0xa] ; release
0102ba1b status = list->AtTail() ? 2 : 1          ; tested BEFORE advancing (0x1029b72: cursor==0||cursor->next==0)
0102ba32 edi = list->Next(wrap=1, addref=1) (0x1029941)
0102ba39 event = movzx word[edi+0xe] ; box = dword[edi+0x12], dword[edi+0x16]
CALL:
0102ba52 oldlist = [esi+0x28]
0102ba5a call [vtbl+0x38](&evt)                     ; ant: 0x101ee84
0102ba60 if [esi+0x28] != oldlist: release edi ; edi = newlist->Current(0)   ; no NULL check
0102ba8b call [vtbl+0x28](movsx[esi+0x38]+evt.dx, movsx[esi+0x3a]+evt.dy)   ; x,y re-read after callback
0102ba96 [esi+0x2c] = list->cursor  (0x1029b64 of the CURRENT [esi+0x28])
0102ba9f [esi+0x10] += movzx word[edi+0xc]
0102bab4 PlayFrameSound(arg ? arg : [esi+8])  (0x102bac8)
0102babe return evt.status != 0                      ; ret 8
```
Semantics to implement exactly:
* When moving from frame k to frame k+1, **d = frame k's dx,dy** and the wait added is **frame k+1's duration**. On wrap, the last frame's d is applied and status=2.
* Timing accumulates (`+0x10 += dur`); it is not re-based on `now`.
* dx/dy are signed 16-bit (movsx). The duration is unsigned 16-bit (movzx).
* **Correction:** ant vtable 0x1004be0 slot +0x28 = **0x101a93a** (ant SetPosition(x,y)). Slot +0x18 = 0x101a928 (SetPosition(point) -> 0x101a93a).
  0x102b7bb is slot +0x18 of the view/base vtable 0x1002090, not an ant slot.
* 0x101a93a SetPosition(x,y): `+0xe |= 0x40`; `+0x38=x; +0x3a=y`. Then tile={row=y/32, col=x/32} (signed idiv) and FUN_0100f17f(world,this,&tile).
  Then it reveals fog: if (mode==0 && localTeam==team) or player[localTeam]+0x68==team, it calls FUN_01006af4(map, tileOf(this), 6, [world+0x4a58]).
* List primitives verified: 0x10298fc(addref) sets cursor=head and returns data only if addref!=0. 0x1029941(wrap,addref) returns 0 if head or cursor is null, otherwise cursor=next (head if null and wrap). 0x1029acf(remove) returns cursor data addref'd.
  0x10297ed is addref (inc word +4). 0x10297f2 is release (dec word +4, deletes at 0, returns the remaining count).

## C3: frame/sequence records CONFIRMED

FUN_0102a977 reads ten 4-byte values in order through 0x10065b1(buf,4):
dx->w+8, dy->w+0xa, dur->w+0xc, L,T,R,B->w+0x12,+0x14,+0x16,+0x18. Event: 0x10f447 (1111111) is stored as 0x2b67 (11111), anything else as its low word, at w+0xe.
Sound goes to w+0x10; if the low word != 0xffff, +0x24 = FUN_0102deda(id).
Then comes the part count, and parts are read by 0x102a710: 3 dwords dx,dy,sprite -> w+8,w+0xa,w+0xc. Each part is appended to list +0x28 and its offset rect is unioned into the rect at +0x1a..+0x21.
FUN_0102be51 reads: len, name(len) -> strcpy +0x14; flag1->bit3, flag2->bit4, flag3->bit5 of +0xe (each set if !=0); frame count; frames (union rect -> +0x20). Finally it sets `+0xe |= 2` (loaded).
CHD file header (0x102da16): version, [x if version>=8], T1, T2, T3, **Tanim**. ants.chd: version 9, Tanim=0x789a5b, **1344 animations**.
My parser shows all 1344 records laid out contiguously, each ending exactly where the next begins.

## C4: play animation CONFIRMED (details added)

FUN_0102c0db(ecx=template; arg1=target, arg2=deepCopy, arg3=cloneCtx), ret 0xc:
```
0102c0f5 if target.+0xe & 0x20 (old anim flag3): 0x102bdab (stop+free tracked sound list +0x34)
0102c100 copy bits 0x08,0x10,0x20 of +0xe from template (xor-mask); bit2 'running' and others preserved
0102c12e target.w+0xc = template.w+0xc (anim id) ; +0x20,+0x24 = template bbox ; strcpy +0x14
0102c151 release target.+0x28 ; +0x28 = 0
0102c161 if deepCopy: clone every frame (0x102ac4a(arg3)) and append (0x102bdfe); restore template list cursor
0102c1d0 else: addref template.+0x28 ; target.+0x28 = template.+0x28      (shared list)
0102c1e6 FUN_0102c1fc(0)
```
FUN_0102c1fc(arg): if +0x28==0 return; if bit3 then +0x30=0 (per-frame sound mask); +0x2c=0; +0x10=timeGetTime(); if (+0xe & 4) call vtbl+0x10(arg).
0x102c235 (vtbl+0x20) sets bit2 and calls 0x102c1fc. 0x102c245 (vtbl+0x24) optionally stops sounds and clears bit2.
FUN_01008829(obj,arg) (0x1008829) addrefs, appends to map+0x78 (count +0x8c) and calls vtbl+0x20(arg). Ant walk plays always pass deepCopy=0, so lists are shared.

## C5: first frame lasts 2x when started from inside the step callback CONFIRMED

The running bit is set for ants:
* Ant spawn 0x100ef18 builds the ant with 0x101a77a (sets vtable 0x1004be0 at 0x101a791). Then 0x100ef9b calls FUN_01008829(map, ant, [world+0x4a7c]), which reaches vtbl+0x20 = 0x102c235 and does `or byte [ecx+0xe],4`.
* FUN_0102c0db never touches bit2.
* The only vtbl+0x24 calls in 0x1008000-0x1030000 are 0x10088c9 (display-list removal), 0x1008e60/0x1008e83 (map tile anims) and 0x102fa9a/0x102fb30 (generic container). None of them is in the ant code.

Order of events (walk example):
```
outer 0x102b997 (now=t1): d=frame_k, edi=frame_{k+1}, call 0x101ee84
  -> 0x101b8cb -> 0x101ad02 -> 0x102c0db(newTemplate) -> 0x102c1fc: +0x2c=0; +0x10=t2 (timeGetTime); running ->
     0x102b95f(0): t3=timeGetTime; 0x102b997(0,t3): cursor==0 -> START: edi'=head, status 0, callback
        (0x101b8cb returns at once on status 0), SetPosition(x+0,y+0), +0x2c=head, +0x10 = t2 + dur0; returns 0
  <- back in outer at 0x102ba5d: list changed -> edi = newlist->Current() = head (the list cursor was set to head by 0x10298fc)
     SetPosition(x + d.x, y + d.y)   (d as possibly rewritten by 0x101b8cb, e.g. the snap)
     +0x2c = head ; +0x10 += dur0  => t2 + 2*dur0
```
Same-list variant (missed by lead): if the callback replays the **same** template (same list pointer), for example on the water-to-mud case for a non-swimmer where both classes map to terrain 3 so terrA!=terrB triggers a restart, then the outer `if list changed` test is false. edi stays = frame_{k+1} of the old run, while the cursor is reset to head.
Result: `+0x10 = t2 + dur0 + dur(frame_{k+1})`. All walk anims have constant per-frame durations, so this is also 2x for walking.
If an animation is played outside a step callback (orders, ANTPAUSE task), the first frame lasts dur0 once.

## C6: display-list update CONFIRMED (partly)

FUN_01008952(arg) at 0x1008952: `for (short i=0; i < (short)map[+0x8c]; i++)`, with signed compares (jle/jl).
Per object it calls 0x1007d03(map,[o+0x3c],[o+0x40]) (dirty rect), addref, vtbl+0x10(arg), release. If release returns 0 it does i-- (the object removed itself).
It is called only from 0x1009c93 inside 0x1009c51, which is slot +0x10 of the view vtable 0x1002090.
Main loop 0x1031916: PeekMessageA(PM_NOREMOVE). If a message exists: GetMessageA, exit on 0x12, else Translate/Dispatch. If none: scheduler [app+0xe88]->vtbl+0x18 (0x10310e8, runs due tasks). There is no Sleep in the loop.
(The Sleep(100) in FUN_010175ad is part of loading.) How often the view update runs was not traced; see the open questions.

## C7: FUN_0101ad02(action, dir, terrA, terrB, flag, extra), ret 0x18 CONFIRMED core, corrected

```
0101ad21 sameAction = (+0xe4 == action)
0101ad26 cf = (action==3 || +0x84!=0)                 ; passed to cleanup handlers
0101ad3d switch(old +0xe4 - 2) via table 0x101b48f (0..0x12):
   2,0x14:FUN_0101e165(cf)  5:FUN_0101e342  6:FUN_0101e798  7:FUN_0101e97b  8:FUN_0101e433  9:FUN_0101e599
   0xd:FUN_0101e27f  0x10:FUN_0101eaec  0x11:FUN_0101ecdf  0x12:FUN_0101ecc4   3,4,0xb: nothing ; 0,1: out of range -> nothing
   0xa,0xe,0x13: if new in {0xa,0xc,0xe,0xf,0x13} nothing; else if FUN_0101e68c(cf) && word hp(+0x74)==0 -> **EXIT**
   0xc:  FUN_0101e120(cf); **EXIT always**                (0x101add3 -> 0x101b41c)
   0xf:  if new==0xf **EXIT**; if FUN_0101e0c2(cf)==0 **EXIT**
   EXIT = 0x101b41c: +8 = 0; return  (nothing stored: +0xe0/+0xe4 unchanged)
0101ae1f sameDir = (word +0xe0 == dir) ; +0xe0 = dir ; +0xe4 = action
0101ae3b if action != 0: FUN_0101cc1e(0)
0101ae45 if action != 3: FUN_0102151a(0)
0101ae67 +8 = [world+0x4a7c]                            ; sound context during the play; cleared at exit
0101ae5b if (word)terrA == 0xffff: provided=0 ; t = tileOf(pixel pos) (0x100ccc0) ; terrA = FUN_01008af7(t.row,t.col)
         else provided=1                                ; stored in the arg1 stack slot ([ebp+8])
0101ae9f type = FUN_0100f9cb(0)                         ; +0x54, or FUN_01021087(map+0x70) if 0
0101aeb5 switch(action) table 0x101b4db
```
Walk handler 0x101b032 (action 1), with `c = word +0xd4` (colour index copied from player+0x2c at ctor 0x101a7cd, **not** team) and `d = +0xe0`:
```
if (flag && type==5) {                                  ; word compares
   if (terrA!=2 && terrB==2) play(world[0x11b0 + d + 8*c])      ; dive (asdi); +0x88=1 ; EXIT (no restart test)
   else if (terrA==2 && terrB!=2) play(world[0x11d0 + d + 8*c]) ; climb (asgo); +0x88=1 ; EXIT
}
t = (!provided) ? terrA : (flag ? terrA : terrB);
if (t==2 && type!=5) t=3;
if (t==2) tpl = world[0x1190 + dir + 8*c];              ; swim; no carry variant
else if (t<=4) tpl = world[(holding(+0xe8) ? 0x928 : 0x490) + d + 8*(t + 5*(type + 6*c))];
restart = !sameAction || !provided || (+0x88 ? (+0x88=0, 1) : (flag ? !sameDir : (word)terrA != (word)terrB));
if (restart) play(tpl, this, deep=0, 0);               ; FUN_0102c0db
EXIT
```
(`world[i]` means dword index i, i.e. byte offset 4*i.) The idle case 0x101aebc always plays; it has no restart test.
Idle uses world[0x47c0/4 + c] if terrA==2 (any type), otherwise world[0x3d0 or 0x868(holding) + d + 8*(type+6c)]. It also sets +0x88=0.
arg6 'extra' is never read in FUN_0101ad02 (no `[ebp+0x1c]` reference in 0x101ad02..0x101b48f).

## C8: animation tables CORRECTED (minor)

Layout confirmed as [6 type][5 terrain][8 dir] ushort, with terrain row 2 = 0x7ffe (null).
Loader FUN_010175ad at 0x101834c/0x101837c: for o=0..5 (type), d=0..4, t=0..4: `world[0x490 + 40*o + d + 8*t] = tpl[W[40*o+8*t+d]]`, and likewise for carry at 0x928.
Mirroring 0x10188d4/0x10188e8 calls FUN_01018a7a(dest, src): `dest[5]=Mirror(dest[3], src[5])`, `dest[6]=Mirror(dest[2], src[6])`, `dest[7]=Mirror(dest[1], src[7])`.
**Correction:** src[5..7] are read. They are IDs 3135..3545 (> 1343, so not CHD indices), stored into the new template's word +0xc (0x1018c04).
Mirror FUN_01018b9f: new sprite (0x48 bytes), deep copy through 0x102c0db(new,1,0x1018ac1). Then per frame: **dx = -dx** (0x1018c54). dy, dur, event and box +0x12..+0x18 are unchanged (**the box is not mirrored**).
Per part: `x = -x + (w[+0x14] - w[+0x18])`, and the image flag `[part+0x1c]+8 |= 1`. The part-union rect is recomputed.
Colours: FUN_01018d48 loops the 4 players. For each colour index c != 0 it builds recoloured copies (FUN_010192fd(src,id,c)) of all 8 dirs into `+240*c` (walk/carry) and `+48*c` (idle).
Swim, dive and climb (0x1004838/0x1004878/0x10048c0) go to world 0x1190/0x11b0/0x11d0 with colour stride 8.

Walk table 0x1002fb8 (dirs 0..4 | mirror IDs):
```
type0 g 817 818 819 820 816 | s 843.. | m 838 839 840 841 837 | d 833 834 835 836 832
type1 g 761 760 759 758 757 | s 1143 1142 1141 1140 1139 | m 1153.. | d 1148..
type2 g 672 673 674 670 671 | s 713 714 715 711 712 | m 723.. | d 718..
type3 g 1056 1057 1058 1054 1055 | s 1081.. | m 1076.. | d 1071..
type4 g 893 894 895 891 892 | s 1277.. | m 1287.. | d 1282..
type5 g 944 945 946 942 943 | s 959.. | m 954.. | d 949..
```
(Full dump: `python3 rc_tables.py`.) Swim 998,999,1000,996,997; dive 988,989,990,986,987; climb 993,994,995,991,992.

## C9: CHD motion data CONFIRMED except mud-diagonal phase

Each walk and carry animation, for every type, terrain and dir 0..4, has 12 frames. All events are 11111 and there are no sounds. Flags are (0,1,0) or (0,1,1).
Each entry below gives px per frame, then ms per frame:
* grass: straight 4 px, diagonal (3,3); 50 ms
* sand: straight 4 px, diagonal (3,3); 40 ms
* dirt: straight 4 px, diagonal (3,3); 60 ms
* mud: straight 2 px; 60 ms. The diagonal alternates (1,1) and (2,2), 18 px per cycle.
* dir 0 is (0,-v), N. dir 2 is (+v,0), E. dir 4 is (0,+v), S.

**Correction:** the mud diagonal starts on a different phase depending on the type:
* walk NE (dir1): types 0,3,4 start with (1,-1); types 1,2,5 start with (2,-2).
* walk SE (dir3): types 0,1,2,4,5 start with (1,1); type 3 starts with (2,2).
* carry NE: types 0,2,3,5 start with (2,-2); types 1,4 start with (1,-1). carry SE follows the same split.

The other animations:
* Swim assw: 12 frames of 40 ms, (0,-3)/(2,-2)/(3,0)/(2,2)/(0,3).
* Dive asdi: 16 frames, 1040 ms, 32 px. dx per frame = 0,0,0,0,8,10,10,4,0... with durations 60,60,60,60,60,60,80,100,80,60...
* Climb asgo: 7 frames of 60 ms (420 ms), 32 px. dx per frame = 4,4,4,4,6,10,0. Flags (0,1,1).

## C10: FUN_0101b8cb walk step callback. The core is CONFIRMED; see "full pseudocode" for the complete version

It is called only from 0x101f692. That call is reached for actions 0 and 1 (0x101eefc), for action 3 when status!=2 (0x101f063), for 0xe/0x13 when status!=2 (0x101f5c8), and for 0xa when status!=2 and vtbl+0x40()==0 (0x101f636).
The lead said actions 0/1 only.
The core claims check out: status 0 returns; the +0x88 && status==2 case snaps and arrives; the new-tile and off-path tests; the nudge; the c4f2 test; the ≤2 tolerance (0x101c038 `cmp bx,2; ja` on abs() from 0x1034f20, an unsigned-16 compare); the new-tile FUN_0101ad02(1,+0xe0,terr(+0x5a,+0x5c),terr(ntile),0,1); and the arrive sequence.
Corrections and omissions:
1. The nudge is `s = (d>0) ? +1 : -1` per axis (0x101bf3a setle/dec/and 2/dec). **An axis with d==0 gets -1.**
2. The c4f2 / +0x60 / +0xfc block runs **only when newtile** (0x101bf0a `je 0x101bffc` skips it).
3. Missed pre-checks (0x101baef-0x101bd4f). They run before the movement logic and are inert while walking along a path, because walk frames never have event 3 and they need `status==2 && +0xd8==0`: bomb (own team), fire-wall 0x86, effect, and water-landing checks for events == 3.
4. Missed gating: `if action not in {0,1,3} return` (0x101bd55).
5. Missed no-path branch (+0xd8==0): `if FUN_0101c0d5() && FUN_0101dbec(4,&pixTile)`: save order, FUN_0101da6f, FUN_0101c184(3000). This is the idle auto-engage path. Otherwise it returns.
6. Missed: with a path and action 0 or 3, jump straight to ARRIVE with cur=+0x5a (0x101bde2 -> 0x101b932). d is not snapped. **This is how a newly assigned path starts moving:** on the next idle-anim frame step, idx++ runs, so waypoint[0] is skipped.
7. Missed harvest early-finish (0x101be8e): `if order==5 && pos==centre(+0x5a) && (query(&wp[idx],0x80).flags&4) && query.id==+0xb0`: d=0, idx=count, ARRIVE, which ends the path.
8. ARRIVE's bomb check queries **+0x5a** (0x101b959), not the arrival tile. ARRIVE's auto-engage success sets **d=0** (0x101ba30), which cancels the snap, so the ant can stay up to 2 px off centre.
   It also calls FUN_0101c184(**2000**), where the idle path uses 3000.
9. `ntile` uses C signed division `/32` (idiv, truncates toward 0). Worked example, confirmed by rc sim: going east 16->48 on grass enters B at x=32 and snaps at 48 on the 8th move.

## C11: FUN_0101ee84 (vtbl+0x38) CONFIRMED (details added)

```
0101ee9b if +0x50 (child): child->vtbl+0x18(PointOf(this))      ; child re-synced BEFORE anything else
0101eec8 if +0x60: return           ; evt NOT zeroed -> outer SetPosition still applies frame d
0101eed1 if +0xfc: evt.dx=evt.dy=0; return                     ; also skips dispatch AND tile update
0101eef5 switch(+0xe4) table 0x101f72c ...  (0,1 -> FUN_0101b8cb(evt))
0101f697 n = pos + d ; tile = {n.y/32, n.x/32} (idiv)
0101f6e0 if +0x50: child->vtbl+0x28(child.x+d.x, child.y+d.y)
0101f70a FUN_0100f17f(world, this, &tile)
```
FUN_0100f17f updates the occupancy grid world+0x553c. Each 16-bit cell holds: low 3 bits team (7 = empty), bits 3..7 ant index (+0x58), and bit 8 = more than one occupant. It then writes +0x5a/+0x5c.
Row value 0x5a is the "no tile" sentinel. Every path that sets FUN_0101cc1e(1) (the callers at 0x101c7f2/0x101c84e/0x101c8ee) immediately calls FUN_0101ace3(0), which starts the idle anim, so the non-zeroed d is 0 in practice.

## C12: ANTPAUSE CONFIRMED with corrections

FUN_0101cc1e(on):
* on=1: if +0x60==0, then task=new(0x40) via 0x1024cf7(this). The task saves action=+0xe4 at task+0x38 and dir=+0xe0 at task+0x3c. Then +0x64=task, 0x103057b(task,0,300), +0x60=1.
  If +0x60 is already set it does nothing (the timer is not re-armed).
* on=0: if +0x60 is set, then +0x60=0, [world+0xe88]->vtbl+0x10(task) (unschedule), release, +0x64=0.

Timing: 0x103057b(a1=0, a2=300) calls scheduler add (0x1030e7b)(task, 0, 300, 0). That sets due = now + 0 and then the period to 300.
The first run (0x10305a8) calls the no-op 0x1010f11 and returns 1, so the task is re-queued at now+300. The second run calls vtbl+0x14 = 0x1024d2b and returns 0, which finishes it.
**Net: the body runs about 300 ms after the first idle tick.**
Body 0x1024d2b: FUN_0101cc1e(0). If FUN_0101cbcc(), it calls FUN_0101cc1e(1) and copies the **old** task's saved action/dir into the new task (0x1024d4e-0x1024d64).
Otherwise it calls **FUN_0101ad02(saved_action, saved_dir, 0xffff, 0xffff, 0, 1)**: `push 1` at 0x1024d42 happens before the `je`, so arg6=1 (unused).
FUN_0101cbcc returns 1 iff all of these hold: order ∈ {3, 0xb, 0xf}, count>0, idx==count-1, +0xd8 != 0, and FUN_0101f780(&wp[idx],0,0)==0.

---

## Full pseudocode (exact, for re-implementation)

```c
// ---- FUN_0101b8cb(evt*)  [0x101b8cb] ----
void Ant_WalkStep(Ant* a, Evt* e) {
  Point C = TileCentre(a->tile);                               // 0x101b8ee  (+0x5a,+0x5c): col*32+16,row*32+16
  if (e->status == 0) return;                                   // 0x101b900
  Tile cur;
  if (a->diveFlag /*+0x88*/ && e->status == 2) {                // 0x101b906
    e->dx = C.x - a->x; e->dy = C.y - a->y; cur = a->tile; goto ARRIVE;
  }
  // ---- pre-checks (inert while walking a path; see report) 0x101baef..0x101bd4f ----
  bool own = (world->mode4ae4 == 0 && world->localTeam == a->team);
  bool idleEnd = (e->status == 2 && a->wp == NULL &&
                  (a->action == 0 || (a->action >= 3 && a->action <= 9) || a->action == 0xb));
  if (own && (e->event == 3 || idleEnd) && IsBomb(Query(a->tile, 8).layer2Id)) {        // 0x101bb3e
    a->order = 0xa; a->orderTile = a->tile; e->dx = e->dy = 0; a->wpIdx = a->wpCount; goto ARRIVE;
  }
  if ((a->action==0xa||a->action==0xe||a->action==0x13) && e->event==3 && Query(a->tile,0x20).f24) {
    e->dx = e->dy = 0; FUN_0101c34c(a, 0, 7); return;                                   // 0x101bbca
  }
  if (own && (e->event == 3 || idleEnd) && Query(a->tile, 8).layer2Id == 0x86) {         // 0x101bc49
    if (Type(a) != 2) { e->dx = e->dy = 0; FUN_0101c34c(a, 1, layer2Owner(a->tile)); return; }
    if (a->wp != NULL || !(a->action==0 || (a->action>=3&&a->action<=9) || a->action==0xb)) {
      e->dx = e->dy = 0; FUN_0102151a(a, 1); a->vtbl18(TileCentre(PixelTile(a))); return; // 0x101bcb9
    }
  }
  if ((a->action==0xa||a->action==0xe||a->action==0x13) && e->event==3 &&
      Query(a->tile, 2).terrain == 2) { e->dx = e->dy = 0; FUN_0101e6b3(a, PixelTile(a)); } // no return
  // ---- gating ----
  if (a->action != 0 && a->action != 1 && a->action != 3) return;                         // 0x101bd55
  if (a->wp == NULL) {                                                                    // 0x101bd67
    if (FUN_0101c0d5(a)) { Tile t = PixelTile(a);
      if (FUN_0101dbec(a, 4, &t)) { a->savOrder=a->order; a->savTile=PixelTile(a); a->sav68=a->f68;
                                    FUN_0101da6f(a,&t); FUN_0101c184(a,3000); } }
    return;
  }
  if (a->action == 0 || a->action == 3) { cur = a->tile; goto ARRIVE; }                   // 0x101bde2
  // ---- action 1 with path ----
  int nx = a->x + e->dx, ny = a->y + e->dy;                                               // 0x101bdf3
  Tile nt = { (int16)(ny/32), (int16)(nx/32) };
  bool newTile = !(nt == a->tile);
  bool offPath = !newTile && !(nt == a->wp[a->wpIdx]);
  if (a->order == 5 && a->x == C.x && a->y == C.y) {                                      // 0x101be8e
    Q q = Query(a->wp[a->wpIdx], 0x80);
    if ((q.f18 & 4) && q.f38 == a->targetId /*+0xb0*/) { e->dx=e->dy=0; a->wpIdx=a->wpCount; goto ARRIVE; }
  }
  if (offPath) return;                                                                    // 0x101bf00
  if (newTile) {
    if (!(nt == a->wp[a->wpIdx])) {                                                       // 0x101bf14
      int sx = (e->dx > 0) ? 1 : -1, sy = (e->dy > 0) ? 1 : -1;                           // d==0 -> -1
      e->dx += sx; nx += sx; e->dy += sy; ny += sy;
      nt.row = ny/32; nt.col = nx/32;
    }
    if (FUN_0101c4f2(a, &nt) == 0 && a->frozen == 0) {                                     // 0x101bfb4
      e->dx = C.x - a->x; e->dy = C.y - a->y; return;
    }
    if (a->waiting /*+0x60*/ || a->frozen /*+0xfc*/) { e->dx = e->dy = 0; return; }
  }
  if (a->diveFlag) return;                                                                // 0x101bffc
  Point N = TileCentre(nt);
  if ((uint16)abs(nx - N.x) <= 2 && (uint16)abs(ny - N.y) <= 2) {                         // 0x101c038
    e->dx = N.x - a->x; e->dy = N.y - a->y; cur = nt; goto ARRIVE;
  }
  if (newTile) SetAction(a, 1, a->dir, TerrainClass(a->tile), TerrainClass(nt), 0, 1);    // 0x101c0bf
  return;
ARRIVE:                                                                                   // 0x101b938
  if (own && IsBomb(Query(a->tile /*+0x5a!*/, 8).layer2Id)) {
    a->order = 0xa; a->orderTile = a->tile; e->dx = e->dy = 0; a->wpIdx = a->wpCount;     // falls to idx++
  } else if (FUN_0101c0d5(a) && (a->action == 1 || a->action == 0)) {
    Tile t = PixelTile(a);
    if (FUN_0101dbec(a, 3, &t)) { a->savOrder=a->order; a->savTile=a->orderTile; a->sav68=a->f68;
      FUN_0101da6f(a,&t); FUN_0101c184(a,2000); e->dy = 0; e->dx = 0; return; }           // cancels snap
  }
  if (++a->wpIdx >= a->wpCount) { ClearPath(a) /*0x101ab56*/; FUN_0101ccaf(a, e); return; } // uint16 jb
  Tile nx2 = a->wp[a->wpIdx];
  SetAction(a, 1, Dir8(cur, nx2) /*0x1017531*/, TerrainClass(cur), TerrainClass(nx2), 1, 1); // 0x101bacd
}
// Dir8(from,to) = table16@0x1002b28[(to.row-from.row+1)*3 + (to.col-from.col+1)] = {7,0,1,6,0,2,5,4,3}; no clamping.
// TerrainClass(r,c) = FUN_01008af7: layer2 id in {0x22..0x25} ? 3 : tileinfo[layer1 id].word0 (16-bit result).
```

## Other things the lead missed (summary)
* ant vtable slot +0x28 is 0x101a93a, which does fog reveal radius 6 and the occupancy update. Slot +0x18 is 0x101a928.
* FUN_0101ad02: early-exit cases (old action 0xc always; 0xf; 0xa/0xe/0x13 with hp==0). +8 is set to [world+0x4a7c] during the call and cleared at exit. arg6 is unused.
* A path does not start walking by itself: FUN_0101ab87 (0x101ab87) only stores the path, sets idx=0 and the order (for order 5 also +0xb4/+0xb0).
  Movement starts at the next step callback where status != 0 while action is 0 or 3. That is the next idle-anim frame change, about 100-150 ms given idle frame durations (agst: 6 frames/900 ms, etc.).
  Then the walk anim's first frame lasts 2*dur (C5). Worked example: the first pixel move comes 100 ms after the walk starts on grass.
* Dive/climb run their full animation. While they run, the new-tile c4f2 check can still snap the ant back to centre(+0x5a) (d = C - pos) when the entered tile is blocked. The +0x88 flag makes the tolerance/anim-change code skip until status==2.
* At a waypoint arrival with the same direction, the anim does NOT restart even if the terrain changes (flag=1 ignores terrain). The terrain anim switch happens when the ant crosses the tile boundary (flag=0 path).

## Open questions
1. FUN_0101c4f2 (0x101c4f2..0x101cb05) is large, with side effects (waits, attack-on-contact, FUN_0101f780 blocking test, path clear). It was not decoded here.
2. FUN_0101c0d5 / FUN_0101dbec / FUN_0101c184 / FUN_0101da6f (auto-engage) and FUN_0101ccaf (order-completion switch at 0x101d680) were not decoded.
3. Call frequency of FUN_01008952: it is reached through view vtable 0x1002090 slot +0x10 (0x1009c51). Its parent and trigger, which the scheduler idle probably drives, were not traced. The catch-up loop makes step timing independent of it, apart from quantization.
4. FUN_0101cc1e's use of 0x1031e92 goes through a scheduler whose run-loop granularity, (idle iterations), was not measured.
5. Path layout: waypoint[0] appears to be the start tile, because ARRIVE increments idx before use. Confirm with the pathfinder.

---

## Adversarial verification

An independent second pass re-derived every claim above from the Capstone disassembly and recorded a verdict per claim.

# verify_core-refute: adversarial re-check of re_core-refute.md

All checks were re-derived from Capstone (cs.py / xref.py / grepins.py) on Original-Ants/Ants.exe and from rc_chd.py for CHD data.
I found no claim that is wrong in substance. Three small refinements are listed under "Corrections".

## Per-claim verdicts

| # | Claim | Verdict | Key instructions re-checked |
|---|---|---|---|
| C1 | Sprite update loop | CONFIRMED | 0x102b964 call [0x10012a4] (IAT name at 0x10460bc = timeGetTime); 0x102b96e push edi(now), push [esp+0x14](arg) -> step(arg,now); 0x102b983 ebx=1; 0x102b986 cmp [[0x104b450]+0xec4],0 / je 0x102b96e (reuses edi=now) |
| C2 | Step semantics | CONFIRMED | 0x102b9ac 0x10303a5 zeroes 4 words at ebp-0x10 (box); start: 0x102b9c1 0x10298fc(1) sets list cursor=head; normal: 0x102b9d7 0x10298b6 (empty OR single) BEFORE 0x102b9e3 cmp now,[+0x10] / jae; 0x102ba03/0a movsx dx,dy of CURRENT; 0x102ba1b 0x1029b72 (cursor==0 or next==0) -> setne/inc = 2 at tail; 0x102ba32 Next(1,1) wraps; 0x102ba39-49 event/box of NEXT; 0x102ba8b push y+dy, push x+dx -> vtbl+0x28(x+dx, y+dy); 0x102ba96 +0x2c = list cursor; 0x102ba9f add [+0x10], movzx word[edi+0xc] |
| C2b | vtable slot +0x28 = 0x101a93a | CONFIRMED | dword 0x1004c08 = 0x0101a93a, 0x1004bf8 = 0x0101a928, 0x10020a8 = 0x0102b7bb; 0x101a94f or byte [esi+0xe],0x40; tile = {y/32, x/32} idiv; 0x101a995 call 0x100f17f; fog: (mode==0 && local==team) else player[local]+0x68==team -> 0x1006af4(tile,6,[world+0x4a58]) |
| C3 | Frame/sequence reader | CONFIRMED | 0x102a996..0x102aa18 order dx,dy,dur,L,T,R,B; 0x102aa2a 0x10f447 -> 0x2b67; 0x102aa4c cmp ax,0xffff; 0x102beb4/bedd/bf06 bits 3/4/5; 0x102bfb2 or byte [esi+0xe],2 |
| C3b | CHD header / 1344 anims contiguous | CONFIRMED | rc_chd.py rerun: version 9, tanim 0x789a5b, 1344 anims, 0 gaps |
| C4 | Play (0x102c0db) | CONFIRMED | 0x102c0f5 test target bit5 -> 0x102bdab (iterates +0x34, DirectSound GetStatus via 0x102e85e, stop 0x102eb9a); 0x102c104-12a copies bits 8/0x10/0x20 only; 0x102c1d0-df share list when arg2==0; 0x102c1fc: +0x2c=0, +0x10=timeGetTime, bit2 -> vtbl+0x10 |
| C5 | Running bit / 2x first frame | CONFIRMED | spawn 0x100ef9b -> 0x1008829 -> 0x1008868 vtbl+0x20 = 0x102c235 (or byte [ecx+0xe],4). Only 0x102c257 clears bit2 in sprite code. Note: the report's list of vtbl+0x24 calls omits 0x102e870, but that is a DirectSound COM call ([ecx+0x24] on the interface at +0x34), so the conclusion stands. Display-list removal 0x10088c9 does call vtbl+0x24 when [map+0x68]!=0, but re-adding via 0x1008829 sets bit2 again. Nested start adds dur0 at 0x102ba9f; outer list-changed path 0x102ba65-75 re-fetches Current() = head and adds dur0 again. Same-list variant math also re-derived. |
| C6 | Display-list pass / main loop | CONFIRMED | 0x100895d cmp word [+0x8c],0 jle; 0x10089ad cmp ax,word [+0x8c] / jl (signed 16); 0x1008982 0x1007d03; 0x1008995 vtbl+0x10; 0x10089a3 dec if release()==0; sole caller 0x1009c93 in 0x1009c51 (=dword 0x10020a0); 0x103192e PeekMessageA, 0x103193f GetMessageA, 0x103194d cmp 0x12, 0x103197d [app+0xe88]->vtbl+0x18 (= 0x10310e8 via vtable slot 0x1005220). No Sleep. |
| C7 | SetAction 0x101ad02 | CONFIRMED | jump table 0x101b48f dumped (2:e165, 3/4:none, 5:e342, 6:e798, 7:e97b, 8:e433, 9:e599, 0xa/0xe/0x13:ade0, 0xb:none, 0xc:add3 exit, 0xd:e27f, 0xf:adb9, 0x10:eaec, 0x11:ecdf, 0x12:ecc4, 0x14:e165); 0x101ae05 cmp word [esi+0x74],di / jbe exit; 0x101ae67 +8=[world+0x4a7c]; 0x101b41f +8=0; 0x101ae5b cmp word [ebp+0x10],0xffff; [ebp+0x1c] never referenced; +0xd4 from player+0x2c at 0x101a7cd |
| C7b | Walk handler | CONFIRMED | 0x101b032 flag test, bx==5; dive (A!=2&&B==2) 0x11b0, climb (A==2&&B!=2) 0x11d0, +0x88=1, exit; 0x101b0c9-d6 t = !provided?A:(flag?A:B); 0x101b0d9-e4 2->3 for non-swimmer; 0x101b430 swim 0x1190+dir+8c; 0x101b444-7a index dir+8*(t+5*(type+6c)) + 0x490/0x928; restart logic 0x101b10a-146 exactly as stated |
| C8 | Mirror IDs / mirror function | CONFIRMED (one refinement) | 0x1018a87-ab8: dest[5]=Mirror(dest[3],src[5]), dest[6]=Mirror(dest[2],src[6]), dest[7]=Mirror(dest[1],src[7]); 0x1018c04 id -> +0xc; 0x1018c54 neg frame dx; no writes to frame +0x12..+0x18; 0x1018c87-9b part x = -x + (w14-w18); 0x1018ca2 or byte [img+8],1. Mirror-ID range over idle/walk/carry/swim/dive/climb tables = 3135..3545. Refinement: see Corrections #1. |
| C9 | CHD motion data | CONFIRMED | re-dumped every frame of all 6x4x5 walk and carry anims: all constant except mud diagonals, which strictly alternate; phase table matches exactly (walk NE 0,3,4 start 1; walk SE only type 3 starts 2; carry NE/SE types 1,4 start 1). Swim 12x40 ms (3 / 2,2), dive 16 frames 1040 ms 32 px per axis, climb 7x60 ms 32 px per axis. |
| C10 | Walk step 0x101b8cb | CONFIRMED | nudge 0x101bf36-54 (d>0 ? +1 : -1); c4f2 block only if newTile (0x101bf0a je 0x101bffc); snap 0x101c024/0x101c033 abs (0x1034f20 = abs), 0x101c038 cmp bx,2 ja / cmp ax,2 ja on nudged n; sole caller 0x101f692; callback table 0x101f72c: 0,1 -> walk; 3 -> walk if status!=2 (0x101f063); 0xe/0x13 -> 0x101f5c8; 0xa -> 0x101f628 vtbl+0x40()==0 and status!=2 |
| C10b | Missed branches list | CONFIRMED | pre-checks 0x101baef-0x101bd4f exactly as in the report's pseudocode (bomb, 0xa/0xe/0x13 & event3 & q.f24, fire wall 0x86 with type!=2 / fire-ant branch, water landing without return); gating 0x101bd55-61; no-path 0x101bd67-ddd (c184(0xbb8)); 0x101bde2-ed action 0/3 -> 0x101b932; harvest 0x101be8e-ef7; ARRIVE bomb query uses lea ebx,[esi+0x5a] (0x101b959); auto-engage only action 0/1, c184(0x7d0) then d=0 (0x101ba24-34) |
| C11 | Step callback 0x101ee84 | CONFIRMED (nuance) | 0x101ee9b-b2 child re-sync first; 0x101eec8 +0x60 -> 0x101f71b with no d change; 0x101eed1-e1 +0xfc zeroes d then skips switch and tail; tail 0x101f697-70a. Nuance: see Additional #1 (a SetPosition to a tile centre sits between cc1e(1) and ace3(0) at two of the three sites). |
| C12 | ANTPAUSE | CONFIRMED | 0x101cc33 early exit if already waiting; 0x101cc5a push 0x12c, push 0 -> 0x103057b(0,300) -> 0x1031e92 -> scheduler vtbl+0xc = 0x1030e7b (+0x1c=0, insert 0x10311a8 due=now+0, +0x1c=300); run 0x10305a8: first calls vtbl+0x10 = 0x1010f11 (ret) and returns 1, which reinserts at timeGetTime()+300 (0x10311b1); second calls vtbl+0x14 = 0x1024d2b and returns 0. Body: 0x1024d42 push 1 before je; resume push order gives (action, dir, 0xffff, 0xffff, 0, 1); re-wait copies old +0x38/+0x3c. FUN_0101cbcc conditions re-derived. |
| D1 | Dirs 5..7 = (-dx,dy) of 3,2,1 | CONFIRMED | FUN_01018a7a slot mapping above plus neg at 0x1018c54 |
| T1 | Worked timeline | CONFIRMED (under stated assumptions) | Hand-simulated with the verified rules. It assumes grass on both tiles, path [A,B], c4f2 passes, no auto-engage, and start from idle. Move k at t2+50+50k; x=32 (col 1) on move 4, where there is no snap (|32-48|=16) and no restart (same terrain); snap on move 8 (nx=48) -> ARRIVE, idx 2>=2 -> ClearPath+ccaf at t2+450. |

## Corrections / refinements
1. C8: the ID tables at 0x1002fb8 (walk) and 0x1003738 (carry) are really [4 colour][6 type][5 terrain][8 dir] ushort, which is 960 entries, not 240. The loader FUN_010175ad reads only the colour-0 block. FUN_01018d48 reads W[240*c + 40*type + 8*t + d] (0x1018e25 / 0x1018e4c, [ebp-0x24] = c*0xf0) as the ID for the recoloured copies of colours 1..3. Those blocks hold synthetic IDs for all 8 dirs, starting at 1352 (c1), 1934 (c2) and 2516 (c3). The same layout probably applies to 0x1002cb8/0x1002e38 (idle), with stride 48*c.
2. C5 evidence: the xref list of vtbl+0x24 calls omits 0x102e870. That call is a DirectSound interface call, so the conclusion does not change.
3. "Other things the lead missed", on path-start latency: this is not "about 100-150 ms" in a loose sense. When the path arrives by the path message (see Additional #3), the idle anim is restarted outside a callback just before the path is stored. The walk therefore starts exactly one idle first-frame duration later: 150 ms for types 0, 4 and 5 (agst/acst/asst), 100 ms for types 1, 2 and 3 (abst/afst/atst). The first pixel move comes 2x the walk dur0 after that. For a worker on grass that is 150+100 = 250 ms from order handling to the first pixel.

## Additional findings (same functions)
1. Blocked response inside FUN_0101c4f2 (reached from the walk callback). Every site that sets the wait calls FUN_0101cc1e(1) first, which creates the task and saves action=1 (walk) and the current dir. At 0x101c7ee (non-swimmer) it then does SetPosition(centre(pixel tile)) via vtbl+0x18 (0x101c81a) and then ace3(0). At 0x101c848 (swimmer with order 3/0xf) it does SetPosition(centre(tile at [ebp-0x18])) (0x101c86e) and then ace3(0). At 0x101c8ea it calls ace3(0) only. Because the callback runs before the move, "pixel tile" is the tile being LEFT. The blocked ant snaps back to that tile's centre, idles, and 300 ms later resumes the walk (dur0 not doubled, since this is outside a callback).
2. FUN_0100f17f (occupancy/tile update) returns without updating +0x5a in these cases: player[team]==NULL (0x100f19c); FUN_0100cfb1(+0x58)==0, meaning the ant is not in the player's ant array (0x100f1ae); new tile == old tile. When the old row is the 0x5a sentinel it skips the old-cell clear. The old-cell clear sets the cell to 0xff low byte (empty) or, if bit 8 is set, rescans with 0x100f2cd. The new cell gets team (low 3 bits) and index&0x1f (bits 3..7), or bit 8 if the cell is already occupied.
3. How a path actually gets assigned. The only writer of ant +0xd8 is FUN_0101ab87 (0x101abea). Its non-empty callers are the message handler at 0x1022a40..0x1022ad8. That handler cancels the wait, calls SetPosition(msg point), calls SetAction(msg.action, msg.dir, -1, -1, 0, 1) and then FUN_0101ab87(count, wp, order, target). The message is built by 0x10228ff (action = ant+0xe4 at 0x1022947). Its only caller is 0x100cc89, and that caller requires ant action == 0 or 3 (0x100cc34-3b) and pixelTile == the given start tile. So a new path always starts with action 0 or 3 and wp[0] = the current tile. This confirms the report's model that the action 0/3 ARRIVE branch starts movement and idx++ skips wp[0].
4. Dive and climb can only trigger with flag=1 (waypoint arrival). The tile-boundary SetAction uses flag=0, and 0x101b032 skips the dive/climb test when flag==0. At a boundary a swimmer switches between swim and walk templates through the normal t=terrB path.
5. The scheduler idle (0x10310e8) runs at most ONE due task per main-loop iteration. It re-inserts a periodic task with due = timeGetTime() + period (0x10311b1), not due + period, so periodic tasks drift by their run latency.
6. Robustness hazards, which matter only for exact emulation of edge cases:
   - FUN_01008af7 on an out-of-bounds tile indexes tileinfo[0x7ffe], past the 1344-entry table.
   - In the walk handler, t>4 falls to 0x101b10a with ecx=[ebp+0xc] (the dir) as the "template", which would crash if a restart happens.
   - FUN_01017531 does not clamp its deltas.
   - 0x1002b40 is a duplicate of the direction table and is the one FUN_01017560 uses.
7. FUN_0100f9cb(0) returns +0x54 when it is nonzero, else FUN_01021087(map+0x70), which maps 0x3e..0x42 to 4,3,1,5,2 and anything else to 0. This is consistent with the lead's convention.

## Unverified
- How often the view update (0x1009c51 -> FUN_01008952) runs, and what triggers it. The report also left this open.
- The internals of FUN_0101c4f2 beyond the wait sites, and of FUN_0101c0d5 / FUN_0101dbec / FUN_0101c184 / FUN_0101da6f / FUN_0101ccaf.
- The exact semantics of query struct fields f14/f18/f24/f38 from FUN_0100f4ab, used by the bomb, harvest and effect checks.
- Whether the path message is looped back and handled immediately in single-player, which affects the order-to-walk latency.
