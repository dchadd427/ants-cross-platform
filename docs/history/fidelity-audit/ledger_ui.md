# Audit ledger: HUD, minimap, score boxes, cursors

Result of the residual-findings audit of this area against commit 4aa985f (v0.0.50 plus the cleanup pass). It compares the remake with `Original-Ants/Ants.exe` (Capstone disassembly) and `ants.chd`; nothing was run in the original. `<scratch>` and `SCRATCH` stand for a scratch folder of the audit session that is not part of the repository: the data files named below (probes, CSV tables, disassembly dumps) are not kept here. The synthesis and the ranked list of changes are in [`../AUDIT_ONE_TO_ONE.md`](../../AUDIT_ONE_TO_ONE.md).

The ledger is finished. I checked all 50 findings of report U against the frozen copy of HEAD 4aa985f, with no repo writes. The full table, the new deviations, the rebuilt coverage CSV and all probe scripts are in `SCRATCH/audit/LU/`.

## Ledger
Status is FIXED unless stated. "0 px" means 0 differing pixels against the original's composition rebuilt from the CHD data. All panel states were compared with `cmp_hud.py` / `cmp_shell.py`.

| ID | Finding | Status | Evidence | What remains |
|---|---|---|---|---|
| U-01 | clock and score digits | PARTIAL | clock and 4 score boxes 0 px in 3 renders (teams 0 and 2); `FUN_01021e36` and `FUN_01010452` re-read | scorcovr never drawn (NEW-1, NEW-2) |
| U-02 | score bubble | FIXED v0.0.26 | anchor now CONFIRMED: AddScore `FUN_01010cc9` uses the flagged hill cell, same as the remake's (x+1, y+1)*32 | none |
| U-03 | minimap | PARTIAL | terrain, speckle, fog-colour, object and class tables identical to the binary; oracle of the original painter: 351 to 2215 px differ (MEDIUM to TREASURE) without fog, 399 to 3803 with fog (`sheets/minimap_cmp.png`) | see below |
| U-04 | pedestal chains | FIXED v0.0.28 | `pedestal_chain` equals every table row; rise chain 0 px at 6 sample times; 22 panel states 0 px | none |
| U-05 | home panel, egg tray | FIXED | 0 px for eggs 0, 1, 5, 9; initial eggs per map verified (TREASURE 9, SMALL 2, MEDIUM 6, TINY 3, ISLANDS 4, GAUNTLET 6) | none |
| S-01 | options dither | FIXED | 50.6 % of HUD pixels change (checker), card 100 % | none |
| S-02 | quit dialog | FIXED | origin (100,100), no dim, 24 px label | none |
| S-03 | match-start modal | FIXED | one 35 px label, animated portrait at (245,250), 5 s | start-flag closing rule UNVERIFIED |
| S-04 | in-game quick help | FIXED | composite plus return button | any click closes it (NEW-13) |
| S-05 | screen text | PARTIAL | sizes, colours and wrap done (v0.0.48); face Libre Franklin = BY DECISION (D4) | anti-aliased vs GDI draft quality |
| C-01 | ears thresholds | FIXED | >=9 green, <=2 red; `MAX_HP` is 10 for all types | none |
| C-02 | HP bar | FIXED | bar gone; Ctrl+L number glyphs hand-drawn = BY DECISION | none |
| U-06 | Stop button | FIXED | 0 px, 125 ms flash, sound 61 | none |
| U-07 | ally pedestal | FIXED, 1 gap | disassembly: active players > 2 and owner not dropped | dropped players still counted (NEW-8) |
| U-08 | palette rule | FIXED | default is raw CHD palette; the 4 HUD tables equal the exe tables | none |
| U-09 | hover / pressed | FIXED | `r` animations carry the base sprite; sound 0; fire at release | none |
| U-10 | glow | FIXED | left for Move/Food cursor, right for Target | harmless `butdef3a` clip hack remains |
| U-11 | press flash | FIXED | 125 ms on all three slots | none |
| U-12 | lunchbox | FIXED | 0 px | none |
| U-13 | status line | FIXED | original strings only; colour, rect, 5 s life | chat input colour (NEW-9) |
| U-14 | chatcovr | FIXED | 0 px | none |
| U-15 | uishell order / wstatus | FIXED | whole shell 0 px for 4 team colours | none |
| U-16 | dead hit rects | FIXED | symbols gone | none |
| U-17 | time warnings | FIXED v0.0.38 | thresholds 61000, 31000, 11000 ... | none |
| U-18 | alliance strings | FIXED | all 72 strings the remake holds equal the original's | none |
| S-06 | setup controls | FIXED | animation-driven, rects match | label offsets of 2 px |
| S-07 | options controls | FIXED | slider formula, defaults | none |
| S-08 | results rows | OPEN | `scorecard.cpp` layout unchanged (portrait static, name at x 90, centred numbers) | whole layout |
| S-09 | click semantics | PARTIAL | HUD, options Return, dialogs, results Leave fire at release | setup screen fires on press |
| S-10 | startup | OPEN | 1.5 s, no splash sprite 161 | splash plus timings |
| S-11 | loading bar | OPEN | 234x8 ramp | 235x9 at (228,447), original steps |
| S-12 | loading composite | OPEN | dclay tiles skipped, parts forward | both |
| S-13 | menu palette | OPEN (low) | green table; 5 entries differ by up to 16 levels | |
| S-14 | portrait | PARTIAL | animated in modal and setup screen | results screen static |
| S-15 | missing screens | PARTIAL | alliance dialogs built v0.0.50, thumbs drawn; the guest setup screen `nh_start` (with `d_fowyes`) built v0.0.80 | sm_screen, error, web dialog; the host's Drop button `drop1` .. `drop3` |
| C-03 | hover region | FIXED | `in_map_rect` (16,21)-(458,461) | none |
| C-04 | cursor while dragging | FIXED | band >4 px gives Normal | none |
| C-05 | hill brackets | FIXED | anchor +1 tile | none |
| C-07 | scuffle cloud | SUPERSEDED | invented cloud gone; original collision cloud (`FUN_01022ca1`) not rechecked (audits V/E) | |
| C-08 | target vs move cursor | FIXED | armed modes removed | none |
| C-09 | ally hover | FIXED | Attack for allies, confirm dialog | none |
| C-10 | cursor phase | OPEN (low) | free-running phase; the report's "all frames identical" is wrong: c_targ1, c_mov1, c_attack, c_food differ | |
| C-11 | invented cursor states | FIXED | removed | none |
| C-12 | xmarks | FIXED | single object, spawned on every release | none |
| C-13 | edge scroll | FIXED | v0.0.40 | none |
| C-14 | hit boxes | FIXED | v0.0.41 | none |
| C-15 | food fizz | FIXED | template clocks | none |
| C-16 | marquee | FIXED | (255,0,0), 4 px rule | none |
| C-17 | getpow | FIXED | 770 ms clip | none |
| C-18 | view origin | OPEN (low) | still (17,22) 441x439 | |

U-03 remainder:
- **Fog (NEW-3).** The original paints every layer-2 object except food, power-ups, fire wall and bombs in its own colour even on unexplored cells (`0x1009738` to `0x1009766`). The remake hides them all, so enemy hills and obstacles are invisible under fog.
- **Dots (NEW-4).** The remake draws a 3x3 dot per layer-2 cell. The original paints cells at pixel resolution and gives dots only to ants and object-list plants. The plant dots are missing.
- **Ants (NEW-5).** Own or allied ants that fought in the last 5 s blink index 250 every 200 ms (`FUN_0101a88c`); this is missing. The original also draws ants at pixel rather than tile position and exempts allies from the fog test.
- **Frame (NEW-6).** Colour is (251,251,255), not white; size is a fixed 28x21.

## NEW deviations
| ID | Deviation | Visible impact |
|---|---|---|
| NEW-1 | Allied teams: the original fills the score box half own colour, half ally colour, and shows the summed score (`FUN_01021e36` @0x102204e-0x102208f). The remake draws one colour and one score. | every allied game |
| NEW-2 | Absent or dropped teams: the original draws scorcovr over the box and assigns slots by team index. The remake leaves black holes and keeps a dropped player's score. | games with fewer than 4 players, web build |
| NEW-3 to NEW-6 | minimap items above | all games with fog or fights |
| NEW-7 | Score box fill is probably 53x13 in the original (DDraw exclusive rect, pre-cut box is 54x14); the remake fills the whole box. UNVERIFIED. | 1 px edge |
| NEW-8 | ally pedestal counts dropped players | network drop-outs |
| NEW-9 | chat input text (20,50,40) vs original (7,11,15) (`0x100ddc5`) | every chat |
| NEW-10 | world sprites using palette indices 1..31 (ears, hill brackets, explosions) use the raw palette; the original tints them for non-green players | a few pixels |
| NEW-11 | More Help button (bmohelp1 at (529,407)) and dialog `FUN_01016aa2` missing | low |
| NEW-12 | score label text surface is 13 px high with partial-glyph clipping; the remake drops whole characters | 1 px |
| NEW-13 | a click outside the options card, or anywhere on the in-game help, closes it (`hud.cpp:1254`, `1318`) | accidental closes |

## Coverage
**Run against the current code:**
- The HUD in all 22 panel modes: no selection, one ant of each type, several ants, own, enemy and allied hill, chat on and off, for the four team colours.
- Dialogs: options, quit, help, match-start and the alliance dialogs.
- The pedestal rise chain at 6 sample times.
- Clock and score digits, score boxes, bubbles and cursors.
- The minimap oracle on all six maps, with and without fog.
- The whole-HUD shell and score-box pixel comparisons.
- The coverage tour of the texture cache.

**Verified by reading code and disassembly only:** the pointer and cursor rules (C-03/04/08/09/11/12/14, read in `hud_input.cpp`; C-13 edge scroll was not re-read), the string table, the CHECKGO timings, the pedestal rules, and the option defaults.

**Not verified:**
- The startup sequence, results layout and alliance-dialog timing are inherited from audit U.
- The minimap frame offset and the score-box edge need a screenshot of the real game.
- C-07 belongs to audits V and E.

**Coverage numbers:** of 523 UI sprites, 385 are drawn now and 94 are world sprites drawn by the world renderer (other audits). 44 are never drawn: 25 are unused by the original (start markers, FOOD, RESERVED, c_cant, c_scroll, egg1h-3, flowbut, zobjbut, quit1-3, bleavhelp, op_ok*), and 18 are used by the original (scorcovr, ms_logo, drop1-3, error, multi/single, bmohelp1; fowyes and the nh_start set are drawn since v0.0.80). The "unused by the original" verdicts are inherited from audit U. I re-derived op_ok*, because the options builder `FUN_0101487c` creates exactly 5 buttons. I did not re-derive the rest.

## Top 10 to fix next
1. **Minimap fog and objects** (NEW-3/U-03): paint layer-2 objects per pixel in fog too, with food, power-ups, fire wall and bombs showing terrain. Tests: add an objects-in-fog case to `test_hud_layout::test_minimap` (`mini_oracle.py` can generate expected pixels).
2. **Minimap dots** (NEW-4/5/6): per-pixel cells only, plant dots (colour 51, 3x3), ants at px/scale with the ally fog exemption, blink 250 (needs a last-fight timestamp from the sim), frame (251,251,255) 28x21. `test_minimap` pins the 4x3 px hill dot and needs rewriting.
3. **Allied score boxes** (NEW-1): split fill plus summed score. Extend `test_score_digits` and `test_network_app` N5.9.
4. **scorcovr and slot assignment** (NEW-2/U-01): cover absent and dropped teams; slot by team index. Extend `test_score_digits`; the web `label_unnamed_teams` tests change.
5. **Startup** (S-10/11/12): splash, 3 s plus 3 s, loading bar. Existing app-state tests change.
6. **Results screen** (S-08/S-14): layout from the animations plus animated portraits. Integration scorecard tests pin the old positions.
7. **Release semantics** (S-09/NEW-13): setup screen fires at release; in-game help closes only via Return or keys; remove the click-outside close of options. `test_buttons` and the `test_pointer_model` dialog cases need checking.
8. **View origin** (C-18): (16,21), 442x440 for drawing and pointer mapping. Tests touching `PLAYFIELD_X/Y`.
9. **Chat input colour** (NEW-9), dropped-player handling in the ally pedestal (U-07/NEW-8), decide on the More Help button (NEW-11).
10. **Cursor phase restart** (C-10), palette 1..31 for world sprites (NEW-10/S-13), NEW-7 and NEW-12 after a screenshot of the original.

Files are in `SCRATCH/audit/LU/` (SCRATCH is the session scratchpad):
- ledger_U.md
- data/coverage_ui_sprites_now.csv
- data/tour_cache.csv
- luh.cpp and luh (harness and binary)
- build.sh
- mini_oracle.py
- cmp_hud.py and cmp_shell.py
- sheets/minimap_cmp.png
- out/ (renders)
