# Audit ledger: Abilities (bombs, fire, bridges) and power-ups

Result of the residual-findings audit of this area against commit 4aa985f (v0.0.50 plus the cleanup pass). It compares the remake with `Original-Ants/Ants.exe` (Capstone disassembly) and `ants.chd`; nothing was run in the original. `<scratch>` and `SCRATCH` stand for a scratch folder of the audit session that is not part of the repository: the data files named below (probes, CSV tables, disassembly dumps) are not kept here. The synthesis and the ranked list of changes are in [`../AUDIT_ONE_TO_ONE.md`](../../AUDIT_ONE_TO_ONE.md).

## Ledger

Every finding of audit B and every claim of audit P now has a verdict. They are FIXED, wrong, or in three cases PARTIAL. Six NEW deviations turned up, all small. Full per-row tables (all of B's D1–D26, §0–§7 and golden tests, and P-1…P-16) are in `SCRATCH/audit/LB/ledger_LB.md`; probes and their outputs are in `SCRATCH/audit/LB/probe/`.

Evidence tags: RUN = probe against the frozen libs; READ = remake code read against the disassembly; BIN = re-derived in `Ants.exe` by me.

| ID | finding | status | evidence | remains |
|---|---|---|---|---|
| B-0.1–0.6 | Order codes 8/9, 6/7, 0xd/0xe, 0xa, 4, 5, 0xb; no cooldown; no blast radius; fire never spreads; bridge = terrain class 3 | FIXED (0.6 n/a) | READ `ant_unit.hpp:116-130`, `classify_order` vs `FUN_01020655`. BIN: the only `push 0x86` is at 0x101e864, in the ignite cleanup. RUN golden G23: 300/300 victims −2 hp, bystander untouched | none |
| B-1, B-2a/b/c/e | Synchronous handlers; SetAction cleanup table, cflag = (new==stun ∥ engaged); clip table; `+0x84`; solid footprint | FIXED | READ `action_system.cpp:84-140`; RUN G04/G13 (melee cancels plant / bridge build), tests 1.3, 1.4, 1.6, 2.3 | none |
| B-2d | Validators for ground, water and bomb tiles | PARTIAL | BIN `FUN_0101d762`, `0101d6d6`, `0101d7f9`, `0100f3e8`, `0100cf0f` match `valid_ground` / `valid_water` / `occupied_stationary` | NEW-3 |
| D1–D6 | Solid placeholder 0xa0; interruption; plant sound 880/920 ms; defuse, ignite and wall at clip end; no fire or bomb on mud | FIXED | RUN golden G02–G08 pass. Real flows (`flow_probe`): bomb 1460 ms after the start, defuse 1200, wall 1910, extinguish 1400 (clip + nested first frame) | none |
| D7, D8 | Bridge stages per pass (480/500 ms); sound 81 land / 82 water; demolish only on 0x25, destroyed at end of pass 4 | FIXED | RUN: stages at 500/1000/1450 ms, passes 540/480/480 in the real flow; demolish destroyed at tick 39 | none |
| D9 | Partial build removed; interrupted demolish restores 0x25 and flips owner | PARTIAL | RUN G13 and tests 3.3/3.5 pass, but the restored bridge has lost its collapse timer | NEW-1 |
| D10–D14 | Pick-up at arrival, hp unchanged, West 2/9 drop, cue 0xd5 when nothing is free, drop on death, Stop ignored during getpow | FIXED | RUN G14/G15/G17/G18. 900 seeds: West 0.227, others 0.106–0.114; BIN `FUN_01020ff6` = `finish_death` | drop-tile test: NEW-5 |
| D15–D17 | Food value = LVL interval, units = LVL delay; exhausted pile keeps its last solid stage; lunchbox rules | FIXED | RUN food_probe totals GAUNTLET 1800, ISLANDS 5600, MEDIUM 4900, SMALL 4000, TINY 4800, TREASURE 10950 (= B's original totals); G25/G26; READ `stage_tile`, `remove_ant` | none |
| D18–D22 | Drowning keeps hp for 2370 ms; lethal bomb flies then dies; fire contact; landing search; dud overlay by type | FIXED | RUN G19/G20 (47 ticks), G24; 238 west + 62 duds of 300; `fire_probe`: worker 8→7 hp at the wall, killer = wall owner, hop back; fire ant only stunned; LA F9 | none |
| D23, D25, D26 | Approach tile; bomb trigger only at ARRIVE / event 3 / animation end; thief amount fixed at start | FIXED | BIN `FUN_01020128` = `approach_tile` 1:1 (N,S,W,E, min 16×Chebyshev, first wins, flags 0x81). READ `walk_step` block B, `start_raid` | none |
| D24 | "Extra" burnout sound | NOT A DEVIATION | One cue 5 per sputter (`sim_engine.cpp:263`, `ability_system.cpp:234`); the sputter clip carries sound 5 at frame 0 | position law is audit S |
| B-4, B-GT12 | B's ms timelines; golden "getpow 770 ms, chime 420 ms" | B WRONG | BIN: `FUN_0102c1fc` sets +0x10 and runs the start step; `FUN_0102b997` adds the first frame again at 0x102ba9f, so a clip started in a callback has a doubled first frame. Getpow = 840 ms from the snap, chime +490 ms. The remake matches (RUN: G27/G28 are the only 2 of 32 golden checks that fail, for exactly this reason) | keep the remake |
| B-7a | Burnout poll jitter ("180.0–182.5 s") | PARTIAL, machine dependent | BIN: 8 ms timer wheel, 2500 ms = 312.5 slots, so polls come every 2496 ms (fast loop) or 2504 ms. The strict `clock < thr` test passes at poll 72 (≈180.3 s) on a slow loop, else at poll 73 (≈182.2 s). B's range is right | remake 180.000 s (0.3–2.2 s early, same for bridges); needs the real game |
| B-7b–d, B-claim | Non-local branches; texts and voices; bridge clip = class 3; "player orders plan through own bombs (mask 0x20)" | by design / FIXED / CONFIRMED / WRONG | BIN step cost 0x1020b59-0x1020bb7: own and ally bombs cost 8000 unless it is the ordered tile. RUN `bomb_rules_probe`: own/ally bombs are routed around, an explicitly clicked own bomb is walked onto and explodes, an enemy bomb in the way explodes | none |
| P-1–P-5 | Pick-up = arrival of order 1/4 → message 9 (one sender, 0x101cda6), synchronous; getpow action 4, 840 ms; hp untouched; type table | FIXED | BIN xref: one builder site, `FUN_01007202` has 7 callers. RUN G14; flow px 372 vs centre 368 = +4 px overshoot, as P says | none |
| P-6, P-7 | DropOld scan and `Free()` test; orders refused during the clip | FIXED, validity PARTIAL | BIN dump `0x1020de7/0x1020e6e`: scan order identical; West 2/9 confirmed analytically | NEW-5 |
| P-8, P-9 | Cancel window = crossing→landing (grass E/S/diag 200 ms, W/N 150; sand 160/120; dirt 240/180; mud 420/360/540); any accepted order cancels | FIXED | RUN `window_probe`: grass 4/3 ticks, sand 4/3, dirt 5/3, mud 9/8/11 ticks = P's CSV at 50 ms resolution | none |
| P-10–P-13 | Standing on a power-up: Stop skips the ant; re-order onto own tile picks up after ~200 ms; attack order → "Can't go there"; combat auto-engage fails; nobody lands on it | FIXED | BIN `FUN_01028a60` vs `stop_ant` identical. RUN `powerup_probe`: Stop accepted=0, 200 ms, status 58 with hp 10, combat ant 8 s hp 10, 0 of 30 punches land on it | none |
| P-14, P-16 | Status texts 6–11 after a type change; map default type unused | FIXED (v0.0.38); CONFIRMED | LS S-site 02–09; RUN blk3: all six maps 0x7ffe | none |
| P-15 | Flower droppers "approximate" | OPEN (known) | quantified below | NEW-4 |

## NEW deviations

| ID | what | original | remake | visible impact | fix |
|---|---|---|---|---|---|
| NEW-1 | An interrupted demolish makes the bridge permanent | BIN `FUN_0101ecdf`: the restore path (0x101ed58) never calls the timer cancel `FUN_0101edfe`; `BridgeTimeout` (0x1024e66) collapses the tile when layer 2 == 0x25 at the timeout | `Grid::set_layer2` zeroes the timer (`grid.cpp:474`), called from `ability_system.cpp:320` and `:340`. RUN `timer_probe`: hit at tick 3 or 14 → timer 0, bridge still standing at tick 2400 (original: gone at tick 2000) | A swimmer that loses a demolish fight leaves an eternal bridge (rare; matters on ISLANDS) | keep the timer through the passes and the restore; at timeout act only if id == 0x25 |
| NEW-2 | Swimmer under a collapsing bridge: clip never re-entered | BIN `FUN_0100f8bf` 0x100f96b-0x100f99f: dsplash effect, then `SetActionDefault(current action)`, so the clip is re-chosen by the new terrain | `combat_system.cpp:422-439` sets flags only. RUN `collapse_probe`: idle swimmer keeps clip 965 (land idle) instead of astw301; a walking swimmer keeps the mud clip 955 | Standing pose or mud gait over water until its next action. The invented splash (71, `:433`) is LS NEW-9 | call `set_action(current)`; drop the sound |
| NEW-3 | Bomb and fire wall refused on three tiles left of every hill | BIN HillSpecial (`FUN_0101d858`/`0101d8a4`) = entrance, raid tile, the three tiles above the hill; a dropped team has none | `grid.cpp:358` rule 1 (x == bx−2, y = by−1..by+1) used by `valid_ground` (`movement_system.cpp:1225`). RUN `hillspot_probe`: X at (8,26..28) beside hill (10,27) on MEDIUM, plain free ground | "Can't do that..." on 3 tiles per hill; test 12.112 encodes it | drop rule 1; skip special tiles of a dropped team |
| NEW-4 | Flower droppers differ | BIN FDTASK polls every 3000 ms (exact grid), strict `now−last > param·1000`, stamp at the request, tile needs layer 2 empty or a power-up and no ant, landing at 820 ms, clover offset (0,0). Data: 6 droppers on the stock maps | `sim_engine.cpp:312-392`: timer reloaded at the LANDING (`:325`), refuses only ant/bomb/fire, drop tile always (x,y+1). RUN `dropper_probe`: SMALL 15.00 / 30.80 s, MEDIUM 8.00 / 16.80 / 25.60 s | Original 15 s (or 18 s when the jitter loses the strict compare) and MEDIUM 9 s vs remake +0.8 s per cycle | poll model, stamp at request, 820 ms landing, layer-2 rule; tests 12.33, 12.126 |
| NEW-5 | Power-up drop tile test ≠ `FUN_01020de7` | BIN 0x1020de7: layer-1 solid bit (`FUN_0100cf0f`), not water, layer 2 empty, not HillSpecial | `sim_engine_impl.hpp:172-198` uses terrain-flag `is_solid_obstacle` plus the whole 4×4 hill | Drop can land on solid row 0 (READ only; needs a real-map probe) | use `is_solid_object` + `is_special_base_tile` |
| NEW-6 | Invented counters `bombs_planted`, `bombs_defused`, `fires_lit` | none in `EndPlant` / `EndDefuse` / `EndIgnite` (BIN) | `ability_system.cpp:142,168,207`, hashed | none (never shown) | delete |

## Coverage

- **Ran:**
  - the 32 golden checks of B on the current libs (30 pass);
  - B's three older probes re-run;
  - real order-driven flows for plant, ignite, defuse, extinguish and bridge;
  - the power-up window on 4 terrains × 8 directions, plus the standing, immunity and landing cases;
  - bridge timer after an interrupted demolish;
  - fire contact, food totals on all six maps, bomb rules;
  - dropper timing on SMALL and MEDIUM, swimmer clip at collapse;
  - tiles refused around a hill;
  - data dumps of Block 4, the tile dictionary and hill flags.
- **Read against the disassembly:** `FUN_01020655` (classification), the order-time refusal (`FUN_0101fc50`, text 0x30 plus can't-go clip), `FUN_01020128`, `FUN_01026f91` (special target test), the ends of plant, defuse, ignite and bridge build and demolish, the bridge step callbacks, `FUN_0100f8bf`, pick-up, DropOld, Die, Stop, the timer tasks and the timer wheel.
- **Read only (no probe):** D16, D25, NEW-5.
- **Could not verify:** the loop latency behind the 180.3 versus 182.2 s lifetime; the dropper jitter outcome. Both need a runtime oracle from the real game. Screenshots or a stopwatch of a fire wall's lifetime would settle the first.
- **Overlap:** world-effect timing of abilities is LA F10, and cue positions and the invented splash are LS (NEW-9); I cite them instead of repeating.

## Top 10 to fix next

Only six items remain in this area; none is REGRESSED.

1. **NEW-3** (affects play): delete rule 1 of `Grid::is_anthill_reserved_spot` (`grid.cpp:358`) and make HillSpecial skip a dropped team. The test that encodes the invented rule is 12.112 (`test_app_integration.cpp:6722`).
2. **NEW-1**: preserve `timer_ticks` in `bridge_demolish_pass_end` and in the `end_bridge_demolish` restore (`ability_system.cpp:320, 340`). Timeout acts only if id == 0x25 and is consumed either way. Extend tests 3.4/3.5 (`test_ability_actions.cpp`) with a timer assertion after a hit.
3. **NEW-4**: droppers get a 3000 ms poll, strict `>`, stamp at the request, landing at 820 ms (tick 17), layer-2-empty-or-power-up rule, clover offset. Update tests 12.33 and 12.126 (`test_app_integration.cpp:5516, 7363`); the `test_render_parity` dropper lifetimes stay.
4. **NEW-2**: re-enter the swimmer's current action (attack → clear path, idle) in `bridge_gone_scan`, and drop the sound event together with LS NEW-9. Extend test 3.6.
5. **NEW-5**: use `grid_.is_solid_object` and `is_special_base_tile` in `is_valid_powerup_drop_tile`. Existing tests 1.11/1.12 use empty worlds and keep passing.
6. **NEW-6**: delete the three counters and their hash lines.
7. **B-7a**: leave the 180 s timers alone until a runtime oracle exists. If the original proves to be the fast-loop case, change to 3645 ticks; tests 2.1, 2.2 and 3.1 pin `LIFETIME_180S_TICKS`.
