# Audit ledger: Effects and map objects

Result of the residual-findings audit of this area against commit 4aa985f (v0.0.50 plus the cleanup pass). It compares the remake with `Original-Ants/Ants.exe` (Capstone disassembly) and `ants.chd`; nothing was run in the original. `<scratch>` and `SCRATCH` stand for a scratch folder of the audit session that is not part of the repository: the data files named below (probes, CSV tables, disassembly dumps) are not kept here. The synthesis and the ranked list of changes are in [`../AUDIT_ONE_TO_ONE.md`](../../AUDIT_ONE_TO_ONE.md).

## Ledger

All 15 findings of O and all 37 rows of E's catalogue have a verdict against the frozen copy of v0.0.50. The full per-row table is in `SCRATCH/audit/LE/ledger_full.md`. The probes and their logs are in `SCRATCH/audit/LE/` (`lecap.cpp`, `fxprobe.cpp`, the `le_*.py` references, `logs/`).

**How I checked.**
- **Pixel probes:** the real `Renderer::render_world` of the frozen libraries, compared with a reference I built from `ants.chd` and the original's rules. I re-read those rules in the disassembly: layer-2 loop `FUN_01008089`, sprite list `0x10088e7`/`0x10089bd`, anchor `FUN_010100ab`, clock `0x102c1fc`/`0x102b997`.
- **Sim probes:** what spawns each effect, when, and where.
- **Disassembly:** for every effect creator and task.

The reference was not reused from the old audits.

| ID | Finding | Status | Evidence | Remains |
|---|---|---|---|---|
| O-01..O-04 | food stage art, hills, animated foods, `stick2a` | FIXED | 150 stage windows (all stages of 53 food objects, incl. `fdpmeat2-4`): 0 px. 24 hills, `fdcola1/2/3`, `fdjelo1`, and `fdpmeat1` over its full 8720 ms loop: 0 px at 105 clock values. All six maps at t=0: 0 px. | none |
| O-05 | plants against ants | FIXED | Plants have key row*32+16 (`FUN_0100cd00` adds 16). Probe on 7x7 ant offsets: ant above the plant root is under it, ant in the same row or below is over it. | equal-y history is [LA NEW-2] |
| O-06, C04-C07 | sputter, bsputter, dsplash | FIXED | Fire burnout at exactly tick 3600, armed only if more than 180 s remain. Bridge built in 1500 ms, collapses 3600 ticks later. Swimmer landing in water at 500 ms. All at the tile top-left, with Table-4 lifetimes. | NEW-2, NEW-4, NEW-5 |
| C01/C02 | bombex | FIXED | 1200 detonations (dud, throw, lethal): all spawn it at the bomb cell top-left, 680 ms, y key row*32, fog-gated. Lethal victim flies 900 ms, then dies. | none |
| C03 | sputter on extinguish | FIXED | Spawns at clip end (tick 26), 830 ms. | none |
| O-07..O-11 | BLUEHILL offset, bomb offset, lockstep, palette, draw order | FIXED | 17 clock values on 4 bomb owners, 4 fire walls, bridges 1-4 and 4b, lunchbox, 5 pick-ups: 0 px. 1204 effect renders at 10 ms steps: 0 px. | none |
| O-10 | map-object palette | FIXED | No object or effect animation uses palette indices 2..31. Only four never-drawn editor markers do. So [LA NEW-4] does not apply to objects; it applies to HUD art and the fog dither, which use the HUD table. | none |
| R01-R09, R11 | health bar, shadow/hop, draw order, fog gate, part order, 180 s arming | FIXED | see rows above, `renderer.cpp:676-680, 1088`, `sim_engine_impl.hpp:148` | none |
| C14-C16, R12 | death clips, getpow, drown, thief, hatch clips | FIXED | Death variants over 800 seeds: 200/198/202/200, full durations, played at landing. Clips per [LA F1/F5/F6/F9]. | none |
| C09, C11, C18-C21 | bump cue, exithill cue, `xmarks`, ears, hill brackets, bubbles | FIXED | Brackets: `FUN_01028b4c` places them at the top-left of tile (row+1, col+1) of the home tile, which is the hill's anchor cell (`0x100edb4`). Footprint origin + 64, so C20 is now CONFIRMED (was INFERRED). Bubbles per [LF B2]. | see NEW-9 for bubble timing |
| C08, O-12, R10 | dropper | PARTIAL | Frames, anchor, fog gate and sound are right. The drop ends at 800 ms instead of 820. Measured: MEDIUM drops at 8.0 s then every 8.8 s; SMALL 15.0 s then 15.8 s; GAUNTLET 30.0 s then 30.8 s. Original `FDTASK` (0x100fc0d): first pass stamps, then a 3000 ms pass grid, and it restamps when the drop is posted. So MEDIUM is 9 s then every 9 s, and params 15/30/60 are about param then every param. E's own "+3 s" model was a pass too long. | end at 820 ms; restamp at posting; 3 s pass grid |
| C10 | `powerupd` cue | PARTIAL | `FUN_01020e6e` starts with the IsLocal test (`0x1020e7a`): owner's machine only. The remake plays it to everyone (`combat_system.cpp:525`). | audio only, rare |
| C12 | battle dust cloud | PARTIAL | Pile-up cloud is right [LA, LX T3]. Fire-wall blasts also spawn it wrongly (NEW-1). | NEW-1 |
| C13 | fight-prelude cloud | NOT A DEVIATION | Network latency only [LA]. | none |
| C17 | burn overlays | PARTIAL | Durations match. The original draws the overlay as a view child over everything; the remake draws it inside the y-sorted pass [LA F14]. | move to the overlay queue |
| R13 | hatch | PARTIAL | 8000 ms and the `*hatch` clip are right (newborn at tick 160). The retry while the entrance is blocked exists but is `kHatchRetryMs = 8` (`action_system.cpp:19`). Original retries every 1000 ms [LX #8]. | set to 1000 |
| O-13 | fog rules for layer-2 items | PARTIAL | Bridges are never hidden, food by footprint cell, pick-ups/bombs/fire by anchor, decor never: all as in the original. Non-anchor cell redraw is missing (NEW-3). | NEW-3 |
| O-14 | Block-2 semantics | FIXED | [LF A1-A3]. Parser names `initial_delay`/`respawn_interval` are still misleading (units / points per unit). | cosmetic rename |
| O-15 | cull margin | PARTIAL (trivial) | Original culls by anchor within ±3 cells; remake rect-culls. Only 5 placed objects reach past that: `grassbig2`, `brush`, `buglass`, `glasses`, `stick2a`. | slivers of at most 32 px at the screen edge |
| C22, C23/C24 | cursors, `egg1h..9h`, `c_cant` | OUT OF SCOPE / UNCHANGED | Cursors belong to audit I. The unreferenced sprites were not re-derived and have no visible effect. | none |
| O open items | `*_S` hill icons; template phase origin; build cadence | NOT A DEVIATION / UNVERIFIED / FIXED | Icons: no code reference in the exe and no longer used by the HUD. Phase origin: needs a runtime oracle; lockstep itself is confirmed. Cadence: [LA F9]. | none |

## NEW deviations

| ID | What | Who sees it, how often | Fix |
|---|---|---|---|
| NEW-1 | An enemy ant blasted by a fire wall spawns the battle cloud and plays sound 3. `blast()` spawns it for every foreign ant (`combat_system.cpp:342`) and the fire block calls it (`movement_system.cpp:655-659`). In the original only `Blast` (`FUN_0101c34c`) makes a cloud, and only in its non-local branch. The fire-wall block (`0x101bbd7`) is guarded by `[world+0xf2a] == ant+0x56`, local ants only. Only the pile-up block (`0x101bba7`) reaches it. Probe `fire_contact`: cloud plus sound 3 on an enemy ant, none on an own ant. | Every fire-wall fight, audible and a brief dust puff. | Spawn the cloud at the pile-up call only (`movement_system.cpp:646-649`). Add a fire-wall test. |
| NEW-2 | At bridge burnout the remake creates bsputter before dsplash (`sim_engine.cpp:271-274`). The original creates dsplash first (`0x100f983`), then bsputter (`0x1024eb2`), so bsputter is drawn on top. | Only while a swimmer stands on a burning-out bridge, 460 ms. | Run `bridge_gone_scan` first. |
| NEW-3 | Fog: in the original, a non-anchor cell that is explored while its anchor is not redraws the object's tile at the anchor position (`0x10082a9-0x1008322`). The remake draws once at the anchor's turn. 12 random masks on 6 maps: the remake equals the "draw once" model in all 72 cases; it differs from the original table in 22 of 72, 4591 px in total. | Stacking of overlapping grass/toys/food at fog edges. | Draw explored non-anchor cells at their own turn. |
| NEW-4 | The original's timeout task only tests the tile (0x86 / 0x25) when it expires. Extinguish and demolish do not cancel it (`FUN_0101e97b`). A fire re-lit or bridge rebuilt within 180 s is removed at the old task's time. The remake resets a per-cell timer (`grid.cpp:396, 441`). | Rare, shortens a re-lit wall. | Keep a list of (tile, deadline) tasks. |
| NEW-5 | For every swimmer on a destroyed bridge the original calls SetAction(current action, facing), so an attack becomes idle (`0x100f988-0x100f99f`, `FUN_0101ace3` = SetAction with 0xffff terrain). The remake only relabels idle (`combat_system.cpp:428-433`). | One step of wrong clip on a walking swimmer. | Re-issue the current clip. |
| NEW-6 | Dropper target rule. The original needs an empty tile or a pick-up and no ant (`0x100fd65-0x100fd83`). The remake checks ant, bomb and fire only (`sim_engine.cpp:349-351`). Clover droppers drop on the plant's own row in the original (table `0x1001af8`), the remake always uses +1 (line 119). A legacy 40x40 fallback (lines 133-152) is invented. | A lunchbox on the drop tile is overwritten. Clover droppers exist on custom maps only. | Use the original test and offsets. Delete the fallback. |
| NEW-7 | Every non-marker Block-1 entry is drawn (`renderer.cpp:621-633`); the original creates only the 14 plant ids and effect ids. | Custom maps only. | Filter to the plant ids. |
| NEW-8 | World view rectangle is 17,22,441x439 (`renderer.hpp:92-95`). The exe's view rect is (16,21)-(458,461), 442x440 (`0x1026a7e`). Already open as I-06. | Every world sprite is 1 px right/down; the view is 1 px smaller. | Owner: audit I. |
| NEW-9 | The exe runs periodic tasks once per display refresh (list scheduler, [LX]). A 20 ms bubble step at 60 Hz would step every 33 ms, so a bubble may live 400 to 667 ms. | Unknown without a runtime measurement. | UNVERIFIED. |

Two harness artifacts were mine, not bugs: a hill-less synthetic level made the grid treat a plant as a hill, and `take_food` alone does not run SetTile.

## Coverage

**Run as code:**
- Real-renderer pixel diffs: all six maps at t=0, 105 clock values, 12 fog masks (72 cases), 17 clock values of runtime items, 1204 effect renders, 150 food-stage windows, 147 plant/ant offsets.
- Sim probes: 1200 detonations; fire and bridge expiry and arming; extinguish; 800 death picks; hatch; droppers on four maps; water landing; fire contact.

**Disassembly only:**
- Score-bubble geometry (confirmed only via [LF]).
- Hill brackets (`0x1028b4c`).
- The `xmarks` spawn sites.
- C10, and the dropper target rule.

**Not verifiable here:**
- The original's template phase origin.
- The real display cadence, hence bubble and task periods.
- Sound positions.
- Anything that needs the real game running.

## Top 10 to fix next

1. **NEW-1.** Move the cloud spawn out of `blast()` into the pile-up call. The tests near `test_app_integration.cpp:5640-5660, 6182, 7031` pin the pile-up cloud and stay valid; add a fire-wall case.
2. **R10/C08/O-12.** End the drop at 820 ms, restamp at posting, use the 3 s pass grid. Test 12.33 (`test_app_integration.cpp:5515-5600`) pins the old 15 s + 16 tick cadence and must be rewritten.
3. **R13.** `kHatchRetryMs = 1000`. Test 2.4 of `test_hill_actions` still passes [LX].
4. **C17.** Draw the burn overlay from the overlay queue ([LA F14]).
5. **NEW-2.** Swap the spawn order at burnout. Test 12.116 (`:8959`) checks presence only.
6. **NEW-3.** Redraw explored non-anchor cells at their turn; add fog cases to `test_render_parity`.
7. **NEW-4.** Keep (tile, deadline) tasks instead of per-cell timers.
8. **NEW-6 and NEW-7.** Original drop-tile test and per-id offsets, delete the fallback, filter Block 1.
9. **NEW-5 and C10.** Re-init the swimmer clip; make the `powerupd` cue owner-only.
10. **O-15 and NEW-8.** The cull slivers are trivial. The view origin is for audit I. NEW-9 needs a runtime measurement.

`[LA NEW-2]` (equal-y tie order) stays open.
