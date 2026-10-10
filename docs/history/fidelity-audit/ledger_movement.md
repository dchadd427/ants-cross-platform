# Audit ledger: Movement and path finding

Result of the residual-findings audit of this area against commit 4aa985f (v0.0.50 plus the cleanup pass). It compares the remake with `Original-Ants/Ants.exe` (Capstone disassembly) and `ants.chd`; nothing was run in the original. `<scratch>` and `SCRATCH` stand for a scratch folder of the audit session that is not part of the repository: the data files named below (probes, CSV tables, disassembly dumps) are not kept here. The synthesis and the ranked list of changes are in [`../AUDIT_ONE_TO_ONE.md`](../AUDIT_ONE_TO_ONE.md).

## Ledger

The earlier movement and path-finding reports are almost all realised exactly by the current code. The open defects sit in blocked-tile handling and in the group-order re-click rules.

SCRATCH = `<scratch>`. Every claim has its own row in `SCRATCH/audit/LM/ledger_LM_full.md`. Line numbers are those of `frozen_4aa985f`.

**How I verified.** I did not use the repo's own generators or tests as evidence.
- **[T]** Table comparison against the exe's static tables and ants.chd. It covers all 2533 clip cells the remake can return (mirror flag, CHD index, every frame's dx/dy/duration/event/sound, sound flags). It also covers the terrain class and flag bits of all 1344 tiles, the passability words, the step weights, the direction table, and the `1.4` constant: 0 differences.
- **[W]** My own walk model, written from the disassembly I re-read this pass (`0x102b95f..0x102bac5`, `0x101bdf3..0x101c0d0`), run against the remake's per-step locomotion trace. 400 uniform-terrain and 600 mixed-terrain random walks give 0 mismatches in step time or pixel position. All 160,000 tick-boundary positions equal the continuous-time model.
- **[A]** My own model of `FUN_01019a66` plus the heap `FUN_01019e20/f0a` (re-read), run against `PathSearch` with budgets 1000/37/5 on 1500 random maps: identical paths. 39 non-optimal paths from the stale heap are reproduced.
- **[G]** Six shipped maps: terrain class and `is_solid_object` of all 16961 cells match the loader model. 1578 random clicks resolve to the same goal tile as my model of the goal ring scan.
- **[P]** Probes, **[C]** code reading, **[B]** disassembly re-read.

| ID | finding | status | evidence | remains |
|---|---|---|---|---|
| AT-0..9 | dirs 5..7 mirror 3..1 (dx negated), colour-major tables, per-colour copies, palette offset | FIXED | [T]; renderer.cpp:204-222 | none |
| AT-10..16 | SetAction walk/idle/dive/climb rules, 20 action tables incl. 4-direction ones, mud-diagonal phases, restart rule | FIXED | movement_system.cpp:460-605; [T]; [W] | dive/climb only exercised toward E by golden tests |
| AT-17 | colour-1 NULL template bug (`DAT_01004778`) | NOT A DEVIATION | original bug, the remake has no colour dimension | none |
| CR-0..6, AT-V | stepper: catch-up, displacement at frame END, next frame's duration added, start step, first frame doubled inside a callback | FIXED | `loco_*` :278-345 equals Orig `0x102b95f..0x102bac5`; [W] | none |
| CR-7 | the original steps sprites every scheduler pass (REFRESH, period 0) | DIFFERENT BY DESIGN | the sim runs the exact-ms schedule inside each 50 ms tick; [W] | LA NEW-1 (presentation) |
| WS-1..11, CR-8..14 | walk step: nudge (d==0 gives -1), arrival ≤2 px, snap, restart only on direction or terrain change, ADVANCE, first step latency | FIXED | `walk_step` :618-742, `arrive` :745-777; [B]; [W] | none |
| WS-12/13, WS-V1/V2 | path end is order 1, StopSync→StopAt, handled cases keep the snap delta | FIXED | `path_complete`; probe_overshoot: power-up pick-up ends 4 px past centre (308 vs 304), hill entry exact | other handled cases by code only |
| WS-3..5 | dive/climb end snap, bomb at ARRIVE, auto-engage | FIXED | `arrive`; combat_system.cpp:584-681 | COMBEVT fire time UNVERIFIED (uninitialised task memory); remake fires at exactly ms |
| WS-14, CO-13 | occupancy grid and default occupant | FIXED | `occ_move` :114; golden 3.4 | NEW-M6 |
| CO-0..7, WS-16, PF-14 | CanEnter rules R1..R7, hill footprint (16 cells per hill, entrance (bx+1,by+1)), solid bits | FIXED | `can_enter` :1040-1128 re-read; [G]; probe_hills; LH line-by-line | dropped-team tiles (LH NEW-5) |
| CO-8/9, WS-15, OR-15, PF-18 | blocked-tile handling | PARTIAL | waiting 300 ms FIXED (golden 3.3); re-path: hill flag (LH NEW-1/2, my probe_home confirms), abilities NEW-M1 | see NEW |
| WS-19, CO-14/15 | ANTPAUSE, exactly 300 ms | FIXED | `pause_fire` :938 | latency not modelled (by design) |
| WS-19 / OR-V2 | "8 ms wheel" | claim WRONG | default is the list scheduler (Orig `0x103155c`, `[0x104b448]`=0; `-newtask` sets 1) | docs, NEW-M7 |
| WS-20, OR-3/4 | order snaps to pixel-tile centre, cancels pause/COMBEVT, stamps +0x98 | FIXED | `go_to` :1384; golden 2.3/2.4 | none |
| OR-0/1, A1-A4 | group order: accept predicate, 16×Chebyshev, strict-`>` exchange sort, ack | PARTIAL | sort and ack FIXED: probe_group gives [C,B,A] with goals C (10,10), B (9,9), A (9,10); skip rule wrong for special clicks | NEW-M2 |
| OR-5..7, PF-10..12 | classification, goal ring scan (d=1..4, first acceptable, mask 0xffd3), approach tile | FIXED | `adjust_goal` :1343; [G] (420 ring substitutes, 42 unreachable) | none |
| OR-8, PF-0/1/17, OR-V1 | PATHMGR: 50 ms, one request per pass, 1000 expansions, 4 grids, one path per run | FIXED (nominal period) | probe_queue: 8 ants deliver at 50..400 ms, nearest first; [A] | NEW-M5 |
| PF-2..9 | cell layout, 0-rooted heap, ties, cost table, fail at f≥8000 before the goal test, path layout | FIXED | [A]; [B] heap; all 15 cost pairs recomputed | no shipped map exceeds 60 rows (row-90 sentinel cannot occur) |
| PF-V1..3 | stale heap, live world state, no path off water | FIXED | [A] | none |
| OR-9..12, PF-16 | delivery gate, count 0 → stop + can't-go + text, message-6 install | FIXED except action 3 | `deliver_path` :1470 | NEW-M3 |
| OR-14 | ally confirmation | FIXED for attack mode, OPEN for thief raid on ally hill | hud_input.cpp:241 | LF B7 |
| OR-A5 | 16-entry stack overflow for 17+ ants | BY DECISION / undefined | `kMaxCommandAnts` = 32 | none |
| WS-V4 | blast keeps the old path, then the direction of a non-adjacent waypoint | UNDEFINED in the original (table overrun) | `dir_between` :46 clamps to sign | none |
| WS-V5 | same-list first-frame variant | NOT A DEVIATION | unobservable: frame 0 and frame 1 durations are equal for every idle/walk clip | none |
| docs | wheel described in body text | OPEN (text) | see NEW-M7 | |

## NEW deviations

| ID | what differs (original VA; remake line) | visible impact | fix |
|---|---|---|---|
| NEW-M1 | **Ability orders lose their identity when the approach tile is blocked.** Orig re-issues `GoTo(+0xb0, player 0, special 1)` for orders 6,7,8,9,0xd,0xe (`0x101ca65/87/a9`). Remake `default: go_to(final_tile,false,false)` (movement_system.cpp:1022). Probe_repath: a team-mate stands on the approach tile, the bomber ends idle with order 0 and no bomb. | Bombers, fire ants and swimmers whose approach tile is taken. Groups ordered onto one tile pick the same tile (mask 0x81, no claims). | Add a case for 6..9, 0xd, 0xe: `go_to(a, a.orig_special_tile, false, true)`. No test pins the old behaviour. |
| NEW-M2 | **Group re-click rules.** Orig skips orders 1/4/5 only when `!special && !attack` (`0x1028874`), and skips orders 6..9/0xd/0xe when `special && +0xb0 == tile` (`0x10288b2`). Remake sim_engine.cpp:1056-1062 skips Move/PowerUp/Harvest even for special clicks and never skips ability orders. Probe_group: a second special click snaps a walking bomber 216→208 px and drops its path; a special click on a plain move's tile is ignored. | Re-clicking an ability target makes the ants jump back and restart. "Walk there, then plant there" does nothing. | Skip Move/PowerUp/Harvest only when `!special`; skip ability orders when `special && orig_special_tile == target`. |
| NEW-M3 | **A path request queued before a stun is delivered to the stunned ant in the original.** Delivery accepts action 3 (`0x100cc2e`) and the ant walks out of the stun. Remake `stun_or_die` bumps `move_serial`, and delivery needs Idle (:1489). Code only. | Rare: an ant ordered less than ~200 ms before it is stunned. | Accept action 3 at delivery; do not bump `move_serial` in `stun_or_die`. |
| NEW-M4 | The remake also drops a FAILED search result after any `clear_path()`; the original always delivers "Can't go there." (`0x100cbe7`). | Negligible. | None needed. |
| NEW-M5 | Original clock jitter: a clip started in a callback is anchored at real `timeGetTime`, and task periods are interval + run time + refresh latency (LX A). Remake uses exact due times and exact 50/300/200 ms. | A few ms slower per restart in the original. A group order's paths come 50–67 ms apart instead of 50 ms. | Needs a runtime measurement on a real setup; cannot be reproduced deterministically. |
| NEW-M6 | `occ_scan` scans players 0..3; the original scans team slots 0..3, which are the reverse (LK NEW-8 root). | Only which ant is "the occupant" of a multi-ant tile (pile-ups). | Iterate players 3,2,1,0. |
| NEW-M7 | The docs still describe the 8 ms wheel: `movement/03_walk_step_arrival_and_stop.md:663`, `06_orders_goto_and_group_dispatch.md:506,:627`, `GAME_REVERSE_ENGINEERING.md:1509`. Corrections exist only in the appended verdicts. | None on play. | Edit the body text. **Done in v0.0.77** (the headings and statements now say that the sorted list is the default and the wheel only exists with `newtask`). |
| NEW-M8 | The regression net is thin: golden tests cover E/SE/W/NW and a few terrain mixes. My differential found 0 differences over 1000 walks, 1500 maps and 2533 clip cells. | None. | Add `model_walk2.py`, `model_astar.py` and `compare_tables.py` as tools plus tests. **Done in v0.0.77** as `tests/test_sim/test_movement_differential.cpp` (C++ ports of the walk and A* models; the table comparison is covered by `test_movement_tables`). |

Already reported by LH and reproduced by my `probe_home`:
- **LH NEW-1:** a ring-bound ant that re-paths ends with `home_state` 0 and is never queued.
- **LH NEW-2:** a ring-bound ant blocked on its last tile stays at `home_state` 1 and is never queued. The code at movement_system.cpp:1006 tests `order == kOrderHome`, which is never true for a ring-bound ant.

## Does the 50 ms tick change movement outcomes?

For unobstructed movement, no. The sim replays the exact millisecond schedule inside each tick. Step time and pixel position equal my continuous-time model for all 1000 walks, and all 160,000 tick-boundary positions are equal.

What the tick does change:
1. Orders are applied on tick boundaries. The original's INPUT task also runs on a ≥50 ms grid.
2. PATHMGR runs at tick end; the original runs at interval + run time + latency (NEW-M5).
3. Ties between ants are broken by millisecond, then ant order; the original breaks them by polling order.
4. Presentation and click picking use the tick-sampled state (LA NEW-1/3). Picking can be up to ~8 px off the moving sprite.

Arrival order follows step times, so it matches the model. Group orders keep the original's nearest-first, 50 ms-apart order.

## Coverage

- **Run:** the tables, 1000 walks plus tick samples, 1500 A* maps, goal resolution plus cells on all six maps, and the probes for blocked approach tiles, home flag, overshoot, group sort/re-click and PATHMGR queueing.
- **Golden tests:** `test_movement_golden` (39 cases, 17710 assertions) and `test_path_planner` (45 cases) pass on the frozen libraries. I could not build `test_movement_tables` (it needs a CMake include directory).
- **Disassembly re-read this pass:** stepper, walk core, A* step and heap, group order, REPATH, scheduler selection, tile-info builder, level cell reader, message-7 builder, stun, COMBEVT task.
- **Code reading only:** handled path-end cases other than power-up and hill entry, dive/climb in directions other than E, NEW-M3, NEW-M4 and NEW-M6, and the combat/hill items taken from LK/LH.
- **Not verifiable without a runtime oracle:** COMBEVT fire time, polling/refresh latency and `timeGetTime` resolution, the original's effective PATHMGR period, and the original's slot order in multi-ant tiles.

Data files in `SCRATCH/audit/LM/`: `ledger_LM_full.md`, `compare_tables.py`, `remake_tables.txt`, `model_walk.py`, `model_walk2.py`, `walk_remake.txt`, `walk2_remake.txt`, `walk3_remake.txt`, `model_astar.py`, `astar_scn.txt`, `astar_remake.txt`, `goal_remake.txt`, `cells_remake.txt`, `d_heap.txt`, `d_walkcore.txt`, and `probe/*.cpp` with their binaries.

## Top 10 to fix next

1. **LH NEW-1 (hill flag lost on re-path).** In the REPATH block of `try_enter_tile`, set `home_state = 0` after the clear and `if (was_home) home_state = 1` after the re-issue. Add a case to `test_hill_actions`; none encodes the old behaviour.
2. **LH NEW-2 (blocked ring tile never queued).** Line 1006 becomes `if (was_home) a.home_state = 2;`. Same test file. My probe: a carrier stays at `home_state` 1, queue size 0.
3. **NEW-M1 (ability orders lose identity).** Add the switch case at movement_system.cpp:1010-1025. New golden case in suite 3: a bomber whose approach tile is taken still plants.
4. **NEW-M2 (group re-click rules).** Change sim_engine.cpp:1056-1062. Add cases to `test_commands` or the golden suite for a repeated special click and "move, then special on the same tile".
5. **LH NEW-5 (dropped team's special tiles).** Make `can_enter` R5, `step_cost` and `is_special_base_tile` skip teams in `dropped_mask_`.
6. **NEW-M3 (stun and pending path).** Accept action 3 at delivery and drop the `move_serial` bump in `stun_or_die`. Low priority.
7. **NEW-M6 (`occ_scan` team order).** Reverse the team order together with LK NEW-8.
8. **NEW-M7 (docs).** Edit the four body-text places that still describe the wheel.
9. **NEW-M8 (regression net).** Add my differential harnesses as regression tests; they would have caught any change to tables, stepping or A*.
10. **NEW-M5 (clock jitter).** Measure the original's real PATHMGR and INPUT period on a real setup (the LX proposal) before deciding whether to model latency.
