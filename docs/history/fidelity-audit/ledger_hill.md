# Audit ledger: Anthill: enter, deposit, heal, hatch, waiting ring, raid

Result of the residual-findings audit of this area against commit 4aa985f (v0.0.50 plus the cleanup pass). It compares the remake with `Original-Ants/Ants.exe` (Capstone disassembly) and `ants.chd`; nothing was run in the original. `<scratch>` and `SCRATCH` stand for a scratch folder of the audit session that is not part of the repository: the data files named below (probes, CSV tables, disassembly dumps) are not kept here. The synthesis and the ranked list of changes are in [`../AUDIT_ONE_TO_ONE.md`](../AUDIT_ONE_TO_ONE.md).

## Audit LH: is the remake now identical to the original for the anthill area?

Nearly all of report H is now fixed. The waiting-ring bookkeeping still differs, and in a crowded hill about half the ants never deposit.

The full per-finding table with evidence is in `SCRATCH/audit/LH/ledger_LH.md`. Food, score, cue and ally-dialog items are in `audit/LF/ledger_LF.md` (B2, B5, B6, B7) and are only cited here.

### Correction to report H's timings
When a clip starts inside a step callback, the original books the new clip's first frame twice. I read this in the animation step function `FUN_0102b997` (0x102ba60 to 0x102ba9f), and the remake reproduces it (`movement_system.cpp:299-345`). Report H's golden numbers are table lengths without that term.
- **Enter clip:** worker/thief 1060 ms, bomber/fire 1340, combat 1240, swimmer 940 at hp 10, stretched by (10-hp)*200-40 for wounded ants.
- **Raid clip:** 3710 ms instead of 3510.

## Ledger

Status words: FIXED = remake equals the original; PARTIAL; OPEN.

| ID | Finding | Status | Evidence |
|---|---|---|---|
| H-1 | Enter is one clip; deposit and heal at clip end | FIXED | `action_system.cpp:149-188`; 48-combo probe (6 types x food/empty x hp 10/9/5/1) matches H's table plus the first-frame term. The only events in a whole enter are cue 87 and text 61 at the end step. |
| H-2 | Hidden frame lasts (10-hp)*200 ms | FIXED | Same probe; original `imul 0xc8` at 0x101e221. |
| H-3 | No post-exit invulnerability, no underground state; melee refused only in actions 2/0x14/0xa/0xe/0xf/0xc | FIXED | No such fields remain. Probe: melee during the clip is refused, one tick after it is accepted. The stun-end flag `+0x78` is cleared by a timer with delay 0 (about 16 ms), so it is irrelevant. |
| H-4 | Hatch: 8000 ms, pays at once, no ant exists meanwhile, one at a time | FIXED | Probe: newborn exists in the tick ending 8000 ms; a second click gives text 14 and costs nothing. |
| H-5 | Retry is about 8 ms, the `[task+0x1c]=1000` store is dead | FIXED | Scheduler re-read: insert `0x1031465` overwrites that field; `kHatchRetryMs = 8`; test 2.4. |
| H-6 | Home queue model (`+0x68/+0x6c/+0x70`, ANTHILLQ, ring) | PARTIAL | The model matches the binary line by line. The queue bookkeeping in the tile-entry function (`FUN_0101c4f2`) does not: NEW-1 to NEW-3. |
| H-7 | Raid clip, loot fixed at arrival and taken at clip end | FIXED | Probe: 3710 ms, victim -50 at the end step, text 62 to the thief's owner, deposit +50. |
| H-8 | Alarm: cue 48 and text 53 at clip start, victim only | FIXED (content) | Probe. The cue is positional in the remake; see LF B5. |
| G | Hill geometry: entrance, raid tile, tile42, alternative waiting tile, A1..A3 | FIXED | Re-read 0x100edb4 to 0x100ee25; all 16 cells of every hill carry ids 245 to 248. |
| 2.1 | Only an order-2 path end enters; no hp/food/type test; non-thief on an enemy hill gets a silent stop; hp-1 retreat; pathing through own hill cells | FIXED | An idle carrier on the entrance does not enter. |
| 2.2-2.4 | Message 7, clip tables, orders refused during the clip, end-of-clip order (food source or tile42) | FIXED | A blast on an entering ant: code only. |
| 2.5 | Entrance rules (occupant, A-tile "exactly 2" count, claims, FIFO rule), ring search order, arrival at the ring tile | FIXED for live ants | Compared against 0x101f780 and 0x10202e7. Exceptions: 2.5d, 2.5e, NEW-3. |
| 2.5d | Re-route of a ring-bound ant keeps `+0x68=1` | OPEN | NEW-1 |
| 2.5e | Blocked last tile sets `+0x68=2` | OPEN | NEW-2 |
| 2.5f | ANTHILLQ: 200 ms, smallest `+0x70` with strict `<`, Order(home) | FIXED | Re-read 0x100ff1f. Phase and period drift are not observable. |
| 2.6 | No hidden state; hit boxes; auto-engage filter; hill click is never an attack | FIXED | The dot on the minimap is PARTIAL (NEW-7). |
| 3.1-3.3 | Checks 16/14/13, cost min(200,score), auto free-hatch, starting eggs 6/4/6/2/3/9, newborn (worker, hp 10, dir 1..7, `aghatch` 520, cue 43, text 63) | FIXED | Probe `probe_hatch` and `probe_maps`. H's open question about pedestal slots 1 and 2 is resolved: slot 2 is hidden on the base panel (kinds 8-or-9, 9, 0 at 0x1028137), so only slot 1 can hatch. |
| 4.1-4.4 | Raid: only thieves, refusals, start, clip, end | FIXED | Except the dropped-victim refusal (LF B6) and the ally confirmation at order time (LF B7). |
| D1-D5, D7-D9, D11-D18 | Enter trigger, clip, duration, deposit and heal moment, invulnerability, underground, retreat, hatch rules, incubation, retry, newborn, raid trigger and clip | FIXED | See the data file. |
| D6, D19, D20 | Texts and cues, raid audio, loot | FIXED except | Cues 48 and 61 are positional (LF B5); the score clamp at 0 (LF B2). |
| D10 | Queue model | PARTIAL | See H-6. |
| D21 | Ally raid confirmation | OPEN | LF B7 |
| D22 | Tests that pinned the old behaviour | PARTIAL | `test_hill_actions` (13 cases) pins the new model. Two e2e heal/alarm tests assert `8 == 8` or run on the standalone `SimulationModel`, so they pin nothing of the engine (NEW-8). |

## NEW deviations

| ID | Finding | Original | Remake | Impact |
|---|---|---|---|---|
| NEW-1 | A re-routed ring-bound ant loses its queue flag | Re-path (0x101c935) remembers `wasHome`, clears `+0x68`, then restores `+0x68=1` after the Order (0x101cad1) | `try_enter_tile` (`movement_system.cpp:995-1025`) never restores it; `go_to` sets it to 0 | **High.** One group click on the hill with 8 carriers: ants 3, 4, 7 lose the flag while walking, park on their ring tile and never deposit. |
| NEW-2 | A blocked ring tile never queues the ant | STOP, then `if (wasHome) +0x68=2` (0x101caf2), no order test | Line 1006 requires `order == Home && was_home`, which can never be true | **Medium-high.** An enemy or parked ant on the ring tile leaves the arriving ant stopped next to it, unqueued (ant 6 in the rush). |
| NEW-3 | Dead ants keep influencing live ones | RemoveAnt clears the ant slot, so loops never see it | `can_enter` (`movement_system.cpp:1107-1119`) has no `removed` test | **Medium.** After a queued ant dies, every later arrival is sent to the ring instead of the free entrance. A dead ant's stale move order also shifts goals (30,30 becomes 29,29). |
| NEW-4 | Level-start ants can face North | `rand()%7+1` (never 0) for every spawn | `spawn_unit` uses `rand()%8` (`sim_engine.cpp:965`) | Low, cosmetic: 5 of 24 start ants face North on GAUNTLET. |
| NEW-5 | A dropped team's hill keeps its special tiles | Its entrance, raid tile and A1..A3 become ordinary (0x101d858, 0x101f9a6) | `is_special_base_tile`, `can_enter` and `step_cost` ignore the dropped flag | Low, only after a drop-out. |
| NEW-6 | Ally pedestal on another hill | Only if more than two live teams and the owner has not dropped (0x1028188) | `hud.cpp:399` and `hud_input.cpp:470` use `world.anthills.size() > 2` | Low, only after a drop-out. |
| NEW-7 | Minimap ant dots | All ants keep a dot until removed; own and allied ants that fought within 5 s blink (0x101a88c) | `hud.cpp:684` skips hp 0 and drowning ants; no blink | Low. |
| NEW-8 | Test hygiene | - | Two e2e tests assert constants, and the alarm test runs on the standalone model | None in play. |

## Coverage

**Run against the current libraries**
- Enter timeline: 48 combos.
- Hatch flow.
- Raid, including empty victim and a holding thief.
- Re-path and blocked ring tile.
- Dead-ant effects.
- 8-carrier rush.
- Six-map check of starting eggs, starting ants and hill cells.
- Melee refusal and no auto-enter or auto-raid.

I also built a patched copy of `movement_system` in my scratch directory, not in the repo. With the NEW-1/NEW-2 edits, all 8 carriers deposit (200 points) instead of 4 (100), and the repath and blocked-tile cases end queued.

**Verified in the binary** (dumps in my directory): path-end handler, Order, goal scan, CanEnter, step cost, ANTHILLQ, hatch request and task, scheduler, spawn, raid start and cleanup, enter cleanup, remove-ant, drop-out, panel setup, pedestal hit test, minimap loop.

**Code reading only:** a blast on an entering ant.

**Unverified (needs a runtime oracle):** `+0x6c/+0x70` (the ant constructor 0x101a77a leaves them uninitialised), the real hatch poll rate, and whether the cues are spatialised.

## Top 10 to fix next

1. **NEW-1.** In `try_enter_tile`, clear `home_state` at the re-path and restore it to 1 after the switch. Add a rush test (8 carriers all deposit) and a re-path test to `test_hill_actions`. Existing cases 1.4-1.6 do not re-path and are unaffected.
2. **NEW-2.** Replace `order == Home && was_home` with `was_home` and set `home_state = 2`. Test: enemy ant on the ring tile.
3. **NEW-3.** Skip `x->removed` in both claims loops (or clear state in `remove_ant`). Test: kill a queued ant, then order an ant home onto the free entrance.
4. **LF B5.** Make cues 48 and 61 global at plain volume.
5. **LF B7.** Open the break-alliance question for a thief order on an ally's hill.
6. **NEW-6.** Count live teams and test the owner's dropped flag in the ally pedestal (`hud.cpp:399`, `hud_input.cpp:470`).
7. **NEW-5.** Skip dropped teams in the special-tile loops.
8. **NEW-4.** Use `rand()%7+1` in `init`.
9. **NEW-7.** Keep dying ants' dots and port the fight blink.
10. **Missing golden tests:** hidden-window start per type, melee/blast/pick during the clip, raid sound times (start + 2250 / 2810 / 3570), hatch with a blocked entrance and the redirect at 8000 ms, auto free-hatch through `remove_ant`. Replace the two vacuous e2e tests.

Files are in `<scratch>/audit/LH/`:
- `ledger_LH.md`
- `probe_enter.csv`
- `probe_*.cpp` and their binaries
- `patched/movement_system_patched.cpp`
- `d_*.txt` (disassembly dumps)
