# Audit ledger: Terrain, fog of war, passability

Result of the residual-findings audit of this area against commit 4aa985f (v0.0.50 plus the cleanup pass). It compares the remake with `Original-Ants/Ants.exe` (Capstone disassembly) and `ants.chd`; nothing was run in the original. `<scratch>` and `SCRATCH` stand for a scratch folder of the audit session that is not part of the repository: the data files named below (probes, CSV tables, disassembly dumps) are not kept here. The synthesis and the ranked list of changes are in [`../AUDIT_ONE_TO_ONE.md`](../AUDIT_ONE_TO_ONE.md).

Terrain is now identical to the original, and fog rendering is identical with one rare exception. The remaining differences are in how fog gets revealed and in the minimap. The full tables, logs and probes are in `SCRATCH/audit/LT/` (main file `ledger_LT.md`; data in `template_durations.csv`, `radar_model_vs_remake.csv`, `run_nofog_all.log`, `run_fog_a.log`; probes are the `lt_*.cpp` / `lt_*.py` files there).

Method: I wrote an independent Python reference of the original's rules and compared it pixel-exact with the real `Renderer::render_world`. The rules are: positional tile ids, one template per id with t0 at map load, layer 1 / layer 2 / y-sorted list sprites / fog overlay, and the original's table values.

**The question (terrain animation, tick vs real time): 0 % differs.**
- Terrain and all layer-2 and list templates are sampled at real time once per rendered frame. `begin_frame` (`src/ants_app/renderer.cpp:639-646`) uses `SDL_GetTicks() - map_epoch_ms_`.
- A probe with irregular frame pacing gave clock steps of 8 to 30 ms, not 0 or 50.
- All 372 terrain frame durations are multiples of 50 ms (40 x 50, 42 x 100, 68 x 150, idle 1000 to 4300), so even a tick-aligned clock would show the same frames.
- Template animations with other durations are `wallup04`, `fdcola1-3` (120 ms) and `fdpmeat1` (80 to 500 ms). They use the same real-time clock. No animation has a zero-duration frame.
- For scale only: a tick-sampled clock with arbitrary phase would show each new frame about 25 ms late, about 17 % of the time for water and about 10 % for mud pops. That is not the case here.
- This is consistent with LX: the original samples once per display pass at real time.

## Ledger
| ID | finding | status | evidence | what remains |
|---|---|---|---|---|
| T1 | m01e family (723 cells) never animated | FIXED | Positional id maps to the Table-4 template (`renderer.cpp:476-496`, draw at `:805-812`). 6 maps x 220 clock values (every 50 ms to 4950, plus 120 random up to 70 s) = 1,320 full-map renders, 0 differing pixels. The reference shifted by 50 / 150 / 600 ms differs by 2,275 / 133,778 / 13,052 px, so the test is sensitive. Pinned by `test_render_parity.cpp` (`test_template_clock`, `test_map_layers`). | none |
| T2 | mud timing invented | FIXED | `template_frame_index` (`renderer.cpp:727`) gives one frame per id per render (t mod cycle). I re-read FUN_0102c1fc / FUN_0102b997 / FUN_0102b95f: reset stamps timeGetTime, frame 0 starts at t0, the catch-up loop is exact unless the `nocatchup` switch is given. | none |
| T3 | MW02a..09a on the mud model | FIXED | Own 4-frame templates; class 3 is used by movement only. | none |
| T4 | d-family frame sets | FIXED | Frames come from Table 4 (same parity). | none |
| T5 | epoch / start phase | FIXED | `map_epoch_ms_` is set in `set_level` (`renderer.cpp:635`); frame 0 is at load. | Absolute phase at the first visible frame depends on load time in both programs. |
| T6 | name resolution, solid blocks for "." | FIXED | Identity mapping (0 positional mismatches on all 6 maps); "." or an unknown id draws nothing (`:809-810`). | A dictionary entry whose name differs from animation i falls back to a by-name lookup (foreign maps only). |
| T7 | layer 1 ignores parts / offsets | FIXED | `draw_frame_parts` (`:708-725`), last part first, native size. | No visible effect (terrain is single-part). |
| T8 | 121 unused ids | NOT A DEVIATION | 106 are in none of the three loader lists (0x1001360, 0x1001640, 0x1001818; union of 377 ids), so they are never loaded. All 118 ids the six maps use are listed. | The remake draws any id; no shipped-map effect. |
| T-V | static terrain, water, sprite palette, dither LUT, fog not over layer 1 | STILL IDENTICAL | Parity at all times. Dither table re-decoded from the map constructor (0x1006189): 16 entries map to anim 162-177 and sprites 352-367, single frame and part. Also identical for local team 0-3. | none |
| T-V7 | "0 seams at 13 window sizes" | CONTRADICTED (PARTIAL) | Software renderer, fractional scales (1.6, 1.273, 2.327, 1.042): 1-px gaps every 5 tiles at 1.6. None at 1.0, 1.25, 1.5625, 2, 2.25, 3. | The GPU path cannot be tested headless: UNVERIFIED. See NEW-5. |
| T-O1 | layer 1 static at run time (SetTile 0x10238d7) | CONFIRMED | FUN_010238b1 is the SetTile command handler; all 6 constructors of that message (FUN_01023885) push layer 2. | none |
| T-O2 | minimap colours not compared | DONE | Class noise bytes (0x1001c28), fog bytes (0x1001c48) and the 215-entry object table (0x1001c50) equal the remake (0 mismatches), but the rules differ (NEW-2). | |
| T-O3 | malformed maps | FIXED | Draws nothing. | |
| T-O4 | dirty-grid artifacts | UNVERIFIED | Needs screenshots of the real game. | |
| F1 | reveal radius, shape, trigger | IDENTICAL | FUN_01006af4 is called only from ant SetPos (FUN_0101a93a @0x101a9f8): 13x13 square (imm 6 at 0x101a9e4) around the pixel tile, clamped with signed words, sticky. Sim probe over 600 ticks on 6 maps: cells the model has that the remake lacks = 0. | Updates on the 50 ms tick, not continuously. |
| F2 | per-team rule | IDENTICAL | Reveal only for the local player's ants and the local player's teammate (+0x68). | |
| F3 | what fog hides | IDENTICAL | Power-ups, food, bombs and fire are hidden at an unexplored anchor; hills, bridges and rocks are not. Parity with 8 synthetic patterns plus 6 real sim states. Ant visibility under fog equals 0x101aa0d (by reading). | |
| P1 | terrain classes and flags | IDENTICAL | Independent extraction: 133 pairs to 1344 classes, 6 flag lists to 1344 flag bytes: 0 mismatches. | |
| P2 | passable / weights / walk tables | IDENTICAL | Passable {1,1,0,1,1,0,0,0} (0x10049b8), weights {20,16,8000,48,24,40}, swimmer-on-water 21 (0x1020902). Walk, carry-walk (150 entries each) and idle tables, dir and neighbour tables: 0 mismatches. Rule R1 (FUN_0101f780 @0x101f7b7) equals `movement_system.cpp:1041`. Bridge (0x22-0x25) to mud equals `Grid::terrain_class_at`. | |
| P3 | speed classes | IDENTICAL | From exe clips: grass 80, sand 100, dirt 67, mud 33 px/s for every ant type; water only for swimmers. Absent teams lose their hill (FUN_0100ecdf) and `for_roster` does the same. | |

## NEW deviations
| ID | what (original evidence, remake file:line) | who sees it, how often |
|---|---|---|
| NEW-1 | The remake reveals a 16x16 box around every own and allied hill each tick (`src/ants_sim/sim_engine_impl.hpp:101-107`). The original has no hill reveal. | Every fog player at match start. Extra explored cells: GAUNTLET +26 (+9 %), ISLANDS +27 (+5 %), MEDIUM +80 (+36 %), SMALL +63 (+43 %), TINY +53 (+27 %), TREASURE +95 (+35 %). All are inside the box. |
| NEW-1b | The per-tick scan of allied ants and hills reveals at once when an alliance forms (+210 to +531 cells in one tick, no ant moved). The original reveals only when an allied ant's SetPos runs. | Every teaming in a fog game. |
| NEW-2a | Minimap, fogged cell: the remake paints the fog colour for every object (`hud.cpp:646-648`). Original (asm 0x1009704-0x1009766): bombs 0x81-0x84 are always terrain; in fog only power-ups, food (flag 2) and fire 0x86 fall back to terrain. Rocks, toys, hills and grass keep their object colour. | Every fog player, constantly: the obstacle layout of unexplored ground is visible on the original. |
| NEW-2b | The remake hides fire walls always (`hud.cpp:650-653`); the original shows colour 250 wherever explored. | Fire-ant games. |
| NEW-2c | The remake adds a dot (about 4 px) per layer-2 object cell (`hud.cpp:660-681`). The original paints cell colours only and draws dots for display-list sprites only (flowers and clover: colour 51, flag 2). The remake has no flower dots. | Every player. |
| NEW-2d | Ant dots: original at the sprite pixel position, size max(trunc(px per cell), 2), so TINY 3x2 and SMALL 2x2. Own and allied ants flash 250 / team colour (200 ms toggle) for 5 s after being attacked (FUN_0101a88c; stamp from FUN_01020c70 @0x1020cd1). The remake snaps to the tile centre, uses lround (TINY 4x3, SMALL 3x2), steady colour, no flash (`hud.cpp:661-686`). | Every player; the under-attack flash is missing. |
| NEW-2e | Terrain speckle: the original calls rand() % 5 at every repaint; the remake draws one fixed speckle per map. | Cosmetic. |
| NEW-3 | Layer-2 scan margin is 5 cells (`renderer.cpp:876`); the original uses 3 (FUN_01008089). | At the view edge the remake shows slivers early. Measured: `grassbig2` up to 32 px wide (1,603 px at camera x 352), `glasses` / `stick2a` 15 px (181 px). |
| NEW-4 | Partially explored objects: the original redraws an object from every explored body cell whose anchor is unexplored, at that cell's turn in the row-major pass. The remake draws only at the anchor, so the z-order differs where two objects overlap at the explored edge. | 21 to 357 px per render; 1 of 36 real sim fog states (`out/crop_g1.png`). |
| NEW-5 | 1-px tile seams at fractional window scales with the SDL software renderer. | Fullscreen or odd window sizes on the software path only. |
| NEW-6 | Food fog footprint is radius 4 with the same tile, fixed at load (`renderer.cpp:596-611`); the original uses the anchor bytes of the current stage. | Unverified, tiny. |

The radar comparison (12 real states) puts the rule differences at 3.5 to 31.9 % of the 10,829 radar pixels, camera frame excluded. The worst cases are TREASURE 20.6 % / 31.9 % (fog off / on) and ISLANDS with fog 14.5 %. A model of the remake's own rules explains every pixel except the camera frame, which validates the comparison.

## Coverage
- **Run against the real renderer or sim:**
  - 1,320 no-fog full-map renders and 396 fog renders (288 synthetic, 108 from real sim states), against the reference.
  - Fog reveal over 600 ticks on 6 maps; the alliance burst.
  - The real HUD radar in 12 states.
  - 27 camera positions per margin test; the clock probe; the seam probe (software renderer only).
  - Independent exe extraction of every table listed above.
- **Code or disassembly reading only:** T5/T6/T7 code paths, ant visibility under fog, rule R1, the SetTile layer argument, hill removal for absent teams.
- **Not verifiable without the real game:**
  - Dirty-rect artifacts and `timeGetTime` granularity.
  - GPU-renderer seams.
  - The radar camera-frame colour 0xFFFBFB (the remake draws white).
  - The exact order of overlapping radar dots.
  - The food footprint after stage changes.

## Top 10 to fix next
Only six real items remain; I found no further ones.
1. **NEW-2 minimap rules.**
   - Change: in fog, only power-ups, food and fire fall back to terrain; bombs are always terrain; fire is visible when explored; drop the layer-2 dots; add flower and clover dots; draw ants at pixel position with trunc size, min 2; flash own and allied ants for 5 s after an attack.
   - Tests to update: `tests/test_app/test_hud_layout.cpp` (minimap checks around :606), `TEST_INFRA.md` row 38 (`test_minimap_*`).
2. **NEW-1 / 1b reveal.**
   - Change: reveal only from ant position updates (`set_position`, creation) for the viewer and teammate; remove the hill box and the per-tick scan.
   - Tests to update: `test_app_integration.cpp:5808-5838` and `:6522-6539`, `test_pointer_model.cpp:198-275`.
3. **NEW-3 margin.** Change: `kMargin` = 3 with the original's window formula. Re-check `test_map_layers` stitching.
4. **NEW-4 z-order.** Change: walk all layer-2 cells and draw from explored body cells. Add `lt_ref.py` fog patterns as a golden test in `test_render_parity.cpp`.
5. **NEW-5 seams.** Change: render the 640x480 canvas to an offscreen texture and scale once.
6. **NEW-6 food footprint.** Change: use the live anchor footprint.
