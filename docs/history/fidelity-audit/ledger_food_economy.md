# Audit ledger: Food, economy and scoring

Result of the residual-findings audit of this area against commit 4aa985f (v0.0.50 plus the cleanup pass). It compares the remake with `Original-Ants/Ants.exe` (Capstone disassembly) and `ants.chd`; nothing was run in the original. `<scratch>` and `SCRATCH` stand for a scratch folder of the audit session that is not part of the repository: the data files named below (probes, CSV tables, disassembly dumps) are not kept here. The synthesis and the ranked list of changes are in [`../AUDIT_ONE_TO_ONE.md`](../../AUDIT_ONE_TO_ONE.md).

**LF audit: food, economy and scoring.** The core is a faithful port. Piles, grab, bite, carrying, deposit, theft, lunchbox, hatch, counters and texts all match the original. The deviations sit at the edges: audio routing, end-of-match presentation and rules, allied scoring, negative scores and two multiplayer rules.

Files are in `SCRATCH/audit/LF/` (SCRATCH = <scratch>):
- `ledger_LF.md`: full ledger with VAs and remake lines.
- `food_maps_dump.txt`: all 53 piles with stages, units and points.
- `food_maps_compare.txt`: remake vs LVL, 0 mismatches.
- `grab_clips_original.txt`, `grab_clips_remake.txt`, `econ_probe.out`, `end_probe.out`.
- Sources: `probe/*.cpp` with `build.sh` (links the frozen libs), `lvl_raw.py`.

## Ledger (full version in `ledger_LF.md`)

| ID | Finding | Status | Evidence | Remains |
|---|---|---|---|---|
| A1 | Block-2 food records: row, col, units, points per unit, stage pairs; list ends at the first stage without a tile; StageTile; TakeFood (n = min(n, left), returns points×n); AddFoodObject; SetTile | MATCH | Loader 0x1006d19; 0x1009ed3, 0x1009f06, 0x1008ca0, 0x1007352. Remake `grid.cpp:201-230,498-555`. Independent LVL decode plus probe: 53/53 objects equal in fields, stage art at full units, and footprint cells vs the file's own cells. | none |
| A2 | The six maps: 2/10/4/5/14/18 objects (GAUNTLET, ISLANDS, MEDIUM, SMALL, TINY, TREASURE). 10..100 units, 10..50 points per unit, 300..1600 points per pile, 1..5 stages. Corn, chocolate, sucker and pmeat keep a leftover tile at 0 units. | MATCH | `food_maps_dump.txt`. LVL dictionary name equals CHD animation name for every stage tile, so the identity tile remap holds. | none |
| A3 | Duplicate anchors (TREASURE (59,2)×3, (2,59)×2) | MATCH | Loader does RemoveObject then SetTile per object. Cell lookup loop 0x100f760-0x100f7a4 has no break, so the last object wins. StartHarvest 0x1008c63 takes the first. `grid.cpp:466-496`. | none |
| A4 | Grab clips per type and direction | MATCH | Table 0x1003f18 via SetAction case 0x101b27c. Probe: 48/48 clips equal. Worker N 460 ms, other 420 ms (cue 77 cardinal, 66 diagonal). Bomber 440 (66), fire 400 (66). Thief 340 cardinal (77), 300 diagonal (silent). Combat 360 (77 at frame 0). Swimmer 320 (77) / 400 (66). | none |
| A5 | Arrival case 5 (0x101ce07): empty hand and units>0 starts the harvest; a carrier has its thief flag cleared, source = anchor, goes home, text 17 (blink); an empty pile is a plain stop. StartHarvest 0x102178a. EndHarvest 0x101e342 (TakeFood 1, points = order amount or object value, text 60, SetTile on stage change, both ants get food on the last unit). After-bite callback 0x101f06f. | MATCH | `movement_system.cpp:822-838`, `action_system.cpp:374-424`; tests 3.1-3.9. | none |
| A6 | Carried representation | MATCH | SetHolding 0x101ac8c: +0xe8 flag, +0xf0 points, +0xec thief flag, +0xf4 source tile. The remake's `carried_food` is only a flag. Carry clips exist only for idle, walk, cant-go, enter and stun, in both. | `carried_food*25` fallback (`action_system.cpp:178`) is INVENTED and dead |
| A7 | Death with food: lunchbox | MATCH | RemoveAnt 0x100cd9f. Needs a holding ant, tile = position/32, layer-2 tile 0x7ffe, not water, solid bit 0. Message 0x25 then 0x100fdf8: 1 unit, stages {1→tile 356, 0→gone}. `combat_system.cpp:529-545`; probe P6 leaves 40 points on the tile. | none |
| B1 | Deposit 0x101e165: AddScore(+points); text 61 only if points>0; clear; heal | MATCH | `action_system.cpp:176-189`; probe P9: +25, bubble at (672,672) = entrance tile top-left. | none |
| B2 | Score bubble: home tile = hill origin+(1,1) (checked on all 24 hills), sign, 9 px digit field, 20 runs ×20 ms ×5 px (up for a gain, down for a loss). Cues are GameSound 55/56 (sounds 87/88), global. | MATCH | 0x1010560, 0x1010452, task 0x1025479; `sim_engine.cpp:398-411`, `renderer.cpp:1647-1690`. | remake shows step 1 at t=0 and cuts at 400 ms (±5 px, ≤20 ms) |
| B3 | Thief raid: loot = min(victim individual score, 50) fixed at arrival; holding thief refused (17, blink); victim loses at clip end; thief gets 62; victim gets 53 + alarm at start. A thief can only raid hills: nothing steals from an ant. | MATCH | 0x101d57c, 0x101e27f, 0x102184e; `action_system.cpp:283-353`. | see NEW-4, NEW-5 |
| B4 | Hatch: check order (16, 14, 13 + cue), cost min(score,200) via message 0x21, eggs−1, hatched+1, 8000 ms, worker newborn, exithill as an effect, free forced hatch via CheckNoAnts | MATCH | 0x1010aca, 0x1010c14, 0x1025072, 0x100cf48; `sim_engine.cpp:593-628`. Start eggs = final LVL word (6,4,6,2,3,9). Egg tray min(eggs,9). | remake substitutes 10 when the word is 0; the original would give 0 (latent) |
| B5 | Counters: friendly lost on the dying ant's team, enemy killed on the last damager (no alliance test), hatched at the hatch message; starting ants not counted | MATCH | 0x100cdf3, 0x100ce15, 0x1010d1c; `combat_system.cpp:533-535`. | `food_deposited/stolen/lost` fields INVENTED, never incremented, hashed |
| B6 | Texts and blink flags (13-16, 60-63 steady; 17, 53 blink) | MATCH | Every call site decoded. | none |
| C1 | HUD score boxes: number = own + ally | MATCH | 0x1021e36. | colour split: see NEW-3 |
| C2 | Timeout winner by combined score | MATCH (rule) | 0x1015136 sort; `match_stats.hpp:254-273`. | presentation: see NEW-6 |

## NEW deviations (not listed in docs or plan unless stated)

- **NEW-1 (audible, every refused hatch and every raid).** The can't-hatch cue (61) and the raid alarm (48) are global GameSounds in the original: 0x102bd7e with an owner that has no position gives plain volume 100, heard wherever the view is. The remake queues them as positional events. Probe P3 shows 61 at (688,688) and 48 at (1392,1360). The mixer attenuates to silence beyond 800 px (`audio_mixer.cpp:297,324-331`). Sites: `sim_engine.cpp:613`, `action_system.cpp:317`.
- **NEW-2 (audible, every match end).** The end cue plays twice in the same frame. `handle_game_over` queues targeted 56/42 events (`sim_engine_impl.hpp:391-400`), and `application.cpp:949-955` plays the same sting again through the scorecard. `play_sfx` does not de-duplicate, so the local player hears it at double amplitude. The original plays one cue: winner if the local team or its ally is the top row, else losers (0x10159f1).
- **NEW-3 (every allied game).** The original fills the box with the team colour, then refills the right half with the ally's colour and adds the ally's score (0x1022045-0x102208c). The remake draws one colour (`hud.cpp:568,771`). Only the number is combined.
- **NEW-4 (negative scores).** The original AddScore has no clamp (0x1010cc9), and the loot compare at 0x101d57f is signed. The remake clamps at 0 (`match_stats.hpp:175`). Probe P1: victim with 60, two thieves raiding together. Remake: victim 0, loot 50+50 (points created). Original: victim −40, displayed as 0, results show −40.
- **NEW-5 (multiplayer only).** The original refuses a raid on a dropped team's hill (victim +0x64, 0x101d577). The remake does not: `raid_arrive` (`action_system.cpp:283-300`) checks only own, ally and hill exists. Probe P4: victim 120→70.
- **NEW-6 (results screen, partly known as "stage R" in docs 5.14).**
  - The original builds one row per alliance (0x1015136): names joined " & " (string at 0x1047308), all four columns summed over both partners. Rows sort by summed score with the viewer first on ties. The top row is the Winner box (y 235; others 323/373/423). Labels are left-aligned at x 100/485/534/555/576.
  - The remake gives one row per player, each with the combined score but its own counters. The winner is the lowest index of the tied group, other rows show only if a player name is set, and numbers are centred on 497/537/558/579 (`scorecard.cpp:22-63,146`). Probe end_probe: 0+1 allied (100+50) and 2 (120) gives winners {0,1} and scores 150 150 120 0.
- **Known, still MISSING.** (a) Break-alliance question for orders 3/0xb on an ally (0x101ffab, dialog string 4): in the remake the thief walks to the ally's raid tile and stops (probe P5). (b) CHECKGO end rules (0x1024839; docs 5.47 agree with my decode): the match should also end when nobody has ants or eggs, or when the local alliance is the last alive and strictly leads; quit should count as forfeit. The remake ends only at clock < 0 (`sim_engine.cpp:430-449`).
- **Minor.** One-step offset in the bubble. `food_*` counters never incremented. Start eggs default 10 vs 0.

## Coverage
- **Verified by decode and running code:**
  - Food maps: independent LVL decode, 53/53 objects, stage art, footprint cells.
  - Grab clips, 48/48.
  - Economy probes against the frozen libs: P1 double raid, P3 cues, P4 dropped team, P5 ally raid, P6 lunchbox, P9 deposit/bubble, end_probe.
- **Verified by disassembly only (remake checked by reading):** arrival, StartHarvest, EndHarvest, SetHolding, AddScore, bubble task and sounds, GameSound table 0x1002c28, raid start and cleanup, RemoveAnt, hatch chain, CheckNoAnts, HUD score boxes, results builder, CHECKGO.
- **Not verified:**
  - Rendering of the animated stage-1 tiles (fdjelo1 7 frames, fdpmeat1 42 frames); the renderer code paths exist.
  - Whether message 0xa/0x21 delivery is synchronous in local play.
  - The exact results-screen rectangles (label params decoded; docs 5.14 agree).
  - Sound attenuation maths of positional sounds; I only classified global vs positional.

## Top to fix next (ranked by visible effect) and the tests that pin the old behaviour

1. **NEW-2, double end cue.** Keep one path: drop the `application.cpp:950-954` play, or the queued events in `handle_game_over`. Tests to update: `test_app_integration.cpp:761-794` (Suite 6 `get_audio_to_play`), `:5340-5343`, `test_challenger_m2_2.cpp:461`.
2. **NEW-1, global cues.** Push `AudioEvent` with world (0,0) at `sim_engine.cpp:613` and `action_system.cpp:317`, keeping the target. Tests that assert the events exist (no positions, they keep passing; add a world==(0,0) assertion): `test_hill_actions.cpp:262,344`, `test_status_messages.cpp:589`, `test_pointer_model.cpp:897`, `test_app_integration.cpp:5745`.
3. **NEW-6 and NEW-3, allied presentation.**
   - Merge allies into one results row (sum the four columns, " & " names, sort by sum with viewer first on ties), pin the row layout to the decoded x/y, and draw the two-colour box.
   - Tests: `test_app_integration.cpp` Suite 6 (734-840), `test_hud_layout.cpp:181-195`, and the `SCORE_BG` cases at `test_app_integration.cpp:434-445`.
4. **NEW-4, negative scores.** Remove the clamp, keep the signed loot compare, let the HUD draw 0 and the results show the real number. No engine test pins the clamp. The e2e model test `feat48_adv_score_deduction_never_negative` (`tests/e2e/tier1_app_hud.cpp:526`) encodes the claim, but only on the separate e2e model.
5. **Known MISSING: CHECKGO end rules and quit-as-forfeit.** New tests; `test_commands` N1.16-19 and `test_lockstep` N2.18-20 cover drop-out only.
6. **NEW-5, dropped team's hill.** Refuse the raid in `raid_arrive`. Add a test next to `test_hill_actions` 3.3.
7. **Known MISSING: ally break-alliance dialog.** Needs an `OrderType`/HUD dialog hook. The repo has no test for it.
8. **Low:** start-egg default, inert `food_*` counters, `carried_food*25` fallback, bubble step offset.

`tests/test_sim/test_challenger_m2_it2_deep_stress.cpp` was stale (it called `step_thief_animation` and `execute_thief_loot`, which no longer exist, and it was not built) and has been removed.
