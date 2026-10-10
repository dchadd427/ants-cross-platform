# Audit ledger: Combat and combat feel

Result of the residual-findings audit of this area against commit 4aa985f (v0.0.50 plus the cleanup pass). It compares the remake with `Original-Ants/Ants.exe` (Capstone disassembly) and `ants.chd`; nothing was run in the original. `<scratch>` and `SCRATCH` stand for a scratch folder of the audit session that is not part of the repository: the data files named below (probes, CSV tables, disassembly dumps) are not kept here. The synthesis and the ranked list of changes are in [`../AUDIT_ONE_TO_ONE.md`](../../AUDIT_ONE_TO_ONE.md).

## Ledger

Audits K (combat) and V (combat feel) checked against HEAD 4aa985f. The simulation core of both areas is now faithful. The open items are in the layers around it: minimap, audio, death drawing, tests.

The full per-ID table (every K and V finding, with VAs and file:line) is in `SCRATCH/audit/LK/ledger_full.md`. Probes are in `SCRATCH/audit/LK/probe/`, outputs in `.../out/`.

| ID | finding | status | evidence | remains |
|---|---|---|---|---|
| K-H1, K1, K12 | HP lost at contact; victim idle, engaged and unorderable until the strike frame | FIXED (v0.0.33) | `combat_system.cpp:220-237`, `:152-163`; `movement_system.cpp:370`. Probe `t_melee`: hp 9 at t=0, Gh at the strike frame. Probe `stunorder`: order refused during the flight. Test 1.1 | none |
| K-H2, K7 | One blow per order, no cooldown, combat ant resumes its order, auto-engage only more than 2 s after the last order | FIXED | `can_auto_engage` equals 0x101c0d5 (re-read, every condition). Probes `spam`, `chase`, `auto`. Tests 5.2, 6.1-6.3 | none |
| K-H3, K8 | Reach is the tile crossing; water on either tile refused with text 0x3a | FIXED | `try_enter_tile` A plus `melee_contact` equal 0x101c53f-0x101c671 (re-read). Test 1.10 | none |
| K-H4, K4 | Range 1 or 4, KnockDir deflection dir/+1/-1/+2/-2, cornered = no flight | FIXED | `combat_system.cpp:55-78` equals 0x101d8ed. Probe `bombtypes`: 8 directions, 4 tiles. Tests 1.8, 1.9, 1.11 | none |
| K-H5, K3, K10, V-K2 | No stun after a gh/gb flight, no sound 70 | FIXED | `action_system.cpp:118-135`. Probes: no sound 70 in the pile, fire and water runs. Test 6.5 | see V-K4 |
| K-H6, K2 | Death deferred until the end of the flight, then death1-4, removal | FIXED | Probe `t_death`: hp 0 at contact, flight 800 ms, clip, removal at 2500 ms, text and counters at removal. Tests 1.4, 12.117 | drawing: NEW-3 |
| K-H7, K5, K15, K18 | Landing blocks bomb, pile-up, fire wall, water; drown; any bridge stage is not water | FIXED | `walk_step` equals 0x101baef-0x101bd4f (re-read). Probes: pile, fire, fireant, water, bombpile, chain, firepile (pile-up before fire: hp 9/10), bridge stages 1-4 (class 3, no drown). Tests 3.1-3.4 | G7 and G13 untested (see K-G) |
| K-H8, K6 | No collision fight, no scuffle, no hidden ants; dust cloud only for pile-ups of non-local ants | FIXED | No scuffle code left. Test 5.1. Fuzz: 12 seeds x 4000 ticks, 0 violations. Probe `pile_foreign`: cloud and sound 3 | wrong for fire walls: NEW-1 |
| K-H9, K17 | "Ouch!" and sound 58, alarm at most once per 10 s, local or ally only | FIXED | `on_attacked` equals 0x1010a03 (re-read). Test 2.1 | none |
| K-H10, K16, K20 | Power-up drop on a free neighbour tile, food drop, auto-hatch, kill credit at removal | FIXED | Probes `drop_stats`, `bombkill`, `ownbomb` (own bomb: lost 1, killed 0). Tests 1.5, 1.6; LF B10 and A11 | drop-out: NEW-4 |
| K9 | Refusal list, abilities cancelled, ally dialog; "power-up target not protected" | FIXED; last part NOT A DEVIATION | List equals 0x101cb0c. Ally question in v0.0.50. A power-up tile is solid for paths and AttackTile, so standing on it is immune in the original too. Test 12.110 | `+0x78` window about 2 scheduler slots (LA), not modelled, unobservable |
| K11, K13, K14, K19, K-2.x, K-3, K-4 | Clips, hp-1 retreat, bomb victim (2 damage, 20% dud), combat hp 10, timelines, sound tables | FIXED | `timing_types` is identical to V's table (12 rows). Probes `dud` (frozen 1150 ms, then stun), `retreat`, `bombtypes` (81 duds of 400). `max_hp` 10 everywhere. LA P2b/P6a | none |
| K-2.8 | Auto-engage (rings, AttackTile, COMBEVT 2000/3000 ms, resume) | FIXED | Probe `auto`: engages at distance 2 and 3, not 4. Probe `chase`: gives up at +3000 ms and walks back | none |
| K-O1..O6 | K's open list | resolved, except contact latency (UNVERIFIED) | Counters re-read: +0x58 lost, +0x5c killed, +0x60 hatched, +0x50 locked. Frame 0 booked twice (found by V) | runtime oracle for the latency |
| K-T | Tests that encoded old behaviour | FIXED for unit and integration tests; OPEN for E2E | Sim, challenger and integration tests rewritten. `tests/e2e/e2e_model.hpp:680-739` is a private model with the old rules | NEW-5 |
| K-G1..G13 | Golden tests to add | covered except G5 (only worker, bomber, thief stun lengths), G7 (only bomb-first), G13 (bridge landing) | test list in the full ledger | add those three |
| V-A1, A2, A4, S11-S13, S15 | Group attack order, armed attack mode, "Attack!" text and voice, ally question | FIXED | `issue_group_attack_order` (`sim_engine.cpp:1124`), `hud_input.cpp:266-279`. Probe `spam`: contact at 2450 ms for any re-click period. Tests 12.142, 12.143 | none |
| V-A3, S14 | Pick rectangles | FIXED (v0.0.41) | `hud_input.cpp:72-90` equals 0x1026a39 | NEW-6 |
| V-K1 | 50 ms display quantisation of flights | PARTIAL | Probe `seqcmp2` (V's harness, repaired), victim only, vs the real-time model: bomb 0-1.4% of frames differ (V: 29%), worker punch 0.7-10.6% (V: 22%), combat punch 2.1-8.5% (V: 31%) | launch up to 50 ms late; idle and walk frames not predicted (LA NEW-1) |
| V-K3 | Cull by anchor | FIXED | `renderer.cpp:1304` margin 160 | none |
| V-B1, B-6 | Dust cloud and sound 3 for foreign pile-ups | FIXED for pile-ups; WRONG for fire walls | see V-B1 in the full ledger | NEW-1 |
| V-B2, V-C1 | Frozen ants not drawn | FIXED | `renderer.cpp:1415`; 0x10088e7 and 0x101aa5e re-read | overlay layer (LA F14) |
| V-B3 | Bump cue for the local player's ants only | FIXED | `movement_system.cpp:1028`; 0x101c98b-0x101cae3 re-read; golden 3.1b | none |
| V-K4 | Sounds of a replaced clip are stopped (track flag) | OPEN | 0x102c0db, 0x102bdab and 0x102eb9a re-read. Every combat clip has the track flag, so bomber and fire attack.wav and the 64/65 thumps are cut in the original. The mixer has no owner or stop concept | owner plus stop events |
| V-X1 | Combat hp 12 | FIXED | see K19 | none |
| V-X2 | Allied ants hidden in fog | PARTIAL | main view fixed (`renderer.cpp:1621`) | minimap: NEW-2 |
| V-X3 | Equal-y and step order | OPEN | comment at `movement_system.cpp:1599` | low (LA NEW-2) |
| V B-1..B-3, B-5, B-8..B-15, S1-S10 | Bounce, pause, re-path, pile-up, fire, bomb, water, chase | FIXED (identical) | probes and fuzz; `test_movement_golden` 3.1-3.5 | B-4 BY DESIGN; B-7 n.a.; B-16 (hill queue) not re-verified here |
| V-C2..C4, V-U | K-report corrections, audibility of Play+Stop, scheduler latency | confirmed / UNVERIFIED | | needs a runtime oracle |

## NEW deviations

| ID | what | original | remake | who sees it | fix |
|---|---|---|---|---|---|
| NEW-1 | Dust ball and sound 3 also appear for a foreign ant thrown onto a fire wall | The cloud is made only by Blast's non-local branch, reached through block C (pile-up, no IsLocal gate). Block D (fire wall) is IsLocal-gated (0x101bbdd-0x101bbf4), so a foreign ant there only hops and loses 1 hp | `combat_system.cpp:342` spawns the cloud for every caller. Probe `fire`: SND 3 and cloud at the landing | anyone watching another team's fire fight, about 1 s per event | add a `cloud` argument to `blast()`, true from block C only |
| NEW-2 | Minimap: no attack blink, dying ants missing, allies hidden in fog | Own and allied ants hit within 5 s alternate team colour and palette 250 (RGB 255,255,97) every more than 200 ms (0x101a88c; `+0x9c` set by StartEngaged). Every ant has a dot until RemoveAnt. Allied ants pass 0x101aa0d in fog | `hud.cpp:684-685` draws a flat colour, skips hp 0 and drowning ants, hides allies in fog. No last-attacked time in `AntUnit` | every player in every fight; the "my ants are under attack" cue is missing | store last-attacked time, add the blink rule, draw all dots, show allies. Repaint cadence of the original unverified |
| NEW-3 | Death clips are still a separate effect, about 80-100 ms ahead | The ant's own sprite plays death1-4. Frame 0 is booked twice, so death1 lasts 1020 ms. The ant is drawn with its HP digit and minimap dot until removal | `movement_system.cpp:599` calls `spawn_effect(death1-4)`; `renderer.cpp:1627` skips Dead ants. Probe `t_death`: clip starts at 1480 ms, effect visible 1500-2420, nothing drawn 2420-2500, removal at 2500. Test 12.117 pins the effect. The changelog sentence "no longer started by a separate effect" is wrong: only the unused helper was deleted | every death | draw Dead ants through their loco clip and delete the effect; rewrite test 12.117 |
| NEW-4 | A dropped team's typed ants leave power-ups | Kill drops only when `IsLocal(ant)` (0x1020ff6); survivors' machines drop nothing for a dropped team | `finish_death` drops for every typed ant (`combat_system.cpp:494-499`) | after a drop-out | skip when `dropped_mask_` has the team |
| NEW-5 | The 506 E2E tests do not test the game | AGENTS.md names them as the 100% gate | `tests/e2e` links no engine; `e2e_model.hpp:680-739` has the old rules (4-tile knockback, 12-tick stun, sound 57 for all standard attackers, ricochets) | nobody in play; false assurance | drive `SimulationEngine` or re-derive |
| NEW-6 | Pick scans every ant on a tile | One registered occupant per scanned tile (0x1026904, Query mask 1) | `hud_input.cpp:72-90` | only with several ants on one tile | use the tile's default occupant |
| NEW-7 | Stale guard-post wording | The original has only auto-engage | AGENTS.md rule 8 still names a "guard post patrol". Comments at `sim_engine.cpp:14,41`, `movement_system.cpp:748`, `ant_unit.hpp:22,39`; `tests/TEST_INFRA.md` and `TEST_READY.md` still list the old melee model | nobody | correct the texts |
| NEW-8 | `blast()` walks ants in creation order | team slot order (0x100f2cd) | `combat_system.cpp:344` | nobody (same statistics) | none |

## Coverage

**Run against the current libraries.**
- Contact and strike timelines for all 6 attacker types x 2 directions (`timing_types` is identical to V's table).
- Death sequence, pile-up dispersal, fire wall, fire ant on a wall, water drown, bridge stages 1-4.
- Bomb ratio (20.2%), landing directions, chain detonation, dud burn, bomb kill credit, own-bomb friendly fire.
- Click spam, head-on, chase with the 3000 ms timeout, combat ant auto-engage at distances 2 to 4, hp-1 retreat.
- Seed fuzz (12 seeds x 4000 ticks) and V's tick-versus-real-time display comparison.

**Re-read in the original disassembly.**
- TakeHit, RemoveAnt, OnAttacked, CanAutoEngage, TryEnterTile branch A, msg-8 handler.
- WalkStep blocks B-E, Blast, Kill and FinishDeath, REPATH, display loop, frame-sound and SetAnimation track flag, pick routine, 0x101a88c and the minimap painter.

**Read only.**
- B-16 hill queue (not re-verified here).
- Audio mixer internals.
- V's 1800-map A* differential was not re-run.

**Not verifiable without an oracle.**
- Contact latency (path manager plus idle dwell).
- Minimap repaint cadence.
- Audibility of a sound that starts and stops in one call chain.
- Whether tracked-sound cuts are audible.

I did not run the test suites (read-only rule). Claims about tests come from reading them.

## Top 10 to fix next

1. **NEW-2 minimap.** Add `last_attacked_ms` (+0x9c) to `AntUnit` and the snapshot. In the radar, blink own and allied ants for 5 s (alternate palette 250 and team colour, toggle every more than 200 ms). Draw dots for hp 0 and drowning ants. Show allied ants in fog. The state hash changes. Existing radar tests do not pin the old dots.
2. **NEW-1 fire-wall cloud.** Add a `cloud` argument to `blast()`, true only from the pile-up block. Tests: `test_combat_actions` 3.4 still passes; add "foreign ant on a fire wall: no battle effect".
3. **V-K4 tracked sounds.** Give `AudioEvent` an owner. `loco_play` appends a Stop for the old clip when it has the track flag. The mixer stops by owner. This replaces the trimming in `action_cleanup`. Test 6.5 still holds; add a bomber attack-sound cut test.
4. **V-K1 residual / LA NEW-1.** Predict walk and idle clips, and process clip starts at event time. Extend the `seqcmp2` cases.
5. **NEW-3 death drawing.** Draw Dead ants from their loco clip and remove the death effect. Rewrite test 12.117 (it pins `world.effects`) and the render-parity tests.
6. **NEW-5 E2E model.** Drive `SimulationEngine` or re-derive the model. Keep the 506 count; tiers 1-4 combat cases change.
7. **NEW-4 drop-out power-ups.** Skip the drop for dropped teams. Network and `drop_player` tests.
8. **V-X3 / LA NEW-2 tie order.** A persistent sprite array with the original incremental sort.
9. **NEW-6 pick by registered occupant.** Low priority.
10. **Golden tests for K-G5, G7, G13** (stun lengths for fire, combat and swimmer; pile-up before fire and water; bridge landing), plus the NEW-7 text corrections.
