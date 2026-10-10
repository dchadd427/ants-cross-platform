# Audit ledger: Ant sprites, clips, overlays and timing

Result of the residual-findings audit of this area against commit 4aa985f (v0.0.50 plus the cleanup pass). It compares the remake with `Original-Ants/Ants.exe` (Capstone disassembly) and `ants.chd`; nothing was run in the original. `<scratch>` and `SCRATCH` stand for a scratch folder of the audit session that is not part of the repository: the data files named below (probes, CSV tables, disassembly dumps) are not kept here. The synthesis and the ranked list of changes are in [`../AUDIT_ONE_TO_ONE.md`](../AUDIT_ONE_TO_ONE.md).

## Ledger

The long tables, the original-side facts and the probe list are in `SCRATCH/audit/LA/ledger_full.md`. All probe sources are in the same directory: `p1_render_sweep.cpp`, `p2a`–`p2j`, `p3_overlays.cpp`, `p5_stress.cpp`, `p6_tables_drown.cpp`, `dump_chd.py`.

**How I checked.** The probes link against the frozen HEAD libraries. The reference side is the CHD read by the independent Python reader and dumped to `chd_dump.bin`, drawn by the original's rules. Those rules I re-read in the disassembly:
- Parts are drawn last stored part first (list Add `0x1029a5a`, First `0x1029924`, Next `0x1029987`, DrawAt `0x102b8d7`).
- The blitter (`0x102d144`–`0x102d159`) adds the colour offset to every non-transparent index, with 8-bit wrap. It exempts digit-named images and stores mirrored parts in columns dx'+1 to dx'+w.
- The animation step (`0x102b997`) books the first frame twice when a callback starts a clip.

Verdicts for all 15 findings, with evidence:

| ID | Finding | Status | Evidence | Remains |
|---|---|---|---|---|
| F1 | Enter own hill | FIXED (v0.0.32) | P2f, 36 runs (6 types × food × hp 10/5/1). Clip is `a?h0` / `h?h0`, at the entrance tile centre. The evt-5 frame lasts (10−hp)×200 ms. Hatch is only for newborns; the spawn anchor is now confirmed at `0x100ef18`. | none |
| F2 | Carrying ant that attacks is invisible | FIXED | P2a, 96 attacker scenarios (type × 8 directions × food): the clip is always `a?at`. | none |
| F3 | Melee timing, sounds, victim clip | FIXED (v0.0.33) | P2a–c. The hit lands at the evt-4 frame (ticks W/B 4, F 5, T 7, C 4, S 11). Sounds come from the clip frames only, so no stray 57 for W/T/S. The victim clip is `a?gh` for range 1 and `a?gb` for range 4 (96 combinations). The range→action mapping is now confirmed at `0x101df0d` / `0x101defb`; A had it INFERRED. | none |
| F4 | Flinch / blown-back flights | FIXED | P2d: gh is 24+8 (worker) or 16+10+6 (combat). gb is 128 px in one step at frame end. No parabola or shadow. A flight ends idle, not stunned. | none |
| F5 | Lethal hit order | FIXED | P2e: a 1-hp victim flies the full 800 ms, then plays the death clip with its effect at the ant position. | none |
| F6 | Thief infiltration `atcr501` | FIXED (v0.0.32) | P2g: 33 frames on the raid tile centre (1392,1360); sounds 84/85/86 at 2050/2610/3370 ms. The marker is hidden during the raid (P3). | none |
| F7 | Part draw order | FIXED (v0.0.25) | P1: 531 clips, 9,756 clip-frames (mirror variants included), 39,024 renders over 4 teams, 0 differing pixels. | none |
| F8 | Mirrored parts one pixel left | FIXED (v0.0.31) | P1 mirror variants, 0 px. | none |
| F9 | Per-type durations | FIXED | P6a: all 520 generated clips (6,123 frames) equal the CHD. Runs: stun 3125/3835/3000/2610/3000/2880, burn 1150/1610/1330/1150/1165/1050, drown 2370, harvest 420/440/400/340/360/320, getpow 770, bridge 480/500. | none |
| F10 | World effect timing | FIXED (v0.0.34) | P2i: the bomb and fire wall appear at the END of `absb` / `afsf`; the bomb is removed at the END of `abdb`; the fire goes out at the END of `afxf`. | none |
| F11 | Held bomb recoloured | FIXED | P1: `2bomb` is identical for all four teams. | none |
| F12 | Stray colour indices | FIXED | P1, 0 px. Also 0 ant pixels ever land in palette 1..31, so the HUD palette rewrite (`0x100ea70`) cannot affect ants. | none |
| F13 | Overlays | FIXED | P3, 36 checks: ears by hp (9/2 thresholds), own clock with restart and loop, above lower ants, raw palette. `max_hp` is 10. Hill brackets sit at the footprint origin + 64. Health bar and shadow are gone. Score bubbles by code reading only. | see NEW-3, NEW-6 |
| F14 | Burn overlay | PARTIAL | Position, colour and timing match (P2h). The layer differs: the remake draws it inside the y-sorted ant pass. The original adds it as a View child (`0x101af66`, Insert at head `0x10299ed`, Draw `0x102f6bd`), so it is drawn over everything. | move it to the overlay queue |
| F15 | Bounce state | FIXED | The state is removed; P5 never produces it. | none |

**Section 3 (per-type durations).** Every row is FIXED; the evidence is the F9 row above and P6a. The remake replays the original's first-frame double booking, so ability and attack clips end one first-frame late, as in the original.

**Section 5 (open questions of A):**
- Hatch anchor: CONFIRMED (above).
- Melee handler path `0x1022ca1`: CONFIRMED. The normal path calls StartMelee `0x1010245`.
- Flinch or punch ending in stun: CONFIRMED no. The cleanup's stun clip is replaced inside the same SetAction.
- Collision battle object: the looping pile-up cloud exists. The two-ant variant sits in the position-mismatch branch of `0x1022ca1` (network latency only), so it is not reproducible here and not needed.
- Stun-end invulnerability: NOT A DEVIATION in practice. The flag at `+0x78` is read by CanBeAttackedFrom at `0x101cb48`. The Invuln task is added with delay 0 and period 0 (`FUN_0103057b(0,0)` at `0x1021603`), so it lasts about two scheduler slots, well under one 50 ms tick. The remake has none. This rests on my reading of the scheduler Add semantics.
- Death clips death1–4: FIXED.
- HP digits: PARTIAL (see NEW-3, NEW-6).

**overlays_re.md:**
- A1–A9 (selection marker, hill marker): FIXED, apart from the small anchor lag in NEW-3.
- B1: PARTIAL. The toggle needs Ctrl+L, and the glyph shapes are approximated.
- B2, C, D1–D3: FIXED.
- E1, E2, E5–E7: FIXED.
- E3 (burn overlay): PARTIAL, see F14.
- E4: the pile-up cloud is FIXED; the two-ant variant is not applicable (see above).
- E8: not re-checked.

**Data files of A.** `zorder_*.csv` (old order) and `team_diff_ant_sprites.csv` (284 stray-index rows) are now all zero, because P1 covers every ant sprite of every clip for 4 teams. `timing_original.csv` equals what the sim plays (P6a). `timing_remake.csv` is obsolete.

## NEW deviations

| ID | What | Who sees it, how often | Fix |
|---|---|---|---|
| NEW-1 | Walking, idle, swim, dive, climb and harvest frames are sampled at the 50 ms tick. The original redraws every scheduler pass (REFRESH task, period 0, at `0x102c4d1`) and steps at the real time a frame ends. `predict_ant_clip` only covers action clips (renderer.cpp 1274–1281). | Every walking ant, constantly. Sand (40 ms frames) shows 1,1,1,2 steps per tick. Mud and dirt (60 ms) skip a step every 6th tick. Grass is identical. | Predict walking clips up to the next event frame, or run the animation clock in finer sub-steps. |
| NEW-2 | Equal-y draw order. The original's incremental sort (`0x10089bd`) keeps insertion history. The remake uses one stable sort over plants, ants, effects, droppers. | Idle ant in the same row as a clover or flower. Low. | Keep a persistent sprite array and replay the incremental sort. |
| NEW-3 | The marker and HP digit use the tick position, not the predicted one, so the ears lag up to 50 ms at the 128 px gb jump. The digits are also drawn for frozen ants and skipped for dying ones. | Debug feature only. Low. | Use the predicted dx/dy; skip frozen ants. |
| NEW-4 | For non-ant world sprites, palette indices 2..31 are drawn raw from the CHD. The original substitutes the local player's HUD table once, globally. The raw palette equals the green table except indices 9, 11, 12, 18, 22. | Rocks, toys and food pixels (0.07–1.75 %) for a non-green viewer. Ants are unaffected (verified). Low. | Apply the viewer's table to raw-palette draws. |
| NEW-5 | Hygiene: the legacy prefix+action fallback in `draw_single_ant` (renderer.cpp ~1420–1536) is unreachable (P5). `AntUnit::tick_timers` is a leftover. `issue_order(InfiltrateAnthill)` lets any type raid (not used by the game). | none | Delete. |
| NEW-6 | The `L` digit toggle needs Ctrl. The original tests no modifier; with chat on (the default) the chat edit swallows plain letters, so only chat-off differs. | Only with chat off. Low. | Accept plain `L` when chat is off. |

## Coverage

Verified by running code:
- **Renderer:** every frame of every ant clip, 4 teams, mirrored, pixel-exact (P1).
- **Sim clips:** all 520 generated clips against the CHD (P6a).
- **Scenarios:** attack, flights, death, enter, hatch, raid, getpow, bombs and duds, abilities, harvest, bridges, drowning, swimmer clips.
- **Overlays:** marker behaviour (P3).
- **Stress:** 2,246,755 random-play snapshots with a clip-consistency oracle. The only flag was my own misuse of the unused `InfiltrateAnthill` API, not a game path.

Verified only by reading or disassembly: score bubbles, the map-bomb draw, hill brackets, effect spawns, the end-of-clip handlers, and the claims listed under "Original facts" in `ledger_full.md`.

Not verifiable here: the original's real display cadence (I infer it from the period-0 REFRESH task), Fixedsys glyph pixels, and anything needing a runtime oracle such as screenshots of the real game.

## Top 10 to fix next

Only six items remain; none of the rest is open.
1. **NEW-1, walking cadence.** Extend `predict_ant_clip` to walking clips. No existing test pins the quantization; add cases to `test_subtick_prediction`.
2. **F14, burn overlay layer.** Draw it from the overlay queue, with creation time = now − `burn_elapsed_ms`. `test_frozen_ant` stays valid; add a lower-ant case.
3. **NEW-4, world palette.** Apply the viewer's HUD table to world draws. The tests `test_ant_colour_rule` (raw CHD palette for `TEAM_NONE`) and `test_map_layers` encode the old rule and must be rewritten.
4. **NEW-3, marker and digit position.** Use the predicted position. Touches `test_selection_markers`.
5. **NEW-2, tie order.** Check what `test_dynamic_items` pins before changing it.
6. **NEW-6 and NEW-5.** Accept plain `L` when chat is off, and remove the dead fallback.
